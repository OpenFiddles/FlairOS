#include <stdio.h>
#include <stdint.h>
#include "airgraph.h"
#include "fb.h"
#include "desktop_render.h"
static uint32_t pixels[20*24];
#define CHECK(x) do{if(!(x)){printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}}while(0)
int main(void){
 struct fb fb;struct ag_surface s;struct ag_device d;
 uint16_t cells[2]={(uint16_t)('A'|(0x1Fu<<8)),(uint16_t)(0xDBu|(0x2Eu<<8))};
 fb_init(&fb,pixels,20,24,20*4);
 CHECK(ag_surface_init(&s,pixels,20,24,20*4,AG_PIXEL_XRGB8888)==0);
 CHECK(ag_device_init(&d,&fb,0)==0);
 CHECK(desktop_render_cells(&d,&s,cells,2,1)==0);
 CHECK(pixels[0]==0x0000AA);
 CHECK(pixels[2+5*20]==0xFFFFFF);
 CHECK(pixels[10]==0xFFFF55);
 CHECK(desktop_render_cells(&d,&s,cells,0,1)!=0);
 ag_device_shutdown(&d);puts("Desktop pixel renderer host test: all checks passed");return 0;
}
