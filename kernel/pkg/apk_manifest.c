/* SPDX-License-Identifier: AGPL-3.0-only */
/* Real binary AndroidManifest.xml (AXML) and resources.arsc reader.
 *
 * Real packages do not ship a text manifest: AndroidManifest.xml is a chunked
 * binary structure (RES_XML_TYPE) holding a string pool, an android resource-id
 * map for repeated attributes, and start/end element chunks whose attributes
 * carry a typed value. That is why a string search for "package=\"" finds
 * nothing on a real APK. This file walks the chunks instead, and resolves a
 * @string label through resources.arsc when the manifest only holds a reference.
 *
 * Unknown chunk types are counted and skipped, never walked into, so a new
 * chunk from a future platform degrades to "not understood" instead of a crash.
 */
#include "../../include/axml.h"
#include "../../include/apk.h"

#define AXML_TYPE_STRING_POOL   0x0001u
#define AXML_TYPE_XML           0x0003u
#define AXML_TYPE_START_NS      0x0100u
#define AXML_TYPE_END_NS        0x0101u
#define AXML_TYPE_START_ELEM    0x0102u
#define AXML_TYPE_END_ELEM      0x0103u
#define AXML_TYPE_CDATA         0x0104u
#define AXML_TYPE_RESOURCE_MAP  0x0180u
#define AXML_TYPE_TABLE         0x0002u
#define AXML_TYPE_TABLE_PACKAGE 0x0200u
#define AXML_TYPE_TABLE_TYPE    0x0201u

#define AXML_ATTR_LABEL        0x01010001u
#define AXML_ATTR_NAME         0x01010003u
#define AXML_ATTR_ENABLED      0x0101000eu
#define AXML_ATTR_MINSDK       0x0101020cu
#define AXML_ATTR_VERSION_CODE 0x0101021bu
#define AXML_ATTR_VERSION_NAME 0x0101021cu
#define AXML_ATTR_TARGETSDK    0x01010270u

#define AXML_VALUE_STRING      0x03u
#define AXML_VALUE_REFERENCE   0x01u

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void copy_str(char *dst, uint32_t cap, const char *src) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int element_is(const char *name, const char *want) {
    return str_eq(name, want);
}

static unsigned long strtoul_simple(const char *s) {
    unsigned long v = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10u + (unsigned long)(*s - '0'); s++; }
    return v;
}

static void u32_to_ascii(char *out, uint32_t cap, uint32_t v) {
    char tmp[16];
    int n = 0;
    if (cap == 0) return;
    if (v == 0) { copy_str(out, cap, "0"); return; }
    while (v && n < (int)sizeof tmp) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    uint32_t o = 0;
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}

struct axml_pool {
    const uint8_t *base;
    uint32_t size;
    uint32_t count;
    uint32_t strings_start;
    int utf8;
};

static int pool_init(struct axml_pool *p, const uint8_t *chunk, uint32_t avail) {
    if (!p || avail < 28 || rd16(chunk) != AXML_TYPE_STRING_POOL) return -1;
    uint16_t hsize = rd16(chunk + 2);
    uint32_t size = rd32(chunk + 4);
    if (hsize < 28 || size < hsize || size > avail) return -1;
    p->base = chunk;
    p->size = size;
    p->count = rd32(chunk + 8);
    p->strings_start = rd32(chunk + 20);
    p->utf8 = (rd32(chunk + 16) & 0x100u) != 0;
    if (p->count > (size - hsize) / 4u) return -1;
    if (p->strings_start > size) return -1;
    return 0;
}

/* Decode one pool entry. Returns the number of characters written (0 for an
 * empty or out-of-range index), never overruns out. */
static int pool_get(const struct axml_pool *p, uint32_t idx, char *out, uint32_t cap) {
    if (!p || !out || cap == 0) return -1;
    out[0] = 0;
    if (idx >= p->count) return 0;
    uint32_t off = rd32(p->base + rd16(p->base + 2) + idx * 4u);
    uint32_t pos = p->strings_start + off;
    if (pos >= p->size) return 0;

    uint32_t i = 0;
    if (p->utf8) {
        uint32_t len = p->base[pos++];
        if (len & 0x80u) len = ((len & 0x7fu) << 8) | p->base[pos++];
        uint32_t blen = p->base[pos++];
        if (blen & 0x80u) blen = ((blen & 0x7fu) << 8) | p->base[pos++];
        if (pos + blen > p->size) blen = p->size > pos ? p->size - pos : 0;
        while (i < len && i + 1 < cap && i < blen) { out[i] = (char)p->base[pos + i]; i++; }
    } else {
        uint32_t len = rd16(p->base + pos); pos += 2;
        if (len & 0x8000u) { len = ((len & 0x7fffu) << 16) | rd16(p->base + pos); pos += 2; }
        while (i < len && i + 1 < cap && pos + 2 <= p->size) {
            uint16_t ch = rd16(p->base + pos);
            out[i] = ch < 0x80u ? (char)ch : '?';
            pos += 2;
            i++;
        }
    }
    out[i] = 0;
    return (int)i;
}

struct axml_attr {
    uint32_t res_id;      /* android resource id, 0 when the attribute has none */
    char name[40];
    uint32_t raw;         /* string index, or 0xFFFFFFFF */
    uint8_t type;
    uint32_t data;
};

static int attr_key(const struct axml_pool *pool, const uint8_t *resmap,
                    uint32_t resmap_count, uint32_t name_idx, struct axml_attr *a) {
    a->res_id = 0;
    a->name[0] = 0;
    if (name_idx < resmap_count && resmap) a->res_id = rd32(resmap + name_idx * 4u);
    (void)pool_get(pool, name_idx, a->name, sizeof a->name);
    return 0;
}

static void attr_string(const struct axml_pool *pool, const struct axml_attr *a,
                        char *out, uint32_t cap) {
    out[0] = 0;
    if (a->type == AXML_VALUE_STRING) {
        (void)pool_get(pool, a->data, out, cap);
    } else if (a->raw != 0xFFFFFFFFu) {
        (void)pool_get(pool, a->raw, out, cap);
    }
}

static int attr_is(const struct axml_attr *a, uint32_t res_id, const char *name) {
    if (res_id && a->res_id == res_id) return 1;
    if (!name) return 0;
    uint32_t i = 0;
    while (a->name[i] && name[i]) {
        if (a->name[i] != name[i]) return 0;
        i++;
    }
    return a->name[i] == 0 && name[i] == 0;
}

int axml_parse(const uint8_t *xml, uint32_t len, struct apk_manifest *out) {
    if (!xml || !out || len < 8 || rd16(xml) != AXML_TYPE_XML) return -1;

    out->package_name[0] = 0;
    out->version_name[0] = 0;
    out->version_code = 0;
    out->min_sdk = 0;
    out->target_sdk = 0;
    out->label[0] = 0;
    out->label_res_id = 0;
    out->label_resolved = 0;
    out->launch_count = 0;
    out->app_class[0] = 0;
    out->element_count = 0;
    out->attribute_count = 0;
    out->unknown_chunks = 0;

    struct axml_pool pool;
    int have_pool = 0;
    const uint8_t *resmap = 0;
    uint32_t resmap_count = 0;

    char activity[128];
    int in_activity = 0, main_seen = 0, launcher_seen = 0, cur_enabled = 1;
    activity[0] = 0;

    uint32_t pos = rd16(xml + 2);
    if (pos < 8) pos = 8;
    while (pos + 8 <= len) {
        uint16_t type = rd16(xml + pos);
        uint16_t hsize = rd16(xml + pos + 2);
        uint32_t size = rd32(xml + pos + 4);
        if (hsize < 8 || size < hsize || pos + size > len) break;

        if (type == AXML_TYPE_STRING_POOL) {
            if (pool_init(&pool, xml + pos, size) == 0) have_pool = 1;
        } else if (type == AXML_TYPE_RESOURCE_MAP) {
            resmap = xml + pos + hsize;
            resmap_count = (size - hsize) / 4u;
        } else if (type == AXML_TYPE_START_ELEM) {
            if (!have_pool || hsize + 20u > size) { out->unknown_chunks++; pos += size; continue; }
            const uint8_t *el = xml + pos + hsize;
            uint32_t name_idx = rd32(el + 4);
            uint16_t attr_start = rd16(el + 8);
            uint16_t attr_size = rd16(el + 10);
            uint16_t attr_count = rd16(el + 12);

            char name[40];
            (void)pool_get(&pool, name_idx, name, sizeof name);
            out->element_count++;

            /* A real package can declare dozens of launcher aliases that are
             * switched off with android:enabled="false" (Fennec ships one per
             * icon theme). Track the flag before reading the attributes. */
            int is_act = element_is(name, "activity") || element_is(name, "activity-alias");
            if (is_act) { in_activity = 1; main_seen = 0; launcher_seen = 0; cur_enabled = 1; }

            const uint8_t *attrs = el + attr_start;
            uint32_t attrs_avail = size - hsize - attr_start;
            struct axml_attr a;

            for (uint16_t i = 0; i < attr_count; i++) {
                if (attr_size < 20 || (uint32_t)(i + 1u) * attr_size > attrs_avail) break;
                const uint8_t *ap = attrs + (uint32_t)i * attr_size;
                /* ResXMLTree_attribute: ns@0, name@4, rawValue@8,
                 * typedValue{size@12,res0@14,dataType@15,data@16}. */
                a.raw = rd32(ap + 8);
                a.type = ap[15];
                a.data = rd32(ap + 16);
                (void)attr_key(&pool, resmap, resmap_count, rd32(ap + 4), &a);
                out->attribute_count++;

                char value[128];
                attr_string(&pool, &a, value, sizeof value);

                if (attr_is(&a, 0, "package")) {
                    copy_str(out->package_name, sizeof out->package_name, value);
                } else if (attr_is(&a, AXML_ATTR_VERSION_CODE, "versionCode")) {
                    if (a.type == AXML_VALUE_STRING) out->version_code = (uint32_t)strtoul_simple(value);
                    else out->version_code = a.data;
                } else if (attr_is(&a, AXML_ATTR_VERSION_NAME, "versionName")) {
                    if (value[0]) copy_str(out->version_name, sizeof out->version_name, value);
                    else if (a.type >= 0x10u) u32_to_ascii(out->version_name, sizeof out->version_name, a.data);
                } else if (attr_is(&a, AXML_ATTR_MINSDK, "minSdkVersion")) {
                    out->min_sdk = (a.type == AXML_VALUE_STRING) ? (uint32_t)strtoul_simple(value) : a.data;
                } else if (attr_is(&a, AXML_ATTR_TARGETSDK, "targetSdkVersion")) {
                    out->target_sdk = (a.type == AXML_VALUE_STRING) ? (uint32_t)strtoul_simple(value) : a.data;
                } else if (attr_is(&a, AXML_ATTR_LABEL, "label") && element_is(name, "application")) {
                    if (a.type == AXML_VALUE_REFERENCE) {
                        out->label_res_id = a.data;
                    } else if (a.type == AXML_VALUE_STRING) {
                        (void)pool_get(&pool, a.data, out->label, sizeof out->label);
                        out->label_resolved = out->label[0] ? 1 : 0;
                    }
                } else if (attr_is(&a, AXML_ATTR_ENABLED, "enabled") && is_act) {
                    if (a.type == 0x12u && a.data == 0u) cur_enabled = 0;
                } else if (attr_is(&a, AXML_ATTR_NAME, "name")) {
                    if (element_is(name, "activity") || element_is(name, "activity-alias")) {
                        copy_str(activity, sizeof activity, value);
                    } else if (element_is(name, "application")) {
                        copy_str(out->app_class, sizeof out->app_class, value);
                    } else if (element_is(name, "action")) {
                        if (str_eq(value, "android.intent.action.MAIN")) main_seen = 1;
                    } else if (element_is(name, "category")) {
                        if (str_eq(value, "android.intent.category.LAUNCHER")) launcher_seen = 1;
                    }
                }

                /* action/category live inside intent-filter; also match by name
                 * when the attribute carried no android resource id. */
                if (!a.res_id) {
                    if (element_is(name, "action") && str_eq(value, "android.intent.action.MAIN")) main_seen = 1;
                    if (element_is(name, "category") && str_eq(value, "android.intent.category.LAUNCHER")) launcher_seen = 1;
                }
            }


        } else if (type == AXML_TYPE_END_ELEM) {
            if (have_pool && hsize + 8u <= size) {
                char name[40];
                (void)pool_get(&pool, rd32(xml + pos + hsize + 4), name, sizeof name);
                if (element_is(name, "activity") || element_is(name, "activity-alias")) {
                    if (in_activity && main_seen && launcher_seen && activity[0] &&
                        cur_enabled && out->launch_count < AXML_MAX_LAUNCHERS) {
                        int dup = 0;
                        for (int i = 0; i < out->launch_count; i++)
                            if (str_eq(out->launch_activity[i], activity)) dup = 1;
                        if (!dup)
                            copy_str(out->launch_activity[out->launch_count++], 128, activity);
                    }
                    in_activity = 0;
                }
            }
        } else if (type == AXML_TYPE_START_NS || type == AXML_TYPE_END_NS ||
                   type == AXML_TYPE_CDATA) {
            /* no payload we need */
        } else {
            out->unknown_chunks++;
        }
        pos += size;
    }
    return out->package_name[0] ? 0 : -2;
}

/* Locate the Res_value for one resource id in the (default config) entry
 * tables. Returns 0 and fills the type and data outputs on success, -1 when the id has no
 * entry in any config chunk we walk. */
static int arsc_entry_value(const uint8_t *arsc, uint32_t len, uint32_t res_id,
                            uint8_t *vtype, uint32_t *data) {
    uint32_t pos = rd16(arsc + 2);
    struct axml_pool values;
    if (pos + 8u > len || pool_init(&values, arsc + pos, len - pos) != 0) return -1;
    uint32_t pkg_id = (res_id >> 24) & 0xffu;
    uint32_t type_id = (res_id >> 16) & 0xffu;
    uint32_t entry_id = res_id & 0xffffu;

    uint32_t cursor = pos + values.size;
    while (cursor + 8u <= len) {
        uint16_t type = rd16(arsc + cursor);
        uint16_t hsize = rd16(arsc + cursor + 2);
        uint32_t size = rd32(arsc + cursor + 4);
        if (hsize < 8 || size < hsize || cursor + size > len) break;

        if (type == AXML_TYPE_TABLE_PACKAGE && hsize >= 12u) {
            uint32_t this_pkg = rd32(arsc + cursor + 8);
            if (this_pkg == pkg_id) {
                uint32_t inner = cursor + hsize;
                while (inner + 8u <= cursor + size) {
                    uint16_t ity = rd16(arsc + inner);
                    uint16_t ihs = rd16(arsc + inner + 2);
                    uint32_t isz = rd32(arsc + inner + 4);
                    if (ihs < 8 || isz < ihs || inner + isz > cursor + size) break;
                    if (ity == AXML_TYPE_TABLE_TYPE && ihs >= 20u) {
                        uint8_t this_type = arsc[inner + 8];
                        uint8_t chunk_flags = arsc[inner + 9];
                        uint32_t entry_count = rd32(arsc + inner + 12);
                        uint32_t entries_start = rd32(arsc + inner + 16);
                        if (this_type == type_id) {
                            uint32_t off = 0xFFFFFFFFu;
                            if (chunk_flags & 1u) {
                                /* Sparse table: pairs of (index, offset/4). */
                                for (uint32_t i = 0; i < entry_count; i++) {
                                    uint32_t at = inner + ihs + i * 4u;
                                    if (at + 4u > inner + isz) break;
                                    if (rd16(arsc + at) == entry_id) {
                                        off = (uint32_t)rd16(arsc + at + 2) * 4u;
                                        break;
                                    }
                                }
                            } else if (entry_id < entry_count) {
                                uint32_t off_pos = inner + ihs + entry_id * 4u;
                                if (off_pos + 4u <= inner + isz) off = rd32(arsc + off_pos);
                            }
                            if (off != 0xFFFFFFFFu && entry_id < entry_count) {
                                uint32_t entry = inner + entries_start + off;
                                if (entry + 8u <= inner + isz) {
                                    uint16_t esize = rd16(arsc + entry);
                                    uint16_t eflags = rd16(arsc + entry + 2);
                                    if (!(eflags & 1u) && esize >= 8u && entry + esize + 8u <= len) {
                                        const uint8_t *val = arsc + entry + esize;
                                        *vtype = val[3];
                                        *data = rd32(val + 4);
                                        return 0;
                                    }
                                }
                            }
                        }
                    }
                    inner += isz;
                }
            }
        }
        cursor += size;
    }
    return -1;
}

int arsc_string_for_id(const uint8_t *arsc, uint32_t len, uint32_t res_id,
                       char *out, uint32_t cap) {
    if (!arsc || !out || cap == 0 || len < 12 || rd16(arsc) != AXML_TYPE_TABLE) return -1;
    out[0] = 0;
    uint32_t pos = rd16(arsc + 2);
    struct axml_pool values;
    if (pos + 8u > len || pool_init(&values, arsc + pos, len - pos) != 0) return -2;

    /* Resource entries may point at other resources (F-Droid's app_name is a
     * reference to a second id), so follow the chain a bounded number of
     * times instead of giving up on the first indirection. */
    for (int depth = 0; depth < 8; depth++) {
        uint8_t vtype = 0;
        uint32_t data = 0;
        if (arsc_entry_value(arsc, len, res_id, &vtype, &data) != 0) return -3;
        if (vtype == AXML_VALUE_STRING)
            return pool_get(&values, data, out, cap) > 0 ? 0 : -3;
        if (vtype != AXML_VALUE_REFERENCE) return -3;
        res_id = data;
    }
    return -3;
}

int apk_read_manifest(const uint8_t *apk, uint32_t apk_len,
                      struct apk_manifest *out,
                      uint8_t *scratch, uint32_t scratch_cap,
                      apk_inflate_fn inflate) {
    if (!apk || !out) return -1;
    struct apk_entry ent;
    if (apk_find_entry(apk, apk_len, "AndroidManifest.xml", &ent) != 0) return -1;

    const uint8_t *xml;
    uint32_t xml_len;
    if (ent.method == 0u) {
        xml = apk + ent.data_offset;
        xml_len = ent.comp_size;
    } else if (ent.method == 8u && inflate && scratch && ent.uncomp_size <= scratch_cap) {
        uint32_t out_len = 0;
        if (inflate(apk + ent.data_offset, ent.comp_size, scratch, scratch_cap, &out_len) != 0) return -2;
        xml = scratch;
        xml_len = out_len;
    } else {
        return -3;
    }

    int rc = axml_parse(xml, xml_len, out);
    if (rc != 0) return rc;

    if (out->label_res_id && !out->label[0]) {
        struct apk_entry arsc;
        if (apk_find_entry(apk, apk_len, "resources.arsc", &arsc) == 0 && arsc.method == 0u) {
            if (arsc_string_for_id(apk + arsc.data_offset, arsc.comp_size, out->label_res_id,
                                   out->label, sizeof out->label) == 0)
                out->label_resolved = 1;
        }
    }
    return 0;
}
