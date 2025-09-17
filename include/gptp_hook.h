

#pragma once

#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/def.h"
#include "netif/etharp.h"


err_t gptp_protocol_hook(struct pbuf *p, struct netif *netif);

#define LWIP_HOOK_UNKNOWN_ETH_PROTOCOL gptp_protocol_hook