/* Kernel heap: first-fit blocks carved from contiguous PMM frame runs.
   Chunks are never returned to the frame allocator. Every block header
   carries its chunk bounds so kfree can merge a freed block with its
   neighbours by walking the chunk from its start. */

#include "../include/pmm.h"
#include "../include/kheap.h"

#define KHEAP_CHUNK_BYTES  (256u * 1024u)
#define KHEAP_HDR          16u
#define KHEAP_MAX_CHUNKS   64u
#define KHEAP_MIN_SPLIT    (KHEAP_HDR + 16u)

struct kblk {
    unsigned int size;
    unsigned int used;
    unsigned int chunk_start;
    unsigned int chunk_end;
};

static unsigned int chunks[KHEAP_MAX_CHUNKS];
static unsigned int chunk_count;
static unsigned int live;

static unsigned int round16(unsigned int n) {
    return (n + 15u) & ~15u;
}

static unsigned int round4k(unsigned int n) {
    return (n + PMM_FRAME_SIZE - 1u) & ~(PMM_FRAME_SIZE - 1u);
}

static void coalesce(unsigned int start, unsigned int end) {
    unsigned int a = start;
    while (a < end) {
        struct kblk *b = (struct kblk *)a;
        unsigned int next = a + b->size;
        if (!b->used && next < end) {
            struct kblk *n = (struct kblk *)next;
            if (!n->used) {
                b->size += n->size;
                continue;
            }
        }
        a = next;
    }
}

static void *take(unsigned int need) {
    for (unsigned int c = 0; c < chunk_count; c++) {
        unsigned int a = chunks[c];
        unsigned int end = ((struct kblk *)a)->chunk_end;
        while (a < end) {
            struct kblk *b = (struct kblk *)a;
            if (!b->used && b->size >= need) {
                unsigned int cs = b->chunk_start;
                unsigned int ce = b->chunk_end;
                if (b->size - need >= KHEAP_MIN_SPLIT) {
                    struct kblk *rest = (struct kblk *)(a + need);
                    rest->size = b->size - need;
                    rest->used = 0;
                    rest->chunk_start = cs;
                    rest->chunk_end = ce;
                    b->size = need;
                }
                b->used = 1;
                live += b->size;
                return (void *)(a + KHEAP_HDR);
            }
            a += b->size;
        }
    }
    return 0;
}

void *kmalloc(unsigned int size) {
    unsigned int need;
    unsigned int bytes;
    unsigned int base;
    struct kblk *hdr;
    void *p;

    if (size == 0)
        size = 1;
    if (size > 0x7FFFFFFFu)
        return 0;
    need = round16(size + KHEAP_HDR);
    p = take(need);
    if (p)
        return p;

    if (chunk_count >= KHEAP_MAX_CHUNKS)
        return 0;
    bytes = need > KHEAP_CHUNK_BYTES ? round4k(need) : KHEAP_CHUNK_BYTES;
    base = pmm_alloc_contig(bytes / PMM_FRAME_SIZE);
    if (!base)
        return 0;
    hdr = (struct kblk *)base;
    hdr->size = bytes;
    hdr->used = 0;
    hdr->chunk_start = base;
    hdr->chunk_end = base + bytes;
    chunks[chunk_count++] = base;
    return take(need);
}

void kfree(void *p) {
    struct kblk *b;
    if (!p)
        return;
    b = (struct kblk *)((unsigned int)p - KHEAP_HDR);
    if (!b->used)
        return;
    b->used = 0;
    live -= b->size;
    coalesce(b->chunk_start, b->chunk_end);
}

unsigned int kheap_live_bytes(void) {
    return live;
}

int kheap_selftest(void) {
    unsigned int before = live;
    unsigned char *a = kmalloc(100);
    unsigned char *b = kmalloc(5000);
    unsigned char *c = kmalloc(70000);
    unsigned char *d = 0;
    unsigned int i;
    int ok = a && b && c;

    if (ok) {
        for (i = 0; i < 100; i++)
            a[i] = (unsigned char)(i ^ 0x3Cu);
        for (i = 0; i < 70000; i++)
            c[i] = (unsigned char)(i * 7u);
        kfree(b);
        d = kmalloc(4000);
        ok = d == b;
        for (i = 0; ok && i < 100; i++)
            if (a[i] != (unsigned char)(i ^ 0x3Cu))
                ok = 0;
        for (i = 0; ok && i < 70000; i++)
            if (c[i] != (unsigned char)(i * 7u))
                ok = 0;
    }
    kfree(d);
    kfree(a);
    kfree(c);
    if (live != before)
        ok = 0;
    serial_str(ok ? "kheap: selftest passed\n" : "kheap: selftest FAILED\n");
    return ok;
}
