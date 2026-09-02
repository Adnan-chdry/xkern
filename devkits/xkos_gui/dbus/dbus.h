#ifndef XKOS_DBUS_H
#define XKOS_DBUS_H

/*
 * dbus.h - XKOS in-kernel message bus (D-Bus-inspired, simplified).
 *
 * A tiny publish/subscribe + method-call bus for the GUI.  Services
 * "listen" on an object path (e.g. "/com/xkos/TopBar"); anyone may
 * "emit" a signal on a path and every listener on that path receives it
 * with (sender, path, member, arg).  This is how the top bar menu, the
 * status cluster and the control-center popovers talk to the desktop
 * environment without the widgets knowing anything about the DE.
 *
 * There is no marshalling: args are NUL-terminated strings (numbers are
 * formatted with xkos_dbus_itoa()).  Single-threaded kernel context, so
 * delivery is synchronous and immediate.
 */

#include "types.h"

typedef void (*xkos_dbus_handler_t)(const char *sender, const char *path,
                                    const char *member, const char *arg,
                                    void *ud);
typedef u32 xkos_dbus_conn_t;

#define XKOS_DBUS_TOPBAR    "/com/xkos/TopBar"
#define XKOS_DBUS_MENU      "/com/xkos/Menu"
#define XKOS_DBUS_STATUS    "/com/xkos/Status"
#define XKOS_DBUS_DESKTOP   "/com/xkos/Desktop"
#define XKOS_DBUS_CC        "/com/xkos/ControlCenter"

void   xkos_dbus_init(void);
xkos_dbus_conn_t xkos_dbus_listen(const char *path,
                                  xkos_dbus_handler_t h, void *ud);
void   xkos_dbus_emit(const char *sender, const char *path,
                      const char *member, const char *arg);

/* static buffer holding an int as a string (for emit args) */
const char *xkos_dbus_itoa(int v);

#endif
