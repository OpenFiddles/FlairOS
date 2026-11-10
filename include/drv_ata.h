/*
FILE PATH: include/drv_ata.h
CONTRIBUTOR (Optional): Textile
PURPOSE: Public interface of the polling ATA PIO disk driver for the primary master drive.
*/

#ifndef AIROS_DRV_ATA_H
#define AIROS_DRV_ATA_H

#include "airos_types.h"

#define ATA_SECTOR_SIZE 512

int ata_init(void);
int ata_is_ready(void);
u32 ata_sector_count(void);
int ata_read_sector(u32 lba, void *buf);
int ata_write_sector(u32 lba, const void *buf);

#endif
