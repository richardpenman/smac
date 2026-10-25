#!/usr/bin/env python3
# crop.py ORACLE.smxbase SMACX.smxbase CANVAS(0|1) x0 y0 x1 y1 OUT.png [SCALE]: one region of a
# canvas from both dumps (oracle above, smacx, differing pixels in white) and its match share.
import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from cmp import read, img
from PIL import Image
pa, ca = read(sys.argv[1]); pb, cb = read(sys.argv[2])
k = int(sys.argv[3]); x0, y0, x1, y1 = map(int, sys.argv[4:8])
A = img(pa, ca[k]).crop((x0, y0, x1, y1)); B = img(pb, cb[k]).crop((x0, y0, x1, y1))
w, h = A.size
a = ca[k]; b = cb[k]
same = sum(1 for y in range(y0, y1) for x in range(x0, x1) if a[2][y * a[0] + x] == b[2][y * b[0] + x])
print('match %.4f' % (same / (w * h)))
D = Image.new('RGB', (w, h))
for y in range(y0, y1):
    for x in range(x0, x1):
        if a[2][y * a[0] + x] != b[2][y * b[0] + x]: D.putpixel((x - x0, y - y0), (255, 255, 255))
out = Image.new('RGB', (w, h * 3)); out.paste(A, (0, 0)); out.paste(B, (0, h)); out.paste(D, (0, 2 * h))
s = int(sys.argv[9]) if len(sys.argv) > 9 else 2
out.resize((w * s, h * 3 * s), Image.NEAREST).save(sys.argv[8])
