// Objectives, score and replay (original module around 0x5ABFF0-0x5AE120).
#include "engine/score.h"

#include <cstddef>
#include <cstring>

#include "engine/base.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/random.h"
#include "engine/tech.h"

namespace {
inline bool faction_alive(int32_t faction_id) { return (FactionStatus[1] & (uint8_t)(1u << (faction_id & 31))) != 0; }
inline bool faction_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
// Whether the base has the facility (bitmask lookup as the original).
bool base_has_facility(int32_t base_id, int32_t facility_id) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}
// Pact between faction_id and faction_id_with as seen by faction_id.
inline bool has_pact(int32_t faction_id, int32_t faction_id_with) {
    return (Factions[faction_id].diplo_status[faction_id_with] & DIPLO_PACT) != 0;
}
}  // namespace

int32_t GAMEAPI get_rating(int32_t faction_id, int32_t score) {
    int32_t diff_level = Factions[faction_id].diff_level;
    int32_t value = diff_level;
    for (int32_t level = 3; level <= diff_level; level++) value += (level - 1) / 2;
    if (GameRules & RULES_INTENSE_RIVALRY) value++;
    return (value + 6) * score / 160;
}

int32_t GAMEAPI is_objective(int32_t base_id) {
    if (GameRules & RULES_SCN_VICT_ALL_BASE_COUNT_OBJ) return 1;
    if (Bases[base_id].event_flags & 0x8000) return 1;
    if (GameRules & RULES_SCN_VICT_SP_COUNT_OBJ) {
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] == base_id) return 1;
    }
    if ((GameState & STATE_SCN_VICT_BASE_FACIL_COUNT_OBJ) && ScnVictFacilityObj < 65 &&
        base_has_facility(base_id, ScnVictFacilityObj))
        return 1;
    return 0;
}

int32_t GAMEAPI num_objectives(int32_t faction_id, int32_t incl_pact) {
    if (!faction_alive(faction_id)) return 0;
    Faction& f = Factions[faction_id];
    int32_t score = f.unk_101;
    for (int32_t i = 0; i < VehCount; i++) {
        const VEH& veh = Vehs[i];
        if (veh.faction_id != (uint32_t)faction_id || !(veh.flags & 0x20)) continue;
        if (!(GameRules & RULES_SCN_VICT_OBJ_UNITS_REACH_FRIEND_OBJ_BASE)) {
            if (GameRules & RULES_SCN_VICT_OBJ_UNITS_REACH_FRIEND_HQ_BASE) {
                // Objective units count at a headquarters base (own, or a pact partner's with
                // cooperative victory).
                int32_t base_id = base_at(veh.x, veh.y);
                if (base_id < 0 || !base_has_facility(base_id, FAC_HEADQUARTERS)) continue;
                int32_t owner = Bases[base_id].faction_id;
                if (owner != faction_id && (!has_pact(faction_id, owner) || !(GameRules & RULES_VICTORY_COOPERATIVE)))
                    continue;
            }
            score++;
            continue;
        }
        // Objective units count at an objective base (is_objective inlined).
        int32_t base_id = base_at(veh.x, veh.y);
        if (base_id < 0) continue;
        bool objective = (GameRules & RULES_SCN_VICT_ALL_BASE_COUNT_OBJ) || (Bases[base_id].event_flags & 0x8000);
        if (!objective && (GameRules & RULES_SCN_VICT_SP_COUNT_OBJ)) {
            for (int32_t j = 0; j < 64; j++) {
                if (SecretProjects[j] == base_id) {
                    objective = true;
                    break;
                }
            }
        }
        if (!objective) {
            if (!(GameState & STATE_SCN_VICT_BASE_FACIL_COUNT_OBJ) || ScnVictFacilityObj > 64 ||
                !base_has_facility(base_id, ScnVictFacilityObj))
                continue;
        }
        int32_t owner = Bases[base_id].faction_id;
        if (owner == faction_id || (has_pact(faction_id, owner) && (GameRules & RULES_VICTORY_COOPERATIVE))) score++;
    }
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id != faction_id) continue;
        if (GameState & STATE_SCN_VICT_POPULATION_COUNT_OBJ) score += Bases[i].pop_size;
        if ((GameRules & RULES_SCN_VICT_ALL_BASE_COUNT_OBJ) || (Bases[i].event_flags & 0x8000)) score++;
    }
    if (GameState & STATE_SCN_VICT_TECH_COUNT_OBJ) {
        for (int32_t tech_id = 0; tech_id < 89; tech_id++)
            if (has_tech(tech_id, faction_id)) score++;
        score += f.tech_count_transcendent;
    }
    if (GameState & STATE_SCN_VICT_CREDITS_COUNT_OBJ) score += f.energy_credits;
    if (GameState & STATE_SCN_VICT_BASE_FACIL_COUNT_OBJ) {
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id == faction_id && ScnVictFacilityObj < 65 && base_has_facility(i, ScnVictFacilityObj))
                score++;
        }
    }
    if (GameState & STATE_SCN_VICT_TERRAIN_ENH_COUNT_OBJ) {
        for (int32_t y = 0; y < MapAreaY; y++) {
            for (int32_t x = y & 1; x < MapAreaX; x += 2) {
                MAP* sq = map_tile(x, y);
                if (sq->owner == faction_id) score += bit_count(sq->items & 0x81E4885C);
            }
        }
    }
    if (GameState & STATE_SCN_VICT_TERRITORY_COUNT_OBJ) {
        for (int32_t y = 0; y < MapAreaY; y++) {
            for (int32_t x = y & 1; x < MapAreaX; x += 2)
                if (map_tile(x, y)->owner == faction_id) score++;
        }
    }
    if (GameRules & RULES_SCN_VICT_SP_COUNT_OBJ) {
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] >= 0 && Bases[SecretProjects[i]].faction_id == faction_id) score++;
    }
    if (incl_pact && (GameRules & RULES_VICTORY_COOPERATIVE)) {
        for (int32_t i = 1; i < 8; i++)
            if (i != faction_id && has_pact(faction_id, i)) score += num_objectives(i, 0);
    }
    return score;
}

int32_t GAMEAPI most_objectives(int32_t* winner_id, int32_t* tied_count) {
    int32_t best = 0;
    int32_t best_id = 0;
    if (tied_count) *tied_count = 0;
    for (int32_t i = 1; i < 8; i++) {
        int32_t value = num_objectives(i, GameRules & RULES_VICTORY_COOPERATIVE);
        if (value < best) continue;
        if (tied_count) {
            if (value > best)
                *tied_count = 0;
            else if (!has_pact(best_id, i) || !(GameRules & RULES_VICTORY_COOPERATIVE))
                ++*tied_count;
        }
        best = value;
        best_id = i;
    }
    if (winner_id) *winner_id = best_id;
    return best;
}

int32_t GAMEAPI transcending(int32_t faction_id) {
    if (SecretProjects[FAC_VOICE_OF_PLANET - 70] == -1) return 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id == faction_id && Bases[i].queue_items[0] == FAC_ASCENT_TO_TRANSCENDENCE)  // BUG(original): +102 (a unit), not -102
            return 1;
    }
    return 0;
}

int32_t GAMEAPI ascending() { return SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1; }

void GAMEAPI rankings(int32_t full) {
    // Strongest conventional weapon among the prototyped designs (at least 1).
    int32_t best_weapon = 1;
    for (int32_t unit_id = 64; unit_id < 512; unit_id++) {
        const UNIT& u = Units[unit_id];
        if ((u.unit_flags & 1) && (u.unit_flags & 4)) {
            int32_t offense = Weapon[u.weapon_id].offense_value;
            if (best_weapon <= offense && offense < 99) best_weapon = offense;
        }
    }
    for (int32_t fid = 0; fid < 8; fid++) {
        Faction& f = Factions[fid];
        if (full) f.ranking = 0;
        if (fid == 0) continue;
        int32_t& score = FactionRankingsUnk[fid];
        if (!faction_alive(fid)) {
            score = 0;
        } else if ((ObjectiveReqVictory < 9000 || ObjectivesSuddenDeathVictory < 9000) && !ObjectiveAchievePts) {
            score = num_objectives(fid, 0) * 10;
        } else {
            score = (f.tech_count_transcendent + f.pop_total) * 4;
            for (int32_t tech_id = 0; tech_id < 89; tech_id++) {
                if (has_tech(tech_id, fid))
                    score = score + Tech[tech_id].AI_growth + Tech[tech_id].AI_tech + Tech[tech_id].AI_power +
                            Tech[tech_id].AI_wealth;
            }
            for (int32_t i = 0; i < 64; i++)
                if (SecretProjects[i] >= 0 && Bases[SecretProjects[i]].faction_id == fid) score += 10;
            score += num_objectives(fid, 0) * ObjectiveAchievePts;
            for (int32_t unit_id = 0; unit_id < 512; unit_id++) {
                const UNIT& u = Units[unit_id];
                if (unit_id < 64 && !has_tech(u.preq_tech, fid)) continue;
                // BUG(original): counts of 251 and more (the byte counter can also wrap) are ignored.
                uint32_t count = f.units_active[unit_id];
                if (count >= 251) continue;
                uint32_t value = u.cost * count;
                if (!value) continue;
                int32_t offense = Weapon[u.weapon_id].offense_value;
                if (offense == 0) {
                    value = (uint32_t)((int32_t)value >> 2);
                } else if (offense < 0) {
                    value = value / 2;
                } else if (offense > 98) {
                    // planet busters: full value
                } else {
                    // BUG(original): integer quotient first, so weaker weapons count 0.
                    value = (uint32_t)(offense / best_weapon) * value;
                }
                score += value;
            }
        }
        if (CurrentTurn < 1000) FactionTurnMight[CurrentTurn][fid] = (int16_t)score;
    }
    if (!full) return;
    for (int32_t rank = 7; rank > 0; rank--) {
        int32_t best = -1;
        int32_t best_id = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (FactionRankingsUnk[i] > best) {
                best_id = i;
                best = FactionRankingsUnk[i];
            }
        }
        FactionRankingsUnk[best_id] = -1;
        Factions[best_id].ranking = rank;
    }
    for (int32_t i = 0; i < 8; i++) FactionRankings[i] = 0;
    for (int32_t i = 1; i < 8; i++)
        if (faction_alive(i)) FactionRankings[Factions[i].ranking] = i;
    RankingFactionIDUnk1 = 0;
    RankingFactionIDUnk2 = 0;
    for (int32_t rank = 7; rank >= 0; rank--) {
        if (faction_human(FactionRankings[rank])) {
            RankingFactionIDUnk1 = FactionRankings[rank];
            break;
        }
    }
    for (int32_t rank = 0; rank < 8; rank++) {
        if (faction_human(FactionRankings[rank])) {
            RankingFactionIDUnk2 = FactionRankings[rank];
            break;
        }
    }
    // The faction the AIs unite against: the runaway leader, or (in a climactic battle) a human
    // at Librarian or above (any human with intense rivalry; the last one wins).
    int32_t leader = great_satan(FactionRankings[7], 0) ? FactionRankings[7] : 0;
    if (climactic_battle()) {
        for (int32_t i = 1; i < 8; i++)
            if (faction_human(i) && (Factions[i].diff_level > 3 || (GameRules & RULES_INTENSE_RIVALRY))) leader = i;
    }
    if (!leader) {
        GameState &= ~0x200;
        return;
    }
    GameState |= 0x200;
    int32_t vendettas = 0;
    for (int32_t i = 1; i < 8; i++)
        if (Factions[i].diplo_status[leader] & DIPLO_VENDETTA) vendettas++;
    for (int32_t i = 1; i < 8; i++) {
        if (faction_human(i)) continue;
        uint32_t status = Factions[i].diplo_status[leader];
        if (!(status & 7) || (status & 0xC01)) continue;
        if ((status & 0x4000000) && !(status & 0x800)) continue;
        int32_t difficulty = (GameRules & RULES_INTENSE_RIVALRY) ? 5 : Factions[leader].diff_level;
        uint32_t revenge = Factions[leader].diplo_status[i];
        int32_t rep = reputation(leader, i);
        int32_t climax = climactic_battle();
        uint32_t ext = Factions[i].player_flags_ext;
        int32_t span = (vendettas - Factions[i].AI_fight + 2) * 256;
        int32_t roll = (span == 1 || span - 1 < 0) ? 0 : game_rand() % span;
        int32_t chance = (rep + (climax ? 4 : 0) + ((ext >> 3) & 4) + ((revenge & DIPLO_WANT_REVENGE) ? 4 : 0)) * difficulty;
        if (roll < chance) set_treaty(i, leader, DIPLO_SHALL_BETRAY, 1);
    }
}

void GAMEAPI compute_score(int32_t faction_id, int32_t* score, int32_t* table, int32_t is_query) {
    if ((GameState & STATE_FINAL_SCORE_DONE) && !is_query) return;
    Faction& f = Factions[faction_id];
    if (!score) score = &f.unk_109;
    if (!table) table = &f.unk_110;
    int32_t* const own_table = &f.unk_110;
    *score = 0;
    if (!is_query) f.unk_107 = 0;
    for (int32_t i = 0; i < 9; i++) table[i] = 0;
    // [0] population (own bases, pact partners' and surrendered factions' bases), [1] commerce.
    for (int32_t base_id = 0; base_id < BaseCount; base_id++) {
        const BASE& base = Bases[base_id];
        int32_t owner = base.faction_id;
        if (owner == faction_id) {
            table[0] += base.pop_size;
            set_base(base_id);
            base_compute(0);
            for (int32_t i = 1; i < 8; i++) {
                if ((MFactions[faction_id].rule_flags & RFLAG_ALIEN) || (MFactions[i].rule_flags & RFLAG_ALIEN)) continue;
                if (!f.sanction_turns && !Factions[i].sanction_turns) table[1] += BaseCommerceImport[i];
            }
        } else if (!(f.player_flags & 0x60000)) {
            uint32_t status = Factions[owner].diplo_status[faction_id];
            if (status & DIPLO_HAVE_SURRENDERED) {
                if (status & DIPLO_PACT) table[0] += base.pop_size;
            } else if ((status & DIPLO_PACT) && (GameRules & RULES_VICTORY_COOPERATIVE)) {
                table[0] += base.pop_size / 2;
            }
        } else if (!has_pact(faction_id, owner) || !(GameRules & RULES_VICTORY_COOPERATIVE)) {
            table[0] += base.pop_size / 2;
        } else {
            table[0] += base.pop_size;
        }
    }
    for (int32_t tech_id = 0; tech_id < 89; tech_id++)
        if (has_tech(tech_id, faction_id)) table[2]++;
    table[3] = f.tech_count_transcendent * 10;
    int32_t points = ObjectiveAchievePts < 1 ? 1 : ObjectiveAchievePts;
    table[6] = num_objectives(faction_id, GameRules & RULES_VICTORY_COOPERATIVE) * points;
    for (int32_t i = 0; i < 64; i++)
        if (SecretProjects[i] >= 0 && Bases[SecretProjects[i]].faction_id == faction_id) table[4] += 25;
    // [6] after the Ascent to Transcendence: bonus shared by population among the ascending faction
    // (counted twice) and its pact partners.
    ScoreBonusPts = 0;
    for (int32_t i = 0; i < 8; i++) ScorePopTotal[i] = 0;
    int32_t ascent_base = SecretProjects[FAC_ASCENT_TO_TRANSCENDENCE - 70];
    if (ascent_base >= 0) {
        ScoreBonusPts = 300;
        if ((1000 - CurrentTurn) * 2 >= 300) ScoreBonusPts = (1000 - CurrentTurn) * 2;
        int32_t mine = 0;
        int32_t total = 0;
        int32_t ascender = -1;
        for (int32_t i = 1; i < 8; i++) {
            if (Bases[ascent_base].faction_id == i) {
                ascender = i;
                total = Factions[i].pop_total * 2;
                ScorePopTotal[i] = total;
                if (i == faction_id) mine = total;
                break;
            }
        }
        for (int32_t i = 1; i < 8; i++) {
            if (ascender == i) continue;
            // BUG(original): with no ascender (-1) this reads the word before diplo_status
            // (last_base_turn).
            const uint32_t* status =
                reinterpret_cast<const uint32_t*>(&Factions[i]) + offsetof(Faction, diplo_status) / 4;
            if (!(status[ascender] & DIPLO_PACT)) continue;
            int32_t pop = Factions[i].pop_total;
            total += pop;
            ScorePopTotal[i] = pop;
            if (i == faction_id) mine += pop;
        }
        // BUG(original): divides by zero (crash) when no population counts.
        table[6] = total ? ScoreBonusPts * mine / total : 0;
        ScorePopTotal[0] = total;
    }
    // [7] victory bonus.
    int32_t conquest = (500 - CurrentTurn) * 2;
    table[7] = conquest < 0 ? 0 : conquest;
    if (!(GameRules & RULES_VICTORY_CONQUEST)) table[7] = 0;
    if ((GameState & STATE_VICTORY_CONQUER) && (GameRules & RULES_VICTORY_CONQUEST) && table[7]) {
        // Conquest: the faction's stored bonus scaled by its share of the population.
        int32_t mine = 0;
        int32_t total = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            total += Bases[i].pop_size;
            uint32_t status = Factions[Bases[i].faction_id].diplo_status[faction_id];
            if (Bases[i].faction_id == faction_id || ((status & DIPLO_PACT) && (status & DIPLO_HAVE_SURRENDERED)))
                mine += Bases[i].pop_size;
        }
        if (BaseCount > 0 && total) table[7] = f.unk_117 * mine / total;
    } else {
        table[7] = 0;
        if (f.player_flags & 0x60000) {
            int32_t value = (600 - CurrentTurn) * 2;
            table[7] = value < 200 ? 200 : value;
        } else if (f.player_flags & 0x100000) {
            int32_t value = (1000 - CurrentTurn) * 2;
            table[7] = value < 300 ? 300 : value;
        } else {
            for (int32_t i = 1; i < 8; i++) {
                if ((Factions[i].player_flags & 0x60000) && has_pact(faction_id, i) &&
                    (GameRules & RULES_VICTORY_COOPERATIVE)) {
                    int32_t value = 600 - CurrentTurn;
                    table[7] = value < 100 ? 100 : value;
                }
            }
        }
    }
    if (!faction_alive(faction_id)) {
        table[7] = 0;
        table[8] = 0;
    }
    if ((ObjectiveReqVictory < 9000 || ObjectivesSuddenDeathVictory < 9000) &&
        !(GameState & STATE_SCN_VICT_HIGHEST_AC_SCORE_WINS)) {
        // Scenario objectives: the faction's own table is cleared except the objectives.
        for (int32_t i = 0; i < 9; i++)
            if (i != 6) own_table[i] = 0;
    }
    if (!f.unk_117) {
        if (!ObjectiveReqVictory) {
            int32_t most = most_objectives(nullptr, nullptr);
            most = most < 1 ? 1 : most_objectives(nullptr, nullptr);
            if (num_objectives(faction_id, GameRules & RULES_VICTORY_COOPERATIVE) >= most)
                table[7] = VictoryAchieveBonusPts;
        } else if (num_objectives(faction_id, GameRules & RULES_VICTORY_COOPERATIVE) >= ObjectiveReqVictory) {
            table[7] = VictoryAchieveBonusPts;
        }
    }
    for (int32_t i = 0; i < 9; i++) *score += table[i];
    if (MapNativeLifeForms != 1 && !(GameState & STATE_IS_SCENARIO)) {
        table[5] = (MapNativeLifeForms - 1) * *score / 4;
        *score += table[5];
    }
    if (is_query) return;
    if (GameState & STATE_SCN_VICT_HIGHEST_AC_SCORE_WINS) {
        int32_t best = 0;
        for (int32_t i = 1; i < 8; i++) {
            int32_t other_score;
            int32_t other_table[9];
            compute_score(i, &other_score, other_table, 1);
            if (best < other_score) best = other_score;
        }
        if (best <= *score) table[7] = VictoryAchieveBonusPts ? VictoryAchieveBonusPts : 1000;
    }
    f.unk_107 = *score;
    if ((GameRules & RULES_IRONMAN) && !(GameState & STATE_SCENARIO_CHEATED_FLAG)) f.unk_107 = *score * 2;
}


void GAMEAPI replay_base(int32_t event, int32_t x, int32_t y, int32_t faction_id) {
    log_say_values("Replay Base", x, y, faction_id);
    log_say_values("Replay Base Info", CurrentTurn, event, ReplayEventSize);
    ReplayEvent e;
    e.event = (int8_t)event;
    e.faction_id = (int8_t)faction_id;
    e.turn = (int16_t)CurrentTurn;
    e.x = (int16_t)x;
    e.y = (int16_t)y;
    // BUG(original): no bounds check; past 1024 events the record is written beyond ReplayEvents.
    std::memcpy(reinterpret_cast<uint8_t*>(ReplayEvents) + ReplayEventSize, &e, sizeof(e));
    ReplayEventSize += 8;
}
