/*
FILE PATH: include/ata_identify.h
PURPOSE: Host-testable ATA IDENTIFY data decoding.
*/
#ifndef FLAIR_ATA_IDENTIFY_H
#define FLAIR_ATA_IDENTIFY_H

#include "airos_types.h"

#define ATA_IDENTIFY_MODEL_LEN 40
#define ATA_IDENTIFY_SERIAL_LEN 20

struct ata_identify_info {
    u32 sectors_lba28;
    u16 capabilities;
    u16 command_sets_83;
    int lba_supported;
    int lba48_supported;
    char model[ATA_IDENTIFY_MODEL_LEN + 1];
    char serial[ATA_IDENTIFY_SERIAL_LEN + 1];
};

int ata_identify_parse(const u16 words[256], struct ata_identify_info *out);

#endif
