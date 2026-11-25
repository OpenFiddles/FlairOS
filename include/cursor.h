#ifndef FLAIROS_CURSOR_H
#define FLAIROS_CURSOR_H

#include "airgraph.h"

/* Mouse cursor sprites (original FlairOS artwork, built in as a fallback).
   An optional 32-bit/24-bit CURSOR.BMP (up to 32x32, magenta 0xFF00FF = transparent,
   hotspot at the top-left pixel) on the system volume replaces the arrow. */
enum cursor_shape { CURSOR_ARROW = 0, CURSOR_HAND, CURSOR_IBEAM, CURSOR_SHAPE_COUNT };

#define CURSOR_BMP_KEY 0x00FF00FFu
#define CURSOR_BMP_MAX 32

void cursor_set_shape(enum cursor_shape shape);
enum cursor_shape cursor_get_shape(void);

/* Replace the arrow with a BMP. Returns 1 when accepted, 0 on any problem
   (the built-in arrow is kept). */
int cursor_load_bmp(const unsigned char *data, unsigned int length);
void cursor_reset_custom(void);

/* Draw the current cursor with its hotspot at pixel (x, y). */
void cursor_draw(struct ag_device *device, struct ag_surface *surface, int x, int y);

/* Width/height/hotspot of the sprite for a shape; used by tests. */
void cursor_metrics(enum cursor_shape shape, int *width, int *height, int *hot_x, int *hot_y);

/* Sprite pixel: 0 transparent, 1 outline, 2 fill. Returns -1 outside the sprite. */
int cursor_builtin_pixel(enum cursor_shape shape, int x, int y);

#endif
