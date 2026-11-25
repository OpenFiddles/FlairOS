#include "kernel.h"
#include "../include/desktop_gfx.h"
#include "../include/windown.h"
#include "../include/sprunkui.h"
#include "../include/kheap.h"
#include "../include/cursor.h"
#include "../include/airnim.h"
#include "../include/vfs.h"

#define TASK_ROW (VGA_ROWS - 1)
#define BG_CHAR 0xB0
#define BG_ATTR 0x13
#define BODY_ATTR 0x70
#define TITLE_ON 0x1F
#define TITLE_OFF 0x87
#define TASK_ATTR 0x70
#define BTN_OPEN 0x87
#define BTN_TOP 0x1F
#define ICON_ATTR 0x70
#define FRAME_TICKS 2
#define CURSOR_ATTR 0x0F
#define FILES_ROW 9
#define SETTINGS_ROW 11
#define START_MENU_X 1
#define START_MENU_Y (TASK_ROW - 9)
#define START_MENU_W 25
#define START_MENU_H 9
#define SETTINGS_STAGE ((const u8 *)0x30000)
#define SETTINGS_STAGE_LEN (32u * 512u)
#define NOTES_STAGE ((const u8 *)0x34000)
#define NOTES_STAGE_LEN (16u * 512u)
#define EXPLORER_STAGE ((const u8 *)0x90000)
#define EXPLORER_STAGE_LEN (32u * 512u)
#define TERMINAL_STAGE ((const u8 *)0x38000)
#define TERMINAL_STAGE_LEN (64u * 512u)
#define APP_STACK_TOP 0x7F000u

u8 mouse_take_clicks(void);
extern volatile u32 mouse_press_x, mouse_press_y;
void axi_gfx_render(void);

int airp_load(const u8 *file, u32 len, u32 *entry);
void enter_user(u32 entry, u32 user_esp);
int vm_run_app(u32 entry, u32 user_esp);

enum { WIN_HELLO, WIN_CLOCK, WIN_TERM, WIN_COUNT };

struct window {
    int open;
    i32 x, y, w, h;
    const char *title;
};

static struct window wins[WIN_COUNT] = {
    {0, 22, 3, 38, 9, "Hello"},
    {0, 60, 4, 18, 5, "Clock"},
    {0, 12, 2, 68, 17, "Terminal"},
};

static const char *const icon_label[WIN_COUNT] = {"Hello", "Clock", "Term"};
static const char *const log_name[WIN_COUNT] = {"hello", "clock", "terminal"};
static const i32 icon_row[WIN_COUNT] = {3, 5, 7};

static u16 *back;
static int z[WIN_COUNT] = {WIN_HELLO, WIN_CLOCK, WIN_TERM};
static int drag_win = -1;
static int keyboard_focus = -1;
static i32 drag_dx, drag_dy;
static u8 prev_buttons;
static int start_open;
static int apk_context_open;
static int apk_context_index = -1;
static int apk_context_y;
static char apk_context_title[VFS_NAME_MAX];
static struct wd_compositor *desktop_wd;
static int desktop_wd_ids[WIN_COUNT];

static void cell(int r, int c, u8 ch, u8 attr) {
    if (r < 0 || r >= VGA_ROWS || c < 0 || c >= VGA_COLS)
        return;
    back[r * VGA_COLS + c] = (u16)(ch | (attr << 8));
}

static void text(int r, int c, const char *s, u8 attr, int maxw) {
    for (int i = 0; s[i] && i < maxw; i++)
        cell(r, c + i, (u8)s[i], attr);
}

static void fill(int r, int c, int h, int w, u8 ch, u8 attr) {
    for (int dr = 0; dr < h; dr++)
        for (int dc = 0; dc < w; dc++)
            cell(r + dr, c + dc, ch, attr);
}

static void put2(int r, int c, u8 v, u8 attr) {
    cell(r, c, (u8)('0' + v / 10), attr);
    cell(r, c + 1, (u8)('0' + v % 10), attr);
}

static void draw_hms(int r, int c, u8 attr, const u8 *t) {
    put2(r, c, t[2], attr);
    cell(r, c + 2, ':', attr);
    put2(r, c + 3, t[1], attr);
    cell(r, c + 5, ':', attr);
    put2(r, c + 6, t[0], attr);
}

static i32 clampi(i32 v, i32 lo, i32 hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int num_str(char *out, i32 v) {
    char tmp[12];
    int n = 0, k = 0;
    if (v < 0) { out[k++] = '-'; v = -v; }
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) out[k++] = tmp[--n];
    return k;
}

static void log_point(const char *tag, i32 a, i32 b) {
    char buf[64];
    int n = 0;
    for (const char *p = tag; *p && n < 30; p++) buf[n++] = *p;
    buf[n++] = ' ';
    n += num_str(buf + n, a);
    buf[n++] = ' ';
    n += num_str(buf + n, b);
    buf[n++] = '\n';
    buf[n] = 0;
    serial_str(buf);
}

int axi_apk_launch_path(const char *path);
int axi_apk_view_path(const char *path);

static int desktop_apk_name(const char *name) {
    int n = 0;
    while (name && name[n]) n++;
    return n >= 4 && name[n-4] == '.' && name[n-3] == 'a' &&
           name[n-2] == 'p' && name[n-1] == 'k';
}

static int start_menu_apk_count(void) {
    struct vfs_dirent ent;
    int count = 0;
    for (unsigned int i = 0; i < 16 && count < 4; ++i) {
        if (vfs_readdir("/apps", i, &ent) != 0) break;
        if (ent.type == VFS_TYPE_FILE && desktop_apk_name(ent.name)) count++;
    }
    return count;
}

static int start_menu_item_count(void) { return 7 + start_menu_apk_count(); }
static int start_menu_y(void) { return TASK_ROW - (start_menu_item_count() + 2); }

static int installed_apk_get(int index, char *path, unsigned int path_cap,
                              char *name, unsigned int name_cap) {
    struct vfs_dirent ent;
    int seen = 0;
    if (index < 0 || index >= 4 || !path || path_cap < 8) return -1;
    for (unsigned int i = 0; i < 16; ++i) {
        if (vfs_readdir("/apps", i, &ent) != 0) break;
        if (ent.type != VFS_TYPE_FILE || !desktop_apk_name(ent.name)) continue;
        if (seen++ != index) continue;
        const char prefix[] = "/apps/";
        unsigned int j = 0;
        for (unsigned int k = 0; k < sizeof(prefix); ++k) path[k] = prefix[k];
        while (ent.name[j] && j + sizeof(prefix) < path_cap) {
            path[j + sizeof(prefix) - 1] = ent.name[j];
            j++;
        }
        path[j + sizeof(prefix) - 1] = 0;
        if (name && name_cap) {
            unsigned int j = 0;
            while (ent.name[j] && j + 1 < name_cap) { name[j] = ent.name[j]; j++; }
            name[j] = 0;
        }
        return 0;
    }
    return -1;
}

static int launch_installed_apk(int index) {
    char path[96];
    if (installed_apk_get(index, path, sizeof(path), 0, 0) != 0) return -1;
    return axi_apk_launch_path(path);
}

int axi_apk_launch_path(const char *path);
int axi_apk_view_path(const char *path);

static int btn_col(int i) { return 12 + i * 10; }

static void raise_win(int k) {
    int i = 0;
    while (i < WIN_COUNT && z[i] != k) i++;
    for (; i < WIN_COUNT - 1; i++) z[i] = z[i + 1];
    z[WIN_COUNT - 1] = k;
    keyboard_focus = k;
}

static int top_win(void) {
    for (int i = WIN_COUNT - 1; i >= 0; i--)
        if (wins[z[i]].open) return z[i];
    return -1;
}

static int win_contains(const struct window *w, i32 mx, i32 my) {
    return mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h;
}

static int window_at(i32 mx, i32 my) {
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int k = z[i];
        if (wins[k].open && win_contains(&wins[k], mx, my)) return k;
    }
    return -1;
}

static void open_win(int k) {
    wins[k].open = 1;
    raise_win(k);
    serial_str("desktop: open ");
    serial_str(log_name[k]);
    serial_str("\n");
}

static void launch_terminal(void) {
    u32 entry = 0;
    if (airp_load(TERMINAL_STAGE, TERMINAL_STAGE_LEN, &entry) != 0) {
        serial_str("desktop: terminal.airp did not load\n");
        return;
    }
    serial_str("desktop: open terminal\n");
    vm_run_app(entry, APP_STACK_TOP);
}

static void activate_win(int k) {
    if (k == WIN_TERM)
        launch_terminal();
    else
        open_win(k);
}

static void launch_settings(void) {
    u32 entry = 0;
    if (airp_load(SETTINGS_STAGE, SETTINGS_STAGE_LEN, &entry) != 0)
        kernel_panic("settings.airp failed validation");
    serial_str("desktop: open settings\\n");
    vm_run_app(entry, APP_STACK_TOP);
}

static void launch_notes(void) {
    u32 entry = 0;
    if (airp_load(NOTES_STAGE, NOTES_STAGE_LEN, &entry) != 0)
        kernel_panic("notes.airp failed validation");
    serial_str("desktop: open notes\\n");
    vm_run_app(entry, APP_STACK_TOP);
}

static void launch_explorer(void) {
    u32 entry = 0;
    if (airp_load(EXPLORER_STAGE, EXPLORER_STAGE_LEN, &entry) != 0) {
        serial_str("desktop: explorer.airp did not load\n");
        return;
    }
    serial_str("desktop: open explorer\n");
    vm_run_app(entry, APP_STACK_TOP);
}

#define TERM_IN_MAX 50
#define TERM_HISTORY 8
static char term_in[TERM_IN_MAX + 1];
static int term_in_len;
static int term_cursor;
static char (*term_history)[TERM_IN_MAX + 1];
static int term_history_count;
static int term_history_next;
static int term_history_view = -1;

static void term_history_save(void) {
    if (!term_history) return;
    if (!term_in_len) return;
    if (term_history_count &&
        term_history[(term_history_next + TERM_HISTORY - 1) % TERM_HISTORY][0] &&
        term_in_len == 0) return;
    for (int i = 0; i < term_in_len; i++)
        term_history[term_history_next][i] = term_in[i];
    term_history[term_history_next][term_in_len] = 0;
    term_history_next = (term_history_next + 1) % TERM_HISTORY;
    if (term_history_count < TERM_HISTORY) term_history_count++;
    term_history_view = -1;
}

static void term_history_load(int offset) {
    if (!term_history_count) return;
    if (term_history_view < 0) term_history_view = 0;
    else term_history_view += offset;
    if (term_history_view < 0) term_history_view = 0;
    if (term_history_view >= term_history_count) {
        term_history_view = -1;
        term_in_len = 0;
        term_cursor = 0;
        term_in[0] = 0;
        return;
    }
    int index = (term_history_next + TERM_HISTORY - 1 - term_history_view) % TERM_HISTORY;
    term_in_len = 0;
    while (term_history[index][term_in_len] && term_in_len < TERM_IN_MAX) {
        term_in[term_in_len] = term_history[index][term_in_len];
        term_in_len++;
    }
    term_in[term_in_len] = 0;
    term_cursor = term_in_len;
}

static void draw_terminal(const struct window *w) {
    int rows = w->h - 2;
    int total = term_count();
    int first = total - rows;
    if (first < 0)
        first = 0;
    for (int r = 0; r < rows && first + r < total; r++) {
        int len;
        const char *s = term_text(first + r, &len);
        for (int c = 0; c < len && c < w->w - 4; c++)
            cell(w->y + 1 + r, w->x + 2 + c, (u8)s[c], BODY_ATTR);
    }
    int py = w->y + w->h - 1;
    term_in[term_in_len] = 0;
    text(py, w->x + 2, "> ", BODY_ATTR, 2);
    text(py, w->x + 4, term_in, BODY_ATTR, w->w - 6);
    cell(py, w->x + 4 + term_cursor, 0xDB, 0x70);
}

static void draw_content(int k) {
    const struct window *w = &wins[k];
    if (k == WIN_TERM) {
        draw_terminal(w);
        return;
    }
    if (k == WIN_HELLO) {
        text(w->y + 2, w->x + 2, "Welcome to FlareOS.", BODY_ATTR, w->w - 4);
        text(w->y + 3, w->x + 2, "Click an icon on the left", BODY_ATTR, w->w - 4);
        text(w->y + 4, w->x + 2, "or a button on the taskbar.", BODY_ATTR, w->w - 4);
        text(w->y + 6, w->x + 2, "Ctrl+Alt+D opens developer mode.", BODY_ATTR, w->w - 4);
        text(w->y + 7, w->x + 2, "Arrow keys move the pointer.", BODY_ATTR, w->w - 4);
    } else {
        u8 t[6];
        int c = w->x + (w->w - 8) / 2;
        int dc = w->x + (w->w - 10) / 2;
        rtc_now(t);
        draw_hms(w->y + 2, c, BODY_ATTR, t);
        cell(w->y + 3, dc, '2', BODY_ATTR);
        cell(w->y + 3, dc + 1, '0', BODY_ATTR);
        put2(w->y + 3, dc + 2, t[5], BODY_ATTR);
        cell(w->y + 3, dc + 4, '-', BODY_ATTR);
        put2(w->y + 3, dc + 5, t[4], BODY_ATTR);
        cell(w->y + 3, dc + 7, '-', BODY_ATTR);
        put2(w->y + 3, dc + 8, t[3], BODY_ATTR);
    }
}

static void draw_window(int k) {
    const struct window *w = &wins[k];
    u8 ta = (top_win() == k) ? TITLE_ON : TITLE_OFF;
    fill(w->y + 1, w->x, w->h - 1, w->w, ' ', BODY_ATTR);
    fill(w->y, w->x, 1, w->w, ' ', ta);
    text(w->y, w->x + 1, wins[k].title, ta, w->w - 6);
    text(w->y, w->x + w->w - 8, "[_]", ta, 3);
    text(w->y, w->x + w->w - 4, "[x]", ta, 3);
    draw_content(k);
}

static void draw_start_menu(void) {
    static const char *const items[7] = {
        "1  Welcome", "2  Clock", "3  Terminal", "4  Files",
        "5  Python", "6  Settings", "7  Notes"
    };
    if (!start_open) return;
    fill(START_MENU_Y, START_MENU_X, START_MENU_H, START_MENU_W, ' ', 0x70);
    fill(START_MENU_Y, START_MENU_X, 1, START_MENU_W, ' ', 0x1F);
    text(START_MENU_Y, START_MENU_X + 2, "FlairOS", 0x1F, START_MENU_W - 4);
    for (int i = 0; i < 7; i++)
        text(START_MENU_Y + 1 + i, START_MENU_X + 2, items[i], 0x70, START_MENU_W - 4);
    fill(START_MENU_Y + START_MENU_H - 1, START_MENU_X, 1, START_MENU_W, ' ', 0x17);
    text(START_MENU_Y + START_MENU_H - 1, START_MENU_X + 2, "Tab / Esc to close", 0x17, START_MENU_W - 4);
}

static void start_select(int item) {
    start_open = 0;
    if (item >= 0 && item < WIN_COUNT) activate_win(item);
    else if (item == WIN_COUNT) launch_explorer();
    else if (item == WIN_COUNT + 1) {
        if (micropython_available()) {
            int restore_pixel_desktop = desktop_gfx_active();
            if (restore_pixel_desktop) desktop_gfx_shutdown();
            run_micropython();
            if (restore_pixel_desktop && !desktop_gfx_init()) kernel_panic("AirGraph failed to resume after Python runtime");
        } else {
            open_win(WIN_HELLO);
        }
    } else if (item == WIN_COUNT + 2) launch_settings();
    else if (item == WIN_COUNT + 3) launch_notes();
    else if (item >= 7) (void)launch_installed_apk(item - 7);
    serial_str("desktop: start menu select ");
    log_point("item", item + 1, 0);
}

static void draw_taskbar(void) {
    u8 t[6];
    int top = top_win();
    fill(TASK_ROW, 0, 1, VGA_COLS, ' ', TASK_ATTR);
    text(TASK_ROW, 1, "Start", 0x1F, 5);
    for (int i = 0; i < WIN_COUNT; i++) {
        u8 a = !wins[i].open ? TASK_ATTR : (top == i ? BTN_TOP : BTN_OPEN);
        fill(TASK_ROW, btn_col(i), 1, 9, ' ', a);
        text(TASK_ROW, btn_col(i) + 1, icon_label[i], a, 7);
    }
    text(TASK_ROW, VGA_COLS - 30, "Ctrl+Alt+D: dev", TASK_ATTR, 15);
    rtc_now(t);
    draw_hms(TASK_ROW, VGA_COLS - 9, TASK_ATTR, t);
}

static void pointer_grid(i32 *x, i32 *y) {
    if (desktop_gfx_active() &&
        desktop_gfx_map_pointer(cursor_norm_x, cursor_norm_y, x, y) == 0)
        return;
    *x = (i32)cursor_x;
    *y = (i32)cursor_y;
}

static void draw_cursor(void) {
    i32 r, c;
    pointer_grid(&c, &r);
    if (r < 0 || r >= VGA_ROWS || c < 0 || c >= VGA_COLS) return;
    back[r * VGA_COLS + c] = (u16)(0xDB | (CURSOR_ATTR << 8));
}

static int hover_is_clickable(i32 mx, i32 my) {
    if (my == TASK_ROW) return 1;
    if (apk_context_open && mx >= 27 && mx < 47 &&
        my >= apk_context_y + 1 && my < apk_context_y + 4) return 1;
    if (start_open && mx >= START_MENU_X && mx < START_MENU_X + START_MENU_W &&
        my >= start_menu_y() + 1 && my < start_menu_y() + 1 + start_menu_item_count()) return 1;
    {
        int k = window_at(mx, my);
        if (k >= 0) {
            const struct window *w = &wins[k];
            if (my == w->y && mx >= w->x + w->w - 8 && mx <= w->x + w->w - 2) return 1;
            return 0;
        }
    }
    for (int i = 0; i < WIN_COUNT; i++)
        if (mx >= 2 && mx < 13 && my == icon_row[i]) return 1;
    return (mx >= 2 && mx < 20 && (my == FILES_ROW || my == SETTINGS_ROW));
}

static void update_cursor_shape(void) {
    i32 mx, my;
    pointer_grid(&mx, &my);
    if (keyboard_focus == WIN_TERM && wins[WIN_TERM].open) {
        const struct window *w = &wins[WIN_TERM];
        if (win_contains(w, mx, my) && my > w->y) { cursor_set_shape(CURSOR_IBEAM); return; }
    }
    cursor_set_shape(hover_is_clickable(mx, my) ? CURSOR_HAND : CURSOR_ARROW);
}

struct wd_compositor *desktop_windown_get(void) {
    if (!desktop_wd) {
        desktop_wd = (struct wd_compositor *)kmalloc(sizeof(*desktop_wd));
        if (!desktop_wd) return 0;
        wd_init(desktop_wd);
        for (int i = 0; i < WIN_COUNT; i++) desktop_wd_ids[i] = 0;
    }
    return desktop_wd;
}

static void draw_frame(void) {
    fill(0, 0, VGA_ROWS, VGA_COLS, BG_CHAR, BG_ATTR);
    for (int i = 0; i < WIN_COUNT; i++)
        if (wins[z[i]].open) draw_window(z[i]);
    if (!desktop_gfx_active())
        kernel_panic("AirGraph became unavailable while the desktop was running");
    if (desktop_gfx_present(back, VGA_COLS, VGA_ROWS) != 0)
        kernel_panic("AirGraph desktop renderer failed");
    {
        struct sp_context *ui = desktop_gfx_ui_context();
        if (!ui) kernel_panic("WinDown window composition failed");
        if (!desktop_windown_get()) kernel_panic("WinDown allocation failed");
        for (int i = 0; i < WIN_COUNT; i++) {
            int k = z[i];
            int id = desktop_wd_ids[k];
            if (id <= 0) {
                id = wd_create_window(desktop_wd, wins[k].x, wins[k].y,
                                      wins[k].w, wins[k].h, wins[k].title);
                desktop_wd_ids[k] = id;
            }
            if (id > 0) {
                int32_t wp = an_track(an_id_at(an_id_str("win.open"), k, 0),
                                      wins[k].open ? AN_ONE : 0, 14, AN_OUT_CUBIC);
                int dy = (wins[k].open && wp < AN_ONE) ? ((AN_ONE - wp) * 2) / AN_ONE : 0;
                int wy = wins[k].y + dy;
                if (wy + wins[k].h > VGA_ROWS - 1) wy = wins[k].y;
                wd_set_geometry(desktop_wd, id, wins[k].x, wy, wins[k].w, wins[k].h);
                wd_set_visible(desktop_wd, id, wins[k].open);
                if (wins[k].open) wd_raise(desktop_wd, id);
            }
        }
        if (wd_composite(desktop_wd, ui->device, ui->surface, VGA_COLS, VGA_ROWS) != 0)
            kernel_panic("WinDown window composition failed");
    }
    {
        u8 now[6];
        char clock_text[9];
        rtc_now(now);
        clock_text[0]=(char)('0'+now[2]/10); clock_text[1]=(char)('0'+now[2]%10);
        clock_text[2]=':'; clock_text[3]=(char)('0'+now[1]/10);
        clock_text[4]=(char)('0'+now[1]%10); clock_text[5]=':';
        clock_text[6]=(char)('0'+now[0]/10); clock_text[7]=(char)('0'+now[0]%10);
        clock_text[8]=0;
        desktop_gfx_set_app_context(apk_context_open, apk_context_y, apk_context_title);
        if (desktop_gfx_draw_shell(start_open, clock_text) != 0)
            kernel_panic("Material desktop shell composition failed");
    }
    update_cursor_shape();
    axi_gfx_render();
    if (desktop_gfx_flush() != 0)
        kernel_panic("AirGraph frame presentation failed");
}

void axi_poll_mouse(void);
void desktop_present_app(void) {
    draw_frame();
}

static void click(i32 mx, i32 my) {
    log_point("desktop: click", mx, my);
    if (apk_context_open) {
        if (mx >= 27 && mx < 47 && my >= apk_context_y + 1 && my < apk_context_y + 4) {
            int action = my - apk_context_y - 1;
            char path[96];
            apk_context_open = 0;
            desktop_gfx_set_app_context(0, 0, "");
            if (installed_apk_get(apk_context_index, path, sizeof(path), 0, 0) == 0) {
                if (action == 0) (void)axi_apk_launch_path(path);
                else if (action == 1) (void)axi_apk_view_path(path);
                else if (action == 2 && vfs_unlink(path) == 0)
                    serial_str("desktop: APK uninstalled\n");
            }
            apk_context_index = -1;
            return;
        }
        apk_context_open = 0;
        apk_context_index = -1;
        desktop_gfx_set_app_context(0, 0, "");
    }
    if (desktop_gfx_active()) {
        if (my == TASK_ROW && desktop_gfx_status_hit(mx)) {
            start_open = 0;
            desktop_gfx_panel_set(!desktop_gfx_panel_open());
            return;
        }
        if (desktop_gfx_panel_open()) {
            int used = desktop_gfx_panel_click(mx, my);
            if (used == 2) { desktop_gfx_panel_set(0); launch_settings(); return; }
            if (used == 1) return;
            desktop_gfx_panel_set(0);   /* a click elsewhere closes the panel, then acts normally */
        }
    }
    if (my == TASK_ROW && mx >= 0 && mx < 10) {
        start_open = !start_open;
        if (start_open && desktop_gfx_active()) desktop_gfx_panel_set(0);
        serial_str(start_open ? "desktop: start menu open\n" : "desktop: start menu close\n");
        return;
    }
    if (start_open) {
        if (mx >= START_MENU_X && mx < START_MENU_X + START_MENU_W &&
            my >= start_menu_y() + 1 && my < start_menu_y() + 1 + start_menu_item_count()) {
            start_select(my - start_menu_y() - 1);
            return;
        }
        start_open = 0;
    }
    if (my == TASK_ROW) {
        for (int i = 0; i < WIN_COUNT; i++)
            if (mx >= btn_col(i) && mx < btn_col(i) + 9) { activate_win(i); return; }
        return;
    }
    int k = window_at(mx, my);
    if (k >= 0) {
        struct window *w = &wins[k];
        if (my == w->y && mx >= w->x + 1 && mx <= w->x + 2) {
            w->open = 0;
            if (keyboard_focus == k) keyboard_focus = top_win();
            return;
        }
        if (my == w->y && mx >= w->x + 3 && mx <= w->x + 4) {
            w->open = 0;
            if (keyboard_focus == k) keyboard_focus = top_win();
            serial_str("desktop: minimize\n");
            return;
        }
        raise_win(k);
        if (my == w->y) {
            drag_win = k;
            drag_dx = mx - w->x;
            drag_dy = my - w->y;
            return;
        }
        return;
    }
    for (int i = 0; i < WIN_COUNT; i++)
        if (mx >= 2 && mx < 13 && my == icon_row[i]) { activate_win(i); return; }
    if (mx >= 2 && mx < 20 && my == FILES_ROW) { launch_explorer(); return; }
    if (mx >= 2 && mx < 20 && my == SETTINGS_ROW) { launch_settings(); return; }
}

static void handle_mouse(void) {
    axi_poll_mouse();
    i32 mx, my;
    pointer_grid(&mx, &my);
    u8 b = mouse_buttons;
    u8 latched = mouse_take_clicks();
    if ((b & 2u) && !(prev_buttons & 2u) && start_open &&
        mx >= START_MENU_X && mx < START_MENU_X + START_MENU_W) {
        int item = my - start_menu_y() - 1;
        if (item >= 7 && item < start_menu_item_count() &&
            installed_apk_get(item - 7, (char[96]){0}, 96,
                              apk_context_title, sizeof(apk_context_title)) == 0) {
            apk_context_index = item - 7;
            apk_context_y = my;
            if (apk_context_y > TASK_ROW - 4) apk_context_y = TASK_ROW - 4;
            apk_context_open = 1;
            desktop_gfx_set_app_context(1, apk_context_y, apk_context_title);
        }
    }
    if (latched & 1) {
        /* Use the pointer position recorded when the button went down, not where it is now. */
        i32 px = mx, py = my;
        if (!(desktop_gfx_active() &&
              desktop_gfx_map_pointer(mouse_press_x, mouse_press_y, &px, &py) == 0)) { px = mx; py = my; }
        click(px, py);
    } else if ((b & 1) && !(prev_buttons & 1)) click(mx, my);
    if (!(b & 1)) { drag_win = -1; }
    else if (drag_win >= 0) {
        struct window *w = &wins[drag_win];
        w->x = clampi(mx - drag_dx, 0, VGA_COLS - w->w);
        w->y = clampi(my - drag_dy, 0, TASK_ROW - w->h);
    }
    prev_buttons = b;
}

static void term_key(int c) {
    if (c == '\n') {
        term_in[term_in_len] = 0;
        term_history_save();
        terminal_exec(term_in);
        term_in_len = 0;
        term_cursor = 0;
        term_in[0] = 0;
        term_history_view = -1;
        return;
    }
    if (c == '\b') {
        if (term_cursor > 0) {
            for (int i = term_cursor - 1; i < term_in_len - 1; i++)
                term_in[i] = term_in[i + 1];
            term_cursor--;
            term_in_len--;
            term_in[term_in_len] = 0;
        }
        return;
    }
    if (c >= 32 && c < 127 && term_in_len < TERM_IN_MAX) {
        for (int i = term_in_len; i > term_cursor; i--)
            term_in[i] = term_in[i - 1];
        term_in[term_cursor++] = (char)c;
        term_in_len++;
        term_in[term_in_len] = 0;
        term_history_view = -1;
    }
}

static void term_arrow(int a) {
    if (a == ARROW_UP) {
        term_history_load(1);
    } else if (a == ARROW_DOWN) {
        term_history_load(-1);
    } else if (a == ARROW_LEFT) {
        if (term_cursor > 0) term_cursor--;
    } else if (a == ARROW_RIGHT) {
        if (term_cursor < term_in_len) term_cursor++;
    }
}

static void handle_key(int c) {
#ifdef FLAIROS_DEBUG
    log_point("desktop: key", c, keyboard_focus);
#endif
    if (c == 27 && desktop_gfx_active() && desktop_gfx_panel_open()) {
        desktop_gfx_panel_set(0);
        return;
    }
    if (c == '\t') {
        start_open = !start_open;
        if (start_open && desktop_gfx_active()) desktop_gfx_panel_set(0);
        serial_str(start_open ? "desktop: start menu open\n" : "desktop: start menu close\n");
        return;
    }
    if (start_open) {
        if (c >= '1' && c <= '9' && (c - '1') < start_menu_item_count()) {
            start_select(c - '1');
            return;
        }
        if (c == 27) { start_open = 0; return; }
    }
    if (keyboard_focus == WIN_TERM && wins[WIN_TERM].open) {
        term_key(c);
        return;
    }
    if (c == '\n') {
        i32 mx, my;
        pointer_grid(&mx, &my);
        click(mx, my);
        return;
    }
}

int desktop_run(void) {
    /* Start with a clean workspace. The built-in Terminal remains available
       from its desktop icon and the Start menu; it is not restored on boot. */
    serial_str("desktop: start\n");
    if (!back) back = (u16 *)kmalloc(VGA_ROWS * VGA_COLS * sizeof(u16));
    if (!term_history) term_history = (char (*)[TERM_IN_MAX + 1])kmalloc(TERM_HISTORY * (TERM_IN_MAX + 1));
    if (!back) kernel_panic("Desktop backbuffer allocation failed");
    if (!desktop_gfx_init()) kernel_panic("Required AirGraph desktop initialization failed");
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    prev_buttons = mouse_buttons;
    drag_win = -1;
    start_open = 0;
    for (;;) {
        int c;
        while ((c = kbd_read()) >= 0)
            handle_key(c);
        int a;
        while ((a = kbd_arrow_read()) >= 0) {
            if (top_win() == WIN_TERM) {
                term_arrow(a);
            } else {
                i32 dx = 0, dy = 0;
                if (a == ARROW_UP) dy = -1;
                else if (a == ARROW_DOWN) dy = 1;
                else if (a == ARROW_LEFT) dx = -1;
                else dx = 1;
                cursor_nudge(dx, dy);
            }
        }
        handle_mouse();
        draw_frame();
        /* Ctrl+Alt+D leaves the desktop for the developer text console.
           The outer session loop returns here after the console exits. */
        if (kbd_toggle_take()) {
            serial_str("debug: Ctrl+Alt+D -> writing /SYS.LOG\n");
            debug_log_dump_sys_log();
            if (desktop_gfx_active())
                desktop_gfx_shutdown();
            break;
        }
        wait_ticks(FRAME_TICKS);
    }
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
    serial_str("desktop: exit\n");
    return 1;
}
