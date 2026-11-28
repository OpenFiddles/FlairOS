#ifndef AIRGRAPH_H
#define AIRGRAPH_H

#include <stdint.h>
#include "fb.h"
#include "image.h"

enum ag_pixel_format { AG_PIXEL_XRGB8888 = 1, AG_PIXEL_ARGB8888 = 2 };
enum ag_orientation { AG_ORIENTATION_LANDSCAPE = 0, AG_ORIENTATION_PORTRAIT = 1 };
enum ag_scale_policy { AG_SCALE_FIT = 0, AG_SCALE_FILL = 1, AG_SCALE_STRETCH = 2 };
#define AG_BACKEND_GPU (1u << 0)
#define AG_BACKEND_BLIT (1u << 1)
enum ag_capability {
    AG_CAP_FILL_RECT = 1u << 0, AG_CAP_BLEND_RECT = 1u << 1,
    AG_CAP_BLIT = 1u << 2, AG_CAP_PRESENT = 1u << 3
};
struct ag_surface {
    uint32_t *pixels;
    int32_t width, height;
    uint32_t pitch;
    enum ag_pixel_format format;
};
struct ag_device;
struct ag_backend_ops {
    const char *name;
    uint32_t capabilities;
    uint32_t flags;
    int (*init)(struct ag_device *);
    void (*shutdown)(struct ag_device *);
    int (*fill_rect)(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, uint32_t);
    int (*blend_rect)(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, uint32_t);
    int (*blit)(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, const uint32_t *, uint32_t);
    int (*present)(struct ag_device *, const struct ag_surface *);
};
struct ag_device {
    const struct ag_backend_ops *backend;
    struct fb *display;
    void *private_data;
    int ready;
};
struct ag_viewport {
    int32_t display_width, display_height;
    int32_t logical_width, logical_height;
    int32_t x, y, width, height;
    uint32_t scale_x_16, scale_y_16;
    enum ag_orientation orientation;
    enum ag_scale_policy policy;
};
struct ag_resource_info {
    const char *renderer_name;
    uint32_t renderer_capabilities;
    uint32_t backend_flags;
    int cpu_renderer_available;
    int gpu_available;
    int gpu_accelerated;
};
int ag_surface_init(struct ag_surface *, void *, int32_t, int32_t, uint32_t, enum ag_pixel_format);
int ag_device_init(struct ag_device *, struct fb *, const struct ag_backend_ops *);
void ag_device_shutdown(struct ag_device *);
void ag_clear(struct ag_surface *, uint32_t);
void ag_put_pixel(struct ag_surface *, int32_t, int32_t, uint32_t);
void ag_fill_rect(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, uint32_t);
void ag_frame_rect(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, uint32_t);
void ag_round_rect(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t);
void ag_round_frame(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t);
void ag_blend_rect(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, uint32_t);
void ag_blit(struct ag_device *, struct ag_surface *, int32_t, int32_t, int32_t, int32_t, const uint32_t *, uint32_t);
void ag_alpha_blit(struct ag_surface *dst, int32_t x, int32_t y, int32_t w, int32_t h,
                   const uint32_t *argb_pixels, uint32_t source_pitch);
/* Draw a decoded image at native size; clipping is handled by AirGraph. */
int ag_draw_image(struct ag_device *, struct ag_surface *, int32_t, int32_t,
                  const struct image_buffer *);
/* Nearest-neighbour scaling; destination dimensions are in pixels. */
int ag_draw_image_scaled(struct ag_device *, struct ag_surface *,
                         int32_t, int32_t, int32_t, int32_t,
                         const struct image_buffer *);
int ag_present(struct ag_device *, const struct ag_surface *);
int ag_viewport_init_adaptive(struct ag_viewport *, int32_t, int32_t,
                              int32_t, int32_t, int32_t, int32_t,
                              enum ag_scale_policy);
int ag_viewport_map_point(const struct ag_viewport *, int32_t, int32_t,
                          int32_t *, int32_t *);
int ag_viewport_unmap_point(const struct ag_viewport *, int32_t, int32_t,
                            int32_t *, int32_t *);
int ag_viewport_map_rect(const struct ag_viewport *, int32_t, int32_t,
                         int32_t, int32_t, int32_t *, int32_t *,
                         int32_t *, int32_t *);
int ag_get_resource_info(const struct ag_device *, struct ag_resource_info *);

/* ---- Density: one design layout, every panel ----------------------------
   Panels range from 480x800 phones to 3840x2160 TVs. Shells author geometry
   once in design pixels and ask AirGraph for the device pixels, so strokes and
   radii stay whole-pixel crisp instead of being stretched by the compositor. */
#define AG_SCALE_ONE 65536u      /* 1.0 in Q16 */
#define AG_SCALE_MAX 262144u     /* 4.0 in Q16 */

/* The one implementation of panel/design ratio math. Session layouts (TV,
   mobile) clamp the result differently from the desktop, so the ratio itself
   lives here and each caller applies its own bounds. Returns 0 for invalid
   sizes. */
static inline uint32_t ag_scale_ratio_q16(int32_t panel_w, int32_t panel_h,
                                          int32_t design_w, int32_t design_h) {
    uint32_t sx, sy;
    if (panel_w <= 0 || panel_h <= 0 || design_w <= 0 || design_h <= 0 ||
        panel_w > 8192 || panel_h > 8192 || design_w > 8192 || design_h > 8192)
        return 0u;
    sx = ((uint32_t)panel_w << 16) / (uint32_t)design_w;
    sy = ((uint32_t)panel_h << 16) / (uint32_t)design_h;
    return sx < sy ? sx : sy;
}

/* Q16 scale for a panel against a design size: the shared ratio, clamped to
   [1.0, 4.0] so a large design never shrinks below its authored size and a 4K
   panel never scales past 4x. */
int ag_scale_q16_for(int32_t panel_w, int32_t panel_h,
                     int32_t design_w, int32_t design_h, uint32_t *out);
/* The same ratio rounded to a whole 1x..4x step, for pixel art such as the
   pointer that must scale by whole pixels instead of a fractional transform.
   Returns 0 for invalid sizes. */
int ag_scale_step_for(int32_t panel_w, int32_t panel_h,
                      int32_t design_w, int32_t design_h);
/* Design pixels to device pixels, rounded to the nearest whole pixel; a
   positive design length never becomes 0. scale_q16 == 0 means 1.0. */
int32_t ag_px(int32_t design_px, uint32_t scale_q16);

/* Stroke a rectangle with an integer thickness, drawn inside its bounds.
   At 1x this is a hairline; at 4K the same design thickness becomes 3-4 solid
   pixels without fractional edges. A thickness at or above half the box
   fills it. */
void ag_stroke_rect(struct ag_device *, struct ag_surface *,
                    int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t thickness, uint32_t color);
/* Rounded rectangle whose radius is authored in design pixels and scaled to
   the panel, so a 4K corner is a real 4x radius rather than a stretched 1x
   one. */
void ag_round_rect_scaled(struct ag_device *, struct ag_surface *,
                          int32_t x, int32_t y, int32_t w, int32_t h,
                          int32_t design_radius, uint32_t scale_q16,
                          uint32_t color);

struct ag_damage { int valid; int32_t x0, y0, x1, y1; };
void ag_damage_reset(struct ag_damage *d);
void ag_damage_add(struct ag_damage *d, int32_t x, int32_t y, int32_t w, int32_t h, int32_t max_w, int32_t max_h);
void ag_damage_full(struct ag_damage *d, int32_t max_w, int32_t max_h);

#endif
