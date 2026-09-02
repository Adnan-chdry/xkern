#include "stdlib.h"
#include "ctype.h"

int atoi(const char *s)
{
    int n = 0, neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

long atol(const char *s)
{
    long n = 0;
    int neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (isdigit(*s)) n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

int abs(int n)
{
    return n < 0 ? -n : n;
}

long labs(long n)
{
    return n < 0 ? -n : n;
}
