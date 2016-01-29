#ifndef ATOMIC_H
#define ATOMIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef long AtomicBool;
typedef long AtomicFlag;

bool atomic_flag_test_and_set(AtomicFlag* flag);
void atomic_flag_clear(AtomicFlag* flag);

void atomic_bool_store(AtomicBool* b, bool value);
bool atomic_bool_load(AtomicBool* b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
