/*
FILE PATH: drivers/drv_ata.c
PURPOSE: Polling ATA PIO driver with discovery for primary/secondary master/slave.
        Legacy ata_read_sector/write_sector use the first detected LBA28 disk.
        IDENTIFY and device discovery do not write to disks.
*/
#include "../include/airos_types.h"
#include "../include/drv_ata.h"

#define ATA_REG_DATA       0
#define ATA_REG_SECCOUNT   2
#define ATA_REG_LBA_LO     3
#define ATA_REG_LBA_MID    4
#define ATA_REG_LBA_HI     5
#define ATA_REG_DRIVE      6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7
#define ATA_REG_CONTROL    0x206

#define ATA_SR_ERR         0x01
#define ATA_SR_DRQ         0x08
#define ATA_SR_DF          0x20
#define ATA_SR_BSY         0x80

#define ATA_CMD_READ       0x20
#define ATA_CMD_WRITE      0x30
#define ATA_CMD_FLUSH      0xE7
#define ATA_CMD_IDENT      0xEC
#define ATA_TIMEOUT        100000u
#define ATA_WORDS          256u

static const u16 ata_io_base[2] = { 0x1F0, 0x170 };
static const u16 ata_ctrl_base[2] = { 0x3F6, 0x376 };
static struct ata_device ata_devices[ATA_DEVICE_MAX];
static int ata_devices_found;
static int ata_default_device = -1;

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

static void ata_delay(u16 control) {
    (void)ata_inb(control);
    (void)ata_inb(control);
    (void)ata_inb(control);
    (void)ata_inb(control);
}

static int ata_wait_not_busy(u16 base) {
    u32 n;
    for (n = 0; n < ATA_TIMEOUT; ++n) {
        u8 status = ata_inb((u16)(base + ATA_REG_STATUS));
        if (status == 0 || status == 0xFF)
            return -1;
        if (!(status & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(u16 base) {
    u32 n;
    for (n = 0; n < ATA_TIMEOUT; ++n) {
        u8 status = ata_inb((u16)(base + ATA_REG_STATUS));
        if (status == 0 || status == 0xFF || (status & (ATA_SR_ERR | ATA_SR_DF)))
            return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ))
            return 0;
    }
    return -1;
}

static int ata_identify_device(u8 channel, u8 drive, struct ata_device *out) {
    u16 words[ATA_WORDS];
    u16 base = ata_io_base[channel];
    u16 control = ata_ctrl_base[channel];
    u32 i;
    u8 status;

    ata_outb((u16)(base + ATA_REG_DRIVE), (u8)(0xA0 | (drive << 4)));
    ata_delay(control);
    ata_outb((u16)(base + ATA_REG_SECCOUNT), 0);
    ata_outb((u16)(base + ATA_REG_LBA_LO), 0);
    ata_outb((u16)(base + ATA_REG_LBA_MID), 0);
    ata_outb((u16)(base + ATA_REG_LBA_HI), 0);
    ata_outb((u16)(base + ATA_REG_COMMAND), ATA_CMD_IDENT);

    status = ata_inb((u16)(base + ATA_REG_STATUS));
    if (status == 0 || status == 0xFF || ata_wait_not_busy(base) != 0)
        return -1;

    /* Non-zero signature registers identify ATAPI/other devices, not ATA disks. */
    if (ata_inb((u16)(base + ATA_REG_LBA_MID)) != 0 ||
        ata_inb((u16)(base + ATA_REG_LBA_HI)) != 0)
        return -1;
    if (ata_wait_drq(base) != 0)
        return -1;

    for (i = 0; i < ATA_WORDS; ++i)
        words[i] = ata_inw((u16)(base + ATA_REG_DATA));

    if (ata_identify_parse(words, &out->identify) != 0)
        return -1;
    out->channel = channel;
    out->drive = drive;
    return 0;
}

int ata_init(void) {
    u8 channel, drive;
    ata_devices_found = 0;
    ata_default_device = -1;

    for (channel = 0; channel < 2; ++channel) {
        for (drive = 0; drive < 2; ++drive) {
            struct ata_device found;
            if (ata_identify_device(channel, drive, &found) == 0) {
                if (ata_devices_found < ATA_DEVICE_MAX) {
                    ata_devices[ata_devices_found] = found;
                    if (ata_default_device < 0)
                        ata_default_device = ata_devices_found;
                    ++ata_devices_found;
                }
            }
        }
    }
    return ata_devices_found ? 0 : -1;
}

int ata_is_ready(void) {
    return ata_default_device >= 0;
}

int ata_device_count(void) {
    return ata_devices_found;
}

int ata_get_device(int index, struct ata_device *out) {
    u32 i;
    if (!out || index < 0 || index >= ata_devices_found)
        return -1;
    out->channel = ata_devices[index].channel;
    out->drive = ata_devices[index].drive;
    out->identify = ata_devices[index].identify;
    for (i = 0; i <= ATA_IDENTIFY_MODEL_LEN; ++i)
        out->identify.model[i] = ata_devices[index].identify.model[i];
    for (i = 0; i <= ATA_IDENTIFY_SERIAL_LEN; ++i)
        out->identify.serial[i] = ata_devices[index].identify.serial[i];
    return 0;
}

u32 ata_sector_count(void) {
    if (ata_default_device < 0)
        return 0;
    return ata_devices[ata_default_device].identify.sectors_lba28;
}

static int ata_setup_device(int index, u32 lba, u8 command) {
    struct ata_device *device;
    u16 base;
    if (index < 0 || index >= ata_devices_found)
        return -1;
    device = &ata_devices[index];
    if (lba >= device->identify.sectors_lba28 || lba > 0x0FFFFFFFu)
        return -1;
    base = ata_io_base[device->channel];
    if (ata_wait_not_busy(base) != 0)
        return -1;

    ata_outb((u16)(base + ATA_REG_DRIVE),
             (u8)(0xE0 | (device->drive << 4) | ((lba >> 24) & 0x0F)));
    ata_delay(ata_ctrl_base[device->channel]);
    ata_outb((u16)(base + ATA_REG_SECCOUNT), 1);
    ata_outb((u16)(base + ATA_REG_LBA_LO), (u8)lba);
    ata_outb((u16)(base + ATA_REG_LBA_MID), (u8)(lba >> 8));
    ata_outb((u16)(base + ATA_REG_LBA_HI), (u8)(lba >> 16));
    ata_outb((u16)(base + ATA_REG_COMMAND), command);
    return 0;
}

int ata_read_device_sector(int index, u32 lba, void *buf) {
    u16 *words = (u16 *)buf;
    u16 base;
    u32 i;
    if (!buf || ata_setup_device(index, lba, ATA_CMD_READ) != 0)
        return -1;
    base = ata_io_base[ata_devices[index].channel];
    if (ata_wait_drq(base) != 0)
        return -1;
    for (i = 0; i < ATA_WORDS; ++i)
        words[i] = ata_inw((u16)(base + ATA_REG_DATA));
    return 0;
}

int ata_write_device_sector(int index, u32 lba, const void *buf) {
    const u16 *words = (const u16 *)buf;
    u16 base;
    u32 i;
    u8 status;
    if (!buf || ata_setup_device(index, lba, ATA_CMD_WRITE) != 0)
        return -1;
    base = ata_io_base[ata_devices[index].channel];
    if (ata_wait_drq(base) != 0)
        return -1;
    for (i = 0; i < ATA_WORDS; ++i)
        ata_outw((u16)(base + ATA_REG_DATA), words[i]);
    ata_outb((u16)(base + ATA_REG_COMMAND), ATA_CMD_FLUSH);
    if (ata_wait_not_busy(base) != 0)
        return -1;
    status = ata_inb((u16)(base + ATA_REG_STATUS));
    return (status & (ATA_SR_ERR | ATA_SR_DF)) ? -1 : 0;
}

int ata_read_sector(u32 lba, void *buf) {
    return ata_default_device < 0 ? -1 :
        ata_read_device_sector(ata_default_device, lba, buf);
}

int ata_write_sector(u32 lba, const void *buf) {
    return ata_default_device < 0 ? -1 :
        ata_write_device_sector(ata_default_device, lba, buf);
}
