#include "printf.h"
#include "vga.h"
#include "multiboot.h"
#include "serial.h"
#include "IOGraphicsFamily/fb.h"
#include "IOGraphicsFamily/font.h"
#include "gpukit/lv_console.h"
#include "float.h"
#include "math.h"

static void print_char(char **buf, u32 *pos, u32 n, char c);
static void print_str(char **buf, u32 *pos, u32 n, const char *s);
static void print_num64(char **buf, u32 *pos, u32 n, unsigned long long val64, int base, int upper, int sign, int width, int zero);

static void print_float(char **buf, u32 *pos, u32 n, double value, int prec, int width, int zero, int plus, int space);

static const double _ftoa_pow10[] = {
    1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0,
    1000000.0, 10000000.0, 100000000.0, 1000000000.0
};

static void print_float(char **buf, u32 *pos, u32 n, double value, int prec, int width, int zero, int plus, int space)
{
    char tmp[48];
    int idx = 0;
    int neg = 0;

    if (value != value) {
        print_str(buf, pos, n, "nan");
        return;
    }
    if (value < -DBL_MAX) {
        print_str(buf, pos, n, "-inf");
        return;
    }
    if (value > DBL_MAX) {
        print_str(buf, pos, n, "+inf");
        return;
    }

    if (value < 0.0) {
        neg = 1;
        value = -value;
    }

    if (prec < 0) prec = 6;
    if (prec > 9) prec = 9;

    if (prec == 0) {
        double diff = value - (double)(int)(value + 0.5);
        if (!(diff < 0.5) && diff > 0.5) value += 1.0;
        else if (diff == 0.5 && ((int)(value + 0.5) & 1)) value += 1.0;
    } else {
        int whole_part = (int)value;
        double frac = value - (double)whole_part;
        unsigned long frac_part = (unsigned long)(frac * _ftoa_pow10[prec] + 0.5);
        if (frac_part >= (unsigned long)_ftoa_pow10[prec]) {
            frac_part = 0;
            whole_part++;
        }
        int count = prec;
        while (count--) {
            tmp[idx++] = (char)('0' + (frac_part % 10));
            frac_part /= 10;
        }
        tmp[idx++] = '.';
        value = (double)whole_part;
    }

    unsigned long whole = (unsigned long)value;
    if (whole == 0) {
        tmp[idx++] = '0';
    } else {
        while (whole > 0) {
            tmp[idx++] = (char)('0' + (whole % 10));
            whole /= 10;
        }
    }

    int total = idx + neg;
    if (!neg && (plus || space)) total++;
    int pad = width > total ? width - total : 0;

    if (!zero || pad <= 0) {
        while (pad-- > 0) print_char(buf, pos, n, ' ');
    }
    if (neg) print_char(buf, pos, n, '-');
    else if (plus) print_char(buf, pos, n, '+');
    else if (space) print_char(buf, pos, n, ' ');
    if (zero && pad > 0) {
        while (pad-- > 0) print_char(buf, pos, n, '0');
    }

    while (idx-- > 0) print_char(buf, pos, n, tmp[idx]);
}

static void print_scientific(char **buf, u32 *pos, u32 n, double value, int prec, int width, int zero, int plus, int space, int upper)
{
    char tmp[48];
    int idx = 0;
    int neg = 0;
    int exp = 0;

    if (value != value) {
        print_str(buf, pos, n, "nan");
        return;
    }
    if (value < -DBL_MAX) {
        print_str(buf, pos, n, "-inf");
        return;
    }
    if (value > DBL_MAX) {
        print_str(buf, pos, n, "+inf");
        return;
    }

    if (value < 0.0) {
        neg = 1;
        value = -value;
    }

    if (prec < 0) prec = 6;
    if (prec > 9) prec = 9;

    while (value >= 10.0) { value /= 10.0; exp++; }
    while (value < 1.0 && value != 0.0) { value *= 10.0; exp--; }

    if (prec == 0) {
        double diff = value - (double)(int)(value + 0.5);
        if (!(diff < 0.5) && diff > 0.5) value += 1.0;
        else if (diff == 0.5 && ((int)(value + 0.5) & 1)) value += 1.0;
    } else {
        int whole_part = (int)value;
        double frac = value - (double)whole_part;
        unsigned long frac_part = (unsigned long)(frac * _ftoa_pow10[prec] + 0.5);
        if (frac_part >= (unsigned long)_ftoa_pow10[prec]) {
            frac_part = 0;
            whole_part++;
        }
        int count = prec;
        while (count--) {
            tmp[idx++] = (char)('0' + (frac_part % 10));
            frac_part /= 10;
        }
        tmp[idx++] = '.';
        value = (double)whole_part;
    }

    unsigned long whole = (unsigned long)value;
    if (whole == 0) {
        tmp[idx++] = '0';
    } else {
        while (whole > 0) {
            tmp[idx++] = (char)('0' + (whole % 10));
            whole /= 10;
        }
    }

    const char *e_char = upper ? "E" : "e";
    int exp_neg = exp < 0;
    if (exp_neg) exp = -exp;
    char exp_buf[8];
    int exp_idx = 0;
    if (exp >= 100) exp_buf[exp_idx++] = (char)('0' + exp / 100);
    if (exp >= 10) exp_buf[exp_idx++] = (char)('0' + (exp / 10) % 10);
    exp_buf[exp_idx++] = (char)('0' + exp % 10);

    int total = idx + neg + 2 + exp_idx;
    if (!neg && (plus || space)) total++;
    int pad = width > total ? width - total : 0;

    if (!zero || pad <= 0) {
        while (pad-- > 0) print_char(buf, pos, n, ' ');
    }
    if (neg) print_char(buf, pos, n, '-');
    else if (plus) print_char(buf, pos, n, '+');
    else if (space) print_char(buf, pos, n, ' ');
    if (zero && pad > 0) {
        while (pad-- > 0) print_char(buf, pos, n, '0');
    }

    while (idx-- > 0) print_char(buf, pos, n, tmp[idx]);
    print_str(buf, pos, n, e_char);
    print_char(buf, pos, n, exp_neg ? '-' : '+');
    exp_idx = 0;
    while (exp_idx-- > 0) print_char(buf, pos, n, exp_buf[exp_idx]);
}

/*
 * console sink selection (per char):
 *   LVGL console active -> LVGL terminal + serial echo
 *   framebuffer mode    -> bitmap font console + serial echo
 *   vga text mode       -> vga console (mirrors to serial)
 */
static void print_char(char **buf, u32 *pos, u32 n, char c)
{
    if (buf) {
        if (*pos < n) {
            (*buf)[*pos] = c;
            (*pos)++;
        }
    } else {
        if (lv_console_active()) {
            lv_console_putc(c);
            serial_putchar(c);
        } else if (framebuffer_ready()) {
            font_putc(c);
            serial_putchar(c);
        } else {
            vga_putchar(c);
        }
        (*pos)++;
    }
}

static void print_str(char **buf, u32 *pos, u32 n, const char *s)
{
    if (!s) s = "(null)";
    while (*s) print_char(buf, pos, n, *s++);
}

/* 64-bit core: uses __udivdi3/__umoddi3 on i386 */
static void print_num64(char **buf, u32 *pos, u32 n, unsigned long long val64, int base, int upper, int sign, int width, int zero)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = upper ? digits_upper : digits_lower;
    char tmp[72];
    int idx = 0, neg = 0;

    if (sign && (long long)val64 < 0) {
        neg = 1;
        val64 = (unsigned long long)(-(long long)val64);
    }

    if (val64 == 0) {
        tmp[idx++] = '0';
    } else {
        while (val64 > 0) {
            tmp[idx++] = digits[val64 % base];
            val64 /= base;
        }
    }

    int pad = width > idx ? width - idx - neg : 0;
    if (zero && pad > 0) {
        if (neg) print_char(buf, pos, n, '-');
        while (pad--) print_char(buf, pos, n, '0');
        while (idx--) print_char(buf, pos, n, tmp[idx]);
    } else {
        while (pad--) print_char(buf, pos, n, ' ');
        if (neg) print_char(buf, pos, n, '-');
        while (idx--) print_char(buf, pos, n, tmp[idx]);
    }
}

int vsnprintf(char *buf, u32 n, const char *fmt, va_list ap)
{
    u32 pos = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            print_char(buf ? &buf : 0, &pos, n, *fmt);
            continue;
        }

        fmt++;
        int width = 0;
        int zero = 0;
        int prec = -1;

        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* length modifiers: 'l', 'll' ('z'/'h' accepted, treated as int) */
        int lng = 0;
        while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') {
            if (*fmt == 'l') lng++;
            fmt++;
        }

        if (*fmt == '\0') break;

        unsigned long long val64;
        switch (*fmt) {
            case 'd':
            case 'i':
                val64 = lng >= 2 ? (unsigned long long)va_arg(ap, long long)
                      : lng == 1 ? (unsigned long long)(long)va_arg(ap, long)
                                 : (u32)va_arg(ap, int);
                print_num64(buf ? &buf : 0, &pos, n, val64, 10, 0, 1, width, zero);
                break;
            case 'u':
                val64 = lng >= 2 ? va_arg(ap, unsigned long long)
                      : lng == 1 ? (unsigned long)va_arg(ap, unsigned long)
                                 : (u32)va_arg(ap, u32);
                print_num64(buf ? &buf : 0, &pos, n, val64, 10, 0, 0, width, zero);
                break;
            case 'x':
            case 'X':
                val64 = lng >= 2 ? va_arg(ap, unsigned long long)
                      : lng == 1 ? (unsigned long)va_arg(ap, unsigned long)
                                 : (u32)va_arg(ap, u32);
                print_num64(buf ? &buf : 0, &pos, n, val64, 16, *fmt == 'X', 0, width, zero);
                break;
            case 'o':
                val64 = lng >= 2 ? va_arg(ap, unsigned long long)
                      : lng == 1 ? (unsigned long)va_arg(ap, unsigned long)
                                 : (u32)va_arg(ap, u32);
                print_num64(buf ? &buf : 0, &pos, n, val64, 8, 0, 0, width, zero);
                break;
            case 'b':
                val64 = (u32)va_arg(ap, u32);
                print_num64(buf ? &buf : 0, &pos, n, val64, 2, 0, 0, width, zero);
                break;
            case 'p': {
                print_str(buf ? &buf : 0, &pos, n, "0x");
                val64 = va_arg(ap, u32);
                print_num64(buf ? &buf : 0, &pos, n, val64, 16, 0, 0, 8, 1);
                break;
            }
            case 's':
                print_str(buf ? &buf : 0, &pos, n, va_arg(ap, const char *));
                break;
            case 'c':
                val64 = (unsigned char)va_arg(ap, int);
                print_char(buf ? &buf : 0, &pos, n, (char)val64);
                break;
            case 'f':
            case 'F': {
                double val = va_arg(ap, double);
                print_float(buf ? &buf : 0, &pos, n, val, prec < 0 ? 6 : prec, width, zero, 0, 0);
                break;
            }
            case 'e':
            case 'E': {
                double val = va_arg(ap, double);
                print_scientific(buf ? &buf : 0, &pos, n, val, prec < 0 ? 6 : prec, width, zero, 0, 0, *fmt == 'E');
                break;
            }
            case 'g':
            case 'G': {
                double val = va_arg(ap, double);
                int p = prec < 0 ? 6 : prec;
                int exp = 0;
                double tmp = val < 0 ? -val : val;
                if (tmp != 0.0) {
                    while (tmp >= 10.0) { tmp /= 10.0; exp++; }
                    while (tmp < 1.0) { tmp *= 10.0; exp--; }
                }
                if (exp >= -4 && exp < 6) {
                    int fp = p - exp - 1;
                    if (fp < 0) fp = 0;
                    print_float(buf ? &buf : 0, &pos, n, val, fp, width, zero, 0, 0);
                } else {
                    print_scientific(buf ? &buf : 0, &pos, n, val, p > 0 ? p - 1 : 0, width, zero, 0, 0, *fmt == 'G');
                }
                break;
            }
            case '%':
                print_char(buf ? &buf : 0, &pos, n, '%');
                break;
            default:
                print_char(buf ? &buf : 0, &pos, n, '%');
                print_char(buf ? &buf : 0, &pos, n, *fmt);
                break;
        }
    }

    if (buf && pos < n) buf[pos] = '\0';
    else if (buf && n > 0) buf[n - 1] = '\0';

    return pos;
}

int snprintf(char *buf, u32 n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(0, 0, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, 0xFFFFFFFF, fmt, ap);
    va_end(ap);
    return ret;
}

int putchar(int c)
{
    vga_putchar((char)c);
    return (unsigned char)c;
}

int puts(const char *s)
{
    while (*s)
        vga_putchar(*s++);
    vga_putchar('\n');
    return 0;
}
