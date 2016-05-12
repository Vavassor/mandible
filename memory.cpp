#include "memory.h"

// @Incomplete: alloca is linux-only in cstdlib!
#include <cstdlib>

void* heap_allocate(std::size_t bytes) {
    return std::calloc(1, bytes);
}

void heap_deallocate(void* memory) {
    std::free(memory);
}

void* stack_allocate(std::size_t bytes) {
    return alloca(bytes);
}
