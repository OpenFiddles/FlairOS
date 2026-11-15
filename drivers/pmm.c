/* SPDX-License-Identifier: AGPL-3.0-only */
/* Physical frame allocator. Frames are 4 KiB and cover 1 MiB to 256 MiB.
   Stage 2 leaves the BIOS E820 map at 0x5000. The bitmaps occupy the reserved
   0x30000..0x36000 gap between the kernel image and the app slot. */

#include "../include/pmm.h"
#include "../include/vm.h"
#include "../include/kheap.h"

#define E820_COUNT_ADDR   0x5000u
#define E820_ENTRIES_ADDR 0x5010u
#define E820_TYPE_USABLE  1u
#define E820_MAX_REGIONS  32u
#define FRAME_MIN    0x00100000u
#define FRAME_MAX    0x10000000u
#define FRAME_COUNT  ((FRAME_MAX - FRAME_MIN) / PMM_FRAME_SIZE)
#define BITMAP_BYTES (FRAME_COUNT / 8u)
#define AVAIL_BITS   ((unsigned char *)0x30000u)
#define MANAGED_BITS ((unsigned char *)0x32000u)
#define RESERVED_BITS ((unsigned char *)0x34000u)

static unsigned int free_count;
static unsigned int managed_count;
static unsigned int reserved_count;
static unsigned int next_hint;

static unsigned int bit_get(unsigned char *map, unsigned int i) {
    return (map[i >> 3] >> (i & 7u)) & 1u;
}
static void bit_set(unsigned char *map, unsigned int i, unsigned int on) {
    unsigned char mask = (unsigned char)(1u << (i & 7u));
    if (on) map[i >> 3] |= mask;
    else map[i >> 3] &= (unsigned char)~mask;
}
static void serial_dec(unsigned int v) {
    char buf[12]; int n = 11; buf[n] = 0;
    do { buf[--n] = (char)('0' + v % 10u); v /= 10u; } while (v);
    serial_str(buf + n);
}
static void clear_bitmap(unsigned char *map) {
    for (unsigned int i = 0; i < BITMAP_BYTES; i++) map[i] = 0;
}
static void manage_range(unsigned int base, unsigned int len) {
    unsigned int end;
    if (len == 0 || base >= FRAME_MAX) return;
    end = (base > 0xFFFFFFFFu - len) ? 0xFFFFFFFFu : base + len;
    if (base < FRAME_MIN) base = FRAME_MIN;
    if (end > FRAME_MAX) end = FRAME_MAX;
    base = (base + PMM_FRAME_SIZE - 1u) & ~(PMM_FRAME_SIZE - 1u);
    end &= ~(PMM_FRAME_SIZE - 1u);
    for (unsigned int a = base; a < end; a += PMM_FRAME_SIZE) {
        unsigned int i = (a - FRAME_MIN) / PMM_FRAME_SIZE;
        if (bit_get(MANAGED_BITS, i)) continue;
        bit_set(MANAGED_BITS, i, 1);
        bit_set(AVAIL_BITS, i, 1);
        free_count++;
        managed_count++;
    }
}

void pmm_init(void) {
    const volatile unsigned int *count = (const volatile unsigned int *)E820_COUNT_ADDR;
    const volatile unsigned int *entry = (const volatile unsigned int *)E820_ENTRIES_ADDR;
    unsigned int regions = *count;
    if (regions > E820_MAX_REGIONS) {
        serial_str("pmm: E820 count exceeds buffer; clamping to 32\n");
        regions = E820_MAX_REGIONS;
    }
    clear_bitmap(AVAIL_BITS);
    clear_bitmap(MANAGED_BITS);
    clear_bitmap(RESERVED_BITS);
    free_count = managed_count = reserved_count = next_hint = 0;
    for (unsigned int k = 0; k < regions; k++, entry += 6) {
        if (entry[4] != E820_TYPE_USABLE || entry[1] != 0) continue;
        manage_range(entry[0], entry[3] ? 0xFFFFFFFFu : entry[2]);
    }
    serial_str("pmm: e820 regions "); serial_dec(regions); serial_str("\n");
    serial_str("pmm: "); serial_dec(free_count >> 8);
    serial_str(" MB free of "); serial_dec(managed_count >> 8);
    serial_str(" MB managed\n");
    serial_str(pmm_selftest() ? "pmm: selftest passed\n" : "pmm: selftest FAILED\n");
    vm_init();
    kheap_selftest();
}

unsigned int pmm_alloc_frame(void) {
    if (free_count == 0) return 0;
    for (unsigned int n = 0; n < FRAME_COUNT; n++) {
        unsigned int i = next_hint + n;
        if (i >= FRAME_COUNT) i -= FRAME_COUNT;
        if (bit_get(AVAIL_BITS, i)) {
            bit_set(AVAIL_BITS, i, 0);
            free_count--;
            next_hint = (i + 1u < FRAME_COUNT) ? i + 1u : 0u;
            return FRAME_MIN + i * PMM_FRAME_SIZE;
        }
    }
    return 0;
}

int pmm_free_frame(unsigned int addr) {
    unsigned int i;
    if (addr < FRAME_MIN || addr >= FRAME_MAX || (addr & (PMM_FRAME_SIZE - 1u))) return -1;
    i = (addr - FRAME_MIN) / PMM_FRAME_SIZE;
    if (!bit_get(MANAGED_BITS, i) || bit_get(RESERVED_BITS, i) || bit_get(AVAIL_BITS, i)) return -1;
    bit_set(AVAIL_BITS, i, 1);
    free_count++;
    return 0;
}

/* Permanently reserve every managed frame intersecting [base, base + length).
   Returns the number newly reserved, or -1 for invalid/overflowing input.
   Frames already allocated remain owned by their caller. */
int pmm_reserve_range(unsigned int base, unsigned int length) {
    unsigned int end, start, reserved = 0;
    if (length == 0 || base > 0xFFFFFFFFu - length) return -1;
    end = base + length;
    if (base >= FRAME_MAX || end <= FRAME_MIN) return 0;
    if (base < FRAME_MIN) base = FRAME_MIN;
    if (end > FRAME_MAX) end = FRAME_MAX;
    start = base & ~(PMM_FRAME_SIZE - 1u);
    end = (end + PMM_FRAME_SIZE - 1u) & ~(PMM_FRAME_SIZE - 1u);
    if (end > FRAME_MAX) end = FRAME_MAX;
    for (unsigned int a = start; a < end; a += PMM_FRAME_SIZE) {
        unsigned int i;
        if (a < FRAME_MIN || a >= FRAME_MAX) continue;
        i = (a - FRAME_MIN) / PMM_FRAME_SIZE;
        if (!bit_get(MANAGED_BITS, i) || bit_get(RESERVED_BITS, i) ||
            !bit_get(AVAIL_BITS, i))
            continue;
        bit_set(RESERVED_BITS, i, 1);
        bit_set(AVAIL_BITS, i, 0);
        free_count--;
        reserved_count++;
        reserved++;
    }
    return (int)reserved;
}

unsigned int pmm_free_frames(void) { return free_count; }
unsigned int pmm_managed_frames(void) { return managed_count; }
unsigned int pmm_reserved_frames(void) { return reserved_count; }

int pmm_selftest(void) {
    unsigned int before = free_count;
    unsigned int a = pmm_alloc_frame(), b = pmm_alloc_frame();
    int ok = a && b && a != b;
    if (ok) {
        volatile unsigned int *pa = (volatile unsigned int *)a;
        volatile unsigned int *pb = (volatile unsigned int *)b;
        for (unsigned int k = 0; k < PMM_FRAME_SIZE / 4u; k++) {
            pa[k] = 0xA5A5A5A5u ^ k;
            pb[k] = 0x5A5A5A5Au + k;
        }
        for (unsigned int k = 0; k < PMM_FRAME_SIZE / 4u; k++)
            if (pa[k] != (0xA5A5A5A5u ^ k) || pb[k] != (0x5A5A5A5Au + k)) { ok = 0; break; }
    }
    if (a) { if (pmm_free_frame(a) != 0) ok = 0; if (pmm_free_frame(a) == 0) ok = 0; }
    if (b && pmm_free_frame(b) != 0) ok = 0;
    if (free_count != before) ok = 0;
    return ok;
}

unsigned int pmm_alloc_contig(unsigned int frames) {
    unsigned int run = 0, start = 0;
    if (frames == 0 || frames > free_count) return 0;
    for (unsigned int i = 0; i < FRAME_COUNT; i++) {
        if (!bit_get(AVAIL_BITS, i)) { run = 0; continue; }
        if (run == 0) start = i;
        if (++run == frames) {
            for (unsigned int k = start; k <= i; k++) bit_set(AVAIL_BITS, k, 0);
            free_count -= frames;
            return FRAME_MIN + start * PMM_FRAME_SIZE;
        }
    }
    return 0;
}
