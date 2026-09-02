/*
 * dbus.c - XKOS in-kernel message bus (D-Bus-inspired, simplified).
 *
 * See dbus.h.  Listeners are kept in a small static table; emit() fans a
 * signal out to every listener registered on the destination path.
 */

#include "dbus.h"
#include "string.h"

#define XKOS_DBUS_MAX 64

struct xkos_dbus_listener {
    const char          *path;
    xkos_dbus_handler_t  h;
    void                *ud;
    int                  used;
};

static struct xkos_dbus_listener g_listeners[XKOS_DBUS_MAX];
static int g_count;
static int g_inited;

void xkos_dbus_init(void)
{
    g_count = 0;
    g_inited = 1;
    memset(g_listeners, 0, sizeof(g_listeners));
}

xkos_dbus_conn_t xkos_dbus_listen(const char *path,
                                  xkos_dbus_handler_t h, void *ud)
{
    if (!g_inited) xkos_dbus_init();
    if (g_count >= XKOS_DBUS_MAX) return 0;

    g_listeners[g_count].path = path;
    g_listeners[g_count].h    = h;
    g_listeners[g_count].ud   = ud;
    g_listeners[g_count].used = 1;
    return (xkos_dbus_conn_t)++g_count;
}

void xkos_dbus_emit(const char *sender, const char *path,
                    const char *member, const char *arg)
{
    int i;

    if (!g_inited) xkos_dbus_init();
    for (i = 0; i < g_count; i++) {
        if (!g_listeners[i].used) continue;
        if (strcmp(g_listeners[i].path, path) != 0) continue;
        g_listeners[i].h(sender ? sender : "bus",
                         path, member ? member : "",
                         arg ? arg : "", g_listeners[i].ud);
    }
}

const char *xkos_dbus_itoa(int v)
{
    static char buf[16];
    int i = sizeof(buf) - 1;
    int neg = 0;

    if (v < 0) { neg = 1; v = -v; }
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg) buf[--i] = '-';
    return &buf[i];
}
