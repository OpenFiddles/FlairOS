#include "../../include/cursor.h"
#include "../../include/bmp.h"
#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
#else
#include "../../include/kheap.h"
#endif

/* '.' transparent, '#' outline, 'o' fill. All artwork below is original. */
static const char *const arrow_art[] = {
    "#...........",
    "##..........",
    "#o#.........",
    "#oo#........",
    "#ooo#.......",
    "#oooo#......",
    "#ooooo#.....",
    "#oooooo#....",
    "#ooooooo#...",
    "#oooooooo#..",
    "#ooooooooo#.",
    "#oooooo#####",
    "#oo#oo#.....",
    "#o#.#oo#....",
    "##..#oo#....",
    "#....#oo#...",
    ".....#oo#...",
    "......##....",
};

static const char *const hand_art[] = {
    "......##........",
    ".....#oo#.......",
    ".....#oo#.......",
    ".....#oo#.......",
    ".....#oo#.......",
    ".....#oo#.##....",
    ".....#oo##oo###.",
    ".....#ooo#oo#oo#",
    ".##..#oooooooo#.",
    "#oo#.#oooooooo#.",
    "#ooo##oooooooo#.",
    ".#oooooooooooo#.",
    "..#oooooooooo#..",
    "..#oooooooooo#..",
    "...#oooooooo#...",
    "...#oooooooo#...",
    "....#oooooo#....",
    "....########....",
};

static const char *const ibeam_art[] = {
    "#######",
    "#ooooo#",
    "###o###",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "..#o#..",
    "###o###",
    "#ooooo#",
    "#######",
};

struct sprite_info {
    const char *const *rows;
    int width, height, hot_x, hot_y;
};

static const struct sprite_info sprites[CURSOR_SHAPE_COUNT] = {
    { arrow_art, 12, 18, 0, 0 },
    { hand_art, 16, 18, 6, 0 },
    { ibeam_art, 7, 16, 3, 8 },
};

#define COLOR_OUTLINE 0x00101018u
#define COLOR_FILL 0x00FFFFFFu

static enum cursor_shape current_shape = CURSOR_ARROW;
static int custom_loaded;
static int custom_w, custom_h;
static uint32_t *custom_pixels; /* Heap-backed; 0xFFFFFFFF = transparent. */

static uint32_t *cursor_pixels_alloc(void) {
#ifdef FLAIROS_HOST_TEST
    return (uint32_t *)malloc(CURSOR_BMP_MAX * CURSOR_BMP_MAX * sizeof(uint32_t));
#else
    return (uint32_t *)kmalloc(CURSOR_BMP_MAX * CURSOR_BMP_MAX * sizeof(uint32_t));
#endif
}

void cursor_set_shape(enum cursor_shape shape) {
    current_shape = (shape >= 0 && shape < CURSOR_SHAPE_COUNT) ? shape : CURSOR_ARROW;
}

enum cursor_shape cursor_get_shape(void) { return current_shape; }

void cursor_reset_custom(void) { custom_loaded = 0; custom_w = custom_h = 0; }

int cursor_load_bmp(const unsigned char *data, unsigned int length) {
    struct bmp_info info;
    int w, h;
    cursor_reset_custom();
    if (!data || bmp_parse(data, length, &info) != BMP_OK) return 0;
    w = (int)info.width; h = (int)info.height;
    if (w < 1 || h < 1 || w > CURSOR_BMP_MAX || h > CURSOR_BMP_MAX) return 0;
    if (!custom_pixels) custom_pixels = cursor_pixels_alloc();
    if (!custom_pixels) return 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned int rgb = 0;
            if (bmp_pixel(data, length, &info, x, y, &rgb) != BMP_OK) return 0;
            custom_pixels[y * CURSOR_BMP_MAX + x] =
                (rgb & 0x00FFFFFFu) == CURSOR_BMP_KEY ? 0xFFFFFFFFu : (rgb & 0x00FFFFFFu);
        }
    custom_w = w; custom_h = h; custom_loaded = 1;
    return 1;
}

void cursor_metrics(enum cursor_shape shape, int *width, int *height, int *hot_x, int *hot_y) {
    const struct sprite_info *s;
    if (shape < 0 || shape >= CURSOR_SHAPE_COUNT) shape = CURSOR_ARROW;
    s = &sprites[shape];
    if (width) *width = s->width;
    if (height) *height = s->height;
    if (hot_x) *hot_x = s->hot_x;
    if (hot_y) *hot_y = s->hot_y;
}

int cursor_builtin_pixel(enum cursor_shape shape, int x, int y) {
    const struct sprite_info *s;
    char c;
    if (shape < 0 || shape >= CURSOR_SHAPE_COUNT) return -1;
    s = &sprites[shape];
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) return -1;
    c = s->rows[y][x];
    return c == '#' ? 1 : (c == 'o' ? 2 : 0);
}

static void put(struct ag_device *d, struct ag_surface *surf, int x, int y, int scale, uint32_t color) {
    ag_fill_rect(d, surf, x, y, scale, scale, color);
}

void cursor_draw(struct ag_device *device, struct ag_surface *surface, int x, int y) {
    int scale, hot_x, hot_y, w, h;
    if (!surface || !surface->pixels) return;
    scale = ag_scale_step_for(surface->width, surface->height, 1280, 720);
    if (scale < 1) scale = 1;

    if (custom_loaded && current_shape == CURSOR_ARROW) {
        w = custom_w; h = custom_h; hot_x = 0; hot_y = 0;
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx)
                if (custom_pixels[sy * CURSOR_BMP_MAX + sx] != 0xFFFFFFFFu)
                    ag_blend_rect(device, surface, x + (sx - hot_x + 1) * scale,
                                  y + (sy - hot_y + 1) * scale, scale, scale, 0x50000000u);
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx) {
                uint32_t c = custom_pixels[sy * CURSOR_BMP_MAX + sx];
                if (c != 0xFFFFFFFFu)
                    put(device, surface, x + (sx - hot_x) * scale, y + (sy - hot_y) * scale, scale, c);
            }
        return;
    }

    {
        const struct sprite_info *s = &sprites[current_shape];
        w = s->width; h = s->height; hot_x = s->hot_x; hot_y = s->hot_y;
        for (int sy = 0; sy < h; ++sy)          /* soft drop shadow */
            for (int sx = 0; sx < w; ++sx)
                if (s->rows[sy][sx] != '.')
                    ag_blend_rect(device, surface, x + (sx - hot_x + 1) * scale,
                                  y + (sy - hot_y + 1) * scale, scale, scale, 0x50000000u);
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx) {
                char c = s->rows[sy][sx];
                if (c == '.') continue;
                put(device, surface, x + (sx - hot_x) * scale, y + (sy - hot_y) * scale, scale,
                    c == '#' ? COLOR_OUTLINE : COLOR_FILL);
            }
    }
}
