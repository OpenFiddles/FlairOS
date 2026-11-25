#include <stdint.h>
#include <stdio.h>
#include "airnim.h"
#include "fb.h"

volatile unsigned int ticks;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static int gpu_blends;
static int fake_blend(struct ag_device *d, struct ag_surface *s, int32_t x, int32_t y,
                      int32_t w, int32_t h, uint32_t c) {
    (void)d; (void)s; (void)x; (void)y; (void)w; (void)h; (void)c;
    gpu_blends++;
    return 1;
}
static const struct ag_backend_ops fake_gpu = {
    .name = "fake-gpu", .capabilities = AG_CAP_BLEND_RECT,
    .flags = AG_BACKEND_GPU, .blend_rect = fake_blend };
/* Like a PCI probe: names a GPU and claims caps but implements nothing. */
static const struct ag_backend_ops probe_only = {
    .name = "pci-gpu", .capabilities = AG_CAP_BLIT | AG_CAP_FILL_RECT,
    .flags = AG_BACKEND_GPU };

static uint32_t pixels[8 * 8];

static uint32_t red_of(uint32_t px) { return (px >> 16) & 0xFFu; }

int main(void) {
    struct fb fb;
    struct ag_device dev;
    struct ag_surface surf;
    an_id a = an_id_str("a"), b = an_id_str("b");
    int32_t v, max_v = 0;
    unsigned int t;

    fb_init(&fb, pixels, 8, 8, 8 * 4);
    CHECK(ag_surface_init(&surf, pixels, 8, 8, 8 * 4, AG_PIXEL_XRGB8888) == 0);

    /* ids */
    CHECK(a != b);
    CHECK(an_id_at(a, 1, 2) != an_id_at(a, 2, 1));
    CHECK(an_id_at(a, 1, 2) == an_id_at(a, 1, 2));

    /* first track returns the target; then it glides */
    ticks = 0;
    CHECK(an_track(a, 0, 10, AN_LINEAR) == 0);
    CHECK(an_track(a, 1000, 10, AN_LINEAR) == 0);
    CHECK(an_busy(a));
    CHECK(an_any_busy());
    ticks = 5;
    CHECK(an_track(a, 1000, 10, AN_LINEAR) == 500);
    /* retarget from the current value, not from the old start */
    CHECK(an_track(a, 0, 10, AN_LINEAR) == 500);
    ticks = 10;
    CHECK(an_track(a, 0, 10, AN_LINEAR) == 250);
    ticks = 15;
    CHECK(an_track(a, 0, 10, AN_LINEAR) == 0);
    CHECK(!an_busy(a));
    CHECK(an_value(a, -1) == 0);
    CHECK(an_value(an_id_str("never"), -1) == -1);

    /* disabled snaps */
    an_set_enabled(0);
    CHECK(!an_enabled());
    CHECK(an_track(b, 0, 10, AN_LINEAR) == 0);
    CHECK(an_track(b, 1000, 10, AN_LINEAR) == 1000);
    CHECK(!an_busy(b));
    an_play(b, 0, 777, 10, AN_LINEAR);
    CHECK(an_value(b, 0) == 777);
    an_set_enabled(1);

    /* ease curves: endpoints exact, out-back overshoots */
    ticks = 100;
    an_play(b, 0, 1000, 100, AN_OUT_BACK);
    for (t = 100; t <= 200; ++t) {
        ticks = t;
        v = an_value(b, 0);
        if (v > max_v) max_v = v;
    }
    CHECK(max_v > 1000);
    CHECK(an_value(b, 0) == 1000);
    ticks = 300;
    an_play(b, 0, 1000, 100, AN_IN_OUT);
    ticks = 350;
    v = an_value(b, 0);
    CHECK(v > 400 && v < 600);
    ticks = 300;
    an_play(b, 0, 1000, 100, AN_OUT_CUBIC);
    ticks = 350;
    CHECK(an_value(b, 0) > 800);   /* decelerating curve is ahead at half time */

    /* slot table survives more ids than slots and keeps answering */
    for (t = 0; t < 200; ++t) {
        an_id id = an_id_at(a, (int32_t)t, 0);
        CHECK(an_track(id, (int32_t)t, 5, AN_LINEAR) == (int32_t)t);
    }

    /* colors */
    CHECK(an_mix_color(0x00000000u, 0x00FFFFFFu, 0) == 0x00000000u);
    CHECK(an_mix_color(0x00000000u, 0x00FFFFFFu, AN_ONE) == 0x00FFFFFFu);
    CHECK(an_mix_color(0x00000000u, 0x00FFFFFFu, 500) == 0x007F7F7Fu);
    CHECK(an_lerp(10, 20, 500) == 15);

    /* backend selection: no backend -> CPU */
    CHECK(ag_device_init(&dev, &fb, 0) == 0);
    an_init(&dev);
    CHECK(!an_gpu_present());
    CHECK(!an_using_gpu());
    CHECK(an_backend_name()[0] == 'c');
    for (t = 0; t < 64; ++t) pixels[t] = 0;
    an_blend_rect(&dev, &surf, 0, 0, 8, 8, 0x00FF0000u, 128);
    CHECK(red_of(pixels[0]) > 100 && red_of(pixels[0]) < 160);
    ag_device_shutdown(&dev);

    /* a probe that implements nothing must not count as a GPU path */
    CHECK(ag_device_init(&dev, &fb, &probe_only) == 0);
    an_init(&dev);
    CHECK(!an_gpu_present());
    CHECK(an_backend_name()[0] == 'c');
    ag_device_shutdown(&dev);

    /* a real GPU backend is used, unless the user turns it off */
    CHECK(ag_device_init(&dev, &fb, &fake_gpu) == 0);
    an_init(&dev);
    CHECK(an_gpu_present());
    CHECK(an_using_gpu());
    CHECK(an_backend_name()[0] == 'f');
    gpu_blends = 0;
    an_blend_rect(&dev, &surf, 0, 0, 8, 8, 0x0000FF00u, 64);
    CHECK(gpu_blends == 1);
    an_set_gpu_allowed(0);
    CHECK(!an_using_gpu());
    CHECK(an_backend_name()[0] == 'c');
    for (t = 0; t < 64; ++t) pixels[t] = 0;
    an_blend_rect(&dev, &surf, 0, 0, 8, 8, 0x00FF0000u, 128);
    CHECK(gpu_blends == 1);                 /* CPU path, GPU untouched */
    CHECK(red_of(pixels[0]) > 100);         /* and the pixels still changed */
    an_set_gpu_allowed(1);
    ag_device_shutdown(&dev);

    if (failures) { printf("airnim host test: %d failure(s)\n", failures); return 1; }
    printf("airnim host test: all checks passed\n");
    return 0;
}
