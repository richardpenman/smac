// base_terraform (0x4ECBF0): the AI terraforming planner of the current base (former goals).
#include <cstdlib>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/base_misc.h"
#include "engine/clock.h"
#include "engine/compat.h"
#include "engine/design.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/proto.h"
#include "engine/tech.h"
#include "engine/veh.h"
#include "engine/zoc.h"

namespace {
// The distance measure the original inlines (see veh_vector_dist) from (x, y) to the current base.
int32_t dist_to_base(int32_t x, int32_t y) {
    int32_t dx = std::abs(x - cur_base().x);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    int32_t dy = std::abs(y - cur_base().y);
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t big = a > b ? a : b;
    int32_t small = a < b ? a : b;
    return big - ((((a + b) >> 1) - small + 1) >> 1);
}
// How unsuited (x, y) is to farming: no rain, rocky, or flat and arid.
int32_t poor_soil(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    uint32_t rain = sq->climate & 0x18;
    uint32_t rocky = sq->val3 & 0xC0;
    int32_t count = rain == 0;
    if (rocky == 0x80) count++;
    if (rocky == 0 && rain < 0x10) count++;
    return count;
}
}  // namespace

void GAMEAPI base_terraform(int32_t flag) {
    BASE& base = cur_base();
    int32_t faction_id = (uint8_t)base.faction_id;
    uint8_t bit = (uint8_t)(1 << faction_id);
    bool human = (bit & FactionStatus[0]) != 0;
    int32_t mirror_score = -1, bore_score = -1, cond_score = -1;
    int32_t rain_sum = 0;
    int32_t bore_seen = 0, cond_seen = 0;
    int32_t tech = 0, wealth = 0, power = 0;
    if (!human) {
        tech = game().factions[faction_id].AI_tech;
        wealth = game().factions[faction_id].AI_wealth;
        power = game().factions[faction_id].AI_power;
    } else {
        uint32_t gov = base.governor_flags;
        if (gov & 0x1000000) {
        } else if (gov & 0x2000000) {
            tech = 1;
        } else if (gov & 0x4000000) {
            wealth = 1;
        } else if (gov & 0x8000000) {
            power = 1;
        }
    }
    int32_t pop = base.pop_size;
    int32_t energy_need = ((wealth - power + 4 + tech) * pop + 3) / 5 - base.energy_intake + BaseTerraformEnergy;
    if (energy_need < 0) energy_need = 0;
    else if (energy_need > 99) energy_need = 99;
    int32_t mineral_need = power * 2 - base.mineral_intake + 1 + pop;
    if (mineral_need < 0) mineral_need = 0;
    else if (mineral_need > 99) mineral_need = 99;
    if (energy_need == 0 || energy_need + 1 <= mineral_need) base.state_flags &= ~0x1000;
    else base.state_flags |= 0x1000;
    if (mineral_need == 0 || mineral_need + 1 <= energy_need) base.state_flags &= ~0x2000;
    else base.state_flags |= 0x2000;
    int32_t m2 = mineral_need < 0 ? 0 : (mineral_need > 2 ? 2 : mineral_need);
    int32_t e2 = energy_need < 0 ? 0 : (energy_need > 2 ? 2 : energy_need);
    int32_t food_want = 3 - e2 - m2;
    if (food_want < 0) food_want = 0;
    else if (food_want > 3) food_want = 3;
    int32_t food_need;
    if (base.nutrient_surplus < food_want) {
        food_need = 1;
        base.state_flags |= 0x4000;
        if (base.nutrient_surplus < 0) food_need = 2;
    } else {
        food_need = 0;
        base.state_flags &= ~0x4000;
    }

    // Survey the own territory around the base.
    int32_t sensors = 0, unworked_rich = 0, worked_farmable = 0, unworked_good = 0;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x = map_wrap_x(base.x + TableOffsetX[i]);
        int32_t y = base.y + TableOffsetY[i];
        if (!map_in_bounds(x, y) || (map_tile(x, y)->climate & 0xE0) < 0x60) continue;
        if (whose_territory(faction_id, x, y, nullptr, 0) != faction_id) continue;
        if (is_sensor(x, y)) sensors++;
        MAP* sq = map_tile(x, y);
        uint32_t items = sq->items;
        if (items & BIT_FOREST) continue;
        bool fungus = (items & BIT_FUNGUS) != 0;
        if (fungus && (sq->climate & 0xE0) >= 0x40) continue;
        if (!(base.worked_tiles & (1 << i))) {
            if (((sq->climate & 0x18) >> 3) + ((items >> 15) & 1) < 2) continue;
            bool free = true;
            for (int32_t b = 0; b < game().base_count; b++) {
                BASE& other = game().bases[b];
                if ((uint8_t)other.faction_id != faction_id) continue;
                int32_t dx = std::abs(x - other.x);
                if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
                if (vector_dist(dx, std::abs(y - other.y)) > 2) continue;
                int32_t idx = radius_move_tiles(other.x, other.y, x, y, 21);
                if ((1u << (idx & 31)) & (uint32_t)other.worked_tiles) free = false;
            }
            if (!free) continue;
            unworked_good++;
            if ((map_tile(x, y)->val3 & 0xC0) || bonus_at(x, y)) unworked_rich++;
        } else {
            if (items & BIT_FARM) continue;
            if ((sq->val3 & 0xC0) >= 0x80) continue;
            if (fungus && (sq->climate & 0xE0) >= 0x40) continue;
            if ((sq->climate & 0x18) >= 0x10 && !has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id) &&
                bonus_at(x, y) != 1)
                continue;
            worked_farmable++;
        }
    }

    // Tidal harness site on the largest adjacent ocean.
    int32_t best_region = -1;
    int32_t ocean_tiles = 0;
    int32_t best = 0;
    int32_t site_y = -1, site_x = -1;
    int32_t score = 0;  // carried across tiles like the original's local
    for (int32_t i = 0; i < 21; i++) {
        int32_t weight = 21 - i;
        int32_t x = map_wrap_x(base.x + TableOffsetX[i]);
        int32_t y = base.y + TableOffsetY[i];
        if (!map_in_bounds(x, y)) continue;
        MAP* sq = map_tile(x, y);
        uint32_t alt = sq->climate & 0xE0;
        if (alt >= 0x60) continue;
        ocean_tiles++;
        if (i == 0) continue;
        int32_t region = sq->region;
        if (best_region >= 0 && region != best_region) {
            if (Continents[region].tile_count <= Continents[best_region].tile_count) continue;
        }
        best_region = region;
        if (alt != 0x40) continue;
        if (!can_terraform(faction_id, x, y, 1, CurrentBaseID)) return;
        score = weight;
        if (bonus_at(x, y)) score = weight + 21;
        if (score > best) {
            site_x = x;
            site_y = y;
            best = score;
        }
    }
    if (map_in_bounds(site_x, site_y) && map_tile(site_x, site_y)->region == best_region)
        add_goal(faction_id, 0x19, 2, site_x, site_y, CurrentBaseID);

    int32_t best_idx = -1;
    best = 0;
    base.unk_x = -1;
    int32_t best_kind = 0;
    int32_t rough_tiles = 0;
    base.unk_y = -1;
    int32_t improved = 0, worked_rough = 0, worked_improved = 0, specials = 0;
    Faction& f = game().factions[faction_id];
    int32_t fungus_yield = f.tech_fungus_energy + f.tech_fungus_mineral + f.tech_fungus_nutrient;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x = map_wrap_x(base.x + TableOffsetX[i]);
        int32_t y = base.y + TableOffsetY[i];
        if (!map_in_bounds(x, y)) {
            rain_sum++;
            continue;
        }
        MAP* sq = map_tile(x, y);
        uint32_t alt = sq->climate & 0xE0;
        if (alt < 0x60 && !(flag && (game().mfactions[faction_id].rule_flags & 0x100))) continue;
        int32_t height = (sq->climate >> 5) - 2;
        int32_t rain = (sq->climate >> 3) & 3;
        int32_t rocky = sq->val3 >> 6;
        if (height < 0) height = 0;
        uint32_t items = sq->items;
        bool land_fungus = (items & BIT_FUNGUS) && alt >= 0x40;
        if (items & 0x1C00000) specials++;
        if (i != 0 && (height + rocky + rain > 3 || (items & BIT_FOREST) || (land_fungus && fungus_yield > 3))) {
            rough_tiles++;
            uint32_t worked = (1 << i) & base.worked_tiles;
            if (worked) worked_rough++;
            if (!land_fungus) {
                if ((items & BIT_FOREST) ||
                    (((items & BIT_FARM) || (rocky > 1 && (items & BIT_MINE))) &&
                     (rocky > 1 || (items & BIT_SOLAR) || ((items & BIT_MINE) && (items & BIT_FARM))))) {
                    improved++;
                    if (worked) worked_improved++;
                }
            } else if (fungus_yield < 4) {
                if (!has_tech(Terraform[10].preq_tech, faction_id)) rough_tiles--;
            } else {
                improved++;
            }
        }
        rain_sum += rain;
    }
    if (!human && pop > 3) rough_tiles--;
    int32_t wet = rain_sum > 14;
    if (rain_sum > 25) wet++;

    // Pick the tile to improve, and the sites for a borehole, a condenser and raised land.
    int32_t high_tiles = 0;
    int32_t home_region = map_tile(base.x, base.y)->region;
    int32_t raise_x = site_x, raise_y = site_y;  // the tidal site until something better
    int32_t bore_x = 0, bore_y = 0, cond_x = 0, cond_y = 0;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x = map_wrap_x(base.x + TableOffsetX[i]);
        int32_t y = base.y + TableOffsetY[i];
        if (!map_in_bounds(x, y)) continue;
        uint32_t items = map_tile(x, y)->items;
        if (items & BIT_MONOLITH) continue;
        if (items & BIT_THERMAL_BORE) bore_seen = 1;
        if (items & BIT_CONDENSER) cond_seen = 1;
        if (i == 0) continue;
        if ((items & BIT_FUNGUS) && (map_tile(x, y)->climate & 0xE0) >= 0x40 &&
            !has_tech(Terraform[10].preq_tech, faction_id))
            continue;
        if ((map_tile(x, y)->climate & 0xE0) < 0x60 && !(flag && (game().mfactions[faction_id].rule_flags & 0x100)))
            continue;
        if (map_tile(x, y)->region != home_region) continue;
        int32_t zoc = zoc_move(x, y, faction_id);
        if (zoc >= 0) {
            if (zoc == 1) continue;
            // BUG(original): zoc_move returns 0 when there is no zone of control, so this reads the
            // word before diplo_status (last_base_turn).
            // Fixed (compat out_of_range_reads): no zone of control, no status.
            uint32_t status = zoc ? f.diplo_status[zoc - 1] : g_compat.out_of_range_reads ? (uint32_t)f.last_base_turn : 0;
            if (status & 0xC30) continue;
            MAP* sq = map_tile(x, y);
            if ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8) continue;
        }
        int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
        if (owner >= 0 && owner != faction_id) continue;

        MAP* sq = map_tile(x, y);
        items = sq->items;
        int32_t alt = sq->climate >> 5;
        uint32_t farm = items & BIT_FARM;
        int32_t solar_or_bore = (items & BIT_SOLAR) || (items & BIT_THERMAL_BORE);
        int32_t mine_or_bore = (items & BIT_MINE) || (items & BIT_THERMAL_BORE);
        uint32_t improvements = items & 0x1E00050;
        int32_t food = crop_yield(faction_id, -1, x, y, 1);
        int32_t rocky = map_tile(x, y)->val3 >> 6;
        int32_t bonus = bonus_at(x, y);
        int32_t rain = (map_tile(x, y)->climate & 0x18) >> 3;
        int32_t sensor = is_sensor(x, y);
        sq = map_tile(x, y);
        items = sq->items;
        int32_t has_special = (items & 0x1C00000) != 0;
        uint32_t forest = items & BIT_FOREST;
        int32_t fungus = ((items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) ? 4 : 0;
        uint32_t worked = (1 << i) & base.worked_tiles;
        int32_t height = alt - 2 < 0 ? 0 : alt - 2;
        bool keep_forest = false, eval_fungus = false;
        if (!worked && worked_improved == worked_rough && unworked_rich == 0) {
            if (forest) {
                keep_forest = true;
            } else if (fungus) {
                eval_fungus = true;
            } else if (rain != 0 && rocky < 2) {
                worked = 1;
            }
        } else if (forest) {
            keep_forest = true;
        } else if (fungus) {
            eval_fungus = true;
        }
        if (keep_forest) {
            // Forest stays unless the tile is good enough for something else.
            score = (sensor ? -1 : 1) + height + rocky + rain;
            if (has_tech(Terraform[1].preq_tech, faction_id)) score++;
            if (base_fac(CurrentBaseID, FAC_TREE_FARM)) score--;
            if (base_fac(CurrentBaseID, FAC_HYBRID_FOREST)) score--;
            if (score > 4) forest = 0;
            if (fungus) eval_fungus = true;
        }
        if (eval_fungus) {
            int32_t value = (sensor == 0) + height + rocky + rain;
            if (value < fungus_yield) continue;
            if (value == fungus_yield) fungus = 1;
        }

        // Sensor.
        if (!sensors && !sensor && !bonus && (!improvements || pop > 7) &&
            (!human || (GameMorePreferences & MPREF_AUTO_FORMER_BUILD_SENSORS)) &&
            (!unworked_rich || pop - 1 <= worked_farmable || pop > 1) &&
            (!fungus || has_tech(Rules.tech_preq_improv_fungus, faction_id)) &&
            (rocky > 1 || forest || rain == 0 || (fungus && has_tech(Rules.tech_preq_improv_fungus, faction_id)))) {
            int32_t value = pop + 2 + f.AI_fight * 2;
            score = value * 4;
            if (forest || fungus || rain == 0) score = value * 8;
            bool coastal = false;
            for (int32_t k = 1; k < 21; k++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[k]);
                int32_t y2 = y + TableOffsetY[k];
                if (map_in_bounds(x2, y2) && (map_tile(x2, y2)->climate & 0xE0) < 0x60) {
                    coastal = k < 9;
                    break;
                }
            }
            if (!coastal) score *= 2;
            if (best < score) {
                best_idx = i;
                best_kind = 9;
                best = score;
            }
        }

        // Solar collector.
        if ((energy_need || (base.state_flags & 0x8000)) && !forest && !has_special &&
            (!sensor || 2 - (worked != 0) < sensors) && (!mine_or_bore || (!mineral_need && height > 1))) {
            int32_t high = (energy_need < 2 || height < 2) ? 0 : 1;
            if ((farm != 0) + 1 - high <= food && height) {
                int32_t value = height * 3;
                if (bonus == 2 || rocky == 1) value += 2;
                if (bonus == 1) value += 2;
                else if (bonus == 3) value += farm ? -1 : 1;
                if (farm) value++;
                if (rocky == 2) value -= 3;
                if (map_tile(x, y)->items & BIT_FOREST) value /= 2;
                int32_t mult = energy_need * 2;
                if (mult < 1) mult = 1;
                else if (mult > 99) mult = 99;
                score = mult * value;
                if (worked) score *= 2;
                if (fungus) score += base.eco_damage * fungus * 2;
                if (worked || energy_need > 1) {
                    if (!farm && (bonus == 3 || energy_need == 0 || food_need) && rocky < 2) {
                        if (food_need) score *= 2;
                        if (best < score) {
                            best_idx = i;
                            best_kind = 0;
                            best = score;
                        }
                    } else if (!solar_or_bore) {
                        if (best < score) {
                            best_idx = i;
                            best_kind = 3;
                            best = score;
                        }
                    } else if (!farm && food < 3 && rocky < 2) {
                        if (best < score) {
                            best_idx = i;
                            best_kind = 0;
                            best = score;
                        }
                    }
                }
            }
        }

        // Raise land: only one very high tile may be considered.
        if (alt > 4 && ++high_tiles > 1) mirror_score = -99999;
        if (!wet && mirror_score > -99999 && (mirror_score > -9999 || bonus == 1)) {
            score = TableOffsetX[i] * TableOffsetX[i] * 2;
            if (alt == 3) {
                if (rocky == 1) score += 3;
                if (bonus == 1) score += 6;
            } else {
                if (bonus != 1) score -= food * 3;
                if (rocky == 1) score -= 4;
            }
            int32_t ocean_left = ocean_tiles;
            for (int32_t r = 1, level = alt; r < alt + 1; r++, level--) {
                bool found = false;
                for (int32_t k = TableRange[r - 1]; k < TableRange[r]; k++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[k]);
                    int32_t y2 = y + TableOffsetY[k];
                    if (!map_in_bounds(x2, y2) || (map_tile(x2, y2)->climate >> 5) >= level) continue;
                    found = true;
                    int32_t dist = dist_to_base(x2, y2);
                    if (dist < 3) {
                        score++;
                        if (dist < 2 && (map_tile(x2, y2)->climate & 0xE0) == 0x40) ocean_left--;
                    }
                }
                if (!found) break;
            }
            if (!ocean_tiles || ocean_left > 0) {
                if (mine_or_bore) score -= 4;
                if (solar_or_bore) score += 2;
                if (bonus == 1) score += 8;
                if (i < 8) score /= 2;
                if (mirror_score < score) {
                    raise_x = x;
                    raise_y = y;
                    mirror_score = score;
                }
            }
        }

        // Mine / farm.
        bool farm_eval;  // false: straight on to the fungus / borehole / condenser checks
        if ((!mineral_need && ((rain < 1 - Rules.nutrient_effect_mine_sq && rain) || rocky)) ||
            (sensor && sensors <= 2 - (worked != 0)) || (solar_or_bore && (energy_need || height > 1)) || food_need ||
            forest || (rocky == 1 && height > 1 && mineral_need < 2) ||
            (food + mine_or_bore < (farm != 0) + 2 && rocky == 1)) {
            farm_eval = !farm;
        } else {
            if (rocky == 0) score = food + 1;
            else if (rocky == 1) score = 3 - food;
            else if (rocky == 2) score = 6;
            if (bonus == 1) score += 4;
            else if (bonus == 2) score *= 2;
            else if (bonus == 3) score += 2;
            if (farm) score++;
            MAP* sq2 = map_tile(x, y);
            uint32_t items2 = sq2->items;
            if (items2 & BIT_ROAD) score++;
            if (items2 & BIT_FOREST) score--;
            int32_t value = score * mineral_need;
            score = value * 3;
            if (worked) score = value * 6;
            if (fungus) score += base.eco_damage * fungus * 2;
            bool to_farm = true;  // continue at the farm check
            if (!worked && mineral_need < 2 && (rain == 0 || rocky)) {
            } else if (!mine_or_bore && !has_special) {
                if (best < score) {
                    best_idx = i;
                    best_kind = 2;
                    best = score;
                }
            } else {
                bool road_check = (items2 & BIT_ROAD) != 0;
                if (!road_check) {
                    if (mine_or_bore || (items2 & BIT_THERMAL_BORE)) {
                        if (human && (GameMorePreferences & MPREF_AUTO_FORMER_CANT_BUILD_ROADS)) {
                            road_check = true;
                        } else if (best < score) {
                            best_idx = i;
                            best_kind = 5;
                            best = score;
                        }
                    }
                }
                if (road_check && mine_or_bore && !has_special) {
                    if (!farm && rocky < 2) {
                        if (((sq2->climate & 0x18) || bonus == 1) && best < score) {
                            best_idx = i;
                            best_kind = 0;
                            best = score;
                        }
                    } else {
                        to_farm = false;
                    }
                }
            }
            farm_eval = to_farm && !farm;
        }
        if (farm_eval && rocky < 2 && !forest) {
            bool skip = false;
            if (food_need < 2) {
                int32_t spare = unworked_good;
                if (rocky && food) spare = unworked_rich;
                if (spare && !worked) skip = true;
            }
            if (!skip && (food_need || (mineral_need && rocky && food) ||
                          (rain && (rocky == 1 || rain == 2 || mine_or_bore || solar_or_bore)))) {
                int32_t value = food * 3 + 3;
                if (bonus > 1) value = food * 3 + 6;
                uint32_t items2 = map_tile(x, y)->items;
                if (items2 & BIT_RIVER) value += 1 + energy_need;
                if (rocky == 1) value += 1 + mineral_need;
                if (items2 & BIT_FOREST) value--;
                int32_t mult = food_need + 1;
                if (mult < 1) mult = 1;
                else if (mult > 99) mult = 99;
                score = mult * value * 2;
                if (mineral_need && rocky) score = mult * value * 4;
                if (worked) score *= 2;
                if (rain > 1 && !has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id)) score /= 3;
                if (fungus) score = score / 2 + base.eco_damage * fungus * 2;
                if (best < score) {
                    best_idx = i;
                    best_kind = 0;
                    best = score;
                }
            }
        }

        // Remove fungus.
        sq = map_tile(x, y);
        if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) {
            score = base.eco_damage * fungus * 2;
            if (score != best && best <= score) {
                best_idx = i;
                best_kind = 10;
                best = score;
            }
        }
        // Thermal borehole.
        if (!bore_seen) {
            int32_t extra = (rocky == 2 || rain == 0) ? 12 : 0;
            score = (rocky == 2 ? 12 : 0) - i + 21 + extra;
            if (improvements) score -= 8;
            for (int32_t k = 0; k < 8; k++) {
                int32_t x2 = map_wrap_x(x + NearOffsetX[k]);
                int32_t y2 = y + NearOffsetY[k];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* sq2 = map_tile(x2, y2);
                if ((sq2->climate & 0xE0) < 0x60) continue;
                if ((sq2->climate >> 5) < alt) score = -1;
                if (sq2->items & BIT_THERMAL_BORE) score = -1;
            }
            if (bore_score < score) {
                bore_score = score;
                bore_y = y;
                bore_x = x;
            }
        }
        // Condenser.
        if (!cond_seen) {
            int32_t value = improvements ? 21 - i : 29 - i;
            if (!farm) value += 2;
            score = value + ((2 - rain) * (2 - rain) + rocky * 2) * 2;
            if (MapCloudCover == 0 && rain < 2) score *= 2;
            if (cond_score < score) {
                cond_score = score;
                cond_x = x;
                cond_y = y;
            }
        }
    }

    int32_t road_priority = has_tech(Terraform[6].preq_tech, faction_id) ? 2 : 1;
    if (best_idx < 1) {
        if (!human || !(GameMorePreferences & MPREF_AUTO_FORMER_CANT_BUILD_ROADS)) base_connect(2);
    } else {
        int32_t x = map_wrap_x(base.x + TableOffsetX[best_idx]);
        int32_t y = base.y + TableOffsetY[best_idx];
        if (best_kind == 9) {
            add_goal(faction_id, 0x79, 2, x, y, CurrentBaseID);
        } else if (best_kind == 5 || can_terraform(faction_id, x, y, 1, CurrentBaseID)) {
            add_goal(faction_id, 9, 2, x, y, CurrentBaseID);
        } else {
            road_priority = 2;
        }
        if (!human || !(GameMorePreferences & MPREF_AUTO_FORMER_CANT_BUILD_ROADS)) base_connect(road_priority);
        if (flag) {
            // Instant terraforming (time warp).
            map_tile(x, y)->items &= ~BIT_FUNGUS;
            int32_t order = can_terraform(faction_id, x, y, 1, CurrentBaseID);
            if (ExpansionEnabled && (game().mfactions[faction_id].rule_flags & 0x100)) {
                uint32_t r = (engine_logic_time() + CurrentBaseID + y + x) & 3;
                if (r) r++;
                order = (int32_t)r + 4;
            }
            MAP* sq = map_tile(x, y);
            switch (order) {
            case 4:
                sq->items |= BIT_FARM | BIT_ROAD;
                if (!(sq->climate & 7) && (sq->climate & 0xE0) <= 0x60) sq->items |= BIT_MINE;
                else sq->items |= BIT_SOLAR;
                break;
            case 6:
                sq->items |= BIT_MINE | BIT_ROAD;
                if ((sq->val3 & 0xC0) < 0x80) sq->items |= BIT_FARM;
                break;
            case 7:
                sq->items |= BIT_SOLAR | BIT_ROAD;
                if ((sq->val3 & 0xC0) < 0x80) sq->items |= BIT_SOLAR;
                break;
            case 8: sq->items |= BIT_FOREST; break;
            default: break;
            }
        }
    }

    pop = cur_base().pop_size;
    bool done = false;
    bool no_raise;
    if (improved < pop + 1 && improved < rough_tiles) {
        no_raise = true;
    } else {
        no_raise = false;
        if (!bore_seen && bore_score >= 1 && cur_base().eco_damage == 0 && poor_soil(bore_x, bore_y)) {
            bool ok = true;
            if (pop < 8 && energy_need < 2) {
                int32_t abil = abil_index(1);
                if (!has_tech(Ability[abil].preq_tech, faction_id) && CurrentTurn <= 200) ok = false;
            }
            if (ok && terrain_avail(14, 0, faction_id) &&
                (!human || (faction_id == CurrentPlayerFaction && (GamePreferences & PREF_AUTO_FORMER_BUILD_ADV)))) {
                add_goal(faction_id, 0x69, 1, bore_x, bore_y, CurrentBaseID);
                done = true;
            }
        }
        if (!done && !cond_seen && cond_score > 0 && cur_base().eco_damage == 0) {
            bool want = false;
            if (!wet) {
                want = true;
            } else if (wet < 2 && specials == 0) {
                if (food_need) {
                    want = true;
                } else if (poor_soil(cond_x, cond_y)) {
                    int32_t abil = abil_index(1);
                    if (has_tech(Ability[abil].preq_tech, faction_id) || CurrentTurn > 200) want = true;
                }
            }
            if (want && terrain_avail(12, 0, faction_id) &&
                (!human || (faction_id == CurrentPlayerFaction && (GamePreferences & PREF_AUTO_FORMER_BUILD_ADV)))) {
                add_goal(faction_id, 0x49, 1, cond_x, cond_y, CurrentBaseID);
                done = true;
            }
        }
    }
    if (!done &&
        !((wet && energy_need < 3) || mirror_score < 1 || no_raise ||
          (human && (faction_id != CurrentPlayerFaction || !(GamePreferences & PREF_AUTO_FORMER_RAISE_LWR_TERRAIN))) ||
          !terrain_avail(16, 0, faction_id)))
        add_goal(faction_id, 0x29, 1, raise_x, raise_y, CurrentBaseID);

    BASE& b = cur_base();
    int32_t x = map_wrap_x(b.x - 8);
    int32_t y = b.y;
    uint32_t alt = map_in_bounds(x, y) ? (map_tile(x, y)->climate & 0xE0) : 0;
    if (!map_in_bounds(x, y) || alt < 0x60 || !wet || alt >= 0xC0) {
        b.unk_x = -1;
        b.unk_y = -1;
    } else {
        b.unk_x = (int16_t)x;
        b.unk_y = b.y;
    }
    // The original continues with a raise-land goal east of the base when wet != 2, behind a test
    // that is always false ((CurrentTurn >= wet ? 0 : 25) + 25 != 0 skips it): dead code.
}
