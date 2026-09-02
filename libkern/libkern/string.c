#include "string.h"
#include "types.h"
#include <stdint.h>

void *memcpy(void *dest, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if ((((uintptr_t)d | (uintptr_t)s) & 7) == 0) {
        u64 *dw = (u64 *)d;
        const u64 *sw = (const u64 *)s;
        while (n >= 8) { *dw++ = *sw++; n -= 8; }
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
    }
    if ((((uintptr_t)d | (uintptr_t)s) & 3) == 0) {
        u32 *dw = (u32 *)d;
        const u32 *sw = (const u32 *)s;
        while (n >= 4) { *dw++ = *sw++; n -= 4; }
        d = (unsigned char *)dw;
        s = (const unsigned char *)sw;
    }
    while (n--) *d++ = *s++;

    return dest;
}

void *memmove(void *dest, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s)
        return dest;

    if (d < s) {
        return memcpy(dest, src, n);
    }

    d += n;
    s += n;
    while (n--) *--d = *--s;
    return dest;
}

void *memset(void *s, int c, unsigned int n)
{
    unsigned char *p = (unsigned char *)s;

    if (((uintptr_t)p & 3) == 0) {
        u64 c8 = (u64)(unsigned char)c * 0x0101010101010101ull;
        while (n >= 4) { *(u32 *)p = (u32)c8; p += 4; n -= 4; }
    }
    while (n--) *p++ = (unsigned char)c;

    return s;
}

int memcmp(const void *s1, const void *s2, unsigned int n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (unsigned int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(a[i] - b[i]);
    }
    return 0;
}

void *memchr(const void *s, int c, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)s;
    for (unsigned int i = 0; i < n; i++) {
        if (p[i] == (unsigned char)c) return (void *)(p + i);
    }
    return 0;
}

unsigned int strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncmp(const char *s1, const char *s2, unsigned int n)
{
    for (unsigned int i = 0; i < n; i++) {
        if (s1[i] != s2[i] || !s1[i])
            return (unsigned char)s1[i] - (unsigned char)s2[i];
    }
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, unsigned int n)
{
    char *d = dest;
    while (n-- && (*d++ = *src++));
    if (n) while (n--) *d++ = '\0';
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, unsigned int n)
{
    char *d = dest;
    while (*d) d++;
    for (unsigned int i = 0; i < n && src[i]; i++)
        d[i] = src[i];
    d[n] = '\0';
    return dest;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
    const char *last = 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
    }
    return 0;
}
