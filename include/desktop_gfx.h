#ifndef FLAIROS_DESKTOP_GFX_H
#define FLAIROS_DESKTOP_GFX_H
#include <stdint.h>
int desktop_gfx_init(void);
int desktop_gfx_active(void);
int desktop_gfx_present(const uint16_t *,int,int);
int desktop_gfx_map_pointer(uint32_t,uint32_t,int32_t *,int32_t *);
#endif
