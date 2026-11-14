#include <stddef.h>
#include <stdint.h>

#include "py/mpconfig.h"
#include "py/gc.h"
#include "py/misc.h"
#include "py/obj.h"
#include "py/repl.h"
#include "py/stackctrl.h"
#include "shared/runtime/pyexec.h"
#include "flareos_abi.h"

#define FLAREOS_PY_HEAP_BYTES (384u * 1024u)

void *flareos_heap_alloc(size_t size);
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);

static void *stack_top;

void gc_collect(void) {
    void *stack_marker;
    uintptr_t top = (uintptr_t)stack_top;
    uintptr_t bottom = (uintptr_t)&stack_marker;
    if (top < bottom) {
        uintptr_t swap = top;
        top = bottom;
        bottom = swap;
    }
    bottom = (bottom + sizeof(void *) - 1u) & ~(uintptr_t)(sizeof(void *) - 1u);
    gc_collect_start();
    if (top > bottom)
        gc_collect_root((void **)bottom, (top - bottom) / sizeof(void *));
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    (void)val;
    static const char message[] = "\nMicroPython: unhandled fatal exception\n";
    mp_hal_stdout_tx_strn_cooked(message, sizeof(message) - 1u);
    for (;;) flareos_call(SYS_SLEEP, 100, 0, 0);
}

void MP_NORETURN __fatal_error(const char *msg) {
    size_t n = 0;
    while (msg && msg[n]) ++n;
    if (n) mp_hal_stdout_tx_strn_cooked(msg, n);
    mp_hal_stdout_tx_strn_cooked("\n", 1);
    for (;;) flareos_call(SYS_SLEEP, 100, 0, 0);
}

void flareos_app_entry(void) {
    int stack_marker;
    void *heap;

    stack_top = &stack_marker;
    mp_stack_ctrl_init();
    mp_stack_set_top(stack_top);
    mp_stack_set_limit(12u * 1024u);

    heap = flareos_heap_alloc(FLAREOS_PY_HEAP_BYTES);
    if (!heap) {
        static const char message[] = "MicroPython: cannot allocate GC heap\n";
        mp_hal_stdout_tx_strn_cooked(message, sizeof(message) - 1u);
        flareos_call(SYS_EXIT, 0, 0, 0);
        for (;;) {}
    }

    gc_init(heap, (uint8_t *)heap + FLAREOS_PY_HEAP_BYTES);
    mp_init();
    {
        static const char banner[] = "FlareOS MicroPython (minimal)\nType help() or import flareos.\n";
        mp_hal_stdout_tx_strn_cooked(banner, sizeof(banner) - 1u);
    }
    pyexec_friendly_repl();
    mp_deinit();

    flareos_call(SYS_EXIT, 0, 0, 0);
    for (;;) {}
}

__attribute__((section(".head"), used))
const uint32_t flareos_app_head[2] = {
    TX_APP_MAGIC, (uint32_t)(uintptr_t)flareos_app_entry
};
