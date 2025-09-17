


#pragma once

#define PTP_TEST

typedef enum
{
    CLOCK_TYPE_NONE,
    CLOCK_TYPE_GENERAL_NMEA_GPS,
    CLOCK_TYPE_PTP,
    CLOCK_TYPE_GPTP
} clock_type;

typedef enum
{
    TS_NEVER,
    TS_COARSE,
    TS_FINE,
    TS_LOST
} timesync_state_e;


void timesync_init(clock_type type);