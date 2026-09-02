/*
 * ether.h - ethernet II framing.
 */
#pragma once
#include "ionet.h"
#include "netdev.h"

#define ETHERTYPE_IPV4  0x0800
#define ETHERTYPE_ARP   0x0806

struct eth_hdr {
    u8  dst[NET_ETH_ALEN];
    u8  src[NET_ETH_ALEN];
    u16 type;
} __attribute__((packed));

int         ether_send(const u8 dst[NET_ETH_ALEN], u16 ethertype,
                       void *frame, u32 plen);
void        ether_rx(void *frame, u32 len);
const u8   *net_bcast_mac(void);
