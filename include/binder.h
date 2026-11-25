#ifndef FLAIROS_BINDER_H
#define FLAIROS_BINDER_H

#include <stdint.h>

#define BINDER_MAX_SERVICES 16
#define BINDER_MAX_MSGS     32
#define BINDER_NAME_MAX     32
#define BINDER_PAYLOAD_MAX  64

struct binder_service {
    int used;
    int owner_pid;
    uint32_t handle;
    char name[BINDER_NAME_MAX];
};

struct binder_msg {
    int used;
    int sender_pid;
    int target_pid;
    uint32_t handle;
    uint32_t code;
    uint32_t len;
    uint8_t data[BINDER_PAYLOAD_MAX];
};

int binder_init(void);
int binder_register(const char *name, int owner_pid, uint32_t *handle_out);
int binder_lookup(const char *name, uint32_t *handle_out, int *owner_pid_out);
int binder_transact(int sender_pid, uint32_t handle, uint32_t code,
                    const void *data, uint32_t len);
int binder_recv(int target_pid, struct binder_msg *out);

#endif
