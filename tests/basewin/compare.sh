#!/bin/bash
# The base window of the original (oracle basedump) and of smacx (--basedump), compared pixel by
# pixel.
#
#   tests/basewin/compare.sh OUTDIR [SAVE:BASE ...]     (default: turn_100 bases 0 1 5 30, turn_147 base 2)
#
# Env: SMACX (default build/bw-native/smacx); HYBRID_BUILD (oracle.dll, default build/hybrid).
# For each save the oracle (harness/run_oracle.sh, one Wine process) loads it and dumps the base
# windows (harness/README.md "Base window dumps"): the 8-bit canvases of BaseWin and of the main
# interface (MainInfc), and the screen (FILE.bmp). smacx --basedump writes the same canvases for
# the same base with the oracle's tile array address (terrain variants). tests/basewin/cmp.py
# prints the share of identical pixels per canvas and writes OUTDIR/cmp_<save>_<base>.png (oracle |
# smacx | differing pixels). The buttons are child windows, not part of the canvases: compare the
# screens (OUTDIR/<save>/bN.smxbase.bmp and OUTDIR/smacx_<save>_N.smxbase.bmp) by eye.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"; shift
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/bw-native/smacx}")"
CASES=("${@:-turn_100:0 turn_100:1 turn_100:5 turn_100:30 turn_147:2}")
CASES=(${CASES[@]})
mkdir -p "$OUT"
for save in $(printf '%s\n' "${CASES[@]}" | cut -d: -f1 | sort -u); do
    bases=$(printf '%s\n' "${CASES[@]}" | grep "^$save:" | cut -d: -f2)
    if [ ! -f "$OUT/$save/oracle.log" ]; then
        {
            echo "load $ROOT/work/saves_late/$save.sav"
            for b in $bases; do echo "basedump b$b.smxbase base=$b"; done
            echo "turns 0"
        } > "$OUT/$save.cmd"
        "$ROOT/harness/run_oracle.sh" "$OUT/$save.cmd" "$OUT/$save" 300 > /dev/null 2>&1
    fi
    for b in $bases; do
        tiles=$(grep -o "basedump b$b.smxbase base=$b viewer=[0-9]* tiles=[0-9a-f]*" "$OUT/$save/oracle.log" |
            sed 's/.*tiles=//')
        if [ -z "$tiles" ]; then echo "$save base $b: no oracle dump"; continue; fi
        (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --hidden --save "work/saves_late/$save.sav" \
            --basedump "$b:$OUT/smacx_${save}_$b.smxbase:$tiles" > "$OUT/smacx_${save}_$b.log" 2>&1)
        echo "$save base $b:"
        "$PY" "$ROOT/tests/basewin/cmp.py" "$OUT/$save/b$b.smxbase" "$OUT/smacx_${save}_$b.smxbase" \
            "$OUT/cmp_${save}_$b.png" | sed 's/^/  /'
    done
done
