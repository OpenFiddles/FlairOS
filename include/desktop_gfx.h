#ifndef FLAIROS_DESKTOP_GFX_H
#define FLAIROS_DESKTOP_GFX_H
#include <stdint.h>
struct sp_context;
struct sp_theme;
int desktop_gfx_init(void);
int desktop_gfx_active(void);
int desktop_gfx_set_accent(unsigned int accent);
void desktop_gfx_shutdown(void);
int desktop_gfx_present(const uint16_t *,int,int);
int desktop_gfx_draw_window(int,int,int,int,const char *,int);
int desktop_gfx_flush(void);
int desktop_gfx_draw_shell(int start_open, const char *clock_text);
void desktop_gfx_set_app_context(int open, int y, const char *title);
int desktop_gfx_map_pointer(uint32_t,uint32_t,int32_t *,int32_t *);
struct sp_context *desktop_gfx_ui_context(void);
int desktop_gfx_set_profile(unsigned int profile);
unsigned int desktop_gfx_get_profile(void);
int desktop_gfx_set_touch(unsigned int enabled);
unsigned int desktop_gfx_get_touch(void);

int desktop_gfx_rotate(void);
int desktop_gfx_profile_allowed(unsigned int profile);

int desktop_gfx_set_anim(unsigned int enabled);
unsigned int desktop_gfx_get_anim(void);
unsigned int desktop_gfx_status_flags(void);
/* Animation control: 0/1 off/on, 2/3 forbid/allow GPU (set); 0xFF query via get. */
unsigned int desktop_gfx_get_anim(void);
int desktop_gfx_set_anim(unsigned int op);
unsigned int desktop_gfx_anim_info(void);
/* Taskbar status cluster and quick-settings panel. */
int desktop_gfx_status_hit(int mx);
void desktop_gfx_panel_set(int open);
int desktop_gfx_panel_open(void);
int desktop_gfx_panel_click(int mx, int my);

/* Session theme. The desktop package (FlairDesktop.airp) supplies its theme
   resource; desktop_gfx_apply_theme installs a validated theme. */
struct sp_theme *desktop_gfx_theme(void);
void desktop_gfx_apply_theme(const struct sp_theme *theme);

#endif
