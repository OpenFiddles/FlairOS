#ifndef FLARE_C_STDLIB_H
#define FLARE_C_STDLIB_H

#include "stddef.h"
#include "flare_stdlib.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* Implemented by userland/rt.c. */
int abs(int n);
int atoi(const char *s);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void abort(void);
void _exit(int code);
static inline void exit(int code) { _exit(code); }

static inline long labs(long n) { return flare_labs(n); }
static inline long strtol(const char *s, char **end, int base) {
    return flare_strtol(s, end, base);
}
static inline unsigned long strtoul(const char *s, char **end, int base) {
    return flare_strtoul(s, end, base);
}
static inline void qsort(void *base, size_t count, size_t size,
                         int (*compare)(const void *, const void *)) {
    flare_qsort(base, count, size, compare);
}
static inline void *bsearch(const void *key, const void *base, size_t count,
                            size_t size, int (*compare)(const void *, const void *)) {
    return flare_bsearch(key, base, count, size, compare);
}
static inline long atol(const char *s) { return strtol(s, 0, 10); }

/* Small deterministic PRNG for non-security uses; state lives in rt.c. */
#define RAND_MAX 32767
void srand(unsigned int seed);
int rand(void);

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
static inline div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}
static inline ldiv_t ldiv(long numer, long denom) {
    ldiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

#endif
