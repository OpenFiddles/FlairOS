#include "../include/desktop_chrome.h"
#include "../include/font5x7.h"

static void draw_title_text(struct ag_device *device, struct ag_surface *surface,
                            int x, int y, int max_width, const char *title,
                            uint32_t color) {
    int scale = surface->width >= 800 ? 2 : 1;
    int text_width = 0;
    if (!title) return;
    for (int i = 0; title[i] && i < 48; ++i)
        text_width += 6 * scale;
    if (text_width > max_width) scale = 1;
    for (int i = 0; title[i] && i < 48; ++i) {
        const uint8_t *glyph = font5x7_glyph(title[i]);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (glyph[gy] & (1u << (4 - gx)))
                    ag_fill_rect(device, surface,
                                 x + gx * scale, y + gy * scale,
                                 scale, scale, color);
            }
        }
        x += 6 * scale;
        if (x >= surface->width) break;
    }
}

int desktop_chrome_draw(struct ag_device *device, struct ag_surface *surface,
                        int grid_columns, int grid_rows,
                        int x, int y, int width, int height,
                        const char *title, int active) {
    int px, py, right, bottom, pw, ph, title_height;
    uint32_t title_color, border_color;
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
    if (pw < 48 || ph < 32) return -1;

    title_height = ph / 12;
    if (title_height < 22) title_height = 22;
    if (title_height > 32) title_height = 32;
    title_color = active ? 0x00243B6B : 0x002B3A52;
    border_color = active ? 0x006B8AC7 : 0x00465770;

    /* Soft drop shadow is kept outside the content area. */
    ag_blend_rect(device, surface, px + 4, bottom - 1, pw - 2, 4, 0x50000000);
    ag_blend_rect(device, surface, right - 1, py + 4, 4, ph - 2, 0x50000000);

    /* The existing cell renderer owns window contents; this layer owns chrome. */
    ag_round_rect(device, surface, px, py, pw, title_height, 9, title_color);
    if (title_height > 9)
        ag_fill_rect(device, surface, px + 9, py + title_height / 2,
                     pw - 18, title_height - title_height / 2, title_color);
    ag_round_frame(device, surface, px, py, pw, ph, 10, border_color);

    /* Three compact traffic-light controls. */
    {
        int cy = py + (title_height - 9) / 2;
        int cx = right - 14;
        ag_round_rect(device, surface, cx, cy, 8, 8, 4, 0x00F87171);
        ag_round_rect(device, surface, cx - 14, cy, 8, 8, 4, 0x00FBBF24);
        ag_round_rect(device, surface, cx - 28, cy, 8, 8, 4, 0x0034D399);
    }

    draw_title_text(device, surface, px + 12,
                    py + (title_height - 14) / 2,
                    pw - 58, title, 0x00F1F5F9);
    return 0;
}
