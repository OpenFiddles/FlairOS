#ifndef FLAIROS_MOBILE_LAYOUT_H
#define FLAIROS_MOBILE_LAYOUT_H

#include <stdint.h>

/* The phone shell is laid out once in 480x800 design coordinates and scaled to
   the panel it draws on, so a 480x800 handset and a 1080x1920 phone get the same
   composition instead of a small layout in the corner.

   The scale is Q16 fixed point taken from the smaller axis and clamped to
   [1.0, 3.0]: below the design size text stops being legible, above 3x the
   design has no more room to give. */
#define MOBILE_LAYOUT_BASE_W 480
#define MOBILE_LAYOUT_BASE_H 800
#define MOBILE_LAYOUT_UNIT_Q16 (1 << 16)

int32_t mobile_layout_scale_q16(int32_t width, int32_t height);
int32_t mobile_layout_px(int32_t base, int32_t scale_q16);
int32_t mobile_layout_text(int32_t base, int32_t scale_q16);

#endif
