#ifndef ATOMIC_H
#define ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef long AtomicBool;
typedef long AtomicFlag;
typedef long AtomicInt;

bool atomic_flag_test_and_set(AtomicFlag* flag);
void atomic_flag_clear(AtomicFlag* flag);

void atomic_bool_store(AtomicBool* b, bool value);
bool atomic_bool_load(AtomicBool* b);

void atomic_int_store(AtomicInt* i, long value);
long atomic_int_load(AtomicInt* i);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
