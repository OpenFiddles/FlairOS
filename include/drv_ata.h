/*
FILE PATH: include/drv_ata.h
PURPOSE: Public interface for polling ATA PIO disks on the four legacy IDE positions. Legacy API stays on primary master.
*/
#ifndef AIROS_DRV_ATA_H
#define AIROS_DRV_ATA_H

#include "airos_types.h"
#include "ata_identify.h"

#define ATA_SECTOR_SIZE 512
#define ATA_DEVICE_MAX 4

struct ata_device {
    u8 channel;
    u8 drive;
    struct ata_identify_info identify;
};

int ata_init(void);
int ata_is_ready(void);
u32 ata_sector_count(void);
int ata_device_count(void);
int ata_get_device(int index, struct ata_device *out);
int ata_read_device_sector(int index, u32 lba, void *buf);
int ata_write_device_sector(int index, u32 lba, const void *buf);
int ata_read_sector(u32 lba, void *buf);
int ata_write_sector(u32 lba, const void *buf);

#endif
