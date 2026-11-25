/* SPDX-License-Identifier: AGPL-3.0-only */
/* Android-style Per-App UID Sandbox & VFS Path Confinement */
#include "sandbox.h"
#include "../../include/vfs.h"

static struct sandbox_app g_apps[SANDBOX_MAX_APPS];
static uint32_t g_cur_uid = SANDBOX_UID_SYSTEM;
static int g_sb_inited = 0;

static void sb_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int sb_seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static int sb_starts(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return 0; }
    return 1;
}

void sandbox_init(void) {
    if (g_sb_inited) return;
    g_sb_inited = 1;
    for (int i = 0; i < SANDBOX_MAX_APPS; i++) g_apps[i].active = 0;
    (void)vfs_mkdir("/data");
    (void)vfs_mkdir("/data/data");
    (void)vfs_mkdir("/data/app");
    (void)vfs_mkdir("/system");
    (void)vfs_mkdir("/system/priv-app");
}

int sandbox_register_app(const char *pkg_name, int is_system, uint32_t perms, uint32_t *out_uid) {
    sandbox_init();
    if (!pkg_name || !pkg_name[0]) return -1;
    for (int i = 0; i < SANDBOX_MAX_APPS; i++) {
        if (g_apps[i].active && sb_seq(g_apps[i].pkg_name, pkg_name)) {
            if (out_uid) *out_uid = g_apps[i].uid;
            return 0;
        }
    }
    for (int i = 0; i < SANDBOX_MAX_APPS; i++) {
        if (!g_apps[i].active) {
            uint32_t uid = is_system ? SANDBOX_UID_SYSTEM : (SANDBOX_UID_APP_BASE + (uint32_t)i);
            g_apps[i].active = 1;
            g_apps[i].uid = uid;
            g_apps[i].perms = perms | (is_system ? SANDBOX_PERM_SYSTEM : 0u);
            sb_scpy(g_apps[i].pkg_name, pkg_name, sizeof(g_apps[i].pkg_name));
            sb_scpy(g_apps[i].data_dir, "/data/data/", sizeof(g_apps[i].data_dir));
            int l = 0; while (g_apps[i].data_dir[l]) l++;
            sb_scpy(g_apps[i].data_dir + l, pkg_name, (int)sizeof(g_apps[i].data_dir) - l);
            (void)vfs_mkdir(g_apps[i].data_dir);
            if (out_uid) *out_uid = uid;
            return 0;
        }
    }
    return -1;
}

void sandbox_set_current_uid(uint32_t uid) { g_cur_uid = uid; }
uint32_t sandbox_current_uid(void) { return g_cur_uid; }

const struct sandbox_app *sandbox_find_by_uid(uint32_t uid) {
    for (int i = 0; i < SANDBOX_MAX_APPS; i++) {
        if (g_apps[i].active && g_apps[i].uid == uid) return &g_apps[i];
    }
    return 0;
}

int sandbox_check_path(uint32_t uid, const char *path, int write_mode) {
    if (!path || path[0] != '/') return -1;
    if (uid < SANDBOX_UID_APP_BASE) return 0; /* System UID allowed */

    const struct sandbox_app *app = sandbox_find_by_uid(uid);
    if (!app) return -1;
    if (app->perms & SANDBOX_PERM_SYSTEM) return 0;

    /* Own private sandbox directory: full read/write */
    if (sb_starts(path, app->data_dir)) {
        int dl = 0; while (app->data_dir[dl]) dl++;
        if (path[dl] == 0 || path[dl] == '/') return 0;
    }

    /* Read-only system/app assets */
    if (!write_mode && (sb_starts(path, "/apps/") || sb_starts(path, "/system/"))) return 0;

    /* Shared storage requires SANDBOX_PERM_STORAGE */
    if ((app->perms & SANDBOX_PERM_STORAGE) && sb_starts(path, "/home/")) return 0;

    return -1;
}
