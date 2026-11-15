#include "../include/desktop_render.h"
#include "../include/font5x7.h"
static const uint32_t colors[16]={0x000000,0x0000AA,0x00AA00,0x00AAAA,0xAA0000,0xAA00AA,0xAA5500,0xAAAAAA,0x555555,0x5555FF,0x55FF55,0x55FFFF,0xFF5555,0xFF55FF,0xFFFF55,0xFFFFFF};
int desktop_render_cells(struct ag_device *dev,struct ag_surface *s,const uint16_t *cells,int cols,int rows){
 if(!s||!s->pixels||!cells||cols<=0||rows<=0||cols>s->width||rows>s->height)return -1;
 int cw=s->width/cols,ch=s->height/rows;if(cw<5||ch<7)return -1;
 for(int r=0;r<rows;++r){int y=r*ch,y1=r==rows-1?s->height:y+ch;
  for(int c=0;c<cols;++c){uint16_t e=cells[r*cols+c];unsigned char glyph=(unsigned char)e,attr=(unsigned char)(e>>8);
   uint32_t fg=colors[attr&15],bg=colors[(attr>>4)&7];int x=c*cw,x1=c==cols-1?s->width:x+cw,w=x1-x,h=y1-y;
   ag_fill_rect(dev,s,x,y,w,h,bg);
   if(glyph==0xDB){ag_fill_rect(dev,s,x,y,w,h,fg);continue;}
   if(glyph==0xB0||glyph==' ')continue;
   const uint8_t *bits=font5x7_glyph((char)glyph);int sx=w/5,sy=h/7,scale=sx<sy?sx:sy;if(scale>2)scale=2;if(scale<1)continue;
   int ox=x+(w-5*scale)/2,oy=y+(h-7*scale)/2;
   for(int gy=0;gy<7;++gy)for(int gx=0;gx<5;++gx)
    if(bits[gy]&(1u<<(4-gx)))ag_fill_rect(dev,s,ox+gx*scale,oy+gy*scale,scale,scale,fg);
  }
 }
 return 0;
}
