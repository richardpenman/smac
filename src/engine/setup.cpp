// New game setup (original module around 0x5AE120-0x5B4210).
#include "engine/setup.h"

#include <cstdlib>
#include <cstring>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "platform/win32.h"

// ---- helpers (not original functions) -----------------------------------------------------------

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (1 << faction_id)) != 0; }
static inline bool is_alien(int32_t faction_id) { return (game().mfactions[faction_id].rule_flags & RFLAG_ALIEN) != 0; }
static inline bool is_aquatic(int32_t faction_id) {
    return (game().mfactions[faction_id].rule_flags & RFLAG_AQUATIC) != 0;
}

// The faction's file name upper-cased in StrBuffer, as the original builds it before comparing it
// with a faction key ("FUNGBOY", "ANGELS", ...). Returns StrBuffer.
static char* faction_key_upper(int32_t faction_id) {
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, game().mfactions[faction_id].filename);
    win32::CharUpperA(StrBuffer);
    return StrBuffer;
}

// setup_player: a name already used by a lower faction gets "-<n>" appended (cut to `cut` chars).
template <size_t N>
static void unique_faction_name(int32_t faction_id, char (MFaction::*field)[N], int32_t cut) {
    if (faction_id <= 1) return;
    char* name = game().mfactions[faction_id].*field;
    int32_t dupes = 0;
    for (int32_t i = 1; i < faction_id; i++) {
        if (!crt_strcmp(game().mfactions[i].*field, name)) dupes++;
    }
    if (!dupes) return;
    char num[80];
    name[cut] = 0;
    crt_strcat(name, "-");
    crt_itoa(dupes + 1, num, 10);
    crt_strcat(name, num);
}

// The setup_player argument for factions arriving in the course of the game (turn_upkeep).
constexpr int32_t kSetupArrival = -282;

// ---- setup_player -------------------------------------------------------------------------------

int32_t GAMEAPI setup_player(int32_t faction_id, int32_t killer_id, int32_t force) {
    const int32_t f = faction_id;
    Faction& F = game().factions[f];
    MFaction& M = game().mfactions[f];
    bool gave_up = false;

    unique_faction_name(f, &MFaction::name_leader, 21);
    unique_faction_name(f, &MFaction::noun_faction, 21);
    unique_faction_name(f, &MFaction::formal_name_faction, 37);
    for (int32_t i = game().base_count - 1; i >= 0; i--) {
        if (game().bases[i].faction_id == f) base_kill(i);
    }
    for (int32_t i = game().veh_count - 1; i >= 0; i--) {
        if (i < game().veh_count && game().vehs[i].faction_id == f &&
            (game().vehs[i].unit_id != BSC_FUNGAL_TOWER || f != 0))
            kill(i);
    }
    F.target_y = -1;
    F.target_x = -1;
    F.base_id_attack_target = -1;
    if (CurrentTurn == 0 || killer_id == kSetupArrival) {
        init_goals(f);
        clear_bunglist(f);
        F.player_flags = 0;
        F.base_governor_adv = 0xDFBE41;
        if (DiffLevel == 0) F.base_governor_adv = (int32_t)0xC0DFBE41;
        F.base_name_offset = 0;
        F.base_sea_name_offset = 0;
        F.AI_fight = M.AI_fight;
        F.AI_power = M.AI_power;
        F.AI_tech = M.AI_tech;
        F.AI_wealth = M.AI_wealth;
        F.AI_growth = M.AI_growth;
        F.unk_37 = 0;
        for (int32_t i = 0; i < 8; i++) {
            char num[80];
            F.saved_queue_size[i] = 0;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, label_get(609));
            crt_strcat(StrBuffer, " ");
            crt_itoa(i + 1, num, 10);
            crt_strcat(StrBuffer, num);
            crt_strcpy(F.saved_queue_name[i], StrBuffer);
        }
        for (int32_t i = 0; i < 4; i++) {
            (&F.SE_Politics)[i] = 0;
            (&F.SE_Politics_pending)[i] = 0;
        }
        for (int32_t i = 0; i < 11; i++) (&F.SE_economy_pending)[i] = 0;
        F.SE_upheaval_cost_paid = 0;
        for (int32_t i = 0; i < 8; i++) F.secret_project_intel[i] = 0;
        F.diff_level = is_human(f) ? DiffLevel : 3;
        F.integrity_blemishes = 0;
        F.diplo_reputation = 0;
        F.traded_maps = 0;
        F.energy_credits = 0;
        F.atrocities = 0;
        F.major_atrocities = 0;
        F.clean_minerals_modifier = 0;
        F.sanction_turns = 0;
        TectonicDetonationCount[f] = 0;
        F.council_call_turn = -999;
        if (GameState & STATE_RAND_FAC_LEADER_PERSONALITIES) {
            F.AI_fight = game_rand() % 3 - 1;
            F.AI_power = game_rand() % 2;
            F.AI_tech = game_rand() % 2;
            F.AI_wealth = game_rand() % 2;
            F.AI_growth = game_rand() % 2;
        }
        if (GameRules & RULES_INTENSE_RIVALRY) F.AI_fight = 1;
        F.total_combat_units = 0;
        F.base_count = 0;
        F.mil_strength_1 = 0;
        F.mil_strength_2 = 0;
        F.pop_total = 0;
        F.unk_70 = 0;
        F.mind_control_total = 0;
        for (int32_t i = 0; i < 8; i++) {
            F.diplo_mind_control[i] = 0;
            F.diplo_stolen_techs[i] = 0;
        }
        F.net_random_event = 0;
        for (int32_t i = 0; i < 128; i++) {
            F.region_total_combat_units[i] = 0;
            F.region_total_bases[i] = 0;
            F.region_total_offensive_units[i] = 0;
            F.region_force_rating[i] = 0;
            F.region_flags[i] = 0;
        }
        for (int32_t i = 0; i < 11; i++) {
            F.unk_29[i] = 0;
            F.unk_30[i] = 0;
        }
        // corner_market_turn .. eliminated_count
        F.corner_market_turn = 0;
        F.corner_market_cost = 0;
        F.unk_101 = 0;
        F.unk_102 = 0;
        F.bunglist_next = 0;
        F.player_flags_ext = 0;
        F.unk_105 = 0;
        F.unk_106 = 0;
        F.unk_107 = 0;
        F.eliminated_count = 0;
        F.unk_93 = 0;
        F.goody_opened = 0;
        F.goody_artifact = 0;
        F.goody_earthquake = 0;
        F.goody_tech = 0;
        F.tech_achieved = 0;
        F.time_bonus_count = 1;
    }
    F.energy_credits += M.rule_energy + (MultiplayerActive ? 100 : Rules.starting_energy_reserve);
    F.hurry_cost_total = 0;
    F.tech_cost = -1;
    F.last_base_turn = CurrentTurn;
    for (int32_t j = 0; j < 8; j++) {
        if (CurrentTurn == 0 || killer_id == kSetupArrival) {
            F.diplo_status[j] = 0;
            F.diplo_merc[j] = 0;
            int32_t n = ((GameRules & RULES_INTENSE_RIVALRY) ? 5 : DiffLevel) * 3 + 8;
            int32_t friction = 1;
            if (n - 1 > 0) {
                int32_t r = game_rand() % n;
                if (r >= 1) friction = r > 16 ? 16 : r;
            }
            F.diplo_friction[j] = friction;
            F.diplo_gifts[j] = 0;
            F.diplo_wrongs[j] = 0;
            F.diplo_betrayed[j] = 0;
            F.diplo_patience[j] = 0;
            F.diplo_spoke[j] = -1;
            F.diplo_combat_count[j] = 0;
            F.diplo_combat_total[j] = 0;
        } else {
            treaty_off(f, j, 0x9000FF48);
        }
        F.unk_1[j] = 0;
        F.loan_balance[j] = 0;
        F.loan_payment[j] = 0;
    }
    for (int32_t i = 0; i < 512; i++) {
        F.units_active[i] = 0;
        F.units_queue[i] = 0;
        if (CurrentTurn == 0) F.units_lost[i] = 0;
    }
    if (CurrentTurn == 0 || killer_id == kSetupArrival) {
        for (int32_t i = 0; i < 12; i++) F.tech_pact_shared_goals[i] = 0;
        for (int32_t i = 0; i < 89; i++) F.tech_trade_source[i] = 0;
        for (int32_t i = 0; i < 9; i++) F.facility_announced[i] = 0;
        F.satellites_nutrient = 0;
        F.satellites_mineral = 0;
        F.satellites_energy = 0;
        F.satellites_ODP = 0;
        F.tech_count_transcendent = 0;
        F.tech_ranking = 0;
        F.ODP_deployed = 0;
        F.tech_research_id = -1;
        F.tech_accumulated = 0;
        F.earned_techs_saved = 0;
        F.unk_27 = 0;
        F.unk_102 = 0;
    }
    F.best_mineral_output = 0;
    F.energy_surplus_total = 0;
    F.facility_maint_total = 0;
    F.unk_18 = 0;
    F.unk_17 = 0;
    F.tech_commerce_bonus = 0;
    F.tech_fungus_nutrient = 0;
    F.tech_fungus_mineral = 0;
    F.tech_fungus_energy = 0;
    F.tech_fungus_unused = 0;
    F.turn_commerce_income = 0;
    F.best_weapon_value = 0;
    F.best_armor_value = 0;
    F.best_land_speed = 0;
    F.best_psi_land_offense = 0;
    F.best_psi_land_defense = 0;
    F.enemy_best_weapon_value = 0;
    F.enemy_best_armor_value = 0;
    F.enemy_best_land_speed = 0;
    F.enemy_best_psi_land_offense = 0;
    F.enemy_best_psi_land_defense = 0;
    if (f == 0) return 0;
    const uint8_t bit = (uint8_t)(1 << f);
    if (!(FactionStatus[1] & bit)) return 0;
    if (killer_id < -1 && killer_id != kSetupArrival) {
        FactionStatus[1] &= (uint8_t)~bit;
        return 0;
    }

    // ---- landing site: random draws, the area shrinking towards the map centre ----
    int32_t x = 0, y = 0, region = 0, others = 0, near_base = -1, attempts = 0;
    for (;;) {
        const int32_t flat = game().map.flat & 1;
        int32_t margin_x = clamp((0x400 - attempts) * game().map.x / 8192, 0, 9999);
        x = map_rand_mod(game().map.x - (flat ? margin_x * 2 : 0)) + (flat ? margin_x : 0);
        int32_t margin_y = clamp((0x400 - attempts) * game().map.y / 8192, 0, 9999);
        y = map_rand_mod(game().map.y - margin_y * 2) + margin_y;
        if (x & 1) x--;
        if (y & 1) x++;
        // Nearest ring (0..7) with a landmark tile, 32 if none.
        int32_t landmark_ring = 32;
        for (int32_t r = 0; r < 8; r++) {
            for (int32_t i = r ? TableRange[r - 1] : 0; i < TableRange[r]; i++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                int32_t y2 = y + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                uint32_t landmarks = map_tile(x2, y2)->landmarks;
                if (landmarks && (int32_t)(landmarks & 0xFF000000) > 0) {
                    landmark_ring = r;
                    r = 99;
                    break;
                }
            }
        }
        // Quality of the 21 tiles around.
        int32_t quality = 0;
        for (int32_t i = 0; i < 21; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            MAP* sq = map_tile(x2, y2);
            uint32_t alt = sq->climate & 0xE0;
            if (is_aquatic(f)) {
                if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
                quality += (2 - (sq->climate >> 5)) * 2 + 1;
            } else {
                if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
                if (alt < 0x60 || (sq->val3 & 0xC0) > 0x40 || !(sq->climate & 0x18)) continue;
                quality++;
            }
        }
        bool aquatic = is_aquatic(f);
        if (aquatic) quality /= 3;
        region = map_tile(x, y)->region;
        others = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (game().factions[i].region_total_bases[region]) others |= 1 << i;
        }
        if (!aquatic && killer_id != kSetupArrival)
            near_base = base_find_filtered(x, y, -1, region, -1, -1);
        else
            near_base = base_find(x, y);
        if (near_base < 0) BaseFindDist = 32;
        if (CurrentTurn == 0 || killer_id == kSetupArrival) {
            for (int32_t v = 0; v < game().veh_count; v++) {
                if (map_tile(game().vehs[v].x, game().vehs[v].y)->region != region) continue;
                others |= 1 << game().vehs[v].faction_id;
                int32_t dist = veh_vector_dist(x, y, game().vehs[v].x, game().vehs[v].y);
                if (dist < BaseFindDist) BaseFindDist = dist;
            }
        }
        attempts++;
        if (attempts > 0x400 && !(is_human(f) && CurrentTurn == 0)) {
            gave_up = true;
            break;
        }
        aquatic = is_aquatic(f);
        MAP* sq = map_tile(x, y);
        uint32_t alt = sq->climate & 0xE0;
        if (aquatic ? alt > 0x40 : alt < 0x60) continue;
        if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
        if (!aquatic && (sq->val3 & 0xC0) > 0x40) continue;
        int32_t spacing = (CurrentTurn == 0 || killer_id == kSetupArrival) ? 6 : 12;
        if (BaseFindDist < spacing * game().map.sq_root / 56) continue;
        if (alt >= 0xA0) continue;
        if ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8) continue;
        if (y < 2 || y >= game().map.y - 2) continue;
        if (quality < 12 - attempts / 80) continue;
        int32_t relax = attempts / 64;
        if (BaseFindDist < (16 - relax) * game().map.sq_root / 56) continue;
        if ((CurrentTurn == 0 || killer_id == kSetupArrival) && attempts < 900 &&
            Continents[region].tile_count < 36 - relax)
            continue;
        if (!aquatic) {
            int32_t need = is_human(f) ? (6 - DiffLevel) * 8 : 8;
            if (Continents[region].open_terrain * (MapOceanCoverage + 1) / 2 <
                -(attempts / 16) - CurrentTurn / 16 + need + 24)
                continue;
        }
        int32_t diff = is_human(f) ? DiffLevel : 3;
        if (Continents[region].tile_count < (5 - diff) * 48 - CurrentTurn / 16 - attempts / 4) continue;
        if (others && killer_id != kSetupArrival &&
            Continents[region].tile_count / (bit_count(others) + 1) < 64 - relax)
            continue;
        int32_t site = 0;
        if (!(sq->items & BIT_BASE_RADIUS)) {
            site = sq->val2 >> 4;
            if (!site) {
                site = world_site(x, y, 0);
                site_set(x, y, site);
            }
        }
        if (site < 15 - relax && CurrentTurn == 0 && !(GameRules & RULES_LOOK_FIRST)) continue;
        if (!is_aquatic(f) && (landmark_ring < (0x400 - attempts) / 128 || landmark_ring == 0)) continue;
        if (CurrentTurn != 0 && killer_id != kSetupArrival && sq->owner > 0) continue;
        if (near_base >= 0 && game().bases[near_base].faction_id == killer_id &&
            map_tile(game().bases[near_base].x, game().bases[near_base].y)->region == region)
            continue;
        break;
    }
    if (F.player_flags & PFLAG_UNK_1000) gave_up = true;
    if (!gave_up || force) {
        if (CurrentTurn == 0 || killer_id == kSetupArrival) {
            // Best tile within radius 2 of the same region (site value plus coast).
            int32_t best = -1, best_i = 0;
            for (int32_t i = 0; i < 21; i++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                int32_t y2 = y + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* sq = map_tile(x2, y2);
                uint32_t alt = sq->climate & 0xE0;
                if ((alt < 0x60) != is_aquatic(f) || sq->region != region || alt >= 0xA0) continue;
                if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
                if ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8) continue;
                base_find(x2, y2);
                for (int32_t v = 0; v < game().veh_count; v++) {
                    // BUG(original): the distance is measured from (x, y), not from the candidate.
                    COMPAT_ORIGINAL(setup_site_distance_origin);
                    int32_t dist = veh_vector_dist(x, y, game().vehs[v].x, game().vehs[v].y);
                    if (dist < BaseFindDist) BaseFindDist = dist;
                }
                if (BaseFindDist < 6) continue;
                int32_t site = 0;
                if (!(sq->items & BIT_BASE_RADIUS)) {
                    site = sq->val2 >> 4;
                    if (!site) {
                        site = world_site(x2, y2, 0);
                        site_set(x2, y2, site);
                    }
                }
                for (int32_t k = 0; k < 8; k++) {
                    int32_t x3 = map_wrap_x(x2 + NearOffsetX[k]);
                    int32_t y3 = y2 + NearOffsetY[k];
                    if (!map_in_bounds(x3, y3)) continue;
                    if (is_aquatic(f)) {
                        site += 2 - (map_tile(x3, y3)->climate >> 5);
                    } else if (map_is_ocean(x3, y3)) {
                        site++;
                        break;
                    }
                }
                if (site > best) {
                    best = site;
                    best_i = i;
                }
            }
            x = map_wrap_x(x + TableOffsetX[best_i]);
            y += TableOffsetY[best_i];
        }
        if (force) gave_up = false;
    }
    bool spawn = force != 0;
    if (!gave_up && !(CurrentTurn != 0 && killer_id != kSetupArrival && (GameRules & RULES_DO_OR_DIE)) &&
        CurrentTurn <= 100)
        spawn = true;

    if (!spawn) {
        // ---- eliminated ----
        if (is_human(f) && !MultiplayerActive) {
            GameVictoryType = 7;
            FactionStatus[1] &= (uint8_t)~bit;
            GameState |= STATE_GAME_DONE;
            return 0;
        }
        F.unk_102 = 0;
        FactionStatus[1] &= (uint8_t)~bit;
        int32_t leader = cs.FactionRankings[7];
        if (f == leader) leader = cs.FactionRankings[6];
        int32_t alive = 0, allies = 0;
        uint8_t alive_mask = FactionStatus[1];
        for (int32_t i = 1; i < 8; i++) {
            if (!(alive_mask & (1 << i))) continue;
            alive++;
            uint32_t to_viewer = game().factions[i].diplo_status[CurrentPlayerFaction];
            if (!MultiplayerActive && (to_viewer & DIPLO_PACT) && (to_viewer & 0x2000000)) {
                alive--;
            } else if (i != leader && (game().factions[i].diplo_status[leader] & DIPLO_PACT) &&
                       (GameRules & RULES_VICTORY_COOPERATIVE) && allies < 2) {
                allies++;
            }
        }
        if (f == CurrentPlayerFaction) {
            int32_t k = killer_id < 0 ? 0 : killer_id;
            GenderDefault = game().mfactions[k].noun_gender;
            PluralDefault = game().mfactions[k].is_noun_plural;
            parse_says(0, game().mfactions[k].noun_faction, -1, -1);
            if (MultiplayerActive) {
                NetDaemon_hang_up(&NetState);
                static const uint8_t kCleared[] = {0,  1,  2,  3,  12, 11, 15, 14, 19, 30, 28, 29, 39, 38,
                                                   8,  9,  10, 37, 36, 32, 31, 33, 4,  5,  42, 41, 6,  7};
                for (uint8_t i : kCleared) NetSessionState[i] = 0;
                NetSessionState[40] = 0xFF;
                NetSessionState[18] = 0;
                for (int32_t i = 20; i < 28; i++) NetSessionState[i] = 0;
                Lock_clear(&NetLock);
                AlphaNet_close(&NetState);
                MultiplayerActive = 0;
                ControlTurnA = 1;
            }
            X_pop(killer_id == 0 && GameLanguage == 0 ? "YOULOSE2" : "YOULOSE", 0);
            if (f == CurrentPlayerFaction) {
                GameState |= STATE_GAME_DONE;
                GameVictoryType = killer_id ? 7 : 15;
                return 0;
            }
        }
        if (alive - allies < 2) {
            if (allies) {
                popp(ScriptFileName, "CONQCOOP", 0, "conq_sm.pcx", 0);
                if (!GameVictoryType) GameVictoryType = 5;
            } else if (leader == CurrentPlayerFaction) {
                popp(ScriptFileName, "CONQSING", 0, "conq_sm.pcx", 0);
                if (!GameVictoryType) GameVictoryType = 4;
            }
            GameState |= STATE_GAME_DONE | STATE_VICTORY_CONQUER;
        }
        return 0;
    }

    // ---- landing: personality, starting techs and units ----
    F.SE_alloc_labs = 5;
    F.SE_alloc_psych = 0;
    if ((CurrentTurn == 0 || killer_id == kSetupArrival) && DiffLevel >= 2) {
        uint32_t& ext = F.player_flags_ext;
        if ((F.AI_wealth || F.AI_tech) && game_rand() % 4 == 0) ext |= PFLAG_EXT_STRAT_LOTS_PROBE_TEAMS;
        faction_key_upper(f);
        if (!crt_strcmp(StrBuffer, "ANGELS")) ext |= PFLAG_EXT_STRAT_LOTS_PROBE_TEAMS;
        uint32_t rule_flags = (uint32_t)M.rule_flags;
        if (rule_flags & RFLAG_AQUATIC) {
            ext |= PFLAG_EXT_STRAT_LOTS_SEA_BASES;
            F.player_flags |= PFLAG_EMPHASIZE_SEA_POWER;
        }
        if (rule_flags & RFLAG_ALIEN) {
            ext |= PFLAG_EXT_STRAT_LOTS_MISSILES;
            F.player_flags |= PFLAG_EMPHASIZE_AIR_POWER;
        }
        if (!crt_strcmp(StrBuffer, "DRONE")) ext = (ext & ~PFLAG_EXT_STRAT_LOTS_PROBE_TEAMS) | PFLAG_EXT_STRAT_LOTS_TERRAFORMERS;
        if (Continents[region].tile_count < 250 && game_rand() % 3 == 0) ext |= PFLAG_EXT_STRAT_LOTS_SEA_BASES;
        if (F.AI_fight >= 0 && game_rand() % 4 == 0) ext |= PFLAG_EXT_STRAT_LOTS_MISSILES;
        if (game_rand() % 6 == 0) ext |= PFLAG_EXT_STRAT_LOTS_TERRAFORMERS;
        if (game_rand() % 6 == 0) ext |= PFLAG_EXT_STRAT_LOTS_COLONY_PODS;
        if (game_rand() % 5 == 0) ext |= PFLAG_EXT_STRAT_LOTS_ARTILLERY;
        if (((GameRules & RULES_INTENSE_RIVALRY) || DiffLevel >= 4) &&
            F.AI_fight > ((GameRules & RULES_INTENSE_RIVALRY) ? -1 : 0) && game_rand() % 4 == 0)
            ext |= PFLAG_EXT_SHAMELESS_BETRAY_HUMANS;
        if (((GameRules & RULES_INTENSE_RIVALRY) || DiffLevel >= 4) &&
            F.AI_fight > ((GameRules & RULES_INTENSE_RIVALRY) ? -1 : 0) && game_rand() % 4 == 0) {
            F.player_flags |= PFLAG_COMMIT_ATROCITIES_WANTONLY;
            if (game_rand() % 2 == 0) F.player_flags |= PFLAG_OBLITERATE_CAPTURED_BASES;
        }
        if (F.AI_power || F.AI_fight > 0) {
            if (Continents[region].tile_count >= 400 && game_rand() % 4 == 0)
                F.player_flags |= PFLAG_EMPHASIZE_LAND_POWER;
            int32_t n = Continents[region].tile_count > 250 ? 6 : 4;
            if (n - 1 <= 0 || game_rand() % n == 0) F.player_flags |= PFLAG_EMPHASIZE_SEA_POWER;
            if (game_rand() % 4 == 0) F.player_flags |= PFLAG_EMPHASIZE_AIR_POWER;
        }
    }
    for (int32_t i = 0; i < 21; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        map_tile(x2, y2)->visibility |= bit;
        synch_bit(x2, y2, f);
    }
    int32_t scout_unit = BSC_SCOUT_PATROL;
    if (CurrentTurn == 0 || killer_id == kSetupArrival) {
        for (int32_t i = 0; i < M.faction_bonus_count; i++) {
            if (M.faction_bonus_id[i] == RULE_TECH) tech_achieved(f, M.faction_bonus_val1[i], 0, 0);
        }
        if (ExpansionEnabled && is_alien(f)) {
            tech_achieved(f, 78, 0, 0);
            tech_achieved(f, 79, 0, 0);
        }
        if (MultiplayerActive) {
            for (int32_t i = 0;; i++) {
                int32_t research = F.SE_research_base > 0 ? 1 : (F.SE_research_base >= 0 ? 0 : -1);
                int32_t morale = F.SE_morale_base > 0 ? 1 : (F.SE_morale_base >= 0 ? 0 : -1);
                if (i >= research - morale + 1) break;
                tech_achieved(f, tech_ai(f), 0, 0);
            }
            F.energy_credits += M.rule_energy / 2;
        }
        consider_designs(f);
        for (int32_t i = 0; i < 64; i++) {
            UNIT& u = Units[f * 64 + i];
            if (u.unit_flags & UNIT_ACTIVE) {
                u.unit_flags |= UNIT_PROTOTYPED;
                if (u.plan == PLAN_RECON) scout_unit = f * 64 + i;
            }
        }
    }
    if (CurrentTurn != 0 && killer_id != kSetupArrival) {
        int32_t veh_id;
        if (is_aquatic(f)) {
            veh_init(BSC_SEA_ESCAPE_POD, f, x, y);
            veh_id = veh_init(BSC_SEA_FORMERS, f, x, y);
        } else {
            veh_init(BSC_COLONY_POD, f, x, y);
            veh_id = veh_init(BSC_FORMERS, f, x, y);
        }
        veh_demote(veh_id);
    } else {
        if ((!MultiplayerActive && !(GameRules & RULES_LOOK_FIRST)) || (GameRules & RULES_TIME_WARP)) {
            int32_t base_id = base_init(f, x, y);
            if (!crt_strcmp(faction_key_upper(f), "FUNGBOY") && killer_id == kSetupArrival)
                game().bases[base_id].pop_size = 3;
            if (is_alien(f) && killer_id == kSetupArrival) game().bases[base_id].pop_size = 3;
        } else if (is_aquatic(f)) {
            veh_init(BSC_SEA_ESCAPE_POD, f, x, y);
        } else {
            veh_init(BSC_COLONY_POD, f, x, y);
        }
        if (!is_aquatic(f)) {
            int32_t veh_id = veh_init(scout_unit, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
        }
        if (MultiplayerActive && !is_aquatic(f)) {
            int32_t veh_id = veh_init(BSC_UNITY_ROVER, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
            veh_id = veh_init(BSC_FORMERS, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
        }
        int32_t coast = is_coast(x, y, 0);
        if (coast && (DiffLevel > 1 || MultiplayerActive || Continents[region].tile_count <= 32)) {
            region = map_tile(x, y)->region;
            // Unity foils on a crowded continent, one tile off the coast.
            if (MultiplayerActive && Continents[region].tile_count / (bit_count(others) + 1) <= 75 && is_human(f)) {
                int32_t veh_id = veh_init(BSC_UNITY_FOIL, f, map_wrap_x(x + TableOffsetX[coast]), y + TableOffsetY[coast]);
                game().vehs[veh_id].morale = MORALE_DISCIPLINED;
                veh_demote(veh_id);
                game().vehs[veh_id].home_base_id = -1;
            }
            if (Continents[region].tile_count / (bit_count(others) + 1) <= 50) {
                int32_t veh_id = veh_init(BSC_UNITY_FOIL, f, map_wrap_x(x + TableOffsetX[coast]), y + TableOffsetY[coast]);
                game().vehs[veh_id].morale = MORALE_DISCIPLINED;
                veh_demote(veh_id);
                game().vehs[veh_id].home_base_id = -1;
            }
        }
    }
    if (CurrentTurn == 0 || killer_id == kSetupArrival) {
        int32_t* modifiers = &F.SE_economy_base;
        for (int32_t i = 0; i < 11; i++) modifiers[i] = 0;
        int32_t count = M.faction_bonus_count;
        for (int32_t i = 0; i < count; i++) {
            if (M.faction_bonus_id[i] == RULE_SOCIAL) modifiers[M.faction_bonus_val1[i]] += M.faction_bonus_val2[i];
        }
        for (int32_t i = 0; i < M.faction_bonus_count; i++) {
            if (M.faction_bonus_id[i] != RULE_UNIT) continue;
            int32_t veh_id = veh_init(M.faction_bonus_val1[i], f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
            if (M.faction_bonus_val1[i] == BSC_COLONY_POD) F.player_flags |= PFLAG_UNK_100;
        }
        if (ExpansionEnabled && is_alien(f)) {
            int32_t veh_id = veh_init(BSC_BATTLE_OGRE_MK1, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
            veh_id = veh_init(BSC_COLONY_POD, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
        }
        if (is_aquatic(f)) {
            if (!is_human(f)) {
                // Sea colony pod prototype for the AI in the first free slot.
                // BUG(original): with no free slot the prototype id is killer_id.
                COMPAT_ORIGINAL(sea_pod_slot_killer_id);
                int32_t unit_id = killer_id;
                for (int32_t i = 0; i < 64; i++) {
                    if (!(Units[f * 64 + i].unit_flags & UNIT_ACTIVE)) {
                        unit_id = f * 64 + i;
                        break;
                    }
                }
                char name[80];
                make_proto(unit_id, CHS_FOIL, WPN_COLONY_MODULE, ARM_NO_ARMOR, ABL_NONE, REC_FISSION);
                Units[unit_id].unit_flags = (uint16_t)((Units[unit_id].unit_flags & 0xFF3F) | UNIT_PROTOTYPED);
                name_proto(name, unit_id, f, CHS_FOIL, WPN_COLONY_MODULE, ARM_NO_ARMOR, ABL_NONE, REC_FISSION);
                Units[unit_id].unit_flags &= ~UNIT_CUSTOM_NAME_SET;
                name[31] = 0;
                crt_strcpy(Units[unit_id].name, name);
            }
            int32_t veh_id = veh_init(BSC_SEA_ESCAPE_POD, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
            veh_id = veh_init(BSC_UNITY_GUNSHIP, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
            veh_demote(veh_id);
        }
    }
    if (CurrentTurn != 0 && !gave_up && killer_id != kSetupArrival) {
        // Respawn: an escort chosen among the available land (sea for aquatic factions) designs.
        int32_t best_def = -1, def_unit = BSC_SCOUT_PATROL;
        int32_t best_att = -1, att_unit = -1;
        int32_t best_fast = -1, fast_unit = -1;
        for (int32_t u = 0; u < 512; u++) {
            if (!veh_avail(u, f, -1) || !Weapon[Units[u].weapon_id].offense_value) continue;
            const CChassis& chs = Chassis[Units[u].chassis_id];
            if (is_aquatic(f) ? chs.triad != TRIAD_SEA : chs.triad != TRIAD_LAND) continue;
            int32_t value = armor_val(u, f) * 8;
            value -= weap_val(u, f);
            if (value > best_def) {
                best_def = value;
                def_unit = u;
            }
            int32_t weapon = weap_val(u, f);
            value = chs.speed + (armor_val(u, f) + weapon * 2) * 2;
            if (value > best_att) {
                best_att = value;
                att_unit = u;
            }
            weapon = weap_val(u, f);
            value = chs.speed * 4 - weapon;
            value -= armor_val(u, f);
            if (value > best_fast && chs.speed > 1) {
                best_fast = value;
                fast_unit = u;
            }
        }
        int32_t first = veh_init(is_aquatic(f) ? BSC_SEA_ESCAPE_POD : BSC_COLONY_POD, f, x, y);
        veh_init(def_unit, f, x, y);
        if (CurrentTurn > 20 && fast_unit >= 0) veh_init(fast_unit, f, x, y);
        if (CurrentTurn > 40 && att_unit >= 0) veh_init(att_unit, f, x, y);
        if (CurrentTurn > 60 && def_unit >= 0) veh_init(def_unit, f, x, y);
        if (CurrentTurn > 80 && att_unit >= 0) veh_init(att_unit, f, x, y);
        if (CurrentTurn > 100) veh_init(is_aquatic(f) ? BSC_SEA_ESCAPE_POD : BSC_COLONY_POD, f, x, y);
        veh_promote(first);
        F.unk_27 = F.tech_ranking * 3 / 4;
    }
    if (f == CurrentPlayerFaction) {
        mapwin_field(MapWins[0], 0x1DD9C) = x;
        mapwin_field(MapWins[0], 0x1DDA0) = y;
        for (int32_t i = 0; i < 32; i++) {
            MapWinCursorX[i] = x;
            MapWinCursorY[i] = y;
        }
    }
    return 1;
}

// ---- clear_scenario, compute_faction_modifiers ---------------------------------------------------

void GAMEAPI clear_scenario() {
    ObjectiveReqVictory = 9999;
    ObjectivesSuddenDeathVictory = 9999;
    ObjectiveAchievePts = 0;
    VictoryAchieveBonusPts = 0;
    StartingMissionYear = Rules.normal_start_year;
    if (DiffLevel < 3) {
        EndingMissionYear = Rules.normal_end_year_low_three_diff;
        return;
    }
    EndingMissionYear = Rules.normal_end_year_high_three_diff;
}

void GAMEAPI compute_faction_modifiers(int32_t faction_id) {
    int32_t* modifiers = &game().factions[faction_id].SE_economy_base;
    for (int32_t i = 0; i < 11; i++) modifiers[i] = 0;
    MFaction& m = game().mfactions[faction_id];
    for (int32_t i = 0; i < m.faction_bonus_count; i++)
        if (m.faction_bonus_id[i] == RULE_SOCIAL) modifiers[m.faction_bonus_val1[i]] += m.faction_bonus_val2[i];
}

// ---- get_plan, balance --------------------------------------------------------------------------

int32_t GAMEAPI get_plan(int32_t faction_id, int32_t plan) {
    for (int32_t i = 0; i < 128; i++) {
        int32_t unit_id = i < 64 ? i : faction_id * 64 - 64 + i;
        const UNIT& u = Units[unit_id];
        if ((u.unit_flags & UNIT_ACTIVE) && !(u.obsolete_factions & (uint8_t)(1 << faction_id)) &&
            (unit_id >= 64 || has_tech(u.preq_tech, faction_id)) && (uint8_t)u.plan == (uint32_t)plan)
            return unit_id;
    }
    return -1;
}

void GAMEAPI balance() {
    // Starting vehicle of each faction (on land if it has one there).
    int32_t start[7];
    int32_t score[7];
    for (int32_t f = 1; f < 8; f++) {
        int32_t veh_id = veh_find(0, 0, f, -1);
        start[f - 1] = veh_id;
        if (veh_id >= 0 && map_tile(game().vehs[veh_id].x, game().vehs[veh_id].y)->region >= 0x40)
            start[f - 1] = veh_find(game().vehs[veh_id].x, game().vehs[veh_id].y, f, veh_id);
    }
    int32_t best_score = 0;
    for (int32_t f = 1; f < 8; f++) {
        int32_t veh_id = start[f - 1];
        if (veh_id < 0) continue;
        int32_t x = game().vehs[veh_id].x, y = game().vehs[veh_id].y;
        uint32_t region = map_tile(x, y)->region;
        // Room: distance to the nearest rival on the same landmass, else the landmass size.
        int32_t room = 9999;
        for (int32_t g = 1; g < 8; g++) {
            int32_t other = start[g - 1];
            if (f == g || other < 0) continue;
            if (map_tile(game().vehs[other].x, game().vehs[other].y)->region != region) continue;
            int32_t dist =
                veh_vector_dist(x, y, game().vehs[other].x, game().vehs[other].y) * 3200 / game().map.tile_count;
            if (dist < room) room = dist;
        }
        if (room < 1000)
            room = room / 5 - 3;
        else
            room = clamp((Continents[region].tile_count + 31) / 32, 1, 8);
        int32_t good = 0;
        for (int32_t i = 0; i < 21; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            MAP* sq = map_tile(x2, y2);
            int32_t rocky = sq->val3 >> 6;
            int32_t rain = (sq->climate >> 3) & 3;
            int32_t level = (sq->climate >> 5) - 2;
            if (rain < 1) continue;
            if ((level > 0 || (sq->items & BIT_RIVER)) && rocky < 2 && (rocky == 1 || level > 1 || rain > 1)) good++;
            if (rocky == 1 && level >= 0 && level > 1) good++;
        }
        int32_t value = room + clamp(good / 2, 0, 8) + clamp(Continents[region].tile_count / 64, 0, 3);
        if (value > best_score) best_score = value;
        score[f - 1] = value;
    }
    for (int32_t f = 1; f < 8; f++) {
        int32_t veh_id = start[f - 1];
        if (veh_id < 0) continue;
        MFaction& m = game().mfactions[f];
        int32_t x = game().vehs[veh_id].x, y = game().vehs[veh_id].y;
        int32_t spiral = game_rand() % 16;
        int32_t deficit = best_score - score[f - 1];
        for (int32_t i = 0; i < m.faction_bonus_count; i++) {
            if (m.faction_bonus_id[i] == RULE_UNIT) deficit -= Units[m.faction_bonus_val1[i]].plan != PLAN_COLONY ? 1 : 3;
        }
        if (DiffLevel > 1 && (deficit >= 3 || DiffLevel >= (is_human(f) ? 4 : 5))) {
            deficit -= 3;
            if (!(m.rule_flags & RFLAG_AQUATIC)) veh_id = veh_init(BSC_COLONY_POD, f, x, y);
            game().vehs[veh_id].home_base_id = -1;
        }
        if (DiffLevel < 2) deficit = (deficit + 1) / 2;
        // Supply pods (or unity pods, at most two) on the radius-2 ring.
        int32_t placed = 0;
        spiral *= 7;
        for (int32_t k = 0; k < deficit; k++) {
            spiral += 7;
            int32_t i = spiral % 16 + 9;
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            if (goody_at(x2, y2) && !(map_tile(x2, y2)->items & 0xC000000)) score[f - 1]--;
            if (placed < 2 && deficit - k >= 3 && ((k ^ placed) & 1) == 0) {
                bit_set(x2, y2, 0x4000000, 1);
                deficit--;
                placed++;
            } else {
                bit_set(x2, y2, 0x8000000, 1);
            }
        }
    }
}

// ---- setup_game ---------------------------------------------------------------------------------

// Faction pairs (by search key) that start with the "personality conflict" agenda bit 0x200.
static const char* const kRivalKeys[][2] = {
    {"PEACE", "SPARTANS"}, {"GAIANS", "MORGAN"}, {"HIVE", "BELIEVE"}, {"UNIV", "BELIEVE"},
    {"FUNGBOY", "BELIEVE"}, {"PEACE", "PIRATES"}, {"DRONE", "PIRATES"}, {"DRONE", "MORGAN"},
    {"DRONE", "HIVE"},      {"ANGELS", "HIVE"},  {"ANGELS", "CYBORG"},
};

void GAMEAPI setup_game(int32_t reload) {
    int32_t pairs[8];
    LastSavePath[0] = 0;
    prefs_put("Latest Save", "");
    prefs_put("Latest Scenario", "");
    if (DiffLevel == 0 && reload == 0 && prefs_get_int("DontResetBeginnerPrefs", 0, 0) == 0) {
        prefs_load(1);
        AlphaIniPrefs.preferences |= 0x20;
        prefs_put_int("DontResetBeginnerPrefs", 0, 0);
    }
    if (MultiplayerActive) AlphaIniPrefs.preferences &= ~0x20;
    prefs_use();
    ObjectiveAchievePts = 0;
    ObjectiveReqVictory = 9999;
    ObjectivesSuddenDeathVictory = 9999;
    VictoryAchieveBonusPts = 0;
    StartingMissionYear = Rules.normal_start_year;
    EndingMissionYear = DiffLevel < 3 ? Rules.normal_end_year_low_three_diff : Rules.normal_end_year_high_three_diff;
    clear_monuments();
    wipe_undo();
    ReplayEventSize = 0;
    GameState &= STATE_RAND_FAC_LEADER_PERSONALITIES | STATE_RAND_FAC_LEADER_SOCIAL_AGENDA;
    cs.GameInterludeState[0] = 0;
    cs.TutorialChecks = 0;
    cs.TutorialChecks2 = 0;
    cs.TutWinMapState[0] = 0;
    cs.TutWinMapState[1] = 0;
    cs.dword_9A64BC = 0;
    CurrentTurn = 0;
    FactionStatus[1] = 0xFF;
    game().base_count = 0;
    cs.dword_9A64D0 = 0;
    clear_all_offers();
    clear_hotseat_chat();
    clear_council_notify_all();
    clear_tamper();
    cs.GameTimeControl[1] = 0;
    GovernorFaction = -1;
    for (int32_t i = 0; i < 11; i++) {
        ProposalPassCount[i] = 0;
        ProposalCallTurn[i] = -9999;
    }
    ClimateValueA = 0;
    ClimateValueB = 0;
    ClimateValueC = 0;
    ClimateFutureChange = 0;
    if (!MultiplayerActive && reload && !ControlTurnB)
        GameState |= (STATE_SCENARIO_CHEATED_FLAG | STATE_SCENARIO_EDITOR | STATE_OMNISCIENT_VIEW);
    for (int32_t i = 0; i < 89; i++) TechOwners[i] = 0;
    for (int32_t i = 0; i < 64; i++) SecretProjects[i] = -1;
    std::memset(ReplayEvents, 0, sizeof(ReplayEvents));
    std::memset(FactionTurnMightBlock, 0, sizeof(FactionTurnMightBlock));
    cs.SunspotDuration = 0;
    cs.MountPlanetX = 0;
    cs.MountPlanetY = 0;
    cs.DustCloudDuration = 0;
    cs.SaveFileVersion = 0;
    cs.ScnVictFacilityObj = 0;
    cs.SolarFlaresEvent = 0;
    cs.GameMoreRules = 0;
    cs.CurrentFaction = 0;
    cs.ClimateLevel = 0;
    if (GameState & STATE_RAND_FAC_LEADER_SOCIAL_AGENDA) {
        // Random social priorities: a (category, model) not taken yet, and its first positive effect.
        int32_t taken[12] = {};
        int32_t tries_max = 1;
        for (int32_t f = 1; f < 8;) {
            MFaction& m = game().mfactions[f];
            int32_t tries = 0, category, model;
            m.soc_priority_category = -1;
            m.soc_priority_model = 0;
            m.soc_priority_effect = -1;
            do {
                category = game_rand() % 3;
                model = game_rand() % 3 + 1;
                if (!taken[category * 3 + model]) break;
                tries++;
            } while (tries < tries_max + 1);
            tries_max++;
            f++;
            if (tries <= tries_max) {
                m.soc_priority_category = category;
                m.soc_priority_model = model;
                taken[category * 3 + model] = 1;
                for (int32_t e = 0; e < 11; e++) {
                    if (SocialField[category].soc_effect[model].values[e] > 0) {
                        m.soc_priority_effect = e;
                        break;
                    }
                }
            }
        }
    }
    for (int32_t f = 0; f < 8; f++) {
        if (FactionStatus[0] & (1 << f)) {
            setup_player(f, -1 - reload, 0);
            pairs[f] = 0;
        }
    }
    for (int32_t i = 0; i < 8; i++) TectonicDetonationCount[i] = 0;
    for (int32_t f = 0; f < 8; f++) {
        if (!(FactionStatus[0] & (1 << f))) {
            setup_player(f, -1 - reload, 0);
            pairs[f] = 0;
        }
    }
    std::memset(DiploStateB, 0, sizeof(DiploStateB));
    crt_strcpy(game().mfactions[0].formal_name_faction, label_get(245));
    crt_strcpy(game().mfactions[0].noun_faction, label_get(245));
    crt_strcpy(game().mfactions[0].adj_name_faction, label_get(244));
    if (reload) return;
    // Random pairings of the factions (draws only: the pairs are not used).
    for (int32_t i = 1; i < 8; i++) {
        if (pairs[i] != 0) continue;
        int32_t tries = 0, j;
        for (;;) {
            do {
                j = game_rand() % 7 + 1;
                tries++;
            } while (j == i);
            if (pairs[j] != 0 &&
                (tries < 200 || (tries < 400 && !(game().mfactions[j].rule_flags & RFLAG_COMMFREQ))))
                continue;
            if (is_human(i) && is_human(j) && tries < 100) continue;
            break;
        }
        pairs[i]++;
        pairs[j]++;
    }
    for (int32_t i = 1; i < 8; i++) {
        if (pairs[i] > 1 || !(game().mfactions[i].rule_flags & RFLAG_COMMFREQ)) continue;
        int32_t tries = 0, j;
        for (;;) {
            do {
                j = game_rand() % 7 + 1;
            } while (j == i);
            if (is_human(i) && is_human(j) && ++tries < 200) continue;
            break;
        }
        pairs[i]++;
        pairs[j]++;
    }
    if (!MultiplayerActive || (GameRules & RULES_INTENSE_RIVALRY)) {
        if (!(GameState & STATE_RAND_FAC_LEADER_PERSONALITIES)) {
            for (int32_t i = 1; i < 8; i++) {
                for (int32_t j = 1; j < 8; j++) {
                    if (i == j) continue;
                    char key_i[24], key_j[24];
                    key_i[0] = 0;
                    key_j[0] = 0;
                    crt_strcat(key_i, game().mfactions[i].search_key);
                    crt_strcat(key_j, game().mfactions[j].search_key);
                    win32::CharUpperA(key_i);
                    win32::CharUpperA(key_j);
                    bool rivals = false;
                    for (const auto& pair : kRivalKeys) {
                        if (!crt_strcmp(key_i, pair[0]) && !crt_strcmp(key_j, pair[1])) {
                            rivals = true;
                            break;
                        }
                    }
                    if (rivals || is_alien(i) || is_alien(j)) agenda_on(i, j, 0x200);
                    if (is_alien(i) && is_alien(j)) {
                        set_treaty(j, i, 0xC80C0C38, 1);
                        set_agenda(j, i, 0x2C2D, 1);
                    }
                }
            }
        } else {
            // Random personalities: each human faction gets a rival AI for intense rivalry and one
            // more at diff_level 3+.
            for (int32_t i = 1; i < 8; i++) {
                if (!(FactionStatus[0] & (1 << i))) continue;
                for (int32_t n = 0;
                     n < (int32_t)(((uint32_t)GameRules >> 6 & 1) + (game().factions[i].diff_level >= 3)); n++) {
                    int32_t tries = 0, j;
                    for (;;) {
                        j = game_rand() % 7 + 1;
                        if (++tries >= 100) break;
                        if (game().factions[j].diplo_agenda[i] & 0x200) continue;
                        uint8_t bit = (uint8_t)(1 << j);
                        if (!(FactionStatus[1] & bit) || (FactionStatus[0] & bit)) continue;
                        break;
                    }
                    if (tries < 100) agenda_on(j, i, 0x200);
                }
            }
        }
    }
    if (GameRules & RULES_TIME_WARP) {
        time_warp();
        return;
    }
    balance();
}

// ---- eliminate_player ---------------------------------------------------------------------------

int32_t GAMEAPI eliminate_player(int32_t faction_id, int32_t killer_id) {
    const int32_t f = faction_id;
    if (f == 0) return 0;
    for (int32_t i = 0; i < game().base_count; i++) {
        if (game().bases[i].faction_id == f) return 0;
    }
    for (int32_t i = game().veh_count - 1; i >= 0; i--) {
        if (i < game().veh_count && game().vehs[i].faction_id == f) veh_kill(i);
    }
    game().factions[f].base_count = 0;
    int32_t respawned = setup_player(f, killer_id, 0);
    if (!respawned) {
        if (killer_id > 0 && !(GameState & (STATE_GAME_DONE | STATE_FINAL_SCORE_DONE)))
            game().factions[killer_id].eliminated_count++;
        if (f == CurrentPlayerFaction) return 1;
    }
    if (killer_id < 0)
        killer_id = 0;
    else if (killer_id > 0)
        set_treaty(f, killer_id, 0x2030, 1);
    draw_map(1);
    MFaction& m = game().mfactions[f];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(0, m.noun_faction, -1, -1);
    GenderDefault = game().mfactions[killer_id].noun_gender;
    PluralDefault = game().mfactions[killer_id].is_noun_plural;
    parse_says(1, game().mfactions[killer_id].noun_faction, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(2, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(3, m.name_leader, -1, -1);
    parse_says(4, get_his_her(f, 0), -1, -1);
    char num[80];
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "WIPEOUT");
    if (killer_id == 0) {
        crt_itoa(2, num, 10);
    } else {
        if (killer_id == CurrentPlayerFaction) crt_strcat(StrBuffer, "YOU");
        crt_itoa(respawned != 0, num, 10);
    }
    crt_strcat(StrBuffer, num);
    char pcx[80];
    pcx[0] = 0;
    crt_strcat(pcx, m.filename);
    crt_strcat(pcx, "2.PCX");
    const char* pic = respawned ? (is_alien(f) ? "al_pod_sm.pcx" : "escpod_sm.pcx") : pcx;
    popp(ScriptFileName, StrBuffer, 0, pic, 0);
    if (!respawned) mon_killed_faction(killer_id, f);
    bool track;
    if (!MultiplayerActive && (GamePreferences & PREF_AV_SECRET_PROJECT_MOVIES) && killer_id == CurrentPlayerFaction) {
        if (!respawned) amovie_project_name(m.filename);
        track = respawned != 0;
    } else {
        track = respawned && !MultiplayerActive;
    }
    if (track && killer_id == CurrentPlayerFaction) {
        // The viewer's intelligence tracks the escaping colonists.
        int32_t n = (game().factions[CurrentPlayerFaction].diplo_status[f] & DIPLO_HAVE_INFILTRATOR) ? 2 : 4;
        int32_t veh_id;
        if ((n - 1 <= 0 || game_rand() % n == 0) && (veh_id = veh_find(0, 0, f, -1)) >= 0) {
            int32_t x2 = 0, y2 = 0;
            for (int32_t i = 0; i < 9; i++) {
                x2 = map_wrap_x(game().vehs[veh_id].x + TableOffsetX[i]);
                y2 = game().vehs[veh_id].y + TableOffsetY[i];
                if (map_in_bounds(x2, y2)) spot_loc(x2, y2, killer_id);
            }
            int32_t top = veh_id;
            while (game().vehs[top].prev_veh_id_stack >= 0) top = game().vehs[top].prev_veh_id_stack;
            bool shown = false;
            for (int32_t v = top; v >= 0; v = game().vehs[v].next_veh_id_stack) {
                game().vehs[v].visibility = 0;
                if (Units[game().vehs[v].unit_id].plan == PLAN_COLONY && !shown) {
                    shown = true;
                    game().vehs[v].visibility = (uint8_t)(1 << killer_id);
                }
            }
            draw_tiles(x2, y2, 2);
            pfncMoveMap(MapWins[0], x2, y2, 1);
            parse_says(6, m.adj_name_faction, -1, -1);
            popp(ScriptFileName, "TRACKED", 0, is_alien(f) ? "al_pod_sm.pcx" : "escpod_sm.pcx", 0);
        }
    }
    if (is_alien(f) && is_alien(CurrentPlayerFaction) && f != CurrentPlayerFaction) interlude(0x2A, 0, 1, 0);
    return 1;
}

// ---- scenario_setup -----------------------------------------------------------------------------

void GAMEAPI scenario_setup() {
    for (int32_t v = game().veh_count - 1; v >= 0; v--) {
        if (!(game().vehs[v].flags & (VFLAG_START_RAND_LOCATION | VFLAG_START_RAND_MONOLITH | VFLAG_START_RAND_FUNGUS)))
            continue;
        veh_lift(v);
        int32_t tries, x = 0, y = 0;
        for (tries = 0; tries < 10000; tries++) {
            int32_t flat = (int8_t)game().map.flat & 1;
            x = map_rand_mod(game().map.x - flat * 4) + (game().map.flat & 1) * 2;
            y = map_rand_mod(game().map.y - 4) + 2;
            if (x & 1) x--;
            if (y & 1) x++;
            uint8_t triad = Chassis[Units[game().vehs[v].unit_id].chassis_id].triad;
            if (triad != TRIAD_AIR && map_is_ocean(x, y) != (triad == TRIAD_SEA) && tries < 9950) continue;
            MAP* sq = map_tile(x, y);
            uint16_t flags = game().vehs[v].flags;
            if ((flags & VFLAG_START_RAND_MONOLITH) && !(sq->items & BIT_MONOLITH) && tries < 9900) continue;
            if ((flags & VFLAG_START_RAND_FUNGUS) && !((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) &&
                tries < 9900)
                continue;
            int32_t other = veh_at(x, y);
            if (other >= 0) {
                if (game().vehs[other].faction_id != game().vehs[v].faction_id) continue;
                if (tries < (stack_check(other, 1, -1, -1, -1) + 10) * 500) continue;
            }
            // Distance to the nearest other vehicle or base (an approximate measure).
            int32_t nearest = 9999;
            for (int32_t i = 0; i < game().veh_count; i++) {
                if (i == v) continue;
                int32_t dx = std::abs(x - game().vehs[i].x);
                if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
                int32_t dist = (std::abs(y - game().vehs[i].y) + dx) >> 1;
                if (dist < nearest) nearest = dist;
            }
            base_find(x, y);
            if (nearest >= BaseFindDist) nearest = BaseFindDist;
            if (tries >= (32 - nearest) * 100) break;
        }
        veh_drop(v, x, y);
        if (tries >= 10000) veh_kill(v);
    }
}

// ---- time_warp ----------------------------------------------------------------------------------

static inline int32_t sign_of(int32_t v) { return v > 0 ? 1 : (v >= 0 ? 0 : -1); }

void GAMEAPI time_warp() {
    // Warp length: the turn the game starts at, techs (base + per research/morale step), bases per
    // faction and the population of the first base.
    int32_t warp_turn = 0, tech_base = 0, tech_step = 0, bases_target = 0, first_pop = 0;
    switch (game_rand() % 3) {
    case 0:
        warp_turn = 30, tech_base = 6, tech_step = 2, bases_target = 3, first_pop = 5;
        break;
    case 1:
        warp_turn = 60, tech_base = 12, tech_step = 3, bases_target = 4, first_pop = 6;
        break;
    case 2:
        warp_turn = 80, tech_base = 16, tech_step = 4, bases_target = 5, first_pop = 6;
        break;
    }
    for (int32_t f = 1; f < 8; f++) {
        const uint8_t bit = (uint8_t)(1 << f);
        if (!(FactionStatus[1] & bit)) continue;
        int32_t first = base_find_faction(0, 0, f);
        if (first < 0) continue;
        MFaction& m = game().mfactions[f];
        Faction& F = game().factions[f];
        BASE& fb = game().bases[first];
        fb.pop_size = (int8_t)first_pop;
        if (m.rule_talent) fb.pop_size = (int8_t)(first_pop + 1);
        if (m.rule_drone || m.rule_population) fb.pop_size--;
        bool foil_placed = false;
        fb.nutrients_accumulated = fb.pop_size * 10 / 2;
        uint32_t home_region = map_tile(fb.x, fb.y)->region;
        int32_t techs = (sign_of(F.SE_research_base) - sign_of(F.SE_morale_base)) * tech_step + tech_base;
        SkipTechScreenB = 1;
        for (; techs > 0; techs--) {
            F.tech_research_id = tech_ai(f);
            tech_advance(f);
        }
        F.tech_ranking = F.tech_achieved * 2;
        SkipTechScreenB = 0;
        F.tech_research_id = -1;
        consider_designs(f);
        set_fac(FAC_RECREATION_COMMONS, first, 1);
        set_fac(FAC_RECYCLING_TANKS, first, 1);
        if (F.AI_wealth) set_fac(FAC_ENERGY_BANK, first, 1);
        if (F.AI_power) set_fac(FAC_COMMAND_CENTER, first, 1);
        if (F.AI_tech) set_fac(FAC_NETWORK_NODE, first, 1);
        if (F.AI_growth) {
            int32_t unit_id = BSC_UNITY_ROVER;
            if (ExpansionEnabled && is_aquatic(f))
                unit_id = has_tech(Units[BSC_TRANSPORT_FOIL].preq_tech, f) ? BSC_TRANSPORT_FOIL : BSC_UNITY_FOIL;
            veh_init(unit_id, f, fb.x, fb.y);
            // BUG(original): arguments out of order: facility 4 (perimeter defense) set at base 1.
            COMPAT_ORIGINAL(perimeter_defense_args);
            set_fac(FAC_PERIMETER_DEFENSE, 1, first);
        }
        F.energy_credits += (F.AI_wealth * 9 + F.AI_growth * 5 + F.AI_tech + 1) * 25;
        int32_t sea_unit = get_plan(f, PLAN_NAVAL_SUPERIORITY);
        int32_t transport = has_tech(Units[BSC_TRANSPORT_FOIL].preq_tech, f) ? BSC_TRANSPORT_FOIL : BSC_UNITY_FOIL;
        int32_t defender = get_plan(f, PLAN_DEFENSE);
        if (defender < 0) {
            defender = get_plan(f, PLAN_COMBAT);
            if (defender < 0) defender = BSC_SCOUT_PATROL;
        }
        Units[defender].unit_flags |= UNIT_PROTOTYPED;
        int32_t veh_id = veh_init(defender, f, fb.x, fb.y);
        game().vehs[veh_id].order = ORDER_HOLD;
        game().vehs[veh_id].morale++;
        veh_id = veh_init(defender, f, fb.x, fb.y);
        game().vehs[veh_id].order = ORDER_HOLD;
        // last_veh: the vehicle the original keeps in a local (see the fallback below).
        int32_t last_veh =
            veh_init(ExpansionEnabled && is_aquatic(f) ? BSC_SEA_FORMERS : BSC_FORMERS, f, fb.x, fb.y);
        int32_t unit_id = get_plan(f, PLAN_COMBAT);
        if (unit_id >= 0) {
            Units[unit_id].unit_flags |= UNIT_PROTOTYPED;
            last_veh = veh_init(unit_id, f, fb.x, fb.y);
            game().vehs[last_veh].morale++;
            if (unit_id == BSC_MIND_WORMS) game().vehs[last_veh].morale = MORALE_DISCIPLINED;
        }
        unit_id = get_plan(f, PLAN_OFFENSE);
        if (unit_id >= 0) {
            Units[unit_id].unit_flags |= UNIT_PROTOTYPED;
            if (F.AI_fight <= 0 && !F.AI_power) {
                int32_t combat = get_plan(f, PLAN_COMBAT);
                if (combat >= 0) {
                    last_veh = veh_init(combat, f, fb.x, fb.y);
                    if (combat == BSC_MIND_WORMS) game().vehs[last_veh].morale = MORALE_DISCIPLINED;
                }
            } else {
                last_veh = veh_init(unit_id, f, fb.x, fb.y);
                game().vehs[last_veh].morale++;
                if (F.AI_fight > 0) last_veh = veh_init(unit_id, f, fb.x, fb.y);
            }
        }
        // A secret project in the first base (not 4, nor 5 for the Hive).
        // BUG(original): never ends when the eligible projects 0-7 are all built (only possible if
        // time_warp runs on a game in progress).
        COMPAT_ORIGINAL(time_warp_project_loop);
        int32_t project;
        do {
            project = game_rand() % 8;
        } while (SecretProjects[project] != -1 || project == 4 ||
                 (project == 5 && !crt_strcmp(m.filename, "HIVE")));
        SecretProjects[project] = first;
        // More bases: the best site 3-8 tiles from an own base.
        int32_t last_x = 0, last_y = 0;  // the last own base visited by the site search
        for (int32_t k = 0; k < bases_target - 1; k++) {
            int32_t best = 0, best_x = 0, best_y = 0;
            for (int32_t i = 0; i < game().base_count; i++) {
                if (game().bases[i].faction_id != f) continue;
                int32_t bx = game().bases[i].x, by = game().bases[i].y;
                uint32_t base_region = map_tile(bx, by)->region;
                last_x = bx;
                last_y = by;
                for (int32_t j = 25; j < 289; j++) {
                    int32_t x2 = map_wrap_x(bx + TableOffsetX[j]);
                    int32_t y2 = by + TableOffsetY[j];
                    if (!map_in_bounds(x2, y2)) continue;
                    MAP* sq = map_tile(x2, y2);
                    uint32_t alt = sq->climate & 0xE0;
                    if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
                    if ((alt < 0x60) != is_aquatic(f)) continue;
                    if ((sq->val3 & 0xC0) > 0x40 && !is_aquatic(f)) continue;
                    uint32_t region = sq->region;
                    int32_t site = 0;
                    if (!(sq->val3 & 7) && !(sq->items & BIT_BASE_RADIUS)) {
                        site = sq->val2 >> 4;
                        if (!site) {
                            site = world_site(x2, y2, is_aquatic(f) ? 1 : 0);
                            site_set(x2, y2, site);
                        }
                    }
                    int32_t value = (game().bases[i].pop_size + site) * 32;
                    if (base_region != region) value /= 2;
                    int32_t ring = j - 25;  // index in the radius 3-8 part of the table
                    if (ring >= 56 && ring < 144) value = value * 3 / 2;
                    if (ring >= 200) value -= value / 4;
                    if (ring < 24) value /= 2;
                    int32_t near_base = base_find(x2, y2);
                    if (i != near_base) {
                        int32_t dx = std::abs(x2 - game().bases[near_base].x);
                        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
                        if (((std::abs(y2 - game().bases[near_base].y) + dx) & ~1) < 8) continue;
                    }
                    if (game().bases[near_base].faction_id != f) {
                        // BUG(original): compares the tile's region with itself: always skipped.
                        COMPAT_ORIGINAL(setup_region_self_compare);
                        if (map_tile(x2, y2)->region == region) continue;
                        value /= 2;
                    }
                    if (value > best) {
                        best = value;
                        best_x = x2;
                        best_y = y2;
                    }
                }
            }
            if (best) {
                int32_t base_id = base_init(f, best_x, best_y);
                BASE& b = game().bases[base_id];
                int8_t pop = (int8_t)(bases_target - k);
                b.pop_size = pop;
                if (pop > 2) set_fac(FAC_RECREATION_COMMONS, base_id, 1);
                if (b.pop_size > 3) set_fac(FAC_RECYCLING_TANKS, base_id, 1);
                b.nutrients_accumulated = b.pop_size * 10 / 2;
                int32_t guard = get_plan(f, PLAN_DEFENSE);
                if (guard < 0) {
                    guard = get_plan(f, PLAN_COMBAT);
                    if (guard < 0) guard = BSC_SCOUT_PATROL;
                }
                last_veh = veh_init(guard, f, best_x, best_y);
                if (guard == BSC_MIND_WORMS) game().vehs[last_veh].morale = MORALE_DISCIPLINED;
                game().vehs[last_veh].order = ORDER_HOLD;
                if (b.pop_size > 2) {
                    last_veh = veh_init(guard, f, best_x, best_y);
                    if (guard == BSC_MIND_WORMS) game().vehs[last_veh].morale = MORALE_DISCIPLINED;
                    game().vehs[last_veh].order = ORDER_HOLD;
                }
                if (F.AI_growth || b.pop_size > 3)
                    last_veh = veh_init(is_aquatic(f) ? BSC_SEA_FORMERS : BSC_FORMERS, f, best_x, best_y);
                // First base on another landmass: a transport with a rover (and a warship) offshore.
                if (map_tile(best_x, best_y)->region != home_region && is_coast(b.x, b.y, 0) && !foil_placed) {
                    for (int32_t i = 0; i < 8; i++) {
                        int32_t x2 = map_wrap_x(best_x + NearOffsetX[i]);
                        int32_t y2 = best_y + NearOffsetY[i];
                        if (!map_in_bounds(x2, y2) || !map_is_ocean(x2, y2)) continue;
                        if (sea_unit >= 0) {
                            Units[sea_unit].unit_flags |= UNIT_PROTOTYPED;
                            veh_id = veh_init(sea_unit, f, x2, y2);
                            game().vehs[veh_id].morale++;
                        }
                        veh_init(transport, f, x2, y2);
                        last_veh = veh_init(BSC_UNITY_ROVER, f, x2, y2);
                        sleep(last_veh);
                        foil_placed = true;
                        break;
                    }
                }
            } else {
                // No site: the smallest own base grows instead.
                int32_t small = -1, min_pop = 999;
                for (int32_t i = 0; i < game().base_count; i++) {
                    if (game().bases[i].faction_id == f && game().bases[i].pop_size < min_pop) {
                        min_pop = game().bases[i].pop_size;
                        small = i;
                    }
                }
                if (small < 0) continue;
                BASE& b = game().bases[small];
                if (b.pop_size < 6) b.pop_size += 2;
                set_fac(FAC_RECREATION_COMMONS, small, 1);
                set_fac(FAC_RECYCLING_TANKS, small, 1);
                set_fac(FAC_COMMAND_CENTER, small, 1);
                int32_t combat = get_plan(f, PLAN_COMBAT);
                if (combat >= 0) {
                    veh_init(combat, f, b.x, b.y);
                    // BUG(original): the morale goes to the vehicle created last before, not this one.
                    COMPAT_ORIGINAL(setup_morale_last_veh);
                    if (combat == BSC_MIND_WORMS) game().vehs[last_veh].morale = MORALE_DISCIPLINED;
                }
                COMPAT_ORIGINAL(setup_escape_pod_location);
                if (is_aquatic(f))
                    last_veh = veh_init(BSC_SEA_ESCAPE_POD, f, last_x, last_y);  // BUG(original): stale location
                else
                    veh_init(BSC_COLONY_POD, f, b.x, b.y);
                veh_init(BSC_ALIEN_ARTIFACT, f, b.x, b.y);
            }
        }
        SkipTechScreenB = 1;
        reset_territory();
        enemy_strategy(f);
        for (int32_t i = 0; i < game().base_count; i++) {
            BASE& b = game().bases[i];
            if (b.faction_id != f) continue;
            for (int32_t j = 0; j < 81; j++) {
                int32_t x2 = map_wrap_x(b.x + TableOffsetX[j]);
                int32_t y2 = b.y + TableOffsetY[j];
                if (!map_in_bounds(x2, y2)) continue;
                map_tile(x2, y2)->visibility |= bit;
                if (goody_at(x2, y2) && j < 21) map_tile(x2, y2)->items |= (BIT_BONUS_RES | BIT_SUPPLY_REMOVE);
            }
            set_base(i);
            base_compute(1);
            for (int32_t k = 0; k < b.pop_size * 2 / 3 + F.AI_growth; k++) {
                base_terraform(1);
                base_compute(1);
            }
            b.worked_tiles = 0;
            for (int32_t j = 0; j < 81; j++) {
                int32_t x2 = map_wrap_x(b.x + TableOffsetX[j]);
                int32_t y2 = b.y + TableOffsetY[j];
                if (map_in_bounds(x2, y2)) synch_bit(x2, y2, f);
            }
            // Roads to the other own bases of the landmass (at most 50 steps each).
            for (int32_t o = 0; o < game().base_count; o++) {
                if (o == i || game().bases[o].faction_id != f) continue;
                if (map_tile(b.x, b.y)->region != map_tile(game().bases[o].x, game().bases[o].y)->region) continue;
                int32_t x = game().bases[o].x, y = game().bases[o].y;
                int32_t to_x = b.x, to_y = b.y;
                int32_t steps = 0;
                int32_t flags = has_tech(Rules.tech_preq_build_road_fungus, f) ? 0 : 4;
                int32_t dir = Path_find(&Paths, x, y, to_x, to_y, 0, -1, flags, -1);
                while (dir >= 0) {
                    x = map_wrap_x(x + NearOffsetX[dir]);
                    y += NearOffsetY[dir];
                    if (x == to_x && y == to_y) break;
                    MAP* sq = map_tile(x, y);
                    if ((!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) >= 8) && !(sq->items & BIT_ROAD)) {
                        sq->items |= BIT_ROAD;
                        synch_bit(x, y, f);
                    }
                    if (++steps > 50) break;
                    flags = has_tech(Rules.tech_preq_build_road_fungus, f) ? 0 : 4;
                    dir = Path_find(&Paths, x, y, to_x, to_y, 0, -1, flags, -1);
                }
            }
            base_reset(i, 0);
        }
        SkipTechScreenB = 0;
        // Formers move next to their start onto the best tile around.
        for (int32_t v = 0; v < game().veh_count; v++) {
            if (game().vehs[v].faction_id != f) continue;
            const UNIT& u = Units[game().vehs[v].unit_id];
            if (u.plan != PLAN_TERRAFORM) continue;
            uint8_t triad = Chassis[u.chassis_id].triad;
            if (is_aquatic(f) ? triad != TRIAD_SEA : triad != TRIAD_LAND) continue;
            int32_t best = 0;
            for (int32_t k = 0; k < 8; k++) {
                int32_t x2 = map_wrap_x(game().vehs[v].x + NearOffsetX[k]);
                int32_t y2 = game().vehs[v].y + NearOffsetY[k];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* sq = map_tile(x2, y2);
                uint32_t alt = sq->climate & 0xE0;
                if (is_aquatic(f) ? alt >= 0x60 : alt < 0x60) continue;
                uint32_t rocky = sq->val3 & 0xC0;
                int32_t score = rocky < 0x80 ? 2 : 1;
                if (sq->items & BIT_ROAD) score += 2;
                if (rocky == 0x40 && (sq->climate & 0x18) && (int32_t)(sq->climate >> 5) - 2 > 0) score += 2;
                if (score > best) {
                    best = score;
                    veh_drop(veh_lift(v), x2, y2);
                }
            }
        }
    }
    CurrentTurn = warp_turn;
}

// ---- crash_landing ------------------------------------------------------------------------------

void GAMEAPI crash_landing(int32_t faction_id) {
    int32_t base_id = -1, x, y;
    for (int32_t i = 0; i < game().base_count; i++) {
        if (game().bases[i].faction_id == faction_id) {
            base_id = i;
            break;
        }
    }
    if (base_id >= 0) {
        x = game().bases[base_id].x;
        y = game().bases[base_id].y;
    } else {
        int32_t veh_id = -1;
        for (int32_t i = 0; i < game().veh_count; i++) {
            if (game().vehs[i].faction_id == faction_id) {
                veh_id = i;
                break;
            }
        }
        if (veh_id < 0) return;
        x = game().vehs[veh_id].x;
        y = game().vehs[veh_id].y;
    }
    bool alien = is_alien(CurrentPlayerFaction);
    faction_key_upper(CurrentPlayerFaction);
    // Aliens and the Cult of Planet land at turn 5; the Cult has no landing animation.
    bool animate = true;
    if (!MultiplayerActive) {
        if (alien) CurrentTurn = 5;
        if (!crt_strcmp(StrBuffer, "FUNGBOY")) {
            CurrentTurn = 5;
            animate = false;
        }
    }
    Console_update_data(&MainConsole, 0);
    if (animate) ui_crash_landing_drop(x, y, alien);
    flush_input();
    BlitDestScale = 1;
    BlitSourceScale = 1;
    if (base_id >= 0) {
        char name[80];
        crt_strcpy(name, game().bases[base_id].name);
        ui_first_base_name(name);
        crt_strcpy(game().bases[base_id].name, ParseStrBuffer[0]);
        make_base_unique(base_id);
    }
    mapwin_field(MapWins[0], 0x1DD70) |= 0x8C0;
    draw_map(1);
    flush_input();
    ui_crash_landing_close();
}
