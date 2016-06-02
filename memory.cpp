#include "memory.h"

#include "atomic.h"

#include <cstdlib> // @Incomplete: alloca is linux-only in cstdlib!
#include <cassert>

namespace {
    AtomicInt total_bytes;
}

void* heap_allocate(std::size_t bytes) {
    assert(bytes != 0);
    std::size_t header_and_data = sizeof(std::size_t) + bytes;
    std::size_t* memory = static_cast<std::size_t*>(std::calloc(1, header_and_data));
    if (!memory) {
        return nullptr;
    }
    *memory = bytes;
    atomic_int_add(&total_bytes, bytes);
    return memory + 1;
}

void heap_deallocate(void* memory) {
    assert(memory);
    std::size_t* memory_with_header = static_cast<std::size_t*>(memory) - 1;
    std::size_t bytes = *memory_with_header;
    std::free(memory_with_header);
    atomic_int_subtract(&total_bytes, bytes);
}

void* stack_allocate(std::size_t bytes) {
    assert(bytes != 0);
    return alloca(bytes);
}

std::size_t get_heap_allocated_total() {
    return atomic_int_load(&total_bytes);
}
