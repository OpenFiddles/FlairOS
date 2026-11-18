#ifndef AIROS_VBE_H
#define AIROS_VBE_H

#include "fb.h"

/* Bochs/QEMU DISPI linear framebuffer. All dimensions are pixels. */
int vbe_get_limits(int32_t *max_width, int32_t *max_height);
int vbe_init(struct fb *f, int32_t width, int32_t height);
void vbe_text_mode(void);

#endif
