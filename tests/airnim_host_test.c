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

    /* preset animations: a named table, lookup, play and offset */
    {
        an_id c = an_id_str("preset");
        const struct an_preset *p;
        int i, j;
        CHECK(an_preset_count() >= 6);
        CHECK(an_preset_at(-1) == 0);
        CHECK(an_preset_at(an_preset_count()) == 0);
        CHECK(an_preset_lookup("fade-in") == an_preset_at(0));
        CHECK(an_preset_lookup("no-such-preset") == 0);
        CHECK(an_preset_lookup(0) == 0);
        for (i = 0; i < an_preset_count(); ++i) {
            p = an_preset_at(i);
            CHECK(p && p->name && p->name[0]);
            CHECK(p->duration_ticks > 0);
            CHECK(p->from >= 0 && p->from <= AN_ONE);
            CHECK(p->to >= 0 && p->to <= AN_ONE);
            CHECK(an_preset_lookup(p->name) == p);
            for (j = 0; j < i; ++j) {
                const struct an_preset *q = an_preset_at(j);
                int same = 1, k = 0;
                while (p->name[k] || q->name[k]) {
                    if (p->name[k] != q->name[k]) { same = 0; break; }
                    k++;
                }
                CHECK(!same);
            }
        }

        ticks = 1000;
        an_set_enabled(1);
        CHECK(an_preset_play(c, "fade-in") == 0);
        CHECK(an_preset_value(c, an_preset_lookup("fade-in")) == 0);
        ticks = 1006;
        CHECK(an_preset_value(c, an_preset_lookup("fade-in")) > 0);
        ticks = 1012;
        CHECK(an_preset_value(c, an_preset_lookup("fade-in")) == AN_ONE);
        CHECK(!an_busy(c));
        CHECK(an_preset_play(c, "bogus") == -1);
        CHECK(an_preset_play_at(c, 0) == -1);

        /* Direction, distance and viewport composition are preset data. */
        {
            struct an_motion m, n;
            const struct an_preset *left = an_preset_lookup("slide-in-left");
            const struct an_preset *right = an_preset_lookup("slide-in-right");
            const struct an_preset *top = an_preset_lookup("slide-in-top");
            const struct an_preset *bottom = an_preset_lookup("slide-in-bottom");
            const struct an_preset *fade = an_preset_lookup("fade-in");
            const struct an_preset *fadeout = an_preset_lookup("fade-out");
            const struct an_preset *cross = an_preset_lookup("crossfade");
            const struct an_preset *pop = an_preset_lookup("pop");
            CHECK(left && right && top && bottom && fade && fadeout && cross && pop);
            CHECK(left->edge == AN_EDGE_LEFT && right->edge == AN_EDGE_RIGHT);
            CHECK(top->edge == AN_EDGE_TOP && bottom->edge == AN_EDGE_BOTTOM);
            CHECK(left->distance_pm == 250 && (left->flags & AN_PRESET_COMPOSE_VIEWPORT));

            /* Same fraction of the element on any panel. */
            an_preset_motion(left, 0, 100, 50, 65536, &m);
            CHECK(m.dx == 25 && m.dy == 0);
            an_preset_motion(left, 0, 400, 200, 65536, &n);
            CHECK(n.dx == 100 && n.dx == m.dx * 4);
            /* The viewport scale must not multiply the fraction again. */
            an_preset_motion(left, 0, 400, 200, 196608, &n);
            CHECK(n.dx == 100);
            an_preset_motion(right, 0, 100, 50, 65536, &m);
            CHECK(m.dx == -25);
            an_preset_motion(top, 0, 100, 50, 65536, &m);
            CHECK(m.dy == 12 && m.dx == 0);
            an_preset_motion(bottom, 0, 100, 50, 65536, &m);
            CHECK(m.dy == -12);
            an_preset_motion(left, 500, 100, 50, 65536, &m);
            CHECK(m.dx > 0 && m.dx < 25);
            an_preset_motion(left, 1000, 100, 50, 65536, &m);
            CHECK(m.dx == 0 && m.alpha == AN_ONE);

            /* Fade and cross-fade weights. */
            an_preset_motion(fade, 0, 100, 50, 65536, &m);
            CHECK(m.alpha == 0);
            an_preset_motion(fade, 1000, 100, 50, 65536, &m);
            CHECK(m.alpha == 1000);
            an_preset_motion(fadeout, 0, 100, 50, 65536, &m);
            CHECK(m.alpha == 0);
            an_preset_motion(cross, 250, 100, 50, 65536, &m);
            CHECK(m.alpha_a == 750 && m.alpha_b == 250);

            /* Pop grows from 80% and ends at rest. */
            an_preset_motion(pop, 0, 40, 40, 65536, &m);
            CHECK(m.scale_pm == 800);
            an_preset_motion(pop, 1000, 40, 40, 65536, &m);
            CHECK(m.scale_pm == 1000);

            /* Clamping keeps a sliding element inside the viewport. */
            an_preset_motion(left, 0, 100, 50, 65536, &m);
            CHECK(an_motion_clamp(&m, 0, 0, 100, 50, 100, 50) == 0);
            CHECK(m.dx == 0 && m.dy == 0);
            an_preset_motion(right, 0, 100, 50, 65536, &m);
            CHECK(an_motion_clamp(&m, 0, 0, 100, 50, 100, 50) == 0);
            CHECK(m.dx == 0);
            an_preset_motion(bottom, 0, 100, 50, 65536, &m);
            CHECK(an_motion_clamp(&m, 20, 10, 100, 50, 220, 150) == 0);
            CHECK(m.dy == -10);
        }

        /* Cross-fade is a pixel mix at the midpoint, not a swap. */
        {
            uint32_t blend[4];
            struct ag_surface bs;
            CHECK(ag_surface_init(&bs, blend, 2, 2, 8, AG_PIXEL_XRGB8888) == 0);
            an_crossfade_rect(0, &bs, 0, 0, 2, 2, 0x00000000u, 0x00FFFFFFu, 500);
            CHECK(red_of(blend[0]) > 110 && red_of(blend[0]) < 145);
            an_crossfade_rect(0, &bs, 0, 0, 2, 2, 0x00000000u, 0x00FFFFFFu, 1000);
            CHECK(red_of(blend[0]) == 255);
        }

        /* disabled animations snap to the preset's end state */
        an_set_enabled(0);
        CHECK(an_preset_play(c, "slide-out") == 0);
        CHECK(an_preset_value(c, an_preset_lookup("slide-out")) == 0);
        an_set_enabled(1);

        /* slide presets drive an offset: span at progress 0, 0 at AN_ONE */
        ticks = 2000;
        p = an_preset_lookup("slide-in");
        CHECK(an_preset_play_at(c, p) == 0);
        CHECK(an_preset_offset(c, p, 300) == 300);
        ticks = 2008;
        CHECK(an_preset_offset(c, p, 300) > 0);
        CHECK(an_preset_offset(c, p, 300) < 300);
        ticks = 2016;
        CHECK(an_preset_offset(c, p, 300) == 0);
        CHECK(an_preset_offset(c, p, 0) == 0);
    }

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
