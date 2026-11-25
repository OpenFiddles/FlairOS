#include <stdio.h>
#include <string.h>
#include "apk.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static uint32_t add_zip_entry(uint8_t *dst, const char *name, const char *payload) {
    uint16_t nlen = (uint16_t)strlen(name);
    uint32_t plen = (uint32_t)strlen(payload);
    memset(dst, 0, 30);
    dst[0]=0x50; dst[1]=0x4B; dst[2]=0x03; dst[3]=0x04;
    dst[18]=(uint8_t)plen; dst[22]=(uint8_t)plen;
    dst[26]=(uint8_t)nlen;
    memcpy(dst + 30, name, nlen);
    memcpy(dst + 30 + nlen, payload, plen);
    return 30u + nlen + plen;
}

int main(void) {
    uint8_t zip[512];
    uint32_t off = 0;
    const char *xml = "<manifest package=\"com.test.app\"><application android:label=\"TestApp\" userTestApp=\"true\"/></manifest>";
    off += add_zip_entry(zip + off, "AndroidManifest.xml", xml);
    off += add_zip_entry(zip + off, "classes.dex", "DEXDATA");
    off += add_zip_entry(zip + off, "lib/x86/libapp.so", "ELF");

    struct apk_info info;
    CHECK(apk_inspect(zip, off, &info) == 0);
    CHECK(info.entry_count == 3);
    CHECK(info.has_manifest == 1 && info.has_dex == 1 && info.has_x86_so == 1);
    CHECK(info.is_user_test_app == 1);
    CHECK(strcmp(info.package_name, "com.test.app") == 0);
    CHECK(strcmp(info.app_label, "TestApp") == 0);

    struct apk_entry ent;
    CHECK(apk_find_entry(zip, off, "classes.dex", &ent) == 0);
    CHECK(ent.uncomp_size == 7 && memcmp(zip + ent.data_offset, "DEXDATA", 7) == 0);

    uint8_t dex_out[16];
    CHECK(apk_extract_stored(zip, off, "classes.dex", dex_out, sizeof(dex_out)) == 7);
    CHECK(memcmp(dex_out, "DEXDATA", 7) == 0);

    puts("APK/ZIP parser host test: all checks passed");
    return 0;
}
