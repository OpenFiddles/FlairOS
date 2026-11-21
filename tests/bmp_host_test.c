#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bmp.h"

#define CHECK(expr) do { if (!(expr)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; \
} } while (0)

static void put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static void make_bmp(unsigned char *p, int height) {
    memset(p, 0, 58);
    p[0] = 'B'; p[1] = 'M';
    put32(p + 2, 58);
    put32(p + 10, 54);
    put32(p + 14, 40);
    put32(p + 18, 1);
    put32(p + 22, (uint32_t)height);
    put16(p + 26, 1);
    put16(p + 28, 24);
    put32(p + 34, 4);
    p[54] = 0x33; p[55] = 0x22; p[56] = 0x11; p[57] = 0;
}
int main(void) {
    unsigned char data[58];
    struct bmp_info info;
    unsigned int rgb = 0;
    make_bmp(data, 1);
    CHECK(bmp_parse(data, sizeof data, &info) == BMP_OK);
    CHECK(info.width == 1 && info.height == 1 && info.bpp == 24);
    CHECK(info.top_down == 0);
    CHECK(bmp_pixel(data, sizeof data, &info, 0, 0, &rgb) == BMP_OK);
    CHECK(rgb == 0x112233u);

    make_bmp(data, -1);
    CHECK(bmp_parse(data, sizeof data, &info) == BMP_OK);
    CHECK(info.top_down == 1);
    CHECK(bmp_pixel(data, sizeof data, &info, 0, 0, &rgb) == BMP_OK);
    CHECK(rgb == 0x112233u);

    make_bmp(data, 1);
    put32(data + 10, 40); /* Pixel array cannot overlap the DIB header. */
    CHECK(bmp_parse(data, sizeof data, &info) == BMP_ERR_FORMAT);
    make_bmp(data, 1);
    put16(data + 28, 16);
    CHECK(bmp_parse(data, sizeof data, &info) == BMP_ERR_FORMAT);
    CHECK(bmp_parse(data, 53, &info) == BMP_ERR_SIZE);
    puts("BMP host test: all checks passed");
    return 0;
}
