/*
 * arp.h - address resolution protocol.
 */
#pragma once
#include "ionet.h"

void arp_init(void);
int  arp_lookup(u32 ip_host, u8 mac_out[NET_ETH_ALEN]);
int  arp_request(u32 ip_host);
void arp_rx(void *pkt, u32 len);
