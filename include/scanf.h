#ifndef STDIO_H
#include <stdarg.h>

int scanf(const char *fmt, ...);
int sscanf(const char *buf, const char *fmt, ...);
int vsscanf(const char *buf, const char *fmt, va_list ap);

#endif