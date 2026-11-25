/* SPDX-License-Identifier: AGPL-3.0-only */
/* 64-bit Linux/Bionic-compatible Syscall Dispatch Layer for FlairOS */
#include "syscall64.h"
#include "../include/vfs.h"
#include "../include/kmem64.h"
#include "../include/hal.h"
#include "../runtime/art/sandbox.h"

static struct syscall64_stats g_sc_stats = {0, 0, 1001};

int64_t syscall64_dispatch(uint64_t nr, uint64_t a0, uint64_t a1, uint64_t a2,
                           uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    g_sc_stats.total_calls++;

    switch (nr) {
        case FLAIR_SYS_read:
            return (int64_t)vfs_read((int)a0, (void *)(uintptr_t)a1, (uint32_t)a2);
        case FLAIR_SYS_write: {
            int fd = (int)a0;
            const char *buf = (const char *)(uintptr_t)a1;
            uint32_t len = (uint32_t)a2;
            if (fd == 1 || fd == 2) {
                for (uint32_t i = 0; i < len; i++) {
#ifdef FLAIROS_HOST_TEST
                    (void)buf;
#else
                    hal_serial_putc(buf[i]);
#endif
                }
                return (int64_t)len;
            }
            return (int64_t)vfs_write(fd, buf, len);
        }
        case FLAIR_SYS_openat: {
            const char *path = (const char *)(uintptr_t)a1;
            uint32_t flags = (uint32_t)a2;
            int wr = (flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC)) != 0;
            if (sandbox_check_path(sandbox_current_uid(), path, wr) != 0) return -13; /* -EACCES */
            return (int64_t)vfs_open(path, flags);
        }
        case FLAIR_SYS_close:
            return (int64_t)vfs_close((int)a0);
        case FLAIR_SYS_mmap: {
            void *p = kmem64_alloc((size_t)a1);
            return p ? (int64_t)(uintptr_t)p : -12; /* -ENOMEM */
        }
        case FLAIR_SYS_munmap:
            kfree((void *)(uintptr_t)a0);
            return 0;
        case FLAIR_SYS_brk:
            return (int64_t)(uintptr_t)kmem64_alloc(4096u);
        case FLAIR_SYS_getpid:
            return (int64_t)g_sc_stats.active_pid;
        case FLAIR_SYS_getuid:
            return (int64_t)sandbox_current_uid();
        case FLAIR_SYS_clock_gettime: {
            uint64_t *ts = (uint64_t *)(uintptr_t)a1;
            if (!ts) return -14;
            uint32_t t = 0;
#ifndef FLAIROS_HOST_TEST
            t = hal_timer_ticks();
#endif
            ts[0] = (uint64_t)(t / 1000u);
            ts[1] = (uint64_t)((t % 1000u) * 1000000u);
            return 0;
        }
        case FLAIR_SYS_futex:
            return 0;
        case FLAIR_SYS_exit_group:
            g_sc_stats.last_exit_code = (int)a0;
            return 0;
        default:
            return -38; /* -ENOSYS */
    }
}

const struct syscall64_stats *syscall64_get_stats(void) {
    return &g_sc_stats;
}
