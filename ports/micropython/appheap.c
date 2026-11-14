#include <stddef.h>
#include <stdint.h>
#include "flareos_abi.h"

/* The VM provides a private, zero-filled 4 MiB user heap via SYS_BRK.
   MicroPython's GC arena is one monotonic allocation from that heap. */
void *flareos_heap_alloc(size_t size) {
    uint32_t base, end, actual;
    uint32_t result;

    result = flareos_call(SYS_BRK, 0, 0, 0);
    base = result;
    if (size == 0 || size > 0x003FFFF0u || base > 0x10400000u - (uint32_t)size)
        return 0;

    end = base + (uint32_t)size;
    actual = flareos_call(SYS_BRK, end, 0, 0);
    if (actual < end) return 0;
    return (void *)(uintptr_t)base;
}
