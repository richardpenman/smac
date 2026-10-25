# Bases changing hands without capture (give_a_base, 0x54CF40: diplomacy's base trades and
# demands), on the long300 game at turn 147. DiploCounterProposal (0x93FAB0) 3 marks a base given
# under a counter-proposal: the base keeps its former owner and gets 50 assimilation turns, unless
# it returns to its former owner.
load /home/labaron/code/smac/work/saves_late/turn_147.sav
seed 1306 2306
# base 3 (faction 5, former owner 4) back to faction 4; base 0 (faction 5) to faction 7
at 147 poke 93FAB0 4 3
at 147 call give_a_base 3 4
at 147 call give_a_base 0 7
at 147 poke 93FAB0 4 0
# base 27 (faction 2) to faction 5; base 15 (faction 4) to faction 3
at 147 call give_a_base 27 5
at 147 call give_a_base 15 3
turns 4
