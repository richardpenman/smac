# Unit actions that only human players order (and AI actions that observer games rarely reach),
# called directly on end300's turn-300 state: nerve stapling, artillery fire, self-destruct,
# obliterating a base, patrols (land, sea, air), giving units away, the gate, tectonic and fungal
# missiles (with orbital defence pods and a flechette defence system in reach: shoot_it),
# satellite attacks. Each unit is acted on right after `call veh_init` creates it (`$veh`: the id
# that call returned), so the ids stay right when an action kills a unit and renumbers the ones
# above it. Satellites given to factions 3, 5 and 7 also let enemy_diplomacy attack them.
# Base ids are those of the turn-300 state until action_oblit removes base 15 (the bases above
# it then move down by one); the calls that name a base run before it.
load /home/labaron/code/smac/work/saves_late/turn_300.sav
seed 1303 2303
# nerve stapling: base 17 (8,22) four times (later attempts can fail), bases 0 and 3 (former owners)
at 300 call action_staple 17
at 300 call action_staple 17
at 300 call action_staple 17
at 300 call action_staple 17
at 300 call action_staple 0
at 300 call action_staple 3
# faction 5: 3 ODP satellites
at 300 poke 9775F0 4 3
# faction 5: 2 nutrient satellites
at 300 poke 9775E4 4 2
# faction 5: 1 mineral satellites
at 300 poke 9775E8 4 1
# faction 3: 4 ODP satellites
at 300 poke 973458 4 4
# faction 3: 2 energy satellites
at 300 poke 973454 4 2
# faction 7: 2 ODP satellites
at 300 poke 97B788 4 2
# faction 7: 1 nutrient satellites
at 300 poke 97B77C 4 1
# flechette defence system (facility 39) in base 4 (faction 5), next to the first fungal target
at 300 call set_fac 39 4 1
# fungal missile, faction 5 has orbital defence pods
at 300 call veh_init 203 3 23 23
at 300 call action_fungal $veh 26 38
# fungal missile
at 300 call veh_init 203 3 22 22
at 300 call action_fungal $veh 22 20
# tectonic missile on own land
at 300 call veh_init 199 3 36 12
at 300 call action_tectonic $veh 36 14
# tectonic missile
at 300 call veh_init 199 3 21 21
at 300 call action_tectonic $veh 21 21
# hovertank through the gate to base 9
at 300 call veh_init 211 3 12 24
at 300 call action_gate $veh 9
# probe team (basic unit) given to faction 5
at 300 call veh_init 6 3 34 12
at 300 call action_give $veh 5
# hovertank given to faction 7
at 300 call veh_init 211 3 35 11
at 300 call action_give $veh 7
# needlejet outside a base: air patrol
at 300 call veh_init 232 3 34 10
at 300 call valid_patrol $veh 36 14
at 300 call action_patrol $veh 36 14
# needlejet in base 9: air patrol to base 2
at 300 call veh_init 232 3 33 11
at 300 call valid_patrol $veh 41 9
at 300 call action_patrol $veh 41 9
at 300 call action_patrol $veh 37 7
# cruiser in sea base 45: sea patrol
at 300 call veh_init 201 3 8 18
at 300 call action_patrol $veh 4 16
at 300 call valid_patrol $veh 30 20
# hovertank: land patrol
at 300 call veh_init 211 3 10 22
at 300 call action_patrol $veh 10 26
at 300 call valid_patrol $veh 40 40
# hovertank next to base 15 (12,22): obliterate it
at 300 call veh_init 211 3 13 23
at 300 call action_oblit $veh 15
# hovertank next to faction 5 shock troops: self-destruct
at 300 call veh_init 211 3 29 39
at 300 call action_destruct $veh
# artillery next to base 17 (8,22; base 16 after the obliteration), own-faction target
at 300 call veh_init 227 3 9 21
at 300 call action_arty $veh 8 22
# artillery next to base 7, empty target tile
at 300 call veh_init 227 3 26 32
at 300 call action_arty $veh 27 33
# artillery next to base 0 (faction 5)
at 300 call veh_init 227 3 23 21
at 300 call action_arty $veh 22 20
at 300 call action_feature
# satellite attacks (types 0-3: nutrient, mineral, energy, defence pod; 4: sky hydroponics farm)
at 300 call action_sat_attack 3 5 0 -1
at 300 call action_sat_attack 3 5 1 -1
at 300 call action_sat_attack 3 7 3 -1
at 300 call action_sat_attack 7 3 2 -1
at 300 call action_sat_attack 3 5 4 0
at 300 call fungal_tile 24 30
turns 3
