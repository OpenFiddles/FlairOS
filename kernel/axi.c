#include "kernel.h"
#include "../include/axi.h"
#include "../include/desktop_gfx.h"
#include "../include/sprunkui.h"

void desktop_present_app(void);

#define AXK_SLOTS 4
#define AXK_TITLE_MAX 24
#define AXK_DESKTOP_ROWS 24
#define AXK_BODY_ATTR 0x70
#define AXK_TITLE_ATTR 0x1F
#define AXK_CURSOR_ATTR 0x0F
#define AXK_MIN_W 8
#define AXK_MIN_H 3

struct axk_win {
    int used;
    int open;
    int clicked;
    i32 x, y, w, h;
    i32 click_row, click_col;
    char title[AXK_TITLE_MAX + 1];
    u16 body[AX_MAX_BODY_H * AX_MAX_BODY_W];
};

static struct axk_win axk_wins[AXK_SLOTS];
static u8 axk_prev_buttons;
static i32 axk_cur_r = -1, axk_cur_c = -1;
static u16 axk_under;

static void axk_vga(i32 r, i32 c, u8 ch, u8 attr) {
    if (r < 0 || r >= VGA_ROWS || c < 0 || c >= VGA_COLS)
        return;
    VGA_MEM[r * VGA_COLS + c] = (u16)(ch | (attr << 8));
}

static struct axk_win *axk_get(u32 handle) {
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

static void axk_mouse(void) {
    i32 mx = (i32)cursor_x, my = (i32)cursor_y;
    u8 b = mouse_buttons;
    if (desktop_gfx_active())
        (void)desktop_gfx_map_pointer(cursor_norm_x, cursor_norm_y, &mx, &my);
    if ((b & 1) && !(axk_prev_buttons & 1)) {
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
    }
    axk_prev_buttons = b;
}

static void axk_draw(void) {
    if (axk_cur_r >= 0)
        VGA_MEM[axk_cur_r * VGA_COLS + axk_cur_c] = axk_under;
    axk_cur_r = -1;

    for (int k = 0; k < AXK_SLOTS; k++) {
        struct axk_win *w = &axk_wins[k];
        if (!w->used || !w->open)
            continue;
        for (i32 c = 0; c < w->w; c++)
            axk_vga(w->y, w->x + c, ' ', AXK_TITLE_ATTR);
        for (i32 c = 0; w->title[c] && c < w->w - 6; c++)
            axk_vga(w->y, w->x + 1 + c, (u8)w->title[c], AXK_TITLE_ATTR);
        axk_vga(w->y, w->x + w->w - 4, '[', AXK_TITLE_ATTR);
        axk_vga(w->y, w->x + w->w - 3, 'x', AXK_TITLE_ATTR);
        axk_vga(w->y, w->x + w->w - 2, ']', AXK_TITLE_ATTR);
        for (i32 r = 0; r < w->h - 1; r++)
            for (i32 c = 0; c < w->w; c++)
                VGA_MEM[(w->y + 1 + r) * VGA_COLS + w->x + c] = w->body[r * AX_MAX_BODY_W + c];
    }

    i32 r = (i32)cursor_y, c = (i32)cursor_x;
    if (r >= 0 && r < VGA_ROWS && c >= 0 && c < VGA_COLS) {
        axk_cur_r = r;
        axk_cur_c = c;
        axk_under = VGA_MEM[r * VGA_COLS + c];
        VGA_MEM[r * VGA_COLS + c] = (u16)(0xDB | (AXK_CURSOR_ATTR << 8));
    }
}

static int axk_is_terminal(const char *title) {
    static const char name[] = "Terminal";
    int i;
    for (i = 0; name[i] && title[i] == name[i]; ++i) {}
    return name[i] == 0 && title[i] == 0;
}

void axi_gfx_render(void) {
    struct sp_context *ui = desktop_gfx_ui_context();
    int32_t sw, sh;
    if (!ui || !ui->surface || !ui->theme) return;
    sw = ui->surface->width;
    sh = ui->surface->height;

    for (int k = 0; k < AXK_SLOTS; ++k) {
        struct axk_win *w = &axk_wins[k];
        int32_t x, y, width, height, title_h, body_h, body_rows;
        if (!w->used || !w->open) continue;

        x = (w->x * sw) / VGA_COLS;
        y = (w->y * sh) / VGA_ROWS;
        width = ((w->x + w->w) * sw + VGA_COLS - 1) / VGA_COLS - x;
        height = ((w->y + w->h) * sh + VGA_ROWS - 1) / VGA_ROWS - y;
        if (width < 40 || height < 30) continue;

        title_h = sh / VGA_ROWS;
        if (title_h < 12) title_h = 12;
        if (title_h > height / 2) title_h = height / 2;
        body_h = height - title_h;
        body_rows = w->h - 1;
        if (body_rows <= 0 || body_h <= 0) continue;

        sp_panel(ui, x, y, width, height);
        sp_button(ui, x + 3, y + 2, width - 50, title_h - 4,
                  w->title, 1);
        sp_badge(ui, x + width - 44, y + 2, "X",
                 ui->theme->color.danger);
        sp_separator(ui, x + 4, y + title_h, width - 8);

        for (int row = 0; row < body_rows; ++row) {
            char line[AX_MAX_BODY_W + 1];
            int last = w->w;
            int selected = 0, muted = 0;
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
                }
            }
            if (last > AX_MAX_BODY_W) last = AX_MAX_BODY_W;
            while (last > 0 && line[last - 1] == ' ') --last;
            line[last] = 0;
            if (row_h <= 0 || !last) continue;

            if (axk_is_terminal(w->title) && row == body_rows - 1) {
                sp_text_field(ui, x + 4, row_y + 1, width - 8,
                              row_h - 2, line, 1);
            } else if (selected) {
                sp_list_item(ui, x + 4, row_y, width - 8, row_h,
                             line, 0, 1);
            } else {
                sp_label(ui, x + 8,
                         row_y + (row_h > 7 ? (row_h - 7) / 2 : 0),
                         line, muted);
            }
        }
    }

    /* Draw the pointer last so app windows do not cover it. */
    {
        int32_t mx = (int32_t)cursor_x, my = (int32_t)cursor_y;
        if (desktop_gfx_map_pointer(cursor_norm_x, cursor_norm_y, &mx, &my) == 0) {
            int32_t px = (mx * sw) / VGA_COLS;
            int32_t py = (my * sh) / VGA_ROWS;
            ag_fill_rect(ui->device, ui->surface, px, py, 3, 13, 0x000000);
            ag_fill_rect(ui->device, ui->surface, px + 1, py + 1, 1, 10, 0x00FFFFFF);
        }
    }
}

static u32 axk_present(void) {
    axk_mouse();
    if (desktop_gfx_active()) {
        desktop_present_app();
        if (desktop_gfx_active()) return 0;
    }
    axk_draw();
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
    default:
        return 0;
    }
}
