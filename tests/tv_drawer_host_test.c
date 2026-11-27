#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airgraph.h"
#include "../ui/systemui/airstin_ui.h"
#include "../kernel/ui/tv_layout.h"
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

    printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
