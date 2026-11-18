#ifndef FLARE_C_STRING_H
#define FLARE_C_STRING_H
#include "stddef.h"
#include "flare_stdlib.h"

static inline void *memset(void *s, int c, size_t n) { return flare_memset(s, c, n); }
static inline void *memcpy(void *d, const void *s, size_t n) { return flare_memcpy(d, s, n); }
static inline void *memmove(void *d, const void *s, size_t n) { return flare_memmove(d, s, n); }
static inline int memcmp(const void *a, const void *b, size_t n) { return flare_memcmp(a, b, n); }
static inline void *memchr(const void *s, int c, size_t n) { return flare_memchr(s, c, n); }

static inline size_t strlen(const char *s) { return flare_strlen(s); }
static inline int strcmp(const char *a, const char *b) { return flare_strcmp(a, b); }
static inline int strncmp(const char *a, const char *b, size_t n) { return flare_strncmp(a, b, n); }
static inline char *strcpy(char *d, const char *s) {
    size_t n = flare_strlen(s) + 1u;
    flare_memcpy(d, s, n);
    return d;
}
static inline char *strncpy(char *d, const char *s, size_t n) { return flare_strncpy(d, s, n); }
static inline char *strcat(char *d, const char *s) { return flare_strcat(d, s); }
static inline char *strncat(char *d, const char *s, size_t n) { return flare_strncat(d, s, n); }
static inline char *strchr(const char *s, int c) { return flare_strchr(s, c); }
static inline char *strrchr(const char *s, int c) { return flare_strrchr(s, c); }
static inline char *strstr(const char *s, const char *n) { return flare_strstr(s, n); }
static inline size_t strlcpy(char *d, const char *s, size_t n) { return flare_strlcpy(d, s, n); }

#endif
