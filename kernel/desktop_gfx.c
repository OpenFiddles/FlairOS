#include "kernel.h"
#include "../include/desktop_gfx.h"
#include "../include/desktop_render.h"
#include "../include/vbe.h"
#include "../include/airgraph.h"

/* DISPLAY=portrait selects a 600x750 framebuffer (Bochs DISPI limit-safe).
   The desktop scene remains 80x25 logical cells and is rasterized to pixels. */
#if defined(FLAIROS_DISPLAY_PORTRAIT)
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
static struct ag_device display_device;
static int display_ready;
static int display_attempted;

int desktop_gfx_active(void) {
    return display_ready;
}

int desktop_gfx_init(void) {
    if (display_ready)
        return 1;
    if (display_attempted)
        return 0;
    display_attempted = 1;

    if (!vbe_init(&display_fb, DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
        serial_str("display: framebuffer unavailable; VGA recovery mode active\\n");
        return 0;
    }

    if (ag_device_init(&display_device, &display_fb, 0) != 0 ||
        ag_surface_init(&display_surface, display_fb.base,
                        display_fb.width, display_fb.height,
                        display_fb.pitch, AG_PIXEL_XRGB8888) != 0) {
        ag_device_shutdown(&display_device);
        vbe_text_mode();
        serial_str("display: AirGraph setup failed; VGA recovery mode active\\n");
        return 0;
    }

    display_ready = 1;
    ag_clear(&display_surface, 0x00131A2A);
    serial_str("display: AirGraph pixel desktop active (");
#if defined(FLAIROS_DISPLAY_PORTRAIT)
    serial_str("600x750 portrait");
#elif defined(FLAIROS_DISPLAY_WIDE)
    serial_str("1024x768 wide");
#else
    serial_str("800x600 landscape");
#endif
    serial_str(")\\n");
    return 1;
}

int desktop_gfx_present(const uint16_t *cells, int columns, int rows) {
    if (!display_ready)
        return -1;

    if (desktop_render_cells(&display_device, &display_surface,
                             cells, columns, rows) == 0)
        return 0;

    display_ready = 0;
    ag_device_shutdown(&display_device);
    vbe_text_mode();
    serial_str("display: renderer fault; switched to VGA recovery mode\\n");
    return -1;
}
