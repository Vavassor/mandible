#include "string_utilities.h"

#include <cassert>

void append_string(char* to, const char* from, std::size_t to_size) {
    assert(from);
    assert(to);
    assert(string_size(from) + string_size(to) <= to_size);
    while (*(++to) && --to_size);
    while ((*to++ = *from++) && --to_size);
}

std::size_t copy_string(char* to, const char* from, std::size_t to_size) {
    assert(from);
    assert(to);
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

std::size_t string_size(const char* string) {
    assert(string);
    const char* s;
    for (s = string; *s; ++s);
    return s - string;
}

bool strings_match(const char* a, const char* b) {
    assert(a);
    assert(b);
    while (*a && (*a == *b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}

