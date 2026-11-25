/* Cursor sprites: geometry, rendering, BMP override and a golden pixel hash.
   The hash makes the artwork reproducible: any change to a sprite or to the
   drawing code is caught here and must be accepted deliberately. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "airgraph.h"
#include "cursor.h"

#define W 64
#define H 32
#define BACKGROUND 0x00808080u
#define GOLDEN_HASH 0xF9E67D1Du

static uint32_t pixels[W * H];
static struct ag_surface surface;
static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void clear(void) {
    for (int i = 0; i < W * H; ++i) pixels[i] = BACKGROUND;
}

static uint32_t at(int x, int y) { return pixels[y * W + x] & 0x00FFFFFFu; }

static uint32_t fnv1a(void) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < W * H; ++i)
        for (int b = 0; b < 4; ++b) { h ^= (pixels[i] >> (b * 8)) & 0xFFu; h *= 16777619u; }
    return h;
}

static void write_le32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

int main(void) {
    int w, h, hx, hy;
    ag_surface_init(&surface, pixels, W, H, W * 4, AG_PIXEL_XRGB8888);

    /* Geometry: every shape has a hotspot inside its sprite. */
    for (int s = 0; s < CURSOR_SHAPE_COUNT; ++s) {
        cursor_metrics((enum cursor_shape)s, &w, &h, &hx, &hy);
        CHECK(w > 0 && h > 0 && hx >= 0 && hx < w && hy >= 0 && hy < h);
        CHECK(cursor_builtin_pixel((enum cursor_shape)s, -1, 0) == -1);
        CHECK(cursor_builtin_pixel((enum cursor_shape)s, w, 0) == -1);
    }
    CHECK(cursor_builtin_pixel(CURSOR_ARROW, 0, 0) == 1);   /* outline at the tip */
    CHECK(cursor_builtin_pixel(CURSOR_ARROW, 1, 0) == 0);   /* transparent beside it */
    CHECK(cursor_builtin_pixel(CURSOR_ARROW, 1, 2) == 2);   /* white fill inside */

    /* Rendering: the tip lands on the hotspot, transparent pixels leave the background. */
    clear();
    cursor_reset_custom();
    cursor_set_shape(CURSOR_ARROW);
    cursor_draw(0, &surface, 5, 5);
    CHECK(at(5, 5) == 0x00101018u);
    CHECK(at(6, 7) == 0x00FFFFFFu);
    CHECK(at(10, 5) == BACKGROUND);

    /* Hand and I-beam are placed by their own hotspots. */
    clear();
    cursor_set_shape(CURSOR_HAND);
    cursor_metrics(CURSOR_HAND, &w, &h, &hx, &hy);
    cursor_draw(0, &surface, 20 + hx, 4 + hy);
    CHECK(at(26, 4) == 0x00101018u);                     /* fingertip outline */
    CHECK(cursor_get_shape() == CURSOR_HAND);
    cursor_set_shape((enum cursor_shape)99);              /* out of range falls back */
    CHECK(cursor_get_shape() == CURSOR_ARROW);

    /* BMP override: magenta is transparent, hotspot is the top-left pixel. */
    {
        unsigned char bmp[70];
        memset(bmp, 0, sizeof(bmp));
        bmp[0] = 'B'; bmp[1] = 'M';
        write_le32(bmp + 2, sizeof(bmp)); write_le32(bmp + 10, 54);
        write_le32(bmp + 14, 40); write_le32(bmp + 18, 2); write_le32(bmp + 22, 2);
        bmp[26] = 1; bmp[28] = 24; write_le32(bmp + 34, 16);
        /* bottom-up rows, BGR: row y=1 (green, blue), then row y=0 (red, magenta) */
        unsigned char rows[16] = { 0,255,0,  255,0,0, 0,0,   0,0,255,  255,0,255, 0,0 };
        memcpy(bmp + 54, rows, sizeof(rows));
        CHECK(cursor_load_bmp(bmp, sizeof(bmp)) == 1);
        clear();
        cursor_set_shape(CURSOR_ARROW);
        cursor_draw(0, &surface, 4, 4);
        CHECK(at(4, 4) == 0x00FF0000u);                   /* red   */
        CHECK(at(5, 4) == BACKGROUND);                    /* magenta key is transparent */
        CHECK(at(4, 5) == 0x0000FF00u);                   /* green */
        CHECK(at(5, 5) == 0x000000FFu);                   /* blue  */
        CHECK(cursor_load_bmp(bmp, 10) == 0);             /* truncated file is rejected... */
        clear();
        cursor_draw(0, &surface, 4, 4);                   /* ...and the arrow is back */
        CHECK(at(4, 4) == 0x00101018u);
    }

    /* Golden hash of all three built-in shapes on one canvas. */
    clear();
    cursor_reset_custom();
    cursor_set_shape(CURSOR_ARROW); cursor_draw(0, &surface, 2, 2);
    cursor_set_shape(CURSOR_HAND);  cursor_metrics(CURSOR_HAND, &w, &h, &hx, &hy);
    cursor_draw(0, &surface, 24 + hx, 2 + hy);
    cursor_set_shape(CURSOR_IBEAM); cursor_metrics(CURSOR_IBEAM, &w, &h, &hx, &hy);
    cursor_draw(0, &surface, 48 + hx, 4 + hy);
    {
        uint32_t hash = fnv1a();
        if (GOLDEN_HASH == 0u) printf("golden hash (set GOLDEN_HASH to this): 0x%08X\n", hash);
        else CHECK(hash == GOLDEN_HASH);
    }

    if (failures) { printf("cursor_host_test: %d failure(s)\n", failures); return 1; }
    printf("cursor_host_test: ok\n");
    return 0;
}
