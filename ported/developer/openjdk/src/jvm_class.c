/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS OpenJDK Portable 64-bit .class (0xCAFEBABE) Bytecode Interpreter */
#include "../include/jvm64.h"
#include "../../libs/flair_posix/flair_posix.h"

#define CP_MAX 128

struct cp_entry {
    uint8_t tag;
    uint16_t a;
    uint16_t b;
    uint32_t off;
    uint16_t len;
};

static uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void read_utf8(const uint8_t *buf, const struct cp_entry *cp, uint16_t cp_cnt,
                      uint16_t idx, char *out, int cap) {
    if (!out || cap <= 0) return;
    out[0] = 0;
    if (idx == 0 || idx >= cp_cnt || cp[idx].tag != 1u) return;
    uint16_t n = cp[idx].len;
    if (n + 1 > (uint16_t)cap) n = (uint16_t)(cap - 1);
    for (uint16_t i = 0; i < n; i++) out[i] = (char)buf[cp[idx].off + i];
    out[n] = 0;
}

static void push_line(struct jvm_run_result *out, const char *s) {
    if (!out || out->line_count >= JVM_MAX_OUT_LINES) return;
    int i = 0;
    while (s && s[i] && i + 1 < JVM_LINE_LEN) {
        out->lines[out->line_count][i] = s[i];
        i++;
    }
    out->lines[out->line_count][i] = 0;
    out->line_count++;
}

static void push_int_line(struct jvm_run_result *out, int32_t val) {
    char buf[20], tmp[16];
    int o = 0, n = 0;
    uint32_t u;
    if (val < 0) { buf[o++] = '-'; u = (uint32_t)(-(val + 1)) + 1u; }
    else u = (uint32_t)val;
    if (u == 0) tmp[n++] = '0';
    while (u > 0 && n < 15) { tmp[n++] = (char)('0' + (u % 10u)); u /= 10u; }
    while (n > 0 && o + 1 < (int)sizeof(buf)) buf[o++] = tmp[--n];
    buf[o] = 0;
    push_line(out, buf);
}

static int exec_code(const uint8_t *buf, const struct cp_entry *cp, uint16_t cp_cnt,
                     const uint8_t *code, uint32_t code_len, struct jvm_run_result *out) {
    int32_t stack[64];
    int32_t locals[16];
    int sp = 0;
    for (int i = 0; i < 16; i++) locals[i] = 0;
    uint32_t pc = 0;

    while (pc < code_len) {
        uint8_t op = code[pc++];
        if (op == 0x00u) { /* nop */ }
        else if (op == 0x01u) { stack[sp++] = 0; }
        else if (op >= 0x02u && op <= 0x08u) { stack[sp++] = (int32_t)op - 3; } /* iconst_m1..5 */
        else if (op == 0x10u) { if (pc >= code_len) return -3; stack[sp++] = (int8_t)code[pc++]; } /* bipush */
        else if (op == 0x11u) { if (pc + 1 >= code_len) return -3; stack[sp++] = (int16_t)be16(code + pc); pc += 2; } /* sipush */
        else if (op == 0x12u) { /* ldc */
            if (pc >= code_len) return -3;
            uint16_t idx = code[pc++];
            if (idx < cp_cnt && cp[idx].tag == 3u) stack[sp++] = (int32_t)be32(buf + cp[idx].off);
            else if (idx < cp_cnt && cp[idx].tag == 8u) stack[sp++] = (int32_t)cp[idx].a; /* String -> utf8 idx */
            else stack[sp++] = 0;
        }
        else if (op == 0x15u) { if (pc >= code_len) return -3; stack[sp++] = locals[code[pc++] & 15u]; }
        else if (op >= 0x1Au && op <= 0x1Du) { stack[sp++] = locals[op - 0x1Au]; } /* iload_0..3 */
        else if (op == 0x36u) { if (pc >= code_len) return -3; locals[code[pc++] & 15u] = stack[--sp]; }
        else if (op >= 0x3Bu && op <= 0x3Eu) { locals[op - 0x3Bu] = stack[--sp]; } /* istore_0..3 */
        else if (op == 0x57u) { if (sp > 0) sp--; } /* pop */
        else if (op == 0x59u) { stack[sp] = stack[sp - 1]; sp++; } /* dup */
        else if (op == 0x60u) { int32_t b = stack[--sp], a = stack[--sp]; stack[sp++] = a + b; } /* iadd */
        else if (op == 0x64u) { int32_t b = stack[--sp], a = stack[--sp]; stack[sp++] = a - b; } /* isub */
        else if (op == 0x68u) { int32_t b = stack[--sp], a = stack[--sp]; stack[sp++] = a * b; } /* imul */
        else if (op == 0x6Cu) { int32_t b = stack[--sp], a = stack[--sp]; stack[sp++] = b ? a / b : 0; } /* idiv */
        else if (op == 0x70u) { int32_t b = stack[--sp], a = stack[--sp]; stack[sp++] = b ? a % b : 0; } /* irem */
        else if (op == 0x74u) { stack[sp - 1] = -stack[sp - 1]; } /* ineg */
        else if (op == 0x84u) { /* iinc */
            if (pc + 1 >= code_len) return -3;
            locals[code[pc] & 15u] += (int8_t)code[pc + 1];
            pc += 2;
        }
        else if (op == 0xA7u) { /* goto */
            if (pc + 1 >= code_len) return -3;
            int16_t off = (int16_t)be16(code + pc);
            pc = (uint32_t)((int32_t)(pc - 1) + off);
        }
        else if (op == 0xB1u) { out->ret_val = 0; return 0; } /* return */
        else if (op == 0xACu) { out->ret_val = sp > 0 ? stack[--sp] : 0; return 0; } /* ireturn */
        else if (op == 0xB2u) { /* getstatic */
            if (pc + 1 >= code_len) return -3;
            pc += 2;
            stack[sp++] = 0x5359534Fu; /* System.out marker */
        }
        else if (op == 0xB6u || op == 0xB8u) { /* invokevirtual / invokestatic */
            if (pc + 1 >= code_len) return -3;
            uint16_t midx = be16(code + pc);
            pc += 2;
            if (midx < cp_cnt && cp[midx].tag == 10u) {
                uint16_t nt = cp[midx].b;
                char mname[32], desc[48];
                read_utf8(buf, cp, cp_cnt, cp[nt].a, mname, sizeof(mname));
                read_utf8(buf, cp, cp_cnt, cp[nt].b, desc, sizeof(desc));
                if (flair_strcmp(mname, "println") == 0) {
                    int32_t arg = sp > 0 ? stack[--sp] : 0;
                    if (op == 0xB6u && sp > 0) sp--; /* pop receiver */
                    if (flair_strcmp(desc, "(I)V") == 0) {
                        push_int_line(out, arg);
                    } else {
                        char str[JVM_LINE_LEN];
                        read_utf8(buf, cp, cp_cnt, (uint16_t)arg, str, sizeof(str));
                        push_line(out, str);
                    }
                }
            }
        }
        else {
            return -4;
        }
        if (sp < 0 || sp >= 63) return -5;
    }
    return 0;
}

int jvm_run_class(const uint8_t *buf, uint32_t len, struct jvm_run_result *out) {
    if (!buf || len < 16u || !out) return -1;
    if (be32(buf) != JVM_MAGIC) return -2;
    flair_memset(out, 0, sizeof(*out));
    out->minor_ver = be16(buf + 4);
    out->major_ver = be16(buf + 6);

    uint16_t cp_cnt = be16(buf + 8);
    if (cp_cnt == 0 || cp_cnt > CP_MAX) return -3;

    struct cp_entry cp[CP_MAX];
    flair_memset(cp, 0, sizeof(cp));
    uint32_t p = 10u;

    for (uint16_t i = 1; i < cp_cnt; i++) {
        if (p >= len) return -4;
        uint8_t tag = buf[p++];
        cp[i].tag = tag;
        if (tag == 1u) { /* Utf8 */
            if (p + 2 > len) return -4;
            uint16_t l = be16(buf + p); p += 2;
            if (p + l > len) return -4;
            cp[i].off = p; cp[i].len = l; p += l;
        } else if (tag == 3u || tag == 4u) {
            if (p + 4 > len) return -4;
            cp[i].off = p; p += 4;
        } else if (tag == 5u || tag == 6u) {
            if (p + 8 > len) return -4;
            cp[i].off = p; p += 8; i++;
        } else if (tag == 7u || tag == 8u || tag == 16u) {
            if (p + 2 > len) return -4;
            cp[i].a = be16(buf + p); p += 2;
        } else if (tag == 9u || tag == 10u || tag == 11u || tag == 12u || tag == 18u) {
            if (p + 4 > len) return -4;
            cp[i].a = be16(buf + p); cp[i].b = be16(buf + p + 2); p += 4;
        } else if (tag == 15u) {
            if (p + 3 > len) return -4;
            p += 3;
        } else {
            return -5;
        }
    }

    if (p + 6 > len) return -6;
    p += 2; /* access_flags */
    uint16_t this_cls = be16(buf + p); p += 2;
    p += 2; /* super_class */
    if (this_cls < cp_cnt && cp[this_cls].tag == 7u)
        read_utf8(buf, cp, cp_cnt, cp[this_cls].a, out->class_name, sizeof(out->class_name));

    uint16_t if_cnt = be16(buf + p); p += 2;
    if (p + if_cnt * 2u > len) return -6;
    p += if_cnt * 2u;

    uint16_t fld_cnt = be16(buf + p); p += 2;
    for (uint16_t i = 0; i < fld_cnt; i++) {
        if (p + 8 > len) return -6;
        uint16_t ac = be16(buf + p + 6); p += 8;
        for (uint16_t a = 0; a < ac; a++) {
            if (p + 6 > len) return -6;
            uint32_t al = be32(buf + p + 2); p += 6 + al;
        }
    }

    if (p + 2 > len) return -6;
    uint16_t m_cnt = be16(buf + p); p += 2;
    for (uint16_t i = 0; i < m_cnt; i++) {
        if (p + 8 > len) return -6;
        uint16_t name_idx = be16(buf + p + 2);
        uint16_t ac = be16(buf + p + 6);
        p += 8;
        char mname[32];
        read_utf8(buf, cp, cp_cnt, name_idx, mname, sizeof(mname));
        for (uint16_t a = 0; a < ac; a++) {
            if (p + 6 > len) return -6;
            uint16_t aname_idx = be16(buf + p);
            uint32_t al = be32(buf + p + 2);
            p += 6;
            if (p + al > len) return -6;
            char aname[16];
            read_utf8(buf, cp, cp_cnt, aname_idx, aname, sizeof(aname));
            if (flair_strcmp(aname, "Code") == 0 && flair_strcmp(mname, "main") == 0 && al >= 12u) {
                uint32_t clen = be32(buf + p + 4);
                if (8u + clen <= al)
                    return exec_code(buf, cp, cp_cnt, buf + p + 8, clen, out);
            }
            p += al;
        }
    }
    return -7;
}
