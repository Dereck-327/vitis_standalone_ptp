
#include "ptp.h"

#include "ptp_sync.h"
#include "ttc_tsu/tsu_time.h"
#include "timesync_private.h"

#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "xstatus.h"
#include "../fml_udp_log.h"


#define IPV4_BYTE(address,byte)        ((address>>(byte*8))&0xFF)

#define PTP_EVENT_MSG_PORT             (319)
#define PTP_GEN_MSG_PORT               (320)

#define PTP_MAX_ERR_NS                 (500) //+-1us hysteresis


#define TASK_DELAY                     (800)

#define PTP_KP_DEN          (0.00001)
#define PTP_STEP_THRESHOLD_NS  (1000000)

static const struct timesync_init_param* init_params;


static bool stop = false;
// static int last_sync_age_ms;
static int timeout_ms;

extern struct netif server_netif;
struct ip4_addr group;
struct udp_pcb *ptp_pcb;



ptp_timestamp_t get_systime(void)
{
  ptp_timestamp_t ts;
  tsu_timestamp_t tsu;

  tsu_get_time(&tsu);
  ts.seconds = tsu.seconds;
  ts.nanosec = tsu.nanosec;
  return ts;
}


void ptp_start_sync(void)
{
    igmp_joingroup_netif(&server_netif, &group);

    xil_printf("Joining to multicast group: %d.%d.%d.%d\r\n",
                 IPV4_BYTE(group.addr, 0),
                 IPV4_BYTE(group.addr, 1),
                 IPV4_BYTE(group.addr, 2),
                 IPV4_BYTE(group.addr, 3)
                );
    xil_printf("PTP sync started\r\n");
}

void set_gptp_param(const struct timesync_init_param *init_param) 
{
    init_params = init_param;
}

void ptp_process_time_diff(int32_t offset, ptp_timestamp_t *sync_ts, ptp_timestamp_t *diff_ts)
{
    static int sync_diff_integrated_ns = 0;

    if(diff_ts->seconds != 0 )
    {
        if(init_params->coarse_time_received)
        {
            init_params->coarse_time_received(sync_ts->seconds, sync_ts->nanosec);
            sync_diff_integrated_ns = 0;
        }
    }
    else
    {
        // const int corr = (sync_diff_integrated_ns - diff_ts->nanosec) / 10;
        // sync_diff_integrated_ns += corr;
        sync_diff_integrated_ns += diff_ts->nanosec;
        // const int corr = (sync_diff_integrated_ns - diff_ts->nanosec) / 16; // / 10;
        // sync_diff_integrated_ns += diff_ts->nanosec * PTP_KP_DEN;
        // tsu_clock_fine_tune(sync_diff_integrated_ns, offset, PTP_MAX_ERR_NS);
        tsu_clock_fine_tune(diff_ts->nanosec, sync_diff_integrated_ns, PTP_MAX_ERR_NS);
        tsu_systick_cb();
        if(init_params->fine_time_received)
            init_params->fine_time_received(0, 0);    //notify only
        // last_sync_age_ms = 0;

        // xil_printf("diff(%-10d) :: %-2d  %-10d\r\n", sync_diff_integrated_ns, diff_ts->seconds, diff_ts->nanosec);
        if(init_params->error_report)
            init_params->error_report(diff_ts->nanosec, sync_diff_integrated_ns);
    }
    xil_printf("diff(%-10d) :: %-2d  %-10d \r\n", sync_diff_integrated_ns, diff_ts->seconds, diff_ts->nanosec);

}
// static void ptp_process_time_diff(int32_t offset, ptp_timestamp_t *sync_ts, ptp_timestamp_t *diff_ts)
// {
//     static int sync_diff_integrated_ns = 0;

//     if(diff_ts->seconds > 1 || diff_ts->seconds <= -1)
//     {
//         if(init_params->coarse_time_received)
//             init_params->coarse_time_received(sync_ts->seconds, sync_ts->nanosec);
//     }
//     else
//     {
//         const int corr = (sync_diff_integrated_ns - diff_ts->nanosec) / 10;
//         sync_diff_integrated_ns -= corr;
//         // tsu_clock_fine_tune(sync_diff_integrated_ns, diff_ts->nanosec, PTP_MAX_ERR_NS);
//         tsu_clock_fine_tune(diff_ts->nanosec, offset, PTP_MAX_ERR_NS);
//         // adjust systime 
//         tsu_systick_cb();
//         if(init_params->fine_time_received)
//             init_params->fine_time_received(0, 0);    //notify only
//         // last_sync_age_ms = 0;

//         xil_printf("diff(%-10d) :: %-2d  %-10d\r\n", sync_diff_integrated_ns, diff_ts->seconds, diff_ts->nanosec);
//         if(init_params->error_report)
//             init_params->error_report(diff_ts->nanosec, sync_diff_integrated_ns);
//     }
// }

static void ptp_send_delay_request(void* msg, size_t msg_len)
{
    struct udp_pcb *pcb;
    struct pbuf *p;

    pcb = udp_new();
    p = pbuf_alloc(PBUF_TRANSPORT, msg_len, PBUF_RAM);

    p->len = msg_len;
    p->tot_len = msg_len;
    memcpy(p->payload, msg, msg_len);
    udp_sendto(pcb, p, &group, PTP_EVENT_MSG_PORT);
    pbuf_free(p);
    udp_remove(pcb);
}

static void ptp_msg_interpreter(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    int stat = ptp_sync_interpreter(p->payload);
    pbuf_free(p);

    if(stat != PTP_MSG_OK)
        xil_printf("Invalid PTP message received!\r\n");
}

static void bind_udp_receiver()
{
    static bool udp_bound = false;
    struct udp_pcb *pcbEventMsg;
    struct udp_pcb *pcbGenMsg;

    if(udp_bound)
        return;
    udp_bound = true;

    pcbEventMsg = udp_new();
    if(pcbEventMsg)
    {
        udp_bind(pcbEventMsg, IP_ANY_TYPE, PTP_EVENT_MSG_PORT);
        udp_recv(pcbEventMsg, ptp_msg_interpreter, NULL);
        xil_printf("UDP listener for PTP on port %d\r\n", PTP_EVENT_MSG_PORT);
    }

    pcbGenMsg = udp_new();
    if(pcbGenMsg)
    {
        udp_bind(pcbGenMsg, IP_ANY_TYPE, PTP_GEN_MSG_PORT);
        udp_recv(pcbGenMsg, ptp_msg_interpreter, NULL);
        xil_printf("UDP listener for PTP on port %d\r\n", PTP_GEN_MSG_PORT);
    }
}



void handle_ptp()
{
#if 1
  if(init_params->housekeeping)
      init_params->housekeeping();

//   if(init_params->coarse_timeout)
//       init_params->coarse_timeout();
//   if(init_params->fine_timeout)
//       init_params->fine_timeout();
//  tsu_systick_cb();
#endif
    // print_tsu_local_time();
}

void ptp_init(const struct timesync_init_param *init_param)
{
  init_params = init_param;
  init_ptp_task();
}

void ptp_cleanup(void)
{
  stop = true;
}


int init_ptp_task()
{

  int status = XST_SUCCESS;
  init_tsu(handle_ptp);
//   tsu_start();
  ptp_sync_register_gettime_cb(get_systime);
  ptp_sync_register_report_diff_cb(ptp_process_time_diff);
  ptp_sync_register_delay_req_cb(ptp_send_delay_request);
  bind_udp_receiver();

//   group.addr = PTP_MULTICAST_IPV4_ADDR32;
  IP4_ADDR(&group, 224, 0, 1, 129);
  ptp_start_sync();

  return status;
}
