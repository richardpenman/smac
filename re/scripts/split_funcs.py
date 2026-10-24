#!/usr/bin/env python3
"""Split re/out/functions.jsonl into per-function files and build re/out/index.tsv."""
import json, os, re, bisect
RE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mods = []
for l in open(os.path.join(RE, 'modules.tsv')):
    if l.startswith('#') or not l.strip(): continue
    s, e, m, k = l.split('\t')[:4]
    mods.append((int(s, 16), int(e, 16), m, k))
starts = [m[0] for m in mods]
syms = {}
for l in open(os.path.join(RE, 'out/symbols.tsv')):
    p = l.rstrip('\n').split('\t')
    syms[int(p[0], 16)] = p
fs = [json.loads(l) for l in open(os.path.join(RE, 'out/functions.jsonl'))]
byaddr = {int(f['addr'], 16): f for f in fs}
outdir = os.path.join(RE, 'out/funcs')
idx = open(os.path.join(RE, 'out/index.tsv'), 'w')
idx.write('addr\tname\tsize\tmodule\tkind\tnamed\tncallers\tncallees\tfile\n')
for a, f in sorted(byaddr.items()):
    if f['name'].startswith(('Unwind@', 'Catch@', 'Catch_All@')): continue
    i = bisect.bisect_right(starts, a) - 1
    mod, kind = (mods[i][2], mods[i][3]) if i >= 0 and a < mods[i][1] else ('misc', 'sys')
    name = f['name']
    d = os.path.join(outdir, mod); os.makedirs(d, exist_ok=True)
    fn = os.path.join(d, f"{a:06x}_{re.sub(r'[^A-Za-z0-9_]', '_', name)[:80]}.c")
    def nm(c):
        try: return byaddr[int(c, 16)]['name']
        except (ValueError, KeyError): return c
    callees = [nm(c) for c in f['callees']]
    callers = [nm(c) for c in f['callers']]
    with open(fn, 'w') as o:
        o.write(f"// {a:08X} {name}  size={f['size']} cc={f['cc']}\n")
        if a in syms and syms[a][4:] and syms[a][4]:
            o.write(f"// note: {syms[a][4]}\n")
        o.write(f"// callers: {', '.join(callers)}\n// callees: {', '.join(callees)}\n")
        o.write(f['c'])
    idx.write(f"{a:08X}\t{name}\t{f['size']}\t{mod}\t{kind}\t{1 if a in syms else 0}\t{len(callers)}\t{len(callees)}\t{os.path.relpath(fn, RE)}\n")
idx.close()
