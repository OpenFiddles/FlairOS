#ifndef FLAREOS_MICROPYTHON_ABI_H
#define FLAREOS_MICROPYTHON_ABI_H
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define TX_APP_MAGIC 0x54505041u
#define SYS_PUTC 1u
#define SYS_CLEAR 3u
#define SYS_TICKS 4u
#define SYS_SLEEP 5u
#define SYS_TONE 6u
#define SYS_GETKEY 9u
#define SYS_GETARROW 12u
#define SYS_POINTER 29u
#define SYS_SCREEN 30u
#define SYS_EXIT 11u
#define SYS_BRK 34u
#define VGA_COLS 80u
#define VGA_ROWS 25u
#define SYS_FS_OPEN 21u
#define SYS_FS_READ 22u
#define SYS_FS_WRITE 23u
#define SYS_FS_CLOSE 24u
#define SYS_FS_DELETE 25u
#define SYS_FS_LIST 26u
#define SYS_FS_SIZE 27u
#define SYS_FS_SEEK 28u
#define SYS_FS_RENAME 31u
#define SYS_FS_EXISTS 32u
#define SYS_FS_SLOTS 33u
#define FS_READ 0u
#define FS_WRITE 1u
#define FS_APPEND 2u
#define FS_FAIL 0xFFFFFFFFu
#define FS_NAME_MAX 12u
#define FS_FILE_MAX 65536u
#define FS_FILES 16u
#define FS_HANDLES 8u
static inline u32 flareos_call(u32 n, u32 b, u32 c, u32 d) {
    u32 ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(b), "c"(c), "d"(d) : "memory");
    return ret;
}
static inline u32 fs_open(const char *name, u32 mode) {
    return flareos_call(SYS_FS_OPEN, (u32)(uintptr_t)name, mode, 0);
}
static inline u32 fs_read(u32 h, void *buf, u32 max) {
    return flareos_call(SYS_FS_READ, h, (u32)(uintptr_t)buf, max);
}
static inline u32 fs_write(u32 h, const void *buf, u32 count) {
    return flareos_call(SYS_FS_WRITE, h, (u32)(uintptr_t)buf, count);
}
static inline u32 fs_close(u32 h) { return flareos_call(SYS_FS_CLOSE, h, 0, 0); }
static inline u32 fs_delete(const char *name) {
    return flareos_call(SYS_FS_DELETE, (u32)(uintptr_t)name, 0, 0);
}
static inline u32 fs_list(u32 index, char *out) {
    return flareos_call(SYS_FS_LIST, index, (u32)(uintptr_t)out, 0);
}
static inline u32 fs_size(u32 h) { return flareos_call(SYS_FS_SIZE, h, 0, 0); }
static inline u32 fs_exists(const char *name) {
    return flareos_call(SYS_FS_EXISTS, (u32)(uintptr_t)name, 0, 0);
}
static inline u32 fs_seek(u32 h, u32 position) {
    return flareos_call(SYS_FS_SEEK, h, position, 0);
}
static inline u32 fs_rename(const char *old_name, const char *new_name) {
    return flareos_call(SYS_FS_RENAME, (u32)(uintptr_t)old_name, (u32)(uintptr_t)new_name, 0);
}
#endif
