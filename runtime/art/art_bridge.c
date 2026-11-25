/* SPDX-License-Identifier: AGPL-3.0-only */
#include "art_bridge.h"
#include "sandbox.h"
#include "../../ported/developer/libs/flair_zlib/flair_zlib.h"
#include "../../include/binder.h"
#include "../../include/ashmem.h"
#include "../../include/kmem64.h"

static struct art_package pkgs[ART_MAX_PACKAGES];
static struct art_activity current_act;
static struct art_notification notifs[ART_MAX_NOTIFS];
static char runtime_abi[24];

static void s_copy(char *d, const char *s, int cap) {
    int i = 0;
    if (!d || cap <= 0) return;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int s_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void push_notif(const char *app, const char *msg) {
    for (int i = ART_MAX_NOTIFS - 1; i > 0; i--) notifs[i] = notifs[i - 1];
    notifs[0].used = 1;
    s_copy(notifs[0].app_label, app, sizeof(notifs[0].app_label));
    s_copy(notifs[0].text, msg, sizeof(notifs[0].text));
}

void art_bridge_init(const char *arch_abi) {
    s_copy(runtime_abi, arch_abi ? arch_abi : "arm64-v8a", sizeof(runtime_abi));
    for (int i = 0; i < ART_MAX_PACKAGES; i++) pkgs[i].used = 0;
    for (int i = 0; i < ART_MAX_NOTIFS; i++) notifs[i].used = 0;
    current_act.active = 0;
    current_act.state = ART_STATE_STOPPED;
    binder_init();
    ashmem_init();
    (void)binder_register("package_manager", 1, 0);
    (void)binder_register("activity_manager", 1, 0);
    (void)binder_register("surface_flinger", 1, 0);
}

const char *art_bridge_abi(void) { return runtime_abi; }

int art_pm_register_apk(const uint8_t *apk_bytes, uint32_t apk_len, uint32_t perms) {
    struct apk_info info;
    if (!apk_bytes || apk_len < 30u) return -1;
    if (apk_inspect(apk_bytes, apk_len, &info) != 0 || !info.has_dex) return -2;
    for (int i = 0; i < ART_MAX_PACKAGES; i++) {
        if (!pkgs[i].used) {
            uint32_t uid = 0;
            (void)sandbox_register_app(info.package_name, info.is_system_app, perms, &uid);
            pkgs[i].used = 1;
            pkgs[i].info = info;
            pkgs[i].apk_bytes = apk_bytes;
            pkgs[i].apk_len = apk_len;
            pkgs[i].permissions = perms;
            pkgs[i].installed = 1;
            return i;
        }
    }
    return -3;
}

int art_pm_count(void) {
    int c = 0;
    for (int i = 0; i < ART_MAX_PACKAGES; i++) if (pkgs[i].used) c++;
    return c;
}

const struct art_package *art_pm_get(int idx) {
    if (idx < 0 || idx >= ART_MAX_PACKAGES || !pkgs[idx].used) return 0;
    return &pkgs[idx];
}

int art_pm_find_by_package(const char *pkg_name) {
    if (!pkg_name) return -1;
    for (int i = 0; i < ART_MAX_PACKAGES; i++) {
        if (pkgs[i].used && s_eq(pkgs[i].info.package_name, pkg_name)) return i;
    }
    return -1;
}

int art_am_launch(int pkg_idx) {
    if (pkg_idx < 0 || pkg_idx >= ART_MAX_PACKAGES || !pkgs[pkg_idx].used) return -1;
    struct art_package *p = &pkgs[pkg_idx];
    uint32_t dcap = p->info.dex_size > 0 && p->info.dex_size <= 32768u ? p->info.dex_size + 64u : 32768u;
    uint8_t *dex_buf = (uint8_t *)kmem64_alloc(dcap);
    if (!dex_buf) return -2;
    struct apk_entry ent;
    int dlen = -1;
    if (apk_find_entry(p->apk_bytes, p->apk_len, "classes.dex", &ent) == 0) {
        if (ent.method == 0u) {
            dlen = apk_extract_stored(p->apk_bytes, p->apk_len, "classes.dex", dex_buf, dcap);
        } else if (ent.method == 8u && ent.uncomp_size <= dcap) {
            uint32_t out_sz = 0;
            if (flair_inflate_raw(p->apk_bytes + ent.data_offset, ent.comp_size, dex_buf, dcap, &out_sz) == 0)
                dlen = (int)out_sz;
        }
    }
    if (dlen <= 0) { kfree(dex_buf); return -3; }
    {
        uint32_t uid = 0;
        if (sandbox_register_app(p->info.package_name, p->info.is_system_app, p->permissions, &uid) == 0)
            sandbox_set_current_uid(uid);
    }

    struct dex_ui_ctx ui;
    int32_t ret = 0;
    if (dex_run_buffer(dex_buf, (uint32_t)dlen, &ui, &ret) != 0) {
        kfree(dex_buf);
        return -4;
    }
    kfree(dex_buf);

    current_act.active = 1;
    current_act.pkg_index = pkg_idx;
    current_act.state = ART_STATE_RESUMED;
    current_act.ui = ui;
    current_act.surface_id = ashmem_create("SurfaceFlinger:MainActivity", 65536u);
    { uint32_t bh = 0; (void)binder_lookup("activity_manager", &bh, 0); current_act.binder_handle = (int32_t)bh; }
    s_copy(current_act.action_status, ui.toast[0] ? ui.toast : "Activity resumed",
           sizeof(current_act.action_status));
    push_notif(ui.title[0] ? ui.title : p->info.app_label, current_act.action_status);
    return 0;
}

int art_am_launch_intent(const char *action, const char *pkg_name) {
    (void)action;
    int idx = art_pm_find_by_package(pkg_name);
    if (idx < 0) return -1;
    return art_am_launch(idx);
}

void art_am_pause(void) {
    if (current_act.active && current_act.state == ART_STATE_RESUMED)
        current_act.state = ART_STATE_PAUSED;
}

void art_am_resume(void) {
    if (current_act.active && current_act.state == ART_STATE_PAUSED)
        current_act.state = ART_STATE_RESUMED;
}

void art_am_stop(void) {
    current_act.active = 0;
    current_act.state = ART_STATE_STOPPED;
}

const struct art_activity *art_am_active(void) {
    return current_act.active ? &current_act : 0;
}

void art_am_trigger_button(int btn_idx) {
    if (!current_act.active || btn_idx < 0 || btn_idx >= current_act.ui.button_count) return;
    s_copy(current_act.action_status, "Binder IPC: ", sizeof(current_act.action_status));
    int i = 0;
    while (current_act.action_status[i]) i++;
    const char *b = current_act.ui.buttons[btn_idx];
    int j = 0;
    while (b[j] && i + 1 < (int)sizeof(current_act.action_status))
        current_act.action_status[i++] = b[j++];
    current_act.action_status[i] = 0;
    push_notif(current_act.ui.title, current_act.action_status);
}

int art_notif_count(void) {
    int c = 0;
    for (int i = 0; i < ART_MAX_NOTIFS; i++) if (notifs[i].used) c++;
    return c;
}

const struct art_notification *art_notif_get(int idx) {
    if (idx < 0 || idx >= ART_MAX_NOTIFS || !notifs[idx].used) return 0;
    return &notifs[idx];
}
