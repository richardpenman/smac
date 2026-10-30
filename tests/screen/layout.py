#!/usr/bin/env python3
"""Layout facts of OpenSMAC screenshots (24-bit BMPs, tests/screen/run.sh); no third-party modules.

  layout.py strips SHOT.bmp            the strips beside the console (x outside the centred 1024
                                       columns, the bottom 213 rows): "strips W H nonblack N of T"
  layout.py console SHOT.bmp REF.bmp   the console's columns at the bottom of SHOT against those of
                                       REF (1024 x 768), the world map pane left out: "console match F"
  layout.py stretch SHOT.bmp REF.bmp X0 the opening picture: SHOT's columns left of X0 against REF
                                       (1024 x 768) stretched to SHOT's size, the pixel under each
                                       pixel's centre (as change_opening's Buffer_copy_3): "stretch match F"
  layout.py scale2 SHOT.png            (a PNG of the display, tests/screen/xshot.py) every 2 x 2
                                       block of one colour: "scale2 blocks N of T"
"""
import struct
import sys


def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    if bpp != 24:
        raise SystemExit('%s: %d bits per pixel' % (path, bpp))
    stride = (w * 3 + 3) & ~3
    flip = h > 0
    h = abs(h)
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        rows.append(d[off + sy * stride: off + sy * stride + w * 3])
    return w, h, rows


def px(row, x):
    return row[3 * x: 3 * x + 3]


def strips(path):
    w, h, rows = read_bmp(path)
    x0 = (w - 1024) // 2
    n = t = 0
    for y in range(h - 213, h):
        r = rows[y]
        for x in list(range(0, x0)) + list(range(x0 + 1024, w)):
            t += 1
            if px(r, x) != b'\0\0\0':
                n += 1
    print('strips %d %d nonblack %d of %d' % (w, h, n, t))


def console(path, ref):
    w, h, rows = read_bmp(path)
    rw, rh, rrows = read_bmp(ref)
    x0 = (w - 1024) // 2
    same = tot = 0
    for dy in range(1, 214):
        a, b = rows[h - dy], rrows[rh - dy]
        for x in range(0, 0x30B):  # left of the world map pane (whose view box follows the view's size)
            tot += 1
            if px(a, x0 + x) == px(b, x):
                same += 1
    print('console match %.3f' % (same / tot))


def stretch(path, ref, x_end):
    w, h, rows = read_bmp(path)
    rw, rh, rrows = read_bmp(ref)
    x_end = int(x_end)
    same = tot = 0
    sx, sy = (rw << 16) // w, (rh << 16) // h  # the source pixel under each pixel's centre, 16.16
    for y in range(0, h, 3):
        a, b = rows[y], rrows[min((y * sy + sy // 2) >> 16, rh - 1)]
        for x in range(0, min(x_end, w), 3):
            tot += 1
            if px(a, x) == px(b, min((x * sx + sx // 2) >> 16, rw - 1)):
                same += 1
    print('stretch match %.3f' % (same / tot))


def scale2(path):
    import zlib
    d = open(path, 'rb').read()
    # PNG (8-bit RGB / RGBA, as mss writes it): the IHDR, the IDAT stream, the rows unfiltered.
    p = 8
    idat = b''
    while p < len(d):
        n, kind = struct.unpack_from('>I4s', d, p)
        body = d[p + 8: p + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype = struct.unpack_from('>IIBB', body)
        elif kind == b'IDAT':
            idat += body
        p += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[i]
        line = bytearray(raw[i + 1: i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    n = t = 0
    for y in range(0, h - 1, 2 * 7):  # every 7th block row and column
        a, b = rows[y], rows[y + 1]
        for x in range(0, w - 1, 2 * 7):
            t += 1
            q = [a[bpp * x: bpp * x + 3], a[bpp * (x + 1): bpp * (x + 1) + 3], b[bpp * x: bpp * x + 3],
                 b[bpp * (x + 1): bpp * (x + 1) + 3]]
            if q.count(q[0]) == 4:
                n += 1
    print('scale2 blocks %d of %d' % (n, t))


if __name__ == '__main__':
    {'strips': strips, 'console': console, 'stretch': stretch, 'scale2': scale2}[sys.argv[1]](*sys.argv[2:])
