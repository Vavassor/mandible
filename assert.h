#pragma once

void assert_fail(const char* expression, const char* file, int line);

#if defined(NDEBUG)
#define ASSERT(expression) static_cast<void>(0)
#else
#define ASSERT(expression) ((expression) ? static_cast<void>(0) : assert_fail(#expression, __FILE__, __LINE__))
#endif

#define INVALID_DEFAULT_CASE default: { ASSERT(false); break; }
