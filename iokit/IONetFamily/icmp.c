/*
 * icmp.c - echo replies (so the kernel is pingable).
 */
#include "icmp.h"
#include "ipv4.h"
#include "klog.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQ   8

struct icmp_hdr {
    u8  type;
    u8  code;
    u16 checksum;
    u16 id;
    u16 seq;
} __attribute__((packed));

void icmp_rx(const void *pkt, u32 len, u32 src_ip_host)
{
    const struct icmp_hdr *h = pkt;

    if (len < sizeof(*h))
        return;

    if (h->type == 0) {         /* echo reply (to our gateway pings) */
        klog("net.icmp", "echo reply from %s (id=%u seq=%u)",
             net_ipv4_str(src_ip_host, (char[16]){0}),
             NET_NTOHS(h->id), NET_NTOHS(h->seq));
        return;
    }

    if (h->type != ICMP_ECHO_REQ)
        return;

    klog("net.icmp", "echo request from %s (id=%u seq=%u)",
         net_ipv4_str(src_ip_host, (char[16]){0}),
         NET_NTOHS(h->id), NET_NTOHS(h->seq));

    /* reply = same packet, type flipped, checksum recomputed */
    u8 reply[NET_MTU];
    for (u32 i = 0; i < len; i++)
        reply[i] = ((const u8 *)pkt)[i];
    reply[0] = ICMP_ECHO_REPLY;

    struct icmp_hdr *r = (struct icmp_hdr *)reply;
    r->checksum = 0;
    r->checksum = ip_checksum(reply, len);

    ipv4_send(src_ip_host, IP_PROTO_ICMP, reply, len);
}

int icmp_echo_request(u32 dst_ip, u16 id, u16 seq)
{
    struct {
        struct icmp_hdr h;
        char            payload[8];
    } pkt;

    pkt.h.type = ICMP_ECHO_REQ;
    pkt.h.code = 0;
    pkt.h.checksum = 0;
    pkt.h.id = NET_HTONS(id);
    pkt.h.seq = NET_HTONS(seq);
    for (int i = 0; i < 8; i++)
        pkt.payload[i] = 'x';

    pkt.h.checksum = ip_checksum(&pkt, sizeof(pkt));

    return ipv4_send(dst_ip, IP_PROTO_ICMP, &pkt, sizeof(pkt));
}
