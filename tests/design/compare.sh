#!/bin/bash
# Screenshots of the design workshop, the datalinks and the research priorities in the original and
# in smacx, compared side by side (as tests/reports/compare.sh).
#
#   tests/design/compare.sh OUTDIR [ORIG_DIR]
#
# ORIG_DIR: PNGs of the original from tests/popups/orig_drive.py with orig_design.steps and
# tests/reports/orig_extra.ini (Difficulty=2), taken now when omitted (needs Wine, Xvfb and
# tools/venv). The original's QUICK START picks a random faction and map: the frames, boxes,
# buttons, positions and colours are what is compared, the designs and texts differ.
# Writes OUTDIR/cmp_<screen>.png (original above, smacx in the middle, differing pixels below) and
# prints the matching fraction per screen.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
ORIG="${2:-$OUT/orig}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/p7-native/smacx}")"
mkdir -p "$OUT/smacx"
if [ ! -f "$ORIG/design.png" ]; then
    "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/design/orig_design.steps" "$ORIG" \
        "$ROOT/tests/reports/orig_extra.ini"
fi
(cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --hidden --new size=0 --human 1 --seed 1,2 \
    --outdir "$OUT/smacx" --keys "ENTER,SHOT:main.bmp,u,SHOT:design.bmp,ESC,F1,SHOT:datalink.bmp,ESC,R,\
SHOT:priorities.bmp,ESC" > "$OUT/smacx.log" 2>&1)
# Regions: the design workshop with the main interface (its window above the interface); the
# datalinks with the main interface; the research priorities popup.
for p in "design 200 160 840 768" "datalink 100 20 924 768" "priorities 140 240 910 560"; do
    set -- $p
    printf '%-11s ' "$1"
    "$PY" "$ROOT/tests/popups/cmp.py" "$ORIG/$1.png" "$OUT/smacx/$1.bmp" "$OUT/cmp_$1.png" $2 $3 $4 $5 2>/dev/null ||
        echo "(missing)"
done
