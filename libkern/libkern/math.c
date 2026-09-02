#include "math.h"

double fabs(double x)
{
    if (x < 0.0) return -x;
    return x;
}

double fmod(double x, double y)
{
    if (y == 0.0) return x;
    double q = x / y;
    int n = (int)q;
    return x - (double)n * y;
}

double pow10(int n)
{
    double result = 1.0;
    double base = 10.0;
    if (n < 0) {
        n = -n;
        base = 0.1;
    }
    for (int i = 0; i < n; i++)
        result *= base;
    return result;
}
