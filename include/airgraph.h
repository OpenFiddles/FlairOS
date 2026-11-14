#ifndef AIRGRAPH_H
#define AIRGRAPH_H

#include <stdint.h>
#include "fb.h"

/* AirGraph v1: architecture-neutral 32-bit pixel surfaces and a pluggable
   rendering backend. Coordinates are pixels; pitch is measured in bytes. */

enum ag_pixel_format {
    AG_PIXEL_XRGB8888 = 1,
    AG_PIXEL_ARGB8888 = 2
};

enum ag_capability {
    AG_CAP_FILL_RECT = 1u << 0,
    AG_CAP_BLEND_RECT = 1u << 1,
    AG_CAP_BLIT = 1u << 2,
    AG_CAP_PRESENT = 1u << 3
};

struct ag_surface {
    uint32_t *pixels;
    int32_t width;
    int32_t height;
    uint32_t pitch;
    enum ag_pixel_format format;
};

struct ag_device;

struct ag_backend_ops {
    const char *name;
    uint32_t capabilities;
    int (*init)(struct ag_device *device);
    void (*shutdown)(struct ag_device *device);
    int (*fill_rect)(struct ag_device *device, struct ag_surface *surface,
                     int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    int (*blend_rect)(struct ag_device *device, struct ag_surface *surface,
                      int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    int (*blit)(struct ag_device *device, struct ag_surface *surface,
                int32_t x, int32_t y, int32_t w, int32_t h, const uint32_t *pixels,
                uint32_t source_pitch);
    int (*present)(struct ag_device *device, const struct ag_surface *surface);
};

struct ag_device {
    const struct ag_backend_ops *backend;
    struct fb *display;
    void *private_data;
    int ready;
};

int ag_surface_init(struct ag_surface *surface, void *pixels, int32_t width,
                    int32_t height, uint32_t pitch, enum ag_pixel_format format);
int ag_device_init(struct ag_device *device, struct fb *display,
                   const struct ag_backend_ops *backend);
void ag_device_shutdown(struct ag_device *device);

void ag_clear(struct ag_surface *surface, uint32_t color);
void ag_put_pixel(struct ag_surface *surface, int32_t x, int32_t y, uint32_t color);
void ag_fill_rect(struct ag_device *device, struct ag_surface *surface,
                  int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void ag_frame_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void ag_blend_rect(struct ag_device *device, struct ag_surface *surface,
                   int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
void ag_blit(struct ag_device *device, struct ag_surface *surface,
             int32_t x, int32_t y, int32_t w, int32_t h,
             const uint32_t *pixels, uint32_t source_pitch);
int ag_present(struct ag_device *device, const struct ag_surface *surface);

#endif
