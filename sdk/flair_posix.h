#ifndef FLAIROS_POSIX_H
#define FLAIROS_POSIX_H

#include "flareos.h"

/* Minimal POSIX / newlib syscall bridge over FlairOS int 0x80. */
#define FLARE_SYS_VFS_OPEN    47u
#define FLARE_SYS_VFS_READ    48u
#define FLARE_SYS_VFS_WRITE   49u
#define FLARE_SYS_VFS_CLOSE   50u
#define FLARE_SYS_VFS_MKDIR   51u
#define FLARE_SYS_VFS_READDIR 52u
#define FLARE_SYS_VFS_STAT    53u
#define FLARE_SYS_VFS_CHDIR   54u
#define FLARE_SYS_VFS_GETCWD  55u
#define FLARE_SYS_VFS_UNLINK  56u

static inline int posix_open(const char *path, unsigned int flags) {
    return (int)flare_call(FLARE_SYS_VFS_OPEN, (flare_u32)(unsigned long)path, flags, 0);
}
static inline int posix_read(int fd, void *buf, flare_u32 count) {
    return (int)flare_call(FLARE_SYS_VFS_READ, (flare_u32)fd, (flare_u32)(unsigned long)buf, count);
}
static inline int posix_write(int fd, const void *buf, flare_u32 count) {
    return (int)flare_call(FLARE_SYS_VFS_WRITE, (flare_u32)fd, (flare_u32)(unsigned long)buf, count);
}
static inline int posix_close(int fd) {
    return (int)flare_call(FLARE_SYS_VFS_CLOSE, (flare_u32)fd, 0, 0);
}
static inline int posix_mkdir(const char *path) {
    return (int)flare_call(FLARE_SYS_VFS_MKDIR, (flare_u32)(unsigned long)path, 0, 0);
}
static inline int posix_unlink(const char *path) {
    return (int)flare_call(FLARE_SYS_VFS_UNLINK, (flare_u32)(unsigned long)path, 0, 0);
}
static inline int posix_chdir(const char *path) {
    return (int)flare_call(FLARE_SYS_VFS_CHDIR, (flare_u32)(unsigned long)path, 0, 0);
}
static inline int posix_getcwd(char *buf, flare_u32 size) {
    return (int)flare_call(FLARE_SYS_VFS_GETCWD, (flare_u32)(unsigned long)buf, size, 0);
}
static inline int posix_getpid(void) { return flare_getpid(); }
static inline void posix_yield(void) { flare_yield(); }
static inline void *posix_mmap(flare_u32 len) { return flare_mmap(len); }
static inline int posix_munmap(void *p, flare_u32 len) { return flare_munmap(p, len); }
static inline flare_u32 posix_time(void) { return flare_time_unix(); }
static inline flare_u32 posix_getrandom(void *buf, flare_u32 len) { return flare_getrandom(buf, len); }

static inline int posix_set_tls(void *base) {
    return (int)flare_call(FLARE_SYS_SET_TLS, (flare_u32)(unsigned long)base, 0, 0);
}

static inline int posix_msleep(flare_u32 ms) {
    return (int)flare_call(FLARE_SYS_NANOSLEEP, ms, 0, 0);
}
static inline int posix_dup2(int oldfd, int newfd) {
    return (int)flare_call(FLARE_SYS_DUP2, (flare_u32)oldfd, (flare_u32)newfd, 0);
}
static inline int posix_lseek(int fd, int offset, int whence) {
    return (int)flare_call(FLARE_SYS_LSEEK, (flare_u32)fd, (flare_u32)offset, (flare_u32)whence);
}

#endif
