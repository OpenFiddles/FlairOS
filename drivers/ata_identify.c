/*
FILE PATH: drivers/ata_identify.c
PURPOSE: Decode ATA IDENTIFY words without hardware I/O so parsing can be host-tested.
*/
#include "../include/ata_identify.h"

static void ata_identify_string(char *out, const u16 *words, u32 first, u32 count) {
    u32 i;
    for (i = 0; i < count; ++i) {
        out[i * 2] = (char)(words[first + i] >> 8);
        out[i * 2 + 1] = (char)(words[first + i] & 0xFF);
    }
    while (count && (out[count * 2 - 1] == ' ' || out[count * 2 - 1] == 0))
        out[--count * 2 + 1] = 0;
    out[(count * 2) ? count * 2 : 0] = 0;
}

int ata_identify_parse(const u16 words[256], struct ata_identify_info *out) {
    u32 i;
    if (!words || !out)
        return -1;

    out->capabilities = words[49];
    out->command_sets_83 = words[83];
    out->lba_supported = (words[49] & (1u << 9)) != 0;
    out->lba48_supported = (words[83] & (1u << 10)) != 0;
    out->sectors_lba28 = (u32)words[60] | ((u32)words[61] << 16);

    for (i = 0; i <= ATA_IDENTIFY_MODEL_LEN; ++i)
        out->model[i] = 0;
    for (i = 0; i <= ATA_IDENTIFY_SERIAL_LEN; ++i)
        out->serial[i] = 0;

    ata_identify_string(out->model, words, 27, 20);
    ata_identify_string(out->serial, words, 10, 10);

    if (!out->lba_supported || out->sectors_lba28 == 0)
        return -1;
    return 0;
}
