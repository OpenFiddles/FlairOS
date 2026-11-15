#include <assert.h>
#include <stdio.h>
#include "../include/vm_region.h"
int main(void){
 struct vm_region slots[8],r;struct vm_region_set s;unsigned int a,b;
 assert(vm_regions_init(&s,slots,8,0x10000000,0x11000000,4096)==0);
 assert(vm_region_reserve(&s,0x10002000,0x2000,3,7)==0);
 assert(vm_region_reserve(&s,0x10003000,0x1000,0,8)==-1); /* overlap */
 assert(vm_region_reserve(&s,0x10005001,4096,0,8)==-1); /* unaligned */
 assert(vm_region_find(&s,0x10002010,&r)==0&&r.owner==7);
 assert(vm_region_find(&s,0x10004000,&r)==-1); /* end exclusive */
 assert(vm_region_alloc(&s,0x3000,0x10000,1,9,&a)==0&&a==0x10010000);
 assert(vm_region_alloc(&s,0x2000,4096,1,10,&b)==0&&b==0x10000000);
 assert(vm_region_release(&s,a,10)==-1); /* wrong owner */
 assert(vm_region_release(&s,a,9)==0);
 assert(vm_region_release(&s,a,9)==-1); /* double release */
 assert(vm_region_alloc(&s,0x1000,4096,0,11,&a)==0&&a==0x10010000);
 puts("VM region tests passed");return 0;
}
