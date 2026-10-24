#!/usr/bin/env python3
"""Build a name map for terranx.exe (v2.0 GOG) from the Thinker and OpenSMACX references.

Output: re/out/symbols.tsv  (addr \t kind \t name \t source \t comment)
kind = func | data
"""
import re, sys, os, glob

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEXT_LO, TEXT_HI = 0x401000, 0x664000 + 0x415d

syms = {}  # addr -> (kind, name, source, comment)

def add(addr, name, source, comment=""):
    kind = "func" if TEXT_LO <= addr < TEXT_HI else "data"
    if addr in syms:
        k, n, s, c = syms[addr]
        # prefer real names over placeholders like dword_XXXX / UNK
        if re.match(r"^(dword|byte|word|sub|unk)_", n, re.I) or "UNK" in n:
            syms[addr] = (kind, name, source, comment or c)
        return
    syms[addr] = (kind, name, source, comment)

# Thinker: engine.cpp style `Type Name = (Type)0xADDR;  // comment`
pat = re.compile(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*=\s*\(\s*[^;=]*?\)\s*\(?\s*0x([0-9A-Fa-f]{5,8})\s*\)?\s*;\s*(?://\s*(.*))?$")
for f in glob.glob(os.path.join(ROOT, "ref/thinker/src/*.cpp")) + glob.glob(os.path.join(ROOT, "ref/thinker/src/*.h")):
    for line in open(f, encoding="latin-1"):
        m = pat.search(line.strip())
        if m:
            add(int(m.group(2), 16), m.group(1), "thinker", (m.group(3) or "").strip())

# OpenSMACX: comment blocks with "Original Offset: XXXXXXXX" followed by a function definition
fdef = re.compile(r"^[A-Za-z_][\w\s\*&:<>,]*?\b(?:__cdecl\s+|__stdcall\s+|__thiscall\s+)?([A-Za-z_][\w:]*)\s*\(")
for f in glob.glob(os.path.join(ROOT, "ref/OpenSMACX/src/*.cpp")):
    lines = open(f, encoding="latin-1").read().split("\n")
    for i, line in enumerate(lines):
        m = re.search(r"Original Offset:\s*([0-9A-Fa-f]{6,8})", line)
        if not m:
            continue
        purpose = ""
        for j in range(max(0, i - 4), i):
            if "Purpose:" in lines[j]:
                purpose = lines[j].split("Purpose:", 1)[1].strip()
        for j in range(i + 1, min(len(lines), i + 12)):
            mm = fdef.match(lines[j])
            if mm and not lines[j].strip().startswith(("Purpose", "Return", "Status", "Original")):
                name = mm.group(1).replace("::", "_")
                add(int(m.group(1), 16), name, "opensmacx", purpose)
                break

out = os.path.join(ROOT, "re/out/symbols.tsv")
with open(out, "w") as fo:
    for a in sorted(syms):
        k, n, s, c = syms[a]
        fo.write(f"{a:08X}\t{k}\t{n}\t{s}\t{c}\n")
kinds = {}
for v in syms.values():
    kinds[(v[0], v[2])] = kinds.get((v[0], v[2]), 0) + 1
print(len(syms), kinds)
