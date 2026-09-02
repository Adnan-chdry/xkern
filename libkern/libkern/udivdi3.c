static unsigned long long udivmoddi4(unsigned long long num,
                                     unsigned long long den, int mod) {
    unsigned long long quot = 0, qbit = 1;

    if (den == 0) {
        return 0;
    }

    while ((long long)den >= 0 && den < num) {
        den <<= 1;
        qbit <<= 1;
    }

    while (qbit != 0) {
        if (num >= den) {
            num -= den;
            quot |= qbit;
        }
        den >>= 1;
        qbit >>= 1;
    }

    return mod ? num : quot;
}

unsigned long long __udivdi3(unsigned long long num, unsigned long long den) {
    return udivmoddi4(num, den, 0);
}

unsigned long long __umoddi3(unsigned long long num, unsigned long long den) {
    return udivmoddi4(num, den, 1);
}

long long __divdi3(long long num, long long den) {
    int neg = 0;
    unsigned long long a, b, q;

    if (num < 0) {
        num = -num;
        neg = !neg;
    }
    if (den < 0) {
        den = -den;
        neg = !neg;
    }

    a = (unsigned long long)num;
    b = (unsigned long long)den;
    q = __udivdi3(a, b);

    return neg ? -(long long)q : (long long)q;
}

long long __moddi3(long long num, long long den) {
    int neg = 0;
    unsigned long long a, b, r;

    if (num < 0) {
        num = -num;
        neg = 1;
    }
    if (den < 0) {
        den = -den;
    }

    a = (unsigned long long)num;
    b = (unsigned long long)den;
    r = __umoddi3(a, b);

    return neg ? -(long long)r : (long long)r;
}
