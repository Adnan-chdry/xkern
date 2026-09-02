#ifndef STDIO_H
#define STDIO_H

#include "stdarg.h"

int printf(const char *fmt, ...);
int snprintf(char *buf, unsigned int n, const char *fmt, ...);
int vsnprintf(char *buf, unsigned int n, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);

int putchar(int c);
int puts(const char *s);

int getchar(void);
int scanf(const char *fmt, ...);
int sscanf(const char *buf, const char *fmt, ...);

#endif
