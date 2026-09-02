/*
 * udp.c - user datagram protocol.
 */
#include "udp.h"
#include "ipv4.h"
#include "klog.h"
#include "tsc.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

#define UDP_MAX_PORTS 8

struct udp_ent {
    u16          port;
    udp_handler_t fn;
};

static struct udp_ent ports[UDP_MAX_PORTS];

void udp_init(void)
{
    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        ports[i].port = 0;
        ports[i].fn = NULL;
    }
}

struct udp_hdr {
    u16 src_port;
    u16 dst_port;
    u16 len;
    u16 checksum;
} __attribute__((packed));

int udp_bind(u16 port, udp_handler_t fn)
{
    int slot = -1;

    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (ports[i].port == port) {
            ports[i].fn = fn;
            return 0;
        }
        if (!ports[i].port && slot < 0)
            slot = i;
    }
    if (slot < 0 || !fn)
        return -1;

    ports[slot].port = port;
    ports[slot].fn = fn;
    klog("net.udp", "bound port %u", port);
    return 0;
}

void udp_rx(const void *pkt, u32 len, u32 src_ip_host)
{
    const struct udp_hdr *h = pkt;

    if (len < sizeof(*h))
        return;

    u16 dport = NET_NTOHS(h->dst_port);
    u16 ulen = NET_NTOHS(h->len);

    if (ulen < sizeof(*h) || ulen > len)
        return;

    for (int i = 0; i < UDP_MAX_PORTS; i++) {
        if (ports[i].port == dport && ports[i].fn) {
            ports[i].fn(src_ip_host, NET_NTOHS(h->src_port),
                        pkt + sizeof(*h), ulen - sizeof(*h));
            return;
        }
    }
}

/*
 * UDP with pseudo-header checksum.  IPv4 layer prepends its header;
 * we build the full UDP segment here and hand it to ipv4_send().
 */
int udp_send(u32 dst_ip, u16 dst_port, u16 src_port,
             const void *data, u32 len)
{
    static u8 seg[NET_MTU];
    struct udp_hdr h;
    u32 total = sizeof(h) + len;

    if (total > NET_MTU)
        return -1;

    /* pick an ephemeral-ish source port derived from dst */
    if (!src_port)
        src_port = (u16)(0xC000 ^ (u32)tsc_ms());

    h.src_port = NET_HTONS(src_port);
    h.dst_port = NET_HTONS(dst_port);
    h.len = NET_HTONS((u16)total);
    h.checksum = 0;

    u8 *body = seg + sizeof(h);
    const u8 *s = data;
    for (u32 i = 0; i < len; i++)
        body[i] = s[i];

    for (u32 i = 0; i < sizeof(h); i++)
        ((u8 *)seg)[i] = ((u8 *)&h)[i];

    /*
     * Pseudo header sum folded into checksum field:
     * src ip + dst ip + zero + proto + udp len
     */
    u32 sum = 0;
    sum += (g_net_ip >> 16) & 0xFFFF;
    sum += g_net_ip & 0xFFFF;
    sum += (dst_ip >> 16) & 0xFFFF;
    sum += dst_ip & 0xFFFF;
    sum += NET_HTONS(IP_PROTO_UDP);
    sum += NET_HTONS((u16)total);

    for (u32 i = 0; i + 1 < total; i += 2)
        sum += ((u16)seg[i] << 8) | seg[i + 1];
    if (total & 1)
        sum += (u16)seg[total - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    h.checksum = NET_HTONS(~sum & 0xFFFF);

    for (u32 i = 0; i < sizeof(h); i++)
        seg[i] = ((u8 *)&h)[i];

    return ipv4_send(dst_ip, IP_PROTO_UDP, seg, total);
}
