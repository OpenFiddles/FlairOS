#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "airgraph.h"

#define W 8
#define H 6
#define PITCH (W * 4 + 8)

static uint8_t pixels[PITCH * H];
static uint8_t display_mem[PITCH * H];
static int fails;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t pixel_at(const uint8_t *mem, int x, int y) {
    uint32_t value;
    memcpy(&value, mem + y * PITCH + x * 4, 4);
    return value;
}

static int padding_ok(const uint8_t *mem) {
    for (int y = 0; y < H; y++)
        for (int x = W * 4; x < PITCH; x++)
            if (mem[y * PITCH + x] != 0xA5)
                return 0;
    return 1;
}

int main(void) {
    struct ag_surface s;
    struct fb fb;
    struct ag_device dev;

    memset(pixels, 0xA5, sizeof pixels);
    memset(display_mem, 0xA5, sizeof display_mem);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, AG_PIXEL_XRGB8888) == 0);
    CHECK(ag_surface_init(&s, pixels, W, H, W * 4 - 1, AG_PIXEL_XRGB8888) != 0);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, (enum ag_pixel_format)99) != 0);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, AG_PIXEL_XRGB8888) == 0);

    ag_clear(&s, 0x123456);
    CHECK(pixel_at(pixels, 0, 0) == 0x00123456);
    CHECK(pixel_at(pixels, W - 1, H - 1) == 0x00123456);
    CHECK(padding_ok(pixels));

    ag_put_pixel(&s, 2, 3, 0x00ABCDEF);
    CHECK(pixel_at(pixels, 2, 3) == 0x00ABCDEF);
    ag_put_pixel(&s, -1, 0, 0x00FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x00123456);

    ag_fill_rect(0, &s, -2, -1, 4, 3, 0x00FF0000);
    CHECK(pixel_at(pixels, 0, 0) == 0x00FF0000);
    CHECK(pixel_at(pixels, 1, 1) == 0x00FF0000);
    CHECK(pixel_at(pixels, 2, 1) == 0x00123456);
    CHECK(padding_ok(pixels));

    ag_clear(&s, 0);
    ag_blend_rect(0, &s, 0, 0, W, H, 0x80FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x007F7F7F);
    ag_blend_rect(0, &s, 0, 0, W, H, 0x00FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x007F7F7F);

    {
        uint32_t src[9] = {0x00000011, 0x00000022, 0x00000033,
                           0x00000044, 0x00000055, 0x00000066,
                           0x00000077, 0x00000088, 0x00000099};
        ag_blit(0, &s, -1, -1, 3, 3, src, 12);
        CHECK(pixel_at(pixels, 0, 0) == 0x00000055);
    }

    fb_init(&fb, display_mem, W, H, PITCH);
    CHECK(ag_device_init(&dev, &fb, 0) == 0);
    CHECK(ag_present(&dev, &s) == 0);
    CHECK(pixel_at(display_mem, 0, 0) == pixel_at(pixels, 0, 0));
    CHECK(padding_ok(display_mem));
    ag_device_shutdown(&dev);
    CHECK(ag_present(&dev, &s) != 0);

    if (!fails)
        puts("AirGraph host test: all checks passed");
    return fails ? 1 : 0;
}
