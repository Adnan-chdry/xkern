/*
 * ipv4.c - internet protocol v4 (tx + rx dispatch).
 */
#include "ipv4.h"
#include "ether.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "klog.h"

struct ipv4_hdr {
    u8  ver_ihl;
    u8  tos;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  proto;
    u16 checksum;
    u32 src;
    u32 dst;                    /* network order on the wire */
} __attribute__((packed));

u16 ip_checksum(const void *data, u32 len)
{
    const u8 *p = data;
    u32 sum = 0;

    for (u32 i = 0; i + 1 < len; i += 2)
        sum += ((u16)p[i] << 8) | p[i + 1];

    if (len & 1)
        sum += (u16)p[len - 1] << 8;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return NET_HTONS(~sum & 0xFFFF);
}

static void hdr_fill(struct ipv4_hdr *h, u32 src_host, u32 dst_host,
                     u8 proto, u16 plen)
{
    h->ver_ihl = 0x45;
    h->tos = 0;
    h->total_len = NET_HTONS((u16)(sizeof(*h) + plen));
    h->id = 0;
    h->flags_frag = NET_HTONS(0x4000);      /* DF */
    h->ttl = 64;
    h->proto = proto;
    h->checksum = 0;
    h->src = NET_HTONL(src_host);
    h->dst = NET_HTONL(dst_host);
    h->checksum = ip_checksum(h, sizeof(*h));
}

int ipv4_send(u32 dst_ip, u8 proto, const void *payload, u32 plen)
{
    u8 frame[NET_FRAMESZ];
    struct ipv4_hdr *h =
        (struct ipv4_hdr *)(frame + sizeof(struct eth_hdr));
    u8 mac[NET_ETH_ALEN];

    if (plen > NET_MTU - sizeof(*h))
        return -1;

    /* resolve next-hop: host on our subnet directly, else gateway */
    u32 nh = ((dst_ip ^ g_net_ip) & g_net_mask) ? g_net_gw : dst_ip;

    /* link-broadcast never needs ARP */
    if (dst_ip == 0xFFFFFFFF) {
        hdr_fill(h, g_net_ip, dst_ip, proto, (u16)plen);
        u8 *body = frame + sizeof(struct eth_hdr) + sizeof(*h);
        const u8 *src = payload;
        for (u32 i = 0; i < plen; i++)
            body[i] = src[i];
        return ether_send(net_bcast_mac(), ETHERTYPE_IPV4,
                          frame, sizeof(*h) + plen);
    }

    if (arp_lookup(nh, mac) != 0) {
        arp_request(nh);
        klog("net.ipv4", "arping %s for next hop",
             net_ipv4_str(nh, (char[16]){0}));
        return -1;              /* caller retries once cached */
    }

    hdr_fill(h, g_net_ip, dst_ip, proto, (u16)plen);

    u8 *body = frame + sizeof(struct eth_hdr) + sizeof(*h);
    const u8 *src = payload;
    for (u32 i = 0; i < plen; i++)
        body[i] = src[i];

    return ether_send(mac, ETHERTYPE_IPV4, frame, sizeof(*h) + plen);
}

void ipv4_rx(const void *pkt, u32 len)
{
    const struct ipv4_hdr *h = pkt;

    if (len < sizeof(*h))
        return;
    if ((h->ver_ihl >> 4) != 4 || (h->ver_ihl & 0xF) != 5)
        return;                 /* v4, no options */

    u16 tot = NET_NTOHS(h->total_len);
    if (tot < sizeof(*h) || tot > len)
        return;

    if (NET_NTOHL(h->dst) != g_net_ip &&
        !(g_net_ip == 0 && NET_NTOHL(h->dst) == 0xFFFFFFFF))
        return;

    if (ip_checksum(h, sizeof(*h)) != 0)
        return;

    const void *payload = pkt + sizeof(*h);
    u32 plen = tot - sizeof(*h);

    switch (h->proto) {
    case IP_PROTO_ICMP:
        icmp_rx(payload, plen, NET_NTOHL(h->src));
        break;
    case IP_PROTO_UDP:
        udp_rx(payload, plen, NET_NTOHL(h->src));
        break;
    default:
        break;
    }
}
