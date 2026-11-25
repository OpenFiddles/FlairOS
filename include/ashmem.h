#ifndef FLAIROS_ASHMEM_H
#define FLAIROS_ASHMEM_H

#include <stdint.h>

#define ASHMEM_MAX_REGIONS 16
#define ASHMEM_NAME_MAX    32

struct ashmem_region {
    int used;
    int id;
    uint32_t size;
    uint32_t refcount;
    void *data;
    char name[ASHMEM_NAME_MAX];
};

int ashmem_init(void);
int ashmem_create(const char *name, uint32_t size);
void *ashmem_map(int id, uint32_t *size_out);
int ashmem_find(const char *name);
int ashmem_release(int id);

#endif
