#ifndef FLAIROS_BOOT_H
#define FLAIROS_BOOT_H

#include "airos_types.h"

#define FLAIR_BOOT_INFO_MAGIC 0x544F4F42u /* "BOOT" */
#define FLAIR_BOOT_INFO_VERSION 1u
#define FLAIR_RECOVERY_MARKER_ADDR 0x4FE0u
#define FLAIR_RECOVERY_MARKER 0x52564359u

enum flair_boot_mode {
    FLAIR_BOOT_NORMAL = 0,
    FLAIR_BOOT_LIVE = 1,
    FLAIR_BOOT_SETUP = 2,
    FLAIR_BOOT_RECOVERY = 3,
    FLAIR_BOOT_DIAGNOSTICS = 4
};

void boot_init(void);
enum flair_boot_mode boot_mode(void);
int boot_set_mode(enum flair_boot_mode mode);
int boot_handoff_valid(void);
int boot_is_live_session(void);

#endif
