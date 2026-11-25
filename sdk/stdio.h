#ifndef FLARE_C_STDIO_H
#define FLARE_C_STDIO_H

/*
 * Console-only stdio subset. This is not a hosted FILE/stream implementation.
 * Use flare_file_t and flare_f* helpers from flare_libc.h for kernel files.
 * getchar() is nonblocking and returns EOF when no key is waiting.
 */
#include <stdarg.h>
#include "stddef.h"
#include "flareos.h"
#include "flare_stdlib.h"

#define EOF (-1)
#define BUFSIZ 512

static inline int vsnprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    return flare_vsnprintf(out, cap, fmt, ap);
}
static inline int snprintf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = flare_vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return n;
}
static inline int vprintf(const char *fmt, va_list ap) {
    char buffer[BUFSIZ];
    int n = flare_vsnprintf(buffer, sizeof buffer, fmt, ap);
    flare_print(buffer);
    return n;
}
static inline int printf(const char *fmt, ...) {
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}
static inline int putchar(int ch) {
    flare_putc((char)ch);
    return (unsigned char)ch;
}
static inline int puts(const char *s) {
    flare_print(s);
    flare_putc('\n');
    return 0;
}
static inline int getchar(void) {
    int ch = flare_getkey();
    return ch < 0 ? EOF : ch;
}

typedef struct { int fd; } FILE;
static FILE _flair_stdout = { 1 }, _flair_stderr = { 2 };
#define stdout (&_flair_stdout)
#define stderr (&_flair_stderr)
static inline int sprintf(char *out, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = flare_vsnprintf(out, 4096u, fmt, ap);
    va_end(ap); return n;
}
static inline int fprintf(FILE *f, const char *fmt, ...) {
    (void)f; char buf[BUFSIZ]; va_list ap; va_start(ap, fmt);
    int n = flare_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap); flare_print(buf); return n;
}

#endif
