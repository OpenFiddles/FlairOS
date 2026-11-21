#include <stdio.h>
#include <stdint.h>
#include "windown.h"
#include "airgraph.h"
#include "fb.h"

static uint32_t target_pixels[320 * 200];
static uint32_t client_pixels[40 * 30];

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    struct fb fb;
    struct ag_surface target;
    struct ag_surface client;
    struct ag_device device;
    struct wd_compositor comp;

    for (unsigned int i = 0; i < 320u * 200u; i++) target_pixels[i] = 0x00101828u;
    for (unsigned int i = 0; i < 40u * 30u; i++) client_pixels[i] = 0x0022CC88u;

    fb_init(&fb, target_pixels, 320, 200, 320 * 4);
    CHECK(ag_surface_init(&target, target_pixels, 320, 200, 320 * 4, AG_PIXEL_XRGB8888) == 0);
    CHECK(ag_surface_init(&client, client_pixels, 40, 30, 40 * 4, AG_PIXEL_XRGB8888) == 0);
    CHECK(ag_device_init(&device, &fb, 0) == 0);

    wd_init(&comp);
    int w1 = wd_create_window(&comp, 10, 4, 30, 12, "Back");
    int w2 = wd_create_window(&comp, 20, 6, 30, 12, "Front");
    CHECK(w1 > 0 && w2 > 0 && w1 != w2);
    CHECK(comp.active_id == w2);

    /* Overlapping region (25, 8) hits topmost window w2. */
    CHECK(wd_hit_test(&comp, 25, 8) == w2);
    CHECK(wd_hit_test(&comp, 12, 5) == w1);
    CHECK(wd_hit_test(&comp, 2, 2) == -1);

    /* Raising w1 makes it active and topmost at (25, 8). */
    CHECK(wd_raise(&comp, w1) == 0);
    CHECK(comp.active_id == w1);
    CHECK(wd_hit_test(&comp, 25, 8) == w1);

    /* Chrome hit-testing matches traffic-light layout on w1 (x=10, y=4). */
    CHECK(wd_hit_chrome(&comp, w1, 11, 4) == WD_HIT_CLOSE);
    CHECK(wd_hit_chrome(&comp, w1, 13, 4) == WD_HIT_MINIMIZE);
    CHECK(wd_hit_chrome(&comp, w1, 15, 4) == WD_HIT_ZOOM);
    CHECK(wd_hit_chrome(&comp, w1, 22, 4) == WD_HIT_TITLEBAR);
    CHECK(wd_hit_chrome(&comp, w1, 22, 8) == WD_HIT_CLIENT);

    /* Attach client pixel surface to w1 and composite. */
    CHECK(wd_set_surface(&comp, w1, &client) == 0);
    CHECK(wd_composite(&comp, &device, &target, 80, 25) == 0);
    CHECK(comp.dirty.valid == 0);

    /* w1 at x=10,y=4 on 320x200 -> px=40, py=32, title_h=8 -> client at (41, 40). */
    CHECK(target_pixels[45 * 320 + 50] == 0x0022CC88u);

    /* Minimizing w1 falls back active_id to w2. */
    CHECK(wd_set_minimized(&comp, w1, 1) == 0);
    CHECK(comp.active_id == w2);
    CHECK(wd_hit_test(&comp, 12, 5) == -1);

    CHECK(wd_destroy_window(&comp, w1) == 0);
    CHECK(wd_destroy_window(&comp, w2) == 0);
    CHECK(comp.active_id == -1);

    ag_device_shutdown(&device);
    puts("WinDown compositor host test: all checks passed");
    return 0;
}
