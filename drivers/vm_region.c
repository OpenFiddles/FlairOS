/* SPDX-License-Identifier: AGPL-3.0-only */
#include "../include/vm_region.h"

static int aligned(unsigned int value,unsigned int alignment) {
    return alignment && !(value & (alignment - 1u));
}

int vm_regions_init(struct vm_region_set *set,struct vm_region *items,
                    unsigned int capacity,unsigned int base,
                    unsigned int limit,unsigned int page_size) {
    if (!set || !items || !capacity || !page_size ||
        (page_size & (page_size - 1u)) || base >= limit ||
        !aligned(base,page_size) || !aligned(limit,page_size))
        return -1;
    set->items=items;
    set->count=0;
    set->capacity=capacity;
    set->base=base;
    set->limit=limit;
    set->page_size=page_size;
    return 0;
}

int vm_region_reserve(struct vm_region_set *set,unsigned int start,
                      unsigned int length,unsigned int flags,
                      unsigned int owner) {
    unsigned int end,position=0;
    if (!set || !set->items || !set->page_size || !length ||
        start<set->base || start>=set->limit ||
        (start&(set->page_size-1u)) ||
        (length&(set->page_size-1u)) ||
        length>set->limit-start || set->count>=set->capacity)
        return -1;
    end=start+length;
    while (position<set->count && set->items[position].start<start)
        position++;
    if ((position && set->items[position-1u].end>start) ||
        (position<set->count && end>set->items[position].start))
        return -1;
    for (unsigned int i=set->count;i>position;i--)
        set->items[i]=set->items[i-1u];
    set->items[position].start=start;
    set->items[position].end=end;
    set->items[position].flags=flags;
    set->items[position].owner=owner;
    set->count++;
    return 0;
}

int vm_region_alloc(struct vm_region_set *set,unsigned int length,
                    unsigned int alignment,unsigned int flags,
                    unsigned int owner,unsigned int *result) {
    unsigned int address;
    if (!set || !result || !set->page_size || !length ||
        length>set->limit-set->base)
        return -1;
    if (alignment<set->page_size) alignment=set->page_size;
    if ((alignment&(alignment-1u)) ||
        length>0xFFFFFFFFu-(set->page_size-1u) ||
        set->base>0xFFFFFFFFu-(alignment-1u))
        return -1;
    length=(length+set->page_size-1u)&~(set->page_size-1u);
    address=(set->base+alignment-1u)&~(alignment-1u);
    for (unsigned int i=0;i<=set->count;i++) {
        unsigned int next=(i<set->count)?set->items[i].start:set->limit;
        if (address<=next && length<=next-address) {
            if (vm_region_reserve(set,address,length,flags,owner)!=0)
                return -1;
            *result=address;
            return 0;
        }
        if (i<set->count) {
            unsigned int after=set->items[i].end;
            if (after>0xFFFFFFFFu-(alignment-1u)) return -1;
            address=(after+alignment-1u)&~(alignment-1u);
        }
    }
    return -1;
}

int vm_region_release(struct vm_region_set *set,unsigned int start,
                      unsigned int owner) {
    if (!set || !set->items) return -1;
    for (unsigned int i=0;i<set->count;i++) {
        if (set->items[i].start!=start) continue;
        if (set->items[i].owner!=owner) return -1;
        for (unsigned int j=i+1u;j<set->count;j++)
            set->items[j-1u]=set->items[j];
        set->count--;
        return 0;
    }
    return -1;
}

int vm_region_find(const struct vm_region_set *set,unsigned int address,
                   struct vm_region *result) {
    if (!set || !set->items) return -1;
    for (unsigned int i=0;i<set->count;i++) {
        if (address<set->items[i].start ||
            address>=set->items[i].end)
            continue;
        if (result) *result=set->items[i];
        return 0;
    }
    return -1;
}
