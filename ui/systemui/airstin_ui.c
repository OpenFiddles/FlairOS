/* SPDX-License-Identifier: AGPL-3.0-only */
/* AirStinUI.airp: SystemUI (Status Bar, Control Center + '>' Drill-Downs, Installer, Power) */
#include "airstin_ui.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../net/service/net_service.h"
#include "../../net/airp/airp_net.h"
#include "../../net/bt/bt_core.h"
#include "../../include/hal.h"

static struct airstin_installer g_inst;
static struct airstin_power_state g_pwr = {0, 0, 0, 0, 0, 0};
static int g_ui_inited = 0;
static enum cc_subview g_cc_view = CC_VIEW_ROOT;
static int32_t g_cc_slide = 0;
static int g_wifi_sel = 0;

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
    ui_scpy(g_inst.stage, "Ready", sizeof(g_inst.stage));
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
    ui_scpy(g_inst.stage, "Ready to install package", sizeof(g_inst.stage));
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
        if (g_inst.progress < 30)      ui_scpy(g_inst.stage, "Inspecting archive...", sizeof(g_inst.stage));
        else if (g_inst.progress < 60) ui_scpy(g_inst.stage, "Extracting DEX & .so...", sizeof(g_inst.stage));
        else if (g_inst.progress < 90) ui_scpy(g_inst.stage, "Registering Surface...", sizeof(g_inst.stage));
        else                           ui_scpy(g_inst.stage, "Finalizing install...", sizeof(g_inst.stage));
        return 1;
    }
    return changed;
}

void airstin_draw_cell_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, int active, uint32_t rgb) {
    for (int i = 0; i < 4; i++) {
        int32_t bh = 4 + i * 3;
        int32_t bx = x + i * 4;
        int32_t by = y + 14 - bh;
        uint32_t col = (active && i < bars) ? rgb : THEME_GLASS_BORDER;
        uint8_t a = (active && i < bars) ? 245u : 110u;
        gfx_fill_round_rect(s, bx, by, 3, bh, 1, col, a);
    }
}

void airstin_render_status_cluster(struct ag_surface *s, int32_t w, const char *arch_name, int cc_open) {
    const struct net_service_state *ns = net_service_state();
    const struct bt_adapter_status *bt = bt_core_status();
    gfx_pill_badge(s, w - 370, 5, arch_name, THEME_ACCENT_BLUE, THEME_TEXT_PRIMARY, 2);
    if (cc_open) {
        gfx_fill_round_rect(s, w - 246, 4, 236, 26, 8, THEME_GLASS_LIGHT, 185u);
    }
    /* Bluetooth state icon */
    gfx_draw_bt_icon(s, w - 240, 9, bt->powered, bt->state == BT_STATE_CONNECTED,
                     bt->state == BT_STATE_ERROR ? THEME_ACCENT_PINK : THEME_ACCENT_BLUE);
    /* Cellular 5G signal bars (NetService.airp) */
    airstin_draw_cell_icon(s, w - 214, 9, ns->cell_bars, ns->cell_on && !ns->airplane_mode, THEME_ACCENT_TEAL);
    gfx_draw_text_scaled(s, w - 196, 12,
                         ns->airplane_mode ? "AP" : (ns->cell_on ? (ns->cell_rat[0] ? ns->cell_rat : "5G") : "--"),
                         THEME_TEXT_MUTED, 1);
    /* Wi-Fi or Ethernet */
    if (ns->wifi_on && !ns->airplane_mode) gfx_draw_wifi_icon(s, w - 174, 7, ns->wifi_bars, THEME_ACCENT_TEAL);
    else                                   gfx_draw_eth_icon(s, w - 174, 7, THEME_ACCENT_BLUE);
    /* Battery + % (or AC when no PMIC/battery is present) */
    int show_pct = ns->battery_pct >= 0 ? ns->battery_pct : 100;
    gfx_draw_battery_icon(s, w - 144, 8, show_pct, ns->ac_power, THEME_ACCENT_GREEN);
    char bpct[8];
    if (ns->battery_pct >= 0) {
        ui_fmt_u32((uint32_t)ns->battery_pct, bpct, sizeof(bpct));
        int l = 0; while (bpct[l]) l++;
        if (l + 1 < (int)sizeof(bpct)) { bpct[l++] = '%'; bpct[l] = 0; }
    } else {
        ui_scpy(bpct, "AC", sizeof(bpct));
    }
    gfx_draw_text_aa(s, w - 108, 10, bpct, THEME_TEXT_PRIMARY, 2);
    /* Control Center toggles icon */
    gfx_fill_round_rect(s, w - 44, 8, 24, 7, 3, THEME_GLASS_BORDER, 210u);
    gfx_fill_round_rect(s, w - 30, 9, 8, 5, 2, THEME_TEXT_PRIMARY, 240u);
    gfx_fill_round_rect(s, w - 44, 19, 24, 7, 3, THEME_GLASS_BORDER, 210u);
    gfx_fill_round_rect(s, w - 42, 20, 8, 5, 2, THEME_ACCENT_TEAL, 240u);
}

static void draw_cc_conn_row(struct ag_surface *s, int32_t x, int32_t y, int32_t w,
                             int active, uint32_t col, const char *title, const char *sub) {
    gfx_fill_round_rect(s, x, y, w - 44, 38, 8, active ? col : THEME_GLASS_LIGHT, active ? 215u : 185u);
    gfx_draw_text_scaled(s, x + 10, y + 7, title, THEME_TEXT_PRIMARY, 1);
    gfx_draw_text_scaled(s, x + 10, y + 21, sub, THEME_TEXT_MUTED, 1);

    gfx_fill_round_rect(s, x + w - 40, y, 40, 38, 8, THEME_GLASS_LIGHT, 205u);
    gfx_draw_chevron_right(s, x + w - 24, y + 14, 10, THEME_TEXT_PRIMARY);
}

void airstin_render_control_center(struct ag_surface *s, int32_t w, int cc_open, int cc_anim,
                                   int anim_on, int theme_idx) {
    if (!cc_open && cc_anim <= 0) return;
    const struct net_service_state *ns = net_service_state();
    const struct airp_session *as = airp_session_status();
    const struct bt_adapter_status *bt = bt_core_status();
    int32_t cc_w = 280, cc_h = 336, cc_x = w - cc_w - 12 + (anim_on ? g_cc_slide : 0);
    int32_t cc_y = 40 - (24 - cc_anim);
    gfx_glass_panel(s, cc_x, cc_y, cc_w, cc_h, 14, THEME_GLASS_DARK, 244u);

    if (g_cc_view != CC_VIEW_ROOT) {
        /* Shared Drill-Down Back Header */
        gfx_fill_round_rect(s, cc_x + 12, cc_y + 10, 68, 26, 7, THEME_GLASS_LIGHT, 215u);
        gfx_draw_chevron_left(s, cc_x + 20, cc_y + 18, 10, THEME_TEXT_PRIMARY);
        gfx_draw_text_scaled(s, cc_x + 34, cc_y + 19, "Back", THEME_TEXT_PRIMARY, 1);
        const char *htitle = (g_cc_view == CC_VIEW_WIFI) ? "Wi-Fi" :
                             (g_cc_view == CC_VIEW_CELL ? "Cellular" : "Bluetooth");
        gfx_draw_text_aa(s, cc_x + 92, cc_y + 15, htitle, THEME_TEXT_PRIMARY, 2);

        if (g_cc_view == CC_VIEW_WIFI) {
            int w_on = ns->wifi_on && !ns->airplane_mode;
            gfx_fill_round_rect(s, cc_x + cc_w - 66, cc_y + 10, 54, 26, 7,
                                w_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 225u);
            gfx_draw_text_scaled(s, cc_x + cc_w - 50, cc_y + 19, w_on ? "ON" : "OFF", THEME_TEXT_PRIMARY, 1);

            gfx_fill_round_rect(s, cc_x + 12, cc_y + 46, cc_w - 24, 62, 8, THEME_GLASS_LIGHT, 180u);
            gfx_draw_text_scaled(s, cc_x + 20, cc_y + 56,
                                 w_on ? (ns->ssid[0] ? ns->ssid : "Wi-Fi Connected") : "No Wi-Fi Radio",
                                 w_on ? THEME_ACCENT_TEAL : THEME_TEXT_MUTED, 1);
            gfx_draw_text_scaled(s, cc_x + 20, cc_y + 74,
                                 w_on ? (ns->wifi_ip[0] ? ns->wifi_ip : "DHCP Pending") : "Ethernet / Hardware NIC",
                                 THEME_TEXT_MUTED, 1);
            gfx_draw_text_scaled(s, cc_x + 20, cc_y + 92,
                                 ns->tel_source[0] ? ns->tel_source : "hardware",
                                 THEME_TEXT_MUTED, 1);
        } else if (g_cc_view == CC_VIEW_CELL) {
            int c_on = ns->cell_on && !ns->airplane_mode;
            gfx_fill_round_rect(s, cc_x + cc_w - 66, cc_y + 10, 54, 26, 7,
                                c_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 225u);
            gfx_draw_text_scaled(s, cc_x + cc_w - 50, cc_y + 19, c_on ? "ON" : "OFF", THEME_TEXT_PRIMARY, 1);

            gfx_fill_round_rect(s, cc_x + 12, cc_y + 48, cc_w - 24, 90, 8, THEME_GLASS_LIGHT, 185u);
            gfx_draw_text_aa(s, cc_x + 22, cc_y + 60,
                             ns->carrier[0] ? ns->carrier : "No Cellular Modem",
                             THEME_TEXT_PRIMARY, 2);
            gfx_draw_text_scaled(s, cc_x + 22, cc_y + 88,
                                 c_on && ns->cell_rat[0] ? ns->cell_rat : "--",
                                 THEME_ACCENT_TEAL, 1);
            gfx_draw_text_scaled(s, cc_x + 76, cc_y + 88,
                                 c_on ? (ns->cell_roaming ? "Roaming" : "Home Network") : "No Hardware SIM",
                                 THEME_TEXT_MUTED, 1);
            airstin_draw_cell_icon(s, cc_x + 22, cc_y + 108, ns->cell_bars, c_on, THEME_ACCENT_TEAL);
        } else {
            /* CC_VIEW_BT */
            gfx_fill_round_rect(s, cc_x + cc_w - 66, cc_y + 10, 54, 26, 7,
                                bt->powered ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 225u);
            gfx_draw_text_scaled(s, cc_x + cc_w - 50, cc_y + 19, bt->powered ? "ON" : "OFF", THEME_TEXT_PRIMARY, 1);

            gfx_fill_round_rect(s, cc_x + 12, cc_y + 46, 124, 28, 7, THEME_ACCENT_INDIGO, 215u);
            gfx_draw_text_scaled(s, cc_x + 26, cc_y + 56, bt->scanning ? "Scanning..." : "Scan Devices", THEME_TEXT_PRIMARY, 1);
            gfx_fill_round_rect(s, cc_x + 144, cc_y + 46, 124, 28, 7,
                                bt->state == BT_STATE_ERROR ? THEME_ACCENT_PINK : THEME_GLASS_LIGHT, 200u);
            gfx_draw_text_scaled(s, cc_x + 162, cc_y + 56, bt->state == BT_STATE_ERROR ? "Recover" : "Reset HCI", THEME_TEXT_PRIMARY, 1);
            gfx_draw_text_scaled(s, cc_x + 14, cc_y + 82, bt->status_text, THEME_ACCENT_TEAL, 1);

            int cnt = bt_core_device_count();
            if (cnt == 0) {
                gfx_fill_round_rect(s, cc_x + 12, cc_y + 102, cc_w - 24, 42, 8, THEME_GLASS_LIGHT, 165u);
                gfx_draw_text_scaled(s, cc_x + 22, cc_y + 118, bt->powered ? "Tap Scan Devices" : "Turn Bluetooth ON", THEME_TEXT_MUTED, 1);
            }
            for (int i = 0; i < cnt && i < 4; i++) {
                const struct bt_device *d = bt_core_device_get(i);
                if (!d) continue;
                int32_t ry = cc_y + 102 + i * 54;
                gfx_fill_round_rect(s, cc_x + 12, ry, cc_w - 24, 46, 8,
                                    d->connected ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, d->connected ? 215u : 175u);
                gfx_draw_text_scaled(s, cc_x + 20, ry + 10, d->name, THEME_TEXT_PRIMARY, 1);
                gfx_draw_text_scaled(s, cc_x + 20, ry + 26,
                                     d->connected ? "Connected" : (d->paired ? "Paired - Connect" : "Tap to Pair"),
                                     THEME_TEXT_MUTED, 1);
            }
        }
        return;
    }

    /* CC_VIEW_ROOT */
    gfx_draw_text_aa(s, cc_x + 14, cc_y + 12, "AirStinUI Control", THEME_TEXT_PRIMARY, 2);

    draw_cc_conn_row(s, cc_x + 12, cc_y + 42, cc_w - 24,
                     ns->wifi_on && !ns->airplane_mode, THEME_ACCENT_BLUE,
                     (ns->wifi_on && !ns->airplane_mode) ? "Wi-Fi: ON" : "Wi-Fi: OFF",
                     (ns->wifi_on && !ns->airplane_mode) ? (ns->ssid[0] ? ns->ssid : "Connected") : "No Wi-Fi");

    draw_cc_conn_row(s, cc_x + 12, cc_y + 84, cc_w - 24,
                     ns->cell_on && !ns->airplane_mode, THEME_ACCENT_TEAL,
                     (ns->cell_on && !ns->airplane_mode) ? "Cellular: ON" : "Cellular: OFF",
                     (ns->cell_on && !ns->airplane_mode) ? (ns->carrier[0] ? ns->carrier : "Active") : "No Modem");

    draw_cc_conn_row(s, cc_x + 12, cc_y + 126, cc_w - 24,
                     bt->powered, THEME_ACCENT_INDIGO,
                     bt->powered ? "Bluetooth: ON" : (bt->state == BT_STATE_UNAVAILABLE ? "Bluetooth: NO HW" : "Bluetooth: OFF"),
                     bt->state == BT_STATE_CONNECTED ? bt->active_name : bt->status_text);

    /* Airplane & Motion split */
    int32_t hw = (cc_w - 30) / 2;
    gfx_fill_round_rect(s, cc_x + 12, cc_y + 170, hw, 32, 8,
                        ns->airplane_mode ? THEME_ACCENT_ORANGE : THEME_GLASS_LIGHT, 205u);
    gfx_draw_text_scaled(s, cc_x + 20, cc_y + 182, ns->airplane_mode ? "Airplane: ON" : "Airplane: OFF", THEME_TEXT_PRIMARY, 1);
    gfx_fill_round_rect(s, cc_x + 18 + hw, cc_y + 170, hw, 32, 8,
                        anim_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 205u);
    gfx_draw_text_scaled(s, cc_x + 26 + hw, cc_y + 182, anim_on ? "Motion: ON" : "Motion: OFF", THEME_TEXT_PRIMARY, 1);

    /* Theme swatches */
    int32_t sw = (cc_w - 36) / 3;
    const char *cc_tn[3] = {"Indigo", "Blue", "Pink"};
    uint32_t cc_tc[3] = {THEME_ACCENT_INDIGO, THEME_ACCENT_BLUE, THEME_ACCENT_PINK};
    for (int t = 0; t < 3; t++) {
        gfx_fill_round_rect(s, cc_x + 12 + t * (sw + 6), cc_y + 208, sw, 30, 8, cc_tc[t],
                            theme_idx == t ? 250u : 170u);
        gfx_draw_text_scaled(s, cc_x + 18 + t * (sw + 6), cc_y + 219, cc_tn[t], THEME_TEXT_PRIMARY, 1);
    }

    /* Quick Actions Tray: Search, AirP Cast, Power */
    gfx_fill_round_rect(s, cc_x + 12, cc_y + 244, 80, 34, 8, THEME_GLASS_LIGHT, 210u);
    gfx_draw_text_scaled(s, cc_x + 26, cc_y + 257, "Search", THEME_TEXT_PRIMARY, 1);

    int airp_busy = (as->state == AIRP_STATE_DISCOVERING || as->state == AIRP_STATE_CONNECTED || as->state == AIRP_STATE_STREAMING);
    gfx_fill_round_rect(s, cc_x + 98, cc_y + 244, 92, 34, 8,
                        airp_busy ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 215u);
    gfx_draw_text_scaled(s, cc_x + 108, cc_y + 257,
                         as->state == AIRP_STATE_IDLE ? "AirP Cast" : airp_state_name(as->state),
                         THEME_TEXT_PRIMARY, 1);

    gfx_fill_round_rect(s, cc_x + 196, cc_y + 244, 72, 34, 8,
                        g_pwr.modal_open ? THEME_ACCENT_PINK : THEME_ACCENT_ORANGE, 225u);
    gfx_draw_text_scaled(s, cc_x + 212, cc_y + 257, "Power", THEME_TEXT_PRIMARY, 1);

    /* Power + Hardware Telemetry Footer */
    gfx_fill_round_rect(s, cc_x + 12, cc_y + 286, cc_w - 24, 40, 8, THEME_GLASS_LIGHT, 185u);
    gfx_draw_battery_icon(s, cc_x + 20, cc_y + 298, ns->battery_pct >= 0 ? ns->battery_pct : 100, ns->ac_power, THEME_ACCENT_GREEN);
    gfx_draw_text_scaled(s, cc_x + 56, cc_y + 294, ns->battery_pct >= 0 ? (ns->ac_power ? "Power: AC" : "Power: Battery") : "Power: AC (No PMIC)", THEME_TEXT_PRIMARY, 1);
    gfx_draw_text_scaled(s, cc_x + 56, cc_y + 308,
                         ns->tel_source[0] ? ns->tel_source : "hardware",
                         THEME_TEXT_MUTED, 1);
}

int airstin_cc_handle_click(int32_t mx, int32_t my, int32_t w,
                            int *anim_on, int *theme_idx,
                            int *spotlight_open, int *cc_open) {
    int32_t cc_w = 280, cc_h = 336, cc_x = w - cc_w - 12, cc_y = 40;
    if (!cc_hit(mx, my, cc_x, cc_y, cc_w, cc_h)) return 0;

    if (g_cc_view != CC_VIEW_ROOT) {
        if (cc_hit(mx, my, cc_x + 12, cc_y + 10, 68, 26)) { airstin_cc_back(); return 1; }
        if (g_cc_view == CC_VIEW_WIFI) {
            if (cc_hit(mx, my, cc_x + cc_w - 66, cc_y + 10, 54, 26)) { net_service_toggle_wifi(); return 1; }
            return 1;
        }
        if (g_cc_view == CC_VIEW_CELL) {
            if (cc_hit(mx, my, cc_x + cc_w - 66, cc_y + 10, 54, 26)) { net_service_toggle_cell(); return 1; }
            return 1;
        }
        /* CC_VIEW_BT */
        if (cc_hit(mx, my, cc_x + cc_w - 66, cc_y + 10, 54, 26)) { (void)bt_core_toggle_power(); return 1; }
        if (cc_hit(mx, my, cc_x + 12, cc_y + 46, 124, 28)) {
            if (!bt_core_status()->powered) (void)bt_core_power(1);
            (void)bt_core_scan_start();
            return 1;
        }
        if (cc_hit(mx, my, cc_x + 144, cc_y + 46, 124, 28)) { bt_core_recover(); return 1; }
        int cnt = bt_core_device_count();
        for (int i = 0; i < cnt && i < 4; i++) {
            if (cc_hit(mx, my, cc_x + 12, cc_y + 102 + i * 54, cc_w - 24, 46)) {
                const struct bt_device *d = bt_core_device_get(i);
                if (d && d->connected) (void)bt_core_disconnect(i);
                else (void)bt_core_connect(i);
                return 1;
            }
        }
        return 1;
    }

    /* CC_VIEW_ROOT: Toggle (left) vs '>' Drill-Down (right) */
    int32_t rw = cc_w - 24;
    if (cc_hit(mx, my, cc_x + 12, cc_y + 42, rw - 44, 38)) { net_service_toggle_wifi(); return 1; }
    if (cc_hit(mx, my, cc_x + 12 + rw - 40, cc_y + 42, 40, 38)) { airstin_cc_set_view(CC_VIEW_WIFI); return 1; }

    if (cc_hit(mx, my, cc_x + 12, cc_y + 84, rw - 44, 38)) { net_service_toggle_cell(); return 1; }
    if (cc_hit(mx, my, cc_x + 12 + rw - 40, cc_y + 84, 40, 38)) { airstin_cc_set_view(CC_VIEW_CELL); return 1; }

    if (cc_hit(mx, my, cc_x + 12, cc_y + 126, rw - 44, 38)) { (void)bt_core_toggle_power(); return 1; }
    if (cc_hit(mx, my, cc_x + 12 + rw - 40, cc_y + 126, 40, 38)) { airstin_cc_set_view(CC_VIEW_BT); return 1; }

    int32_t hw = (cc_w - 30) / 2;
    if (cc_hit(mx, my, cc_x + 12, cc_y + 170, hw, 32)) { net_service_toggle_airplane(); return 1; }
    if (cc_hit(mx, my, cc_x + 18 + hw, cc_y + 170, hw, 32)) { if (anim_on) *anim_on = !(*anim_on); return 1; }

    int32_t sw = (cc_w - 36) / 3;
    for (int t = 0; t < 3; t++) {
        if (cc_hit(mx, my, cc_x + 12 + t * (sw + 6), cc_y + 208, sw, 30)) {
            if (theme_idx) *theme_idx = t;
            return 1;
        }
    }
    if (cc_hit(mx, my, cc_x + 12, cc_y + 244, 80, 34)) {
        if (spotlight_open) *spotlight_open = 1;
        if (cc_open) *cc_open = 0;
        return 1;
    }
    if (cc_hit(mx, my, cc_x + 98, cc_y + 244, 92, 34)) {
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
    if (cc_hit(mx, my, cc_x + 196, cc_y + 244, 72, 34)) {
        if (cc_open) *cc_open = 0;
        airstin_power_open_modal();
        return 1;
    }
    return 1;
}

void airstin_render_installer(struct ag_surface *s, int32_t w, int32_t h) {
    if (!g_inst.active) return;
    int32_t dw0 = 400, dh0 = 236, dx0 = (w - dw0) / 2, dy0 = (h - dh0) / 2, bw = (dw0 - 44) / 2;
    gfx_fill_rect_alpha(s, 0, 0, w, h, 0x00050810u, 155u);
    gfx_glass_panel(s, dx0, dy0, dw0, dh0, 16, THEME_GLASS_DARK, 245u);
    gfx_draw_text_aa(s, dx0 + 20, dy0 + 18, "Android Package Installer", THEME_TEXT_PRIMARY, 2);
    gfx_pill_badge(s, dx0 + 20, dy0 + 50,
                   g_inst.info.is_system_app ? "SYSTEM APP" : "USER TEST APP",
                   g_inst.info.is_system_app ? THEME_ACCENT_BLUE : THEME_ACCENT_PINK, THEME_TEXT_PRIMARY, 2);
    gfx_draw_text_scaled(s, dx0 + 20, dy0 + 84, g_inst.info.app_label[0] ? g_inst.info.app_label : g_inst.path, THEME_TEXT_PRIMARY, 2);
    gfx_draw_text_scaled(s, dx0 + 20, dy0 + 108, g_inst.info.package_name, THEME_TEXT_MUTED, 1);

    gfx_draw_text_scaled(s, dx0 + 20, dy0 + 132, g_inst.stage, THEME_ACCENT_TEAL, 1);
    int32_t pb_w = dw0 - 40;
    gfx_fill_round_rect(s, dx0 + 20, dy0 + 150, pb_w, 16, 8, THEME_GLASS_LIGHT, 180u);
    int32_t fill_w = (pb_w * g_inst.progress) / 100;
    if (fill_w > 4) gfx_fill_round_rect(s, dx0 + 20, dy0 + 150, fill_w, 16, 8, THEME_ACCENT_GREEN, 240u);

    gfx_fill_round_rect(s, dx0 + 16, dy0 + dh0 - 42, bw, 28, 8, THEME_GLASS_LIGHT, 230u);
    gfx_draw_text_scaled(s, dx0 + 34, dy0 + dh0 - 35, "Cancel", THEME_TEXT_PRIMARY, 2);
    gfx_fill_round_rect(s, dx0 + 28 + bw, dy0 + dh0 - 42, bw, 28, 8, THEME_ACCENT_BLUE, 245u);
    gfx_draw_text_scaled(s, dx0 + 36 + bw, dy0 + dh0 - 35,
                         g_inst.installing ? "Installing..." : "Install & Open", THEME_TEXT_PRIMARY, 2);
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
    if (!g_pwr.modal_open) return;
    int32_t mw = 380, mh = 188, mx = (w - mw) / 2, my = (h - mh) / 2;
    gfx_fill_rect_alpha(s, 0, 0, w, h, 0x00050810u, 155u);
    gfx_glass_panel(s, mx, my, mw, mh, 16, THEME_GLASS_DARK, 246u);
    gfx_draw_text_aa(s, mx + 20, my + 18, "Power & Shutdown", THEME_TEXT_PRIMARY, 2);
    if (g_pwr.shutting_down) {
        gfx_draw_text_scaled(s, mx + 20, my + 62,
                             g_pwr.forced ? "Forced hardware poweroff..." : "Graceful shutdown: syncing VFS...",
                             THEME_ACCENT_ORANGE, 2);
        gfx_fill_round_rect(s, mx + 20, my + 104, mw - 40, 14, 7, THEME_GLASS_LIGHT, 180u);
        int32_t prog = (8 - g_pwr.countdown) * (mw - 40) / 8;
        if (prog > 4) gfx_fill_round_rect(s, mx + 20, my + 104, prog, 14, 7, THEME_ACCENT_PINK, 240u);
        return;
    }
    gfx_draw_text_scaled(s, mx + 20, my + 58,
                         g_pwr.taps == 1 ? "Armed (1/2) - Tap Shutdown again!" : "Double-tap Shutdown to power off",
                         g_pwr.taps == 1 ? THEME_ACCENT_ORANGE : THEME_TEXT_MUTED, 1);
    gfx_draw_text_scaled(s, mx + 20, my + 80, "Hold hardware Power key to force off", THEME_TEXT_MUTED, 1);

    int32_t bw = (mw - 52) / 2, by = my + mh - 46;
    gfx_fill_round_rect(s, mx + 18, by, bw, 32, 8, THEME_GLASS_LIGHT, 225u);
    gfx_draw_text_scaled(s, mx + 48, by + 9, "Cancel", THEME_TEXT_PRIMARY, 2);

    gfx_fill_round_rect(s, mx + 34 + bw, by, bw, 32, 8,
                        g_pwr.taps == 1 ? THEME_ACCENT_ORANGE : THEME_ACCENT_PINK, 240u);
    gfx_draw_text_scaled(s, mx + 44 + bw, by + 9,
                         g_pwr.taps == 1 ? "Confirm 2/2" : "Shutdown", THEME_TEXT_PRIMARY, 2);
}
