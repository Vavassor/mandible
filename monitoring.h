#pragma once

#include <cstdint>

namespace monitoring {

static const int MAX_SLICES = 100;
static const int MAX_READINGS = 16;
static const int MAX_COUNTERS = 8;

struct Reading {
    const char* name;
    int64_t elapsed_total;
    int count;
};

struct Counter {
    const char* name;
    int ticks;
};

struct Chart {
    struct Slice {
        Reading readings[MAX_READINGS];
        Counter counters[MAX_COUNTERS];
        int total_readings;
        int total_counters;
    } slices[MAX_SLICES];
    int current_slice;
};

void startup();
void shutdown();
int64_t begin_period();
void end_period(int64_t start_time, const char* period_name);
void tick_counter(const char* name);
void complete_frame();

void lock();
void unlock();
Chart* get_chart();

} // namespace monitoring

#define BEGIN_MONITORING(period_name) \
    int64_t start_time_##period_name = monitoring::begin_period();

#define END_MONITORING(period_name) \
    monitoring::end_period(start_time_##period_name, #period_name)
