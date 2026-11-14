/*
FILE PATH: kernel/fs.c
CONTRIBUTOR: Textile
PURPOSE: Dynamic filesystem for FlareOS. Files are variable-size (up to 64 KB),
  held in kernel heap buffers and written through to the ATA disk. Sector FS_DIR_LBA
  holds the directory; file slot N owns a fixed 128-sector region starting at
  sector 399 + N * 128, so a file that grows never overlaps another file.
  Paging is not involved: data buffers come from kmalloc, not the app address space.
*/

#include "kernel.h"
#include "../include/fs.h"
#include "../include/drv_ata.h"
#include "../include/kheap.h"

#define FSK_DIR_LBA    FS_DIR_LBA
#define FSK_DATA_LBA   (FS_DIR_LBA + 1)
#define FSK_FILE_SECTS (FS_FILE_MAX / ATA_SECTOR_SIZE)
#define FSK_MAGIC      FS_MAGIC
#define FSK_HDR        8
#define FSK_ENTRY      FS_ENTRY_SIZE

struct fs_file {
    u8 used;
    char name[FS_NAME_MAX + 1];
    u32 size;
    u32 cap;
    u32 start_lba;
    u8 *data;
};

struct fs_handle {
    u8 used;
    u8 mode;
    u8 file;
    u32 pos;
};

static struct fs_file fsk_files[FS_FILES];
static struct fs_handle fsk_handles[FS_HANDLES];
static u8 fsk_sector[ATA_SECTOR_SIZE];
static int fsk_loaded;
static int fsk_disk;

static u32 fsk_file_lba(int i) {
    return FSK_DATA_LBA + (u32)i * FSK_FILE_SECTS;
}

static int fsk_disk_fits(void) {
    return ata_sector_count() >= fsk_file_lba(FS_FILES);
}

static void fsk_put32(int at, u32 v) {
    fsk_sector[at] = (u8)v;
    fsk_sector[at + 1] = (u8)(v >> 8);
    fsk_sector[at + 2] = (u8)(v >> 16);
    fsk_sector[at + 3] = (u8)(v >> 24);
}

static u32 fsk_get32(int at) {
    return (u32)fsk_sector[at] | ((u32)fsk_sector[at + 1] << 8) |
           ((u32)fsk_sector[at + 2] << 16) | ((u32)fsk_sector[at + 3] << 24);
}

static int fsk_dir_write(void) {
    int i, k, at;

    for (i = 0; i < ATA_SECTOR_SIZE; i++)
        fsk_sector[i] = 0;
    fsk_put32(0, FSK_MAGIC);
    fsk_put32(4, FS_FILES);
    for (i = 0; i < FS_FILES; i++) {
        at = FSK_HDR + i * FSK_ENTRY;
        fsk_sector[at] = fsk_files[i].used ? 1 : 0;
        for (k = 0; k <= FS_NAME_MAX; k++)
            fsk_sector[at + 1 + k] = (u8)fsk_files[i].name[k];
        fsk_put32(at + 1 + FS_NAME_MAX + 1, fsk_files[i].size);
        fsk_put32(at + 1 + FS_NAME_MAX + 5, fsk_files[i].start_lba);
    }
    return ata_write_sector(FSK_DIR_LBA, fsk_sector);
}

static int fsk_file_write(int i) {
    struct fs_file *f = &fsk_files[i];
    u32 offset = 0;

    while (offset < f->size) {
        u32 chunk = f->size - offset;
        if (chunk > ATA_SECTOR_SIZE)
            chunk = ATA_SECTOR_SIZE;
        for (u32 b = 0; b < ATA_SECTOR_SIZE; b++)
            fsk_sector[b] = b < chunk ? f->data[offset + b] : 0;
        if (ata_write_sector(f->start_lba + offset / ATA_SECTOR_SIZE, fsk_sector) != 0)
            return -1;
        offset += chunk;
    }
    return 0;
}

static int fsk_file_read(int i) {
    struct fs_file *f = &fsk_files[i];
    u32 offset = 0;

    while (offset < f->size) {
        u32 chunk = f->size - offset;
        if (chunk > ATA_SECTOR_SIZE)
            chunk = ATA_SECTOR_SIZE;
        if (ata_read_sector(f->start_lba + offset / ATA_SECTOR_SIZE, fsk_sector) != 0)
            return -1;
        for (u32 b = 0; b < chunk; b++)
            f->data[offset + b] = fsk_sector[b];
        offset += chunk;
    }
    return 0;
}

static int fsk_reserve(struct fs_file *f, u32 need) {
    u8 *buf;
    u32 cap, i;

    if (need <= f->cap)
        return 0;
    cap = (need + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE * ATA_SECTOR_SIZE;
    buf = (u8 *)kmalloc(cap);
    if (!buf)
        return -1;
    for (i = 0; f->data && i < f->size; i++)
        buf[i] = f->data[i];
    if (f->data)
        kfree(f->data);
    f->data = buf;
    f->cap = cap;
    return 0;
}

static void fsk_drop(struct fs_file *f) {
    if (f->data)
        kfree(f->data);
    f->data = 0;
    f->cap = 0;
    f->size = 0;
    f->used = 0;
}

static int fsk_commit(int file) {
    if (!fsk_disk)
        return 0;
    if (file >= 0 && fsk_file_write(file) != 0)
        return -1;
    return fsk_dir_write() == 0 ? 0 : -1;
}

static void fsk_load(void) {
    int i, k, at;

    fsk_loaded = 1;
    fsk_disk = 0;
    if (!ata_is_ready() || !fsk_disk_fits())
        return;
    if (ata_read_sector(FSK_DIR_LBA, fsk_sector) != 0)
        return;
    if (fsk_get32(0) != FSK_MAGIC)
        return;
    fsk_disk = 1;
    for (i = 0; i < FS_FILES; i++) {
        at = FSK_HDR + i * FSK_ENTRY;
        fsk_files[i].used = fsk_sector[at] == 1;
        for (k = 0; k <= FS_NAME_MAX; k++)
            fsk_files[i].name[k] = (char)fsk_sector[at + 1 + k];
        fsk_files[i].name[FS_NAME_MAX] = 0;
        fsk_files[i].size = fsk_get32(at + 1 + FS_NAME_MAX + 1);
        fsk_files[i].start_lba = fsk_file_lba(i);
        fsk_files[i].data = 0;
        fsk_files[i].cap = 0;
        if (fsk_files[i].used && fsk_files[i].size > FS_FILE_MAX)
            fsk_files[i].used = 0;
    }
    for (i = 0; i < FS_FILES; i++) {
        if (!fsk_files[i].used || fsk_files[i].size == 0)
            continue;
        if (fsk_reserve(&fsk_files[i], fsk_files[i].size) != 0 || fsk_file_read(i) != 0)
            fsk_drop(&fsk_files[i]);
    }
}

static void fsk_ensure(void) {
    if (!fsk_loaded)
        fsk_load();
}

static int fsk_copy_name(char *dst, u32 src) {
    const char *s = (const char *)src;
    int n = 0;
    if (!s)
        return 0;
    while (s[n]) {
        if (n >= FS_NAME_MAX)
            return 0;
        dst[n] = s[n];
        n++;
    }
    if (n == 0)
        return 0;
    dst[n] = 0;
    return 1;
}

static int fsk_name_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

static int fsk_find(const char *name) {
    for (int i = 0; i < FS_FILES; i++)
        if (fsk_files[i].used && fsk_name_eq(fsk_files[i].name, name))
            return i;
    return -1;
}

static int fsk_file_busy(int file) {
    for (int i = 0; i < FS_HANDLES; i++)
        if (fsk_handles[i].used && fsk_handles[i].file == file)
            return 1;
    return 0;
}

static int fsk_free_file(void) {
    for (int i = 0; i < FS_FILES; i++)
        if (!fsk_files[i].used)
            return i;
    return -1;
}

static int fsk_free_handle(void) {
    for (int i = 0; i < FS_HANDLES; i++)
        if (!fsk_handles[i].used)
            return i;
    return -1;
}

static struct fs_handle *fsk_get(u32 h) {
    if (h < 1 || h > FS_HANDLES)
        return 0;
    struct fs_handle *s = &fsk_handles[h - 1];
    return s->used ? s : 0;
}

static u32 fsk_open(u32 name_ptr, u32 mode) {
    char name[FS_NAME_MAX + 1];
    int file, slot, created = 0;
    u32 old_size;

    if (mode > FS_APPEND || !fsk_copy_name(name, name_ptr))
        return FS_FAIL;
    file = fsk_find(name);
    if (file >= 0 && fsk_file_busy(file))
        return FS_FAIL;
    if (file < 0 && mode == FS_READ)
        return FS_FAIL;
    slot = fsk_free_handle();
    if (slot < 0)
        return FS_FAIL;
    if (file < 0) {
        file = fsk_free_file();
        if (file < 0)
            return FS_FAIL;
        fsk_files[file].used = 1;
        fsk_files[file].size = 0;
        fsk_files[file].start_lba = fsk_file_lba(file);
        for (int i = 0; i <= FS_NAME_MAX; i++)
            fsk_files[file].name[i] = name[i];
        created = 1;
    }
    old_size = fsk_files[file].size;
    if (mode == FS_WRITE)
        fsk_files[file].size = 0;
    if ((created || fsk_files[file].size != old_size) && fsk_commit(file) != 0) {
        if (created)
            fsk_files[file].used = 0;
        else
            fsk_files[file].size = old_size;
        return FS_FAIL;
    }
    fsk_handles[slot].used = 1;
    fsk_handles[slot].mode = (u8)mode;
    fsk_handles[slot].file = (u8)file;
    fsk_handles[slot].pos = mode == FS_APPEND ? fsk_files[file].size : 0;
    return (u32)(slot + 1);
}

static u32 fsk_read(u32 h, u32 buf, u32 max) {
    struct fs_handle *s = fsk_get(h);
    struct fs_file *f;
    u32 left, n;
    u8 *dst = (u8 *)buf;

    if (!s || s->mode != FS_READ || !buf)
        return FS_FAIL;
    f = &fsk_files[s->file];
    left = f->size > s->pos ? f->size - s->pos : 0;
    n = max < left ? max : left;
    for (u32 i = 0; i < n; i++)
        dst[i] = f->data[s->pos + i];
    s->pos += n;
    return n;
}

static u32 fsk_write(u32 h, u32 buf, u32 count) {
    struct fs_handle *s = fsk_get(h);
    struct fs_file *f;
    u32 room, n, old_size, old_pos;
    const u8 *src = (const u8 *)buf;

    if (!s || s->mode == FS_READ || !buf)
        return FS_FAIL;
    f = &fsk_files[s->file];
    room = FS_FILE_MAX - s->pos;
    n = count < room ? count : room;
    if (n == 0)
        return 0;
    if (fsk_reserve(f, s->pos + n) != 0)
        return FS_FAIL;
    old_size = f->size;
    old_pos = s->pos;
    for (u32 i = 0; i < n; i++)
        f->data[s->pos + i] = src[i];
    s->pos += n;
    if (s->pos > f->size)
        f->size = s->pos;
    if (fsk_commit(s->file) != 0) {
        f->size = old_size;
        s->pos = old_pos;
        return FS_FAIL;
    }
    return n;
}

static u32 fsk_close(u32 h) {
    struct fs_handle *s = fsk_get(h);
    if (!s)
        return FS_FAIL;
    s->used = 0;
    return 0;
}

static u32 fsk_delete(u32 name_ptr) {
    char name[FS_NAME_MAX + 1];
    int file;

    if (!fsk_copy_name(name, name_ptr))
        return FS_FAIL;
    file = fsk_find(name);
    if (file < 0 || fsk_file_busy(file))
        return FS_FAIL;
    fsk_drop(&fsk_files[file]);
    if (fsk_commit(-1) != 0)
        return FS_FAIL;
    return 0;
}

static u32 fsk_list(u32 index, u32 name_out) {
    char *dst = (char *)name_out;
    int k;

    if (!name_out)
        return FS_FAIL;
    for (int i = 0; i < FS_FILES; i++) {
        if (!fsk_files[i].used)
            continue;
        if (index == 0) {
            for (k = 0; fsk_files[i].name[k]; k++)
                dst[k] = fsk_files[i].name[k];
            dst[k] = 0;
            return fsk_files[i].size;
        }
        index--;
    }
    return FS_FAIL;
}

static u32 fsk_size(u32 h) {
    struct fs_handle *s = fsk_get(h);
    if (!s)
        return FS_FAIL;
    return fsk_files[s->file].size;
}

static u32 fsk_seek(u32 h, u32 pos) {
    struct fs_handle *s = fsk_get(h);
    if (!s || pos > fsk_files[s->file].size)
        return FS_FAIL;
    s->pos = pos;
    return pos;
}

static u32 fsk_rename(u32 old_ptr, u32 new_ptr) {
    char old_name[FS_NAME_MAX + 1];
    char new_name[FS_NAME_MAX + 1];
    int file, i;

    if (!fsk_copy_name(old_name, old_ptr) || !fsk_copy_name(new_name, new_ptr))
        return FS_FAIL;
    file = fsk_find(old_name);
    if (file < 0 || fsk_file_busy(file) || fsk_find(new_name) >= 0)
        return FS_FAIL;
    for (i = 0; new_name[i]; i++)
        fsk_files[file].name[i] = new_name[i];
    fsk_files[file].name[i] = 0;
    if (fsk_commit(-1) != 0)
        return FS_FAIL;
    return 0;
}

static u32 fsk_exists(u32 name_ptr) {
    char name[FS_NAME_MAX + 1];

    if (!fsk_copy_name(name, name_ptr))
        return 0;
    return fsk_find(name) >= 0 ? 1 : 0;
}

static u32 fsk_slots(void) {
    u32 n = 0;
    for (int i = 0; i < FS_FILES; i++)
        if (!fsk_files[i].used)
            n++;
    return n;
}

int fs_format(void) {
    fsk_ensure();
    for (int i = 0; i < FS_HANDLES; i++)
        if (fsk_handles[i].used)
            return -1;
    if (!ata_is_ready() || !fsk_disk_fits())
        return -1;
    for (int i = 0; i < FS_FILES; i++)
        fsk_drop(&fsk_files[i]);
    fsk_disk = 1;
    if (fsk_dir_write() != 0) {
        fsk_disk = 0;
        return -1;
    }
    return 0;
}

int fs_shell_save(const char *name, const char *text) {
    u32 n = 0, h, w;

    fsk_ensure();
    while (text[n])
        n++;
    h = fsk_open((u32)(unsigned long)name, FS_WRITE);
    if (h == FS_FAIL)
        return -1;
    w = fsk_write(h, (u32)(unsigned long)text, n);
    fsk_close(h);
    return w == n ? 0 : -1;
}

int fs_shell_load(const char *name, char *out, u32 max) {
    u32 h, n;

    fsk_ensure();
    if (max == 0)
        return -1;
    h = fsk_open((u32)(unsigned long)name, FS_READ);
    if (h == FS_FAIL)
        return -1;
    n = fsk_read(h, (u32)(unsigned long)out, max - 1);
    fsk_close(h);
    if (n == FS_FAIL)
        return -1;
    out[n] = 0;
    return (int)n;
}

int fs_dispatch(struct saved_regs *r) {
    fsk_ensure();
    switch (r->eax) {
    case SYS_FS_OPEN:
        r->eax = fsk_open(r->ebx, r->ecx);
        return 1;
    case SYS_FS_READ:
        r->eax = fsk_read(r->ebx, r->ecx, r->edx);
        return 1;
    case SYS_FS_WRITE:
        r->eax = fsk_write(r->ebx, r->ecx, r->edx);
        return 1;
    case SYS_FS_CLOSE:
        r->eax = fsk_close(r->ebx);
        return 1;
    case SYS_FS_DELETE:
        r->eax = fsk_delete(r->ebx);
        return 1;
    case SYS_FS_LIST:
        r->eax = fsk_list(r->ebx, r->ecx);
        return 1;
    case SYS_FS_SIZE:
        r->eax = fsk_size(r->ebx);
        return 1;
    case SYS_FS_SEEK:
        r->eax = fsk_seek(r->ebx, r->ecx);
        return 1;
    case SYS_FS_RENAME:
        r->eax = fsk_rename(r->ebx, r->ecx);
        return 1;
    case SYS_FS_EXISTS:
        r->eax = fsk_exists(r->ebx);
        return 1;
    case SYS_FS_SLOTS:
        r->eax = fsk_slots();
        return 1;
    default:
        return 0;
    }
}
