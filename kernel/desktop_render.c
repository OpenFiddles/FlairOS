#include "../include/desktop_render.h"
#include "../include/font5x7.h"
#include "../include/bmp.h"

static void draw_wallpaper(struct ag_surface *surface,
                           const struct bmp_info *info,
                           const unsigned char *data, unsigned int length) {
    int crop_x = 0, crop_y = 0;
    int visible_w = info->width;
    int visible_h = info->height;
    int width = surface->width;
    int height = surface->height;

    if (info->width * height > info->height * width) {
        visible_w = info->height * width / height;
        crop_x = (info->width - visible_w) / 2;
    } else {
        visible_h = info->width * height / width;
        crop_y = (info->height - visible_h) / 2;
    }

    for (int y = 0; y < height; ++y) {
        int sy = crop_y + y * visible_h / height;
        for (int x = 0; x < width; ++x) {
            int sx = crop_x + x * visible_w / width;
            unsigned int rgb;
            if (bmp_pixel(data, length, info,
                          sx, sy, &rgb) == BMP_OK)
                ag_put_pixel(surface, x, y, rgb);
        }
    }
}

/* Original default wallpaper: a dusk gradient with three layers of dunes.
   Pure integer math, so every host and the kernel render identical pixels. */
extern void *kmalloc(unsigned int size) __attribute__((weak));
extern void kfree(void *p) __attribute__((weak));

static uint32_t *wall_cache;
static int wall_cache_w, wall_cache_h;

static uint32_t lerp_rgb(uint32_t a, uint32_t b, int t, int span) {
    int r = (int)((a >> 16) & 255), g = (int)((a >> 8) & 255), bl = (int)(a & 255);
    int r2 = (int)((b >> 16) & 255), g2 = (int)((b >> 8) & 255), b2 = (int)(b & 255);
    r += (r2 - r) * t / span; g += (g2 - g) * t / span; bl += (b2 - bl) * t / span;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static uint32_t dusk_color(int y, int h) {
    static const uint32_t stop_color[5] = {0x141A44, 0x3B2A78, 0x9A3F8F, 0xE0706F, 0xF6B26B};
    static const int stop_pos[5] = {0, 380, 640, 840, 1024};
    int t = h > 1 ? y * 1024 / (h - 1) : 0;
    for (int i = 0; i < 4; ++i)
        if (t <= stop_pos[i + 1])
            return lerp_rgb(stop_color[i], stop_color[i + 1],
                            t - stop_pos[i], stop_pos[i + 1] - stop_pos[i]);
    return stop_color[4];
}

static int tri_wave(int x, int period) {
    int m = x % period, half = period / 2;
    if (m < 0) m += period;
    return m < half ? m : period - m;
}

static int dune_ridge(int x, int w, int h, int base_pct, int amp_pct, int phase, int slow) {
    int p1 = w * slow / 4 + 2;
    int p2 = w / 3 + 2;
    int a1 = h * amp_pct / 100, a2 = a1 / 3;
    int y = h * base_pct / 100;
    y -= tri_wave(x + phase, p1) * a1 / (p1 / 2);
    y -= tri_wave(x * 2 + phase, p2) * a2 / (p2 / 2);
    return y;
}

static void paint_default_wallpaper(struct ag_device *device, struct ag_surface *surface) {
    static const uint32_t dune_color[3] = {0x70331A5Cu, 0xA0261047u, 0xD01A0B33u};
    static const int dune_base[3] = {70, 78, 88};
    static const int dune_amp[3] = {9, 7, 5};
    static const int dune_phase[3] = {40, 210, 90};
    static const int dune_slow[3] = {6, 5, 4};
    int w = surface->width, h = surface->height;
    for (int y = 0; y < h; ++y)
        ag_fill_rect(device, surface, 0, y, w, 1, dusk_color(y, h));
    for (int layer = 0; layer < 3; ++layer)
        for (int x = 0; x < w; ++x) {
            int top = dune_ridge(x, w, h, dune_base[layer], dune_amp[layer],
                                 dune_phase[layer], dune_slow[layer]);
            if (top < 0) top = 0;
            if (top < h)
                ag_blend_rect(device, surface, x, top, 1, h - top, dune_color[layer]);
        }
}

static void draw_default_wallpaper(struct ag_device *device, struct ag_surface *surface) {
    int w = surface->width, h = surface->height;
    if (!wall_cache && kmalloc && (uint32_t)w * (uint32_t)h <= 1024u * 768u) {
        wall_cache = (uint32_t *)kmalloc((unsigned int)(w * h * 4));
        if (wall_cache) {
            struct ag_surface tmp;
            if (ag_surface_init(&tmp, wall_cache, w, h, (uint32_t)w * 4u, AG_PIXEL_XRGB8888) == 0) {
                paint_default_wallpaper(0, &tmp);
                wall_cache_w = w; wall_cache_h = h;
            } else { kfree(wall_cache); wall_cache = 0; }
        }
    }
    if (wall_cache && wall_cache_w == w && wall_cache_h == h) {
        for (int y = 0; y < h; ++y) {
            uint32_t *dst = (uint32_t *)((uint8_t *)surface->pixels + (uint32_t)y * surface->pitch);
            const uint32_t *src = wall_cache + y * w;
            for (int x = 0; x < w; ++x) dst[x] = src[x];
        }
        return;
    }
    paint_default_wallpaper(device, surface);
}

/* VGA attributes remain the desktop scene format, but are translated
   into a calmer modern palette at the pixel-rendering boundary. */
static const uint32_t desktop_backgrounds[16] = {
    0x0F172A, 0x243B6B, 0x14532D, 0x134E4A,
    0x7F1D1D, 0x581C87, 0x78350F, 0x1E293B,
    0x273449, 0x1D4ED8, 0x15803D, 0x0F766E,
    0xB91C1C, 0x7E22CE, 0xB45309, 0xF8FAFC
};
static const uint32_t desktop_foregrounds[16] = {
    0xE2E8F0, 0xBFDBFE, 0xBBF7D0, 0x99F6E4,
    0xFECACA, 0xE9D5FF, 0xFED7AA, 0xCBD5E1,
    0xE2E8F0, 0x60A5FA, 0x4ADE80, 0x5EEAD4,
    0xFCA5A5, 0xD8B4FE, 0xFCD34D, 0xFFFFFF
};

int desktop_render_cells_with_wallpaper(struct ag_device *device, struct ag_surface *surface,
                                        const uint16_t *cells, int columns, int rows,
                                        const unsigned char *wallpaper_data,
                                        unsigned int wallpaper_length) {
    if (!surface || !surface->pixels || !cells ||
        columns <= 0 || rows <= 0 ||
        columns > surface->width || rows > surface->height)
        return -1;

    struct bmp_info active_wallpaper;
    int use_wallpaper = wallpaper_data && wallpaper_length &&
        bmp_parse(wallpaper_data, wallpaper_length, &active_wallpaper) == BMP_OK;
    int cell_width = surface->width / columns;
    int cell_height = surface->height / rows;
    if (cell_width < 5 || cell_height < 7)
        return -1;

    if (use_wallpaper)
        draw_wallpaper(surface, &active_wallpaper, wallpaper_data, wallpaper_length);
    else
        draw_default_wallpaper(device, surface);

    for (int row = 0; row < rows; ++row) {
        int y = (row * surface->height) / rows;
        int y_end = ((row + 1) * surface->height) / rows;

        for (int column = 0; column < columns; ++column) {
            uint16_t entry = cells[row * columns + column];
            unsigned char character = (unsigned char)(entry & 0xFFu);
            unsigned char attribute = (unsigned char)(entry >> 8);
            unsigned char foreground_index = attribute & 0x0Fu;
            unsigned char background_index = (attribute >> 4) & 0x0Fu;
            uint32_t foreground = desktop_foregrounds[foreground_index];
            uint32_t background = desktop_backgrounds[background_index];
            int x = (column * surface->width) / columns;
            int x_end = ((column + 1) * surface->width) / columns;
            int width = x_end - x;
            int height = y_end - y;

            if (character == 0xB0u) {
                /* The full-frame wallpaper has already been painted. */
            } else {
                ag_fill_rect(device, surface, x, y, width, height, background);
                /* Give title bars a fine highlight and lower edge for depth. */
                if (character != 0xB0u &&
                    (background_index == 1u || background_index == 8u)) {
                    uint32_t highlight = background_index == 1u ? 0x4965A4u : 0x40536Bu;
                    uint32_t edge = background_index == 1u ? 0x1A2A50u : 0x1B2635u;
                    ag_fill_rect(device, surface, x, y, width, 1, highlight);
                    if (height > 2)
                        ag_fill_rect(device, surface, x, y + height - 1,
                                     width, 1, edge);
                }
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

int desktop_render_cells(struct ag_device *device, struct ag_surface *surface,
                         const uint16_t *cells, int columns, int rows) {
    return desktop_render_cells_with_wallpaper(device, surface, cells,
                                               columns, rows, 0, 0);
}
