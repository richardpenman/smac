#!/bin/bash
# Scripted checks of the social engineering window, the F-key reports, the monuments and the hall
# of fame (src/ui/socialwin.cpp, reports.cpp, monuwin.cpp, screenwin.cpp).
#
#   tests/reports/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/rp-native/smacx)
#
# Plays scripted keys (smacx --keys, hidden, SDL dummy video) on late saves as a human faction. The
# open screen is logged by SCREENSTATE ("screenstate NAME", "screentext ..." for every text drawn,
# then "socialstate ...", "report ..." or "monustate ..." lines) and the viewer's faction by
# FACTIONSTATE ("factionstate fF credits C social P E V F pending ... paid U alloc ECON PSYCH LABS
# effects ..."); the game saved after a change is loaded again to check that it stays.
# Screenshots go to OUTDIR/<case>/. Exit status 0 when every check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/rp-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/reports_test}")"
mkdir -p "$OUT"
FAIL=0
# turn_147 as faction 3 and turn_300 as faction 6 start with a computer player's call (communicate):
# DOWN,ENTER answers its COMM popup "not now".

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp "$OUT/$name"/hallfame.jhh "$OUT/$name"/hof_*.pcx
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --original-bugs --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
line() {  # line CASE PREFIX N : the Nth log line starting with PREFIX
    grep -a "^$2" "$OUT/$1.log" | sed -n "$3p"
}
check() {  # check WHAT GOT WANT
    if [ "$2" == "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; FAIL=1; fi
}
has() {  # has CASE WHAT REGEX : the log matches
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
fs() {  # fs CASE N : the Nth factionstate line without the effects
    line "$1" factionstate "$2" | sed 's/ effects.*//'
}
ss() {  # ss CASE N : the Nth socialstate line
    line "$1" socialstate "$2"
}

# Screen positions at 1024 x 768. Social engineering window at (111, 201): DEMOCRATIC (politics,
# model 2), OK, the EFFIC effect button; the main interface at y 511: economy +, the economy lock,
# labs -.
DEMOCRATIC=CLICK:456x240
OK=CLICK:305x525
EFFIC=CLICK:790x262
ECON_PLUS=CLICK:246x719
ECON_LOCK=CLICK:26x746
LABS_MINUS=CLICK:520x719

# 1. turn_100 as faction 1 (The Flowers Preach): Democratic costs 24 of the 94 credits (UPHEAVAL:
#    "Never mind" first, then pay), the pending choice changes, the window closes.
run social100 --save work/saves_late/turn_100.sav --human 1 --keys \
"FACTIONSTATE,e,SHOT:open.bmp,SCREENSTATE,$DEMOCRATIC,SCREENSTATE,$OK,ENTER,SCREENSTATE,$OK,SHOT:upheaval.bmp,DOWN,ENTER,\
SCREENSTATE,FACTIONSTATE,SAVE:after.sav"
nocrash social100
check "social100 start" "$(fs social100 1)" "factionstate f1 credits 94 social 3 0 0 0 pending 3 0 0 0 paid 0 alloc 4 0 6"
check "social100 open" "$(ss social100 1 | cut -d' ' -f2-7)" "f1 choice 3 0 0 0"
check "social100 democratic" "$(ss social100 2 | sed 's/.* upheaval \([0-9-]*\) .*/\1/;') $(ss social100 2 | cut -d' ' -f3-7)" \
    "24 choice 2 0 0 0"
has social100 "upheaval question" "popup UPHEAVAL answer: 0"
check "social100 never mind keeps the window" "$(grep -a '^screenstate' "$OUT/social100.log" | sed -n 3p)" "screenstate social"
has social100 "upheaval paid" "popup UPHEAVAL answer: 1"
check "social100 closed" "$(grep -a '^screenstate' "$OUT/social100.log" | sed -n 4p)" "screenstate none"
check "social100 paid" "$(fs social100 2)" "factionstate f1 credits 70 social 3 0 0 0 pending 2 0 0 0 paid 24 alloc 4 0 6"
run social100r --save "$OUT/social100/after.sav" --human 1 --keys "FACTIONSTATE"
check "social100 reload" "$(fs social100r 1)" "factionstate f1 credits 70 social 3 0 0 0 pending 2 0 0 0 paid 24 alloc 4 0 6"

# 2. Cancel (Esc): Reset then OK, nothing changes; the effect button EFFIC picks the models with the
#    largest EFFIC per category.
run socialcancel --save work/saves_late/turn_100.sav --human 1 --keys \
"e,$DEMOCRATIC,ESC,FACTIONSTATE,e,$EFFIC,SCREENSTATE,SHOT:effic.bmp,ESC,FACTIONSTATE"
nocrash socialcancel
check "socialcancel esc" "$(fs socialcancel 1)" "factionstate f1 credits 94 social 3 0 0 0 pending 3 0 0 0 paid 0 alloc 4 0 6"
check "socialcancel effic" "$(ss socialcancel 1 | cut -d' ' -f3-7)" "choice 2 0 0 0"
check "socialcancel esc 2" "$(fs socialcancel 2)" "factionstate f1 credits 94 social 3 0 0 0 pending 3 0 0 0 paid 0 alloc 4 0 6"

# 3. The energy allocation: economy +1 twice (labs give way), the economy locked, labs -1 (psych
#    takes it); OK keeps it (written at once, social_set); a second visit's change is undone by Esc.
run alloc100 --save work/saves_late/turn_100.sav --human 1 --keys \
"e,$ECON_PLUS,$ECON_PLUS,FACTIONSTATE,$ECON_LOCK,$LABS_MINUS,SCREENSTATE,SHOT:alloc.bmp,ENTER,FACTIONSTATE,\
e,$ECON_PLUS,FACTIONSTATE,ESC,FACTIONSTATE,SAVE:after.sav"
nocrash alloc100
check "alloc100 econ +2" "$(fs alloc100 1 | sed 's/.* alloc //')" "6 0 4"
check "alloc100 locked econ, labs -1" "$(ss alloc100 1 | sed 's/.* alloc //')" "6 1 3 locks 1 0 0"
check "alloc100 ok" "$(fs alloc100 2 | sed 's/.* alloc //')" "6 1 3"
check "alloc100 second visit (psych gives way first, the lock stays)" "$(fs alloc100 3 | sed 's/.* alloc //')" "7 0 3"
check "alloc100 esc resets" "$(fs alloc100 4 | sed 's/.* alloc //')" "6 1 3"
run alloc100r --save "$OUT/alloc100/after.sav" --human 1 --keys "FACTIONSTATE"
check "alloc100 reload" "$(fs alloc100r 1 | sed 's/.* alloc //')" "6 1 3"

# 4. The reports on turn_147 as faction 3 (University, nine bases): base operations (order, the
#    NUTRIENT sort, the garrison view, a double click opens the base, its F-key closes), security
#    nexus (the land units filter), score, projects, labs, energy, satellites; Right cycles.
run reports147 --save work/saves_late/turn_147.sav --human 3 --keys \
"DOWN,ENTER,F4,SHOT:ops.bmp,SCREENSTATE,CLICK:180x586,CLICK:30x628,SCREENSTATE,CLICK:740x40,SHOT:garrison.bmp,SCREENSTATE,CLICK:530x40,\
DCLICK:400x120,BASESTATE,ESC,SCREENSTATE,F4,SCREENSTATE,\
F7,SHOT:security.bmp,SCREENSTATE,CLICK:50x605,SCREENSTATE,F8,SHOT:score.bmp,SCREENSTATE,F5,SHOT:projects.bmp,SCREENSTATE,\
F2,SHOT:labs.bmp,SCREENSTATE,F3,SHOT:energy.bmp,SCREENSTATE,F6,SHOT:sat.bmp,SCREENSTATE,RIGHT,SCREENSTATE,ESC,SCREENSTATE"
nocrash reports147
r() { grep -a "^report $2" "$OUT/$1.log"; }
check "ops order" "$(r reports147 'ops [0-9] ' | head -9 | cut -d' ' -f5 | tr '\n' ' ')" "2 5 6 9 12 24 38 43 45 "
check "ops first row" "$(r reports147 'ops 0 ' | head -1)" \
    "report ops 0 base 2 University Base gov Governor Off build Chaos Squad (7/24)    turns:  9"
check "ops nutrient sort" "$(r reports147 'ops [0-9] ' | sed -n 10,18p | cut -d' ' -f5 | tr '\n' ' ')" \
    "9 5 38 43 2 6 24 45 12 "
check "ops sort title" "$(grep -a '^screentext \[' "$OUT/reports147.log" | sed -n 2p)" \
    "screentext [UNIVERSITY BASE OPERATIONS - NUTRIENT]"
has reports147 "ops garrison view" "^report ops sort 2 view garrison bases 9"
has reports147 "ops garrison units" "^report ops [0-9] base 2 University Base gov Governor Off units 157 212 177 4 45"
check "ops double click opens the base" "$(line reports147 basestate 1 | cut -d' ' -f1-5)" "basestate base 5 U.N. Headquarters"
check "ops after the base window" "$(grep -a '^screenstate' "$OUT/reports147.log" | sed -n 4p)" "screenstate report"
check "ops F4 closes" "$(grep -a '^screenstate' "$OUT/reports147.log" | sed -n 5p)" "screenstate none"
has reports147 "security list" "^report security 64 Chaos Rover|^report security filter 4 types [0-9]+"
has reports147 "security land filter" "^report security filter 0 types"
has reports147 "score total" "^report score total [0-9]+"
has reports147 "projects" "^report project \{The Planetary Datalinks\} \{Koppernigk Observatory\} \(University\)"
has reports147 "labs" "^report labs cost [0-9]+ accumulated 277 per turn"
has reports147 "energy" "^report energy reserves 195 "
has reports147 "satellites (nobody has Orbital Spaceflight)" "^report sat none"
check "right cycles to the security nexus" "$(grep -a '^reportstate' "$OUT/reports147.log" | tail -1 | cut -d' ' -f1-3)" \
    "reportstate kind 7"
check "esc closes" "$(grep -a '^screenstate' "$OUT/reports147.log" | tail -1)" "screenstate none"

# 5. Monuments (F9): the faction's nine monuments in the order of their years; a click shows one
#    and records it as the latest (saved); Hall of fame (F10) writes hallfame.jhh (in --outdir).
run monuments147 --save work/saves_late/turn_147.sav --human 3 --keys \
"DOWN,ENTER,F9,SHOT:monuments.bmp,SCREENSTATE,CLICK:700x475,SHOT:text.bmp,SCREENSTATE,ESC,ESC,SAVE:after.sav,F10,SHOT:fame.bmp,SCREENSTATE,ESC"
nocrash monuments147
check "monuments order" "$(grep -a '^screentext monument' "$OUT/monuments147.log" | head -9 | cut -d' ' -f3 | tr '\n' ' ')" \
    "1 0 2 3 4 6 5 7 8 "
check "monuments state" "$(line monuments147 monustate 1)" "monustate f3 count 9 latest 8 zoom 0"
has monuments147 "monument text" "^screentext M\.Y\. 2162: "
check "monuments latest recorded" "$(line monuments147 monustate 2 | cut -d' ' -f5-8)" "latest 7 zoom 1"
has monuments147 "hall of fame" "^famestate faction 3 entries 0"
check "hallfame.jhh written" "$(stat -c %s "$OUT/monuments147/hallfame.jhh" 2>/dev/null)" "4900"
run monuments147r --save "$OUT/monuments147/after.sav" --human 3 --keys "F9,SCREENSTATE,ESC"
check "monuments reload" "$(line monuments147r monustate 1)" "monustate f3 count 9 latest 7 zoom 0"

# 5b. Retiring (shift+Q, REALLYRETIRE "yes") ends the game: report_score, then hall_of_fame(1) adds
#     the viewer's entry (FUN_0044A660: its score, rating, the "best of" marks of a first entry)
#     with the world map pane's picture hof_3_1.pcx (in --outdir), shown in WORLD MAP; the hall's
#     other panels (FUN_00448D90): the factions' emblems, SCORE ANALYSIS (the marks, the score
#     lines), the first mark's banner, TOP 5 SCORES. The
#     monuments' decorations: the viewer's emblem above the centre.
run retire147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,S-q,DOWN,ENTER,ESC,SCREENSTATE,ESC"
nocrash retire147
has retire147 "the new entry" "^hall of fame: faction 3 slot 0 score 247 rating 15 flags 01 best 0F picture 0"
has retire147 "its world map" "^screentext world map hof_3_1.pcx"
has retire147 "the window" "^famestate faction 3 entries 1"
has retire147 "SCORE ANALYSIS: the entry's marks" "^screentext HIGHEST TECH LEVEL SCORE"
has retire147 "SCORE ANALYSIS: its score lines" "^screentext ALPHA CENTAURI RATING"
has retire147 "SCORE ANALYSIS: the difficulty" "^screentext LIBRARIAN"
has retire147 "TOP 5 SCORES: the entry" "^screentext 15%  M.Y.2248"
check "hof_3_1.pcx written" "$(head -c 4 "$OUT/retire147/hof_3_1.pcx" 2>/dev/null | od -An -tx1 | tr -d ' ')" "0a050108"
check "hallfame.jhh with the entry" "$(od -An -tx1 -j $((10 * 0x8C)) -N 2 "$OUT/retire147/hallfame.jhh" 2>/dev/null | tr -d ' ')" "010f"

# 5c. The reports' hover help and base locator: a right click on a report button shows its name
#     (BaseButton_set_bubble_text, ReportIf_on_status); a click on a base of base operations
#     selects it alone: located on the world map pane (WorldWin_show_all, ReportIf_bl_anim's dot
#     at its pixel less 7, 5).
run locator147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,F4,RCLICK:360x732,CLICK:500x75,SHOT:loc.bmp,ESC"
nocrash locator147
has locator147 "the report button's help" "^bubble: BASE OPERATIONS"
has locator147 "the base located" "^report locator: base 2 at 180,26"

# 5d. The satellites report's ORBITAL ATTACK VIEW button (0xD3D, ReportWin_draw_sat's animation:
#     satellite.flc in its box and the satellites of all factions as dots; none on turn_300).
run satanim300 --save work/saves_late/turn_300.sav --human 3 --keys "ESC,ESC,ESC,ESC,ESC,ESC,ESC,F6,CLICK:700x41,SCREENSTATE,SHOT:sat.bmp,ESC"
nocrash satanim300
has satanim300 "the animation view" "^report sat anim satellites 0"

# 5e. The satellites table (ReportWin_draw_sat: every faction, the five columns; the panes' SAT0 /
#     SAT1 texts) and the orbital attack: in the editor's omniscient view a right click on a cell
#     adds a satellite (sat_edit: two ODPs for the University, a Sky Hydroponics Lab for the
#     Spartans); reopened, ORBITAL ATTACK VIEW, the Spartans (target button), their Lab (kind
#     button), ATTACK (0xD3E): sat_attack, ReportWin_start_attack's explosion, action_sat_attack
#     (WEMUFFEDIT: the pod failed), one satellite fewer in the view (reopened: animated, the
#     explosion ends on its timer after the click, WAIT, and does not redraw the report).
run satattack300 --save work/saves_late/turn_300.sav --human 3 --keys \
"ESC,ESC,ESC,ESC,ESC,ESC,ESC,F6,SCREENSTATE,ESC,C-k,DOWN,ENTER,ENTER,F6,RCLICK:677x227,RCLICK:677x227,RCLICK:371x325,ESC,F6,\
CLICK:700x41,CLICK:63x631,CLICK:838x609,SCREENSTATE,SHOT:attack.bmp,CLICK:563x487,WAIT:1500,ENTER,SCREENSTATE,ESC,F6,CLICK:700x41,SCREENSTATE"
nocrash satattack300
has satattack300 "the table's rows" "^report sat Peacekeepers 0 0 0 0/0 0"
has satattack300 "SAT0 in the left pane" "^screentext Each satellite \\(Lab, Mining Station"
has satattack300 "sat_edit" "^sat edit: faction 5 kind 0 -> 1"
has satattack300 "the target and kind buttons" "^report sat iface view 3389 target 5 kind 65 targets 5@13,110 kinds 0@788,88 attack shown"
has satattack300 "the explosion" "^sat attack start: faction 3 on 5 kind 0 base 0"
has satattack300 "action_sat_attack" "^sat attack: faction 3 on 5 kind 0 base 0"
has satattack300 "its result" "^popup WEMUFFEDIT shown"
has satattack300 "a satellite fewer" "^report sat anim satellites 2"

# 6. Base operations' governor (turn_147, University Base selected: Set to BUILD, then off) and the
#    commlink panel (F12): the contacts, a right click's menu, Faction Profile (the intelligence
#    report), the Council button (call_council: COUNCILNEEDALL here).
run commlink147 --save work/saves_late/turn_147.sav --human 3 --keys \
"DOWN,ENTER,F4,CLICK:500x75,CLICK:120x678,SCREENSTATE,CLICK:120x744,SCREENSTATE,ESC,F12,SHOT:commlink.bmp,SCREENSTATE,\
RCLICK:950x384,SHOT:menu.bmp,SCREENSTATE,CLICK:900x413,SHOT:intel.bmp,SCREENSTATE,ESC,CLICK:950x478,ENTER,SCREENSTATE,ESC,\
SCREENSTATE"
nocrash commlink147
check "governor set to build" "$(r commlink147 'ops 0 ' | head -1 | cut -d' ' -f5-10)" "2 University Base gov BUILD build"
check "governor off" "$(r commlink147 'ops 0 ' | sed -n 2p | cut -d' ' -f5-11)" "2 University Base gov Governor Off build"
check "commlink contacts" "$(line commlink147 multistate 1)" "multistate rows 2 4 5 6 7 votes 39 menu -1 hover -1"
check "commlink menu" "$(line commlink147 multistate 2 | sed 's/.* menu //')" "4 hover -1"
has commlink147 "faction profile" "^multiwin action 2 faction 4"
has commlink147 "intelligence report" "^report intel CEO Morgan of the Morganites"
has commlink147 "council" "^popup COUNCILNEEDALL shown"
check "commlink closed" "$(grep -a '^screenstate' "$OUT/commlink147.log" | tail -1)" "screenstate none"

# 7. turn_300 as faction 6 (three popups first, the last one opens a base window: Esc): every
#    screen opens and closes.
run late300 --save work/saves_late/turn_300.sav --human 6 --keys \
"DOWN,ENTER,ENTER,ENTER,ENTER,BASESTATE,ESC,F4,SCREENSTATE,ESC,F7,ESC,F8,SCREENSTATE,ESC,F5,ESC,F2,ESC,F3,ESC,F6,ESC,e,SCREENSTATE,ESC,\
F9,ESC,F10,ESC,SCREENSTATE,F12,CLICK:950x478,SHOT:council.bmp,ESC,ESC"
nocrash late300
has late300 "council: the proposals" "^popup COUNCILISSUES shown"
has late300 "base operations" "^reportstate kind 4 faction 6"
has late300 "score" "^report score total"
has late300 "social engineering" "^socialstate f6"
check "late300 all closed" "$(grep -a '^screenstate' "$OUT/late300.log" | tail -1)" "screenstate none"

exit $FAIL
