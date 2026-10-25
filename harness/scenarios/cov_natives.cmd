# Native life under AI control (alien_move, 0x566810) on the long300 game at turn 147: Isles of
# the Deep carrying mind worms and a spore launcher next to faction 5 bases (they land the units),
# spore launchers in artillery range of bases and units, mind worms, locusts and a sealurk.
# Units are created with veh_init (faction 0) before the turn-147 snapshot; passengers are put to
# sleep as turn_upkeep's native spawning does (order 1, waypoint -1/0).
load /home/labaron/code/smac/work/saves_late/turn_147.sav
seed 1305 2305
# veh 224: Isle of the Deep next to base 0 (faction 5)
at 147 call veh_init 9 0 21 19
# veh 225: mind worms aboard
at 147 call veh_init 8 0 21 19
at 147 poke 9555ED 1 1
at 147 poke 9555F0 2 -1
at 147 poke 9555F8 2 0
# veh 226: mind worms aboard
at 147 call veh_init 8 0 21 19
at 147 poke 955621 1 1
at 147 poke 955624 2 -1
at 147 poke 95562C 2 0
# veh 227: Isle of the Deep next to bases 3 and 15
at 147 call veh_init 9 0 13 21
# veh 228: spore launcher aboard
at 147 call veh_init 15 0 13 21
at 147 poke 955689 1 1
at 147 poke 95568C 2 -1
at 147 poke 955694 2 0
# veh 229: Isle of the Deep near base 4
at 147 call veh_init 9 0 30 34
# veh 230: mind worms aboard
at 147 call veh_init 8 0 30 34
at 147 poke 9556F1 1 1
at 147 poke 9556F4 2 -1
at 147 poke 9556FC 2 0
# veh 231: Isle of the Deep near base 1, sailing
at 147 call veh_init 9 0 9 43
# veh 232: mind worms aboard
at 147 call veh_init 8 0 9 43
at 147 poke 955759 1 1
at 147 poke 95575C 2 -1
at 147 poke 955764 2 0
# veh 233: spore launcher between bases 0 and 3
at 147 call veh_init 15 0 20 22
# veh 234: spore launcher next to base 7
at 147 call veh_init 15 0 24 34
# veh 235: spore launcher next to base 18
at 147 call veh_init 15 0 23 31
# veh 236: spore launcher next to base 25
at 147 call veh_init 15 0 19 33
# veh 237: mind worms next to base 0
at 147 call veh_init 8 0 22 22
# veh 238: mind worms next to base 11
at 147 call veh_init 8 0 12 32
# veh 239: locusts of Chiron next to base 7
at 147 call veh_init 10 0 26 32
# veh 240: sealurk near base 18
at 147 call veh_init 14 0 22 28
turns 6
