#include <stdio.h>
#include <string.h>

#include "airjson.h"
#include "airp.h"
#include "ui/tv_layout.h"
#include "ui/tv_launcher.h"

/* Proves the TV session's two data paths on the host: the ten-foot layout scale
   that takes 720p to 4K, and the launcher layout read out of a component
   package. Packages are built the way tools/airpkg.py writes them, and the
   layout JSON is the shipped tv/resources/launcher.json, so a broken resource
   fails here rather than on a TV. */

static int failures;

/* Session strings are a view of the packaged JSON, so checks copy out what they
   want to compare. */
static int str_is(const struct tv_strings *s, const char *key, const char *want) {
    char buf[TV_STRING_TEXT_MAX];
    return tv_strings_copy(s, key, buf, sizeof buf) && strcmp(buf, want) == 0;
}

static int str_has(const struct tv_strings *s, const char *key) {
    char buf[TV_STRING_TEXT_MAX];
    return tv_strings_copy(s, key, buf, sizeof buf);
}

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        failures++;
}

static void put_le32(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void put_le16(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

#define PKG_RES_MAX 4
#define PKG_CAP 16384

struct pkg_res {
    const char *name;
    const char *data;
    unsigned short type;
};

static unsigned int build_pkg(unsigned char *buf, unsigned int buf_cap,
                              const struct pkg_res *res, unsigned int count,
                              unsigned int flags) {
    unsigned int offsets[PKG_RES_MAX][2];
    unsigned int i, manifest_off, manifest_len, tab_off, tab_len, name_off, file_size;
    const char *manifest = "{\"name\":\"FlairTV\",\"version\":\"1.0.0\"}";

    manifest_len = (unsigned int)strlen(manifest);
    manifest_off = AIRP_HEADER_V2_SIZE;
    tab_off = manifest_off + manifest_len;
    tab_len = count * AIRP_RESOURCE_SIZE;
    name_off = tab_off + tab_len;

    memset(buf, 0, buf_cap);
    memcpy(buf + manifest_off, manifest, manifest_len);

    {
        unsigned int cursor = name_off;
        for (i = 0; i < count; i++) {
            offsets[i][0] = cursor;
            memcpy(buf + cursor, res[i].name, strlen(res[i].name) + 1);
            cursor += (unsigned int)strlen(res[i].name) + 1;
        }
        for (i = 0; i < count; i++) {
            offsets[i][1] = cursor;
            memcpy(buf + cursor, res[i].data, strlen(res[i].data));
            cursor += (unsigned int)strlen(res[i].data);
        }
        file_size = cursor;
    }

    for (i = 0; i < count; i++) {
        unsigned char *e = buf + tab_off + i * AIRP_RESOURCE_SIZE;
        unsigned int len = (unsigned int)strlen(res[i].data);
        put_le32(e + 0, offsets[i][0]);
        put_le32(e + 4, offsets[i][1]);
        put_le32(e + 8, len);
        put_le32(e + 12, airp_crc32((const unsigned char *)res[i].data, len));
        put_le16(e + 16, res[i].type);
        put_le16(e + 18, AIRP_RF_REQUIRED);
        put_le32(e + 20, 0);
    }

    put_le32(buf + 0, AIRP_MAGIC);
    put_le16(buf + 4, AIRP_VERSION_V2);
    put_le16(buf + 6, AIRP_HEADER_V2_SIZE);
    put_le32(buf + 8, AIRP_API_LEVEL);
    put_le32(buf + 12, 0);
    put_le32(buf + 16, 0);
    put_le32(buf + 20, 0);
    put_le32(buf + 24, flags);
    put_le32(buf + 28, manifest_off);
    put_le32(buf + 32, manifest_len);
    put_le32(buf + 36, count);
    put_le32(buf + 40, tab_off);
    put_le32(buf + 44, tab_len);
    put_le32(buf + 48, AIRP_ARCH_I386);
    put_le32(buf + 56, airp_crc32(buf + AIRP_HEADER_V2_SIZE,
                                  file_size - AIRP_HEADER_V2_SIZE));
    put_le32(buf + 60, file_size);
    put_le32(buf + 52, 0);
    put_le32(buf + 52, airp_crc32(buf, AIRP_HEADER_V2_SIZE));
    return file_size;
}

/* Reads a shipped session resource; returns 0 when it cannot be read. */
static unsigned int read_shipped(const char *path, char *buf, unsigned int cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f)
        return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (unsigned int)n;
}

static const char *EMPTY_TILES =
    "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":[]}}";

static const char *TOO_MANY_TILES =
    "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":["
    "{\"id\":\"a\",\"label\":\"A\",\"target\":\"app:0\"},"
    "{\"id\":\"b\",\"label\":\"B\",\"target\":\"app:0\"},"
    "{\"id\":\"c\",\"label\":\"C\",\"target\":\"app:0\"},"
    "{\"id\":\"d\",\"label\":\"D\",\"target\":\"app:0\"},"
    "{\"id\":\"e\",\"label\":\"E\",\"target\":\"app:0\"},"
    "{\"id\":\"f\",\"label\":\"F\",\"target\":\"app:0\"},"
    "{\"id\":\"g\",\"label\":\"G\",\"target\":\"app:0\"},"
    "{\"id\":\"h\",\"label\":\"H\",\"target\":\"app:0\"},"
    "{\"id\":\"i\",\"label\":\"I\",\"target\":\"app:0\"}]}}";

static const char *LONG_LABEL =
    "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":[{\"id\":\"a\","
    "\"label\":\"This label is far too long for a ten-foot tile\","
    "\"target\":\"app:0\"}]}}";

static const char *LONG_STRING =
    "{\"focus\":\"012345678901234567890123456789012345678901234567890123456789\"}";

int main(void) {
    static char launcher_json[4096];
    static char strings_json[4096];
    static unsigned char pkg[PKG_CAP];
    struct tv_launcher tv, sentinel;
    struct tv_launcher_report report;
    char text[64];
    unsigned int len, json_len;
    const char *value;
    unsigned int value_len;
    struct airjson_doc doc;
    struct pkg_res res[2];
    int count = 0;

    json_len = read_shipped("tv/resources/launcher.json", launcher_json, sizeof launcher_json);
    check(json_len > 0, "shipped tv/resources/launcher.json is readable");
    check(read_shipped("tv/resources/strings.json", strings_json, sizeof strings_json) > 0,
          "shipped tv/resources/strings.json is readable");

    /* --- Layout scale: one 1280x720 design, correct on every panel --------- */
    check(tv_layout_scale_q16(1280, 720) == 65536, "720p keeps the design scale");
    check(tv_layout_scale_q16(1920, 1080) == 98304, "1080p scales 1.5x");
    check(tv_layout_scale_q16(2560, 1440) == 131072, "1440p scales 2x");
    check(tv_layout_scale_q16(3840, 2160) == 196608, "4K scales 3x");
    check(tv_layout_scale_q16(4096, 2160) == 196608, "wider-than-16:9 4K clamps to 3x");
    check(tv_layout_scale_q16(5120, 2880) == 196608, "5K clamps to 3x");
    check(tv_layout_scale_q16(800, 600) == 65536, "sub-720p clamps to 1x");
    check(tv_layout_scale_q16(0, 0) == 65536, "degenerate surface falls back to 1x");
    check(tv_layout_px(48, 196608) == 144, "design pixels scale on 4K");
    check(tv_layout_px(1280, 196608) == 3840, "the full design width fits 4K");
    check(tv_layout_px(48, 98304) == 72, "design pixels scale on 1080p");
    check(tv_layout_text(2, 98304) == 3, "text sizes scale with the layout");
    check(tv_layout_text(1, 4096) == 1, "text never scales below one pixel");

    /* --- AirJSON array iteration over the shipped layout ------------------- */
    check(airjson_open(&doc, launcher_json, json_len) == AIRJSON_OK,
          "shipped launcher layout parses as JSON");
    check(airjson_root_get(&doc, "tv", &value, &value_len) == AIRJSON_OK,
          "shipped layout has a tv object");
    {
        const char *tv_span = value;
        unsigned int tv_len = value_len;
        check(airjson_object_get(&doc, tv_span, tv_len, "tiles", &value, &value_len)
                  == AIRJSON_OK,
              "shipped layout has a tiles array");
        check(airjson_array_count(value, value_len, &count) == AIRJSON_OK && count == 3,
              "tiles array counts three tiles");
        {
            const char *tiles_span = value;
            unsigned int tiles_len = value_len;
            const char *tile;
            unsigned int tile_len;
            check(airjson_array_at(tiles_span, tiles_len, 0, &tile, &tile_len) == AIRJSON_OK,
                  "first tile is addressable");
            text[0] = 0;
            check(airjson_object_get(&doc, tile, tile_len, "label", &value, &value_len)
                      == AIRJSON_OK &&
                      airjson_get_string(value, value_len, text, sizeof text) == AIRJSON_OK &&
                      strcmp(text, "Hello World") == 0,
                  "first tile label comes from the resource");
            check(airjson_array_at(tiles_span, tiles_len, 3, &tile, &tile_len)
                      != AIRJSON_OK,
                  "iterating past the last tile is refused");
        }
    }
    check(airjson_array_count("{}", 2, &count) != AIRJSON_OK,
          "array helpers reject an object");

    /* --- Launcher loaded from a package ----------------------------------- */
    tv_launcher_default(&tv);
    res[0].name = "launcher.json";
    res[0].data = launcher_json;
    res[0].type = AIRP_RT_UI;
    res[1].name = "strings.json";
    res[1].data = strings_json;
    res[1].type = AIRP_RT_STRINGS;
    len = build_pkg(pkg, PKG_CAP, res, 2,
                    AIRP_FLAG_SYSTEM | AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 1 &&
              report.loaded == 1 && report.tiles == 3,
          "launcher layout loads from FlairTV.airp");
    check(strcmp(tv.title, "FlairTV Living Room") == 0, "package supplies the title");
    check(tv.tile_count == 3, "package supplies three tiles");
    check(strcmp(tv.tiles[0].label, "Hello World") == 0, "tile label comes from the package");
    check(strcmp(tv.tiles[1].target, "app:1") == 0, "tile target comes from the package");
    check(strcmp(tv.tiles[2].target, "airp:cast") == 0, "cast tile keeps its target");
    check(tv.tiles[0].accent == 0x00EC4899u, "tile accent colour comes from the package");
    check(tv.tiles[2].accent == 0x003B82F6u, "third tile accent comes from the package");

    /* --- Session strings come from the package's strings.json -------------- */
    {
        struct airjson_doc sdoc;
        int shipped_keys = 0;
        check(airjson_open(&sdoc, strings_json, (unsigned int)strlen(strings_json)) == AIRJSON_OK &&
                  airjson_object_count(strings_json, (unsigned int)strlen(strings_json),
                                       &shipped_keys) == AIRJSON_OK &&
                  shipped_keys == report.strings,
              "package supplies every session string");
    }
    check(str_is(&tv.strings, "focus", "10-FOOT FOCUS"),
          "a shell label is read from the packaged strings.json");
    check(str_is(&tv.strings, "no_signal", "No signal"),
          "strings the package adds beyond the built-in keys are kept");
    check(str_is(&tv.strings, "open", "OPEN"),
          "a built-in string the package repeats stays intact");
    check(str_is(&tv.strings, "cc.title", "FlairTV Control"),
          "the drawer's title is carried in the same strings.json");

    res[1].data = "{\"focus\":\"BIG FOCUS\"}";
    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 1 &&
              str_is(&tv.strings, "focus", "BIG FOCUS"),
          "a package string overrides the built-in wording");
    check(str_is(&tv.strings, "home", "HOME"),
          "strings the package does not declare keep their built-in wording");
    check(str_is(&tv.strings, "cc.title", "FlairTV Control") == 0,
          "a package that declares one key does not invent the others");

    /* --- Rejections keep the built-in layout ------------------------------ */
    tv_launcher_default(&sentinel);
    sentinel = tv;

    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_SYSTEM);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0 && !report.loaded,
          "a package without the resources-only flag is refused");
    check(report.reason != 0 && report.reason[0] != 0, "refusal carries a reason");
    check(strcmp(tv.title, sentinel.title) == 0, "refused package leaves the layout untouched");

    len = build_pkg(pkg, PKG_CAP, res + 1, 1, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
          "a package without launcher.json is refused");

    res[0].data = "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":[}";
    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
          "a malformed launcher.json is refused");

    res[0].data = EMPTY_TILES;
    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
          "a launcher with no tiles is refused");

    res[0].data = TOO_MANY_TILES;
    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
          "a launcher with more tiles than fit is refused");

    res[0].data = LONG_LABEL;
    len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
    check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
          "an over-long tile label is refused");

    /* --- Bad strings take the whole package down, text and layout ---------- */
    res[0].data = launcher_json;
    tv_launcher_default(&sentinel);
    tv_launcher_default(&tv);
    tv.title[0] = 'X';
    tv_strings_default(&tv.strings);
    {
        struct tv_launcher keep = tv;
        res[1].data = LONG_STRING;
        len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
        check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
              "an over-long session string is refused");
        res[1].data = "{\"focus\":}";
        len = build_pkg(pkg, PKG_CAP, res, 2, AIRP_FLAG_RESOURCES_ONLY);
        check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
              "a malformed strings.json is refused");
        len = build_pkg(pkg, PKG_CAP, res, 1, AIRP_FLAG_RESOURCES_ONLY);
        check(tv_launcher_load_package(pkg, len, &tv, &report) == 0,
              "a package without strings.json is refused");
        check(strcmp(tv.title, keep.title) == 0 &&
                  strcmp(tv.tiles[0].label, keep.tiles[0].label) == 0 &&
                  tv.strings.json == keep.strings.json &&
                  tv.strings.len == keep.strings.len,
              "a rejected package leaves both layout and text untouched");
    }
    check(str_has(&sentinel.strings, "app") &&
              str_has(&sentinel.strings, "home") &&
              str_has(&sentinel.strings, "sections") &&
              str_has(&sentinel.strings, "focus") &&
              str_has(&sentinel.strings, "status_card") &&
              str_has(&sentinel.strings, "focused") &&
              str_has(&sentinel.strings, "open"),
          "the built-in layout declares every label the shell draws");
    {
        char buf[TV_STRING_TEXT_MAX];
        check(tv_strings_copy(0, "app", buf, sizeof buf) == 0,
              "a missing layout has no strings");
    }

    check(tv_launcher_load_package(0, 0, &tv, &report) == 0 && !report.loaded,
          "an unstaged session keeps the built-in layout");

    tv_launcher_default(&tv);
    check(tv.tile_count == 3 && tv.tiles[0].target[0] != 0 &&
              tv.tiles[2].target[0] != 0,
          "built-in layout has launchable tiles");

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
