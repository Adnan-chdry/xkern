/*
 * ionet.c - IONetFamily entry point: driver bring-up, stack init,
 *           periodic polling and the developer-facing io_service hook.
 */
#include "ionet.h"
#include "ether.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "dhcp.h"
#include "e1000.h"
#include "tsc.h"
#include "klog.h"
#include "IOServiceFamily/io_service.h"


#ifndef NULL
#define NULL ((void *)0)
#endif

/* interface state */
struct net_mac g_net_mac = { { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 } };
u32 g_net_ip   = 0;
u32 g_net_mask = 0xFFFFFF00;    /* /24 default */
u32 g_net_gw   = 0;

struct netdev_ops *g_netdev = NULL;

/* developer registry hook: registered via io_service_register() */
io_service_t ionet_service = {
    .name = "com.xkern.net",
    .desc = "e1000 NIC + ethernet/ARP/IPv4/ICMP/UDP/DHCP stack (polling)",
    .init = ionet_init,
    .exit = ionet_exit,
};

const char *net_ipv4_str(u32 ip, char out[16])
{
    u8 a = (ip >> 24) & 0xFF, b = (ip >> 16) & 0xFF;
    u8 c = (ip >> 8) & 0xFF, d = ip & 0xFF;

    /* tiny u8->dec */
    char *p = out;
    u8 v[4] = { a, b, c, d };

    for (int i = 0; i < 4; i++) {
        if (v[i] >= 100) *p++ = '0' + v[i] / 100;
        if (v[i] >= 10)  *p++ = '0' + (v[i] / 10) % 10;
        *p++ = '0' + v[i] % 10;
        if (i < 3) *p++ = '.';
    }
    *p = '\0';
    return out;
}

int netdev_send(const void *frame, u32 len)
{
    if (!g_netdev || !g_netdev->send)
        return -1;
    return g_netdev->send(frame, len);
}

/* one-shot gateway ping after DHCP completes (end-to-end smoke test) */
static u64 ping_next_ms;
static int pings_sent;

int ionet_init(void)
{
    arp_init();
    udp_init();

    if (e1000_init() != 0) {
        klog("net", "no NIC, network stack idle");
        return IOSVC_OK;        /* not fatal: other services continue */
    }

    dhcp_init();
    dhcp_start();

    klog("net", "IONetFamily up (polling)");
    return IOSVC_OK;
}

void ionet_exit(void)
{
    e1000_exit();
}

void ionet_poll(void)
{
    if (!g_netdev)
        return;

    e1000_poll();

    if (!dhcp_done()) {
        dhcp_poll();
        return;
    }

    /* post-lease: ping the gateway every 10 s (stops after 3) */
    if (g_net_gw && pings_sent < 3 && tsc_ms() >= ping_next_ms) {
        icmp_echo_request(g_net_gw, 0xBEEF, ++pings_sent);
        klog("net.icmp", "ping %s seq=%d",
             net_ipv4_str(g_net_gw, (char[16]){0}), pings_sent);
        ping_next_ms = tsc_ms() + 10000;
    }
}
