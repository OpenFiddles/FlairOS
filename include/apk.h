#ifndef FLAIROS_APK_H
#define FLAIROS_APK_H

#include <stdint.h>

#define ZIP_LOCAL_MAGIC 0x04034B50u /* "PK\x03\x04" */

struct apk_entry {
    uint32_t data_offset;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t method;
};

struct apk_info {
    int has_manifest;
    int has_dex;
    int has_x86_so;
    int is_system_app;
    int is_user_test_app;
    int is_debuggable;
    uint32_t entry_count;
    uint32_t dex_size;
    char package_name[64];
    char app_label[48];
};

int apk_find_entry(const uint8_t *buf, uint32_t len, const char *target_name,
                   struct apk_entry *out);
int apk_inspect(const uint8_t *buf, uint32_t len, struct apk_info *info);

int apk_extract_stored(const uint8_t *buf, uint32_t len, const char *target_name,
                       uint8_t *out, uint32_t max_out);

#endif
