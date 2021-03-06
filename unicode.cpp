#include "unicode.h"

char16_t* utf8_to_utf16(const char* s, char16_t* buffer, std::size_t count) {
    const unsigned char* str = reinterpret_cast<const unsigned char*>(s);
    --count;
    std::size_t i = 0;
    while (*str) {
        if (i >= count) {
            return nullptr;
        }

        char32_t c;
        if (!(*str & 0x80)) {
            buffer[i++] = *str++;

        } else if ((*str & 0xE0) == 0xC0) {
            if (*str < 0xC2) return nullptr;
            c = (*str++ & 0x1F) << 6;
            if ((*str & 0xC0) != 0x80) return nullptr;
            buffer[i++] = c + (*str++ & 0x3F);

        } else if ((*str & 0xf0) == 0xe0) {
            if (*str == 0xE0 && (str[1] < 0xA0 || str[1] > 0xBF)) return nullptr;
            if (*str == 0xED && str[1] > 0x9F) return nullptr;
            c = (*str++ & 0x0F) << 12;
            if ((*str & 0xC0) != 0x80) return nullptr;
            c += (*str++ & 0x3F) << 6;
            if ((*str & 0xC0) != 0x80) return nullptr;
            buffer[i++] = c + (*str++ & 0x3F);

        } else if ((*str & 0xF8) == 0xF0) {
            if (*str > 0xF4) return nullptr;
            if (*str == 0xF0 && (str[1] < 0x90 || str[1] > 0xBF)) return nullptr;
            if (*str == 0xF4 && str[1] > 0x8F) return nullptr; // str[1] < 0x80 is checked below

            c = (*str++ & 0x07) << 18;
            if ((*str & 0xC0) != 0x80) return nullptr;
            c += (*str++ & 0x3F) << 12;
            if ((*str & 0xC0) != 0x80) return nullptr;
            c += (*str++ & 0x3F) << 6;
            if ((*str & 0xC0) != 0x80) return nullptr;
            c += (*str++ & 0x3F);

            // utf-8 encodings of values used in surrogate pairs are invalid
            if ((c & 0xFFFFF800) == 0xD800) return nullptr;
            if (c >= 0x10000) {
                c -= 0x10000;
                if (i + 2 > count) return nullptr;
                buffer[i++] = 0xD800 | (0x3FF & (c >> 10));
                buffer[i++] = 0xDC00 | (0x3FF & c);
            }

        } else {
            return nullptr;
        }
    }
    buffer[i] = u'\0';
    return buffer;
}

char* utf16_to_utf8(const char16_t* str, char* buffer, std::size_t count) {
    --count;
    std::size_t i = 0;
    while (*str) {
        char32_t c;
        if (*str < 0x80) {
            if (i + 1 > count) return nullptr;
            buffer[i++] = (char) *str++;

        } else if (*str < 0x800) {
            if (i + 2 > count) return nullptr;
            buffer[i++] = 0xC0 + (*str >> 6);
            buffer[i++] = 0x80 + (*str & 0x3F);
            str += 1;

        } else if (*str >= 0xD800 && *str < 0xDC00) {
            if (i + 4 > count) return nullptr;
            c = ((str[0] - 0xD800) << 10) + ((str[1]) - 0xDC00) + 0x10000;
            buffer[i++] = 0xF0 +  (c >> 18);
            buffer[i++] = 0x80 + ((c >> 12) & 0x3F);
            buffer[i++] = 0x80 + ((c >>  6) & 0x3F);
            buffer[i++] = 0x80 + ((c      ) & 0x3F);
            str += 2;

        } else if (*str >= 0xDC00 && *str < 0xE000) {
            return nullptr;

        } else {
            if (i + 3 > count) return nullptr;
            buffer[i++] = 0xE0 + (*str >> 12);
            buffer[i++] = 0x80 + (*str >> 6 & 0x3F);
            buffer[i++] = 0x80 + (*str & 0x3F);
            str += 1;
        }
    }
    buffer[i] = '\0';
    return buffer;
}

static char32_t utf32_at_internal(const char* cur, std::size_t* num_read) {
    const char first_char = *cur;
    if ((first_char & 0x80) == 0) {
        // ASCII
        *num_read = 1;
        return *cur;
    }
    ++cur;

    std::size_t num_to_read = 0;
    char32_t mask, to_ignore_mask;
    char32_t codepoint = first_char;
    for (num_to_read = 1, mask = 0x40, to_ignore_mask = 0xFFFFFF80;
         first_char & mask;
         ++num_to_read, to_ignore_mask |= mask, mask >>= 1) {
        // 0x3F == 00111111
        codepoint = (codepoint << 6) + (*cur++ & 0x3F);
    }
    to_ignore_mask |= mask;
    codepoint &= ~(to_ignore_mask << (6 * (num_to_read - 1)));

    *num_read = num_to_read;
    return codepoint;
}

std::size_t utf8_to_utf32(const char* src, std::size_t src_len,
                          char32_t* dst, std::size_t dst_len) {

    if (!src || src_len == 0 || !dst || dst_len == 0) {
        return 0;
    }

    const char* cur = src;
    const char* end = src + src_len;
    char32_t* cur_utf32 = dst;
    const char32_t* end_utf32 = dst + dst_len;
    while (cur_utf32 < end_utf32 && cur < end) {
        std::size_t num_read;
        *cur_utf32++ = utf32_at_internal(cur, &num_read);
        cur += num_read;
    }
    if (cur_utf32 < end_utf32) {
        *cur_utf32 = 0;
    }

    return static_cast<std::size_t>(cur_utf32 - dst);
}

std::size_t utf8_surrogate_count(const char* str) {
    std::size_t count = 0;
    const char* s = str;
    while (*s) {
        if (!(*s & 0x80))             { s += 1; count += 1; }
        else if ((*s & 0xE0) == 0xC0) { s += 2; count += 1; }
        else if ((*s & 0xf0) == 0xe0) { s += 3; count += 1; }
        else if ((*s & 0xF8) == 0xF0) { s += 4; count += 2; }
        else                          { return 0; }
    }
    return count;
}

std::size_t utf16_octet_count(const char16_t* str) {
    std::size_t count = 0;
    const char16_t* s = str;
    while (*s) {
        if (*s < 0x80)                        { count += 1; s += 1; }
        else if (*s < 0x800)                  { count += 2; s += 1; }
        else if (*s >= 0xD800 && *s < 0xDC00) { count += 4; s += 2; }
        else if (*s >= 0xDC00 && *s < 0xE000) { return 0; }
        else                                  { count += 3; s += 1; }
    }
    return count;
}

std::size_t utf8_codepoint_count(const char* s) {
    std::size_t count = 0;
    while (*s) {
        count += (*s++ & 0xc0) != 0x80;
    }
    return count;
}

std::size_t utf32_octet_count(const char32_t* s, std::size_t n) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (s[i] < 0x80)         count += 1;
        else if (s[i] < 0x800)   count += 2;
        else if (s[i] < 0x10000) count += 3;
        else                     count += 4;
    }
    return count;
}

// @Unused
#if 0
static int isLegalUTF8(const char* source, int length)
{
    char a;
    const char* srcptr = source + length;
    switch (length)
    {
        default: return 0;

        // Everything else falls through when "true"...
        case 4: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 3: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
        case 2: if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;

            switch (*source)
            {
                // no fall-through in this inner switch
                case 0xE0: if (a < 0xA0) return 0; break;
                case 0xED: if (a > 0x9F) return 0; break;
                case 0xF0: if (a < 0x90) return 0; break;
                case 0xF4: if (a > 0x8F) return 0; break;
                default:   if (a < 0x80) return 0;
            }

        case 1: if (*source >= 0x80 && *source < 0xC2) return 0;
    }
    if (*source > 0xF4) return 0;
    return 1;
}
#endif
