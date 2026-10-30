#!/usr/bin/env python3
# cmp.py ORACLE.smxbase OPENSMAC.smxbase OUT.png: compares the base window canvases of the oracle's
# basedump and opensmac --basedump / BASEDUMP: (index exact). Prints the share of identical pixels of
# each canvas and per pane rectangle; writes oracle | OpenSMAC | difference for both canvases.
import sys, struct
from PIL import Image

def read(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'SMXBASE1'
    pal = d[8:776]; off = 776; cs = []
    for _ in range(2):
        w, h = struct.unpack_from('<ii', d, off); off += 8
        cs.append((w, h, d[off:off + w * h])); off += w * h
    return pal, cs

def img(pal, c):
    w, h, px = c
    im = Image.frombytes('P', (w, h), px); im.putpalette(pal); return im.convert('RGB')

def main():
  pa, ca = read(sys.argv[1]); pb, cb = read(sys.argv[2])
  rows = []
  for name, a, b in (('BaseWin', ca[0], cb[0]), ('MainInfc', ca[1], cb[1])):
      w, h = a[0], a[1]
      if (w, h) != (b[0], b[1]):
          print(f'{name}: size differs {w}x{h} vs {b[0]}x{b[1]}'); continue
      same = sum(1 for i in range(w * h) if a[2][i] == b[2][i])
      print(f'{name} {w}x{h}: match {same / (w * h):.4f}')
      diff = Image.new('RGB', (w, h))
      dp = diff.load()
      for i in range(w * h):
          if a[2][i] != b[2][i]: dp[i % w, i // w] = (255, 255, 255)
      rows.append((img(pa, a), img(pb, b), diff))
  if len(sys.argv) > 3 and rows:
      W = max(r[0].width for r in rows) * 3; H = sum(r[0].height for r in rows)
      out = Image.new('RGB', (W, H), (255, 0, 255)); y = 0
      for r in rows:
          for i, im in enumerate(r): out.paste(im, (i * im.width, y))
          y += r[0].height
      out.save(sys.argv[3])

if __name__ == '__main__':
    main()
