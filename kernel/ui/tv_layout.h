#ifndef FLAIROS_TV_LAYOUT_H
#define FLAIROS_TV_LAYOUT_H

#include <stdint.h>

/* The TV shell is laid out once, in the coordinates of a 1280x720 ten-foot
   design, and scaled to the surface it actually draws on. A 4K panel therefore
   gets the same composition three times larger instead of a 720p layout
   stranded in the corner.

   The scale is Q16 fixed point (1.0 = 65536) taken from the smaller axis, so a
   wide or tall surface never overflows, and clamped to [1.0, 3.0]: below 720p
   text would stop being legible, above 4K the design has no more room to give. */
#define TV_LAYOUT_BASE_W 1280
#define TV_LAYOUT_BASE_H 720
#define TV_LAYOUT_UNIT_Q16 (1 << 16)

int32_t tv_layout_scale_q16(int32_t width, int32_t height);

/* Scale a design pixel value; rounding is to nearest. */
int32_t tv_layout_px(int32_t base, int32_t scale_q16);

/* Scale a text size, never below 1: a glyph that rounds to nothing is a bug. */
int32_t tv_layout_text(int32_t base, int32_t scale_q16);

#endif
