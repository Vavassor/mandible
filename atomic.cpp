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
    return _InterlockedExchange(const_cast<volatile long*>(flag), 1L);
#elif defined(COMPILER_GCC)
    return __sync_lock_test_and_set(flag, 1L);
#endif
}

void atomic_flag_clear(AtomicFlag* flag) {
#if defined(COMPILER_MSVC)
    _InterlockedExchange(const_cast<volatile long*>(flag), 0L);
    MemoryBarrier();
#elif defined(COMPILER_GCC)
    __sync_lock_release(flag);
    __sync_synchronize();
#endif
}

void atomic_bool_store(AtomicBool* b, bool value) {
#if defined(COMPILER_MSVC)
    _InterlockedExchange(const_cast<volatile long*>(b), static_cast<long>(value));
#elif defined(COMPILER_GCC)
    __sync_lock_test_and_set(b, static_cast<long>(value));
#endif
}

bool atomic_bool_load(AtomicBool* b) {
#if defined(COMPILER_MSVC)
    return _InterlockedAdd(const_cast<volatile long*>(b), 0L);
#elif defined(COMPILER_GCC)
    return __sync_fetch_and_add(b, 0L);
#endif
}

void atomic_int_store(AtomicInt* i, long value) {
#if defined(COMPILER_MSVC)
    _InterlockedExchange(const_cast<volatile long*>(i), value);
#elif defined(COMPILER_GCC)
    __sync_lock_test_and_set(i, value);
#endif
}

long atomic_int_load(AtomicInt* i) {
#if defined(COMPILER_MSVC)
    return _InterlockedAdd(const_cast<volatile long*>(i), 0L);
#elif defined(COMPILER_GCC)
    return __sync_fetch_and_add(i, 0L);
#endif
}
