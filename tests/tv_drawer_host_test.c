#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airgraph.h"
#include "airjson.h"
#include "airp.h"
#include "../ui/systemui/airstin_ui.h"
#include "../kernel/ui/tv_layout.h"
#include "../kernel/ui/shell_launcher.h"
#include "../net/service/net_service.h"
#include "../net/bt/bt_core.h"
#include "../net/airp/airp_net.h"

/* The shared SystemUI drawer is authored at 720p pixel sizes and scales through
   the same ten-foot layout scale the TV shell uses. These tests drive the real
   renderer and the real hit test:
     - geometry matches the legacy 720p rect whenever no session sets a scale,
     - geometry follows 1080p/1440p/4K, the sub-720p floor and the 5K clamp,
     - rendered pixels occupy exactly that rect at every size,
     - clicks land on what was drawn at the scaled positions and nowhere else. */

static int failures;

static void check(int cond, const char *what) {
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) failures++;
}

/* --- Session service stubs: the drawer reads them, these tests do not. ---- */
static struct net_service_state g_net;
static struct bt_adapter_status g_bt;
static struct airp_session g_airp;
static int g_wifi_toggles;

void net_service_init(void) { memset(&g_net, 0, sizeof g_net); }
void net_service_sync(void) {}
void net_service_toggle_wifi(void) { g_wifi_toggles++; }
void net_service_toggle_cell(void) {}
void net_service_toggle_airplane(void) {}
const struct net_service_state *net_service_state(void) { return &g_net; }

void bt_core_init(void) { memset(&g_bt, 0, sizeof g_bt); }
int bt_core_power(int on) { g_bt.powered = on ? 1 : 0; return 0; }
int bt_core_toggle_power(void) { g_bt.powered = !g_bt.powered; return 0; }
int bt_core_scan_start(void) { return 0; }
int bt_core_connect(int idx) { (void)idx; return 0; }
int bt_core_disconnect(int idx) { (void)idx; return 0; }
void bt_core_recover(void) {}
void bt_core_tick(void) {}
const struct bt_adapter_status *bt_core_status(void) { return &g_bt; }
int bt_core_device_count(void) { return 0; }
const struct bt_device *bt_core_device_get(int idx) { (void)idx; return 0; }

int airp_discover_start(void) { return 0; }
int airp_peer_count(void) { return 0; }
int airp_pair_peer(int peer_idx, unsigned int pin) { (void)peer_idx; (void)pin; return -1; }
int airp_start_stream(enum airp_payload_type type, const char *label, unsigned int bytes) {
    (void)type; (void)label; (void)bytes;
    return 0;
}
void airp_disconnect(void) {}
const struct airp_session *airp_session_status(void) { return &g_airp; }
const char *airp_state_name(enum airp_session_state st) { (void)st; return "Idle"; }

/* --- Surface helpers ------------------------------------------------------ */
#define SENTINEL 0x00112233u

static struct ag_surface make_surface(int32_t w, int32_t h, uint32_t **storage) {
    struct ag_surface s;
    uint32_t *pixels = calloc((size_t)w * (size_t)h, sizeof(uint32_t));
    *storage = pixels;
    memset(&s, 0, sizeof s);
    if (!pixels) return s;
    ag_surface_init(&s, pixels, w, h, (uint32_t)w * 4u, AG_PIXEL_XRGB8888);
    ag_clear(&s, SENTINEL);
    return s;
}

static int changed_bbox(const struct ag_surface *s, int32_t *x0, int32_t *y0,
                        int32_t *x1, int32_t *y1, int *changed) {
    int32_t minx = s->width, miny = s->height, maxx = -1, maxy = -1;
    int count = 0;
    for (int32_t y = 0; y < s->height; y++) {
        const uint32_t *row = (const uint32_t *)((const unsigned char *)s->pixels + (uint32_t)y * s->pitch);
        for (int32_t x = 0; x < s->width; x++) {
            if ((row[x] & 0x00FFFFFFu) != SENTINEL) {
                count++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    *x0 = minx; *y0 = miny; *x1 = maxx; *y1 = maxy;
    *changed = count;
    return count > 0;
}

static int near(int32_t a, int32_t b) { return a >= b - 2 && a <= b + 2; }

static uint32_t pixel_checksum(const struct ag_surface *s) {
    uint32_t h = 2166136261u;
    for (int32_t y = 0; y < s->height; y++) {
        const uint32_t *row = (const uint32_t *)((const unsigned char *)s->pixels +
                                                (uint32_t)y * s->pitch);
        for (int32_t x = 0; x < s->width; x++) {
            h ^= row[x];
            h *= 16777619u;
        }
    }
    return h;
}

/* --- TV session package, built the way tools/airpkg.py writes it ---------- */
#define PKG_CAP 16384

static void put_le32(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
}

static void put_le16(unsigned char *p, unsigned int v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
}

/* A resources-only component package carrying a one-tile launcher and the given
   strings.json. Returns the package length. */
static unsigned int build_tv_pkg(unsigned char *buf, unsigned int cap,
                                 const char *launcher, const char *strings) {
    const char *names[2] = { "launcher.json", "strings.json" };
    const char *data[2] = { launcher, strings };
    unsigned short types[2] = { AIRP_RT_UI, AIRP_RT_STRINGS };
    unsigned int offsets[2][2];
    unsigned int i, manifest_off, manifest_len, tab_off, tab_len, name_off, file_size;
    const char *manifest = "{\"name\":\"FlairTV\",\"version\":\"1.0.0\"}";

    manifest_len = (unsigned int)strlen(manifest);
    manifest_off = AIRP_HEADER_V2_SIZE;
    tab_off = manifest_off + manifest_len;
    tab_len = 2u * AIRP_RESOURCE_SIZE;
    name_off = tab_off + tab_len;

    memset(buf, 0, cap);
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

/* --- Label source under test ---------------------------------------------- */
static struct shell_launcher g_tv;

static const char *pkg_label(const char *key) {
    static char buf[SHELL_STRING_TEXT_MAX];
    return shell_strings_copy(&g_tv.strings, key, buf, sizeof buf) ? buf : 0;
}

/* Supplies exactly the built-in title and nothing else, so every other label
   falls through to its own built-in wording: the pixels must not move. */
static const char *literal_label(const char *key) {
    if (key && strcmp(key, "cc.title") == 0) return "AirStinUI Control";
    return 0;
}

static uint32_t render_checksum(int32_t w, int32_t h) {
    struct ag_surface s;
    uint32_t *pixels = 0;
    uint32_t sum;

    s = make_surface(w, h, &pixels);
    if (!pixels) return 0;
    airstin_render_control_center(&s, w, 1, tv_layout_px(24, tv_layout_scale_q16(w, h)), 0, 0);
    sum = pixel_checksum(&s);
    free(pixels);
    return sum;
}

/* Sets the session scale the way the TV shell does and checks the geometry. */
static void check_geometry(int32_t w, int32_t h, int32_t ex, int32_t ey, int32_t ew,
                           int32_t eh, int32_t erow, int32_t etitle, int32_t elabel,
                           const char *what) {
    struct airstin_cc_geometry g;
    airstin_ui_set_scale_q16(tv_layout_scale_q16(w, h));
    memset(&g, 0, sizeof g);
    airstin_cc_geometry_for(w, &g);
    printf("     %s: scale=%d x=%d y=%d w=%d h=%d row=%d glyphs=%d/%d\n", what,
           (int)g.scale_q16, (int)g.x, (int)g.y, (int)g.w, (int)g.h, (int)g.row_h,
           (int)g.title_glyph, (int)g.label_glyph);
    check(g.x == ex && g.y == ey && g.w == ew && g.h == eh, what);
    check(g.row_h == erow && g.title_glyph == etitle && g.label_glyph == elabel,
          "drawer rows and glyph sizes follow the same scale");
}

static void check_render(int32_t w, int32_t h, const char *what) {
    struct airstin_cc_geometry g;
    struct ag_surface s;
    uint32_t *pixels = 0;
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int changed = 0;

    airstin_ui_set_scale_q16(tv_layout_scale_q16(w, h));
    airstin_cc_geometry_for(w, &g);
    s = make_surface(w, h, &pixels);
    if (!pixels) { check(0, "surface allocation"); return; }
    /* cc_anim at its resting value keeps the panel at its resting position. */
    airstin_render_control_center(&s, w, 1, tv_layout_px(24, g.scale_q16), 0, 0);
    check(changed_bbox(&s, &x0, &y0, &x1, &y1, &changed), what);
    printf("     rendered bbox=(%d,%d)-(%d,%d) expected x=%d y=%d w=%d h=%d\n",
           (int)x0, (int)y0, (int)x1, (int)y1, (int)g.x, (int)g.y, (int)g.w, (int)g.h);
    /* The panel's own drop shadow overhangs its rect by a fixed 3/6 px, so the
       ink must start exactly at the rect and end just past it, never further. */
    check(near(x0, g.x) && near(y0, g.y) &&
              x1 >= g.x + g.w - 1 && x1 <= g.x + g.w + 7 &&
              y1 >= g.y + g.h - 1 && y1 <= g.y + g.h + 7,
          "drawer pixels occupy exactly the scaled panel rect");
    free(pixels);
}

int main(void) {
    struct airstin_cc_geometry g;
    struct ag_surface s;
    uint32_t *pixels = 0;
    int32_t x0, y0, x1, y1;
    int changed;

    memset(&g_airp, 0, sizeof g_airp);
    bt_core_init();
    net_service_init();

    /* --- A session that never sets a scale keeps the 720p drawer ----------- */
    check(airstin_ui_scale_q16() == AIRSTIN_UNIT_Q16,
          "drawer scale defaults to 1.0 for desktop and mobile");
    airstin_cc_geometry_for(1280, &g);
    check(g.x == 988 && g.y == 40 && g.w == 280 && g.h == 336 && g.row_h == 38,
          "default drawer geometry is the legacy 720p rect");
    check(g.title_glyph == 2 && g.label_glyph == 1,
          "default drawer glyph sizes are unchanged");
    airstin_ui_set_scale_q16(-1);
    check(airstin_ui_scale_q16() == AIRSTIN_UNIT_Q16,
          "a nonsensical scale request is refused rather than shrinking the UI");

    /* A TV session at 720p draws the identical drawer. */
    check_geometry(1280, 720, 988, 40, 280, 336, 38, 2, 1, "720p keeps the drawer as drawn today");

    /* --- Higher panels open the drawer through the ten-foot scale ---------- */
    check_geometry(1920, 1080, 1482, 60, 420, 504, 57, 3, 2, "1080p drawer scales 1.5x");
    check_geometry(2560, 1440, 1976, 80, 560, 672, 76, 4, 2, "1440p drawer scales 2x");
    check_geometry(3840, 2160, 2964, 120, 840, 1008, 114, 6, 3, "4K drawer scales 3x");

    /* Edges: below 720p it holds at 1x; beyond 4K it clamps at 3x. */
    check_geometry(800, 600, 508, 40, 280, 336, 38, 2, 1, "sub-720p surface holds the 1x drawer");
    check_geometry(5120, 2880, 4244, 120, 840, 1008, 114, 6, 3, "5K surface clamps the drawer at 3x");

    /* --- Pixels: the drawer really renders at the scaled rect -------------- */
    check_render(1280, 720, "720p drawer renders");
    check_render(1920, 1080, "1080p drawer renders");
    check_render(3840, 2160, "4K drawer renders");

    /* Status cluster: right-anchored, so its left edge must move with scale. */
    {
        int32_t w = 3840;
        airstin_ui_set_scale_q16(tv_layout_scale_q16(w, 2160));
        s = make_surface(w, 2160, &pixels);
        if (pixels) {
            airstin_render_status_cluster(&s, w, "aarch64", 0);
            changed = changed_bbox(&s, &x0, &y0, &x1, &y1, &changed);
            printf("     status cluster bbox=(%d,%d)-(%d,%d)\n", (int)x0, (int)y0, (int)x1, (int)y1);
            check(changed && near(x0, w - 3 * 370) && x1 <= w &&
                      near(y0, 15) && y1 < 3 * 40,
                  "status cluster scales and stays inside the 4K top bar");
            free(pixels);
        }
    }

    /* --- Hit testing follows the same geometry ----------------------------- */
    /* 4K: a click where the drawer's Wi-Fi row is drawn toggles it. */
    airstin_ui_set_scale_q16(tv_layout_scale_q16(3840, 2160));
    airstin_cc_geometry_for(3840, &g);
    g_wifi_toggles = 0;
    check(airstin_cc_handle_click(g.x + 3 * 30, g.y + 3 * 60, 3840, 0, 0, 0, 0) == 1 &&
              g_wifi_toggles == 1,
          "a click on the 4K drawer's Wi-Fi row reaches the toggle");
    /* The unscaled 720p coordinates are far outside the 4K drawer. */
    g_wifi_toggles = 0;
    check(airstin_cc_handle_click(1020, 101, 3840, 0, 0, 0, 0) == 0 && g_wifi_toggles == 0,
          "720p coordinates do not hit the 4K drawer");

    /* 720p default scale: the legacy coordinates still hit. */
    airstin_ui_set_scale_q16(0);
    airstin_cc_geometry_for(1280, &g);
    g_wifi_toggles = 0;
    check(airstin_cc_handle_click(g.x + 30, g.y + 60, 1280, 0, 0, 0, 0) == 1 &&
              g_wifi_toggles == 1,
          "a click on the 720p drawer's Wi-Fi row still reaches the toggle");

    /* --- Session wording: TV labels come from the package ------------------ */
    {
        static char launcher_json[4096];
        static char strings_json[8192];
        static unsigned char pkg[PKG_CAP];
        static unsigned char bad_pkg[PKG_CAP];
        struct shell_launcher_report report;
        struct airjson_doc doc;
        const char *value = 0;
        unsigned int value_len = 0;
        char packaged_title[SHELL_STRING_TEXT_MAX];
        const char *launcher_min =
            "{\"tv\":{\"title\":\"FlairTV\",\"tiles\":[{\"id\":\"a\",\"label\":\"A\",\"target\":\"app:0\"}]}}";
        unsigned int len;
        uint32_t desktop_1080, packaged_1080, literal_1080, bad_1080;
        uint32_t desktop_4k, packaged_4k, bad_4k;

        check(read_file("tv/resources/strings.json", strings_json, sizeof strings_json) > 0,
              "shipped strings.json is readable");
        check(read_file("tv/resources/launcher.json", launcher_json, sizeof launcher_json) > 0,
              "shipped launcher.json is readable");

        /* The value the test expects is read from the file, not hardcoded. */
        check(airjson_open(&doc, strings_json, (unsigned int)strlen(strings_json)) == AIRJSON_OK &&
                  airjson_root_get(&doc, "cc.title", &value, &value_len) == AIRJSON_OK &&
                  airjson_get_string(value, value_len, packaged_title,
                                     sizeof packaged_title) == AIRJSON_OK,
              "the packaged strings.json declares the drawer title");
        check(strcmp(packaged_title, "AirStinUI Control") != 0,
              "the packaged drawer title differs from the built-in wording");

        len = build_tv_pkg(pkg, PKG_CAP, launcher_json, strings_json);
        check(shell_launcher_load_package(pkg, len, SHELL_KIND_TV, &g_tv, &report) == 1 && report.loaded,
              "the shipped package resolves for the drawer");
        check(strcmp(pkg_label("cc.title"), packaged_title) == 0,
              "the drawer label resolver returns the packaged title");
        check(strcmp(pkg_label("cc.search"), "Find") == 0,
              "a second packaged label resolves too");
        check(pkg_label("cc.not_declared") == 0,
              "a key the package does not declare resolves to nothing");

        /* Baseline: no provider at all, i.e. desktop and mobile. */
        airstin_ui_set_labels(0);
        airstin_ui_set_scale_q16(tv_layout_scale_q16(1280, 720));
        check(render_checksum(1280, 720) == 0x7b730680u,
              "720p desktop drawer is pixel-identical to before the label source");
        airstin_ui_set_scale_q16(tv_layout_scale_q16(1920, 1080));
        desktop_1080 = render_checksum(1920, 1080);
        airstin_ui_set_scale_q16(tv_layout_scale_q16(3840, 2160));
        desktop_4k = render_checksum(3840, 2160);

        /* Same geometry and scale, but the wording now comes from the package. */
        airstin_ui_set_labels(pkg_label);
        airstin_ui_set_scale_q16(tv_layout_scale_q16(1920, 1080));
        packaged_1080 = render_checksum(1920, 1080);
        airstin_ui_set_scale_q16(tv_layout_scale_q16(3840, 2160));
        packaged_4k = render_checksum(3840, 2160);
        printf("     1080p checksums: desktop=%08x packaged=%08x\n",
               (unsigned)desktop_1080, (unsigned)packaged_1080);
        printf("     4K checksums: desktop=%08x packaged=%08x\n",
               (unsigned)desktop_4k, (unsigned)packaged_4k);
        check(packaged_1080 != desktop_1080,
              "a TV session at 1080p paints packaged drawer text");
        check(packaged_4k != desktop_4k, "a TV session at 4K paints packaged drawer text");

        /* The plumbing itself changes nothing: same words in, same pixels out. */
        airstin_ui_set_labels(literal_label);
        airstin_ui_set_scale_q16(tv_layout_scale_q16(1920, 1080));
        literal_1080 = render_checksum(1920, 1080);
        check(literal_1080 == desktop_1080,
              "resolving the built-in wording yields the unchanged drawer");

        /* Geometry is untouched by any of this. */
        airstin_ui_set_scale_q16(tv_layout_scale_q16(3840, 2160));
        airstin_cc_geometry_for(3840, &g);
        check(g.x == 2964 && g.y == 120 && g.w == 840 && g.h == 1008,
              "packaged wording does not move the drawer");

        /* --- A rejected package leaves labels and geometry untouched -------- */
        {
            struct shell_launcher before = g_tv;
            uint32_t before_checksum = packaged_4k;
            /* A separate buffer: the resolved layout views the accepted
               package's bytes, exactly as the image's rodata would. */
            len = build_tv_pkg(bad_pkg, PKG_CAP, launcher_min, "{\"cc.title\":}");
            check(shell_launcher_load_package(bad_pkg, len, SHELL_KIND_TV, &g_tv, &report) == 0,
                  "a malformed strings.json rejects the package");
            check(g_tv.strings.json == before.strings.json &&
                      g_tv.strings.len == before.strings.len &&
                      g_tv.title[0] == before.title[0],
                  "the rejected package leaves the resolved layout alone");
            airstin_ui_set_labels(pkg_label);
            airstin_ui_set_scale_q16(tv_layout_scale_q16(3840, 2160));
            bad_4k = render_checksum(3840, 2160);
            check(bad_4k == before_checksum,
                  "a rejected package keeps painting the previous drawer text");
            airstin_ui_set_scale_q16(tv_layout_scale_q16(1920, 1080));
            bad_1080 = render_checksum(1920, 1080);
            check(bad_1080 == packaged_1080,
                  "and the rejected package does not shift the 1080p drawer");
        }

        /* Back to the desktop/mobile configuration for any later test. */
        airstin_ui_set_labels(0);
        airstin_ui_set_scale_q16(0);
    }

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
