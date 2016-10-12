#pragma once

#include <cstddef>

#include "sized_types.h"

// Stack Functions.............................................................

struct Stack {
    u8* base;
    size_t top;
    size_t total_bytes;
};

typedef size_t StackHandle;

void stack_make_in_place(Stack* stack, void* place, size_t bytes);
bool stack_create(Stack* stack, size_t bytes);
bool stack_create_on_stack(Stack* to, Stack* from, size_t bytes);
void stack_destroy(Stack* stack);
void* stack_allocate(Stack* stack, size_t bytes, size_t alignment,
                     StackHandle* handle);
void* stack_reallocate(Stack* stack, size_t bytes, size_t alignment,
                       StackHandle* handle);
void stack_rewind(Stack* stack, StackHandle to);

class ScopedAllocation {
public:
    ScopedAllocation(Stack* stack, void** memory, size_t bytes,
                     size_t alignment): stack_(stack) {
        *memory = stack_allocate(stack, bytes, alignment, &handle_);
    }
    ~ScopedAllocation() {
        stack_rewind(stack_, handle_);
    }
    Stack* stack_;
    StackHandle handle_;
};

// Pool Functions..............................................................

struct Pool {
    u8* memory;
    size_t object_size;
    size_t object_alignment;
    size_t object_count;
    void** free_list;
};

void pool_make_in_place(Pool* pool, void* place, size_t bytes,
                        size_t object_size, size_t object_alignment);
bool pool_create(Pool* pool, size_t bytes, size_t object_size,
                 size_t object_alignment);
void pool_destroy(Pool* pool);
void* pool_allocate(Pool* pool);
void pool_deallocate(Pool* pool, void* memory);

// Heap Functions..............................................................

struct Heap {
    struct Node {
        s32 next;
        s32 previous;
    };

    struct Block {
        union Header {
            Node used;
        } header;
        union Body {
            Node free;
            u8 data[sizeof(Node)];
        } body;
    } *blocks;

    size_t total_blocks;
};

struct HeapInfo {
    size_t total_entries;
    size_t total_blocks;
    size_t free_entries;
    size_t free_blocks;
    size_t used_entries;
    size_t used_blocks;
};

void heap_make_in_place(Heap* heap, void* place, size_t bytes);
bool heap_create_on_stack(Heap* heap, Stack* stack, size_t bytes);
bool heap_create(Heap* heap, size_t bytes);
void heap_destroy(Heap* heap);
void* heap_allocate(Heap* heap, size_t bytes);
void* heap_reallocate(Heap* heap, void* memory, size_t bytes);
void heap_deallocate(Heap* heap, void* memory);
HeapInfo heap_get_info(Heap* heap);

// Memory Manipulation Functions...............................................

void* move_memory(void* to, const void* from, size_t bytes);
void* set_memory(void* memory, unsigned char value, size_t bytes);

// ALIGNOF macro...............................................................

#if defined(_MSC_VER)
#define ALIGNOF(type) __alignof(type)

#elif defined(__GNUC__)
#define ALIGNOF(type) __alignof__ (type)

#else
namespace alignment {

template<typename T>
struct AlignOf {
    struct S { char c; T t; };
    static const std::size_t value = sizeof(S) - sizeof(T);
};

} // namespace alignment

#define ALIGNOF(type) \
    alignment::AlignOf<type>::value

#endif // defined(_MSC_VER)

// Helper macros...............................................................

#define STACK_ALLOCATE(stack, type, count, handle)                   \
    static_cast<type*>(stack_allocate(stack, sizeof(type) * (count), \
                                      ALIGNOF(type), (handle)))

#define STACK_REALLOCATE(stack, type, count, handle)                   \
    static_cast<type*>(stack_reallocate(stack, sizeof(type) * (count), \
                                        ALIGNOF(type), (handle)))

#define MEMORY_MACRO_PASTE2(a, b) a##b
#define MEMORY_MACRO_PASTE(a, b)  MEMORY_MACRO_PASTE2(a, b)

#define SCOPED_ALLOCATE(stack, memory, type, count)                    \
    ScopedAllocation MEMORY_MACRO_PASTE(scoped_allocation_, __LINE__)( \
        stack,                                                         \
        reinterpret_cast<void**>(memory), sizeof(type) * (count),      \
        ALIGNOF(type))

#define POOL_ALLOCATE(pool, type) \
    static_cast<type*>(pool_allocate(pool))

#define ALLOCATE(heap, type, count) \
    static_cast<type*>(heap_allocate(heap, sizeof(type) * (count)))

#define DEALLOCATE(heap, memory) \
    heap_deallocate(heap, memory)

#define SAFE_DEALLOCATE(heap, memory) \
    if (memory) { heap_deallocate(heap, memory); (memory) = nullptr; }

#define HEAP_REALLOCATE(heap, memory, type, count) \
    static_cast<type*>(heap_reallocate(heap, memory, sizeof(type) * (count)))

#define KIBIBYTES(count) (1024 * (count))
#define MEBIBYTES(count) (1024 * KIBIBYTES(count))
