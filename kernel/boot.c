#include "../include/boot.h"

#define FLAIR_BOOT_INFO_ADDR 0x4FF0u
#define FLAIR_BOOT_MODE_ADDR 0x4FF8u

static enum flair_boot_mode active_mode = FLAIR_BOOT_NORMAL;
static int handoff_valid;

void boot_init(void) {
    volatile const u32 *info = (volatile const u32 *)FLAIR_BOOT_INFO_ADDR;
    handoff_valid = 0;
    active_mode = FLAIR_BOOT_NORMAL;
    if (info[0] != FLAIR_BOOT_INFO_MAGIC ||
        info[1] != FLAIR_BOOT_INFO_VERSION)
        return;
    u32 mode = *(volatile const u32 *)FLAIR_BOOT_MODE_ADDR;
    if (mode > FLAIR_BOOT_DIAGNOSTICS)
        return;
    active_mode = (enum flair_boot_mode)mode;
    handoff_valid = 1;
}

enum flair_boot_mode boot_mode(void) {
    return active_mode;
}

int boot_set_mode(enum flair_boot_mode mode) {
    if (mode < FLAIR_BOOT_NORMAL || mode > FLAIR_BOOT_DIAGNOSTICS)
        return -1;
    active_mode = mode;
    return 0;
}

int boot_handoff_valid(void) {
    return handoff_valid;
}

int boot_is_live_session(void) {
    return active_mode == FLAIR_BOOT_LIVE;
}
