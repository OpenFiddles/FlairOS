#include "../../include/desktop_chrome.h"
#include "../../include/font5x7.h"

/* Window chrome: original FlairOS design.
   Layout contract shared with the input code in desktop.c (grid cells):
     close button  = cell column x+1, minimize = x+3, zoom = x+5, on the title row.
   The title bar is exactly one grid row tall so pointer hit tests match pixels. */

static int title_pixel_width(const char *title, int scale) {
    int n = 0;
    while (title && title[n] && n < 48) ++n;
    return n * 6 * scale;
}

static void draw_title_text(struct ag_device *device, struct ag_surface *surface,
                            int x, int y, const char *title, int scale,
                            uint32_t color) {
    for (int i = 0; title && title[i] && i < 48; ++i) {
        const uint8_t *glyph = font5x7_glyph(title[i]);
        for (int gy = 0; gy < 7; ++gy)
            for (int gx = 0; gx < 5; ++gx)
                if (glyph[gy] & (1u << (4 - gx)))
                    ag_fill_rect(device, surface, x + gx * scale, y + gy * scale,
                                 scale, scale, color);
        x += 6 * scale;
        if (x >= surface->width) break;
    }
}

/* Soft shadow: stacked translucent rings that fade outward. Only the area
   outside the window is shaded, so window contents are never darkened. */
static void blend_clip(struct ag_device *device, struct ag_surface *surface,
                       int x0, int y0, int x1, int y1, uint32_t color) {
    if (x1 > x0 && y1 > y0)
        ag_blend_rect(device, surface, x0, y0, x1 - x0, y1 - y0, color);
}

static void draw_shadow(struct ag_device *device, struct ag_surface *surface,
                        int px, int py, int pw, int ph) {
    static const uint32_t alpha[5] = {0x30000000u, 0x24000000u, 0x18000000u,
                                      0x0E000000u, 0x06000000u};
    int ix0 = px, iy0 = py, ix1 = px + pw, iy1 = py + ph;
    for (int i = 0; i < 5; ++i) {
        int grow = 2 + i * 2;
        int l = px - grow + 2, t = py - grow + 8;
        int r = px + pw + grow - 2, b = py + ph + grow + 6;
        int my0 = t > iy0 ? t : iy0, my1 = b < iy1 ? b : iy1;
        blend_clip(device, surface, l, t, r, iy0 < b ? iy0 : b, alpha[i]);           /* top */
        blend_clip(device, surface, l, iy1 > t ? iy1 : t, r, b, alpha[i]);           /* bottom */
        blend_clip(device, surface, l, my0, ix0 < r ? ix0 : r, my1, alpha[i]);       /* left */
        blend_clip(device, surface, ix1 > l ? ix1 : l, my0, r, my1, alpha[i]);       /* right */
    }
}

int desktop_chrome_draw(struct ag_device *device, struct ag_surface *surface,
                        int grid_columns, int grid_rows,
                        int x, int y, int width, int height,
                        const char *title, int active) {
    int px, py, right, bottom, pw, ph, cw, ch, title_height, scale, tw, tx;
    uint32_t bar, border;
    if (!surface || !surface->pixels || grid_columns <= 0 || grid_rows <= 0 ||
        width <= 0 || height <= 0 || x < 0 || y < 0 ||
        x + width > grid_columns || y + height > grid_rows)
        return -1;

    px = (x * surface->width) / grid_columns;
    py = (y * surface->height) / grid_rows;
    right = ((x + width) * surface->width) / grid_columns;
    bottom = ((y + height) * surface->height) / grid_rows;
    pw = right - px;
    ph = bottom - py;
    cw = surface->width / grid_columns;
    ch = surface->height / grid_rows;
    if (pw < 48 || ph < 32 || cw < 2 || ch < 4) return -1;

    title_height = ((y + 1) * surface->height) / grid_rows - py;
    bar = active ? 0x002B2930 : 0x00211F26;    /* M3 surfaceContainerHigh / surfaceContainer */
    border = active ? 0x00524E5A : 0x003B383E; /* M3 outline / outlineVariant */

    draw_shadow(device, surface, px, py, pw, ph);

    /* Material 3 rounded window header & tonal outline. */
    ag_round_rect(device, surface, px, py, pw, title_height + 6, 10, bar);
    ag_fill_rect(device, surface, px, py + title_height - 1, pw, 1,
                 active ? 0x003B383E : 0x002B2930);
    ag_round_frame(device, surface, px, py, pw, ph, 10, border);

    /* Traffic-light controls: close, minimize, zoom. */
    {
        int d = ch / 2; if (d < 2) d = 2;
        int cy = py + (title_height - d) / 2;
        static const uint32_t on[3] = {0x00F0605A, 0x00F2BE3C, 0x003CCB5B};
        for (int i = 0; i < 3; ++i) {
            int cx = px + cw * (1 + 2 * i) + (cw - d) / 2;
            ag_round_rect(device, surface, cx, cy, d, d, d / 2,
                          active ? on[i] : 0x004A5262);
        }
    }

    /* Centered title, shrunk to scale 1 when it would collide with the buttons. */
    scale = surface->width >= 800 ? 2 : 1;
    tw = title_pixel_width(title, scale);
    if (tw > pw - cw * 14) { scale = 1; tw = title_pixel_width(title, 1); }
    tx = px + (pw - tw) / 2;
    if (tx < px + cw * 7) tx = px + cw * 7;
    draw_title_text(device, surface, tx, py + (title_height - 7 * scale) / 2,
                    title, scale, active ? 0x00E6E0E9 : 0x00938F99);
    return 0;
}

int desktop_chrome_draw_scaled(struct ag_device *device, struct ag_surface *surface,
                               int x, int y, int width, int height,
                               const char *title, int active,
                               int title_h, int inset, int button_w, int button_gap) {
    uint32_t body, header, border;
    int radius, border_t, d, bx, scale, tw, tx, ty;
    if (!surface || !surface->pixels || width <= 0 || height <= 0 ||
        x < 0 || y < 0)
        return -1;
    if (title_h <= 0) title_h = 1;
    if (inset <= 0) inset = 1;
    if (button_w <= 0) button_w = 1;
    if (button_gap < 0) button_gap = 0;
    if (title_h >= height) title_h = height - 1;
    if (title_h < 1) title_h = 1;
    radius = title_h / 3;
    if (radius < 2) radius = 2;
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    if (radius < 1) radius = 1;
    border_t = inset / 2;
    if (border_t < 1) border_t = 1;
    if (border_t * 2 >= width || border_t * 2 >= height) border_t = 1;
    body = active ? 0x002B2930 : 0x00211F26;
    header = active ? 0x0034323A : 0x0026242B;
    border = active ? 0x00524E5A : 0x003B383E;

    ag_round_rect(device, surface, x, y, width, height, radius, border);
    ag_round_rect(device, surface, x + border_t, y + border_t,
                  width - border_t * 2, height - border_t * 2,
                  radius - border_t > 0 ? radius - border_t : 1, body);
    ag_fill_rect(device, surface, x + border_t, y + border_t,
                 width - border_t * 2, title_h - border_t, header);

    d = button_w;
    if (d > title_h - 2) d = title_h - 2;
    if (d < 1) d = 1;
    bx = x + inset;
    for (int i = 0; i < 3; ++i) {
        static const uint32_t on[3] = {0x00F0605A, 0x00F2BE3C, 0x003CCB5B};
        int cx = bx + d / 2;
        if (cx + d / 2 >= x + width - border_t) break;
        ag_round_rect(device, surface, cx - d / 2, y + (title_h - d) / 2, d, d,
                      d / 2, active ? on[i] : 0x004A5262);
        bx += d + button_gap;
    }

    scale = title_h / 12;
    if (scale < 1) scale = 1;
    tw = title_pixel_width(title, scale);
    tx = x + (width - tw) / 2;
    if (tx < bx) tx = bx;
    if (tx + tw > x + width - border_t) tx = x + width - border_t - tw;
    if (tx < x + border_t) tx = x + border_t;
    ty = y + (title_h - 7 * scale) / 2;
    if (ty < y) ty = y;
    draw_title_text(device, surface, tx, ty, title, scale,
                    active ? 0x00E6E0E9 : 0x00938F99);

    /* Resize grip: a few diagonal strokes in the bottom-right corner. */
    {
        int g = title_h / 2;
        if (g > width / 3) g = width / 3;
        if (g > height / 3) g = height / 3;
        for (int i = 1; i <= 3; ++i) {
            int len = g / 4 * i;
            ag_fill_rect(device, surface, x + width - border_t - len,
                         y + height - border_t - 1, len, 1, border);
            ag_fill_rect(device, surface, x + width - border_t - 1,
                         y + height - border_t - len, 1, len, border);
        }
    }
    return 0;
}
