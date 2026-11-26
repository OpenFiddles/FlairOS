#ifndef FLAIROS_SYSCALL64_H
#define FLAIROS_SYSCALL64_H

#include <stdint.h>

#define FLAIR_SYS_read          63u
#define FLAIR_SYS_write         64u
#define FLAIR_SYS_openat        56u
#define FLAIR_SYS_close         57u
#define FLAIR_SYS_brk           214u
#define FLAIR_SYS_munmap        215u
#define FLAIR_SYS_mmap          222u
#define FLAIR_SYS_getpid        172u
#define FLAIR_SYS_getuid        174u
#define FLAIR_SYS_clock_gettime 113u
#define FLAIR_SYS_futex         98u
#define FLAIR_SYS_exit_group    94u

struct syscall64_stats {
    uint64_t total_calls;
    int last_exit_code;
    int active_pid;
};

int64_t syscall64_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                           uint64_t a3, uint64_t a4, uint64_t a5);
const struct syscall64_stats *syscall64_get_stats(void);

#endif
