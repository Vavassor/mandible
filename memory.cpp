#include "memory.h"

#include "atomic.h"
#include "assert.h"

#include <cstdlib>
#include <cstring>

using std::size_t;
using std::calloc;
using std::free;
using std::memset;
using std::memcpy;

#if defined(_WIN32)
// On windows, alloca isn't defined in <cstdlib>, where it is in linux.
#include <malloc.h>
static void* alloca(size_t bytes) {
    return _malloca(bytes);
}
#endif

namespace {
    AtomicInt total_bytes;
}

void* heap_allocate(size_t bytes) {
    ASSERT(bytes != 0);
    size_t header_and_data = sizeof(size_t) + bytes;
    size_t* memory = static_cast<size_t*>(calloc(1, header_and_data));
    if (!memory) {
        return nullptr;
    }
    *memory = bytes;
    atomic_int_add(&total_bytes, bytes);
    return memory + 1;
}

void* heap_reallocate(void* memory, size_t bytes) {
    ASSERT(bytes != 0);
    void* new_memory = heap_allocate(bytes);
    if (!new_memory) {
        return nullptr;
    }
    if (memory) {
        size_t* memory_with_header = static_cast<size_t*>(memory) - 1;
        size_t memory_size = *memory_with_header;
        memcpy(new_memory, memory, memory_size);
        heap_deallocate(memory);
    }
    return new_memory;
}

void heap_deallocate(void* memory) {
    if (!memory) {
        return;
    }
    size_t* memory_with_header = static_cast<size_t*>(memory) - 1;
    size_t bytes = *memory_with_header;
    free(memory_with_header);
    atomic_int_subtract(&total_bytes, bytes);
}

#if 0
// @Incomplete: This only works when this function is inlined. Otherwise,
// returning from this function itself deallocates the alloca memory from the
// stack. Also, issues to do with alloca and inlined functions in general
// are scary and instead a custom stack allocator would be preferable to
// replace this.
void* stack_allocate(size_t bytes) {
    ASSERT(bytes != 0);
    return alloca(bytes);
}
#endif

size_t get_heap_allocated_total() {
    return atomic_int_load(&total_bytes);
}

void* copy_memory(void* to, const void* from, size_t bytes) {
    return memcpy(to, from, bytes);
}

void* set_memory(void* memory, unsigned char value, size_t bytes) {
    return memset(memory, value, bytes);
}
