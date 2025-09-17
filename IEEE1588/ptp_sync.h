#pragma once

#include <stdint.h>
#include <stddef.h>


#define PTP_MSG_OK                     ( 0)
#define PTP_MSG_ERR                    (-1)

typedef struct
{
    int64_t seconds;
    int32_t nanosec;
    double dts;
} ptp_timestamp_t;

typedef struct __attribute__((packed))
{
    uint8_t messageType : 4;
    uint8_t majorSdoId  : 4;
    uint8_t versionPTP  : 4;
    uint8_t minorVerPtp : 4;
    uint16_t messageLen;
    uint8_t domainNumber;
    uint8_t _RESERVED_1;
    uint16_t flags;
    uint64_t correctionField;
    uint8_t _RESERVED_2[4];
    uint8_t srcPortId[10];
    uint16_t seqId;
    uint8_t controlField;
    uint8_t logMsgInterval;
}
ptp_msg_header_t;

typedef ptp_timestamp_t (read_systime_cb_t)(void);
typedef void (report_time_diff_cb_t)(int32_t, ptp_timestamp_t*, ptp_timestamp_t*);
typedef void (send_delay_req_cb_t)(void*, size_t);

void ptp_sync_register_gettime_cb(read_systime_cb_t cb);
void ptp_sync_register_report_diff_cb(report_time_diff_cb_t cb);
void ptp_sync_register_delay_req_cb(send_delay_req_cb_t cb);



int ptp_sync_interpreter(void *msg);
