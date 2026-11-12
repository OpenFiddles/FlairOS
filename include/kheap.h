#ifndef KHEAP_H
#define KHEAP_H

void *kmalloc(unsigned int size);
void kfree(void *p);
unsigned int kheap_live_bytes(void);
int kheap_selftest(void);

#endif
