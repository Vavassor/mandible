#pragma once

void append_string(char* to, int to_size, const char* from);
int copy_string(char* to, int to_size, const char* from);
int string_size(const char* string);
bool strings_match(const char* a, const char* b);
char* find_string(const char* a, const char* b);
int string_to_int(const char* string);
