/*
    classic kernel logging system elaborated from previous printk()
*/

#include "printf.h"
#include "stdarg.h"
#include "vga.h" //legecy api for test
#include "klibc.h"
#include "tsc.h"
#include "version.h"

static const char *const klog_prio[] = {
    "Emergency", "Alert", "Critical", "Error",
    "Warning", "Notice", "Info", "Debug",
};

#define KLOG_NOTICE 5

static void klog_emit(int prio, const char *driver, const char *buf)
{
    u32 ms = tsc_ms();
    const char *level = (prio >= 0 && prio <= 7) ? klog_prio[prio] : klog_prio[KLOG_NOTICE];

    char line[352];
    klibc.snprintf(line, sizeof(line),
                   "%02u:%02u:%02u.%03u xkern kernel[0] <%s>: com.xkern.%s: %s\n",
                   ms / 3600000u, (ms / 60000u) % 60u, (ms / 1000u) % 60u,
                   ms % 1000u,
                   level, driver, buf);

    /* sink (fb or vga) selected by printf */
    klibc.printf("%s", line);
}

void klog_vlvl(int prio, const char *driver, const char *fmt, va_list ap)
{
    char buf[256];
    klibc.vsnprintf(buf, sizeof(buf), fmt, ap);
    klog_emit(prio, driver, buf);
}

void klog_lvl(int prio, const char *driver, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    klog_vlvl(prio, driver, fmt, ap);
    va_end(ap);
}

//modern kernel logger with klibc system (syslog NOTICE by default)
void klog(const char *driver, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    klog_vlvl(KLOG_NOTICE, driver, fmt, ap);
    va_end(ap);
}

void __klog(const char *driver, const char *msg){
  klibc.printf("%s->%s.%s",ostype,driver,msg);
}

//vklog is legacy kernel logging system created first(wont be shown in graphical env)
//a system meant to run on 80x25 std vga not in std vbe so dont use this API abonden it 19/8/2026
void vklog(const char *driver, const char *did){
    vga_print("com.xkern.");
    vga_print(driver);
    vga_print(": ");
    vga_print(did);
}
