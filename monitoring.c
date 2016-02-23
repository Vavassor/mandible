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

#define MAX_READINGS 64

struct Monitor {
    struct {
        const char *name;
        int64_t duration;
    } readings[MAX_READINGS];
    pthread_mutex_t mutex;
    int total_readings;
};

Monitor *monitor_create() {
    Monitor *monitor = calloc(1, sizeof(Monitor));
    pthread_mutex_init(&monitor->mutex, NULL);
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

void monitor_dump(Monitor *monitor) {
    pthread_mutex_lock(&monitor->mutex);
#if 0
    int i;
    for (i = 0; i < monitor->total_readings; ++i) {
        printf("%s: %lli\n", monitor->readings[i].name,
               (long long) monitor->readings[i].duration);
    }
#endif
    monitor->total_readings = 0;
    pthread_mutex_unlock(&monitor->mutex);
}
