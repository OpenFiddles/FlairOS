/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef FLAIROS_VM_REGION_H
#define FLAIROS_VM_REGION_H
#define VM_REGION_MAX 64u
struct vm_region { unsigned int start,end,flags,owner; };
struct vm_region_set { struct vm_region *items; unsigned int count,capacity,base,limit,page_size; };
int vm_regions_init(struct vm_region_set *set,struct vm_region *storage,unsigned int capacity,unsigned int base,unsigned int limit,unsigned int page_size);
int vm_region_reserve(struct vm_region_set *set,unsigned int start,unsigned int length,unsigned int flags,unsigned int owner);
int vm_region_alloc(struct vm_region_set *set,unsigned int length,unsigned int alignment,unsigned int flags,unsigned int owner,unsigned int *result);
int vm_region_release(struct vm_region_set *set,unsigned int start,unsigned int owner);
int vm_region_find(const struct vm_region_set *set,unsigned int address,struct vm_region *result);
#endif
