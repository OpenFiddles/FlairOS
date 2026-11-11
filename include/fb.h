#ifndef AIROS_FB_H
#define AIROS_FB_H

#include <stdint.h>

/* Linear framebuffer. Pixels are 0x00RRGGBB and pitch is bytes per scanline.
   Writes go straight to video memory with no backbuffer, so a frame drawn over
   several calls can flicker until a backbuffer exists. */

struct fb {
    uint8_t *base;
    int32_t width;
    int32_t height;
    uint32_t pitch;
};

void fb_init(struct fb *f, void *base, int32_t width, int32_t height, uint32_t pitch);
void fb_put(const struct fb *f, int32_t x, int32_t y, uint32_t color);
void fb_clear(const struct fb *f, uint32_t color);
void fb_fill_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_frame_rect(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void fb_blit32(const struct fb *f, int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *src);

#endif
