/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../include/mm_core.h"
#define PAGE_SIZE 4096u
static unsigned int get(const unsigned char *b,unsigned int i){return (b[i>>3]>>(i&7u))&1u;}
static void set(unsigned char *b,unsigned int i,unsigned int v){unsigned char m=(unsigned char)(1u<<(i&7u));if(v)b[i>>3]|=m;else b[i>>3]&=(unsigned char)~m;}
void mm_frames_init(struct mm_frame_allocator *m,unsigned char *a,unsigned char *g,unsigned char *r,unsigned int base,unsigned int count){
 m->available=a;m->managed=g;m->reserved=r;m->base=base;m->frame_count=count;
 m->free_count=m->managed_count=m->reserved_count=m->hint=0;
 for(unsigned int i=0;i<(count+7u)/8u;i++)a[i]=g[i]=r[i]=0;
}
unsigned int mm_frames_add_range(struct mm_frame_allocator *m,unsigned int base,unsigned int length){
 if(!length)return 0;
 unsigned int end=(base>0xFFFFFFFFu-length)?0xFFFFFFFFu:base+length;
 unsigned int limit=m->base+m->frame_count*PAGE_SIZE;
 if(end<=m->base||base>=limit)return 0;
 if (base < m->base)
  base = m->base;
 if (end > limit)
  end = limit;
 base=(base+PAGE_SIZE-1u)&~(PAGE_SIZE-1u);end&=~(PAGE_SIZE-1u);
 unsigned int added=0;
 for(unsigned int a=base;a<end;a+=PAGE_SIZE){unsigned int i=(a-m->base)/PAGE_SIZE;if(!get(m->managed,i)){set(m->managed,i,1);set(m->available,i,1);m->free_count++;m->managed_count++;added++;}}
 return added;
}
unsigned int mm_frames_alloc(struct mm_frame_allocator *m){
 if(!m->free_count)return 0;
 for(unsigned int n=0;n<m->frame_count;n++){unsigned int i=m->hint+n;if(i>=m->frame_count)i-=m->frame_count;if(get(m->available,i)){set(m->available,i,0);m->free_count--;m->hint=(i+1u<m->frame_count)?i+1u:0;return m->base+i*PAGE_SIZE;}}
 return 0;
}
unsigned int mm_frames_alloc_contiguous(struct mm_frame_allocator *m,unsigned int count){
 if(!count||count>m->free_count)return 0;
 unsigned int run=0,start=0;
 for(unsigned int i=0;i<m->frame_count;i++){if(!get(m->available,i)){run=0;continue;}if(!run)start=i;if(++run==count){for(unsigned int k=start;k<=i;k++)set(m->available,k,0);m->free_count-=count;return m->base+start*PAGE_SIZE;}}
 return 0;
}
int mm_frames_free(struct mm_frame_allocator *m,unsigned int address){
 if(address<m->base||(address-m->base)%PAGE_SIZE)return -1;
 unsigned int i=(address-m->base)/PAGE_SIZE;
 if(i>=m->frame_count||!get(m->managed,i)||get(m->reserved,i)||get(m->available,i))return -1;
 set(m->available,i,1);m->free_count++;return 0;
}
int mm_frames_reserve(struct mm_frame_allocator *m,unsigned int base,unsigned int length){
 if(!length||base>0xFFFFFFFFu-length)return -1;
 unsigned int end=base+length,limit=m->base+m->frame_count*PAGE_SIZE;
 if(end<=m->base||base>=limit)return 0;
 if (base < m->base)
  base = m->base;
 if (end > limit)
  end = limit;
 unsigned int start=base&~(PAGE_SIZE-1u);
 if(end>0xFFFFF000u)end=0xFFFFFFFFu;else end=(end+PAGE_SIZE-1u)&~(PAGE_SIZE-1u);
 if(end>limit)end=limit;
 unsigned int n=0;
 for(unsigned int a=start;a<end;a+=PAGE_SIZE){if(a<m->base||a>=limit)continue;unsigned int i=(a-m->base)/PAGE_SIZE;if(!get(m->managed,i)||get(m->reserved,i)||!get(m->available,i))continue;set(m->reserved,i,1);set(m->available,i,0);m->free_count--;m->reserved_count++;n++;}
 return (int)n;
}
