#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../include/mm_core.h"
#define N 16
int main(void){
 unsigned char a[2],m[2],r[2],a2[1],m2[1],r2[1];struct mm_frame_allocator f,f2;
 mm_frames_init(&f,a,m,r,0x100000,N);
 assert(mm_frames_add_range(&f,0x100000,8*4096)==8);
 assert(mm_frames_add_range(&f,0x102000,4*4096)==0); /* duplicate range */
 assert(f.managed_count==8&&f.free_count==8);
 mm_frames_init(&f2,a2,m2,r2,0x200000,4);
 assert(mm_frames_add_range(&f2,0x100000,0xFFFFFFFFu)==4); /* saturating end, no overflow */
 assert(mm_frames_reserve(&f,0x101001,4096)==2); /* touches two pages */
 assert(f.reserved_count==2&&f.free_count==6);
 assert(mm_frames_reserve(&f,0x101000,4096)==0); /* already reserved */
 unsigned int x=mm_frames_alloc(&f),y=mm_frames_alloc(&f);
 assert(x&&y&&x!=y&&f.free_count==4);
 assert(mm_frames_free(&f,x)==0);
 assert(mm_frames_free(&f,x)==-1); /* double free */
 assert(mm_frames_free(&f,0x101000)==-1); /* reserved */
 assert(mm_frames_free(&f,0x200000)==-1); /* unmanaged */
 unsigned int run=mm_frames_alloc_contiguous(&f,3);
 assert(run!=0&&f.free_count==2); /* two free frames remain, but are fragmented */
 assert(mm_frames_alloc_contiguous(&f,2)==0); /* no contiguous pair remains */
 assert(mm_frames_free(&f,y)==0);
 assert(f.free_count==3);
 puts("pmm core tests passed");return 0;
}
