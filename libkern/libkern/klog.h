#pragma once
#include "stdarg.h"

void klog(const char *driver, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void klog_lvl(int prio, const char *driver, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void klog_vlvl(int prio, const char *driver, const char *fmt, va_list ap);
void vklog(const char *driver, const char *did);
void __klog(const char *driver, const char *msg); /*prints out
                                                   *  xkern-26.0.8 -> driver.msg
                                                   * */

/* syslog(3) priorities */
#define KLOG_EMERG   0
#define KLOG_ALERT   1
#define KLOG_CRIT    2
#define KLOG_ERR     3
#define KLOG_WARNING 4
#define KLOG_NOTICE  5
#define KLOG_INFO    6
#define KLOG_DEBUG   7
