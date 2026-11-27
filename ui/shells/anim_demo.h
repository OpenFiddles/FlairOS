#ifndef FLAIROS_ANIM_DEMO_H
#define FLAIROS_ANIM_DEMO_H

#include "../../include/airgraph.h"

/* A short, deterministic Airnimation preset showcase drawn over the session
   while the desktop or phone shell starts. It exists so the booted kernels
   exercise the same public preset API the shells use, at every panel size and
   orientation, and then disappears so the resting frame is the normal UI. */

void shell_anim_demo_render(struct ag_surface *surface);
int shell_anim_demo_tick(void);   /* 1 while frames still need redrawing */
int shell_anim_demo_active(void);

#endif
