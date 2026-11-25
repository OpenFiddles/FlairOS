#ifndef VM_H
#define VM_H

#define VM_WRITE 0x002u
#define VM_USER  0x004u

struct vm_region;

/* Kernel virtual regions: reserve metadata, allocate PMM-backed mappings,
   or free a complete allocation using its owner ID. */
int vm_kernel_reserve_region(unsigned int start, unsigned int length,
                             unsigned int flags, unsigned int owner);
int vm_kernel_find_region(unsigned int address, struct vm_region *result);
int vm_kernel_release_region(unsigned int start, unsigned int owner);
int vm_kernel_alloc(unsigned int length, unsigned int alignment,
                    unsigned int flags, unsigned int owner,
                    unsigned int *result);
int vm_kernel_free(unsigned int base, unsigned int owner);

int vm_init(void);
int vm_map(unsigned int virt, unsigned int phys, unsigned int flags);
int vm_unmap(unsigned int virt);
int vm_selftest(void);
int vm_run_app(unsigned int entry, unsigned int user_esp);
unsigned int vm_brk(unsigned int want);
int vm_is_ready(void);
unsigned int vm_mmap(unsigned int length);
int vm_munmap(unsigned int addr, unsigned int length);

#endif
