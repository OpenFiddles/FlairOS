/*
FILE PATH: kernel/gfx/bmp.c
CONTRIBUTOR (Optional): Textile
PURPOSE: Validates uncompressed & BI_BITFIELDS BMP headers and exposes safe RGB/ARGB pixel reads.
*/
#include "bmp.h"
#include <stdint.h>

static unsigned int rd16(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int bmp_parse(const unsigned char *d, unsigned int len, struct bmp_info *o) {
    int32_t w, h;
    unsigned int planes, comp, dib;
    if (!d || !o) return BMP_ERR_FORMAT;
    if (len < 54) return BMP_ERR_SIZE;
    if (d[0] != 'B' || d[1] != 'M') return BMP_ERR_MAGIC;
    dib = rd32(d + 14);
    if (dib < 40 || dib > len - 14) return BMP_ERR_FORMAT;
    o->pixel_offset = rd32(d + 10);
    if (o->pixel_offset < 14u + dib) return BMP_ERR_FORMAT;
    w = (int32_t)rd32(d + 18);
    h = (int32_t)rd32(d + 22);
    planes = rd16(d + 26);
    o->bpp = (int)rd16(d + 28);
    comp = rd32(d + 30);
    o->comp = comp;
    if (planes != 1) return BMP_ERR_FORMAT;
    if (!(comp == 0u || (comp == 3u && o->bpp == 32))) return BMP_ERR_FORMAT;
    if (o->bpp != 24 && o->bpp != 32) return BMP_ERR_FORMAT;
    if (w <= 0 || w > 16384 || h == 0 ||
        h == (-2147483647 - 1) || h > 16384 || h < -16384)
        return BMP_ERR_FORMAT;
    o->width = w;
    o->top_down = h < 0;
    o->height = h < 0 ? -h : h;
    o->row_stride = ((w * o->bpp + 31) / 32) * 4;
    if (o->pixel_offset >= len) return BMP_ERR_SIZE;
    if ((unsigned int)o->height > (len - o->pixel_offset) / (unsigned int)o->row_stride) return BMP_ERR_SIZE;
    return BMP_OK;
}

int bmp_pixel(const unsigned char *d, unsigned int len, const struct bmp_info *o, int x, int y, unsigned int *rgb) {
    unsigned int bytes, off;
    int row;
    const unsigned char *p;
    if (!d || !o || !rgb || o->width <= 0 || o->height <= 0 ||
        o->row_stride <= 0 || (o->bpp != 24 && o->bpp != 32) ||
        (o->top_down != 0 && o->top_down != 1))
        return BMP_ERR_FORMAT;
    if (x < 0 || y < 0 || x >= o->width || y >= o->height)
        return BMP_ERR_SIZE;
    row = o->top_down ? y : o->height - 1 - y;
    bytes = (unsigned int)o->bpp / 8;
    off = o->pixel_offset + (unsigned int)row * (unsigned int)o->row_stride + (unsigned int)x * bytes;
    if (off > len || len - off < bytes) return BMP_ERR_SIZE;
    p = d + off;
    *rgb = ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0];
    return BMP_OK;
}

int bmp_pixel_argb(const unsigned char *d, unsigned int len, const struct bmp_info *o, int x, int y, unsigned int *argb) {
    unsigned int bytes, off;
    int row;
    const unsigned char *p;
    if (!d || !o || !argb || o->width <= 0 || o->height <= 0 ||
        o->row_stride <= 0 || (o->bpp != 24 && o->bpp != 32) ||
        (o->top_down != 0 && o->top_down != 1))
        return BMP_ERR_FORMAT;
    if (x < 0 || y < 0 || x >= o->width || y >= o->height)
        return BMP_ERR_SIZE;
    row = o->top_down ? y : o->height - 1 - y;
    bytes = (unsigned int)o->bpp / 8;
    off = o->pixel_offset + (unsigned int)row * (unsigned int)o->row_stride + (unsigned int)x * bytes;
    if (off > len || len - off < bytes) return BMP_ERR_SIZE;
    p = d + off;
    unsigned int rgb = ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0];
    if (o->bpp == 32) {
        unsigned int a = p[3];
        *argb = (a << 24) | rgb;
    } else {
        *argb = (rgb == 0x00FF00FFu) ? 0x00FF00FFu : (0xFF000000u | rgb);
    }
    return BMP_OK;
}
