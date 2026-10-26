#!/usr/bin/env python3
"""Original-function name table: re/original_names.tsv.

Every function declared with ADDR / ADDR_NATIVE keeps its original address while its source name
may change (cleanup phase, docs/CLEANUP_PLAN.md). The table records, per address, the name of the
original function in the decompilation (re/out/index.tsv: a symbol, or FUN_00xxxxxx for functions
the symbol maps do not name) and the current source name, so docs, harness logs and Ghidra output
stay connected to the source after renames.

  names.py update   regenerate re/original_names.tsv from src/ (original names: kept from the
                    table, else looked up in re/out/index.tsv for new addresses)
  names.py check    exit 1 unless the table matches src/: no declared address lost or added,
                    every source name current, and every harness/oracle/fcompare_table.h entry
                    naming a declared function
  names.py lookup NAME|ADDR   print the matching rows (source or original name, or hex address)

Columns: addr, original (index.tsv name), source (current name), kind (ADDR | ADDR_NATIVE),
header (path under src/).
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TABLE = os.path.join(ROOT, "re", "original_names.tsv")
INDEX = os.path.join(ROOT, "re", "out", "index.tsv")
FCOMPARE = os.path.join(ROOT, "harness", "oracle", "fcompare_table.h")
# Same declaration syntax as gen_bindings.py.
DECL = re.compile(
    r"ADDR(_NATIVE)?\(\s*(0x[0-9A-Fa-f]+)\s*\)\s*"
    r"((?:[\w:<>]+[\s\*&]+)+?)(GAMEAPI\s+|THISCALL\s+)?(\w+)\s*\(", re.S)
HEADER = "addr\toriginal\tsource\tkind\theader"


def scan_src():
    """{addr: (source name, kind, header)} of every ADDR declaration under src/."""
    decls = {}
    src = os.path.join(ROOT, "src")
    for root, _, files in os.walk(src):
        for f in sorted(files):
            if not f.endswith(".h"):
                continue
            path = os.path.join(root, f)
            text = open(path, encoding="utf-8", errors="replace").read()
            for m in DECL.finditer(text):
                addr = int(m.group(2), 16)
                kind = "ADDR_NATIVE" if m.group(1) else "ADDR"
                rel = os.path.relpath(path, src).replace(os.sep, "/")
                if addr in decls and decls[addr][0] != m.group(5):
                    sys.exit(f"names.py: 0x{addr:06X} declared twice: {decls[addr][0]} and {m.group(5)}")
                decls[addr] = (m.group(5), kind, rel)
    return decls


def read_table():
    rows = {}
    if not os.path.exists(TABLE):
        return rows
    for line in open(TABLE, encoding="utf-8").read().splitlines()[1:]:
        addr, orig, source, kind, header = line.split("\t")
        rows[int(addr, 16)] = (orig, source, kind, header)
    return rows


def read_index():
    names = {}
    if os.path.exists(INDEX):
        for line in open(INDEX, encoding="utf-8").read().splitlines()[1:]:
            p = line.split("\t")
            names[int(p[0], 16)] = p[1]
    return names


def build(decls, old):
    index = None
    rows = {}
    for addr, (source, kind, header) in decls.items():
        if addr in old:
            orig = old[addr][0]
        else:
            if index is None:
                index = read_index()
            orig = index.get(addr, "?")  # "?": not a function start in the index (data, CRT thunk)
        rows[addr] = (orig, source, kind, header)
    return rows


def format_table(rows):
    lines = [HEADER] + [f"{a:08X}\t" + "\t".join(r) for a, r in sorted(rows.items())]
    return "\n".join(lines) + "\n"


def fcompare_names():
    if not os.path.exists(FCOMPARE):
        return []
    text = re.sub(r"//[^\n]*", "", open(FCOMPARE, encoding="utf-8").read())
    return re.findall(r"^\s*\{\s*\"(\w+)\"", text, re.M)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "check"
    decls = scan_src()
    old = read_table()
    if cmd == "update":
        rows = build(decls, old)
        open(TABLE, "w", encoding="utf-8", newline="\n").write(format_table(rows))
        renamed = [(a, old[a][1], rows[a][1]) for a in rows if a in old and old[a][1] != rows[a][1]]
        for a, o, n in renamed:
            print(f"renamed 0x{a:06X}: {o} -> {n}")
        print(f"names.py: {len(rows)} functions, {len(set(rows) - set(old))} added, "
              f"{len(set(old) - set(rows))} removed, {len(renamed)} renamed")
        return 0
    if cmd == "check":
        errors = []
        for a in sorted(set(old) - set(decls)):
            errors.append(f"0x{a:06X} {old[a][1]} (original {old[a][0]}): in the table but no longer declared")
        for a in sorted(set(decls) - set(old)):
            errors.append(f"0x{a:06X} {decls[a][0]}: declared but not in the table")
        for a in sorted(set(decls) & set(old)):
            if decls[a][0] != old[a][1]:
                errors.append(f"0x{a:06X}: source name {decls[a][0]}, table says {old[a][1]}")
            elif decls[a][1] != old[a][2]:
                errors.append(f"0x{a:06X} {decls[a][0]}: kind {decls[a][1]}, table says {old[a][2]}")
        declared = {d[0] for d in decls.values()}
        for n in fcompare_names():
            if n not in declared:
                errors.append(f"fcompare_table.h: {n} is not a declared function")
        for e in errors:
            print("names.py: " + e)
        if errors:
            print("names.py: FAIL (after an intended rename or new declaration: src/tools/names.py update)")
            return 1
        print(f"names.py: OK ({len(decls)} functions)")
        return 0
    if cmd == "lookup":
        key = sys.argv[2]
        rows = build(decls, old)
        addr = int(key, 16) if re.fullmatch(r"(0x)?[0-9A-Fa-f]{6,8}", key) else None
        for a, r in sorted(rows.items()):
            if a == addr or key in (r[0], r[1]):
                print(f"{a:08X}\t" + "\t".join(r))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
