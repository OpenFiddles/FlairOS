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

    /* Event queue, resize corner, and zoom toggle */
    CHECK(wd_set_minimized(&comp, w1, 0) == 0);
    CHECK(wd_hit_chrome(&comp, w1, 39, 15) == WD_HIT_RESIZE);
    struct wd_event ev = { WD_EVT_KEY, 0, 0, 0, 'A' }, out;
    CHECK(wd_post_event(&comp, w1, &ev) == 0);
    CHECK(wd_poll_event(&comp, w1, &out) == 1 && out.type == WD_EVT_KEY && out.key == 'A');
    CHECK(wd_poll_event(&comp, w1, &out) == 0);
    CHECK(wd_zoom_toggle(&comp, w1, 80, 24) == 0);
    CHECK(wd_zoom_toggle(&comp, w1, 80, 24) == 0);

    /* Density: unconfigured keeps the legacy 1 px chrome. */
    {
        struct wd_metrics legacy = wd_chrome_metrics(&comp);
        CHECK(legacy.title_h == 1 && legacy.inset == 1 && legacy.button_w == 2);
        CHECK(legacy.button_gap == 0 && legacy.grip == 1);
        CHECK(wd_scale_q16(&comp) == 65536u);
    }

    /* A 4K panel derives 3x chrome from a 720p design: 36 px title bar, 36 px
       buttons, and hit regions that follow the scaled geometry. */
    {
        struct wd_compositor dense;
        struct wd_metrics m;
        wd_init(&dense);
        CHECK(wd_set_display(&dense, 3840, 2160) == 0);
        CHECK(wd_scale_q16(&dense) == 196608u);
        m = wd_chrome_metrics(&dense);
        CHECK(m.title_h == 36 && m.inset == 24 && m.button_w == 36);
        CHECK(m.button_gap == 18 && m.grip == 30);
        int d = wd_create_window(&dense, 4, 4, 240, 120, "4K");
        CHECK(d > 0);
        CHECK(wd_hit_chrome(&dense, d, 30, 8) == WD_HIT_CLOSE);
        CHECK(wd_hit_chrome(&dense, d, 100, 8) == WD_HIT_MINIMIZE);
        CHECK(wd_hit_chrome(&dense, d, 150, 8) == WD_HIT_ZOOM);
        CHECK(wd_hit_chrome(&dense, d, 200, 8) == WD_HIT_TITLEBAR);
        CHECK(wd_hit_chrome(&dense, d, 200, 60) == WD_HIT_CLIENT);
        CHECK(wd_hit_chrome(&dense, d, 243, 123) == WD_HIT_RESIZE);
        CHECK(wd_hit_chrome(&dense, d, 220, 60) == WD_HIT_CLIENT);
        CHECK(wd_set_display(&dense, 0, 720) != 0);
        CHECK(wd_set_scale_q16(&dense, 0) != 0);

        /* Composite reserves the scaled title height and border, not one
           grid row: a pixel-space window (0,0,80,60) puts its client at
           (12,36) and blits the 40x30 surface from there. */
        for (unsigned int i = 0; i < 320u * 200u; i++) target_pixels[i] = 0x00101828u;
        CHECK(wd_set_surface(&dense, d, 0) == 0);
        CHECK(wd_create_window(&dense, 0, 0, 80, 60, "Small") > 0);
        int small = dense.active_id;
        CHECK(wd_set_surface(&dense, small, &client) == 0);
        CHECK(wd_composite(&dense, &device, &target, 80, 25) == 0);
        CHECK(target_pixels[40 * 320 + 16] == 0x0022CC88u);
        CHECK(target_pixels[30 * 320 + 16] != 0x0022CC88u);
        /* The scaled header is drawn, not a one-pixel line. */
        CHECK(target_pixels[30 * 320 + 15] == 0x0034323Au);

        /* Adoption of a larger panel rescales open geometry instead of
           leaving it stranded at the old coordinates. */
        struct wd_compositor grown;
        wd_init(&grown);
        CHECK(wd_set_display(&grown, 1280, 720) == 0);
        int g = wd_create_window(&grown, 100, 100, 200, 150, "Grow");
        CHECK(g > 0);
        CHECK(wd_rescale_display(&grown, 1280, 720, 3840, 2160) == 0);
        CHECK(wd_scale_q16(&grown) == 196608u);
        CHECK(wd_hit_test(&grown, 305, 305) == g);
        CHECK(wd_hit_test(&grown, 105, 105) == -1);
        CHECK(wd_rescale_display(&grown, 0, 720, 3840, 2160) != 0);
    }

    CHECK(wd_destroy_window(&comp, w1) == 0);
    CHECK(wd_destroy_window(&comp, w2) == 0);
    CHECK(comp.active_id == -1);

    ag_device_shutdown(&device);
    puts("WinDown compositor host test: all checks passed");
    return 0;
}
