#include <stdint.h>
#include <stdio.h>
#include "sprunkui.h"
#include "airnim.h"
#include "fb.h"

volatile unsigned int ticks;
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

#define W 240
#define H 120
static uint32_t pixels[W * H];
static int text_calls;

static void stub_text(struct ag_surface *s, int32_t x, int32_t y, const char *t, uint32_t c) {
    (void)s; (void)x; (void)y; (void)t; (void)c;
    text_calls++;
}
static uint32_t at(int x, int y) { return pixels[y * W + x] & 0x00FFFFFFu; }
static void clear(void) { for (int i = 0; i < W * H; ++i) pixels[i] = 0x00141218u; }

int main(void) {
    struct sp_theme theme;
    struct sp_context ui;
    struct ag_surface surf;
    const char *tabs[3] = {"One", "Two", "Three"};
    an_id btn = an_id_str("t.btn"), tog = an_id_str("t.tog");
    uint32_t idle, hot, edge_idle;

    sp_theme_default(&theme);
    CHECK(ag_surface_init(&surf, pixels, W, H, W * 4, AG_PIXEL_XRGB8888) == 0);
    ui.device = 0; ui.surface = &surf; ui.theme = &theme; ui.draw_text = stub_text;
    ui.pointer_x = -1; ui.pointer_y = -1; ui.pointer_down = 0; ui.text_w = 6;

    /* sp_hit */
    CHECK(!sp_hit(&ui, 10, 10, 50, 20));
    ui.pointer_x = 20; ui.pointer_y = 15;
    CHECK(sp_hit(&ui, 10, 10, 50, 20));
    CHECK(!sp_hit(&ui, 30, 10, 50, 20));

    /* button: hover tints it smoothly, release returns it */
    ticks = 0; ui.pointer_x = -1; ui.pointer_y = -1;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    idle = at(14, 25);
    edge_idle = at(10, 25);
    CHECK(edge_idle != 0x00141218u);                          /* idle button reaches its left edge */
    ui.pointer_x = 20; ui.pointer_y = 20;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);   /* starts moving */
    ticks = 3;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    CHECK(an_busy(btn));
    ticks = 20;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    hot = at(14, 25);
    CHECK(hot != idle);
    CHECK(text_calls > 0);
    ui.pointer_down = 1;
    ticks = 40;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    ticks = 60;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    CHECK(at(10, 25) == 0x00141218u);                         /* pressed: button pulled in from the edge */
    ui.pointer_down = 0; ui.pointer_x = -1; ui.pointer_y = -1;
    ticks = 80;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    ticks = 120;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    CHECK(at(14, 25) == idle);

    /* with animations off a hover change is instant */
    an_set_enabled(0);
    ui.pointer_x = 20; ui.pointer_y = 20;
    clear(); sp_button_a(&ui, btn, 10, 10, 100, 30, "Go", 0);
    CHECK(at(14, 25) == hot);
    an_set_enabled(1);

    /* toggle: knob travels from left to right */
    ticks = 200;
    clear(); sp_toggle_a(&ui, tog, 10, 60, 40, 20, 0);
    CHECK(at(14, 70) == theme.color.accent_text);          /* knob on the left */
    clear(); sp_toggle_a(&ui, tog, 10, 60, 40, 20, 1);
    ticks = 215;
    clear(); sp_toggle_a(&ui, tog, 10, 60, 40, 20, 1);
    CHECK(at(14, 70) != theme.color.accent_text);          /* knob has left */
    CHECK(at(44, 70) == theme.color.accent_text);          /* and arrived on the right */

    /* tabs */
    ui.pointer_x = 150; ui.pointer_y = 5;
    CHECK(sp_tabs_hit(&ui, 0, 0, 240, 20, 3) == 1);
    ui.pointer_x = 5;
    CHECK(sp_tabs_hit(&ui, 0, 0, 240, 20, 3) == 0);
    ui.pointer_y = 50;
    CHECK(sp_tabs_hit(&ui, 0, 0, 240, 20, 3) == -1);
    clear(); sp_tabs_a(&ui, an_id_str("t.tabs"), 0, 0, 240, 20, tabs, 3, 0);
    CHECK(at(100, 19) == theme.color.border || at(100, 19) != 0x00141218u);   /* divider line */
    CHECK(at(30, 17) == theme.color.accent);                 /* indicator under tab 0 */

    /* icons */
    clear(); sp_icon_battery(&ui, 10, 100, 1, 50, 0);
    CHECK(at(12, 104) == theme.color.success);               /* filled */
    clear(); sp_icon_battery(&ui, 10, 100, 1, 10, 0);
    CHECK(at(12, 104) == theme.color.danger);
    clear(); sp_icon_battery(&ui, 10, 100, 1, 20, 0);
    CHECK(at(12, 104) == theme.color.warning);
    clear(); sp_icon_battery(&ui, 10, 100, 1, -1, 0);
    CHECK(at(12, 104) == 0x00141218u);                       /* unknown: empty outline */
    CHECK(sp_icon_battery(&ui, 10, 100, 1, 50, 1) > 25);
    clear(); sp_icon_network(&ui, 100, 100, 1, 1, 1, 1, 100);
    CHECK(at(100 + 15 + 1, 100 + 2) == theme.color.success); /* tallest bar lit */
    clear(); sp_icon_network(&ui, 100, 100, 1, 1, 0, 1, -1);
    CHECK(at(100 + 15 + 1, 100 + 2) == theme.color.border);  /* down: all bars dim */
    clear(); sp_icon_network(&ui, 100, 100, 1, 0, 0, 0, -1);
    CHECK(at(100 + 8, 100 + 1) == theme.color.muted);        /* no device: muted */

    if (failures) { printf("sprunkui anim host test: %d failure(s)\n", failures); return 1; }
    printf("sprunkui anim host test: all checks passed\n");
    return 0;
}
