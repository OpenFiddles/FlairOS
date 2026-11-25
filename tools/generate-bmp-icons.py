#!/usr/bin/env python3
"""Generate the built-in FlairOS Material 3 icon pack as 24-bit BMP files."""
from pathlib import Path
import struct

OUT = Path(__file__).resolve().parents[1] / "resources/images/icons"
S = 4
W = H = 64
BG = (43, 41, 48)
WHITE = (230, 224, 233)
MUTED = (202, 196, 208)
VIOLET = (208, 188, 255)
TEAL = (110, 220, 200)
BLUE = (145, 190, 240)
ORANGE = (255, 183, 145)
GREEN = (166, 220, 180)

def canvas():
    return [[BG for _ in range(W*S)] for _ in range(H*S)]

def rect(p, x0, y0, x1, y1, color):
    x0,y0,x1,y1 = [int(v*S) for v in (x0,y0,x1,y1)]
    for y in range(max(0,y0), min(H*S,y1)):
        row=p[y]
        for x in range(max(0,x0), min(W*S,x1)):
            row[x]=color

def roundrect(p, x0,y0,x1,y1,r,color):
    x0,y0,x1,y1,r=[int(v*S) for v in (x0,y0,x1,y1,r)]
    for y in range(max(0,y0),min(H*S,y1)):
        for x in range(max(0,x0),min(W*S,x1)):
            cx=min(max(x,x0+r),x1-r-1)
            cy=min(max(y,y0+r),y1-r-1)
            if (x-cx)**2+(y-cy)**2 <= r*r:
                p[y][x]=color

def circle(p,cx,cy,r,color,fill=True,thick=3):
    cx,cy,r,thick=[int(v*S) for v in (cx,cy,r,thick)]
    for y in range(max(0,cy-r),min(H*S,cy+r+1)):
        for x in range(max(0,cx-r),min(W*S,cx+r+1)):
            d=(x-cx)**2+(y-cy)**2
            if d<=r*r and (fill or d>=(r-thick)**2):
                p[y][x]=color

def line(p,x0,y0,x1,y1,color,width=3):
    x0,y0,x1,y1,width=[int(v*S) for v in (x0,y0,x1,y1,width)]
    dx=abs(x1-x0); sx=1 if x0<x1 else -1
    dy=-abs(y1-y0); sy=1 if y0<y1 else -1
    err=dx+dy
    while True:
        rad=max(1,width//2)
        for yy in range(y0-rad,y0+rad+1):
            for xx in range(x0-rad,x0+rad+1):
                if 0<=xx<W*S and 0<=yy<H*S and (xx-x0)**2+(yy-y0)**2<=rad*rad:
                    p[yy][xx]=color
        if x0==x1 and y0==y1: break
        e2=2*err
        if e2>=dy: err+=dy; x0+=sx
        if e2<=dx: err+=dx; y0+=sy

def downsample(p):
    out=[]
    for y in range(H):
        row=[]
        for x in range(W):
            pts=[p[y*S+dy][x*S+dx] for dy in range(S) for dx in range(S)]
            row.append(tuple(sum(c[i] for c in pts)//(S*S) for i in range(3)))
        out.append(row)
    return out

def save(name,p):
    rows=downsample(p)
    stride=(W*3+3)&~3
    pixels=bytearray()
    for row in reversed(rows):
        for r,g,b in row: pixels.extend((b,g,r))
        pixels.extend(b'\0'*(stride-W*3))
    size=54+len(pixels)
    header=b'BM'+struct.pack('<IHHI',size,0,0,54)
    dib=struct.pack('<IiiHHIIiiII',40,W,H,1,24,0,len(pixels),2835,2835,0,0)
    (OUT/(name+'.BMP')).write_bytes(header+dib+pixels)

def build(name,kind):
    p=canvas()
    if kind=='home':
        roundrect(p,14,29,51,49,7,VIOLET); roundrect(p,17,23,36,33,4,VIOLET)
        rect(p,20,36,45,40,WHITE)
    elif kind=='clock':
        circle(p,32,32,22,VIOLET,False,4)
        line(p,32,13,32,32,WHITE,4); line(p,32,32,46,32,WHITE,4)
        circle(p,32,32,3,TEAL)
    elif kind=='terminal':
        roundrect(p,9,15,55,49,8,(59,56,66))
        line(p,19,25,27,32,VIOLET,4); line(p,27,32,19,39,VIOLET,4)
        line(p,33,40,45,40,MUTED,3)
    elif kind=='folder':
        roundrect(p,11,25,53,49,7,BLUE); roundrect(p,14,18,34,30,5,BLUE)
        rect(p,17,34,47,38,(214,232,250))
    elif kind=='settings':
        circle(p,32,32,21,VIOLET,False,7)
        for a in range(8):
            import math
            ang=a*math.pi/4
            x0=32+18*math.cos(ang); y0=32+18*math.sin(ang)
            x1=32+25*math.cos(ang); y1=32+25*math.sin(ang)
            line(p,x0,y0,x1,y1,VIOLET,5)
        circle(p,32,32,8,BG)
        circle(p,32,32,4,TEAL)
    elif kind=='battery':
        roundrect(p,10,21,51,44,6,WHITE)
        rect(p,51,28,55,37,WHITE)
        roundrect(p,15,26,27,39,4,TEAL)
        line(p,35,25,29,34,ORANGE,4); line(p,29,34,37,34,ORANGE,4); line(p,37,34,32,41,ORANGE,4)
    elif kind=='document':
        roundrect(p,16,10,48,54,5,WHITE)
        rect(p,23,23,41,26,VIOLET); rect(p,23,31,41,34,MUTED); rect(p,23,39,37,42,MUTED)
    elif kind=='image':
        roundrect(p,10,15,54,49,7,BLUE)
        circle(p,23,26,5,ORANGE)
        line(p,15,43,27,32,GREEN,5); line(p,27,32,36,40,GREEN,5); line(p,36,40,45,29,GREEN,5)
    save(name,p)

for name,kind in [
    ('home','home'),('clock','clock'),('terminal','terminal'),('folder','folder'),
    ('settings','settings'),('battery','battery'),('document','document'),('image','image')
]:
    build(name,kind)
print('Generated 8 anti-aliased 64x64 24-bit BMP icons in', OUT)
