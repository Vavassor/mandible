#pragma once

#include <cstddef>

void* heap_allocate(std::size_t bytes);
void* heap_reallocate(void* memory, std::size_t bytes);
void heap_deallocate(void* memory);
// void* stack_allocate(std::size_t bytes);
std::size_t get_heap_allocated_total();
void* copy_memory(void* to, const void* from, std::size_t bytes);
void* set_memory(void* memory, unsigned char value, std::size_t bytes);

#define ALLOCATE(type, count) \
    static_cast<type*>(heap_allocate(sizeof(type) * (count)))

#define DEALLOCATE(array) \
    heap_deallocate(array)

#define SAFE_DEALLOCATE(array) \
    if (array) { DEALLOCATE(array); (array) = nullptr; }

#if 0
#define STACK_ALLOCATE(type, count) \
    static_cast<type*>(stack_allocate(sizeof(type) * (count)))
#endif

#define KIBIBYTES(count) (1024 * (count))
#define MEBIBYTES(count) (1024 * KIBIBYTES(count))
