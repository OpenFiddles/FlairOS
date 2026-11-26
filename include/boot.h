#ifndef FLAIROS_BOOT_H
#define FLAIROS_BOOT_H

#include "airos_types.h"

#define FLAIR_BOOT_INFO_MAGIC 0x544F4F42u /* "BOOT" */
#define FLAIR_BOOT_INFO_VERSION 2u
#define FLAIR_BOOT_INFO_LEGACY_VERSION 1u

#define FLAIR_BOOT_INFO_ADDR 0x4000u
#define FLAIR_BOOT_INFO_LEGACY_ADDR 0x4FF0u
#define FLAIR_BOOT_E820_MAX 32u

#define FLAIR_BOOT_FLAG_SERIAL 0x1u
#define FLAIR_BOOT_FLAG_EDD 0x2u
#define FLAIR_BOOT_FLAG_VIDEO 0x4u

enum flair_boot_mode {
    FLAIR_BOOT_NORMAL = 0,
    FLAIR_BOOT_LIVE = 1,
    FLAIR_BOOT_SETUP = 2,
    FLAIR_BOOT_RECOVERY = 3,
    FLAIR_BOOT_DIAGNOSTICS = 4
};

/* Field-for-field mirror of legacy/i386/bootloader/bootinfo.inc. The loader writes this
   block, followed by e820_count entries of flair_e820_entry at e820_addr. */
struct flair_boot_info {
    u32 magic;
    u32 version;
    u32 mode;
    u32 drive;
    u32 e820_count;
    u32 e820_addr;
    u32 kernel_lba;
    u32 kernel_sectors;
    u32 kernel_bytes;
    u32 lfb;
    u16 width;
    u16 height;
    u16 bpp;
    u16 pitch;
    u32 vbe_version;
    u32 vram_kib;
    u32 flags;
    u32 checksum;
};

struct flair_e820_entry {
    u32 base_lo;
    u32 base_hi;
    u32 len_lo;
    u32 len_hi;
    u32 type;
    u32 acpi;
};

/* The framebuffer the loader programmed before the handoff. */
struct flair_boot_video {
    u32 lfb;
    u16 width;
    u16 height;
    u16 bpp;
    u16 pitch;
    u32 vbe_version;
    u32 vram_kib;
};

void boot_init(void);
enum flair_boot_mode boot_mode(void);
int boot_set_mode(enum flair_boot_mode mode);
int boot_handoff_valid(void);
int boot_is_live_session(void);

/* Facts established by the loader before the kernel ran. */
u32 boot_drive(void);
u32 boot_e820_count(void);
const struct flair_e820_entry *boot_e820_entries(void);
u32 boot_memory_entries(void);
u32 boot_memory_kib(void);
u32 boot_kernel_sectors(void);
u32 boot_flags(void);
int boot_video_available(void);
const struct flair_boot_video *boot_video(void);

#endif
