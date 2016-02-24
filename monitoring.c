#include "monitoring.h"

#include <time.h>
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

static void timer_begin(Timer *timer) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    timer->start = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
}

static int64_t timer_end(Timer *timer) {
    struct timespec timestamp;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    int64_t end = timestamp.tv_nsec + timestamp.tv_sec * 1e9;
    int64_t duration = end - timer->start;
    return duration;
}

static int64_t timer_get_resolution() {
    struct timespec resolution;
    clock_getres(CLOCK_MONOTONIC, &resolution);
    int64_t nanoseconds = resolution.tv_nsec + resolution.tv_sec * 1e9;
    return nanoseconds;
}

#define MAX_READINGS 64

struct Monitor {
    struct {
        const char *name;
        int64_t duration;
    } readings[MAX_READINGS];
    char text_buffer[128];
    pthread_mutex_t mutex;
    double clock_frequency;
    int total_readings;
};

Monitor *monitor_create() {
    Monitor *monitor = calloc(1, sizeof(Monitor));
    pthread_mutex_init(&monitor->mutex, NULL);

    int64_t nanoseconds = timer_get_resolution();
    monitor->clock_frequency = (double) nanoseconds / 1.0e6;

    return monitor;
}

void monitor_destroy(Monitor *monitor) {
    if (monitor) {
        pthread_mutex_destroy(&monitor->mutex);
        free(monitor);
    }
}

void monitor_begin_period(Monitor *monitor, Timer *timer) {
    timer_begin(timer);
}

void monitor_end_period(Monitor *monitor, Timer *timer,
                        const char *period_name) {
    int64_t duration = timer_end(timer);
    pthread_mutex_lock(&monitor->mutex);
    monitor->readings[monitor->total_readings].name = period_name;
    monitor->readings[monitor->total_readings].duration = duration;
    monitor->total_readings += 1;
    assert(monitor->total_readings < MAX_READINGS);
    pthread_mutex_unlock(&monitor->mutex);
}

void monitor_lock(Monitor *monitor) {
    pthread_mutex_lock(&monitor->mutex);
}

void monitor_unlock(Monitor *monitor) {
    pthread_mutex_unlock(&monitor->mutex);
}

const char *monitor_pull_reading(Monitor *monitor) {
    if (monitor->total_readings <= 0) {
        return NULL;
    }

    int i = --monitor->total_readings;
    double milliseconds = (double) monitor->readings[i].duration *
                          monitor->clock_frequency;
    sprintf(monitor->text_buffer, "%s: %f\n", monitor->readings[i].name,
            milliseconds);

    return monitor->text_buffer;
}
