#ifndef VM_H
#define VM_H

#define VM_WRITE 0x002u
#define VM_USER  0x004u

int vm_init(void);
int vm_map(unsigned int virt, unsigned int phys, unsigned int flags);
int vm_unmap(unsigned int virt);
int vm_selftest(void);

#endif
