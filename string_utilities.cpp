#include "string_utilities.h"

#include "assert.h"

void append_string(char* to, int to_size, const char* from) {
    ASSERT(from);
    ASSERT(to);
    ASSERT(string_size(from) + string_size(to) <= to_size);
    while (*(++to) && --to_size);
    while ((*to++ = *from++) && --to_size);
}

int copy_string(char* to, int to_size, const char* from) {
    ASSERT(from);
    ASSERT(to);
    ASSERT(string_size(to) <= to_size);
    int i;
    for (i = 0; i < to_size - 1; ++i) {
        if (from[i] == '\0') {
            break;
        }
        to[i] = from[i];
    }
    to[i] = '\0';
    return i;
}

int string_size(const char* string) {
    ASSERT(string);
    const char* s;
    for (s = string; *s; ++s);
    return s - string;
}

bool strings_match(const char* a, const char* b) {
    ASSERT(a);
    ASSERT(b);
    while (*a && (*a == *b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool memory_matches(const void* a, const void* b, int n) {
    ASSERT(a);
    ASSERT(b);
    const unsigned char* p1 = static_cast<const unsigned char*>(a);
    const unsigned char* p2 = static_cast<const unsigned char*>(b);
    while (n--) {
        if (*p1 != *p2) {
            return false;
        } else {
            ++p1;
            ++p2;
        }
    }
    return true;
}

char* find_string(const char* a, const char* b) {
    ASSERT(a);
    ASSERT(b);
    int n = string_size(b);
    while (*a) {
        if (memory_matches(a, b, n)) {
            return const_cast<char*>(a);
        }
        ++a;
    }
    return nullptr;
}

static inline bool is_space(char c) {
    return c == ' ' || c - 9 <= 5;
}

static inline int char_to_integer(char c) {
    int x;
    if ('0' <= c && c <= '9') {
        x = c - '0';
    } else if ('a' <= c && c <= 'z') {
        x = c - 'a' + 10;
    } else if ('A' <= c && c <= 'Z') {
        x = c - 'A' + 10;
    } else {
        x = 36;
    }
    return x;
}

#define ULLONG_MAX static_cast<unsigned long long>(~0ull)

static unsigned long long string_to_ull(const char* string, char** after, int base) {
    ASSERT(string);
    ASSERT(base >= 0 && base != 1 && base <= 36);

    unsigned long long result = 0;

    const char* s = string;

    while (is_space(*s)) {
        s += 1;
    }

    bool negative;
    if (*s == '-') {
        negative = true;
        s += 1;
    } else {
        negative = false;
        if (*s == '+') {
            s += 1;
        }
    }

    if (base < 0 || base == 1 || base > 36) {
        if (after) {
            *after = const_cast<char*>(string);
        }
        return 0;
    } else if (base == 0) {
        if (*s != '0') {
            base = 10;
        } else if (s[1] == 'x' || s[1] == 'X') {
            base = 16;
            s += 2;
        } else {
            base = 8;
            s += 1;
        }
    }

    bool digits_read = false;
    bool out_of_range = false;
    for (; *s; ++s) {
        int digit = char_to_integer(*s);
        if (digit >= base) {
            break;
        } else {
            digits_read = true;
            if (!out_of_range) {
                if (result > ULLONG_MAX / base || result * base > ULLONG_MAX - digit) {
                    out_of_range = true;
                }
                result = result * base + digit;
            }
        }
    }

    if (after) {
        if (!digits_read) {
            *after = const_cast<char*>(string);
        } else {
            *after = const_cast<char*>(s);
        }
    }
    if (out_of_range) {
        return ULLONG_MAX;
    }
    if (negative) {
        result = -result;
    }

    return result;
}

int string_to_int(const char* string) {
    return string_to_ull(string, nullptr, 0);
}
