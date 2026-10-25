#!/usr/bin/env python3
"""Compare two snapshot directories (oracle vs. reconstruction, or two oracle runs).

Each directory holds, per snapshot:
  turn_NNN.state  game-state memory regions (see harness/state_regions.h)
  turn_NNN.rng    RNG / logic clock state
  turn_NNN.sav    the game's own save file (compared only with --sav, by section with
                  scripts/savdiff.py: uninitialised header, heap pointers and UI state masked)

For every common snapshot the RNG state and each state region are compared; differences are
reported as named struct fields (e.g. Bases[3].mineral_surplus) using the layouts in
src/engine/types. By default reporting stops at the first divergent snapshot.

Usage: compare.py <dirA> <dirB> [--all] [--max N] [--sav]
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "scripts"))
import savdiff  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---------------------------------------------------------------- region list
def load_regions():
    regions, masks = {}, []
    for line in open(os.path.join(ROOT, "harness/state_regions.h")):
        m = re.match(r"\s*REGION(?:_VAR)?\((\w+),\s*(0x[0-9A-Fa-f]+)", line)
        if m:
            regions[int(m.group(2), 16)] = m.group(1)
        m = re.match(r"\s*MASK\((0x[0-9A-Fa-f]+),\s*(\w+)\)", line)
        if m:
            masks.append((int(m.group(1), 16), int(m.group(2), 0)))
    return regions, masks


# ---------------------------------------------------------------- struct layouts
PRIM = {"int8_t": 1, "uint8_t": 1, "char": 1, "byte": 1, "bool": 1, "int16_t": 2, "uint16_t": 2,
        "int32_t": 4, "uint32_t": 4, "int": 4, "float": 4, "unsigned": 4, "int64_t": 8, "uint64_t": 8,
        "double": 8}


def parse_structs():
    text = ""
    tdir = os.path.join(ROOT, "src/engine/types")
    for f in sorted(os.listdir(tdir)):
        text += open(os.path.join(tdir, f), encoding="utf-8", errors="replace").read() + "\n"
    text = re.sub(r"//[^\n]*|/\*.*?\*/", "", text, flags=re.S)
    enums = set(re.findall(r"enum\s+(\w+)", text))
    structs = {}
    for m in re.finditer(r"\bstruct\s+(\w+)\s*(?::\s*(\w+))?\s*\{(.*?)\n\};", text, re.S):
        name, base, body = m.group(1), m.group(2), m.group(3)
        fields, off = [], 0
        if base and base in structs:
            fields = list(structs[base][0])
            off = structs[base][1]
        ok = True
        for decl in body.split(";"):
            decl = " ".join(decl.split())
            if not decl or "(" in decl or decl.startswith(("union", "struct {")):
                if decl.startswith("union"):
                    ok = False
                continue
            dm = re.match(r"(?:const\s+)?([\w:]+)\s*(\*?)\s*(\w+)((?:\[[^\]]+\])*)$", decl)
            if not dm:
                continue
            ty, ptr, fname, dims = dm.groups()
            if ptr:
                size = 4
            elif ty in PRIM:
                size = PRIM[ty]
            elif ty in structs:
                size = structs[ty][1]
            elif ty in enums:
                size = 4
            else:
                ok = False
                break
            count = 1
            dimlist = []
            for d in re.findall(r"\[([^\]]+)\]", dims):
                try:
                    n = int(eval(d, {}, {}))
                except Exception:
                    ok = False
                    n = 1
                dimlist.append(n)
                count *= n
            align = min(size, 4) if not dimlist and ty not in structs else 1
            if align > 1:
                off = (off + align - 1) // align * align
            fields.append((off, fname, size, dimlist, ty if ty in structs else None))
            off += size * count
        if ok:
            structs[name] = (fields, (off + 3) // 4 * 4 if off % 4 else off)
    return structs


REGION_TYPES = {"Factions": "Faction", "MFactions": "MFaction", "Bases": "BASE", "Vehs": "VEH",
                "Units": "UNIT", "MapTiles": "MAP", "Continents": "Continent", "GamePrefs": "GamePrefs",
                "Monuments": "Monument"}


def field_name(structs, ty, off):
    if ty not in structs:
        return f"+0x{off:x}"
    fields, _ = structs[ty]
    for foff, fname, size, dims, sub in reversed(fields):
        total = size
        for d in dims:
            total *= d
        if foff <= off < foff + total:
            rel = off - foff
            idx = rel // size
            s = fname
            if dims:
                sub_idx = []
                for d in reversed(dims):
                    sub_idx.append(idx % d)
                    idx //= d
                s += "".join(f"[{i}]" for i in reversed(sub_idx))
            if sub:
                s += "." + field_name(structs, sub, rel % size)
            elif rel % size:
                s += f"+{rel % size}"
            return s
    return f"+0x{off:x}"


def describe(structs, region, off):
    ty = REGION_TYPES.get(region)
    if ty and ty in structs:
        size = structs[ty][1]
        if region == "GamePrefs":
            return f"{region}.{field_name(structs, ty, off)}"
        return f"{region}[{off // size}].{field_name(structs, ty, off % size)}"
    return f"{region}+0x{off:x}"


# ---------------------------------------------------------------- snapshots
def load_state(path):
    d = open(path, "rb").read()
    out, i = {}, 0
    while i + 8 <= len(d):
        addr, size = struct.unpack_from("<II", d, i)
        out[addr] = d[i + 8:i + 8 + size]
        i += 8 + size
    return out


def load_rng(path):
    d = {}
    for line in open(path):
        k, v = line.split()
        d[k] = int(v)
    return d


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    show_all = "--all" in sys.argv
    maxn = 30
    if "--max" in sys.argv:
        maxn = int(sys.argv[sys.argv.index("--max") + 1])
    a, b = args[0], args[1]
    regions, masks = load_regions()
    structs = parse_structs()
    names = lambda d: {f[:-6] for f in os.listdir(d) if f.endswith(".state")}
    # order snapshots chronologically: turn_T sorts before step_T_*
    def key(n):
        m = re.match(r"(turn|step)_(\d+)(?:_(\d+)_(\w+))?", n)
        if not m:
            if n == "game_end":  # the final snapshot (end of game) sorts last
                return (1 << 30, 0, 0, n)
            return (-1, 0, 0, n)  # other names (e.g. "boot") sort first, alphabetically
        return (int(m.group(2)), 0 if m.group(1) == "turn" else 1, int(m.group(3) or 0), m.group(4) or "")
    snaps = sorted(names(a) & names(b), key=key)
    if not snaps:
        print("no common snapshots")
        return 2
    all_ok = True
    for s in snaps:
        ra, rb = load_rng(f"{a}/{s}.rng"), load_rng(f"{b}/{s}.rng")
        rng_diff = {k: (ra.get(k), rb.get(k)) for k in sorted(set(ra) | set(rb)) if ra.get(k) != rb.get(k)}
        sa, sb = load_state(f"{a}/{s}.state"), load_state(f"{b}/{s}.state")
        diffs = []
        for addr in sorted(set(sa) | set(sb)):
            name = regions.get(addr, f"0x{addr:x}")
            xa, xb = sa.get(addr), sb.get(addr)
            if xa is None or xb is None:
                diffs.append(f"{name}: missing in {'A' if xa is None else 'B'}")
                continue
            if len(xa) != len(xb):
                diffs.append(f"{name}: size {len(xa)} vs {len(xb)}")
            last = None
            for i in range(min(len(xa), len(xb))):
                if xa[i] != xb[i] and not any(m <= addr + i < m + n for m, n in masks):
                    desc = describe(structs, name, i)
                    if desc != last:
                        diffs.append(f"{desc}: {xa[i]:#04x} vs {xb[i]:#04x}")
                        last = desc
        if "--sav" in sys.argv and os.path.exists(f"{a}/{s}.sav") and os.path.exists(f"{b}/{s}.sav"):
            fa, fb = open(f"{a}/{s}.sav", "rb").read(), open(f"{b}/{s}.sav", "rb").read()
            try:  # section-level comparison (masks the expected differences, see scripts/savdiff.py)
                lines, sav_ok = savdiff.compare(fa, fb)
                if not sav_ok:
                    diffs += ["save file: " + line for line in lines if "(expected)" not in line]
            except ValueError:
                nd = sum(1 for i in range(min(len(fa), len(fb))) if fa[i] != fb[i]) + abs(len(fa) - len(fb))
                if nd:
                    diffs.append(f"save file: {nd} bytes differ")
        ok = not rng_diff and not diffs
        print(f"{s}: {'OK' if ok else 'DIFF'}" + (f"  rng {rng_diff}" if rng_diff else "") +
              (f"  {len(diffs)} field diffs" if diffs else ""))
        if not ok:
            for d in diffs[:maxn]:
                print("    " + d)
            if len(diffs) > maxn:
                print(f"    ... {len(diffs) - maxn} more")
            all_ok = False
            if not show_all:
                break
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
