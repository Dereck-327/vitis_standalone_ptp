#pragma once

#include "xil_types.h"
#include <stdint.h>


#define FASTCODE __attribute__((section(".fastcode")))
#define ADJUST_ADD 0
#define ADJUST_SUBTRACT 1
#define TSU_US_IN_MS             (1000)


#define TSU_US_IN_SEC            (1000000UL)
#define TSU_NS_IN_MS             (1000000UL)
#define TSU_MS_IN_SEC            (1000)
#define TSU_US_IN_MS             (1000) // (1000)
#define TSU_NS_IN_US             (1000000UL)


typedef struct {
    u64 seconds;
    u32 nanosec;
} tsu_timestamp_t;



typedef union
{
    struct
    {
        uint32_t nanosec: 30;
        uint32_t reserved: 1;
        uint32_t direction: 1;
    };
    uint32_t word;
} tsu_adjustment;
_Static_assert(sizeof(tsu_adjustment) == sizeof(uint32_t), "invalid union size");


void init_tsu(void (*ttc_handle));
void tsu_start();
void tsu_stop();

void tsu_get_time(tsu_timestamp_t *out);
void tsu_set_time(tsu_timestamp_t *ts);

void tsu_clock_fine_tune(const int error_ns, int32_t offset, const int boundary);
u32 tsu_get_uptime_ms(void);
uint64_t tsu_get_tick_cnt(void);
void tsu_driver_adjust_time(tsu_adjustment adjust);
void tsu_systick_cb(void);

void print_tsu_local_time();

void read_tsu_local_time();

int8_t is_readable();