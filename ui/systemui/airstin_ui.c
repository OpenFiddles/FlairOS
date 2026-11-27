/* SPDX-License-Identifier: AGPL-3.0-only */
/* AirStinUI.airp: SystemUI (Status Bar, Control Center + '>' Drill-Downs, Installer, Power)
 *
 * The whole surface is authored in the pixel sizes of a 1280x720 session and
 * drawn through a pen that scales design units to the surface. The scale is 1.0
 * unless a session sets it, so desktop and mobile keep their exact pixels while
 * a TV session opens the drawer at its ten-foot layout scale.
 *
 * Renderer and hit test both read `airstin_cc_geometry_for`, so a click lands on
 * what was drawn at every scale.
 */
#include "airstin_ui.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../net/service/net_service.h"
#include "../../net/airp/airp_net.h"
#include "../../net/bt/bt_core.h"
#include "../../include/hal.h"

/* Design sizes of the drawer, in 1280x720 pixels. */
#define CC_DESIGN_W 280
#define CC_DESIGN_H 336
#define CC_DESIGN_MARGIN 12
#define CC_DESIGN_TOP 40
#define CC_DESIGN_ROW_H 38
#define CC_DESIGN_DROP 24 /* entrance travel */

static struct airstin_installer g_inst;
static struct airstin_power_state g_pwr = {0, 0, 0, 0, 0, 0};
static int g_ui_inited = 0;
static enum cc_subview g_cc_view = CC_VIEW_ROOT;
static int32_t g_cc_slide = 0;
static int32_t g_ui_scale_q16 = AIRSTIN_UNIT_Q16;

int32_t airstin_ui_scale_q16(void) { return g_ui_scale_q16; }

void airstin_ui_set_scale_q16(int32_t scale_q16) {
    if (scale_q16 < AIRSTIN_UNIT_Q16) scale_q16 = AIRSTIN_UNIT_Q16;
    if (scale_q16 > AIRSTIN_SCALE_MAX_Q16) scale_q16 = AIRSTIN_SCALE_MAX_Q16;
    g_ui_scale_q16 = scale_q16;
}

static airstin_label_fn g_labels;

void airstin_ui_set_labels(airstin_label_fn fn) { g_labels = fn; }

/* Resolved session text, copied into a small ring so several labels can be in
   flight in one draw call (a row's title and subtitle, for instance) while the
   resolver itself only owns one buffer. */
const char *airstin_ui_label(const char *key, const char *fallback) {
    static char ring[4][64];
    static unsigned next;
    const char *text = (g_labels && key) ? g_labels(key) : 0;
    char *slot;
    int i = 0;

    if (!text || !text[0]) return fallback;
    slot = ring[next];
    next = (next + 1u) % 4u;
    while (text[i] && i < 63) { slot[i] = text[i]; i++; }
    slot[i] = 0;
    return slot;
}

/* Design units -> surface pixels. At 1.0 this is the identity, so every path
   below is bit-for-bit the old fixed-pixel drawing. */
static int32_t ui_len(int32_t base, int32_t q16) {
    if (q16 == AIRSTIN_UNIT_Q16) return base;
    return (int32_t)(((int64_t)base * q16 + (AIRSTIN_UNIT_Q16 / 2)) >> 16);
}

static int32_t ui_glyph(int32_t base, int32_t q16) {
    int32_t v = ui_len(base, q16);
    return v < 1 ? 1 : v;
}

/* A draw origin: design coordinates are relative to (ox, oy) and scaled by q16. */
struct ui_pen {
    struct ag_surface *s;
    int32_t ox, oy, q16;
};

static struct ui_pen ui_pen_at(struct ag_surface *s, int32_t ox, int32_t oy, int32_t q16) {
    struct ui_pen p;
    p.s = s;
    p.ox = ox;
    p.oy = oy;
    p.q16 = q16;
    return p;
}

static int32_t px_of(const struct ui_pen *p, int32_t dx) { return p->ox + ui_len(dx, p->q16); }
static int32_t py_of(const struct ui_pen *p, int32_t dy) { return p->oy + ui_len(dy, p->q16); }
static int32_t w_of(const struct ui_pen *p, int32_t dw) { return ui_len(dw, p->q16); }

static void ui_round(struct ui_pen *p, int32_t x, int32_t y, int32_t w, int32_t h,
                     int32_t r, uint32_t rgb, uint8_t a) {
    gfx_fill_round_rect(p->s, px_of(p, x), py_of(p, y), w_of(p, w), w_of(p, h),
                        w_of(p, r), rgb, a);
}

static void ui_text(struct ui_pen *p, int32_t x, int32_t y, const char *text,
                    uint32_t rgb, int32_t size) {
    gfx_draw_text_scaled(p->s, px_of(p, x), py_of(p, y), text, rgb, ui_glyph(size, p->q16));
}

static void ui_text_aa(struct ui_pen *p, int32_t x, int32_t y, const char *text,
                       uint32_t rgb, int32_t size) {
    gfx_draw_text_aa(p->s, px_of(p, x), py_of(p, y), text, rgb, ui_glyph(size, p->q16));
}

static void ui_pill(struct ui_pen *p, int32_t x, int32_t y, const char *text,
                    uint32_t bg, uint32_t fg, int32_t size) {
    gfx_pill_badge(p->s, px_of(p, x), py_of(p, y), text, bg, fg, ui_glyph(size, p->q16));
}

/* --- Scaled glyphs and status icons ----------------------------------------
   These take surface pixels so a caller can anchor them to either edge; only
   their internal sizes scale. */

static void ui_chevron_right(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb) {
    int32_t half = size / 2;
    for (int32_t i = 0; i <= half; i++) {
        gfx_blend_pixel(s, x + i, y + i, rgb, 240u);
        gfx_blend_pixel(s, x + i + 1, y + i, rgb, 150u);
        gfx_blend_pixel(s, x + i, y + size - i, rgb, 240u);
        gfx_blend_pixel(s, x + i + 1, y + size - i, rgb, 150u);
    }
}

static void ui_chevron_left(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb) {
    int32_t half = size / 2;
    for (int32_t i = 0; i <= half; i++) {
        gfx_blend_pixel(s, x + half - i, y + i, rgb, 240u);
        gfx_blend_pixel(s, x + half - i - 1, y + i, rgb, 150u);
        gfx_blend_pixel(s, x + half - i, y + size - i, rgb, 240u);
        gfx_blend_pixel(s, x + half - i - 1, y + size - i, rgb, 150u);
    }
}

static void ui_cell_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, int active,
                         uint32_t rgb, int32_t q16) {
    for (int i = 0; i < 4; i++) {
        int32_t bh = ui_len(4 + i * 3, q16);
        int32_t bx = x + ui_len(i * 4, q16);
        int32_t by = y + ui_len(14, q16) - bh;
        uint32_t col = (active && i < bars) ? rgb : THEME_GLASS_BORDER;
        uint8_t a = (active && i < bars) ? 245u : 110u;
        gfx_fill_round_rect(s, bx, by, ui_glyph(3, q16), bh, ui_glyph(1, q16), col, a);
    }
}

static void ui_wifi_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, uint32_t rgb, int32_t q16) {
    for (int b = 0; b < 4; b++) {
        int32_t bh = ui_len(3 + b * 3, q16);
        int32_t bx = x + ui_len(b * 5, q16);
        int32_t by = y + ui_len(12, q16) - bh;
        uint32_t c = (b < bars) ? rgb : THEME_GLASS_BORDER;
        gfx_fill_round_rect(s, bx, by, ui_glyph(3, q16), bh, ui_glyph(1, q16), c, 240u);
    }
}

static void ui_eth_icon(struct ag_surface *s, int32_t x, int32_t y, uint32_t rgb, int32_t q16) {
    gfx_fill_round_rect(s, x, y + ui_len(2, q16), ui_len(16, q16), ui_len(11, q16),
                        ui_glyph(2, q16), rgb, 235u);
    gfx_fill_round_rect(s, x + ui_len(3, q16), y + ui_len(4, q16), ui_len(10, q16),
                        ui_len(6, q16), ui_glyph(1, q16), THEME_GLASS_DARK, 255u);
    gfx_fill_rect_alpha(s, x + ui_len(5, q16), y + ui_len(13, q16), ui_len(6, q16),
                        ui_glyph(2, q16), rgb, 240u);
}

static void ui_battery_icon(struct ag_surface *s, int32_t x, int32_t y, int pct, int ac,
                            uint32_t rgb, int32_t q16) {
    gfx_stroke_round_rect(s, x, y + ui_len(2, q16), ui_len(24, q16), ui_len(12, q16),
                          ui_glyph(3, q16), rgb, 230u);
    gfx_fill_rect_alpha(s, x + ui_len(24, q16), y + ui_len(5, q16), ui_glyph(2, q16),
                        ui_len(6, q16), rgb, 230u);
    {
        int32_t fw = (pct * ui_len(18, q16)) / 100;
        int32_t fmin = ui_glyph(2, q16);
        uint32_t fc = ac ? THEME_ACCENT_GREEN : (pct < 20 ? THEME_ACCENT_PINK : rgb);
        if (fw < fmin) fw = fmin;
        if (fw > ui_len(18, q16)) fw = ui_len(18, q16);
        gfx_fill_round_rect(s, x + ui_len(3, q16), y + ui_len(5, q16), fw, ui_len(6, q16),
                            ui_glyph(1, q16), fc, 245u);
    }
}

static void ui_bt_icon(struct ag_surface *s, int32_t x, int32_t y, int active, int connected,
                       uint32_t rgb, int32_t q16) {
    uint32_t col = active ? rgb : THEME_GLASS_BORDER;
    uint8_t a = active ? 245u : 120u;
    int32_t step = ui_glyph(1, q16);
    gfx_fill_rect_alpha(s, x + ui_len(5, q16), y + ui_len(1, q16), step, ui_len(13, q16), col, a);
    for (int i = 0; i < 4; i++) {
        int32_t i1 = ui_len(i + 1, q16);
        int32_t i2 = ui_len(i, q16);
        gfx_blend_pixel(s, x + ui_len(6, q16) + i1, y + ui_len(1, q16) + i2, col, a);
        gfx_blend_pixel(s, x + ui_len(6, q16) + i1, y + ui_len(7, q16) - i2, col, a);
        gfx_blend_pixel(s, x + ui_len(6, q16) + i1, y + ui_len(7, q16) + i2, col, a);
        gfx_blend_pixel(s, x + ui_len(6, q16) + i1, y + ui_len(13, q16) - i2, col, a);
        gfx_blend_pixel(s, x + ui_len(2, q16) + i1, y + ui_len(4, q16) + i2, col,
                        (uint8_t)(a * 3u / 4u));
        gfx_blend_pixel(s, x + ui_len(2, q16) + i1, y + ui_len(10, q16) - i2, col,
                        (uint8_t)(a * 3u / 4u));
    }
    if (connected)
        gfx_fill_round_rect(s, x + ui_len(11, q16), y + ui_len(5, q16), ui_glyph(3, q16),
                            ui_glyph(4, q16), ui_glyph(1, q16), THEME_ACCENT_GREEN, 250u);
}

void airstin_cc_geometry_for(int32_t surface_w, struct airstin_cc_geometry *out) {
    if (!out) return;
    out->scale_q16 = g_ui_scale_q16;
    out->w = ui_len(CC_DESIGN_W, g_ui_scale_q16);
    out->h = ui_len(CC_DESIGN_H, g_ui_scale_q16);
    out->x = surface_w - out->w - ui_len(CC_DESIGN_MARGIN, g_ui_scale_q16);
    out->y = ui_len(CC_DESIGN_TOP, g_ui_scale_q16);
    out->row_h = ui_len(CC_DESIGN_ROW_H, g_ui_scale_q16);
    out->title_glyph = ui_glyph(2, g_ui_scale_q16);
    out->label_glyph = ui_glyph(1, g_ui_scale_q16);
}

static void ui_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static void ui_fmt_u32(uint32_t v, char *out, int cap) {
    char tmp[12];
    int n = 0, o = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}

static int cc_hit(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void airstin_ui_init(void) {
    if (g_ui_inited) return;
    g_ui_inited = 1;
    net_service_init();
    bt_core_init();
    g_inst.active = 0;
    g_inst.installing = 0;
    g_inst.progress = 0;
    ui_scpy(g_inst.stage, airstin_ui_label("inst.ready", "Ready"), sizeof(g_inst.stage));
}

enum cc_subview airstin_cc_view(void) { return g_cc_view; }

void airstin_cc_set_view(enum cc_subview v) {
    if (g_cc_view != v) {
        g_cc_view = v;
        g_cc_slide = (v == CC_VIEW_ROOT) ? -18 : 18;
    }
}

void airstin_cc_back(void) {
    airstin_cc_set_view(CC_VIEW_ROOT);
}

void airstin_ui_begin_install(const char *path, const struct apk_info *info) {
    airstin_ui_init();
    g_inst.active = 1;
    g_inst.installing = 0;
    g_inst.progress = 0;
    ui_scpy(g_inst.path, path ? path : "", sizeof(g_inst.path));
    ui_scpy(g_inst.stage, airstin_ui_label("inst.stage_ready", "Ready to install package"),
            sizeof(g_inst.stage));
    if (info) g_inst.info = *info;
}

void airstin_ui_cancel_install(void) {
    g_inst.active = 0;
    g_inst.installing = 0;
    g_inst.progress = 0;
}

int airstin_ui_is_installing(void) { return g_inst.active && g_inst.installing; }
int airstin_ui_install_done(void) { return g_inst.active && g_inst.installing && g_inst.progress >= 100; }
const struct airstin_installer *airstin_ui_installer(void) { return &g_inst; }

int airstin_ui_tick(void) {
    int changed = 0;
    airstin_ui_init();
    net_service_sync();
    bt_core_tick();
    if (g_cc_slide > 0) { g_cc_slide -= 6; if (g_cc_slide < 0) g_cc_slide = 0; changed = 1; }
    else if (g_cc_slide < 0) { g_cc_slide += 6; if (g_cc_slide > 0) g_cc_slide = 0; changed = 1; }

    if (g_pwr.shutting_down && !g_pwr.halted) {
        if (g_pwr.countdown > 0) { g_pwr.countdown--; return 1; }
        g_pwr.halted = 1;
#ifndef FLAIROS_HOST_TEST
        hal_power_shutdown(g_pwr.forced);
#endif
        return 1;
    }
    if (!g_inst.active || !g_inst.installing) return changed;
    if (g_inst.progress < 100) {
        g_inst.progress += 15;
        if (g_inst.progress > 100) g_inst.progress = 100;
        if (g_inst.progress < 30)
            ui_scpy(g_inst.stage, airstin_ui_label("inst.inspecting", "Inspecting archive..."),
                    sizeof(g_inst.stage));
        else if (g_inst.progress < 60)
            ui_scpy(g_inst.stage, airstin_ui_label("inst.extracting", "Extracting DEX & .so..."),
                    sizeof(g_inst.stage));
        else if (g_inst.progress < 90)
            ui_scpy(g_inst.stage, airstin_ui_label("inst.registering", "Registering Surface..."),
                    sizeof(g_inst.stage));
        else
            ui_scpy(g_inst.stage, airstin_ui_label("inst.finalizing", "Finalizing install..."),
                    sizeof(g_inst.stage));
        return 1;
    }
    return changed;
}

void airstin_draw_cell_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, int active, uint32_t rgb) {
    ui_cell_icon(s, x, y, bars, active, rgb, AIRSTIN_UNIT_Q16);
}

void airstin_render_status_cluster(struct ag_surface *s, int32_t w, const char *arch_name, int cc_open) {
    const struct net_service_state *ns = net_service_state();
    const struct bt_adapter_status *bt = bt_core_status();
    int32_t q = g_ui_scale_q16;

    gfx_pill_badge(s, w - ui_len(370, q), ui_len(5, q), arch_name, THEME_ACCENT_BLUE,
                   THEME_TEXT_PRIMARY, ui_glyph(2, q));
    if (cc_open)
        gfx_fill_round_rect(s, w - ui_len(246, q), ui_len(4, q), ui_len(236, q),
                            ui_len(26, q), ui_glyph(8, q), THEME_GLASS_LIGHT, 185u);
    ui_bt_icon(s, w - ui_len(240, q), ui_len(9, q), bt->powered,
               bt->state == BT_STATE_CONNECTED,
               bt->state == BT_STATE_ERROR ? THEME_ACCENT_PINK : THEME_ACCENT_BLUE, q);
    ui_cell_icon(s, w - ui_len(214, q), ui_len(9, q), ns->cell_bars,
                 ns->cell_on && !ns->airplane_mode, THEME_ACCENT_TEAL, q);
    gfx_draw_text_scaled(s, w - ui_len(196, q), ui_len(12, q),
                         ns->airplane_mode ? "AP" : (ns->cell_on ? (ns->cell_rat[0] ? ns->cell_rat : "5G") : "--"),
                         THEME_TEXT_MUTED, ui_glyph(1, q));
    if (ns->wifi_on && !ns->airplane_mode)
        ui_wifi_icon(s, w - ui_len(174, q), ui_len(7, q), ns->wifi_bars, THEME_ACCENT_TEAL, q);
    else
        ui_eth_icon(s, w - ui_len(174, q), ui_len(7, q), THEME_ACCENT_BLUE, q);
    {
        int show_pct = ns->battery_pct >= 0 ? ns->battery_pct : 100;
        char bpct[8];
        ui_battery_icon(s, w - ui_len(144, q), ui_len(8, q), show_pct, ns->ac_power,
                        THEME_ACCENT_GREEN, q);
        if (ns->battery_pct >= 0) {
            ui_fmt_u32((uint32_t)ns->battery_pct, bpct, sizeof(bpct));
            int l = 0; while (bpct[l]) l++;
            if (l + 1 < (int)sizeof(bpct)) { bpct[l++] = '%'; bpct[l] = 0; }
        } else {
            ui_scpy(bpct, "AC", sizeof(bpct));
        }
        gfx_draw_text_aa(s, w - ui_len(108, q), ui_len(10, q), bpct, THEME_TEXT_PRIMARY,
                         ui_glyph(2, q));
    }
    /* Control Center toggles icon */
    gfx_fill_round_rect(s, w - ui_len(44, q), ui_len(8, q), ui_len(24, q), ui_glyph(7, q),
                        ui_glyph(3, q), THEME_GLASS_BORDER, 210u);
    gfx_fill_round_rect(s, w - ui_len(30, q), ui_len(9, q), ui_len(8, q), ui_glyph(5, q),
                        ui_glyph(2, q), THEME_TEXT_PRIMARY, 240u);
    gfx_fill_round_rect(s, w - ui_len(44, q), ui_len(19, q), ui_len(24, q), ui_glyph(7, q),
                        ui_glyph(3, q), THEME_GLASS_BORDER, 210u);
    gfx_fill_round_rect(s, w - ui_len(42, q), ui_len(20, q), ui_len(8, q), ui_glyph(5, q),
                        ui_glyph(2, q), THEME_ACCENT_TEAL, 240u);
}

static void draw_cc_conn_row(struct ui_pen *p, int32_t x, int32_t y, int32_t w,
                             int active, uint32_t col, const char *title, const char *sub) {
    ui_round(p, x, y, w - 44, 38, 8, active ? col : THEME_GLASS_LIGHT, active ? 215u : 185u);
    ui_text(p, x + 10, y + 7, title, THEME_TEXT_PRIMARY, 1);
    ui_text(p, x + 10, y + 21, sub, THEME_TEXT_MUTED, 1);

    ui_round(p, x + w - 40, y, 40, 38, 8, THEME_GLASS_LIGHT, 205u);
    ui_chevron_right(p->s, px_of(p, x + w - 24), py_of(p, y + 14), ui_glyph(10, p->q16),
                     THEME_TEXT_PRIMARY);
}

void airstin_render_control_center(struct ag_surface *s, int32_t w, int cc_open, int cc_anim,
                                   int anim_on, int theme_idx) {
    if (!cc_open && cc_anim <= 0) return;
    const struct net_service_state *ns = net_service_state();
    const struct airp_session *as = airp_session_status();
    const struct bt_adapter_status *bt = bt_core_status();
    struct airstin_cc_geometry geo;
    struct ui_pen pen;
    int32_t drop = ui_len(CC_DESIGN_DROP, g_ui_scale_q16);
    int32_t cc_x, cc_y, cc_w = 0, cc_h = 0;

    airstin_cc_geometry_for(w, &geo);
    cc_w = geo.w;
    cc_h = geo.h;
    cc_x = geo.x + (anim_on ? ui_len(g_cc_slide, g_ui_scale_q16) : 0);
    cc_y = geo.y - (drop - cc_anim);
    pen = ui_pen_at(s, cc_x, cc_y, g_ui_scale_q16);
    gfx_glass_panel(s, cc_x, cc_y, cc_w, cc_h, ui_glyph(14, g_ui_scale_q16),
                    THEME_GLASS_DARK, 244u);

    if (g_cc_view != CC_VIEW_ROOT) {
        /* Shared Drill-Down Back Header */
        ui_round(&pen, 12, 10, 68, 26, 7, THEME_GLASS_LIGHT, 215u);
        ui_chevron_left(s, px_of(&pen, 20), py_of(&pen, 18), ui_glyph(10, pen.q16),
                        THEME_TEXT_PRIMARY);
        ui_text(&pen, 34, 19, airstin_ui_label("cc.back", "Back"), THEME_TEXT_PRIMARY, 1);
        {
            const char *htitle = (g_cc_view == CC_VIEW_WIFI)
                                     ? airstin_ui_label("cc.wifi", "Wi-Fi")
                                     : (g_cc_view == CC_VIEW_CELL
                                            ? airstin_ui_label("cc.cellular", "Cellular")
                                            : airstin_ui_label("cc.bluetooth", "Bluetooth"));
            ui_text_aa(&pen, 92, 15, htitle, THEME_TEXT_PRIMARY, 2);
        }

        if (g_cc_view == CC_VIEW_WIFI) {
            int w_on = ns->wifi_on && !ns->airplane_mode;
            ui_round(&pen, CC_DESIGN_W - 66, 10, 54, 26, 7,
                     w_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 225u);
            ui_text(&pen, CC_DESIGN_W - 50, 19,
                    w_on ? airstin_ui_label("cc.on", "ON") : airstin_ui_label("cc.off", "OFF"),
                    THEME_TEXT_PRIMARY, 1);

            ui_round(&pen, 12, 46, CC_DESIGN_W - 24, 62, 8, THEME_GLASS_LIGHT, 180u);
            ui_text(&pen, 20, 56,
                    w_on ? (ns->ssid[0] ? ns->ssid
                                        : airstin_ui_label("cc.wifi_connected_full", "Wi-Fi Connected"))
                         : airstin_ui_label("cc.wifi_no_radio", "No Wi-Fi Radio"),
                    w_on ? THEME_ACCENT_TEAL : THEME_TEXT_MUTED, 1);
            ui_text(&pen, 20, 74,
                    w_on ? (ns->wifi_ip[0] ? ns->wifi_ip
                                           : airstin_ui_label("cc.dhcp_pending", "DHCP Pending"))
                         : airstin_ui_label("cc.ethernet_nic", "Ethernet / Hardware NIC"),
                    THEME_TEXT_MUTED, 1);
            ui_text(&pen, 20, 92, ns->tel_source[0] ? ns->tel_source : "hardware",
                    THEME_TEXT_MUTED, 1);
        } else if (g_cc_view == CC_VIEW_CELL) {
            int c_on = ns->cell_on && !ns->airplane_mode;
            ui_round(&pen, CC_DESIGN_W - 66, 10, 54, 26, 7,
                     c_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 225u);
            ui_text(&pen, CC_DESIGN_W - 50, 19,
                    c_on ? airstin_ui_label("cc.on", "ON") : airstin_ui_label("cc.off", "OFF"),
                    THEME_TEXT_PRIMARY, 1);

            ui_round(&pen, 12, 48, CC_DESIGN_W - 24, 90, 8, THEME_GLASS_LIGHT, 185u);
            ui_text_aa(&pen, 22, 60,
                       ns->carrier[0] ? ns->carrier
                                      : airstin_ui_label("cc.no_cell_modem", "No Cellular Modem"),
                       THEME_TEXT_PRIMARY, 2);
            ui_text(&pen, 22, 88, c_on && ns->cell_rat[0] ? ns->cell_rat : "--",
                    THEME_ACCENT_TEAL, 1);
            ui_text(&pen, 76, 88,
                    c_on ? (ns->cell_roaming ? airstin_ui_label("cc.roaming", "Roaming")
                                             : airstin_ui_label("cc.home_network", "Home Network"))
                         : airstin_ui_label("cc.no_sim", "No Hardware SIM"),
                    THEME_TEXT_MUTED, 1);
            ui_cell_icon(s, px_of(&pen, 22), py_of(&pen, 108), ns->cell_bars, c_on,
                         THEME_ACCENT_TEAL, pen.q16);
        } else {
            /* CC_VIEW_BT */
            ui_round(&pen, CC_DESIGN_W - 66, 10, 54, 26, 7,
                     bt->powered ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 225u);
            ui_text(&pen, CC_DESIGN_W - 50, 19,
                    bt->powered ? airstin_ui_label("cc.on", "ON")
                                : airstin_ui_label("cc.off", "OFF"),
                    THEME_TEXT_PRIMARY, 1);

            ui_round(&pen, 12, 46, 124, 28, 7, THEME_ACCENT_INDIGO, 215u);
            ui_text(&pen, 26, 56,
                    bt->scanning ? airstin_ui_label("cc.scanning", "Scanning...")
                                 : airstin_ui_label("cc.scan_devices", "Scan Devices"),
                    THEME_TEXT_PRIMARY, 1);
            ui_round(&pen, 144, 46, 124, 28, 7,
                     bt->state == BT_STATE_ERROR ? THEME_ACCENT_PINK : THEME_GLASS_LIGHT, 200u);
            ui_text(&pen, 162, 56,
                    bt->state == BT_STATE_ERROR ? airstin_ui_label("cc.recover", "Recover")
                                                : airstin_ui_label("cc.reset_hci", "Reset HCI"),
                    THEME_TEXT_PRIMARY, 1);
            ui_text(&pen, 14, 82, bt->status_text, THEME_ACCENT_TEAL, 1);

            {
                int cnt = bt_core_device_count();
                if (cnt == 0) {
                    ui_round(&pen, 12, 102, CC_DESIGN_W - 24, 42, 8, THEME_GLASS_LIGHT, 165u);
                    ui_text(&pen, 22, 118,
                            bt->powered ? airstin_ui_label("cc.tap_scan", "Tap Scan Devices")
                                        : airstin_ui_label("cc.turn_bt_on", "Turn Bluetooth ON"),
                            THEME_TEXT_MUTED, 1);
                }
                for (int i = 0; i < cnt && i < 4; i++) {
                    const struct bt_device *d = bt_core_device_get(i);
                    int32_t ry = 102 + i * 54;
                    if (!d) continue;
                    ui_round(&pen, 12, ry, CC_DESIGN_W - 24, 46, 8,
                             d->connected ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT,
                             d->connected ? 215u : 175u);
                    ui_text(&pen, 20, ry + 10, d->name, THEME_TEXT_PRIMARY, 1);
                    ui_text(&pen, 20, ry + 26,
                            d->connected ? airstin_ui_label("cc.connected", "Connected")
                                         : (d->paired
                                                ? airstin_ui_label("cc.paired_connect", "Paired - Connect")
                                                : airstin_ui_label("cc.tap_to_pair", "Tap to Pair")),
                            THEME_TEXT_MUTED, 1);
                }
            }
        }
        return;
    }

    /* CC_VIEW_ROOT */
    ui_text_aa(&pen, 14, 12, airstin_ui_label("cc.title", "AirStinUI Control"),
               THEME_TEXT_PRIMARY, 2);

    draw_cc_conn_row(&pen, 12, 42, CC_DESIGN_W - 24,
                     ns->wifi_on && !ns->airplane_mode, THEME_ACCENT_BLUE,
                     (ns->wifi_on && !ns->airplane_mode)
                         ? airstin_ui_label("cc.wifi_on", "Wi-Fi: ON")
                         : airstin_ui_label("cc.wifi_off", "Wi-Fi: OFF"),
                     (ns->wifi_on && !ns->airplane_mode)
                         ? (ns->ssid[0] ? ns->ssid : airstin_ui_label("cc.connected", "Connected"))
                         : airstin_ui_label("cc.wifi_none", "No Wi-Fi"));

    draw_cc_conn_row(&pen, 12, 84, CC_DESIGN_W - 24,
                     ns->cell_on && !ns->airplane_mode, THEME_ACCENT_TEAL,
                     (ns->cell_on && !ns->airplane_mode)
                         ? airstin_ui_label("cc.cell_on", "Cellular: ON")
                         : airstin_ui_label("cc.cell_off", "Cellular: OFF"),
                     (ns->cell_on && !ns->airplane_mode)
                         ? (ns->carrier[0] ? ns->carrier : airstin_ui_label("cc.cell_active", "Active"))
                         : airstin_ui_label("cc.cell_none", "No Modem"));

    draw_cc_conn_row(&pen, 12, 126, CC_DESIGN_W - 24,
                     bt->powered, THEME_ACCENT_INDIGO,
                     bt->powered ? airstin_ui_label("cc.bt_on", "Bluetooth: ON")
                                 : (bt->state == BT_STATE_UNAVAILABLE
                                        ? airstin_ui_label("cc.bt_nohw", "Bluetooth: NO HW")
                                        : airstin_ui_label("cc.bt_off", "Bluetooth: OFF")),
                     bt->state == BT_STATE_CONNECTED ? bt->active_name : bt->status_text);

    /* Airplane & Motion split */
    {
        int32_t hw = (CC_DESIGN_W - 30) / 2;
        ui_round(&pen, 12, 170, hw, 32, 8,
                 ns->airplane_mode ? THEME_ACCENT_ORANGE : THEME_GLASS_LIGHT, 205u);
        ui_text(&pen, 20, 182,
                ns->airplane_mode ? airstin_ui_label("cc.airplane_on", "Airplane: ON")
                                  : airstin_ui_label("cc.airplane_off", "Airplane: OFF"),
                THEME_TEXT_PRIMARY, 1);
        ui_round(&pen, 18 + hw, 170, hw, 32, 8,
                 anim_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 205u);
        ui_text(&pen, 26 + hw, 182,
                anim_on ? airstin_ui_label("cc.motion_on", "Motion: ON")
                        : airstin_ui_label("cc.motion_off", "Motion: OFF"),
                THEME_TEXT_PRIMARY, 1);
    }

    /* Theme swatches */
    {
        int32_t sw = (CC_DESIGN_W - 36) / 3;
        const char *cc_tn[3] = {"cc.theme_indigo", "cc.theme_blue", "cc.theme_pink"};
        const char *cc_tf[3] = {"Indigo", "Blue", "Pink"};
        uint32_t cc_tc[3] = {THEME_ACCENT_INDIGO, THEME_ACCENT_BLUE, THEME_ACCENT_PINK};
        for (int t = 0; t < 3; t++) {
            ui_round(&pen, 12 + t * (sw + 6), 208, sw, 30, 8, cc_tc[t],
                     theme_idx == t ? 250u : 170u);
            ui_text(&pen, 18 + t * (sw + 6), 219, airstin_ui_label(cc_tn[t], cc_tf[t]),
                    THEME_TEXT_PRIMARY, 1);
        }
    }

    /* Quick Actions Tray: Search, AirP Cast, Power */
    ui_round(&pen, 12, 244, 80, 34, 8, THEME_GLASS_LIGHT, 210u);
    ui_text(&pen, 26, 257, airstin_ui_label("cc.search", "Search"), THEME_TEXT_PRIMARY, 1);

    {
        int airp_busy = (as->state == AIRP_STATE_DISCOVERING || as->state == AIRP_STATE_CONNECTED ||
                         as->state == AIRP_STATE_STREAMING);
        ui_round(&pen, 98, 244, 92, 34, 8,
                 airp_busy ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 215u);
        ui_text(&pen, 108, 257,
                as->state == AIRP_STATE_IDLE ? airstin_ui_label("cc.cast", "AirP Cast")
                                             : airp_state_name(as->state),
                THEME_TEXT_PRIMARY, 1);
    }

    ui_round(&pen, 196, 244, 72, 34, 8,
             g_pwr.modal_open ? THEME_ACCENT_PINK : THEME_ACCENT_ORANGE, 225u);
    ui_text(&pen, 212, 257, airstin_ui_label("cc.power", "Power"), THEME_TEXT_PRIMARY, 1);

    /* Power + Hardware Telemetry Footer */
    ui_round(&pen, 12, 286, CC_DESIGN_W - 24, 40, 8, THEME_GLASS_LIGHT, 185u);
    ui_battery_icon(s, px_of(&pen, 20), py_of(&pen, 298),
                    ns->battery_pct >= 0 ? ns->battery_pct : 100, ns->ac_power,
                    THEME_ACCENT_GREEN, pen.q16);
    ui_text(&pen, 56, 294,
            ns->battery_pct >= 0
                ? (ns->ac_power ? airstin_ui_label("cc.power_ac", "Power: AC")
                                : airstin_ui_label("cc.power_battery", "Power: Battery"))
                : airstin_ui_label("cc.power_ac_nomic", "Power: AC (No PMIC)"),
            THEME_TEXT_PRIMARY, 1);
    ui_text(&pen, 56, 308, ns->tel_source[0] ? ns->tel_source : "hardware",
            THEME_TEXT_MUTED, 1);
}

int airstin_cc_handle_click(int32_t mx, int32_t my, int32_t w,
                            int *anim_on, int *theme_idx,
                            int *spotlight_open, int *cc_open) {
    struct airstin_cc_geometry geo;
    int32_t q = g_ui_scale_q16;
    int32_t cc_x, cc_y, cc_w, cc_h;

    airstin_cc_geometry_for(w, &geo);
    cc_x = geo.x;
    cc_y = geo.y;
    cc_w = geo.w;
    cc_h = geo.h;
    if (!cc_hit(mx, my, cc_x, cc_y, cc_w, cc_h)) return 0;

    if (g_cc_view != CC_VIEW_ROOT) {
        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(10, q),
                   ui_len(68, q), ui_len(26, q))) { airstin_cc_back(); return 1; }
        if (g_cc_view == CC_VIEW_WIFI) {
            if (cc_hit(mx, my, cc_x + cc_w - ui_len(66, q), cc_y + ui_len(10, q),
                       ui_len(54, q), ui_len(26, q))) { net_service_toggle_wifi(); return 1; }
            return 1;
        }
        if (g_cc_view == CC_VIEW_CELL) {
            if (cc_hit(mx, my, cc_x + cc_w - ui_len(66, q), cc_y + ui_len(10, q),
                       ui_len(54, q), ui_len(26, q))) { net_service_toggle_cell(); return 1; }
            return 1;
        }
        /* CC_VIEW_BT */
        if (cc_hit(mx, my, cc_x + cc_w - ui_len(66, q), cc_y + ui_len(10, q),
                   ui_len(54, q), ui_len(26, q))) { (void)bt_core_toggle_power(); return 1; }
        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(46, q),
                   ui_len(124, q), ui_len(28, q))) {
            if (!bt_core_status()->powered) (void)bt_core_power(1);
            (void)bt_core_scan_start();
            return 1;
        }
        if (cc_hit(mx, my, cc_x + ui_len(144, q), cc_y + ui_len(46, q),
                   ui_len(124, q), ui_len(28, q))) { bt_core_recover(); return 1; }
        {
            int cnt = bt_core_device_count();
            for (int i = 0; i < cnt && i < 4; i++) {
                if (cc_hit(mx, my, cc_x + ui_len(12, q),
                           cc_y + ui_len(102 + i * 54, q), cc_w - ui_len(24, q),
                           ui_len(46, q))) {
                    const struct bt_device *d = bt_core_device_get(i);
                    if (d && d->connected) (void)bt_core_disconnect(i);
                    else (void)bt_core_connect(i);
                    return 1;
                }
            }
        }
        return 1;
    }

    /* CC_VIEW_ROOT: Toggle (left) vs '>' Drill-Down (right) */
    {
        int32_t rw = cc_w - ui_len(24, q);
        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(42, q),
                   rw - ui_len(44, q), geo.row_h)) { net_service_toggle_wifi(); return 1; }
        if (cc_hit(mx, my, cc_x + ui_len(12, q) + rw - ui_len(40, q), cc_y + ui_len(42, q),
                   ui_len(40, q), geo.row_h)) { airstin_cc_set_view(CC_VIEW_WIFI); return 1; }

        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(84, q),
                   rw - ui_len(44, q), geo.row_h)) { net_service_toggle_cell(); return 1; }
        if (cc_hit(mx, my, cc_x + ui_len(12, q) + rw - ui_len(40, q), cc_y + ui_len(84, q),
                   ui_len(40, q), geo.row_h)) { airstin_cc_set_view(CC_VIEW_CELL); return 1; }

        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(126, q),
                   rw - ui_len(44, q), geo.row_h)) { (void)bt_core_toggle_power(); return 1; }
        if (cc_hit(mx, my, cc_x + ui_len(12, q) + rw - ui_len(40, q), cc_y + ui_len(126, q),
                   ui_len(40, q), geo.row_h)) { airstin_cc_set_view(CC_VIEW_BT); return 1; }
    }

    {
        int32_t hw = (cc_w - ui_len(30, q)) / 2;
        if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(170, q),
                   hw, ui_len(32, q))) { net_service_toggle_airplane(); return 1; }
        if (cc_hit(mx, my, cc_x + ui_len(18, q) + hw, cc_y + ui_len(170, q),
                   hw, ui_len(32, q))) { if (anim_on) *anim_on = !(*anim_on); return 1; }
    }

    {
        int32_t sw = (cc_w - ui_len(36, q)) / 3;
        for (int t = 0; t < 3; t++) {
            if (cc_hit(mx, my, cc_x + ui_len(12, q) + t * (sw + ui_len(6, q)),
                       cc_y + ui_len(208, q), sw, ui_len(30, q))) {
                if (theme_idx) *theme_idx = t;
                return 1;
            }
        }
    }
    if (cc_hit(mx, my, cc_x + ui_len(12, q), cc_y + ui_len(244, q),
               ui_len(80, q), ui_len(34, q))) {
        if (spotlight_open) *spotlight_open = 1;
        if (cc_open) *cc_open = 0;
        return 1;
    }
    if (cc_hit(mx, my, cc_x + ui_len(98, q), cc_y + ui_len(244, q),
               ui_len(92, q), ui_len(34, q))) {
        const struct airp_session *as = airp_session_status();
        if (as->state == AIRP_STATE_IDLE || as->state == AIRP_STATE_ERROR) {
            (void)airp_discover_start();
            if (airp_peer_count() > 0 && airp_pair_peer(0, 4242u) == 0) {
                (void)airp_start_stream(AIRP_PAY_SCREEN, "AirP Cast", 4096u);
            }
        } else {
            airp_disconnect();
        }
        return 1;
    }
    if (cc_hit(mx, my, cc_x + ui_len(196, q), cc_y + ui_len(244, q),
               ui_len(72, q), ui_len(34, q))) {
        if (cc_open) *cc_open = 0;
        airstin_power_open_modal();
        return 1;
    }
    return 1;
}

void airstin_render_installer(struct ag_surface *s, int32_t w, int32_t h) {
    struct ui_pen pen;
    int32_t q = g_ui_scale_q16;
    int32_t dw0, dh0, dx0, dy0;

    if (!g_inst.active) return;
    dw0 = ui_len(400, q);
    dh0 = ui_len(236, q);
    dx0 = (w - dw0) / 2;
    dy0 = (h - dh0) / 2;
    pen = ui_pen_at(s, dx0, dy0, q);
    gfx_fill_rect_alpha(s, 0, 0, w, h, 0x00050810u, 155u);
    gfx_glass_panel(s, dx0, dy0, dw0, dh0, ui_glyph(16, q), THEME_GLASS_DARK, 245u);
    ui_text_aa(&pen, 20, 18, airstin_ui_label("inst.title", "Android Package Installer"),
               THEME_TEXT_PRIMARY, 2);
    ui_pill(&pen, 20, 50,
            g_inst.info.is_system_app ? airstin_ui_label("inst.system", "SYSTEM APP")
                                      : airstin_ui_label("inst.user", "USER TEST APP"),
            g_inst.info.is_system_app ? THEME_ACCENT_BLUE : THEME_ACCENT_PINK,
            THEME_TEXT_PRIMARY, 2);
    ui_text(&pen, 20, 84, g_inst.info.app_label[0] ? g_inst.info.app_label : g_inst.path,
            THEME_TEXT_PRIMARY, 2);
    ui_text(&pen, 20, 108, g_inst.info.package_name, THEME_TEXT_MUTED, 1);

    ui_text(&pen, 20, 132, g_inst.stage, THEME_ACCENT_TEAL, 1);
    {
        int32_t pb_w = 400 - 40;
        int32_t fill_w = (pb_w * g_inst.progress) / 100;
        ui_round(&pen, 20, 150, pb_w, 16, 8, THEME_GLASS_LIGHT, 180u);
        if (fill_w > 4)
            ui_round(&pen, 20, 150, fill_w, 16, 8, THEME_ACCENT_GREEN, 240u);
    }

    ui_round(&pen, 16, 236 - 42, 178, 28, 8, THEME_GLASS_LIGHT, 230u);
    ui_text(&pen, 34, 236 - 35, airstin_ui_label("inst.cancel", "Cancel"),
            THEME_TEXT_PRIMARY, 2);
    ui_round(&pen, 28 + 178, 236 - 42, 178, 28, 8, THEME_ACCENT_BLUE, 245u);
    ui_text(&pen, 36 + 178, 236 - 35,
            g_inst.installing ? airstin_ui_label("inst.installing", "Installing...")
                              : airstin_ui_label("inst.install_open", "Install & Open"),
            THEME_TEXT_PRIMARY, 2);
}

void airstin_power_open_modal(void) {
    g_pwr.modal_open = 1;
    g_pwr.taps = 0;
    g_pwr.shutting_down = 0;
    g_pwr.forced = 0;
    g_pwr.halted = 0;
}

void airstin_power_cancel(void) {
    if (!g_pwr.shutting_down) {
        g_pwr.modal_open = 0;
        g_pwr.taps = 0;
    }
}

void airstin_power_tap_shutdown(void) {
    if (!g_pwr.modal_open) return;
    g_pwr.taps++;
    if (g_pwr.taps >= 2) {
        g_pwr.shutting_down = 1;
        g_pwr.forced = 0;
        g_pwr.countdown = 8;
    }
}

void airstin_power_force_hold(void) {
    g_pwr.modal_open = 1;
    g_pwr.shutting_down = 1;
    g_pwr.forced = 1;
    g_pwr.countdown = 0;
    g_pwr.halted = 1;
#ifndef FLAIROS_HOST_TEST
    hal_power_shutdown(1);
#endif
}

const struct airstin_power_state *airstin_power_state(void) { return &g_pwr; }

void airstin_render_power_modal(struct ag_surface *s, int32_t w, int32_t h) {
    struct ui_pen pen;
    int32_t q = g_ui_scale_q16;
    int32_t mw, mh, mx, my, bw, by;

    if (!g_pwr.modal_open) return;
    mw = ui_len(380, q);
    mh = ui_len(188, q);
    mx = (w - mw) / 2;
    my = (h - mh) / 2;
    pen = ui_pen_at(s, mx, my, q);
    gfx_fill_rect_alpha(s, 0, 0, w, h, 0x00050810u, 155u);
    gfx_glass_panel(s, mx, my, mw, mh, ui_glyph(16, q), THEME_GLASS_DARK, 246u);
    ui_text_aa(&pen, 20, 18, airstin_ui_label("power.title", "Power & Shutdown"),
               THEME_TEXT_PRIMARY, 2);
    if (g_pwr.shutting_down) {
        ui_text(&pen, 20, 62,
                g_pwr.forced ? airstin_ui_label("power.forced", "Forced hardware poweroff...")
                             : airstin_ui_label("power.graceful", "Graceful shutdown: syncing VFS..."),
                THEME_ACCENT_ORANGE, 2);
        ui_round(&pen, 20, 104, 380 - 40, 14, 7, THEME_GLASS_LIGHT, 180u);
        {
            int32_t prog = (8 - g_pwr.countdown) * (380 - 40) / 8;
            if (prog > 4) ui_round(&pen, 20, 104, prog, 14, 7, THEME_ACCENT_PINK, 240u);
        }
        return;
    }
    ui_text(&pen, 20, 58,
            g_pwr.taps == 1
                ? airstin_ui_label("power.armed", "Armed (1/2) - Tap Shutdown again!")
                : airstin_ui_label("power.tap_hint", "Double-tap Shutdown to power off"),
            g_pwr.taps == 1 ? THEME_ACCENT_ORANGE : THEME_TEXT_MUTED, 1);
    ui_text(&pen, 20, 80, airstin_ui_label("power.hold_hint", "Hold hardware Power key to force off"),
            THEME_TEXT_MUTED, 1);

    bw = (380 - 52) / 2;
    by = 188 - 46;
    ui_round(&pen, 18, by, bw, 32, 8, THEME_GLASS_LIGHT, 225u);
    ui_text(&pen, 48, by + 9, airstin_ui_label("power.cancel", "Cancel"),
            THEME_TEXT_PRIMARY, 2);

    ui_round(&pen, 34 + bw, by, bw, 32, 8,
             g_pwr.taps == 1 ? THEME_ACCENT_ORANGE : THEME_ACCENT_PINK, 240u);
    ui_text(&pen, 44 + bw, by + 9,
            g_pwr.taps == 1 ? airstin_ui_label("power.confirm", "Confirm 2/2")
                            : airstin_ui_label("power.shutdown", "Shutdown"),
            THEME_TEXT_PRIMARY, 2);
}
