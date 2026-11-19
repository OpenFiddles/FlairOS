#ifndef FLAIROS_DESKTOP_GFX_H
#define FLAIROS_DESKTOP_GFX_H
#include <stdint.h>
struct sp_context;
int desktop_gfx_init(void);
int desktop_gfx_active(void);
int desktop_gfx_set_accent(unsigned int accent);
void desktop_gfx_shutdown(void);
int desktop_gfx_present(const uint16_t *,int,int);
int desktop_gfx_draw_window(int,int,int,int,const char *,int);
int desktop_gfx_flush(void);
int desktop_gfx_draw_shell(int start_open, const char *clock_text);
int desktop_gfx_map_pointer(uint32_t,uint32_t,int32_t *,int32_t *);
struct sp_context *desktop_gfx_ui_context(void);
#endif
