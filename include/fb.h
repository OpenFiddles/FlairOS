#ifndef AIROS_FB_H
#define AIROS_FB_H

#include <stdint.h>

/* Validated 32-bit linear framebuffer. Pixels are 0x00RRGGBB; pitch is bytes. */
struct fb {
    uint8_t *base;
    int32_t width;
    int32_t height;
    uint32_t pitch;
};

void fb_init(struct fb *f, void *base, int32_t width, int32_t height, uint32_t pitch);
int fb_is_valid(const struct fb *f);
void fb_put(const struct fb *f, int32_t x, int32_t y, uint32_t color);
void fb_clear(const struct fb *f, uint32_t color);
void fb_fill_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_frame_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_blit32(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *src);
void fb_blit32_pitch(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h,
                     const uint32_t *src, uint32_t source_pitch);

#endif
