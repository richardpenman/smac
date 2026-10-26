#!/bin/bash
# The original and smacx side by side for the placeholder sweep's windows (the map's right-click
# menu so far).
#
#   tests/placeholders/compare.sh OUTDIR [ORIG_DIR]
#
# ORIG_DIR: PNGs of the original from tests/popups/orig_drive.py with orig_rmenu.steps (taken now
# when omitted: Wine, Xvfb, tools/venv). The original's quick start picks a random faction, so the
# map and the menu's items differ; the menu's frame, rows, fonts and colours are compared. Writes
# OUTDIR/cmp_<name>.png (original above, smacx in the middle, differing pixels below) and prints
# the matching fraction.
set -u
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
ORIG="${2:-$OUT/orig}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/ph-native/smacx}")"
mkdir -p "$OUT/smacx"
if [ ! -f "$ORIG/rmenu.png" ]; then
    "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/placeholders/orig_rmenu.steps" "$ORIG"
fi
(cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --hidden --new size=0 --human 1 --seed 1,2 \
    --outdir "$OUT/smacx" --keys "ENTER,S-RCLICK:512x300,SHOT:rmenu.bmp,ESC" > "$OUT/smacx.log" 2>&1)
for p in "rmenu 513 301 745 700"; do
    set -- $p
    printf '%-16s ' "$1"
    "$PY" "$ROOT/tests/popups/cmp.py" "$ORIG/$1.png" "$OUT/smacx/$1.bmp" "$OUT/cmp_$1.png" $2 $3 $4 $5 2>/dev/null
done
