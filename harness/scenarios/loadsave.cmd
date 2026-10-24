# Save module: load oracle saves (late and early game) and snapshot right after loading; each
# snapshot also re-saves the game (compare the .sav files with compare.py --sav or
# harness/scripts/savdiff.py).
seed 1001 2001
load /home/labaron/code/smac/work/saves_late/turn_100.sav
snapshot load_late100
load /home/labaron/code/smac/work/saves_late/tiny20_turn_005.sav
snapshot load_tiny005
quit
