#!/bin/bash
# Scripted checks of the datalinks (the help browser, src/ui/datalink.cpp, help.cpp).
#
#   tests/datalinks/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/p7-native/smacx)
#
# Plays scripted keys on late saves as a human faction (smacx --keys, hidden, SDL dummy video).
# SCREENSTATE logs the open browser ("datalink cat C index I sel S items N history ...", the
# entry's "screentext title ...", "screentext text1/text2 ..." lines). Screenshots go to
# OUTDIR/<case>/. Exit status 0 when every check passes.
set -u
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/p7-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/datalinks_test}")"
mkdir -p "$OUT"
FAIL=0
# turn_147 as faction 3 and turn_300 as faction 6 start with a computer player's call (communicate):
# DOWN,ENTER answers its COMM popup "not now".

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
line() { grep -a "^$2" "$OUT/$1.log" | sed -n "$3p"; }
check() {
    if [ "$2" == "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; FAIL=1; fi
}
has() {
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -aq "is not reconstructed yet\|Aborted\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc") $(grep -am1 'not reconstructed yet\|abort' "$OUT/$1.log")"
        FAIL=1
    fi
}

# Screen positions (1024 x 768): the browser at (112, 30); the link "Recycling Tanks" of
# Biogenetics' entry (in the tree's centre box, 665, 247), BACK (387, 512), EXIT (636, 512); the category buttons of the main
# interface: BASIC CONCEPTS (124, 581), BASIC UNIT TYPES (124, 653), WEAPONS (124, 725), BASE
# FACILITIES (900, 701); the labs report's DATALINKS button (388, 520).

# 1. turn_147 as faction 3: F1 opens the technologies at Biogenetics (the labs report's tree:
#    ReportWin_draw_labs in the entry); a link opens Recycling Tanks
#    (base facilities), BACK returns, a category button shows its first entry, Down the next one;
#    EXIT closes.
run browse147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,F1,SHOT:techs.bmp,SCREENSTATE,CLICK:665x247,\
SHOT:facility.bmp,SCREENSTATE,CLICK:387x512,SCREENSTATE,CLICK:124x581,SCREENSTATE,DOWN,SHOT:concept.bmp,SCREENSTATE,\
CLICK:124x653,DOWN,DOWN,DOWN,DOWN,SHOT:unit.bmp,SCREENSTATE,CLICK:124x725,SCREENSTATE,CLICK:636x512,SCREENSTATE"
nocrash browse147
check "browse147 F1" "$(line browse147 datalink 1)" "datalink cat 14 index 0 sel 11 items 86 history 140000"
has browse147 "tech entry title" "^screentext title BIOGENETICS"
has browse147 "tech entry tree" "^screentext labs tree 0 preq leads 48 49"
has browse147 "tech entry centre" "^screentext labs centre \\[DISCOVER 1\\]"
has browse147 "tech entry links" "^screentext labs allows Base Facilities: \\\$LINK<Recycling Tanks=100003>"
has browse147 "tech quote" "^screentext blurb +-- Chairman Sheng-ji Yang,"
check "browse147 link" "$(line browse147 datalink 2)" "datalink cat 10 index 3 sel 34 items 46 history 140000 100003"
has browse147 "facility entry" "^screentext title RECYCLING TANKS"
check "browse147 back" "$(line browse147 datalink 3)" "datalink cat 14 index 0 sel 11 items 86 history 140000"
check "browse147 concepts" "$(line browse147 datalink 4 | cut -d" " -f1-9)" "datalink cat 0 index 2 sel 0 items 51"
check "browse147 next concept" "$(line browse147 datalink 5 | cut -d' ' -f1-5)" "datalink cat 0 index 13"
check "browse147 unit" "$(line browse147 datalink 6 | cut -d' ' -f1-5)" "datalink cat 3 index 0"
has browse147 "unit stats" "^screentext text1 MOVES: \{1\}"
has browse147 "unit description" "^screentext text2 A \{Colony Pod\} represents the manpower"
check "browse147 weapons" "$(line browse147 datalink 7 | cut -d' ' -f1-5)" "datalink cat 6 index 5"
has browse147 "weapon stats" "^screentext text1 FIREPOWER: 8"
check "browse147 closed" "$(grep -a '^screenstate' "$OUT/browse147.log" | tail -1)" "screenstate none"

# 2. The labs report's DATALINKS opens the research goal (Fusion Power, 81); Esc closes it, then the
#    report. turn_100 as faction 1: F1, the factions (Down to the second), Esc.
run reports147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,F2,CLICK:388x520,SCREENSTATE,ESC,SCREENSTATE,ESC,\
SCREENSTATE"
nocrash reports147
check "reports147 labs datalinks" "$(line reports147 datalink 1 | cut -d' ' -f1-5)" "datalink cat 14 index 81"
check "reports147 back to the report" "$(grep -a '^screenstate' "$OUT/reports147.log" | sed -n 2p)" "screenstate report"
check "reports147 closed" "$(grep -a '^screenstate' "$OUT/reports147.log" | tail -1)" "screenstate none"
run factions100 --save work/saves_late/turn_100.sav --human 1 --keys "F1,CLICK:900x581,DOWN,SHOT:faction.bmp,SCREENSTATE,ESC"
nocrash factions100
check "factions100 faction entry" "$(line factions100 datalink 1 | cut -d' ' -f1-5)" "datalink cat 15 index 1"
has factions100 "faction profile" "^screentext text1 LEADER: "

exit $FAIL
