/* Paging. The kernel page directory identity-maps the 32-bit address space with
   supervisor-only 4 MiB pages. Splitting a large page preserves that protection;
   vm_map may opt an individual PTE into user access when explicitly requested.

   Each app gets a private page directory. Its low-memory page table keeps kernel
   pages supervisor-only and exposes only the copied 256 KiB APP_LO..APP_HI
   app/stack region. The heap has its own user-accessible page table. Kernel mappings remain
   supervisor-only in every app address space.

   The app heap (HEAP_BASE..HEAP_LIMIT) starts empty; vm_brk maps zeroed frames into
   it on demand. It sits above 256 MB, so it relies on RAM ending below that (QEMU's
   default fits). Nothing the kernel uses lives at those virtual addresses. */

#include <cpuid.h>
#include "../include/vm.h"
#include "../include/pmm.h"
#include "../include/vm_region.h"

#define FLAG_P   0x001u
#define FLAG_RW  0x002u
#define FLAG_US  0x004u
#define FLAG_PS  0x080u
#define CR4_PSE  0x00000010u
#define CR0_PG   0x80000000u
#define ADDR_MASK 0xFFFFF000u

#define APP_LO    0x00040000u
#define APP_HI    0x00080000u
#define APP_PAGES ((APP_HI - APP_LO) / 4096u)

#define HEAP_BASE  0x10000000u
#define HEAP_LIMIT 0x10400000u
#define HEAP_PDI   (HEAP_BASE >> 22)

void enter_user(unsigned int entry, unsigned int user_esp);

static unsigned int pd[1024] __attribute__((aligned(4096)));
static int paging_on;

static unsigned int app_pd;
static unsigned int app_pt;
static unsigned int app_hpt;
static unsigned int heap_top;
static unsigned int app_frames[APP_PAGES];

/* Kernel-only virtual allocation arena; allocations replace identity PTEs. */
#define KERNEL_VM_BASE  0xC0000000u
#define KERNEL_VM_LIMIT 0xE0000000u
#define VM_REGION_MAPPED 0x80000000u

static struct vm_region kernel_vm_storage[VM_REGION_MAX];
static struct vm_region_set kernel_vm_regions;
static int kernel_vm_regions_ready;

static void load_cr3(unsigned int phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(phys) : "memory");
}

static void write_cr3(void) {
    load_cr3((unsigned int)(unsigned long)pd);
}

static unsigned int read_cr3(void) {
    unsigned int value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value & ADDR_MASK;
}

static unsigned int kernel_pd_phys(void) {
    return (unsigned int)(unsigned long)pd & ADDR_MASK;
}

static int kernel_vm_context_ready(void) {
    return paging_on && kernel_vm_regions_ready &&
           read_cr3() == kernel_pd_phys();
}

static void invlpg(unsigned int virt) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static void zero_frame(unsigned int frame) {
    unsigned int *p = (unsigned int *)(unsigned long)frame;
    unsigned int k;

    for (k = 0; k < 1024u; k++)
        p[k] = 0;
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
        pt[j] = (base + (j << 12)) | FLAG_RW | FLAG_P;
    pd[pdi] = frame | FLAG_RW | FLAG_P;
    write_cr3();
    return 0;
}

int vm_map(unsigned int virt, unsigned int phys, unsigned int flags) {
    unsigned int pdi;

    if ((virt & 0xFFFu) || (phys & 0xFFFu) ||
        (flags & ~(VM_WRITE | VM_USER)))
        return -1;
    pdi = virt >> 22;
    unsigned int pti = (virt >> 12) & 0x3FFu;
    unsigned int *pt;

    if (!paging_on)
        return -1;
    if ((pd[pdi] & FLAG_PS) && split_pde(pdi) != 0)
        return -1;
    if (!(pd[pdi] & FLAG_P))
        return -1;
    pt = (unsigned int *)(unsigned long)(pd[pdi] & ADDR_MASK);
    if (pt[pti] & FLAG_P)
        return -1;
    pt[pti] = (phys & ADDR_MASK) | (flags & 0xFFFu) | FLAG_P;
    if (flags & FLAG_US)
        pd[pdi] |= FLAG_US;
    invlpg(virt);
    return 0;
}

int vm_unmap(unsigned int virt) {
    unsigned int pdi;

    if (virt & 0xFFFu)
        return -1;
    pdi = virt >> 22;
    unsigned int pti = (virt >> 12) & 0x3FFu;
    unsigned int *pt;

    if (!paging_on)
        return -1;
    if ((pd[pdi] & FLAG_PS) && split_pde(pdi) != 0)
        return -1;
    if (!(pd[pdi] & FLAG_P))
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
    if (vm_unmap(virt) == 0 && vm_map(virt, frame, FLAG_RW) == 0) {
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

static void release_app_space(void) {
    unsigned int k, j;
    unsigned int *hpt;

    for (k = 0; k < APP_PAGES; k++) {
        if (app_frames[k])
            pmm_free_frame(app_frames[k]);
        app_frames[k] = 0;
    }
    if (app_hpt) {
        hpt = (unsigned int *)(unsigned long)app_hpt;
        for (j = 0; j < 1024u; j++) {
            if (hpt[j] & FLAG_P)
                pmm_free_frame(hpt[j] & ADDR_MASK);
        }
        pmm_free_frame(app_hpt);
    }
    if (app_pt)
        pmm_free_frame(app_pt);
    if (app_pd)
        pmm_free_frame(app_pd);
    app_hpt = 0;
    app_pt = 0;
    app_pd = 0;
    heap_top = 0;
}

static int build_app_space(void) {
    unsigned int *npd, *npt, *kpt;
    unsigned int i, j, k, frame, virt;
    unsigned int pd_frame, pt_frame, hpt_frame;
    const unsigned char *src;
    unsigned char *dst;

    release_app_space();
    pd_frame = pmm_alloc_frame();
    if (!pd_frame)
        return -1;
    app_pd = pd_frame;
    pt_frame = pmm_alloc_frame();
    if (!pt_frame) {
        release_app_space();
        return -1;
    }
    app_pt = pt_frame;

    npd = (unsigned int *)(unsigned long)pd_frame;
    npt = (unsigned int *)(unsigned long)pt_frame;
    for (i = 0; i < 1024u; i++)
        npd[i] = pd[i];
    if (pd[0] & FLAG_PS) {
        for (j = 0; j < 1024u; j++)
            npt[j] = (j << 12) | FLAG_RW | FLAG_P;
    } else {
        kpt = (unsigned int *)(unsigned long)(pd[0] & ADDR_MASK);
        for (j = 0; j < 1024u; j++)
            npt[j] = kpt[j];
    }
    npd[0] = pt_frame | FLAG_US | FLAG_RW | FLAG_P;

    for (k = 0; k < APP_PAGES; k++) {
        virt = APP_LO + (k << 12);
        frame = pmm_alloc_frame();
        if (!frame) {
            release_app_space();
            return -1;
        }
        app_frames[k] = frame;
        src = (const unsigned char *)(unsigned long)virt;
        dst = (unsigned char *)(unsigned long)frame;
        for (j = 0; j < 4096u; j++)
            dst[j] = src[j];
        npt[virt >> 12] = frame | FLAG_US | FLAG_RW | FLAG_P;
    }

    hpt_frame = pmm_alloc_frame();
    if (!hpt_frame) {
        release_app_space();
        return -1;
    }
    zero_frame(hpt_frame);
    app_hpt = hpt_frame;
    npd[HEAP_PDI] = hpt_frame | FLAG_US | FLAG_RW | FLAG_P;

    /* Prove the app can reach its image/stack and heap, but not kernel text,
       neighboring low memory, or the next page beyond its private slot. */
    if (!(npd[0] & FLAG_US) || !(npd[HEAP_PDI] & FLAG_US) ||
        (npt[0x10000u >> 12] & FLAG_US) ||
        !(npt[APP_LO >> 12] & FLAG_US) ||
        (npt[APP_HI >> 12] & FLAG_US) ||
        (npd[1] & FLAG_US)) {
        serial_str("vm: app permission check FAILED\n");
        release_app_space();
        return -1;
    }
    serial_str("vm: app permissions isolated\n");
    heap_top = HEAP_BASE;
    return 0;
}

int vm_run_app(unsigned int entry, unsigned int user_esp) {
    if (!paging_on) {
        enter_user(entry, user_esp);
        return 0;
    }
    if (build_app_space() != 0) {
        serial_str("vm: app space failed\n");
        return -1;
    }
    serial_str("vm: app space ready\n");
    load_cr3(app_pd);
    enter_user(entry, user_esp);
    write_cr3();
    return 0;
}

/* Grows the current app's heap to `want` bytes of break address and returns the
   break. A request at or below the current break (including 0) only queries it, so
   shrinking is not supported. On failure the break stops short of `want`. The
   frames are written and mapped under the kernel directory, because the app's view
   of low frames may differ from their physical contents; the app directory is
   reloaded afterwards, which also flushes the TLB. */
int vm_is_ready(void) {
    return paging_on;
}

unsigned int vm_brk(unsigned int want) {
    unsigned int *hpt;
    unsigned int v, end, frame;

    if (!paging_on || !app_hpt || want <= heap_top || want > HEAP_LIMIT)
        return heap_top;

    load_cr3((unsigned int)(unsigned long)pd);
    hpt = (unsigned int *)(unsigned long)app_hpt;
    end = (want + 4095u) & ~4095u;
    for (v = (heap_top + 4095u) & ~4095u; v < end; v += 4096u) {
        frame = pmm_alloc_frame();
        if (!frame)
            break;
        zero_frame(frame);
        hpt[(v - HEAP_BASE) >> 12] = frame | FLAG_US | FLAG_RW | FLAG_P;
    }
    heap_top = (v >= end) ? want : v;
    load_cr3(app_pd);
    return heap_top;
}

int vm_init(void) {
    unsigned int a, b, c, d, cr4, cr0, i;

    if (!__get_cpuid(1, &a, &b, &c, &d) || !(d & (1u << 3))) {
        serial_str("vm: no PSE, paging off\n");
        return 0;
    }
    for (i = 0; i < 1024u; i++)
        pd[i] = (i << 22) | FLAG_PS | FLAG_RW | FLAG_P;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    write_cr3();
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    paging_on = 1;
    if (vm_regions_init(&kernel_vm_regions, kernel_vm_storage,
                        VM_REGION_MAX, KERNEL_VM_BASE, KERNEL_VM_LIMIT,
                        4096u) == 0) {
        kernel_vm_regions_ready = 1;
        serial_str("vm: kernel virtual region manager ready\n");
    } else {
        kernel_vm_regions_ready = 0;
        serial_str("vm: kernel virtual region manager FAILED\n");
    }
    serial_str("vm: paging on, 4 GB identity map\n");
    serial_str(vm_selftest() ? "vm: selftest passed\n" : "vm: selftest FAILED\n");
    return 1;
}


/* Query the kernel page directory. Physical results are page-aligned. */
static int vm_kernel_lookup_mapping(unsigned int virt, unsigned int *phys,
                                    unsigned int *flags) {
    unsigned int pdi=virt>>22, pti=(virt>>12)&0x3FFu;
    unsigned int pde=pd[pdi], *pt, pte;

    if (!paging_on || (virt&0xFFFu) || !(pde&FLAG_P))
        return -1;
    if (pde&FLAG_PS) {
        if (phys) *phys=(pde&0xFFC00000u)|(virt&0x003FF000u);
        if (flags) *flags=pde&0xFFFu;
        return 0;
    }
    pt=(unsigned int *)(unsigned long)(pde&ADDR_MASK);
    pte=pt[pti];
    if (!(pte&FLAG_P)) return -1;
    if (phys) *phys=pte&ADDR_MASK;
    if (flags) *flags=pte&0xFFFu;
    return 0;
}

/* Metadata-only reservation: this does not acquire frames or alter PTEs. */
int vm_kernel_reserve_region(unsigned int start, unsigned int length,
                             unsigned int flags, unsigned int owner) {
    if (!paging_on || !kernel_vm_regions_ready ||
        (flags&VM_REGION_MAPPED))
        return -1;
    return vm_region_reserve(&kernel_vm_regions,start,length,flags,owner);
}

int vm_kernel_release_region(unsigned int start, unsigned int owner) {
    struct vm_region region;

    if (!paging_on || !kernel_vm_regions_ready ||
        vm_region_find(&kernel_vm_regions, start, &region) != 0 ||
        region.start != start || region.owner != owner ||
        (region.flags & VM_REGION_MAPPED))
        return -1;

    return vm_region_release(&kernel_vm_regions, start, owner);
}

int vm_kernel_find_region(unsigned int address, struct vm_region *result) {
    if (!paging_on || !kernel_vm_regions_ready) return -1;
    return vm_region_find(&kernel_vm_regions,address,result);
}

/* Allocate supervisor pages from PMM. Only the kernel page directory is
   supported until independent process address-space objects are implemented. */
int vm_kernel_alloc(unsigned int length, unsigned int alignment,
                    unsigned int flags, unsigned int owner,
                    unsigned int *result) {
    struct vm_region region;
    unsigned int base, virt, frame;

    if (!kernel_vm_context_ready() || !result || (flags&~VM_WRITE))
        return -1;
    if (vm_region_alloc(&kernel_vm_regions,length,alignment,
                        flags|VM_REGION_MAPPED,owner,&base)!=0)
        return -1;
    if (vm_region_find(&kernel_vm_regions,base,&region)!=0 ||
        region.start!=base) {
        (void)vm_region_release(&kernel_vm_regions,base,owner);
        return -1;
    }

    for (virt=base;virt<region.end;virt+=4096u) {
        unsigned int old_phys;
        /* Initially the kernel maps the entire 32-bit space identically. */
        if (vm_kernel_lookup_mapping(virt,&old_phys,0)==0) {
            if (old_phys!=virt || vm_unmap(virt)!=0) goto rollback;
        }
        frame=pmm_alloc_frame();
        if (!frame) goto rollback;
        zero_frame(frame);
        if (vm_map(virt,frame,flags)!=0) {
            (void)pmm_free_frame(frame);
            goto rollback;
        }
    }
    *result=base;
    return 0;

rollback:
    for (unsigned int undo=base;undo<virt;undo+=4096u) {
        unsigned int mapped_phys;
        if (vm_kernel_lookup_mapping(undo,&mapped_phys,0)==0) {
            (void)vm_unmap(undo);
            (void)pmm_free_frame(mapped_phys);
        }
    }
    (void)vm_region_release(&kernel_vm_regions,base,owner);
    return -1;
}

/* Free only a whole vm_kernel_alloc region. Raw vm_unmap stays non-owning. */
int vm_kernel_free(unsigned int base, unsigned int owner) {
    struct vm_region region;
    unsigned int virt,phys;

    if (!kernel_vm_context_ready() ||
        vm_region_find(&kernel_vm_regions,base,&region)!=0 ||
        region.start!=base || !(region.flags&VM_REGION_MAPPED) ||
        region.owner!=owner)
        return -1;

    /* Preflight every page so a bad region cannot be half-unmapped. */
    for (virt=region.start;virt<region.end;virt+=4096u) {
        if (vm_kernel_lookup_mapping(virt,&phys,0)!=0 || phys==virt)
            return -1;
    }
    for (virt=region.start;virt<region.end;virt+=4096u) {
        if (vm_kernel_lookup_mapping(virt,&phys,0)!=0 ||
            vm_unmap(virt)!=0 || pmm_free_frame(phys)!=0)
            return -1;
    }
    return vm_region_release(&kernel_vm_regions,base,owner);
}
