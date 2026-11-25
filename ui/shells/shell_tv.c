/* SPDX-License-Identifier: AGPL-3.0-only */
/* Modular 10-foot tvOS-inspired Interactive TV Shell (D-pad Focus Spring + AirStinUI Drawer) */
#include "shell_common.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../runtime/art/art_bridge.h"
#include "../../net/airp/airp_net.h"
#include "../../net/bt/bt_core.h"
#include "../systemui/airstin_ui.h"

static int focused_card = 0;
static int tv_cc_open = 0;
static int tv_anim_on = 1;
static int tv_theme_idx = 0;
static int32_t tv_cc_anim = 0;
static int32_t tv_focus_x = 48;
static int32_t tv_focus_vx = 0;

static int32_t tv_spring(int32_t *pos, int32_t *vel, int32_t target) {
    if (!tv_anim_on) { *pos = target; *vel = 0; return 0; }
    int32_t err = target - *pos;
    int32_t acc = (err * 180) / 1000;
    *vel = ((*vel + acc) * 760) / 1000;
    *pos += *vel;
    if (err < 2 && err > -2 && *vel < 2 && *vel > -2) { *pos = target; *vel = 0; }
    return (*pos != target || *vel != 0);
}

int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h) {
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
    if (in->key == INPUT64_KEY_LEFT && focused_card > 0) { focused_card--; return 1; }
    if (in->key == INPUT64_KEY_RIGHT && focused_card < 2) { focused_card++; return 1; }
    if (in->key == INPUT64_KEY_ENTER) {
        if (tv_cc_open) {
            if (airstin_cc_view() == CC_VIEW_ROOT) airstin_cc_set_view(CC_VIEW_BT);
            else {
                if (!bt_core_status()->powered) (void)bt_core_power(1);
                (void)bt_core_scan_start();
            }
            return 1;
        }
        if (focused_card == 0) (void)art_am_launch(0);
        else if (focused_card == 1) (void)art_am_launch(1);
        else (void)airp_start_stream(AIRP_PAY_SCREEN, "AirP TV Receiver", 65536u);
        return 1;
    }
    if (in->clicked) {
        if (tv_cc_open) {
            int dummy_spot = 0;
            if (airstin_cc_handle_click(in->mouse_x, in->mouse_y, w, &tv_anim_on, &tv_theme_idx, &dummy_spot, &tv_cc_open))
                return 1;
        }
        if (in->mouse_y < 64 && in->mouse_x >= w - 280) {
            tv_cc_open = !tv_cc_open;
            if (!tv_cc_open) airstin_cc_set_view(CC_VIEW_ROOT);
            return 1;
        }
        if (tv_cc_open) {
            tv_cc_open = 0;
            airstin_cc_set_view(CC_VIEW_ROOT);
            return 1;
        }
        int32_t hx = 48, hy = 86, hw = w - 96, hh = h / 2 - 44;
        int32_t ry = hy + hh + 28, cw = (hw - 48) / 3, ch = h - ry - 48;
        for (int i = 0; i < 3; i++) {
            int32_t cx = hx + i * (cw + 24);
            if (in->mouse_x >= cx && in->mouse_x < cx + cw &&
                in->mouse_y >= ry && in->mouse_y < ry + ch) {
                focused_card = i;
                if (i == 0) (void)art_am_launch(0);
                else if (i == 1) (void)art_am_launch(1);
                else (void)airp_start_stream(AIRP_PAY_SCREEN, "AirP TV Receiver", 65536u);
                return 1;
            }
        }
    }
    return 0;
}

void shell_render(struct ag_surface *s, const struct shell_boot_info *info, int frame_idx) {
    (void)frame_idx;
    if (!s || !info) return;
    int32_t w = s->width, h = s->height;

    uint32_t top_c = tv_theme_idx == 1 ? 0x000A1A36u : (tv_theme_idx == 2 ? 0x00260F24u : 0x000A0D1Cu);
    uint32_t bot_c = tv_theme_idx == 1 ? 0x00143252u : (tv_theme_idx == 2 ? 0x00461834u : 0x00241438u);
    gfx_gradient_v(s, 0, 0, w, h, top_c, bot_c);

    /* Overscan-safe 10-foot Top Status Bar */
    gfx_draw_text_aa(s, 48, 26, "FlairTV", THEME_TEXT_PRIMARY, 3);
    gfx_pill_badge(s, 210, 26, "HOME", THEME_ACCENT_BLUE, THEME_TEXT_PRIMARY, 2);
    gfx_draw_text_aa(s, 310, 30, "Apps   Android   AirP   Status", THEME_TEXT_MUTED, 2);
    airstin_render_status_cluster(s, w - 16, info->arch_name, tv_cc_open);

    int32_t hx = 48, hy = 86, hw = w - 96, hh = h / 2 - 44;
    gfx_glass_panel(s, hx, hy, hw, hh, 24, THEME_GLASS_DARK, 232u);
    gfx_stroke_round_rect(s, hx - 2, hy - 2, hw + 4, hh + 4, 26, THEME_ACCENT_TEAL, 220u);

    const struct art_activity *act = art_am_active();
    const struct airp_session *as = airp_session_status();
    const struct bt_adapter_status *bt = bt_core_status();
    gfx_pill_badge(s, hx + 28, hy + 24, "10-FOOT FOCUS", THEME_ACCENT_TEAL, THEME_BG_TOP, 2);
    gfx_pill_badge(s, hx + 240, hy + 24, art_bridge_abi(), THEME_ACCENT_INDIGO, THEME_TEXT_PRIMARY, 2);

    gfx_draw_text_aa(s, hx + 28, hy + 68,
                     act ? act->ui.title : "FlairTV Living Room",
                     THEME_TEXT_PRIMARY, 4);
    if (act && act->ui.line_count > 0)
        gfx_draw_text_aa(s, hx + 28, hy + 122, act->ui.lines[0], THEME_TEXT_MUTED, 2);
    gfx_draw_text_scaled(s, hx + 28, hy + hh - 46,
                         "UP: Control Drawer  |  LEFT/RIGHT: Focus  |  ENTER: Launch",
                         THEME_TEXT_MUTED, 2);

    int32_t rx = hx + hw - 430, ry0 = hy + 24, rw = 402, rh = hh - 48;
    gfx_fill_round_rect(s, rx, ry0, rw, rh, 18, THEME_GLASS_LIGHT, 200u);
    gfx_stroke_round_rect(s, rx, ry0, rw, rh, 18, THEME_GLASS_BORDER, 130u);
    gfx_pill_badge(s, rx + 20, ry0 + 18, airp_state_name(as->state), THEME_ACCENT_GREEN, THEME_TEXT_PRIMARY, 2);
    gfx_draw_text_aa(s, rx + 20, ry0 + 62, "AIRP + BT AUDIO", THEME_TEXT_PRIMARY, 2);
    gfx_draw_text_scaled(s, rx + 20, ry0 + 96, as->status_text, THEME_ACCENT_TEAL, 2);
    gfx_draw_text_scaled(s, rx + 20, ry0 + 128,
                         bt->state == BT_STATE_CONNECTED ? bt->active_name : bt->status_text,
                         THEME_TEXT_MUTED, 2);

    int32_t ry = hy + hh + 28, cw = (hw - 48) / 3, ch = h - ry - 48;
    int32_t target_fx = hx + focused_card * (cw + 24);
    const char *cards[3] = {"Hello World", "DroidCalc", "AirP Cast"};
    const char *sub[3]   = {"Android APK", "Dalvik Math", "Mirror Screen"};
    uint32_t cbg[3]      = {THEME_ACCENT_PINK, THEME_ACCENT_ORANGE, THEME_ACCENT_BLUE};
    for (int i = 0; i < 3; i++) {
        int32_t cx = hx + i * (cw + 24);
        gfx_glass_panel(s, cx, ry, cw, ch, 20, THEME_GLASS_LIGHT, i == focused_card ? 235u : 195u);
        gfx_fill_round_rect(s, cx + 24, ry + 24, 56, 56, 14, cbg[i], 255u);
        gfx_draw_text_aa(s, cx + 24, ry + 96, cards[i], THEME_TEXT_PRIMARY, 3);
        gfx_draw_text_scaled(s, cx + 24, ry + 134, sub[i], THEME_TEXT_MUTED, 2);
        gfx_pill_badge(s, cx + 24, ry + ch - 46, i == focused_card ? "FOCUSED" : "OPEN",
                       i == focused_card ? THEME_ACCENT_BLUE : THEME_GLASS_DARK, THEME_TEXT_PRIMARY, 2);
    }
    /* Smooth Spring-Gliding 10-Foot Focus Ring */
    gfx_stroke_round_rect(s, tv_focus_x - 3, ry - 3, cw + 6, ch + 6, 22, THEME_TEXT_PRIMARY, 245u);
    if (tv_focus_x == 0) tv_focus_x = target_fx;

    airstin_render_control_center(s, w, tv_cc_open, tv_cc_anim, tv_anim_on, tv_theme_idx);
}

#ifndef PRESET_W
#define PRESET_W 1280
#endif

int shell_tick(void) {
    static uint32_t syn = 0;
    syn += 16u;
    return shell_tick_at(syn, 1000u);
}

int shell_tick_at(uint32_t now_ticks, uint32_t ticks_per_second) {
    (void)now_ticks; (void)ticks_per_second;
    int changed = airstin_ui_tick();
    int32_t hw = PRESET_W - 96, cw = (hw - 48) / 3;
    int32_t target_fx = 48 + focused_card * (cw + 24);
    if (tv_spring(&tv_focus_x, &tv_focus_vx, target_fx)) changed = 1;
    int32_t target_cc = tv_cc_open ? 24 : 0;
    if (tv_cc_anim < target_cc) { tv_cc_anim += 6; if (tv_cc_anim > target_cc) tv_cc_anim = target_cc; changed = 1; }
    else if (tv_cc_anim > target_cc) { tv_cc_anim -= 6; if (tv_cc_anim < target_cc) tv_cc_anim = target_cc; changed = 1; }
    return changed;
}
