/* SPDX-License-Identifier: AGPL-3.0-only */
/* Shared POSIX/CRT compatibility library for /ported/developer/ apps */
#include "flair_posix.h"
#include "../../../../include/kmem64.h"
#include "../../../../include/vfs.h"

void *flair_malloc(size_t size) { return kmem64_alloc(size); }
void flair_free(void *ptr) { kfree(ptr); }

void *flair_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *flair_memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

size_t flair_strlen(const char *s) {
    size_t n = 0;
    while (s && s[n]) n++;
    return n;
}

int flair_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int flair_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

int flair_open(const char *path, int flags) { return vfs_open(path, (uint32_t)flags); }
int flair_read(int fd, void *buf, size_t count) { return vfs_read(fd, buf, (uint32_t)count); }
int flair_write(int fd, const void *buf, size_t count) { return vfs_write(fd, buf, (uint32_t)count); }
int flair_close(int fd) { return vfs_close(fd); }
