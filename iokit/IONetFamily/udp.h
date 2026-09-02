/*
 * udp.h - user datagram protocol with port demux.
 */
#pragma once
#include "ionet.h"

typedef void (*udp_handler_t)(u32 src_ip, u16 src_port,
                              const void *data, u32 len);

int  udp_bind(u16 port, udp_handler_t fn);      /* fn=NULL unbinds */
int  udp_send(u32 dst_ip, u16 dst_port, u16 src_port,
              const void *data, u32 len);
void udp_rx(const void *pkt, u32 len, u32 src_ip);
