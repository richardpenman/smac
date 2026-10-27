#!/bin/bash
# Scripted checks of the sound layer (src/ui/sound.cpp, music.cpp, mp3.cpp, movie.cpp): which FX,
# voices, ambience, music and movie calls the game makes (SMACX_SOUNDLOG=1), the volume page of the
# preferences, the MP3 decoder, the WVE movie player and the mixer's output.
#
#   tests/sound/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/p10-native/smacx)
#
# 1. observer: an all-AI game (tiny20: size 0, difficulty 3, seeds 1001,2001, 20 turns) logs the
#    same sound calls in the same order as the original under the oracle (orig_tiny20.log, made by
#    tests/sound/compare.sh: oracle.dll's "logsound" hooks FX_play, FX_play_loc, FX_fade, FX_stop,
#    wave_it, ambience, ambience_veh, boom, the movies, load_music, popup_wave_callback and
#    Wave_play_2 in terranx.exe), the explosion sounds of the original's other map windows aside
#    (soundcmp.py --one-window).
# 2. human: the triggers of scripted actions, as the original's code makes them (sound.cpp):
#    FX_init and load_music at the start, a unit's sound when it is selected (ambience_veh, FX by
#    chassis), the base window's sounds (fx\menu2.wav, ambience 0xCD / 0xCE, fx\menu down.wav).
# 3. menu: the opening menu's music and the menu column's sound.
# 4. prefs: the audio page's buttons and sliders (state, the music switch, the volumes saved).
# 5. voice: an MP3 voice decoded and played (24.3 s), and the mixer's output not silent (SDL's disk
#    audio driver).
# 6. music: the default music set rendered for 30 s (MUSICRENDER).
# 7. movie: a secret project movie (WVE): 100 frames decoded, the 100th a picture.
# Runs 1-4 use --nosound (the same calls as with a device: every call is logged before the device
# is used). Exit status 0 when every check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/p10-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/sound_test}")"
PY="$ROOT/tools/venv/bin/python"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE ARGS... : log in OUT/CASE.log
    local name="$1"; shift
    rm -rf "$OUT/$name"
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy SMACX_SOUNDLOG=1 timeout 900 "$SMACX" --hidden --outdir "$OUT/$name" "$@" \
        > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
check() {  # check CASE DESCRIPTION REGEX: the log matches (grep -E)
    if grep -qE -- "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
inorder() {  # inorder CASE DESCRIPTION LINE...: the sound log has these lines in this order
    local name="$1" desc="$2"; shift 2
    if python3 - "$OUT/$name.log" "$@" <<'EOF'
import sys
lines = [l.split(" ", 2)[2].strip() for l in open(sys.argv[1], errors="replace") if l.startswith("sound: ")]
i = 0
for want in sys.argv[2:]:
    while i < len(lines) and lines[i] != want:
        i += 1
    if i == len(lines):
        print("missing: " + want)
        sys.exit(1)
    i += 1
EOF
    then echo "PASS $name: $desc"; else echo "FAIL $name: $desc"; FAIL=1; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -q "is not reconstructed yet\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}

# 1. The observer game against the original's calls.
# (Snapshots as the oracle writes them: save_game reads the logic clock, so they are part of the game.)
run observer --nosound --new size=0 diff=3 --seed 1001,2001 --human 0 --turns 20 --snapshots "$OUT/observer/snap"
if python3 "$ROOT/tests/sound/soundcmp.py" "$ROOT/tests/sound/orig_tiny20.log" "$OUT/observer.log" --one-window \
    > "$OUT/observer.cmp"; then
    echo "PASS observer: the original's sound calls ($(head -1 "$OUT/observer.cmp"))"
else
    echo "FAIL observer: sound calls differ (see $OUT/observer.cmp)"; FAIL=1
fi
nocrash observer

# 2. A human's first turn (faction 1, the Cyborgs: aset1): the unit sounds and the base window.
run human --nosound --new size=0 --human 1 --seed 1,2 --keys "ENTER,h,/,BASE:0,ESC"
inorder human "startup: FX_init, the music set" "FX_init" "load_music 1 CYBORG aset1.amb"
inorder human "the selected units' sounds (infantry 0x40, sea 0x45)" "ambience_veh 4" "FX_play 64" \
    "Wave_play_2 infantry.wav" "ambience_veh 5" "FX_play 69" "Wave_play_2 military ship.wav"
inorder human "base window: open and close sounds" "FX_play 15" "Wave_play_2 menu2.wav" "ambience 205" \
    "ambience 206" "Wave_play_2 menu down.wav"
nocrash human

# 3. The main menu.
run menu --nosound --seed 1001,2001 --keys "MENUSTATE"
inorder menu "the opening menu's music and the column's sound" "FX_init" "Wave_play_2 opening menu.wav" \
    "Wave_load fx\\menu out.wav" "Wave_play_2 menu out.wav"
nocrash menu

# 4. Preferences, audio page (ctrl+I): Main/SFX/Voice/Music buttons (the ini has voiceovers off) and
#    the sliders' volumes; the Music button switches the music off; OK saves the volumes.
run prefs --nosound --new size=0 --human 1 --seed 1,2 --keys "ENTER,C-i,SCREENSTATE,CLICK:480x509,SCREENSTATE,ENTER"
check prefs "volume buttons and sliders" "screenstate prefwin page=4 .* buttons 1101 volumes 127 85 95 63"
check prefs "the Music button turns the music off" "screenstate prefwin page=4 .* buttons 1100 volumes"
check prefs "music preference cleared on OK" "prefs: preferences [0-9A-F]*[0-7][0-9A-F]{2} more"
if grep -q "^Music Volume=63" "$OUT/prefs/Alpha Centauri.Ini" && grep -q "^SFX Volume=95" "$OUT/prefs/Alpha Centauri.Ini"; then
    echo "PASS prefs: volumes written to the ini"
else
    echo "FAIL prefs: volumes written to the ini"; FAIL=1
fi
nocrash prefs

# 5. A voice clip (MP3) through the mixer, written by SDL's disk driver.
rm -f "$OUT/voice.raw"
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$OUT/voice.raw" \
    run voice --new size=0 --human 1 --seed 1,2 --keys "ENTER,PLAYSOUND:voices\\tech1.mp3,UNITS,UNITS,UNITS"
check voice "MP3 decoded: 22050 Hz mono, 24.3 s" "playsound voices.tech1.mp3: 22050 Hz, 1 channels, 24320 ms"
if [ -f "$OUT/voice.raw" ] && "$PY" -c '
import sys, numpy as np
a = np.fromfile(sys.argv[1], dtype=np.float32)
sys.exit(0 if len(a) and np.abs(a).max() > 0.01 else 1)' "$OUT/voice.raw"; then
    echo "PASS voice: the mixer's output is not silent"
else
    echo "FAIL voice: the mixer's output is silent or missing"; FAIL=1
fi
nocrash voice

# 6. The music (aset1, the default set: the sequencer's groups of the .amb file's MIDI tracks on its
#    sampled instruments) rendered for 30 seconds (the music needs a device: the dummy driver).
SDL_AUDIODRIVER=dummy run music --new size=0 --human 1 --seed 1,2 --keys "ENTER,MUSICRENDER:music.wav:30"
check music "the music sequencer plays (30 s, level above silence)" "musicrender .*music.wav: 30 s, rms 0\.0[1-9]"
nocrash music

# 7. A secret project's movie (amovie_project_name): the WVE container, its TQI frames and its sound.
SMACX_MOVIE_FRAMES=100 run movie --new size=0 --human 1 --seed 1,2 --keys "ENTER,MOVIE:humanGenome"
check movie "movie call" "sound: T1 movie humanGenome"
check movie "the movie's frames and sound" "movie movies.humanGenome.wve: 320x240, 422 frames, sound 28066 ms"
check movie "100 frames decoded" "movie movies.humanGenome.wve: 100 of 100 frames decoded"
if "$PY" -c '
import sys
from PIL import Image, ImageStat
s = ImageStat.Stat(Image.open(sys.argv[1]).convert("L"))
sys.exit(0 if s.mean[0] > 60 and s.stddev[0] > 30 else 1)' "$OUT/movie/movie.bmp"; then
    echo "PASS movie: frame 100 is a picture"
else
    echo "FAIL movie: frame 100 is not a picture"; FAIL=1
fi
nocrash movie

exit $FAIL
