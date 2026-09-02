/*
 * arp.c - address resolution protocol (ipv4 over ethernet).
 */
#include "arp.h"
#include "ether.h"
#include "tsc.h"
#include "klog.h"

#define ARP_HTYPE_ETHER 1
#define ARP_OP_REQUEST  1
#define ARP_OP_REPLY    2

#define ARP_CACHE_SIZE  16
#define ARP_CACHE_TTL_MS 600000

struct arp_cache_ent {
    u32 ip;
    u8  mac[NET_ETH_ALEN];
    u64 stamp;
    u8  used;
};

static struct arp_cache_ent cache[ARP_CACHE_SIZE];

struct arp_pkt {
    u16 htype;
    u16 ptype;
    u8  hlen;
    u8  plen;
    u16 op;
    u8  sha[NET_ETH_ALEN];
    u32 spa;                    /* network order on the wire */
    u8  tha[NET_ETH_ALEN];
    u32 tpa;
} __attribute__((packed));

void arp_init(void)
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++)
        cache[i].used = 0;
}

int arp_lookup(u32 ip_host, u8 mac_out[NET_ETH_ALEN])
{
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].used && cache[i].ip == NET_HTONL(ip_host)) {
            for (int k = 0; k < NET_ETH_ALEN; k++)
                mac_out[k] = cache[i].mac[k];
            return 0;
        }
    }
    return -1;
}

int arp_request(u32 ip_host)
{
    u8 frame[NET_FRAMESZ];
    struct arp_pkt *a = (struct arp_pkt *)(frame + sizeof(struct eth_hdr));

    a->htype = NET_HTONS(ARP_HTYPE_ETHER);
    a->ptype = NET_HTONS(ETHERTYPE_IPV4);
    a->hlen = NET_ETH_ALEN;
    a->plen = NET_IPV4_ALEN;
    a->op = NET_HTONS(ARP_OP_REQUEST);
    for (int i = 0; i < NET_ETH_ALEN; i++)
        a->sha[i] = g_net_mac.b[i];
    a->spa = NET_HTONL(g_net_ip);
    for (int i = 0; i < NET_ETH_ALEN; i++)
        a->tha[i] = 0;
    a->tpa = NET_HTONL(ip_host);

    return ether_send(net_bcast_mac(), ETHERTYPE_ARP, frame, sizeof(*a));
}

static void arp_cache_add(const u8 sha[NET_ETH_ALEN], u32 spa_wire)
{
    int slot = -1;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (cache[i].used && cache[i].ip == spa_wire) {
            slot = i;
            break;
        }
        if (!cache[i].used && slot < 0)
            slot = i;
    }
    if (slot < 0)
        return;                 /* table full: drop */

    cache[slot].ip = spa_wire;
    for (int i = 0; i < NET_ETH_ALEN; i++)
        cache[slot].mac[i] = sha[i];
    cache[slot].stamp = tsc_ms();
    cache[slot].used = 1;
}

void arp_rx(void *pkt, u32 len)
{
    struct arp_pkt *a = pkt;

    if (len < sizeof(*a))
        return;
    if (NET_NTOHS(a->ptype) != ETHERTYPE_IPV4)
        return;

    arp_cache_add(a->sha, a->spa);

    if (NET_NTOHS(a->op) == ARP_OP_REQUEST && NET_NTOHL(a->tpa) == g_net_ip) {
        /* reply: swap fields */
        u8 tha[NET_ETH_ALEN];
        for (int i = 0; i < NET_ETH_ALEN; i++)
            tha[i] = a->sha[i];

        a->op = NET_HTONS(ARP_OP_REPLY);
        for (int i = 0; i < NET_ETH_ALEN; i++) {
            a->tha[i] = tha[i];
            a->sha[i] = g_net_mac.b[i];
        }
        u32 tmp = a->spa;
        a->spa = a->tpa;
        a->tpa = tmp;

        ether_send(tha, ETHERTYPE_ARP, pkt, sizeof(*a));
    }
}
