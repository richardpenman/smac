#!/bin/bash
# Screenshots of the opening screens in the original and in smacx, compared (as
# tests/reports/compare.sh): the main menu, the scenario menu, the load FileBox, the map menu, the
# planet's size, ocean, erosion, native life and clouds, the difficulty, the rules, the faction
# picker, the leader's name, PLANETFALL, then in the game the preferences and the save FileBox.
#
#   tests/menu/compare.sh OUTDIR [ORIG_DIR]
#
# ORIG_DIR: PNGs of the original from tests/popups/orig_drive.py with orig_menu.steps and
# tests/reports/orig_extra.ini (Difficulty=2), taken now when omitted (needs Wine, Xvfb and
# tools/venv). Writes OUTDIR/cmp_<screen>.png (original, smacx, differing pixels) and prints the
# matching fraction per screen. The credits (animated) and the multiplayer setup (a placeholder)
# are taken but not scored.
set -u
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
ORIG="${2:-$OUT/orig}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/p9-native/smacx}")"
rm -rf "$OUT/smacx"
mkdir -p "$OUT/smacx"
if [ ! -f "$ORIG/topmenu.png" ]; then
    "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/menu/orig_menu.steps" "$ORIG" \
        "$ROOT/tests/reports/orig_extra.ini"
fi
cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" "$ROOT/tests/reports/orig_extra.ini" > "$OUT/menu.ini"
(cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --original-bugs --hidden --seed 1,2 --ini "$OUT/menu.ini" \
    --outdir "$OUT/smacx" --keys "SHOT:topmenu.bmp,DOWN,SHOT:topmenu_down.bmp,DOWN,ENTER,SHOT:scenariomenu.bmp,\
ESC,DOWN,ENTER,SHOT:loadgame.bmp,ESC,DOWN,ENTER,SHOT:multimenu.bmp,ESC,DOWN,ENTER,SHOT:credits.bmp,ESC,\
UP,UP,UP,UP,UP,ENTER,SHOT:mapmenu.bmp,DOWN,ENTER,SHOT:worldsize.bmp,ENTER,SHOT:worldland.bmp,ENTER,\
SHOT:worldtides.bmp,ENTER,SHOT:worldlife.bmp,ENTER,SHOT:worldclouds.bmp,ENTER,SHOT:difficulty.bmp,ENTER,\
SHOT:userules.bmp,ENTER,SHOT:pickwin.bmp,DOWN,DOWN,ENTER,SHOT:name.bmp,ENTER,SHOT:planetfall.bmp,ENTER,\
ENTER,C-p,SHOT:prefs.bmp,ESC,C-s,SHOT:savegame.bmp,ESC" > "$OUT/smacx.log" 2>&1)
for p in "topmenu 0 0 1024 768" "topmenu_down 0 0 1024 768" "scenariomenu 0 0 1024 768" \
         "loadgame 340 140 700 560" "mapmenu 0 0 1024 768" "worldsize 0 0 1024 768" \
         "worldland 0 0 1024 768" "worldtides 0 0 1024 768" "worldlife 0 0 1024 768" \
         "worldclouds 0 0 1024 768" "difficulty 0 0 1024 768" "userules 0 0 1024 768" \
         "pickwin 0 0 800 520" "name 0 0 1024 768" "planetfall 300 560 1024 768" \
         "prefs 180 145 895 540" "savegame 325 150 730 560"; do
    set -- $p
    printf '%-13s ' "$1"
    "$PY" "$ROOT/tests/popups/cmp.py" "$ORIG/$1.png" "$OUT/smacx/$1.bmp" "$OUT/cmp_$1.png" $2 $3 $4 $5 2>/dev/null ||
        echo "(missing)"
done
