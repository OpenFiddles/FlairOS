#ifndef FLAIROS_AIRSTIN_UI_H
#define FLAIROS_AIRSTIN_UI_H

#include <stdint.h>
#include "../../include/airgraph.h"
#include "../../include/apk.h"
#include "../../include/vfs.h"
#include "../../include/telemetry_contract.h"

struct airstin_installer {
    int active;
    int installing;
    int progress; /* 0..100 */
    char path[VFS_PATH_MAX];
    char stage[48];
    struct apk_info info;
};

/* Layout scale for the shared SystemUI. It is 1.0 (Q16) unless a session sets
   it, so desktop and mobile draw the same pixels they always have; a TV session
   sets it to its ten-foot layout scale so the drawer grows with the panel. */
#define AIRSTIN_UNIT_Q16 (1 << 16)
#define AIRSTIN_SCALE_MAX_Q16 (3 * (1 << 16))

void airstin_ui_set_scale_q16(int32_t scale_q16);
int32_t airstin_ui_scale_q16(void);

/* Drawer geometry in surface pixels, derived from the scale above. Both the
   renderer and the hit test read it, so they cannot disagree at any scale. */
struct airstin_cc_geometry {
    int32_t scale_q16;
    int32_t x, y, w, h;   /* panel rect at rest */
    int32_t row_h;        /* connection row height */
    int32_t title_glyph;  /* glyph scale for the drawer title */
    int32_t label_glyph;  /* glyph scale for row and button labels */
};

void airstin_cc_geometry_for(int32_t surface_w, struct airstin_cc_geometry *out);

void airstin_ui_init(void);
int airstin_ui_tick(void);
void airstin_ui_begin_install(const char *path, const struct apk_info *info);
void airstin_ui_cancel_install(void);
int airstin_ui_is_installing(void);
int airstin_ui_install_done(void);
const struct airstin_installer *airstin_ui_installer(void);

enum cc_subview airstin_cc_view(void);
void airstin_cc_set_view(enum cc_subview v);
void airstin_cc_back(void);
int airstin_cc_handle_click(int32_t mx, int32_t my, int32_t w,
                            int *anim_on, int *theme_idx,
                            int *spotlight_open, int *cc_open);

void airstin_draw_cell_icon(struct ag_surface *s, int32_t x, int32_t y, int bars, int active, uint32_t rgb);
void airstin_render_status_cluster(struct ag_surface *s, int32_t w, const char *arch_name, int cc_open);
void airstin_render_control_center(struct ag_surface *s, int32_t w, int cc_open, int cc_anim,
                                   int anim_on, int theme_idx);
void airstin_render_installer(struct ag_surface *s, int32_t w, int32_t h);

struct airstin_power_state {
    int modal_open;
    int taps;
    int shutting_down;
    int forced;
    int countdown;
    int halted;
};

void airstin_power_open_modal(void);
void airstin_power_cancel(void);
void airstin_power_tap_shutdown(void);
void airstin_power_force_hold(void);
const struct airstin_power_state *airstin_power_state(void);
void airstin_render_power_modal(struct ag_surface *s, int32_t w, int32_t h);

#endif
