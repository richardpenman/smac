# Late game: turn 140 of the long300 game (saves copied from work/ref/long300 to work/saves_late).
# NOTE: not deterministic even for the plain oracle (vehicle orders differ at turns 141/142 between
# runs): reproduces the uninitialised-stack read below enemy_veh that stack poisoning does not cover.
load /home/labaron/code/smac/work/saves_late/turn_140.sav
seed 1140 2140
turns 3
