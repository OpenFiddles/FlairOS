#include "kernel.h"
#include "../include/desktop_gfx.h"
#include "../include/fs.h"
#include "../include/desktop_render.h"
#include "../include/desktop_chrome.h"
#include "../include/vbe.h"
#include "../include/airgraph.h"
#include "../include/bmp.h"
#include "../include/fs.h"
#include "../include/kheap.h"
#include "../include/sprunkui.h"
#include "../include/airnim.h"
#include "../include/status.h"
#include "../include/font5x7.h"
#include "../include/cursor.h"
#include "../include/vfs.h"
#include "../include/drv_net.h"
#include "../include/hwinfo.h"

void pointer_set_display(int width, int height);

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
static unsigned int active_profile =
#if defined(FLAIROS_DEVICE_TV) || defined(FLAIROS_DISPLAY_4K)
    3u;
#elif defined(FLAIROS_DEVICE_PHONE) || defined(FLAIROS_DEVICE_TABLET) || defined(FLAIROS_DISPLAY_PORTRAIT)
    1u;
#elif defined(FLAIROS_DEVICE_LAPTOP) || defined(FLAIROS_DISPLAY_WIDE)
    2u;
#else
    0u;
#endif
static unsigned int touch_mode =
#if defined(FLAIROS_DEVICE_PHONE) || defined(FLAIROS_DEVICE_TABLET)
    1u;
#else
    0u;
#endif

static const struct ag_backend_ops lfb_backend_ops = {
    .name = "bochs-lfb", .capabilities = AG_CAP_BLIT, .flags = 0u
};
static const struct ag_backend_ops gpu_probe_ops = {
    .name = "pci-gpu", .capabilities = AG_CAP_BLIT | AG_CAP_FILL_RECT, .flags = AG_BACKEND_GPU
};

static const struct ag_backend_ops *probe_display_backend(void) {
    const struct hwinfo_inventory *hw = hwinfo_get();
    if (hw) {
        for (uint32_t i = 0; i < hw->pci_count; ++i) {
            uint16_t vid = hw->pci[i].vendor;
            uint16_t did = hw->pci[i].device;
            if ((vid == 0x1AF4u && did == 0x1050u) || /* VirtIO-GPU */
                (vid == 0x15ADu && did == 0x0405u)) { /* VMware SVGA II */
                serial_str("display: GPU backend probed (VirtIO/SVGA PCI)\n");
                return &gpu_probe_ops;
            }
        }
    }
    serial_str("display: GPU probe: Bochs/QEMU LFB (CPU AirGraph renderer)\n");
    return &lfb_backend_ops;
}

int desktop_gfx_profile_allowed(unsigned int profile) {
#if defined(FLAIROS_DEVICE_TV)
    return profile == 3u || profile == 4u;
#elif defined(FLAIROS_DEVICE_PHONE)
#ifdef FLAIROS_SHELL_MOBILE
    return profile == 1u; /* Mobile shell is portrait-only for this milestone. */
#else
    return profile == 1u || profile == 0u; /* Portrait + Landscape rotation */
#endif
#elif defined(FLAIROS_DEVICE_TABLET)
    return profile == 1u || profile == 2u; /* Portrait + Wide rotation */
#elif defined(FLAIROS_DEVICE_LAPTOP)
    return profile == 2u || profile == 0u;
#else
    return profile <= 4u;
#endif
}

static void profile_dims(unsigned int profile, int32_t *w, int32_t *h) {
    if (profile == 1u) { *w = 600; *h = 750; }
    else if (profile == 2u) { *w = 1024; *h = 768; }
    else if (profile == 3u) { *w = 1280; *h = 720; }
    else if (profile == 4u) { *w = 3840; *h = 2160; }
    else { *w = 800; *h = 600; }
}

static int vbe_open_best(struct fb *fb, int32_t req_w, int32_t req_h) {
    if (vbe_init(fb, req_w, req_h)) return 1;
    if (req_w == 3840 && req_h == 2160) {
        if (vbe_init(fb, 1920, 1080)) {
            serial_str("display: 4K VRAM insufficient; using 1920x1080 scanout + RAM 4K buffer\n");
            return 1;
        }
        if (vbe_init(fb, 1280, 720)) {
            serial_str("display: 4K VRAM insufficient; using 1280x720 scanout + RAM 4K buffer\n");
            return 1;
        }
        if (vbe_init(fb, 800, 600)) {
            serial_str("display: 4K VRAM insufficient; using 800x600 scanout + RAM 4K buffer\n");
            return 1;
        }
    }
    return 0;
}

static void setup_render_surface(int32_t logical_w, int32_t logical_h) {
    void *new_pixels = 0;
    if (render_pixels) {
        kfree(render_pixels);
        render_pixels = 0;
        render_buffer_ready = 0;
    }
    if (logical_w > 0 && logical_h > 0 &&
        (uint32_t)logical_w <= 0x3FFFFFFFu / 4u &&
        (uint32_t)logical_w * 4u <= 0xFFFFFFFFu / (uint32_t)logical_h) {
        uint32_t pitch = (uint32_t)logical_w * 4u;
        new_pixels = kmalloc(pitch * (uint32_t)logical_h);
        if (new_pixels &&
            ag_surface_init(&render_surface, new_pixels, logical_w, logical_h,
                            pitch, AG_PIXEL_XRGB8888) == 0) {
            render_pixels = new_pixels;
            render_buffer_ready = 1;
            return;
        }
        if (new_pixels) kfree(new_pixels);
    }
    if (display_fb.height > 0 &&
        display_fb.pitch <= 0xFFFFFFFFu / (uint32_t)display_fb.height) {
        new_pixels = kmalloc(display_fb.pitch * (uint32_t)display_fb.height);
        if (new_pixels &&
            ag_surface_init(&render_surface, new_pixels,
                            display_fb.width, display_fb.height,
                            display_fb.pitch, AG_PIXEL_XRGB8888) == 0) {
            render_pixels = new_pixels;
            render_buffer_ready = 1;
            return;
        }
        if (new_pixels) kfree(new_pixels);
    }
}
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

unsigned int desktop_gfx_get_profile(void) { return active_profile; }
unsigned int desktop_gfx_get_touch(void) { return touch_mode; }

int desktop_gfx_set_touch(unsigned int enabled) {
    if (enabled > 1u) return -1;
    touch_mode = enabled;
    return 0;
}

unsigned int desktop_gfx_get_anim(void) { return an_enabled() ? 1u : 0u; }

/* 0/1 turn animations off/on; 2/3 forbid/allow the GPU backend. */
int desktop_gfx_set_anim(unsigned int op) {
    if (op <= 1u) { an_set_enabled((int)op); return 0; }
    if (op <= 3u) { an_set_gpu_allowed(op == 3u); return 0; }
    return -1;
}

/* bit0 animations on, bit1 GPU in use, bit2 GPU allowed, bit3 GPU backend present */
unsigned int desktop_gfx_anim_info(void) {
    return (an_enabled() ? 1u : 0u) | (an_using_gpu() ? 2u : 0u) |
           (an_gpu_allowed() ? 4u : 0u) | (an_gpu_present() ? 8u : 0u);
}

static void desktop_power_query(int *bat_present, int *ac_online, int *bat_pct) {
    struct power_info pi;
    *bat_present = power_read(&pi) ? 1 : 0;
    /* No battery reported means mains power; with a battery, only "charging" proves AC. */
    *ac_online = !*bat_present || pi.charging == 1;
    *bat_pct = (*bat_present && pi.percent >= 0) ? pi.percent : 100;
}

unsigned int desktop_gfx_status_flags(void) {
    struct net_device *nd = net_default();
    struct ag_resource_info ri;
    int bat_present = 0, ac_online = 1, bat_pct = 100;
    unsigned int flags = 0u;
    uint32_t vram = vbe_vram_mb();
    if (nd) {
        flags |= 1u;
        if (net_link_up(nd)) flags |= 2u;
    }
    desktop_power_query(&bat_present, &ac_online, &bat_pct);
    if (bat_present) flags |= 4u;
    if (ac_online) flags |= 8u;
    flags |= ((unsigned int)(bat_pct & 0x7Fu) << 4);
    if (display_ready && ag_get_resource_info(&display_device, &ri) == 0 && ri.gpu_available)
        flags |= (1u << 11);
    if (vram > 255u) vram = 255u;
    flags |= ((vram & 0xFFu) << 12);
    if (render_buffer_ready &&
        (render_surface.width != display_surface.width ||
         render_surface.height != display_surface.height))
        flags |= (1u << 20);
    return flags;
}

int desktop_gfx_set_profile(unsigned int profile) {
    int32_t req_w, req_h;
    struct fb new_fb;
    void *new_pixels = 0;
    if (!desktop_gfx_profile_allowed(profile)) return -1;
    if (!display_ready) {
        active_profile = profile;
        return 0;
    }
    if (profile == active_profile) return 0;
    (void)new_pixels;
    profile_dims(profile, &req_w, &req_h);
    if (!vbe_open_best(&new_fb, req_w, req_h))
        return -1;
    display_fb = new_fb;
    ag_surface_init(&display_surface, display_fb.base,
                    display_fb.width, display_fb.height,
                    display_fb.pitch, AG_PIXEL_XRGB8888);
    setup_render_surface(req_w, req_h);
    desktop_ui_context.surface = desktop_render_surface();
    ag_viewport_init_adaptive(&display_viewport,
                              desktop_ui_context.surface->width,
                              desktop_ui_context.surface->height,
                              VGA_COLS, VGA_ROWS,
                              VGA_COLS, VGA_ROWS,
                              AG_SCALE_STRETCH);
    pointer_set_display(desktop_ui_context.surface->width,
                        desktop_ui_context.surface->height);
    active_profile = profile;
    return 0;
}


int desktop_gfx_rotate(void) {
#if defined(FLAIROS_DEVICE_TV)
    return -1; /* TV profile is fixed landscape */
#elif defined(FLAIROS_DEVICE_TABLET)
    return desktop_gfx_set_profile(active_profile == 1u ? 2u : 1u);
#else
    return desktop_gfx_set_profile(active_profile == 1u ? 0u : 1u);
#endif
}

int desktop_gfx_set_accent(unsigned int accent) {
    if (!display_ready || accent > 1u) return -1;
    desktop_ui_theme.color.accent = accent ? 0x000F9D8Au : 0x006750A4u;
    desktop_ui_theme.color.accent_text = 0x00FFFFFFu;
    return 0;
}

void desktop_gfx_shutdown(void) {
    if (display_ready) {
        ag_clear(&display_surface, 0);
        ag_device_shutdown(&display_device);
    }
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

    {
        unsigned char cfg[5] = {0, 0, 0, 1, 1};
        int n = fs_kernel_read_file("UI.CFG", cfg, 5);
        if (n >= 2 && desktop_gfx_profile_allowed(cfg[1])) active_profile = cfg[1];
        if (n >= 3 && cfg[2] <= 1u) touch_mode = cfg[2];
        if (n >= 4 && cfg[3] <= 1u) an_set_enabled(cfg[3]);
        if (n >= 5 && cfg[4] <= 1u) an_set_gpu_allowed(cfg[4]);
    }
    {
        int32_t req_w = DISPLAY_WIDTH, req_h = DISPLAY_HEIGHT;
        profile_dims(active_profile, &req_w, &req_h);
        if (!vbe_open_best(&display_fb, req_w, req_h)) {
            serial_str("display: fatal: framebuffer initialization failed: ");
            serial_str(vbe_last_error());
            serial_str("; refusing VGA desktop fallback\n");
            return 0;
        }
        if (ag_device_init(&display_device, &display_fb, probe_display_backend()) != 0 ||
            ag_surface_init(&display_surface, display_fb.base,
                            display_fb.width, display_fb.height,
                            display_fb.pitch, AG_PIXEL_XRGB8888) != 0) {
            ag_device_shutdown(&display_device);
            vbe_text_mode();
            serial_str("display: fatal: AirGraph initialization failed; refusing VGA desktop fallback\n");
            return 0;
        }
        an_init(&display_device);
        setup_render_surface(req_w, req_h);
        if (render_buffer_ready)
            serial_str("display: off-screen frame buffer enabled\n");
        else
            serial_str("display: off-screen buffer unavailable; direct rendering\n");
        if (ag_viewport_init_adaptive(&display_viewport,
                                      desktop_render_surface()->width,
                                      desktop_render_surface()->height,
                                      VGA_COLS, VGA_ROWS,
                                      VGA_COLS, VGA_ROWS,
                                      AG_SCALE_STRETCH) != 0) {
            ag_device_shutdown(&display_device);
            vbe_text_mode();
            return 0;
        }
    }

    display_ready = 1;
    sp_theme_default(&desktop_ui_theme);
    {
        unsigned char accent = 0;
        if (fs_kernel_read_file("UI.CFG", &accent, 1) == 1 && accent <= 1)
            desktop_ui_theme.color.accent = accent ? 0x000F9D8Au : 0x006750A4u;
    }
    desktop_ui_context.device = &display_device;
    desktop_ui_context.surface = desktop_render_surface();
    desktop_ui_context.theme = &desktop_ui_theme;
    desktop_ui_context.draw_text = desktop_ui_draw_text;
    ag_clear(desktop_render_surface(), 0x00141218);

    pointer_set_display(desktop_render_surface()->width, desktop_render_surface()->height);
    {
        unsigned int cursor_length = 0;
        struct bmp_info cursor_info;
        cursor_reset_custom();
        if (load_desktop_bmp("CURSOR.BMP", &cursor_length, &cursor_info) &&
            cursor_load_bmp(desktop_bmp_asset->bytes, cursor_length))
            serial_str("display: custom BMP cursor loaded\n");
    }

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

    pixel_x = (int32_t)(((uint32_t)normalized_x * desktop_render_surface()->width) >> 16);
    pixel_y = (int32_t)(((uint32_t)normalized_y * desktop_render_surface()->height) >> 16);
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



static void desktop_shell_icon(struct sp_context *ui, struct ag_surface *surface,
                               int kind, int x, int y, int scale) {
    uint32_t primary = ui->theme->color.accent;
    uint32_t ink = ui->theme->color.text;
    uint32_t muted = ui->theme->color.muted;
    int u = scale < 1 ? 1 : scale;
    if (kind == 0) { /* Welcome / home */
        ag_round_rect(ui->device, surface, x+2*u, y+8*u, 23*u, 16*u, 4*u, primary);
        ag_round_rect(ui->device, surface, x+4*u, y+3*u, 12*u, 8*u, 3*u, primary);
        ag_fill_rect(ui->device, surface, x+5*u, y+12*u, 16*u, 2*u, 0x00EADDFF);
    } else if (kind == 1) { /* Clock */
        ag_round_frame(ui->device, surface, x+3*u, y+2*u, 22*u, 22*u, 11*u, primary);
        ag_fill_rect(ui->device, surface, x+13*u, y+6*u, 2*u, 8*u, ink);
        ag_fill_rect(ui->device, surface, x+13*u, y+13*u, 7*u, 2*u, ink);
        ag_fill_rect(ui->device, surface, x+12*u, y+12*u, 4*u, 4*u, primary);
    } else if (kind == 2) { /* Terminal */
        ag_round_rect(ui->device, surface, x+1*u, y+3*u, 27*u, 20*u, 5*u, 0x003B3842);
        ag_fill_rect(ui->device, surface, x+7*u, y+9*u, 2*u, 2*u, 0x00D0BCFF);
        ag_fill_rect(ui->device, surface, x+9*u, y+11*u, 2*u, 2*u, 0x00D0BCFF);
        ag_fill_rect(ui->device, surface, x+7*u, y+13*u, 2*u, 2*u, 0x00D0BCFF);
        ag_fill_rect(ui->device, surface, x+14*u, y+15*u, 8*u, 2*u, muted);
    } else if (kind == 3) { /* Files */
        ag_round_rect(ui->device, surface, x+2*u, y+7*u, 24*u, 17*u, 4*u, 0x007FA8D8);
        ag_round_rect(ui->device, surface, x+4*u, y+3*u, 12*u, 8*u, 3*u, 0x007FA8D8);
        ag_fill_rect(ui->device, surface, x+5*u, y+13*u, 17*u, 2*u, 0x00D6E8FA);
    } else { /* Settings */
        ag_round_frame(ui->device, surface, x+4*u, y+3*u, 20*u, 20*u, 10*u, primary);
        ag_round_rect(ui->device, surface, x+10*u, y+9*u, 8*u, 8*u, 4*u, primary);
        ag_round_rect(ui->device, surface, x+12*u, y+11*u, 4*u, 4*u, 2*u, ui->theme->color.surface);
        for (int a=0; a<4; ++a) {
            ag_fill_rect(ui->device, surface, x+(13+((a==0)?-1:(a==1)?1:0))*u,
                         y+(a==2?0:a==3?21:2)*u, 3*u, 3*u, primary);
        }
    }
}

/* ---- Status cache, quick-settings panel and animated shell ---- */
#define PANEL_COLS 26
#define PANEL_ROWS 11

static int panel_open;
static int app_context_open;
static int app_context_y;
static char app_context_title[VFS_NAME_MAX];
static int cluster_col = VGA_COLS - 27;
static struct power_info pwr_cache;
static struct link_info lnk_cache;
static uint32_t status_stamp;
static uint8_t status_valid;

static void status_refresh(void) {
    if (status_valid && (ticks - status_stamp) < 25u) return;
    (void)power_read(&pwr_cache);
    (void)link_read(&lnk_cache);
    status_stamp = ticks;
    status_valid = 1u;
}

static unsigned int sc_len(const char *s) { unsigned int n = 0; while (s[n]) ++n; return n; }
static void sc_cat(char *d, const char *s) {
    unsigned int n = sc_len(d), i = 0;
    while (s[i]) { d[n + i] = s[i]; ++i; }
    d[n + i] = 0;
}
static void sc_uint(char *d, unsigned int v) {
    char t[8];
    int n = 0;
    unsigned int l = sc_len(d);
    if (!v) t[n++] = '0';
    while (v && n < 7) { t[n++] = (char)('0' + v % 10u); v /= 10u; }
    while (n) d[l++] = t[--n];
    d[l] = 0;
}

static void ui_sync_pointer(struct sp_context *ui, int cw, int ch, int tw) {
    int32_t mx = -1, my = -1;
    if (desktop_gfx_map_pointer(cursor_norm_x, cursor_norm_y, &mx, &my) != 0) { mx = -1; my = -1; }
    ui->pointer_x = mx * cw + cw / 2;
    ui->pointer_y = my * ch + ch / 2;
    ui->pointer_down = (mouse_buttons & 1u) != 0;
    ui->text_w = tw;
}

static void desktop_draw_net(struct sp_context *ui, struct ag_surface *surface,
                             int x, int y, int scale) {
    int u = scale < 1 ? 1 : scale;
    const char *name;
    uint32_t col;
    status_refresh();
    sp_icon_network(ui, x, y, u, lnk_cache.present, lnk_cache.up,
                    lnk_cache.wireless, lnk_cache.signal);
    col = !lnk_cache.present ? ui->theme->color.muted :
          (lnk_cache.up ? ui->theme->color.success : ui->theme->color.warning);
    name = !lnk_cache.present ? "NO NET" :
           (lnk_cache.up ? (lnk_cache.wireless ? "WIFI" : "ETH")
                         : (lnk_cache.wireless ? "NO WIFI" : "NO LINK"));
    ui->draw_text(surface, x + 22 * u, y + 1 * u, name, col);
}

static void desktop_draw_battery(struct sp_context *ui, struct ag_surface *surface,
                                 int x, int y, int scale) {
    int u = scale < 1 ? 1 : scale;
    status_refresh();
    if (pwr_cache.present) {
        sp_icon_battery(ui, x, y, u, pwr_cache.percent, pwr_cache.charging == 1);
    } else {
        /* No battery reported: a machine on mains power. */
        uint32_t col = ui->theme->color.muted;
        ag_round_frame(ui->device, surface, x, y, 19*u, 10*u, 3*u, col);
        ag_fill_rect(ui->device, surface, x+19*u, y+3*u, 2*u, 4*u, col);
        ag_fill_rect(ui->device, surface, x+3*u, y+2*u, 13*u, 6*u, ui->theme->color.success);
        ui->draw_text(surface, x+25*u, y+1*u, "AC PWR", col);
    }
}

static unsigned int next_profile(void) {
    static const unsigned char ladder[4] = {0u, 2u, 3u, 4u};
    unsigned int i, cur = 0u;
    for (i = 0u; i < 4u; ++i) if (ladder[i] == active_profile) cur = i;
    for (i = 1u; i <= 4u; ++i) {
        unsigned int p = ladder[(cur + i) % 4u];
        if (desktop_gfx_profile_allowed(p)) return p;
    }
    return active_profile;
}

int desktop_gfx_status_hit(int mx) { return mx >= cluster_col; }
void desktop_gfx_panel_set(int open) { panel_open = open ? 1 : 0; }
int desktop_gfx_panel_open(void) { return panel_open; }

/* Returns 0 outside the panel, 1 when the click was used, 2 for "Open Settings". */
int desktop_gfx_panel_click(int mx, int my) {
    int x0 = VGA_COLS - 1 - PANEL_COLS, y0 = VGA_ROWS - 1 - PANEL_ROWS, r;
    if (!panel_open || !display_ready) return 0;
    if (mx < x0 || mx >= x0 + PANEL_COLS || my < y0 || my >= y0 + PANEL_ROWS) return 0;
    r = my - y0;
    if (r == 5) (void)desktop_gfx_set_profile(next_profile());
    else if (r == 6) an_set_enabled(!an_enabled());
    else if (r == 7) an_set_gpu_allowed(!an_gpu_allowed());
    else if (r == 10) return 2;
    return 1;
}

static void desktop_draw_panel(struct sp_context *ui, struct ag_surface *surface,
                               int cw, int ch, int sx, int sy, const char *clock_text,
                               int32_t p) {
    int tw = ui->text_w > 0 ? ui->text_w : 6;
    int px = (VGA_COLS - 1 - PANEL_COLS) * cw, pw = PANEL_COLS * cw, ph = PANEL_ROWS * ch;
    int py = (VGA_ROWS - 1 - PANEL_ROWS) * ch + (AN_ONE - p) * (3 * ch) / AN_ONE;
    int ty = (ch - 7 * sy) / 2, bw, iw;
    char line[40];
    const char *comp;
    status_refresh();
    sp_panel(ui, px, py, pw, ph);
    sp_label(ui, px + cw, py + ty, "Quick settings", 0);
    if (clock_text) sp_label(ui, px + pw - cw - 8 * tw, py + ty, clock_text, 1);
    sp_separator(ui, px + cw / 2, py + ch, pw - cw);

    iw = sp_icon_network(ui, px + cw, py + 2 * ch + (ch - 13 * sx) / 2, sx,
                         lnk_cache.present, lnk_cache.up, lnk_cache.wireless, lnk_cache.signal);
    line[0] = 0;
    if (!lnk_cache.present) sc_cat(line, "No network device");
    else if (!lnk_cache.up) sc_cat(line, lnk_cache.wireless ? "Wi-Fi not connected" : "Ethernet: no link");
    else if (lnk_cache.wireless) {
        sc_cat(line, "Wi-Fi ");
        if (lnk_cache.signal >= 0) { sc_uint(line, (unsigned int)lnk_cache.signal); sc_cat(line, "%"); }
        else sc_cat(line, "connected");
    } else sc_cat(line, "Ethernet connected");
    sp_label(ui, px + cw + iw + 8 * sx, py + 2 * ch + ty, line, 0);

    iw = sp_icon_battery(ui, px + cw, py + 3 * ch + (ch - 10 * sx) / 2, sx,
                         pwr_cache.present ? pwr_cache.percent : -1, pwr_cache.charging == 1);
    line[0] = 0;
    if (!pwr_cache.present) sc_cat(line, "No battery");
    else if (pwr_cache.charging == 1) sc_cat(line, "Charging");
    else if (pwr_cache.charging == 0) sc_cat(line, "On battery");
    sp_label(ui, px + cw + iw + 8 * sx, py + 3 * ch + ty, line, 1);
    sp_separator(ui, px + cw / 2, py + 4 * ch, pw - cw);

    sp_label(ui, px + cw, py + 5 * ch + ty, "Display", 0);
    line[0] = 0;
    sc_uint(line, (unsigned int)surface->width); sc_cat(line, "x");
    sc_uint(line, (unsigned int)surface->height);
    bw = pw / 2 - cw;
    sp_button_a(ui, an_id_str("panel.display"), px + pw - cw - bw, py + 5 * ch + 3 * sy,
                bw, ch - 6 * sy, line, 0);

    sp_label(ui, px + cw, py + 6 * ch + ty, "Animations", 0);
    sp_toggle_a(ui, an_id_str("panel.anim"), px + pw - cw - 36 * sx, py + 6 * ch + 4 * sy,
                36 * sx, ch - 8 * sy, an_enabled());
    sp_label(ui, px + cw, py + 7 * ch + ty, "Use GPU if present", 0);
    sp_toggle_a(ui, an_id_str("panel.gpu"), px + pw - cw - 36 * sx, py + 7 * ch + 4 * sy,
                36 * sx, ch - 8 * sy, an_gpu_allowed());
    comp = an_using_gpu() ? "Compositing: GPU"
         : (an_gpu_present() ? "Compositing: CPU (GPU off)" : "Compositing: CPU (no GPU driver)");
    sp_label(ui, px + cw, py + 8 * ch + ty, comp, 1);
    sp_separator(ui, px + cw / 2, py + 9 * ch, pw - cw);
    sp_button_a(ui, an_id_str("panel.settings"), px + cw, py + 10 * ch + 3 * sy,
                pw - 2 * cw, ch - 6 * sy, "Open Settings", 1);

}

static int desktop_is_apk_name(const char *name) {
    int n = 0;
    while (name && name[n]) n++;
    return n >= 4 && name[n-4] == '.' && name[n-3] == 'a' &&
           name[n-2] == 'p' && name[n-1] == 'k';
}

static int desktop_apk_menu_items(char names[4][VFS_NAME_MAX]) {
    struct vfs_dirent ent;
    int count = 0;
    for (unsigned int i = 0; i < 16 && count < 4; ++i) {
        if (vfs_readdir("/apps", i, &ent) != 0) break;
        if (ent.type != VFS_TYPE_FILE || !desktop_is_apk_name(ent.name)) continue;
        for (int j = 0; j + 1 < VFS_NAME_MAX && ent.name[j]; ++j)
            names[count][j] = ent.name[j], names[count][j+1] = 0;
        count++;
    }
    return count;
}

void desktop_gfx_set_app_context(int open, int y, const char *title) {
    int i = 0;
    app_context_open = open ? 1 : 0;
    app_context_y = y;
    while (title && title[i] && i + 1 < VFS_NAME_MAX) {
        app_context_title[i] = title[i];
        i++;
    }
    app_context_title[i] = 0;
}

int desktop_gfx_draw_shell(int start_open, const char *clock_text) {
    struct ag_surface *surface = desktop_render_surface();
    struct sp_context *ui = &desktop_ui_context;
    int cw, ch, sx, sy, w, h, bar_y, tw;
    int clock_x, bat_x, net_x, cluster_px;
    char apk_names[4][VFS_NAME_MAX] = {{0}};
    int apk_count = desktop_apk_menu_items(apk_names);
    int menu_count = 7 + apk_count;
    int menu_height = menu_count + 2;
    int32_t sp_p, pn_p;
    static const int icon_rows[5] = {3, 5, 7, 9, 11};
    static const char *const labels[5] = {"Welcome", "Clock", "Terminal", "Files", "Settings"};
    static const char *const subtitles[5] = {
        "Your workspace", "Time & date", "Command line", "Browse storage", "Personalize"
    };
    static const char *const menu[7] = {
        "Welcome", "Clock", "Terminal", "Files", "Python", "Settings", "Notes"
    };
    if (!display_ready || !surface || !ui->theme) return -1;
    w = surface->width;
    h = surface->height;
    cw = w / VGA_COLS;
    ch = h / VGA_ROWS;
    sx = w / 800; if (sx < 1) sx = 1;
    sy = h / 600; if (sy < 1) sy = 1;
    tw = w >= 960 ? 12 : 6;
    if (cw < 1 || ch < 1) return -1;
    ui_sync_pointer(ui, cw, ch, tw);

    /* Material 3 desktop header: elevated surface, workspace context, live status. */
    sp_panel(ui, 12*sx, 6*sy, w-24*sx, 38*sy);
    sp_label(ui, 28*sx, 17*sy, "FlairOS", 0);
    sp_label(ui, 28*sx, 27*sy, "A calm place to get things done", 1);
    sp_label(ui, 230*sx, 22*sy, "WORKSPACE 1", 1);
    sp_badge(ui, w-160*sx, 15*sy, "AIRGRAPH READY", ui->theme->color.success);

    /* Five consistent app tiles with purpose-drawn, scalable AirGraph glyphs. */
    for (int i=0; i<5; ++i) {
        int y = icon_rows[i]*ch + sy;
        sp_card(ui, 2*cw, y, 16*cw, ch-2*sy);
        desktop_shell_icon(ui, surface, i, 2*cw+7*sx, y+(ch-28*sy)/2, sx);
        sp_label(ui, 2*cw+43*sx, y+(ch-17*sy)/2, labels[i], 0);
        sp_label(ui, 2*cw+43*sx, y+(ch-17*sy)/2+9*sy, subtitles[i], 1);
    }

    /* Start menu and quick panel slide up from behind the taskbar. */
    sp_p = an_track(an_id_str("shell.start"), start_open ? AN_ONE : 0,
                    start_open ? 14u : 10u, start_open ? AN_OUT_CUBIC : AN_IN_OUT);
    pn_p = an_track(an_id_str("shell.panel"), panel_open ? AN_ONE : 0,
                    panel_open ? 14u : 10u, panel_open ? AN_OUT_CUBIC : AN_IN_OUT);
    bar_y = (VGA_ROWS-1)*ch;
    if (start_open || sp_p > 0) {
        int menu_x = cw;
        int menu_y = (VGA_ROWS-1-menu_height)*ch + (AN_ONE - sp_p) * (3 * ch) / AN_ONE;
        sp_panel(ui, menu_x, menu_y, 25*cw, menu_height*ch);
        sp_label(ui, menu_x+2*cw, menu_y+(ch-7*sy)/2, "Applications", 0);
        sp_separator(ui, menu_x+cw, menu_y+ch, 23*cw);
        for (int i=0; i<menu_count; ++i) {
            const char *item = i < 7 ? menu[i] : apk_names[i-7];
            const char *subtitle = i < 7 ? "" : "Installed Android app";
            sp_list_item_a(ui, an_id_at(an_id_str("menu.item"), i, 0), menu_x+cw,
                           menu_y+(i+1)*ch, 23*cw, ch, item, subtitle, 0);
        }
        sp_label(ui, menu_x+2*cw, menu_y+(menu_count+1)*ch+(ch-7*sy)/2,
                 "Tab / Esc to close", 1);

    }
    if (app_context_open) {
        int cx = 27 * cw;
        int cy = app_context_y * ch;
        if (cy + 4 * ch > bar_y) cy = bar_y - 4 * ch;
        if (cy < 0) cy = 0;
        sp_panel(ui, cx, cy, 20 * cw, 4 * ch);
        sp_label(ui, cx + cw, cy + (ch - 7 * sy) / 2, app_context_title, 0);
        sp_list_item_a(ui, an_id_str("apk.context.open"), cx + cw, cy + ch,
                       18 * cw, ch, "Open", "Launch application", 0);
        sp_list_item_a(ui, an_id_str("apk.context.view"), cx + cw, cy + 2 * ch,
                       18 * cw, ch, "View App", "Package information", 0);
        sp_list_item_a(ui, an_id_str("apk.context.remove"), cx + cw, cy + 3 * ch,
                       18 * cw, ch, "Uninstall", "Remove installed APK", 0);
    }
    if (panel_open || pn_p > 0)
        desktop_draw_panel(ui, surface, cw, ch, sx, sy, clock_text, pn_p);

    /* Floating taskbar. Status cluster positions come from the real text width. */
    clock_x = w - 8*tw - 14*sx;
    bat_x = clock_x - 14*sx - (25*sx + 6*tw);
    net_x = bat_x - 16*sx - (22*sx + 7*tw);
    cluster_px = net_x - 8*sx;
    cluster_col = cluster_px / cw;
    if (cluster_col < 0) cluster_col = 0;
    if (cluster_col > VGA_COLS - 1) cluster_col = VGA_COLS - 1;
    sp_panel(ui, 12*sx, bar_y+2*sy, w-24*sx, ch-4*sy);
    sp_button_a(ui, an_id_str("shell.startbtn"), 2*cw, bar_y+4*sy, 8*cw, ch-8*sy, "Start", 1);
    sp_label(ui, 12*cw, bar_y+(ch-7*sy)/2, "Explorer   Settings   Terminal", 1);
    if (pn_p > 0)
        an_blend_rect(ui->device, surface, cluster_px, bar_y+4*sy,
                      w - 14*sx - cluster_px, ch-8*sy,
                      ui->theme->color.accent, (uint32_t)(pn_p * 90 / AN_ONE));
    desktop_draw_net(ui, surface, net_x, bar_y+7*sy, sx);
    desktop_draw_battery(ui, surface, bat_x, bar_y+7*sy, sx);
    if (clock_text) sp_label(ui, clock_x, bar_y+(ch-7*sy)/2, clock_text, 0);
    (void)h;
    return 0;
}

int desktop_gfx_flush(void) {
    struct ag_surface *rs;
    if (!display_ready) return -1;
    rs = desktop_render_surface();
    if (!render_buffer_ready) return 0;
    if (rs->width == display_surface.width && rs->height == display_surface.height) {
        if (ag_present(&display_device, rs) != 0)
            kernel_panic("AirGraph chrome presentation failed");
    } else {
        struct image_buffer img;
        img.pixels = (uint32_t *)rs->pixels;
        img.width = rs->width;
        img.height = rs->height;
        img.pitch = rs->pitch;
        img.capacity = rs->pitch * (uint32_t)rs->height;
        if (ag_draw_image_scaled(&display_device, &display_surface, 0, 0,
                                 display_surface.width, display_surface.height,
                                 &img) != 0)
            kernel_panic("AirGraph scaled presentation failed");
    }
    return 0;
}

