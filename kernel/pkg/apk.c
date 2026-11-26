#include "../../include/apk.h"

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int name_eq(const uint8_t *a, uint16_t alen, const char *b) {
    uint16_t i = 0;
    while (i < alen && b[i]) {
        if ((char)a[i] != b[i]) return 0;
        i++;
    }
    return i == alen && b[i] == 0;
}

static int starts_with(const uint8_t *a, uint16_t alen, const char *pfx) {
    uint16_t i = 0;
    while (pfx[i]) {
        if (i >= alen || (char)a[i] != pfx[i]) return 0;
        i++;
    }
    return 1;
}

static int find_sub(const uint8_t *hay, uint32_t hlen, const char *needle) {
    uint32_t nlen = 0;
    while (needle[nlen]) nlen++;
    if (nlen == 0 || hlen < nlen) return -1;
    for (uint32_t i = 0; i <= hlen - nlen; i++) {
        uint32_t j = 0;
        while (j < nlen && (char)hay[i + j] == needle[j]) j++;
        if (j == nlen) return (int)i;
    }
    return -1;
}

static void extract_attr(const uint8_t *xml, uint32_t xlen, const char *key,
                         char *out, uint32_t max_out) {
    int pos = find_sub(xml, xlen, key);
    if (pos < 0 || !out || max_out == 0) return;
    uint32_t klen = 0;
    while (key[klen]) klen++;
    uint32_t i = (uint32_t)pos + klen;
    uint32_t o = 0;
    while (i < xlen && (char)xml[i] != '"' && o + 1 < max_out)
        out[o++] = (char)xml[i++];
    out[o] = 0;
}

static void parse_manifest_xml(const uint8_t *xml, uint32_t xlen, struct apk_info *info) {
    extract_attr(xml, xlen, "package=\"", info->package_name, sizeof(info->package_name));
    extract_attr(xml, xlen, "android:label=\"", info->app_label, sizeof(info->app_label));
    if (!info->app_label[0])
        extract_attr(xml, xlen, "label=\"", info->app_label, sizeof(info->app_label));
    if (find_sub(xml, xlen, "systemApp=\"true\"") >= 0 ||
        find_sub(xml, xlen, "coreApp=\"true\"") >= 0 ||
        find_sub(xml, xlen, "appType=\"system\"") >= 0) {
        info->is_system_app = 1;
    }
    if (find_sub(xml, xlen, "userTestApp=\"true\"") >= 0 ||
        find_sub(xml, xlen, "testOnly=\"true\"") >= 0 ||
        find_sub(xml, xlen, "flairos.user_test_app") >= 0) {
        info->is_user_test_app = 1;
    }
    if (find_sub(xml, xlen, "debuggable=\"true\"") >= 0) {
        info->is_debuggable = 1;
    }
}

int apk_find_entry(const uint8_t *buf, uint32_t len, const char *target_name,
                   struct apk_entry *out) {
    uint32_t pos = 0;
    if (!buf || len < 30 || !target_name) return -1;
    while (pos + 30 <= len) {
        if (rd32(buf + pos) != ZIP_LOCAL_MAGIC) break;
        uint16_t method = rd16(buf + pos + 8);
        uint32_t comp_sz = rd32(buf + pos + 18);
        uint32_t uncomp_sz = rd32(buf + pos + 22);
        uint16_t name_len = rd16(buf + pos + 26);
        uint16_t extra_len = rd16(buf + pos + 28);
        uint32_t hdr_end = pos + 30u + name_len + extra_len;
        if (hdr_end > len || comp_sz > len - hdr_end) return -1;
        if (name_eq(buf + pos + 30, name_len, target_name)) {
            if (out) {
                out->data_offset = hdr_end;
                out->comp_size = comp_sz;
                out->uncomp_size = uncomp_sz;
                out->method = method;
            }
            return 0;
        }
        pos = hdr_end + comp_sz;
    }
    return -1;
}

int apk_inspect(const uint8_t *buf, uint32_t len, struct apk_info *info) {
    uint32_t pos = 0;
    if (!buf || len < 30 || !info) return -1;
    info->has_manifest = 0;
    info->has_dex = 0;
    info->has_x86_so = 0;
    info->is_system_app = 0;
    info->is_user_test_app = 0;
    info->is_debuggable = 0;
    info->entry_count = 0;
    info->dex_size = 0;
    info->package_name[0] = 0;
    info->app_label[0] = 0;
    while (pos + 30 <= len) {
        if (rd32(buf + pos) != ZIP_LOCAL_MAGIC) break;
        uint16_t method = rd16(buf + pos + 8);
        uint32_t comp_sz = rd32(buf + pos + 18);
        uint32_t uncomp_sz = rd32(buf + pos + 22);
        uint16_t name_len = rd16(buf + pos + 26);
        uint16_t extra_len = rd16(buf + pos + 28);
        uint32_t hdr_end = pos + 30u + name_len + extra_len;
        if (hdr_end > len || comp_sz > len - hdr_end) return -1;
        const uint8_t *nm = buf + pos + 30;
        if (name_eq(nm, name_len, "AndroidManifest.xml")) {
            info->has_manifest = 1;
            if (method == 0u && comp_sz > 0)
                parse_manifest_xml(buf + hdr_end, comp_sz, info);
        }
        if (name_eq(nm, name_len, "classes.dex")) {
            info->has_dex = 1;
            info->dex_size = uncomp_sz;
        }
        if (starts_with(nm, name_len, "lib/x86/")) info->has_x86_so = 1;
        info->entry_count++;
        pos = hdr_end + comp_sz;
    }
    return info->entry_count > 0 ? 0 : -1;
}

int apk_extract_stored(const uint8_t *buf, uint32_t len, const char *target_name,
                       uint8_t *out, uint32_t max_out) {
    struct apk_entry ent;
    if (!out || apk_find_entry(buf, len, target_name, &ent) != 0) return -1;
    if (ent.method != 0u || ent.comp_size > max_out) return -2;
    for (uint32_t i = 0; i < ent.comp_size; i++)
        out[i] = buf[ent.data_offset + i];
    return (int)ent.comp_size;
}
