#!/bin/bash
# Screenshots of the same popups in the original and in smacx, compared side by side.
#
#   tests/popups/compare.sh OUTDIR [ORIG_DIR]
#
# ORIG_DIR: PNGs of the original from tests/popups/orig_drive.py with orig_quickstart.steps (taken
# now when omitted: needs Wine, Xvfb and python-xlib/mss/pillow in tools/venv). The original's
# quick start picks a random faction, so map, names and the base name differ; the popups' frames,
# boxes, positions and colours are what is compared. Writes OUTDIR/cmp_<popup>.png (original
# above, smacx in the middle, differing pixels below) and prints the matching fraction per popup.
set -u
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
ORIG="${2:-$OUT/orig}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/pp-native/smacx}")"
mkdir -p "$OUT/smacx"
if [ ! -f "$ORIG/reallyover.png" ]; then
    "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/popups/orig_quickstart.steps" "$ORIG"
fi
(cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --original-bugs --hidden --new size=0 --human 1 --seed 1,2 \
    --outdir "$OUT/smacx" --keys "SHOT:firstbase.bmp,ENTER,C-ENTER,SHOT:reallyover.bmp,ESC,C-q,SHOT:reallyquit.bmp,DOWN,SHOT:reallyquit_down.bmp,UP,ENTER" \
    > "$OUT/smacx.log" 2>&1)
# Region of each popup (the main interface's frame); the console panel below differs (MainInterface
# is a placeholder in smacx).
for p in "firstbase 230 250 790 545" "reallyover 230 250 790 545" "reallyquit 150 250 870 545" \
         "reallyquit_down 150 250 870 545"; do
    set -- $p
    printf '%-16s ' "$1"
    "$PY" "$ROOT/tests/popups/cmp.py" "$ORIG/$1.png" "$OUT/smacx/$1.bmp" "$OUT/cmp_$1.png" $2 $3 $4 $5 2>/dev/null
done
