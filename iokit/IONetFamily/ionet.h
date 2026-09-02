/*
 * ionet.h - IONetFamily: network stack for XKERN (ethernet/ARP/IPv4/
 *           ICMP/UDP/DHCP on top of an IONet NIC driver such as e1000).
 */
#pragma once
#include "types.h"

#define NET_ETH_ALEN        6
#define NET_IPV4_ALEN       4
#define NET_MTU             1500
#define NET_FRAMESZ         (NET_MTU + 14 + 4)      /* eth hdr + fcs slack */

/* big-endian helpers */
#define NET_HTONS(x) ((((u16)(x) & 0xFF) << 8) | (((u16)(x) >> 8) & 0xFF))
#define NET_NTOHS(x) NET_HTONS(x)
#define NET_HTONL(x) (((u32)(x) >> 24) | (((u32)(x) & 0xFF0000) >> 8) | \
                      (((u32)(x) & 0xFF00) << 8) | ((u32)(x) << 24))
#define NET_NTOHL(x) NET_HTONL(x)

struct net_mac { u8 b[NET_ETH_ALEN]; };

extern struct net_mac g_net_mac;
extern u32 g_net_ip;            /* host order */
extern u32 g_net_mask;          /* host order */
extern u32 g_net_gw;            /* host order */

int  ionet_init(void);          /* bring up driver + stack (io_service) */
void ionet_exit(void);
void ionet_poll(void);          /* call periodically (PIT tick) */

/* io_service descriptor for the IOServiceFamily registry */
extern struct io_service ionet_service;

const char *net_ipv4_str(u32 ip, char out[16]);
