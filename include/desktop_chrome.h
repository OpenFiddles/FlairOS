#ifndef FLAIROS_DESKTOP_CHROME_H
#define FLAIROS_DESKTOP_CHROME_H

#include "airgraph.h"

/* Draw pixel-based window chrome over the existing logical desktop scene.
   Window geometry is supplied in logical grid coordinates for input compatibility. */
int desktop_chrome_draw(struct ag_device *device, struct ag_surface *surface,
                        int grid_columns, int grid_rows,
                        int x, int y, int width, int height,
                        const char *title, int active);

#endif
