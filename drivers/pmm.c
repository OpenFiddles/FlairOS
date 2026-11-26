/* SPDX-License-Identifier: AGPL-3.0-only */
/* i386 BIOS E820 adapter for the architecture-neutral physical frame core. */
#include "../include/pmm.h"
#include "../include/mm_core.h"
#include "../include/vm.h"
#include "../include/kheap.h"
#include "../include/boot.h"
#define E820_TYPE_USABLE 1u
#define E820_MAX_REGIONS 32u
#define FRAME_MIN 0x00100000u
#define FRAME_MAX 0x10000000u
#define FRAME_COUNT ((FRAME_MAX-FRAME_MIN)/PMM_FRAME_SIZE)
#define BITMAP_BYTES (FRAME_COUNT/8u)
#define AVAIL_BITS ((unsigned char *)0x80000u)
#define MANAGED_BITS ((unsigned char *)0x82000u)
#define RESERVED_BITS ((unsigned char *)0x84000u)
static struct mm_frame_allocator frames;
static void serial_dec(unsigned int v){char b[12];int n=11;b[n]=0;do{b[--n]=(char)('0'+v%10u);v/=10u;}while(v);serial_str(b+n);}
void pmm_init(void){
 /* The loader publishes the firmware map through the boot protocol. */
 const struct flair_e820_entry *entry=boot_e820_entries();
 unsigned int regions=entry?boot_e820_count():0;
 if(regions>E820_MAX_REGIONS){serial_str("pmm: E820 count exceeds buffer; clamping to 32\n");regions=E820_MAX_REGIONS;}
 mm_frames_init(&frames,AVAIL_BITS,MANAGED_BITS,RESERVED_BITS,FRAME_MIN,FRAME_COUNT);
 for(unsigned int k=0;k<regions;k++,entry++){
  if(entry->type!=E820_TYPE_USABLE||entry->base_hi!=0)continue;
  unsigned int length=entry->len_hi?0xFFFFFFFFu:entry->len_lo;
  mm_frames_add_range(&frames,entry->base_lo,length);
 }
 serial_str("pmm: E820 regions ");serial_dec(regions);serial_str("\n");
 serial_str("pmm: ");serial_dec(frames.free_count>>8);serial_str(" MB free of ");
 serial_dec(frames.managed_count>>8);serial_str(" MB managed\n");
 serial_str(pmm_selftest()?"pmm: selftest passed\n":"pmm: selftest FAILED\n");
 vm_init();kheap_selftest();
}
unsigned int pmm_alloc_frame(void){return mm_frames_alloc(&frames);}
unsigned int pmm_alloc_contig(unsigned int n){return mm_frames_alloc_contiguous(&frames,n);}
int pmm_free_frame(unsigned int a){return mm_frames_free(&frames,a);}
int pmm_reserve_range(unsigned int a,unsigned int n){return mm_frames_reserve(&frames,a,n);}
unsigned int pmm_free_frames(void){return frames.free_count;}
unsigned int pmm_managed_frames(void){return frames.managed_count;}
unsigned int pmm_reserved_frames(void){return frames.reserved_count;}
int pmm_selftest(void){
 unsigned int before=frames.free_count,a=pmm_alloc_frame(),b=pmm_alloc_frame();int ok=a&&b&&a!=b;
 if(a){if(pmm_free_frame(a)!=0)ok=0;if(pmm_free_frame(a)==0)ok=0;}
 if(b&&pmm_free_frame(b)!=0)ok=0;
 return ok&&frames.free_count==before;
}
