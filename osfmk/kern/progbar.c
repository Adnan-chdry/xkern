#include "progbar.h"
#include <xkern/libkern/libkern/klibc.h>


/*
 * Replace these with your kernel's console functions.
 *
 * klibc.printf() should print one character.
 */

#define PROGBAR_WIDTH 30

static const char *bar_label;
static unsigned int bar_total;
static unsigned int bar_last = 0;

/*
 * Print a string without libc.
 */
static void print_string(const char *s)
{
    while (*s) {
        klibc.printf(*s++);
    }
}

/*
 * Print an unsigned integer without printf.
 */
static void print_uint(unsigned int n)
{
    char buf[11];
    int i = 0;

    if (n == 0) {
        klibc.printf('0');
        return;
    }

    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i > 0) {
        klibc.printf(buf[--i]);
    }
}

/*
 * Clear the current line and move to its beginning.
 *
 * '\r' returns the cursor to column 0.
 *
 * If your terminal supports ANSI escape sequences, the
 * escape below also clears the line:
 *
 *     ESC [ 2 K
 */
static void clear_line(void)
{
    klibc.printf('\r');

    klibc.printf(0x1b);
    klibc.printf('[');
    klibc.printf('2');
    klibc.printf('K');
    klibc.printf('\r');
}

void progbar_init(const char *label, unsigned int total)
{
    bar_label = label;
    bar_total = total;
    bar_last = 0;

    progbar_update(0);
}

void progbar_update(unsigned int current)
{
    unsigned int percent;
    unsigned int filled;
    unsigned int i;

    if (bar_total == 0)
        return;

    if (current > bar_total)
        current = bar_total;

    percent = (current * 100) / bar_total;

    /*
     * Don't redraw if nothing visually changed.
     */
    if (percent == bar_last && current != bar_total)
        return;

    bar_last = percent;

    clear_line();

    if (bar_label) {
        print_string(bar_label);
        print_string("  ");
    }

    /*
     * Modern-looking ASCII progress bar:
     *
     * [====================>---------] 72%
     */
    klibc.printf('[');

    filled = (percent * PROGBAR_WIDTH) / 100;

    for (i = 0; i < PROGBAR_WIDTH; i++) {
        if (i < filled) {
            klibc.printf('=');
        } else if (i == filled && percent < 100) {
            klibc.printf('>');
        } else {
            klibc.printf('-');
        }
    }

    klibc.printf(']');

    klibc.printf(' ');

    if (percent < 10)
        klibc.printf(' ');

    if (percent < 100)
        klibc.printf(' ');

    print_uint(percent);
    klibc.printf('%');
}

void progbar_finish(void)
{
    if (bar_total != 0)
        progbar_update(bar_total);

    klibc.printf('\n');

    bar_label = 0;
    bar_total = 0;
    bar_last = 0;
}
