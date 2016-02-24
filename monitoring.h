#ifndef MONITORING_H_
#define MONITORING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Monitor;
typedef struct Monitor Monitor;

typedef struct {
    int64_t start;
} Timer;

Monitor *monitor_create();
void monitor_destroy(Monitor *monitor);
void monitor_begin_period(Monitor *monitor, Timer *timer);
void monitor_end_period(Monitor *monitor, Timer *timer, const char *period_name);

void monitor_lock(Monitor *monitor);
void monitor_unlock(Monitor *monitor);
const char *monitor_pull_reading(Monitor *monitor);

#define BEGIN_MONITORING(monitor, period_name) \
    Timer timer_##period_name; \
    monitor_begin_period((monitor), &timer_##period_name);

#define END_MONITORING(monitor, period_name) \
    monitor_end_period((monitor), &timer_##period_name, #period_name)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MONITORING_H_ */
