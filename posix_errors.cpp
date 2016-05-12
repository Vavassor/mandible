#include "posix_errors.h"

#include "logging.h"

#include <signal.h>
#include <execinfo.h>

#include <cstdlib>

#if defined(__GNUC__)
#define DEMANGLE_FUNCTION_NAMES_GCC
#endif

#if defined(DEMANGLE_FUNCTION_NAMES_GCC)
#include <cxxabi.h>
#endif

#define MAX_STACK_FRAMES 64

namespace {
    void* stack_traces[MAX_STACK_FRAMES];
    unsigned char signal_handler_stack[SIGSTKSZ];
}

static void log_stack_trace() {
    LOG_ERROR("stack trace:");

    int trace_size = backtrace(stack_traces, MAX_STACK_FRAMES);
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

        if (mangled_name && begin_offset && end_offset && mangled_name < begin_offset) {
            // Terminate the strings before doing anything with them.
            *mangled_name++ = '\0';
            *begin_offset++ = '\0';
            *end_offset = '\0';

            // De-mangle the function name.

            int status;
            char* demangled_name = abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status);
            if (status == 0) {
                LOG_ERROR("  %s: %s+%s", messages[i], demangled_name, begin_offset);
            } else {
                LOG_ERROR("  %s: %s+%s", messages[i], mangled_name, begin_offset);
            }
            if (demangled_name) {
                std::free(demangled_name);
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
    std::free(messages);
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
    }
    return "Exception unknown.";
}

static void handle_posix_signal(int signal, siginfo_t* info, void* /*context*/) {
    switch (signal) {
        case SIGSEGV: {
            LOG_ERROR("A segmentation fault occurred at memory address %p.", info->si_addr);
            break;
        }
        case SIGFPE: {
            LOG_ERROR("An arithmetic exception occurred - %s", describe_arithmetic_exception(info->si_code));
            break;
        }
    }

    log_stack_trace();

    system("zenity --error --text=\"mandible encountered an error it was not "
           "able to recover from. Check the log for specifics!\"");

    std::exit(EXIT_FAILURE);
}

bool register_posix_signal_handlers() {
    // Set the alternate stack to be used by the POSIX signal handlers.
    {
        stack_t stack = {};
        stack.ss_sp = static_cast<void*>(signal_handler_stack);
        stack.ss_size = SIGSTKSZ;
        stack.ss_flags = 0;
        if (sigaltstack(&stack, nullptr) != 0) {
            LOG_ERROR("Couldn't set the stack.");
            return false;
        }
    }

    // Register the POSIX signal handlers.
    {
        struct sigaction action = {};
        action.sa_flags = SA_SIGINFO | SA_STACK;
        sigemptyset(&action.sa_mask);
        action.sa_sigaction = handle_posix_signal;
        if (sigaction(SIGSEGV, &action, nullptr) == -1) {
            LOG_ERROR("The signal action for handling segmentation faults could not be set.");
            return false;
        }
        if (sigaction(SIGFPE, &action, nullptr) == -1) {
            LOG_ERROR("The signal action for handling arithmetic exceptions could not be set.");
            return false;
        }
    }

    return true;
}
