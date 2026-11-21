#include <stdio.h>
#include <stdint.h>
#include "airgraph.h"
#include "fb.h"
#include "desktop_chrome.h"

static uint32_t pixels[160 * 100];
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    struct fb fb;
    struct ag_surface surface;
    struct ag_device device;
    for (unsigned int i = 0; i < 160u * 100u; ++i) pixels[i] = 0x00111111u;
    fb_init(&fb, pixels, 160, 100, 160 * 4);
    CHECK(ag_surface_init(&surface, pixels, 160, 100, 160 * 4,
                          AG_PIXEL_XRGB8888) == 0);
    CHECK(ag_device_init(&device, &fb, 0) == 0);
    CHECK(desktop_chrome_draw(&device, &surface, 80, 25,
                              10, 4, 40, 15, "Terminal", 1) == 0);
    /* Window at x=10, y=4, w=40, h=15 -> px=20, py=16, pw=80, ph=60, cw=2, ch=4 */
    CHECK(pixels[18 * 160 + 40] == 0x00303544); /* active title bar interior */
    CHECK(pixels[17 * 160 + 22] == 0x00F0605A); /* left close traffic-light control */
    CHECK(pixels[50 * 160 + 60] == 0x00111111); /* body stays untouched */
    CHECK(pixels[78 * 160 + 60] != 0x00111111); /* bottom shadow */
    CHECK(desktop_chrome_draw(&device, &surface, 80, 25,
                              -1, 4, 40, 15, "Bad", 0) != 0);
    ag_device_shutdown(&device);
    puts("Desktop pixel chrome host test: all checks passed");
    return 0;
}
