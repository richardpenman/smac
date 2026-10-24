#!/usr/bin/env python3
"""One-off: draft src/engine/globals.h from Thinker's engine.cpp address table (MIT).
Only plain-data globals are emitted; UI object globals are left for the UI reconstruction."""
import re, sys
src = open(sys.argv[1], encoding='latin-1').read().split('\n')
out = []
seen = set()
simple = re.compile(r'^\s*(int|uint8_t|char|int16_t|uint32_t|int8_t)\*\s+const\s+(\w+)\s*=\s*\(\w+\*\)\s*\(?0x([0-9A-Fa-f]+)\)?;\s*(?://\s*(.*))?$')
arr2 = re.compile(r'^\s*(int|char|int16_t|uint8_t)\s*\(\*const\s+(\w+)\)((?:\[\d+\])+)\s*=.*?0x([0-9A-Fa-f]+)\)?;\s*(?://\s*(.*))?$')
structp = re.compile(r'^\s*([A-Z]\w*)\*\s*(?:const\s+)?(\w+)\s*=\s*\(\s*\w+\*\s*\)\s*0x([0-9A-Fa-f]+);\s*(?://\s*(.*))?$')
T = {'int': 'int32_t', 'char': 'char', 'int16_t': 'int16_t', 'uint8_t': 'uint8_t', 'uint32_t': 'uint32_t', 'int8_t': 'int8_t'}
STRUCTS = {'MFaction', 'Faction', 'BASE', 'UNIT', 'VEH', 'Landmark', 'Continent', 'CTerraform', 'CResourceInfo',
           'CCitizen', 'CResourceName', 'CCombatMode', 'CBonusName', 'CRules', 'CTech', 'CFacility', 'CAbility',
           'CChassis', 'CArmor', 'CReactor', 'CWeapon', 'CMorale', 'COrder', 'CTimeControl', 'CSocialCategory',
           'CSocialEffect', 'CSocialParam', 'CSocialField', 'CMandate', 'CMight', 'CProposal', 'CNatural',
           'CWorldbuilder', 'CEnergy', 'GamePrefs', 'Goal', 'Monument', 'ReplayEvent', 'GameChecksum', 'Path'}
for l in src:
    m = simple.match(l)
    if m:
        t, n, a, c = m.group(1), m.group(2), int(m.group(3), 16), (m.group(4) or '').strip()
        dims = re.match(r'\[(\d+)\](?:\[(\d+)\])?', c)
        ty = T[t]
        if dims:
            ty += ''.join(f'[{d}]' for d in dims.groups() if d)
        elif t == 'char':
            ty = 'char[256]'
        if a >= 0x682000 and n not in seen:
            seen.add(n); out.append((a, f'GLOBAL({ty}, {n}, 0x{a:06X});' + (f'  // {c}' if c else '')))
        continue
    m = arr2.match(l)
    if m:
        t, n, d, a, c = m.group(1), m.group(2), m.group(3), int(m.group(4), 16), (m.group(5) or '').strip()
        full = re.match(r'((?:\[\d+\])+)', c)
        dims = full.group(1) if full else '[?]' + d
        if n not in seen:
            seen.add(n); out.append((a, f'GLOBAL({T[t]}{dims}, {n}, 0x{a:06X});' + (f'  // {c}' if c else '')))
        continue
    m = structp.match(l)
    if m and m.group(1) in STRUCTS:
        t, n, a, c = m.group(1), m.group(2), int(m.group(3), 16), (m.group(4) or '').strip()
        dims = re.match(r'\[(\d+)\]', c)
        ty = f'{t}[{dims.group(1)}]' if dims else f'{t}[?]'
        if n not in seen:
            seen.add(n); out.append((a, f'GLOBAL({ty}, {n}, 0x{a:06X});' + (f'  // {c}' if c else '')))
for a, l in sorted(out):
    print(l)
