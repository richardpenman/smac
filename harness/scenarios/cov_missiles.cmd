# Missiles and planet busters under AI control (air_power, enemy_planet_buster, planet_busting,
# action_fungal, shoot_it), which the AI factions of the reference games never build. end300's
# turn-300 save; at turn 300 missiles are created in bases of factions 3 (University), 5 and 7.
# Faction 3 may commit atrocities wantonly (player_flags |= 0x1000000), so it uses its planet
# busters. Faction 5's vendettas are lifted, so its missiles have no target and are
# stockpiled. AI tectonic missiles are aimed but never fired: air_power returns after setting
# the course, also in the original (action_tectonic is covered by cov_actions).
load /home/labaron/code/smac/work/saves_late/turn_300.sav
seed 1301 2301
# veh 263-265: faction 3 tectonic missiles (base 9, base 14, sea base 45)
at 300 call veh_init 199 3 33 11
at 300 call veh_init 199 3 22 40
at 300 call veh_init 199 3 8 18
# veh 266-268: faction 5 tectonic missiles; 269-270: fungal missiles
at 300 call veh_init 339 5 22 20
at 300 call veh_init 339 5 0 32
at 300 call veh_init 339 5 41 37
at 300 call veh_init 358 5 25 33
at 300 call veh_init 358 5 46 28
# veh 271: faction 3 fungal missile; 272-273: planet busters
at 300 call veh_init 203 3 33 11
at 300 call veh_init 206 3 33 11
at 300 call veh_init 233 3 31 7
# veh 274-278: faction 3 conventional missiles
at 300 call veh_init 198 3 34 18
at 300 call veh_init 198 3 34 18
at 300 call veh_init 198 3 22 40
at 300 call veh_init 198 3 22 40
at 300 call veh_init 198 3 22 40
# veh 279-281: faction 7 planet busters in one base
at 300 call veh_init 451 7 15 9
at 300 call veh_init 451 7 15 9
at 300 call veh_init 451 7 15 9
# veh 282-284: faction 5 fungal missiles in base 23; faction 5's vendettas (with 3 and 6) are
# lifted below, so they have no target and are stockpiled
at 300 call veh_init 358 5 41 37
at 300 call veh_init 358 5 41 37
at 300 call veh_init 358 5 41 37
# veh 285: faction 3 needlejet on automatic patrol (order_auto_type 12), damaged
at 300 call veh_init 232 3 41 9
at 300 poke 956210 4 0x200
at 300 poke 956232 1 12
at 300 poke 95621C 1 10
# faction 3: commit atrocities wantonly; factions 7 and 5: vendettas lifted
at 300 poke 972C44 4 0x1000a14
at 300 poke 97AF98 4 0xC8008108
at 300 poke 976E00 4 0xC8008108
at 300 poke 976E0C 4 0xC8000008
turns 8
