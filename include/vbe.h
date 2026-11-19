#ifndef AIROS_VBE_H
#define AIROS_VBE_H

#include "fb.h"

/* Bochs/QEMU DISPI linear framebuffer. Dimensions are pixels. */
int vbe_init(struct fb *f, int32_t width, int32_t height);
const char *vbe_last_error(void);
void vbe_text_mode(void);

#endif
