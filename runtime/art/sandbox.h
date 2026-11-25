#ifndef FLAIROS_SANDBOX_H
#define FLAIROS_SANDBOX_H

#include <stdint.h>

#define SANDBOX_UID_SYSTEM   1000u
#define SANDBOX_UID_APP_BASE 10000u
#define SANDBOX_MAX_APPS     16

#define SANDBOX_PERM_INTERNET (1u << 0)
#define SANDBOX_PERM_STORAGE  (1u << 1)
#define SANDBOX_PERM_SYSTEM   (1u << 2)

struct sandbox_app {
    int active;
    uint32_t uid;
    uint32_t perms;
    char pkg_name[64];
    char data_dir[96];
};

void sandbox_init(void);
int sandbox_register_app(const char *pkg_name, int is_system, uint32_t perms, uint32_t *out_uid);
void sandbox_set_current_uid(uint32_t uid);
uint32_t sandbox_current_uid(void);
int sandbox_check_path(uint32_t uid, const char *path, int write_mode);
const struct sandbox_app *sandbox_find_by_uid(uint32_t uid);

#endif
