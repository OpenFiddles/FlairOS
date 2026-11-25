#ifndef FLAIROS_DEX_INTERP_H
#define FLAIROS_DEX_INTERP_H

#include <stdint.h>

#define DEX_EXEC_MAX_LOGS 8

struct dex_exec_result {
    int ok;
    int steps;
    int32_t ret_val;
    int log_count;
    char logs[DEX_EXEC_MAX_LOGS][64];
};

int dex_interp_exec(const uint16_t *insns, uint32_t insn_count,
                    const char *const *str_pool, uint32_t str_count,
                    struct dex_exec_result *out);

#endif
