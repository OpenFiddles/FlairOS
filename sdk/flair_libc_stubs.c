#include "flair_posix.h"

int errno;

struct flair_timespec {
    long tv_sec;
    long tv_nsec;
};

void _exit(int code) {
    (void)code;
    flare_exit();
    for (;;) {}
}

void abort(void) {
    _exit(134);
}

int open(const char *path, int flags, ...) {
    return posix_open(path, (unsigned int)flags);
}

int read(int fd, void *buf, unsigned int count) {
    return posix_read(fd, buf, count);
}

int write(int fd, const void *buf, unsigned int count) {
    return posix_write(fd, buf, count);
}

int close(int fd) {
    return posix_close(fd);
}

int lseek(int fd, int offset, int whence) {
    return posix_lseek(fd, offset, whence);
}

int dup2(int oldfd, int newfd) {
    return posix_dup2(oldfd, newfd);
}

int isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int mkdir(const char *path, unsigned int mode) {
    (void)mode;
    return posix_mkdir(path);
}

int unlink(const char *path) {
    return posix_unlink(path);
}

int chdir(const char *path) {
    return posix_chdir(path);
}

char *getcwd(char *buf, unsigned int size) {
    return posix_getcwd(buf, size) == 0 ? buf : (char *)0;
}

int getpid(void) {
    return posix_getpid();
}

int sched_yield(void) {
    posix_yield();
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    posix_msleep(seconds * 1000u);
    return 0;
}

int usleep(unsigned int usec) {
    posix_msleep((usec + 999u) / 1000u);
    return 0;
}

void *mmap(void *addr, unsigned int len, int prot, int flags, int fd, int off) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    void *p = posix_mmap(len);
    return p ? p : (void *)(unsigned long)-1;
}

int munmap(void *addr, unsigned int len) {
    return posix_munmap(addr, len);
}

void *sbrk(int incr) {
    static unsigned int cur_brk = 0;
    if (!cur_brk) cur_brk = flare_call(34u, 0, 0, 0);
    if (incr == 0) return (void *)(unsigned long)cur_brk;
    unsigned int prev = cur_brk;
    unsigned int next = flare_call(34u, prev + (unsigned int)incr, 0, 0);
    if (next < prev + (unsigned int)incr) {
        errno = 12;
        return (void *)(unsigned long)-1;
    }
    cur_brk = next;
    return (void *)(unsigned long)prev;
}

long time(long *tloc) {
    long t = (long)posix_time();
    if (tloc) *tloc = t;
    return t;
}

int clock_gettime(int clk_id, struct flair_timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;
    tp->tv_sec = (long)posix_time();
    tp->tv_nsec = (long)((flare_ticks() % 100u) * 10000000u);
    return 0;
}

int getentropy(void *buf, unsigned int len) {
    return posix_getrandom(buf, len) == len ? 0 : -1;
}

char *getenv(const char *name) {
    (void)name;
    return (char *)0;
}

int flairos_set_tls(void *base) {
    return posix_set_tls(base);
}

/* Single-thread pthread stubs for libstd / C++ / Python bring-up */
unsigned int pthread_self(void) { return (unsigned int)getpid(); }
int pthread_mutex_init(void *m, const void *a) { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void *m) { (void)m; return 0; }
int pthread_mutex_unlock(void *m) { (void)m; return 0; }
int pthread_mutex_destroy(void *m) { (void)m; return 0; }

#include "sys/stat.h"
#include "dirent.h"

int fstat(int fd, struct stat *st) {
    if (fd < 0 || !st) return -1;
    int cur = posix_lseek(fd, 0, 1);
    int end = posix_lseek(fd, 0, 2);
    if (cur >= 0) (void)posix_lseek(fd, cur, 0);
    st->st_dev = 1;
    st->st_ino = (unsigned long)fd + 1;
    st->st_mode = S_IFREG | 0644;
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_size = (off_t)(end >= 0 ? end : 0);
    st->st_mtime = 0;
    return 0;
}
int stat(const char *path, struct stat *st) {
    if (!path || !st) return -1;
    int fd = posix_open(path, 0);
    if (fd < 0) return -1;
    int rc = fstat(fd, st);
    posix_close(fd);
    return rc;
}
DIR *opendir(const char *path) { (void)path; return (DIR *)0; }
struct dirent *readdir(DIR *d) { (void)d; return (struct dirent *)0; }
int closedir(DIR *d) { (void)d; return 0; }
