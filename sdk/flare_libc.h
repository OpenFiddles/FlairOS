#ifndef FLARE_LIBC_H
#define FLARE_LIBC_H

/*
 * FlareOS C helpers for apps (SDK 1.2): stream-style file calls over the kernel
 * file API, plus number printing. Pure helpers live in flare_string.h.
 */

#include "flareos.h"
#include "flare_string.h"

typedef struct {
    flare_u32 handle;
} flare_file_t;

/* Mode is 'r' (read), 'w' (write, truncates) or 'a' (append). Returns 0, or -1 on failure. */
static inline int flare_fopen(flare_file_t *f, const char *name, char mode) {
    flare_u32 m;
    switch (mode) {
    case 'r': m = FLARE_FS_READ; break;
    case 'w': m = FLARE_FS_WRITE; break;
    case 'a': m = FLARE_FS_APPEND; break;
    default:
        f->handle = FLARE_FS_FAIL;
        return -1;
    }
    f->handle = flare_fs_open(name, m);
    return f->handle == FLARE_FS_FAIL ? -1 : 0;
}

/* Returns the number of bytes read; 0 at end of file or on failure. */
static inline flare_u32 flare_fread(flare_file_t *f, void *buf, flare_u32 max) {
    flare_u32 n = flare_fs_read(f->handle, buf, max);
    return n == FLARE_FS_FAIL ? 0 : n;
}

/* Returns the number of bytes written; 0 on failure. */
static inline flare_u32 flare_fwrite(flare_file_t *f, const void *buf, flare_u32 count) {
    flare_u32 n = flare_fs_write(f->handle, buf, count);
    return n == FLARE_FS_FAIL ? 0 : n;
}

/* Returns 0 when the whole string was written, -1 otherwise. */
static inline int flare_fputs(flare_file_t *f, const char *s) {
    flare_u32 len = flare_strlen(s);
    return flare_fwrite(f, s, len) == len ? 0 : -1;
}

static inline flare_u32 flare_fsize(flare_file_t *f) {
    flare_u32 n = flare_fs_size(f->handle);
    return n == FLARE_FS_FAIL ? 0 : n;
}

static inline int flare_fseek(flare_file_t *f, flare_u32 pos) {
    return flare_fs_seek(f->handle, pos) == FLARE_FS_FAIL ? -1 : 0;
}

static inline void flare_fclose(flare_file_t *f) {
    if (f->handle != FLARE_FS_FAIL) {
        flare_fs_close(f->handle);
        f->handle = FLARE_FS_FAIL;
    }
}

static inline void flare_print_uint(flare_u32 v) {
    char digits[11];
    flare_utoa(v, digits);
    flare_print(digits);
}

static inline void flare_print_int(flare_i32 v) {
    char digits[12];
    flare_itoa(v, digits);
    flare_print(digits);
}

#endif
