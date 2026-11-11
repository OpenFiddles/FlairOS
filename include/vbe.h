#ifndef AIROS_VBE_H
#define AIROS_VBE_H

#include "fb.h"

/* Sets a linear 32bpp mode through the Bochs display ports (0x01CE/0x01CF), which
   QEMU's std VGA implements. Stays in protected mode, so no BIOS call is needed.
   Returns 0 when that adapter is absent; the caller then stays in VGA text mode. */
int vbe_init(struct fb *f, int32_t width, int32_t height);
void vbe_text_mode(void);

#endif
