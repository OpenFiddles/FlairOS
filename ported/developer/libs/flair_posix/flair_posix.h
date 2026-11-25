#ifndef FLAIROS_PORTED_POSIX_H
#define FLAIROS_PORTED_POSIX_H

#include <stddef.h>
#include <stdint.h>

void *flair_malloc(size_t size);
void flair_free(void *ptr);
void *flair_memcpy(void *dst, const void *src, size_t n);
void *flair_memset(void *dst, int c, size_t n);
size_t flair_strlen(const char *s);
int flair_strcmp(const char *a, const char *b);
int flair_strncmp(const char *a, const char *b, size_t n);

int flair_open(const char *path, int flags);
int flair_read(int fd, void *buf, size_t count);
int flair_write(int fd, const void *buf, size_t count);
int flair_close(int fd);

#endif
