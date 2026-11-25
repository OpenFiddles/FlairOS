#ifndef TEXTILE_H
#define TEXTILE_H

/* App ABI: a flat binary linked at TX_APP_BASE whose first two words are
   TX_APP_MAGIC and the address of app_main. Syscalls use int 0x80:
   eax = number, ebx/ecx/edx = arguments, return value in eax. */
#define TX_APP_MAGIC 0x54505041u
#define TX_APP_BASE 0x40000u

enum tx_syscall {
    SYS_PUTC = 1,
    SYS_PUTAT = 2,
    SYS_CLEAR = 3,
    SYS_TICKS = 4,
    SYS_SLEEP = 5,
    SYS_TONE = 6,
    SYS_ANIM_START = 7,
    SYS_ANIM_VALUE = 8,
    SYS_GETKEY = 9,
    SYS_PRINT = 10,
    SYS_EXIT = 11,
    SYS_GETARROW = 12,
    SYS_CURSOR = 13,
    /* 14-20 belong to AXI window management; 21-28 belong to filesystem I/O.
       Keep this input-state query outside those public syscall ranges. */
    SYS_TOGGLE_PENDING = 37,
    SYS_TIME = 39,
    SYS_GETRANDOM = 41,
    SYS_YIELD = 42,
    SYS_GETPID = 43,
    SYS_TASK_COUNT = 44,
    SYS_MMAP = 45,
    SYS_MUNMAP = 46,
    SYS_VFS_OPEN = 47,
    SYS_VFS_READ = 48,
    SYS_VFS_WRITE = 49,
    SYS_VFS_CLOSE = 50,
    SYS_VFS_MKDIR = 51,
    SYS_VFS_READDIR = 52,
    SYS_VFS_STAT = 53,
    SYS_VFS_CHDIR = 54,
    SYS_VFS_GETCWD = 55,
    SYS_VFS_UNLINK = 56,
    SYS_NET_PING = 57,
    SYS_NET_INFO = 58,
    SYS_SET_TLS = 62,
    SYS_NANOSLEEP = 63,
    SYS_DUP2 = 64,
    SYS_LSEEK = 65,
    SYS_FUTEX_WAIT = 66,
    SYS_FUTEX_WAKE = 67,
    SYS_ASHMEM_CREATE = 68,
    SYS_ASHMEM_MAP = 69,
    SYS_BINDER_REG = 70,
    SYS_BINDER_LOOKUP = 71,
    SYS_BINDER_TX = 72
};

/* Arrow codes returned by SYS_GETARROW (-1 when none is waiting). */
#define ARROW_UP 1
#define ARROW_DOWN 2
#define ARROW_LEFT 3
#define ARROW_RIGHT 4
#define ARROW_PAGE_UP 5
#define ARROW_PAGE_DOWN 6

enum tx_ease { EASE_LINEAR = 0, EASE_OUT_CUBIC = 1, EASE_IN_OUT = 2 };

#define SYS_POINTER 29 /* eax = x | y << 8 | buttons << 16 */
#define SYS_SCREEN 30 /* eax = cols | rows << 8 */
#define SYS_BRK 34 /* ebx = new break (0 queries); returns the break, below the request on failure */


struct flare_time {
    unsigned char sec;
    unsigned char min;
    unsigned char hour;
    unsigned char day;
    unsigned char month;
    unsigned char pad;
    unsigned short year;
};

#endif
