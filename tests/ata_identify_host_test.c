#include <stdio.h>
#include <string.h>
#include "ata_identify.h"

static int failures;
#define CHECK(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

static void set_string(unsigned short *words, int first, int count, const char *s) {
    int i;
    for (i = 0; i < count; ++i) {
        unsigned char a = s[i * 2] ? (unsigned char)s[i * 2] : ' ';
        unsigned char b = s[i * 2 + 1] ? (unsigned char)s[i * 2 + 1] : ' ';
        words[first + i] = (unsigned short)(((unsigned short)a << 8) | b);
    }
}

int main(void) {
    unsigned short words[256];
    struct ata_identify_info info;
    memset(words, 0, sizeof(words));
    words[49] = 1u << 9;
    words[60] = 0x1234;
    words[61] = 0x0001;
    words[83] = 1u << 10;
    set_string(words, 27, 20, "FLAIR STORAGE MODEL 01");
    set_string(words, 10, 10, "SERIAL123456789");
    CHECK(ata_identify_parse(words, &info) == 0);
    CHECK(info.lba_supported == 1);
    CHECK(info.lba48_supported == 1);
    CHECK(info.sectors_lba28 == 0x00011234u);
    CHECK(strncmp(info.model, "FLAIR STORAGE MODEL 01", 22) == 0);
    CHECK(strncmp(info.serial, "SERIAL123456789", 15) == 0);

    words[49] = 0;
    CHECK(ata_identify_parse(words, &info) != 0);
    words[49] = 1u << 9;
    words[60] = 0;
    words[61] = 0;
    CHECK(ata_identify_parse(words, &info) != 0);
    CHECK(ata_identify_parse(0, &info) != 0);
    CHECK(ata_identify_parse(words, 0) != 0);

    if (!failures)
        puts("ATA IDENTIFY host test: all checks passed");
    return failures ? 1 : 0;
}
