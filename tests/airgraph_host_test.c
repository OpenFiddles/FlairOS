#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "airgraph.h"
#include "sprunkui.h"

#define W 8
#define H 6
#define PITCH (W * 4 + 8)

static uint8_t pixels[PITCH * H];
static uint8_t display_mem[PITCH * H];
static int fails;
static int text_calls;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t pixel_at(const uint8_t *mem, int x, int y) {
    uint32_t value;
    memcpy(&value, mem + y * PITCH + x * 4, 4);
    return value;
}
static int padding_ok(const uint8_t *mem) {
    for (int y = 0; y < H; y++)
        for (int x = W * 4; x < PITCH; x++)
            if (mem[y * PITCH + x] != 0xA5) return 0;
    return 1;
}
static void test_text(struct ag_surface *s, int32_t x, int32_t y,
                      const char *text, uint32_t color) {
    (void)s; (void)x; (void)y; (void)text; (void)color;
    text_calls++;
}
int main(void) {
    struct ag_surface s;
    struct fb fb;
    struct ag_device dev;
    struct sp_theme theme;
    struct sp_context ui;
    struct ag_viewport vp;
    struct ag_resource_info resources;
    struct ag_device gpu_dev;
    static const struct ag_backend_ops fake_gpu = {
        .name = "test-gpu", .capabilities = AG_CAP_FILL_RECT, .flags = AG_BACKEND_GPU
    };

    memset(pixels, 0xA5, sizeof pixels);
    memset(display_mem, 0xA5, sizeof display_mem);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, AG_PIXEL_XRGB8888) == 0);
    CHECK(ag_surface_init(&s, pixels, W, H, W * 4 - 1, AG_PIXEL_XRGB8888) != 0);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, (enum ag_pixel_format)99) != 0);
    CHECK(ag_surface_init(&s, pixels, W, H, PITCH, AG_PIXEL_XRGB8888) == 0);

    ag_clear(&s, 0x123456);
    CHECK(pixel_at(pixels, 0, 0) == 0x00123456);
    CHECK(pixel_at(pixels, W - 1, H - 1) == 0x00123456);
    CHECK(padding_ok(pixels));
    ag_put_pixel(&s, 2, 3, 0x00ABCDEF);
    CHECK(pixel_at(pixels, 2, 3) == 0x00ABCDEF);
    ag_put_pixel(&s, -1, 0, 0x00FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x00123456);

    ag_fill_rect(0, &s, -2, -1, 4, 3, 0x00FF0000);
    CHECK(pixel_at(pixels, 0, 0) == 0x00FF0000);
    CHECK(pixel_at(pixels, 1, 1) == 0x00FF0000);
    CHECK(pixel_at(pixels, 2, 1) == 0x00123456);
    CHECK(padding_ok(pixels));

    ag_clear(&s, 0);
    ag_frame_rect(0, &s, -1, -1, 4, 4, 0x0000FF);
    CHECK(pixel_at(pixels, 0, 0) == 0);
    CHECK(pixel_at(pixels, 2, 0) == 0x0000FF);
    CHECK(pixel_at(pixels, 0, 2) == 0x0000FF);
    CHECK(pixel_at(pixels, 1, 1) == 0);

    ag_clear(&s, 0);
    ag_blend_rect(0, &s, 0, 0, W, H, 0x80FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x00808080);
    ag_blend_rect(0, &s, 0, 0, W, H, 0x00FFFFFF);
    CHECK(pixel_at(pixels, 0, 0) == 0x00808080);

    {
        uint32_t src[9] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
        ag_blit(0, &s, -1, -1, 3, 3, src, 12);
        CHECK(pixel_at(pixels, 0, 0) == 0x55);
    }

    {
        uint32_t image_pixels[2] = {0x00112233u, 0x00445566u};
        struct image_buffer image = {image_pixels, 2, 1, 8, sizeof(image_pixels)};
        ag_clear(&s, 0);
        CHECK(ag_draw_image(0, &s, 2, 2, &image) == 0);
        CHECK(pixel_at(pixels, 2, 2) == 0x00112233u);
        CHECK(pixel_at(pixels, 3, 2) == 0x00445566u);
        CHECK(ag_draw_image(0, &s, 7, 5, &image) == 0);
        CHECK(pixel_at(pixels, 7, 5) == 0x00112233u);
        ag_clear(&s, 0);
        CHECK(ag_draw_image_scaled(0, &s, 1, 1, 4, 2, &image) == 0);
        CHECK(pixel_at(pixels, 1, 1) == 0x00112233u);
        CHECK(pixel_at(pixels, 2, 1) == 0x00112233u);
        CHECK(pixel_at(pixels, 3, 1) == 0x00445566u);
        CHECK(pixel_at(pixels, 4, 2) == 0x00445566u);
        ag_clear(&s, 0);
        CHECK(ag_draw_image_scaled(0, &s, -1, 0, 4, 2, &image) == 0);
        CHECK(pixel_at(pixels, 0, 0) == 0x00112233u);
        CHECK(pixel_at(pixels, 1, 0) == 0x00445566u);
        CHECK(ag_draw_image_scaled(0, &s, 0, 0, 0, 2, &image) != 0);
        image.capacity = 4;
        CHECK(ag_draw_image(0, &s, 0, 0, &image) != 0);
    }

    fb_init(&fb, display_mem, W, H, PITCH);
    CHECK(ag_device_init(&dev, &fb, 0) == 0);
    CHECK(ag_present(&dev, &s) == 0);
    CHECK(pixel_at(display_mem, 0, 0) == pixel_at(pixels, 0, 0));
    CHECK(padding_ok(display_mem));
    CHECK(ag_get_resource_info(&dev, &resources) == 0);
    CHECK(resources.cpu_renderer_available == 1 && resources.gpu_available == 0);
    CHECK(ag_device_init(&gpu_dev, &fb, &fake_gpu) == 0);
    CHECK(ag_get_resource_info(&gpu_dev, &resources) == 0);
    CHECK(resources.gpu_available == 1 && resources.gpu_accelerated == 1);
    CHECK(resources.cpu_renderer_available == 1);
    ag_device_shutdown(&gpu_dev);
    ag_device_shutdown(&dev);
    CHECK(ag_present(&dev, &s) != 0);
    CHECK(ag_viewport_init_adaptive(&vp, 360, 800, 800, 480, 360, 800, AG_SCALE_FIT) == 0);
    CHECK(vp.orientation == AG_ORIENTATION_PORTRAIT);
    CHECK(vp.logical_width == 360 && vp.logical_height == 800);
    {
        int32_t x, y, w, h;
        CHECK(ag_viewport_map_point(&vp, 240, 400, &x, &y) == 0);
        CHECK(x == 240 && y == 400);
        CHECK(ag_viewport_unmap_point(&vp, 240, 400, &x, &y) == 0);
        CHECK(x == 240 && y == 400);
        CHECK(ag_viewport_unmap_point(&vp, -1, 400, &x, &y) != 0);
        CHECK(ag_viewport_map_rect(&vp, 10, 20, 100, 50, &x, &y, &w, &h) == 0);
        CHECK(x == 10 && y == 20 && w == 100 && h == 50);
    }
    CHECK(ag_viewport_init_adaptive(&vp, 800, 600, 800, 480, 480, 800, AG_SCALE_FIT) == 0);
    CHECK(vp.orientation == AG_ORIENTATION_LANDSCAPE && vp.y == 60);
    CHECK(ag_viewport_init_adaptive(&vp, 360, 800, 800, 480, 360, 800, AG_SCALE_STRETCH) == 0);
    CHECK(vp.width == 360 && vp.height == 800);
    CHECK(ag_viewport_init_adaptive(&vp, 600, 750, 80, 25, 80, 25, AG_SCALE_STRETCH) == 0);
    CHECK(vp.orientation == AG_ORIENTATION_PORTRAIT);
    {
        int32_t x, y;
        CHECK(ag_viewport_map_point(&vp, 40, 12, &x, &y) == 0);
        CHECK(x == 300 && y == 360);
        CHECK(ag_viewport_unmap_point(&vp, x, y, &x, &y) == 0);
        CHECK(x == 40 && y == 12);
        CHECK(ag_viewport_unmap_point(&vp, 592, 720, &x, &y) == 0);
        CHECK(x == 79 && y == 24);
        CHECK(ag_viewport_unmap_point(&vp, 591, 719, &x, &y) == 0);
        CHECK(x == 78 && y == 23);
        CHECK(ag_viewport_unmap_point(&vp, 599, 749, &x, &y) == 0);
        CHECK(x == 79 && y == 24);
    }
    CHECK(ag_viewport_init_adaptive(&vp, 0, 800, 800, 480, 480, 800, AG_SCALE_FIT) != 0);
    CHECK(ag_viewport_init_adaptive(&vp, 8193, 600, 800, 480, 480, 800, AG_SCALE_FIT) != 0);
    CHECK(ag_viewport_init_adaptive(&vp, 8192, 600, 8193, 480, 480, 800, AG_SCALE_FIT) != 0);

    ag_clear(&s, 0);
    ag_round_rect(0, &s, 1, 1, 6, 4, 2, 0x00ABCDEF);
    CHECK(pixel_at(pixels, 1, 1) == 0);
    CHECK(pixel_at(pixels, 2, 1) == 0x00ABCDEF);
    CHECK(pixel_at(pixels, 3, 2) == 0x00ABCDEF);
    ag_round_frame(0, &s, 0, 0, 8, 6, 2, 0x0000FF);
    CHECK(pixel_at(pixels, 0, 0) == 0);
    CHECK(pixel_at(pixels, 2, 0) == 0x0000FF);

    /* Density: 4K derives 3x from a 720p design and snaps to whole pixels,
       while smaller panels clamp up to 1x and huge ones to 4x. */
    {
        uint32_t scale = 0;
        CHECK(ag_scale_q16_for(3840, 2160, 1280, 720, &scale) == 0);
        CHECK(scale == 196608u);
        CHECK(ag_scale_q16_for(1024, 768, 1280, 720, &scale) == 0);
        CHECK(scale == AG_SCALE_ONE);
        CHECK(ag_scale_q16_for(7680, 4320, 1280, 720, &scale) == 0);
        CHECK(scale == AG_SCALE_MAX);
        CHECK(ag_scale_q16_for(0, 2160, 1280, 720, &scale) != 0);
        CHECK(ag_scale_q16_for(3840, 2160, 0, 720, &scale) != 0);
        CHECK(ag_px(12, 196608u) == 36);
        CHECK(ag_px(1, 196608u) == 3);
        CHECK(ag_px(10, 98304u) == 15);
        CHECK(ag_px(0, 196608u) == 0);
        CHECK(ag_px(1, 0u) == 1);
        /* Pixel art needs whole steps: 720p=1x, 1080p=2x, 4K=3x, 8K=4x. */
        CHECK(ag_scale_step_for(1280, 720, 1280, 720) == 1);
        CHECK(ag_scale_step_for(1920, 1080, 1280, 720) == 2);
        CHECK(ag_scale_step_for(3840, 2160, 1280, 720) == 3);
        CHECK(ag_scale_step_for(7680, 4320, 1280, 720) == 4);
        CHECK(ag_scale_step_for(800, 600, 1280, 720) == 1);
        CHECK(ag_scale_step_for(0, 2160, 1280, 720) == 0);
    }
    ag_clear(&s, 0);
    ag_stroke_rect(0, &s, 0, 0, 8, 6, 1, 0x0000FF);
    CHECK(pixel_at(pixels, 0, 0) == 0x0000FF);
    CHECK(pixel_at(pixels, 7, 0) == 0x0000FF);
    CHECK(pixel_at(pixels, 0, 5) == 0x0000FF);
    CHECK(pixel_at(pixels, 4, 2) == 0);
    ag_clear(&s, 0);
    ag_stroke_rect(0, &s, 0, 0, 8, 6, 2, 0x0000FF);
    CHECK(pixel_at(pixels, 1, 1) == 0x0000FF);
    CHECK(pixel_at(pixels, 2, 2) == 0);
    ag_clear(&s, 0);
    ag_stroke_rect(0, &s, 0, 0, 8, 6, 4, 0x0000FF);
    CHECK(pixel_at(pixels, 3, 3) == 0x0000FF);
    ag_clear(&s, 0);
    ag_round_rect_scaled(0, &s, 0, 0, 8, 6, 2, 131072u, 0x00ABCDEF);
    CHECK(pixel_at(pixels, 0, 0) == 0);
    CHECK(pixel_at(pixels, 4, 3) == 0x00ABCDEF);
    CHECK(pixel_at(pixels, 4, 0) == 0x00ABCDEF);

    sp_theme_default(&theme);
    ui.device = 0; ui.surface = &s; ui.theme = &theme; ui.draw_text = test_text;
    ag_clear(&s, theme.color.background);
    sp_panel(&ui, 1, 1, 6, 4);
    sp_button(&ui, 2, 2, 4, 2, "OK", 1);
    CHECK(pixel_at(pixels, 3, 3) == theme.color.accent);
    sp_label(&ui, 0, 0, "Label", 0);
    sp_badge(&ui, 0, 0, "NEW", theme.color.success);
    sp_text_field(&ui, 0, 0, 8, 6, "x", 1);
    sp_list_item(&ui, 0, 0, 8, 6, "item", 0, 1);
    sp_toggle(&ui, 0, 0, 8, 6, 1); /* undersized: safely ignored */
    sp_checkbox(&ui, 0, 0, 6, 1);
    sp_slider(&ui, 0, 0, 8, 6, 50);
    CHECK(text_calls >= 4);

    ag_clear(&s, 0);
    sp_progress(&ui, 1, 1, 6, 1, 50); /* too short: safely ignored */
    CHECK(pixel_at(pixels, 3, 1) == 0);
    sp_progress(&ui, 1, 1, 6, 5, 150); /* clamps to 100 */
    CHECK(pixel_at(pixels, 3, 3) == theme.color.accent);
    sp_slider(&ui, 0, 0, 8, 6, 100);
    CHECK(padding_ok(pixels));

    if (!fails) puts("AirGraph host test: all checks passed");
    return fails ? 1 : 0;
}
