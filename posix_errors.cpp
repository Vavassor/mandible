#include "posix_errors.h"

#include "logging.h"
#include "asset_handling.h"

#include <signal.h>
#include <execinfo.h>

#include <cstdlib>
#include <cstdio>

#if defined(__GNUC__)
#define DEMANGLE_FUNCTION_NAMES_GCC
#endif

#if defined(DEMANGLE_FUNCTION_NAMES_GCC)
#include <cxxabi.h>
#endif

using std::free;
using std::snprintf;

#if !defined(TRAP_BRANCH)
#define TRAP_BRANCH (TRAP_BRKPT + 2)
#endif

#if !defined(TRAP_HWBKPT)
#define TRAP_HWBKPT (TRAP_BRKPT + 3)
#endif

namespace {
    const int max_stack_frames = 64;
    void* stack_traces[max_stack_frames];
    char signal_handler_stack[SIGSTKSZ];
}

static void log_stack_trace() {
    LOG_ERROR("stack trace:");

    int trace_size = backtrace(stack_traces, max_stack_frames);
    char** messages = backtrace_symbols(stack_traces, trace_size);
    if (!messages) {
        return;
    }

#if defined(DEMANGLE_FUNCTION_NAMES_GCC)
    for (int i = 1; i < trace_size; ++i) {
        char* mangled_name = nullptr;
        char* begin_offset = nullptr;
        char* end_offset = nullptr;

        // Scan the string and find all the points needed to separate the
        // function name from the rest of the message.
        for (char* p = messages[i]; *p; ++p) {
            if (*p == '(') {
                mangled_name = p;
            } else if (*p == '+') {
                begin_offset = p;
            } else if (*p == ')' && begin_offset) {
                end_offset = p;
                break;
            }
        }

        if (mangled_name && begin_offset &&
            end_offset && mangled_name < begin_offset) {
            // Terminate the strings before doing anything with them.
            *mangled_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            // De-mangle the function name.

            int status;
            char* demangled_name = abi::__cxa_demangle(mangled_name, nullptr,
                                                       nullptr, &status);
            if (status == 0) {
                LOG_ERROR("  %s: %s+%s", messages[i], demangled_name,
                          begin_offset);
            } else {
                LOG_ERROR("  %s: %s+%s", messages[i], mangled_name,
                          begin_offset);
            }
            if (demangled_name) {
                free(demangled_name);
            }
        } else {
            LOG_ERROR("  %s", messages[i]);
        }
    }
#else
    // Print out the stack trace line by line, with symbols still mangled.
    for (int i = 1; i < trace_size; ++i) {
        LOG_ERROR("  %s", messages[i]);
    }
#endif
    free(messages);
}

static const char* describe_arithmetic_exception(int code) {
    switch (code) {
        case FPE_INTDIV: return "Integer divide by zero.";
        case FPE_INTOVF: return "Integer overflow.";
        case FPE_FLTDIV: return "Floating point divide by zero.";
        case FPE_FLTOVF: return "Floating point overflow.";
        case FPE_FLTUND: return "Floating point underflow.";
        case FPE_FLTRES: return "Floating point inexact result.";
        case FPE_FLTINV: return "Floating point invalid operation.";
        case FPE_FLTSUB: return "Subscript out of range.";
        defualt:         return "Reason unknown.";
    }
}

static const char* describe_bus_error(int code) {
    switch (code) {
        case BUS_ADRALN: return "Invalid address alignment.";
        case BUS_ADRERR: return "Nonexistent physical address.";
        case BUS_OBJERR: return "Object-specific hardware error.";
        default:         return "Reason unknown.";
    }
}

static const char* describe_illegal_instruction(int code) {
    switch (code) {
        case ILL_ILLOPC: return "Illegal opcode.";
        case ILL_ILLOPN: return "Illegal operand.";
        case ILL_ILLADR: return "Illegal addressing mode.";
        case ILL_ILLTRP: return "Illegal trap.";
        case ILL_PRVOPC: return "Privileged opcode.";
        case ILL_PRVREG: return "Privileged register.";
        case ILL_COPROC: return "Coprocessor error.";
        case ILL_BADSTK: return "Internal stack error.";
        default:         return "Reason unknown.";
    }
}

static const char* describe_segmentation_fault(int code) {
    switch (code) {
        case SEGV_MAPERR: return "Address not mapped to object.";
        case SEGV_ACCERR: return "Invalid permissions for mapped object.";
        default:          return "Unknown cause of fault.";
    }
}

static const char* describe_trap(int code) {
    switch (code) {
        case TRAP_BRKPT:  return "Process breakpoint.";
        case TRAP_TRACE:  return "Process trace trap.";
        case TRAP_BRANCH: return "Process taken branch trap.";
        case TRAP_HWBKPT: return "Hardware breakpoint/watchpoint.";
        default:          return "Reason unknown.";
    }
}

static void describe_error(char* message, int message_max,
                           int signal, siginfo_t* info, void* context) {
    switch (signal) {
        case SIGABRT: {
            snprintf(message, message_max, "The process was told to abort.");
            break;
        }
        case SIGBUS: {
            snprintf(message, message_max, "Access to an undefined portion of "
                     "a memory object at address %p occurred. %s",
                     info->si_addr, describe_bus_error(info->si_code));
            break;
        }
        case SIGILL: {
            snprintf(message, message_max, "An illegal instruction was given "
                     "at address %p. %s", info->si_addr,
                     describe_illegal_instruction(info->si_code));
            break;
        }
        case SIGFPE: {
            snprintf(message, message_max, "An arithmetic exception occurred "
                     "at address %p. %s", info->si_addr,
                     describe_arithmetic_exception(info->si_code));
            break;
        }
        case SIGSEGV: {
            snprintf(message, message_max, "A segmentation fault occurred at "
                     "memory address %p. %s", info->si_addr,
                     describe_segmentation_fault(info->si_code));
            break;
        }
        case SIGTRAP: {
            snprintf(message, message_max, "A trap instruction was "
                     "encountered at memory address %p. %s", info->si_addr,
                     describe_trap(info->si_code));
            break;
        }
    }
}

static void handle_pre_logging_posix_signal(int signal, siginfo_t* info,
                                            void* context) {
    static_cast<void>(context);

    // @Incomplete: see "asynchronous signal handler unsafe" comment in
    // handle_posix_signal

    const int message_max = 128;
    char message[message_max];
    describe_error(message, message_max, signal, info, context);
    report_error_in_a_popup(message, false);

    if (signal == SIGTRAP) {
        raise(signal);
    }
}

static void handle_posix_signal(int signal, siginfo_t* info, void* context) {
    static_cast<void>(context);

    // @Incomplete: snprintf, backtrace, system, and more are classified as
    // "asynchronous signal handler unsafe", meaning that since this handler
    // can be called in the middle of an in-progress operation, calling
    // "unsafe" functions means there's no guarantee the operation goes as
    // expected. Since this handler is intended to be fatal, it doesn't matter
    // much for the things that are potentially being interrupted outside, but
    // it could possibly cause errors if this function is being entered by two
    // separate signals at the same time.

    const int message_max = 128;
    char message[message_max];
    describe_error(message, message_max, signal, info, context);
    LOG_ERROR("%s", message);

    log_stack_trace();

    report_error_in_a_popup(message);

    if (signal == SIGTRAP) {
        raise(signal);
    }
}

namespace {
    const int num_signals = 6;
    const int signals[num_signals] = {
        SIGABRT,
        SIGBUS,
        SIGFPE,
        SIGILL,
        SIGSEGV,
        SIGTRAP,
    };
}

bool set_posix_signal_handler_stack() {
    stack_t stack = {};
    stack.ss_sp = static_cast<void*>(signal_handler_stack);
    stack.ss_size = SIGSTKSZ;
    stack.ss_flags = 0;
    return sigaltstack(&stack, nullptr) == 0;
}

bool register_initial_posix_signal_handlers() {
    struct sigaction action = {};
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = handle_pre_logging_posix_signal;
    for (int i = 0; i < num_signals; ++i) {
        if (sigaction(signals[i], &action, nullptr) == -1) {
            return false;
        }
    }
    return true;
}

const char* describe_signal(int signal) {
    switch (signal) {
        case SIGABRT: return "process abort";
        case SIGBUS:  return "bus error";
        case SIGFPE:  return "arithmetic exception";
        case SIGILL:  return "illegal instruction";
        case SIGSEGV: return "segmentation fault";
        case SIGTRAP: return "trace trap";
        default:      return "unknown signal";
    }
}

bool register_posix_signal_handlers() {
    struct sigaction action = {};
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = handle_posix_signal;
    for (int i = 0; i < num_signals; ++i) {
        if (sigaction(signals[i], &action, nullptr) == -1) {
            LOG_ERROR("Could not set the signal action to handle signals of "
                      "type %s.", describe_signal(signals[i]));
            return false;
        }
    }
    return true;
}
