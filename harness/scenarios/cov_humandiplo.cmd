# AI diplomacy towards human players (enemy_diplomacy, 0x55F930), which observer games never
# reach: on end300's turn-300 save, enemy_diplomacy of factions 3, 5 and 7 is called while other
# factions are marked human in FactionStatus[0] (0x9A64E8), none of them the viewer. The human bits
# are cleared again before the turn is played.
load /home/labaron/code/smac/work/saves_late/turn_300.sav
seed 1307 2307
at 300 poke 9A64E8 1 0xA0
at 300 call enemy_diplomacy 3
at 300 poke 9A64E8 1 0x88
at 300 call enemy_diplomacy 5
at 300 poke 9A64E8 1 0x28
at 300 call enemy_diplomacy 7
at 300 poke 9A64E8 1 0x40
at 300 call enemy_diplomacy 3
at 300 call enemy_diplomacy 5
at 300 poke 9A64E8 1 0
turns 2
