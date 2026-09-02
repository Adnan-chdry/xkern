/*
 * ipv4.h - internet protocol v4.
 */
#pragma once
#include "ionet.h"

#define IP_PROTO_ICMP   1
#define IP_PROTO_UDP    17

u16 ip_checksum(const void *data, u32 len);

int  ipv4_send(u32 dst_ip, u8 proto, const void *payload, u32 plen);
void ipv4_rx(const void *pkt, u32 len);
