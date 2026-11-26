#ifndef AIRP_H
#define AIRP_H

/* FlairOS AIRP package format.
 *
 * Version 1 (legacy) is a 36-byte header followed by a flat executable image:
 *   struct airp_header + image bytes.
 * Version 2 adds flags, an executable payload with its own integrity check, a
 * UTF-8 JSON manifest, and a typed resource table with per-resource CRCs.
 *
 * A package is a container. When it carries an executable, that executable is
 * still a flat image linked for TX_APP_BASE, with TX_APP_MAGIC and the absolute
 * entry address as its first two words; the container never changes how the
 * image is linked. A component package (AIRP_FLAG_RESOURCES_ONLY) has no
 * executable at all: its image_size and entry are zero. That is the explicit
 * container/executable split: FlairDesktop.airp is such a component, because
 * the session shell implementation lives in the kernel and the package carries
 * the declarative session definition (manifest, theme, launcher, strings).
 *
 * All integers are little-endian. All offsets are relative to the start of the
 * file. tools/airpkg.py writes and inspects this format; kernel/airp.c reads
 * and validates it.
 */

#define AIRP_MAGIC            0x50524941u /* bytes on disk: "AIRP" */
#define AIRP_VERSION_V1       1
#define AIRP_VERSION_V2       2
#define AIRP_VERSION          AIRP_VERSION_V2
#define AIRP_API_LEVEL        2            /* adds the session UI accent API (syscall 38) */
#define AIRP_NAME_MAX         12           /* v1 name is NUL padded, at most 11 characters */
#define AIRP_IMAGE_MAX        131072       /* maximum flat executable image (256 sectors) */
#define AIRP_SLOT_SIZE        262144       /* private app image plus stack region (64 pages) */

#define AIRP_HEADER_V1_SIZE   36u
#define AIRP_HEADER_V2_SIZE   64u
#define AIRP_RESOURCE_SIZE    24u
#define AIRP_RES_MAX          64
#define AIRP_RES_NAME_MAX     40

/* Package flags (airp_v2_header.flags). */
#define AIRP_FLAG_HAS_ICON    0x0001u
#define AIRP_FLAG_SYSTEM      0x0002u  /* installed by the OS image, not a user app */
#define AIRP_FLAG_DESKTOP     0x0004u  /* session shell package */
#define AIRP_FLAG_RESOURCES_ONLY 0x0008u /* component package: resources, no executable */

/* Resource flags (airp_resource_entry.flags). */
#define AIRP_RF_REQUIRED      0x0001u

/* Architecture identifiers (airp_v2_header.arch). */
#define AIRP_ARCH_ANY         0xFFFFu
#define AIRP_ARCH_I386        0x0000u
#define AIRP_ARCH_X86_64      0x0001u
#define AIRP_ARCH_AARCH64     0x0002u

/* Resource types (airp_resource_entry.type). */
#define AIRP_RT_BLOB          0u
#define AIRP_RT_MANIFEST      1u
#define AIRP_RT_THEME         2u
#define AIRP_RT_ICON          3u
#define AIRP_RT_FONT          4u
#define AIRP_RT_SOUND         5u
#define AIRP_RT_STRINGS       6u
#define AIRP_RT_UI            7u
#define AIRP_RT_PLUGIN        8u
#define AIRP_RT_LIBRARY       9u

/* Loader results. Values are stable and documented so the shell can print
   something more useful than a number. */
#define AIRP_LOAD_OK          0
#define AIRP_LOAD_TOO_SHORT  (-1)
#define AIRP_LOAD_BAD_MAGIC  (-2)
#define AIRP_LOAD_VERSION    (-3)
#define AIRP_LOAD_HEADER     (-4)
#define AIRP_LOAD_API        (-5)
#define AIRP_LOAD_BASE       (-6)
#define AIRP_LOAD_SIZE       (-7)
#define AIRP_LOAD_ENTRY      (-8)
#define AIRP_LOAD_IMAGE      (-9)
#define AIRP_LOAD_ARCH      (-10)
#define AIRP_LOAD_CRC       (-11)
#define AIRP_LOAD_MANIFEST  (-12)
#define AIRP_LOAD_RESOURCE  (-13)
#define AIRP_LOAD_NOT_FOUND (-14)
#define AIRP_LOAD_NO_EXEC   (-15)

/* Legacy version 1 header. Kept so existing packages and tools keep working. */
struct airp_header {
    unsigned int magic;
    unsigned short version;
    unsigned short header_size;
    unsigned int api_level;
    unsigned int image_base;   /* must equal TX_APP_BASE in version 1 */
    unsigned int entry;        /* absolute address inside the image */
    unsigned int image_size;   /* bytes after the header */
    char name[AIRP_NAME_MAX];
} __attribute__((packed));

/* Version 2 header, exactly 64 bytes. */
struct airp_v2_header {
    unsigned int magic;
    unsigned short version;
    unsigned short header_size;
    unsigned int api_level;
    unsigned int image_base;
    unsigned int entry;
    unsigned int image_size;
    unsigned int flags;
    unsigned int manifest_off;
    unsigned int manifest_len;
    unsigned int resource_count;
    unsigned int resource_tab_off;
    unsigned int resource_tab_len;
    unsigned int arch;
    unsigned int header_crc;   /* CRC32 of this header with header_crc zeroed */
    unsigned int payload_crc;  /* CRC32 of the executable payload */
    unsigned int file_size;    /* total container size on disk */
} __attribute__((packed));

/* One resource record, exactly 24 bytes. Names live in a NUL-terminated string
   blob addressed by name_off. */
struct airp_resource_entry {
    unsigned int name_off;
    unsigned int data_off;
    unsigned int data_len;
    unsigned int crc;
    unsigned short type;
    unsigned short flags;
    unsigned int reserved;
} __attribute__((packed));

/* A validated package. The kernel works from this view; it never modifies the
   package bytes, which may live in a read-only staging buffer. */
struct airp_package {
    const unsigned char *file;
    unsigned int file_len;
    unsigned int version;
    unsigned int api_level;
    unsigned int image_base;
    unsigned int entry;
    unsigned int image_size;
    unsigned int arch;
    unsigned int flags;
    const unsigned char *payload;
    const unsigned char *manifest;
    unsigned int manifest_len;
    unsigned int resource_count;
    const unsigned char *resource_table;
};

/* Borrowed pointer plus length into a package. */
struct airp_view {
    const unsigned char *data;
    unsigned int size;
};

/* Validate a package and fill `out`. `expected_arch` is one of AIRP_ARCH_*;
   AIRP_ARCH_ANY accepts every package. A version 1 package is accepted and
   reported as version 1 with no manifest or resources. */
int airp_open(const unsigned char *file, unsigned int len,
              unsigned int expected_arch, struct airp_package *out);

/* Look up a resource by exact name. Returns AIRP_LOAD_OK and fills `type` and
   `out`, or AIRP_LOAD_NOT_FOUND. Data integrity is checked during airp_open. */
int airp_find_resource(const struct airp_package *pkg, const char *name,
                       unsigned int *type, struct airp_view *out);

/* Look up the first resource of a type. Useful for "the theme" or "the icon"
   without hardcoding a name. */
int airp_find_resource_type(const struct airp_package *pkg, unsigned int type,
                            struct airp_view *out, char *name_out, unsigned int name_cap);

/* Copy the executable payload to TX_APP_BASE, clear the rest of the slot, and
   store the entry address. Accepts version 1 and version 2 packages. */
int airp_load(const unsigned char *file, unsigned int len, unsigned int *entry);

/* Human-readable text for a loader result. */
const char *airp_error_string(int code);

/* CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320). Exposed because the
   packaging tools and tests must agree bit-for-bit with the kernel. */
unsigned int airp_crc32(const unsigned char *data, unsigned int len);

#endif
