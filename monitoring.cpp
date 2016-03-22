#include "monitoring.h"

#include <time.h>
#include <pthread.h>

#include <cstdlib>
#include <cstdio>
#include <cassert>

namespace monitoring {

static void timer_begin(Timer* timer) {
    timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    timer->start = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
}

static int64_t timer_end(Timer* timer) {
    timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    int64_t end = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
    int64_t duration = end - timer->start;
    return duration;
}

static int64_t timer_get_resolution() {
    timespec resolution;
    clock_getres(CLOCK_MONOTONIC, &resolution);
    int64_t nanoseconds = resolution.tv_nsec + resolution.tv_sec * 1e9;
    return nanoseconds;
}

#define MAX_READINGS 64

struct Monitor {
    struct Reading {
        const char* name;
        int64_t duration;
    } readings[MAX_READINGS];
    char text_buffer[128];
    pthread_mutex_t mutex;
    double clock_frequency;
    int total_readings;
};

namespace {
    Monitor* monitor;
}

bool startup() {
    monitor = static_cast<Monitor*>(std::calloc(1, sizeof(Monitor)));
    if (!monitor) {
        return false;
    }
    pthread_mutex_init(&monitor->mutex, nullptr);

    int64_t nanoseconds = timer_get_resolution();
    monitor->clock_frequency = static_cast<double>(nanoseconds) / 1.0e6;

    return true;
}

void shutdown() {
    if (monitor) {
        pthread_mutex_destroy(&monitor->mutex);
        free(monitor);
    }
}

void begin_period(Timer* timer) {
    timer_begin(timer);
}

void end_period(Timer* timer, const char* period_name) {
    int64_t duration = timer_end(timer);
    lock();
    monitor->readings[monitor->total_readings].name = period_name;
    monitor->readings[monitor->total_readings].duration = duration;
    monitor->total_readings += 1;
    assert(monitor->total_readings < MAX_READINGS);
    unlock();
}

void flush_readings() {
    lock();
    monitor->total_readings = 0;
    unlock();
}

void lock() {
    pthread_mutex_lock(&monitor->mutex);
}

void unlock() {
    pthread_mutex_unlock(&monitor->mutex);
}

static inline int compare_strings(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *reinterpret_cast<const unsigned char*>(s1) -
           *reinterpret_cast<const unsigned char*>(s2);
}

static int reading_compare(const void* reading1, const void* reading2) {
    auto* r1 = static_cast<const Monitor::Reading*>(reading1);
    auto* r2 = static_cast<const Monitor::Reading*>(reading2);
    return compare_strings(r1->name, r2->name);
}

void sort_readings() {
    std::qsort(monitor->readings, monitor->total_readings,
               sizeof(*monitor->readings), reading_compare);
}

const char* pull_reading() {
    if (monitor->total_readings <= 0) {
        return nullptr;
    }

    monitor->total_readings -= 1;
    Monitor::Reading* reading = monitor->readings + monitor->total_readings;
    double milliseconds = static_cast<double>(reading->duration) *
                          monitor->clock_frequency;
    std::sprintf(monitor->text_buffer, "%s: %f\n", reading->name,
                 milliseconds);

    return monitor->text_buffer;
}

} // namespace monitoring
