#include "../include/airgraph.h"

static uint32_t *surface_row(const struct ag_surface *s, int32_t y) {
    return (uint32_t *)((uint8_t *)s->pixels + (uint32_t)y * s->pitch);
}

static int surface_valid(const struct ag_surface *s) {
    if (!s || !s->pixels || s->width <= 0 || s->height <= 0)
        return 0;
    if ((uint32_t)s->width > 0x3FFFFFFFu || s->pitch < (uint32_t)s->width * 4u)
        return 0;
    return s->format == AG_PIXEL_XRGB8888 || s->format == AG_PIXEL_ARGB8888;
}

/* Clip without x+w arithmetic, which could overflow signed 32-bit coordinates. */
static int clip_rect(const struct ag_surface *s, int32_t *x, int32_t *y,
                     int32_t *w, int32_t *h) {
    if (!surface_valid(s) || *w <= 0 || *h <= 0)
        return 0;
    if (*x < 0) {
        if (*x <= -*w)
            return 0;
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        if (*y <= -*h)
            return 0;
        *h += *y;
        *y = 0;
    }
    if (*x >= s->width || *y >= s->height)
        return 0;
    if (*w > s->width - *x)
        *w = s->width - *x;
    if (*h > s->height - *y)
        *h = s->height - *y;
    return *w > 0 && *h > 0;
}

static uint32_t opaque_color(const struct ag_surface *s, uint32_t color) {
    return s->format == AG_PIXEL_XRGB8888 ? (color & 0x00FFFFFFu) : color;
}

static uint32_t blend_pixel(uint32_t dst, uint32_t src) {
    uint32_t a = src >> 24;
    uint32_t inv = 255u - a;
    uint32_t r = (((src >> 16) & 255u) * a + ((dst >> 16) & 255u) * inv + 127u) / 255u;
    uint32_t g = (((src >> 8) & 255u) * a + ((dst >> 8) & 255u) * inv + 127u) / 255u;
    uint32_t b = ((src & 255u) * a + (dst & 255u) * inv + 127u) / 255u;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

int ag_surface_init(struct ag_surface *surface, void *pixels, int32_t width,
                    int32_t height, uint32_t pitch, enum ag_pixel_format format) {
    if (!surface || !pixels || width <= 0 || height <= 0 ||
        (uint32_t)width > 0x3FFFFFFFu || pitch < (uint32_t)width * 4u ||
        (pitch & 3u) != 0 ||
        (format != AG_PIXEL_XRGB8888 && format != AG_PIXEL_ARGB8888))
        return -1;
    surface->pixels = (uint32_t *)pixels;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->format = format;
    return 0;
}

int ag_device_init(struct ag_device *device, struct fb *display,
                   const struct ag_backend_ops *backend) {
    if (!device || !display || !display->base || display->width <= 0 ||
        display->height <= 0 || (uint32_t)display->width > 0x3FFFFFFFu ||
        display->pitch < (uint32_t)display->width * 4u)
        return -1;
    device->backend = backend;
    device->display = display;
    device->private_data = 0;
    device->ready = 0;
    if (backend && backend->init && backend->init(device) != 0) {
        device->backend = 0;
        return -1;
    }
    device->ready = 1;
    return 0;
}

void ag_device_shutdown(struct ag_device *device) {
    if (!device || !device->ready)
        return;
    if (device->backend && device->backend->shutdown)
        device->backend->shutdown(device);
    device->ready = 0;
    device->backend = 0;
    device->display = 0;
}

void ag_clear(struct ag_surface *surface, uint32_t color) {
    if (!surface_valid(surface))
        return;
    color = opaque_color(surface, color);
    for (int32_t y = 0; y < surface->height; y++) {
        uint32_t *row = surface_row(surface, y);
        for (int32_t x = 0; x < surface->width; x++)
            row[x] = color;
    }
}

void ag_put_pixel(struct ag_surface *surface, int32_t x, int32_t y, uint32_t color) {
    if (!surface_valid(surface) || x < 0 || y < 0 ||
        x >= surface->width || y >= surface->height)
        return;
    surface_row(surface, y)[x] = opaque_color(surface, color);
}

void ag_fill_rect(struct ag_device *device, struct ag_surface *surface,
                  int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!clip_rect(surface, &x, &y, &w, &h))
        return;
    if (device && device->ready && device->backend &&
        (device->backend->capabilities & AG_CAP_FILL_RECT) &&
        device->backend->fill_rect &&
        device->backend->fill_rect(device, surface, x, y, w, h, color) == 0)
        return;
    color = opaque_color(surface, color);
    for (int32_t dy = 0; dy < h; dy++) {
        uint32_t *row = surface_row(surface, y + dy) + x;
        for (int32_t dx = 0; dx < w; dx++)
            row[dx] = color;
    }
}


void ag_frame_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    int32_t original_x = x, original_y = y;
    int32_t original_w = w, original_h = h;
    int32_t right_x = 0, bottom_y = 0;
    int right_visible = 0, bottom_visible = 0;
    if (!clip_rect(surface, &x, &y, &w, &h))
        return;

    if (original_y >= 0 && original_y < surface->height)
        ag_fill_rect(device, surface, original_x, original_y, original_w, 1, color);
    if (original_h > 1) {
        if (original_y < 0) {
            bottom_y = original_y + original_h - 1;
            bottom_visible = bottom_y >= 0 && bottom_y < surface->height;
        } else if (original_y < surface->height &&
                   original_h <= surface->height - original_y) {
            bottom_y = original_y + original_h - 1;
            bottom_visible = 1;
        }
    }
    if (bottom_visible)
        ag_fill_rect(device, surface, original_x, bottom_y, original_w, 1, color);

    if (original_x >= 0 && original_x < surface->width)
        ag_fill_rect(device, surface, original_x, y, 1, h, color);
    if (original_w > 1) {
        if (original_x < 0) {
            right_x = original_x + original_w - 1;
            right_visible = right_x >= 0 && right_x < surface->width;
        } else if (original_x < surface->width &&
                   original_w <= surface->width - original_x) {
            right_x = original_x + original_w - 1;
            right_visible = 1;
        }
    }
    if (right_visible)
        ag_fill_rect(device, surface, right_x, y, 1, h, color);
}
void ag_blend_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color) {
    if (!clip_rect(surface, &x, &y, &w, &h))
        return;
    if (device && device->ready && device->backend &&
        (device->backend->capabilities & AG_CAP_BLEND_RECT) &&
        device->backend->blend_rect &&
        device->backend->blend_rect(device, surface, x, y, w, h, color) == 0)
        return;
    if ((color >> 24) == 255u) {
        ag_fill_rect(device, surface, x, y, w, h, color);
        return;
    }
    if ((color >> 24) == 0u)
        return;
    for (int32_t dy = 0; dy < h; dy++) {
        uint32_t *row = surface_row(surface, y + dy) + x;
        for (int32_t dx = 0; dx < w; dx++)
            row[dx] = opaque_color(surface, blend_pixel(row[dx], color));
    }
}

void ag_blit(struct ag_device *device, struct ag_surface *surface,
             int32_t x, int32_t y, int32_t w, int32_t h,
             const uint32_t *pixels, uint32_t source_pitch) {
    int32_t original_x = x, original_y = y;
    if (!pixels || w <= 0 || h <= 0 || (uint32_t)w > 0x3FFFFFFFu ||
        source_pitch < (uint32_t)w * 4u || (source_pitch & 3u) != 0 ||
        !clip_rect(surface, &x, &y, &w, &h))
        return;
    const uint8_t *source = (const uint8_t *)pixels +
                            (uint32_t)(y - original_y) * source_pitch +
                            (uint32_t)(x - original_x) * 4u;
    if (device && device->ready && device->backend &&
        (device->backend->capabilities & AG_CAP_BLIT) &&
        device->backend->blit &&
        device->backend->blit(device, surface, x, y, w, h,
                              (const uint32_t *)source, source_pitch) == 0)
        return;
    for (int32_t dy = 0; dy < h; dy++) {
        const uint32_t *src = (const uint32_t *)(source + (uint32_t)dy * source_pitch);
        uint32_t *dst = surface_row(surface, y + dy) + x;
        for (int32_t dx = 0; dx < w; dx++)
            dst[dx] = opaque_color(surface, src[dx]);
    }
}

int ag_present(struct ag_device *device, const struct ag_surface *surface) {
    if (!device || !device->ready || !device->display || !surface_valid(surface))
        return -1;
    if (device->backend && (device->backend->capabilities & AG_CAP_PRESENT) &&
        device->backend->present && device->backend->present(device, surface) == 0)
        return 0;
    int32_t w = surface->width < device->display->width ? surface->width : device->display->width;
    int32_t h = surface->height < device->display->height ? surface->height : device->display->height;
    for (int32_t y = 0; y < h; y++)
        fb_blit32(device->display, 0, y, w, 1, surface_row(surface, y));
    return 0;
}

/* Rounded rectangles are scanline-rendered and clipped by the normal surface
   primitives. Radius is clamped so tiny widgets remain well-defined. */
static int32_t round_inset(int32_t y, int32_t h, int32_t radius) {
    int32_t cy = y < radius ? radius - 1 - y : (y >= h - radius ? y - (h - radius) : 0);
    if (cy <= 0) return 0;
    /* 64-bit binary integer square root avoids overflow and radius-sized loops. */
    uint64_t rr = (uint64_t)radius * (uint64_t)radius;
    uint64_t yy = (uint64_t)cy * (uint64_t)cy;
    uint64_t xx = rr - yy;
    uint64_t lo = 0, hi = (uint64_t)radius;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1u) / 2u;
        if (mid * mid <= xx) lo = mid;
        else hi = mid - 1u;
    }
    return radius - (int32_t)lo;
}

void ag_round_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h,
                   int32_t radius, uint32_t color) {
    if (!surface_valid(surface) || w <= 0 || h <= 0 || radius <= 0) {
        ag_fill_rect(device, surface, x, y, w, h, color);
        return;
    }
    int32_t maxr = (w < h ? w : h) / 2;
    if (radius > maxr) radius = maxr;
    if (radius < 1) { ag_fill_rect(device, surface, x, y, w, h, color); return; }
    for (int32_t row = 0; row < h; row++) {
        int32_t inset = round_inset(row, h, radius);
        if (w > inset * 2)
            ag_fill_rect(device, surface, x + inset, y + row, w - inset * 2, 1, color);
    }
}

void ag_round_frame(struct ag_device *device, struct ag_surface *surface,
                    int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t radius, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (radius <= 0) { ag_frame_rect(device, surface, x, y, w, h, color); return; }
    int32_t maxr = (w < h ? w : h) / 2;
    if (radius > maxr) radius = maxr;
    for (int32_t row = 0; row < h; row++) {
        int32_t inset = round_inset(row, h, radius);
        int32_t left = x + inset, right = x + w - inset - 1;
        ag_put_pixel(surface, left, y + row, color);
        ag_put_pixel(surface, right, y + row, color);
        if (row == 0 || row == h - 1) {
            if (right > left)
                ag_fill_rect(device, surface, left + 1, y + row, right - left - 1, 1, color);
        }
    }
}


static int32_t ag_clamp_i64(int64_t v) {
    if (v > 2147483647LL) return 2147483647;
    if (v < (-2147483647LL - 1LL)) return (-2147483647 - 1);
    return (int32_t)v;
}

int ag_viewport_init_adaptive(struct ag_viewport *vp, int32_t dw, int32_t dh,
                              int32_t lsw, int32_t lsh, int32_t psw, int32_t psh,
                              enum ag_scale_policy policy) {
    int32_t lw, lh;
    uint32_t sx, sy, scale;
    if (!vp || dw <= 0 || dh <= 0 || lsw <= 0 || lsh <= 0 ||
        psw <= 0 || psh <= 0 || dw > 8192 || dh > 8192 ||
        lsw > 8192 || lsh > 8192 || psw > 8192 || psh > 8192 ||
        policy < AG_SCALE_FIT || policy > AG_SCALE_STRETCH)
        return -1;
    vp->orientation = dw < dh ? AG_ORIENTATION_PORTRAIT : AG_ORIENTATION_LANDSCAPE;
    lw = vp->orientation == AG_ORIENTATION_PORTRAIT ? psw : lsw;
    lh = vp->orientation == AG_ORIENTATION_PORTRAIT ? psh : lsh;
    vp->display_width = dw; vp->display_height = dh;
    vp->logical_width = lw; vp->logical_height = lh; vp->policy = policy;
    sx = ((uint32_t)dw << 16) / (uint32_t)lw;
    sy = ((uint32_t)dh << 16) / (uint32_t)lh;
    if (!sx || !sy) return -1;
    if (policy == AG_SCALE_STRETCH) {
        vp->scale_x_16 = (uint32_t)sx; vp->scale_y_16 = (uint32_t)sy;
        vp->x = vp->y = 0; vp->width = dw; vp->height = dh;
        return 0;
    }
    scale = policy == AG_SCALE_FIT ? (sx < sy ? sx : sy) : (sx > sy ? sx : sy);
    if (!scale) return -1;
    vp->scale_x_16 = vp->scale_y_16 = (uint32_t)scale;
    vp->width = ag_clamp_i64(((int64_t)lw * (int64_t)scale) >> 16);
    vp->height = ag_clamp_i64(((int64_t)lh * (int64_t)scale) >> 16);
    vp->x = (dw - vp->width) / 2; vp->y = (dh - vp->height) / 2;
    return vp->width > 0 && vp->height > 0 ? 0 : -1;
}

int ag_viewport_map_point(const struct ag_viewport *vp, int32_t lx, int32_t ly,
                          int32_t *dx, int32_t *dy) {
    if (!vp || !dx || !dy || !vp->scale_x_16 || !vp->scale_y_16) return -1;
    *dx = ag_clamp_i64(((int64_t)lx * vp->scale_x_16) / 65536 + vp->x);
    *dy = ag_clamp_i64(((int64_t)ly * vp->scale_y_16) / 65536 + vp->y);
    return 0;
}

int ag_viewport_unmap_point(const struct ag_viewport *vp, int32_t dx, int32_t dy,
                            int32_t *lx, int32_t *ly) {
    int64_t right, bottom, logical_x, logical_y;

    if (!vp || !lx || !ly || !vp->scale_x_16 || !vp->scale_y_16 ||
        vp->width <= 0 || vp->height <= 0 ||
        vp->logical_width <= 0 || vp->logical_height <= 0)
        return -1;

    right = (int64_t)vp->x + vp->width;
    bottom = (int64_t)vp->y + vp->height;
    if ((int64_t)dx < vp->x || (int64_t)dy < vp->y ||
        (int64_t)dx >= right || (int64_t)dy >= bottom)
        return -1;

    logical_x = (((int64_t)dx - vp->x) << 16) / vp->scale_x_16;
    logical_y = (((int64_t)dy - vp->y) << 16) / vp->scale_y_16;
    if (logical_x < 0 || logical_y < 0 ||
        logical_x >= vp->logical_width || logical_y >= vp->logical_height)
        return -1;

    *lx = (int32_t)logical_x;
    *ly = (int32_t)logical_y;
    return 0;
}

int ag_viewport_map_rect(const struct ag_viewport *vp, int32_t lx, int32_t ly,
                         int32_t lw, int32_t lh, int32_t *dx, int32_t *dy,
                         int32_t *dw, int32_t *dh) {
    int64_t left, top, right, bottom;
    if (!vp || !dx || !dy || !dw || !dh || lw <= 0 || lh <= 0 ||
        !vp->scale_x_16 || !vp->scale_y_16) return -1;
    left = ((int64_t)lx * vp->scale_x_16) / 65536 + vp->x;
    top = ((int64_t)ly * vp->scale_y_16) / 65536 + vp->y;
    right = (((int64_t)lx + lw) * vp->scale_x_16) / 65536 + vp->x;
    bottom = (((int64_t)ly + lh) * vp->scale_y_16) / 65536 + vp->y;
    *dx = ag_clamp_i64(left); *dy = ag_clamp_i64(top);
    *dw = ag_clamp_i64(right - left); *dh = ag_clamp_i64(bottom - top);
    return (*dw > 0 && *dh > 0) ? 0 : -1;
}

int ag_get_resource_info(const struct ag_device *device, struct ag_resource_info *info) {
    if (!info) return -1;
    info->renderer_name = "cpu-software";
    info->renderer_capabilities = 0; info->backend_flags = 0;
    info->cpu_renderer_available = 1; info->gpu_available = 0; info->gpu_accelerated = 0;
    if (device && device->ready && device->backend) {
        info->renderer_name = device->backend->name ? device->backend->name : "unnamed-backend";
        info->renderer_capabilities = device->backend->capabilities;
        info->backend_flags = device->backend->flags;
        if (device->backend->flags & AG_BACKEND_GPU) {
            info->gpu_available = 1;
            info->gpu_accelerated = device->backend->capabilities != 0;
        }
    }
    return 0;
}
