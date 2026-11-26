#include "../../include/dex.h"

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void fmt_int(int32_t val, char *out, uint32_t max_out) {
    char tmp[16];
    int n = 0;
    uint32_t u;
    uint32_t o = 0;
    if (!out || max_out == 0) return;
    if (val < 0) {
        if (o + 1 < max_out) out[o++] = '-';
        u = (uint32_t)(-(val + 1)) + 1u;
    } else {
        u = (uint32_t)val;
    }
    if (u == 0) tmp[n++] = '0';
    while (u > 0 && n < 15) {
        tmp[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (n > 0 && o + 1 < max_out) out[o++] = tmp[--n];
    out[o] = 0;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static void str_copy(char *dst, const char *src, uint32_t cap) {
    uint32_t i = 0;
    if (!dst || cap == 0) return;
    while (src && src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static uint32_t read_uleb128(const uint8_t *p, uint32_t max_len, uint32_t *adv) {
    uint32_t val = 0, shift = 0, i = 0;
    while (i < 5 && i < max_len) {
        uint8_t b = p[i++];
        val |= (uint32_t)(b & 0x7Fu) << shift;
        if (!(b & 0x80u)) break;
        shift += 7;
    }
    if (adv) *adv = i;
    return val;
}

int dex_validate(const uint8_t *buf, uint32_t len, struct dex_header *out) {
    const struct dex_header *h;
    if (!buf || len < DEX_HEADER_SIZE) return -1;
    h = (const struct dex_header *)buf;
    if (h->magic[0] != 'd' || h->magic[1] != 'e' || h->magic[2] != 'x' || h->magic[3] != '\n' ||
        h->magic[7] != 0)
        return -2;
    if (h->header_size != DEX_HEADER_SIZE) return -3;
    if (h->endian_tag != 0x12345678u) return -4;
    if (h->file_size > len || h->file_size < DEX_HEADER_SIZE) return -5;
    if (out) *out = *h;
    return 0;
}

int dex_read_string(const uint8_t *dex, uint32_t len, uint32_t str_idx,
                    char *out, uint32_t max_out) {
    struct dex_header hdr;
    uint32_t str_off, adv = 0, ulen, i = 0;
    if (!out || max_out == 0 || dex_validate(dex, len, &hdr) != 0) return -1;
    if (str_idx >= hdr.string_ids_size) return -2;
    if (hdr.string_ids_off + (str_idx + 1u) * 4u > len) return -3;
    str_off = rd32(dex + hdr.string_ids_off + str_idx * 4u);
    if (str_off >= len) return -4;
    ulen = read_uleb128(dex + str_off, len - str_off, &adv);
    str_off += adv;
    while (i < ulen && i + 1 < max_out && str_off + i < len && dex[str_off + i]) {
        out[i] = (char)dex[str_off + i];
        i++;
    }
    out[i] = 0;
    return (int)i;
}

static int dex_exec_internal(const uint8_t *dex, uint32_t dex_len,
                             const uint16_t *insns, uint32_t insn_count,
                             struct dex_ui_ctx *ui, int32_t *ret_out) {
    int32_t v[16];
    uint32_t pc = 0, steps = 0;
    if (!insns || insn_count == 0) return -1;
    for (int i = 0; i < 16; i++) v[i] = 0;
    while (pc < insn_count && steps++ < 4096u) {
        uint16_t w = insns[pc];
        uint8_t op = (uint8_t)(w & 0xFFu);
        uint8_t a = (uint8_t)((w >> 8) & 0x0Fu);
        uint8_t b = (uint8_t)((w >> 12) & 0x0Fu);
        uint8_t aa = (uint8_t)((w >> 8) & 0xFFu);

        if (op == 0x00u) {
            pc++;
        } else if (op == 0x01u) { /* move vA, vB */
            v[a] = v[b];
            pc++;
        } else if (op == 0x0Eu) { /* return-void */
            if (ret_out) *ret_out = 0;
            return 0;
        } else if (op == 0x0Fu) { /* return vAA */
            if (aa >= 16u) return -2;
            if (ret_out) *ret_out = v[aa];
            return 0;
        } else if (op == 0x12u) { /* const/4 vA, #+B */
            int32_t imm = (b & 0x08u) ? (int32_t)(b | 0xFFFFFFF0u) : (int32_t)b;
            v[a] = imm;
            pc++;
        } else if (op == 0x13u) { /* const/16 vAA, #+BBBB */
            if (pc + 1u >= insn_count || aa >= 16u) return -3;
            v[aa] = (int16_t)insns[pc + 1u];
            pc += 2u;
        } else if (op == 0x14u) { /* const vAA, #+BBBBBBBB */
            if (pc + 2u >= insn_count || aa >= 16u) return -3;
            v[aa] = (int32_t)((uint32_t)insns[pc + 1u] | ((uint32_t)insns[pc + 2u] << 16));
            pc += 3u;
        } else if (op == 0x1Au) { /* const-string vAA, string@BBBB */
            if (pc + 1u >= insn_count || aa >= 16u) return -3;
            v[aa] = (int32_t)insns[pc + 1u]; /* store string index in register */
            pc += 2u;
        } else if (op == 0x28u) { /* goto +AA */
            int8_t off = (int8_t)aa;
            pc = (uint32_t)((int32_t)pc + off);
        } else if (op == 0x29u) { /* goto/16 +AAAA */
            if (pc + 1u >= insn_count) return -3;
            pc = (uint32_t)((int32_t)pc + (int16_t)insns[pc + 1u]);
        } else if (op >= 0x32u && op <= 0x37u) { /* if-test vA, vB, +CCCC */
            int cond = 0;
            if (pc + 1u >= insn_count) return -3;
            int16_t off = (int16_t)insns[pc + 1u];
            if (op == 0x32u) cond = (v[a] == v[b]);
            else if (op == 0x33u) cond = (v[a] != v[b]);
            else if (op == 0x34u) cond = (v[a] < v[b]);
            else if (op == 0x35u) cond = (v[a] >= v[b]);
            else if (op == 0x36u) cond = (v[a] > v[b]);
            else cond = (v[a] <= v[b]);
            pc = cond ? (uint32_t)((int32_t)pc + off) : (pc + 2u);
        } else if (op >= 0x38u && op <= 0x3Du) { /* if-testz vAA, +BBBB */
            int cond = 0;
            if (pc + 1u >= insn_count || aa >= 16u) return -3;
            int16_t off = (int16_t)insns[pc + 1u];
            if (op == 0x38u) cond = (v[aa] == 0);
            else if (op == 0x39u) cond = (v[aa] != 0);
            else if (op == 0x3Au) cond = (v[aa] < 0);
            else if (op == 0x3Bu) cond = (v[aa] >= 0);
            else if (op == 0x3Cu) cond = (v[aa] > 0);
            else cond = (v[aa] <= 0);
            pc = cond ? (uint32_t)((int32_t)pc + off) : (pc + 2u);
        } else if (op == 0x6Eu || op == 0x71u) { /* invoke-virtual / invoke-static */
            if (pc + 2u >= insn_count) return -3;
            uint16_t midx = insns[pc + 1u];
            uint16_t regs = insns[pc + 2u];
            uint8_t argc = (uint8_t)((w >> 12) & 0x0Fu);
            uint8_t r0 = (uint8_t)(regs & 0x0Fu);
            uint8_t r1 = (uint8_t)((regs >> 4) & 0x0Fu);
            if (dex && ui) {
                struct dex_header hdr;
                if (dex_validate(dex, dex_len, &hdr) == 0 &&
                    midx < hdr.method_ids_size &&
                    hdr.method_ids_off + (uint32_t)(midx + 1u) * 8u <= dex_len) {
                    uint32_t name_idx = rd32(dex + hdr.method_ids_off + (uint32_t)midx * 8u + 4u);
                    char mname[32], arg_str[DEX_UI_LINE_LEN];
                    mname[0] = 0; arg_str[0] = 0;
                    (void)dex_read_string(dex, dex_len, name_idx, mname, sizeof(mname));
                    uint8_t sreg = (op == 0x71u || argc == 1u) ? r0 : r1;
                    if (sreg < 16u && v[sreg] >= 0)
                        (void)dex_read_string(dex, dex_len, (uint32_t)v[sreg], arg_str, sizeof(arg_str));
                    if (str_eq(mname, "setTitle")) {
                        str_copy(ui->title, arg_str, sizeof(ui->title));
                    } else if (str_eq(mname, "setSubtitle")) {
                        str_copy(ui->subtitle, arg_str, sizeof(ui->subtitle));
                    } else if (str_eq(mname, "setBadge")) {
                        str_copy(ui->badge, arg_str, sizeof(ui->badge));
                    } else if (str_eq(mname, "setText") || str_eq(mname, "setContentText") ||
                               str_eq(mname, "println")) {
                        if (ui->line_count < DEX_UI_MAX_LINES)
                            str_copy(ui->lines[ui->line_count++], arg_str, DEX_UI_LINE_LEN);
                    } else if (str_eq(mname, "printlnInt")) {
                        if (ui->line_count < DEX_UI_MAX_LINES && sreg < 16u) {
                            char num[20];
                            fmt_int(v[sreg], num, sizeof(num));
                            str_copy(ui->lines[ui->line_count++], num, DEX_UI_LINE_LEN);
                        }
                    } else if (str_eq(mname, "addButton")) {
                        if (ui->button_count < DEX_UI_MAX_BTNS)
                            str_copy(ui->buttons[ui->button_count++], arg_str, 24);
                    } else if (str_eq(mname, "showToast")) {
                        str_copy(ui->toast, arg_str, sizeof(ui->toast));
                    }
                }
            }
            pc += 3u;
        } else if (op >= 0x90u && op <= 0x97u) { /* binop vAA, vBB, vCC */
            if (pc + 1u >= insn_count) return -3;
            uint16_t ops = insns[pc + 1u];
            uint8_t bb = (uint8_t)(ops & 0xFFu);
            uint8_t cc = (uint8_t)((ops >> 8) & 0xFFu);
            if (aa >= 16u || bb >= 16u || cc >= 16u) return -2;
            if (op == 0x90u) v[aa] = v[bb] + v[cc];
            else if (op == 0x91u) v[aa] = v[bb] - v[cc];
            else if (op == 0x92u) v[aa] = v[bb] * v[cc];
            else if (op == 0x93u) v[aa] = v[cc] ? (v[bb] / v[cc]) : 0;
            else if (op == 0x94u) v[aa] = v[cc] ? (v[bb] % v[cc]) : 0;
            else if (op == 0x95u) v[aa] = v[bb] & v[cc];
            else if (op == 0x96u) v[aa] = v[bb] | v[cc];
            else v[aa] = v[bb] ^ v[cc];
            pc += 2u;
        } else if (op >= 0xB0u && op <= 0xB7u) { /* binop/2addr vA, vB */
            if (op == 0xB0u) v[a] += v[b];
            else if (op == 0xB1u) v[a] -= v[b];
            else if (op == 0xB2u) v[a] *= v[b];
            else if (op == 0xB3u) v[a] = v[b] ? (v[a] / v[b]) : 0;
            else if (op == 0xB4u) v[a] = v[b] ? (v[a] % v[b]) : 0;
            else if (op == 0xB5u) v[a] &= v[b];
            else if (op == 0xB6u) v[a] |= v[b];
            else v[a] ^= v[b];
            pc++;
        } else if (op == 0xD8u) { /* add-int/lit8 vAA, vBB, #+CC */
            if (pc + 1u >= insn_count || aa >= 16u) return -3;
            uint16_t ops = insns[pc + 1u];
            uint8_t bb = (uint8_t)(ops & 0xFFu);
            int8_t cc = (int8_t)((ops >> 8) & 0xFFu);
            if (bb >= 16u) return -2;
            v[aa] = v[bb] + cc;
            pc += 2u;
        } else {
            return -4;
        }
    }
    return -5;
}

int dex_exec_bytecode(const uint16_t *insns, uint32_t insn_count, int32_t *ret_out) {
    return dex_exec_internal(0, 0, insns, insn_count, 0, ret_out);
}

int dex_run_buffer(const uint8_t *dex, uint32_t len, struct dex_ui_ctx *ui,
                   int32_t *ret_out) {
    struct dex_header hdr;
    if (dex_validate(dex, len, &hdr) != 0) return -1;
    if (ui) {
        str_copy(ui->title, "Android App", sizeof(ui->title));
        ui->subtitle[0] = 0;
        ui->badge[0] = 0;
        ui->line_count = 0;
        ui->button_count = 0;
        ui->toast[0] = 0;
    }
    if (hdr.class_defs_size == 0 || hdr.class_defs_off + 32u > len) return -2;
    uint32_t class_data_off = rd32(dex + hdr.class_defs_off + 24u);
    if (class_data_off == 0 || class_data_off >= len) return -3;
    /* Parse class_data_item: static_fields, instance_fields, direct_methods, virtual_methods */
    uint32_t p = class_data_off, adv = 0;
    uint32_t sf = read_uleb128(dex + p, len - p, &adv); p += adv;
    uint32_t inf = read_uleb128(dex + p, len - p, &adv); p += adv;
    uint32_t dm = read_uleb128(dex + p, len - p, &adv); p += adv;
    uint32_t vm = read_uleb128(dex + p, len - p, &adv); p += adv;
    for (uint32_t i = 0; i < sf + inf && p < len; i++) {
        (void)read_uleb128(dex + p, len - p, &adv); p += adv;
        (void)read_uleb128(dex + p, len - p, &adv); p += adv;
    }
    /* Run first method with non-zero code_off (prefer virtual/onCreate if present) */
    uint32_t target_code_off = 0;
    for (uint32_t i = 0; i < dm + vm && p < len; i++) {
        (void)read_uleb128(dex + p, len - p, &adv); p += adv;
        (void)read_uleb128(dex + p, len - p, &adv); p += adv;
        uint32_t code_off = read_uleb128(dex + p, len - p, &adv); p += adv;
        if (code_off != 0 && (i >= dm || target_code_off == 0))
            target_code_off = code_off;
    }
    if (target_code_off == 0 || target_code_off + 16u > len) return -4;
    uint32_t insns_size = rd32(dex + target_code_off + 12u);
    if (insns_size == 0 || target_code_off + 16u + insns_size * 2u > len) return -5;
    return dex_exec_internal(dex, len, (const uint16_t *)(dex + target_code_off + 16u),
                             insns_size, ui, ret_out);
}
