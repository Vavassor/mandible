#pragma once

#if defined(NDEBUG)
#define ASSERT(expression) static_cast<void>(0)
#else
#define ASSERT(expression) ((expression) ? static_cast<void>(0) : assert_fail(#expression, __FILE__, __LINE__))
#endif

#if !defined(NDEBUG)

#include <cstdio>
#include <cstdlib>

#if defined(__linux__)

#include <csignal>

inline void assert_fail(const char* expression, const char* file, int line) {
    std::printf("Assertion failed: %s file %s line number %i\n", expression, file, line);
    std::raise(SIGTRAP);
    std::abort();
}

#elif defined(_WIN32)

#include <Windows.h>

inline void assert_fail(const char* expression, const char* file, int line) {
    if (IsDebuggerPresent()) {
        char string[256];
        _snprintf_s(string, 256, _TRUNCATE, "Assertion failed: %s file %s line number %i\n", expression, file, line);
        OutputDebugStringA(string);
        DebugBreak();
    }
    std::abort();
}

#endif // defined(__linux__)

#endif // !defined(NDEBUG)
