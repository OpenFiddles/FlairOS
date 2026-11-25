/* SPDX-License-Identifier: AGPL-3.0-only */
/* Android Native Libs (liblog, libandroid, JNIEnv, Bionic helpers) */
#include "android_libs.h"
#include "surface_flinger.h"
#include "../../include/kmem64.h"

#define ALOG_MAX 16
static char g_alogs[ALOG_MAX][64];
static int g_alog_cnt = 0;

static void al_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int al_seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

int android_log_print(int prio, const char *tag, const char *msg) {
    (void)prio;
    if (g_alog_cnt >= ALOG_MAX) {
        for (int i = 0; i < ALOG_MAX - 1; i++) al_scpy(g_alogs[i], g_alogs[i + 1], 64);
        g_alog_cnt = ALOG_MAX - 1;
    }
    char line[64];
    int p = 0;
    if (tag && tag[0]) {
        al_scpy(line, tag, 20);
        while (line[p]) p++;
        if (p + 2 < 64) { line[p++] = ':'; line[p++] = ' '; }
    }
    al_scpy(line + p, msg ? msg : "", 64 - p);
    al_scpy(g_alogs[g_alog_cnt++], line, 64);
    return 0;
}

void *android_native_window_lock(int surf_id, int32_t *w, int32_t *h) {
    return sf_lock_buffer(surf_id, w, h);
}

void android_native_window_unlock_and_post(int surf_id) {
    sf_unlock_and_post(surf_id);
}

const char *android_jni_new_string_utf(const char *utf) {
    if (!utf) return 0;
    int len = 0;
    while (utf[len]) len++;
    char *copy = (char *)kmem64_alloc((size_t)(len + 1));
    if (!copy) return 0;
    al_scpy(copy, utf, len + 1);
    return copy;
}

uintptr_t android_lookup_bionic_sym(const char *name) {
    if (!name) return 0;
    if (al_seq(name, "__android_log_print")) return (uintptr_t)android_log_print;
    if (al_seq(name, "ANativeWindow_lock")) return (uintptr_t)android_native_window_lock;
    if (al_seq(name, "ANativeWindow_unlockAndPost")) return (uintptr_t)android_native_window_unlock_and_post;
    if (al_seq(name, "JNI_NewStringUTF")) return (uintptr_t)android_jni_new_string_utf;
    return 0;
}

int android_log_count(void) { return g_alog_cnt; }
const char *android_log_get(int idx) {
    return (idx >= 0 && idx < g_alog_cnt) ? g_alogs[idx] : "";
}
