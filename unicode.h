#pragma once

char16_t* utf8_to_utf16(const char* s, char16_t* buffer, int count);
char* utf16_to_utf8(const char16_t* str, char* buffer, int count);
int utf8_to_utf32(const char* src, int src_len, char32_t* dst, int dst_len);

int utf8_surrogate_count(const char* s);
int utf16_octet_count(const char16_t* s);
int utf8_codepoint_count(const char* s);
int utf32_octet_count(const char32_t* s, int n);
