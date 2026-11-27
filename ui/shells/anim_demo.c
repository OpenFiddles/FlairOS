/* SPDX-License-Identifier: AGPL-3.0-only */
/* Boot-time Airnimation preset showcase.

   The demo enumerates the public preset table and asks for each preset by name;
   it never carries its own durations, curves, directions or distances. The card
   is sized from the panel, and each motion is resolved against the card's own
   extent, so the same tour reads the same on 800x600, 1080p, 4K, or a portrait
   phone. Every frame is clamped inside the viewport. */
#include "anim_demo.h"
#include "shell_common.h"
#include "../../include/airnim.h"
#include "../../include/hal.h"
#include "../../graphics/compositor/glass_fx.h"

extern volatile unsigned int ticks;

static uint32_t demo_start, phase_start;
static int demo_started, rest_logged, cur_index;

static void put_u32(uint32_t value) {
    char buf[11];
    int n = 0;
    if (!value) {
        hal_serial_puts("0");
        return;
    }
    while (value && n < (int)sizeof(buf)) {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (n--) hal_serial_puts((char[]){ buf[n], 0 });
}

static void start_preset(int index, int count) {
    const struct an_preset *p = an_preset_at(index);
    if (!p) return;
    /* Request by name, exactly as an app would. */
    (void)an_preset_play(an_id_str(p->name), p->name);
    hal_serial_puts("flairos64: anim=pst i=");
    put_u32((uint32_t)index);
    hal_serial_puts("/");
    put_u32((uint32_t)count);
    hal_serial_puts(" name=");
    hal_serial_puts(p->name);
    hal_serial_puts("\n");
}

int shell_anim_demo_active(void) {
    /* Keep one extra frame after the last preset so the rest marker is emitted
       and the shell repaints the normal UI underneath. */
    return demo_started && !rest_logged;
}

int shell_anim_demo_tick(void) {
    return shell_anim_demo_active();
}

void shell_anim_demo_render(struct ag_surface *s) {
    uint32_t scale, elapsed, hold;
    int32_t w, h, margin, cw, ch, x, y, pad, alpha, p, ts;
    int count;
    const struct an_preset *preset;
    struct an_motion motion;
    uint32_t tint, border, text;

    if (!s || s->width <= 0 || s->height <= 0) return;
    count = an_preset_count();
    if (count <= 0) return;

    if (!demo_started) {
        demo_started = 1;
        demo_start = ticks;
        phase_start = 0;   /* relative to demo_start, not an absolute tick */
        cur_index = 0;
        hal_serial_puts("flairos64: anim=pst count=");
        put_u32((uint32_t)count);
        hal_serial_puts("\n");
        start_preset(0, count);
    }

    elapsed = ticks - demo_start;
    /* Advance through the enumerated table; each preset gets its own duration
       plus a short hold so the frame gate can capture it in motion. */
    for (;;) {
        preset = an_preset_at(cur_index);
        if (!preset) break;
        hold = preset->duration_ticks * 2u + 24u;
        if (elapsed - phase_start < hold) break;
        phase_start += hold;
        cur_index++;
        if (cur_index < count) start_preset(cur_index, count);
    }
    if (cur_index >= count) {
        if (!rest_logged) {
            rest_logged = 1;
            hal_serial_puts("flairos64: anim=rest\n");
        }
        return;
    }

    preset = an_preset_at(cur_index);
    w = s->width;
    h = s->height;
    (void)ag_scale_q16_for(w, h, 1280, 720, &scale);
    margin = ag_px(24, scale);
    cw = ag_px(420, scale);
    ch = ag_px(180, scale);
    if (cw > w - 2 * margin) cw = w - 2 * margin;
    if (ch > h / 2) ch = h / 2;
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    x = (w - cw) / 2;
    y = (h - ch) / 2;

    p = an_preset_value(an_id_str(preset->name), preset);
    if (an_preset_frame(preset, p, x, y, cw, ch, w, h, scale, &motion) != 0)
        return;
    alpha = motion.alpha;
    if (alpha <= 10) return;

    if (preset->kind == AN_KIND_POP) {
        int32_t sw = (cw * motion.scale_pm) / 1000;
        int32_t sh = (ch * motion.scale_pm) / 1000;
        if (sw < 1) sw = 1;
        if (sh < 1) sh = 1;
        x += (cw - sw) / 2;
        y += (ch - sh) / 2;
        cw = sw;
        ch = sh;
    }
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
    if (preset->kind == AN_KIND_CROSSFADE) {
        an_crossfade_rect(0, s, x, y, cw, ch, 0x001B4D5Cu, 0x005C3A1Bu, p);
        gfx_stroke_round_rect(s, x, y, cw, ch, ag_px(14, scale), border,
                              (uint8_t)((alpha * 210) / AN_ONE));
    } else {
        gfx_fill_round_rect(s, x, y, cw, ch, ag_px(14, scale), tint,
                            (uint8_t)((alpha * 238) / AN_ONE));
        gfx_stroke_round_rect(s, x, y, cw, ch, ag_px(14, scale), border,
                              (uint8_t)((alpha * 210) / AN_ONE));
    }
    gfx_draw_text_scaled(s, x + pad, y + pad, "Airnimation presets", text, 2 * ts);
    gfx_draw_text_scaled(s, x + pad, y + ag_px(64, scale), preset->name,
                         preset->kind == AN_KIND_CROSSFADE
                             ? an_mix_color(0x0090D8FFu, 0x00FFC278u, p) : text, ts);
    gfx_draw_text_scaled(s, x + pad, y + ch - ag_px(28, scale), "preset API tour",
                         an_mix_color(0x0090D8FFu, 0x00FFC278u, p), ts);
}
