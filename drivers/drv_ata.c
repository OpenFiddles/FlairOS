/*
FILE PATH: drivers/drv_ata.c
CONTRIBUTOR (Optional): Textile
PURPOSE: Polling ATA PIO driver for the primary master disk. Detects the drive with IDENTIFY and reads or writes 512-byte sectors with 28-bit LBA. Buffers must be 2-byte aligned.
*/

#include "../include/airos_types.h"
#include "../include/drv_ata.h"

#define ATA_DATA        0x1F0
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALTSTATUS   0x3F6

#define ATA_SR_ERR      0x01
#define ATA_SR_DRQ      0x08
#define ATA_SR_DF       0x20
#define ATA_SR_BSY      0x80

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30
#define ATA_CMD_FLUSH   0xE7
#define ATA_CMD_IDENT   0xEC

#define ATA_TIMEOUT     100000u
#define ATA_WORDS       256u

static u32 ata_sectors;
static int ata_ready;

static inline void ata_outb(u16 port, u8 value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 ata_inb(u16 port) {
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void ata_outw(u16 port, u16 value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 ata_inw(u16 port) {
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void ata_delay(void) {
    ata_inb(ATA_ALTSTATUS);
    ata_inb(ATA_ALTSTATUS);
    ata_inb(ATA_ALTSTATUS);
    ata_inb(ATA_ALTSTATUS);
}

static int ata_wait_clear(void) {
    u32 n;
    for (n = 0; n < ATA_TIMEOUT; n++) {
        if (!(ata_inb(ATA_STATUS) & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;
}

static int ata_wait_drq(void) {
    u32 n;
    for (n = 0; n < ATA_TIMEOUT; n++) {
        u8 status = ata_inb(ATA_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            return -1;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) {
            return 0;
        }
    }
    return -1;
}

int ata_init(void) {
    u16 id[ATA_WORDS];
    u32 i;

    ata_ready = 0;
    ata_sectors = 0;

    ata_outb(ATA_DRIVE, 0xA0);
    ata_delay();
    ata_outb(ATA_SECCOUNT, 0);
    ata_outb(ATA_LBA_LO, 0);
    ata_outb(ATA_LBA_MID, 0);
    ata_outb(ATA_LBA_HI, 0);
    ata_outb(ATA_COMMAND, ATA_CMD_IDENT);

    if (ata_inb(ATA_STATUS) == 0) {
        return -1;
    }
    if (ata_wait_clear() != 0) {
        return -1;
    }
    if (ata_inb(ATA_LBA_MID) != 0 || ata_inb(ATA_LBA_HI) != 0) {
        return -1;
    }
    if (ata_wait_drq() != 0) {
        return -1;
    }

    for (i = 0; i < ATA_WORDS; i++) {
        id[i] = ata_inw(ATA_DATA);
    }

    ata_sectors = (u32)id[60] | ((u32)id[61] << 16);
    if (ata_sectors == 0) {
        return -1;
    }

    ata_ready = 1;
    return 0;
}

int ata_is_ready(void) {
    return ata_ready;
}

u32 ata_sector_count(void) {
    return ata_sectors;
}

static int ata_setup(u32 lba, u8 command) {
    if (!ata_ready || lba >= ata_sectors || lba > 0x0FFFFFFFu) {
        return -1;
    }
    if (ata_wait_clear() != 0) {
        return -1;
    }

    ata_outb(ATA_DRIVE, (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_delay();
    ata_outb(ATA_SECCOUNT, 1);
    ata_outb(ATA_LBA_LO, (u8)(lba & 0xFF));
    ata_outb(ATA_LBA_MID, (u8)((lba >> 8) & 0xFF));
    ata_outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
    ata_outb(ATA_COMMAND, command);
    return 0;
}

int ata_read_sector(u32 lba, void *buf) {
    u16 *words = (u16 *)buf;
    u32 i;

    if (ata_setup(lba, ATA_CMD_READ) != 0) {
        return -1;
    }
    if (ata_wait_drq() != 0) {
        return -1;
    }
    for (i = 0; i < ATA_WORDS; i++) {
        words[i] = ata_inw(ATA_DATA);
    }
    return 0;
}

int ata_write_sector(u32 lba, const void *buf) {
    const u16 *words = (const u16 *)buf;
    u32 i;
    u8 status;

    if (ata_setup(lba, ATA_CMD_WRITE) != 0) {
        return -1;
    }
    if (ata_wait_drq() != 0) {
        return -1;
    }
    for (i = 0; i < ATA_WORDS; i++) {
        ata_outw(ATA_DATA, words[i]);
    }

    ata_outb(ATA_COMMAND, ATA_CMD_FLUSH);
    if (ata_wait_clear() != 0) {
        return -1;
    }
    status = ata_inb(ATA_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return -1;
    }
    return 0;
}
