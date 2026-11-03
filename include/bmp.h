/*
FILE PATH: include/bmp.h
CONTRIBUTOR (Optional): Textile
PURPOSE: Header-only BMP check for resources/images/BOOT_LOGO.bmp and icon_menubar.bmp. Accepts uncompressed 24-bit and 32-bit BMP files and reports width, height, row stride and pixel offset. Nothing draws the image yet because the graphics mode is not built.
*/

#ifndef FLARE_BMP_H
#define FLARE_BMP_H

struct bmp_info {
    int width;
    int height;
    int top_down;
    unsigned int bpp;
    unsigned int data_offset;
    unsigned int stride;
};

static inline unsigned int bmp_u16(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1] << 8);
}

static inline unsigned int bmp_u32(const unsigned char *p) {
    return p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline int bmp_parse(const unsigned char *buf, unsigned int len, struct bmp_info *out) {
    unsigned int hdr, offset, bpp, compression, stride, rows;
    int w, h;

    if (len < 54 || buf[0] != 'B' || buf[1] != 'M') return -1;
    offset = bmp_u32(buf + 10);
    hdr = bmp_u32(buf + 14);
    if (hdr != 40 && hdr != 108 && hdr != 124) return -2;
    w = (int)bmp_u32(buf + 18);
    h = (int)bmp_u32(buf + 22);
    bpp = bmp_u16(buf + 28);
    compression = bmp_u32(buf + 30);
    if (w <= 0 || w > 4096) return -3;
    if (h == 0 || h > 4096 || h < -4096) return -3;
    if (bpp != 24 && bpp != 32) return -4;
    if (compression != 0 && !(compression == 3 && bpp == 32)) return -5;
    if (offset < 14 + hdr || offset >= len) return -6;

    stride = ((unsigned int)w * bpp + 31) / 32 * 4;
    rows = (unsigned int)(h < 0 ? -h : h);
    if (offset + stride * rows > len) return -7;

    out->width = w;
    out->height = (int)rows;
    out->top_down = h < 0;
    out->bpp = bpp;
    out->data_offset = offset;
    out->stride = stride;
    return 0;
}

#endif
