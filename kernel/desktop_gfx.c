#include "kernel.h"
#include "../include/desktop_gfx.h"
#include "../include/desktop_render.h"
#include "../include/desktop_chrome.h"
#include "../include/vbe.h"
#include "../include/airgraph.h"
#include "../include/bmp.h"
#include "../include/fs.h"
#include "../include/kheap.h"
#include "../include/sprunkui.h"
#include "../include/font5x7.h"

/* DISPLAY=portrait selects a 600x750 framebuffer (Bochs DISPI limit-safe).
   The desktop scene remains 80x25 logical cells and is rasterized to pixels. */
#if defined(FLAIROS_DISPLAY_4K)
#define DISPLAY_WIDTH 3840
#define DISPLAY_HEIGHT 2160
#elif defined(FLAIROS_DISPLAY_PORTRAIT)
#define DISPLAY_WIDTH 600
#define DISPLAY_HEIGHT 750
#elif defined(FLAIROS_DISPLAY_WIDE)
#define DISPLAY_WIDTH 1024
#define DISPLAY_HEIGHT 768
#else
#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 600
#endif

static struct fb display_fb;
static struct ag_surface display_surface;
static struct ag_surface render_surface;
static void *render_pixels;
static int render_buffer_ready;
static struct ag_device display_device;
static struct ag_viewport display_viewport;
static struct sp_theme desktop_ui_theme;
static struct sp_context desktop_ui_context;
static int display_ready;
static int display_attempted;
struct desktop_bmp_asset {
    unsigned int length;
    unsigned char bytes[FS_FILE_MAX];
};
static struct desktop_bmp_asset *desktop_bmp_asset;

static struct ag_surface *desktop_render_surface(void) {
    return render_buffer_ready ? &render_surface : &display_surface;
}

static int load_desktop_bmp(const char *name, unsigned int *length,
                            struct bmp_info *info) {
    int bytes;
    if (!desktop_bmp_asset) {
        desktop_bmp_asset = (struct desktop_bmp_asset *)kmalloc(sizeof(*desktop_bmp_asset));
        if (!desktop_bmp_asset)
            return 0;
        desktop_bmp_asset->length = 0;
    }
    desktop_bmp_asset->length = 0;
    bytes = fs_kernel_read_file(name, desktop_bmp_asset->bytes, FS_FILE_MAX);
    if (bytes <= 0 || bmp_parse(desktop_bmp_asset->bytes, (unsigned int)bytes, info) != BMP_OK)
        return 0;
    desktop_bmp_asset->length = (unsigned int)bytes;
    *length = (unsigned int)bytes;
    return 1;
}

static void draw_bmp_cover(const struct bmp_info *info, unsigned int length) {
    struct ag_surface *surface = desktop_render_surface();
    int width = surface->width;
    int height = surface->height;
    int crop_x = 0, crop_y = 0;
    int visible_w = info->width;
    int visible_h = info->height;
    if ((int)info->width * height > (int)info->height * width) {
        visible_w = (int)((int)info->height * width / height);
        crop_x = (info->width - visible_w) / 2;
    } else {
        visible_h = (int)((int)info->width * height / width);
        crop_y = (info->height - visible_h) / 2;
    }
    for (int y = 0; y < height; ++y) {
        int sy = crop_y + (int)((int)y * visible_h / height);
        for (int x = 0; x < width; ++x) {
            int sx = crop_x + (int)((int)x * visible_w / width);
            unsigned int rgb;
            if (bmp_pixel(desktop_bmp_asset->bytes, length, info, sx, sy, &rgb) == BMP_OK)
                ag_put_pixel(surface, x, y, rgb);
        }
    }
}

static void desktop_ui_draw_text(struct ag_surface *surface, int32_t x,
                                 int32_t y, const char *text, uint32_t color) {
    int scale;
    if (!surface || !text) return;
    scale = surface->width >= 960 ? 2 : 1;
    for (unsigned int i = 0; text[i]; ++i) {
        const uint8_t *glyph = font5x7_glyph(text[i]);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (glyph[gy] & (1u << (4 - gx)))
                    ag_fill_rect(0, surface, x + gx * scale, y + gy * scale,
                                 scale, scale, color);
            }
        }
        x += 6 * scale;
        if (x >= surface->width) break;
    }
}

struct sp_context *desktop_gfx_ui_context(void) {
    return display_ready ? &desktop_ui_context : 0;
}

int desktop_gfx_active(void) {
    return display_ready;
}

void desktop_gfx_shutdown(void) {
    if (display_ready)
        ag_device_shutdown(&display_device);
    if (render_pixels) {
        kfree(render_pixels);
        render_pixels = 0;
    }
    render_buffer_ready = 0;
    vbe_text_mode();
    display_ready = 0;
    display_attempted = 0;
    serial_str("display: returned to VGA text mode\n");
}

int desktop_gfx_init(void) {
    if (display_ready)
        return 1;
    if (display_attempted)
        return 0;
    display_attempted = 1;

    if (!vbe_init(&display_fb, DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
        serial_str("display: framebuffer unavailable; VGA recovery mode active\n");
        return 0;
    }

    if (ag_device_init(&display_device, &display_fb, 0) != 0 ||
        ag_surface_init(&display_surface, display_fb.base,
                        display_fb.width, display_fb.height,
                        display_fb.pitch, AG_PIXEL_XRGB8888) != 0 ||
        ag_viewport_init_adaptive(&display_viewport,
                                  display_fb.width, display_fb.height,
                                  VGA_COLS, VGA_ROWS,
                                  VGA_COLS, VGA_ROWS,
                                  AG_SCALE_STRETCH) != 0) {
        ag_device_shutdown(&display_device);
        vbe_text_mode();
        serial_str("display: AirGraph setup failed; VGA recovery mode active\n");
        return 0;
    }

    /* Render into RAM first, then copy a complete frame to the scanout buffer.
       If contiguous memory is unavailable, retain the direct-framebuffer path. */
    if (display_fb.height > 0 &&
        display_fb.pitch <= 0xFFFFFFFFu / (uint32_t)display_fb.height) {
        uint32_t bytes = display_fb.pitch * (uint32_t)display_fb.height;
        render_pixels = kmalloc(bytes);
        if (render_pixels &&
            ag_surface_init(&render_surface, render_pixels,
                            display_fb.width, display_fb.height,
                            display_fb.pitch, AG_PIXEL_XRGB8888) == 0) {
            render_buffer_ready = 1;
            serial_str("display: off-screen frame buffer enabled\n");
        } else {
            if (render_pixels)
                kfree(render_pixels);
            render_pixels = 0;
            render_buffer_ready = 0;
            serial_str("display: off-screen buffer unavailable; direct rendering\n");
        }
    }

    display_ready = 1;
    sp_theme_default(&desktop_ui_theme);
    desktop_ui_context.device = &display_device;
    desktop_ui_context.surface = desktop_render_surface();
    desktop_ui_context.theme = &desktop_ui_theme;
    desktop_ui_context.draw_text = desktop_ui_draw_text;
    ag_clear(desktop_render_surface(), 0x00131A2A);

    /* Optional system-volume artwork. BOOT.BMP is shown once; WALL.BMP
       remains behind the Explorer desktop until a new wallpaper is set. */
    {
        unsigned int image_length = 0;
        struct bmp_info image_info;
        int have_boot = load_desktop_bmp("BOOT.BMP", &image_length, &image_info);
        if (have_boot) {
            draw_bmp_cover(&image_info, image_length);
            ag_present(&display_device, desktop_render_surface());
            wait_ticks(12);
        }
        if (!load_desktop_bmp("WALL.BMP", &image_length, &image_info) && have_boot)
            have_boot = load_desktop_bmp("BOOT.BMP", &image_length, &image_info);
        if (image_length)
            serial_str("display: custom BMP wallpaper loaded\n");
    }
    serial_str("display: AirGraph pixel desktop active (");
#if defined(FLAIROS_DISPLAY_4K)
    serial_str("3840x2160 4K");
#elif defined(FLAIROS_DISPLAY_PORTRAIT)
    serial_str("600x750 portrait");
#elif defined(FLAIROS_DISPLAY_WIDE)
    serial_str("1024x768 wide");
#else
    serial_str("800x600 landscape");
#endif
    serial_str(")\n");
    return 1;
}

int desktop_gfx_map_pointer(uint32_t normalized_x, uint32_t normalized_y,
                            int32_t *grid_x, int32_t *grid_y) {
    int32_t pixel_x, pixel_y;

    if (!display_ready || !grid_x || !grid_y)
        return -1;
    if (normalized_x > 65535u) normalized_x = 65535u;
    if (normalized_y > 65535u) normalized_y = 65535u;

    pixel_x = (int32_t)(((uint32_t)normalized_x * display_surface.width) >> 16);
    pixel_y = (int32_t)(((uint32_t)normalized_y * display_surface.height) >> 16);
    return ag_viewport_unmap_point(&display_viewport, pixel_x, pixel_y,
                                   grid_x, grid_y);
}

int desktop_gfx_present(const uint16_t *cells, int columns, int rows) {
    if (!display_ready)
        return -1;

    struct ag_surface *surface = desktop_render_surface();
    if (desktop_render_cells_with_wallpaper(
            &display_device, surface, cells, columns, rows,
            desktop_bmp_asset && desktop_bmp_asset->length
                ? desktop_bmp_asset->bytes : 0,
            desktop_bmp_asset ? desktop_bmp_asset->length : 0) == 0) {
        if (render_buffer_ready && ag_present(&display_device, surface) != 0)
            kernel_panic("AirGraph framebuffer present failed");
        return 0;
    }

    kernel_panic("AirGraph pixel renderer failed");
}

int desktop_gfx_draw_window(int x, int y, int width, int height,
                              const char *title, int active) {
    if (!display_ready) return -1;
    return desktop_chrome_draw(&display_device, desktop_render_surface(),
                               VGA_COLS, VGA_ROWS, x, y, width, height,
                               title, active);
}

int desktop_gfx_flush(void) {
    if (!display_ready) return -1;
    if (render_buffer_ready && ag_present(&display_device, desktop_render_surface()) != 0)
        kernel_panic("AirGraph chrome presentation failed");
    return 0;
}

