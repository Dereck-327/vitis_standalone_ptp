#include "ptp_sync.h"
#include "../fml_udp_log.h"

#include <assert.h>
#include "timesync.h"

#define NS_SEC_PERIOD                  (1000000000)

typedef enum
{
    /* Event messages */
    SyncMsg_e                = 0x00,
    DelayRequest_e           = 0x01,
    PDelayRequest_e          = 0x02,
    PDelayResponse_e         = 0x03,

    /* General messages */
    FollowUpMsg_e            = 0x08,
    DelayResponse_e          = 0x09,
    PDelayResponseFollowUp_e = 0x0A,
    Announce_e               = 0x0B,
    Signaling_e              = 0x0C,
    Management_e             = 0x0D
} msg_type_e;

typedef struct __attribute__((packed))
{
    uint8_t seconds[6];
    uint8_t nanoseconds[4];
}
msg_time_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t origin_ts;
}
ptp_delay_req_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;

    msg_time_t origin_ts;

    uint16_t originUtcOffset;
    uint8_t _RESERVED_1;
    uint8_t gmPriority1;
    uint32_t gmClockQuality;
    uint8_t gmPriority2;
    uint8_t gmId[8];
    uint16_t stepsRemoved;
    uint8_t timeSource;

}
ptp_announce_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t origin_ts;
}
ptp_sync_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t precise_origin_ts;
}
ptp_follow_up_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t recv_ts;
    uint8_t req_port_id[10];

}
ptp_delay_resp_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t origin_ts;
    uint8_t reserved[10];

}
ptp_pdelay_req_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t recv_receipt_ts;
    uint8_t req_port_id[10];
}
ptp_pdelay_resp_msg_fmt_t;

typedef struct __attribute__((packed))
{
    ptp_msg_header_t header;
    msg_time_t resp_origin_ts;
    uint8_t req_port_id[10];
}
ptp_pdelay_resp_follow_up_msg_fmt_t;

static   read_systime_cb_t *sSyscTimeCb = NULL;  // callback
static   report_time_diff_cb_t *sReportDiffCb = NULL;
static   send_delay_req_cb_t *sSendDlyReqCb = NULL;

static   ptp_announce_msg_fmt_t*               pAnnounce           = NULL;
static   ptp_sync_msg_fmt_t*                   pSync               = NULL;
static   ptp_follow_up_msg_fmt_t*              pFollowUp           = NULL;
static   ptp_delay_resp_msg_fmt_t*             pDelayResp          = NULL;
static   ptp_pdelay_resp_msg_fmt_t*            pPDelayResp         = NULL;
static   ptp_pdelay_resp_follow_up_msg_fmt_t*  pPDelayRespFollwUp  = NULL;



void ptp_sync_register_gettime_cb(read_systime_cb_t cb)
{
  sSyscTimeCb = cb;
}

void ptp_sync_register_report_diff_cb(report_time_diff_cb_t cb)
{
    sReportDiffCb = cb;
}

void ptp_sync_register_delay_req_cb(send_delay_req_cb_t cb)
{
    sSendDlyReqCb = cb;
}

static ptp_timestamp_t ptp_sync_read_systime(void)
{
  assert(sSyscTimeCb && "Getting system time API is not registered!");
  return sSyscTimeCb();
}

// --------------------- TIME CALCULATION --------------------------------------

static ptp_timestamp_t ptp_time_add(ptp_timestamp_t *pt1, ptp_timestamp_t *pt2)
{
    static ptp_timestamp_t retVal;
    int64_t sec_result;
    int32_t ns_result;

    sec_result = pt1->seconds  + pt2->seconds;
    ns_result  = pt1->nanosec  + pt2->nanosec;

    retVal.seconds = sec_result;
    retVal.nanosec = ns_result;

    retVal.dts = (double)sec_result + (double)ns_result / NS_SEC_PERIOD;
    return retVal;
}


static void ptp_prepare_timestamp(ptp_timestamp_t* in, uint8_t* out)
{
    uint8_t* p = &out[5];
    for(int i = 0; i < 6; i++, p--)
    {
        *p = (in->seconds >> (i * 8)) & 0xFF;

    }
    p = &out[9];
    for(int i = 0; i < 4; i++, p--)
    {
        *p = (in->nanosec >> (i * 8)) & 0xFF;
    }

}

static void ptp_store_timestamp(uint8_t* timestamp, ptp_timestamp_t *out)
{
    uint64_t sec = 0;
    uint32_t nanosec = 0;
    for(int i = 0; i < 6; i++)
    {
        sec <<= 8;
        sec += timestamp[i];
    }
    out->seconds = sec;
    for(int i = 6; i < 10; i++)
    {
        nanosec <<= 8;
        nanosec += timestamp[i];
    }
    out->nanosec = nanosec;

}

static ptp_timestamp_t ptp_time_sub(ptp_timestamp_t *pt1, ptp_timestamp_t *pt2)
{
    ptp_timestamp_t ts;
    ts.seconds = -pt2->seconds;
    ts.nanosec = -pt2->nanosec;
    return ptp_time_add(pt1, &ts);
}

static ptp_timestamp_t ptp_calc_prop_delay(ptp_timestamp_t *pt1, ptp_timestamp_t *pt2)
{
    ptp_timestamp_t diff, result;

    diff = ptp_time_sub(pt2, pt1);

    if(diff.seconds % 2 == 0)
    {
        result.seconds = diff.seconds / 2;
        result.nanosec = diff.nanosec / 2;
    }
    else
    {
        result.seconds = (diff.seconds - 1) / 2;
        result.nanosec = (diff.nanosec / 2) + NS_SEC_PERIOD / 2;
    }

    result.dts = (double)result.seconds + (double)result.nanosec / NS_SEC_PERIOD;

    return result;
}

static void ptp_sycn_report_diff(int32_t offset, ptp_timestamp_t *sync_ts, ptp_timestamp_t *diff_ts)
{
    assert(sReportDiffCb && "Reporting API is not registered!");
    sReportDiffCb(offset, sync_ts, diff_ts);
}




// ------------------- CALLBACK FUNCTIONS --------------------------------------

static void ptp_sync_send_delay_req_event(ptp_msg_header_t* hdr, ptp_timestamp_t *ts)
{
    assert(sSendDlyReqCb && "Send delay request API is not registered");

    static ptp_delay_req_msg_fmt_t msg = {};

    memcpy(&msg.header, hdr, 4);
    ptp_prepare_timestamp(ts, (uint8_t*)&msg.origin_ts);

    msg.header.messageType = DelayRequest_e;
    msg.header.messageLen = __builtin_bswap16(sizeof(msg));
    msg.header.controlField = 5;
    msg.header.domainNumber = 0;
    msg.header._RESERVED_1 = 0x00;
    msg.header.flags = 0x0000;
    msg.header.correctionField = 0x00;

    sSendDlyReqCb(&msg, sizeof(msg));
}

static int32_t ptp_clock_offset(ptp_timestamp_t t3, ptp_timestamp_t t4, ptp_timestamp_t delay)
{
  int32_t offset = (t3.nanosec - t4.nanosec) + delay.nanosec;
  // xil_printf("offset :: %d\r\n", offset);
  return offset;
}

#if 0
int ptp_sync_interpreter(void *msg)
{
    ptp_msg_header_t* hdr = (ptp_msg_header_t*)msg;
    ptp_timestamp_t diff;
    ptp_timestamp_t prop_delay;
    ptp_timestamp_t synced_time;
    static ptp_timestamp_t t1, t2, sys_ts;
    static offset = 0;

#ifdef PTP_TEST
    // xil_printf("[%s]:: recv type :: %d\r\n", __func__, hdr->messageType);
#endif
    switch(hdr->messageType)
    {
    case SyncMsg_e:
      pSync = (ptp_sync_msg_fmt_t*)msg;
      break;

    case FollowUpMsg_e:
      pFollowUp = (ptp_follow_up_msg_fmt_t*)msg;
      t1 = ptp_sync_read_systime();
      ptp_sync_send_delay_req_event(hdr, &t1);
      break;

    case DelayResponse_e:
      t2 = ptp_sync_read_systime();
      pDelayResp = (ptp_delay_resp_msg_fmt_t*)msg;

      ptp_timestamp_t ptp_clock;
      ptp_store_timestamp((uint8_t*)&pDelayResp->recv_ts, &ptp_clock);

      prop_delay  = ptp_calc_prop_delay(&t1, &t2);
      synced_time = ptp_time_add(&ptp_clock, &prop_delay);  
      sys_ts      = ptp_sync_read_systime();
      diff        = ptp_time_sub(&synced_time, &sys_ts);
    //   offset      = ptp_clock_offset(t1, ptp_clock, prop_delay); // (t1 - ptp_clock) + prop_delay;
    // xil_printf("%d :: ", t2.nanosec - t1.nanosec);
      ptp_sycn_report_diff(offset, &synced_time, &diff);
      break;

    case PDelayResponse_e:
      pPDelayResp = (ptp_pdelay_resp_msg_fmt_t*)msg;
      break;

    case PDelayResponseFollowUp_e:
      pPDelayRespFollwUp = (ptp_pdelay_resp_follow_up_msg_fmt_t*)msg;
      break;

    case Announce_e:
      pAnnounce = (ptp_announce_msg_fmt_t*)msg;
      break;

    case Signaling_e:
        //Not implemented
        return PTP_MSG_ERR;
        break;

    case Management_e:
        //Not implemented
        return PTP_MSG_ERR;
        break;

    default:
        //Invalid message type
        return PTP_MSG_ERR;
        break;
    }

    return PTP_MSG_OK;

}

#else

static void ptp_time_except_2(ptp_timestamp_t *pt1)
{

    if(pt1->seconds % 2 == 0)
    {
        pt1->seconds = pt1->seconds / 2;
        pt1->nanosec = pt1->nanosec / 2;
    }
    else
    {
        pt1->seconds = (pt1->seconds - 1) / 2;
        pt1->nanosec = (pt1->nanosec / 2) + NS_SEC_PERIOD / 2;
    }
    return ;
}

int ptp_sync_interpreter(void *msg)
{
    ptp_msg_header_t* hdr = (ptp_msg_header_t*)msg;
    ptp_timestamp_t diff;
    ptp_timestamp_t prop_delay;
    ptp_timestamp_t synced_time;
    static ptp_timestamp_t t1, t2, t3, t4, sys_ts;
    static offset = 0;

#ifdef PTP_TEST
    // xil_printf("[%s]:: recv type :: %d\r\n", __func__, hdr->messageType);
#endif
    switch(hdr->messageType)
    {
    case SyncMsg_e:
      pSync = (ptp_sync_msg_fmt_t*)msg;
      t2 = ptp_sync_read_systime();
      break;

    case FollowUpMsg_e:
      pFollowUp = (ptp_follow_up_msg_fmt_t*)msg;
      ptp_store_timestamp((uint8_t*)&pFollowUp->precise_origin_ts, &t1);
      t3 = ptp_sync_read_systime();
      ptp_sync_send_delay_req_event(hdr, &t3);
      break;

    case DelayResponse_e:
      pDelayResp = (ptp_delay_resp_msg_fmt_t*)msg;
      sys_ts      = ptp_sync_read_systime();
      ptp_store_timestamp((uint8_t*)&pDelayResp->recv_ts, &t4);

      ptp_timestamp_t t2_t1 = ptp_time_sub(&t2, &t1);
      ptp_timestamp_t t4_t3 = ptp_time_sub(&t4, &t3);
      ptp_timestamp_t delay = ptp_time_add(&t2_t1, &t4_t3);
      ptp_time_except_2(&delay);

      ptp_timestamp_t offset = ptp_time_sub(&t2_t1, &t4_t3);
      ptp_time_except_2(&offset);

      
      prop_delay  = ptp_time_sub(&delay, &offset);
      synced_time = ptp_time_add(&t3, &prop_delay);  
      diff        = ptp_time_sub(&synced_time, &sys_ts);

      ptp_sycn_report_diff(0, &synced_time, &diff);
      break;

    case PDelayResponse_e:
      pPDelayResp = (ptp_pdelay_resp_msg_fmt_t*)msg;
      break;

    case PDelayResponseFollowUp_e:
      pPDelayRespFollwUp = (ptp_pdelay_resp_follow_up_msg_fmt_t*)msg;
      break;

    case Announce_e:
      pAnnounce = (ptp_announce_msg_fmt_t*)msg;
      break;

    case Signaling_e:
        //Not implemented
        return PTP_MSG_ERR;
        break;

    case Management_e:
        //Not implemented
        return PTP_MSG_ERR;
        break;

    default:
        //Invalid message type
        return PTP_MSG_ERR;
        break;
    }

    return PTP_MSG_OK;

}

#endif


