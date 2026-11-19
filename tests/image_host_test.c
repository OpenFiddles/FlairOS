#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "image.h"

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
static void make_bmp(unsigned char *p) {
    memset(p, 0, 62);
    p[0] = 'B'; p[1] = 'M';
    put32(p + 2, 62);
    put32(p + 10, 54);
    put32(p + 14, 40);
    put32(p + 18, 2);
    put32(p + 22, 1);
    put16(p + 26, 1);
    put16(p + 28, 24);
    put32(p + 34, 8);
    /* BMP stores B,G,R; output is packed 0x00RRGGBB. */
    p[54] = 0x33; p[55] = 0x22; p[56] = 0x11;
    p[57] = 0x66; p[58] = 0x55; p[59] = 0x44;
}
int main(void) {
    unsigned char bmp[62];
    uint32_t output[2] = {0, 0};
    struct image_buffer destination = {output, 2, 1, 8, sizeof(output)};
    struct image_info info;
    make_bmp(bmp);
    CHECK(image_probe(bmp, sizeof(bmp), &info) == IMAGE_OK);
    CHECK(info.format == IMAGE_FORMAT_BMP);
    CHECK(info.width == 2 && info.height == 1 && info.bits_per_pixel == 24);
    CHECK(image_decode(bmp, sizeof(bmp), &destination, &info) == IMAGE_OK);
    CHECK(output[0] == 0x00112233u);
    CHECK(output[1] == 0x00445566u);
    CHECK(info.format == IMAGE_FORMAT_BMP && info.width == 2);
    destination.capacity = 4;
    CHECK(image_decode(bmp, sizeof(bmp), &destination, 0) == IMAGE_ERR_BUFFER);
    CHECK(image_probe(0, sizeof(bmp), &info) == IMAGE_ERR_ARGUMENT);
    CHECK(image_decode(bmp, sizeof(bmp), 0, 0) == IMAGE_ERR_ARGUMENT);
    puts("Image subsystem host test: all checks passed");
    return 0;
}
