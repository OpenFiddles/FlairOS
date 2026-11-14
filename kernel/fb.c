#include "../include/fb.h"

static int fb_valid(const struct fb *f) {
    return f && f->base && f->width > 0 && f->height > 0 &&
           (uint32_t)f->width <= 0x3FFFFFFFu &&
           f->pitch >= (uint32_t)f->width * 4u &&
           (f->pitch & 3u) == 0;
}

static uint32_t *row(const struct fb *f, int32_t y) {
    return (uint32_t *)(f->base + (uint32_t)y * f->pitch);
}

/* Overflow-safe clipping: never evaluate x + w or y + h. */
static int clip(const struct fb *f, int32_t *x, int32_t *y,
                int32_t *w, int32_t *h) {
    if (!fb_valid(f) || *w <= 0 || *h <= 0) return 0;
    if (*x < 0) {
        if (*x <= -*w) return 0;
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        if (*y <= -*h) return 0;
        *h += *y;
        *y = 0;
    }
    if (*x >= f->width || *y >= f->height) return 0;
    if (*w > f->width - *x) *w = f->width - *x;
    if (*h > f->height - *y) *h = f->height - *y;
    return *w > 0 && *h > 0;
}

void fb_init(struct fb *f, void *base, int32_t width, int32_t height, uint32_t pitch) {
    if (!f) return;
    f->base = (uint8_t *)base;
    f->width = width;
    f->height = height;
    f->pitch = pitch;
}

int fb_is_valid(const struct fb *f) { return fb_valid(f); }

void fb_put(const struct fb *f, int32_t x, int32_t y, uint32_t color) {
    if (!fb_valid(f) || x < 0 || y < 0 || x >= f->width || y >= f->height) return;
    row(f, y)[x] = color & 0x00FFFFFFu;
}

void fb_fill_rect(const struct fb *f, int32_t x, int32_t y,
                  int32_t w, int32_t h, uint32_t color) {
    if (!clip(f, &x, &y, &w, &h)) return;
    color &= 0x00FFFFFFu;
    for (int32_t r = 0; r < h; r++) {
        uint32_t *p = row(f, y + r) + x;
        for (int32_t c = 0; c < w; c++) p[c] = color;
    }
}

void fb_clear(const struct fb *f, uint32_t color) {
    if (!fb_valid(f)) return;
    fb_fill_rect(f, 0, 0, f->width, f->height, color);
}

void fb_frame_rect(const struct fb *f, int32_t x, int32_t y,
                   int32_t w, int32_t h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    fb_fill_rect(f, x, y, w, 1, color);
    if (h > 1) fb_fill_rect(f, x, y + h - 1, w, 1, color);
    fb_fill_rect(f, x, y, 1, h, color);
    if (w > 1) fb_fill_rect(f, x + w - 1, y, 1, h, color);
}

void fb_blit32_pitch(const struct fb *f, int32_t x, int32_t y,
                     int32_t w, int32_t h, const uint32_t *src,
                     uint32_t source_pitch) {
    if (!src || w <= 0 || h <= 0 || (uint32_t)w > 0x3FFFFFFFu ||
        source_pitch < (uint32_t)w * 4u || (source_pitch & 3u) ||
        !clip(f, &x, &y, &w, &h)) return;
    /* Source origin is adjusted by the amount clipped from the destination. */
    const uint8_t *source = (const uint8_t *)src;
    /* x/y clipping offsets are computed from the original destination. */
    (void)source;
    for (int32_t dy = 0; dy < h; dy++) {
        const uint32_t *s = (const uint32_t *)((const uint8_t *)src +
                              (uint32_t)dy * source_pitch);
        uint32_t *d = row(f, y + dy) + x;
        for (int32_t dx = 0; dx < w; dx++) d[dx] = s[dx] & 0x00FFFFFFu;
    }
}

void fb_blit32(const struct fb *f, int32_t x, int32_t y,
               int32_t w, int32_t h, const uint32_t *src) {
    if (w <= 0 || (uint32_t)w > 0x3FFFFFFFu) return;
    fb_blit32_pitch(f, x, y, w, h, src, (uint32_t)w * 4u);
}
