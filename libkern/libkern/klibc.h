#ifndef KLIB_H
#define KLIB_H

#include "stdarg.h"

struct klibc {
    /* stdio */
    int (*printf)(const char *fmt, ...);
    int (*snprintf)(char *buf, unsigned int n, const char *fmt, ...);
    int (*vsnprintf)(char *buf, unsigned int n, const char *fmt, va_list ap);
    int (*sprintf)(char *buf, const char *fmt, ...);
    int (*putchar)(int c);
    int (*puts)(const char *s);
    int (*getchar)(void);
    int (*scanf)(const char *fmt, ...);
    int (*sscanf)(const char *buf, const char *fmt, ...);

    /* string */
    void *(*memcpy)(void *dest, const void *src, unsigned int n);
    void *(*memmove)(void *dest, const void *src, unsigned int n);
    void *(*memset)(void *s, int c, unsigned int n);
    int   (*memcmp)(const void *s1, const void *s2, unsigned int n);
    void *(*memchr)(const void *s, int c, unsigned int n);

    unsigned int (*strlen)(const char *s);
    int   (*strcmp)(const char *s1, const char *s2);
    int   (*strncmp)(const char *s1, const char *s2, unsigned int n);
    char *(*strcpy)(char *dest, const char *src);
    char *(*strncpy)(char *dest, const char *src, unsigned int n);
    char *(*strcat)(char *dest, const char *src);
    char *(*strncat)(char *dest, const char *src, unsigned int n);
    char *(*strchr)(const char *s, int c);
    char *(*strrchr)(const char *s, int c);
    char *(*strstr)(const char *haystack, const char *needle);

    /* stdlib */
    int  (*atoi)(const char *s);
    long (*atol)(const char *s);
    int  (*abs)(int n);
    long (*labs)(long n);
    double (*atof)(const char *s);
    double (*strtod)(const char *s, char **end);
    float  (*strtof)(const char *s, char **end);

    /* math */
    double (*fabs)(double x);
    double (*fmod)(double x, double y);
    double (*pow10)(int n);

    /* ctype */
    int (*isdigit)(int c);
    int (*isalpha)(int c);
    int (*isalnum)(int c);
    int (*isupper)(int c);
    int (*islower)(int c);
    int (*isspace)(int c);
    int (*isprint)(int c);
    int (*isxdigit)(int c);
    int (*toupper)(int c);
    int (*tolower)(int c);
};

extern struct klibc klibc;

#endif
