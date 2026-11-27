#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../../include/dalvik.h"

/* A deliberately small Dalvik interpreter.
 *
 * It decodes real DEX data: string/type/proto/method/class tables, class_data
 * (uleb128), code_item, try/catch handlers and the instruction stream. It
 * interprets calls into methods of the same DEX directly and lowers calls into
 * a tiny host class library (Object/String/StringBuilder/Integer/Math) so that
 * real methods can produce observable results without an Android framework.
 * Everything it cannot do aborts the current run with a diagnostic.
 */

#define DVM_STR_BASE   0x20000000u   /* handle = STR_BASE | dex string index */
#define DVM_OBJ_BASE   0x40000000u   /* handle = OBJ_BASE | heap slot       */
#define DVM_SYN_BASE   0x60000000u   /* handle = SYN_BASE | synthetic string */
#define DVM_MAX_SYN    64
#define DVM_MAX_STATIC 64

struct dvm_object {
    uint32_t class_idx;
    uint32_t field_ids[DVM_MAX_FIELDS];
    int32_t  field_vals[DVM_MAX_FIELDS];
    int      nfields;
    char     text[160];          /* StringBuilder contents */
};

struct dvm_frame {
    uint32_t method_idx;
    uint32_t regs_size;
    int32_t  regs[DVM_MAX_REGISTERS];
};

struct dvm {
    const uint8_t *buf;
    uint32_t len;

    uint32_t string_ids_size, string_ids_off;
    uint32_t type_ids_size, type_ids_off;
    uint32_t proto_ids_size, proto_ids_off;
    uint32_t method_ids_size, method_ids_off;
    uint32_t class_defs_size, class_defs_off;

    struct dvm_object heap[DVM_MAX_OBJECTS];
    int nobj;

    char     syn[DVM_MAX_SYN][160];
    int      nsyn;

    uint32_t static_ids[DVM_MAX_STATIC];
    int32_t  static_vals[DVM_MAX_STATIC];
    int      nstatic;

    int      has_exc;
    int      exc_caught;
    char     exc_class[64];
    int32_t  last_result;
    uint32_t last_object;

    char     trap[96];
    uint32_t insns;
    uint32_t calls;
};

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t uleb(const uint8_t *p, uint32_t *used) {
    uint32_t val = 0, shift = 0, i = 0;
    for (; i < 5; i++) {
        uint8_t b = p[i];
        val |= (uint32_t)(b & 0x7Fu) << shift;
        if (!(b & 0x80u)) { i++; break; }
        shift += 7;
    }
    if (used) *used = i;
    return val;
}

static int32_t sleb(const uint8_t *p, uint32_t *used) {
    int32_t val = 0;
    uint32_t shift = 0, i = 0;
    uint8_t b = 0;
    for (; i < 5; i++) {
        b = p[i];
        val |= (int32_t)(b & 0x7Fu) << shift;
        shift += 7;
        if (!(b & 0x80u)) { i++; break; }
    }
    if (shift < 32 && (b & 0x40u)) val |= -(1 << shift);
    if (used) *used = i;
    return val;
}

/* ---- DEX table access ------------------------------------------------- */

static int dex_string(struct dvm *vm, uint32_t idx, char *out, uint32_t cap) {
    if (!out || cap == 0) return -1;
    out[0] = 0;
    if (idx >= vm->string_ids_size) return -1;
    uint32_t off = rd32(vm->buf + vm->string_ids_off + idx * 4u);
    if (off >= vm->len) return -1;
    uint32_t used = 0;
    (void)uleb(vm->buf + off, &used);          /* utf16 length, unused */
    const uint8_t *p = vm->buf + off + used;
    uint32_t o = 0;
    while (p < vm->buf + vm->len && *p && o + 1 < cap) {
        uint8_t c = *p++;
        if (c < 0x80u) {
            out[o++] = (char)c;
        } else if ((c & 0xE0u) == 0xC0u && p < vm->buf + vm->len) {
            out[o++] = (char)(((c & 0x1Fu) << 6) | (*p++ & 0x3Fu));
        } else if ((c & 0xF0u) == 0xE0u && p + 1 < vm->buf + vm->len) {
            uint8_t c2 = *p++, c3 = *p++;
            out[o++] = (char)(((c & 0x0Fu) << 12) | ((c2 & 0x3Fu) << 6) | (c3 & 0x3Fu));
        } else {
            break;
        }
    }
    out[o] = 0;
    return 0;
}

static int dex_type_name(struct dvm *vm, uint32_t type_idx, char *out, uint32_t cap) {
    if (type_idx >= vm->type_ids_size) return -1;
    uint32_t sidx = rd32(vm->buf + vm->type_ids_off + type_idx * 4u);
    return dex_string(vm, sidx, out, cap);
}

/* Descriptor of a proto's parameters and return type, e.g. "(ILjava/lang/String;)V". */
static int proto_desc(struct dvm *vm, uint32_t proto_idx, char *out, uint32_t cap) {
    if (proto_idx >= vm->proto_ids_size) return -1;
    const uint8_t *pe = vm->buf + vm->proto_ids_off + proto_idx * 12u;
    uint32_t ret_type = rd32(pe + 4);
    uint32_t params_off = rd32(pe + 8);
    uint32_t o = 0;
    if (o + 1 < cap) out[o++] = '(';
    if (params_off) {
        uint32_t count = rd32(vm->buf + params_off);
        for (uint32_t i = 0; i < count && o + 1 < cap; i++) {
            uint32_t t = rd16(vm->buf + params_off + 4u + i * 2u);
            char tn[96];
            if (dex_type_name(vm, t, tn, sizeof tn) != 0) return -1;
            uint32_t tl = (uint32_t)strlen(tn);
            if (o + tl + 1 >= cap) return -1;
            memcpy(out + o, tn, tl);
            o += tl;
        }
    }
    if (o + 1 < cap) out[o++] = ')';
    char rt[96];
    if (dex_type_name(vm, ret_type, rt, sizeof rt) != 0) return -1;
    uint32_t rl = (uint32_t)strlen(rt);
    if (o + rl + 1 >= cap) return -1;
    memcpy(out + o, rt, rl);
    o += rl;
    out[o] = 0;
    return 0;
}

static int method_name(struct dvm *vm, uint32_t method_idx, char *out, uint32_t cap) {
    if (method_idx >= vm->method_ids_size) return -1;
    uint32_t sidx = rd32(vm->buf + vm->method_ids_off + method_idx * 8u + 4u);
    return dex_string(vm, sidx, out, cap);
}

static int method_class(struct dvm *vm, uint32_t method_idx, char *out, uint32_t cap) {
    if (method_idx >= vm->method_ids_size) return -1;
    uint32_t t = rd16(vm->buf + vm->method_ids_off + method_idx * 8u);
    return dex_type_name(vm, t, out, cap);
}

static int method_code_off(struct dvm *vm, uint32_t method_idx, uint32_t *code_off,
                           uint32_t *access_flags) {
    *code_off = 0;
    uint32_t cls_type = rd16(vm->buf + vm->method_ids_off + method_idx * 8u);
    for (uint32_t i = 0; i < vm->class_defs_size; i++) {
        const uint8_t *cd = vm->buf + vm->class_defs_off + i * 32u;
        if (rd32(cd) != cls_type) continue;
        uint32_t data_off = rd32(cd + 24);
        if (!data_off) return -1;
        const uint8_t *p = vm->buf + data_off;
        uint32_t used = 0;
        uint32_t sf = uleb(p, &used); p += used;
        uint32_t inf = uleb(p, &used); p += used;
        uint32_t dm = uleb(p, &used); p += used;
        uint32_t vmcount = uleb(p, &used); p += used;
        for (uint32_t f = 0; f < sf + inf; f++) {
            (void)uleb(p, &used); p += used;
            uint32_t a = uleb(p, &used); p += used;
            (void)a;
        }
        uint32_t cur = 0;
        for (uint32_t m = 0; m < dm + vmcount; m++) {
            uint32_t diff = uleb(p, &used); p += used;
            cur += diff;
            uint32_t acc = uleb(p, &used); p += used;
            uint32_t co = uleb(p, &used); p += used;
            if (cur == method_idx) {
                if (access_flags) *access_flags = acc;
                *code_off = co;
                return 0;
            }
        }
        return -1;
    }
    return -1;
}

/* ---- values ----------------------------------------------------------- */

static int is_obj(int32_t v) { return (uint32_t)v >= DVM_OBJ_BASE && (uint32_t)v < DVM_OBJ_BASE + DVM_MAX_OBJECTS; }
static int is_str(int32_t v) {
    uint32_t u = (uint32_t)v;
    return (u >= DVM_STR_BASE && u < DVM_STR_BASE + 0x10000000u) ||
           (u >= DVM_SYN_BASE && u < DVM_SYN_BASE + DVM_MAX_SYN);
}

static int value_string(struct dvm *vm, int32_t v, char *out, uint32_t cap) {
    uint32_t u = (uint32_t)v;
    if (u >= DVM_SYN_BASE && (u - DVM_SYN_BASE) < (uint32_t)vm->nsyn) {
        strncpy(out, vm->syn[u - DVM_SYN_BASE], cap - 1);
        out[cap - 1] = 0;
        return 0;
    }
    if (u >= DVM_STR_BASE) {
        uint32_t idx = u - DVM_STR_BASE;
        return dex_string(vm, idx, out, cap);
    }
    out[0] = 0;
    return -1;
}

static int32_t make_syn_string(struct dvm *vm, const char *s) {
    if (vm->nsyn >= DVM_MAX_SYN) return 0;
    strncpy(vm->syn[vm->nsyn], s, sizeof vm->syn[0] - 1);
    vm->syn[vm->nsyn][sizeof vm->syn[0] - 1] = 0;
    return (int32_t)(DVM_SYN_BASE | (uint32_t)vm->nsyn++);
}

static int32_t alloc_object(struct dvm *vm, uint32_t class_idx) {
    if (vm->nobj >= DVM_MAX_OBJECTS) return 0;
    struct dvm_object *o = &vm->heap[vm->nobj];
    memset(o, 0, sizeof *o);
    o->class_idx = class_idx;
    return (int32_t)(DVM_OBJ_BASE | (uint32_t)vm->nobj++);
}

static struct dvm_object *object_at(struct dvm *vm, int32_t v) {
    if (!is_obj(v)) return NULL;
    return &vm->heap[(uint32_t)v - DVM_OBJ_BASE];
}

static void obj_field_set(struct dvm *vm, int32_t objv, uint32_t field_idx, int32_t val) {
    struct dvm_object *o = object_at(vm, objv);
    if (!o) return;
    for (int i = 0; i < o->nfields; i++)
        if (o->field_ids[i] == field_idx) { o->field_vals[i] = val; return; }
    if (o->nfields < DVM_MAX_FIELDS) {
        o->field_ids[o->nfields] = field_idx;
        o->field_vals[o->nfields] = val;
        o->nfields++;
    }
}

static int32_t obj_field_get(struct dvm *vm, int32_t objv, uint32_t field_idx) {
    struct dvm_object *o = object_at(vm, objv);
    if (!o) return 0;
    for (int i = 0; i < o->nfields; i++)
        if (o->field_ids[i] == field_idx) return o->field_vals[i];
    return 0;
}

static void static_set(struct dvm *vm, uint32_t field_idx, int32_t val) {
    for (int i = 0; i < vm->nstatic; i++)
        if (vm->static_ids[i] == field_idx) { vm->static_vals[i] = val; return; }
    if (vm->nstatic < DVM_MAX_STATIC) {
        vm->static_ids[vm->nstatic] = field_idx;
        vm->static_vals[vm->nstatic] = val;
        vm->nstatic++;
    }
}

static int32_t static_get(struct dvm *vm, uint32_t field_idx) {
    for (int i = 0; i < vm->nstatic; i++)
        if (vm->static_ids[i] == field_idx) return vm->static_vals[i];
    return 0;
}

static void trap(struct dvm *vm, const char *what, uint32_t pc, uint32_t opcode) {
    if (vm->trap[0]) return;
    snprintf(vm->trap, sizeof vm->trap, "%s at pc=%u (opcode 0x%02x)", what, pc, opcode);
}

static void raise_exc(struct dvm *vm, const char *cls) {
    vm->has_exc = 1;
    strncpy(vm->exc_class, cls, sizeof vm->exc_class - 1);
    vm->exc_class[sizeof vm->exc_class - 1] = 0;
}

/* ---- execution -------------------------------------------------------- */

static int dvm_exec(struct dvm *vm, uint32_t method_idx, const int32_t *args,
                    int nargs, struct dvm_result *res, int depth);

/* Find a catch handler for `pc` in the code item at `code`. Returns handler pc
 * or -1. */
static int find_handler(struct dvm *vm, const uint8_t *code, uint32_t pc_start) {
    (void)vm;
    uint16_t tries = rd16(code + 6);
    uint32_t insns_size = rd32(code + 12);
    if (!tries) return -1;
    uint32_t try_base = 16u + insns_size * 2u + ((insns_size & 1u) ? 2u : 0u);
    const uint8_t *handlers = code + try_base + (uint32_t)tries * 8u;
    for (uint16_t t = 0; t < tries; t++) {
        const uint8_t *ti = code + try_base + (uint32_t)t * 8u;
        uint32_t start = rd32(ti);
        uint16_t count = rd16(ti + 4);
        uint16_t hoff = rd16(ti + 6);
        if (pc_start < start || pc_start >= start + count) continue;
        const uint8_t *h = handlers + hoff;
        uint32_t used = 0;
        int32_t size = sleb(h, &used);
        h += used;
        int pairs = size < 0 ? -size : size;
        int first_typed = -1;
        for (int i = 0; i < pairs; i++) {
            (void)uleb(h, &used); h += used;         /* exception type_idx */
            uint32_t addr = uleb(h, &used); h += used;
            if (first_typed < 0) first_typed = (int)addr;
        }
        if (size <= 0) {
            uint32_t addr = uleb(h, &used);          /* catch-all */
            return (int)addr;
        }
        /* No exception-type matching yet: we only raise a couple of host
         * exceptions, so the first typed handler is taken. */
        if (first_typed >= 0) return first_typed;
    }
    return -1;
}

/* Host class library: only what running real code needs, nothing invented
 * beyond Object/String/StringBuilder/Integer/Math. */
static int call_intrinsic(struct dvm *vm, const char *cls, const char *name,
                          const char *sig, const int32_t *args, int nargs,
                          int32_t *ret, int *is_ref, uint32_t *pc_hint) {
    (void)pc_hint;
    *ret = 0;
    *is_ref = 0;

    if (strcmp(cls, "Ljava/lang/Object;") == 0 && strcmp(name, "<init>") == 0)
        return 0;

    if (strcmp(cls, "Ljava/lang/String;") == 0) {
        if (strcmp(name, "length") == 0) {
            char tmp[160];
            *ret = value_string(vm, args[0], tmp, sizeof tmp) == 0 ? (int32_t)strlen(tmp) : 0;
            return 0;
        }
        if (strcmp(name, "isEmpty") == 0) {
            char tmp[160];
            *ret = (value_string(vm, args[0], tmp, sizeof tmp) == 0 && tmp[0]) ? 0 : 1;
            return 0;
        }
        if (strcmp(name, "charAt") == 0) {
            char tmp[160];
            int idx = nargs > 1 ? args[1] : 0;
            if (value_string(vm, args[0], tmp, sizeof tmp) == 0 &&
                idx >= 0 && (size_t)idx < strlen(tmp)) {
                *ret = (unsigned char)tmp[idx];
                return 0;
            }
            raise_exc(vm, "java/lang/StringIndexOutOfBoundsException");
            return 1;
        }
        if (strcmp(name, "equals") == 0 || strcmp(name, "contentEquals") == 0) {
            char a[160], b[160];
            int oka = value_string(vm, args[0], a, sizeof a) == 0;
            int okb = value_string(vm, nargs > 1 ? args[1] : 0, b, sizeof b) == 0;
            *ret = (oka && okb && strcmp(a, b) == 0) ? 1 : 0;
            return 0;
        }
        if (strcmp(name, "hashCode") == 0) {
            char a[160];
            int h = 0;
            if (value_string(vm, args[0], a, sizeof a) == 0)
                for (const char *p = a; *p; p++) h = h * 31 + (unsigned char)*p;
            *ret = h;
            return 0;
        }
    }

    if (strcmp(cls, "Ljava/lang/StringBuilder;") == 0) {
        struct dvm_object *o = nargs ? object_at(vm, args[0]) : NULL;
        if (strcmp(name, "<init>") == 0) {
            if (o) o->text[0] = 0;
            return 0;
        }
        if (strcmp(name, "append") == 0) {
            if (!o) { raise_exc(vm, "java/lang/NullPointerException"); return 1; }
            char add[160];
            int32_t a = nargs > 1 ? args[1] : 0;
            if (is_str(a)) {
                if (value_string(vm, a, add, sizeof add) != 0) add[0] = 0;
            } else if (a == 0 && nargs > 1) {
                strcpy(add, "null");
            } else {
                snprintf(add, sizeof add, "%d", (int)a);
            }
            size_t used = strlen(o->text);
            strncat(o->text, add, sizeof o->text - used - 1);
            *ret = args[0];
            *is_ref = 1;
            return 0;
        }
        if (strcmp(name, "toString") == 0) {
            *ret = make_syn_string(vm, o ? o->text : "");
            *is_ref = 1;
            return 0;
        }
        if (strcmp(name, "length") == 0) {
            *ret = o ? (int32_t)strlen(o->text) : 0;
            return 0;
        }
    }

    if (strcmp(cls, "Ljava/lang/Integer;") == 0) {
        if (strcmp(name, "parseInt") == 0) {
            char tmp[160];
            if (value_string(vm, args[0], tmp, sizeof tmp) != 0) {
                raise_exc(vm, "java/lang/NumberFormatException");
                return 1;
            }
            const char *p = tmp;
            int neg = 0;
            if (*p == '-') { neg = 1; p++; }
            if (!*p) { raise_exc(vm, "java/lang/NumberFormatException"); return 1; }
            long v = 0;
            for (; *p; p++) {
                if (*p < '0' || *p > '9') {
                    raise_exc(vm, "java/lang/NumberFormatException");
                    return 1;
                }
                v = v * 10 + (*p - '0');
                if (v > 0x7fffffffL) { raise_exc(vm, "java/lang/NumberFormatException"); return 1; }
            }
            *ret = (int32_t)(neg ? -v : v);
            return 0;
        }
        if (strcmp(name, "toString") == 0 || strcmp(name, "valueOf") == 0) {
            char tmp[32];
            snprintf(tmp, sizeof tmp, "%d", nargs ? (int)args[nargs - 1] : 0);
            *ret = make_syn_string(vm, tmp);
            *is_ref = 1;
            return 0;
        }
        if (strcmp(name, "intValue") == 0) { *ret = nargs ? args[0] : 0; return 0; }
    }

    if (strcmp(cls, "Ljava/lang/Math;") == 0) {
        if (strcmp(name, "max") == 0) { *ret = args[0] > args[1] ? args[0] : args[1]; return 0; }
        if (strcmp(name, "min") == 0) { *ret = args[0] < args[1] ? args[0] : args[1]; return 0; }
        if (strcmp(name, "abs") == 0) { *ret = args[0] < 0 ? -args[0] : args[0]; return 0; }
    }

    snprintf(vm->trap, sizeof vm->trap, "no intrinsic for %s->%s%s", cls, name, sig);
    return -1;
}

static int dvm_exec(struct dvm *vm, uint32_t method_idx, const int32_t *args,
                    int nargs, struct dvm_result *res, int depth) {
    if (depth > DVM_MAX_FRAMES) return DVM_ERR_DEPTH;

    uint32_t code_off = 0;
    if (method_code_off(vm, method_idx, &code_off, NULL) != 0 || !code_off)
        return DVM_ERR_NO_CODE;

    const uint8_t *code = vm->buf + code_off;
    uint32_t regs_size = rd16(code);
    uint32_t ins_size = rd16(code + 2);
    uint32_t insns_size = rd32(code + 12);
    const uint16_t *insns = (const uint16_t *)(code + 16);

    if (regs_size > DVM_MAX_REGISTERS) {
        snprintf(vm->trap, sizeof vm->trap, "method needs %u registers", regs_size);
        return DVM_ERR_TRAP;
    }

    struct dvm_frame frame_storage;
    struct dvm_frame *fr = &frame_storage;
    memset(fr, 0, sizeof *fr);
    fr->method_idx = method_idx;
    fr->regs_size = regs_size;
    if (ins_size > regs_size) {
        snprintf(vm->trap, sizeof vm->trap, "code_item ins=%u > regs=%u", ins_size, regs_size);
        return DVM_ERR_TRAP;
    }
    if (nargs > (int)ins_size) {
        snprintf(vm->trap, sizeof vm->trap, "call passes %d args, method takes %u", nargs, ins_size);
        return DVM_ERR_TRAP;
    }
    uint32_t base = regs_size - ins_size;
    for (int i = 0; i < nargs; i++)
        fr->regs[base + (uint32_t)i] = args[i];

    uint32_t pc = 0;
    vm->has_exc = 0;
    vm->exc_caught = 0;
    vm->exc_class[0] = 0;

    for (;;) {
        if (pc >= insns_size) return DVM_ERR_TRAP;
        uint16_t u = insns[pc];
        uint8_t op = (uint8_t)(u & 0xffu);
        uint8_t aa = (uint8_t)(u >> 8);
        vm->insns++;

        switch (op) {
        case 0x00: pc++; break;                            /* nop / payload */
        case 0x01: case 0x07: case 0x04:                   /* move, move-object */
            fr->regs[aa] = fr->regs[(insns[pc] >> 12) & 0xf]; pc++; break;
        case 0x02: case 0x08:
            fr->regs[aa] = fr->regs[insns[pc + 1]]; pc += 2; break;
        case 0x03: case 0x09:
            fr->regs[insns[pc + 1]] = fr->regs[insns[pc + 2]]; pc += 3; break;
        case 0x0a: case 0x0b: case 0x0c:                   /* move-result* */
            fr->regs[aa] = vm->last_result; vm->last_result = 0; pc++; break;
        case 0x0d:                                         /* move-exception */
            fr->regs[aa] = vm->has_exc ? make_syn_string(vm, vm->exc_class) : 0;
            vm->has_exc = 0;
            pc++;
            break;
        case 0x0e:                                         /* return-void */
            if (res) res->value = 0;
            return DVM_OK;
        case 0x0f: case 0x10:                              /* return */
            if (res) res->value = fr->regs[aa];
            return DVM_OK;
        case 0x11:                                         /* return-object */
            if (res) {
                res->object = (uint32_t)fr->regs[aa];
                res->value = 0;
                char tmp[120];
                if (is_str(fr->regs[aa]) && value_string(vm, fr->regs[aa], tmp, sizeof tmp) == 0) {
                    strncpy(res->string, tmp, sizeof res->string - 1);
                    res->string[sizeof res->string - 1] = 0;
                }
            }
            return DVM_OK;
        case 0x12:                                         /* const/4 */
            fr->regs[(insns[pc] >> 8) & 0xf] = (int32_t)((int8_t)((insns[pc] >> 12) << 4)) >> 4;
            pc++; break;
        case 0x13:                                         /* const/16 */
            fr->regs[aa] = (int16_t)insns[pc + 1]; pc += 2; break;
        case 0x14:                                         /* const */
            fr->regs[aa] = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
            pc += 3; break;
        case 0x15:                                         /* const/high16 */
            fr->regs[aa] = (int32_t)((uint32_t)insns[pc + 1] << 16); pc += 2; break;
        case 0x1a:                                         /* const-string */
            fr->regs[aa] = (int32_t)(DVM_STR_BASE | (uint32_t)insns[pc + 1]);
            pc += 2; break;
        case 0x1b:                                         /* const-string/jumbo */
            fr->regs[aa] = (int32_t)(DVM_STR_BASE |
                ((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16)));
            pc += 3; break;
        case 0x1c:                                         /* const-class */
            trap(vm, "const-class (no Class objects)", pc, op);
            return DVM_ERR_TRAP;
        case 0x1d: case 0x1e:                              /* monitor-* */
            pc++; break;
        case 0x1f:                                         /* check-cast */
            pc += 2; break;
        case 0x20: {                                       /* instance-of */
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)((insns[pc] >> 12) & 0xf);
            uint16_t type_idx = insns[pc + 1];
            struct dvm_object *o = object_at(vm, fr->regs[b]);
            fr->regs[a] = (o && o->class_idx == type_idx) ? 1 : 0;
            pc += 2; break;
        }
        case 0x21: {                                       /* array-length */
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            fr->regs[a] = 0;
            trap(vm, "array-length (no array model)", pc, op);
            return DVM_ERR_TRAP;
        }
        case 0x22:                                         /* new-instance */
            fr->regs[aa] = alloc_object(vm, insns[pc + 1]);
            pc += 2; break;
        case 0x23: case 0x24: case 0x26:                   /* new-array family */
            trap(vm, "array construction (no array model)", pc, op);
            return DVM_ERR_TRAP;
        case 0x27:                                         /* throw */
            vm->has_exc = 1;
            if (!vm->exc_class[0]) strcpy(vm->exc_class, "java/lang/Throwable");
            vm->last_result = fr->regs[aa];
            {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { vm->exc_caught = 1; pc = (uint32_t)h; vm->has_exc = 1; break; }
            }
            return 1;                                      /* propagate */
        case 0x28: {                                       /* goto */
            int8_t off = (int8_t)aa;
            pc = (uint32_t)((int32_t)pc + off);
            break;
        }
        case 0x29: {                                       /* goto/16 */
            int16_t off = (int16_t)insns[pc + 1];
            pc = (uint32_t)((int32_t)pc + off);
            break;
        }
        case 0x2a: {                                       /* goto/32 */
            int32_t off = (int32_t)((uint32_t)insns[pc + 1] | ((uint32_t)insns[pc + 2] << 16));
            pc = (uint32_t)((int32_t)pc + off);
            break;
        }
        case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37: {
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)(insns[pc] >> 12);
            int16_t off = (int16_t)insns[pc + 1];
            int32_t x = fr->regs[a], y = fr->regs[b];
            int take = 0;
            switch (op) {
            case 0x32: take = x == y; break;
            case 0x33: take = x != y; break;
            case 0x34: take = x < y; break;
            case 0x35: take = x >= y; break;
            case 0x36: take = x > y; break;
            case 0x37: take = x <= y; break;
            }
            pc = take ? (uint32_t)((int32_t)pc + off) : pc + 2;
            break;
        }
        case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: {
            uint8_t a = (uint8_t)(insns[pc] >> 8);
            int16_t off = (int16_t)insns[pc + 1];
            int32_t x = fr->regs[a];
            int take = 0;
            switch (op) {
            case 0x38: take = x == 0; break;
            case 0x39: take = x != 0; break;
            case 0x3a: take = x < 0; break;
            case 0x3b: take = x >= 0; break;
            case 0x3c: take = x > 0; break;
            case 0x3d: take = x <= 0; break;
            }
            pc = take ? (uint32_t)((int32_t)pc + off) : pc + 2;
            break;
        }
        case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58: {
            /* iget / iget-object / iget-boolean ... : 22c */
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)(insns[pc] >> 12);
            uint16_t field_idx = insns[pc + 1];
            fr->regs[a] = obj_field_get(vm, fr->regs[b], field_idx);
            pc += 2; break;
        }
        case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)(insns[pc] >> 12);
            uint16_t field_idx = insns[pc + 1];
            obj_field_set(vm, fr->regs[b], field_idx, fr->regs[a]);
            pc += 2; break;
        }
        case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
            fr->regs[aa] = static_get(vm, insns[pc + 1]);
            pc += 2; break;
        case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d:
            static_set(vm, insns[pc + 1], fr->regs[aa]);
            pc += 2; break;
        case 0x6e: case 0x6f: case 0x70: case 0x71: case 0x72: {
            /* invoke-* 35c: A|G|op BBBB F|E|D|C.
             * The argument count A and the fifth register G live in the high
             * byte of the first code unit; C..F are nibbles of the last unit. */
            uint16_t midx = insns[pc + 1];
            uint16_t regs = insns[pc + 2];
            int n = (aa >> 4) & 0xf;
            uint8_t g_reg = aa & 0xf;
            int32_t argv[8];
            int argc = 0;
            const uint8_t arg_regs[5] = {
                (uint8_t)(regs & 0xf), (uint8_t)((regs >> 4) & 0xf),
                (uint8_t)((regs >> 8) & 0xf), (uint8_t)((regs >> 12) & 0xf), g_reg
            };
            if (n > 5) {
                snprintf(vm->trap, sizeof vm->trap, "invoke 35c with %d args", n);
                return DVM_ERR_TRAP;
            }
            for (int i = 0; i < n; i++)
                argv[argc++] = fr->regs[arg_regs[i]];
            char cls[96], name[64], sig[160];
            if (method_class(vm, midx, cls, sizeof cls) != 0 ||
                method_name(vm, midx, name, sizeof name) != 0) {
                trap(vm, "bad method id", pc, op);
                return DVM_ERR_TRAP;
            }
            uint32_t proto_idx = rd16(vm->buf + vm->method_ids_off + midx * 8u + 2u);
            if (proto_desc(vm, proto_idx, sig, sizeof sig) != 0) sig[0] = 0;

            vm->calls++;
            struct dvm_result sub;
            memset(&sub, 0, sizeof sub);
            int rc = dvm_exec(vm, midx, argv, argc, &sub, depth + 1);
            if (rc == DVM_ERR_NO_CODE) {
                int32_t ret = 0; int is_ref = 0;
                rc = call_intrinsic(vm, cls, name, sig, argv, argc, &ret, &is_ref, &pc);
                if (rc < 0) { pc += 3; return DVM_ERR_TRAP; }
                if (rc == 1) {                             /* host exception */
                    int h = find_handler(vm, code, pc);
                    if (h >= 0) { vm->exc_caught = 1; pc = (uint32_t)h; break; }
                    return 1;
                }
                if (is_ref) vm->last_result = ret;
                else vm->last_result = ret;
                pc += 3;
                break;
            }
            if (rc == 0) {
                vm->last_result = (int32_t)sub.object ? (int32_t)sub.object : sub.value;
                if (!sub.object) vm->last_result = sub.value;
                pc += 3;
                break;
            }
            if (rc == 1) {
                /* exception propagated from callee: try this frame's handlers */
                int h = find_handler(vm, code, pc);
                if (h >= 0) { vm->exc_caught = 1; pc = (uint32_t)h; vm->has_exc = 1; break; }
                return 1;
            }
            if (rc == DVM_ERR_UNCAUGHT) return 1;
            return rc;
        }
        case 0x74: case 0x75: case 0x76: case 0x77: case 0x78: {
            uint16_t midx = insns[pc + 1];
            uint16_t first = insns[pc + 2];
            uint8_t count = aa;
            int32_t argv[8];
            if (count > 8u || (uint32_t)first + count > regs_size) {
                snprintf(vm->trap, sizeof vm->trap,
                         "invoke-range with %u args from v%u (regs=%u)", count, first, regs_size);
                return DVM_ERR_TRAP;
            }
            for (int i = 0; i < count; i++) argv[i] = fr->regs[first + i];
            char cls[96], name[64], sig[160];
            if (method_class(vm, midx, cls, sizeof cls) != 0 ||
                method_name(vm, midx, name, sizeof name) != 0) {
                trap(vm, "bad method id", pc, op);
                return DVM_ERR_TRAP;
            }
            uint32_t proto_idx = rd16(vm->buf + vm->method_ids_off + midx * 8u + 2u);
            if (proto_desc(vm, proto_idx, sig, sizeof sig) != 0) sig[0] = 0;
            vm->calls++;
            struct dvm_result sub;
            memset(&sub, 0, sizeof sub);
            int rc = dvm_exec(vm, midx, argv, count, &sub, depth + 1);
            if (rc == DVM_ERR_NO_CODE) {
                int32_t ret = 0; int is_ref = 0;
                rc = call_intrinsic(vm, cls, name, sig, argv, count, &ret, &is_ref, &pc);
                if (rc < 0) { pc += 3; return DVM_ERR_TRAP; }
                if (rc == 1) {
                    int h = find_handler(vm, code, pc);
                    if (h >= 0) { vm->exc_caught = 1; pc = (uint32_t)h; break; }
                    return 1;
                }
                vm->last_result = ret;
                pc += 3;
                break;
            }
            if (rc == 0) {
                vm->last_result = sub.object ? (int32_t)sub.object : sub.value;
                pc += 3;
                break;
            }
            if (rc == 1) {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { vm->exc_caught = 1; pc = (uint32_t)h; vm->has_exc = 1; break; }
                return 1;
            }
            if (rc == DVM_ERR_UNCAUGHT) return 1;
            return rc;
        }
        case 0x7b: fr->regs[aa] = -fr->regs[(insns[pc] >> 12) & 0xf]; pc++; break;
        case 0x7c: fr->regs[aa] = ~fr->regs[(insns[pc] >> 12) & 0xf]; pc++; break;
        case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86:
        case 0x87: case 0x88: case 0x89: case 0x8a: case 0x8b: case 0x8c:
            trap(vm, "numeric conversion to wide/float", pc, op);
            return DVM_ERR_TRAP;
        case 0x8d: { /* int-to-byte */
            uint8_t b = (uint8_t)((insns[pc] >> 12) & 0xf);
            fr->regs[aa] = (int8_t)fr->regs[b]; pc++; break;
        }
        case 0x8e: { /* int-to-char */
            uint8_t b = (uint8_t)((insns[pc] >> 12) & 0xf);
            fr->regs[aa] = (uint16_t)fr->regs[b]; pc++; break;
        }
        case 0x8f: { /* int-to-short */
            uint8_t b = (uint8_t)((insns[pc] >> 12) & 0xf);
            fr->regs[aa] = (int16_t)fr->regs[b]; pc++; break;
        }
        case 0x90: case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97: case 0x98: case 0x99: case 0x9a: {
            uint8_t a = (uint8_t)(insns[pc] >> 8), b = (uint8_t)(insns[pc] >> 12);
            int32_t x = fr->regs[b], y = fr->regs[insns[pc + 1]];
            int32_t r = 0;
            switch (op) {
            case 0x90: r = x + y; break;
            case 0x91: r = x - y; break;
            case 0x92: r = x * y; break;
            case 0x93:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero; }
                r = x / y; break;
            case 0x94:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero; }
                r = x % y; break;
            case 0x95: r = x & y; break;
            case 0x96: r = x | y; break;
            case 0x97: r = x ^ y; break;
            case 0x98: r = x << (y & 31); break;
            case 0x99: r = x >> (y & 31); break;
            case 0x9a: r = (int32_t)((uint32_t)x >> (y & 31)); break;
            }
            fr->regs[a] = r;
            pc += 2;
            break;
        div_zero: {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { pc = (uint32_t)h; break; }
                return 1;
            }
        }
        case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4:
        case 0xb5: case 0xb6: case 0xb7: case 0xb8: case 0xb9: case 0xba: {
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)(insns[pc] >> 12);
            int32_t x = fr->regs[a], y = fr->regs[b];
            int32_t r = 0;
            switch (op) {
            case 0xb0: r = x + y; break;
            case 0xb1: r = x - y; break;
            case 0xb2: r = x * y; break;
            case 0xb3:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero2; }
                r = x / y; break;
            case 0xb4:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero2; }
                r = x % y; break;
            case 0xb5: r = x & y; break;
            case 0xb6: r = x | y; break;
            case 0xb7: r = x ^ y; break;
            case 0xb8: r = x << (y & 31); break;
            case 0xb9: r = x >> (y & 31); break;
            case 0xba: r = (int32_t)((uint32_t)x >> (y & 31)); break;
            }
            fr->regs[a] = r;
            pc++;
            break;
        div_zero2: {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { pc = (uint32_t)h; break; }
                return 1;
            }
        }
        case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4:
        case 0xd5: case 0xd6: case 0xd7: {
            uint8_t a = (uint8_t)((insns[pc] >> 8) & 0xf);
            uint8_t b = (uint8_t)(insns[pc] >> 12);
            int32_t x = fr->regs[b], y = (int16_t)insns[pc + 1];
            int32_t r = 0;
            switch (op) {
            case 0xd0: r = x + y; break;
            case 0xd1: r = y - x; break;
            case 0xd2: r = x * y; break;
            case 0xd3:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero3; }
                r = x / y; break;
            case 0xd4:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero3; }
                r = x % y; break;
            case 0xd5: r = x & y; break;
            case 0xd6: r = x | y; break;
            case 0xd7: r = x ^ y; break;
            }
            fr->regs[a] = r;
            pc += 2;
            break;
        div_zero3: {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { pc = (uint32_t)h; break; }
                return 1;
            }
        }
        case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc:
        case 0xdd: case 0xde: case 0xdf: case 0xe0: case 0xe1: case 0xe2: {
            uint8_t a = (uint8_t)(insns[pc] >> 8);
            int32_t x = fr->regs[a], y = (int8_t)(insns[pc + 1] >> 8);
            int32_t r = 0;
            switch (op) {
            case 0xd8: r = x + y; break;
            case 0xd9: r = y - x; break;
            case 0xda: r = x * y; break;
            case 0xdb:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero4; }
                r = x / y; break;
            case 0xdc:
                if (!y) { raise_exc(vm, "java/lang/ArithmeticException"); goto div_zero4; }
                r = x % y; break;
            case 0xdd: r = x & y; break;
            case 0xde: r = x | y; break;
            case 0xdf: r = x ^ y; break;
            case 0xe0: r = x << (y & 31); break;
            case 0xe1: r = x >> (y & 31); break;
            case 0xe2: r = (int32_t)((uint32_t)x >> (y & 31)); break;
            }
            fr->regs[a] = r;
            pc += 2;
            break;
        div_zero4: {
                int h = find_handler(vm, code, pc);
                if (h >= 0) { pc = (uint32_t)h; break; }
                return 1;
            }
        }
        default:
            trap(vm, "unimplemented opcode", pc, op);
            return DVM_ERR_TRAP;
        }
    }
}

/* ---- public API ------------------------------------------------------- */

struct dvm *dvm_create(const uint8_t *dex, uint32_t len) {
    if (!dex || len < 112) return NULL;
    struct dvm *vm = (struct dvm *)calloc(1, sizeof *vm);
    if (!vm) return NULL;
    vm->buf = dex;
    vm->len = len;
    vm->string_ids_size = rd32(dex + 56);
    vm->string_ids_off = rd32(dex + 60);
    vm->type_ids_size = rd32(dex + 64);
    vm->type_ids_off = rd32(dex + 68);
    vm->proto_ids_size = rd32(dex + 72);
    vm->proto_ids_off = rd32(dex + 76);
    vm->method_ids_size = rd32(dex + 88);
    vm->method_ids_off = rd32(dex + 92);
    vm->class_defs_size = rd32(dex + 96);
    vm->class_defs_off = rd32(dex + 100);
    return vm;
}

void dvm_destroy(struct dvm *vm) { free(vm); }

int dvm_call(struct dvm *vm, const char *class_name, const char *method_name_in,
             const char *signature, const int32_t *args, int nargs,
             struct dvm_result *out) {
    if (!vm || !out) return DVM_ERR_BAD_DEX;
    memset(out, 0, sizeof *out);
    for (uint32_t m = 0; m < vm->method_ids_size; m++) {
        char cls[96], name[64];
        if (method_class(vm, m, cls, sizeof cls) != 0) continue;
        if (strcmp(cls, class_name) != 0) continue;
        if (method_name(vm, m, name, sizeof name) != 0) continue;
        if (method_name_in && strcmp(name, method_name_in) != 0) continue;
        uint32_t proto_idx = rd16(vm->buf + vm->method_ids_off + m * 8u + 2u);
        char sig[160];
        if (proto_desc(vm, proto_idx, sig, sizeof sig) == 0 && signature && signature[0] &&
            strcmp(sig, signature) != 0)
            continue;
        vm->trap[0] = 0;
        int rc = dvm_exec(vm, m, args, nargs, out, 0);
        out->insns = vm->insns;
        out->calls = vm->calls;
        out->caught = vm->exc_caught;
        if (rc == 0) return DVM_OK;
        if (rc == 1) {
            out->threw = 1;
            strncpy(out->exception_class, vm->exc_class, sizeof out->exception_class - 1);
            return DVM_ERR_UNCAUGHT;
        }
        strncpy(out->trap, vm->trap, sizeof out->trap - 1);
        return rc;
    }
    snprintf(out->trap, sizeof out->trap, "method not found: %s->%s", class_name, method_name_in);
    return DVM_ERR_NO_CODE;
}

int dvm_stats(struct dvm *vm, uint32_t *classes, uint32_t *methods_with_code,
              uint32_t *try_blocks) {
    if (!vm) return -1;
    uint32_t mwc = 0, tries = 0;
    for (uint32_t i = 0; i < vm->class_defs_size; i++) {
        const uint8_t *cd = vm->buf + vm->class_defs_off + i * 32u;
        uint32_t data_off = rd32(cd + 24);
        if (!data_off) continue;
        const uint8_t *p = vm->buf + data_off;
        uint32_t used = 0;
        uint32_t sf = uleb(p, &used); p += used;
        uint32_t inf = uleb(p, &used); p += used;
        uint32_t dm = uleb(p, &used); p += used;
        uint32_t vmc = uleb(p, &used); p += used;
        for (uint32_t f = 0; f < sf + inf; f++) {
            (void)uleb(p, &used); p += used;
            (void)uleb(p, &used); p += used;
        }
        for (uint32_t m = 0; m < dm + vmc; m++) {
            (void)uleb(p, &used); p += used;
            (void)uleb(p, &used); p += used;
            uint32_t co = uleb(p, &used); p += used;
            if (co) {
                mwc++;
                tries += rd16(vm->buf + co + 6);
            }
        }
    }
    if (classes) *classes = vm->class_defs_size;
    if (methods_with_code) *methods_with_code = mwc;
    if (try_blocks) *try_blocks = tries;
    return 0;
}

/* Run one static method with the argument tuple the scanner can supply for its
 * parameter list. Returns 1 when the call was made. */
static int scan_call(struct dvm *vm, uint32_t m, uint32_t params_off,
                     uint32_t nparams, int32_t sample_int, const char *sample_str,
                     struct dvm_result *res) {
    int32_t args[2];
    if (nparams > 2) return 0;
    for (uint32_t q = 0; q < nparams; q++) {
        uint32_t pt = rd16(vm->buf + params_off + 4u + q * 2u);
        char ptn[96];
        if (dex_type_name(vm, pt, ptn, sizeof ptn) != 0) return 0;
        if (strcmp(ptn, "I") == 0) args[q] = sample_int;
        else if (strcmp(ptn, "Ljava/lang/String;") == 0)
            args[q] = make_syn_string(vm, sample_str);
        else return 0;
    }
    memset(res, 0, sizeof *res);
    vm->trap[0] = 0;
    vm->has_exc = 0;
    vm->exc_caught = 0;
    vm->exc_class[0] = 0;
    uint32_t before = vm->insns;
    (void)dvm_exec(vm, m, args, (int)nparams, res, 0);
    res->insns = vm->insns - before;
    res->caught = vm->exc_caught;
    if (res->caught)
        strncpy(res->exception_class, vm->exc_class, sizeof res->exception_class - 1);
    return 1;
}

int dvm_scan(struct dvm *vm, struct dvm_scan_stats *out) {
    if (!vm || !out) return -1;
    memset(out, 0, sizeof *out);

    for (uint32_t i = 0; i < vm->class_defs_size; i++) {
        const uint8_t *cd = vm->buf + vm->class_defs_off + i * 32u;
        uint32_t data_off = rd32(cd + 24);
        if (!data_off) continue;
        const uint8_t *p = vm->buf + data_off;
        uint32_t used = 0;
        uint32_t sf = uleb(p, &used); p += used;
        uint32_t inf = uleb(p, &used); p += used;
        uint32_t dm = uleb(p, &used); p += used;
        uint32_t vmc = uleb(p, &used); p += used;
        for (uint32_t f = 0; f < sf + inf; f++) {
            (void)uleb(p, &used); p += used;
            (void)uleb(p, &used); p += used;
        }
        uint32_t cur = 0;
        for (uint32_t m = 0; m < dm + vmc; m++) {
            uint32_t diff = uleb(p, &used); p += used;
            cur += diff;
            uint32_t acc = uleb(p, &used); p += used;
            uint32_t co = uleb(p, &used); p += used;
            if (!co) continue;
            out->with_code++;
            if (!(acc & 0x0008u)) continue;            /* static only */

            uint32_t proto_idx = rd16(vm->buf + vm->method_ids_off + cur * 8u + 2u);
            const uint8_t *pe = vm->buf + vm->proto_ids_off + proto_idx * 12u;
            uint32_t ret_type = rd32(pe + 4);
            uint32_t params_off = rd32(pe + 8);
            uint32_t nparams = params_off ? rd32(vm->buf + params_off) : 0;
            uint32_t tries = rd16(vm->buf + co + 6);
            char name[64], cls[96], rt[96];
            if (method_name(vm, cur, name, sizeof name) != 0) continue;
            if (name[0] == '<') continue;              /* <init> / <clinit> */
            if (method_class(vm, cur, cls, sizeof cls) != 0) cls[0] = 0;
            if (dex_type_name(vm, ret_type, rt, sizeof rt) != 0) rt[0] = 0;
            int is_string_ret = strcmp(rt, "Ljava/lang/String;") == 0;
            int is_object_ret = rt[0] == 'L' && !is_string_ret;

            if (nparams == 0) {
                struct dvm_result res;
                if (scan_call(vm, cur, params_off, 0, 0, "", &res)) {
                    if (res.insns == 0) { /* nothing executed: not evidence */ }
                    if (vm->trap[0] == 0 && !vm->has_exc) {
                        out->executed++;
                        if (res.insns >= 8 && out->hits < 2) {
                            snprintf(out->hit[out->hits].name, sizeof out->hit[0].name,
                                     "%s->%s", cls, name);
                            out->hit[out->hits].res = res;
                            out->hits++;
                        }
                    } else {
                        out->trapped++;
                    }
                }
            }

            /* A real try/catch table: feed input that makes a host intrinsic
             * throw and see whether a DEX handler takes it. */
            if (tries > 0 && nparams >= 1 && nparams <= 2) {
                struct dvm_result res;
                uint32_t pt = rd16(vm->buf + params_off + 4u);
                char ptn[96];
                if (dex_type_name(vm, pt, ptn, sizeof ptn) == 0) {
                    int str_arg = strcmp(ptn, "Ljava/lang/String;") == 0;
                    if (scan_call(vm, cur, params_off, nparams,
                                  str_arg ? 0 : 0, "12x", &res)) {
                        if (str_arg) out->strparam_tried++;
                        if (res.caught && out->caught_hits < 2 && res.insns >= 3) {
                            snprintf(out->caught[out->caught_hits].name,
                                     sizeof out->caught[0].name, "%s->%s", cls, name);
                            out->caught[out->caught_hits].res = res;
                            out->caught_hits++;
                        }
                        if (str_arg && vm->trap[0] == 0 && !vm->has_exc) out->strparam_ok++;
                    }
                }
            }

            if (is_string_ret && nparams <= 1 && out->nstr < 2) {
                struct dvm_result res;
                if (scan_call(vm, cur, params_off, nparams, 7, "Hello", &res) &&
                    vm->trap[0] == 0 && !vm->has_exc && res.string[0] && res.insns >= 6) {
                    snprintf(out->str[out->nstr].name, sizeof out->str[0].name,
                             "%s->%s", cls, name);
                    out->str[out->nstr].res = res;
                    out->nstr++;
                }
            }

            if (is_object_ret && nparams <= 1 && out->nobj < 2) {
                struct dvm_result res;
                if (scan_call(vm, cur, params_off, nparams, 7, "Hello", &res) &&
                    vm->trap[0] == 0 && !vm->has_exc && res.object && res.insns >= 6) {
                    struct dvm_object *o = object_at(vm, (int32_t)res.object);
                    if (o && o->nfields > 0) {
                        snprintf(out->obj[out->nobj].name, sizeof out->obj[0].name,
                                 "%s->%s [object of %s with %d fields set]", cls, name, rt,
                                 o->nfields);
                        out->obj[out->nobj].res = res;
                        out->nobj++;
                    }
                }
            }
        }
    }
    return 0;
}
