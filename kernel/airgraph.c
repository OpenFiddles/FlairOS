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
