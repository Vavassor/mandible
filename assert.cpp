#include "assert.h"

#if !defined(NDEBUG)

#include <cstdlib>

using std::abort;

#if defined(__linux__)

#include <cstdio>
#include <csignal>

using std::printf;
using std::raise;

void assert_fail(const char* expression, const char* file, int line) {
    fprintf(stderr, "Assertion failed: %s file %s line number %i\n", expression, file, line);
    raise(SIGTRAP);
    abort();
}

#elif defined(_WIN32)

#include <Windows.h>

void assert_fail(const char* expression, const char* file, int line) {
    if (IsDebuggerPresent()) {
        char string[256];
        _snprintf_s(string, 256, _TRUNCATE, "Assertion failed: %s file %s line number %i\n", expression, file, line);
        OutputDebugStringA(string);
        DebugBreak();
    }
    abort();
}

#endif // defined(__linux__)

#endif // !defined(NDEBUG)
