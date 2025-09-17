

#pragma once

#include "timesync_private.h"
#include "lwip/netif.h"

void handle_gptp();
void gptp_init(const struct timesync_init_param* init_param);

void init_gptp_task();

void enable_mac_multicast(struct netif *netif0);

void open_xemacps_promiscuous(struct netif *netif0);
void close_xemacps_promiscuous(struct netif *netif0);
