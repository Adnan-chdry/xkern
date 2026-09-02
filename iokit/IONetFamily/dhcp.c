/*
 * dhcp.c - minimal DHCP client.
 *
 * State machine (driven by dhcp_poll() from ionet_poll()):
 *   DISCOVER -> OFFER -> REQUEST -> ACK -> bound
 * Retries every ~4 s while unconfigured.
 */
#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "klog.h"

#define DHCP_PORT_SERVER 67
#define DHCP_PORT_CLIENT 68

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPACK      5

#define OPT_PAD       0
#define OPT_SUBNET    1
#define OPT_ROUTER    3
#define OPT_MSGTYPE  53
#define OPT_REQ_IP   50
#define OPT_SRV_ID   54
#define OPT_END     255

/* fixed-size header portion: bootp fields (236) + magic cookie (4) */
#define DH_HDRSZ 240

enum {
    DH_OFF = 0,
    DH_DISCOVERING,
    DH_REQUESTING,
    DH_BOUND,
};

static u8  dh_state = DH_OFF;
static u32 dh_xid;
static u32 dh_offer_ip;         /* host order */
static u32 dh_srv_id;           /* host order */
static u64 dh_next_try_ms;
static u64 dh_started_ms;

static void dhcp_rx(u32 src_ip, u16 sport, const void *data, u32 len);

void dhcp_init(void)
{
    udp_bind(DHCP_PORT_CLIENT, dhcp_rx);
}

void dhcp_start(void)
{
    if (dh_state != DH_OFF)
        return;
    dh_state = DH_DISCOVERING;
    dh_xid = (u32)tsc_ms();
    dh_next_try_ms = tsc_ms();      /* fire immediately */
    dh_started_ms = dh_next_try_ms;
    klog("net.dhcp", "starting discovery");
}

int dhcp_done(void)
{
    return dh_state == DH_BOUND;
}

static void opt_put(u8 *o, u32 *n, u8 code, const void *val, u8 len)
{
    o[(*n)++] = code;
    o[(*n)++] = len;
    for (u8 i = 0; i < len; i++)
        o[(*n)++] = ((const u8 *)val)[i];
}

static int dhcp_send(u8 msgtype, u32 req_ip_host, u32 srv_host)
{
    static u8 pkt[DH_HDRSZ + 32];   /* static: keep off the boot stack */
    u32 nopt = 0;
    u8 *o = pkt + DH_HDRSZ;

    for (u32 i = 0; i < DH_HDRSZ; i++)
        pkt[i] = 0;

    pkt[0] = 1;                 /* op: bootrequest */
    pkt[1] = 1;                 /* htype: ethernet */
    pkt[2] = NET_ETH_ALEN;      /* hlen */

    *(u32 *)(pkt + 4)  = NET_HTONL(dh_xid);
    *(u16 *)(pkt + 10) = NET_HTONS(0x8000);     /* broadcast flag */
    for (int i = 0; i < NET_ETH_ALEN; i++)
        pkt[28 + i] = g_net_mac.b[i];           /* chaddr */
    *(u32 *)(pkt + 236) = NET_HTONL(0x63825363);

    u8 mt = msgtype;
    opt_put(o, &nopt, OPT_MSGTYPE, &mt, 1);
    if (req_ip_host) {
        u32 rip = NET_HTONL(req_ip_host);
        opt_put(o, &nopt, OPT_REQ_IP, &rip, 4);
    }
    if (srv_host) {
        u32 sid = NET_HTONL(srv_host);
        opt_put(o, &nopt, OPT_SRV_ID, &sid, 4);
    }
    o[nopt++] = OPT_END;

    /* discover/request always go out as IP broadcast */
    return udp_send(0xFFFFFFFF, DHCP_PORT_SERVER, DHCP_PORT_CLIENT,
                    pkt, DH_HDRSZ + nopt);
}

static void opt_parse(const u8 *data, u32 len,
                      u8 *msgtype, u32 *subnet, u32 *router, u32 *srv)
{
    *msgtype = 0;
    *subnet = *router = *srv = 0;

    u32 i = DH_HDRSZ;
    while (i < len) {
        u8 code = data[i];
        if (code == OPT_PAD) { i++; continue; }
        if (code == OPT_END) break;
        if (i + 1 >= len) break;
        u8 l = data[i + 1];
        if (i + 2 + l > len) break;
        const u8 *v = data + i + 2;

        switch (code) {
        case OPT_MSGTYPE:
            if (l >= 1) *msgtype = v[0];
            break;
        case OPT_SUBNET:
            if (l == 4) *subnet = NET_NTOHL(*(const u32 *)v);
            break;
        case OPT_ROUTER:
            if (l == 4) *router = NET_NTOHL(*(const u32 *)v);
            break;
        case OPT_SRV_ID:
            if (l == 4) *srv = NET_NTOHL(*(const u32 *)v);
            break;
        default:
            break;
        }
        i += 2 + l;
    }
}

static void dhcp_rx(u32 src_ip, u16 sport, const void *data, u32 len)
{
    u8 type;
    u32 subnet, router, srv;

    (void)src_ip;
    (void)sport;

    if (len < DH_HDRSZ + 3)
        return;
    if (*(const u32 *)(data + 236) != NET_HTONL(0x63825363))
        return;
    if (*(const u32 *)(data + 4) != NET_HTONL(dh_xid))
        return;

    u32 yiaddr = NET_NTOHL(*(const u32 *)(data + 16));

    opt_parse(data, len, &type, &subnet, &router, &srv);

    if (dh_state == DH_DISCOVERING && type == DHCPOFFER) {
        dh_offer_ip = yiaddr;
        dh_srv_id = srv;
        klog("net.dhcp", "offer %s - requesting",
             net_ipv4_str(yiaddr, (char[16]){0}));
        dhcp_send(DHCPREQUEST, dh_offer_ip, srv);
        dh_state = DH_REQUESTING;
        dh_next_try_ms = tsc_ms() + 4000;
    }
    else if (dh_state == DH_REQUESTING && type == DHCPACK) {
        g_net_ip = yiaddr ? yiaddr : dh_offer_ip;
        g_net_mask = subnet;
        g_net_gw = router;
        dh_state = DH_BOUND;
        klog("net.dhcp", "bound: ip %s mask %s gw %s",
             net_ipv4_str(g_net_ip, (char[16]){0}),
             net_ipv4_str(g_net_mask, (char[16]){0}),
             net_ipv4_str(g_net_gw, (char[16]){0}));
    }
}

void dhcp_poll(void)
{
    u64 now = tsc_ms();

    if (dh_state == DH_OFF || dh_state == DH_BOUND)
        return;
    if (now < dh_next_try_ms)
        return;

    if (dh_state == DH_DISCOVERING) {
        klog("net.dhcp", "sending DISCOVER");
        dhcp_send(DHCPDISCOVER, 0, 0);
        dh_next_try_ms = now + 4000;
    } else if (dh_state == DH_REQUESTING) {
        klog("net.dhcp", "sending REQUEST");
        dhcp_send(DHCPREQUEST, dh_offer_ip, dh_srv_id);
        dh_next_try_ms = now + 4000;
    }
}
