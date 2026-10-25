# Unit actions that only human players order (and AI actions that observer games rarely reach),
# called directly on end300's turn-300 state: artillery fire, self-destruct, obliterating a base,
# patrols (land, sea, air), giving units away, the gate, tectonic and fungal missiles (with
# orbital defence pods and a flechette defence system in reach: shoot_it), nerve stapling,
# satellite attacks. Units are created first; calls that kill a unit run in descending vehicle
# order. Satellites given to factions 3, 5 and 7 also let enemy_diplomacy attack them.
load /home/labaron/code/smac/work/saves_late/turn_300.sav
seed 1303 2303
# veh 263: artillery next to base 0 (faction 5)
at 300 call veh_init 227 3 23 21
# veh 264: artillery next to base 7, empty target tile
at 300 call veh_init 227 3 26 32
# veh 265: artillery next to base 17, own-faction target
at 300 call veh_init 227 3 9 21
# veh 266: hovertank next to faction 5 shock troops: self-destruct
at 300 call veh_init 211 3 29 39
# veh 267: hovertank next to base 15: obliterate it
at 300 call veh_init 211 3 13 23
# veh 268: hovertank: land patrol
at 300 call veh_init 211 3 10 22
# veh 269: cruiser in sea base 45: sea patrol
at 300 call veh_init 201 3 8 18
# veh 270: needlejet in base 9: air patrol to base 2
at 300 call veh_init 232 3 33 11
# veh 271: needlejet outside a base: air patrol
at 300 call veh_init 232 3 34 10
# veh 272: hovertank given to faction 7
at 300 call veh_init 211 3 35 11
# veh 273: probe team (basic unit) given to faction 5
at 300 call veh_init 6 3 34 12
# veh 274: hovertank through the gate to base 9
at 300 call veh_init 211 3 12 24
# veh 275: tectonic missile
at 300 call veh_init 199 3 21 21
# veh 276: fungal missile
at 300 call veh_init 203 3 22 22
# veh 277: fungal missile, faction 5 has orbital defence pods
at 300 call veh_init 203 3 23 23
# veh 278: tectonic missile on own land
at 300 call veh_init 199 3 36 12
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
at 300 call action_fungal 277 26 38
at 300 call action_fungal 276 22 20
at 300 call action_tectonic 278 36 14
at 300 call action_tectonic 275 21 21
at 300 call action_gate 274 9
at 300 call action_give 273 5
at 300 call action_give 272 7
at 300 call valid_patrol 271 36 14
at 300 call action_patrol 271 36 14
at 300 call valid_patrol 270 41 9
at 300 call action_patrol 270 41 9
at 300 call action_patrol 270 37 7
at 300 call action_patrol 269 4 16
at 300 call valid_patrol 269 30 20
at 300 call action_patrol 268 10 26
at 300 call valid_patrol 268 40 40
at 300 call action_oblit 267 15
at 300 call action_destruct 266
at 300 call action_arty 265 8 22
at 300 call action_arty 264 27 33
at 300 call action_arty 263 22 20
at 300 call action_feature
# nerve stapling just before the last snapshot (turn 303): base 17 four times (later attempts can
# fail), bases 0 and 3 (former owners). Stapling earlier makes the turn loop hang, in the original
# too (enemy_turn, 0x579953).
at 303 call action_staple 17
at 303 call action_staple 17
at 303 call action_staple 17
at 303 call action_staple 17
at 303 call action_staple 0
at 303 call action_staple 3
# satellite attacks (types 0-3: nutrient, mineral, energy, defence pod; 4: sky hydroponics farm)
at 300 call action_sat_attack 3 5 0 -1
at 300 call action_sat_attack 3 5 1 -1
at 300 call action_sat_attack 3 7 3 -1
at 300 call action_sat_attack 7 3 2 -1
at 300 call action_sat_attack 3 5 4 0
at 300 call fungal_tile 24 30
turns 3
