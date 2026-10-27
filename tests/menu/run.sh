#!/bin/bash
# Scripted checks of the opening screens of smacx (docs/UI_PLAN.md section 11): the main menu
# (top_menu) and its sub-menus, a new game set up through the setup dialogs (map menu, size and
# custom planet, difficulty, rules, the faction picker, the leader's name) compared with the --new
# game of the same parameters and seed, a save loaded through the FileBox, and a preference
# changed in the preferences dialog (Alpha Centauri.Ini).
#
#   tests/menu/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/p9-native/smacx)
#
# Every run uses its own ini (OUTDIR/menu.ini: harness/oracle/runtime's with Difficulty=2) and
# writes only below OUTDIR. Exit status 0 when every check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/p9-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/menu_test}")"
rm -rf "$OUT"
mkdir -p "$OUT"
cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/menu.ini"
echo "Difficulty=2" >> "$OUT/menu.ini"
FAIL=0
SEED=1001,2001

run() {  # run CASE ARGS... : smacx with a fresh outdir (its own ini copy), log in OUT/CASE.log
    local name="$1"; shift
    rm -rf "$OUT/$name"
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --ini "$OUT/menu.ini" \
        --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
}
check() {  # check NAME CONDITION-RESULT(0/1) DETAIL
    if [ "$2" == 0 ]; then echo "PASS $1"; else echo "FAIL $1: $3"; FAIL=1; fi
}
has() {  # has CASE TEXT : the log has the line(s)
    grep -qF -- "$2" "$OUT/$1.log"
}
# The scripted keys typing a text.
typed() {
    python3 -c 'import sys; print(",".join({",": "COMMA", " ": "SPACE"}.get(c, c) for c in sys.argv[1]))' "$1"
}
compare() {  # compare NAME DIR_A DIR_B : turn_000 of both runs equal (harness/compare.py)
    local r
    r=$(python3 "$ROOT/harness/compare.py" "$1" "$2" 2>&1)
    echo "$r" > "$OUT/compare_$(basename "$(dirname "$1")").txt"
    [ -f "$1/turn_000.state" ] && [ -f "$2/turn_000.state" ] && echo "$r" | grep -q "^turn_000: OK"
}

# 1. The main menu and its sub-menus: SCENARIO (and back), LOAD GAME's FileBox (cancelled),
#    MULTIPLAYER (the service list, cancelled), VIEW CREDITS, then EXIT GAME.
run menus --seed $SEED --keys \
"MENUSTATE,DOWN,DOWN,ENTER,MENUSTATE,ESC,MENUSTATE,DOWN,ENTER,SCREENSTATE,ESC,DOWN,ENTER,ESC,\
DOWN,ENTER,SCREENSTATE,ESC,MENUSTATE,DOWN,ENTER"
c=0
has menus 'menustate caption="" selected=0 [START GAME] [QUICK START] [SCENARIO] [LOAD GAME] [MULTIPLAYER] [VIEW CREDITS] [EXIT GAME]' || c=1
has menus 'menustate caption="SCENARIO" selected=0 [PLAY SCENARIO] [CREATE SCENARIO] [LOAD MAP FILE] [EDIT SCENARIO]' || c=1
has menus 'menustate caption="" selected=2' || c=1
has menus 'screenstate filebox kind=200 dir="saves"' || c=1
has menus 'filebox: cancelled' || c=1
has menus 'popup SELECTSERVICE answer: -1' || c=1
has menus 'screenstate credits' || c=1
has menus 'menustate caption="" selected=5' || c=1
has menus 'menu  -> 6' || c=1
check menus $c "see $OUT/menus.log"

# 2. A new game from the menu: START GAME, CUSTOMIZE RANDOM MAP, a tiny planet, the custom map's
#    defaults (ocean, erosion, native life, clouds 1; the orbit stays 0: SMACX does not ask it),
#    difficulty 3, standard rules, the faction picker's third slot (the Free Drones), the leader's
#    name. Its turn_000 snapshot equals the --new game's with the same parameters and seed.
FACTIONS=CYBORG,PIRATES,DRONE,ANGELS,FUNGBOY,CARETAKE,USURPER
run setup --seed $SEED --snapshots "$OUT/setup/snap" --keys \
"ENTER,MENUSTATE,DOWN,ENTER,MENUSTATE,UP,UP,ENTER,MENUSTATE,ENTER,ENTER,ENTER,MENUSTATE,ENTER,\
MENUSTATE,DOWN,ENTER,MENUSTATE,ENTER,SCREENSTATE,DOWN,DOWN,SCREENSTATE,ENTER,ENTER,ENTER"
run setup_new --seed $SEED --snapshots "$OUT/setup_new/snap" --keys ENTER --new size=0 ocean=1 erosion=1 \
    orbit=0 clouds=1 natives=1 diff=3 factions=$FACTIONS --human 3
c=0
has setup 'menustate caption="SELECT SIZE OF PLANET" selected=2' || c=1
has setup 'menustate caption="PICK A DIFFICULTY LEVEL" selected=2' || c=1
has setup "screenstate pickwin selected=3 group=104 slots CYBORG PIRATES DRONE ANGELS FUNGBOY CARETAKE USURPER" || c=1
has setup 'popup NAME answer: 0' || c=1
has setup 'popup PLANETFALL answer: 0' || c=1
has setup 'game from the menu: turn 0, human faction 3 (Drones)' || c=1
compare "$OUT/setup/snap" "$OUT/setup_new/snap" || c=1
check setup $c "see $OUT/setup.log, $OUT/compare_setup.txt"

# 3. Custom rules (USERULES' third item, RULES: the first box, victory by transcendence, cleared)
#    and difficulty 1: equals --new rules=0x120E (the standard 0x1A0E without 0x800).
run rules --seed $SEED --snapshots "$OUT/rules/snap" --keys \
"ENTER,DOWN,ENTER,UP,UP,ENTER,ENTER,ENTER,ENTER,ENTER,UP,ENTER,DOWN,DOWN,ENTER,SPACE,ENTER,ENTER,ENTER"
run rules_new --seed $SEED --snapshots "$OUT/rules_new/snap" --keys ENTER --new size=0 ocean=1 erosion=1 \
    orbit=0 clouds=1 natives=1 diff=1 rules=0x120E factions=$FACTIONS --human 1
c=0
has rules 'popup RULES answer' || c=1
compare "$OUT/rules/snap" "$OUT/rules_new/snap" || c=1
check rules $c "see $OUT/rules.log, $OUT/compare_rules.txt"

# 4. LOAD GAME: a save of a --new game (SAVE: in its outdir, beside the saves folder of that run),
#    found by typing its directory in the FileBox's name field, selected in the list and loaded.
run save --new size=0 --human 1 --seed 1,2 --keys "SAVE:menu1.SAV"
DIRKEYS=$(typed "$OUT/save")
run load --seed $SEED --keys "DOWN,DOWN,DOWN,ENTER,$DIRKEYS,ENTER,SCREENSTATE,DOWN,DOWN,ENTER"
c=0
has load "screenstate filebox kind=200 dir=\"$OUT/save\" selected=0 edit=\"\" entries [<..] [<saves] [menu1.SAV]" || c=1
has load "filebox: $OUT/save\\menu1.SAV" || c=1
has load 'game from the menu: turn 0, human faction 1 (Gaians)' || c=1
check load $c "see $OUT/load.log"

# 5. Preferences (ctrl+P): the first box (tutorial messages, bit 0x20) flipped and OK: the
#    dialog's preferences are written to the game's ini (prefs_save).
run prefs --new size=0 --human 1 --seed 1,2 --keys "ENTER,C-p,SCREENSTATE,CLICK:434x168,SCREENSTATE,ENTER"
c=0
before=$(grep -o "screenstate prefwin page=0 7:[01]" "$OUT/prefs.log" | head -1 | sed 's/.*7://')
after=$(grep -o "screenstate prefwin page=0 7:[01]" "$OUT/prefs.log" | tail -1 | sed 's/.*7://')
saved=$(grep -o "prefs: preferences [0-9A-F]*" "$OUT/prefs.log" | awk '{print $3}')
ini=$(grep "^Preferences=" "$OUT/prefs/Alpha Centauri.Ini" | sed 's/Preferences=//' | tr -d '\r')
[ -n "$before" ] && [ -n "$after" ] && [ "$before" != "$after" ] || c=1
[ -n "$saved" ] && [ "$(python3 -c "print(format(int('$saved', 16), 'b'))")" == "$ini" ] || c=1
[ -n "$saved" ] && [ "$(( (16#$saved >> 5) & 1 ))" == "$after" ] || c=1
check prefs $c "tutorial box $before -> $after, saved $saved, ini $ini"

# 6. The gamma correction dialog (Preferences' GAMMA CORRECTION, FUN_005C8E60): Right twice and the
#    scroll bar's right button raise it to 1.03; OK keeps it (the ini's Gamma Correction).
run gamma --new size=0 --human 1 --seed 1,2 --keys "ENTER,C-p,CLICK:253x323,SCREENSTATE,RIGHT,RIGHT,CLICK:618x454,SCREENSTATE,CLICK:452x483,ESC"
c=0
has gamma "screenstate gamma" || c=1
has gamma "gammastate pos 103 old 100" || c=1
has gamma "gamma: 103 (OK)" || c=1
grep -q "^Gamma Correction=103" "$OUT/gamma/Alpha Centauri.Ini" || c=1
check gamma $c "see $OUT/gamma.log"

exit $FAIL
