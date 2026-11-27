/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS phone shell (SpringBoard-style home screen + full-screen apps).
 *
 * The home screen is a resource: FlairMobile.airp carries launcher.json (tiles,
 * dock, title, hint) and strings.json (every label), and this shell only draws
 * what the kernel accepted, keeping its built-in layout when nothing is staged.
 * Geometry is authored once in 480x800 design coordinates and scaled to the
 * panel, so a 480x800 handset and a 1080x1920 phone read the same.
 */
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
#include "../../kernel/ui/mobile_layout.h"
#include "../../kernel/ui/shell_launcher.h"

enum mob_app_id {
    MOB_HOME     = 0,
    MOB_BROWSER  = 1,
    MOB_FILES    = 2,
    MOB_CALC     = 3,
    MOB_NOTES    = 4,
    MOB_ACTIVITY = 5,
    MOB_SETTINGS = 6
};

static struct shell_launcher g_launcher;
static int g_launcher_ready = 0;
static int32_t g_scale = MOBILE_LAYOUT_UNIT_Q16;
static int m_land; /* panel wider than tall: draw the 800x480 canvas */

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

static int32_t px(int32_t v) { return mobile_layout_px(v, g_scale); }
static int32_t tx(int32_t v) { return mobile_layout_text(v, g_scale); }

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
static int hit_rect(int32_t hx, int32_t hy, int32_t x, int32_t y, int32_t w, int32_t h) {
    return hx >= x && hx < x + w && hy >= y && hy < y + h;
}

/* Session text from the package, with the shell's own wording as fallback. */
static const char *mob_label(const char *key, const char *fallback) {
    static char buf[SHELL_STRING_TEXT_MAX];
    return shell_strings_copy(&g_launcher.strings, key, buf, sizeof buf) ? buf : fallback;
}

static void mob_set_surface(int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    m_land = mobile_layout_landscape(w, h);
    g_scale = mobile_layout_scale_q16(w, h);
}

static void mob_ensure(const struct shell_boot_info *info) {
    if (g_launcher_ready) return;
    g_launcher_ready = 1;
    shell_launcher_default(&g_launcher, SHELL_KIND_MOBILE);
    if (info && info->session_pkg && info->session_pkg_len) {
        struct shell_launcher packaged;
        struct shell_launcher_report report;
        if (shell_launcher_load_package(info->session_pkg, info->session_pkg_len,
                                        SHELL_KIND_MOBILE, &packaged, &report))
            g_launcher = packaged;
    }
}

static int mob_tile_index(const char *id) {
    for (int i = 0; i < g_launcher.tile_count; i++)
        if (m_seq(g_launcher.tiles[i].id, id)) return i;
    return -1;
}

/* Tile targets are declarative: "screen:N" opens a built-in surface, "app:N"
   launches an ART package. Unknown targets are ignored rather than guessed. */
static int mob_screen_for_target(const char *target) {
    static const struct { const char *target; int screen; } map[] = {
        { "screen:browser", MOB_BROWSER },
        { "screen:files", MOB_FILES },
        { "screen:calc", MOB_CALC },
        { "screen:notes", MOB_NOTES },
        { "screen:activity", MOB_ACTIVITY },
        { "screen:settings", MOB_SETTINGS }
    };
    for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
        if (m_seq(target, map[i].target)) return map[i].screen;
    if (target[0] == 'a' && target[1] == 'p' && target[2] == 'p' && target[3] == ':') {
        int n = 0;
        const char *p = target + 4;
        if (*p < '0' || *p > '9') return -1;
        while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
        if (n < art_pm_count()) (void)art_am_launch(n);
    }
    return -1;
}

static void mob_open_app(int app) {
    mob_screen = app;
    mob_cc_open = 0;
    mob_slide_y = mob_anim ? px(36) : 0;
}

static void mob_activate_tile(int idx) {
    int screen;
    if (idx < 0 || idx >= g_launcher.tile_count) return;
    screen = mob_screen_for_target(g_launcher.tiles[idx].target);
    if (screen >= 0) mob_open_app(screen);
}

/* --- Home screen geometry, shared by rendering and hit testing ------------ */
#define MOB_COLS 3
#define MOB_COLS_LANDSCAPE 4

static int32_t mob_cols(void) { return m_land ? MOB_COLS_LANDSCAPE : MOB_COLS; }
static int32_t mob_icon_size(void) { return m_land ? px(60) : px(68); }
static int32_t mob_tile_step(void) { return m_land ? px(102) : px(114); }
static int32_t mob_grid_top(void) { return m_land ? px(66) : px(74); }

static int32_t mob_grid_gap(int32_t w) {
    int32_t cols = mob_cols();
    int32_t span = w - px(48) - cols * mob_icon_size();
    int32_t gap = span / (cols - 1);
    return gap > px(6) ? gap : px(6);
}

static void mob_tile_rect(int32_t w, int idx, int32_t *x, int32_t *y) {
    int32_t cols = mob_cols();
    int32_t isz = mob_icon_size(), gap = mob_grid_gap(w);
    int r = idx / cols, c = idx % cols;
    *x = px(24) + c * (isz + gap);
    *y = mob_grid_top() + r * mob_tile_step();
}

static int32_t mob_grid_rows(void) {
    int32_t cols = mob_cols();
    int n = g_launcher.tile_count > 0 ? g_launcher.tile_count : 1;
    return (n + cols - 1) / cols;
}

static int32_t mob_widget_y(void) {
    return mob_grid_top() + mob_grid_rows() * mob_tile_step() + px(m_land ? 4 : 8);
}

static void mob_dock_rect(int32_t w, int slot, int32_t *x) {
    int count = g_launcher.dock_count > 0 ? g_launcher.dock_count : 1;
    int32_t dw = (w - px(80)) / count;
    *x = px(32) + slot * dw;
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
    m_scpy(mob_nstatus, mob_label("notes_loaded", "Loaded"), sizeof(mob_nstatus));
}

static void mob_notes_save(void) {
    int fd = vfs_open(mob_npath, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd >= 0) {
        (void)vfs_write(fd, mob_nbuf, (uint32_t)mob_nlen);
        vfs_close(fd);
        m_scpy(mob_nstatus, mob_label("notes_saved", "Saved to VFS"), sizeof(mob_nstatus));
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
        int32_t step = px(9);
        if (step < 1) step = 1;
        mob_slide_y = mob_anim ? (mob_slide_y > step ? mob_slide_y - step : 0) : 0;
        changed = 1;
    }
    return changed;
}

int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h) {
    int32_t mx, my, oy;

    mob_set_surface(w, h);
    mob_ensure(0);
    airstin_ui_set_scale_q16(g_scale);
    if (!in) return 0;
    if (!mob_nloaded) mob_notes_load("/home/NOTE.TXT");
    mx = in->mouse_x;
    my = in->mouse_y;
    oy = mob_slide_y;

    if (in->clicked) {
        /* Top status bar: tap right side for the control drawer */
        if (my < px(44)) {
            if (mx >= w - px(180)) {
                mob_cc_open = !mob_cc_open;
                if (!mob_cc_open) airstin_cc_set_view(CC_VIEW_ROOT);
                return 1;
            }
            return 0;
        }
        /* The drawer's own geometry decides both what is drawn and what a tap
           hits, so the mobile overlay and the shared drawer cannot disagree. */
        if (mob_cc_open) {
            int spot = 0;
            if (airstin_cc_handle_click(mx, my, w, &mob_anim, &mob_theme, &spot, &mob_cc_open))
                return 1;
            mob_cc_open = 0;
            airstin_cc_set_view(CC_VIEW_ROOT);
            return 1;
        }

        /* Home indicator bar at the bottom returns Home from any app */
        if (mob_screen != MOB_HOME && my >= h - px(34)) {
            mob_open_app(MOB_HOME);
            return 1;
        }

        if (mob_screen == MOB_HOME) {
            for (int i = 0; i < g_launcher.tile_count; i++) {
                int32_t ax, ay;
                mob_tile_rect(w, i, &ax, &ay);
                if (hit_rect(mx, my, ax, ay, mob_icon_size(), mob_icon_size())) {
                    mob_activate_tile(i);
                    return 1;
                }
            }
            for (int slot = 0; slot < g_launcher.dock_count; slot++) {
                int32_t dx;
                mob_dock_rect(w, slot, &dx);
                if (hit_rect(mx, my, dx, h - px(87), px(52), px(52))) {
                    mob_activate_tile(mob_tile_index(g_launcher.dock_ids[slot]));
                    return 1;
                }
            }
            return 0;
        }

        /* Full-screen app interactions */
        if (mob_screen == MOB_BROWSER) {
            return flair_browser_handle_click(mx - px(4), my - (px(42) + oy),
                                              w - px(8), h - px(78));
        } else if (mob_screen == MOB_FILES) {
            if (hit_rect(mx, my, px(16), px(56) + oy, px(76), px(30))) { m_scpy(mob_fpath, "/", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            if (hit_rect(mx, my, px(100), px(56) + oy, px(86), px(30))) { m_scpy(mob_fpath, "/apps", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            if (hit_rect(mx, my, px(194), px(56) + oy, px(86), px(30))) { m_scpy(mob_fpath, "/home", sizeof(mob_fpath)); mob_fsel = 0; return 1; }
            {
                int row = (my - (px(102) + oy)) / (px(36) > 0 ? px(36) : 1);
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
            }
        } else if (mob_screen == MOB_CALC) {
            static const char cmap[5][4] = {
                {'C','n','%','/'}, {'7','8','9','*'}, {'4','5','6','-'}, {'1','2','3','+'}, {'0','0','\b','='}
            };
            int32_t bw = (w - px(50)) / 4, bh = px(74), gx = px(16), gy = px(176) + oy;
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (hit_rect(mx, my, gx + c * (bw + px(6)), gy + r * (bh + px(8)), bw, bh)) {
                        mc_press(cmap[r][c]); return 1;
                    }
                }
            }
        } else if (mob_screen == MOB_NOTES) {
            if (hit_rect(mx, my, w - px(104), px(52) + oy, px(88), px(30))) { mob_notes_save(); return 1; }
        } else if (mob_screen == MOB_ACTIVITY) {
            if (hit_rect(mx, my, px(20), h - px(104), px(180), px(42))) { art_am_trigger_button(0); return 1; }
            if (hit_rect(mx, my, w - px(180), h - px(104), px(160), px(42))) {
                const char *tag = art_am_active() ? art_am_active()->ui.title : "MobileActivity";
                int sid = sf_find_by_tag(tag);
                if (sid >= 0) sf_destroy_surface(sid);
                else {
                    sid = sf_create_surface(tag, w - px(32), h - px(180));
                    if (sid >= 0) {
                        int32_t sw = 0, sh = 0;
                        uint32_t *buf = sf_lock_buffer(sid, &sw, &sh);
                        if (buf) {
                            for (int32_t y = 0; y < sh; y++)
                                for (int32_t x = 0; x < sw; x++)
                                    buf[y * sw + x] = 0xFF000000u | (((uint32_t)(x * 255 / (sw ? sw : 1))) << 16) | (((uint32_t)(y * 255 / (sh ? sh : 1))) << 8) | 0xB8u;
                            sf_unlock_and_post(sid);
                        }
                    }
                }
                return 1;
            }
        } else if (mob_screen == MOB_SETTINGS) {
            if (hit_rect(mx, my, px(20), px(100), w - px(40), px(46))) { net_service_toggle_wifi(); return 1; }
            if (hit_rect(mx, my, px(20), px(158), w - px(40), px(46))) { net_service_toggle_cell(); return 1; }
            if (hit_rect(mx, my, px(20), px(216), w - px(40), px(46))) { net_service_toggle_airplane(); return 1; }
            if (hit_rect(mx, my, px(20), px(274), w - px(40), px(46))) { mob_anim = !mob_anim; return 1; }
            {
                int32_t sw = (w - px(64)) / 3;
                for (int t = 0; t < 3; t++) {
                    if (hit_rect(mx, my, px(20) + t * (sw + px(12)), px(340), sw, px(42))) { mob_theme = t; return 1; }
                }
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
    int32_t w, h, oy;
    uint32_t top_c, bot_c;

    (void)frame_idx;
    if (!s || !info) return;
    mob_set_surface(s->width, s->height);
    mob_ensure(info);
    airstin_ui_set_scale_q16(g_scale);
    if (!mob_nloaded) mob_notes_load("/home/NOTE.TXT");
    w = s->width;
    h = s->height;

    top_c = mob_theme == 1 ? 0x000D1E3Au : (mob_theme == 2 ? 0x002B1028u : 0x00101D3Bu);
    bot_c = mob_theme == 1 ? 0x00163B5Cu : (mob_theme == 2 ? 0x0052193Au : 0x003D1A47u);
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

    /* Status bar: session name, ART/AirP pill, real network cluster. */
    {
        gfx_draw_text_scaled(s, px(18), px(14), mob_label("app", "FlairOS"),
                             THEME_TEXT_PRIMARY, tx(2));
        gfx_pill_badge(s, (w - px(148)) / 2, px(10), mob_label("status_pill", "ART + AirP"),
                       THEME_ACCENT_BLUE, THEME_TEXT_PRIMARY, tx(1));
    }
    airstin_render_status_cluster(s, w, info->arch_name, mob_cc_open);

    oy = mob_slide_y;

    if (mob_screen == MOB_HOME) {
        for (int i = 0; i < g_launcher.tile_count; i++) {
            const struct shell_tile *tile = &g_launcher.tiles[i];
            int32_t ax, ay;
            int icon = (tile->icon >= 0 && tile->icon < ANIM_ICON_COUNT) ? tile->icon : 0;
            mob_tile_rect(w, i, &ax, &ay);
            gfx_draw_bmp_scaled(s, anim_icon_bmps[icon][0], anim_icon_sizes[icon][0],
                                ax, ay, mob_icon_size(), mob_icon_size());
            if (tile->glyph[0])
                gfx_draw_text_scaled(s, ax + mob_icon_size() / 3, ay + mob_icon_size() / 3,
                                     tile->glyph, THEME_TEXT_PRIMARY, tx(2));
            {
                int32_t tw = gfx_text_width(tile->label, tx(2));
                gfx_draw_text_scaled(s, ax + (mob_icon_size() - tw) / 2,
                                     ay + mob_icon_size() + px(8), tile->label,
                                     THEME_TEXT_PRIMARY, tx(2));
            }
        }

        /* Session widget: what the shell is and how to drive it. */
        {
            int32_t wy = mob_widget_y();
            gfx_glass_panel(s, px(18), wy, w - px(36), px(m_land ? 92 : 130), px(20),
                            THEME_GLASS_DARK, 210u);
            gfx_draw_text_scaled(s, px(34), wy + px(m_land ? 14 : 18), g_launcher.title,
                                 THEME_ACCENT_TEAL, tx(2));
            gfx_draw_text_scaled(s, px(34), wy + px(m_land ? 42 : 50),
                                 mob_label("widget_title", "Full-Screen Apps Ready"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_draw_text_scaled(s, px(34), wy + px(m_land ? 70 : 82),
                                 mob_label("widget_hint", "Tap any app or top-right status"),
                                 THEME_TEXT_MUTED, tx(1));
        }

        /* Dock, in the order the package declares. */
        if (g_launcher.dock_count > 0) {
            gfx_glass_panel(s, px(18), h - px(98), w - px(36), px(74), px(24),
                            THEME_GLASS_LIGHT, 195u);
            for (int slot = 0; slot < g_launcher.dock_count; slot++) {
                int idx = mob_tile_index(g_launcher.dock_ids[slot]);
                int32_t dx;
                const struct shell_tile *tile;
                int icon;
                if (idx < 0) continue;
                tile = &g_launcher.tiles[idx];
                icon = (tile->icon >= 0 && tile->icon < ANIM_ICON_COUNT) ? tile->icon : 0;
                mob_dock_rect(w, slot, &dx);
                gfx_draw_bmp_scaled(s, anim_icon_bmps[icon][0], anim_icon_sizes[icon][0],
                                    dx, h - px(87), px(52), px(52));
                if (tile->glyph[0])
                    gfx_draw_text_scaled(s, dx + px(14), h - px(69), tile->glyph,
                                         THEME_TEXT_PRIMARY, tx(2));
            }
        }
    } else {
        /* Full-screen app container */
        gfx_glass_panel(s, px(4), px(42) + oy, w - px(8), h - px(78), px(16),
                        THEME_GLASS_DARK, 238u);
        if (mob_screen == MOB_BROWSER) {
            flair_browser_render(s, px(4), px(42) + oy, w - px(8), h - px(78));
        } else if (mob_screen == MOB_FILES) {
            gfx_fill_round_rect(s, px(16), px(56) + oy, px(76), px(30), px(8),
                                m_seq(mob_fpath, "/") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, px(32), px(64) + oy, mob_label("files_root", "Root"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(100), px(56) + oy, px(86), px(30), px(8),
                                m_seq(mob_fpath, "/apps") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, px(118), px(64) + oy, mob_label("files_apps", "Apps"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(194), px(56) + oy, px(86), px(30), px(8),
                                m_seq(mob_fpath, "/home") ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, px(212), px(64) + oy, mob_label("files_home", "Home"),
                                 THEME_TEXT_PRIMARY, tx(2));
            {
                struct vfs_dirent e;
                for (uint32_t i = 0; i < 12 && vfs_readdir(mob_fpath, i, &e) == 0; i++) {
                    int32_t ry = px(102) + oy + (int32_t)i * px(36);
                    if ((int)i == mob_fsel)
                        gfx_fill_round_rect(s, px(14), ry, w - px(28), px(30), px(8),
                                            THEME_ACCENT_BLUE, 180u);
                    gfx_draw_text_scaled(s, px(24), ry + px(7), e.name, THEME_TEXT_PRIMARY, tx(2));
                }
            }
        } else if (mob_screen == MOB_CALC) {
            gfx_fill_round_rect(s, px(16), px(56) + oy, w - px(32), px(104), px(14),
                                THEME_GLASS_LIGHT, 150u);
            {
                char cd[28];
                if (mc_err) m_scpy(cd, mob_label("calc_error", "Error"), sizeof(cd));
                else m_fmt_i64(mc_cur, cd, sizeof(cd));
                gfx_draw_text_scaled(s, px(28), px(96) + oy, cd, THEME_TEXT_PRIMARY, tx(4));
            }
            {
                static const char *cl[5][4] = {{"C","+/-","%","/"},{"7","8","9","*"},
                                               {"4","5","6","-"},{"1","2","3","+"},
                                               {"0","0","BS","="}};
                int32_t bw = (w - px(50)) / 4, bh = px(m_land ? 50 : 74), gx = px(16);
                int32_t gy = px(m_land ? 160 : 176) + oy;
                for (int r = 0; r < 5; r++) {
                    for (int c = 0; c < 4; c++) {
                        uint32_t bg = (c == 3) ? THEME_ACCENT_ORANGE
                                               : (r == 0 ? THEME_GLASS_BORDER : THEME_GLASS_LIGHT);
                        gfx_fill_round_rect(s, gx + c * (bw + px(6)), gy + r * (bh + px(8)),
                                            bw, bh, px(14), bg, 235u);
                        gfx_draw_text_scaled(s, gx + c * (bw + px(6)) + bw / 3,
                                             gy + r * (bh + px(8)) + bh / 3, cl[r][c],
                                             THEME_TEXT_PRIMARY, tx(2));
                    }
                }
            }
        } else if (mob_screen == MOB_NOTES) {
            gfx_draw_text_scaled(s, px(18), px(58) + oy, mob_npath, THEME_ACCENT_TEAL, tx(1));
            gfx_fill_round_rect(s, w - px(104), px(52) + oy, px(88), px(30), px(8),
                                THEME_ACCENT_BLUE, 235u);
            gfx_draw_text_scaled(s, w - px(86), px(60) + oy, mob_label("save", "Save"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(16), px(94) + oy, w - px(32), h - px(170), px(12),
                                THEME_GLASS_LIGHT, 120u);
            {
                int row = 0, col = 0;
                char line[40];
                int max_rows = m_land ? 9 : 16;
                for (int i = 0; i <= mob_nlen && row < max_rows; i++) {
                    char ch = (i < mob_nlen) ? mob_nbuf[i] : 0;
                    if (ch == '\n' || ch == 0 || col >= 28) {
                        if (ch == 0 && col < 28) line[col++] = '_';
                        line[col] = 0;
                        gfx_draw_text_scaled(s, px(24), px(108) + oy + row * px(26), line,
                                             THEME_TEXT_PRIMARY, tx(2));
                        row++; col = 0; if (ch == 0) break;
                    } else line[col++] = ch;
                }
            }
            gfx_draw_text_scaled(s, px(20), h - px(62), mob_nstatus, THEME_TEXT_MUTED, tx(1));
        } else if (mob_screen == MOB_ACTIVITY) {
            const struct art_activity *act = art_am_active();
            const char *tag = act ? act->ui.title : "MobileActivity";
            int sid = sf_find_by_tag(tag);
            if (sid >= 0) {
                sf_composite_to(sid, s, px(16), px(56) + oy);
            } else if (act) {
                gfx_pill_badge(s, px(20), px(56) + oy,
                               mob_label("activity_fullscreen", "FULLSCREEN ACTIVITY"),
                               THEME_ACCENT_GREEN, THEME_TEXT_PRIMARY, tx(2));
                gfx_draw_text_scaled(s, px(20), px(96) + oy, act->ui.title, THEME_TEXT_PRIMARY, tx(3));
                for (int i = 0; i < act->ui.line_count && i < 4; i++)
                    gfx_draw_text_scaled(s, px(20), px(146) + oy + i * px(28), act->ui.lines[i],
                                         THEME_TEXT_MUTED, tx(2));
                gfx_draw_text_scaled(s, px(20), px(290) + oy, act->action_status,
                                     THEME_ACCENT_TEAL, tx(2));
            }
            gfx_fill_round_rect(s, px(20), h - px(104), px(180), px(42), px(10),
                                THEME_ACCENT_BLUE, 235u);
            gfx_draw_text_scaled(s, px(36), h - px(90), mob_label("activity_trigger", "Trigger Action"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, w - px(180), h - px(104), px(160), px(42), px(10),
                                THEME_ACCENT_TEAL, 235u);
            gfx_draw_text_scaled(s, w - px(166), h - px(90),
                                 sid >= 0 ? mob_label("activity_fallback", "Fallback")
                                          : mob_label("activity_surface", "Surface"),
                                 THEME_TEXT_PRIMARY, tx(2));
        } else if (mob_screen == MOB_SETTINGS) {
            const struct net_service_state *ns = net_service_state();
            gfx_draw_text_scaled(s, px(20), px(56) + oy, mob_label("settings_title", "Mobile Settings"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(20), px(100) + oy, w - px(40), px(46), px(10),
                                ns->wifi_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, px(34), px(115) + oy,
                                 ns->wifi_on ? mob_label("wifi_on", "Wi-Fi: Connected")
                                             : mob_label("wifi_off", "Wi-Fi: Off"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(20), px(158) + oy, w - px(40), px(46), px(10),
                                ns->cell_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, px(34), px(173) + oy,
                                 ns->cell_on ? mob_label("cell_on", "5G Cellular: Active")
                                             : mob_label("cell_off", "5G Cellular: Off"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(20), px(216) + oy, w - px(40), px(46), px(10),
                                ns->airplane_mode ? THEME_ACCENT_ORANGE : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, px(34), px(231) + oy,
                                 ns->airplane_mode ? mob_label("airplane_on", "Airplane Mode: ON")
                                                   : mob_label("airplane_off", "Airplane Mode: OFF"),
                                 THEME_TEXT_PRIMARY, tx(2));
            gfx_fill_round_rect(s, px(20), px(274) + oy, w - px(40), px(46), px(10),
                                mob_anim ? THEME_ACCENT_INDIGO : THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, px(34), px(289) + oy,
                                 mob_anim ? mob_label("anim_on", "Animations: ON")
                                          : mob_label("anim_off", "Animations: OFF"),
                                 THEME_TEXT_PRIMARY, tx(2));
            {
                int32_t sw = (w - px(64)) / 3;
                const char *tk[3] = { "theme_indigo", "theme_blue", "theme_pink" };
                const char *tf[3] = { "Indigo", "Blue", "Pink" };
                uint32_t tc[3] = {THEME_ACCENT_INDIGO, THEME_ACCENT_BLUE, THEME_ACCENT_PINK};
                for (int t = 0; t < 3; t++) {
                    gfx_fill_round_rect(s, px(20) + t * (sw + px(12)), px(340) + oy, sw, px(42),
                                        px(10), tc[t], 235u);
                    gfx_draw_text_scaled(s, px(34) + t * (sw + px(12)), px(354) + oy,
                                         mob_label(tk[t], tf[t]), THEME_TEXT_PRIMARY, tx(2));
                }
            }
        }
    }

    if (mob_cc_open)
        airstin_render_control_center(s, w, 1, px(24), mob_anim, mob_theme);

    /* Home indicator */
    gfx_fill_round_rect(s, (w - px(130)) / 2, h - px(14), px(130), px(5), px(2),
                        THEME_TEXT_PRIMARY, 225u);
}
