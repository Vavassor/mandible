#include "string_utilities.h"

#include <assert.h>
#include <string.h>

void concatenate(char *to, const char *from, size_t to_size) {
    assert(strlen(from) + strlen(to) <= to_size);
    while (*(++to) && --to_size);
    while ((*to++ = *from++) && --to_size);
}

size_t copy_string(char *to, const char *from, size_t to_size) {
    assert(strlen(to) <= to_size);
    size_t i;
    for (i = 0; i < to_size - 1; ++i) {
        if (from[i] == '\0') {
            break;
        }
        to[i] = from[i];
    }
    to[i] = '\0';
    return i;
}

bool strings_match(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}
