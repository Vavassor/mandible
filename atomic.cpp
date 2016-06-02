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

#if defined(COMPILER_MSVC)

bool atomic_flag_test_and_set(AtomicFlag* flag) {
    return _InterlockedExchange(const_cast<volatile long*>(flag), 1L);
}

void atomic_flag_clear(AtomicFlag* flag) {
    _InterlockedExchange(const_cast<volatile long*>(flag), 0L);
}

void atomic_bool_store(AtomicBool* b, bool value) {
    _InterlockedExchange(const_cast<volatile long*>(b), static_cast<long>(value));
}

bool atomic_bool_load(AtomicBool* b) {
    return _InterlockedOr(const_cast<volatile long*>(b), 0L);
}

void atomic_int_store(AtomicInt* i, long value) {
    _InterlockedExchange(const_cast<volatile long*>(i), value);
}

long atomic_int_load(AtomicInt* i) {
    return _InterlockedOr(const_cast<volatile long*>(i), 0L);
}

long atomic_int_add(AtomicInt* augend, long addend) {
    return _InterlockedAdd(const_cast<volatile long*>(augend), addend);
}

long atomic_int_subtract(AtomicInt* minuend, long subtrahend) {
    return _InterlockedAdd(const_cast<volatile long*>(minuend), -subtrahend);
}

#elif defined(COMPILER_GCC)

bool atomic_flag_test_and_set(AtomicFlag* flag) {
    return __atomic_test_and_set(flag, __ATOMIC_SEQ_CST);
}

void atomic_flag_clear(AtomicFlag* flag) {
    __atomic_clear(flag, __ATOMIC_SEQ_CST);
}

void atomic_bool_store(AtomicBool* b, bool value) {
    __atomic_store_n(b, value, __ATOMIC_SEQ_CST);
}

bool atomic_bool_load(AtomicBool* b) {
    return __atomic_load_n(b, __ATOMIC_SEQ_CST);
}

void atomic_int_store(AtomicInt* i, long value) {
    __atomic_store_n(const_cast<volatile long*>(i), value, __ATOMIC_SEQ_CST);
}

long atomic_int_load(AtomicInt* i) {
    return __atomic_load_n(const_cast<volatile long*>(i), __ATOMIC_SEQ_CST);
}

long atomic_int_add(AtomicInt* augend, long addend) {
    return __atomic_add_fetch(const_cast<volatile long*>(augend), addend, __ATOMIC_SEQ_CST);
}

long atomic_int_subtract(AtomicInt* minuend, long subtrahend) {
    return __atomic_sub_fetch(const_cast<volatile long*>(minuend), subtrahend, __ATOMIC_SEQ_CST);
}

#endif // defined(COMPILER_GCC)
