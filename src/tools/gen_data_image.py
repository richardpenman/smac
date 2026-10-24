#!/usr/bin/env python3
"""Extract the initial contents of terranx.exe's writable data region (0x682000-0x9CA000) into a
compact binary image used by native builds to initialise the engine's memory image.

Output format (little endian): repeated records {u32 addr, u32 len, bytes[len]}; regions not
listed are zero (BSS). Usage: gen_data_image.py <terranx.exe> <out.bin>
"""
import struct
import sys

BASE, END = 0x682000, 0x9CA000


def main():
    exe, out = sys.argv[1], sys.argv[2]
    d = open(exe, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    image_base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    sec = pe + 24 + optsz
    recs = []
    for i in range(nsec):
        name, vsize, va, rsize, rptr = struct.unpack_from("<8sIIII", d, sec + i * 40)
        lo = image_base + va
        if lo < BASE or lo >= END:
            continue
        raw = d[rptr:rptr + min(rsize, vsize)].rstrip(b"\0")
        # split into non-zero runs separated by >= 64 zero bytes to keep the image small
        i0 = 0
        while i0 < len(raw):
            while i0 < len(raw) and raw[i0] == 0:
                i0 += 1
            j = i0
            zeros = 0
            while j < len(raw):
                zeros = zeros + 1 if raw[j] == 0 else 0
                if zeros >= 64:
                    break
                j += 1
            chunk = raw[i0:j - zeros + 1 if zeros else j]
            if chunk:
                recs.append((lo + i0, chunk))
            i0 = j + 1
    with open(out, "wb") as f:
        for a, c in recs:
            f.write(struct.pack("<II", a, len(c)))
            f.write(c)
    print(f"{len(recs)} records, {sum(len(c) for _, c in recs)} bytes")


if __name__ == "__main__":
    main()
