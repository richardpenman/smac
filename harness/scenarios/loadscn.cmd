# Save module, old savegame versions: load the scenarios shipped with the game (savegame versions
# 6, 7, 9 and 10: faction/base/unit conversions in game_data) and snapshot after each load (each
# snapshot re-saves the game as version 12).
seed 1001 2001
load scenarios\Alpha Centauri Scenarios\Faction v Faction\Believers v University\BvU35.SC
snapshot scn_v6
load scenarios\Alpha Centauri Scenarios\1Explore\Explore.SC
snapshot scn_v7
load scenarios\Alpha Centauri Scenarios\Borehole\Borehole.SC
snapshot scn_v9
load scenarios\Alpha Centauri Scenarios\borehole2\Boreho2p.SC
snapshot scn_v10
quit
