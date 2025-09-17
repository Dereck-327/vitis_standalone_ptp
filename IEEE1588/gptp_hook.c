

#include "gptp_hook.h"
#include "../fml_udp_log.h"

#include "ptp_sync.h"

#ifndef ETHERNET_HEADER_LEN  
#define ETHERNET_HEADER_LEN  (14)
#endif


static ptp_sync(struct pbuf *p)
{
#if 0
    xil_printf("PTP payload: ");
    u8_t *payload = (u8_t *)p->payload;
    for (int i = 0; i < p->len; i++) {
        xil_printf("%02x ", payload[i]);
    }
    xil_printf("\r\n");
#endif
    u8_t *ptp_data = (u8_t *)p->payload + ETHERNET_HEADER_LEN; // 跳过以太网头
    int stat = ptp_sync_interpreter(ptp_data);
    if(stat != PTP_MSG_OK)
      xil_printf("Invalid GPTP message received!\r\n");
}

err_t gptp_protocol_hook(struct pbuf *p, struct netif *netif)
{
    struct eth_hdr *ethhdr = (struct eth_hdr *)p->payload;
    switch (lwip_htons(ethhdr->type))
    {
    case ETHTYPE_PTP:
        /* code */
        ptp_sync(p);
        break;
    
    default:
        xil_printf("PTP packet protocol unknow \r\n");
        break;
    }
    pbuf_free(p);
    return ERR_OK; 
}