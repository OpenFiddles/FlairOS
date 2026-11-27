#ifndef FLAIROS_DESKTOP_CHROME_H
#define FLAIROS_DESKTOP_CHROME_H

#include "airgraph.h"

/* Draw pixel-based window chrome over the existing logical desktop scene.
   Window geometry is supplied in logical grid coordinates for input compatibility. */
int desktop_chrome_draw(struct ag_device *device, struct ag_surface *surface,
                        int grid_columns, int grid_rows,
                        int x, int y, int width, int height,
                        const char *title, int active);

/* Density-aware chrome for pixel-space compositors: geometry, title bar and
   controls are given in device pixels, so a 4K window gets a 36 px bar and
   36 px traffic lights instead of a stretched hairline. */
int desktop_chrome_draw_scaled(struct ag_device *device, struct ag_surface *surface,
                               int x, int y, int width, int height,
                               const char *title, int active,
                               int title_h, int inset, int button_w, int button_gap);

#endif
