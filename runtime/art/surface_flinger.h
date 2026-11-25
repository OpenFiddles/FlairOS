#ifndef FLAIROS_SURFACE_FLINGER_H
#define FLAIROS_SURFACE_FLINGER_H

#include <stdint.h>
#include "../../include/airgraph.h"

#define SF_MAX_SURFACES 8
#define SF_MAX_JNI_NATIVES 32

struct sf_surface {
    int id;
    int active;
    int dirty;
    int32_t width;
    int32_t height;
    uint32_t *pixels;
    char tag[32];
};

void sf_init(void);
int sf_create_surface(const char *tag, int32_t w, int32_t h);
uint32_t *sf_lock_buffer(int id, int32_t *out_w, int32_t *out_h);
void sf_unlock_and_post(int id);
void sf_composite_to(int id, struct ag_surface *dst, int32_t dx, int32_t dy);
void sf_destroy_surface(int id);
int sf_active_count(void);
int sf_find_by_tag(const char *tag);

int art_jni_register(const char *cls, const char *method, void *fn_ptr);
void *art_jni_lookup(const char *cls, const char *method);

#endif
