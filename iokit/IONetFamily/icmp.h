/*
 * icmp.h - internet control message protocol.
 */
#pragma once
#include "ionet.h"
#include "ipv4.h"

void icmp_rx(const void *pkt, u32 len, u32 src_ip_host);
int  icmp_echo_request(u32 dst_ip, u16 id, u16 seq);
