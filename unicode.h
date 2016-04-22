#pragma once

#include <cstddef>

char16_t* utf8_to_utf16(const char* s, char16_t* buffer, std::size_t count);
char* utf16_to_utf8(const char16_t* str, char* buffer, std::size_t count);

std::size_t utf8_to_utf32(const char* src, std::size_t src_len,
                          char32_t* dst, std::size_t dst_len);

std::size_t utf8_surrogate_count(const char* s);
std::size_t utf16_octet_count(const char16_t* s);
std::size_t utf8_codepoint_count(const char* s);
std::size_t utf32_octet_count(const char32_t* s, std::size_t n);
