#pragma once

#if defined(_MSC_VER)
typedef long AtomicFlag;
#elif defined(__GNUC__)
typedef bool AtomicFlag;
#endif

typedef long AtomicBool;
typedef long AtomicInt;

bool atomic_flag_test_and_set(AtomicFlag* flag);
void atomic_flag_clear(AtomicFlag* flag);

void atomic_bool_store(AtomicBool* b, bool value);
bool atomic_bool_load(AtomicBool* b);

void atomic_int_store(AtomicInt* i, long value);
long atomic_int_load(AtomicInt* i);
long atomic_int_add(AtomicInt* augend, long addend);
long atomic_int_subtract(AtomicInt* minuend, long subtrahend);
