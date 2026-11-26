#include "kernel.h"
#include "../../include/cursor.h"
#include "../../include/axi.h"
#include "../../include/desktop_gfx.h"
#include "../../include/sprunkui.h"
#include "../../include/airnim.h"
#include "../../include/apk.h"
#include "../../include/dex.h"
#include "../../include/vfs.h"
#include "../../include/binder.h"
#include "../../include/ashmem.h"
#include "../../include/task.h"
#include "../../include/kheap.h"
#include "../../include/windown.h"

void desktop_present_app(void);
void mobile_present_app(void);
void axi_poll_mouse(void);
struct wd_compositor *desktop_windown_get(void);

static void axk_present_shell(void) {
#ifdef FLAIROS_SHELL_MOBILE
    mobile_present_app();
#else
    desktop_present_app();
#endif
}

#define AXK_SLOTS 4
#define AXK_TITLE_MAX 24
#define AXK_DESKTOP_ROWS 24
#define AXK_BODY_ATTR 0x70
#define AXK_TITLE_ATTR 0x1F
#define AXK_MIN_W 8
#define AXK_MIN_H 3

struct axk_win {
    int used;
    int open;
    int clicked;
    int wd_id;
    i32 x, y, w, h;
    i32 click_row, click_col;
    char title[AXK_TITLE_MAX + 1];
    u16 body[AX_MAX_BODY_H * AX_MAX_BODY_W];
};

struct apk_installer_overlay {
    int active;
    int decision;
    int is_system;
    char title[48];
    char package_name[64];
    char category[32];
    char detail[64];
};
static struct apk_installer_overlay apk_overlay;

static void cpy_str(char *dst, const char *src, int cap) {
    int i = 0;
    if (!dst || cap <= 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void cat_str(char *dst, const char *src, int cap) {
    int i = 0, j = 0;
    while (dst[i] && i < cap - 1) i++;
    while (src && src[j] && i + 1 < cap) dst[i++] = src[j++];
    dst[i] = 0;
}

static struct axk_win *axk_wins;

static int axk_ensure(void) {
    if (axk_wins) return 1;
    axk_wins = (struct axk_win *)kmalloc(sizeof(struct axk_win) * AXK_SLOTS);
    if (!axk_wins) return 0;
    for (int i = 0; i < AXK_SLOTS; i++) {
        axk_wins[i].used = 0;
        axk_wins[i].open = 0;
        axk_wins[i].wd_id = 0;
    }
    return 1;
}
static u8 axk_prev_buttons;

static struct axk_win *axk_get(u32 handle) {
    if (!axk_ensure()) return 0;
    if (handle < 1 || handle > AXK_SLOTS)
        return 0;
    struct axk_win *w = &axk_wins[handle - 1];
    return w->used ? w : 0;
}

static void axk_fill_body(struct axk_win *w, u8 attr) {
    for (int i = 0; i < AX_MAX_BODY_W * AX_MAX_BODY_H; i++)
        w->body[i] = (u16)(' ' | (attr << 8));
}

static u32 axk_open(u32 xy, u32 wh, const char *title) {
    if (!axk_ensure()) return AX_FAIL;
    i32 x = (i32)(xy & 0xFFFF), y = (i32)(xy >> 16);
    i32 w = (i32)(wh & 0xFFFF), h = (i32)(wh >> 16);
    if (w < AXK_MIN_W || w > AX_MAX_BODY_W || h < AXK_MIN_H || h - 1 > AX_MAX_BODY_H)
        return AX_FAIL;
    if (x < 0 || y < 0 || x + w > VGA_COLS || y + h > AXK_DESKTOP_ROWS)
        return AX_FAIL;
    for (int i = 0; i < AXK_SLOTS; i++) {
        struct axk_win *s = &axk_wins[i];
        if (s->used)
            continue;
        int n = 0;
        while (title && title[n] && n < AXK_TITLE_MAX) {
            s->title[n] = title[n];
            n++;
        }
        s->title[n] = 0;
        s->used = 1;
        s->open = 1;
        {
            struct wd_compositor *wd = desktop_windown_get();
            s->wd_id = wd ? wd_create_window(wd, x, y, w, h, s->title) : 0;
            if (wd && s->wd_id > 0) wd_raise(wd, s->wd_id);
        }
        s->clicked = 0;
        s->x = x;
        s->y = y;
        s->w = w;
        s->h = h;
        axk_fill_body(s, AXK_BODY_ATTR);
        return (u32)(i + 1);
    }
    return AX_FAIL;
}

static u32 axk_close(u32 handle) {
    struct axk_win *w = axk_get(handle);
    if (!w)
        return AX_FAIL;
    if (w->wd_id > 0) {
        struct wd_compositor *wd = desktop_windown_get();
        if (wd) (void)wd_destroy_window(wd, w->wd_id);
        w->wd_id = 0;
    }
    w->used = 0;
    w->open = 0;
    return 0;
}

static u32 axk_text(u32 win_attr, u32 rowcol, const char *s) {
    struct axk_win *w = axk_get(win_attr & 0xFF);
    if (!w)
        return AX_FAIL;
    u8 attr = (u8)((win_attr >> 8) & 0xFF);
    i32 row = (i32)(rowcol & 0xFFFF), col = (i32)(rowcol >> 16);
    if (row >= w->h - 1 || col >= w->w)
        return AX_FAIL;
    for (i32 i = 0; s[i] && col + i < w->w; i++)
        w->body[row * AX_MAX_BODY_W + col + i] = (u16)((u8)s[i] | (attr << 8));
    return 0;
}

static u32 axk_clear(u32 handle, u32 attr) {
    struct axk_win *w = axk_get(handle);
    if (!w)
        return AX_FAIL;
    axk_fill_body(w, (u8)(attr & 0xFF));
    return 0;
}

static u32 axk_click(u32 handle) {
    struct axk_win *w = axk_get(handle);
    if (!w || !w->clicked)
        return AX_FAIL;
    w->clicked = 0;
    return (u32)w->click_row | ((u32)w->click_col << 16);
}

static u32 axk_alive(u32 handle) {
    struct axk_win *w = axk_get(handle);
    return (w && w->open) ? 1u : 0u;
}

extern volatile u32 mouse_press_x, mouse_press_y;

static void axk_mouse(void) {
    if (!axk_wins) return;
    i32 mx = (i32)cursor_x, my = (i32)cursor_y;
    u8 b = mouse_buttons;
    u8 latched = mouse_take_clicks();
    if (desktop_gfx_active())
        (void)desktop_gfx_map_pointer((latched & 1) ? mouse_press_x : cursor_norm_x,
                                      (latched & 1) ? mouse_press_y : cursor_norm_y, &mx, &my);
    if (((b & 1) && !(axk_prev_buttons & 1)) || (latched & 1)) {
#ifdef FLAIROS_SHELL_MOBILE
        for (int i = AXK_SLOTS - 1; i >= 0; i--) {
            struct axk_win *w = &axk_wins[i];
            if (!w->used || !w->open) continue;
            /* Android-style bottom navigation acts as Back/close. */
            if (my >= VGA_ROWS - 2) {
                w->open = 0;
            } else {
                w->clicked = 1;
                w->click_row = (my * (w->h - 1)) / VGA_ROWS;
                w->click_col = (mx * w->w) / VGA_COLS;
                if (w->click_row >= w->h - 1) w->click_row = w->h - 2;
                if (w->click_col >= w->w) w->click_col = w->w - 1;
            }
            break;
        }
#else
        for (int i = AXK_SLOTS - 1; i >= 0; i--) {
            struct axk_win *w = &axk_wins[i];
            if (!w->used || !w->open)
                continue;
            if (mx < w->x || mx >= w->x + w->w || my < w->y || my >= w->y + w->h)
                continue;
            if (my == w->y) {
                if (mx >= w->x + w->w - 4 && mx <= w->x + w->w - 2)
                    w->open = 0;
            } else {
                w->clicked = 1;
                w->click_row = my - w->y - 1;
                w->click_col = mx - w->x;
            }
            break;
        }
#endif
    }
    axk_prev_buttons = b;
}

static int axk_is_terminal(const char *title) {
    static const char name[] = "Terminal";
    int i;
    for (i = 0; name[i] && title[i] == name[i]; ++i) {}
    return name[i] == 0 && title[i] == 0;
}

void axi_gfx_render(void) {
    if (!axk_wins) return;
    struct sp_context *ui = desktop_gfx_ui_context();
    int32_t sw, sh;
    if (!ui || !ui->surface || !ui->theme) return;
    sw = ui->surface->width;
    sh = ui->surface->height;

    for (int k = 0; k < AXK_SLOTS; ++k) {
        struct axk_win *w = &axk_wins[k];
        int32_t x, y, width, height, title_h, body_h, body_rows;
        if (!w->used || !w->open) {
            (void)an_track(an_id_at(an_id_str("axwin"), k, 0), 0, 1, AN_LINEAR);
            continue;
        }

#ifndef FLAIROS_SHELL_MOBILE
        {
            struct wd_compositor *wd = desktop_windown_get();
            if (wd) {
                if (w->wd_id <= 0)
                    w->wd_id = wd_create_window(wd, w->x, w->y, w->w, w->h, w->title);
                if (w->wd_id > 0) {
                    (void)wd_set_geometry(wd, w->wd_id, w->x, w->y, w->w, w->h);
                    (void)wd_set_visible(wd, w->wd_id, 1);
                    (void)wd_raise(wd, w->wd_id);
                }
            }
        }
        {
            int32_t wp = an_track(an_id_at(an_id_str("axwin"), k, 0), AN_ONE, 14, AN_OUT_CUBIC);
            x = (w->x * sw) / VGA_COLS;
            y = (w->y * sh) / VGA_ROWS + ((AN_ONE - wp) * 18) / AN_ONE;
            width = ((w->x + w->w) * sw + VGA_COLS - 1) / VGA_COLS - x;
            height = ((w->y + w->h) * sh + VGA_ROWS - 1) / VGA_ROWS - y;
        }
        if (width < 40 || height < 30) continue;
        title_h = sh / VGA_ROWS;
        if (title_h < 12) title_h = 12;
        if (title_h > height / 2) title_h = height / 2;
        body_h = height - title_h;
#else
        /* Mobile apps are activities, not floating desktop windows. */
        x = 0; y = 0; width = sw; height = sh;
        title_h = 0;
        body_h = sh;
#endif
        body_rows = w->h - 1;
        if (body_rows <= 0 || body_h <= 0) continue;

#ifdef FLAIROS_SHELL_MOBILE
        ag_clear(ui->surface, 0x001B1B23u);
        sp_panel(ui, 0, 0, width, height);
#else
        sp_panel(ui, x, y, width, height);
        sp_button(ui, x + 3, y + 2, width - 50, title_h - 4,
                  w->title, 1);
        sp_badge(ui, x + width - 44, y + 2, "X",
                 ui->theme->color.danger);
        sp_separator(ui, x + 4, y + title_h, width - 8);
#endif

        for (int row = 0; row < body_rows; ++row) {
            char line[AX_MAX_BODY_W + 1];
            int last = w->w;
            int selected = 0, muted = 0, primary = 0, danger = 0;
            int32_t row_y = y + title_h + (row * body_h) / body_rows;
            int32_t row_end = y + title_h + ((row + 1) * body_h) / body_rows;
            int32_t row_h = row_end - row_y;
            for (int col = 0; col < w->w; ++col) {
                u16 entry = w->body[row * AX_MAX_BODY_W + col];
                unsigned char ch = (unsigned char)(entry & 0xFFu);
                unsigned char attr = (unsigned char)(entry >> 8);
                line[col] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
                if (col == 0) {
                    selected = attr == 0x1Fu;
                    muted = attr == 0x78u;
                    primary = attr == 0x2Fu;
                    danger = attr == 0x4Fu;
                }
            }
            if (last > AX_MAX_BODY_W) last = AX_MAX_BODY_W;
            while (last > 0 && line[last - 1] == ' ') --last;
            line[last] = 0;
            if (row_h <= 0 || !last) continue;

            if (axk_is_terminal(w->title) && row == body_rows - 1) {
                sp_text_field(ui, x + 4, row_y + 1, width - 8,
                              row_h - 2, line, 1);
            } else if (primary) {
                sp_button(ui, x + 4, row_y + 1, width - 8, row_h - 2, line, 1);
            } else if (danger) {
                sp_badge(ui, x + 8, row_y + (row_h > 20 ? (row_h - 20) / 2 : 0),
                         line, ui->theme->color.danger);
            } else if (selected) {
                sp_list_item(ui, x + 4, row_y, width - 8, row_h,
                             line, 0, 1);
            } else {
                sp_label(ui, x + 8,
                         row_y + (row_h > 7 ? (row_h - 7) / 2 : 0),
                         line, muted);
            }
        }
#ifdef FLAIROS_SHELL_MOBILE
        /* Persistent Android navigation affordance; tap it to return Home. */
        ag_round_rect(ui->device, ui->surface, (sw - 112) / 2, sh - 18,
                      112, 5, 3, 0x00D5DCE8u);
#endif
    }

    if (apk_overlay.active) {
        int32_t dw = sw > 420 ? 380 : sw - 32;
        int32_t dh = sh > 260 ? 204 : sh - 32;
        int32_t dx = (sw - dw) / 2;
        int32_t dy = (sh - dh) / 2;
        int32_t bw = (dw - 44) / 2;
        an_blend_rect(ui->device, ui->surface, 0, 0, sw, sh, 0x050810u, 160u);
        sp_panel(ui, dx, dy, dw, dh);
        sp_button(ui, dx + 10, dy + 10, dw - 20, 26, "Android Package Installer", 1);
        sp_badge(ui, dx + 16, dy + 44, apk_overlay.category,
                 apk_overlay.is_system ? ui->theme->color.accent : ui->theme->color.danger);
        sp_label(ui, dx + 16, dy + 72, apk_overlay.title, 0);
        sp_label(ui, dx + 16, dy + 92, apk_overlay.package_name, 1);
        sp_label(ui, dx + 16, dy + 116, "Install and launch in WinDown?", 0);
        sp_label(ui, dx + 16, dy + 136, apk_overlay.detail, 1);
        sp_button(ui, dx + 16, dy + dh - 40, bw, 28, "Cancel (Esc)", 0);
        sp_button(ui, dx + 28 + bw, dy + dh - 40, bw, 28, "Install & Open", 1);
    }

    /* Draw the pointer last so app windows do not cover it. It is placed at its
       true pixel position, not snapped to the logical 80x25 grid. */
    {
        uint32_t nx = cursor_norm_x > 65535u ? 65535u : cursor_norm_x;
        uint32_t ny = cursor_norm_y > 65535u ? 65535u : cursor_norm_y;
        cursor_draw(ui->device, ui->surface,
                    (int32_t)((nx * (uint32_t)sw) >> 16),
                    (int32_t)((ny * (uint32_t)sh) >> 16));
    }
}


void axi_poll_mouse(void) {
    axk_mouse();
}

static int str_ends_with(const char *s, const char *suf) {
    int sl = 0, fl = 0;
    if (!s || !suf) return 0;
    while (s[sl]) sl++;
    while (suf[fl]) fl++;
    if (sl < fl) return 0;
    for (int i = 0; i < fl; i++)
        if (s[sl - fl + i] != suf[i]) return 0;
    return 1;
}

static int axk_apk_install(const char *src_path, const char *name) {
    int sfd, dfd, n;
    uint8_t *buf;
    struct apk_info info;
    char dst[96], safe[80];
    const char *pfx = "/apps/";
    int di = 0, si = 0;
    if (!src_path || !name || !name[0]) return -1;

    /* Treat the supplied display name as a filename stem, never as a path. */
    while (name[si] && si < (int)sizeof(safe) - 5) {
        unsigned char c = (unsigned char)name[si];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            c = '_';
        safe[si++] = (char)c;
    }
    safe[si] = 0;
    if (!safe[0]) return -1;
    if (!str_ends_with(safe, ".apk")) {
        safe[si++] = '.';
        safe[si++] = 'a';
        safe[si++] = 'p';
        safe[si++] = 'k';
        safe[si] = 0;
    }

    buf = (uint8_t *)kmalloc(65536u);
    if (!buf) return -2;
    sfd = vfs_open(src_path, VFS_O_RDONLY);
    if (sfd < 0) { kfree(buf); return -3; }
    n = vfs_read(sfd, buf, 65536u);
    vfs_close(sfd);
    if (n <= 0 || apk_inspect(buf, (uint32_t)n, &info) != 0 || !info.has_dex) {
        kfree(buf);
        return -4;
    }
    (void)vfs_mkdir("/apps");
    while (pfx[di]) { dst[di] = pfx[di]; di++; }
    for (si = 0; safe[si] && di + 1 < (int)sizeof(dst); ++si)
        dst[di++] = safe[si];
    dst[di] = 0;

    dfd = vfs_open(dst, VFS_O_CREAT | VFS_O_WRONLY | VFS_O_TRUNC);
    if (dfd < 0) { kfree(buf); return -5; }
    if (vfs_write(dfd, buf, (uint32_t)n) != n) {
        vfs_close(dfd);
        (void)vfs_unlink(dst);
        kfree(buf);
        return -6;
    }
    vfs_close(dfd);
    kfree(buf);
    serial_str("apk: installed ");
    serial_str(dst);
    serial_str("\n");
    return 0;
}

static int axk_run_installer_overlay(const struct apk_info *info, const char *path) {
    u8 prev_b = mouse_buttons;
    apk_overlay.active = 1;
    apk_overlay.decision = 0;
    apk_overlay.is_system = info->is_system_app ? 1 : 0;
    cpy_str(apk_overlay.title,
            info->app_label[0] ? info->app_label : "Android Application",
            sizeof(apk_overlay.title));
    cpy_str(apk_overlay.package_name,
            info->package_name[0] ? info->package_name : path,
            sizeof(apk_overlay.package_name));
    if (info->is_system_app)
        cpy_str(apk_overlay.category, "SYSTEM APP", sizeof(apk_overlay.category));
    else if (info->is_user_test_app)
        cpy_str(apk_overlay.category, "USER APP (TEST)", sizeof(apk_overlay.category));
    else
        cpy_str(apk_overlay.category, "USER APP", sizeof(apk_overlay.category));
    cpy_str(apk_overlay.detail, "Dalvik 035 + Binder + Ashmem", sizeof(apk_overlay.detail));

    (void)mouse_take_clicks();
    while (kbd_read() >= 0) {}

    while (apk_overlay.decision == 0) {
        if (desktop_gfx_active()) axk_present_shell();
        int k = kbd_read();
        if (k == 27 || k == 'n' || k == 'N') {
            apk_overlay.decision = -1;
            break;
        }
        if (k == '\n' || k == '\r' || k == 'y' || k == 'Y') {
            apk_overlay.decision = 1;
            break;
        }
        u8 b = mouse_buttons;
        u8 latched = mouse_take_clicks();
        if (((b & 1) && !(prev_b & 1)) || (latched & 1)) {
            struct sp_context *ui = desktop_gfx_ui_context();
            if (ui && ui->surface) {
                int32_t sw = ui->surface->width, sh = ui->surface->height;
                uint32_t nx = (latched & 1) ? mouse_press_x : cursor_norm_x;
                uint32_t ny = (latched & 1) ? mouse_press_y : cursor_norm_y;
                if (nx > 65535u) nx = 65535u;
                if (ny > 65535u) ny = 65535u;
                int32_t px = (int32_t)((nx * (uint32_t)sw) >> 16);
                int32_t py = (int32_t)((ny * (uint32_t)sh) >> 16);
                int32_t dw = sw > 420 ? 380 : sw - 32;
                int32_t dh = sh > 260 ? 204 : sh - 32;
                int32_t dx = (sw - dw) / 2;
                int32_t dy = (sh - dh) / 2;
                int32_t bw = (dw - 44) / 2;
                int32_t by = dy + dh - 40;
                if (py >= by && py <= by + 28) {
                    if (px >= dx + 16 && px <= dx + 16 + bw)
                        apk_overlay.decision = -1;
                    else if (px >= dx + 28 + bw && px <= dx + 28 + bw + bw)
                        apk_overlay.decision = 1;
                } else if (px < dx || px > dx + dw || py < dy || py > dy + dh) {
                    apk_overlay.decision = -1;
                }
            }
        }
        prev_b = b;
        wait_ticks(1);
    }
    apk_overlay.active = 0;
    if (desktop_gfx_active()) axk_present_shell();
    return apk_overlay.decision;
}

static int axk_apk_launch(const char *path) {
    int fd, n, dex_len = 0;
    uint8_t *raw, *dex_buf = 0;
    struct dex_ui_ctx ui;
    struct apk_info info;
    struct vfs_stat st;
    uint32_t buf_cap = 32768u;
    int32_t ret = 0;
    u32 win;
    if (!path || !path[0]) return -1;
    if (vfs_stat(path, &st) == 0 && st.size > 0 && st.size <= 65536u)
        buf_cap = st.size + 64u;
    raw = (uint8_t *)kmalloc(buf_cap);
    if (!raw) return -2;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { kfree(raw); return -3; }
    n = vfs_read(fd, raw, buf_cap);
    vfs_close(fd);
    if (n <= 0) { kfree(raw); return -4; }

    if (apk_inspect(raw, (uint32_t)n, &info) == 0 && info.has_dex) {
        if (desktop_gfx_active()) {
            if (axk_run_installer_overlay(&info, path) != 1) {
                kfree(raw);
                serial_str("apk: install/launch cancelled\n");
                return 0;
            }
        }
        if (!(path[0] == '/' && path[1] == 'a' && path[2] == 'p' &&
              path[3] == 'p' && path[4] == 's' && path[5] == '/')) {
            const char *nm = info.app_label[0] ? info.app_label : "userapp";
            (void)axk_apk_install(path, nm);
        }
        uint32_t dcap = info.dex_size > 0 && info.dex_size <= 32768u ? info.dex_size + 64u : 32768u;
        dex_buf = (uint8_t *)kmalloc(dcap);
        if (!dex_buf) { kfree(raw); return -5; }
        dex_len = apk_extract_stored(raw, (uint32_t)n, "classes.dex", dex_buf, dcap);
        kfree(raw);
        if (dex_len <= 0) { kfree(dex_buf); return -6; }
    } else {
        info.has_manifest = 0;
        info.is_system_app = 0;
        info.is_user_test_app = 0;
        info.package_name[0] = 0;
        info.app_label[0] = 0;
        dex_buf = raw;
        dex_len = n;
    }

    (void)binder_register("activity", task_current_pid(), 0);
    (void)ashmem_create("apk_surface", 4096u);
    if (dex_run_buffer(dex_buf, (uint32_t)dex_len, &ui, &ret) != 0) {
        kfree(dex_buf);
        return -7;
    }
    kfree(dex_buf);

    const char *win_title = ui.title[0] ? ui.title : (info.app_label[0] ? info.app_label : "Android App");
    win = axk_open((14u) | (4u << 16), (54u) | (14u << 16), win_title);
    if (win == AX_FAIL) return -8;
    (void)axk_clear(win, 0x70u);
    u32 row = 1u;
    if (info.is_system_app) {
        (void)axk_text(win | (0x1Fu << 8), row | (2u << 16), "SYSTEM APP");
        row++;
    } else if (info.is_user_test_app) {
        (void)axk_text(win | (0x4Fu << 8), row | (2u << 16), "USER APP (TEST)");
        row++;
    } else {
        (void)axk_text(win | (0x1Fu << 8), row | (2u << 16), "USER APP");
        row++;
    }
    if (ui.subtitle[0]) {
        (void)axk_text(win | (0x78u << 8), row | (2u << 16), ui.subtitle);
        row++;
    } else if (info.package_name[0]) {
        (void)axk_text(win | (0x78u << 8), row | (2u << 16), info.package_name);
        row++;
    }
    for (int i = 0; i < ui.line_count && row < 9u; i++, row++)
        (void)axk_text(win | (0x70u << 8), row | (2u << 16), ui.lines[i]);
    for (int b = 0; b < ui.button_count && b < 2; b++)
        (void)axk_text(win | (0x2Fu << 8), (u32)(10 + b) | (2u << 16), ui.buttons[b]);
    if (ui.toast[0])
        (void)axk_text(win | (0x78u << 8), 12u | (2u << 16), ui.toast);

    serial_str("apk: launched ");
    serial_str(win_title);
    if (info.package_name[0]) {
        serial_str(" (");
        serial_str(info.package_name);
        serial_str(")");
    }
    if (info.is_system_app) serial_str(" [system_app]");
    else if (info.is_user_test_app) serial_str(" [user_test_app]");
    else serial_str(" [user_app]");
    serial_str(" in WinDown\n");

    if (desktop_gfx_active()) {
        while (axk_alive(win)) {
            axi_poll_mouse();
            axk_present_shell();
            int k = kbd_read();
            if (k == 27 || k == 'q' || k == 'Q')
                break;
            u32 clk = axk_click(win);
            if (clk != AX_FAIL) {
                i32 crow = (i32)(clk & 0xFFFFu);
                if (crow >= 10 && crow < 10 + ui.button_count) {
                    char msg[48];
                    cpy_str(msg, "Action: ", sizeof(msg));
                    cat_str(msg, ui.buttons[crow - 10], sizeof(msg));
                    cat_str(msg, " (OK)", sizeof(msg));
                    (void)axk_text(win | (0x78u << 8), 12u | (2u << 16),
                                   "                                              ");
                    (void)axk_text(win | (0x78u << 8), 12u | (2u << 16), msg);
                }
            }
            wait_ticks(1);
        }
        (void)axk_close(win);
        axk_present_shell();
    }
    return (int)win;
}

int axi_apk_launch_path(const char *path) {
    return axk_apk_launch(path);
}

int axi_apk_view_path(const char *path) {
    uint8_t *raw;
    struct apk_info info;
    int fd, n;
    u32 win;
    if (!path || !path[0]) return -1;
    raw = (uint8_t *)kmalloc(65536u);
    if (!raw) return -2;
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { kfree(raw); return -3; }
    n = vfs_read(fd, raw, 65536u);
    vfs_close(fd);
    if (n <= 0 || apk_inspect(raw, (uint32_t)n, &info) != 0) {
        kfree(raw);
        return -4;
    }
    kfree(raw);
    win = axk_open((12u) | (4u << 16), (56u) | (14u << 16),
                   info.app_label[0] ? info.app_label : "Application details");
    if (win == AX_FAIL) return -5;
    (void)axk_clear(win, 0x70u);
    (void)axk_text(win | (0x1Fu << 8), 1u | (2u << 16), "ANDROID PACKAGE");
    (void)axk_text(win | (0x70u << 8), 3u | (2u << 16),
                   info.package_name[0] ? info.package_name : "Package name unavailable");
    (void)axk_text(win | (0x70u << 8), 5u | (2u << 16),
                   info.is_system_app ? "Classification: system app" :
                   info.is_user_test_app ? "Classification: test app" : "Classification: user app");
    (void)axk_text(win | (0x70u << 8), 7u | (2u << 16),
                   info.has_dex ? "Dalvik bytecode: classes.dex present" : "Dalvik bytecode: missing");
    (void)axk_text(win | (0x70u << 8), 9u | (2u << 16),
                   info.has_manifest ? "Android manifest: parsed" : "Android manifest: unavailable");
    return (int)win;
}

int axi_apk_install_path(const char *src_path, const char *name) {
    return axk_apk_install(src_path, name);
}

static u32 axk_present(void) {
    axk_mouse();
    /* AXI window presentation is a user-facing compositor operation. When the
       desktop is not active (developer console, text mode, or a recovered
       session after an AirGraph failure) there is no surface to present to.
       Report the condition to the caller instead of panicking the kernel. */
    if (!desktop_gfx_active()) {
        serial_str("axi: present skipped, AirGraph inactive\n");
        return AX_FAIL;
    }
    axk_present_shell();
    if (!desktop_gfx_active()) {
        serial_str("axi: AirGraph went inactive during presentation\n");
        return AX_FAIL;
    }
    return 0;
}

int axi_dispatch(struct saved_regs *r) {
    switch (r->eax) {
    case SYS_WIN_OPEN:
        r->eax = axk_open(r->ebx, r->ecx, (const char *)r->edx);
        return 1;
    case SYS_WIN_CLOSE:
        r->eax = axk_close(r->ebx);
        return 1;
    case SYS_WIN_TEXT:
        r->eax = axk_text(r->ebx, r->ecx, (const char *)r->edx);
        return 1;
    case SYS_WIN_CLEAR:
        r->eax = axk_clear(r->ebx, r->ecx);
        return 1;
    case SYS_WIN_PRESENT:
        r->eax = axk_present();
        return 1;
    case SYS_WIN_CLICK:
        r->eax = axk_click(r->ebx);
        return 1;
    case SYS_WIN_ALIVE:
        r->eax = axk_alive(r->ebx);
        return 1;
    case SYS_UI_ACCENT:
        r->eax = desktop_gfx_set_accent(r->ebx) == 0 ? 0u : AX_FAIL;
        return 1;
    case SYS_UI_DISPLAY:
        if (r->ebx == 0xFFu) r->eax = desktop_gfx_get_profile();
        else if (r->ebx == 0xFEu) r->eax = desktop_gfx_rotate() == 0 ? 0u : AX_FAIL;
        else r->eax = desktop_gfx_set_profile(r->ebx) == 0 ? 0u : AX_FAIL;
        return 1;
    case SYS_UI_TOUCH:
        if (r->ebx == 0xFFu) r->eax = desktop_gfx_get_touch();
        else r->eax = desktop_gfx_set_touch(r->ebx) == 0 ? 0u : AX_FAIL;
        return 1;
    case SYS_UI_ANIM:
        if (r->ebx == 0xFFu) r->eax = desktop_gfx_anim_info();
        else if (r->ebx == 0xFEu) r->eax = desktop_gfx_anim_info();
        else r->eax = desktop_gfx_set_anim(r->ebx) == 0 ? 0u : AX_FAIL;
        return 1;
    case SYS_UI_STATUS:
        r->eax = desktop_gfx_status_flags();
        return 1;
    case SYS_APK_LAUNCH:
        r->eax = (u32)axk_apk_launch((const char *)(uintptr_t)r->ebx);
        return 1;
    case SYS_APK_INSTALL:
        r->eax = (u32)axk_apk_install((const char *)(uintptr_t)r->ebx, (const char *)(uintptr_t)r->ecx);
        return 1;
    default:
        return 0;
    }
}
