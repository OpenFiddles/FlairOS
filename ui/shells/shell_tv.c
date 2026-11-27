/* SPDX-License-Identifier: AGPL-3.0-only */
/* Modular 10-foot TV shell (D-pad focus spring + AirStinUI drawer).
 *
 * One layout, authored in 1280x720 design coordinates and scaled to the surface
 * it draws on: a 4K panel gets the same composition at three times the size
 * rather than a 720p layout in the corner. The home-screen tiles come from
 * FlairTV.airp when the platform stages one, and from the built-in layout
 * otherwise, so the launcher is a packaged resource rather than kernel tables.
 */
#include "shell_common.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../runtime/art/art_bridge.h"
#include "../../net/airp/airp_net.h"
#include "../../net/bt/bt_core.h"
#include "../systemui/airstin_ui.h"
#include "../../kernel/ui/tv_layout.h"
#include "../../kernel/ui/tv_launcher.h"

static struct tv_launcher g_launcher;
static int g_launcher_ready = 0;
static int focused_tile = 0;
static int tv_cc_open = 0;
static int tv_anim_on = 1;
static int tv_theme_idx = 0;
static int32_t tv_cc_anim = 0;
static int32_t tv_focus_x = 0;
static int32_t tv_focus_vx = 0;
static int32_t tv_focus_target = 0;
static int32_t tv_surface_w = TV_LAYOUT_BASE_W;
static int32_t tv_surface_h = TV_LAYOUT_BASE_H;
static int32_t tv_scale_q16 = TV_LAYOUT_UNIT_Q16;

/* Design pixels and design text sizes, scaled for the current surface. */
static int32_t px(int32_t base) { return tv_layout_px(base, tv_scale_q16); }
static int32_t tx(int32_t base) { return tv_layout_text(base, tv_scale_q16); }

static void tv_set_surface(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    tv_surface_w = w;
    tv_surface_h = h;
    tv_scale_q16 = tv_layout_scale_q16(w, h);
}

static int tv_streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void tv_launcher_ensure(const struct shell_boot_info *info) {
    if (g_launcher_ready) return;
    g_launcher_ready = 1;
    tv_launcher_default(&g_launcher);
    if (info && info->session_pkg && info->session_pkg_len) {
        struct tv_launcher packaged;
        struct tv_launcher_report report;
        if (tv_launcher_load_package(info->session_pkg, info->session_pkg_len,
                                      &packaged, &report))
            g_launcher = packaged;
    }
    if (focused_tile >= g_launcher.tile_count) focused_tile = 0;
}

/* Text the layout declares, or empty when it declares none. The built-in
   layout always declares the keys the shell draws. */
static const char *tv_label(const char *key) {
    static char text[TV_STRING_TEXT_MAX];
    return tv_strings_copy(&g_launcher.strings, key, text, sizeof text) ? text : "";
}

/* The control drawer asks for every label it draws; in a TV session those come
   from the same packaged strings.json as the shell's own labels. */
static const char *tv_drawer_label(const char *key) {
    static char text[TV_STRING_TEXT_MAX];
    return tv_strings_copy(&g_launcher.strings, key, text, sizeof text) ? text : 0;
}

/* "app:N" launches an ART package by index, "airp:cast" starts the screen
   stream. Unknown targets are ignored rather than guessed at. */
static void tv_launch_tile(int idx) {
    const char *target;
    int n = 0;

    if (idx < 0 || idx >= g_launcher.tile_count) return;
    target = g_launcher.tiles[idx].target;
    if (target[0] == 'a' && target[1] == 'p' && target[2] == 'p' && target[3] == ':') {
        const char *p = target + 4;
        if (*p < '0' || *p > '9') return;
        while (*p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            p++;
        }
        if (n < art_pm_count()) (void)art_am_launch(n);
        return;
    }
    if (tv_streq(target, "airp:cast"))
        (void)airp_start_stream(AIRP_PAY_SCREEN, "AirP TV Receiver", 65536u);
}

/* Tile row geometry, shared by rendering and hit testing so the two cannot
   disagree — at any scale. */
static void tv_row_geom(int32_t w, int32_t h, int32_t *rx, int32_t *ry,
                        int32_t *cw, int32_t *ch) {
    int32_t hx = px(48);
    int32_t hy = px(86);
    int32_t hw = w - px(96);
    int32_t hh = h / 2 - px(44);
    int32_t gap = px(24);
    int n = g_launcher.tile_count > 0 ? g_launcher.tile_count : 1;

    *rx = hx;
    *ry = hy + hh + px(28);
    *cw = (hw - gap * (n - 1)) / n;
    *ch = h - *ry - px(48);
    if (*ch < px(96)) *ch = px(96);
}

static int32_t tv_focus_target_for(int32_t w, int32_t h) {
    int32_t rx, ry, cw, ch;
    tv_row_geom(w, h, &rx, &ry, &cw, &ch);
    return rx + focused_tile * (cw + px(24));
}

int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h) {
    int32_t rx, ry, cw, ch;

    tv_set_surface(w, h);
    airstin_ui_set_scale_q16(tv_scale_q16);
    tv_launcher_ensure(0);
    airstin_ui_set_labels(tv_drawer_label);
    if (!in) return 0;
    if (in->key == INPUT64_KEY_UP) {
        tv_cc_open = 1;
        return 1;
    }
    if (in->key == INPUT64_KEY_ESC || (in->key == INPUT64_KEY_DOWN && tv_cc_open)) {
        if (airstin_cc_view() != CC_VIEW_ROOT) airstin_cc_back();
        else tv_cc_open = 0;
        return 1;
    }
    if (in->key == INPUT64_KEY_LEFT && focused_tile > 0) { focused_tile--; return 1; }
    if (in->key == INPUT64_KEY_RIGHT && focused_tile + 1 < g_launcher.tile_count) {
        focused_tile++;
        return 1;
    }
    if (in->key == INPUT64_KEY_ENTER) {
        if (tv_cc_open) {
            if (airstin_cc_view() == CC_VIEW_ROOT) airstin_cc_set_view(CC_VIEW_BT);
            else {
                if (!bt_core_status()->powered) (void)bt_core_power(1);
                (void)bt_core_scan_start();
            }
            return 1;
        }
        tv_launch_tile(focused_tile);
        return 1;
    }
    if (in->clicked) {
        if (tv_cc_open) {
            int dummy_spot = 0;
            if (airstin_cc_handle_click(in->mouse_x, in->mouse_y, w, &tv_anim_on,
                                        &tv_theme_idx, &dummy_spot, &tv_cc_open))
                return 1;
        }
        if (in->mouse_y < px(64) && in->mouse_x >= w - px(280)) {
            tv_cc_open = !tv_cc_open;
            if (!tv_cc_open) airstin_cc_set_view(CC_VIEW_ROOT);
            return 1;
        }
        if (tv_cc_open) {
            tv_cc_open = 0;
            airstin_cc_set_view(CC_VIEW_ROOT);
            return 1;
        }
        tv_row_geom(w, h, &rx, &ry, &cw, &ch);
        for (int i = 0; i < g_launcher.tile_count; i++) {
            int32_t cx = rx + i * (cw + px(24));
            if (in->mouse_x >= cx && in->mouse_x < cx + cw &&
                in->mouse_y >= ry && in->mouse_y < ry + ch) {
                focused_tile = i;
                tv_launch_tile(i);
                return 1;
            }
        }
    }
    return 0;
}

void shell_render(struct ag_surface *s, const struct shell_boot_info *info, int frame_idx) {
    int32_t w, h, hx, hy, hw, hh, rx, ry, cw, ch;
    uint32_t top_c, bot_c;
    const struct art_activity *act;
    const struct airp_session *as;
    const struct bt_adapter_status *bt;

    (void)frame_idx;
    if (!s || !info) return;
    tv_set_surface(s->width, s->height);
    airstin_ui_set_scale_q16(tv_scale_q16);
    tv_launcher_ensure(info);
    airstin_ui_set_labels(tv_drawer_label);
    w = s->width;
    h = s->height;

    top_c = tv_theme_idx == 1 ? 0x000A1A36u : (tv_theme_idx == 2 ? 0x00260F24u : 0x000A0D1Cu);
    bot_c = tv_theme_idx == 1 ? 0x00143252u : (tv_theme_idx == 2 ? 0x00461834u : 0x00241438u);
    gfx_gradient_v(s, 0, 0, w, h, top_c, bot_c);

    /* Overscan-safe 10-foot top status bar. */
    gfx_draw_text_aa(s, px(48), px(26), tv_label("app"), THEME_TEXT_PRIMARY, tx(3));
    gfx_pill_badge(s, px(210), px(26), tv_label("home"), THEME_ACCENT_BLUE,
                   THEME_TEXT_PRIMARY, tx(2));
    gfx_draw_text_aa(s, px(310), px(30), tv_label("sections"), THEME_TEXT_MUTED, tx(2));
    airstin_render_status_cluster(s, w - px(16), info->arch_name, tv_cc_open);

    hx = px(48);
    hy = px(86);
    hw = w - px(96);
    hh = h / 2 - px(44);
    gfx_glass_panel(s, hx, hy, hw, hh, px(24), THEME_GLASS_DARK, 232u);
    gfx_stroke_round_rect(s, hx - px(2), hy - px(2), hw + px(4), hh + px(4), px(26),
                          THEME_ACCENT_TEAL, 220u);

    act = art_am_active();
    as = airp_session_status();
    bt = bt_core_status();
    gfx_pill_badge(s, hx + px(28), hy + px(24), tv_label("focus"), THEME_ACCENT_TEAL,
                   THEME_BG_TOP, tx(2));
    gfx_pill_badge(s, hx + px(240), hy + px(24), art_bridge_abi(), THEME_ACCENT_INDIGO,
                   THEME_TEXT_PRIMARY, tx(2));

    gfx_draw_text_aa(s, hx + px(28), hy + px(68),
                     act ? act->ui.title : g_launcher.title, THEME_TEXT_PRIMARY, tx(4));
    if (act && act->ui.line_count > 0)
        gfx_draw_text_aa(s, hx + px(28), hy + px(122), act->ui.lines[0],
                         THEME_TEXT_MUTED, tx(2));
    if (g_launcher.hint[0])
        gfx_draw_text_scaled(s, hx + px(28), hy + hh - px(46), g_launcher.hint,
                             THEME_TEXT_MUTED, tx(2));

    {
        int32_t crx = hx + hw - px(430), cry = hy + px(24);
        int32_t crw = px(402), crh = hh - px(48);
        gfx_fill_round_rect(s, crx, cry, crw, crh, px(18), THEME_GLASS_LIGHT, 200u);
        gfx_stroke_round_rect(s, crx, cry, crw, crh, px(18), THEME_GLASS_BORDER, 130u);
        gfx_pill_badge(s, crx + px(20), cry + px(18), airp_state_name(as->state),
                       THEME_ACCENT_GREEN, THEME_TEXT_PRIMARY, tx(2));
        gfx_draw_text_aa(s, crx + px(20), cry + px(62), tv_label("status_card"),
                         THEME_TEXT_PRIMARY, tx(2));
        gfx_draw_text_scaled(s, crx + px(20), cry + px(96), as->status_text,
                             THEME_ACCENT_TEAL, tx(2));
        gfx_draw_text_scaled(s, crx + px(20), cry + px(128),
                             bt->state == BT_STATE_CONNECTED ? bt->active_name
                                                             : bt->status_text,
                             THEME_TEXT_MUTED, tx(2));
    }

    tv_row_geom(w, h, &rx, &ry, &cw, &ch);
    tv_focus_target = rx + focused_tile * (cw + px(24));
    if (tv_focus_x <= 0) tv_focus_x = tv_focus_target;
    for (int i = 0; i < g_launcher.tile_count; i++) {
        const struct tv_launcher_tile *tile = &g_launcher.tiles[i];
        int32_t cx = rx + i * (cw + px(24));
        gfx_glass_panel(s, cx, ry, cw, ch, px(20), THEME_GLASS_LIGHT,
                        i == focused_tile ? 235u : 195u);
        gfx_fill_round_rect(s, cx + px(24), ry + px(24), px(56), px(56), px(14),
                            tile->accent ? tile->accent : THEME_ACCENT_BLUE, 255u);
        gfx_draw_text_aa(s, cx + px(24), ry + px(96), tile->label,
                         THEME_TEXT_PRIMARY, tx(3));
        if (tile->subtitle[0])
            gfx_draw_text_scaled(s, cx + px(24), ry + px(134), tile->subtitle,
                                 THEME_TEXT_MUTED, tx(2));
        gfx_pill_badge(s, cx + px(24), ry + ch - px(46),
                       i == focused_tile ? tv_label("focused") : tv_label("open"),
                       i == focused_tile ? THEME_ACCENT_BLUE : THEME_GLASS_DARK,
                       THEME_TEXT_PRIMARY, tx(2));
    }
    /* Spring-gliding 10-foot focus ring, in surface pixels. */
    gfx_stroke_round_rect(s, tv_focus_x - px(3), ry - px(3), cw + px(6), ch + px(6),
                          px(22), THEME_TEXT_PRIMARY, 245u);

    airstin_render_control_center(s, w, tv_cc_open, tv_cc_anim, tv_anim_on, tv_theme_idx);
}

static int32_t tv_spring(int32_t *pos, int32_t *vel, int32_t target) {
    int32_t err, acc, snap = px(2);
    if (!tv_anim_on) { *pos = target; *vel = 0; return 0; }
    err = target - *pos;
    acc = (err * 180) / 1000;
    *vel = ((*vel + acc) * 760) / 1000;
    *pos += *vel;
    if (err < snap && err > -snap && *vel < snap && *vel > -snap) {
        *pos = target;
        *vel = 0;
    }
    return (*pos != target || *vel != 0);
}

int shell_tick(void) {
    static uint32_t syn = 0;
    syn += 16u;
    return shell_tick_at(syn, 1000u);
}

int shell_tick_at(uint32_t now_ticks, uint32_t ticks_per_second) {
    int changed;

    (void)now_ticks;
    (void)ticks_per_second;
    changed = airstin_ui_tick();
    if (tv_focus_target <= 0) tv_focus_target = tv_focus_target_for(tv_surface_w, tv_surface_h);
    if (tv_spring(&tv_focus_x, &tv_focus_vx, tv_focus_target)) changed = 1;
    {
        int32_t target_cc = tv_cc_open ? px(24) : 0;
        int32_t step = px(6);
        if (step < 1) step = 1;
        if (tv_cc_anim < target_cc) {
            tv_cc_anim += step;
            if (tv_cc_anim > target_cc) tv_cc_anim = target_cc;
            changed = 1;
        } else if (tv_cc_anim > target_cc) {
            tv_cc_anim -= step;
            if (tv_cc_anim < target_cc) tv_cc_anim = target_cc;
            changed = 1;
        }
    }
    return changed;
}
