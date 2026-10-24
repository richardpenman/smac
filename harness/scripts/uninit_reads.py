#!/usr/bin/env python3
"""Find reads of uninitialised stack memory in an oracle single-step trace (`steptrace`).

Usage: tools/venv/bin/python harness/scripts/uninit_reads.py TRACE.trc [--all] [--max N]

A trace holds the register state before every executed terranx.exe instruction of one enemy_veh
call. Each instruction is decoded (capstone) to compute its stack memory accesses (explicit
operands, push/pop/call/ret/leave, string instructions). A read of a stack byte is reported when
that byte was released (ESP moved above it) after its last write, i.e. the value is a leftover of
an earlier, already returned frame ("stale"), or when it was never written during the call
("pre-call": poison or older contents). For stale reads the writing instruction is reported.
"""
import bisect
import os
import struct
import sys
from collections import defaultdict

import numpy as np
import pefile
import capstone
from capstone import x86_const as X

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def load_image():
    pe = pefile.PE(os.path.join(ROOT, "game/terranx.exe"))
    base = pe.OPTIONAL_HEADER.ImageBase
    img = bytearray(pe.OPTIONAL_HEADER.SizeOfImage)
    for s in pe.sections:
        d = s.get_data()
        img[s.VirtualAddress:s.VirtualAddress + len(d)] = d
    return base, bytes(img)


def load_funcs():
    addrs, names = [], []
    for line in open(os.path.join(ROOT, "re/out/index.tsv")):
        f = line.split("\t")
        if f[0] == "addr":
            continue
        addrs.append(int(f[0], 16))
        names.append(f[1])
        KIND[f[1]] = f[4]
    return addrs, names


KIND = {}


REGS = {X.X86_REG_EAX: 2, X.X86_REG_ECX: 3, X.X86_REG_EDX: 4, X.X86_REG_EBX: 5, X.X86_REG_ESP: 6,
        X.X86_REG_EBP: 7, X.X86_REG_ESI: 8, X.X86_REG_EDI: 9}
SUB = {X.X86_REG_AX: (2, 0xFFFF), X.X86_REG_CX: (3, 0xFFFF), X.X86_REG_DX: (4, 0xFFFF), X.X86_REG_BX: (5, 0xFFFF),
       X.X86_REG_SI: (8, 0xFFFF), X.X86_REG_DI: (9, 0xFFFF), X.X86_REG_AL: (2, 0xFF), X.X86_REG_CL: (3, 0xFF),
       X.X86_REG_DL: (4, 0xFF), X.X86_REG_BL: (5, 0xFF)}
STRING_OPS = {0xA4: ("movs", 1), 0xA5: ("movs", 4), 0xAA: ("stos", 1), 0xAB: ("stos", 4), 0xAC: ("lods", 1),
              0xAD: ("lods", 4), 0xA6: ("cmps", 1), 0xA7: ("cmps", 4), 0xAE: ("scas", 1), 0xAF: ("scas", 4)}


def reg_val(r, reg):
    if reg in REGS:
        return int(r[REGS[reg]])
    if reg in SUB:
        i, m = SUB[reg]
        return int(r[i]) & m
    return None


def main():
    args = sys.argv[1:]
    show_all = "--all" in args
    maxn = 20
    if "--max" in args:
        maxn = int(args[args.index("--max") + 1])
    path = [a for a in args if a.endswith(".trc")][0]
    recs = np.fromfile(path, dtype="<u4").reshape(-1, 10)
    base, img = load_image()
    faddrs, fnames = load_funcs()

    def fname(a):
        i = bisect.bisect_right(faddrs, a) - 1
        return f"{fnames[i]}+{a - faddrs[i]:X}" if i >= 0 else f"{a:08X}"

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    cache = {}

    def insn(eip):
        i = cache.get(eip)
        if i is None:
            off = eip - base
            i = next(md.disasm(img[off:off + 16], eip))
            cache[eip] = i
        return i

    ext = recs[:, 1] == 0xFFFFFFFF
    esps = recs[:, 6].astype(np.int64)
    entry_esp = int(esps[0])
    lo = int(esps.min()) - 0x2000
    hi = entry_esp
    n = hi - lo
    last_w = np.full(n, -1, dtype=np.int64)
    w_eip = np.zeros(n, dtype=np.uint32)
    w_chain = np.zeros(n, dtype=np.int32)
    chains = [()]  # call chains (tuples of call-site eips); index 0 = top level
    chain_ids = {(): 0}
    cur = [()]  # current chain as list of (callsite, esp after call)
    cur_id = [0]

    def set_chain(c):
        t = tuple(x[0] for x in c)
        cid = chain_ids.get(t)
        if cid is None:
            cid = chain_ids[t] = len(chains)
            chains.append(t)
        cur_id[0] = cid
    w_val = {}
    dealloc = np.full(n, -1, dtype=np.int64)
    ext_dirty = np.full(n, -1, dtype=np.int64)
    found = defaultdict(list)

    def write(step, addr, size, eip, val=None):
        a, b = max(addr - lo, 0), min(addr + size - lo, n)
        if a < b:
            last_w[a:b] = step
            w_eip[a:b] = eip
            w_chain[a:b] = cur_id[0]
            if val is not None and size == 4 and a == addr - lo:
                w_val[addr] = (step, val)

    def read(step, addr, size, eip, r):
        a, b = max(addr - lo, 0), min(addr + size - lo, n)
        if a >= b:
            return
        lw, da = last_w[a:b], dealloc[a:b]
        bad = (lw < da) | (lw < 0)
        if not bad.any():
            return
        if 0x645550 <= eip < 0x645580:  # _chkstk probes
            return
        kind = "stale" if (lw >= 0).any() else "pre-call"
        wi = int(w_eip[a]) if lw[0] >= 0 else 0
        v = w_val.get(addr)
        val = v[1] if v and v[0] == lw[0] else None
        found[eip].append((step, addr, size, kind, int(lw.max()), wi, val, int(da.max()), int(ext_dirty[a:b].max()),
                           addr - int(r[7]), addr - int(r[6]), cur_id[0], int(w_chain[a]) if lw[0] >= 0 else -1))

    N = len(recs)
    stack = []
    for k in range(N):
        r = recs[k]
        e = int(r[6])
        if stack and e > stack[-1][1]:
            while stack and e > stack[-1][1]:
                stack.pop()
            set_chain(stack)
        if ext[k]:
            esp = int(r[6])
            a, b = max(esp - 0x4000 - lo, 0), max(esp - lo, 0)
            ext_dirty[a:b] = k
            if k + 1 < N and int(recs[k + 1][6]) > esp:
                dealloc[max(esp - lo, 0):max(min(int(recs[k + 1][6]) - lo, n), 0)] = k
            continue
        eip = int(r[0])
        i = insn(eip)
        esp = int(r[6])
        mnem = i.mnemonic
        op0 = i.bytes[0]
        prefixes = [p for p in i.prefix if p]
        sop = None
        for bb in i.bytes:
            if bb in (0xF3, 0xF2, 0x66, 0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65):
                continue
            sop = STRING_OPS.get(bb)
            break
        if sop:
            kind_s, sz = sop
            if 0x66 in i.bytes[:2] and sz == 4:
                sz = 2
            rep = 0xF3 in prefixes or 0xF2 in prefixes
            cnt = 1
            if rep:
                cnt = int(r[3]) - int(recs[k + 1][3]) if k + 1 < N else int(r[3])
                cnt = max(cnt, 0)
            d = -1 if int(r[1]) & 0x400 else 1
            esi, edi = int(r[8]), int(r[9])
            for j in range(min(cnt, 1 << 16)):
                if kind_s in ("movs", "lods", "cmps"):
                    read(k, esi + d * j * sz, sz, eip, r)
                if kind_s in ("cmps", "scas"):
                    read(k, edi + d * j * sz, sz, eip, r)
                if kind_s in ("movs", "stos"):
                    write(k, edi + d * j * sz, sz, eip, int(r[2]) if kind_s == "stos" and sz == 4 else None)
        else:
            if mnem != "lea" and not mnem.startswith("nop"):
                for o in i.operands:
                    if o.type != X.X86_OP_MEM or o.mem.segment in (X.X86_REG_FS, X.X86_REG_GS):
                        continue
                    addr = o.mem.disp
                    if o.mem.base:
                        addr += reg_val(r, o.mem.base)
                    if o.mem.index:
                        addr += reg_val(r, o.mem.index) * o.mem.scale
                    addr &= 0xFFFFFFFF
                    if addr >= 0x80000000:
                        addr -= 1 << 32
                    acc = o.access or capstone.CS_AC_READ
                    if acc & capstone.CS_AC_READ:
                        read(k, addr, o.size, eip, r)
                    if acc & capstone.CS_AC_WRITE:
                        val = None
                        if mnem == "mov" and len(i.operands) == 2:
                            s = i.operands[1]
                            val = s.imm & 0xFFFFFFFF if s.type == X.X86_OP_IMM else (
                                reg_val(r, s.reg) if s.type == X.X86_OP_REG else None)
                        write(k, addr, o.size, eip, val)
            if mnem == "push":
                o = i.operands[0]
                val = o.imm & 0xFFFFFFFF if o.type == X.X86_OP_IMM else (
                    reg_val(r, o.reg) if o.type == X.X86_OP_REG else None)
                write(k, esp - 4, 4, eip, val)
            elif mnem in ("pushal", "pushad"):
                write(k, esp - 32, 32, eip)
            elif mnem in ("pushfd", "pushfl", "pushf"):
                write(k, esp - 4, 4, eip, int(r[1]))
            elif mnem == "call":
                write(k, esp - 4, 4, eip, (eip + i.size) & 0xFFFFFFFF)
                if k + 1 < N and int(recs[k + 1][6]) == esp - 4:
                    stack.append((eip, esp - 4))
                    set_chain(stack)
            elif mnem in ("pop", "popfd", "popf", "popfl", "ret", "retn"):
                read(k, esp, 4, eip, r)
            elif mnem in ("popal", "popad"):
                read(k, esp, 32, eip, r)
            elif mnem == "leave":
                read(k, int(r[7]), 4, eip, r)
        if k + 1 < N:
            e1 = int(recs[k + 1][6])
            if e1 > esp:
                dealloc[max(esp - lo, 0):max(min(e1 - lo, n), 0)] = k

    def rel(rc, wc):
        a, b = chains[rc], chains[wc] if wc >= 0 else ()
        c = 0
        while c < min(len(a), len(b)) and a[c] == b[c]:
            c += 1
        anc = fname(a[c - 1]) if c else "(root)"
        return anc, " > ".join(fname(x) for x in a[c:]), " > ".join(fname(x) for x in b[c:])

    if "--summary" in args:
        engine_only = "--engine" in args
        for eip in sorted(found):
            fn = fname(eip).split("+")[0]
            if engine_only and KIND.get(fn) != "engine":
                continue
            groups = defaultdict(lambda: [0, set()])
            for x in found[eip]:
                key = (x[3], x[5], x[9]) + rel(x[11], x[12])
                groups[key][0] += 1
                groups[key][1].add("?" if x[6] is None else f"{x[6]:X}")
            for (kind, wi, rebp, anc, rchain, wchain), (cnt, vals) in groups.items():
                print(f"{os.path.basename(path)}\t{eip:08X}\t{fname(eip)}\tebp{rebp:+#x}\t{kind}\tx{cnt}\t"
                      f"writer {wi:08X} {fname(wi) if wi else '-'}\tvalues {','.join(sorted(vals))}\t"
                      f"below {anc}: reader {rchain} | writer {wchain}")
        return
    print(f"{path}: {N} records, entry esp {entry_esp:08X}, {len(cache)} distinct instructions")
    print(f"{len(found)} instructions read released/unwritten stack memory")
    for eip in sorted(found, key=lambda e: found[e][0][0]):
        L = found[eip]
        i = insn(eip)
        kinds = sorted({x[3] for x in L})
        print(f"\n{eip:08X} {fname(eip):32s} {i.mnemonic} {i.op_str}   x{len(L)} {kinds}")
        for (step, addr, size, kind, lw, wi, val, da, xd, rel_ebp, rel_esp, rc, wc) in (L if show_all else L[:maxn]):
            wdesc = f"written step {lw} by {wi:08X} {fname(wi)}" if lw >= 0 else "never written in trace"
            vdesc = f" value {val:08X}" if val is not None else ""
            xdesc = " (an external call may have written it since)" if xd >= 0 and xd > lw else ""
            print(f"   step {step:8d} [{addr & 0xFFFFFFFF:08X}] ebp{rel_ebp:+#x} esp{rel_esp:+#x} size {size} {kind}: "
                  f"{wdesc}{vdesc}, released step {da}{xdesc}")
            if wc >= 0:
                a, b = chains[rc], chains[wc]
                c = 0
                while c < min(len(a), len(b)) and a[c] == b[c]:
                    c += 1
                anc = fname(a[c - 1]) if c else "(trace root)"
                print(f"      below {anc}: reader via " + " > ".join(fname(x) for x in a[c:]) +
                      " | writer via " + " > ".join(fname(x) for x in b[c:]))


if __name__ == "__main__":
    main()
