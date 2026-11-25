#include "../include/ashmem.h"

#ifdef FLAIROS_HOST_TEST
#include <stdlib.h>
static void *ash_alloc(unsigned int sz) { return malloc(sz); }
static void ash_free(void *p) { free(p); }
#else
#include "../include/kheap.h"
static void *ash_alloc(unsigned int sz) { return kmalloc(sz); }
static void ash_free(void *p) { kfree(p); }
#endif

static struct ashmem_region *regions;
static int next_ashmem_id = 1;

static int ash_eq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void ash_copy(char *dst, const char *src) {
    int i = 0;
    if (src) {
        while (src[i] && i < ASHMEM_NAME_MAX - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

int ashmem_init(void) {
    if (!regions)
        regions = (struct ashmem_region *)ash_alloc(sizeof(struct ashmem_region) * ASHMEM_MAX_REGIONS);
    if (!regions) return -1;
    for (int i = 0; i < ASHMEM_MAX_REGIONS; i++) {
        regions[i].used = 0;
        regions[i].id = 0;
        regions[i].size = 0;
        regions[i].refcount = 0;
        regions[i].data = 0;
        regions[i].name[0] = 0;
    }
    return 0;
}

int ashmem_create(const char *name, uint32_t size) {
    if ((!regions && ashmem_init() != 0) || size == 0 || size > 4194304u)
        return -1;
    for (int i = 0; i < ASHMEM_MAX_REGIONS; i++) {
        if (!regions[i].used) {
            void *mem = ash_alloc(size);
            if (!mem) return -1;
            uint8_t *b = (uint8_t *)mem;
            for (uint32_t j = 0; j < size; j++) b[j] = 0;
            regions[i].used = 1;
            regions[i].id = next_ashmem_id++;
            regions[i].size = size;
            regions[i].refcount = 1;
            regions[i].data = mem;
            ash_copy(regions[i].name, name ? name : "anon");
            return regions[i].id;
        }
    }
    return -1;
}

int ashmem_find(const char *name) {
    if (!regions || !name) return -1;
    for (int i = 0; i < ASHMEM_MAX_REGIONS; i++) {
        if (regions[i].used && ash_eq(regions[i].name, name))
            return regions[i].id;
    }
    return -1;
}

void *ashmem_map(int id, uint32_t *size_out) {
    if (!regions || id <= 0) return 0;
    for (int i = 0; i < ASHMEM_MAX_REGIONS; i++) {
        if (regions[i].used && regions[i].id == id) {
            regions[i].refcount++;
            if (size_out) *size_out = regions[i].size;
            return regions[i].data;
        }
    }
    return 0;
}

int ashmem_release(int id) {
    if (!regions || id <= 0) return -1;
    for (int i = 0; i < ASHMEM_MAX_REGIONS; i++) {
        if (regions[i].used && regions[i].id == id) {
            if (regions[i].refcount > 0) regions[i].refcount--;
            if (regions[i].refcount == 0) {
                if (regions[i].data) ash_free(regions[i].data);
                regions[i].data = 0;
                regions[i].used = 0;
            }
            return 0;
        }
    }
    return -1;
}
