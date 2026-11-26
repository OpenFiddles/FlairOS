/* Host check for kernel/gfx/fb.c. From the repo root:
   gcc -m32 -std=gnu99 -Wall -Wextra -Iinclude -o build/fb_host_test tests/fb_host_test.c kernel/gfx/fb.c && build/fb_host_test */
#include <stdio.h>
#include <string.h>
#include "fb.h"

#define W 16
#define H 8
#define PITCH (W * 4 + 8)

static uint8_t mem[PITCH * H];
static struct fb screen;
static int fails;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); fails++; } } while (0)

static uint32_t at(int x, int y) {
    uint32_t v;
    memcpy(&v, mem + y * PITCH + x * 4, 4);
    return v;
}

static int padding_intact(void) {
    for (int y = 0; y < H; y++)
        for (int b = W * 4; b < PITCH; b++)
            if (mem[y * PITCH + b] != 0xAA)
                return 0;
    return 1;
}

int main(void) {
    uint32_t src[8];

    memset(mem, 0xAA, sizeof mem);
    fb_init(&screen, mem, W, H, PITCH);

    fb_clear(&screen, 0x111111);
    CHECK(at(0, 0) == 0x111111);
    CHECK(at(W - 1, H - 1) == 0x111111);
    CHECK(padding_intact());

    fb_put(&screen, -1, 0, 0xFF0000);
    fb_put(&screen, W, 0, 0xFF0000);
    fb_put(&screen, 0, H, 0xFF0000);
    CHECK(at(0, 0) == 0x111111);
    fb_put(&screen, 3, 2, 0xFF0000);
    CHECK(at(3, 2) == 0xFF0000);
    CHECK(padding_intact());

    fb_clear(&screen, 0);
    fb_fill_rect(&screen, -2, -2, 5, 5, 0xFF0000);
    CHECK(at(0, 0) == 0xFF0000 && at(2, 2) == 0xFF0000);
    CHECK(at(3, 2) == 0 && at(2, 3) == 0);

    fb_fill_rect(&screen, W, 0, 4, 4, 0x00FF00);
    fb_fill_rect(&screen, 0, H + 1, 4, 4, 0x00FF00);
    CHECK(at(W - 1, 0) == 0);
    CHECK(padding_intact());

    fb_clear(&screen, 0);
    fb_frame_rect(&screen, 2, 2, 4, 3, 0x0000FF);
    CHECK(at(2, 2) == 0x0000FF && at(5, 2) == 0x0000FF);
    CHECK(at(2, 4) == 0x0000FF && at(5, 4) == 0x0000FF);
    CHECK(at(3, 3) == 0 && at(4, 3) == 0);

    for (int i = 0; i < 8; i++)
        src[i] = 0x100 + (uint32_t)i;
    fb_clear(&screen, 0);
    fb_blit32(&screen, -1, -1, 4, 2, src);
    CHECK(at(0, 0) == src[5]);
    CHECK(at(2, 0) == src[7]);
    CHECK(at(3, 0) == 0);
    CHECK(at(0, 1) == 0);

    fb_clear(&screen, 0);
    fb_blit32(&screen, W - 2, H - 1, 4, 2, src);
    CHECK(at(W - 2, H - 1) == src[0]);
    CHECK(at(W - 1, H - 1) == src[1]);
    CHECK(padding_intact());

    if (fails == 0)
        printf("fb host test: all checks passed\n");
    return fails ? 1 : 0;
}
