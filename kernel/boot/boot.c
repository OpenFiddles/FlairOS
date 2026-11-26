#include "../../include/boot.h"

/* The loader publishes a checksummed block at 0x4000 (bootloader/bootinfo.inc).
   A block that fails the checksum is treated as absent: the kernel then falls
   back to the version 1 handoff, and reports no loader facts. */
static enum flair_boot_mode active_mode = FLAIR_BOOT_NORMAL;
static int handoff_valid;
static int video_valid;
static struct flair_boot_video video;
static u32 e820_entries;
static const struct flair_e820_entry *e820_ptr;
static u32 usable_kib;
static u32 kernel_sector_count;
static u32 handoff_flags;
static u32 boot_drive_id;

#define BOOT_HEADER_DWORDS 15u /* everything before the checksum field */

static u32 header_checksum(const struct flair_boot_info *info) {
    const u32 *word = (const u32 *)info;
    u32 sum = 0;
    u32 i;
    for (i = 0; i < BOOT_HEADER_DWORDS; i++)
        sum += word[i];
    return sum;
}

/* Summarise the firmware map: entry count plus usable RAM above 1 MiB. The
   entries stay in place; only the summary is copied into kernel memory. */
static void summarise_memory(const struct flair_boot_info *info) {
    const struct flair_e820_entry *entry;
    u32 count = info->e820_count;
    u32 i;
    unsigned long long total = 0;

    if (count > FLAIR_BOOT_E820_MAX)
        count = FLAIR_BOOT_E820_MAX;
    if (info->e820_addr < 0x1000u || info->e820_addr >= 0x100000u ||
        (info->e820_addr & 3u) != 0)
        count = 0;

    entry = (const struct flair_e820_entry *)info->e820_addr;
    for (i = 0; i < count; i++) {
        unsigned long long base;
        unsigned long long length;
        if (entry[i].type != 1u)
            continue;
        base = ((unsigned long long)entry[i].base_hi << 32) | entry[i].base_lo;
        length = ((unsigned long long)entry[i].len_hi << 32) | entry[i].len_lo;
        if (base < 0x100000ull)
            continue;
        total += length;
    }
    if (total > 0xFFFFFFFFull)
        total = 0xFFFFFFFFull;

    e820_entries = count;
    e820_ptr = count ? (const struct flair_e820_entry *)info->e820_addr : 0;
    usable_kib = (u32)(total / 1024ull);
}

static void clear_handoff(void) {
    handoff_valid = 0;
    video_valid = 0;
    e820_entries = 0;
    e820_ptr = 0;
    usable_kib = 0;
    kernel_sector_count = 0;
    handoff_flags = 0;
    boot_drive_id = 0;
    active_mode = FLAIR_BOOT_NORMAL;
}

void boot_init(void) {
    const struct flair_boot_info *info =
        (const struct flair_boot_info *)FLAIR_BOOT_INFO_ADDR;

    clear_handoff();

    if (info->magic == FLAIR_BOOT_INFO_MAGIC &&
        info->version == FLAIR_BOOT_INFO_VERSION &&
        info->mode <= FLAIR_BOOT_DIAGNOSTICS &&
        info->checksum == header_checksum(info)) {
        active_mode = (enum flair_boot_mode)info->mode;
        handoff_flags = info->flags;
        kernel_sector_count = info->kernel_sectors;
        boot_drive_id = info->drive;
        if ((info->flags & FLAIR_BOOT_FLAG_VIDEO) && info->lfb != 0) {
            video.lfb = info->lfb;
            video.width = info->width;
            video.height = info->height;
            video.bpp = info->bpp;
            video.pitch = info->pitch;
            video.vbe_version = info->vbe_version;
            video.vram_kib = info->vram_kib;
            video_valid = 1;
        }
        summarise_memory(info);
        handoff_valid = 1;
        return;
    }

    /* Version 1 images only carried the boot mode. Accept one so a boot floppy
       written by an older build still starts. */
    {
        volatile const u32 *legacy =
            (volatile const u32 *)FLAIR_BOOT_INFO_LEGACY_ADDR;
        u32 mode;
        if (legacy[0] != FLAIR_BOOT_INFO_MAGIC ||
            legacy[1] != FLAIR_BOOT_INFO_LEGACY_VERSION)
            return;
        mode = legacy[2];
        if (mode > FLAIR_BOOT_DIAGNOSTICS)
            return;
        active_mode = (enum flair_boot_mode)mode;
        boot_drive_id = legacy[3];
        /* Version 1 images left the map at its old fixed address. */
        e820_entries = *(volatile const u32 *)0x5000u;
        if (e820_entries > FLAIR_BOOT_E820_MAX)
            e820_entries = FLAIR_BOOT_E820_MAX;
        e820_ptr = e820_entries ? (const struct flair_e820_entry *)0x5010u : 0;
        handoff_valid = 1;
    }
}

enum flair_boot_mode boot_mode(void) {
    return active_mode;
}

int boot_set_mode(enum flair_boot_mode mode) {
    if (mode < FLAIR_BOOT_NORMAL || mode > FLAIR_BOOT_DIAGNOSTICS)
        return -1;
    active_mode = mode;
    return 0;
}

int boot_handoff_valid(void) {
    return handoff_valid;
}

int boot_is_live_session(void) {
    return active_mode == FLAIR_BOOT_LIVE;
}

u32 boot_drive(void) {
    return boot_drive_id;
}

u32 boot_e820_count(void) {
    return e820_entries;
}

const struct flair_e820_entry *boot_e820_entries(void) {
    return e820_ptr;
}

u32 boot_memory_entries(void) {
    return e820_entries;
}

u32 boot_memory_kib(void) {
    return usable_kib;
}

u32 boot_kernel_sectors(void) {
    return kernel_sector_count;
}

u32 boot_flags(void) {
    return handoff_flags;
}

int boot_video_available(void) {
    return video_valid;
}

const struct flair_boot_video *boot_video(void) {
    return video_valid ? &video : 0;
}
