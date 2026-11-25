#include <stdio.h>
#include <string.h>
#include "dex.h"
#include "apk.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

static int run_apk_file(const char *path, struct apk_info *info_out, struct dex_ui_ctx *ui_out) {
    static uint8_t apk_buf[8192], dex_buf[4096];
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    size_t n = fread(apk_buf, 1, sizeof(apk_buf), fp);
    fclose(fp);
    if (apk_inspect(apk_buf, (uint32_t)n, info_out) != 0) return -2;
    if (!info_out->has_dex || !info_out->has_manifest) return -3;
    int dlen = apk_extract_stored(apk_buf, (uint32_t)n, "classes.dex", dex_buf, sizeof(dex_buf));
    if (dlen <= 0) return -4;
    int32_t ret = -1;
    if (dex_run_buffer(dex_buf, (uint32_t)dlen, ui_out, &ret) != 0 || ret != 0) return -5;
    return 0;
}

int main(void) {
    struct dex_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "dex\n035\0", 8);
    hdr.file_size = sizeof(hdr);
    hdr.header_size = DEX_HEADER_SIZE;
    hdr.endian_tag = 0x12345678u;
    hdr.class_defs_size = 1;

    struct dex_header parsed;
    CHECK(dex_validate((const uint8_t *)&hdr, sizeof(hdr), &parsed) == 0);
    CHECK(parsed.class_defs_size == 1);

    hdr.endian_tag = 0x78563412u;
    CHECK(dex_validate((const uint8_t *)&hdr, sizeof(hdr), 0) != 0);

    /* const/4 v0, 5; const/4 v1, 3; add-int v2, v0, v1; return v2 -> 8 */
    uint16_t prog[5] = { 0x5012u, 0x3112u, 0x0290u, 0x0100u, 0x020Fu };
    int32_t res = 0;
    CHECK(dex_exec_bytecode(prog, 5, &res) == 0 && res == 8);

    {
        struct apk_info info;
        struct dex_ui_ctx ui;
        if (run_apk_file("examples/hello_apk/hello.apk", &info, &ui) == 0) {
            CHECK(info.is_user_test_app == 1);
            CHECK(info.is_debuggable == 1);
            CHECK(strcmp(info.package_name, "com.flairos.helloworld") == 0);
            CHECK(strcmp(info.app_label, "Hello World") == 0);
            CHECK(strcmp(ui.title, "Hello World") == 0);
            CHECK(strcmp(ui.subtitle, "User Test App - WinDown Compositor") == 0);
            CHECK(ui.line_count == 3);
            CHECK(ui.button_count == 1);
            CHECK(strcmp(ui.toast, "Toast: Hello World Ready") == 0);
        }
        if (run_apk_file("examples/calc_apk/calc.apk", &info, &ui) == 0) {
            CHECK(info.is_user_test_app == 1);
            CHECK(strcmp(ui.title, "DroidCalc") == 0);
            CHECK(ui.line_count == 2);
            CHECK(strcmp(ui.lines[1], "42") == 0);
        }
        if (run_apk_file("examples/sysinfo_apk/sysinfo.apk", &info, &ui) == 0) {
            CHECK(info.is_system_app == 1);
            CHECK(strcmp(ui.title, "Device Info") == 0);
            CHECK(ui.line_count == 3);
        }
    }
    puts("DEX bytecode verifier host test: all checks passed");
    return 0;
}
