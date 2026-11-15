#ifndef FLAIROS_DESKTOP_RENDER_H
#define FLAIROS_DESKTOP_RENDER_H
#include <stdint.h>
#include "airgraph.h"
#define DESKTOP_GRID_COLUMNS 80
#define DESKTOP_GRID_ROWS 25
int desktop_render_set_wallpaper(const unsigned char *, unsigned int);
void desktop_render_clear_wallpaper(void);
int desktop_render_cells(struct ag_device *,struct ag_surface *,const uint16_t *,int,int);
#endif
