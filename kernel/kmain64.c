/* SPDX-License-Identifier: AGPL-3.0-only */
/* Portable 64-bit interactive kernel entry for x86_64 and aarch64 */
#include <stdint.h>
#include "../include/hal.h"
#include "../include/kmem64.h"
#include "../include/airgraph.h"
#include "../include/input64.h"
#include "../include/apk_samples.h"
#include "../include/vfs.h"
#include "../graphics/compositor/glass_fx.h"
#include "../runtime/art/art_bridge.h"
#include "../net/airp/airp_net.h"
#include "../ui/shells/shell_common.h"

int kbd_read(void) { return -1; }
void putc(char c) { hal_serial_putc(c); }
void kernel_panic(const char *msg) {
    hal_serial_puts("PANIC64: ");
    hal_serial_puts(msg ? msg : "unknown");
    hal_serial_puts("\n");
    for (;;) hal_cpu_halt();
}


#ifndef PRESET_NAME
#define PRESET_NAME "desktop"
#endif
#ifndef PRESET_W
#define PRESET_W 1024
#endif
#ifndef PRESET_H
#define PRESET_H 768
#endif
#ifndef PRESET_AIRP_ROLE
#define PRESET_AIRP_ROLE AIRP_ROLE_DUPLEX
#endif
#ifndef PRESET_TARGET_HZ
#if PRESET_W >= 3840
#define PRESET_TARGET_HZ 90u
#else
#define PRESET_TARGET_HZ 60u
#endif
#endif

static uint8_t kernel_heap_pool[8u * 1024u * 1024u] __attribute__((aligned(16)));
static uint32_t fb_backing[PRESET_W * PRESET_H] __attribute__((aligned(4096)));
static uint32_t fb_layer[PRESET_W * PRESET_H] __attribute__((aligned(4096)));

#ifdef __aarch64__
int aarch64_ramfb_init(uint64_t fb_phys, uint32_t width, uint32_t height);
#endif

static void serial_u32(uint32_t v) {
    char buf[16];
    int n = 0;
    if (v == 0) { hal_serial_putc('0'); return; }
    while (v > 0 && n < 15) { buf[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n > 0) hal_serial_putc(buf[--n]);
}

static void copy_rect_to_lfb(uint32_t *hw_lfb, uint32_t hw_pitch, const struct ag_surface *src,
                             int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    if (!hw_lfb || hw_lfb == src->pixels) return;
    int32_t x0 = rx < 0 ? 0 : rx, y0 = ry < 0 ? 0 : ry;
    int32_t x1 = rx + rw > src->width ? src->width : rx + rw;
    int32_t y1 = ry + rh > src->height ? src->height : ry + rh;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)hw_lfb + (size_t)y * hw_pitch);
        const uint32_t *srow = (const uint32_t *)((const uint8_t *)src->pixels + (size_t)y * src->pitch);
        for (int32_t x = x0; x < x1; x++) dst[x] = srow[x];
    }
}

static void restore_rect(struct ag_surface *dst, const struct ag_surface *src,
                         int32_t rx, int32_t ry, int32_t rw, int32_t rh) {
    int32_t x0 = rx < 0 ? 0 : rx, y0 = ry < 0 ? 0 : ry;
    int32_t x1 = rx + rw > dst->width ? dst->width : rx + rw;
    int32_t y1 = ry + rh > dst->height ? dst->height : ry + rh;
    for (int32_t y = y0; y < y1; y++) {
        uint32_t *drow = (uint32_t *)((uint8_t *)dst->pixels + (size_t)y * dst->pitch);
        const uint32_t *srow = (const uint32_t *)((const uint8_t *)src->pixels + (size_t)y * src->pitch);
        for (int32_t x = x0; x < x1; x++) drow[x] = srow[x];
    }
}

static void present_lfb(uint32_t *hw_lfb, uint32_t hw_pitch, const struct ag_surface *back) {
    if (!hw_lfb || hw_lfb == back->pixels) return;
    for (int32_t y = 0; y < back->height; y++) {
        uint32_t *dst = (uint32_t *)((uint8_t *)hw_lfb + (size_t)y * hw_pitch);
        const uint32_t *src = (const uint32_t *)((const uint8_t *)back->pixels + (size_t)y * back->pitch);
        uint64_t *d64 = (uint64_t *)dst; const uint64_t *s64 = (const uint64_t *)src;
        int32_t n64 = back->width / 2;
        for (int32_t x = 0; x < n64; x++) d64[x] = s64[x];
    }
}

void kmain64(uint64_t arg0, uint64_t arg1) {
    hal_early_init();
    kmem64_init(kernel_heap_pool, sizeof(kernel_heap_pool));
    (void)vfs_init();

    hal_serial_puts("flairos64: arch=");
    hal_serial_puts(hal_arch_name());
    hal_serial_puts(" preset=" PRESET_NAME "\n");

    struct ag_surface back_surf;
    back_surf.width = PRESET_W;
    back_surf.height = PRESET_H;
    back_surf.pitch = PRESET_W * 4u;
    back_surf.format = AG_PIXEL_XRGB8888;
    back_surf.pixels = fb_backing;

    uint32_t *hw_lfb = fb_backing;
    uint32_t hw_pitch = back_surf.pitch;

#if defined(__x86_64__)
    (void)arg0; (void)arg1;
    extern uint64_t x86_64_bochs_fb_init(uint32_t width, uint32_t height);
    uint64_t lfb = x86_64_bochs_fb_init(PRESET_W, PRESET_H);
    if (lfb != 0) {
        hw_lfb = (uint32_t *)(uintptr_t)lfb;
        hal_serial_puts("flairos64: x86_64 bochs LFB ready\n");
    }
#elif defined(__aarch64__)
    (void)arg0; (void)arg1;
    if (aarch64_ramfb_init((uint64_t)(uintptr_t)fb_backing, PRESET_W, PRESET_H) == 0) {
        hal_serial_puts("flairos64: aarch64 ramfb ready\n");
    } else {
        hal_serial_puts("flairos64: aarch64 ramfb fallback\n");
    }
#endif

#if defined(__aarch64__)
    art_bridge_init("arm64-v8a");
#else
    art_bridge_init("x86_64");
#endif
    int p_hello = art_pm_register_apk(hello_apk_bytes, (uint32_t)sizeof(hello_apk_bytes), 0xFFu);
    (void)art_pm_register_apk(calc_apk_bytes, (uint32_t)sizeof(calc_apk_bytes), 0xFFu);
    (void)art_pm_register_apk(sysinfo_apk_bytes, (uint32_t)sizeof(sysinfo_apk_bytes), 0xFFu);
    if (p_hello >= 0) (void)art_am_launch(p_hello);

    hal_serial_puts("flairos64: art pkgs=");
    serial_u32((uint32_t)art_pm_count());
    hal_serial_puts(" abi=");
    hal_serial_puts(art_bridge_abi());
    hal_serial_puts("\n");

    airp_net_init("FlairOS-" PRESET_NAME, PRESET_AIRP_ROLE);

    hal_serial_puts("flairos64: airp=");
    hal_serial_puts(airp_state_name(airp_session_status()->state));
    hal_serial_puts("\n");

    input64_init(back_surf.width, back_surf.height);

    struct shell_boot_info binfo;
    binfo.arch_name = hal_arch_name();
    binfo.preset_name = PRESET_NAME;
    binfo.width = back_surf.width;
    binfo.height = back_surf.height;

    struct ag_surface layer_surf = back_surf;
    layer_surf.pixels = fb_layer;

    struct input64_state in;
    int32_t last_mx = -1, last_my = -1;
    uint8_t last_btn = 0;
    int first = 1, frame = 0;

    uint32_t hz_freq = hal_timer_freq_hz();
    uint32_t frame_budget = hz_freq / PRESET_TARGET_HZ;
    uint32_t last_tick = hal_timer_ticks();
    for (;;) {
        uint32_t now_tick = hal_timer_ticks();
        input64_poll(&in);
        int ui_dirty = shell_handle_input(&in, back_surf.width, back_surf.height) |
                       shell_tick_at(now_tick, hz_freq);
        if (first || ui_dirty) {
            shell_render(&layer_surf, &binfo, frame++);
            for (int32_t y = 0; y < back_surf.height; y++) {
                uint32_t *d = (uint32_t *)((uint8_t *)back_surf.pixels + (size_t)y * back_surf.pitch);
                const uint32_t *s0 = (const uint32_t *)((const uint8_t *)layer_surf.pixels + (size_t)y * layer_surf.pitch);
                uint64_t *d64 = (uint64_t *)d; const uint64_t *s64 = (const uint64_t *)s0;
                int32_t n64 = back_surf.width / 2;
                for (int32_t x = 0; x < n64; x++) d64[x] = s64[x];
            }
            gfx_draw_cursor(&back_surf, in.mouse_x, in.mouse_y);
            if (first) {
                present_lfb(hw_lfb, hw_pitch, &back_surf);
            } else {
                struct ag_damage dmg = { 1, 0, 0, back_surf.width, back_surf.height };
                if (dmg.valid) {
                    copy_rect_to_lfb(hw_lfb, hw_pitch, &back_surf,
                                     dmg.x0, dmg.y0, dmg.x1 - dmg.x0, dmg.y1 - dmg.y0);
                }
            }
            last_mx = in.mouse_x;
            last_my = in.mouse_y;
            last_btn = in.buttons;
            if (first) {
                hal_serial_puts("flairos64: render=ok ");
                serial_u32((uint32_t)back_surf.width);
                hal_serial_putc('x');
                serial_u32((uint32_t)back_surf.height);
                hal_serial_puts("\n");
                first = 0;
            }
        } else if (in.mouse_x != last_mx || in.mouse_y != last_my || in.buttons != last_btn) {
            restore_rect(&back_surf, &layer_surf, last_mx, last_my, 12, 16);
            copy_rect_to_lfb(hw_lfb, hw_pitch, &back_surf, last_mx, last_my, 12, 16);
            gfx_draw_cursor(&back_surf, in.mouse_x, in.mouse_y);
            copy_rect_to_lfb(hw_lfb, hw_pitch, &back_surf, in.mouse_x, in.mouse_y, 12, 16);
            last_mx = in.mouse_x;
            last_my = in.mouse_y;
            last_btn = in.buttons;
            (void)0;
        }
        if (frame_budget > 0 && (now_tick - last_tick) < frame_budget) hal_io_wait();
        last_tick = now_tick;
    }
}

#ifdef __aarch64__
void kmain(void) { kmain64(0, 0); }
#endif
