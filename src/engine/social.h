// Social engineering (original module around 0x5B4210-0x5B57D0).
//
// A social choice is four model indices (politics, economics, values, future; Faction
// SE_Politics..SE_Future, pending choices in SE_Politics_pending..). Its effects are eleven values
// (economy, efficiency, support, talent, morale, police, growth, planet, probe, industry, research)
// stored as consecutive int32 in Faction (SE_economy_pending.., SE_economy.., SE_economy_2..,
// SE_economy_base..). spot_tile (0x5B5700) at the end of this range belongs to the stack module.
#pragma once
#include "engine/globals.h"

// Effects of the social choice `choices` (4 models) for faction_id into effects[11]: the models'
// effects (faction bonuses SOCIAL immune / doubled penalty, Network Backbone / Cloning Vats cancel
// some penalties); unless choices_only, also Ascetic Virtues, Living Refinery, temples, the
// faction's base modifiers (SE_*_base) and the faction's SOCIAL floor/halving bonuses.
// `unused` is not read.
ADDR(0x5B4210) void GAMEAPI social_calc(const int32_t* choices, int32_t* effects, int32_t faction_id,
                                        int32_t unused, int32_t choices_only);
// Start of the faction's turn: adopts the pending choice and recomputes all effect arrays.
ADDR(0x5B44D0) void GAMEAPI social_upkeep(int32_t faction_id);
// Energy cost of switching from the current choice to `choices` (0 if unchanged).
ADDR(0x5B4550) int32_t GAMEAPI social_upheaval(int32_t faction_id, const int32_t* choices);
// Applies the pending choice: recomputes the pending effects and rescales the accumulated
// nutrients/minerals of the faction's bases to the new cost factors.
ADDR(0x5B4600) void GAMEAPI social_set(int32_t faction_id);
// Whether faction_id may adopt model `model` of category `category` (tech known, not its
// opposition model).
ADDR(0x5B4730) int32_t GAMEAPI society_avail(int32_t category, int32_t model, int32_t faction_id);
// AI choice of social models. Without `out`: computer factions only, priorities from the faction's
// AI settings; picks the best choice, pays the upheaval and applies it. With `out`: writes the best
// choice for the given priorities (growth, tech, wealth, power; growth_or_effect > 100 selects
// effect index growth_or_effect - 100 with the faction's settings) without applying it.
ADDR(0x5B4790) void GAMEAPI social_ai(int32_t faction_id, int32_t growth_or_effect, int32_t tech, int32_t wealth,
                                      int32_t power, int32_t* out);
// Social engineering dialog for faction_id (human players), then applies the choice.
ADDR(0x5B5620) void GAMEAPI social_select(int32_t faction_id);
// Bubble sort of id_list by ascending value_list (both of count entries); restarts after each swap.
ADDR(0x5B5690) void GAMEAPI sort(int32_t count, int32_t* id_list, int32_t* value_list);
