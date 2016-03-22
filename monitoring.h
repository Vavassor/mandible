#pragma once

#include <cstdint>

namespace monitoring {

struct Timer {
    int64_t start;
};

bool startup();
void shutdown();
void begin_period(Timer* timer);
void end_period(Timer* timer, const char* period_name);
void flush_readings();

void lock();
void unlock();
void sort_readings();
const char* pull_reading();

} // namespace monitoring

#define BEGIN_MONITORING(period_name) \
    monitoring::Timer timer_##period_name; \
    monitoring::begin_period(&timer_##period_name);

#define END_MONITORING(period_name) \
    monitoring::end_period(&timer_##period_name, #period_name)
