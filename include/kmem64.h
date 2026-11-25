#ifndef FLAIROS_KMEM64_H
#define FLAIROS_KMEM64_H

#include <stddef.h>
#include <stdint.h>

void kmem64_init(void *pool, size_t bytes);
void *kmem64_alloc(size_t size);
void *kcalloc(size_t nmemb, size_t size);
void kfree(void *ptr);
size_t kmem64_used_bytes(void);
size_t kmem64_total_bytes(void);

#endif
