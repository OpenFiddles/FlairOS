/* SPDX-License-Identifier: AGPL-3.0-only */
/* Real macOS-style Interactive Desktop Shell for x86_64 & aarch64 (no mockups) */
#include "shell_common.h"
#include "../../graphics/compositor/glass_fx.h"
#include "../../ui/theme/ios_macos_theme.h"
#include "../../runtime/art/art_bridge.h"
#include "../../include/vfs.h"
#include "../../include/apk.h"
#include "../../include/kmem64.h"
#include "../../ported/developer/openjdk/include/jvm64.h"
#include "../../ported/developer/toybox/toybox_mini.h"
#include "../../ported/developer/kilo/kilo_core.h"
#include "../../net/bridge/host_bridge.h"
#include "flair_browser.h"
#include "../../include/anim_icons_gen.h"
#include "../../runtime/art/surface_flinger.h"
#include "../../net/service/net_service.h"
#include "../../net/airp/airp_net.h"
#include "../../ported/python/flair_py.h"
#include "../systemui/airstin_ui.h"
#include "../../kernel/ui/shell_launcher.h"
#include "../../include/airnim.h"
#include "desk_windows.h"
#ifndef FLAIROS_HOST_TEST
#include "anim_demo.h"
#endif

enum win_id {
    WIN_FINDER = 0,
    WIN_TERM   = 1,
    WIN_NOTES  = 2,
    WIN_APK    = 3,
    WIN_PREFS  = 4,
    WIN_CALC    = 5,
    WIN_BROWSER  = 6,
    WIN_LAUNCHER = 7,
    WIN_COUNT    = 8
};

/* Window geometry lives in desk_windows.c so the headless harness exercises
   the same table, density metrics and hit dispatch as the booted shell. */

static struct dw_window wins[WIN_COUNT] = {
    {1, 0, 0,  36,  52, 540, 360,  36,  52, 540, 360, "Finder"},
    {0, 0, 0, 524,  66, 460, 330, 524,  66, 460, 330, "Terminal"},
    {0, 0, 0, 140, 110, 480, 330, 140, 110, 480, 330, "Notes"},
    {0, 0, 0, 240, 100, 460, 320, 240, 100, 460, 320, "Android Activity"},
    {0, 0, 0, 260, 120, 480, 310, 260, 120, 480, 310, "System Settings"},
    {0, 0, 0, 680,  76, 300, 360, 680,  76, 300, 360, "Calculator"},
    {0, 0, 0, 180,  64, 580, 390, 180,  64, 580, 390, "FlairBrowser"},
    {0, 0, 0, 210,  70, 520, 350, 210,  70, 520, 350, "App Launcher"}
};
static int z_order[WIN_COUNT] = {WIN_LAUNCHER, WIN_BROWSER, WIN_CALC, WIN_PREFS, WIN_APK, WIN_NOTES, WIN_TERM, WIN_FINDER};

/* The desktop's dock and menu wording are a resource: FlairDesktop.airp carries
   desktop/resources/launcher.json and strings.json, and this shell only draws
   what the kernel validated. Coordinates are this shell's own layout code; the
   authored dock order, labels and glyphs are not. */
static struct shell_launcher g_launcher;
static int g_launcher_ready = 0;

static void desk_apply_titles(void);

static void desk_ensure(const struct shell_boot_info *info) {
    if (g_launcher_ready) return;
    g_launcher_ready = 1;
    shell_launcher_default(&g_launcher, SHELL_KIND_DESKTOP);
    if (info && info->session_pkg && info->session_pkg_len) {
        struct shell_launcher packaged;
        struct shell_launcher_report report;
        if (shell_launcher_load_package(info->session_pkg, info->session_pkg_len,
                                        SHELL_KIND_DESKTOP, &packaged, &report))
            g_launcher = packaged;
    }
    desk_apply_titles();
}

static int desk_str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int desk_tile_index(const char *id) {
    for (int i = 0; i < g_launcher.tile_count; i++)
        if (desk_str_eq(g_launcher.tiles[i].id, id)) return i;
    return -1;
}

/* Dock targets name an existing window surface, so a package can reorder the
   dock but cannot invent a window this shell does not own. */
static int desk_target_window(const char *target) {
    static const struct { const char *target; int win; } map[] = {
        { "screen:launcher", WIN_LAUNCHER }, { "screen:finder", WIN_FINDER },
        { "screen:terminal", WIN_TERM }, { "screen:notes", WIN_NOTES },
        { "screen:calc", WIN_CALC }, { "screen:browser", WIN_BROWSER },
        { "screen:apphost", WIN_APK }, { "screen:settings", WIN_PREFS }
    };
    for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
        if (desk_str_eq(target, map[i].target)) return map[i].win;
    return -1;
}

static int desk_dock_tile(int slot) {
    if (slot < 0 || slot >= g_launcher.dock_count) return -1;
    return desk_tile_index(g_launcher.dock_ids[slot]);
}

static struct dw_state *desk_dw(void);

/* Panel density for the shell's own chrome and dock. AirGraph owns the
   design-to-device math; 720p and below keep the historical pixel layout. */
static uint32_t desk_ui_scale(void) {
    uint32_t sc = desk_dw()->scale_q16;
    return sc ? sc : AG_SCALE_ONE;
}

static int32_t desk_ui_px(int32_t design_px) {
    return ag_px(design_px, desk_ui_scale());
}

static int32_t desk_ui_text_scale(int32_t base) {
    /* Scale the font itself, not an integer density bucket: at 1.5x a base-2
       label needs scale 3 or the menu items collide. */
    uint32_t sc = desk_ui_scale();
    int32_t out = (int32_t)(((uint64_t)(uint32_t)base * sc + 0x8000u) >> 16);
    if (out < base) out = base;
    return out;
}

static int32_t desk_dock_icon_x(int32_t dx, int i) {
    return dx + desk_ui_px(22) + i * desk_ui_px(76);
}

static void desk_dock_geometry(int32_t w, int32_t h, int32_t *dx, int32_t *dy,
                               int32_t *dw, int32_t *dh) {
    int n = g_launcher.dock_count > 0 ? g_launcher.dock_count : 1;
    *dw = desk_ui_px(44) + n * desk_ui_px(76);
    *dh = desk_ui_px(76);
    *dx = (w - *dw) / 2;
    *dy = h - *dh - desk_ui_px(14);
}

static const char *desk_label(const char *key, const char *fallback) {
    return shell_string_or(&g_launcher.strings, key, fallback);
}

/* Window titles are session wording too, and the window table keeps pointers,
   so the packaged text is copied into shell-owned storage once. */
static char desk_titles[WIN_COUNT][SHELL_STRING_TEXT_MAX];

static void desk_apply_titles(void) {
    static const char *const keys[WIN_COUNT] = {
        "win.finder", "win.terminal", "win.notes", "win.activity",
        "win.settings", "win.calculator", "win.browser", "win.launcher"
    };
    static const char *const fallback[WIN_COUNT] = {
        "Finder", "Terminal", "Notes", "Android Activity",
        "System Settings", "Calculator", "FlairBrowser", "App Launcher"
    };
    for (int i = 0; i < WIN_COUNT; i++) {
        int n = 0;
        const char *text = desk_label(keys[i], fallback[i]);
        while (text[n] && n + 1 < SHELL_STRING_TEXT_MAX) { desk_titles[i][n] = text[n]; n++; }
        desk_titles[i][n] = 0;
        wins[i].title = desk_titles[i];
    }
}
static int dragging_win = -1;
static int32_t drag_ox = 0, drag_oy = 0;
static struct dw_state g_dw;
static int g_dw_ready = 0;

static struct dw_state *desk_dw(void) {
    if (!g_dw_ready) {
        dw_init(&g_dw);
        g_dw_ready = 1;
    }
    return &g_dw;
}

/* Configure density from the framebuffer we were handed. When the mode really
   changes, open windows are rescaled before anything reads their geometry. */
static int desk_density(int32_t w, int32_t h) {
    int old_w, old_h, changed;
    struct dw_state *st = desk_dw();
    old_w = st->panel_w;
    old_h = st->panel_h;
    changed = dw_configure(st, w, h);
    if (changed > 0) {
        if (old_w > 0 && old_h > 0)
            (void)dw_rescale(st, wins, WIN_COUNT, old_w, old_h, w, h);
        else
            (void)dw_layout_from_design(st, wins, WIN_COUNT, 1280, 720, w, h);
        /* A dense desktop's visible windows enter with the same "slide-in"
           preset apps get when they open a window. */
        if (dw_active(st)) {
            for (int i = 0; i < WIN_COUNT; i++)
                if (wins[i].open && !wins[i].minimized) dw_open(st, i);
        }
    }
    return changed > 0;
}
static int active_menu = -1; /* -1=none, 0=FlairOS, 1=Finder, 2=File, 3=Window, 4=View */
static int theme_idx = 0;
static int wifi_on = 1;
static int anim_on = 1;
static int cc_open = 0;
static int spotlight_open = 0;
static char spotlight_q[32] = "";
static int spotlight_len = 0;

static int contains_icase(const char *hay, const char *ndl) {
    if (!ndl || !ndl[0]) return 1;
    for (int i = 0; hay[i]; i++) {
        int j = 0;
        while (ndl[j] && hay[i + j]) {
            char a = hay[i + j], b = ndl[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            j++;
        }
        if (!ndl[j]) return 1;
    }
    return 0;
}
static int cc_anim = 0;

static int dock_hover = -1;
static int dock_anim_tick = 0;
static int dock_anim_frame = 0;
static int32_t dock_pointer_x = -10000;
static int32_t dock_scale[8] = {1000,1000,1000,1000,1000,1000,1000,1000};
static int32_t dock_velocity[8] = {0,0,0,0,0,0,0,0};

static int32_t cc_velocity = 0;
static int32_t spotlight_anim_y = -24;
static int32_t spotlight_anim_vy = 0;
static uint32_t shell_last_tick = 0;
static int shell_clock_ready = 0;
static uint32_t *wp_cache = 0;
static int wp_theme = -1;
static int32_t wp_w = 0, wp_h = 0;
static int tooltips_on = 1;
static int target_hz = 60;
static int prefs_tab = 0; /* 0=Appearance, 1=Motion, 2=Network, 3=About */
static int battery_pct = 100;
static int ac_power = 1;

/* Active .airp App Host state (WinDown) */
static int airp_app_active = 0;
static char airp_app_name[16] = "";
static char airp_app_path[VFS_PATH_MAX] = "";
static uint32_t airp_app_size = 0;
static uint32_t airp_app_api = 0;

/* Calculator state */
static int64_t calc_acc = 0;
static int64_t calc_cur = 0;
static char calc_op = 0;
static int calc_fresh = 1;
static int calc_err = 0;

/* Finder state */
static char finder_path[VFS_PATH_MAX] = "/";
static int finder_sel = 0;
static char finder_status[64] = "Click item to select, click again to open.";
static int folder_seq = 1, note_seq = 1;

/* Terminal state */
#define TERM_LINES 11
#define TERM_COLS  54
static char term_lines[TERM_LINES][TERM_COLS];
static int term_count = 0;
static char term_in[44];
static int term_in_len = 0;
static char term_cwd[VFS_PATH_MAX] = "/";
static int term_inited = 0;

/* Notes state */
static char notes_path[VFS_PATH_MAX] = "/home/NOTE.TXT";
static char notes_buf[512];
static int notes_len = 0;
static char notes_status[48] = "Ready";
static int notes_loaded = 0;

/* Installer state is managed by AirStinUI.airp (airstin_ui) */

static void scpy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

static int seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static int starts_with(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

static int ends_with_apk(const char *s) {
    int n = slen(s);
    if (n < 4) return 0;
    const char *e = s + n - 4;
    return e[0] == '.' && (e[1] == 'a' || e[1] == 'A') &&
           (e[2] == 'p' || e[2] == 'P') && (e[3] == 'k' || e[3] == 'K');
}

static int ends_with_airp(const char *s) {
    int n = slen(s);
    if (n < 5) return 0;
    const char *e = s + n - 5;
    return e[0] == '.' && e[1] == 'a' && e[2] == 'i' && e[3] == 'r' && e[4] == 'p';
}

static int ends_with_class(const char *s) {
    int n = slen(s);
    if (n < 6) return 0;
    const char *e = s + n - 6;
    return e[0] == '.' && e[1] == 'c' && e[2] == 'l' && e[3] == 'a' && e[4] == 's' && e[5] == 's';
}

static void fmt_i64(int64_t v, char *out, int cap) {
    char tmp[24];
    int n = 0, o = 0;
    uint64_t uv;
    if (v < 0) {
        if (o + 1 < cap) out[o++] = '-';
        uv = (uint64_t)(-(v + 1)) + 1u;
    } else {
        uv = (uint64_t)v;
    }
    if (uv == 0) tmp[n++] = '0';
    while (uv > 0 && n < 22) { tmp[n++] = (char)('0' + (uv % 10u)); uv /= 10u; }
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}

static void calc_apply_op(void) {
    if (calc_op == '+') calc_acc += calc_cur;
    else if (calc_op == '-') calc_acc -= calc_cur;
    else if (calc_op == '*') calc_acc *= calc_cur;
    else if (calc_op == '/') {
        if (calc_cur == 0) { calc_err = 1; calc_acc = 0; }
        else calc_acc /= calc_cur;
    } else {
        calc_acc = calc_cur;
    }
    calc_cur = calc_acc;
}

static void calc_press(char ch) {
    if (ch == 'C' || ch == 'c') {
        calc_acc = 0; calc_cur = 0; calc_op = 0; calc_fresh = 1; calc_err = 0;
        return;
    }
    if (calc_err) { calc_acc = 0; calc_cur = 0; calc_op = 0; calc_fresh = 1; calc_err = 0; }
    if (ch >= '0' && ch <= '9') {
        int64_t d = (int64_t)(ch - '0');
        if (calc_fresh) { calc_cur = d; calc_fresh = 0; }
        else if (calc_cur <= 999999999LL && calc_cur >= -999999999LL) {
            calc_cur = calc_cur >= 0 ? (calc_cur * 10 + d) : (calc_cur * 10 - d);
        }
        return;
    }
    if (ch == '\b') {
        if (!calc_fresh) calc_cur /= 10;
        return;
    }
    if (ch == 'n') { /* +/- */
        calc_cur = -calc_cur;
        return;
    }
    if (ch == '%') {
        calc_cur /= 100;
        return;
    }
    if (ch == '+' || ch == '-' || ch == '*' || ch == '/') {
        if (!calc_fresh && calc_op) calc_apply_op();
        else calc_acc = calc_cur;
        calc_op = ch;
        calc_fresh = 1;
        return;
    }
    if (ch == '=' || ch == '\n') {
        if (calc_op) calc_apply_op();
        calc_op = 0;
        calc_fresh = 1;
    }
}

static void fmt_u32(uint32_t v, char *out, int cap) {
    char tmp[12];
    int n = 0, o = 0;
    if (v == 0) tmp[n++] = '0';
    while (v > 0 && n < 11) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n > 0 && o + 1 < cap) out[o++] = tmp[--n];
    out[o] = 0;
}

static int join_path(char *out, int cap, const char *base, const char *leaf) {
    int bl = slen(base), ll = slen(leaf);
    if (!ll || bl + ll + 2 >= cap) return -1;
    scpy(out, base, cap);
    if (base[bl - 1] != '/') out[bl++] = '/';
    for (int i = 0; i < ll; i++) out[bl + i] = leaf[i];
    out[bl + ll] = 0;
    return 0;
}

static void parent_path(char *p) {
    int n = slen(p);
    if (n <= 1) return;
    while (n > 1 && p[n - 1] != '/') p[--n] = 0;
    if (n > 1) p[n - 1] = 0;
    if (!p[0]) scpy(p, "/", VFS_PATH_MAX);
}

static int hit(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void focus_win(int id) {
    int was_hidden = (!wins[id].open || wins[id].minimized);
    int pos = -1;
    for (int i = 0; i < WIN_COUNT; i++) if (z_order[i] == id) { pos = i; break; }
    if (pos >= 0) {
        for (int i = pos; i < WIN_COUNT - 1; i++) z_order[i] = z_order[i + 1];
        z_order[WIN_COUNT - 1] = id;
    }
    wins[id].open = 1;
    wins[id].minimized = 0;
    if (was_hidden) dw_open(desk_dw(), id);
}

static void finish_apk_install(void);

static int32_t spring_advance(int32_t *position, int32_t *velocity, int32_t target, uint32_t dt_ms) {
    uint32_t remain = dt_ms;
    if (remain > 64u) remain = 64u;
    if (!anim_on) { *position = target; *velocity = 0; return 0; }
    while (remain) {
        int32_t err, accel;
        uint32_t step = remain > 4u ? 4u : remain;
        err = target - *position;
        accel = (err * 55) / 1000;
        *velocity = ((*velocity + accel) * 930) / 1000;
        *position += (*velocity * (int32_t)step) / 4;
        remain -= step;
    }
    {
        int32_t remaining = target - *position;
        if (remaining < 0) remaining = -remaining;
        if ((remaining <= 2 && *velocity < 2 && *velocity > -2) ||
            (remaining <= 64 && *velocity == 0)) {
            *position = target; *velocity = 0;
        }
    }
    return *position != target || *velocity != 0;
}

int shell_tick(void) {
    static uint32_t synthetic_tick;
    synthetic_tick += 16u;
    return shell_tick_at(synthetic_tick, 1000u);
}

int shell_tick_at(uint32_t now_ticks, uint32_t ticks_per_second) {
    int changed = 0;
    uint32_t dt_ms = 16u;
    if (ticks_per_second) {
        if (shell_clock_ready) {
            uint32_t delta = now_ticks - shell_last_tick;
            uint64_t ms = (uint64_t)delta * 1000u / ticks_per_second;
            dt_ms = ms > 64u ? 64u : (uint32_t)ms;
            if (dt_ms == 0u) dt_ms = 1u;
        }
        shell_last_tick = now_ticks;
        shell_clock_ready = 1;
    }
    if (airstin_ui_tick()) changed = 1;
    if (airstin_ui_install_done()) {
        finish_apk_install();
        changed = 1;
    }
    if (anim_on && dock_hover >= 0 && dock_anim_frame < ANIM_ICON_FRAMES - 1) {
        dock_anim_tick++;
        if ((dock_anim_tick % 3) == 0) {
            dock_anim_frame++;
            changed = 1;
        }
    } else if (dock_hover < 0 && dock_anim_frame != 0) {
        dock_anim_frame = 0;
        dock_anim_tick = 0;
        changed = 1;
    }
    /* Neighbor-aware Dock magnification follows the pointer continuously. */
    for (int i = 0; i < 8; ++i) {
        int32_t center = desk_ui_px(i * 76 + 48);
        int32_t distance = dock_pointer_x - center;
        int32_t target_scale = 1000;
        if (distance < 0) distance = -distance;
        if (anim_on && distance < desk_ui_px(156)) {
            int32_t falloff = desk_ui_px(156) - distance;
            target_scale += (420 * falloff * falloff) /
                            (desk_ui_px(156) * desk_ui_px(156));
        }
        if (spring_advance(&dock_scale[i], &dock_velocity[i], target_scale, dt_ms)) changed = 1;
    }
    const struct net_service_state *ns = net_service_state();
    if (battery_pct != ns->battery_pct || ac_power != ns->ac_power || wifi_on != ns->wifi_on) {
        battery_pct = ns->battery_pct;
        ac_power = ns->ac_power;
        wifi_on = ns->wifi_on;
        changed = 1;
    }
    if (spring_advance(&cc_anim, &cc_velocity, cc_open ? 24 : 0, dt_ms)) changed = 1;
    if (spring_advance(&spotlight_anim_y, &spotlight_anim_vy, spotlight_open ? 0 : -24, dt_ms)) changed = 1;
#ifndef FLAIROS_HOST_TEST
    if (shell_anim_demo_tick()) changed = 1;
#endif
    return changed;
}

static int top_win(void) {
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int id = z_order[i];
        if (wins[id].open && !wins[id].minimized) return id;
    }
    return -1;
}

static void term_log(const char *s) {
    if (term_count >= TERM_LINES) {
        for (int i = 0; i < TERM_LINES - 1; i++) scpy(term_lines[i], term_lines[i + 1], TERM_COLS);
        term_count = TERM_LINES - 1;
    }
    scpy(term_lines[term_count++], s, TERM_COLS);
}

static void term_init_once(void) {
    if (term_inited) return;
    term_inited = 1;
    term_log("FlairOS 64-bit Shell. Type 'help'.");
}

static void notes_load(const char *path) {
    scpy(notes_path, path, sizeof(notes_path));
    int fd = vfs_open(path, VFS_O_RDONLY);
    notes_len = 0;
    if (fd >= 0) {
        int n = vfs_read(fd, notes_buf, (uint32_t)(sizeof(notes_buf) - 1));
        vfs_close(fd);
        if (n > 0) notes_len = n;
    }
    notes_buf[notes_len] = 0;
    notes_loaded = 1;
    scpy(notes_status, path, sizeof(notes_status));
}

static void notes_save(void) {
    int fd = vfs_open(notes_path, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (fd < 0) { scpy(notes_status, "Save failed", sizeof(notes_status)); return; }
    int n = vfs_write(fd, notes_buf, (uint32_t)notes_len);
    vfs_close(fd);
    scpy(notes_status, n == notes_len ? "Saved to VFS" : "Write error", sizeof(notes_status));
}

static void open_airp_app(const char *path) {
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { scpy(finder_status, "Cannot open .airp", sizeof(finder_status)); return; }
    uint8_t hdr[36];
    int n = vfs_read(fd, hdr, sizeof(hdr));
    vfs_close(fd);
    if (n < 36) { scpy(finder_status, "Invalid .airp", sizeof(finder_status)); return; }
    uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    if (magic != 0x50524941u) { scpy(finder_status, "Bad AIRP magic", sizeof(finder_status)); return; }
    airp_app_api = (uint32_t)hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    airp_app_size = (uint32_t)hdr[20] | ((uint32_t)hdr[21] << 8) | ((uint32_t)hdr[22] << 16) | ((uint32_t)hdr[23] << 24);
    scpy(airp_app_name, (const char *)(hdr + 24), sizeof(airp_app_name));
    scpy(airp_app_path, path, sizeof(airp_app_path));
    airp_app_active = 1;
    focus_win(WIN_APK);
}

static void open_apk_modal(const char *path) {
    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { scpy(finder_status, "Cannot open APK", sizeof(finder_status)); return; }
    static uint8_t tmp[4096];
    int n = vfs_read(fd, tmp, sizeof(tmp));
    vfs_close(fd);
    struct apk_info info;
    if (n <= 0 || apk_inspect(tmp, (uint32_t)n, &info) != 0 || !info.has_dex) {
        scpy(finder_status, "Invalid APK", sizeof(finder_status));
        return;
    }
    airstin_ui_begin_install(path, &info);
}

static void finish_apk_install(void) {
    const struct airstin_installer *ai = airstin_ui_installer();
    if (!ai->active) return;
    char path_copy[VFS_PATH_MAX];
    char pkg_copy[64];
    scpy(path_copy, ai->path, sizeof(path_copy));
    scpy(pkg_copy, ai->info.package_name, sizeof(pkg_copy));
    airstin_ui_cancel_install();
    if (art_am_launch_intent("MAIN", pkg_copy) != 0) {
        static uint8_t buf[4096];
        int fd = vfs_open(path_copy, VFS_O_RDONLY);
        if (fd >= 0) {
            int n = vfs_read(fd, buf, sizeof(buf));
            vfs_close(fd);
            if (n > 0) {
                int idx = art_pm_register_apk(buf, (uint32_t)n, 0xFFu);
                if (idx >= 0) (void)art_am_launch(idx);
            }
        }
    }
    airp_app_active = 0;
    focus_win(WIN_APK);
}

static void start_apk_install(void) {
    /* Trigger animated progress bar inside AirStinUI */
    struct airstin_installer *ai = (struct airstin_installer *)airstin_ui_installer();
    if (ai->active && !ai->installing) {
        ai->installing = 1;
        ai->progress = 10;
    }
}

static void finder_open_idx(int idx) {
    if (!seq(finder_path, "/")) {
        if (idx == 0) { parent_path(finder_path); finder_sel = 0; return; }
        idx--;
    }
    struct vfs_dirent e;
    if (vfs_readdir(finder_path, (uint32_t)idx, &e) != 0) return;
    char full[VFS_PATH_MAX];
    if (join_path(full, sizeof(full), finder_path, e.name) != 0) return;
    if (e.type == VFS_TYPE_DIR) {
        scpy(finder_path, full, sizeof(finder_path));
        finder_sel = 0;
        scpy(finder_status, full, sizeof(finder_status));
    } else if (ends_with_airp(e.name)) {
        open_airp_app(full);
    } else if (ends_with_apk(e.name)) {
        open_apk_modal(full);
    } else if (ends_with_class(e.name)) {
        static uint8_t jbuf[4096];
        int fd = vfs_open(full, VFS_O_RDONLY);
        if (fd >= 0) {
            int n = vfs_read(fd, jbuf, sizeof(jbuf));
            vfs_close(fd);
            struct jvm_run_result jr;
            term_init_once();
            if (n > 0 && jvm_run_class(jbuf, (uint32_t)n, &jr) == 0) {
                term_log(full);
                for (int i = 0; i < jr.line_count; i++) term_log(jr.lines[i]);
                focus_win(WIN_TERM);
            } else {
                scpy(finder_status, "Invalid .class", sizeof(finder_status));
            }
        }
    } else {
        notes_load(full);
        focus_win(WIN_NOTES);
    }
}

static void term_toybox_emit(const char *line, void *ctx) {
    (void)ctx;
    term_log(line);
}

static void term_exec(const char *cmd) {
    term_init_once();
    char echo[TERM_COLS];
    echo[0] = '$'; echo[1] = ' ';
    scpy(echo + 2, cmd, TERM_COLS - 2);
    term_log(echo);

    while (*cmd == ' ') cmd++;
    if (!cmd[0]) return;
    if (seq(cmd, "help")) {
        term_log("ls cd pwd cat mkdir touch rm wc grep head tail cp kilo apk java python mem clear");
    } else if (seq(cmd, "clear")) {
        term_count = 0;
    } else if (seq(cmd, "pwd")) {
        term_log(term_cwd);
    } else if (seq(cmd, "mem")) {
        char mb[40];
        scpy(mb, "Heap KB: ", sizeof(mb));
        fmt_u32((uint32_t)(kmem64_used_bytes() / 1024u), mb + 9, sizeof(mb) - 9);
        term_log(mb);
    } else if (seq(cmd, "ls")) {
        struct vfs_dirent e;
        char line[TERM_COLS];
        line[0] = 0;
        for (uint32_t i = 0; i < 12; i++) {
            if (vfs_readdir(term_cwd, i, &e) != 0) break;
            int p = slen(line);
            if (p + slen(e.name) + 3 >= TERM_COLS) { term_log(line); line[0] = 0; p = 0; }
            scpy(line + p, e.name, TERM_COLS - p);
            p = slen(line);
            if (e.type == VFS_TYPE_DIR && p + 2 < TERM_COLS) { line[p++] = '/'; line[p] = 0; }
            if (p + 2 < TERM_COLS) { line[p++] = ' '; line[p] = 0; }
        }
        if (line[0]) term_log(line);
    } else if (starts_with(cmd, "cd ")) {
        const char *arg = cmd + 3;
        if (seq(arg, "..")) parent_path(term_cwd);
        else if (arg[0] == '/') {
            struct vfs_stat st;
            if (vfs_stat(arg, &st) == 0 && st.type == VFS_TYPE_DIR) scpy(term_cwd, arg, sizeof(term_cwd));
            else term_log("cd: no such dir");
        } else {
            char tmp[VFS_PATH_MAX];
            struct vfs_stat st;
            if (join_path(tmp, sizeof(tmp), term_cwd, arg) == 0 &&
                vfs_stat(tmp, &st) == 0 && st.type == VFS_TYPE_DIR)
                scpy(term_cwd, tmp, sizeof(term_cwd));
            else term_log("cd: no such dir");
        }
    } else if (starts_with(cmd, "mkdir ")) {
        char tmp[VFS_PATH_MAX];
        if (cmd[6] == '/') scpy(tmp, cmd + 6, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 6);
        term_log(vfs_mkdir(tmp) == 0 ? "mkdir ok" : "mkdir failed");
    } else if (starts_with(cmd, "touch ")) {
        char tmp[VFS_PATH_MAX];
        if (cmd[6] == '/') scpy(tmp, cmd + 6, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 6);
        int fd = vfs_open(tmp, VFS_O_CREAT | VFS_O_WRONLY);
        if (fd >= 0) { vfs_close(fd); term_log("created"); } else term_log("touch failed");
    } else if (starts_with(cmd, "rm ")) {
        char tmp[VFS_PATH_MAX];
        if (cmd[3] == '/') scpy(tmp, cmd + 3, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 3);
        term_log(vfs_unlink(tmp) == 0 ? "removed" : "rm failed");
    } else if (starts_with(cmd, "cat ")) {
        char tmp[VFS_PATH_MAX], buf[128];
        if (cmd[4] == '/') scpy(tmp, cmd + 4, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 4);
        int fd = vfs_open(tmp, VFS_O_RDONLY);
        if (fd >= 0) {
            int n = vfs_read(fd, buf, sizeof(buf) - 1);
            vfs_close(fd);
            if (n > 0) {
                buf[n] = 0;
                for (int i = 0; i < n; i++) if (buf[i] == '\n') buf[i] = ' ';
                term_log(buf);
            }
        } else term_log("cat: not found");
    } else if (starts_with(cmd, "java ")) {
        char tmp[VFS_PATH_MAX];
        static uint8_t jbuf[4096];
        if (cmd[5] == '/') scpy(tmp, cmd + 5, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 5);
        int fd = vfs_open(tmp, VFS_O_RDONLY);
        if (fd >= 0) {
            int n = vfs_read(fd, jbuf, sizeof(jbuf));
            vfs_close(fd);
            struct jvm_run_result jr;
            if (n > 0 && jvm_run_class(jbuf, (uint32_t)n, &jr) == 0) {
                for (int i = 0; i < jr.line_count; i++) term_log(jr.lines[i]);
            } else {
                term_log("java: invalid or unsupported class");
            }
        } else {
            term_log("java: class not found");
        }
    } else if (starts_with(cmd, "apk ")) {
        char tmp[VFS_PATH_MAX];
        if (cmd[4] == '/') scpy(tmp, cmd + 4, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 4);
        open_apk_modal(tmp);
    } else if (seq(cmd, "python") || seq(cmd, "py")) {
        term_log("CPython 3.14 (FlairOS): python <expr> | <file.py>");
    } else if (starts_with(cmd, "python ") || starts_with(cmd, "py ")) {
        const char *arg = starts_with(cmd, "python ") ? (cmd + 7) : (cmd + 3);
        while (*arg == ' ') arg++;
        int alen = slen(arg);
        if (alen >= 3 && arg[alen - 3] == '.' && arg[alen - 2] == 'p' && arg[alen - 1] == 'y') {
            char tmp[VFS_PATH_MAX];
            if (arg[0] == '/') scpy(tmp, arg, sizeof(tmp));
            else (void)join_path(tmp, sizeof(tmp), term_cwd, arg);
            if (flair_py_run_file(tmp, term_toybox_emit, 0) != 0) term_log("python: file error");
        } else {
            (void)flair_py_eval_line(arg, term_toybox_emit, 0);
        }
    } else if (starts_with(cmd, "kilo ")) {
        char tmp[VFS_PATH_MAX];
        if (cmd[5] == '/') scpy(tmp, cmd + 5, sizeof(tmp));
        else (void)join_path(tmp, sizeof(tmp), term_cwd, cmd + 5);
        notes_load(tmp);
        focus_win(WIN_NOTES);
    } else if (starts_with(cmd, "wc ") || starts_with(cmd, "grep ") ||
               starts_with(cmd, "head ") || starts_with(cmd, "tail ") ||
               starts_with(cmd, "cp ") || starts_with(cmd, "hexdump ")) {
        char copy_cmd[48];
        const char *argv[4] = {0, 0, 0, 0};
        int argc = 0;
        scpy(copy_cmd, cmd, sizeof(copy_cmd));
        char *p = copy_cmd;
        while (*p && argc < 4) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = 0;
        }
        (void)toybox_run_cmd(argc, argv, term_cwd, term_toybox_emit, 0);
    } else {
        term_log("unknown cmd (try help)");
    }
}

int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h) {
    if (!in) return 0;
    term_init_once();
    if (!notes_loaded) notes_load("/home/NOTE.TXT");
    int dirty = 0;
    int32_t mx = in->mouse_x, my = in->mouse_y;

    desk_ensure(0);
    if (desk_density(w, h)) dirty = 1;

    /* Track Dock hover for highlight + floating tooltip */
    {
        int32_t dw, dh, dx, dy;
        int nh = -1;
        desk_dock_geometry(w, h, &dx, &dy, &dw, &dh);
        if (hit(mx, my, dx, dy, dw, dh)) {
            for (int i = 0; i < g_launcher.dock_count && i < 8; i++) {
                if (hit(mx, my, desk_dock_icon_x(dx, i), dy + desk_ui_px(12),
                        desk_ui_px(52), desk_ui_px(52))) { nh = i; break; }
            }
        }
        if (nh != dock_hover) {
            dock_hover = nh;
            dock_anim_tick = 0;
            dock_anim_frame = 0;
            dirty = 1;
        }
        dock_pointer_x = (nh >= 0) ? (mx - dx) : -10000;
    }

    if (dw_active(desk_dw())) {
        struct dw_state *st = desk_dw();
        if (st->drag_id >= 0 && (in->buttons & 1u)) {
            if (dw_drag_to(st, &wins[st->drag_id], st->drag_id, mx, my)) dirty = 1;
        } else if (st->drag_id >= 0) {
            dw_end_drag(st);
            dirty = 1;
        }
    }
    if (!dw_active(desk_dw()) && dragging_win >= 0 && (in->buttons & 1u)) {
        int32_t nx = mx - drag_ox, ny = my - drag_oy;
        if (ny < 34) ny = 34;
        if (ny > h - 100) ny = h - 100;
        if (wins[dragging_win].x != nx || wins[dragging_win].y != ny) {
            wins[dragging_win].x = nx;
            wins[dragging_win].y = ny;
            return 1;
        }
        return 0;
    } else if (!(in->buttons & 1u)) {
        dragging_win = -1;
    }

    if (in->clicked) {
        /* 0a. Power & Shutdown modal takes top priority */
        if (airstin_power_state()->modal_open) {
            if (!airstin_power_state()->shutting_down) {
                int32_t mw = 380, mh = 188, mx0 = (w - mw) / 2, my0 = (h - mh) / 2;
                int32_t bw = (mw - 52) / 2, by = my0 + mh - 46;
                if (hit(mx, my, mx0 + 18, by, bw, 32) || !hit(mx, my, mx0, my0, mw, mh)) {
                    airstin_power_cancel(); return 1;
                }
                if (hit(mx, my, mx0 + 34 + bw, by, bw, 32)) {
                    airstin_power_tap_shutdown(); return 1;
                }
            }
            return 1;
        }

        /* 0. AirStinUI Installer modal takes priority over everything */
        if (airstin_ui_installer()->active) {
            int32_t dw = 400, dh = 236, dx = (w - dw) / 2, dy = (h - dh) / 2;
            int32_t bw = (dw - 44) / 2, by = dy + dh - 42;
            if (hit(mx, my, dx + 16, by, bw, 28) || !hit(mx, my, dx, dy, dw, dh)) {
                airstin_ui_cancel_install(); return 1;
            }
            if (hit(mx, my, dx + 28 + bw, by, bw, 28)) {
                start_apk_install(); return 1;
            }
            return 0;
        }

        /* 1. Dropdown menu items */
        if (active_menu >= 0) {
            int32_t mxs[5] = { desk_ui_px(12), desk_ui_px(118), desk_ui_px(204),
                               desk_ui_px(270), desk_ui_px(356) };
            int32_t mx0 = mxs[active_menu], my0 = desk_ui_px(34);
            int32_t mw = desk_ui_px(190), mh = desk_ui_px(104);
            if (hit(mx, my, mx0, my0, mw, mh)) {
                int row = (my - my0 - desk_ui_px(6)) / desk_ui_px(30);
                if (active_menu == 0) {
                    if (row == 0) focus_win(WIN_PREFS);
                    else if (row == 1) open_apk_modal("/apps/sysinfo.apk");
                    else if (row == 2) { for (int i = 0; i < WIN_COUNT; i++) wins[i].open = 0; }
                } else if (active_menu == 1) {
                    focus_win(WIN_FINDER);
                    scpy(finder_path, row == 1 ? "/apps" : (row == 2 ? "/home" : "/"), sizeof(finder_path));
                    finder_sel = 0;
                } else if (active_menu == 2) {
                    if (row == 0) { notes_load("/home/NOTE.TXT"); focus_win(WIN_NOTES); }
                    else if (row == 1) {
                        char nf[32]; scpy(nf, "/DIR", 32); fmt_u32((uint32_t)folder_seq++, nf + 4, 12);
                        (void)vfs_mkdir(nf); focus_win(WIN_FINDER);
                    } else if (row == 2) finder_open_idx(finder_sel);
                } else if (active_menu == 3) {
                    int tw = top_win();
                    if (row == 0) {
                        wins[WIN_FINDER].x = 24; wins[WIN_FINDER].y = 48; wins[WIN_FINDER].open = 1; wins[WIN_FINDER].minimized = 0;
                        wins[WIN_TERM].x = w / 2 + 8; wins[WIN_TERM].y = 48; wins[WIN_TERM].open = 1; wins[WIN_TERM].minimized = 0;
                    } else if (row == 1 && tw >= 0) wins[tw].minimized = 1;
                    else if (row == 2 && tw >= 0) wins[tw].open = 0;
                } else if (active_menu == 4) {
                    theme_idx = row % 3;
                }
                active_menu = -1;
                return 1;
            }
            active_menu = -1;
            dirty = 1;
        }

        /* 1b. AirStinUI Control Center popover hits */
        if (cc_open) {
            if (airstin_cc_handle_click(mx, my, w, &anim_on, &theme_idx, &spotlight_open, &cc_open)) {
                return 1;
            }
            if (!(my < 34 && mx >= w - 246)) {
                cc_open = 0;
                airstin_cc_set_view(CC_VIEW_ROOT);
                dirty = 1;
            }
        }

        /* 1c. Spotlight Search hits */
        if (spotlight_open) {
            int32_t sp_w = 420, sp_x = (w - sp_w) / 2, sp_y = 64 + spotlight_anim_y;
            if (hit(mx, my, sp_x, sp_y, sp_w, 220)) {
                int row = (my - (sp_y + 52)) / 28;
                if (row >= 0 && row < 5) {
                    struct vfs_dirent e;
                    int matched = 0;
                    for (uint32_t i = 0; vfs_readdir("/apps", i, &e) == 0; i++) {
                        if (e.type == VFS_TYPE_DIR || !contains_icase(e.name, spotlight_q)) continue;
                        if (matched == row) {
                            char full[VFS_PATH_MAX];
                            if (join_path(full, sizeof(full), "/apps", e.name) == 0) {
                                spotlight_open = 0;
                                if (ends_with_airp(e.name)) open_airp_app(full);
                                else if (ends_with_apk(e.name)) open_apk_modal(full);
                                else { scpy(finder_path, "/apps", sizeof(finder_path)); finder_open_idx((int)i + 1); }
                            }
                            return 1;
                        }
                        matched++;
                    }
                }
                return 1;
            }
            spotlight_open = 0;
            dirty = 1;
        }

        /* 2. Top Menu Bar hits */
        if (my < desk_ui_px(34)) {
            if (hit(mx, my, w - 246, 0, 238, desk_ui_px(34))) {
                cc_open = !cc_open;
                if (!cc_open) airstin_cc_set_view(CC_VIEW_ROOT);
                active_menu = -1;
                return 1;
            }
            int32_t mxs[5] = { desk_ui_px(12), desk_ui_px(118), desk_ui_px(204),
                               desk_ui_px(270), desk_ui_px(356) };
            int32_t mws[5] = { desk_ui_px(96), desk_ui_px(80), desk_ui_px(60),
                               desk_ui_px(80), desk_ui_px(64) };
            for (int i = 0; i < 5; i++) {
                if (hit(mx, my, mxs[i], 0, mws[i], desk_ui_px(34))) {
                    cc_open = 0; active_menu = i; return 1;
                }
            }
            return dirty;
        }

        /* 3. Dock hits: the package's dock order decides what each slot opens */
        int32_t dw, dh, dx, dy;
        desk_dock_geometry(w, h, &dx, &dy, &dw, &dh);
        if (hit(mx, my, dx, dy, dw, dh)) {
            for (int i = 0; i < g_launcher.dock_count && i < 8; i++) {
                int32_t ix = desk_dock_icon_x(dx, i);
                int ti, wid;
                if (!hit(mx, my, ix, dy + desk_ui_px(12),
                         desk_ui_px(52), desk_ui_px(52))) continue;
                ti = desk_dock_tile(i);
                if (ti < 0) return 0;
                wid = desk_target_window(g_launcher.tiles[ti].target);
                if (wid < 0) return 0;
                if (wid == WIN_APK && !art_am_active()) open_apk_modal("/apps/hello.apk");
                else focus_win(wid);
                return 1;
            }
        }

        /* 4. Windows top-to-bottom. Density mode dispatches chrome first
           through the same function the headless harness drives. */
        if (dw_active(desk_dw())) {
            int hit_slot = -1;
            enum dw_click acted = DW_CLICK_NONE;
            if (dw_dispatch_click(desk_dw(), wins, z_order, WIN_COUNT, mx, my,
                                  &hit_slot, &acted))
                return 1;
        }
        for (int zi = WIN_COUNT - 1; zi >= 0; zi--) {
            int id = z_order[zi];
            struct dw_window *win = &wins[id];
            if (!win->open || win->minimized) continue;
            if (!hit(mx, my, win->x, win->y, win->w, win->h)) continue;
            focus_win(id);
            if (dw_active(desk_dw())) {
                /* Chrome was dispatched before this loop; what is left is a
                   client click. Map it back into content design coordinates so
                   the same handlers work at every density. */
                struct dw_client c = dw_client_rect(desk_dw(), &wins[id]);
                int32_t sc = desk_dw()->scale_q16;
                mx = wins[id].x + (int32_t)(((int64_t)(mx - c.x) << 16) / sc);
                my = wins[id].y + (int32_t)(((int64_t)(my - c.y) << 16) / sc);
            } else {
            /* Traffic lights */
            if (hit(mx, my, win->x + 12, win->y + 10, 14, 14)) { win->open = 0; return 1; }
            if (hit(mx, my, win->x + 28, win->y + 10, 14, 14)) { win->minimized = 1; return 1; }
            if (hit(mx, my, win->x + 44, win->y + 10, 14, 14)) {
                if (!win->zoomed) {
                    win->sx = win->x; win->sy = win->y; win->sw = win->w; win->sh = win->h;
                    win->x = 16; win->y = 42; win->w = w - 32; win->h = h - 142; win->zoomed = 1;
                } else {
                    win->x = win->sx; win->y = win->sy; win->w = win->sw; win->h = win->sh; win->zoomed = 0;
                }
                return 1;
            }
            /* Titlebar drag */
            if (hit(mx, my, win->x, win->y, win->w, 36)) {
                dragging_win = id; drag_ox = mx - win->x; drag_oy = my - win->y;
                return 1;
            }
            }
            /* Client clicks per window */
            if (id == WIN_FINDER) {
                /* Sidebar Places */
                const char *places[3] = {"/", "/apps", "/home"};
                for (int p_i = 0; p_i < 3; p_i++) {
                    if (hit(mx, my, win->x + 12, win->y + 72 + p_i * 32, 108, 26)) {
                        scpy(finder_path, places[p_i], sizeof(finder_path));
                        finder_sel = 0;
                        scpy(finder_status, places[p_i], sizeof(finder_status));
                        return 1;
                    }
                }
                int32_t cx = win->x + 132, ty = win->y + 44;
                if (hit(mx, my, cx, ty, 48, 24)) { parent_path(finder_path); finder_sel = 0; return 1; }
                if (hit(mx, my, cx + 54, ty, 66, 24)) {
                    char nf[32]; scpy(nf, "DIR", 32); fmt_u32((uint32_t)folder_seq++, nf + 3, 12);
                    char full[VFS_PATH_MAX];
                    if (join_path(full, sizeof(full), finder_path, nf) == 0) (void)vfs_mkdir(full);
                    return 1;
                }
                if (hit(mx, my, cx + 126, ty, 70, 24)) {
                    char nf[32]; scpy(nf, "NOTE", 32); fmt_u32((uint32_t)note_seq++, nf + 4, 12);
                    char full[VFS_PATH_MAX];
                    if (join_path(full, sizeof(full), finder_path, nf) == 0) {
                        int fd = vfs_open(full, VFS_O_CREAT | VFS_O_WRONLY);
                        if (fd >= 0) vfs_close(fd);
                    }
                    return 1;
                }
                if (hit(mx, my, cx + 202, ty, 56, 24)) {
                    int idx = finder_sel;
                    if (!seq(finder_path, "/")) idx--;
                    if (idx >= 0) {
                        struct vfs_dirent e;
                        char full[VFS_PATH_MAX];
                        if (vfs_readdir(finder_path, (uint32_t)idx, &e) == 0 &&
                            join_path(full, sizeof(full), finder_path, e.name) == 0)
                            (void)vfs_unlink(full);
                    }
                    return 1;
                }
                if (hit(mx, my, cx + 264, ty, 64, 24)) {
                    finder_open_idx(finder_sel);
                    return 1;
                }
                int row = (my - (win->y + 98)) / 26;
                if (mx >= cx && row >= 0 && row < 8) {
                    if (row == finder_sel) finder_open_idx(row);
                    else finder_sel = row;
                    return 1;
                }
            } else if (id == WIN_NOTES) {
                int32_t ty = win->y + 44;
                if (hit(mx, my, win->x + win->w - 236, ty, 68, 26)) {
                    char nf[32]; scpy(nf, "/home/NOTE", 32); fmt_u32((uint32_t)note_seq++, nf + 10, 8);
                    int l = slen(nf); scpy(nf + l, ".TXT", 32 - l);
                    int fd = vfs_open(nf, VFS_O_CREAT | VFS_O_WRONLY);
                    if (fd >= 0) vfs_close(fd);
                    notes_load(nf);
                    return 1;
                }
                if (hit(mx, my, win->x + win->w - 160, ty, 72, 26)) {
                    notes_load(notes_path); return 1;
                }
                if (hit(mx, my, win->x + win->w - 82, ty, 68, 26)) {
                    notes_save(); return 1;
                }
            } else if (id == WIN_LAUNCHER) {
                struct vfs_dirent e;
                int tile = 0;
                for (uint32_t i = 0; tile < 9 && vfs_readdir("/apps", i, &e) == 0; i++) {
                    if (e.type == VFS_TYPE_DIR) continue;
                    int col = tile % 3, row = tile / 3;
                    int32_t tx = win->x + 18 + col * 164;
                    int32_t ty = win->y + 52 + row * 92;
                    if (hit(mx, my, tx, ty, 152, 80)) {
                        char full[VFS_PATH_MAX];
                        if (join_path(full, sizeof(full), "/apps", e.name) == 0) {
                            if (ends_with_airp(e.name)) open_airp_app(full);
                            else if (ends_with_apk(e.name)) open_apk_modal(full);
                            else if (ends_with_class(e.name)) {
                                scpy(finder_path, "/apps", sizeof(finder_path));
                                finder_open_idx((int)i + 1);
                            }
                        }
                        return 1;
                    }
                    tile++;
                }
                return 1;
            } else if (id == WIN_APK) {
                if (hit(mx, my, win->x + win->w - 176, win->y + win->h - 76, 156, 34)) {
                    const char *tag = airp_app_active ? airp_app_name :
                                      (art_am_active() ? art_am_active()->ui.title : "WinDown");
                    int sid = sf_find_by_tag(tag);
                    if (sid >= 0) {
                        sf_destroy_surface(sid);
                    } else {
                        sid = sf_create_surface(tag, win->w - 28, win->h - 128);
                        if (sid >= 0) {
                            int32_t sw = 0, sh = 0;
                            uint32_t *px = sf_lock_buffer(sid, &sw, &sh);
                            if (px) {
                                struct ag_surface app_surf = { px, sw, sh, sw * 4, AG_PIXEL_XRGB8888 };
                                gfx_fill_round_rect(&app_surf, 0, 0, sw, sh, 10, THEME_GLASS_DARK, 255u);
                                gfx_stroke_round_rect(&app_surf, 0, 0, sw, sh, 10, THEME_ACCENT_TEAL, 200u);
                                gfx_draw_text_aa(&app_surf, 16, 16, tag, THEME_TEXT_PRIMARY, 2);
                                gfx_draw_text_aa(&app_surf, 16, 48, art_bridge_abi(), THEME_ACCENT_TEAL, 2);
                                char sbuf[32]; scpy(sbuf, "SF Surface #", sizeof(sbuf));
                                fmt_u32((uint32_t)sid, sbuf + 12, sizeof(sbuf) - 12);
                                gfx_draw_text_aa(&app_surf, 16, 80, sbuf, THEME_TEXT_MUTED, 2);
                                for (int32_t i = 0; i < sw * sh; i++) px[i] |= 0xFF000000u;
                                sf_unlock_and_post(sid);
                            }
                        }
                    }
                    return 1;
                }
                if (!airp_app_active && hit(mx, my, win->x + 20, win->y + win->h - 76, 170, 34)) {
                    art_am_trigger_button(0); return 1;
                }
            } else if (id == WIN_PREFS) {
                for (int tb = 0; tb < 4; tb++) {
                    if (hit(mx, my, win->x + 12, win->y + 52 + tb * 36, 118, 30)) {
                        prefs_tab = tb; return 1;
                    }
                }
                int32_t px = win->x + 144;
                if (prefs_tab == 0) {
                    for (int t = 0; t < 3; t++) {
                        if (hit(mx, my, px + t * 106, win->y + 92, 98, 34)) {
                            theme_idx = t; return 1;
                        }
                    }
                } else if (prefs_tab == 1) {
                    if (hit(mx, my, px, win->y + 78, 280, 36)) { anim_on = !anim_on; return 1; }
                    if (hit(mx, my, px, win->y + 126, 280, 36)) { tooltips_on = !tooltips_on; return 1; }
                    if (hit(mx, my, px, win->y + 174, 280, 36)) { target_hz = (target_hz == 60) ? 90 : 60; return 1; }
                } else if (prefs_tab == 2) {
                    if (hit(mx, my, px, win->y + 78, 280, 36)) { wifi_on = !wifi_on; return 1; }
                }
            } else if (id == WIN_BROWSER) {
                return flair_browser_handle_click(mx - win->x, my - win->y, win->w, win->h);
            } else if (id == WIN_CALC) {
                static const char cmap[5][4] = {
                    {'C', 'n', '%', '/'},
                    {'7', '8', '9', '*'},
                    {'4', '5', '6', '-'},
                    {'1', '2', '3', '+'},
                    {'0', '0', '\b', '='}
                };
                int32_t gx = win->x + 14, gy = win->y + 114, bw = 64, bh = 42, gap = 5;
                for (int r = 0; r < 5; r++) {
                    for (int c = 0; c < 4; c++) {
                        int32_t bx = gx + c * (bw + gap);
                        int32_t by = gy + r * (bh + gap);
                        if (hit(mx, my, bx, by, bw, bh)) {
                            calc_press(cmap[r][c]);
                            return 1;
                        }
                    }
                }
            }
            return 1;
        }
    }

    if (in->key) {
        if (airstin_power_state()->modal_open) {
            if (in->key == INPUT64_KEY_ESC) { airstin_power_cancel(); return 1; }
            if (in->key == INPUT64_KEY_ENTER) { airstin_power_tap_shutdown(); return 1; }
            return 0;
        }
        if (airstin_ui_installer()->active) {
            if (in->key == INPUT64_KEY_ESC) { airstin_ui_cancel_install(); return 1; }
            if (in->key == INPUT64_KEY_ENTER) { start_apk_install(); return 1; }
            return 0;
        }
        if (spotlight_open) {
            if (in->key == INPUT64_KEY_ESC) { spotlight_open = 0; return 1; }
            if (in->key == '\b' && spotlight_len > 0) { spotlight_q[--spotlight_len] = 0; return 1; }
            if (in->key >= 32 && in->key < 127 && spotlight_len + 1 < (int)sizeof(spotlight_q)) {
                spotlight_q[spotlight_len++] = (char)in->key; spotlight_q[spotlight_len] = 0; return 1;
            }
            return 0;
        }
        int tw = top_win();
        if (tw == WIN_TERM) {
            if (in->key == '\n') { term_in[term_in_len] = 0; term_exec(term_in); term_in_len = 0; term_in[0] = 0; return 1; }
            if (in->key == '\b' && term_in_len > 0) { term_in[--term_in_len] = 0; return 1; }
            if (in->key >= 32 && in->key < 127 && term_in_len + 1 < (int)sizeof(term_in)) {
                term_in[term_in_len++] = (char)in->key; term_in[term_in_len] = 0; return 1;
            }
        } else if (tw == WIN_NOTES) {
            if (in->key == '\b' && notes_len > 0) { notes_buf[--notes_len] = 0; return 1; }
            if ((in->key == '\n' || (in->key >= 32 && in->key < 127)) && notes_len + 1 < (int)sizeof(notes_buf)) {
                notes_buf[notes_len++] = (char)in->key; notes_buf[notes_len] = 0; return 1;
            }
        } else if (tw == WIN_FINDER) {
            if (in->key == INPUT64_KEY_UP && finder_sel > 0) { finder_sel--; return 1; }
            if (in->key == INPUT64_KEY_DOWN && finder_sel < 7) { finder_sel++; return 1; }
            if (in->key == INPUT64_KEY_ENTER) { finder_open_idx(finder_sel); return 1; }
        } else if (tw == WIN_BROWSER) {
            if (flair_browser_handle_key(in->key)) return 1;
        } else if (tw == WIN_CALC) {
            char k = (char)in->key;
            if ((k >= '0' && k <= '9') || k == '+' || k == '-' || k == '*' || k == '/' ||
                k == '=' || k == '\n' || k == '\b' || k == '%' || k == 'c' || k == 'C') {
                calc_press(k);
                return 1;
            }
        }
    }
    return dirty;
}

static void draw_win_frame(struct ag_surface *s, const struct dw_window *win, int focused) {
    if (focused) gfx_fill_round_rect(s, win->x + 6, win->y + 8, win->w, win->h, 16, 0x0002040Au, 95u);
    gfx_glass_panel(s, win->x, win->y, win->w, win->h, 14,
                    focused ? THEME_GLASS_DARK : THEME_GLASS_LIGHT, focused ? 232u : 205u);
    gfx_traffic_lights(s, win->x + 14, win->y + 12);
    gfx_draw_text_scaled(s, win->x + 74, win->y + 11, win->title, THEME_TEXT_PRIMARY, 2);
    gfx_fill_rect_alpha(s, win->x + 10, win->y + 36, win->w - 20, 1, THEME_GLASS_BORDER, 130u);
}

void shell_render(struct ag_surface *s, const struct shell_boot_info *info, int frame_idx) {
    (void)frame_idx;
    if (!s || !info) return;
    desk_ensure(info);
    term_init_once();
    if (!notes_loaded) notes_load("/home/NOTE.TXT");
    int32_t w = s->width, h = s->height;
    desk_density(w, h);

    uint32_t top_c = theme_idx == 1 ? 0x000D1E3Au : (theme_idx == 2 ? 0x002B1028u : THEME_BG_TOP);
    uint32_t bot_c = theme_idx == 1 ? 0x00163B5Cu : (theme_idx == 2 ? 0x0052193Au : THEME_BG_BOT);
    if (!wp_cache && w > 0 && h > 0) wp_cache = (uint32_t *)kmem64_alloc((size_t)w * (size_t)h * 4u);
    if (wp_cache) {
        struct ag_surface ws = { wp_cache, w, h, w * 4, AG_PIXEL_XRGB8888 };
        if (wp_theme != theme_idx || wp_w != w || wp_h != h) {
            gfx_gradient_v(&ws, 0, 0, w, h, top_c, bot_c);
            wp_theme = theme_idx; wp_w = w; wp_h = h;
        }
        gfx_copy_fast(s, &ws);
    } else {
        gfx_gradient_v(s, 0, 0, w, h, top_c, bot_c);
    }

    /* Top macOS Menu Bar (AirGraph density; 1x at 720p and below) */
    int32_t bar_h = desk_ui_px(34);
    int32_t bar_text_y = desk_ui_px(10);
    int32_t menu_x[5] = { desk_ui_px(16), desk_ui_px(122), desk_ui_px(208),
                          desk_ui_px(274), desk_ui_px(360) };
    int32_t menu_ts = desk_ui_text_scale(2);
    gfx_fill_rect_alpha(s, 0, 0, w, bar_h, THEME_GLASS_DARK, 215u);
    gfx_fill_rect_alpha(s, 0, bar_h, w, 1, THEME_GLASS_BORDER, 160u);
    gfx_draw_text_scaled(s, menu_x[0], bar_text_y, desk_label("menu.apple", "FlairOS"), THEME_TEXT_PRIMARY, menu_ts);
    gfx_draw_text_scaled(s, menu_x[1], bar_text_y, desk_label("menu.finder", "Finder"), active_menu == 1 ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED, menu_ts);
    gfx_draw_text_scaled(s, menu_x[2], bar_text_y, desk_label("menu.file", "File"), active_menu == 2 ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED, menu_ts);
    gfx_draw_text_scaled(s, menu_x[3], bar_text_y, desk_label("menu.window", "Window"), active_menu == 3 ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED, menu_ts);
    gfx_draw_text_scaled(s, menu_x[4], bar_text_y, desk_label("menu.view", "View"), active_menu == 4 ? THEME_ACCENT_BLUE : THEME_TEXT_MUTED, menu_ts);
    airstin_render_status_cluster(s, w, info->arch_name, cc_open);

    int tw = top_win();
    for (int zi = 0; zi < WIN_COUNT; zi++) {
        int id = z_order[zi];
        if (!wins[id].open || wins[id].minimized) continue;
        struct dw_window wdraw = wins[id];
        struct dw_state *dst = desk_dw();
        int density = dw_active(dst);
        wdraw.y += dw_open_offset(dst, id);
        const struct dw_window *win = &wdraw;
        if (density) {
            struct dw_client client;
            struct dw_window design;
            int32_t ox, oy;
            dw_draw_chrome(dst, s, id, win, id == tw);
            /* Content is authored in 720p coordinates. Map that space into the
               scaled client rectangle so insets and text grow with the panel,
               and clip it there so content cannot paint over chrome. */
            client = dw_client_rect(dst, win);
            design = wdraw;
            /* Use the client, not the outer frame: content authored at the
               window's bottom edge would otherwise be clipped by the scaled
               title bar and border. */
            design.w = (int32_t)(((int64_t)client.w << 16) / dst->scale_q16);
            design.h = (int32_t)(((int64_t)client.h << 16) / dst->scale_q16);
            ox = client.x - (int32_t)(((int64_t)win->x * dst->scale_q16) >> 16);
            oy = client.y - (int32_t)(((int64_t)win->y * dst->scale_q16) >> 16);
            gfx_transform_set(dst->scale_q16, ox, oy, client.x, client.y,
                              client.w, client.h);
            win = &design;
        } else {
            draw_win_frame(s, win, id == tw);
        }

        if (id == WIN_FINDER) {
            /* Left Places Sidebar */
            gfx_fill_round_rect(s, win->x + 10, win->y + 42, 114, win->h - 54, 8, THEME_GLASS_LIGHT, 130u);
            gfx_draw_text_scaled(s, win->x + 18, win->y + 50, "Places", THEME_TEXT_MUTED, 1);
            const char *pnames[3] = {"Root /", "Apps", "Home"};
            const char *ppaths[3] = {"/", "/apps", "/home"};
            for (int p_i = 0; p_i < 3; p_i++) {
                int act = seq(finder_path, ppaths[p_i]);
                if (act) gfx_fill_round_rect(s, win->x + 14, win->y + 72 + p_i * 32, 106, 26, 6, THEME_ACCENT_BLUE, 210u);
                gfx_draw_text_scaled(s, win->x + 22, win->y + 78 + p_i * 32, pnames[p_i], THEME_TEXT_PRIMARY, 2);
            }

            int32_t cx = win->x + 132, ty = win->y + 44;
            gfx_fill_round_rect(s, cx, ty, 48, 24, 6, THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, cx + 10, ty + 6, "Up", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, cx + 54, ty, 66, 24, 6, THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, cx + 62, ty + 6, "+Dir", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, cx + 126, ty, 70, 24, 6, THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, cx + 132, ty + 6, "+Note", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, cx + 202, ty, 56, 24, 6, THEME_ACCENT_PINK, 200u);
            gfx_draw_text_scaled(s, cx + 210, ty + 6, "Del", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, cx + 264, ty, 64, 24, 6, THEME_ACCENT_BLUE, 220u);
            gfx_draw_text_scaled(s, cx + 272, ty + 6, "Open", THEME_TEXT_PRIMARY, 2);

            gfx_fill_round_rect(s, cx, win->y + 73, win->w - 144, 18, 5, THEME_GLASS_LIGHT, 150u);
            gfx_draw_text_scaled(s, cx + 8, win->y + 78, finder_path, THEME_ACCENT_TEAL, 1);

            int row = 0;
            if (!seq(finder_path, "/")) {
                if (row == finder_sel) gfx_fill_round_rect(s, cx, win->y + 98, win->w - 144, 24, 6, THEME_ACCENT_BLUE, 180u);
                gfx_draw_text_scaled(s, cx + 8, win->y + 103, "[DIR] ..", THEME_TEXT_PRIMARY, 2);
                row++;
            }
            struct vfs_dirent e;
            for (uint32_t i = 0; row < 8 && vfs_readdir(finder_path, i, &e) == 0; i++, row++) {
                int32_t ry = win->y + 98 + row * 26;
                if (row == finder_sel) gfx_fill_round_rect(s, cx, ry, win->w - 144, 24, 6, THEME_ACCENT_BLUE, 180u);
                const char *tag = e.type == VFS_TYPE_DIR ? "[DIR]" :
                                  (ends_with_airp(e.name) ? "[ARP]" : (ends_with_apk(e.name) ? "[APK]" :
                                  (ends_with_class(e.name) ? "[JVM]" : "[TXT]")));
                gfx_draw_text_scaled(s, cx + 8, ry + 5, tag, THEME_ACCENT_TEAL, 2);
                gfx_draw_text_scaled(s, cx + 76, ry + 5, e.name, THEME_TEXT_PRIMARY, 2);
                if (e.type != VFS_TYPE_DIR) {
                    char sz[16]; fmt_u32(e.size, sz, sizeof(sz));
                    int l = slen(sz); scpy(sz + l, "B", (int)sizeof(sz) - l);
                    gfx_draw_text_scaled(s, win->x + win->w - 64, ry + 8, sz, THEME_TEXT_MUTED, 1);
                }
            }
            gfx_draw_text_scaled(s, cx, win->y + win->h - 22, finder_status, THEME_TEXT_MUTED, 1);
        } else if (id == WIN_TERM) {
            for (int i = 0; i < term_count; i++)
                gfx_draw_text_scaled(s, win->x + 16, win->y + 46 + i * 20, term_lines[i], THEME_TEXT_PRIMARY, 1);
            char prompt[TERM_COLS];
            scpy(prompt, "$ ", sizeof(prompt));
            scpy(prompt + 2, term_in, sizeof(prompt) - 2);
            int plen = slen(prompt);
            if (plen + 1 < TERM_COLS) { prompt[plen] = '_'; prompt[plen + 1] = 0; }
            gfx_fill_round_rect(s, win->x + 14, win->y + win->h - 38, win->w - 28, 26, 6, THEME_GLASS_LIGHT, 210u);
            gfx_draw_text_scaled(s, win->x + 22, win->y + win->h - 31, prompt, THEME_ACCENT_GREEN, 2);
        } else if (id == WIN_NOTES) {
            gfx_draw_text_scaled(s, win->x + 16, win->y + 52, notes_path, THEME_ACCENT_TEAL, 1);
            gfx_fill_round_rect(s, win->x + win->w - 236, win->y + 44, 68, 26, 6, THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, win->x + win->w - 224, win->y + 50, "+New", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, win->x + win->w - 160, win->y + 44, 72, 26, 6, THEME_GLASS_LIGHT, 220u);
            gfx_draw_text_scaled(s, win->x + win->w - 152, win->y + 50, "Load", THEME_TEXT_PRIMARY, 2);
            gfx_fill_round_rect(s, win->x + win->w - 82, win->y + 44, 68, 26, 6, THEME_ACCENT_BLUE, 235u);
            gfx_draw_text_scaled(s, win->x + win->w - 70, win->y + 50, "Save", THEME_TEXT_PRIMARY, 2);

            gfx_fill_round_rect(s, win->x + 14, win->y + 78, win->w - 28, win->h - 112, 8, THEME_GLASS_LIGHT, 120u);
            int row = 0, col = 0;
            char line[44];
            for (int i = 0; i <= notes_len && row < 9; i++) {
                char ch = (i < notes_len) ? notes_buf[i] : 0;
                if (ch == '\n' || ch == 0 || col >= 32) {
                    if (ch == 0 && col < 32) line[col++] = '_';
                    line[col] = 0;
                    char lnum[6]; fmt_u32((uint32_t)(row + 1), lnum, sizeof(lnum));
                    gfx_draw_text_scaled(s, win->x + 20, win->y + 88 + row * 22, lnum, THEME_TEXT_MUTED, 1);
                    gfx_draw_text_scaled(s, win->x + 42, win->y + 86 + row * 22, line, THEME_TEXT_PRIMARY, 2);
                    row++; col = 0;
                    if (ch == 0) break;
                } else {
                    line[col++] = ch;
                }
            }
            char nfoot[48];
            scpy(nfoot, notes_status, 24);
            int nf_l = slen(nfoot);
            scpy(nfoot + nf_l, " | Chars: ", sizeof(nfoot) - nf_l);
            nf_l = slen(nfoot);
            fmt_u32((uint32_t)notes_len, nfoot + nf_l, sizeof(nfoot) - nf_l);
            gfx_draw_text_scaled(s, win->x + 16, win->y + win->h - 24, nfoot, THEME_TEXT_MUTED, 1);
        } else if (id == WIN_LAUNCHER) {
            gfx_draw_text_scaled(s, win->x + 18, win->y + 40,
                                 desk_label("launcher.installed", "Installed & New Apps (/apps)"),
                                 THEME_TEXT_MUTED, 1);
            struct vfs_dirent e;
            int tile = 0;
            for (uint32_t i = 0; tile < 9 && vfs_readdir("/apps", i, &e) == 0; i++) {
                if (e.type == VFS_TYPE_DIR) continue;
                int col = tile % 3, row = tile / 3;
                int32_t tx = win->x + 18 + col * 164;
                int32_t ty = win->y + 52 + row * 92;
                uint32_t bg = ends_with_airp(e.name) ? THEME_ACCENT_INDIGO :
                              (ends_with_apk(e.name) ? THEME_ACCENT_GREEN : THEME_ACCENT_ORANGE);
                gfx_fill_round_rect(s, tx, ty, 152, 80, 12, THEME_GLASS_LIGHT, 210u);
                gfx_pill_badge(s, tx + 10, ty + 10,
                               ends_with_airp(e.name) ? "AIRP" : (ends_with_apk(e.name) ? "APK" : "JAVA"),
                               bg, THEME_TEXT_PRIMARY, 1);
                gfx_draw_text_scaled(s, tx + 10, ty + 42, e.name, THEME_TEXT_PRIMARY, 1);
                tile++;
            }
        } else if (id == WIN_APK) {
            const char *tag = airp_app_active ? airp_app_name :
                              (art_am_active() ? art_am_active()->ui.title : "WinDown");
            int custom_sid = sf_find_by_tag(tag);
            if (custom_sid >= 0) {
                /* WinDown Custom SurfaceFlinger / DEX / AIRP Drawing Mode */
                sf_composite_to(custom_sid, s, win->x + 14, win->y + 44);
                gfx_draw_text_scaled(s, win->x + 20, win->y + win->h - 30, "WinDown: Custom App Surface", THEME_ACCENT_TEAL, 2);
            } else if (airp_app_active) {
                /* Fallback: Native .airp Package Host Card */
                gfx_pill_badge(s, win->x + 20, win->y + 48, "AIRP APP", THEME_ACCENT_INDIGO, THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, win->x + 20, win->y + 84, airp_app_name, THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, win->x + 20, win->y + 114, airp_app_path, THEME_ACCENT_TEAL, 1);
                char abuf[40]; scpy(abuf, "Image Bytes: ", sizeof(abuf));
                fmt_u32(airp_app_size, abuf + 13, sizeof(abuf) - 13);
                gfx_draw_text_scaled(s, win->x + 20, win->y + 138, abuf, THEME_TEXT_MUTED, 2);
                gfx_draw_text_scaled(s, win->x + 20, win->y + win->h - 30, "WinDown Fallback UI", THEME_TEXT_MUTED, 2);
            } else {
                /* Fallback: Native ART Activity Card */
                const struct art_activity *act = art_am_active();
                if (act) {
                    const struct art_package *pkg = art_pm_get(act->pkg_index);
                    int is_sys = (pkg && pkg->info.is_system_app);
                    gfx_pill_badge(s, win->x + 20, win->y + 48, is_sys ? "SYSTEM APP" : "USER TEST APP",
                                   is_sys ? THEME_ACCENT_BLUE : THEME_ACCENT_PINK, THEME_TEXT_PRIMARY, 2);
                    gfx_pill_badge(s, win->x + 220, win->y + 48, art_bridge_abi(), THEME_ACCENT_INDIGO, THEME_TEXT_PRIMARY, 2);
                    gfx_draw_text_scaled(s, win->x + 20, win->y + 84, act->ui.title, THEME_TEXT_PRIMARY, 2);
                    for (int i = 0; i < act->ui.line_count && i < 4; i++)
                        gfx_draw_text_scaled(s, win->x + 20, win->y + 112 + i * 22, act->ui.lines[i], THEME_TEXT_MUTED, 2);
                    if (act->ui.button_count > 0) {
                        gfx_fill_round_rect(s, win->x + 20, win->y + win->h - 76, 170, 34, 8, THEME_ACCENT_BLUE, 240u);
                        gfx_draw_text_scaled(s, win->x + 34, win->y + win->h - 66, act->ui.buttons[0], THEME_TEXT_PRIMARY, 2);
                    }
                    gfx_draw_text_scaled(s, win->x + 20, win->y + win->h - 30, act->action_status, THEME_ACCENT_TEAL, 2);
                }
            }
            gfx_fill_round_rect(s, win->x + win->w - 176, win->y + win->h - 76, 156, 34, 8, THEME_ACCENT_TEAL, 220u);
            gfx_draw_text_scaled(s, win->x + win->w - 166, win->y + win->h - 66,
                                 custom_sid >= 0 ? "Fallback UI" : "Custom Draw", THEME_TEXT_PRIMARY, 2);
        } else if (id == WIN_PREFS) {
            gfx_fill_round_rect(s, win->x + 10, win->y + 42, 122, win->h - 54, 8, THEME_GLASS_LIGHT, 130u);
            const char *tabs[4] = {"Theme", "Motion", "Network", "About"};
            for (int tb = 0; tb < 4; tb++) {
                if (prefs_tab == tb)
                    gfx_fill_round_rect(s, win->x + 14, win->y + 52 + tb * 36, 114, 30, 6, THEME_ACCENT_BLUE, 220u);
                gfx_draw_text_scaled(s, win->x + 24, win->y + 59 + tb * 36, tabs[tb], THEME_TEXT_PRIMARY, 2);
            }
            int32_t px = win->x + 144;
            if (prefs_tab == 0) {
                gfx_draw_text_scaled(s, px, win->y + 54, "Wallpaper & Accent", THEME_TEXT_PRIMARY, 2);
                const char *tn[3] = {"Indigo", "Blue", "Pink"};
                uint32_t tc[3] = {THEME_ACCENT_INDIGO, THEME_ACCENT_BLUE, THEME_ACCENT_PINK};
                for (int t = 0; t < 3; t++) {
                    gfx_fill_round_rect(s, px + t * 106, win->y + 92, 98, 34, 8, tc[t], theme_idx == t ? 250u : 170u);
                    gfx_draw_text_scaled(s, px + 14 + t * 106, win->y + 101, tn[t], THEME_TEXT_PRIMARY, 2);
                }
                gfx_draw_text_scaled(s, px, win->y + 148, "FlairOS Glass: Active", THEME_ACCENT_TEAL, 2);
            } else if (prefs_tab == 1) {
                gfx_draw_text_scaled(s, px, win->y + 48, "Display & Motion", THEME_TEXT_PRIMARY, 2);
                gfx_fill_round_rect(s, px, win->y + 78, 280, 36, 8, anim_on ? THEME_ACCENT_TEAL : THEME_GLASS_LIGHT, 200u);
                gfx_draw_text_scaled(s, px + 12, win->y + 88, anim_on ? "Animations: ON" : "Animations: OFF", THEME_TEXT_PRIMARY, 2);
                gfx_fill_round_rect(s, px, win->y + 126, 280, 36, 8, tooltips_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 200u);
                gfx_draw_text_scaled(s, px + 12, win->y + 136, tooltips_on ? "Dock Tooltips: ON" : "Dock Tooltips: OFF", THEME_TEXT_PRIMARY, 2);
                gfx_fill_round_rect(s, px, win->y + 174, 280, 36, 8, target_hz == 90 ? THEME_ACCENT_GREEN : THEME_ACCENT_BLUE, 200u);
                gfx_draw_text_scaled(s, px + 12, win->y + 184, target_hz == 90 ? "Refresh Target: 90Hz" : "Refresh Target: 60Hz", THEME_TEXT_PRIMARY, 2);
            } else if (prefs_tab == 2) {
                gfx_draw_text_scaled(s, px, win->y + 48, "Network & Power", THEME_TEXT_PRIMARY, 2);
                gfx_fill_round_rect(s, px, win->y + 78, 280, 36, 8, wifi_on ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 200u);
                gfx_draw_text_scaled(s, px + 12, win->y + 88, wifi_on ? "Interface: Wi-Fi" : "Interface: Ethernet", THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, px, win->y + 132, "Power: AC 100%", THEME_ACCENT_GREEN, 2);
                gfx_draw_text_scaled(s, px, win->y + 160, "Host Network: Ready", THEME_TEXT_MUTED, 1);
            } else {
                char hbuf[40];
                scpy(hbuf, "Heap Used KB: ", sizeof(hbuf));
                fmt_u32((uint32_t)(kmem64_used_bytes() / 1024u), hbuf + 14, sizeof(hbuf) - 14);
                gfx_draw_text_scaled(s, px, win->y + 52, info->arch_name, THEME_ACCENT_TEAL, 2);
                gfx_draw_text_scaled(s, px, win->y + 82, hbuf, THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, px, win->y + 112, "OpenJDK 21: Ready", THEME_TEXT_PRIMARY, 2);
                gfx_draw_text_scaled(s, px, win->y + 142, "Toybox + Kilo: Ready", THEME_TEXT_MUTED, 2);
            }
        } else if (id == WIN_BROWSER) {
            flair_browser_render(s, win->x, win->y, win->w, win->h);
        } else if (id == WIN_BROWSER) {
            flair_browser_render(s, win->x, win->y, win->w, win->h);
        } else if (id == WIN_CALC) {
            gfx_fill_round_rect(s, win->x + 14, win->y + 44, win->w - 28, 58, 10, THEME_GLASS_DARK, 240u);
            if (calc_op) {
                char op_s[2] = {calc_op, 0};
                gfx_draw_text_scaled(s, win->x + 24, win->y + 52, op_s, THEME_ACCENT_ORANGE, 2);
            }
            char cdisp[28];
            if (calc_err) scpy(cdisp, "Error", sizeof(cdisp));
            else fmt_i64(calc_cur, cdisp, sizeof(cdisp));
            gfx_draw_text_scaled(s, win->x + 26, win->y + 68, cdisp, THEME_TEXT_PRIMARY, 3);

            static const char *clbl[5][4] = {
                {"C", "+/-", "%", "/"},
                {"7", "8", "9", "*"},
                {"4", "5", "6", "-"},
                {"1", "2", "3", "+"},
                {"0", "0", "BS", "="}
            };
            int32_t gx = win->x + 14, gy = win->y + 114, bw = 64, bh = 42, gap = 5;
            for (int r = 0; r < 5; r++) {
                for (int c = 0; c < 4; c++) {
                    if (r == 4 && c == 1) continue;
                    int32_t bx = gx + c * (bw + gap);
                    int32_t by = gy + r * (bh + gap);
                    int32_t cw = (r == 4 && c == 0) ? (bw * 2 + gap) : bw;
                    uint32_t bg = (c == 3) ? THEME_ACCENT_ORANGE : (r == 0 ? THEME_GLASS_BORDER : THEME_GLASS_LIGHT);
                    gfx_fill_round_rect(s, bx, by, cw, bh, 10, bg, 235u);
                    gfx_draw_text_scaled(s, bx + (cw / 2) - 10, by + 13, clbl[r][c], THEME_TEXT_PRIMARY, 2);
                }
            }
        }
        if (density) gfx_transform_clear();
    }

    /* Dock: order, icons, glyphs and tooltips all come from the session package.
       It enters with the slide-in-bottom preset instead of appearing fully
       formed on the first frame. */
    int32_t dw, dh, dx, dy;
    static int dock_intro_started;
    desk_dock_geometry(w, h, &dx, &dy, &dw, &dh);
    if (!dock_intro_started) {
        (void)an_preset_play(an_id_str("desktop.dock.intro"), "slide-in-bottom");
        dock_intro_started = 1;
    }
    {
        const struct an_preset *dock_in = an_preset_lookup("slide-in-bottom");
        if (dock_in)
            dy += an_preset_offset(an_id_str("desktop.dock.intro"), dock_in,
                                   dh + desk_ui_px(20));
    }
    gfx_glass_panel(s, dx, dy, dw, dh, desk_ui_px(22), THEME_GLASS_LIGHT, 200u);
    if (dock_hover >= 0 && dock_hover < g_launcher.dock_count) {
        gfx_fill_round_rect(s, desk_dock_icon_x(dx, dock_hover) - desk_ui_px(4),
                            dy + desk_ui_px(8), desk_ui_px(60), desk_ui_px(60),
                            desk_ui_px(14), THEME_GLASS_LIGHT, 160u);
    }
    for (int i = 0; i < g_launcher.dock_count && i < 8; i++) {
        int ti = desk_dock_tile(i);
        const struct shell_tile *tile;
        int icon, f;
        int32_t icon_size, ix, iy;
        if (ti < 0) continue;
        tile = &g_launcher.tiles[ti];
        icon = (tile->icon >= 0 && tile->icon < ANIM_ICON_COUNT) ? tile->icon : 0;
        f = (anim_on && dock_hover == i) ? dock_anim_frame : 0;
        icon_size = (desk_ui_px(52) * dock_scale[i]) / 1000;
        ix = desk_dock_icon_x(dx, i) + (desk_ui_px(52) - icon_size) / 2;
        iy = dy + desk_ui_px(12) + (desk_ui_px(52) - icon_size) / 2 -
             ((anim_on && dock_hover == i && (f & 1)) ? desk_ui_px(3) : 0);
        gfx_draw_bmp_scaled(s, anim_icon_bmps[icon][f], anim_icon_sizes[icon][f],
                            ix, iy, icon_size, icon_size);
        if (tile->glyph[0])
            gfx_draw_text_scaled(s, ix + (icon_size - desk_ui_px(24)) / 2,
                                 iy + (icon_size - desk_ui_px(16)) / 2,
                                 tile->glyph, THEME_TEXT_PRIMARY, desk_ui_text_scale(2));
        {
            int wid = desk_target_window(tile->target);
            if (wid >= 0 && wins[wid].open)
                gfx_fill_round_rect(s, dx + desk_ui_px(44) + i * desk_ui_px(76),
                                    dy + desk_ui_px(67), desk_ui_px(8),
                                    desk_ui_px(4), desk_ui_px(2),
                                    THEME_TEXT_PRIMARY, 240u);
        }
    }
    if (tooltips_on && dock_hover >= 0 && dock_hover < g_launcher.dock_count) {
        int ti = desk_dock_tile(dock_hover);
        int32_t tx = dx + desk_ui_px(10) + dock_hover * desk_ui_px(76);
        gfx_glass_panel(s, tx, dy - desk_ui_px(30), desk_ui_px(84),
                        desk_ui_px(24), desk_ui_px(8), THEME_GLASS_DARK, 235u);
        if (ti >= 0)
            gfx_draw_text_scaled(s, tx + desk_ui_px(10), dy - desk_ui_px(24),
                                 g_launcher.tiles[ti].label,
                                 THEME_TEXT_PRIMARY, desk_ui_text_scale(1));
    }

    airstin_render_control_center(s, w, cc_open, cc_anim, anim_on, theme_idx);

    /* Active Menu Dropdown */
    if (active_menu >= 0) {
        static const char *const item_keys[5][3] = {
            {"menu.about", "menu.sysinfo", "menu.close_all"},
            {"menu.open_root", "menu.open_apps", "menu.open_home"},
            {"menu.new_note", "menu.new_folder", "menu.open_selected"},
            {"menu.tile_windows", "menu.minimize", "menu.close_active"},
            {"menu.theme_indigo", "menu.theme_blue", "menu.theme_pink"}
        };
        static const char *const item_text[5][3] = {
            {"About FlairOS", "System Info APK", "Close All"},
            {"Open /", "Open /apps", "Open /home"},
            {"New Note", "New Folder", "Open Selected"},
            {"Tile Windows", "Minimize", "Close Active"},
            {"Theme Indigo", "Theme Blue", "Theme Pink"}
        };
        static const int32_t menu_design_x[5] = {12, 118, 204, 270, 356};
        int32_t mx0 = desk_ui_px(menu_design_x[active_menu]);
        int32_t my0 = desk_ui_px(34);
        int32_t mts = desk_ui_text_scale(2);
        gfx_glass_panel(s, mx0, my0, desk_ui_px(190), desk_ui_px(104),
                        desk_ui_px(10), THEME_GLASS_DARK, 245u);
        for (int r = 0; r < 3; r++)
            gfx_draw_text_scaled(s, mx0 + desk_ui_px(14),
                                 my0 + desk_ui_px(12) + r * desk_ui_px(30),
                                 desk_label(item_keys[active_menu][r], item_text[active_menu][r]),
                                 THEME_TEXT_PRIMARY, mts);
    }

    if (spotlight_open || spotlight_anim_y > -24) {
        int32_t sp_w = 420, sp_x = (w - sp_w) / 2, sp_y = 64 + spotlight_anim_y;
        int32_t sp_alpha = (spotlight_anim_y + 24) * 245 / 24;
        if (sp_alpha < 0) sp_alpha = 0;
        if (sp_alpha > 245) sp_alpha = 245;
        gfx_glass_panel(s, sp_x, sp_y, sp_w, 220, 14, THEME_GLASS_DARK, (uint8_t)sp_alpha);
        gfx_fill_round_rect(s, sp_x + 12, sp_y + 12, sp_w - 24, 30, 8, THEME_GLASS_LIGHT, 200u);
        gfx_draw_text_scaled(s, sp_x + 22, sp_y + 20,
                             spotlight_q[0] ? spotlight_q
                                            : desk_label("spotlight.placeholder", "Spotlight: type app name..."),
                             THEME_TEXT_PRIMARY, 2);
        struct vfs_dirent e;
        int row = 0;
        for (uint32_t i = 0; row < 5 && vfs_readdir("/apps", i, &e) == 0; i++) {
            if (e.type == VFS_TYPE_DIR || !contains_icase(e.name, spotlight_q)) continue;
            int32_t ry = sp_y + 52 + row * 28;
            gfx_fill_round_rect(s, sp_x + 12, ry, sp_w - 24, 24, 6, row == 0 ? THEME_ACCENT_BLUE : THEME_GLASS_LIGHT, 160u);
            gfx_draw_text_scaled(s, sp_x + 22, ry + 5, e.name, THEME_TEXT_PRIMARY, 2);
            row++;
        }
    }
    airstin_render_installer(s, w, h);
    airstin_render_power_modal(s, w, h);
#ifndef FLAIROS_HOST_TEST
    shell_anim_demo_render(s);
#endif
}
