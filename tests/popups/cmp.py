#!/usr/bin/env python3
# cmp.py ORIG SMACX OUT.png x0 y0 x1 y1 [SCALE]: the region of both screenshots stacked (original,
# smacx, then the pixels differing by more than 24 in any channel in white); prints the fraction of
# matching pixels.
import sys
from PIL import Image, ImageChops
a=Image.open(sys.argv[1]).convert('RGB'); b=Image.open(sys.argv[2]).convert('RGB')
x0,y0,x1,y1=map(int,sys.argv[4:8])
ca=a.crop((x0,y0,x1,y1)); cb=b.crop((x0,y0,x1,y1))
d=ImageChops.difference(ca,cb).convert('L').point(lambda v:255 if v>24 else 0)
w,h=ca.size
tot=w*h; same=sum(1 for v in (d.get_flattened_data() if hasattr(d,'get_flattened_data') else d.getdata()) if v==0)
print('match %.3f (%d/%d)'%(same/tot,same,tot))
out=Image.new('RGB',(w,h*3),(255,0,255))
out.paste(ca,(0,0)); out.paste(cb,(0,h)); out.paste(d.convert('RGB'),(0,2*h))
s=int(sys.argv[8]) if len(sys.argv)>8 else 1
if s>1: out=out.resize((w*s,h*3*s),Image.NEAREST)
out.save(sys.argv[3])
