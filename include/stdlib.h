#ifndef STDLIB_H
#define STDLIB_H

int atoi(const char *s);
long atol(const char *s);
int abs(int n);
long labs(long n);

double atof(const char *s);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);

#endif
