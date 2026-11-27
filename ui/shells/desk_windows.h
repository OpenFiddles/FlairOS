#ifndef FLAIROS_DESK_WINDOWS_H
#define FLAIROS_DESK_WINDOWS_H

#include <stdint.h>
#include "../../include/airgraph.h"
#include "../../include/windown.h"

/* The desktop shell's window layer, shared by the shell and the headless
   harness.

   At 720p and below the shell keeps its historical 1 px chrome and pixel hit
   regions byte for byte. From 1080p up the same table is ordered by display
   density: chrome comes from WinDown's metrics, hit regions from
   wd_hit_chrome(), and a mode change rescales every window instead of leaving
   it at its old coordinates. Window opening is an Airnimation preset so the
   preset API is exercised by shipping code, not only by tests. */

#define DW_MAX_WINDOWS 8

struct dw_window {
    int open;
    int minimized;
    int zoomed;
    int32_t x, y, w, h;
    int32_t sx, sy, sw, sh;
    const char *title;
};

struct dw_state {
    struct wd_compositor wd;
    int wd_ids[DW_MAX_WINDOWS];
    int panel_w, panel_h;
    uint32_t scale_q16;
    int active;            /* density mode: scaled chrome and hit regions */
    int drag_id;
    int32_t drag_ox, drag_oy;
};

enum dw_click {
    DW_CLICK_NONE = 0,
    DW_CLICK_CLOSE,
    DW_CLICK_MINIMIZE,
    DW_CLICK_ZOOM,
    DW_CLICK_DRAG,
    DW_CLICK_CLIENT
};

/* The scaled client area a window's content may draw into. */
struct dw_client {
    int32_t x, y, w, h;
};

void dw_init(struct dw_state *st);
/* Returns 1 when the panel changed (caller rescales its windows), 0 when it
   is unchanged, -1 for invalid sizes. */
int dw_configure(struct dw_state *st, int panel_w, int panel_h);
int dw_active(const struct dw_state *st);
struct wd_metrics dw_metrics(struct dw_state *st);

/* Mirror a shell window into the compositor before hit-testing or drawing. */
void dw_sync(struct dw_state *st, int slot, const struct dw_window *win);
enum wd_hit_region dw_region(struct dw_state *st, int slot,
                             const struct dw_window *win, int mx, int my);

/* Draw scaled chrome for a window; no-op while the density mode is off. */
void dw_draw_chrome(struct dw_state *st, struct ag_surface *surface, int slot,
                    const struct dw_window *win, int active);

/* Window open transition: the "slide-in" Airnimation preset. */
void dw_open(struct dw_state *st, int slot);
int32_t dw_open_offset(struct dw_state *st, int slot);

/* Dispatch a click on the window, mutating it exactly as the shell does:
   close hides it, minimize minimizes it, zoom toggles it, a title-bar click
   starts a drag. Returns what happened so the caller can stop. */
enum dw_click dw_click(struct dw_state *st, struct dw_window *win, int slot,
                       int mx, int my);
int dw_drag_to(struct dw_state *st, struct dw_window *win, int slot,
               int mx, int my);
int dw_end_drag(struct dw_state *st);

struct dw_client dw_client_rect(struct dw_state *st,
                                const struct dw_window *win);

/* The shell's window/chrome dispatch, callable by the headless harness.
   Walks z-order top-down, and when density is on delegates to dw_click so a
   scaled close click closes and a scaled title-bar click starts a drag.
   Returns 1 when the click was consumed by chrome (caller must stop), 0 when
   it lands on a client area or nothing. Outputs are optional. */
int dw_dispatch_click(struct dw_state *st, struct dw_window *wins,
                      const int *z_order, int count, int mx, int my,
                      int *slot_out, enum dw_click *action_out);

/* Scale every window (and its saved geometry) to a new panel, clamp it
   on-screen and adopt the new density. */
int dw_rescale(struct dw_state *st, struct dw_window *wins, int count,
               int old_w, int old_h, int new_w, int new_h);

/* First layout at a dense panel: take the shell's 1280x720 design table and
   scale it to the panel. No-op (0) while the density mode is off, so 720p and
   smaller modes keep their historical pixel geometry. */
int dw_layout_from_design(struct dw_state *st, struct dw_window *wins, int count,
                          int design_w, int design_h, int panel_w, int panel_h);

#endif
