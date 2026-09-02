/*
 * netdev.h - NIC driver interface implemented by e1000.c.
 */
#pragma once
#include "ionet.h"

/* driver entry points (set by the active driver at init) */
struct netdev_ops {
    int  (*send)(const void *frame, u32 len);
    int  (*poll_rx)(void);          /* drains the rx queue into ether_rx */
};

extern struct netdev_ops *g_netdev;

int netdev_send(const void *frame, u32 len);
