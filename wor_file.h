#pragma once

struct Stack;

// .wor World file format
namespace wor {

void save_chunk(const char* filename, Stack* stack);
void load_chunk(const char* filename, Stack* stack);

} // namespace wor
