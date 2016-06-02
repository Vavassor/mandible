#pragma once

#include <cstddef>

void append_string(char* to, const char* from, std::size_t to_size);
std::size_t copy_string(char* to, const char* from, std::size_t to_size);
std::size_t string_size(const char* string);
bool strings_match(const char* a, const char* b);
char* find_string(const char* a, const char* b);
