/*
FILE PATH: include/fs.h
CONTRIBUTOR: Textile
PURPOSE: FlareOS dynamic filesystem syscall numbers, limits and the int 0x80
  wrappers shared by the kernel and apps. Files are variable-size (up to 64 KB),
  stored in dynamically allocated kernel memory, and persist on the ATA disk.
*/

#ifndef FS_H
#define FS_H

#define SYS_FS_OPEN   21  /* ebx = name, ecx = FS_READ/FS_WRITE/FS_APPEND; returns handle (1-8) or FS_FAIL */
#define SYS_FS_READ   22  /* ebx = handle, ecx = buffer, edx = max bytes; returns bytes read or FS_FAIL */
#define SYS_FS_WRITE  23  /* ebx = handle, ecx = buffer, edx = byte count; returns bytes written or FS_FAIL */
#define SYS_FS_CLOSE  24  /* ebx = handle */
#define SYS_FS_DELETE 25  /* ebx = name; returns 0 or FS_FAIL */
#define SYS_FS_LIST   26  /* ebx = index, ecx = name buffer (FS_NAME_MAX+1 bytes); returns file size or FS_FAIL past last */
#define SYS_FS_SIZE   27  /* ebx = handle; returns file size in bytes or FS_FAIL */
#define SYS_FS_SEEK   28  /* ebx = handle, ecx = position (0..size); returns position or FS_FAIL */
#define SYS_FS_RENAME 31  /* ebx = old name, ecx = new name; returns 0 or FS_FAIL */
#define SYS_FS_EXISTS 32  /* ebx = name; returns 1 if exists, 0 otherwise */
#define SYS_FS_SLOTS  33  /* returns number of unused file slots */
#define SYS_FS_IS_VOLATILE 35 /* returns 1 for a RAM-only session */
#define SYS_FS_DISK_WRITES  36 /* returns 1 when filesystem writes may reach ATA */

#define FS_READ   0
#define FS_WRITE  1
#define FS_APPEND 2
#define FS_FAIL   0xFFFFFFFFu

#define FS_NAME_MAX    12
#define FS_FILE_MAX    65536
#define FS_FILES       16
#define FS_HANDLES     8
#define FS_DIR_LBA     398
#define FS_MAGIC       0x46534459u
#define FS_ENTRY_SIZE  24
#define FS_MAX_ENTRIES 16

static inline unsigned int fs_sys(unsigned int n, unsigned int b, unsigned int c, unsigned int d) {
    unsigned int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(b), "c"(c), "d"(d) : "memory");
    return ret;
}

static inline unsigned int fs_open(const char *name, unsigned int mode) {
    return fs_sys(SYS_FS_OPEN, (unsigned int)(unsigned long)name, mode, 0);
}

static inline unsigned int fs_read(unsigned int h, void *buf, unsigned int max) {
    return fs_sys(SYS_FS_READ, h, (unsigned int)(unsigned long)buf, max);
}

static inline unsigned int fs_write(unsigned int h, const void *buf, unsigned int count) {
    return fs_sys(SYS_FS_WRITE, h, (unsigned int)(unsigned long)buf, count);
}

static inline void fs_close(unsigned int h) {
    fs_sys(SYS_FS_CLOSE, h, 0, 0);
}

static inline unsigned int fs_delete(const char *name) {
    return fs_sys(SYS_FS_DELETE, (unsigned int)(unsigned long)name, 0, 0);
}

static inline unsigned int fs_list(unsigned int index, char *name_out) {
    return fs_sys(SYS_FS_LIST, index, (unsigned int)(unsigned long)name_out, 0);
}

static inline unsigned int fs_size(unsigned int h) {
    return fs_sys(SYS_FS_SIZE, h, 0, 0);
}

static inline unsigned int fs_seek(unsigned int h, unsigned int pos) {
    return fs_sys(SYS_FS_SEEK, h, pos, 0);
}

static inline unsigned int fs_rename(const char *old_name, const char *new_name) {
    return fs_sys(SYS_FS_RENAME, (unsigned int)(unsigned long)old_name,
                  (unsigned int)(unsigned long)new_name, 0);
}

static inline unsigned int fs_exists(const char *name) {
    return fs_sys(SYS_FS_EXISTS, (unsigned int)(unsigned long)name, 0, 0);
}

static inline unsigned int fs_slots(void) {
    return fs_sys(SYS_FS_SLOTS, 0, 0, 0);
}

static inline int fs_is_volatile(void) {
    return fs_sys(SYS_FS_IS_VOLATILE, 0, 0, 0) == 1;
}

static inline int fs_disk_writes_enabled(void) {
    return fs_sys(SYS_FS_DISK_WRITES, 0, 0, 0) == 1;
}

#endif
