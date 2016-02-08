#include "atomic.h"

#if defined(_MSC_VER)
#define COMPILER_MSVC
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <intrin.h>

#elif defined(__GNUC__)
#define COMPILER_GCC
#endif 

bool atomic_flag_test_and_set(AtomicFlag* flag) {
#if defined(COMPILER_MSVC)
    return (bool) _InterlockedExchange((volatile long*) flag, 1L);
#elif defined(COMPILER_GCC)
    return (bool) __sync_lock_test_and_set(flag, 1L);
#endif
}

void atomic_flag_clear(AtomicFlag* flag) {
#if defined(COMPILER_MSVC)
    _InterlockedExchange((volatile long*) flag, 0L);
    MemoryBarrier();
#elif defined(COMPILER_GCC)
    __sync_lock_release(flag);
    __sync_synchronize();
#endif
}

void atomic_bool_store(AtomicBool* b, bool value) {
#if defined(COMPILER_MSVC)
    _InterlockedExchange((volatile long*) b, (long) value);
#elif defined(COMPILER_GCC)
    __sync_lock_test_and_set(b, (long) value);
#endif
}

bool atomic_bool_load(AtomicBool* b) {
#if defined(COMPILER_MSVC)
    return (bool) _InterlockedAdd((volatile long*) b, 0L);
#elif defined(COMPILER_GCC)
    return (bool) __sync_fetch_and_add(b, 0L);
#endif
}
