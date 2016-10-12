#include "memory.h"
#include "assert.h"
#include "sized_types.h"

#if defined(_WIN32)
#define OS_WINDOWS
#define WINVER        0x0600
#define _WIN32_WINNT  WINVER
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#else
#include <sys/mman.h>
#endif

#include <cstring>

using std::size_t;
using std::memset;
using std::memmove;

#if defined(OS_WINDOWS)

void* virtual_allocate(size_t bytes) {
    return VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                        PAGE_READWRITE);
}

void virtual_deallocate(void* memory) {
    VirtualFree(memory, 0, MEM_RELEASE);
}

#else

void* virtual_allocate(size_t bytes) {
    void* m = mmap(nullptr, bytes + sizeof(size_t), PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    size_t* p = static_cast<size_t*>(m);
    *p = bytes;
    return p + 1;
}

void virtual_deallocate(void* memory) {
    size_t* p = static_cast<size_t*>(memory);
    p -= 1;
    size_t bytes = *p;
    munmap(p, bytes);
}

#endif // defined(OS_WINDOWS)

// Global Helper Functions.....................................................

static void safe_virtual_deallocate(void* memory) {
    if (memory) {
        virtual_deallocate(memory);
        memory = nullptr;
    }
}

static size_t is_power_of_two(size_t x) {
    return (x != 0) && ((x & (~x + 1)) == x);
}

static size_t align_adjustment(const void* pointer, size_t alignment) {
    uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    size_t adjustment = alignment - (address & (alignment - 1));
    if (adjustment == alignment) {
        return 0;
    }
    return adjustment;
}

// Stack Functions.............................................................

void stack_make_in_place(Stack* stack, void* place, size_t bytes) {
    ASSERT(stack);
    ASSERT(place);
    ASSERT(bytes != 0);
    ASSERT(!stack->base); // trying to create an already existent stack
    stack->base = static_cast<u8*>(place);
    stack->top = 0;
    stack->total_bytes = bytes;
}

bool stack_create(Stack* stack, size_t bytes) {
    void* memory = virtual_allocate(bytes);
    if (!memory) {
        return false;
    }
    stack_make_in_place(stack, memory, bytes);
    return true;
}

bool stack_create_on_stack(Stack* to, Stack* from, size_t bytes) {
    void* memory = stack_allocate(from, bytes, 16, nullptr);
    if (!memory) {
        return false;
    }
    stack_make_in_place(to, memory, bytes);
    return true;
}

void stack_destroy(Stack* stack) {
    ASSERT(stack);
    safe_virtual_deallocate(stack->base);
    stack->top = 0;
    stack->total_bytes = 0;
}

void* stack_allocate(Stack* stack, size_t bytes, size_t alignment,
                     StackHandle* handle) {
    ASSERT(stack);
    ASSERT(bytes != 0);
    ASSERT(is_power_of_two(alignment));
    u8* top_pointer = stack->base + stack->top;
    size_t adjustment = align_adjustment(top_pointer, alignment);
    if (stack->top + adjustment + bytes > stack->total_bytes) {
        ASSERT(false);
        return nullptr;
    }
    if (handle) {
        *handle = stack->top;
    }
    stack->top += bytes + adjustment;
    return top_pointer + adjustment;
}

void stack_rewind(Stack* stack, StackHandle handle) {
    ASSERT(stack);
    ASSERT(handle <= stack->top);
    u8* back_to = stack->base + handle;
    size_t bytes = stack->top - handle;
    set_memory(back_to, 0, bytes);
    stack->top = handle;
}

void* stack_reallocate(Stack* stack, size_t bytes, size_t alignment,
                       StackHandle* handle) {
    ASSERT(stack);
    ASSERT(handle);
    if (handle && *handle < stack->top) {
        stack->top = *handle; // rewind without zeroing the memory
    }
    return stack_allocate(stack, bytes, alignment, handle);
}

// Pool Functions..............................................................

void pool_make_in_place(Pool* pool, void* place, size_t bytes,
                        size_t object_size, size_t object_alignment) {
    ASSERT(pool);
    ASSERT(place);
    ASSERT(bytes != 0);
    ASSERT(object_size >= sizeof(void*)); // The free list can't fit in empty
                                          // slots unless this is true.
    ASSERT(is_power_of_two(object_alignment));
    ASSERT(!pool->memory); // trying to create an already existent pool

    pool->memory = static_cast<u8*>(place);
    pool->object_size = object_size;
    pool->object_alignment = object_alignment;
    pool->object_count = 0;

    size_t adjustment = align_adjustment(pool->memory, object_alignment);
    pool->free_list = reinterpret_cast<void**>(pool->memory + adjustment);

    size_t object_count = (bytes - adjustment) / object_size;
    void** p = pool->free_list;
    for (size_t i = 0; i < object_count - 1; ++i) {
        *p = p + object_size;
        p = static_cast<void**>(*p);
    }
    *p = nullptr;
}

bool pool_create(Pool* pool, size_t bytes, size_t object_size,
                 size_t object_alignment) {
    void* memory = virtual_allocate(bytes);
    if (!memory) {
        return false;
    }
    pool_make_in_place(pool, memory, bytes, object_size, object_alignment);
    return true;
}

void pool_destroy(Pool* pool) {
    ASSERT(pool);
    safe_virtual_deallocate(pool->memory);
    pool->free_list = nullptr;
}

void* pool_allocate(Pool* pool) {
    ASSERT(pool);
    if (!pool->free_list) {
        ASSERT(false);
        return nullptr;
    }
    void* next_free = pool->free_list;
    pool->free_list = static_cast<void**>(*pool->free_list);
    pool->object_count += 1;
    return next_free;
}

void pool_deallocate(Pool* pool, void* memory) {
    ASSERT(pool);
    ASSERT(memory);
    set_memory(memory, 0, pool->object_size);
    *static_cast<void**>(memory) = pool->free_list;
    pool->free_list = static_cast<void**>(memory);
    pool->object_count -= 1;
}

// Heap Functions..............................................................

#define DO_BEST_FIT

#define NEXT_FREE(index)   heap->blocks[index].body.free.next
#define PREV_FREE(index)   heap->blocks[index].body.free.previous
#define BLOCK_DATA(index)  heap->blocks[index].body.data
#define NEXT_BLOCK(index)  heap->blocks[index].header.used.next
#define PREV_BLOCK(index)  heap->blocks[index].header.used.previous

namespace {
    const s32 freelist_mask = 0x80000000;
    const s32 blockno_mask = 0x7FFFFFFF;
}

void heap_make_in_place(Heap* heap, void* place, size_t bytes) {
    ASSERT(heap);
    ASSERT(place);
    ASSERT(bytes != 0);
    ASSERT(!heap->blocks); // trying to create an already existent heap
    heap->blocks = static_cast<Heap::Block*>(place);
    heap->total_blocks = bytes / sizeof(Heap::Block);
    NEXT_BLOCK(0) = 1;
    NEXT_FREE(0) = 1;
}

bool heap_create_on_stack(Heap* heap, Stack* stack, size_t bytes) {
    void* space = stack_allocate(stack, bytes, 16, nullptr);
    if (!space) {
        return false;
    }
    heap_make_in_place(heap, space, bytes);
    return true;
}

bool heap_create(Heap* heap, size_t bytes) {
    void* memory = virtual_allocate(bytes);
    if (!memory) {
        return false;
    }
    heap_make_in_place(heap, memory, bytes);
    return true;
}

void heap_destroy(Heap* heap) {
    safe_virtual_deallocate(heap->blocks);
    heap->total_blocks = 0;
}

static s32 determine_blocks_needed(size_t size) {
    // When a block removed from the free list, the space used by the free
    // pointers is available for data.
    if (size <= sizeof(Heap::Block::Body)) {
        return 1;
    }
    // If it's for more than that, then we need to figure out the number of
    // additional whole blocks the size of an Heap::Block are required.
    size -= 1 + sizeof(Heap::Block::Body);
    return 2 + size / sizeof(Heap::Block);
}

static void disconnect_from_free_list(Heap* heap, s32 c) {
    NEXT_FREE(PREV_FREE(c)) = NEXT_FREE(c);
    PREV_FREE(NEXT_FREE(c)) = PREV_FREE(c);
    NEXT_BLOCK(c) &= ~freelist_mask;
}

static void make_new_block(Heap* heap, s32 c, s32 blocks, s32 freemask) {
    NEXT_BLOCK(c + blocks) = NEXT_BLOCK(c) & blockno_mask;
    PREV_BLOCK(c + blocks) = c;
    PREV_BLOCK(NEXT_BLOCK(c) & blockno_mask) = c + blocks;
    NEXT_BLOCK(c) = c + blocks | freemask;
}

void* heap_allocate(Heap* heap, size_t bytes) {
    ASSERT(heap);
    ASSERT(bytes != 0);

    s32 blocks = determine_blocks_needed(bytes);
    s32 cf;
    s32 block_size = 0;
    {
        s32 best_size = 0x7FFFFFFF;
        s32 best_block = NEXT_FREE(0);

        for (cf = NEXT_FREE(0); NEXT_FREE(cf); cf = NEXT_FREE(cf)) {
            block_size = (NEXT_BLOCK(cf) & blockno_mask) - cf;
            #if defined(DO_FIRST_FIT)
                if (block_size >= blocks) {
                    break;
                }
            #elif defined(DO_BEST_FIT)
                if (block_size >= blocks && block_size < best_size) {
                    best_block = cf;
                    best_size = block_size;
                }
            #endif
        }

        if (best_size != 0x7FFFFFFF) {
            cf = best_block;
            block_size = best_size;
        }
    }

    if (NEXT_BLOCK(cf) & blockno_mask) {
        // This is an existing block in the memory heap, we just need to split
        // off what we need, unlink it from the free list and mark it as in
        // use, and link the rest of the block back into the freelist as if it
        // was a new block on the free list...
        if (block_size == blocks) {
            // It's an exact fit and we don't need to split off a block.
            disconnect_from_free_list(heap, cf);
        } else {
            // It's not an exact fit and we need to split off a block.
            make_new_block(heap, cf, block_size - blocks, freelist_mask);
            cf += block_size - blocks;
        }
    } else {
        // We're at the end of the heap - allocate a new block, but check to
        // see if there's enough memory left for the requested block!
        if (heap->total_blocks <= cf + blocks + 1) {
            return nullptr;
        }
        NEXT_FREE(PREV_FREE(cf)) = cf + blocks;
        move_memory(&heap->blocks[cf + blocks], &heap->blocks[cf],
                    sizeof(Heap::Block));
        NEXT_BLOCK(cf) = cf + blocks;
        PREV_BLOCK(cf + blocks) = cf;
    }

    set_memory(&BLOCK_DATA(cf), 0, bytes);
    return &BLOCK_DATA(cf);
}

static void try_to_assimilate_up(Heap* heap, s32 c) {
    if (NEXT_BLOCK(NEXT_BLOCK(c)) & freelist_mask) {
        // The next block is a free block, so assimilate up and remove it from
        // the free list.
        disconnect_from_free_list(heap, NEXT_BLOCK(c));
        // Assimilate the next block with this one
        PREV_BLOCK(NEXT_BLOCK(NEXT_BLOCK(c)) & blockno_mask) = c;
        NEXT_BLOCK(c) = NEXT_BLOCK(NEXT_BLOCK(c)) & blockno_mask;
    }
}

static s32 assimilate_down(Heap* heap, s32 c, s32 freemask) {
    NEXT_BLOCK(PREV_BLOCK(c)) = NEXT_BLOCK(c) | freemask;
    PREV_BLOCK(NEXT_BLOCK(c)) = PREV_BLOCK(c);
    return PREV_BLOCK(c);
}

void* heap_reallocate(Heap* heap, void* memory, size_t bytes) {
    ASSERT(heap);

    if (!memory) {
        return heap_allocate(heap, bytes);
    }
    if (bytes == 0) {
        heap_deallocate(heap, memory);
        return nullptr;
    }

    // which block we're in
    s32 c = static_cast<Heap::Block*>(memory) - heap->blocks;

    s32 blocks = determine_blocks_needed(bytes);
    s32 block_room = NEXT_BLOCK(c) - c;
    s32 current_size = sizeof(Heap::Block) * block_room
                     - sizeof(Heap::Block::Header);

    if (block_room == blocks) {
        // They had the space needed all along. ;o)
        return memory;
    }

    try_to_assimilate_up(heap, c);

    if ((NEXT_BLOCK(PREV_BLOCK(c)) & freelist_mask) &&
        (blocks <= NEXT_BLOCK(c) - PREV_BLOCK(c))) {
        disconnect_from_free_list(heap, PREV_BLOCK(c));
        // Connect the previous block to the next block ... and then realign
        // the current block pointer
        c = assimilate_down(heap, c, 0);
        // Move the bytes down to the new block we just created, but be sure to
        // move only the original bytes.
        void* to = &BLOCK_DATA(c);
        move_memory(to, memory, current_size);
        memory = to;
    }

    block_room = NEXT_BLOCK(c) - c;

    if (block_room == blocks) {
        // return the original pointer
    } else if (blocks < block_room) {
        // New block is smaller than the old block, so just make a new block
        // at the end of this one and put it up on the free list.
        make_new_block(heap, c, blocks, 0);
        heap_deallocate(heap, &BLOCK_DATA(c + blocks));
    } else {
        // New block is bigger than the old block.
        void* old = memory;
        memory = heap_allocate(heap, bytes);
        if (memory) {
            move_memory(memory, old, current_size);
        }
        heap_deallocate(heap, old);
    }

    return memory;
}

void heap_deallocate(Heap* heap, void* memory) {
    ASSERT(heap);
    if (!memory) {
        return;
    }
    // which block the memory is in
    s32 c = static_cast<Heap::Block*>(memory) - heap->blocks;

    try_to_assimilate_up(heap, c);

    if (NEXT_BLOCK(PREV_BLOCK(c)) & freelist_mask) {
        // assimilate with the previous block if possible
        c = assimilate_down(heap, c, freelist_mask);
    } else {
        // The previous block is not a free block, so add this one to the head
        // of the free list
        PREV_FREE(NEXT_FREE(0)) = c;
        NEXT_FREE(c) = NEXT_FREE(0);
        PREV_FREE(c) = 0;
        NEXT_FREE(0) = c;
        NEXT_BLOCK(c) |= freelist_mask;
    }
}

HeapInfo heap_get_info(Heap* heap) {
    HeapInfo info = {};
    u32 blockno = 0;
    for (blockno = NEXT_BLOCK(blockno) & blockno_mask;
        NEXT_BLOCK(blockno) & blockno_mask;
        blockno = NEXT_BLOCK(blockno) & blockno_mask) {
        info.total_entries += 1;
        info.total_blocks += (NEXT_BLOCK(blockno) & blockno_mask) - blockno;
        if (NEXT_BLOCK(blockno) & freelist_mask) {
            info.free_entries += 1;
            info.free_blocks += (NEXT_BLOCK(blockno) & blockno_mask) - blockno;
        } else {
            info.used_entries += 1;
            info.used_blocks += (NEXT_BLOCK(blockno) & blockno_mask) - blockno;
        }
    }
    info.free_blocks  += heap->total_blocks - blockno;
    info.total_blocks += heap->total_blocks - blockno;
    return info;
}

// Memory Manipulation Functions...............................................

void* move_memory(void* to, const void* from, size_t bytes) {
    return memmove(to, from, bytes);
}

void* set_memory(void* memory, unsigned char value, size_t bytes) {
    return memset(memory, value, bytes);
}
