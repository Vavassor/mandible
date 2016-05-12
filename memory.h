#pragma once

#include <cstddef>

void* heap_allocate(std::size_t bytes);
void heap_deallocate(void* memory);
void* stack_allocate(std::size_t bytes);

#define ALLOCATE(type, count) \
    static_cast<type*>(heap_allocate(sizeof(type) * (count)))

#define DEALLOCATE(array) \
    heap_deallocate(array)

#define SAFE_DEALLOCATE(array) \
    if (array) { DEALLOCATE(array); (array) = nullptr; }

#define STACK_ALLOCATE(type, count) \
    static_cast<type*>(stack_allocate(sizeof(type) * (count)))
