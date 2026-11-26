#include <stdio.h>
#include <string.h>

#include "airp.h"

/* TX_APP_BASE mirrors kernel/textile.h so this test does not need kernel.h. */
#define TX_APP_BASE  0x40000u
#define TX_APP_MAGIC 0x54505041u

/* Provided by kernel/pkg/airp.c under FLAIROS_HOST_TEST: a private app slot. */
extern unsigned char airp_host_slot[];

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        failures++;
}

static void put_le32(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void put_le16(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

/* Pack the fields of a v2 header, computing the header CRC over the raw bytes
   the same way the kernel does. */
static void put_v2_header(unsigned char *h, unsigned int file_size,
                          unsigned int manifest_off, unsigned int manifest_len,
                          unsigned int resource_count, unsigned int tab_off,
                          unsigned int tab_len, unsigned int arch,
                          unsigned int payload_crc) {
    unsigned int i;
    for (i = 0; i < 64; i++)
        h[i] = 0;
    h[0] = 'A'; h[1] = 'I'; h[2] = 'R'; h[3] = 'P';
    h[4] = 2; h[6] = 64;
    h[8] = AIRP_API_LEVEL;
    h[12] = TX_APP_BASE & 0xFF;
    h[13] = (TX_APP_BASE >> 8) & 0xFF;
    h[14] = (TX_APP_BASE >> 16) & 0xFF;
    h[15] = (TX_APP_BASE >> 24) & 0xFF;
    put_le32(h + 16, TX_APP_BASE + 8);   /* entry, inside the 16-byte image */
    put_le32(h + 20, 16);                  /* image_size */
    h[28] = manifest_off & 0xFF; h[29] = (manifest_off >> 8) & 0xFF;
    h[30] = (manifest_off >> 16) & 0xFF; h[31] = (manifest_off >> 24) & 0xFF;
    h[32] = manifest_len & 0xFF; h[33] = (manifest_len >> 8) & 0xFF;
    h[34] = (manifest_len >> 16) & 0xFF; h[35] = (manifest_len >> 24) & 0xFF;
    h[36] = resource_count & 0xFF; h[37] = (resource_count >> 8) & 0xFF;
    h[38] = (resource_count >> 16) & 0xFF; h[39] = (resource_count >> 24) & 0xFF;
    h[40] = tab_off & 0xFF; h[41] = (tab_off >> 8) & 0xFF;
    h[42] = (tab_off >> 16) & 0xFF; h[43] = (tab_off >> 24) & 0xFF;
    h[44] = tab_len & 0xFF; h[45] = (tab_len >> 8) & 0xFF;
    h[46] = (tab_len >> 16) & 0xFF; h[47] = (tab_len >> 24) & 0xFF;
    h[48] = arch & 0xFF; h[49] = (arch >> 8) & 0xFF;
    h[50] = (arch >> 16) & 0xFF; h[51] = (arch >> 24) & 0xFF;
    /* header_crc at 52 is computed below */
    h[56] = payload_crc & 0xFF; h[57] = (payload_crc >> 8) & 0xFF;
    h[58] = (payload_crc >> 16) & 0xFF; h[59] = (payload_crc >> 24) & 0xFF;
    h[60] = file_size & 0xFF; h[61] = (file_size >> 8) & 0xFF;
    h[62] = (file_size >> 16) & 0xFF; h[63] = (file_size >> 24) & 0xFF;
    {
        unsigned int crc = airp_crc32(h, 64);
        h[52] = crc & 0xFF; h[53] = (crc >> 8) & 0xFF;
        h[54] = (crc >> 16) & 0xFF; h[55] = (crc >> 24) & 0xFF;
    }
}

/* Builds a complete v2 package with one THEME resource named "theme.json".
   Returns its size. `buf` must hold at least 256 bytes. */
static unsigned int build_pkg(unsigned char *buf) {
    unsigned int image_size = 16;
    unsigned int manifest_off = 64 + image_size;
    const char *manifest = "{\"name\":\"test\"}";
    unsigned int manifest_len = (unsigned int)strlen(manifest);
    unsigned int tab_off = manifest_off + manifest_len;
    unsigned int name_off = tab_off + AIRP_RESOURCE_SIZE;
    const char *res_name = "theme.json";
    const char *res_data = "{\"accent\":170}";
    unsigned int res_len = (unsigned int)strlen(res_data);
    unsigned int data_off = name_off + (unsigned int)strlen(res_name) + 1;
    unsigned int file_size = data_off + res_len;
    unsigned int payload_crc;

    memset(buf, 0, file_size);
    /* Executable payload: magic + entry, then filler. */
    put_le32(buf + 64, TX_APP_MAGIC);
    put_le32(buf + 68, TX_APP_BASE + 8);
    put_le32(buf + 72, 0x11111111);
    put_le32(buf + 76, 0x22222222);
    memcpy(buf + manifest_off, manifest, manifest_len);
    /* resource entry */
    put_le32(buf + tab_off + 0, name_off);
    put_le32(buf + tab_off + 4, data_off);
    put_le32(buf + tab_off + 8, res_len);
    put_le32(buf + tab_off + 12, airp_crc32((const unsigned char *)res_data, res_len));
    put_le16(buf + tab_off + 16, AIRP_RT_THEME);
    put_le16(buf + tab_off + 18, AIRP_RF_REQUIRED);
    put_le32(buf + tab_off + 20, 0);
    memcpy(buf + name_off, res_name, strlen(res_name) + 1);
    memcpy(buf + data_off, res_data, res_len);

    payload_crc = airp_crc32(buf + 64, file_size - 64);
    put_v2_header(buf, file_size, manifest_off, manifest_len, 1, tab_off,
                  AIRP_RESOURCE_SIZE, AIRP_ARCH_I386, payload_crc);
    return file_size;
}

int main(void) {
    unsigned char buf[512];
    struct airp_package pkg;
    struct airp_view view;
    unsigned int len, type;
    unsigned int entry_entry = 0;
    int rc;

    /* CRC matches the standard check value. */
    check(airp_crc32((const unsigned char *)"123456789", 9) == 0xCBF43926u,
          "CRC-32 matches the IEEE check value");

    len = build_pkg(buf);
    rc = airp_open(buf, len, AIRP_ARCH_I386, &pkg);
    check(rc == AIRP_LOAD_OK, "valid v2 package opens");
    check(pkg.version == 2, "package reports version 2");
    check(pkg.image_size == 16, "image size is read");
    check(pkg.manifest_len > 0 && pkg.manifest[0] == '{', "manifest is exposed");
    check(pkg.resource_count == 1, "resource count is read");

    rc = airp_find_resource(&pkg, "theme.json", &type, &view);
    check(rc == AIRP_LOAD_OK && type == AIRP_RT_THEME, "resource found by name");
    check(view.size == 14 && memcmp(view.data, "{\"accent\":170}", 14) == 0,
          "resource bytes are exact");

    rc = airp_find_resource(&pkg, "missing", &type, &view);
    check(rc == AIRP_LOAD_NOT_FOUND, "missing resource is reported");

    rc = airp_find_resource_type(&pkg, AIRP_RT_THEME, &view, 0, 0);
    check(rc == AIRP_LOAD_OK, "resource found by type");

    /* Corrupt one payload byte: CRC must catch it. */
    buf[70] ^= 0xFF;
    rc = airp_open(buf, len, AIRP_ARCH_I386, &pkg);
    check(rc == AIRP_LOAD_CRC, "payload corruption is detected");
    buf[70] ^= 0xFF;

    /* Corrupt the header: header CRC must catch it. */
    buf[24] ^= 0x01;
    rc = airp_open(buf, len, AIRP_ARCH_I386, &pkg);
    check(rc == AIRP_LOAD_CRC, "header corruption is detected");
    buf[24] ^= 0x01;

    rc = airp_open(buf, len, AIRP_ARCH_AARCH64, &pkg);
    check(rc == AIRP_LOAD_ARCH, "architecture mismatch is rejected");

    rc = airp_open(buf, len, AIRP_ARCH_ANY, &pkg);
    check(rc == AIRP_LOAD_OK, "AIRP_ARCH_ANY accepts any package");

    /* Truncated package. */
    rc = airp_open(buf, 40, AIRP_ARCH_I386, &pkg);
    check(rc == AIRP_LOAD_TOO_SHORT, "truncated package is rejected");

    /* Bad magic. */
    buf[0] = 'X';
    rc = airp_open(buf, len, AIRP_ARCH_I386, &pkg);
    check(rc == AIRP_LOAD_BAD_MAGIC, "bad signature is rejected");
    buf[0] = 'A';

    /* Loading copies the payload into the app slot and returns the entry. */
    rc = airp_load(buf, len, &entry_entry);
    check(rc == AIRP_LOAD_OK, "package loads into the app slot");
    check(entry_entry == TX_APP_BASE + 8, "loader returns the entry address");
    check(((unsigned int *)airp_host_slot)[0] == TX_APP_MAGIC &&
          ((unsigned int *)airp_host_slot)[1] == TX_APP_BASE + 8,
          "app slot contains the copied image head");

    /* A legacy v1 package must still load. */
    {
        unsigned char v1[64];
        memset(v1, 0, sizeof(v1));
        put_le32(v1, AIRP_MAGIC);
        put_le16(v1 + 4, 1);
        put_le16(v1 + 6, 36);
        put_le32(v1 + 8, AIRP_API_LEVEL);
        put_le32(v1 + 12, TX_APP_BASE);
        put_le32(v1 + 16, TX_APP_BASE + 8); /* entry */
        put_le32(v1 + 20, 16);              /* image_size */
        memcpy(v1 + 24, "legacy", 7);       /* name[12] */
        put_le32(v1 + 36, TX_APP_MAGIC);    /* payload head */
        put_le32(v1 + 40, TX_APP_BASE + 8);
        rc = airp_open(v1, sizeof(v1), AIRP_ARCH_I386, &pkg);
        check(rc == AIRP_LOAD_OK && pkg.version == 1, "version 1 package still opens");
        rc = airp_load(v1, sizeof(v1), &entry_entry);
        check(rc == AIRP_LOAD_OK, "version 1 package still loads");
    }

    printf("%s\n", failures ? "airp: FAILURES" : "airp: all checks passed");
    return failures ? 1 : 0;
}
