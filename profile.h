#pragma once

#include "sized_types.h"

#define PROFILE_ENABLED

#define PROFILE_MACRO_PASTE2(a, b) a##b
#define PROFILE_MACRO_PASTE(a, b)  PROFILE_MACRO_PASTE2(a, b)

#if defined(_MSC_VER)
#define PROFILE_FUNCTION_NAME __FUNCSIG__
#elif defined(__GNUC__)
#define PROFILE_FUNCTION_NAME __PRETTY_FUNCTION__
#endif

#if defined(PROFILE_ENABLED)

#define PROFILE_BEGIN()           profile::begin_period(PROFILE_FUNCTION_NAME)
#define PROFILE_BEGIN_NAMED(name) profile::begin_period(name)
#define PROFILE_END()             profile::end_period()

#define PROFILE_SCOPED()           profile::ScopedBlock PROFILE_MACRO_PASTE(profile_scoped_, __LINE__)(PROFILE_FUNCTION_NAME)
#define PROFILE_SCOPED_NAMED(name) profile::ScopedBlock PROFILE_MACRO_PASTE(profile_scoped_, __LINE__)(name)

#define PROFILE_THREAD_ENTER(heap)             profile::enter_thread(heap, PROFILE_FUNCTION_NAME)
#define PROFILE_THREAD_ENTER_NAMED(heap, name) profile::enter_thread(heap, name)
#define PROFILE_THREAD_EXIT()                  profile::exit_thread()

#else

#define PROFILE_BEGIN_NAMED(name)
#define PROFILE_BEGIN()
#define PROFILE_END()

#define PROFILE_SCOPED()
#define PROFILE_SCOPED_NAMED(name)

#define PROFILE_THREAD_ENTER(heap)
#define PROFILE_THREAD_ENTER_NAMED(heap, name)
#define PROFILE_THREAD_EXIT()

#endif

struct Heap;

namespace profile {

void begin_period(const char* name);
void end_period();
void pause_period();
void unpause_period();
void enter_thread(Heap* heap, const char* name);
void exit_thread();
void dump_print();
void reset_all();
void cleanup();

struct ScopedBlock {
    ScopedBlock(const char* name) { PROFILE_BEGIN_NAMED(name); }
    ~ScopedBlock() { PROFILE_END(); }
};

} // namespace profile
