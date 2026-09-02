#include "stdio.h"
#include "string.h"
#include "ctype.h"
#include "atkbd.h"
#include "IOUSBFamily/hid/usbhid.h"

static int skip_ws(const char **s)
{
    int n = 0;
    while (isspace(**s)) { (*s)++; n++; }
    return n;
}

int vsscanf(const char *buf, const char *fmt, va_list ap)
{
    int count = 0;
    const char *p = buf;

    while (*fmt) {
        /* whitespace in format: match any amount of whitespace (incl. none) */
        if (isspace(*fmt)) {
            fmt++;
            skip_ws(&p);
            continue;
        }

        /* literal character: match exactly, no whitespace skipping */
        if (*fmt != '%') {
            if (*p != *fmt) break;
            p++;
            fmt++;
            continue;
        }

        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        int lng = 0;
        if (*fmt == 'l') { lng = 1; fmt++; }

        int noassign = 0;
        if (*fmt == '*') { noassign = 1; fmt++; }

        if (*fmt == '\0') break;

        switch (*fmt) {
        case 'd': {
            int neg = 0, val = 0, n = 0;
            skip_ws(&p);
            if (*p == '-') { neg = 1; p++; n++; }
            else if (*p == '+') { p++; n++; }
            while (*p >= '0' && *p <= '9' && (!width || n - neg < width)) {
                val = val * 10 + (*p - '0');
                p++; n++;
            }
            if (neg) val = -val;
            if (n > (neg ? 1 : 0) && !noassign) {
                if (lng) *va_arg(ap, long *) = (long)val;
                else *va_arg(ap, int *) = val;
                count++;
            }
            break;
        }
        case 'u': {
            unsigned int val = 0;
            int n = 0;
            skip_ws(&p);
            while (*p >= '0' && *p <= '9' && (!width || n < width)) {
                val = val * 10 + (*p - '0');
                p++; n++;
            }
            if (n > 0 && !noassign) {
                if (lng) *va_arg(ap, unsigned long *) = (unsigned long)val;
                else *va_arg(ap, unsigned int *) = val;
                count++;
            }
            break;
        }
        case 'x': case 'X': {
            unsigned int val = 0;
            int n = 0;
            skip_ws(&p);
            if (*p == '0') { p++; n++; }
            if (*p == 'x' || *p == 'X') { p++; n++; }
            while ((*p >= '0' && *p <= '9') ||
                   (*p >= 'a' && *p <= 'f') ||
                   (*p >= 'A' && *p <= 'F')) {
                if (*p >= '0' && *p <= '9') val = val * 16 + (*p - '0');
                else if (*p >= 'a' && *p <= 'f') val = val * 16 + (*p - 'a' + 10);
                else val = val * 16 + (*p - 'A' + 10);
                p++; n++;
            }
            if (n > 0 && !noassign) {
                if (lng) *va_arg(ap, unsigned long *) = (unsigned long)val;
                else *va_arg(ap, unsigned int *) = val;
                count++;
            }
            break;
        }
        case 's': {
            char *s = noassign ? (char *)0 : va_arg(ap, char *);
            int n = 0;
            skip_ws(&p);
            while (*p && !isspace(*p) && (!width || n < width)) {
                if (!noassign) s[n] = *p;
                p++; n++;
            }
            if (!noassign && n > 0) { s[n] = '\0'; count++; }
            break;
        }
        case 'c': {
            /* %c does NOT skip leading whitespace */
            if (!*p)
                break;              /* matching failure on empty input */
            if (!noassign) {
                char *c = va_arg(ap, char *);
                *c = *p;
                count++;
            }
            p++;
            break;
        }
        case '[': {
            /* scanset: %[set], %[^set], ranges like %[a-z]; keeps spaces */
            unsigned char set[256];
            char *s = noassign ? (char *)0 : va_arg(ap, char *);
            int invert = 0, n = 0;

            memset(set, 0, sizeof(set));
            fmt++;
            if (*fmt == '^') { invert = 1; fmt++; }
            if (*fmt == ']') { set[(unsigned char)']'] = 1; fmt++; }
            while (*fmt && *fmt != ']') {
                if (fmt[1] == '-' && fmt[2] && fmt[2] != ']') {
                    int lo = (unsigned char)fmt[0];
                    int hi = (unsigned char)fmt[2];
                    while (lo <= hi) set[lo++] = 1;
                    fmt += 3;
                } else {
                    set[(unsigned char)*fmt] = 1;
                    fmt++;
                }
            }
            if (*fmt != ']')
                break;              /* malformed scanset */

            while (*p && (!width || n < width) &&
                   (!!set[(unsigned char)*p] != invert)) {
                if (!noassign) s[n] = *p;
                p++; n++;
            }
            if (!noassign && n > 0) { s[n] = '\0'; count++; }
            break;
        }
        case '%':
            if (*p == '%') p++;
            break;
        }

        /* advance past the conversion specifier */
        if (*fmt)
            fmt++;
    }

    return count;
}

int sscanf(const char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

int getchar(void)
{
    int c = atkbd_pollchar();
    if (c >= 0) return c;
    extern void usb_check_event(void);
    usb_check_event();
    extern int usb_getchar(void);
    return usb_getchar();
}

int scanf(const char *fmt, ...)
{
    char line[256];
    int i = 0;

    for (;;) {
        int c = getchar();
        if (c < 0) continue;
        if (c == '\n' || c == '\r') break;
        if (c == '\b' || c == 0x7F) {
            if (i > 0) i--;
            continue;
        }
        /* spaces and tabs are kept; the format decides how they split */
        if (i < (int)sizeof(line) - 1)
            line[i++] = (char)c;
    }
    line[i] = '\0';

    va_list ap;
    va_start(ap, fmt);
    int ret = vsscanf(line, fmt, ap);
    va_end(ap);
    return ret;
}
