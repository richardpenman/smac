#!/usr/bin/env python3
# basedump.py FILE.smxbase OUT.png: the oracle's base window dump (harness/README.md "Base window
# dumps") as one picture: the BaseWin canvas above the main interface's canvas, placed as on a
# 1024x768 screen (BaseWin at ((1024 - 726) / 2, 768 - 213 - height), MainInfc at the bottom).
import sys, struct
from PIL import Image

def read(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'SMXBASE1'
    pal = d[8:8 + 768]
    off = 8 + 768
    canv = []
    for _ in range(2):
        w, h = struct.unpack_from('<ii', d, off); off += 8
        canv.append((w, h, d[off:off + w * h])); off += w * h
    return pal, canv

def image(pal, c):
    w, h, px = c
    im = Image.frombytes('P', (w, h), px)
    im.putpalette(pal)
    return im.convert('RGB')

def screen(path, sw=1024, sh=768):
    pal, (bw, mi) = read(path)
    scr = Image.new('RGB', (sw, sh), (0, 0, 0))
    mih = mi[1]
    scr.paste(image(pal, mi), (0, sh - mih))
    scr.paste(image(pal, bw), ((sw - 0x2d6) // 2, sh - 0xd5 - bw[1]))
    return scr

if __name__ == '__main__':
    screen(sys.argv[1]).save(sys.argv[2])
