#include "logging.h"

#if defined(_MSC_VER) && defined(_WIN32)
#define WINDOWS_DEBUGGER_PRINT
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#else
#include <cstdio>
#endif

#include <cstdarg>
#include <cassert>

namespace logging {

static void debug_print(Level level, char* message) {
    if (level == Level::Error) {
        std::fputs(message, stderr);
        std::fputc('\n', stderr);
    } else {
#if defined(WINDOWS_DEBUGGER_PRINT)
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
#else
        std::puts(message);
#endif
    }
}

void add_message(Level level, const char* format, ...) {
    char message[256];
    va_list arguments;
    va_start(arguments, format);
    int written = std::vsnprintf(message, sizeof message, format, arguments);
    assert(written > 0 && written < sizeof message);
    va_end(arguments);
#if !defined(NDEBUG)
    if (written > 0) {
        debug_print(level, message);
    }
#endif
}

} // namespace logging
