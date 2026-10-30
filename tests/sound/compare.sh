#!/bin/bash
# The original's sound calls in an all-AI game, logged under Wine by oracle.dll ("logsound": hooks
# on FX_play, FX_play_loc, FX_fade, FX_stop, wave_it, ambience, ambience_veh, boom, the movies,
# load_music, popup_wave_callback and Wave_play_2 of terranx.exe, harness/oracle/oracle.cpp), compared
# with OpenSMAC's (OPENSMAC_SOUNDLOG=1) for the same game; with --update the normalized log replaces
# tests/sound/orig_tiny20.log (the reference of tests/sound/run.sh).
#
#   tests/sound/compare.sh OUTDIR [--update]      (HYBRID_BUILD: the dir with oracle.dll,
#                                                   OPENSMAC: OpenSMAC; needs Wine and Xvfb)
set -u
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
OPENSMAC="$(realpath "${OPENSMAC:-$ROOT/build/p10-native/opensmac}")"
mkdir -p "$OUT"
printf 'seed 1001 2001\nnewgame size=0 diff=3\nlogsound 0\nturns 20\n' > "$OUT/sound20.cmd"
"$ROOT/harness/run_oracle.sh" "$OUT/sound20.cmd" "$OUT/oracle" 900 > "$OUT/oracle_run.log" 2>&1
(cd "$ROOT" && SDL_VIDEODRIVER=dummy OPENSMAC_SOUNDLOG=1 timeout 900 "$OPENSMAC" --original-bugs --hidden --nosound --outdir "$OUT/opensmac" \
    --new size=0 diff=3 --seed 1001,2001 --human 0 --turns 20 --snapshots "$OUT/opensmac/snap" > "$OUT/opensmac.log" 2>&1)
python3 "$ROOT/harness/compare.py" "$OUT/oracle" "$OUT/opensmac/snap" | tail -1
python3 "$ROOT/tests/sound/soundcmp.py" "$OUT/oracle/oracle.log" "$OUT/opensmac.log" --one-window
RC=$?
if [ "${2:-}" = "--update" ]; then
    python3 "$ROOT/tests/sound/soundcmp.py" --normalize "$OUT/oracle/oracle.log" > "$ROOT/tests/sound/orig_tiny20.log"
fi
exit $RC
