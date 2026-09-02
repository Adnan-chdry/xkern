/*
 * io_service.h - IOServiceFamily: driver/service registry for developers.
 *
 * Lets kernel developers plug in components using kernel resources by
 * registering a descriptor (name + init + exit).  Registered services
 * can be started/stopped individually or all at once from the boot flow.
 *
 * Developer example:
 *
 *     #include "IOServiceFamily/io_service.h"
 *
 *     static int  mydrv_init(void) { klog("mydrv", "up");   return IOSVC_OK; }
 *     static void mydrv_exit(void) { klog("mydrv", "down"); }
 *
 *     io_service_t mydrv = {
 *         .name = "com.example.mydrv",
 *         .desc = "example developer service",
 *         .init = mydrv_init,
 *         .exit = mydrv_exit,
 *     };
 *
 *     void mydrv_load(void)
 *     {
 *         io_service_register(&mydrv);
 *         io_service_start("com.example.mydrv");
 *     }
 */
#pragma once
#include "types.h"

#define IO_SERVICE_MAX 32

/* return codes */
#define IOSVC_OK        0
#define IOSVC_ERR      -1      /* generic failure */
#define IOSVC_E_FULL   -2      /* registry full */
#define IOSVC_E_DUP    -3      /* name already registered */
#define IOSVC_E_NOENT  -4      /* unknown name */
#define IOSVC_E_STATE  -5      /* wrong state for that operation */

typedef int  (*io_service_init_fn)(void);
typedef void (*io_service_exit_fn)(void);

typedef struct io_service {
    /* filled in by the developer */
    const char          *name;      /* reverse-DNS style: com.vendor.name */
    const char          *desc;
    io_service_init_fn   init;      /* called by io_service_start*() */
    io_service_exit_fn   exit;      /* called by io_service_stop*()  */

    /* managed by the registry - leave zeroed */
    u8                   registered;
    u8                   started;
} io_service_t;

int           io_service_init(void);
int           io_service_register(io_service_t *svc);
int           io_service_unregister(const char *name);
int           io_service_start(const char *name);
int           io_service_stop(const char *name);
int           io_service_start_all(void);
int           io_service_stop_all(void);
io_service_t *io_service_find(const char *name);
u32           io_service_count(void);
