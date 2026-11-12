#ifndef FLARE_STRING_H
#define FLARE_STRING_H

/*
 * FlareOS string, memory and heap helpers (SDK 1.2).
 * Header-only and free of syscalls, so apps and host tests compile the same code.
 * The heap is a bump allocator over a buffer the app supplies: blocks are never
 * freed one at a time, only all at once with flare_heap_reset.
 */

typedef unsigned int flare_size_t;

static inline void *flare_memset(void *dst, int value, flare_size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)value;
    return dst;
}

static inline void *flare_memcpy(void *dst, const void *src, flare_size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static inline void *flare_memmove(void *dst, const void *src, flare_size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

static inline int flare_memcmp(const void *a, const void *b, flare_size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (; n; n--, p++, q++) {
        if (*p != *q)
            return *p < *q ? -1 : 1;
    }
    return 0;
}

static inline flare_size_t flare_strlen(const char *s) {
    flare_size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static inline int flare_strcmp(const char *a, const char *b) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    while (*p && *p == *q) {
        p++;
        q++;
    }
    if (*p == *q)
        return 0;
    return *p < *q ? -1 : 1;
}

/* Copies at most n-1 characters and always terminates when n is nonzero.
   Returns strlen(src), so a result >= n means the copy was cut short. */
static inline flare_size_t flare_strlcpy(char *dst, const char *src, flare_size_t n) {
    flare_size_t len = flare_strlen(src);
    if (n) {
        flare_size_t k = len < n - 1 ? len : n - 1;
        flare_memcpy(dst, src, k);
        dst[k] = 0;
    }
    return len;
}

/* Writes v in decimal with a terminating NUL. out needs 11 bytes.
   Returns the number of digits. */
static inline flare_size_t flare_utoa(unsigned int v, char *out) {
    char tmp[10];
    flare_size_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (flare_size_t i = 0; i < n; i++)
        out[i] = tmp[n - 1 - i];
    out[n] = 0;
    return n;
}

/* Signed version of flare_utoa. out needs 12 bytes. Returns the length written, without the NUL. */
static inline flare_size_t flare_itoa(int v, char *out) {
    if (v < 0) {
        out[0] = '-';
        return 1 + flare_utoa(0u - (unsigned int)v, out + 1);
    }
    return flare_utoa((unsigned int)v, out);
}

/* The arena must be 8-byte aligned, e.g. __attribute__((aligned(8))). */
typedef struct {
    unsigned char *base;
    flare_size_t size;
    flare_size_t used;
} flare_heap_t;

static inline void flare_heap_init(flare_heap_t *h, void *buf, flare_size_t size) {
    h->base = (unsigned char *)buf;
    h->size = size;
    h->used = 0;
}

/* Returns NULL when the arena cannot hold n more bytes. Blocks are 8-byte aligned. */
static inline void *flare_heap_alloc(flare_heap_t *h, flare_size_t n) {
    flare_size_t start = (h->used + 7u) & ~7u;
    if (start > h->size || n > h->size - start)
        return 0;
    h->used = start + n;
    return h->base + start;
}

static inline void *flare_heap_zalloc(flare_heap_t *h, flare_size_t n) {
    void *p = flare_heap_alloc(h, n);
    if (p)
        flare_memset(p, 0, n);
    return p;
}

/* Frees everything allocated from this heap. */
static inline void flare_heap_reset(flare_heap_t *h) {
    h->used = 0;
}

static inline flare_size_t flare_heap_free_bytes(const flare_heap_t *h) {
    return h->size - h->used;
}

#endif
