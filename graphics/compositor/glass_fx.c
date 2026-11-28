#include "../../include/bmp.h"
/* SPDX-License-Identifier: AGPL-3.0-only */
#include <stddef.h>
#include "glass_fx.h"
#include "../../include/font5x7.h"
#include "../../ui/theme/ios_macos_theme.h"

static uint32_t lerp_rgb(uint32_t a, uint32_t b, uint32_t num, uint32_t den) {
    if (den == 0) return a;
    uint32_t ar = (a >> 16) & 0xFFu, ag = (a >> 8) & 0xFFu, ab = a & 0xFFu;
    uint32_t br = (b >> 16) & 0xFFu, bg = (b >> 8) & 0xFFu, bb = b & 0xFFu;
    uint32_t rr = ar + ((br - ar) * num) / den;
    uint32_t rg = ag + ((bg - ag) * num) / den;
    uint32_t rb = ab + ((bb - ab) * num) / den;
    return (rr << 16) | (rg << 8) | rb;
}

/* One content transform at a time: the shell installs it around a window's
   content and clears it before chrome, cursor or wallpaper. Identity means
   zero branches beyond the flag check in every primitive. */
static uint32_t tf_scale;
static int32_t tf_ox, tf_oy;
static int32_t tf_x0, tf_y0, tf_x1, tf_y1;
static int tf_active;

void gfx_transform_set(uint32_t scale_q16, int32_t ox, int32_t oy,
                       int32_t clip_x, int32_t clip_y,
                       int32_t clip_w, int32_t clip_h) {
    if (scale_q16 == 0u) scale_q16 = AG_SCALE_ONE;
    tf_scale = scale_q16;
    tf_ox = ox;
    tf_oy = oy;
    tf_x0 = clip_x < 0 ? 0 : clip_x;
    tf_y0 = clip_y < 0 ? 0 : clip_y;
    tf_x1 = clip_w > 0 ? clip_x + clip_w : clip_x;
    tf_y1 = clip_h > 0 ? clip_y + clip_h : clip_y;
    tf_active = 1;
}

void gfx_transform_clear(void) { tf_active = 0; }
int gfx_transform_active(void) { return tf_active; }

static int32_t tf_map_x(int32_t x) {
    return (int32_t)(((int64_t)x * tf_scale) >> 16) + tf_ox;
}

static int32_t tf_map_y(int32_t y) {
    return (int32_t)(((int64_t)y * tf_scale) >> 16) + tf_oy;
}

/* Map and clip an inclusive-exclusive design rect into the client rect. */
static int tf_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    int32_t x0, y0, x1, y1;
    if (!tf_active) return 1;
    if (*w <= 0 || *h <= 0) return 0;
    x0 = tf_map_x(*x);
    y0 = tf_map_y(*y);
    x1 = tf_map_x(*x + *w);
    y1 = tf_map_y(*y + *h);
    if (x0 < tf_x0) x0 = tf_x0;
    if (y0 < tf_y0) y0 = tf_y0;
    if (x1 > tf_x1) x1 = tf_x1;
    if (y1 > tf_y1) y1 = tf_y1;
    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return *w > 0 && *h > 0;
}

static void blend_raw(struct ag_surface *s, int32_t x, int32_t y,
                      uint32_t rgb, uint8_t alpha) {
    if (!s || !s->pixels || x < 0 || y < 0 || x >= s->width || y >= s->height) return;
    uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)y * s->pitch);
    if (alpha == 255u) { row[x] = rgb & 0x00FFFFFFu; return; }
    if (alpha == 0u) return;
    uint32_t dst = row[x];
    uint32_t a1 = (uint32_t)alpha + 1u;
    uint32_t inv = 256u - a1;
    uint32_t r = (((rgb >> 16) & 0xFFu) * a1 + ((dst >> 16) & 0xFFu) * inv) >> 8;
    uint32_t g = (((rgb >> 8) & 0xFFu) * a1 + ((dst >> 8) & 0xFFu) * inv) >> 8;
    uint32_t b = ((rgb & 0xFFu) * a1 + (dst & 0xFFu) * inv) >> 8;
    row[x] = (r << 16) | (g << 8) | b;
}

void gfx_blend_pixel(struct ag_surface *s, int32_t x, int32_t y, uint32_t rgb, uint8_t alpha) {
    if (tf_active) {
        /* A transformed design pixel owns its whole scaled footprint. Mapping
           only the top-left corner leaves gaps in text and rounded corners
           (the 4K content looked washed out because of it). */
        int32_t x0 = tf_map_x(x), y0 = tf_map_y(y);
        int32_t x1 = tf_map_x(x + 1), y1 = tf_map_y(y + 1);
        if (x1 <= x0) x1 = x0 + 1;
        if (y1 <= y0) y1 = y0 + 1;
        if (x0 < tf_x0) x0 = tf_x0;
        if (y0 < tf_y0) y0 = tf_y0;
        if (x1 > tf_x1) x1 = tf_x1;
        if (y1 > tf_y1) y1 = tf_y1;
        if (!s || !s->pixels) return;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > s->width) x1 = s->width;
        if (y1 > s->height) y1 = s->height;
        for (int32_t py = y0; py < y1; py++)
            for (int32_t px = x0; px < x1; px++)
                blend_raw(s, px, py, rgb, alpha);
        return;
    }
    blend_raw(s, x, y, rgb, alpha);
}

void gfx_fill_rect_alpha(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgb, uint8_t alpha) {
    if (!s || !s->pixels || w <= 0 || h <= 0 || alpha == 0u) return;
    if (!tf_rect(&x, &y, &w, &h)) return;
    int32_t x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > s->width ? s->width : x + w;
    int32_t y1 = y + h > s->height ? s->height : y + h;
    uint32_t col = rgb & 0x00FFFFFFu;
    if (alpha == 255u) {
        for (int32_t py = y0; py < y1; py++) {
            uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)py * s->pitch);
            for (int32_t px = x0; px < x1; px++) row[px] = col;
        }
        return;
    }
    uint32_t a1 = (uint32_t)alpha + 1u, inv = 256u - a1;
    uint32_t sr = ((col >> 16) & 0xFFu) * a1;
    uint32_t sg = ((col >> 8) & 0xFFu) * a1;
    uint32_t sb = (col & 0xFFu) * a1;
    for (int32_t py = y0; py < y1; py++) {
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)py * s->pitch);
        for (int32_t px = x0; px < x1; px++) {
            uint32_t dst = row[px];
            uint32_t r = (sr + ((dst >> 16) & 0xFFu) * inv) >> 8;
            uint32_t g = (sg + ((dst >> 8) & 0xFFu) * inv) >> 8;
            uint32_t b = (sb + (dst & 0xFFu) * inv) >> 8;
            row[px] = (r << 16) | (g << 8) | b;
        }
    }
}

void gfx_gradient_v(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t top_rgb, uint32_t bot_rgb) {
    if (!s || !s->pixels || w <= 0 || h <= 0) return;
    if (!tf_rect(&x, &y, &w, &h)) return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > s->width ? s->width : x + w;
    int32_t y1 = y + h > s->height ? s->height : y + h;
    uint32_t den = (uint32_t)(h > 1 ? h - 1 : 1);
    for (int32_t py = y0; py < y1; py++) {
        uint32_t c = lerp_rgb(top_rgb, bot_rgb, (uint32_t)(py - y), den);
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)py * s->pitch);
        for (int32_t px = x0; px < x1; px++) row[px] = c;
    }
}

static int inside_round_rect(int32_t px, int32_t py, int32_t x, int32_t y,
                             int32_t w, int32_t h, int32_t r) {
    int32_t cx = px < x + r ? x + r : (px >= x + w - r ? x + w - r - 1 : px);
    int32_t cy = py < y + r ? y + r : (py >= y + h - r ? y + h - r - 1 : py);
    int32_t dx = px - cx;
    int32_t dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void gfx_fill_round_rect(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t r, uint32_t rgb, uint8_t alpha) {
    if (!s || w <= 0 || h <= 0 || alpha == 0u) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { gfx_fill_rect_alpha(s, x, y, w, h, rgb, alpha); return; }
    /* Fast interior solid-alpha spans (no corner math needed for 95% of pixels) */
    gfx_fill_rect_alpha(s, x + r, y, w - 2 * r, r, rgb, alpha);
    gfx_fill_rect_alpha(s, x, y + r, w, h - 2 * r, rgb, alpha);
    gfx_fill_rect_alpha(s, x + r, y + h - r, w - 2 * r, r, rgb, alpha);
    int32_t r2 = r * r, r_outer = (r + 1) * (r + 1), r_span = r_outer - r2;
    int32_t corners[4][4] = {
        {x,         y,         x + r,     y + r},
        {x + w - r, y,         x + w - r - 1, y + r},
        {x,         y + h - r, x + r,     y + h - r - 1},
        {x + w - r, y + h - r, x + w - r - 1, y + h - r - 1}
    };
    for (int c = 0; c < 4; c++) {
        int32_t bx = corners[c][0], by = corners[c][1], cx = corners[c][2], cy = corners[c][3];
        for (int32_t dy = 0; dy < r; dy++) {
            int32_t py = by + dy;
            for (int32_t dx = 0; dx < r; dx++) {
                int32_t px = bx + dx;
                int32_t d2 = (px - cx) * (px - cx) + (py - cy) * (py - cy);
                if (d2 <= r2) gfx_blend_pixel(s, px, py, rgb, alpha);
                else if (d2 < r_outer) {
                    uint32_t cov = (uint32_t)((r_outer - d2) * 255) / (uint32_t)r_span;
                    gfx_blend_pixel(s, px, py, rgb, (uint8_t)((alpha * cov) >> 8));
                }
            }
        }
    }
}

void gfx_stroke_round_rect(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                           int32_t r, uint32_t rgb, uint8_t alpha) {
    if (!s || w <= 2 || h <= 2) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > s->width ? s->width : x + w;
    int32_t y1 = y + h > s->height ? s->height : y + h;
    for (int32_t py = y0; py < y1; py++) {
        for (int32_t px = x0; px < x1; px++) {
            if (inside_round_rect(px, py, x, y, w, h, r) &&
                !inside_round_rect(px, py, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0)) {
                gfx_blend_pixel(s, px, py, rgb, alpha);
            }
        }
    }
}

static void blur_backdrop(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!s || !s->pixels || w < 4 || h < 4) return;
    int32_t x0 = x < 1 ? 1 : x, y0 = y < 1 ? 1 : y;
    int32_t x1 = x + w >= s->width - 1 ? s->width - 2 : x + w;
    int32_t y1 = y + h >= s->height - 1 ? s->height - 2 : y + h;
    uint32_t stride = s->pitch / 4u;
    for (int32_t py = y0; py < y1; py++) {
        uint32_t *row = s->pixels + (size_t)py * stride;
        for (int32_t px = x0; px < x1; px++) {
            uint32_t c0 = row[px - 1], c1 = row[px], c2 = row[px + 1];
            uint32_t c3 = (row - stride)[px], c4 = (row + stride)[px];
            uint32_t rr = (((c0 >> 16) & 0xFFu) + ((c1 >> 16) & 0xFFu) + ((c2 >> 16) & 0xFFu) + ((c3 >> 16) & 0xFFu) + ((c4 >> 16) & 0xFFu)) / 5u;
            uint32_t rg = (((c0 >> 8) & 0xFFu) + ((c1 >> 8) & 0xFFu) + ((c2 >> 8) & 0xFFu) + ((c3 >> 8) & 0xFFu) + ((c4 >> 8) & 0xFFu)) / 5u;
            uint32_t rb = ((c0 & 0xFFu) + (c1 & 0xFFu) + (c2 & 0xFFu) + (c3 & 0xFFu) + (c4 & 0xFFu)) / 5u;
            row[px] = (rr << 16) | (rg << 8) | rb;
        }
    }
}

void gfx_glass_panel(struct ag_surface *s, int32_t x, int32_t y, int32_t w, int32_t h,
                     int32_t r, uint32_t tint_rgb, uint8_t alpha) {
    gfx_fill_round_rect(s, x + 3, y + 6, w, h, r, THEME_SHADOW, 96u);
    blur_backdrop(s, x + 2, y + 2, w - 4, h - 4);
    gfx_fill_round_rect(s, x, y, w, h, r, tint_rgb, alpha);
    gfx_stroke_round_rect(s, x, y, w, h, r, THEME_GLASS_BORDER, 140u);
}

static void fill_circle(struct ag_surface *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r)
                gfx_blend_pixel(s, cx + dx, cy + dy, rgb, 255u);
        }
    }
}

void gfx_traffic_lights(struct ag_surface *s, int32_t x, int32_t y) {
    fill_circle(s, x + 6,  y + 6, 5, THEME_TRAFFIC_RED);
    fill_circle(s, x + 22, y + 6, 5, THEME_TRAFFIC_YEL);
    fill_circle(s, x + 38, y + 6, 5, THEME_TRAFFIC_GRN);
}

void gfx_draw_char_scaled(struct ag_surface *s, int32_t x, int32_t y, char ch,
                          uint32_t rgb, int32_t scale) {
    if (!s || scale <= 0) return;
    unsigned char uc = (unsigned char)ch;
    if (uc < 32 || uc > 126) uc = '?';
    const uint8_t *glyph = font5x7_glyph(uc);
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1u << (4 - col))) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        gfx_blend_pixel(s, x + col * scale + sx, y + row * scale + sy, rgb, 255u);
                    }
                }
            }
        }
    }
}

void gfx_draw_text_scaled(struct ag_surface *s, int32_t x, int32_t y, const char *text,
                          uint32_t rgb, int32_t scale) {
    if (!s || !text || scale <= 0) return;
    int32_t cx = x;
    while (*text) {
        gfx_draw_char_scaled(s, cx, y, *text, rgb, scale);
        cx += 6 * scale;
        text++;
    }
}

int32_t gfx_text_width(const char *text, int32_t scale) {
    int32_t n = 0;
    while (text && text[n]) n++;
    return n > 0 ? (n * 6 - 1) * scale : 0;
}

void gfx_pill_badge(struct ag_surface *s, int32_t x, int32_t y, const char *text,
                    uint32_t bg_rgb, uint32_t fg_rgb, int32_t scale) {
    int32_t tw = gfx_text_width(text, scale);
    int32_t pw = tw + 12 * scale;
    int32_t ph = 7 * scale + 6 * scale;
    gfx_fill_round_rect(s, x, y, pw, ph, ph / 2, bg_rgb, 230u);
    gfx_draw_text_scaled(s, x + 6 * scale, y + 3 * scale, text, fg_rgb, scale);
}

void gfx_app_icon(struct ag_surface *s, int32_t x, int32_t y, int32_t size,
                  uint32_t top_rgb, uint32_t bot_rgb, const char *glyph) {
    int32_t r = size / 4;
    gfx_fill_round_rect(s, x + 2, y + 3, size, size, r, THEME_SHADOW, 80u);
    for (int32_t py = 0; py < size; py++) {
        uint32_t c = lerp_rgb(top_rgb, bot_rgb, (uint32_t)py, (uint32_t)(size > 1 ? size - 1 : 1));
        for (int32_t px = 0; px < size; px++) {
            if (inside_round_rect(x + px, y + py, x, y, size, size, r))
                gfx_blend_pixel(s, x + px, y + py, c, 255u);
        }
    }
    gfx_stroke_round_rect(s, x, y, size, size, r, 0x00FFFFFFu, 75u);
    if (glyph && glyph[0]) {
        int32_t sc = size >= 56 ? 3 : 2;
        int32_t tw = gfx_text_width(glyph, sc);
        int32_t th = 7 * sc;
        gfx_draw_text_scaled(s, x + (size - tw) / 2, y + (size - th) / 2, glyph, THEME_TEXT_PRIMARY, sc);
    }
}

int32_t gfx_cursor_scale(int32_t panel_w, int32_t panel_h) {
    int step = ag_scale_step_for(panel_w, panel_h, 1280, 720);
    return step > 0 ? step : 1;
}

void gfx_draw_cursor(struct ag_surface *s, int32_t x, int32_t y) {
    static const char *rows[16] = {
        "W...........",
        "WW..........",
        "WBW.........",
        "WBBW........",
        "WBBBW.......",
        "WBBBBW......",
        "WBBBBBW.....",
        "WBBBBBBW....",
        "WBBBBBBBW...",
        "WBBBBBBBBW..",
        "WBBBBBWWWWW.",
        "WBBWBBW.....",
        "WBW.WBBW....",
        "WW..WBBW....",
        "W....WW.....",
        "............"
    };
    int32_t scale = gfx_cursor_scale(s->width, s->height);
    for (int ry = 0; ry < 16; ry++) {
        for (int rx = 0; rx < 12; rx++) {
            char ch = rows[ry][rx];
            uint32_t color;
            if (ch == 'W') color = 0x00FFFFFFu;
            else if (ch == 'B') color = 0x00101218u;
            else continue;
            ag_fill_rect(0, s, x + rx * scale, y + ry * scale,
                         scale, scale, color);
        }
    }
}

void gfx_draw_wifi_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, uint32_t rgb) {
    for (int b = 0; b < 4; b++) {
        int32_t bh = 3 + b * 3;
        int32_t bx = x + b * 5;
        int32_t by = y + (12 - bh);
        uint32_t c = (b < bars) ? rgb : THEME_GLASS_BORDER;
        gfx_fill_round_rect(s, bx, by, 3, bh, 1, c, 240u);
    }
}

void gfx_draw_eth_icon(struct ag_surface *s, int32_t x, int32_t y, uint32_t rgb) {
    gfx_fill_round_rect(s, x, y + 2, 16, 11, 2, rgb, 235u);
    gfx_fill_round_rect(s, x + 3, y + 4, 10, 6, 1, THEME_GLASS_DARK, 255u);
    gfx_fill_rect_alpha(s, x + 5, y + 13, 6, 2, rgb, 240u);
}

void gfx_draw_battery_icon(struct ag_surface *s, int32_t x, int32_t y, int pct, int ac, uint32_t rgb) {
    gfx_stroke_round_rect(s, x, y + 2, 24, 12, 3, rgb, 230u);
    gfx_fill_rect_alpha(s, x + 24, y + 5, 2, 6, rgb, 230u);
    int32_t fw = (pct * 18) / 100;
    if (fw < 2) fw = 2;
    if (fw > 18) fw = 18;
    uint32_t fc = ac ? THEME_ACCENT_GREEN : (pct < 20 ? THEME_ACCENT_PINK : rgb);
    gfx_fill_round_rect(s, x + 3, y + 5, fw, 6, 1, fc, 245u);
}

void gfx_draw_bt_icon(struct ag_surface *s, int32_t x, int32_t y, int active, int connected, uint32_t rgb) {
    uint32_t col = active ? rgb : THEME_GLASS_BORDER;
    uint8_t a = active ? 245u : 120u;
    /* Vertical spine */
    gfx_fill_rect_alpha(s, x + 5, y + 1, 2, 13, col, a);
    /* Upper & lower rune triangles (anti-aliased diagonals) */
    for (int i = 0; i < 4; i++) {
        gfx_blend_pixel(s, x + 6 + i, y + 1 + i, col, a);
        gfx_blend_pixel(s, x + 6 + i, y + 7 - i, col, a);
        gfx_blend_pixel(s, x + 6 + i, y + 7 + i, col, a);
        gfx_blend_pixel(s, x + 6 + i, y + 13 - i, col, a);
        gfx_blend_pixel(s, x + 2 + i, y + 4 + i, col, (uint8_t)(a * 3u / 4u));
        gfx_blend_pixel(s, x + 2 + i, y + 10 - i, col, (uint8_t)(a * 3u / 4u));
    }
    if (connected) {
        gfx_fill_round_rect(s, x + 11, y + 5, 3, 4, 1, THEME_ACCENT_GREEN, 250u);
    }
}

void gfx_draw_chevron_right(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb) {
    int32_t half = size / 2;
    for (int32_t i = 0; i <= half; i++) {
        gfx_blend_pixel(s, x + i, y + i, rgb, 240u);
        gfx_blend_pixel(s, x + i + 1, y + i, rgb, 150u);
        gfx_blend_pixel(s, x + i, y + size - i, rgb, 240u);
        gfx_blend_pixel(s, x + i + 1, y + size - i, rgb, 150u);
    }
}

void gfx_draw_chevron_left(struct ag_surface *s, int32_t x, int32_t y, int32_t size, uint32_t rgb) {
    int32_t half = size / 2;
    for (int32_t i = 0; i <= half; i++) {
        gfx_blend_pixel(s, x + half - i, y + i, rgb, 240u);
        gfx_blend_pixel(s, x + half - i - 1, y + i, rgb, 150u);
        gfx_blend_pixel(s, x + half - i, y + size - i, rgb, 240u);
        gfx_blend_pixel(s, x + half - i - 1, y + size - i, rgb, 150u);
    }
}

static int glyph_bit_at(const uint8_t *g, int col, int row) {
    if (!g || col < 0 || col >= 5 || row < 0 || row >= 7) return 0;
    return (g[row] & (1u << (4 - col))) ? 255 : 0;
}

void gfx_draw_text_aa(struct ag_surface *s, int32_t x, int32_t y, const char *text, uint32_t rgb, int32_t scale) {
    if (!text || scale < 1) return;
    if (scale == 1) {
        gfx_draw_text_scaled(s, x, y, text, rgb, 1);
        return;
    }
    /* Bilinear 4-tap coverage-filtered anti-aliased glyph rasterization */
    int32_t cx = x;
    int32_t gw = 5 * scale;
    int32_t gh = 7 * scale;
    for (int i = 0; text[i]; i++) {
        const uint8_t *g = font5x7_glyph(text[i]);
        if (g) {
            for (int32_t py = 0; py < gh; py++) {
                int32_t gy256 = ((py * 256 + 128) / scale) - 128;
                int32_t iy0 = (gy256 < 0) ? -1 : (gy256 / 256);
                int32_t ty = gy256 - (iy0 * 256);
                for (int32_t px = 0; px < gw; px++) {
                    int32_t gx256 = ((px * 256 + 128) / scale) - 128;
                    int32_t ix0 = (gx256 < 0) ? -1 : (gx256 / 256);
                    int32_t tx = gx256 - (ix0 * 256);
                    int b00 = glyph_bit_at(g, ix0, iy0);
                    int b10 = glyph_bit_at(g, ix0 + 1, iy0);
                    int b01 = glyph_bit_at(g, ix0, iy0 + 1);
                    int b11 = glyph_bit_at(g, ix0 + 1, iy0 + 1);
                    int top = (256 - tx) * b00 + tx * b10;
                    int bot = (256 - tx) * b01 + tx * b11;
                    int cov = ((256 - ty) * top + ty * bot) >> 16;
                    if (cov >= 210) cov = 255;
                    else if (cov < 28) cov = 0;
                    if (cov > 0) gfx_blend_pixel(s, cx + px, y + py, rgb, (uint8_t)cov);
                }
            }
        }
        cx += 6 * scale;
    }
}

void gfx_draw_bmp_scaled(struct ag_surface *s, const uint8_t *bmp, uint32_t len,
                         int32_t dx, int32_t dy, int32_t dw, int32_t dh) {
    struct bmp_info bi;
    if (!s || !s->pixels || !bmp || dw <= 0 || dh <= 0) return;
    if (bmp_parse(bmp, len, &bi) != BMP_OK) return;
    for (int32_t y = 0; y < dh; y++) {
        int32_t ty = dy + y;
        if (ty < 0 || ty >= s->height) continue;
        int sy = (int)((y * bi.height) / dh);
        uint32_t *row = (uint32_t *)((uint8_t *)s->pixels + (size_t)ty * s->pitch);
        for (int32_t x = 0; x < dw; x++) {
            int32_t tx = dx + x;
            if (tx < 0 || tx >= s->width) continue;
            int sx = (int)((x * bi.width) / dw);
            unsigned int argb = 0;
            if (bmp_pixel_argb(bmp, len, &bi, sx, sy, &argb) == BMP_OK) {
                uint8_t a = (uint8_t)(argb >> 24);
                if (a == 255u) {
                    row[tx] = argb & 0x00FFFFFFu;
                } else if (a > 0u) {
                    gfx_blend_pixel(s, tx, ty, argb & 0x00FFFFFFu, a);
                }
            }
        }
    }
}

void gfx_copy_fast(struct ag_surface *dst, const struct ag_surface *src) {
    if (!dst || !src || !dst->pixels || !src->pixels) return;
    int32_t h = dst->height < src->height ? dst->height : src->height;
    int32_t w = dst->width < src->width ? dst->width : src->width;
    for (int32_t y = 0; y < h; y++) {
        uint64_t *d = (uint64_t *)((uint8_t *)dst->pixels + (size_t)y * dst->pitch);
        const uint64_t *s0 = (const uint64_t *)((const uint8_t *)src->pixels + (size_t)y * src->pitch);
        int32_t n64 = w / 2;
        for (int32_t x = 0; x < n64; x++) d[x] = s0[x];
        if (w & 1) ((uint32_t *)d)[w - 1] = ((const uint32_t *)s0)[w - 1];
    }
}
