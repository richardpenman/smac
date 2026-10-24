#!/usr/bin/env python3
"""One-off: port Thinker's engine struct/enum headers (MIT) into src/engine/types/*.h.
Strips inline helper methods (they depend on Thinker globals) and Thinker-specific savegame fields."""
import re, os, sys
SRC = sys.argv[1]; DST = sys.argv[2]
os.makedirs(DST, exist_ok=True)
HDR = ("// Struct and enum layouts of terranx.exe v2.0 game data.\n"
       "// Ported from the Thinker mod (https://github.com/induktio/thinker, MIT License,\n"
       "// Copyright (c) Thinker Mod authors); helper methods removed, layouts unchanged.\n"
       "#pragma once\n#include <cstdint>\n#include <cstddef>\n\n")
for fn in ['engine_enums.h', 'engine_types.h', 'engine_base.h', 'engine_veh.h', 'engine_data.h']:
    lines = open(os.path.join(SRC, fn), encoding='latin-1').read().split('\n')
    out = []; i = 0; depth = 0; skip_depth = None
    while i < len(lines):
        l = lines[i]
        s = l.strip()
        if skip_depth is not None:
            depth += l.count('{') - l.count('}')
            if depth <= skip_depth: skip_depth = None
            i += 1; continue
        if s.startswith('#pragma once') or s.startswith('#include'):
            i += 1; continue
        # method definition inside a struct: "<type> name(...) [const] {"
        if depth > 0 and re.match(r'^(?:static\s+|inline\s+|constexpr\s+)?[\w:<>\*&\s]+?\b\w+\s*\([^;]*\)\s*(const)?\s*\{', s) and not s.startswith(('struct', 'union', 'enum')):
            skip_depth = depth
            depth += l.count('{') - l.count('}')
            if depth <= skip_depth: skip_depth = None
            i += 1; continue
        depth += l.count('{') - l.count('}')
        out.append(l)
        i += 1
    txt = '\n'.join(out)
    txt = re.sub(r'\n\s*\n\s*\n+', '\n\n', txt)
    open(os.path.join(DST, fn.replace('engine_', '')), 'w').write(HDR + txt + '\n')
    print(fn, len(lines), '->', len(txt.split('\n')))
