#include "string_utilities.h"

#include <cassert>

void concatenate(char* to, const char* from, std::size_t to_size) {
    assert(string_size(from) + string_size(to) <= to_size);
    while (*(++to) && --to_size);
    while ((*to++ = *from++) && --to_size);
}

std::size_t copy_string(char* to, const char* from, std::size_t to_size) {
    assert(string_size(to) <= to_size);
    std::size_t i;
    for (i = 0; i < to_size - 1; ++i) {
        if (from[i] == '\0') {
            break;
        }
        to[i] = from[i];
    }
    to[i] = '\0';
    return i;
}

std::size_t string_size(const char* str) {
    const char* s;
    for (s = str; *s; ++s);
    return s - str;
}

bool strings_match(const char* a, const char* b) {
    while (*a && (*a==*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}
