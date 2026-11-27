#ifndef FLAIROS_AXML_H
#define FLAIROS_AXML_H

#include <stdint.h>

/* Values parsed out of a real APK's binary AndroidManifest.xml.
 *
 * The manifest is AXML (a chunked binary format with a string pool and an
 * android-resource-id map), not text, and on every real package it is stored
 * DEFLATE-compressed inside the ZIP. Callers hand in a scratch buffer for the
 * decompressed manifest and a raw-inflate function.
 */
struct apk_manifest {
    char package_name[96];
    char version_name[48];
    uint32_t version_code;
    uint32_t min_sdk;
    uint32_t target_sdk;
#define AXML_MAX_LAUNCHERS 16
    char label[64];
    uint32_t label_res_id;     /* android:label when it is a @string reference */
    char label_resolved;       /* 1 when label[] came from resources.arsc */
    /* Every activity/activity-alias whose intent-filter has both MAIN and
     * LAUNCHER, in manifest order. Real packages can declare more than one
     * (F-Droid ships a disguise launcher next to its real one). */
    char launch_activity[AXML_MAX_LAUNCHERS][128];
    int launch_count;
    char app_class[128];
    uint32_t element_count;
    uint32_t attribute_count;
    uint32_t unknown_chunks;   /* chunk types the parser skipped */
};

/* Raw inflate signature shared with ported/developer/libs/flair_zlib. */
typedef int (*apk_inflate_fn)(const uint8_t *src, uint32_t src_len,
                              uint8_t *dst, uint32_t dst_cap, uint32_t *out_len);

/* Parse the AXML manifest already decompressed in memory. Returns 0 on success,
 * negative when the buffer is not AXML. */
int axml_parse(const uint8_t *xml, uint32_t len, struct apk_manifest *out);

/* Resolve a 0xPPTTEEEE resource id to its string value from a resources.arsc
 * table (stored, uncompressed, as it is in real packages). */
int arsc_string_for_id(const uint8_t *arsc, uint32_t len, uint32_t res_id,
                       char *out, uint32_t cap);

/* Full path: find AndroidManifest.xml in the APK, decompress it into scratch,
 * parse it, and resolve the label through resources.arsc when it is a
 * reference. Returns 0 when a manifest was parsed. */
int apk_read_manifest(const uint8_t *apk, uint32_t apk_len,
                      struct apk_manifest *out,
                      uint8_t *scratch, uint32_t scratch_cap,
                      apk_inflate_fn inflate);

#endif
