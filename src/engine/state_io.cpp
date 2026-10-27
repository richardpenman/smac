// The records of the game state and their serialisation (engine/state_io.h).
#include "engine/state_io.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "engine/design.h"
#include "engine/globals.h"
#include "engine/save.h"

namespace {

// ---- the field lists, in the original's order ----

template <class Io>
void record_io(Io& io, GamePrefs& r) {
    io(r.GamePreferences);
    io(r.GameMorePreferences);
    io(r.GameWarnings);
    io(r.GameRules);
    io(r.dword_9A64A0);
    io(r.GameInterludeState);
    io(r.TutorialChecks);
    io(r.TutorialChecks2);
    io(r.TutWinMapState);
    io(r.dword_9A64BC);
    io(r.GameState);
    io(r.DiffLevel);
    io(r.VehCount);
    io(r.BaseCount);
    io(r.dword_9A64D0);
    io(r.CurrentTurn);
    io(r.CurrentMissionYear);
    io(r.CurrentVehID);
    io(r.GameTimeControl);
    io(r.FactionStatus);
    io(r.FactionRankings);
    io(r.RankingFactionIDUnk1);
    io(r.RankingFactionIDUnk2);
    io(r.SecretProjects);
    io(r.GovernorFaction);
    io(r.ProposalPassCount);
    io(r.ProposalCallTurn);
    io(r.TechOwners);
    io(r.LastSavePath);
    io(r.pad0);
    io(r.ClimateValueA);
    io(r.ClimateValueB);
    io(r.ClimateValueC);
    io(r.ClimateFutureChange);
    io(r.ScoreBonusPts);
    io(r.ScorePopTotal);
    io(r.SunspotDuration);
    io(r.MountPlanetX);
    io(r.MountPlanetY);
    io(r.DustCloudDuration);
    io(r.SaveFileVersion);
    io(r.ScnVictFacilityObj);
    io(r.SolarFlaresEvent);
    io(r.GameMoreRules);
    io(r.CurrentFaction);
    io(r.ClimateLevel);
}

template <class Io>
void record_io(Io& io, Faction& r) {
    io(r.player_flags);
    io(r.ranking);
    io(r.diff_level);
    io(r.base_name_offset);
    io(r.base_sea_name_offset);
    io(r.last_base_turn);
    io(r.diplo_status);
    io(r.diplo_agenda);
    io(r.diplo_friction);
    io(r.diplo_spoke);
    io(r.diplo_merc);
    io(r.diplo_patience);
    io(r.sanction_turns);
    io(r.loan_balance);
    io(r.loan_payment);
    io(r.unk_1);
    io(r.integrity_blemishes);
    io(r.diplo_reputation);
    io(r.diplo_gifts);
    io(r.diplo_wrongs);
    io(r.diplo_betrayed);
    io(r.diplo_combat_total);
    io(r.diplo_combat_count);
    io(r.traded_maps);
    io(r.base_governor_adv);
    io(r.atrocities);
    io(r.major_atrocities);
    io(r.mind_control_total);
    io(r.diplo_mind_control);
    io(r.diplo_stolen_techs);
    io(r.energy_credits);
    io(r.hurry_cost_total);
    io(r.SE_Politics_pending);
    io(r.SE_Economics_pending);
    io(r.SE_Values_pending);
    io(r.SE_Future_pending);
    io(r.SE_Politics);
    io(r.SE_Economics);
    io(r.SE_Values);
    io(r.SE_Future);
    io(r.SE_upheaval_cost_paid);
    io(r.SE_economy_pending);
    io(r.SE_effic_pending);
    io(r.SE_support_pending);
    io(r.SE_talent_pending);
    io(r.SE_morale_pending);
    io(r.SE_police_pending);
    io(r.SE_growth_pending);
    io(r.SE_planet_pending);
    io(r.SE_probe_pending);
    io(r.SE_industry_pending);
    io(r.SE_research_pending);
    io(r.SE_economy);
    io(r.SE_effic);
    io(r.SE_support);
    io(r.SE_talent);
    io(r.SE_morale);
    io(r.SE_police);
    io(r.SE_growth);
    io(r.SE_planet);
    io(r.SE_probe);
    io(r.SE_industry);
    io(r.SE_research);
    io(r.SE_economy_2);
    io(r.SE_effic_2);
    io(r.SE_support_2);
    io(r.SE_talent_2);
    io(r.SE_morale_2);
    io(r.SE_police_2);
    io(r.SE_growth_2);
    io(r.SE_planet_2);
    io(r.SE_probe_2);
    io(r.SE_industry_2);
    io(r.SE_research_2);
    io(r.SE_economy_base);
    io(r.SE_effic_base);
    io(r.SE_support_base);
    io(r.SE_talent_base);
    io(r.SE_morale_base);
    io(r.SE_police_base);
    io(r.SE_growth_base);
    io(r.SE_planet_base);
    io(r.SE_probe_base);
    io(r.SE_industry_base);
    io(r.SE_research_base);
    io(r.energy_surplus_total);
    io(r.facility_maint_total);
    io(r.tech_commerce_bonus);
    io(r.turn_commerce_income);
    io(r.unk_17);
    io(r.unk_18);
    io(r.tech_fungus_nutrient);
    io(r.tech_fungus_mineral);
    io(r.tech_fungus_energy);
    io(r.tech_fungus_unused);
    io(r.SE_alloc_psych);
    io(r.SE_alloc_labs);
    io(r.tech_pact_shared_goals);
    io(r.tech_ranking);
    io(r.unk_27);
    io(r.ODP_deployed);
    io(r.tech_count_transcendent);
    io(r.tech_trade_source);
    io(r.tech_accumulated);
    io(r.tech_research_id);
    io(r.tech_cost);
    io(r.earned_techs_saved);
    io(r.net_random_event);
    io(r.AI_fight);
    io(r.AI_growth);
    io(r.AI_tech);
    io(r.AI_wealth);
    io(r.AI_power);
    io(r.target_x);
    io(r.target_y);
    io(r.best_mineral_output);
    io(r.council_call_turn);
    io(r.unk_29);
    io(r.unk_30);
    io(r.facility_announced);
    io(r.clean_minerals_modifier);
    io(r.base_id_attack_target);
    io(r.unk_37);
    io(r.saved_queue_name);
    io(r.saved_queue_size);
    io(r.saved_queue_items);
    io(r.social_support);
    io(r.social_psych);
    io(r.social_effic);
    io(r.unk_45);
    io(r.unk_46);
    io(r.unk_47);
    io(r.nutrient_surplus_total);
    io(r.labs_total);
    io(r.satellites_nutrient);
    io(r.satellites_mineral);
    io(r.satellites_energy);
    io(r.satellites_ODP);
    io(r.best_weapon_value);
    io(r.best_psi_land_offense);
    io(r.best_psi_land_defense);
    io(r.best_armor_value);
    io(r.best_land_speed);
    io(r.enemy_best_weapon_value);
    io(r.enemy_best_armor_value);
    io(r.enemy_best_land_speed);
    io(r.enemy_best_psi_land_offense);
    io(r.enemy_best_psi_land_defense);
    io(r.unk_64);
    io(r.unk_65);
    io(r.unk_66);
    io(r.unk_67);
    io(r.unk_68);
    io(r.unk_69);
    io(r.units_active);
    io(r.units_queue);
    io(r.units_lost);
    io(r.total_combat_units);
    io(r.base_count);
    io(r.mil_strength_1);
    io(r.mil_strength_2);
    io(r.pop_total);
    io(r.unk_70);
    io(r.planet_busters);
    io(r.unk_71);
    io(r.unk_72);
    io(r.region_total_combat_units);
    io(r.region_total_bases);
    io(r.region_total_offensive_units);
    io(r.region_force_rating);
    io(r.region_flags);
    io(r.region_territory_tiles);
    io(r.region_visible_tiles);
    io(r.region_good_tiles);
    io(r.region_unk_5);
    io(r.region_unk_6);
    io(r.region_territory_pods);
    io(r.region_base_plan);
    io(r.goals);
    io(r.sites);
    io(r.unk_93);
    io(r.goody_opened);
    io(r.goody_artifact);
    io(r.goody_earthquake);
    io(r.goody_tech);
    io(r.tech_achieved);
    io(r.time_bonus_count);
    io(r.unk_99);
    io(r.secret_project_intel);
    io(r.corner_market_turn);
    io(r.corner_market_cost);
    io(r.unk_101);
    io(r.unk_102);
    io(r.bunglist_next);
    io(r.player_flags_ext);
    io(r.unk_105);
    io(r.unk_106);
    io(r.unk_107);
    io(r.eliminated_count);
    io(r.unk_109);
    io(r.unk_110);
    io(r.unk_111);
    io(r.unk_112);
    io(r.unk_113);
    io(r.unk_114);
    io(r.unk_115);
    io(r.unk_116);
    io(r.unk_117);
    io(r.unk_118);
}

template <class Io>
void record_io(Io& io, Goal& r) {
    io(r.type);
    io(r.priority);
    io(r.x);
    io(r.y);
    io(r.base_id);
}

template <class Io>
void record_io(Io& io, MFaction& r) {
    io(r.is_leader_female);
    io(r.filename);
    io(r.search_key);
    io(r.name_leader);
    io(r.title_leader);
    io(r.adj_leader);
    io(r.adj_insult_leader);
    io(r.adj_faction);
    io(r.adj_insult_faction);
    io(r.thinker_unused);
    io(r.thinker_probe_lost);
    io(r.thinker_probe_renew);
    io(r.thinker_last_mc_turn);
    io(r.thinker_probe_end_turn);
    io(r.pad_1);
    io(r.noun_faction);
    io(r.noun_gender);
    io(r.is_noun_plural);
    io(r.adj_name_faction);
    io(r.formal_name_faction);
    io(r.insult_leader);
    io(r.desc_name_faction);
    io(r.assistant_name);
    io(r.scientist_name);
    io(r.assistant_city);
    io(r.pad_2);
    io(r.rule_tech_selected);
    io(r.rule_morale);
    io(r.rule_research);
    io(r.rule_drone);
    io(r.rule_talent);
    io(r.rule_energy);
    io(r.rule_interest);
    io(r.rule_population);
    io(r.rule_hurry);
    io(r.rule_techcost);
    io(r.rule_psi);
    io(r.rule_sharetech);
    io(r.rule_commerce);
    io(r.rule_flags);
    io(r.faction_bonus_count);
    io(r.faction_bonus_id);
    io(r.faction_bonus_val1);
    io(r.faction_bonus_val2);
    io(r.AI_fight);
    io(r.AI_growth);
    io(r.AI_tech);
    io(r.AI_wealth);
    io(r.AI_power);
    io(r.soc_priority_category);
    io(r.soc_opposition_category);
    io(r.soc_priority_model);
    io(r.soc_opposition_model);
    io(r.soc_priority_effect);
    io(r.soc_opposition_effect);
}

template <class Io>
void record_io(Io& io, MAP& r) {
    io(r.climate);
    io(r.contour);
    io(r.val2);
    io(r.region);
    io(r.visibility);
    io(r.val3);
    io(r.unk_1);
    io(r.owner);
    io(r.items);
    io(r.landmarks);
    io(r.visible_items);
}

template <class Io>
void record_io(Io& io, Landmark& r) {
    io(r.x);
    io(r.y);
    io(r.name);
}

template <class Io>
void record_io(Io& io, Continent& r) {
    io(r.tile_count);
    io(r.open_terrain);
    io(r.world_site);
    io(r.pods);
    io(r.unk_5);
    io(r.sea_coasts);
}

template <class Io>
void record_io(Io& io, UNIT& r) {
    io(r.name);
    io(r.ability_flags);
    io(r.chassis_id);
    io(r.weapon_id);
    io(r.armor_id);
    io(r.reactor_id);
    io(r.carry_capacity);
    io(r.cost);
    io(r.plan);
    io(r.group_id);
    io(r.obsolete_factions);
    io(r.combat_factions);
    io(r.icon_offset);
    io(r.pad_1);
    io(r.unit_flags);
    io(r.preq_tech);
}

template <class Io>
void record_io(Io& io, VEH& r) {
    io(r.x);
    io(r.y);
    io(r.state);
    io(r.flags);
    io(r.unit_id);
    io(r.pad_0);
    io(r.faction_id);
    io(r.year_end_lurking);
    io(r.damage_taken);
    io(r.order);
    io(r.waypoint_count);
    io(r.patrol_current_point);
    io(r.waypoint_x);
    io(r.waypoint_y);
    io(r.morale);
    io(r.movement_turns);
    io(r.order_auto_type);
    io(r.visibility);
    io(r.moves_spent);
    io(r.rotate_angle);
    io(r.iter_count);
    io(r.status_icon);
    io(r.probe_action);
    io(r.probe_sabotage_id);
    io(r.home_base_id);
    io(r.next_veh_id_stack);
    io(r.prev_veh_id_stack);
}

template <class Io>
void record_io(Io& io, BASE& r) {
    io(r.x);
    io(r.y);
    io(r.faction_id);
    io(r.faction_id_former);
    io(r.pop_size);
    io(r.assimilation_turns_left);
    io(r.nerve_staple_turns_left);
    io(r.ai_plan_status);
    io(r.visibility);
    io(r.factions_pop_size_intel);
    io(r.name);
    io(r.unk_x);
    io(r.unk_y);
    io(r.state_flags);
    io(r.event_flags);
    io(r.governor_flags);
    io(r.nutrients_accumulated);
    io(r.minerals_accumulated);
    io(r.production_id_last);
    io(r.eco_damage);
    io(r.queue_size);
    io(r.queue_items);
    io(r.worked_tiles);
    io(r.specialist_total);
    io(r.specialist_adjust);
    io(r.specialist_types);
    io(r.facilities_built);
    io(r.mineral_surplus_final);
    io(r.minerals_accumulated_2);
    io(r.pad_1);
    io(r.pad_2);
    io(r.pad_3);
    io(r.pad_4);
    io(r.nutrient_intake);
    io(r.mineral_intake);
    io(r.energy_intake);
    io(r.unused_intake);
    io(r.nutrient_intake_2);
    io(r.mineral_intake_2);
    io(r.energy_intake_2);
    io(r.unused_intake_2);
    io(r.nutrient_surplus);
    io(r.mineral_surplus);
    io(r.energy_surplus);
    io(r.unused_surplus);
    io(r.nutrient_inefficiency);
    io(r.mineral_inefficiency);
    io(r.energy_inefficiency);
    io(r.unused_inefficiency);
    io(r.nutrient_consumption);
    io(r.mineral_consumption);
    io(r.energy_consumption);
    io(r.unused_consumption);
    io(r.economy_total);
    io(r.psych_total);
    io(r.labs_total);
    io(r.unk_total);
    io(r.autoforward_land_base_id);
    io(r.autoforward_sea_base_id);
    io(r.autoforward_air_base_id);
    io(r.pad_5);
    io(r.talent_total);
    io(r.drone_total);
    io(r.superdrone_total);
    io(r.random_event_turns);
    io(r.nerve_staple_count);
    io(r.defend_goal);
    io(r.defend_range);
    io(r.pad_6);
    io(r.pad_7);
    io(r.pad_8);
}

template <class Io>
void record_io(Io& io, ReplayEvent& r) {
    io(r.event);
    io(r.faction_id);
    io(r.turn);
    io(r.x);
    io(r.y);
}

template <class Io>
void record_io(Io& io, MonumentEvent& r) {
    io(r.year);
    io(r.param1);
    io(r.param2);
    io(r.faction_id);
    io(r.achieved);
    io(r.prev_count);
    io(r.field_18);
    io(r.field_1C);
    io(r.field_20);
    io(r.field_24);
    io(r.field_28);
    io(r.field_2C);
    io(r.field_30);
    io(r.field_34);
}

template <class Io>
void record_io(Io& io, Monument& r) {
    io(r.events);
    io(r.latest_event_id);
    io(r.field_49C);
    io(r.field_4A0);
    io(r.field_4A4);
    io(r.field_4A8);
    io(r.base_name);
    io(r.facility_name);
    io(r.enemy_base_name);
}

template <class Io>
void record_io(Io& io, MapHeader& r) {
    io(r.area_x);
    io(r.area_y);
    io(r.random_seed);
    io(r.sea_level);
    io(r.sea_level_council);
    io(r.area_tiles);
    io(r.area_sq_root);
    io(r.toggle_flat);
    io(r.landmark_count);
    io(r.landmarks);
    io(r.abstract_x);
    io(r.abstract_y);
    io(r.abstract_count);
    io(r.size_planet);
    io(r.ocean_coverage);
    io(r.land_coverage);
    io(r.erosive_forces);
    io(r.planetary_orbit);
    io(r.cloud_cover);
    io(r.native_life_forms);
    io(r.file_path);
    io(r.tiles_slot);
    io(r.abstract_slot);
}

template <class Io>
void record_io(Io& io, ScenarioRules& r) {
    io(r.req_victory);
    io(r.sudden_death_victory);
    io(r.achieve_pts);
    io(r.victory_achieve_bonus_pts);
    io(r.starting_mission_year);
    io(r.ending_mission_year);
    io(r.unk_18);
}

template <class Io>
void record_io(Io& io, PbemState& r) {
    io(r.game_id);
    io(r.game_date);
    io(r.save_count);
    io(r.tamper_faction);
    io(r.tamper_reloads);
}

}  // namespace

#define SMAC_RECORD(T)                                     \
    void serialize(Writer& io, T& r) { record_io(io, r); } \
    void serialize(Reader& io, T& r) { record_io(io, r); }
SMAC_RECORD(GamePrefs)
SMAC_RECORD(Faction)
SMAC_RECORD(Goal)
SMAC_RECORD(MFaction)
SMAC_RECORD(MapHeader)
SMAC_RECORD(Landmark)
SMAC_RECORD(MAP)
SMAC_RECORD(Continent)
SMAC_RECORD(UNIT)
SMAC_RECORD(VEH)
SMAC_RECORD(BASE)
SMAC_RECORD(ReplayEvent)
SMAC_RECORD(MonumentEvent)
SMAC_RECORD(Monument)
SMAC_RECORD(ScenarioRules)
SMAC_RECORD(PbemState)
#undef SMAC_RECORD

// ---- .state snapshots ----

namespace {

// The serialisation of each region of harness/state_regions.h, by its name there.
void region_GamePrefs(Writer& w) { w(cs); }
void region_Factions(Writer& w) { w(Factions); }
void region_MFactions(Writer& w) { w(MFactions); }
void region_FactionRankingsUnk(Writer& w) { w(FactionRankingsUnk); }
void region_MapHeader(Writer& w) { w(MapHeaderBlock); }
void region_FactionTurnMight(Writer& w) { w(FactionTurnMight); }
void region_ReplayEventSize(Writer& w) { w(ReplayEventSize); }
void region_Continents(Writer& w) { w(Continents); }
void region_Units(Writer& w) { w(Units); }
void region_UnitsExtra94B558(Writer& w) { w(BungList); }
void region_ObjectiveReqVictory(Writer& w) { w(ScenarioRulesBlock); }
void region_Monuments(Writer& w) { w(Monuments); }
void region_TectonicDetonationCount(Writer& w) { w(TectonicDetonationCount); }
void region_DiploStateC(Writer& w) { w(DiploStateC); }
void region_DiploStateB(Writer& w) { w(DiploStateB); }
void region_FactionCombatWin(Writer& w) { w(FactionCombatWin); }
void region_FactionCombatLoss(Writer& w) { w(FactionCombatLoss); }
void region_CouncilWinState(Writer& w) { w(CouncilWinState); }
void region_CouncilWinVotes(Writer& w) {
    w(CouncilWinVotesA);
    w(CouncilWinVotesB);
}
void region_CouncilSessionPending(Writer& w) { w(CouncilSessionPending); }
void region_CouncilProposal(Writer& w) { w(CouncilProposal); }
void region_CouncilVoteState(Writer& w) { w(CouncilVoteState); }
void region_PbemState(Writer& w) { w(PbemStateBlock); }

// Variable regions: the first count records (count from the region's count global, clamped to the
// table), or nothing when the heap array does not exist.
template <class T>
Writer region_records(const T* first, int32_t count, size_t capacity) {
    if (!first || count <= 0) return Writer{};
    return write_records(first, std::min(static_cast<size_t>(count), capacity));
}
Writer region_MapTiles() { return region_records(MapTiles, MapAreaTiles, static_cast<size_t>(MapAreaTiles)); }
Writer region_MapAbstractData() {
    return region_records(MapAbstract, MapAbstractCount, static_cast<size_t>(MapAbstractCount));
}
Writer region_ReplayEvents() {  // ReplayEventSize bytes: the records, and past the table what follows it
    const uint32_t size = static_cast<uint32_t>(std::max(ReplayEventSize, 0));
    Writer w = write_records(ReplayEvents, 1024);
    w.bytes.resize(size);
    if (size > sizeof(ReplayEvents))
        mem::read_bytes(kReplayEventsEnd, &w.bytes[sizeof(ReplayEvents)],
                        size - static_cast<uint32_t>(sizeof(ReplayEvents)));
    return w;
}
Writer region_Vehs() { return region_records(Vehs, VehCount, 2049); }
Writer region_Bases() { return region_records(Bases, BaseCount, 512); }

[[noreturn]] void region_size_error(const char* name) {
    std::fprintf(stderr, "state_regions_write: region %s does not serialise to its size\n", name);
    std::abort();
}

}  // namespace

void state_regions_write(const StateRegionSink& put) {
#define REGION(name, addr, bytes)                          \
    {                                                      \
        Writer w;                                          \
        region_##name(w);                                  \
        if (w.size() != (bytes)) region_size_error(#name); \
        put(addr, w.data(), w.size());                     \
    }
#define REGION_VAR(name, addr, count_addr, elem) \
    {                                            \
        Writer w = region_##name();              \
        put(addr, w.data(), w.size());           \
    }
#define MASK(addr, size)
#include "../../harness/state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
}
