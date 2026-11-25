/* SPDX-License-Identifier: AGPL-3.0-only */
/* SurfaceFlinger / ANativeWindow Buffer Compositor + ART JNI Registry */
#include "surface_flinger.h"
#include "../../include/kmem64.h"

static struct sf_surface g_surfs[SF_MAX_SURFACES];
static int g_sf_inited = 0;

struct jni_entry {
    int used;
    char cls[48];
    char method[32];
    void *fn;
};
static struct jni_entry g_jni[SF_MAX_JNI_NATIVES];

static void sf_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static int sf_seq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

void sf_init(void) {
    if (g_sf_inited) return;
    g_sf_inited = 1;
    for (int i = 0; i < SF_MAX_SURFACES; i++) {
        g_surfs[i].id = i;
        g_surfs[i].active = 0;
        g_surfs[i].pixels = 0;
    }
    for (int i = 0; i < SF_MAX_JNI_NATIVES; i++) g_jni[i].used = 0;
}

int sf_create_surface(const char *tag, int32_t w, int32_t h) {
    sf_init();
    if (w <= 0 || h <= 0 || w > 1024 || h > 768) return -1;
    for (int i = 0; i < SF_MAX_SURFACES; i++) {
        if (!g_surfs[i].active) {
            size_t bytes = (size_t)w * (size_t)h * 4u;
            uint32_t *px = (uint32_t *)kmem64_alloc(bytes);
            if (!px) return -1;
            for (size_t k = 0; k < (size_t)w * (size_t)h; k++) px[k] = 0xFF101828u;
            g_surfs[i].active = 1;
            g_surfs[i].dirty = 1;
            g_surfs[i].width = w;
            g_surfs[i].height = h;
            g_surfs[i].pixels = px;
            sf_scpy(g_surfs[i].tag, tag ? tag : "ANativeWindow", 32);
            return i;
        }
    }
    return -1;
}

uint32_t *sf_lock_buffer(int id, int32_t *out_w, int32_t *out_h) {
    if (id < 0 || id >= SF_MAX_SURFACES || !g_surfs[id].active) return 0;
    if (out_w) *out_w = g_surfs[id].width;
    if (out_h) *out_h = g_surfs[id].height;
    return g_surfs[id].pixels;
}

void sf_unlock_and_post(int id) {
    if (id < 0 || id >= SF_MAX_SURFACES || !g_surfs[id].active) return;
    g_surfs[id].dirty = 1;
}

void sf_composite_to(int id, struct ag_surface *dst, int32_t dx, int32_t dy) {
    if (id < 0 || id >= SF_MAX_SURFACES || !g_surfs[id].active || !dst || !dst->pixels) return;
    const struct sf_surface *sf = &g_surfs[id];
    int32_t pitch_px = dst->pitch / 4;
    for (int32_t y = 0; y < sf->height; y++) {
        int32_t ty = dy + y;
        if (ty < 0 || ty >= dst->height) continue;
        for (int32_t x = 0; x < sf->width; x++) {
            int32_t tx = dx + x;
            if (tx < 0 || tx >= dst->width) continue;
            dst->pixels[ty * pitch_px + tx] = sf->pixels[y * sf->width + x];
        }
    }
    g_surfs[id].dirty = 0;
}

void sf_destroy_surface(int id) {
    if (id < 0 || id >= SF_MAX_SURFACES || !g_surfs[id].active) return;
    
    if (g_surfs[id].pixels) kfree(g_surfs[id].pixels);
    g_surfs[id].pixels = 0;
    g_surfs[id].active = 0;
}

int sf_find_by_tag(const char *tag) {
    if (!tag || !tag[0]) return -1;
    for (int i = 0; i < SF_MAX_SURFACES; i++) {
        if (g_surfs[i].active && sf_seq(g_surfs[i].tag, tag)) return i;
    }
    return -1;
}

int sf_active_count(void) {
    int c = 0;
    for (int i = 0; i < SF_MAX_SURFACES; i++) if (g_surfs[i].active) c++;
    return c;
}

int art_jni_register(const char *cls, const char *method, void *fn_ptr) {
    sf_init();
    if (!cls || !method || !fn_ptr) return -1;
    for (int i = 0; i < SF_MAX_JNI_NATIVES; i++) {
        if (!g_jni[i].used) {
            g_jni[i].used = 1;
            sf_scpy(g_jni[i].cls, cls, 48);
            sf_scpy(g_jni[i].method, method, 32);
            g_jni[i].fn = fn_ptr;
            return 0;
        }
    }
    return -1;
}

void *art_jni_lookup(const char *cls, const char *method) {
    if (!cls || !method) return 0;
    for (int i = 0; i < SF_MAX_JNI_NATIVES; i++) {
        if (g_jni[i].used && sf_seq(g_jni[i].cls, cls) && sf_seq(g_jni[i].method, method))
            return g_jni[i].fn;
    }
    return 0;
}
