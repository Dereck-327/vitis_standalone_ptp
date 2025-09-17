
#pragma once

#include <time.h>
#include <stdint.h>

typedef void(*time_received_t)(const time_t timestamp, const uint32_t nanoseconds);
typedef void(*timeservice_notification_t)();
typedef void(*timeservice_error_report_t)(const int error_ns, const int avg_error_ns);

struct timesync_init_param
{
    // const char* timesource_name;  // 
    int task_priority;
    int coarse_timeout_sec;
    int fine_timeout_sec;
    int pps_irq_priority;
    time_received_t coarse_time_received;
    time_received_t fine_time_received;
    timeservice_notification_t pps_received;
    timeservice_notification_t coarse_timeout;
    timeservice_notification_t fine_timeout;
    timeservice_notification_t housekeeping;
    timeservice_notification_t cleanup_function;
    timeservice_error_report_t error_report;
};
