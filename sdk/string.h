#ifndef FLARE_C_STRING_H
#define FLARE_C_STRING_H
#include "flare_string.h"
typedef flare_size_t size_t;
static inline void *memset(void *s, int c, size_t n) { return flare_memset(s,c,n); }
static inline void *memcpy(void *d,const void *s,size_t n) { return flare_memcpy(d,s,n); }
static inline void *memmove(void *d,const void *s,size_t n) { return flare_memmove(d,s,n); }
static inline int memcmp(const void *a,const void *b,size_t n) { return flare_memcmp(a,b,n); }
static inline size_t strlen(const char *s) { return flare_strlen(s); }
static inline int strcmp(const char *a,const char *b) { return flare_strcmp(a,b); }
static inline size_t strlcpy(char *d,const char *s,size_t n) { return flare_strlcpy(d,s,n); }
#endif
