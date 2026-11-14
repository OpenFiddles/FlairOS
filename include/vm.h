#ifndef VM_H
#define VM_H

#define VM_WRITE 0x002u
#define VM_USER  0x004u

int vm_init(void);
int vm_map(unsigned int virt, unsigned int phys, unsigned int flags);
int vm_unmap(unsigned int virt);
int vm_selftest(void);
int vm_run_app(unsigned int entry, unsigned int user_esp);
unsigned int vm_brk(unsigned int want);
int vm_is_ready(void);

#endif
