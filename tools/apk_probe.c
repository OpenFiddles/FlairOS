/* SPDX-License-Identifier: AGPL-3.0-only */
/* Inspect a real .apk with the same code the kernel runs.
 *
 * This is the host-side entry point for answering "does FlairOS open this
 * package?" without a VM: it uses kernel/pkg/apk.c to walk the ZIP, inflates
 * classes.dex with the same DEFLATE decoder runtime/art/art_bridge.c uses, and
 * then hands the DEX to kernel/pkg/dex.c exactly as an install would.
 *
 * What it reports is only what the code actually reaches: ZIP entries, the
 * manifest entry, each classesN.dex, the DEX header, and the result of
 * dex_run_buffer. A nonzero dex_run_buffer result means the package is readable
 * but not executable by this runtime, and the probe says so instead of
 * pretending otherwise.
 *
 *   cc -std=gnu99 -Iinclude -o build/apk_probe tools/apk_probe.c \
 *      kernel/pkg/apk.c kernel/pkg/dex.c \
 *      ported/developer/libs/flair_zlib/flair_zlib.c
 *   build/apk_probe path/to/app.apk
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apk.h"
#include "dex.h"
#include "flair_zlib.h"

static uint8_t *read_file(const char *path, long *size_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return 0; }
    rewind(f);
    uint8_t *buf = (uint8_t *)malloc((size_t)size);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { free(buf); fclose(f); return 0; }
    fclose(f);
    *size_out = size;
    return buf;
}

/* APK local-header walk: count every entry and the ABI directories present. */
static void scan_entries(const uint8_t *buf, uint32_t len,
                         uint32_t *entries, int abis[4]) {
    static const char *abi_names[4] = { "lib/arm64-v8a/", "lib/armeabi-v7a/",
                                        "lib/x86_64/", "lib/x86/" };
    uint32_t pos = 0;
    *entries = 0;
    for (int i = 0; i < 4; i++) abis[i] = 0;
    while (pos + 30u <= len) {
        uint32_t magic = (uint32_t)buf[pos] | ((uint32_t)buf[pos + 1] << 8) |
                         ((uint32_t)buf[pos + 2] << 16) | ((uint32_t)buf[pos + 3] << 24);
        if (magic != ZIP_LOCAL_MAGIC) break;
        uint32_t comp = (uint32_t)buf[pos + 18] | ((uint32_t)buf[pos + 19] << 8) |
                        ((uint32_t)buf[pos + 20] << 16) | ((uint32_t)buf[pos + 21] << 24);
        uint16_t nlen = (uint16_t)(buf[pos + 26] | (buf[pos + 27] << 8));
        uint16_t elen = (uint16_t)(buf[pos + 28] | (buf[pos + 29] << 8));
        uint32_t hdr_end = pos + 30u + nlen + elen;
        if (hdr_end > len || comp > len - hdr_end) break;
        for (int i = 0; i < 4; i++) {
            size_t plen = strlen(abi_names[i]);
            if (nlen >= plen && memcmp(buf + pos + 30, abi_names[i], plen) == 0) abis[i]++;
        }
        (*entries)++;
        pos = hdr_end + comp;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s path/to/app.apk\n", argv[0]);
        return 2;
    }
    long size = 0;
    uint8_t *apk = read_file(argv[1], &size);
    if (!apk) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    printf("file            = %s\n", argv[1]);
    printf("apk_bytes       = %ld\n", size);

    uint32_t entries = 0;
    int abis[4];
    scan_entries(apk, (uint32_t)size, &entries, abis);
    printf("zip_entries     = %u\n", entries);
    printf("native_libs     = arm64=%d arm=%d x86_64=%d x86=%d\n",
           abis[0], abis[1], abis[2], abis[3]);

    struct apk_info info;
    int rc = apk_inspect(apk, (uint32_t)size, &info);
    printf("apk_inspect     = %d (manifest=%d dex=%d x86_so=%d package=%s label=%s)\n",
           rc, info.has_manifest, info.has_dex, info.has_x86_so,
           info.package_name[0] ? info.package_name : "<unparsed>",
           info.app_label[0] ? info.app_label : "<unparsed>");
    if (rc != 0) { free(apk); return 1; }

    /* Every classesN.dex, in install order: classes.dex, classes2.dex, ... */
    int executables = 0, readable = 0;
    for (int n = 1; n <= 32; n++) {
        char name[32];
        if (n == 1) snprintf(name, sizeof name, "classes.dex");
        else snprintf(name, sizeof name, "classes%d.dex", n);

        struct apk_entry ent;
        if (apk_find_entry(apk, (uint32_t)size, name, &ent) != 0) break;

        printf("%-15s = method %u, stored %u, uncompressed %u\n",
               name, ent.method, ent.comp_size, ent.uncomp_size);

        uint8_t *dex = (uint8_t *)malloc(ent.uncomp_size + 64u);
        if (!dex) break;
        long dlen = -1;
        if (ent.method == 0u) {
            if (ent.comp_size <= ent.uncomp_size) {
                memcpy(dex, apk + ent.data_offset, ent.comp_size);
                dlen = (long)ent.comp_size;
            }
        } else if (ent.method == 8u) {
            uint32_t out = 0;
            if (flair_inflate_raw(apk + ent.data_offset, ent.comp_size, dex,
                                  ent.uncomp_size + 64u, &out) == 0)
                dlen = (long)out;
        }
        if (dlen <= 0) {
            printf("%-15s   extract FAILED (method %u)\n", name, ent.method);
            free(dex);
            continue;
        }
        printf("%-15s   extracted %ld bytes\n", name, dlen);
        readable++;

        struct dex_header hdr;
        int vrc = dex_validate(dex, (uint32_t)dlen, &hdr);
        printf("%-15s   dex_validate=%d version=%.3s file_size=%u strings=%u types=%u methods=%u classes=%u\n",
               name, vrc, hdr.magic + 4, hdr.file_size, hdr.string_ids_size,
               hdr.type_ids_size, hdr.method_ids_size, hdr.class_defs_size);
        if (vrc == 0 && hdr.string_ids_size > 0) {
            char first[80];
            if (dex_read_string(dex, (uint32_t)dlen, 0, first, sizeof first) > 0)
                printf("%-15s   string[0] = \"%s\"\n", name, first);
        }

        struct dex_ui_ctx ui;
        int32_t ret = 0;
        int rrc = dex_run_buffer(dex, (uint32_t)dlen, &ui, &ret);
        printf("%-15s   dex_run_buffer=%d%s%s%s\n", name, rrc,
               rrc == 0 ? " title=\"" : "",
               rrc == 0 ? ui.title : "",
               rrc == 0 ? "\"" : "");
        if (rrc == 0) {
            executables++;
            for (int i = 0; i < ui.line_count; i++) printf("%-15s   ui line %d: %s\n", name, i, ui.lines[i]);
        }
        free(dex);
    }

    printf("summary         = %d readable dex, %d executed by this runtime\n",
           readable, executables);
    free(apk);
    return readable > 0 ? 0 : 1;
}
