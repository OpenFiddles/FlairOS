#ifndef FLARE_C_STDLIB_H
#define FLARE_C_STDLIB_H
#include "stddef.h"
static inline int abs(int n) { return n < 0 ? -n : n; }
static inline int atoi(const char *s) {
    int sign = 1, value = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') ++s;
    if (*s == '-' || *s == '+') { if (*s++ == '-') sign = -1; }
    while (*s >= '0' && *s <= '9') value = value * 10 + (*s++ - '0');
    return sign * value;
}
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif
