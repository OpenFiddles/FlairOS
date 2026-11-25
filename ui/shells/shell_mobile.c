/* SPDX-License-Identifier: AGPL-3.0-only */
/* Real Full-Screen Interactive Mobile OS Shell (iOS/Android hybrid, no mockups) */
#include "shell_common.h"
#include "flair_browser.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../include/anim_icons_gen.h"
#include "../../include/vfs.h"
#include "../../include/kmem64.h"
#include "../../runtime/art/art_bridge.h"
#include "../../runtime/art/surface_flinger.h"
#include "../../net/service/net_service.h"
#include "../../ui/systemui/airstin_ui.h"

enum mob_app_id {
    MOB_HOME     = 0,
    MOB_BROWSER  = 1,
    MOB_FILES    = 2,
    MOB_CALC     = 3,
    MOB_NOTES    = 4,
    MOB_ACTIVITY = 5,
    MOB_SETTINGS = 6
};

static int mob_screen = MOB_HOME;
static int mob_cc_open = 0;
static int mob_theme = 0;
static int mob_anim = 1;
static int32_t mob_slide_y = 0;

/* Files state */
static char mob_fpath[VFS_PATH_MAX] = "/apps";
static int mob_fsel = 0;

/* Notes state */
static char mob_npath[VFS_PATH_MAX] = "/home/NOTE.TXT";
static char mob_nbuf[512];
static int mob_nlen = 0;
static int mob_nloaded = 0;
static char mob_nstatus[32] = "Ready";

/* Calc state */
static int64_t mc_acc = 0, mc_cur = 0;
static char mc_op = 0;
static int mc_fresh = 1, mc_err = 0;

/* Wallpaper cache for fast 60fps */
static uint32_t *m_wp_cache = 0;
static int m_wp_theme = -1;
static int32_t m_wp_w = 0, m_wp_h = 0;

static void m_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}
static int m_slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int m_seq(const char *a, const char *b) { while (*a && *b) { if (*a++ != *b++) return 0; } return *a == *b; }
static void m_fmt_i64(int64_t v, char *out, int cap) {
    char tmp[24]; int n = 0, o = 0; uint64_t uv;
    if (v < 0) { if (o + 1 < cap) out[o++] = '-'; uv = (uint64_t)(-(v + 1)) + 1u; }
    else uv = (uint64_t)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0 && n < 22) { tmp[n++] = (char)('0' + (uv % 10u)); uv /= 10u; }
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}
static int hit_rect(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void mob_open_app(int app) {
    mob_screen = app;
    mob_cc_open = 0;
    mob_slide_y = mob_anim ? 36 : 0;
}

static void mob_notes_load(const char *path) {
    m_scpy(mob_npath, path, sizeof(mob_npath));
    int fd = vfs_open(path, VFS_O_RDONLY);
    mob_nlen = 0;
    if (fd >= 0) {
        int n = vfs_read(fd, mob_nbuf, (uint32_t)(sizeof(mob_nbuf) - 1));
        vfs_close(fd);
        if (n > 0) mob_nlen = n;
    }
    mob_nbuf[mob_nlen] = 0;
    mob_nloaded = 1;
    m_scpy(mob_nstatus, "Loaded", sizeof(mob_nstatus));
}

static void mob_notes_save(void) {
    int fd = vfs_open(mob_npath, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd >= 0) {
        (void)vfs_write(fd, mob_nbuf, (uint32_t)mob_nlen);
        vfs_close(fd);
        m_scpy(mob_nstatus, "Saved to VFS", sizeof(mob_nstatus));
    }
}

static void mc_apply(void) {
    if (mc_op == '+') mc_acc += mc_cur;
    else if (mc_op == '-') mc_acc -= mc_cur;
    else if (mc_op == '*') mc_acc *= mc_cur;
    else if (mc_op == '/') { if (mc_cur == 0) { mc_err = 1; mc_acc = 0; } else mc_acc /= mc_cur; }
    else mc_acc = mc_cur;
    mc_cur = mc_acc;
}

static void mc_press(char ch) {
    if (ch == 'C' || ch == 'c') { mc_acc = 0; mc_cur = 0; mc_op = 0; mc_fresh = 1; mc_err = 0; return; }
    if (mc_err) { mc_acc = 0; mc_cur = 0; mc_op = 0; mc_fresh = 1; mc_err = 0; }
    if (ch >= '0' && ch <= '9') {
        int64_t d = (int64_t)(ch - '0');
        if (mc_fresh) { mc_cur = d; mc_fresh = 0; }
        else if (mc_cur <= 999999999LL && mc_cur >= -999999999LL) mc_cur = mc_cur >= 0 ? mc_cur * 10 + d : mc_cur * 10 - d;
        return;
    }
    if (ch == 'n') { mc_cur = -mc_cur; return; }
    if (ch == '%') { mc_cur /= 100; return; }
    if (ch == '\b') { if (!mc_fresh) mc_cur /= 10; return; }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
        if (!mc_fresh && mc_op) mc_apply(); else mc_acc = mc_cur;
        mc_op = ch; mc_fresh = 1; return;
    }
    if (ch == '=' || ch == '\n') { if (mc_op) mc_apply(); mc_op = 0; mc_fresh = 1; }
}

int shell_tick(void) {
    return shell_tick_at(0u, 0u);
}

int shell_tick_at(uint32_t now_ticks, uint32_t ticks_per_second) {
    (void)now_ticks; (void)ticks_per_second;
    int changed = 0;
    net_service_sync();
    if (mob_slide_y > 0) {
        mob_slide_y = mob_anim ? (mob_slide_y > 9 ? mob_slide_y - 9 : 0) : 0;
        changed = 1;
    }
    return changed;
}

int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h) {
    if (!in) return 0;
    if (!mob_nloaded) mob_notes_load("/home/NOTE.TXT");
    int32_t mx = in->mouse_x, my = in->mouse_y;

    if (in->clicked) {
        /* Top status bar: tap right side for Control Center */
        if (my < 44) {
            if (mx >= w - 180) { mob_cc_open = !mob_cc_open; return 1; }
            return 0;
        }
        /* Mobile Control Center overlay */
        if (mob_cc_open) {
            int32_t cw = w - 32, cx = 16, cy = 50;
            if (hit_rect(mx, my, cx, cy, cw, 230)) {
                int32_t hw = (cw - 36) / 2;
                if (hit_rect(mx, my, cx + 12, cy + 44, hw, 40)) { net_service_toggle_wifi(); return 1; }
                if (hit_rect(mx, my, cx + 24 + hw, cy + 44, hw, 40)) { net_service_toggle_cell(); return 1; }
                if (hit_rect(mx, my, cx + 12, cy + 94, cw - 24, 38)) { net_service_toggle_airplane(); return 1; }
                if (hit_rect(mx, my, cx + 12, cy + 140, cw - 24, 38)) { mob_anim = !mob_anim; return 1; }
                int32_t sw = (cw - 36) / 3;
                for (int t = 0; t < 3; t++) {
                    if (hit_rect(mx, my, cx + 12 + t * (sw + 6), cy + 186, sw, 32)) { mob_theme = t; return 1; }
                }
                return 1;
            }
            mob_cc_open = 0;
            return 1;
        }

        /* Home Indicator bar at very bottom returns Home from any full-screen app */
        if (mob_screen != MOB_HOME && my >= h - 34) {
            mob_open_app(MOB_HOME);
            return 1;
        }

        if (mob_screen == MOB_HOME) {
            int32_t cols = 3, isz = 68;
            int32_t gap = (w - 48 - cols * isz) / (cols - 1);
            const int app_map[6] = {MOB_BROWSER, MOB_FILES, MOB_CALC, MOB_NOTES, MOB_ACTIVITY, MOB_SETTINGS};
            for (int i = 0; i < 6; i++) {
                int r = i / cols, c = i % cols;
                int32_t ax = 24 + c * (isz + gap), ay = 74 + r * 114;
                if (hit_rect(mx, my, ax, ay, isz, isz)) {
                    mob_open_app(app_map[i]);
                    return 1;
                }
            }
            /* Bottom Dock */
            int32_t dw = (w - 80) / 4;
            const int dock_map[4] = {MOB_BROWSER, MOB_FILES, MOB_ACTIVITY, MOB_SETTINGS};
            for (int i = 0; i < 4; i++) {
                if (hit_rect(mx, my, 32 + i * dw, h - 87, 52, 52)) {
                    mob_open_app(dock_map[i]);
                    return 1;
                }
            }
            return 0;
        }

        /* Full-Screen App Interactions */
        if (mob_screen == MOB_BROWSER) {
            return flair_browser_handle_click(mx, my - 44, w, h - 78);
        } else if (mob_screen == MOB_FILES) {
            if (hit_rect(mx, my, 16, 56, 76, 30)) { m_scpy(mob_fpath, "/", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            if (hit_rect(mx, my, 100, 56, 86, 30)) { m_scpy(mob_fpath, "/apps", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            if (hit_rect(mx, my, 194, 56, 86, 30)) { m_scpy(mob_fpath, "/home", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            int row = (my - 102) / 36;
            if (row >= 0 && row < 12) {
                struct vfs_dirent e;
                if (vfs_readdir(mob_fpath, (uint32_t)row, &e) == 0) {
                    mob_fsel = row;
                    char full[VFS_PATH_MAX];
                    int bl = m_slen(mob_fpath);
                    m_scpy(full, mob_fpath, sizeof(full));
                    if (bl > 1) full[bl++] = '/';
                    m_scpy(full + bl, e.name, (int)sizeof(full) - bl);
                    if (e.type == VFS_TYPE_DIR) { m_scpy(mob_fpath, full, sizeof(mob_fpath)); mob_fsel = 0; }
                    else { mob_notes_load(full); mob_open_app(MOB_NOTES); }
                }
                return 1;
            }
        } else if (mob_screen == MOB_CALC) {
            static const char cmap[5][4] = {
                {'C','n','%','/'}, {'7','8','9','*'}, {'4','5','6','-'}, {'1','2','3','+'}, {'0','0','\b','='}
            };
            int32_t bw = (w - 50) / 4, bh = 74, gx = 16, gy = 176;
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (hit_rect(mx, my, gx + c * (bw + 6), gy + r * (bh + 8), bw, bh)) {
                        mc_press(cmap[r][c]); return 1;
                    }
                }
            }
        } else if (mob_screen == MOB_NOTES) {
            if (hit_rect(mx, my, w - 104, 52, 88, 30)) { mob_notes_save(); return 1; }
        } else if (mob_screen == MOB_ACTIVITY) {
            if (hit_rect(mx, my, 20, h - 104, 180, 42)) { art_am_trigger_button(0); return 1; }
            if (hit_rect(mx, my, w - 180, h - 104, 160, 42)) {
                const char *tag = art_am_active() ? art_am_active()->ui.title : "MobileActivity";
                int sid = sf_find_by_tag(tag);
                if (sid >= 0) sf_destroy_surface(sid);
                else {
                    sid = sf_create_surface(tag, w - 32, h - 180);
                    if (sid >= 0) {
                        int32_t sw = 0, sh = 0;
                        uint32_t *px = sf_lock_buffer(sid, &sw, &sh);
                        if (px) {
                            for (int32_t y = 0; y < sh; y++)
                                for (int32_t x = 0; x < sw; x++)
                                    px[y * sw + x] = 0xFF000000u | (((uint32_t)(x * 255 / (sw ? sw : 1))) << 16) | (((uint32_t)(y * 255 / (sh ? sh : 1))) << 8) | 0xB8u;
                            sf_unlock_and_post(sid);
                        }
                    }
                }
                return 1;
            }
        } else if (mob_screen == MOB_SETTINGS) {
            if (hit_rect(mx, my, 20, 100, w - 40, 46)) { net_service_toggle_wifi(); return 1; }
            if (hit_rect(mx, my, 20, 158, w - 40, 46)) { net_service_toggle_cell(); return 1; }
            if (hit_rect(mx, my, 20, 216, w - 40, 46)) { net_service_toggle_airplane(); return 1; }
            if (hit_rect(mx, my, 20, 274, w - 40, 46)) { mob_anim = !mob_anim; return 1; }
            int32_t sw = (w - 64) / 3;
            for (int t = 0; t < 3; t++) {
                if (hit_rect(mx, my, 20 + t * (sw + 12), 340, sw, 42)) { mob_theme = t; return 1; }
            }
        }
    }

    if (in->key) {
        if (in->key == INPUT64_KEY_ESC && mob_screen != MOB_HOME) { mob_open_app(MOB_HOME); return 1; }
        if (mob_screen == MOB_BROWSER) return flair_browser_handle_key(in->key);
        if (mob_screen == MOB_CALC) { mc_press((char)in->key); return 1; }
        if (mob_screen == MOB_NOTES) {
            if (in->key == '\b' && mob_nlen > 0) { mob_nbuf[--mob_nlen] = 0; return 1; }
            if ((in->key == '\n' || (in->key >= 32 && in->key < 127)) && mob_nlen + 1 < (int)sizeof(mob_nbuf)) {
                mob_nbuf[mob_nlen++] = (char)in->key; mob_nbuf[mob_nlen] = 0; return 1;
            }
        }
    }
    return 0;
}

void shell_render(struct ag_surface *s, const struct shell_boot_info *info, int frame_idx) {
    (void)frame_idx;
    if (!s || !info) return;
    if (!mob_nloaded) mob_notes_load("/home/NOTE.TXT");
    int32_t w = s->width, h = s->height;

    uint32_t top_c = mob_theme == 1 ? 0x000D1E3Au : (mob_theme == 2 ? 0x002B1028u : 0x00101D3Bu);
    uint32_t bot_c = mob_theme == 1 ? 0x00163B5Cu : (mob_theme == 2 ? 0x0052193Au : 0x003D1A47u);
    if (!m_wp_cache && w > 0 && h > 0) m_wp_cache = (uint32_t *)kmem64_alloc((size_t)w * (size_t)h * 4u);
    if (m_wp_cache) {
        struct ag_surface ws = { m_wp_cache, w, h, w * 4, AG_PIXEL_XRGB8888 };
        if (m_wp_theme != mob_theme || m_wp_w != w || m_wp_h != h) {
            gfx_gradient_v(&ws, 0, 0, w, h, top_c, bot_c);
            m_wp_theme = mob_theme; m_wp_w = w; m_wp_h = h;
        }
        gfx_copy_fast(s, &ws);
    } else {
        gfx_gradient_v(s, 0, 0, w, h, top_c, bot_c);
    }

    /* Mobile Status Bar (Real NetService 5G + Wi-Fi + Battery) */
    const struct net_service_state *ns = net_service_state();
    gfx_draw_text_scaled(s, 18, 14, "FlairOS", THEME_TEXT_PRIMARY, 2);
    int32_t iw = 148, ih = 26, ix = (w - iw) / 2, iy = 10;
    gfx_fill_round_rect(s, ix, iy, iw, ih, 13, 0x0005060Au, 245u);
    gfx_fill_round_rect(s, ix + 10, iy + 7, 12, 12, 6, THEME_ACCENT_GREEN, 255u);
    gfx_draw_text_scaled(s, ix + 28, iy + 8, "ART+AirP", THEME_TEXT_PRIMARY, 1);

    airstin_draw_cell_icon(s, w - 134, 12, ns->cell_bars, ns->cell_on && !ns->airplane_mode, THEME_ACCENT_TEAL);
    if (ns->wifi_on && !ns->airplane_mode) gfx_draw_wifi_icon(s, w - 108, 10, ns->wifi_bars, THEME_ACCENT_TEAL);
    else                                   gfx_draw_eth_icon(s, w - 108, 10, THEME_ACCENT_BLUE);
    gfx_draw_battery_icon(s, w - 76, 11, ns->battery_pct, ns->ac_power, THEME_ACCENT_GREEN);

    int32_t oy = mob_slide_y;

    if (mob_screen == MOB_HOME) {
        int32_t cols = 3, isz = 68;
        int32_t gap = (w - 48 - cols * isz) / (cols - 1);
        const char *labels[6] = {"Browser", "Files", "Calc", "Notes", "Activity", "Settings"};
        const char *glyphs[6] = {"BR", "FN", "CA", "NT", "AP", "ST"};
        const int anim_idx[6] = {5, 1, 4, 3, 6, 7};
        for (int i = 0; i < 6; i++) {
            int r = i / cols, c = i % cols;
            int32_t ax = 24 + c * (isz + gap), ay = 74 + r * 114;
            gfx_draw_bmp_scaled(s, anim_icon_bmps[anim_idx[i]][0], anim_icon_sizes[anim_idx[i]][0], ax, ay, isz, isz);
            gfx_draw_text_scaled(s, ax + 18, ay + 24, glyphs[i], THEME_TEXT_PRIMARY, 2);
            int32_t tw = gfx_text_width(labels[i], 2);
            gfx_draw_text_scaled(s, ax + (isz - tw) / 2, ay + isz + 8, labels[i], THEME_TEXT_PRIMARY, 2);
        }

        /* Quick Info Widget */
        gfx_glass_panel(s, 18, 326, w - 36, 130, 20, THEME_GLASS_DARK, 210u);
        gfx_draw_text_scaled(s, 34, 344, info->arch_name, THEME_ACCENT_TEAL, 2);
        gfx_draw_text_scaled(s, 34, 376, "Full-Screen Apps Ready", THEME_TEXT_PRIMARY, 2);
        gfx_draw_text_scaled(s, 34, 408, "Tap any app or top-right status", THEME_TEXT_MUTED, 1);

        /* Mobile Dock */
        gfx_glass_panel(s, 18, h - 98, w - 36, 74, 24, THEME_GLASS_LIGHT, 195u);
        int32_t dw = (w - 80) / 4;
        const char *dg[4] = {"BR", "FN", "AP", "ST"};
        const int da[4] = {5, 1, 6, 7};
        for (int i = 0; i < 4; i++) {
            int32_t dx = 32 + i * dw;
            gfx_draw_bmp_scaled(s, anim_icon_bmps[da[i]][0], anim_icon_sizes[da[i]][0], dx, h - 87, 52, 52);
            gfx_draw_text_scaled(s, dx + 14, h - 69, dg[i], THEME_TEXT_PRIMARY, 2);
        }
    } else {
        /* Full-Screen App Container */
        gfx_glass_panel(s, 4, 42 + oy, w - 8, h - 78, 16, THEME_GLASS_DARK, 238u);
        if (mob_screen == MOB_BROWSER) {
            flair_browser_render(s, 4, 42 + oy, w - 8, h - 78);
        } else if (mob_screen == MOB_FILES) {
            gfx_fill_round_rect(s, 16, 56 + oy, 76, 30, 8, m_seq(mob_fpath, "/") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, 32, 64 + oy, "Root", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 100, 56 + oy, 86, 30, 8, m_seq(mob_fpath, "/apps") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, 118, 64 + oy, "Apps", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 194, 56 + oy, 86, 30, 8, m_seq(mob_fpath, "/home") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, 212, 64 + oy, "Home", THEME_TEXT_PRIMARY, 2);
            struct vfs_dirent e;
            for (uint32_t i = 0; i < 12 && vfs_readdir(mob_fpath, i, &e) == 0; i++) {
                int32_t ry = 102 + oy + (int32_t)i * 36;
                if ((int)i == mob_fsel) gfx_fill_round_rect(s, 14, ry, w - 28, 30, 8, THEME_ACCENT_BLUE, 180u);
                gfx_draw_text_scaled(s, 24, ry + 7, e.name, THEME_TEXT_PRIMARY, 2);
            }
        } else if (mob_screen == MOB_CALC) {
            gfx_fill_round_rect(s, 16, 56 + oy, w - 32, 104, 14, THEME_GLASS_LIGHT, 150u);
            char cd[28]; if (mc_err) m_scpy(cd, "Error", sizeof(cd)); else m_fmt_i64(mc_cur, cd, sizeof(cd));
            gfx_draw_text_scaled(s, 28, 96 + oy, cd, THEME_TEXT_PRIMARY, 4);
            static const char *cl[5][4] = {{"C","+/-","%","/"},{"7","8","9","*"},{"4","5","6","-"},{"1","2","3","+"},{"0","0","BS","="}};
            int32_t bw = (w - 50) / 4, bh = 74, gx = 16, gy = 176 + oy;
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    uint32_t bg = (c == 3) ? THEME_ACCENT_ORANGE : (r == 0 ? THEME_GLASS_BORDER : THEME_GLASS_LIGHT);
                    gfx_fill_round_rect(s, gx + c * (bw + 6), gy + r * (bh + 8), bw, bh, 14, bg, 235u);
                    gfx_draw_text_scaled(s, gx + c * (bw + 6) + 28, gy + r * (bh + 8) + 26, cl[r][c], THEME_TEXT_PRIMARY, 2);
                }
            }
        } else if (mob_screen == MOB_NOTES) {
            gfx_draw_text_scaled(s, 18, 58 + oy, mob_npath, THEME_ACCENT_TEAL, 1);
            gfx_fill_round_rect(s, w - 104, 52 + oy, 88, 30, 8, THEME_ACCENT_BLUE, 235u);
            gfx_draw_text_scaled(s, w - 86, 60 + oy, "Save", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 16, 94 + oy, w - 32, h - 170, 12, THEME_GLASS_LIGHT, 120u);
            int row = 0, col = 0; char line[40];
            for (int i = 0; i <= mob_nlen && row < 16; i++) {
                char ch = (i < mob_nlen) ? mob_nbuf[i] : 0;
                if (ch == '\n' || ch == 0 || col >= 28) {
                    if (ch == 0 && col < 28) line[col++] = '_';
                    line[col] = 0;
                    gfx_draw_text_scaled(s, 24, 108 + oy + row * 26, line, THEME_TEXT_PRIMARY, 2);
                    row++; col = 0; if (ch == 0) break;
                } else line[col++] = ch;
            }
            gfx_draw_text_scaled(s, 20, h - 62, mob_nstatus, THEME_TEXT_MUTED, 1);
        } else if (mob_screen == MOB_ACTIVITY) {
            const struct art_activity *act = art_am_active();
            const char *tag = act ? act->ui.title : "MobileActivity";
            int sid = sf_find_by_tag(tag);
            if (sid >= 0) {
                sf_composite_to(sid, s, 16, 56 + oy);
            } else if (act) {
                gfx_pill_badge(s, 20, 56 + oy, "FULLSCREEN ACTIVITY", THEME_ACCENT_GREEN, THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, 20, 96 + oy, act->ui.title, THEME_TEXT_PRIMARY, 3);
                for (int i = 0; i < act->ui.line_count && i < 4; i++)
                    gfx_draw_text_scaled(s, 20, 146 + oy + i * 28, act->ui.lines[i], THEME_TEXT_MUTED, 2);
                gfx_draw_text_scaled(s, 20, 290 + oy, act->action_status, THEME_ACCENT_TEAL, 2);
            }
            gfx_fill_round_rect(s, 20, h - 104, 180, 42, 10, THEME_ACCENT_BLUE, 235u);
            gfx_draw_text_scaled(s, 36, h - 90, "Trigger Action", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, w - 180, h - 104, 160, 42, 10, THEME_ACCENT_TEAL, 235u);
            gfx_draw_text_scaled(s, w - 166, h - 90, sid >= 0 ? "Fallback" : "Surface", THEME_TEXT_PRIMARY, 2);
        } else if (mob_screen == MOB_SETTINGS) {
            gfx_draw_text_scaled(s, 20, 56 + oy, "Mobile Settings", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 20, 100 + oy, w - 40, 46, 10, ns->wifi_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, 34, 115 + oy, ns->wifi_on ? "Wi-Fi: Connected" : "Wi-Fi: Off", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 20, 158 + oy, w - 40, 46, 10, ns->cell_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, 34, 173 + oy, ns->cell_on ? "5G Cellular: Active" : "5G Cellular: Off", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 20, 216 + oy, w - 40, 46, 10, ns->airplane_mode ? THEME_ACCENT_ORANGE : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, 34, 231 + oy, ns->airplane_mode ? "Airplane Mode: ON" : "Airplane Mode: OFF", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, 20, 274 + oy, w - 40, 46, 10, mob_anim ? THEME_ACCENT_INDIGO : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, 34, 289 + oy, mob_anim ? "Animations: ON" : "Animations: OFF", THEME_TEXT_PRIMARY, 2);
            int32_t sw = (w - 64) / 3;
            const char *tn[3] = {"Indigo", "Blue", "Pink"};
            uint32_t tc[3] = {THEME_ACCENT_INDIGO, THEME_ACCENT_BLUE, THEME_ACCENT_PINK};
            for (int t = 0; t < 3; t++) {
                gfx_fill_round_rect(s, 20 + t * (sw + 12), 340 + oy, sw, 42, 10, tc[t], 235u);
                gfx_draw_text_scaled(s, 34 + t * (sw + 12), 354 + oy, tn[t], THEME_TEXT_PRIMARY, 2);
            }
        }
    }

    if (mob_cc_open) {
        airstin_render_control_center(s, w, 1, 24, mob_anim, mob_theme);
    }

    /* Home Indicator Bar */
    gfx_fill_round_rect(s, (w - 130) / 2, h - 14, 130, 5, 2, THEME_TEXT_PRIMARY, 225u);
}
