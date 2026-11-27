#ifndef FLAIROS_SHELL_COMMON_H
#define FLAIROS_SHELL_COMMON_H

#include "../../include/airgraph.h"
#include "../../include/input64.h"

struct shell_boot_info {
    const char *arch_name;
    const char *preset_name;
    int32_t width;
    int32_t height;
    /* Session component package (FlairTV.airp) when the image carries one. The
       TV shell reads its launcher layout from it; a null stage means the shell
       uses its built-in layout. */
    const uint8_t *session_pkg;
    uint32_t session_pkg_len;
};

void shell_render(struct ag_surface *surface, const struct shell_boot_info *info, int frame_idx);
int shell_handle_input(const struct input64_state *in, int32_t w, int32_t h);

int shell_tick(void);
int shell_tick_at(uint32_t now_ticks, uint32_t ticks_per_second);

#endif
