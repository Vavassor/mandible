#ifndef STRING_UTILITIES_H_
#define STRING_UTILITIES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdbool.h>

void concatenate(char *to, const char *from, size_t to_size);
size_t copy_string(char *to, const char *from, size_t to_size);
bool strings_match(const char *a, const char *b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* STRING_UTILITIES_H_ */
