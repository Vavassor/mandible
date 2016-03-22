#pragma once

#include <cstddef>

void concatenate(char* to, const char* from, std::size_t to_size);
std::size_t copy_string(char* to, const char* from, std::size_t to_size);
std::size_t string_size(const char* str);
bool strings_match(const char* a, const char* b);
