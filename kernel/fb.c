#include "../include/fb.h"

static uint32_t *row(const struct fb *f, int32_t y) {
    return (uint32_t *)(f->base + (uint32_t)y * f->pitch);
}

static int clip(const struct fb *f, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
    if (*w <= 0 || *h <= 0)
        return 0;
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > f->width)
        *w = f->width - *x;
    if (*y + *h > f->height)
        *h = f->height - *y;
    return *w > 0 && *h > 0;
}

void fb_init(struct fb *f, void *base, int32_t width, int32_t height, uint32_t pitch) {
    f->base = (uint8_t *)base;
    f->width = width;
    f->height = height;
    f->pitch = pitch;
}

void fb_put(const struct fb *f, int32_t x, int32_t y, uint32_t color) {
    if (x < 0 || y < 0 || x >= f->width || y >= f->height)
        return;
    row(f, y)[x] = color;
}

void fb_fill_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!clip(f, &x, &y, &w, &h))
        return;
    for (int32_t r = 0; r < h; r++) {
        uint32_t *p = row(f, y + r) + x;
        for (int32_t c = 0; c < w; c++)
            p[c] = color;
    }
}

void fb_clear(const struct fb *f, uint32_t color) {
    fb_fill_rect(f, 0, 0, f->width, f->height, color);
}

void fb_frame_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0)
        return;
    fb_fill_rect(f, x, y, w, 1, color);
    fb_fill_rect(f, x, y + h - 1, w, 1, color);
    fb_fill_rect(f, x, y, 1, h, color);
    fb_fill_rect(f, x + w - 1, y, 1, h, color);
}

void fb_blit32(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *src) {
    if (w <= 0 || h <= 0)
        return;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    int32_t x1 = x + w > f->width ? f->width : x + w;
    int32_t y1 = y + h > f->height ? f->height : y + h;
    for (int32_t dy = y0; dy < y1; dy++) {
        const uint32_t *s = src + (dy - y) * w;
        uint32_t *d = row(f, dy);
        for (int32_t dx = x0; dx < x1; dx++)
            d[dx] = s[dx - x];
    }
}
