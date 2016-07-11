#include "profile.h"

#include "sorting.h"
#include "memory.h"
#include "assert.h"

#include <cstdio>

using std::size_t;
using std::printf;

#if defined(__linux__)

#include <time.h>

static void yield() {
    timespec duration;
    duration.tv_sec = 0;
    duration.tv_nsec = 1000;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &duration, nullptr);
}

static u64 get_timestamp_from_system() {
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1e9 + now.tv_nsec;
}

#elif defined(_WIN32)

#include <Windows.h>

static void yield() {
    Sleep(0);
}

static u64 get_timestamp_from_system() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

#endif // defined(__linux__)

#if defined(__GNUC__)

#define THREAD_LOCAL __thread

static bool atomic_compare_exchange(volatile u32* p, u32 expected, u32 desired) {
    u32 old = expected;
    return __atomic_compare_exchange_n(p, &old, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static u64 get_timestamp() {
#if defined(__i386__)
    u64 x;
    asm volatile ("rdtsc" : "=A" (x));
    return x;
#elif defined(__amd64__)
    u64 a, d;
    asm volatile ("rdtsc" : "=a" (a), "=d" (d));
    return (d << 32) | a;
#elif defined(__arm__)
    // ARMv6 has no performance counter and ARMv7-A and ARMv8-A can only access
    // their "Performance Monitor Unit" if the kernel enables user-space to
    // access it. So, it's too inconvenient to get at; Instead, just fall back
    // to the system call.
    return get_timestamp_from_system();
#endif
}

#elif defined(_MSC_VER)

#define THREAD_LOCAL __declspec(thread)

#include <intrin.h>

static bool atomic_compare_exchange(volatile u32* p, u32 expected, u32 desired) {
    return _InterlockedCompareExchange(p, desired, expected) == desired;
}

static u64 get_timestamp() {
#if defined(_M_IX86) || defined(_M_X64)
    return __rdtsc();
#elif defined(_M_ARM)
    return get_timestamp_from_system();
#endif
}

#endif // defined(__GNUC__)

namespace profile {

typedef volatile u32 SpinLock;

struct Caller {
    const char* name;

    Caller* parent;
    Caller** buckets;
    u32 bucket_count;
    u32 child_count;

    bool active; // used by the root caller of each thread to distinguish if that call tree is active

    u64 started;
    u64 ticks;
    int calls;
    bool paused;
};

struct ThreadState {
    SpinLock thread_lock;
    bool require_thread_lock;
    Caller* active_caller;
};

// SpinLock....................................................................

void spin_lock_acquire(SpinLock* lock) {
    while (!atomic_compare_exchange(lock, 0, 1)) {
        yield();
    }
}

void spin_lock_release(SpinLock* lock) {
    while (!atomic_compare_exchange(lock, 1, 0)) {
        yield();
    }
}

bool spin_lock_try_acquire(SpinLock* lock) {
    return atomic_compare_exchange(lock, 0, 1);
}

bool spin_lock_try_release(SpinLock* lock) {
    return atomic_compare_exchange(lock, 1, 0);
}

// Caller Functions............................................................

static void lock_this_thread();
static void unlock_this_thread();

static u32 hash_pointer(const char* name, u32 bucket_count) {
    return (reinterpret_cast<size_t>(name) >> 5) & (bucket_count - 1);
}

static Caller** find_empty_child_slot(Caller** buckets, u32 bucket_count, const char* name) {
    u32 index = hash_pointer(name, bucket_count);
    u32 mask = bucket_count - 1;
    Caller** slot;
    for (slot = &buckets[index]; *slot; slot = &buckets[index & mask]) {
        index += 1;
    }
    return slot;
}

static u32 next_power_of_two(u32 x) {
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

static void resize(Caller* parent, u32 new_size) {
    if (new_size < parent->bucket_count) {
        new_size = 2 * parent->bucket_count;
    } else {
        new_size = next_power_of_two(new_size - 1);
    }
    Caller** new_buckets = ALLOCATE(Caller*, new_size);
    for (u32 i = 0; i < parent->bucket_count; ++i) {
        if (parent->buckets[i]) {
            *find_empty_child_slot(new_buckets, new_size, parent->buckets[i]->name) = parent->buckets[i];
        }
    }
    DEALLOCATE(parent->buckets);
    parent->buckets = new_buckets;
    parent->bucket_count = new_size;
}

static void caller_create(Caller* caller, Caller* parent, const char* name) {
    caller->name = name;
    caller->parent = parent;
    resize(caller, 2); // initialises the tree hash table
}

static void caller_destroy(Caller* caller) {
    for (u32 i = 0; i < caller->bucket_count; ++i) {
        if (caller->buckets[i]) {
            caller_destroy(caller->buckets[i]);
            DEALLOCATE(caller->buckets[i]);
        }
    }
    DEALLOCATE(caller->buckets);
}

static Caller* find_or_create(Caller* parent, const char* name) {
    u32 index = hash_pointer(name, parent->bucket_count);
    u32 mask = parent->bucket_count - 1;
    for (Caller* caller = parent->buckets[index]; caller; caller = parent->buckets[index & mask]) {
        if (caller->name == name) {
            return caller;
        }
        index += 1;
    }

    lock_this_thread();

    parent->child_count += 1;
    if (parent->child_count >= parent->bucket_count / 2) {
        resize(parent, parent->child_count);
    }

    Caller** slot = find_empty_child_slot(parent->buckets, parent->bucket_count, name);
    Caller* temp = ALLOCATE(Caller, 1);
    caller_create(temp, parent, name);
    *slot = temp;

    unlock_this_thread();

    return temp;
}

static double average(int sum, int count) {
    return (count)? static_cast<double>(sum) / static_cast<double>(count) : 0;
}

#define PRINT_BUFFER_MAX 64

static void caller_print(Caller* caller, char* format_buffer, u64 total_duration, int indent = 0, bool is_last = false) {
    ASSERT(indent + 3 <= PRINT_BUFFER_MAX);

    struct {
        Caller** callers;
        int count;
        int capacity;
    } children;
    children.count = 0;

    if (caller->child_count) {
        children.callers = ALLOCATE(Caller*, caller->child_count);
        children.capacity = caller->child_count;
        for (u32 i = 0; i < caller->bucket_count; ++i) {
            if (caller->buckets[i] && caller->buckets[i]->ticks != 0) {
                children.callers[children.count] = caller->buckets[i];
                children.count += 1;
            }
        }
    }

    char* format = &format_buffer[indent];
    if (indent) {
        format[-2] = (is_last)? ' ' : '|';
        format[-1] = (is_last)? '\\' : ' ';
    }
    format[0] = (children.count)? '+' : '-';
    format[1] = '-';
    format[2] = '\0';

    u64 ticks = caller->ticks;
    int calls = caller->calls;
    double ms = static_cast<double>(ticks) / 1000000.0;
    double percent = average(ticks * 100, total_duration);
    printf("%s %.2f mcycles, %d calls, %.0f cycles avg, %.2f%%: %s\n",
           format_buffer, ms, calls, average(ticks, calls), percent, caller->name);

    if (indent && is_last) {
        format[-2] = ' ';
        format[-1] = ' ';
    }
    if (children.count) {
        Caller** merge_buffer = ALLOCATE(Caller*, children.count);
        auto compare = [](const Caller* a, const Caller* b) -> bool {
            return a->ticks > b->ticks;
        };
        merge_sort(children.callers, merge_buffer, 0, children.count, compare);
        for (u32 i = 0; i < children.count - 1; ++i) {
            caller_print(children.callers[i], format_buffer, total_duration, indent + 2, false);
        }
        caller_print(children.callers[children.count - 1], format_buffer, total_duration, indent + 2, true);
        DEALLOCATE(merge_buffer);
        DEALLOCATE(children.callers);
    }
}

static void start_timing(Caller* caller) {
    caller->calls += 1;
    caller->started = get_timestamp();
}

static void stop_timing(Caller* caller) {
    caller->ticks += get_timestamp() - caller->started;
}

static void caller_reset(Caller* caller) {
    caller->ticks = 0;
    caller->calls = 0;
    caller->started = get_timestamp();
    for (u32 i = 0; i < caller->bucket_count; ++i) {
        if (caller->buckets[i]) {
            caller_reset(caller->buckets[i]);
        }
    }
}

static void caller_stop(Caller* caller) {
    if (!caller->paused) {
        u64 t = get_timestamp();
        caller->ticks += t - caller->started;
        caller->started = t;
    }
}

static void caller_pause(Caller* caller, u64 pause_time) {
    caller->ticks += pause_time - caller->started;
    caller->paused = true;
}

static void caller_unpause(Caller* caller, u64 unpause_time) {
    caller->started = unpause_time;
    caller->paused = false;
}

static void merge_caller_tree(Caller* to, Caller* from) {
    Caller* child = find_or_create(to, from->name);
    child->ticks += from->ticks;
    child->calls += from->calls;
    child->parent = from->parent;
    for (u32 i = 0; i < from->bucket_count; ++i) {
        if (from->buckets[i] && from->buckets[i]->ticks != 0) {
            merge_caller_tree(child, from->buckets[i]);
        }
    }
}

// Global Functions............................................................

struct Root {
    Caller caller;
    ThreadState* thread_state;
};

#define MAX_THREAD_ROOTS 8

struct GlobalThreadsList {
    Root roots[MAX_THREAD_ROOTS];
    int roots_count;
    SpinLock lock;
};

// All the global state is kept here.
namespace {
    THREAD_LOCAL ThreadState thread_state;
    THREAD_LOCAL Caller* root;
    GlobalThreadsList threads_list;
}

static void lock_this_thread() {
    if (thread_state.require_thread_lock) {
        spin_lock_acquire(&thread_state.thread_lock);
    }
}

static void unlock_this_thread() {
    if (thread_state.require_thread_lock) {
        spin_lock_release(&thread_state.thread_lock);
    }
}

static void acquire_global_lock() {
    spin_lock_acquire(&threads_list.lock);
}

static void release_global_lock() {
    spin_lock_release(&threads_list.lock);
}

static Caller* add_root(ThreadState* state) {
    Root* out = threads_list.roots + threads_list.roots_count;
    threads_list.roots_count += 1;
    ASSERT(threads_list.roots_count < MAX_THREAD_ROOTS);
    out->thread_state = state;
    return &out->caller;
}

#if defined(PROFILE_ENABLED)

void begin_period(const char* name) {
    Caller* parent = thread_state.active_caller;
    if (!parent) {
        return;
    }
    Caller* active = find_or_create(parent, name);
    start_timing(active);
    thread_state.active_caller = active;
}

void end_period() {
    Caller* active = thread_state.active_caller;
    if (!active) {
        return;
    }
    stop_timing(active);
    thread_state.active_caller = active->parent;
}

void pause_period() {
    u64 pause_time = get_timestamp();
    for (Caller* it = thread_state.active_caller; it; it = it->parent) {
        caller_pause(it, pause_time);
    }
}

void unpause_period() {
    u64 unpause_time = get_timestamp();
    for (Caller* it = thread_state.active_caller; it; it = it->parent) {
        caller_unpause(it, unpause_time);
    }
}

void enter_thread(const char* name) {
    acquire_global_lock();

    Caller* temp = add_root(&thread_state);
    caller_create(temp, nullptr, name);

    lock_this_thread();

    thread_state.active_caller = temp;
    start_timing(temp);
    temp->active = true;
    root = temp;

    unlock_this_thread();

    release_global_lock();
}

void exit_thread() {
    acquire_global_lock();

    lock_this_thread();

    stop_timing(root);
    root->active = false;
    thread_state.active_caller = nullptr;

    unlock_this_thread();

    release_global_lock();
}

void dump_print() {
    Caller* packer = ALLOCATE(Caller, 1);
    caller_create(packer, nullptr, "/Thread Packer");

    struct {
        Caller* callers[MAX_THREAD_ROOTS];
        int callers_count;
    } packed_threads;
    packed_threads.callers_count = 0;

    acquire_global_lock();

    for (int i = 0; i < threads_list.roots_count; ++i) {
        Root* thread = threads_list.roots + i;

        // If the thread is no longer active, the lock won't be valid.
        bool active = thread->caller.active;
        if (active) {
            spin_lock_acquire(&thread->thread_state->thread_lock);
            // Disable requiring our local lock in case the caller is in our thread. Accumulate will try to set it otherwise.
            thread_state.require_thread_lock = false;
            for (Caller* walk = thread->thread_state->active_caller; walk; walk = walk->parent) {
                caller_stop(walk);
            }
        }

        // Merge the thread into the packer object, which will result in 1 caller per thread name, not 1 caller per thread instance.
        merge_caller_tree(packer, &thread->caller);
        Caller* child = find_or_create(packer, thread->caller.name);

        // Add the child to the list of threads to dump (use the active flag to indicate if it's been added).
        if (!child->active) {
            packed_threads.callers[packed_threads.callers_count] = child;
            packed_threads.callers_count += 1;
            ASSERT(packed_threads.callers_count <= MAX_THREAD_ROOTS);
            child->active = true;
        }

        if (active) {
            thread_state.require_thread_lock = true;
            spin_lock_release(&thread->thread_state->thread_lock);
        }
    }

    release_global_lock();

    char format_buffer[PRINT_BUFFER_MAX];
    for (int i = 0; i < packed_threads.callers_count; ++i) {
        Caller* caller = packed_threads.callers[i];
        caller_print(caller, format_buffer, caller->ticks);
    }
    printf("\n");

    caller_destroy(packer);
    DEALLOCATE(packer);
}

void reset_all() {
    acquire_global_lock();

    for (int i = 0; i < threads_list.roots_count; ++i) {
        Root* thread = threads_list.roots + i;
        if (!thread->caller.active) {
            caller_destroy(&thread->caller);
            int last = threads_list.roots_count;
            threads_list.roots_count -= 1;
            Root* removed = threads_list.roots + threads_list.roots_count;
            if (i != last - 1) {
                *thread = *removed;
            }
            i -= 1;
        } else {
            spin_lock_acquire(&thread->thread_state->thread_lock);
            caller_reset(&thread->caller);
            for (Caller* it = thread->thread_state->active_caller; it; it = it->parent) {
                it->calls = 1;
            }
            spin_lock_release(&thread->thread_state->thread_lock);
        }
    }

    release_global_lock();
}

void cleanup() {
    for (int i = 0; i < threads_list.roots_count; ++i) {
        caller_destroy(&threads_list.roots[i].caller);
    }
}

#else // defined(PROFILE_ENABLED)

void begin_period(const char* name) {}
void end_period() {}
void pause_period() {}
void unpause_period() {}
void enter_thread(const char* name) {}
void exit_thread() {}
void dump_print() {}
void reset_all() {}
void cleanup() {}

#endif // defined(PROFILE_ENABLED)

} // namespace profile
