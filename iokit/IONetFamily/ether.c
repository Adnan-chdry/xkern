/*
 * ether.c - ethernet II framing.
 */
#include "ether.h"
#include "arp.h"
#include "ipv4.h"
#include "klog.h"

static const u8 bcast[NET_ETH_ALEN] =
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

const u8 *net_bcast_mac(void)
{
    return bcast;
}

/*
 * Build an ethernet header in place and hand the frame to the NIC.
 * payload must already start at frame+14 with room for the header.
 */
int ether_send(const u8 dst[NET_ETH_ALEN], u16 ethertype,
               void *frame, u32 plen)
{
    struct eth_hdr *h = (struct eth_hdr *)frame;

    for (int i = 0; i < NET_ETH_ALEN; i++) {
        h->dst[i] = dst[i];
        h->src[i] = g_net_mac.b[i];
    }
    h->type = NET_HTONS(ethertype);

    return netdev_send(frame, plen + sizeof(*h));
}

void ether_rx(void *frame, u32 len)
{
    struct eth_hdr *h = (struct eth_hdr *)frame;

    if (len < sizeof(struct eth_hdr) + 1)
        return;

    u16 type = NET_NTOHS(h->type);
    u8  *payload = frame + sizeof(*h);
    u32  plen = len - sizeof(*h);

    switch (type) {
    case ETHERTYPE_ARP:
        arp_rx(payload, plen);
        break;
    case ETHERTYPE_IPV4:
        ipv4_rx(payload, plen);
        break;
    default:
        break;
    }
}
