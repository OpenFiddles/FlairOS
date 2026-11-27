/* Headless proof of the shipping desktop window layer.

   It builds the same ui/shells/desk_windows.c the 64-bit desktop shell uses,
   renders its scaled chrome into a real 3840x2160 surface and asserts the
   pixels WinDown's metrics promise, then dispatches the same clicks the shell
   dispatches: the scaled close button must close the window and a scaled
   title-bar click must begin a drag. Finally it takes the shell's 720p window
   table to 4K and checks every window stays on-screen and proportional.

   The i386 reference compositor is a different code path (kernel/ui/desktop.c
   with an unconfigured wd_compositor), so this test also pins the legacy
   metrics and hit regions the shell falls back to at 720p. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "airgraph.h"
#include "airnim.h"
#include "windown.h"
#include "desk_windows.h"
#include "fb.h"
#include "glass_fx.h"

volatile unsigned int ticks;

#define PANEL_W 3840
#define PANEL_H 2160

static uint32_t *pixels;
static struct ag_surface surface;
static struct fb fb;
static struct ag_device device;

static int failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static uint32_t at(int x, int y) {
    return pixels[(size_t)y * PANEL_W + (size_t)x];
}

/* The shell's window table at its 720p design coordinates. */
static struct dw_window shell_windows[8] = {
    {1, 0, 0,   36,  52, 540, 360,  36,  52, 540, 360, "Finder"},
    {0, 0, 0,  524,  66, 460, 330, 524,  66, 460, 330, "Terminal"},
    {0, 0, 0,  140, 110, 480, 330, 140, 110, 480, 330, "Notes"},
    {0, 0, 0,  240, 100, 460, 320, 240, 100, 460, 320, "Activity"},
    {0, 0, 0,  260, 120, 480, 310, 260, 120, 480, 310, "Settings"},
    {0, 0, 0,  680,  76, 300, 360, 680,  76, 300, 360, "Calculator"},
    {0, 0, 0,  180,  64, 580, 390, 180,  64, 580, 390, "Browser"},
    {0, 0, 0,  210,  70, 520, 350, 210,  70, 520, 350, "Launcher"},
};

static void fill_background(void) {
    for (size_t i = 0; i < (size_t)PANEL_W * PANEL_H; i++) pixels[i] = 0x00101828u;
}

int main(void) {
    static struct dw_state st;
    struct wd_metrics m;
    struct dw_window win;
    struct dw_window scaled[8];
    enum dw_click acted;
    int slot = 0;
    int32_t menu;

    pixels = (uint32_t *)malloc((size_t)PANEL_W * PANEL_H * 4u);
    if (!pixels) { puts("no memory for the 4K surface"); return 1; }
    CHECK(ag_surface_init(&surface, pixels, PANEL_W, PANEL_H, PANEL_W * 4,
                          AG_PIXEL_XRGB8888) == 0);
    fb_init(&fb, pixels, PANEL_W, PANEL_H, PANEL_W * 4);
    CHECK(ag_device_init(&device, &fb, 0) == 0);

    /* Unconfigured and 720p both select the legacy 1 px chrome path. */
    dw_init(&st);
    m = dw_metrics(&st);
    CHECK(m.title_h == 1 && m.inset == 1 && m.button_w == 2 && m.grip == 1);
    CHECK(dw_configure(&st, 1280, 720) == 1);
    CHECK(!dw_active(&st));
    fill_background();
    dw_draw_chrome(&st, &surface, slot, &shell_windows[slot], 1);
    CHECK(at(40, 60) == 0x00101828u);   /* density off: nothing drawn */

    /* 4K derives 3x chrome from the same table. */
    CHECK(dw_configure(&st, PANEL_W, PANEL_H) == 1);
    CHECK(dw_active(&st));
    m = dw_metrics(&st);
    CHECK(m.title_h == 36 && m.inset == 24 && m.button_w == 36);
    CHECK(m.button_gap == 18 && m.grip == 30);

    win = shell_windows[slot];
    fill_background();
    dw_draw_chrome(&st, &surface, slot, &win, 1);

    /* Title bar is 36 device pixels tall: header above, body below. */
    CHECK(at(win.x + win.w - 40, win.y + 18) == 0x0034323Au);
    CHECK(at(win.x + win.w - 40, win.y + m.title_h + 20) == 0x002B2930u);
    CHECK(at(win.x + win.w - 40, win.y + m.title_h - 1) != 0x00101828u);

    /* Traffic lights sit where the metrics put them (36 px box, 18 px gap). */
    {
        int32_t bx = win.x + m.inset;
        int32_t d = m.button_w;
        int32_t cy = win.y + 18;
        if (d > m.title_h - 2) d = m.title_h - 2;
        CHECK(at(bx + d / 2, cy) == 0x00F0605Au);              /* close */
        CHECK(at(bx + d + m.button_gap + d / 2, cy) == 0x00F2BE3Cu); /* minimize */
        CHECK(at(bx + 2 * (d + m.button_gap) + d / 2, cy) == 0x003CCB5Bu); /* zoom */
    }

    /* Resize grip: the diagonal strokes are drawn in the outline colour. */
    CHECK(at(win.x + win.w - m.inset / 2 - 1, win.y + win.h - m.inset / 2 - 1)
          == 0x00524E5Au);

    /* Window CONTENT uses the same density: design coordinates map into the
       scaled client rectangle, clipped there, with proportional insets. */
    {
        struct dw_client c = dw_client_rect(&st, &win);
        CHECK(c.x == win.x + m.inset / 2);
        CHECK(c.y == win.y + m.title_h);
        CHECK(c.w == win.w - 2 * (m.inset / 2));
        CHECK(c.h == win.h - m.title_h - m.inset / 2);
        CHECK(gfx_transform_active() == 0);
        gfx_transform_set(st.scale_q16,
                          c.x - (int32_t)(((int64_t)win.x * st.scale_q16) >> 16),
                          c.y - (int32_t)(((int64_t)win.y * st.scale_q16) >> 16),
                          c.x, c.y, c.w, c.h);
        CHECK(gfx_transform_active() == 1);
        /* A 20x12 design inset lands 60x36 device pixels inside the client. */
        gfx_fill_rect_alpha(&surface, win.x + 20, win.y + 40, 30, 12,
                            0x00CC33AAu, 255);
        CHECK(at(c.x + 60, c.y + 120) == 0x00CC33AAu);
        CHECK(at(c.x + 60 + 89, c.y + 120 + 35) == 0x00CC33AAu);
        /* One transformed design pixel fills its 3x3 device footprint, so
           text is solid instead of a sparse lattice of points. */
        gfx_blend_pixel(&surface, win.x + 5, win.y + 45, 0x00FFCC00u, 255);
        for (int32_t dy = 0; dy < 3; dy++)
            for (int32_t dx = 0; dx < 3; dx++)
                CHECK(at(c.x + 15 + dx, c.y + 135 + dy) == 0x00FFCC00u);
        /* Content drawn above the client top is clipped away by chrome. */
        gfx_fill_rect_alpha(&surface, win.x + 200, win.y + 4, 60, 20,
                            0x0011EE11u, 255);
        CHECK(at(c.x + 200, win.y + 18) == 0x0034323Au);
        gfx_transform_clear();
        CHECK(gfx_transform_active() == 0);
        /* Identity is byte-identical: the same draw lands on the same pixel. */
        gfx_fill_rect_alpha(&surface, win.x + 20, win.y + 40, 2, 2,
                            0x00FFFFFFu, 255);
        CHECK(at(win.x + 20, win.y + 40) == 0x00FFFFFFu);
    }

    /* The shell's dispatch entry point, not just dw_click: a 4K close click
       at the scaled button centre closes through it. */
    {
        int order[8] = {7, 6, 5, 4, 3, 2, 1, 0};
        int hit_slot = -1;
        int32_t bx = win.x + m.inset;
        int32_t d = m.button_w;
        if (d > m.title_h - 2) d = m.title_h - 2;
        win.open = 1;
        CHECK(dw_dispatch_click(&st, &win, order, 8, bx + d / 2, win.y + 18,
                                &hit_slot, &acted) == 1);
        CHECK(acted == DW_CLICK_CLOSE);
        CHECK(hit_slot == slot);
        CHECK(win.open == 0);
    }

    /* A click on the scaled title bar starts a drag through the same entry. */
    {
        int order[8] = {7, 6, 5, 4, 3, 2, 1, 0};
        int hit_slot = -1;
        win.open = 1;
        win.x = shell_windows[slot].x;
        win.y = shell_windows[slot].y;
        win.w = shell_windows[slot].w;
        win.h = shell_windows[slot].h;
        CHECK(dw_dispatch_click(&st, &win, order, 8, win.x + win.w - 120,
                                win.y + 10, &hit_slot, &acted) == 1);
        CHECK(acted == DW_CLICK_DRAG);
        CHECK(hit_slot == slot);
        CHECK(st.drag_id == slot);
    }
    {
        int32_t before_x = win.x, before_y = win.y;
        CHECK(dw_drag_to(&st, &win, slot, win.x + win.w - 120 + 60, win.y + 10 + 30) == 1);
        CHECK(win.x == before_x + 60);
        /* Dragging into the menu bar clamps to its scaled height (102 px). */
        CHECK(win.y == ag_px(34, st.scale_q16));
        CHECK(win.y > before_y);
        CHECK(dw_end_drag(&st) == 1);
    }

    /* A client click is not consumed by chrome. */
    CHECK(dw_click(&st, &win, slot, win.x + 400, win.y + 300) == DW_CLICK_CLIENT);

    /* A first boot directly at 4K lays out the 720p table at 3x instead of
       leaving 540 px windows stamped in the corner. */
    {
        struct dw_state fresh;
        dw_init(&fresh);
        CHECK(dw_configure(&fresh, PANEL_W, PANEL_H) == 1);
        memcpy(scaled, shell_windows, sizeof scaled);
        CHECK(dw_layout_from_design(&fresh, scaled, 8, 1280, 720,
                                    PANEL_W, PANEL_H) == 0);
        CHECK(scaled[0].w == 1620 && scaled[0].h == 1080);
        CHECK(scaled[0].x == 108 && scaled[0].y == 156);
        CHECK(scaled[1].x + scaled[1].w <= PANEL_W);
        CHECK(scaled[0].y >= ag_px(34, wd_scale_q16(&fresh.wd)));
        /* 720p and below keep the historical table untouched. */
        dw_init(&fresh);
        CHECK(dw_configure(&fresh, 1280, 720) == 1);
        memcpy(scaled, shell_windows, sizeof scaled);
        CHECK(dw_layout_from_design(&fresh, scaled, 8, 1280, 720,
                                    1280, 720) == 0);
        CHECK(scaled[0].w == 540 && scaled[0].h == 360);
    }

    /* 720p -> 4K: every window is scaled in proportion and left on-screen. */
    memcpy(scaled, shell_windows, sizeof scaled);
    {
        struct dw_state grow;
        dw_init(&grow);
        CHECK(dw_configure(&grow, 1280, 720) == 1);
        CHECK(dw_rescale(&grow, scaled, 8, 1280, 720, PANEL_W, PANEL_H) == 0);
        CHECK(dw_active(&grow));
        menu = ag_px(34, wd_scale_q16(&grow.wd));
        for (int i = 0; i < 8; i++) {
            const struct dw_window *before = &shell_windows[i];
            const struct dw_window *after = &scaled[i];
            int32_t dw3 = before->w * 3;
            int32_t dh3 = before->h * 3;
            CHECK(after->w >= dw3 - 1 && after->w <= dw3 + 1);
            CHECK(after->h >= dh3 - 1 && after->h <= dh3 + 1);
            CHECK(after->x >= 0 && after->y >= menu);
            CHECK(after->x + after->w <= PANEL_W);
            CHECK(after->y + after->h <= PANEL_H);
        }
    }

    ag_device_shutdown(&device);
    free(pixels);
    if (failures) {
        printf("desk windows host test: %d failure(s)\n", failures);
        return 1;
    }
    puts("desktop density host test: all checks passed");
    return 0;
}
