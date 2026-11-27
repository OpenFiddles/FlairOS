#include <stdio.h>
#include <string.h>

#include "airjson.h"
#include "airgraph.h"
#include "airp.h"
#include "ui/mobile_layout.h"
#include "ui/shell_launcher.h"

/* The phone session is a package too: FlairMobile.airp carries the home-screen
   tiles, dock and every label. These tests prove the layout scale from the
   480x800 portrait and 800x480 landscape canvases, and that the home screen
   resolves from the shipped package bytes while a rejected package leaves the
   built-in one untouched. */

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

static int str_is(const struct shell_strings *s, const char *key, const char *want) {
    char buf[SHELL_STRING_TEXT_MAX];
    return shell_strings_copy(s, key, buf, sizeof buf) && strcmp(buf, want) == 0;
}

static void put_le32(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void put_le16(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

#define PKG_CAP 16384

/* A resources-only component package with launcher.json + strings.json. */
static unsigned int build_pkg(unsigned char *buf, const char *launcher, const char *strings) {
    const char *names[2] = { "launcher.json", "strings.json" };
    const char *data[2] = { launcher, strings };
    unsigned short types[2] = { AIRP_RT_UI, AIRP_RT_STRINGS };
    unsigned int offsets[2][2];
    const char *manifest = "{\"name\":\"FlairMobile\",\"version\":\"1.0.0\"}";
    unsigned int i, manifest_off, manifest_len, tab_off, tab_len, name_off, file_size;

    manifest_len = (unsigned int)strlen(manifest);
    manifest_off = AIRP_HEADER_V2_SIZE;
    tab_off = manifest_off + manifest_len;
    tab_len = 2u * AIRP_RESOURCE_SIZE;
    name_off = tab_off + tab_len;

    memset(buf, 0, PKG_CAP);
    memcpy(buf + manifest_off, manifest, manifest_len);
    {
        unsigned int cursor = name_off;
        for (i = 0; i < 2; i++) {
            offsets[i][0] = cursor;
            memcpy(buf + cursor, names[i], strlen(names[i]) + 1);
            cursor += (unsigned int)strlen(names[i]) + 1;
        }
        for (i = 0; i < 2; i++) {
            offsets[i][1] = cursor;
            memcpy(buf + cursor, data[i], strlen(data[i]));
            cursor += (unsigned int)strlen(data[i]);
        }
        file_size = cursor;
    }
    for (i = 0; i < 2; i++) {
        unsigned char *e = buf + tab_off + i * AIRP_RESOURCE_SIZE;
        unsigned int len = (unsigned int)strlen(data[i]);
        put_le32(e + 0, offsets[i][0]);
        put_le32(e + 4, offsets[i][1]);
        put_le32(e + 8, len);
        put_le32(e + 12, airp_crc32((const unsigned char *)data[i], len));
        put_le16(e + 16, types[i]);
        put_le16(e + 18, AIRP_RF_REQUIRED);
        put_le32(e + 20, 0);
    }
    put_le32(buf + 0, AIRP_MAGIC);
    put_le16(buf + 4, AIRP_VERSION_V2);
    put_le16(buf + 6, AIRP_HEADER_V2_SIZE);
    put_le32(buf + 8, AIRP_API_LEVEL);
    put_le32(buf + 24, AIRP_FLAG_SYSTEM | AIRP_FLAG_RESOURCES_ONLY);
    put_le32(buf + 28, manifest_off);
    put_le32(buf + 32, manifest_len);
    put_le32(buf + 36, 2);
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

static unsigned int read_file(const char *path, char *buf, unsigned int cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (unsigned int)n;
}

static const char *MIN_LAUNCHER =
    "{\"mobile\":{\"title\":\"FlairOS Mobile\",\"tiles\":["
    "{\"id\":\"browser\",\"label\":\"Browser\",\"target\":\"screen:browser\"},"
    "{\"id\":\"files\",\"label\":\"Files\",\"target\":\"screen:files\"}],"
    "\"dock\":[\"browser\"]}}";

static const char *UNKNOWN_DOCK =
    "{\"mobile\":{\"title\":\"M\",\"tiles\":["
    "{\"id\":\"browser\",\"label\":\"Browser\",\"target\":\"screen:browser\"}],"
    "\"dock\":[\"nope\"]}}";

static const char *BAD_ICON =
    "{\"mobile\":{\"title\":\"M\",\"tiles\":["
    "{\"id\":\"browser\",\"label\":\"Browser\",\"target\":\"screen:browser\",\"icon\":-4}]}}";

static const char *TOO_MANY_DOCK =
    "{\"mobile\":{\"title\":\"M\",\"tiles\":["
    "{\"id\":\"a\",\"label\":\"A\",\"target\":\"screen:calc\"}],"
    "\"dock\":[\"a\",\"a\",\"a\",\"a\",\"a\",\"a\",\"a\",\"a\",\"a\"]}}";

static const char *TV_ROOT =
    "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":["
    "{\"id\":\"a\",\"label\":\"A\",\"target\":\"app:0\"}]}}";

int main(void) {
    static char launcher_json[8192];
    static char strings_json[8192];
    static unsigned char pkg[PKG_CAP];
    static unsigned char bad_pkg[PKG_CAP];
    static const char *const simple_strings = "{\"app\":\"FlairOS\"}";
    struct shell_launcher mob, keep;
    struct shell_launcher_report report;
    unsigned int len;

    check(read_file("mobile/resources/launcher.json", launcher_json, sizeof launcher_json) > 0,
          "shipped mobile/resources/launcher.json is readable");
    check(read_file("mobile/resources/strings.json", strings_json, sizeof strings_json) > 0,
          "shipped mobile/resources/strings.json is readable");

    /* --- Layout scale: one 480x800 design, correct on any phone ------------ */
    check(mobile_layout_scale_q16(480, 800) == 65536, "the design size keeps 1x");
    check(mobile_layout_scale_q16(720, 1280) == 98304, "720x1280 scales 1.5x");
    check(mobile_layout_scale_q16(1080, 1920) == 147456, "1080x1920 scales 2.25x");
    check(mobile_layout_scale_q16(1600, 2560) == 196608, "tall phones clamp at 3x");
    check(mobile_layout_scale_q16(1440, 2560) ==
          (int32_t)ag_scale_ratio_q16(1440, 2560, MOBILE_LAYOUT_BASE_W, MOBILE_LAYOUT_BASE_H),
          "phone scale is the shared AirGraph ratio with the 3x session clamp");
    check(mobile_layout_px(68, 147456) == ag_px(68, 147456),
          "phone design pixels use AirGraph snapping");
    /* --- Landscape: the 800x480 canvas, same AirGraph ratio --------------- */
    check(mobile_layout_landscape(800, 480) == 1, "a wide panel uses the landscape canvas");
    check(mobile_layout_landscape(480, 800) == 0, "a tall panel uses the portrait canvas");
    check(mobile_layout_scale_q16(800, 480) == 65536, "the landscape design keeps 1x");
    check(mobile_layout_scale_q16(1280, 720) == 98304, "1280x720 landscape scales 1.5x");
    check(mobile_layout_scale_q16(1920, 1080) == 147456, "1920x1080 landscape scales 2.25x");
    check(mobile_layout_scale_q16(1280, 720) ==
              (int32_t)ag_scale_ratio_q16(1280, 720, MOBILE_LAYOUT_LANDSCAPE_W,
                                          MOBILE_LAYOUT_LANDSCAPE_H),
          "landscape scale is the shared AirGraph ratio for the 800x480 canvas");
    check(mobile_layout_scale_q16(320, 480) == 65536, "sub-design surfaces hold 1x");
    check(mobile_layout_scale_q16(0, 0) == 65536, "a degenerate surface holds 1x");
    check(mobile_layout_px(68, 98304) == 102, "design pixels scale");
    check(mobile_layout_px(480, 196608) == 1440, "the design width scales to the panel");
    check(mobile_layout_text(2, 147456) == 5, "text scales and never disappears");
    check(mobile_layout_text(1, 4096) == 1, "text has a one-pixel floor");

    /* --- Built-in home screen --------------------------------------------- */
    shell_launcher_default(&mob, SHELL_KIND_MOBILE);
    check(mob.tile_count == 6 && mob.dock_count == 4,
          "the built-in phone home screen has six tiles and a four-slot dock");
    check(strcmp(mob.tiles[0].target, "screen:browser") == 0 &&
              strcmp(mob.tiles[5].target, "screen:settings") == 0,
          "built-in tiles target the shell's own screens");
    check(strcmp(mob.dock_ids[0], "browser") == 0 &&
              strcmp(mob.dock_ids[3], "settings") == 0,
          "the built-in dock names declared tiles");
    {
        struct shell_launcher tv;
        shell_launcher_default(&tv, SHELL_KIND_TV);
        check(tv.tile_count == 3 && tv.dock_count == 0,
              "the TV session keeps its own default home screen");
        check(strcmp(shell_launcher_root(SHELL_KIND_MOBILE), "mobile") == 0 &&
                  strcmp(shell_launcher_root(SHELL_KIND_TV), "tv") == 0,
              "each session reads its layout from its own root object");
    }
    {
        char buf[SHELL_STRING_TEXT_MAX];
        check(shell_strings_copy(&mob.strings, "app", buf, sizeof buf) == 0,
              "a phone session does not inherit the ten-foot wording");
    }

    /* --- The shipped package drives the phone home screen ----------------- */
    len = build_pkg(pkg, launcher_json, strings_json);
    check(shell_launcher_load_package(pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 1 &&
              report.loaded,
          "the shipped FlairMobile package loads");
    check(mob.tile_count == 6 && report.tiles == 6, "the package supplies six tiles");
    check(mob.dock_count == 4 && report.dock == 4, "the package supplies a four-slot dock");
    check(strcmp(mob.tiles[0].label, "Browser") == 0 &&
              strcmp(mob.tiles[2].target, "screen:calc") == 0,
          "tile labels and targets come from the package");
    check(strcmp(mob.tiles[0].glyph, "BR") == 0, "tile glyphs come from the package");
    check(mob.tiles[5].icon == 7, "tile icon indices come from the package");
    check(mob.tiles[0].accent == 0x003B82F6u, "tile accents come from the package");
    check(strcmp(mob.dock_ids[0], "browser") == 0 &&
              strcmp(mob.dock_ids[2], "activity") == 0,
          "the dock order comes from the package");
    check(report.strings == 29, "every session string is carried in the package");
    check(str_is(&mob.strings, "settings_title", "Mobile Settings"),
          "a phone label is read from the packaged strings.json");
    check(str_is(&mob.strings, "theme_pink", "Pink"), "the theme names are packaged");

    /* --- Rejections leave the previous home screen alone ------------------ */
    keep = mob;
    len = build_pkg(bad_pkg, UNKNOWN_DOCK, simple_strings);
    check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "a dock that names an unknown tile is refused");
    check(mob.tile_count == keep.tile_count && mob.strings.json == keep.strings.json,
          "the refused dock leaves the loaded home screen untouched");

    len = build_pkg(bad_pkg, BAD_ICON, simple_strings);
    check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "a tile with an invalid icon index is refused");

    len = build_pkg(bad_pkg, TOO_MANY_DOCK, simple_strings);
    check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "a dock longer than the shell can show is refused");

    len = build_pkg(bad_pkg, TV_ROOT, simple_strings);
    check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "a TV layout is not accepted as a phone layout");
    check(strcmp(report.reason, "session launcher.json has no root object") == 0,
          "the refusal explains which object was missing");

    len = build_pkg(bad_pkg, MIN_LAUNCHER, "{\"app\":}");
    check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "a malformed strings.json rejects the package");
    check(mob.strings.json == keep.strings.json && mob.tile_count == keep.tile_count,
          "a rejected package leaves the previous layout and text untouched");

    shell_launcher_default(&mob, SHELL_KIND_MOBILE);
    check(shell_launcher_load_package(0, 0, SHELL_KIND_MOBILE, &mob, &report) == 0,
          "an unstaged phone session keeps its built-in home screen");
    check(mob.tile_count == 6 && mob.dock_count == 4,
          "and that fallback is the full built-in layout");

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
