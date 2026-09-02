#include "float.h"
#include "math.h"
#include "stdlib.h"

double atof(const char *s)
{
    return strtod(s, (void *)0);
}

double strtod(const char *s, char **end)
{
    double result = 0.0;
    double sign = 1.0;
    double frac = 0.0;
    double scale = 1.0;
    int exp = 0;
    int exp_sign = 1;

    while (*s == ' ' || *s == '\t') s++;

    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') { s++; }

    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (double)(*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac = frac * 10.0 + (double)(*s - '0');
            scale *= 10.0;
            s++;
        }
        result += frac / scale;
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') { s++; }
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        exp *= exp_sign;
        result *= pow10(exp);
    }

    if (end) *end = (char *)s;

    return sign * result;
}

float strtof(const char *s, char **end)
{
    return (float)strtod(s, end);
}
