#ifndef FLAIROS_ART_BRIDGE_H
#define FLAIROS_ART_BRIDGE_H

#include <stdint.h>
#include "../../include/apk.h"
#include "../../include/dex.h"

#define ART_MAX_PACKAGES 12
#define ART_MAX_INTENTS  8
#define ART_MAX_NOTIFS   6

enum art_lifecycle_state {
    ART_STATE_STOPPED = 0,
    ART_STATE_CREATED = 1,
    ART_STATE_STARTED = 2,
    ART_STATE_RESUMED = 3,
    ART_STATE_PAUSED  = 4
};

struct art_package {
    int used;
    struct apk_info info;
    const uint8_t *apk_bytes;
    uint32_t apk_len;
    uint32_t permissions;
    int installed;
};

struct art_activity {
    int active;
    int pkg_index;
    enum art_lifecycle_state state;
    struct dex_ui_ctx ui;
    int32_t surface_id;
    int32_t binder_handle;
    char action_status[64];
};

struct art_notification {
    int used;
    char app_label[32];
    char text[64];
};

void art_bridge_init(const char *arch_abi);
const char *art_bridge_abi(void);
int art_pm_register_apk(const uint8_t *apk_bytes, uint32_t apk_len, uint32_t perms);
int art_pm_count(void);
const struct art_package *art_pm_get(int idx);
int art_pm_find_by_package(const char *pkg_name);

int art_am_launch(int pkg_idx);
int art_am_launch_intent(const char *action, const char *pkg_name);
void art_am_pause(void);
void art_am_resume(void);
void art_am_stop(void);
const struct art_activity *art_am_active(void);
void art_am_trigger_button(int btn_idx);

int art_notif_count(void);
const struct art_notification *art_notif_get(int idx);

#endif
