#ifndef FLAIROS_DALVIK_H
#define FLAIROS_DALVIK_H

#include <stdint.h>

/* A small Dalvik interpreter: it runs real DEX bytecode from a real
 * classes.dex, one method at a time.
 *
 * It is not ART and it does not host an Activity: there is no framework below
 * it, so calls into android.* / java.* land in a handful of host intrinsics
 * while calls into methods defined in the same DEX are interpreted for real.
 * Opcodes it does not implement abort the run with a diagnostic instead of
 * guessing.
 */

#define DVM_MAX_REGISTERS 256
#define DVM_MAX_FRAMES    24
#define DVM_MAX_OBJECTS   256
#define DVM_MAX_FIELDS    16
#define DVM_MAX_FRAMES_DEPTH 24

enum dvm_status {
    DVM_OK = 0,
    DVM_ERR_BAD_DEX = -1,
    DVM_ERR_NO_CODE = -2,
    DVM_ERR_TRAP = -3,         /* unimplemented opcode / missing intrinsic */
    DVM_ERR_UNCAUGHT = -4,     /* exception with no matching handler */
    DVM_ERR_DEPTH = -5         /* call depth exhausted */
};

struct dvm_result {
    int32_t  value;            /* return value for int/boolean/short/char/byte */
    uint32_t object;           /* object handle for reference returns, 0 = null */
    char     string[96];       /* decoded return string when it is a String */
    char     trap[96];         /* opcode/method that stopped the run */
    int      threw;            /* an exception was raised during the call */
    char     exception_class[64];
    int      caught;           /* ...and a real DEX catch handler took it */
    uint32_t insns;            /* executed instructions */
    uint32_t calls;            /* interpreted method calls */
};

struct dvm;

struct dvm *dvm_create(const uint8_t *dex, uint32_t len);
void        dvm_destroy(struct dvm *vm);

/* Call a method by "(Lpkg/Class;.name(SIG)RET)"-style lookup. Returns status. */
int dvm_call(struct dvm *vm, const char *class_name, const char *method_name,
             const char *signature, const int32_t *args, int nargs,
             struct dvm_result *out);

struct dvm_scan_hit {
    char name[320];
    struct dvm_result res;
};

struct dvm_scan_stats {
    int with_code;          /* methods carrying a code_item */
    int executed;           /* static no-arg methods that returned */
    int trapped;            /* ...that hit an opcode we do not implement */
    int strparam_tried;     /* static (String) methods we called with a sample */
    int strparam_ok;        /* ...that returned a value */
    int hits;
    struct dvm_scan_hit hit[2];      /* plain value returns */
    int nstr;
    struct dvm_scan_hit str[2];      /* runs that returned a real String */
    int nobj;
    struct dvm_scan_hit obj[2];      /* runs that allocated an object */
    int caught_hits;
    struct dvm_scan_hit caught[2];   /* runs where a DEX catch handler fired */
};

/* Scan every method in the DEX that has code, execute the static ones that our
 * opcode set covers, and report what ran. */
int dvm_scan(struct dvm *vm, struct dvm_scan_stats *out);

/* Count class definitions, methods with code, and try/catch blocks in the DEX. */
int dvm_stats(struct dvm *vm, uint32_t *classes, uint32_t *methods_with_code,
              uint32_t *try_blocks);

#endif
