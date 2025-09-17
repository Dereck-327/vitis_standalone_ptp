#pragma once

#include "timesync_private.h"
#include "ptp_sync.h"

#define bool	_Bool
#if defined __STDC_VERSION__ && __STDC_VERSION__ > 201710L
#define true	((_Bool)+1u)
#define false	((_Bool)+0u)
#else
#define true	1
#define false	0
#endif

#define PTP_MULTICAST_IPV4_ADDR32      (0x810100E0)     //224.0.1.129


void handle_ptp();
void ptp_init(const struct timesync_init_param* init_param);
void ptp_cleanup(void);
ptp_timestamp_t get_systime(void);
void ptp_process_time_diff(int32_t offset, ptp_timestamp_t *sync_ts, ptp_timestamp_t *diff_ts);

void ptp_start_sync(void);

void set_gptp_param(const struct timesync_init_param *init_param);