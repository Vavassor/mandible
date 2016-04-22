#include "monitoring.h"

#include "string_utilities.h"

#include <time.h>
#include <pthread.h>

#include <cassert>

static int64_t read_time() {
    timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    int64_t nanoseconds = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
    return nanoseconds;
}

static int64_t get_time_resolution() {
    timespec resolution;
    clock_getres(CLOCK_MONOTONIC, &resolution);
    int64_t nanoseconds = resolution.tv_nsec + resolution.tv_sec * 1e9;
    return nanoseconds;
}

namespace monitoring {

namespace {
    Chart chart;
    double clock_frequency;
    pthread_mutex_t chart_mutex;
}

void startup() {
    clock_frequency = get_time_resolution();
    pthread_mutex_init(&chart_mutex, nullptr);
}

void shutdown() {
    pthread_mutex_destroy(&chart_mutex);
}

void lock() {
    pthread_mutex_lock(&chart_mutex);
}

void unlock() {
    pthread_mutex_unlock(&chart_mutex);
}

int64_t begin_period() {
    return read_time();
}

void end_period(int64_t start_time, const char* period_name) {
    int64_t end = read_time();
    int64_t duration = end - start_time;

    lock();

    Chart::Slice* slice = chart.slices + chart.current_slice;

    Reading* reading = nullptr;
    for (int i = 0; i < slice->total_readings; ++i) {
        if (strings_match(slice->readings[i].name, period_name)) {
            reading = slice->readings + i;
        }
    }
    if (!reading) {
        reading = slice->readings + slice->total_readings;
        slice->total_readings += 1;
        assert(slice->total_readings < MAX_READINGS);
        *reading = {};
        reading->name = period_name;
    }

    reading->count += 1;
    reading->elapsed_total += duration;

    unlock();
}

void tick_counter(const char* name) {
    lock();

    Chart::Slice* slice = chart.slices + chart.current_slice;

    Counter* counter = nullptr;
    for (int i = 0; i < slice->total_counters; ++i) {
        if (strings_match(slice->counters[i].name, name)) {
            counter = slice->counters + i;
        }
    }
    if (!counter) {
        counter = slice->counters + slice->total_counters;
        slice->total_counters += 1;
        assert(slice->total_counters < MAX_COUNTERS);
        *counter = {};
        counter->name = name;
    }

    counter->ticks += 1;

    unlock();
}

void complete_frame() {
    lock();
    chart.current_slice = (chart.current_slice + 1) % MAX_SLICES;
    Chart::Slice* new_current = chart.slices + chart.current_slice;
    *new_current = {};
    unlock();
}

Chart* get_chart() {
    return &chart;
}

} // namespace monitoring
