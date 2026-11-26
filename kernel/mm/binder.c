#include "../../include/binder.h"

#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
static void *b_alloc(unsigned int sz) { return malloc(sz); }
#else
#include "../../include/kheap.h"
static void *b_alloc(unsigned int sz) { return kmalloc(sz); }
#endif

static struct binder_service *services;
static struct binder_msg *msgs;
static uint32_t next_handle = 1;

static int b_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void b_copy(char *dst, const char *src) {
    int i = 0;
    if (src) {
        while (src[i] && i < BINDER_NAME_MAX - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

int binder_init(void) {
    if (!services) services = (struct binder_service *)b_alloc(sizeof(struct binder_service) * BINDER_MAX_SERVICES);
    if (!msgs) msgs = (struct binder_msg *)b_alloc(sizeof(struct binder_msg) * BINDER_MAX_MSGS);
    if (!services || !msgs) return -1;
    for (int i = 0; i < BINDER_MAX_SERVICES; i++) services[i].used = 0;
    for (int i = 0; i < BINDER_MAX_MSGS; i++) msgs[i].used = 0;
    return 0;
}

int binder_register(const char *name, int owner_pid, uint32_t *handle_out) {
    if ((!services && binder_init() != 0) || !name || !name[0] || owner_pid <= 0)
        return -1;
    for (int i = 0; i < BINDER_MAX_SERVICES; i++) {
        if (services[i].used && b_eq(services[i].name, name))
            return -1; /* duplicate */
    }
    for (int i = 0; i < BINDER_MAX_SERVICES; i++) {
        if (!services[i].used) {
            services[i].used = 1;
            services[i].owner_pid = owner_pid;
            services[i].handle = next_handle++;
            b_copy(services[i].name, name);
            if (handle_out) *handle_out = services[i].handle;
            return 0;
        }
    }
    return -1;
}

int binder_lookup(const char *name, uint32_t *handle_out, int *owner_pid_out) {
    if (!services || !name) return -1;
    for (int i = 0; i < BINDER_MAX_SERVICES; i++) {
        if (services[i].used && b_eq(services[i].name, name)) {
            if (handle_out) *handle_out = services[i].handle;
            if (owner_pid_out) *owner_pid_out = services[i].owner_pid;
            return 0;
        }
    }
    return -1;
}

int binder_transact(int sender_pid, uint32_t handle, uint32_t code,
                    const void *data, uint32_t len) {
    if (!services || !msgs || handle == 0 || len > BINDER_PAYLOAD_MAX)
        return -1;
    int target_pid = -1;
    for (int i = 0; i < BINDER_MAX_SERVICES; i++) {
        if (services[i].used && services[i].handle == handle) {
            target_pid = services[i].owner_pid;
            break;
        }
    }
    if (target_pid < 0) return -1;
    for (int i = 0; i < BINDER_MAX_MSGS; i++) {
        if (!msgs[i].used) {
            msgs[i].used = 1;
            msgs[i].sender_pid = sender_pid;
            msgs[i].target_pid = target_pid;
            msgs[i].handle = handle;
            msgs[i].code = code;
            msgs[i].len = len;
            const uint8_t *src = (const uint8_t *)data;
            for (uint32_t j = 0; j < len && src; j++) msgs[i].data[j] = src[j];
            return 0;
        }
    }
    return -1;
}

int binder_recv(int target_pid, struct binder_msg *out) {
    if (!msgs || !out) return -1;
    for (int i = 0; i < BINDER_MAX_MSGS; i++) {
        if (msgs[i].used && msgs[i].target_pid == target_pid) {
            *out = msgs[i];
            msgs[i].used = 0;
            return 1;
        }
    }
    return 0;
}
