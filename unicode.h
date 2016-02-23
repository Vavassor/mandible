#ifndef UNICODE_H
#define UNICODE_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER)
#include <stdint.h>
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#else
#include <uchar.h>
#endif
#include <stddef.h>

char16_t* utf8_to_utf16(const char* s, char16_t* buffer, size_t count);
char* utf16_to_utf8(const char16_t* str, char* buffer, size_t count);

size_t utf8_to_utf32(const char* src, size_t src_len,
                     char32_t* dst, size_t dst_len);

size_t utf8_surrogate_count(const char* s);
size_t utf16_octet_count(const char16_t* s);
size_t utf8_codepoint_count(const char* s);
size_t utf32_octet_count(const char32_t* s, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UNICODE_H */
