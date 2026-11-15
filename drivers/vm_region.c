/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../include/vm_region.h"
static int aligned(unsigned int v,unsigned int a){return a&&!(v&(a-1u));}
int vm_regions_init(struct vm_region_set *s,struct vm_region *items,unsigned int cap,unsigned int base,unsigned int limit,unsigned int page){
 if(!s||!items||!cap||!page||(page&(page-1u))||base>=limit||!aligned(base,page)||!aligned(limit,page))return -1;
 s->items=items;s->count=0;s->capacity=cap;s->base=base;s->limit=limit;s->page_size=page;return 0;
}
int vm_region_reserve(struct vm_region_set *s,unsigned int start,unsigned int length,unsigned int flags,unsigned int owner){
 if(!s||!length||start<s->base||start>=s->limit||start&(s->page_size-1u)||length&(s->page_size-1u)||length>s->limit-start||s->count>=s->capacity)return -1;
 unsigned int end=start+length,pos=0;
 while(pos<s->count&&s->items[pos].start<start)pos++;
 if((pos&&s->items[pos-1].end>start)||(pos<s->count&&end>s->items[pos].start))return -1;
 for(unsigned int i=s->count;i>pos;i--)s->items[i]=s->items[i-1];
 s->items[pos].start=start;s->items[pos].end=end;s->items[pos].flags=flags;s->items[pos].owner=owner;s->count++;return 0;
}
int vm_region_alloc(struct vm_region_set *s,unsigned int length,unsigned int alignment,unsigned int flags,unsigned int owner,unsigned int *result){
 if(!s||!result||!length||length>s->limit-s->base)return -1;
 if(alignment<s->page_size)alignment=s->page_size;
 if((alignment&(alignment-1u))||length>0xFFFFFFFFu-(s->page_size-1u))return -1;
 if(s->base>0xFFFFFFFFu-(alignment-1u))return -1;
 length=(length+s->page_size-1u)&~(s->page_size-1u);
 unsigned int at=(s->base+alignment-1u)&~(alignment-1u);
 for(unsigned int i=0;i<=s->count;i++){
  unsigned int next=(i<s->count)?s->items[i].start:s->limit;
  if(at<=next&&length<=next-at){if(vm_region_reserve(s,at,length,flags,owner))return -1;*result=at;return 0;}
  if(i<s->count){unsigned int after=s->items[i].end;if(after>0xFFFFFFFFu-(alignment-1u))return -1;at=(after+alignment-1u)&~(alignment-1u);}
 }
 return -1;
}
int vm_region_release(struct vm_region_set *s,unsigned int start,unsigned int owner){
 if(!s)return -1;for(unsigned int i=0;i<s->count;i++)if(s->items[i].start==start){if(s->items[i].owner!=owner)return -1;for(unsigned int j=i+1;j<s->count;j++)s->items[j-1]=s->items[j];s->count--;return 0;}return -1;
}
int vm_region_find(const struct vm_region_set *s,unsigned int address,struct vm_region *out){
 if(!s)return -1;for(unsigned int i=0;i<s->count;i++)if(address>=s->items[i].start&&address<s->items[i].end){if(out)*out=s->items[i];return 0;}return -1;
}
