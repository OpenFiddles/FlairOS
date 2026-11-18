#ifndef AIRGRAPH_H
#define AIRGRAPH_H

#include <stdint.h>
#include "fb.h"

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
int ag_present(struct ag_device *, const struct ag_surface *);
int ag_viewport_init_adaptive(struct ag_viewport *, int32_t, int32_t,
                              int32_t, int32_t, int32_t, int32_t,
                              enum ag_scale_policy);
int ag_viewport_map_point(const struct ag_viewport *, int32_t, int32_t,
                          int32_t *, int32_t *);
int ag_viewport_map_rect(const struct ag_viewport *, int32_t, int32_t,
                         int32_t, int32_t, int32_t *, int32_t *,
                         int32_t *, int32_t *);
int ag_get_resource_info(const struct ag_device *, struct ag_resource_info *);

#endif
