/* SPDX-License-Identifier: AGPL-3.0-only */
/* Boot-time Airnimation preset showcase.

   Everything here asks for a preset by name and resolves it through the public
   API; no durations, curves, directions or distances are picked at this call
   site. The card is sized from the panel and each motion is a fraction of the
   card's own extent, so the same sequence reads the same on 800x600, 1080p, 4K,
   or a portrait phone. Motions are clamped inside the viewport. */
#include "anim_demo.h"
#include "shell_common.h"
#include "../../include/airnim.h"
#include "../../include/hal.h"
#include "../../graphics/compositor/glass_fx.h"

extern volatile unsigned int ticks;

#define DEMO_IN_END     16u   /* slide/fade-in preset durations */
#define DEMO_CROSS_AT   30u
#define DEMO_POP_AT     150u
#define DEMO_OUT_AT     270u
#define DEMO_END        330u

static uint32_t demo_start;
static int demo_started, in_started, cross_started, pop_started, out_started, rest_logged;

int shell_anim_demo_active(void) {
    /* Keep one extra frame after END so the rest marker is emitted and the
       shell repaints the normal UI underneath. */
    return demo_started && !rest_logged;
}

int shell_anim_demo_tick(void) {
    return shell_anim_demo_active();
}

static void play_once(int *flag, const char *name) {
    if (!*flag) {
        (void)an_preset_play(an_id_str(name), name);
        *flag = 1;
    }
}

void shell_anim_demo_render(struct ag_surface *s) {
    uint32_t scale, elapsed;
    int32_t w, h, margin, cw, ch, x, y, pad, alpha, p, ts;
    const struct an_preset *slide_in, *fade_in, *slide_out, *fade_out;
    const struct an_preset *cross, *pop;
    struct an_motion motion, fade;
    uint32_t tint, border, text;

    if (!s || s->width <= 0 || s->height <= 0) return;
    if (!demo_started) {
        demo_started = 1;
        demo_start = ticks;
        hal_serial_puts("flairos64: anim=in\n");
    }
    elapsed = ticks - demo_start;
    if (elapsed >= DEMO_END) {
        if (!rest_logged) {
            rest_logged = 1;
            hal_serial_puts("flairos64: anim=rest\n");
        }
        return;
    }
    if (elapsed >= DEMO_CROSS_AT && !cross_started) {
        hal_serial_puts("flairos64: anim=cross\n");
    }
    if (elapsed >= DEMO_POP_AT && !pop_started) {
        hal_serial_puts("flairos64: anim=pop\n");
    }
    if (elapsed >= DEMO_OUT_AT && !out_started) {
        hal_serial_puts("flairos64: anim=out\n");
    }

    w = s->width;
    h = s->height;
    (void)ag_scale_q16_for(w, h, 1280, 720, &scale);
    margin = ag_px(24, scale);
    cw = ag_px(420, scale);
    ch = ag_px(150, scale);
    if (cw > w - 2 * margin) cw = w - 2 * margin;
    if (ch > h / 2) ch = h / 2;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    x = (w - cw) / 2;
    y = (h - ch) / 2;

    slide_in = an_preset_lookup("slide-in-left");
    fade_in = an_preset_lookup("fade-in");
    slide_out = an_preset_lookup("slide-out-right");
    fade_out = an_preset_lookup("fade-out");
    cross = an_preset_lookup("crossfade");
    pop = an_preset_lookup("pop");
    if (!slide_in || !fade_in || !slide_out || !fade_out || !cross || !pop) return;

    if (!in_started && elapsed < DEMO_OUT_AT) {
        (void)an_preset_play(an_id_str("slide-in-left"), "slide-in-left");
        (void)an_preset_play(an_id_str("fade-in"), "fade-in");
        in_started = 1;
    }
    if (elapsed >= DEMO_OUT_AT && !out_started) {
        (void)an_preset_play(an_id_str("slide-out-right"), "slide-out-right");
        (void)an_preset_play(an_id_str("fade-out"), "fade-out");
        out_started = 1;
    }

    if (elapsed < DEMO_IN_END) {
        an_preset_motion(slide_in, an_preset_value(an_id_str("slide-in-left"), slide_in),
                         cw, ch, scale, &motion);
        an_preset_motion(fade_in, an_preset_value(an_id_str("fade-in"), fade_in),
                         cw, ch, scale, &fade);
    } else if (elapsed < DEMO_OUT_AT) {
        an_preset_motion(fade_in, AN_ONE, cw, ch, scale, &motion);
        fade = motion;
    } else {
        an_preset_motion(slide_out, an_preset_value(an_id_str("slide-out-right"), slide_out),
                         cw, ch, scale, &motion);
        an_preset_motion(fade_out, an_preset_value(an_id_str("fade-out"), fade_out),
                         cw, ch, scale, &fade);
    }
    (void)an_motion_clamp(&motion, x, y, cw, ch, w, h);
    alpha = fade.alpha;
    if (alpha <= 10) return;
    x += motion.dx;
    y += motion.dy;

    pad = ag_px(16, scale);
    ts = (int32_t)(scale >> 16);   /* 1x at phone/800p, 3x on a 4K panel */
    if (ts < 1) ts = 1;
    tint = 0x00141A2Au;
    border = 0x005EEAD4u;
    text = 0x00F4F7FFu;

    gfx_fill_round_rect(s, x + ag_px(4, scale), y + ag_px(6, scale), cw, ch,
                        ag_px(14, scale), 0x00000000u,
                        (uint8_t)((alpha * 120) / AN_ONE));
    gfx_fill_round_rect(s, x, y, cw, ch, ag_px(14, scale), tint,
                        (uint8_t)((alpha * 238) / AN_ONE));
    gfx_stroke_round_rect(s, x, y, cw, ch, ag_px(14, scale), border,
                          (uint8_t)((alpha * 210) / AN_ONE));
    gfx_draw_text_scaled(s, x + pad, y + pad, "Airnimation presets", text, 2 * ts);

    if (elapsed >= DEMO_CROSS_AT && elapsed < DEMO_OUT_AT) {
        int32_t ix = x + pad;
        int32_t iy = y + ag_px(48, scale);
        int32_t iw = cw - 2 * pad;
        int32_t ih = ch - ag_px(48, scale) - pad;
        uint32_t ca = 0x001B4D5Cu, cb = 0x005C3A1Bu;
        if (iw > 0 && ih > 0) {
            play_once(&cross_started, "crossfade");
            p = an_preset_value(an_id_str("crossfade"), cross);
            an_preset_motion(cross, p, iw, ih, scale, &motion);
            an_crossfade_rect(0, s, ix, iy, iw, ih, ca, cb, p);
            gfx_draw_text_scaled(s, ix + pad, iy + ih / 2 - ag_px(6, scale),
                                 "Screen A -> Screen B", text, ts);
            gfx_draw_text_scaled(s, ix + pad, iy + ih - ag_px(18, scale),
                                 "crossfade", an_mix_color(0x0090D8FFu, 0x00FFC278u, p), ts);
        }
    }

    if (elapsed >= DEMO_POP_AT && elapsed < DEMO_OUT_AT) {
        int32_t base = ag_px(40, scale), size;
        play_once(&pop_started, "pop");
        p = an_preset_value(an_id_str("pop"), pop);
        an_preset_motion(pop, p, base, base, scale, &motion);
        size = (base * motion.scale_pm) / 1000;
        if (size > ag_px(64, scale)) size = ag_px(64, scale);
        if (size < 1) size = 1;
        gfx_fill_round_rect(s, x + cw - pad - size, y + pad - (size - base) / 2,
                            size, size, ag_px(10, scale), 0x003B82F6u,
                            (uint8_t)((motion.alpha * 230) / AN_ONE));
        gfx_draw_text_scaled(s, x + cw - pad - size + size / 3,
                             y + pad + size / 3, "AN", text, 2 * ts);
    }
}
