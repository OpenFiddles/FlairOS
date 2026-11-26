#include "textile.h"
#include "../include/airp.h"

typedef unsigned char u8;
typedef unsigned int u32;

/* The on-disk layouts are a contract with tools/airpkg.py. Fail the build if a
   compiler ever changes one of these sizes. */
typedef char airp_assert_v2_size[(sizeof(struct airp_v2_header) == AIRP_HEADER_V2_SIZE) ? 1 : -1];
typedef char airp_assert_res_size[(sizeof(struct airp_resource_entry) == AIRP_RESOURCE_SIZE) ? 1 : -1];
typedef char airp_assert_v1_size[(sizeof(struct airp_header) == AIRP_HEADER_V1_SIZE) ? 1 : -1];

/* airp_load copies payloads into the fixed application slot. On a POSIX host
   that address is not mapped, so host tests get a private buffer of the same
   size. Kernel builds always use the real slot. */
#ifdef FLAIROS_HOST_TEST
unsigned char airp_host_slot[AIRP_SLOT_SIZE] __attribute__((aligned(16)));
#define AIRP_APP_SLOT airp_host_slot
#else
#define AIRP_APP_SLOT ((u8 *)TX_APP_BASE)
#endif

unsigned int airp_crc32(const u8 *data, u32 len) {
    u32 crc = 0xFFFFFFFFu;
    u32 i, bit;
    if (!data && len)
        return 0;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static u32 le16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

static u32 le32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int name_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}


/* True when [off, off+len) fits entirely inside a file of file_len bytes. */
static int range_ok(u32 off, u32 len, u32 file_len) {
    if (off > file_len)
        return 0;
    return len <= file_len - off;
}

const char *airp_error_string(int code) {
    switch (code) {
    case AIRP_LOAD_OK:         return "ok";
    case AIRP_LOAD_TOO_SHORT:  return "file is smaller than the AIRP header";
    case AIRP_LOAD_BAD_MAGIC:  return "missing AIRP signature";
    case AIRP_LOAD_VERSION:    return "unsupported AIRP version";
    case AIRP_LOAD_HEADER:     return "header size does not match version";
    case AIRP_LOAD_API:        return "package needs a newer API level";
    case AIRP_LOAD_BASE:       return "image base is not this kernel's app slot";
    case AIRP_LOAD_SIZE:       return "size is invalid or out of range";
    case AIRP_LOAD_ENTRY:      return "entry address is outside the image";
    case AIRP_LOAD_IMAGE:      return "image is missing the app signature";
    case AIRP_LOAD_ARCH:       return "package targets a different CPU architecture";
    case AIRP_LOAD_CRC:        return "checksum mismatch, package is corrupt";
    case AIRP_LOAD_MANIFEST:   return "manifest is out of range";
    case AIRP_LOAD_RESOURCE:   return "resource table is invalid";
    case AIRP_LOAD_NOT_FOUND:  return "resource not found";
    case AIRP_LOAD_NO_EXEC:    return "package has no executable payload";
    default:                   return "unknown AIRP error";
    }
}

/* Recomputes the header CRC over the on-disk header bytes with the CRC field
   itself treated as zero, without assuming struct padding. */
static u32 header_crc(const u8 *file) {
    u8 copy[AIRP_HEADER_V2_SIZE];
    u32 i;
    for (i = 0; i < AIRP_HEADER_V2_SIZE; i++)
        copy[i] = file[i];
    copy[52] = copy[53] = copy[54] = copy[55] = 0;
    return airp_crc32(copy, AIRP_HEADER_V2_SIZE);
}

static const struct airp_resource_entry *resource_at(const struct airp_package *pkg, u32 index) {
    return (const struct airp_resource_entry *)(pkg->resource_table +
                                                index * AIRP_RESOURCE_SIZE);
}

/* Reads a resource name from the name blob that follows the resource table.
   Names are NUL-terminated and are validated to start and end inside the file. */
static int resource_name(const struct airp_package *pkg, u32 index, char *out, u32 cap) {
    const struct airp_resource_entry *e = resource_at(pkg, index);
    u32 off = e->name_off;
    u32 i;
    if (off >= pkg->file_len)
        return AIRP_LOAD_RESOURCE;
    for (i = 0; i + off < pkg->file_len && i + 1u < cap; i++) {
        out[i] = (char)pkg->file[off + i];
        if (out[i] == 0)
            return AIRP_LOAD_OK;
    }
    out[cap ? cap - 1u : 0] = 0;
    return AIRP_LOAD_RESOURCE;
}

/* Validates a version 2 package completely before returning a usable view. */
static int airp_open_v2(const u8 *file, u32 len, u32 expected_arch,
                        struct airp_package *out) {
    u32 api_level, image_base, entry, image_size, flags;
    u32 manifest_off, manifest_len;
    u32 resource_count, tab_off, tab_len;
    u32 arch, stored_header_crc, payload_crc, file_size;
    u32 tail_off, tail_len, i;

    if (le16(file + 6) != AIRP_HEADER_V2_SIZE)
        return AIRP_LOAD_HEADER;
    if (header_crc(file) != le32(file + 52))
        return AIRP_LOAD_CRC;

    api_level = le32(file + 8);
    if (api_level > AIRP_API_LEVEL)
        return AIRP_LOAD_API;

    image_base = le32(file + 12);
    entry = le32(file + 16);
    image_size = le32(file + 20);
    flags = le32(file + 24);
    manifest_off = le32(file + 28);
    manifest_len = le32(file + 32);
    resource_count = le32(file + 36);
    tab_off = le32(file + 40);
    tab_len = le32(file + 44);
    arch = le32(file + 48);
    stored_header_crc = le32(file + 52);
    payload_crc = le32(file + 56);
    file_size = le32(file + 60);
    (void)stored_header_crc;

    if (file_size < AIRP_HEADER_V2_SIZE || file_size > len)
        return AIRP_LOAD_SIZE;
    if (expected_arch != AIRP_ARCH_ANY && arch != AIRP_ARCH_ANY && arch != expected_arch)
        return AIRP_LOAD_ARCH;

    if (!(flags & AIRP_FLAG_RESOURCES_ONLY)) {
        if (image_base != TX_APP_BASE)
            return AIRP_LOAD_BASE;
        if (image_size < 8 || image_size > AIRP_IMAGE_MAX)
            return AIRP_LOAD_SIZE;
        if (!range_ok(AIRP_HEADER_V2_SIZE, image_size, file_size))
            return AIRP_LOAD_SIZE;
        if (entry < TX_APP_BASE || entry - TX_APP_BASE >= image_size)
            return AIRP_LOAD_ENTRY;
    } else {
        /* A component package declares itself by carrying no executable. */
        if (image_size != 0 || entry != 0)
            return AIRP_LOAD_HEADER;
    }

    if (manifest_len && !range_ok(manifest_off, manifest_len, file_size))
        return AIRP_LOAD_MANIFEST;

    if (resource_count > AIRP_RES_MAX)
        return AIRP_LOAD_RESOURCE;
    if (resource_count) {
        if (tab_len != resource_count * AIRP_RESOURCE_SIZE)
            return AIRP_LOAD_RESOURCE;
        if (!range_ok(tab_off, tab_len, file_size))
            return AIRP_LOAD_RESOURCE;
        if (tab_off < AIRP_HEADER_V2_SIZE + image_size)
            return AIRP_LOAD_RESOURCE;
    } else if (tab_len) {
        return AIRP_LOAD_RESOURCE;
    }

    /* Everything after the header, up to the declared package size, is covered
       by the payload CRC. A staging buffer may be padded to whole sectors. */
    tail_off = AIRP_HEADER_V2_SIZE;
    tail_len = file_size - tail_off;
    if (airp_crc32(file + tail_off, tail_len) != payload_crc)
        return AIRP_LOAD_CRC;

    out->version = AIRP_VERSION_V2;
    out->api_level = api_level;
    out->image_base = image_base;
    out->entry = entry;
    out->image_size = image_size;
    out->arch = arch;
    out->flags = flags;
    out->payload = file + AIRP_HEADER_V2_SIZE;
    out->manifest = manifest_len ? file + manifest_off : 0;
    out->manifest_len = manifest_len;
    out->resource_count = resource_count;
    out->resource_table = resource_count ? file + tab_off : 0;
    out->file = file;
    out->file_len = file_size;

    /* Validate each resource record against its CRC before anyone reads it. */
    for (i = 0; i < resource_count; i++) {
        const struct airp_resource_entry *e = resource_at(out, i);
        char scratch[AIRP_RES_NAME_MAX + 1];
        int rc;
        if (!range_ok(e->data_off, e->data_len, file_size))
            return AIRP_LOAD_RESOURCE;
        /* Names live in the blob directly after the resource table; refusing
           offsets below it stops a crafted table pointing into the payload. */
        if (e->name_off < tab_off + tab_len || e->name_off >= file_size)
            return AIRP_LOAD_RESOURCE;
        if (airp_crc32(file + e->data_off, e->data_len) != e->crc)
            return AIRP_LOAD_CRC;
        rc = resource_name(out, i, scratch, sizeof(scratch));
        if (rc != AIRP_LOAD_OK)
            return rc;
    }
    return AIRP_LOAD_OK;
}

int airp_open(const u8 *file, u32 len, u32 expected_arch,
              struct airp_package *out) {
    if (!out)
        return AIRP_LOAD_TOO_SHORT;
    if (!file || len < AIRP_HEADER_V1_SIZE)
        return AIRP_LOAD_TOO_SHORT;

    out->file = file;
    out->file_len = len;
    out->manifest = 0;
    out->manifest_len = 0;
    out->resource_count = 0;
    out->resource_table = 0;
    out->payload = 0;
    out->flags = 0;
    out->arch = AIRP_ARCH_I386;
    out->version = 0;
    out->api_level = 0;
    out->image_base = 0;
    out->entry = 0;
    out->image_size = 0;

    if (le32(file) != AIRP_MAGIC)
        return AIRP_LOAD_BAD_MAGIC;

    if (le16(file + 4) == AIRP_VERSION_V1) {
        const struct airp_header *vh = (const struct airp_header *)file;
        u32 image_size, entry;
        if (vh->header_size != AIRP_HEADER_V1_SIZE)
            return AIRP_LOAD_HEADER;
        if (vh->api_level > AIRP_API_LEVEL)
            return AIRP_LOAD_API;
        if (vh->image_base != TX_APP_BASE)
            return AIRP_LOAD_BASE;
        image_size = vh->image_size;
        entry = vh->entry;
        if (image_size < 8 || image_size > AIRP_IMAGE_MAX)
            return AIRP_LOAD_SIZE;
        if (!range_ok(AIRP_HEADER_V1_SIZE, image_size, len))
            return AIRP_LOAD_SIZE;
        if (entry < TX_APP_BASE || entry - TX_APP_BASE >= image_size)
            return AIRP_LOAD_ENTRY;
        if (expected_arch != AIRP_ARCH_ANY && expected_arch != AIRP_ARCH_I386)
            return AIRP_LOAD_ARCH;
        out->version = AIRP_VERSION_V1;
        out->api_level = vh->api_level;
        out->image_base = vh->image_base;
        out->entry = entry;
        out->image_size = image_size;
        out->payload = file + AIRP_HEADER_V1_SIZE;
        return AIRP_LOAD_OK;
    }

    if (len < AIRP_HEADER_V2_SIZE)
        return AIRP_LOAD_TOO_SHORT;
    if (le16(file + 4) == AIRP_VERSION_V2)
        return airp_open_v2(file, len, expected_arch, out);
    return AIRP_LOAD_VERSION;
}

int airp_find_resource(const struct airp_package *pkg, const char *name,
                       u32 *type, struct airp_view *out) {
    u32 i;
    if (!pkg || !pkg->resource_count || !pkg->resource_table)
        return AIRP_LOAD_NOT_FOUND;
    for (i = 0; i < pkg->resource_count; i++) {
        char current[AIRP_RES_NAME_MAX + 1];
        const struct airp_resource_entry *e;
        if (resource_name(pkg, i, current, sizeof(current)) != AIRP_LOAD_OK)
            continue;
        if (!name_equal(current, name))
            continue;
        e = resource_at(pkg, i);
        if (type)
            *type = e->type;
        if (out) {
            out->data = pkg->file + e->data_off;
            out->size = e->data_len;
        }
        return AIRP_LOAD_OK;
    }
    return AIRP_LOAD_NOT_FOUND;
}

int airp_find_resource_type(const struct airp_package *pkg, u32 type,
                            struct airp_view *out, char *name_out, u32 name_cap) {
    u32 i;
    if (!pkg || !pkg->resource_count || !pkg->resource_table)
        return AIRP_LOAD_NOT_FOUND;
    for (i = 0; i < pkg->resource_count; i++) {
        const struct airp_resource_entry *e = resource_at(pkg, i);
        if (e->type != type)
            continue;
        if (out) {
            out->data = pkg->file + e->data_off;
            out->size = e->data_len;
        }
        if (name_out && name_cap)
            (void)resource_name(pkg, i, name_out, name_cap);
        return AIRP_LOAD_OK;
    }
    return AIRP_LOAD_NOT_FOUND;
}

int airp_load(const u8 *file, u32 len, u32 *entry) {
    struct airp_package pkg;
    u8 *dst = AIRP_APP_SLOT;
    u32 i, image_size, entry_value;
    int rc;

    rc = airp_open(file, len, AIRP_ARCH_I386, &pkg);
    if (rc != AIRP_LOAD_OK)
        return rc;

    image_size = pkg.image_size;
    entry_value = pkg.entry;
    if (image_size == 0)
        return AIRP_LOAD_NO_EXEC;

    for (i = 0; i < image_size; i++)
        dst[i] = pkg.payload[i];

    /* The package carries no BSS size, so clear the rest of the slot: .bss and
       any bytes left by a previous app read as zero. */
    for (i = image_size; i < AIRP_SLOT_SIZE; i++)
        dst[i] = 0;

    {
        const u32 *head = (const u32 *)AIRP_APP_SLOT;
        if (head[0] != TX_APP_MAGIC || head[1] != entry_value)
            return AIRP_LOAD_IMAGE;
    }

    if (entry)
        *entry = entry_value;
    return AIRP_LOAD_OK;
}
