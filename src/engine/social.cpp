#include "engine/social.h"

#include "engine/base.h"
#include "engine/design.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/net.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"

namespace {
// The four social models and the eleven-value effect arrays are consecutive int32 fields.
int32_t* soc_current(Faction& f) { return &f.SE_Politics; }
int32_t* soc_pending(Faction& f) { return &f.SE_Politics_pending; }
int32_t* eff_pending(Faction& f) { return &f.SE_economy_pending; }
int32_t* eff_current(Faction& f) { return &f.SE_economy; }
int32_t* eff_2(Faction& f) { return &f.SE_economy_2; }
int32_t* eff_base(Faction& f) { return &f.SE_economy_base; }

bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0; }
// Inline project ownership test of the original (SecretProjects index = facility id - 70).
bool owns_project(int32_t project, int32_t faction_id) {
    int32_t base_id = SecretProjects[project];
    return base_id >= 0 && (uint8_t)game().bases[base_id].faction_id == (uint32_t)faction_id;
}
constexpr int32_t kNetworkBackbone = 21;  // FAC_NETWORK_BACKBONE - 70
constexpr int32_t kCloningVats = 24;      // FAC_CLONING_VATS - 70
constexpr int32_t kAsceticVirtues = 13;   // FAC_ASCETIC_VIRTUES - 70
constexpr int32_t kLivingRefinery = 23;   // FAC_LIVING_REFINERY - 70

// Upheaval cost for a change of `changes` models (inline in social_upheaval and social_ai).
int32_t upheaval_cost(int32_t faction_id, int32_t changes) {
    if (!changes) return 0;
    changes++;
    int32_t diff = is_human(faction_id) ? game().factions[faction_id].diff_level : 3;
    int32_t cost = changes * changes * changes * diff;
    if (game().mfactions[faction_id].rule_flags & 0x80) cost += cost / 2;
    return cost;
}
}  // namespace

void GAMEAPI social_calc(const int32_t* choices, int32_t* effects, int32_t faction_id, int32_t, int32_t choices_only) {
    for (int32_t k = 0; k < 11; k++) effects[k] = 0;
    MFaction& m = game().mfactions[faction_id];
    for (int32_t cat = 0; cat < 4; cat++) {
        int32_t model = choices[cat];
        const int32_t* eff = SocialField[cat].soc_effect[model].values;
        for (int32_t k = 0; k < 11; k++) {
            int32_t value = eff[k];
            if (value < 0) {
                // Cybernetic (future 1) with the Network Backbone; Power (values 1) and Thought
                // Control (future 3) with the Cloning Vats: penalties ignored.
                if (cat == 3 && model == 1) {
                    if (SecretProjects[kNetworkBackbone] >= 0 && owns_project(kNetworkBackbone, faction_id)) continue;
                } else if ((cat == 2 && model == 1) || (cat == 3 && model == 3)) {
                    if (owns_project(kCloningVats, faction_id)) continue;
                }
                for (int32_t i = 0; i < m.faction_bonus_count; i++) {
                    if (m.faction_bonus_val1[i] == cat && m.faction_bonus_val2[i] == model) {
                        if (m.faction_bonus_id[i] == RULE_IMPUNITY)  // penalty of this model cancelled
                            effects[k] -= eff[k];
                        else if (m.faction_bonus_id[i] == RULE_PENALTY)  // penalty doubled
                            effects[k] += eff[k];
                    }
                }
            }
            effects[k] += eff[k];
        }
    }
    if (choices_only) return;
    if (owns_project(kAsceticVirtues, faction_id)) effects[5]++;   // police
    if (owns_project(kLivingRefinery, faction_id)) effects[2] += 2;  // support
    if (has_temple(faction_id)) {
        effects[7]++;  // planet
        if (m.rule_flags & 0x80) effects[10]++;  // research
    }
    const int32_t* base = eff_base(game().factions[faction_id]);
    for (int32_t k = 0; k < 11; k++) effects[k] += base[k];
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        int32_t* e = &effects[m.faction_bonus_val1[i]];
        if (m.faction_bonus_id[i] == RULE_IMMUNITY) {  // no negative value (kept within 0..999)
            int32_t v = *e;
            *e = v < 0 ? 0 : (v > 999 ? 999 : v);
        } else if (m.faction_bonus_id[i] == RULE_ROBUST) {  // negative value halved
            if (*e < 0) *e = *e / 2;
        }
    }
}

void GAMEAPI social_upkeep(int32_t faction_id) {
    Faction& f = game().factions[faction_id];
    for (int32_t i = 0; i < 4; i++) soc_current(f)[i] = soc_pending(f)[i];
    social_calc(soc_pending(f), eff_pending(f), faction_id, 0, 0);
    social_calc(soc_pending(f), eff_current(f), faction_id, 0, 0);
    social_calc(soc_pending(f), eff_2(f), faction_id, 1, 0);
    f.SE_upheaval_cost_paid = 0;
}

int32_t GAMEAPI social_upheaval(int32_t faction_id, const int32_t* choices) {
    int32_t changes = 0;
    for (int32_t i = 0; i < 4; i++) changes += choices[i] != soc_current(game().factions[faction_id])[i];
    return upheaval_cost(faction_id, changes);
}

void GAMEAPI social_set(int32_t faction_id) {
    int32_t old_nutrient = cost_factor(faction_id, 0, -1);
    int32_t old_mineral = cost_factor(faction_id, 1, -1);
    Faction& f = game().factions[faction_id];
    social_calc(soc_pending(f), eff_pending(f), faction_id, 0, 0);
    int32_t new_nutrient = cost_factor(faction_id, 0, -1);
    int32_t new_mineral = cost_factor(faction_id, 1, -1);
    for (int32_t base_id = 0; base_id < game().base_count; base_id++) {
        BASE& base = game().bases[base_id];
        if ((uint8_t)base.faction_id != (uint32_t)faction_id) continue;
        if (new_nutrient != old_nutrient)
            base.nutrients_accumulated = (old_nutrient / 2 + new_nutrient * base.nutrients_accumulated) / old_nutrient;
        if (new_mineral != old_mineral) {
            int32_t minerals = (old_mineral / 2 + base.minerals_accumulated * new_mineral) / old_mineral;
            base.minerals_accumulated = minerals;
            base.minerals_accumulated_2 = minerals;
        }
        if (!MultiplayerActive && is_human(faction_id)) {
            set_base(base_id);
            base_compute(1);
        }
    }
    if (faction_id == CurrentPlayerFaction) GraphicWin_redraw(&BaseWin);
}

int32_t GAMEAPI society_avail(int32_t category, int32_t model, int32_t faction_id) {
    MFaction& m = game().mfactions[faction_id];
    if (m.soc_opposition_category == category && m.soc_opposition_model == model) return 0;
    return has_tech(SocialField[category].soc_preq_tech[model], faction_id);
}

void GAMEAPI social_ai(int32_t faction_id, int32_t growth, int32_t tech, int32_t wealth, int32_t power, int32_t* out) {
    Faction& f = game().factions[faction_id];
    MFaction& m = game().mfactions[faction_id];
    int32_t effect_index = -1;  // effect weighted x1000 (growth > 100)
    int32_t fight;
    bool own_settings = false;
    if (!out) {
        if (is_human(faction_id)) return;
        own_settings = true;
    } else {
        if (growth < 0) return;
        if (growth <= 100) {
            fight = power ? 1 : -(wealth != 0);
        } else {
            effect_index = growth - 100;
            own_settings = true;
        }
    }
    if (own_settings) {
        growth = f.AI_growth;
        tech = f.AI_tech;
        wealth = f.AI_wealth;
        power = f.AI_power;
        fight = f.AI_fight;
    }
    int32_t combos = 4;
    for (int32_t i = 3; i; i--) combos <<= 2;

    // Missing population relative to the bases' goals.
    int32_t pop_deficit = 0;
    for (int32_t base_id = 0; base_id < game().base_count; base_id++)
        if ((uint8_t)game().bases[base_id].faction_id == (uint32_t)faction_id)
            pop_deficit += pop_goal(base_id) - game().bases[base_id].pop_size;

    // Military situation of the regions where the faction has bases.
    int32_t threat = 0;
    int32_t peace_regions = 0;
    int32_t plan8_regions = 0;
    for (int32_t region = 1; region < 64; region++) {
        if ((region & 0x3F) == 0x3F || (region & 0x3F) == 0) continue;
        if (!f.region_total_bases[region]) continue;
        uint8_t plan = f.region_base_plan[region];
        if (plan == 2 || (f.region_flags[region] & 0x400)) {
            threat += f.region_total_combat_units[region] * 2;
        } else if (plan == 0) {
            threat += f.region_total_combat_units[region];
            peace_regions++;
        } else if (plan == 8) {
            plan8_regions++;
        }
        // (For plan <= 2 the original also counts bases in regions threatened by enemy mind
        // worms, into a local that is overwritten before use.)
    }
    if (out && power && threat < 1) threat = 1;

    // Tech race: behind the leader.
    int32_t behind = 1;
    if ((GameState & 0x200) && f.ranking < 6) {
        int32_t gap = game().factions[RankingFactionIDUnk1].tech_ranking / 2 - f.tech_ranking / 2;
        if (gap > 5) behind = 2;
        if (gap > 10) behind++;
    }

    int32_t best_score = -9999;
    int32_t best_choice = -1;
    for (int32_t choice = 0; choice < combos; choice++) {
        int32_t models[4];
        int32_t bits = choice;
        bool allowed = true;
        for (int32_t cat = 0; cat < 4; cat++) {
            int32_t model = bits & 3;
            models[cat] = model;
            if (model) {
                if ((m.soc_opposition_category == cat && m.soc_opposition_model == model) ||
                    !has_tech(SocialField[cat].soc_preq_tech[model], faction_id)) {
                    allowed = false;
                    break;
                }
            }
            bits >>= 2;
        }
        if (!allowed) continue;
        do_all_non_input();
        int32_t eff[11];
        social_calc(models, eff, faction_id, 0, 0);
        int32_t score = effect_index >= 0 ? eff[effect_index] * 1000 : 0;
        bool rejected = false;
        if (m.soc_priority_category >= 0 && m.soc_priority_model != 0) {
            int32_t model = models[m.soc_priority_category];
            if (model != 0 && model != m.soc_priority_model) rejected = true;
            if (model == m.soc_priority_model) score += f.base_count;
        }
        if (m.soc_priority_effect >= 0) {
            int32_t value = eff[m.soc_priority_effect];
            if (value > 0) score += f.base_count;
            if (value < 0) rejected = true;
        }
        if (rejected) continue;

        // economy (energy lost to inefficiency)
        int32_t effic_index = 4 - eff[1];
        effic_index = effic_index < 0 ? 0 : (effic_index > 8 ? 8 : effic_index);
        int32_t inefficiency = f.social_effic[effic_index];
        int32_t economy;
        bool divide = true;
        if (eff[0] >= 2) {
            economy = f.unk_45 + (eff[0] > 4 ? 4 : eff[0]) * 2 - 4;
        } else if (eff[0] > 0) {
            economy = 0;
            for (int32_t r = 0; r < 128; r++)
                economy += ((f.region_base_plan[r] != 2) + 1) * f.region_total_bases[r];
        } else if (eff[0] < -1) {
            economy = 0;
            for (int32_t r = 0; r < 128; r++)
                economy -= ((f.region_base_plan[r] != 2) + 1) * f.region_total_bases[r] * (eff[0] + 1);
        } else {
            economy = -1;
            divide = false;
        }
        if (divide) {
            economy /= threat + 1;
            if (economy > 0) {
                while (inefficiency > 0) {
                    economy--;
                    inefficiency--;
                    if (economy <= 0) break;
                }
            }
        }
        if (!threat) economy *= 2;
        economy *= (f.AI_fight < 0 && threat < 2 ? 1 : 0) + tech * 2 + 1 + wealth;
        if (out) {
            if (wealth || tech) economy *= 2;
            if (growth || power) economy /= 2;
        } else if (wealth && !power && !growth) {
            economy *= 2;
        }
        score += economy / behind;

        // support
        int32_t support_index = eff[2] + 4;
        support_index = support_index < 0 ? 0 : (support_index > 7 ? 7 : support_index);
        int32_t support = f.social_support[support_index];
        if (eff[2] <= -4) support *= 3;
        if (threat) {
            support *= 2;
            if (eff[2] <= -3) support *= 2;
            if ((eff[2] == -1 || eff[2] == -2) && eff[0] < 2) support += support / 2;
        }
        if (out) {
            if (power) support *= 3;
            if (growth) support *= 2;
            if (tech) support /= 2;
        } else {
            if (fight > 0) support *= 2;
            if (growth > 0) support += support / 2;
            if (wealth > 0) support /= 2;
            if (tech > 0) support -= support / 4;
            if (power > 0) support += support / 2;
        }
        score -= support;

        // morale
        int32_t morale = eff[4] < -4 ? -4 : (eff[4] > 4 ? 4 : eff[4]);
        morale *= 2;
        if (morale < -6) morale++;
        if (morale < -2) morale++;
        if (morale > 6) morale--;
        if (morale > 2) morale--;
        morale = (f.unk_46 + f.base_count) * (threat + 1) * (fight + 2) * morale * behind;
        morale /= 8;
        if (out) {
            if (power) morale *= 3;
            if (growth || wealth) morale /= 2;
        } else {
            if (fight > 1) morale *= 2;
            if (power && !growth && !wealth) morale *= 2;
            if (fight < 0 && !power && (wealth || tech) && !threat) morale /= 2;
        }
        score += morale;

        // efficiency
        int32_t effic = inefficiency;
        if (eff[1] == -3)
            effic *= 2;
        else if (eff[1] <= -4)
            effic <<= 2;
        if (!threat) effic *= 2;
        if (out) {
            if (tech) effic *= 3;
            if (growth || power) effic /= 2;
        } else if (wealth || tech) {
            if (!power) {
                if (!growth) effic *= 2;
            } else if (!wealth) {
                effic /= 2;
            }
        } else if (power) {
            effic /= 2;
        }
        score -= effic;

        // growth
        int32_t growth_value = (f.nutrient_surplus_total + pop_deficit) * (growth * 2 + 2 - wealth) *
                               (plan8_regions + 1) * eff[6] / 5;
        if (aah_ooga(faction_id, faction_id)) growth_value *= 2;
        if (m.rule_population > 0) growth_value /= 2;
        if (out) {
            if (growth || wealth) growth_value *= 2;
            if (tech) growth_value /= 2;
        } else if (growth && power) {
            growth_value *= 2;
        }
        score += growth_value;

        // talent
        int32_t police_clamped = eff[5] + 2;
        police_clamped = police_clamped < -2 ? -2 : (police_clamped > 2 ? 2 : police_clamped);
        int32_t bases = f.base_count;
        int32_t talent = (peace_regions * 2 + 1) * bases * police_clamped;
        if (f.tech_ranking < game().factions[RankingFactionIDUnk2].tech_ranking &&
            f.ranking < game().factions[RankingFactionIDUnk2].ranking)
            talent *= 2;
        if (f.tech_ranking >= game().factions[RankingFactionIDUnk1].tech_ranking &&
            f.ranking > game().factions[RankingFactionIDUnk1].ranking)
            talent /= 2;
        if (out && (power || growth)) talent *= 2;
        score += talent / (2 - fight);

        // police (drones)
        int32_t talent_index = eff[3] + 4;
        talent_index = talent_index < 0 ? 0 : (talent_index > 7 ? 7 : talent_index);
        int32_t police_index = eff[5] + 5;
        police_index = police_index < 0 ? 0 : (police_index > 8 ? 8 : police_index);
        int32_t psych = f.social_psych[talent_index][police_index];
        if (threat) psych += bases * eff[5];
        if (out) {
            if (power || growth) psych *= 2;
            if (wealth) psych /= 2;
        } else {
            if (growth && tech) psych *= 2;
            if (power && (tech || wealth)) psych /= 2;
        }
        score += psych;

        // planet (ecology)
        int32_t planet = (int32_t)((uint32_t)(f.unk_47 * (4 - eff[7])) << 2) / 4;  // wraps like the original
        if (f.SE_planet_base > 0 || f.best_psi_land_offense > f.best_weapon_value) {
            planet += f.total_combat_units;
        } else if (behind > 1) {
            planet /= 2;
        }
        if (out) {
            if (growth) planet *= 2;
            if (wealth) planet /= 2;
        } else {
            bool halve;
            if (power) {
                if (!growth) planet /= 2;
                halve = (tech || wealth) && !growth;
            } else if (!growth) {
                halve = (tech || wealth) && !growth;
            } else if (tech) {
                halve = !growth;
            } else {
                planet <<= 2;
                halve = wealth && !growth;
            }
            if (halve) planet /= 2;
        }
        score -= planet;

        // research
        int32_t research_clamped = eff[10] < -5 ? -5 : (eff[10] > 5 ? 5 : eff[10]);
        int32_t research = f.labs_total * research_clamped / 10;
        if (!threat) research *= 2;
        if (out) {
            if (tech) research *= 2;
            if (power) research /= 2;
        }
        score += research;

        // industry
        int32_t behind_clamped = behind < 1 ? 1 : (behind > 2 ? 2 : behind);
        int32_t industry = ((threat + wealth) * 2 + 1) * f.pop_total * behind_clamped * eff[9] / 10;
        if (power) industry *= 2;
        if (wealth) industry += industry / 2;
        if (tech) industry /= 2;
        if (fight < 0 && !wealth) industry /= 2;
        score += industry;

        // upheaval
        int32_t changes = 0;
        for (int32_t i = 0; i < 4; i++) changes += models[i] != soc_current(f)[i];
        score += upheaval_cost(faction_id, changes) / -3;

        // probe
        if (eff[8]) {
            for (int32_t other = 1; other < 8; other++) {
                if (other == faction_id) continue;
                uint32_t status = f.diplo_status[other];
                int32_t threat_other = game().factions[other].mind_control_total / 4 + 1;
                if (status & 1) threat_other /= 2;
                if (!(status & 0x20)) {
                    if (status & 2) threat_other /= 2;
                    if (!(status & 8)) threat_other /= 2;
                    if (!(status & 0x10)) threat_other /= 2;
                }
                int32_t probe = eff[8] < -2 ? -2 : (eff[8] > 3 ? 3 : eff[8]);
                int32_t value = (game().factions[other].base_count + bases) * probe * threat_other / 2;
                if (tech || wealth) value *= 2;
                score += value;
            }
        }
        if (score >= best_score) {
            best_score = score;
            best_choice = choice;
        }
    }
    if (best_choice < 0) {
        if (out)
            for (int32_t i = 0; i < 4; i++) out[i] = soc_pending(f)[i];
        return;
    }
    if (out) {
        for (int32_t i = 0; i < 4; i++) {
            out[i] = best_choice & 3;
            best_choice >>= 2;
        }
        return;
    }
    bool unchanged = true;
    for (int32_t i = 0; i < 4; i++) {
        if ((best_choice & 3) != soc_pending(f)[i]) {
            unchanged = false;
            soc_pending(f)[i] = best_choice & 3;
        }
        best_choice >>= 2;
    }
    if (unchanged) return;
    social_set(faction_id);
    int32_t changes = 0;
    for (int32_t i = 0; i < 4; i++) changes += soc_pending(f)[i] != soc_current(f)[i];
    int32_t cost = upheaval_cost(faction_id, changes);
    int32_t credits = f.energy_credits - cost;
    f.energy_credits = credits < 0 ? 0 : (credits > 999999999 ? 999999999 : credits);
    f.SE_upheaval_cost_paid += cost;
    if (!is_human(faction_id)) consider_designs(faction_id);
}

void GAMEAPI social_select(int32_t faction_id) {
    if (!(GameState & (STATE_SCENARIO_EDITOR | STATE_OMNISCIENT_VIEW)) &&
        (GameMoreRules & MRULES_NO_SOCIAL_ENGINEERING))
        return;
    if (GameState & STATE_OMNISCIENT_VIEW) {
        faction_id = Console_editor_who(&MainConsole, faction_id, 1);
        if (faction_id < 0) return;
    }
    SocialWin_exec(&SocialWin, faction_id);
    SocialWin_close(&SocialWin);
    synch_soc(faction_id);
    if (!MultiplayerActive) social_set(faction_id);
}

void GAMEAPI sort(int32_t count, int32_t* id_list, int32_t* value_list) {
    bool swapped;
    do {
        swapped = false;
        for (int32_t i = 0; i < count - 1 && !swapped; i++) {
            if (value_list[i] > value_list[i + 1]) {
                swap(&value_list[i], &value_list[i + 1]);
                swap(&id_list[i], &id_list[i + 1]);
                swapped = true;
            }
        }
    } while (swapped);
}
