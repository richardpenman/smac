#!/bin/bash
# Screenshots of the social engineering window and the F-key screens in the original and in smacx,
# compared side by side (as tests/popups/compare.sh).
#
#   tests/reports/compare.sh OUTDIR [ORIG_DIR]
#
# ORIG_DIR: PNGs of the original from tests/popups/orig_drive.py with orig_reports.steps and
# orig_extra.ini (Difficulty=2: no tutorial windows), taken now when omitted (needs Wine, Xvfb and
# tools/venv). The original's QUICK START picks a random faction and map: the frames, boxes,
# buttons, positions and colours are what is compared, the faction's names and numbers differ.
# Writes OUTDIR/cmp_<screen>.png (original above, smacx in the middle, differing pixels below) and
# prints the matching fraction per screen.
set -u
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
ORIG="${2:-$OUT/orig}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/rp-native/smacx}")"
mkdir -p "$OUT/smacx"
if [ ! -f "$ORIG/social.png" ]; then
    "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/reports/orig_reports.steps" "$ORIG" \
        "$ROOT/tests/reports/orig_extra.ini"
fi
(cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --original-bugs --hidden --new size=0 --human 1 --seed 1,2 \
    --outdir "$OUT/smacx" --keys "ENTER,SHOT:main.bmp,e,SHOT:social.bmp,ESC,F4,SHOT:ops.bmp,F7,SHOT:security.bmp,\
F8,SHOT:score.bmp,F5,SHOT:projects.bmp,F2,SHOT:labs.bmp,F3,SHOT:energy.bmp,F6,SHOT:sat.bmp,ESC,F12,SHOT:commlink.bmp,F12,\
F10,SHOT:fame.bmp,ESC" > "$OUT/smacx.log" 2>&1)
# Regions: the social engineering window with the main interface; the report window with the main
# interface; the commlink panel and the hall of fame (their whole screens).
for p in "social 100 200 950 768" "ops 160 0 864 768" "security 160 0 864 768" "score 160 0 864 768" \
         "projects 160 0 864 768" "labs 160 0 864 768" "energy 160 0 864 768" "sat 160 0 864 768" \
         "commlink 0 0 1024 768" "fame 0 0 1024 768"; do
    set -- $p
    printf '%-10s ' "$1"
    "$PY" "$ROOT/tests/popups/cmp.py" "$ORIG/$1.png" "$OUT/smacx/$1.bmp" "$OUT/cmp_$1.png" $2 $3 $4 $5 2>/dev/null ||
        echo "(missing)"
done
