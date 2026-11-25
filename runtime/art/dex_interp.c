/* SPDX-License-Identifier: AGPL-3.0-only */
/* Real Dalvik DEX Bytecode Interpreter for FlairOS ART */
#include "dex_interp.h"

static void di_scpy(char *d, const char *s, int cap) {
    int i = 0;
    while (s && s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    if (cap > 0) d[i] = 0;
}

static void di_log(struct dex_exec_result *out, const char *msg) {
    if (!out || out->log_count >= DEX_EXEC_MAX_LOGS) return;
    di_scpy(out->logs[out->log_count++], msg ? msg : "", 64);
}

int dex_interp_exec(const uint16_t *insns, uint32_t insn_count,
                    const char *const *str_pool, uint32_t str_count,
                    struct dex_exec_result *out) {
    if (!insns || insn_count == 0 || !out) return -1;
    out->ok = 0;
    out->steps = 0;
    out->ret_val = 0;
    out->log_count = 0;

    uint32_t regs[16];
    for (int i = 0; i < 16; i++) regs[i] = 0;

    uint32_t pc = 0;
    while (pc < insn_count && out->steps < 1024) {
        uint16_t w0 = insns[pc];
        uint8_t op = (uint8_t)(w0 & 0xFFu);
        out->steps++;

        if (op == 0x00u) { /* nop */
            pc += 1;
        } else if (op == 0x0Eu) { /* return-void */
            out->ok = 1;
            return 0;
        } else if (op == 0x0Fu) { /* return vAA */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0xFFu);
            out->ret_val = (int32_t)regs[vA & 0x0Fu];
            out->ok = 1;
            return 0;
        } else if (op == 0x12u) { /* const/4 vA, #+B */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0x0Fu);
            int8_t imm = (int8_t)((int16_t)w0 >> 12);
            regs[vA] = (uint32_t)(int32_t)imm;
            pc += 1;
        } else if (op == 0x13u && pc + 1 < insn_count) { /* const/16 vAA, #+BBBB */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0x0Fu);
            int16_t imm = (int16_t)insns[pc + 1];
            regs[vA] = (uint32_t)(int32_t)imm;
            pc += 2;
        } else if (op == 0x1Au && pc + 1 < insn_count) { /* const-string vAA, string@BBBB */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0x0Fu);
            uint16_t sidx = insns[pc + 1];
            regs[vA] = (uint32_t)sidx;
            pc += 2;
        } else if (op == 0x38u && pc + 1 < insn_count) { /* if-eqz vAA, +BBBB */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0x0Fu);
            int16_t off = (int16_t)insns[pc + 1];
            pc = (regs[vA] == 0) ? (uint32_t)((int32_t)pc + off) : (pc + 2);
        } else if ((op == 0x6Eu || op == 0x71u) && pc + 2 < insn_count) { /* invoke-virtual / invoke-static */
            uint16_t args = insns[pc + 2];
            uint8_t vC = (uint8_t)(args & 0x0Fu);
            uint32_t sidx = regs[vC];
            if (str_pool && sidx < str_count) di_log(out, str_pool[sidx]);
            pc += 3;
        } else if ((op == 0x90u || op == 0x91u || op == 0x92u) && pc + 1 < insn_count) { /* add/sub/mul-int */
            uint8_t vA = (uint8_t)((w0 >> 8) & 0x0Fu);
            uint16_t w1 = insns[pc + 1];
            int32_t a = (int32_t)regs[w1 & 0x0Fu];
            int32_t b = (int32_t)regs[(w1 >> 8) & 0x0Fu];
            if (op == 0x90u) regs[vA] = (uint32_t)(a + b);
            else if (op == 0x91u) regs[vA] = (uint32_t)(a - b);
            else regs[vA] = (uint32_t)(a * b);
            pc += 2;
        } else {
            return -1;
        }
    }
    return -1;
}
