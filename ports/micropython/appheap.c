#include <stddef.h>
#include <stdint.h>
#include "../../kernel/textile.h"

/* The VM provides a private, zero-filled 4 MiB user heap via SYS_BRK.
   MicroPython's GC arena is one monotonic allocation from that heap. */
void *flareos_heap_alloc(size_t size) {
    uint32_t base, end, actual;
    uint32_t result;

    __asm__ volatile ("int $0x80" : "=a"(result)
        : "a"(SYS_BRK), "b"(0), "c"(0), "d"(0) : "memory");
    base = result;
    if (size == 0 || size > 0x003FFFF0u || base > 0x10400000u - (uint32_t)size)
        return 0;

    end = base + (uint32_t)size;
    __asm__ volatile ("int $0x80" : "=a"(actual)
        : "a"(SYS_BRK), "b"(end), "c"(0), "d"(0) : "memory");
    if (actual < end) return 0;
    return (void *)(uintptr_t)base;
}
