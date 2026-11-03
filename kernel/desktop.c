/*
FILE PATH: kernel/desktop.c
CONTRIBUTOR (Optional): Textile
PURPOSE: FlareOS desktop in the 80x25 text buffer. Draws a macOS-style menu bar on row 0,
a centred dock on the last row, and windows with traffic-light buttons (close, minimize,
zoom), centred titles and a one-cell drop shadow. Keyboard, mouse, terminal and piano
input work as before.
*/

#include "kernel.h"

#define MENU_ROW 0
#define DOCK_ROW (VGA_ROWS - 1)
#define BG_CHAR 0xB0
#define BG_ATTR 0x13
#define BODY_ATTR 0x70
#define MENU_ATTR 0x70
#define DOCK_BG_ATTR 0x08
#define DOCK_IDLE_ATTR 0x70
#define DOCK_OPEN_ATTR 0x78
#define DOCK_TOP_ATTR 0x1F
#define TITLE_FOCUS 0x70
#define TITLE_BLUR 0x78
#define LIGHT_CLOSE 0x74
#define LIGHT_MIN 0x7E
#define LIGHT_ZOOM 0x72
#define LIGHT_BLUR 0x78
#define DOT_CHAR 0x07
#define SHADOW_CHAR 0xB2
#define SHADOW_ATTR 0x08
#define ICON_ATTR 0x70
#define KEY_ATTR 0x4F
#define PIANO_KEYS 8
#define PIANO_FLASH 12
#define FRAME_TICKS 2
#define CURSOR_ATTR 0x0F
#define FILES_ROW 10
#define DOCK_ITEM_W 9
#define DOCK_GAP 1
#define DOCK_ITEMS 5
#define DOCK_X0 ((VGA_COLS - (DOCK_ITEMS * DOCK_ITEM_W + (DOCK_ITEMS - 1) * DOCK_GAP)) / 2)
#define EXPLORER_STAGE ((const u8 *)0x30000)
#define EXPLORER_STAGE_LEN 4608
#define APP_STACK_TOP 0x28000u

int airp_load(const u8 *file, u32 len, u32 *entry);
void enter_user(u32 entry, u32 user_esp);

enum { WIN_HELLO, WIN_PIANO, WIN_CLOCK, WIN_TERM, WIN_COUNT };

struct window {
    int open;
    int minimized;
    int zoomed;
    i32 x, y, w, h;
    i32 sx, sy, sw, sh;
    const char *title;
};

static struct window wins[WIN_COUNT] = {
    { .x = 22, .y = 3, .w = 38, .h = 9, .title = "Hello" },
    { .x = 12, .y = 12, .w = 40, .h = 7, .title = "Piano" },
    { .x = 60, .y = 4, .w = 18, .h = 5, .title = "Clock" },
    { .x = 4, .y = 2, .w = 60, .h = 17, .title = "Terminal" },
};

static const char *const icon_label[WIN_COUNT] = {"Hello", "Piano", "Clock", "Term"};
static const char *const dock_label[DOCK_ITEMS] = {"Hello", "Piano", "Clock", "Term", "Files"};
static const char *const log_name[WIN_COUNT] = {"hello", "piano", "clock", "terminal"};
static const i32 icon_row[WIN_COUNT] = {2, 4, 6, 8};
static const char piano_keys[PIANO_KEYS + 1] = "asdfghjk";
static const char piano_notes[PIANO_KEYS + 1] = "CDEFGABC";
static const u32 piano_hz[PIANO_KEYS] = {262, 294, 330, 349, 392, 440, 494, 523};

static u16 back[VGA_ROWS * VGA_COLS];
static int z[WIN_COUNT] = {WIN_HELLO, WIN_PIANO, WIN_CLOCK, WIN_TERM};
static int drag_win = -1;
static i32 drag_dx, drag_dy;
static u8 prev_buttons;
static int piano_last = -1;
static int piano_flash;

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

static void raise_win(int k) {
    int i = 0;
    while (i < WIN_COUNT && z[i] != k) i++;
    for (; i < WIN_COUNT - 1; i++) z[i] = z[i + 1];
    z[WIN_COUNT - 1] = k;
}

static int top_win(void) {
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int k = z[i];
        if (wins[k].open && !wins[k].minimized) return k;
    }
    return -1;
}

static int win_contains(const struct window *w, i32 mx, i32 my) {
    return mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w->h;
}

static int window_at(i32 mx, i32 my) {
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        int k = z[i];
        if (wins[k].open && !wins[k].minimized && win_contains(&wins[k], mx, my)) return k;
    }
    return -1;
}

static void open_win(int k) {
    wins[k].open = 1;
    wins[k].minimized = 0;
    raise_win(k);
    serial_str("desktop: open ");
    serial_str(log_name[k]);
    serial_str("\n");
}

static void minimize_win(int k) {
    wins[k].minimized = 1;
    serial_str("desktop: minimize ");
    serial_str(log_name[k]);
    serial_str("\n");
}

static void close_win(int k) {
    struct window *w = &wins[k];
    if (w->zoomed) {
        w->x = w->sx;
        w->y = w->sy;
        w->w = w->sw;
        w->h = w->sh;
        w->zoomed = 0;
    }
    w->open = 0;
    w->minimized = 0;
    serial_str("desktop: close ");
    serial_str(log_name[k]);
    serial_str("\n");
}

static void zoom_win(int k) {
    struct window *w = &wins[k];
    if (w->zoomed) {
        w->x = w->sx;
        w->y = w->sy;
        w->w = w->sw;
        w->h = w->sh;
        w->zoomed = 0;
    } else {
        w->sx = w->x;
        w->sy = w->y;
        w->sw = w->w;
        w->sh = w->h;
        w->x = 0;
        w->y = 1;
        w->w = VGA_COLS;
        w->h = DOCK_ROW - 1;
        w->zoomed = 1;
    }
    raise_win(k);
}

static void piano_play(int i) {
    piano_last = i;
    piano_flash = PIANO_FLASH;
    log_point("piano: key", i, 0);
    sound_tone(piano_hz[i], 120);
}

static void launch_explorer(void) {
    u32 entry = 0;
    if (airp_load(EXPLORER_STAGE, EXPLORER_STAGE_LEN, &entry) != 0) {
        serial_str("desktop: explorer.airp did not load\n");
        return;
    }
    serial_str("desktop: open explorer\n");
    enter_user(entry, APP_STACK_TOP);
}

#define TERM_IN_MAX 50
static char term_in[TERM_IN_MAX + 1];
static int term_in_len;

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
    cell(py, w->x + 4 + term_in_len, '_', BODY_ATTR);
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
        text(w->y + 4, w->x + 2, "or an icon in the dock.", BODY_ATTR, w->w - 4);
        text(w->y + 6, w->x + 2, "Ctrl+Alt+D opens developer mode.", BODY_ATTR, w->w - 4);
        text(w->y + 7, w->x + 2, "Arrow keys move the pointer.", BODY_ATTR, w->w - 4);
    } else if (k == WIN_PIANO) {
        text(w->y + 2, w->x + 2, "Click or type a s d f g h j k", BODY_ATTR, w->w - 4);
        for (int i = 0; i < PIANO_KEYS; i++) {
            u8 a = (piano_flash > 0 && piano_last == i) ? KEY_ATTR : BODY_ATTR;
            cell(w->y + 3, w->x + 2 + i * 4, (u8)piano_keys[i], a);
            cell(w->y + 4, w->x + 2 + i * 4, (u8)piano_notes[i], a);
        }
        if (piano_last >= 0) {
            text(w->y + 5, w->x + 2, "Last key:", BODY_ATTR, 9);
            cell(w->y + 5, w->x + 12, (u8)piano_keys[piano_last], BODY_ATTR);
        } else {
            text(w->y + 5, w->x + 2, "Nothing played yet", BODY_ATTR, w->w - 4);
        }
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
    int focus = (top_win() == k);
    u8 ta = focus ? TITLE_FOCUS : TITLE_BLUR;
    int len = 0;
    int avail = w->w - 10;
    int c;
    while (w->title[len]) len++;
    if (len > avail) len = avail;
    if (len < 0) len = 0;
    c = w->x + (w->w - len) / 2;
    fill(w->y + 1, w->x + 1, w->h, w->w, SHADOW_CHAR, SHADOW_ATTR);
    fill(w->y, w->x, 1, w->w, ' ', ta);
    fill(w->y + 1, w->x, w->h - 1, w->w, ' ', BODY_ATTR);
    cell(w->y, w->x + 2, DOT_CHAR, focus ? LIGHT_CLOSE : LIGHT_BLUR);
    cell(w->y, w->x + 4, DOT_CHAR, focus ? LIGHT_MIN : LIGHT_BLUR);
    cell(w->y, w->x + 6, DOT_CHAR, focus ? LIGHT_ZOOM : LIGHT_BLUR);
    text(w->y, c, w->title, ta, len);
    draw_content(k);
}

static void draw_dock(void) {
    int top = top_win();
    fill(DOCK_ROW, 0, 1, VGA_COLS, ' ', DOCK_BG_ATTR);
    for (int i = 0; i < DOCK_ITEMS; i++) {
        int x = DOCK_X0 + i * (DOCK_ITEM_W + DOCK_GAP);
        u8 a = DOCK_IDLE_ATTR;
        int shown = i < WIN_COUNT && wins[i].open && !wins[i].minimized;
        if (i < WIN_COUNT && wins[i].open)
            a = DOCK_OPEN_ATTR;
        if (i == top)
            a = DOCK_TOP_ATTR;
        fill(DOCK_ROW, x, 1, DOCK_ITEM_W, ' ', a);
        if (shown)
            cell(DOCK_ROW, x + 1, DOT_CHAR, a);
        text(DOCK_ROW, x + 2, dock_label[i], a, DOCK_ITEM_W - 3);
    }
}

static void draw_menubar(void) {
    u8 t[6];
    fill(MENU_ROW, 0, 1, VGA_COLS, ' ', MENU_ATTR);
    text(MENU_ROW, 1, "FlareOS", MENU_ATTR, 7);
    text(MENU_ROW, 10, "File", MENU_ATTR, 4);
    text(MENU_ROW, 16, "Edit", MENU_ATTR, 4);
    text(MENU_ROW, 22, "View", MENU_ATTR, 4);
    text(MENU_ROW, 28, "Window", MENU_ATTR, 6);
    text(MENU_ROW, 37, "Help", MENU_ATTR, 4);
    text(MENU_ROW, VGA_COLS - 24, "Ctrl+Alt+D: dev", MENU_ATTR, 15);
    rtc_now(t);
    draw_hms(MENU_ROW, VGA_COLS - 9, MENU_ATTR, t);
}

static void draw_cursor(void) {
    i32 r = (i32)cursor_y, c = (i32)cursor_x;
    if (r < 0 || r >= VGA_ROWS || c < 0 || c >= VGA_COLS) return;
    back[r * VGA_COLS + c] = (u16)(0xDB | (CURSOR_ATTR << 8));
}

static void draw_frame(void) {
    fill(0, 0, VGA_ROWS, VGA_COLS, BG_CHAR, BG_ATTR);
    for (int i = 0; i < WIN_COUNT; i++) {
        fill(icon_row[i], 2, 1, 9, ' ', ICON_ATTR);
        text(icon_row[i], 3, icon_label[i], ICON_ATTR, 7);
    }
    fill(FILES_ROW, 2, 1, 9, ' ', ICON_ATTR);
    text(FILES_ROW, 3, "Files", ICON_ATTR, 7);
    for (int i = 0; i < WIN_COUNT; i++)
        if (wins[z[i]].open && !wins[z[i]].minimized)
            draw_window(z[i]);
    draw_dock();
    draw_menubar();
    draw_cursor();
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_MEM[i] = back[i];
}

static void click(i32 mx, i32 my) {
    log_point("desktop: click", mx, my);
    if (my == MENU_ROW)
        return;
    if (my == DOCK_ROW) {
        for (int i = 0; i < DOCK_ITEMS; i++) {
            int x = DOCK_X0 + i * (DOCK_ITEM_W + DOCK_GAP);
            if (mx >= x && mx < x + DOCK_ITEM_W) {
                if (i < WIN_COUNT)
                    open_win(i);
                else
                    launch_explorer();
                return;
            }
        }
        return;
    }
    int k = window_at(mx, my);
    if (k >= 0) {
        struct window *w = &wins[k];
        if (my == w->y) {
            if (mx == w->x + 2) { close_win(k); return; }
            if (mx == w->x + 4) { minimize_win(k); return; }
            if (mx == w->x + 6) { zoom_win(k); return; }
            raise_win(k);
            if (!w->zoomed) {
                drag_win = k;
                drag_dx = mx - w->x;
                drag_dy = my - w->y;
            }
            return;
        }
        raise_win(k);
        if (k == WIN_PIANO && my == w->y + 3) {
            i32 rx = mx - (w->x + 2);
            if (rx >= 0 && rx % 4 == 0 && rx / 4 < PIANO_KEYS)
                piano_play(rx / 4);
        }
        return;
    }
    for (int i = 0; i < WIN_COUNT; i++)
        if (mx >= 2 && mx <= 10 && my == icon_row[i]) { open_win(i); return; }
    if (mx >= 2 && mx <= 10 && my == FILES_ROW) { launch_explorer(); return; }
}

static void handle_mouse(void) {
    i32 mx = (i32)cursor_x, my = (i32)cursor_y;
    u8 b = mouse_buttons;
    if ((b & 1) && !(prev_buttons & 1)) click(mx, my);
    if (!(b & 1)) { drag_win = -1; }
    else if (drag_win >= 0) {
        struct window *w = &wins[drag_win];
        w->x = clampi(mx - drag_dx, 0, VGA_COLS - w->w);
        w->y = clampi(my - drag_dy, 1, DOCK_ROW - w->h);
    }
    prev_buttons = b;
}

static void term_key(int c) {
    if (c == '\n') {
        term_in[term_in_len] = 0;
        terminal_exec(term_in);
        term_in_len = 0;
        return;
    }
    if (c == '\b') {
        if (term_in_len > 0)
            term_in_len--;
        return;
    }
    if (c >= 32 && c < 127 && term_in_len < TERM_IN_MAX)
        term_in[term_in_len++] = (char)c;
}

static void handle_key(int c) {
    if (top_win() == WIN_TERM) {
        term_key(c);
        return;
    }
    if (c == '\n') {
        click((i32)cursor_x, (i32)cursor_y);
        return;
    }
    if (top_win() != WIN_PIANO) return;
    for (int i = 0; i < PIANO_KEYS; i++)
        if (piano_keys[i] == c) { piano_play(i); return; }
}

int desktop_run(void) {
    serial_str("desktop: start\n");
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
    prev_buttons = mouse_buttons;
    drag_win = -1;
    for (;;) {
        if (kbd_toggle_take())
            break;
        int c;
        while ((c = kbd_read()) >= 0)
            handle_key(c);
        int a;
        while ((a = kbd_arrow_read()) >= 0) {
            i32 dx = 0, dy = 0;
            if (a == ARROW_UP) dy = -1;
            else if (a == ARROW_DOWN) dy = 1;
            else if (a == ARROW_LEFT) dx = -1;
            else dx = 1;
            cursor_nudge(dx, dy);
        }
        handle_mouse();
        if (piano_flash > 0) piano_flash--;
        draw_frame();
        wait_ticks(FRAME_TICKS);
    }
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x0E);
    serial_str("desktop: exit\n");
    return 1;
}
