/* Physical frame allocator. Frames are 4 KiB and cover 1 MiB to 256 MiB.
   Stage 2 leaves the BIOS E820 map at 0x5000. The two bitmaps sit at fixed
   low addresses so they stay out of kernel .bss and the app slot at 0x20000.
   Lives in drivers/ so the existing driver rule builds it without a Makefile
   change. */

#include "../include/pmm.h"
#include "../include/vm.h"

#define E820_COUNT_ADDR   0x5000u
#define E820_ENTRIES_ADDR 0x5010u
#define E820_TYPE_USABLE  1u

#define FRAME_MIN    0x00100000u
#define FRAME_MAX    0x10000000u
#define FRAME_COUNT  ((FRAME_MAX - FRAME_MIN) / PMM_FRAME_SIZE)
#define BITMAP_BYTES (FRAME_COUNT / 8u)
#define AVAIL_BITS   ((unsigned char *)0x60000u)
#define MANAGED_BITS ((unsigned char *)0x62000u)

static unsigned int free_count;
static unsigned int managed_count;
static unsigned int next_hint;

static unsigned int bit_get(unsigned char *map, unsigned int i) {
    return (map[i >> 3] >> (i & 7u)) & 1u;
}

static void bit_set(unsigned char *map, unsigned int i, unsigned int on) {
    unsigned char mask = (unsigned char)(1u << (i & 7u));
    if (on)
        map[i >> 3] |= mask;
    else
        map[i >> 3] &= (unsigned char)~mask;
}

static void serial_dec(unsigned int v) {
    char buf[12];
    int n = 11;
    buf[n] = 0;
    do {
        buf[--n] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    serial_str(buf + n);
}

static void clear_bitmap(unsigned char *map) {
    for (unsigned int i = 0; i < BITMAP_BYTES; i++)
        map[i] = 0;
}

static void manage_range(unsigned int base, unsigned int len) {
    unsigned int end;
    if (len == 0 || base >= FRAME_MAX)
        return;
    if (base > 0xFFFFFFFFu - len)
        end = 0xFFFFFFFFu;
    else
        end = base + len;
    if (base < FRAME_MIN)
        base = FRAME_MIN;
    if (end > FRAME_MAX)
        end = FRAME_MAX;
    base = (base + PMM_FRAME_SIZE - 1u) & ~(PMM_FRAME_SIZE - 1u);
    end &= ~(PMM_FRAME_SIZE - 1u);
    for (unsigned int a = base; a < end; a += PMM_FRAME_SIZE) {
        unsigned int i = (a - FRAME_MIN) / PMM_FRAME_SIZE;
        if (bit_get(MANAGED_BITS, i))
            continue;
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

    clear_bitmap(AVAIL_BITS);
    clear_bitmap(MANAGED_BITS);
    free_count = 0;
    managed_count = 0;
    next_hint = 0;

    for (unsigned int k = 0; k < regions; k++, entry += 6) {
        if (entry[4] != E820_TYPE_USABLE || entry[1] != 0)
            continue;
        unsigned int len = entry[3] ? 0xFFFFFFFFu : entry[2];
        manage_range(entry[0], len);
    }

    serial_str("pmm: e820 regions ");
    serial_dec(regions);
    serial_str("\n");
    serial_str("pmm: ");
    serial_dec(free_count >> 8);
    serial_str(" MB free of ");
    serial_dec(managed_count >> 8);
    serial_str(" MB managed\n");
    serial_str(pmm_selftest() ? "pmm: selftest passed\n" : "pmm: selftest FAILED\n");
    /* Paging comes up after the PMM because its page tables are PMM frames. */
    vm_init();
}

unsigned int pmm_alloc_frame(void) {
    if (free_count == 0)
        return 0;
    for (unsigned int n = 0; n < FRAME_COUNT; n++) {
        unsigned int i = next_hint + n;
        if (i >= FRAME_COUNT)
            i -= FRAME_COUNT;
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
    if (addr < FRAME_MIN || addr >= FRAME_MAX || (addr & (PMM_FRAME_SIZE - 1u)))
        return -1;
    i = (addr - FRAME_MIN) / PMM_FRAME_SIZE;
    if (!bit_get(MANAGED_BITS, i) || bit_get(AVAIL_BITS, i))
        return -1;
    bit_set(AVAIL_BITS, i, 1);
    free_count++;
    return 0;
}

unsigned int pmm_free_frames(void) {
    return free_count;
}

unsigned int pmm_managed_frames(void) {
    return managed_count;
}

int pmm_selftest(void) {
    unsigned int before = free_count;
    unsigned int a = pmm_alloc_frame();
    unsigned int b = pmm_alloc_frame();
    int ok = a && b && a != b;

    if (ok) {
        volatile unsigned int *pa = (volatile unsigned int *)a;
        volatile unsigned int *pb = (volatile unsigned int *)b;
        for (unsigned int k = 0; k < PMM_FRAME_SIZE / 4u; k++) {
            pa[k] = 0xA5A5A5A5u ^ k;
            pb[k] = 0x5A5A5A5Au + k;
        }
        for (unsigned int k = 0; k < PMM_FRAME_SIZE / 4u; k++) {
            if (pa[k] != (0xA5A5A5A5u ^ k) || pb[k] != (0x5A5A5A5Au + k)) {
                ok = 0;
                break;
            }
        }
    }

    if (a) {
        if (pmm_free_frame(a) != 0)
            ok = 0;
        if (pmm_free_frame(a) == 0)
            ok = 0;
    }
    if (b && pmm_free_frame(b) != 0)
        ok = 0;
    if (free_count != before)
        ok = 0;
    return ok;
}
