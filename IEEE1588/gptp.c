#include "gptp.h"
#include "ttc_tsu/tsu_time.h"
#include "../fml_udp_log.h"
#include "ptp_sync.h"
#include "ptp.h"

#include "xstatus.h"
#include "lwip/raw.h"
#include "lwip/ip.h"
#include "netif/ethernet.h"
#include "netif/xemacpsif.h"

static const struct timesync_init_param* init_params;

extern struct ip4_addr group;

#ifndef ETHERNET_HEADER_LEN  
#define ETHERNET_HEADER_LEN  (14)
#endif

static const u8_t gptp_eth_header[ETHERNET_HEADER_LEN] = {
    0x01, 0x1B, 0x19, 0x00, 0x00, 0x00, // dst_mac: gPTP多播
    0x00, 0x0a, 0x35, 0x00, 0x01, 0x02, // src_mac: 本机MAC
    0x88, 0xF7                          // type: PTP
};

extern struct netif server_netif;
static struct netif* netif0 = &server_netif;

void handle_gptp()
{
}



static void gptp_msg_interpreter(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  int stat = ptp_sync_interpreter(p->payload);
  pbuf_free(p);

  if(stat != PTP_MSG_OK)
      xil_printf("Invalid GPTP message received!\r\n");
}

static void bind_raw_sock()
{
//  struct raw_pcb *gptp_raw_pcb;
//  gptp_raw_pcb = raw_new(IP_PROTO_IGMP);
//  if (gptp_raw_pcb)
//  {
//    raw_bind(gptp_raw_pcb, IP_ADDR_ANY);
//    raw_recv(gptp_raw_pcb, gptp_msg_interpreter, NULL);
//  }
//
}

static void gptp_send_delay_request(void* msg, size_t msg_len)
{

  u8_t frame[ETHERNET_HEADER_LEN + msg_len];
  memcpy(frame, gptp_eth_header, ETHERNET_HEADER_LEN); // 拷贝头
  memcpy(&frame[ETHERNET_HEADER_LEN], msg, msg_len);   // 拷贝数据

  struct pbuf *p = pbuf_alloc(PBUF_RAW, ETHERNET_HEADER_LEN + msg_len, PBUF_RAM);
  if (p) {
      memcpy(p->payload, frame, ETHERNET_HEADER_LEN + msg_len);
      netif0->linkoutput(netif0, p);
      pbuf_free(p);
  }

}

void init_gptp_task()
{
  int status = XST_SUCCESS;
  init_tsu(handle_gptp);

  ptp_sync_register_gettime_cb(get_systime);
  ptp_sync_register_report_diff_cb(ptp_process_time_diff);
  ptp_sync_register_delay_req_cb(gptp_send_delay_request);
  // bind_raw_sock();

  group.addr = PTP_MULTICAST_IPV4_ADDR32;

  ptp_start_sync();

}

static uint32_t ethernet_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    static uint32_t table[256];
    static int table_init = 0;
    if (!table_init) {
        for (int i = 0; i < 256; ++i) {
            uint32_t v = (uint32_t)i;
            for (int j = 0; j < 8; ++j)
                v = (v & 1) ? (0xEDB88320U ^ (v >> 1)) : (v >> 1);
            table[i] = v;
        }
        table_init = 1;
    }

    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static void mac_to_hash_table(const uint8_t mac[6], uint8_t hash_table[8])
{
    memset(hash_table, 0, 8);
    uint32_t crc = ethernet_crc32(mac, 6);
    /* 取 CRC 的高 6 位作为索引（常见以太网 hash 方式） */
    uint8_t index = (crc >> 26) & 0x3F; /* 0..63 */
    hash_table[index >> 3] |= (1U << (index & 7));
}

void enable_mac_multicast(struct netif *netif0)
{
  struct xemac_s *xemac = (struct xemac_s *)(netif0->state);
  xemacpsif_s *xemacpsif = (xemacpsif_s *)(xemac->state);

  uint8_t ptp_multicast_mac[6] = {0x01, 0x1B, 0x19, 0x00, 0x00, 0x00};
  XEmacPs_Stop(&xemacpsif->emacps);

  XEmacPs_SetOptions(&xemacpsif->emacps, XEMACPS_MULTICAST_OPTION);

  XEmacPs_SetHash(&xemacpsif->emacps, ptp_multicast_mac);
  reset_dma(xemac);
  XEmacPs_Start(&xemacpsif->emacps);

}

void open_xemacps_promiscuous(struct netif *netif0)
{
  int err = 0;
  struct xemac_s *xemac = (struct xemac_s *)(netif0->state);
  xemacpsif_s *xemacpsif = (xemacpsif_s *)(xemac->state);

  err = XEmacPs_SetOptions(&xemacpsif->emacps, XEMACPS_PROMISC_OPTION);
  if (err)
  {
    xil_printf("open_xemacps_promiscuous failed :: %d\r\n", err);
  } else {
    xil_printf("open_xemacps_promiscuous success\r\n");
  }
  
}

void close_xemacps_promiscuous(struct netif *netif0)
{
  struct xemac_s *xemac = (struct xemac_s *)(netif0->state);
  xemacpsif_s *xemacpsif = (xemacpsif_s *)(xemac->state);
  XEmacPs_ClearOptions(&xemacpsif->emacps, XEMACPS_PROMISC_OPTION);
}

void gptp_init(const struct timesync_init_param *init_param)
{
  enable_mac_multicast(netif0);

  set_gptp_param(init_param);
  init_gptp_task();
}

