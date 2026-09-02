/*
 * io_service.c - IOServiceFamily registry implementation.
 */
#include "io_service.h"
#include "klog.h"
#include "klibc.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

static io_service_t *registry[IO_SERVICE_MAX];
static u32 reg_count;

int io_service_init(void)
{
    reg_count = 0;
    klog("IOService", "registry ready (%u slots)", IO_SERVICE_MAX);
    return IOSVC_OK;
}

io_service_t *io_service_find(const char *name)
{
    if (!name)
        return NULL;

    for (u32 i = 0; i < reg_count; i++) {
        if (registry[i]->name && klibc.strcmp(registry[i]->name, name) == 0)
            return registry[i];
    }
    return NULL;
}

u32 io_service_count(void)
{
    return reg_count;
}

int io_service_register(io_service_t *svc)
{
    if (!svc || !svc->name || !svc->init)
        return IOSVC_ERR;

    if (reg_count >= IO_SERVICE_MAX) {
        klog_lvl(KLOG_ERR, "IOService", "registry full, cannot register %s",
                 svc->name);
        return IOSVC_E_FULL;
    }

    if (io_service_find(svc->name)) {
        klog_lvl(KLOG_WARNING, "IOService", "%s already registered",
                 svc->name);
        return IOSVC_E_DUP;
    }

    svc->registered = 1;
    svc->started = 0;
    registry[reg_count++] = svc;

    klog("IOService", "registered %s%s%s",
         svc->name,
         svc->desc ? " - " : "",
         svc->desc ? svc->desc : "");
    return IOSVC_OK;
}

int io_service_unregister(const char *name)
{
    io_service_t *svc = io_service_find(name);

    if (!svc)
        return IOSVC_E_NOENT;

    if (svc->started)
        io_service_stop(name);

    for (u32 i = 0; i < reg_count; i++) {
        if (registry[i] == svc) {
            for (u32 k = i; k < reg_count - 1; k++)
                registry[k] = registry[k + 1];
            break;
        }
    }

    svc->registered = 0;
    reg_count--;
    klog("IOService", "unregistered %s", name);
    return IOSVC_OK;
}

int io_service_start(const char *name)
{
    io_service_t *svc = io_service_find(name);
    int rc;

    if (!svc)
        return IOSVC_E_NOENT;

    if (svc->started)
        return IOSVC_E_STATE;

    rc = svc->init();
    if (rc != IOSVC_OK) {
        klog_lvl(KLOG_ERR, "IOService", "%s init failed (%d)", name, rc);
        return rc;
    }

    svc->started = 1;
    klog("IOService", "%s started", name);
    return IOSVC_OK;
}

int io_service_stop(const char *name)
{
    io_service_t *svc = io_service_find(name);

    if (!svc)
        return IOSVC_E_NOENT;

    if (!svc->started)
        return IOSVC_E_STATE;

    svc->exit();
    svc->started = 0;
    klog("IOService", "%s stopped", name);
    return IOSVC_OK;
}

int io_service_start_all(void)
{
    int ok = 0;

    for (u32 i = 0; i < reg_count; i++) {
        if (registry[i]->started)
            continue;
        if (io_service_start(registry[i]->name) == IOSVC_OK)
            ok++;
    }
    return ok;
}

int io_service_stop_all(void)
{
    int ok = 0;

    /* stop in reverse registration order (LIFO dependencies) */
    for (u32 i = reg_count; i > 0; i--) {
        if (!registry[i - 1]->started)
            continue;
        if (io_service_stop(registry[i - 1]->name) == IOSVC_OK)
            ok++;
    }
    return ok;
}
