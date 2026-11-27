#ifndef FLAIROS_MOBILE_LAYOUT_H
#define FLAIROS_MOBILE_LAYOUT_H

#include <stdint.h>

/* The phone shell is laid out in a design coordinate space and scaled to the
   panel it draws on, so a 480x800 handset and a 1080x1920 phone get the same
   composition instead of a small layout in the corner.

   There are two design canvases, chosen by the panel's orientation: 480x800
   for portrait and 800x480 for landscape. The scale is Q16 fixed point taken
   from the smaller axis and clamped to [1.0, 3.0]: below the design size text
   stops being legible, above 3x the design has no more room to give. */
#define MOBILE_LAYOUT_BASE_W 480
#define MOBILE_LAYOUT_BASE_H 800
#define MOBILE_LAYOUT_LANDSCAPE_W 800
#define MOBILE_LAYOUT_LANDSCAPE_H 480
#define MOBILE_LAYOUT_UNIT_Q16 (1 << 16)

/* True when the panel is wider than it is tall, i.e. the landscape canvas. */
int mobile_layout_landscape(int32_t width, int32_t height);

int32_t mobile_layout_scale_q16(int32_t width, int32_t height);
int32_t mobile_layout_px(int32_t base, int32_t scale_q16);
int32_t mobile_layout_text(int32_t base, int32_t scale_q16);

#endif
