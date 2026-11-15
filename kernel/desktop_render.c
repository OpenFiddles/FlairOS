#include "../include/desktop_render.h"
#include "../include/font5x7.h"
#include "../include/bmp.h"

static const unsigned char *wallpaper_data;
static unsigned int wallpaper_length;
static struct bmp_info wallpaper_info;

int desktop_render_set_wallpaper(const unsigned char *data, unsigned int length) {
    struct bmp_info info;
    if (!data || bmp_parse(data, length, &info) != BMP_OK)
        return -1;
    wallpaper_data = data;
    wallpaper_length = length;
    wallpaper_info = info;
    return 0;
}

void desktop_render_clear_wallpaper(void) {
    wallpaper_data = 0;
    wallpaper_length = 0;
}

static void draw_wallpaper_cell(struct ag_surface *surface, int x, int y, int w, int h) {
    int crop_x = 0, crop_y = 0;
    int visible_w = wallpaper_info.width;
    int visible_h = wallpaper_info.height;
    if ((int64_t)wallpaper_info.width * h > (int64_t)wallpaper_info.height * w) {
        visible_w = (int)((int64_t)wallpaper_info.height * w / h);
        crop_x = (wallpaper_info.width - visible_w) / 2;
    } else {
        visible_h = (int)((int64_t)wallpaper_info.width * h / w);
        crop_y = (wallpaper_info.height - visible_h) / 2;
    }
    for (int py = 0; py < h; ++py) {
        int sy = crop_y + (int)((int64_t)py * visible_h / h);
        for (int px = 0; px < w; ++px) {
            int sx = crop_x + (int)((int64_t)px * visible_w / w);
            unsigned int rgb;
            if (bmp_pixel(wallpaper_data, wallpaper_length, &wallpaper_info,
                          sx, sy, &rgb) == BMP_OK)
                ag_put_pixel(surface, x + px, y + py, rgb);
        }
    }
}

static const uint32_t vga_colors[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

int desktop_render_cells(struct ag_device *device, struct ag_surface *surface,
                         const uint16_t *cells, int columns, int rows) {
    if (!surface || !surface->pixels || !cells ||
        columns <= 0 || rows <= 0 ||
        columns > surface->width || rows > surface->height)
        return -1;

    int cell_width = surface->width / columns;
    int cell_height = surface->height / rows;
    if (cell_width < 5 || cell_height < 7)
        return -1;

    for (int row = 0; row < rows; ++row) {
        int y = (row * surface->height) / rows;
        int y_end = ((row + 1) * surface->height) / rows;

        for (int column = 0; column < columns; ++column) {
            uint16_t entry = cells[row * columns + column];
            unsigned char character = (unsigned char)(entry & 0xFFu);
            unsigned char attribute = (unsigned char)(entry >> 8);
            uint32_t foreground = vga_colors[attribute & 0x0Fu];
            uint32_t background = vga_colors[(attribute >> 4) & 0x07u];
            int x = (column * surface->width) / columns;
            int x_end = ((column + 1) * surface->width) / columns;
            int width = x_end - x;
            int height = y_end - y;

            if (character == 0xB0u && wallpaper_data) {
                draw_wallpaper_cell(surface, x, y, width, height);
            } else {
                ag_fill_rect(device, surface, x, y, width, height, background);
            }

            if (character == 0xDBu) {
                ag_fill_rect(device, surface, x, y, width, height, foreground);
                continue;
            }
            if (character == 0xB0u || character == ' ')
                continue;

            const uint8_t *glyph = font5x7_glyph((char)character);
            int scale_x = width / 5;
            int scale_y = height / 7;
            int scale = scale_x < scale_y ? scale_x : scale_y;
            if (scale > 2)
                scale = 2;
            if (scale < 1)
                continue;

            int glyph_x = x + (width - 5 * scale) / 2;
            int glyph_y = y + (height - 7 * scale) / 2;
            for (int gy = 0; gy < 7; ++gy) {
                for (int gx = 0; gx < 5; ++gx) {
                    if (glyph[gy] & (1u << (4 - gx))) {
                        ag_fill_rect(device, surface,
                                     glyph_x + gx * scale,
                                     glyph_y + gy * scale,
                                     scale, scale, foreground);
                    }
                }
            }
        }
    }
    return 0;
}
