# Late game: turn 140 of the long300 game (saves copied from work/ref/long300 to work/saves_late).
# Turn 142 depends on wants_to_attack's uninitialised slots (spot_all -> encounter, known
# uninitialised read #4); the oracle poisons them, so the game is deterministic and native code can
# reproduce it (docs/RECONSTRUCTION.md).
load /home/labaron/code/smac/work/saves_late/turn_140.sav
seed 1140 2140
turns 3
