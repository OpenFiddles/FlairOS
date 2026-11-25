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
