# Probe team actions against bases (probe, 0x59F120) that AI-only play rarely or never reaches.
# end300's turn-300 save; at turn 300 a probe team is created next to each target base and probe()
# is called directly (commit). A prober marked human in FactionStatus[0] takes its action from
# Vehs[].probe_action (bits 0-2 action, bit 3 secondary choice, bits 5-7 framed faction) and
# probe_sabotage_id, as for a human player who is not the viewer; the other probers choose as the
# AI. The human bits are cleared again before the turn is played. Calls run in descending vehicle
# order so a killed probe team does not renumber the ones not yet used.
load /home/labaron/code/smac/work/saves_late/turn_300.sav
seed 1302 2302
# veh 263: faction 7 -> base 27: human treaty partner as target: mind control
at 300 call veh_init 6 7 39 29
at 300 poke 955DC0 1 6
at 300 poke 955DC1 1 0
at 300 poke 955DB8 1 6
# veh 264: faction 3 -> base 0: mind control of the base (human prober)
at 300 call veh_init 6 3 23 21
at 300 poke 955DF4 1 6
at 300 poke 955DF5 1 0
at 300 poke 955DEC 1 3
# veh 265: faction 3 -> base 7: sabotage: production virus (choice 0)
at 300 call veh_init 6 3 26 32
at 300 poke 955E28 1 2
at 300 poke 955E29 1 0
at 300 poke 955E20 1 3
# veh 266: faction 3 -> base 11: sabotage: targeted facility 4
at 300 call veh_init 6 3 14 30
at 300 poke 955E5C 1 10
at 300 poke 955E5D 1 4
at 300 poke 955E54 1 6
# veh 267: faction 3 -> base 41: sabotage: abort (99)
at 300 call veh_init 6 3 9 31
at 300 poke 955E90 1 10
at 300 poke 955E91 1 99
at 300 poke 955E88 1 3
# veh 268: faction 3 -> base 15: drain energy reserves
at 300 call veh_init 6 3 13 23
at 300 poke 955EC4 1 3
at 300 poke 955EC5 1 0
at 300 poke 955EBC 1 4
# veh 269: faction 3 -> base 17: incite drone riots
at 300 call veh_init 6 3 9 21
at 300 poke 955EF8 1 4
at 300 poke 955EF9 1 0
at 300 poke 955EF0 1 5
# veh 270: faction 3 -> base 18: assassinate researchers
at 300 call veh_init 6 3 23 29
at 300 poke 955F2C 1 5
at 300 poke 955F2D 1 0
at 300 poke 955F24 1 6
# veh 271: faction 3 -> base 25: genetic plague
at 300 call veh_init 6 3 21 33
at 300 poke 955F60 1 7
at 300 poke 955F61 1 0
at 300 poke 955F58 1 3
# veh 272: faction 3 -> base 36: procure research data, framing faction 7
at 300 call veh_init 6 3 14 28
at 300 poke 955F94 1 225
at 300 poke 955F95 1 0
at 300 poke 955F8C 1 6
# veh 273: faction 3 -> base 26: infiltrate datalinks
at 300 call veh_init 6 3 7 25
at 300 poke 955FC8 1 0
at 300 poke 955FC9 1 0
at 300 poke 955FC0 1 2
# veh 274: faction 3 -> base 21: incite drone riots, framing faction 7, low morale
at 300 call veh_init 6 3 1 21
at 300 poke 955FFC 1 228
at 300 poke 955FFD 1 0
at 300 poke 955FF4 1 1
# veh 275: faction 3 -> base 22: human prober vs human target (faction 7): sabotage, low morale
at 300 call veh_init 6 3 18 2
at 300 poke 956030 1 2
at 300 poke 956031 1 0
at 300 poke 956028 1 1
# veh 276: faction 3 -> base 22: human prober vs human target (faction 7): assassinate, morale 0
at 300 call veh_init 6 3 16 4
at 300 poke 956064 1 5
at 300 poke 956065 1 0
at 300 poke 95605C 1 0
# veh 277: faction 5 -> base 8: AI prober vs human target (faction 7): excuse paths
at 300 call veh_init 6 5 16 8
at 300 poke 956098 1 0
at 300 poke 956099 1 0
at 300 poke 956090 1 2
# veh 278: faction 5 -> base 13: AI prober vs human target (faction 7), low morale
at 300 call veh_init 6 5 11 5
at 300 poke 9560CC 1 0
at 300 poke 9560CD 1 0
at 300 poke 9560C4 1 1
# veh 279: faction 7 -> base 10: AI prober vs human target (faction 5)
at 300 call veh_init 6 7 27 43
at 300 poke 956100 1 0
at 300 poke 956101 1 0
at 300 poke 9560F8 1 3
# veh 280: faction 7 -> base 1: AI prober (faction 7 vs 5, treaty)
at 300 call veh_init 6 7 13 39
at 300 poke 956134 1 0
at 300 poke 956135 1 0
at 300 poke 95612C 1 4
# veh 281: faction 7 -> base 19: AI prober (faction 7 vs 5, treaty), elite
at 300 call veh_init 6 7 5 21
at 300 poke 956168 1 0
at 300 poke 956169 1 0
at 300 poke 956160 1 6
# veh 282: faction 5 -> base 16: AI prober (faction 5 vs 7)
at 300 call veh_init 6 5 21 11
at 300 poke 95619C 1 0
at 300 poke 95619D 1 0
at 300 poke 956194 1 3
# veh 283: faction 5 -> base 20: AI prober (faction 5 vs 7), elite
at 300 call veh_init 6 5 7 7
at 300 poke 9561D0 1 0
at 300 poke 9561D1 1 0
at 300 poke 9561C8 1 6
# veh 284: faction 5 -> base 9: AI prober vs the Hunter-Seeker Algorithm (faction 3)
at 300 call veh_init 6 5 34 10
at 300 poke 956204 1 0
at 300 poke 956205 1 0
at 300 poke 9561FC 1 3
# veh 285: faction 7 -> base 4: treaty partner (faction 7 vs 5): procure research data
at 300 call veh_init 6 7 31 37
at 300 poke 956238 1 1
at 300 poke 956239 1 0
at 300 poke 956230 1 4
# veh 286: faction 7 -> base 28: treaty partner: production virus
at 300 call veh_init 6 7 3 25
at 300 poke 95626C 1 2
at 300 poke 95626D 1 0
at 300 poke 956264 1 4
# veh 287: faction 7 -> base 32: treaty partner: incite drone riots, framing faction 3
at 300 call veh_init 6 7 6 36
at 300 poke 9562A0 1 100
at 300 poke 9562A1 1 0
at 300 poke 956298 1 6
# veh 288: faction 7 -> base 34: treaty partner: assassinate researchers
at 300 call veh_init 6 7 20 42
at 300 poke 9562D4 1 5
at 300 poke 9562D5 1 0
at 300 poke 9562CC 1 5
# veh 289: faction 7 -> base 65: treaty partner: genetic plague
at 300 call veh_init 6 7 26 36
at 300 poke 956308 1 7
at 300 poke 956309 1 0
at 300 poke 956300 1 4
# veh 290: faction 7 -> base 30: treaty partner: drain energy, framing faction 3
at 300 call veh_init 6 7 4 42
at 300 poke 95633C 1 99
at 300 poke 95633D 1 0
at 300 poke 956334 1 2
# veh 291: faction 7 -> base 31: human treaty partner as target (faction 5): procure research data
at 300 call veh_init 6 7 0 28
at 300 poke 956370 1 1
at 300 poke 956371 1 0
at 300 poke 956368 1 3
# veh 292: faction 7 -> base 23: human treaty partner as target: production virus, low morale
at 300 call veh_init 6 7 39 37
at 300 poke 9563A4 1 2
at 300 poke 9563A5 1 0
at 300 poke 95639C 1 1
# veh 293: faction 7 -> base 32: pact partner (faction 7 vs 5): procure research data
at 300 call veh_init 6 7 7 37
at 300 poke 9563D8 1 1
at 300 poke 9563D9 1 0
at 300 poke 9563D0 1 5
# veh 294: faction 7 -> base 32: pact partner: incite drone riots
at 300 call veh_init 6 7 4 38
at 300 poke 95640C 1 4
at 300 poke 95640D 1 0
at 300 poke 956404 1 5
# veh 295: faction 7 -> base 4: pact partner: production virus, low morale
at 300 call veh_init 6 7 29 39
at 300 poke 956440 1 2
at 300 poke 956441 1 0
at 300 poke 956438 1 1
# veh 296: faction 7 -> base 7: treaty partner, framing human faction 3, morale 0
at 300 call veh_init 6 7 24 32
at 300 poke 956474 1 97
at 300 poke 956475 1 0
at 300 poke 95646C 1 0
# veh 297: faction 7 -> base 7: treaty partner, framing human faction 3, low morale
at 300 call veh_init 6 7 23 33
at 300 poke 9564A8 1 101
at 300 poke 9564A9 1 0
at 300 poke 9564A0 1 1
# veh 298: faction 7 -> base 7: treaty partner: assassinate, framing faction 3, elite
at 300 call veh_init 6 7 26 34
at 300 poke 9564DC 1 101
at 300 poke 9564DD 1 0
at 300 poke 9564D4 1 6
# veh 299: faction 7 -> base 65: treaty partner: production virus, framing faction 3, elite
at 300 call veh_init 6 7 24 36
at 300 poke 956510 1 98
at 300 poke 956511 1 0
at 300 poke 956508 1 6
# veh 300: faction 7 -> base 25: treaty partner: incite drone riots, morale 0
at 300 call veh_init 6 7 21 35
at 300 poke 956544 1 4
at 300 poke 956545 1 0
at 300 poke 95653C 1 0
# veh 301: faction 7 -> base 4: AI prober vs the headquarters holding captured leaders: free a leader
at 300 call veh_init 6 7 27 37
at 300 poke 956578 1 0
at 300 poke 956579 1 0
at 300 poke 956570 1 6
# veh 302: faction 7 -> base 4: AI prober vs the headquarters holding captured leaders
at 300 call veh_init 6 7 28 36
at 300 poke 9565AC 1 0
at 300 poke 9565AD 1 0
at 300 poke 9565A4 1 3
# veh 303: faction 3 -> base 4: AI prober (faction 3, vendetta) vs the headquarters holding captured leaders
at 300 call veh_init 6 3 29 35
at 300 poke 9565E0 1 0
at 300 poke 9565E1 1 0
at 300 poke 9565D8 1 6
at 300 poke 93E904 4 3
at 300 poke 9A64E8 1 0
at 300 call probe 303 4 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 0
at 300 call probe 302 4 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 0
at 300 call probe 301 4 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 300 25 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 299 65 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 298 7 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 136
at 300 call probe 297 7 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 136
at 300 call probe 296 7 -1 1
at 300 poke 97AFA0 4 0xc8000009
at 300 poke 976E10 4 0xc8000009
at 300 poke 9A64E8 1 128
at 300 call probe 295 4 -1 1
at 300 poke 97AFA0 4 0xc8000009
at 300 poke 976E10 4 0xc8000009
at 300 poke 9A64E8 1 128
at 300 call probe 294 32 -1 1
at 300 poke 97AFA0 4 0xc8000009
at 300 poke 976E10 4 0xc8000009
at 300 poke 9A64E8 1 128
at 300 call probe 293 32 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 160
at 300 call probe 292 23 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 160
at 300 call probe 291 31 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 290 30 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 289 65 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 288 34 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 287 32 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 286 28 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 128
at 300 call probe 285 4 -1 1
at 300 poke 9A64E8 1 0
at 300 call probe 284 9 -1 1
at 300 poke 9A64E8 1 0
at 300 call probe 283 20 -1 1
at 300 poke 9A64E8 1 0
at 300 call probe 282 16 -1 1
at 300 poke 9A64E8 1 0
at 300 call probe 281 19 -1 1
at 300 poke 9A64E8 1 0
at 300 call probe 280 1 -1 1
at 300 poke 9A64E8 1 32
at 300 call probe 279 10 -1 1
at 300 poke 9A64E8 1 128
at 300 call probe 278 13 -1 1
at 300 poke 9A64E8 1 128
at 300 call probe 277 8 -1 1
at 300 poke 9A64E8 1 136
at 300 call probe 276 22 -1 1
at 300 poke 9A64E8 1 136
at 300 call probe 275 22 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 274 21 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 273 26 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 272 36 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 271 25 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 270 18 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 269 17 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 268 15 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 267 41 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 266 11 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 265 7 -1 1
at 300 poke 9A64E8 1 8
at 300 call probe 264 0 -1 1
at 300 poke 97AFA0 4 0xc800000a
at 300 poke 976E10 4 0xc800000a
at 300 poke 9A64E8 1 160
at 300 call probe 263 27 -1 1
at 300 poke 9A64E8 1 0
turns 4
