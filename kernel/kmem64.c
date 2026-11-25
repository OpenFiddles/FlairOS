/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../include/kmem64.h"

struct kblk64 {
    size_t size;
    uint32_t used;
    uint32_t pad;
};

static uint8_t *heap_base;
static size_t heap_total;
static size_t heap_used;

static size_t align16(size_t n) { return (n + 15u) & ~(size_t)15u; }

void kmem64_init(void *pool, size_t bytes) {
    uintptr_t p = (uintptr_t)pool;
    uintptr_t a = (p + 15u) & ~(uintptr_t)15u;
    size_t diff = (size_t)(a - p);
    if (!pool || bytes <= diff + sizeof(struct kblk64) + 16u) {
        heap_base = 0;
        heap_total = 0;
        heap_used = 0;
        return;
    }
    heap_base = (uint8_t *)a;
    heap_total = (bytes - diff) & ~(size_t)15u;
    heap_used = 0;
    struct kblk64 *first = (struct kblk64 *)heap_base;
    first->size = heap_total;
    first->used = 0;
    first->pad = 0;
}

void *kmem64_alloc(size_t size) {
    if (!heap_base || size == 0) return 0;
    size_t need = align16(size) + sizeof(struct kblk64);
    size_t off = 0;
    while (off + sizeof(struct kblk64) <= heap_total) {
        struct kblk64 *b = (struct kblk64 *)(heap_base + off);
        if (b->size == 0) break;
        if (!b->used && b->size >= need) {
            if (b->size >= need + sizeof(struct kblk64) + 16u) {
                struct kblk64 *nxt = (struct kblk64 *)(heap_base + off + need);
                nxt->size = b->size - need;
                nxt->used = 0;
                nxt->pad = 0;
                b->size = need;
            }
            b->used = 1;
            heap_used += b->size;
            return (void *)(b + 1);
        }
        off += b->size;
    }
    return 0;
}

void *kcalloc(size_t nmemb, size_t size) {
    size_t tot = nmemb * size;
    uint8_t *p = (uint8_t *)kmem64_alloc(tot);
    if (!p) return 0;
    for (size_t i = 0; i < tot; i++) p[i] = 0;
    return p;
}

void kfree(void *ptr) {
    if (!ptr || !heap_base) return;
    struct kblk64 *b = ((struct kblk64 *)ptr) - 1;
    if ((uint8_t *)b < heap_base || (uint8_t *)b >= heap_base + heap_total) return;
    if (!b->used) return;
    b->used = 0;
    if (heap_used >= b->size) heap_used -= b->size;
    size_t off = 0;
    while (off + sizeof(struct kblk64) <= heap_total) {
        struct kblk64 *cur = (struct kblk64 *)(heap_base + off);
        if (cur->size == 0) break;
        while (!cur->used && off + cur->size + sizeof(struct kblk64) <= heap_total) {
            struct kblk64 *nxt = (struct kblk64 *)(heap_base + off + cur->size);
            if (nxt->used || nxt->size == 0) break;
            cur->size += nxt->size;
        }
        off += cur->size;
    }
}

void *kmalloc(unsigned int size) { return kmem64_alloc((size_t)size); }
size_t kmem64_used_bytes(void) { return heap_used; }
size_t kmem64_total_bytes(void) { return heap_total; }
