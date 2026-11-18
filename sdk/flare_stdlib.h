#ifndef FLARE_STDLIB_H
#define FLARE_STDLIB_H

/*
 * FlareOS C library, part 2 (SDK 1.3): string search, ctype, number parsing,
 * sorting and a printf-style formatter. Header-only and free of syscalls.
 * Allocation and floating-point math are not here yet.
 */

#include <stdarg.h>
#include "flare_string.h"

#define FLARE_EDOM 33
#define FLARE_ERANGE 34
#define FLARE_EINVAL 22

extern int flare_errno;

#define FLARE_ULONG_MAX (~0UL)
#define FLARE_LONG_MAX ((long)(FLARE_ULONG_MAX >> 1))
#define FLARE_LONG_MIN (-FLARE_LONG_MAX - 1L)

static inline int flare_isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int flare_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int flare_islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int flare_isalpha(int c) { return flare_isupper(c) || flare_islower(c); }
static inline int flare_isalnum(int c) { return flare_isalpha(c) || flare_isdigit(c); }
static inline int flare_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int flare_isxdigit(int c) {
    return flare_isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline int flare_isprint(int c) { return c >= ' ' && c <= '~'; }
static inline int flare_toupper(int c) { return flare_islower(c) ? c - 'a' + 'A' : c; }
static inline int flare_tolower(int c) { return flare_isupper(c) ? c - 'A' + 'a' : c; }

static inline int flare_abs(int v) { return v < 0 ? -v : v; }
static inline long flare_labs(long v) { return v < 0 ? -v : v; }

static inline void *flare_memchr(const void *s, int c, flare_size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return 0;
}

static inline char *flare_strncpy(char *dst, const char *src, flare_size_t n) {
    flare_size_t i;
    for (i = 0; i < n && src[i]; i++)
        dst[i] = src[i];
    for (; i < n; i++)
        dst[i] = 0;
    return dst;
}

static inline int flare_strncmp(const char *a, const char *b, flare_size_t n) {
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (; n; n--, p++, q++) {
        if (*p != *q)
            return *p < *q ? -1 : 1;
        if (!*p)
            return 0;
    }
    return 0;
}

static inline char *flare_strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}

static inline char *flare_strrchr(const char *s, int c) {
    const char *last = 0;
    for (;; s++) {
        if (*s == (char)c)
            last = s;
        if (!*s)
            return (char *)last;
    }
}

static inline char *flare_strstr(const char *hay, const char *needle) {
    if (!*needle)
        return (char *)hay;
    for (; *hay; hay++) {
        const char *a = hay;
        const char *b = needle;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (!*b)
            return (char *)hay;
    }
    return 0;
}

static inline char *flare_strcat(char *dst, const char *src) {
    flare_memcpy(dst + flare_strlen(dst), src, flare_strlen(src) + 1);
    return dst;
}

static inline char *flare_strncat(char *dst, const char *src, flare_size_t n) {
    flare_size_t dl = flare_strlen(dst);
    flare_size_t k = 0;
    while (k < n && src[k])
        k++;
    flare_memcpy(dst + dl, src, k);
    dst[dl + k] = 0;
    return dst;
}

/* Core of strtoul/strtol. Returns the magnitude; sets *neg and *overflow. */
static inline unsigned long flare_parse_mag(const char *s, char **end, int base,
                                            int *neg, int *overflow) {
    const char *p = s;
    unsigned long acc = 0;
    int any = 0;
    *neg = 0;
    *overflow = 0;
    while (flare_isspace((unsigned char)*p))
        p++;
    if (*p == '+' || *p == '-') {
        *neg = *p == '-';
        p++;
    }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (base == 0) {
        base = p[0] == '0' ? 8 : 10;
    }
    if (base < 2 || base > 36) {
        flare_errno = FLARE_EINVAL;
        if (end)
            *end = (char *)s;
        return 0;
    }
    for (;; p++) {
        int c = (unsigned char)*p;
        int d;
        if (flare_isdigit(c))
            d = c - '0';
        else if (flare_isalpha(c))
            d = flare_tolower(c) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        if (acc > (FLARE_ULONG_MAX - (unsigned long)d) / (unsigned long)base)
            *overflow = 1;
        else
            acc = acc * (unsigned long)base + (unsigned long)d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return acc;
}

static inline unsigned long flare_strtoul(const char *s, char **end, int base) {
    int neg, overflow;
    unsigned long mag = flare_parse_mag(s, end, base, &neg, &overflow);
    if (overflow) {
        flare_errno = FLARE_ERANGE;
        return FLARE_ULONG_MAX;
    }
    return neg ? 0UL - mag : mag;
}

static inline long flare_strtol(const char *s, char **end, int base) {
    int neg, overflow;
    unsigned long mag = flare_parse_mag(s, end, base, &neg, &overflow);
    if (overflow || (!neg && mag > (unsigned long)FLARE_LONG_MAX) ||
        (neg && mag > (unsigned long)FLARE_LONG_MAX + 1UL)) {
        flare_errno = FLARE_ERANGE;
        return neg ? FLARE_LONG_MIN : FLARE_LONG_MAX;
    }
    return neg ? (long)(0UL - mag) : (long)mag;
}

static inline int flare_atoi(const char *s) {
    return (int)flare_strtol(s, 0, 10);
}

static inline void flare_swap_bytes(unsigned char *a, unsigned char *b, flare_size_t n) {
    while (n--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

static inline void flare_sift_down(unsigned char *base, flare_size_t root, flare_size_t n,
                                   flare_size_t size,
                                   int (*cmp)(const void *, const void *)) {
    for (;;) {
        flare_size_t child = 2 * root + 1;
        if (child >= n)
            break;
        if (child + 1 < n && cmp(base + child * size, base + (child + 1) * size) < 0)
            child++;
        if (cmp(base + root * size, base + child * size) >= 0)
            break;
        flare_swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}

/* In-place heapsort: no allocation, not stable, O(n log n). */
static inline void flare_qsort(void *base, flare_size_t nmemb, flare_size_t size,
                               int (*cmp)(const void *, const void *)) {
    unsigned char *b = (unsigned char *)base;
    flare_size_t i;
    if (nmemb < 2 || size == 0)
        return;
    for (i = nmemb / 2; i-- > 0;)
        flare_sift_down(b, i, nmemb, size, cmp);
    for (i = nmemb - 1; i > 0; i--) {
        flare_swap_bytes(b, b + i * size, size);
        flare_sift_down(b, 0, i, size, cmp);
    }
}

static inline void *flare_bsearch(const void *key, const void *base, flare_size_t nmemb,
                                  flare_size_t size, int (*cmp)(const void *, const void *)) {
    const unsigned char *b = (const unsigned char *)base;
    flare_size_t lo = 0, hi = nmemb;
    while (lo < hi) {
        flare_size_t mid = lo + (hi - lo) / 2;
        const unsigned char *p = b + mid * size;
        int c = cmp(key, p);
        if (c == 0)
            return (void *)p;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

static inline void flare_fmt_emit(char *out, flare_size_t cap, flare_size_t *pos,
                                  const char *s, flare_size_t len) {
    flare_size_t i;
    for (i = 0; i < len; i++) {
        if (out && *pos + 1 < cap)
            out[*pos] = s[i];
        (*pos)++;
    }
}

static inline void flare_fmt_field(char *out, flare_size_t cap, flare_size_t *pos,
                                   const char *pre, flare_size_t plen,
                                   const char *body, flare_size_t blen,
                                   int width, int left, int zero) {
    flare_size_t total = plen + blen;
    flare_size_t pad = (flare_size_t)width > total ? (flare_size_t)width - total : 0;
    if (!left && !zero)
        for (; pad; pad--)
            flare_fmt_emit(out, cap, pos, " ", 1);
    flare_fmt_emit(out, cap, pos, pre, plen);
    if (!left && zero)
        for (; pad; pad--)
            flare_fmt_emit(out, cap, pos, "0", 1);
    flare_fmt_emit(out, cap, pos, body, blen);
    if (left)
        for (; pad; pad--)
            flare_fmt_emit(out, cap, pos, " ", 1);
}

static inline void flare_fmt_integer(char *out, flare_size_t cap, flare_size_t *pos,
                                     unsigned long u, unsigned int base, int upper,
                                     int neg, int hex_pfx, int width, int left,
                                     int zero, int prec) {
    static const char lo[] = "0123456789abcdef";
    static const char hi[] = "0123456789ABCDEF";
    const char *digits = upper ? hi : lo;
    char dig[24];
    char body[64];
    char pfx[3];
    flare_size_t dl = 0, bl = 0, pl = 0, zeros;
    if (prec > 40)
        prec = 40;
    do {
        dig[dl++] = digits[u % base];
        u /= base;
    } while (u);
    zeros = prec > (int)dl ? (flare_size_t)(prec - (int)dl) : 0;
    while (zeros--)
        body[bl++] = '0';
    while (dl)
        body[bl++] = dig[--dl];
    if (neg)
        pfx[pl++] = '-';
    if (hex_pfx) {
        pfx[pl++] = '0';
        pfx[pl++] = 'x';
    }
    flare_fmt_field(out, cap, pos, pfx, pl, body, bl, width, left, zero && prec < 0);
}

/* printf subset: %% %c %s %d %i %u %o %x %X %p, flags - and 0, width, .precision,
   and l/z length (same width as int on i386). Returns the length that would be
   written, like snprintf. */
static inline int flare_vsnprintf(char *out, flare_size_t cap, const char *fmt, va_list ap) {
    flare_size_t pos = 0;
    while (*fmt) {
        int left = 0, zero = 0, width = 0, prec = -1, is_long = 0;
        char conv;
        if (*fmt != '%') {
            flare_fmt_emit(out, cap, &pos, fmt++, 1);
            continue;
        }
        fmt++;
        for (;;) {
            if (*fmt == '-') {
                left = 1;
                fmt++;
            } else if (*fmt == '0') {
                zero = 1;
                fmt++;
            } else {
                break;
            }
        }
        while (flare_isdigit((unsigned char)*fmt))
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (flare_isdigit((unsigned char)*fmt))
                prec = prec * 10 + (*fmt++ - '0');
        }
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') {
            if (*fmt != 'h')
                is_long = 1;
            fmt++;
        }
        if (!*fmt)
            break;
        conv = *fmt++;
        switch (conv) {
        case '%':
            flare_fmt_emit(out, cap, &pos, "%", 1);
            break;
        case 'c': {
            char ch = (char)va_arg(ap, int);
            flare_fmt_field(out, cap, &pos, "", 0, &ch, 1, width, left, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            flare_size_t len;
            if (!s)
                s = "(null)";
            len = flare_strlen(s);
            if (prec >= 0 && len > (flare_size_t)prec)
                len = (flare_size_t)prec;
            flare_fmt_field(out, cap, &pos, "", 0, s, len, width, left, 0);
            break;
        }
        case 'd':
        case 'i': {
            long v = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
            int neg = v < 0;
            flare_fmt_integer(out, cap, &pos, neg ? 0UL - (unsigned long)v : (unsigned long)v,
                              10, 0, neg, 0, width, left, zero, prec);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            unsigned long u = is_long ? va_arg(ap, unsigned long)
                                      : (unsigned long)va_arg(ap, unsigned int);
            unsigned int base = conv == 'u' ? 10 : conv == 'o' ? 8 : 16;
            flare_fmt_integer(out, cap, &pos, u, base, conv == 'X', 0, 0, width, left, zero, prec);
            break;
        }
        case 'p': {
            unsigned long u = (unsigned long)va_arg(ap, void *);
            flare_fmt_integer(out, cap, &pos, u, 16, 0, 0, 1, width, left, zero, prec);
            break;
        }
        default:
            flare_fmt_emit(out, cap, &pos, "%", 1);
            flare_fmt_emit(out, cap, &pos, &conv, 1);
            break;
        }
    }
    if (cap)
        out[pos < cap ? pos : cap - 1] = 0;
    return (int)pos;
}

static inline int flare_snprintf(char *out, flare_size_t cap, const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = flare_vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}

#endif
