/* Paging. One page directory identity-maps the whole 32-bit space with 4 MiB
   pages, so every address the kernel uses today (framebuffer, PMM frames, app
   slot, stacks) keeps its physical address. vm_map and vm_unmap split a 4 MiB
   page into a 4 KiB page table on demand; the tables come from PMM frames.
   Every entry carries the user bit, so nothing is protected yet. */

#include <cpuid.h>
#include "../include/vm.h"
#include "../include/pmm.h"

#define FLAG_P   0x001u
#define FLAG_RW  0x002u
#define FLAG_US  0x004u
#define FLAG_PS  0x080u
#define CR4_PSE  0x00000010u
#define CR0_PG   0x80000000u
#define ADDR_MASK 0xFFFFF000u

static unsigned int pd[1024] __attribute__((aligned(4096)));
static int paging_on;

static void write_cr3(void) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"((unsigned int)(unsigned long)pd) : "memory");
}

static void invlpg(unsigned int virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static int split_pde(unsigned int pdi) {
    unsigned int frame = pmm_alloc_frame();
    unsigned int *pt;
    unsigned int base = pdi << 22;
    unsigned int j;

    if (!frame)
        return -1;
    pt = (unsigned int *)(unsigned long)frame;
    for (j = 0; j < 1024u; j++)
        pt[j] = (base + (j << 12)) | FLAG_US | FLAG_RW | FLAG_P;
    pd[pdi] = frame | FLAG_US | FLAG_RW | FLAG_P;
    write_cr3();
    return 0;
}

int vm_map(unsigned int virt, unsigned int phys, unsigned int flags) {
    unsigned int pdi = virt >> 22;
    unsigned int pti = (virt >> 12) & 0x3FFu;
    unsigned int *pt;

    if (!paging_on)
        return -1;
    if ((pd[pdi] & FLAG_PS) && split_pde(pdi) != 0)
        return -1;
    pt = (unsigned int *)(unsigned long)(pd[pdi] & ADDR_MASK);
    if (pt[pti] & FLAG_P)
        return -1;
    pt[pti] = (phys & ADDR_MASK) | (flags & 0xFFFu) | FLAG_P;
    invlpg(virt);
    return 0;
}

int vm_unmap(unsigned int virt) {
    unsigned int pdi = virt >> 22;
    unsigned int pti = (virt >> 12) & 0x3FFu;
    unsigned int *pt;

    if (!paging_on)
        return -1;
    if ((pd[pdi] & FLAG_PS) && split_pde(pdi) != 0)
        return -1;
    pt = (unsigned int *)(unsigned long)(pd[pdi] & ADDR_MASK);
    if (!(pt[pti] & FLAG_P))
        return -1;
    pt[pti] = 0;
    invlpg(virt);
    return 0;
}

int vm_selftest(void) {
    /* 0x40000000 lies in an identity-mapped 4 MiB page, so mapping it forces a
       split and exercises the 4 KiB path. Writes through the virtual address
       must land in the frame, which is read back through its physical address. */
    const unsigned int virt = 0x40000000u;
    const unsigned int pattern = 0xC0DE0000u;
    volatile unsigned int *v = (volatile unsigned int *)(unsigned long)virt;
    volatile unsigned int *p;
    unsigned int frame, k;
    int ok = 0;

    frame = pmm_alloc_frame();
    if (!paging_on || !frame) {
        if (frame)
            pmm_free_frame(frame);
        return 0;
    }
    if (vm_unmap(virt) == 0 && vm_map(virt, frame, FLAG_RW | FLAG_US) == 0) {
        ok = 1;
        for (k = 0; k < 1024u; k++)
            v[k] = pattern ^ k;
        p = (volatile unsigned int *)(unsigned long)frame;
        for (k = 0; k < 1024u; k++) {
            if (p[k] != (pattern ^ k)) {
                ok = 0;
                break;
            }
        }
        if (vm_unmap(virt) != 0)
            ok = 0;
    }
    pmm_free_frame(frame);
    return ok;
}

int vm_init(void) {
    unsigned int a, b, c, d, cr4, cr0, i;

    if (!__get_cpuid(1, &a, &b, &c, &d) || !(d & (1u << 3))) {
        serial_str("vm: no PSE, paging off\n");
        return 0;
    }
    for (i = 0; i < 1024u; i++)
        pd[i] = (i << 22) | FLAG_PS | FLAG_US | FLAG_RW | FLAG_P;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    write_cr3();
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    paging_on = 1;
    serial_str("vm: paging on, 4 GB identity map\n");
    serial_str(vm_selftest() ? "vm: selftest passed\n" : "vm: selftest FAILED\n");
    return 1;
}
