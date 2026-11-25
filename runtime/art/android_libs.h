#ifndef FLAIROS_ANDROID_LIBS_H
#define FLAIROS_ANDROID_LIBS_H

#include <stdint.h>

int android_log_print(int prio, const char *tag, const char *msg);
void *android_native_window_lock(int surf_id, int32_t *w, int32_t *h);
void android_native_window_unlock_and_post(int surf_id);
const char *android_jni_new_string_utf(const char *utf);
uintptr_t android_lookup_bionic_sym(const char *name);
int android_log_count(void);
const char *android_log_get(int idx);

#endif
