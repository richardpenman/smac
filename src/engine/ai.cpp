// Computer player strategy and unit movement (original module around 0x55F930-0x579770).
#include "engine/ai.h"

#include <cstdlib>

#include "engine/action.h"
#include "engine/crt.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/goals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/probe.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/zoc.h"

namespace {
inline int32_t tile_region(int32_t x, int32_t y) { return map_tile(x, y)->region; }
// The inlined distance of the original (vector_dist with wrapped dx).
inline int32_t tile_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return veh_vector_dist(x1, y1, x2, y2); }
// The original's "no moves left" test: speed - moves_spent <= 0.
inline bool veh_moves_done(int32_t veh_id) {
    int32_t left = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
    return left < 0 || (left < 1000 && left == 0);
}
}  // namespace

int32_t (*g_harness_enemy_veh)(int32_t) = nullptr;
void (*g_harness_enemy_veh_pump)() = nullptr;

// ---- capabilities -------------------------------------------------------------------------------

void GAMEAPI enemy_capabilities(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    f.best_psi_land_offense = veh_avail(BSC_MIND_WORMS, faction_id, -1) ? weap_strat(WPN_PSI_ATTACK, faction_id) : 0;
    f.best_weapon_value = 1;
    for (int32_t i = 0; i < 26; i++) {
        if (!has_tech(Weapon[i].preq_tech, faction_id) || Weapon[i].offense_value >= 99) continue;
        int32_t value = weap_strat(i, faction_id);
        if (Weapon[i].offense_value < 0 && value >= f.best_psi_land_offense) f.best_psi_land_offense = value;
        if (value >= f.best_weapon_value) f.best_weapon_value = value;
    }
    f.best_psi_land_defense = veh_avail(BSC_MIND_WORMS, faction_id, -1) ? arm_strat(ARM_PSI_DEFENSE, faction_id) : 0;
    f.best_armor_value = 1;
    for (int32_t i = 0; i < 14; i++) {
        if (!has_tech(Armor[i].preq_tech, faction_id)) continue;
        int32_t value = arm_strat(i, faction_id);
        if (Armor[i].defense_value < 0 && value >= f.best_psi_land_defense) f.best_psi_land_defense = value;
        if (value >= f.best_armor_value) f.best_armor_value = value;
    }
    f.best_land_speed = 1;
    for (int32_t i = 0; i < 9; i++) {
        if (has_tech(Chassis[i].preq_tech, faction_id) && Chassis[i].triad == TRIAD_LAND &&
            (int32_t)Chassis[i].speed >= f.best_land_speed)
            f.best_land_speed = Chassis[i].speed;
    }
    f.enemy_best_land_speed = 0;
    f.enemy_best_armor_value = 0;
    f.enemy_best_weapon_value = 0;
    f.enemy_best_psi_land_defense = 0;
    f.enemy_best_psi_land_offense = 0;
    // Enemy values from the most hostile non-pact factions known: at war, then without a treaty,
    // then any with contact, finally any.
    for (int32_t pass = 0; pass < 4; pass++) {
        if (f.enemy_best_weapon_value != 0) return;
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id) continue;
            uint32_t status = f.diplo_status[i];
            if (status & DIPLO_PACT) continue;
            if (pass == 0 && !(status & DIPLO_VENDETTA)) continue;
            if (pass <= 1 && (status & DIPLO_TREATY)) continue;
            if (pass <= 2 && !(status & DIPLO_COMMLINK)) continue;
            const Faction& e = Factions[i];
            if (e.best_weapon_value >= f.enemy_best_weapon_value) f.enemy_best_weapon_value = e.best_weapon_value;
            if (e.best_armor_value >= f.enemy_best_armor_value) f.enemy_best_armor_value = e.best_armor_value;
            if (e.best_land_speed >= f.enemy_best_land_speed) f.enemy_best_land_speed = e.best_land_speed;
            if (e.best_psi_land_offense >= f.enemy_best_psi_land_offense)
                f.enemy_best_psi_land_offense = e.best_psi_land_offense;
            if (e.best_psi_land_defense >= f.enemy_best_psi_land_defense)
                f.enemy_best_psi_land_defense = e.best_psi_land_defense;
        }
    }
}

// ---- small helpers ------------------------------------------------------------------------------

void GAMEAPI go_to(int32_t veh_id, int32_t type, int32_t x, int32_t y) {
    VEH& v = Vehs[veh_id];
    v.order = ORDER_MOVE_TO;
    v.status_icon = (uint8_t)type;
    v.waypoint_x[0] = (int16_t)x;
    v.waypoint_y[0] = (int16_t)y;
    if (Units[v.unit_id].plan == PLAN_COLONY) v.movement_turns = 0;
}

int32_t GAMEAPI garrison_check(int32_t base_id) {
    const BASE& b = Bases[base_id];
    int32_t faction_id = (uint8_t)b.faction_id;
    int32_t region = tile_region(b.x, b.y);
    int32_t projects = 0;
    for (int32_t i = 0; i < 64; i++) {
        if (SecretProjects[i] == base_id) projects++;
    }
    int32_t base = (projects + 2) / 3 + (b.pop_size + 1) / 4;
    int32_t offset, mask;
    bitmask(FAC_HEADQUARTERS, &offset, &mask);
    int32_t want;
    if ((b.facilities_built[offset] & (uint8_t)mask) || (map_tile(b.x, b.y)->items & BIT_DOUBLE_SEA) ||
        is_objective(base_id)) {
        want = base + 2;
    } else {
        want = base + 1;
    }
    if (is_objective(base_id) && (Factions[faction_id].player_flags & PFLAG_STRAT_DEF_OBJECTIVES)) want++;
    if (Factions[faction_id].region_base_plan[region] == 8) want--;
    int32_t enemy = zoc_sea(b.x, b.y, faction_id) - 1;
    if (enemy > 0 &&
        ((Factions[faction_id].diplo_status[enemy] & (DIPLO_VENDETTA | DIPLO_WANT_REVENGE)) ||
         (Factions[enemy].diplo_status[faction_id] & DIPLO_WANT_REVENGE) || Factions[enemy].integrity_blemishes > 3)) {
        want++;
    }
    if (Factions[faction_id].SE_support_pending < -3) want--;
    if (Factions[faction_id].SE_support_pending < -2 && want > 2) want--;
    if (want < 1) return 1;
    if (want > 10) return 10;
    return want;
}

int32_t GAMEAPI defensive_check(int32_t base_id) {
    int32_t want = garrison_check(base_id);
    if (want > 5) want--;
    if (want > 2) want--;
    return want;
}

int32_t GAMEAPI guard_check(int32_t faction_id, int32_t region) {
    if (region >= 64) return 0;
    const Faction& f = Factions[faction_id];
    int32_t per = 2 - f.AI_fight;
    if (f.region_base_plan[region] == 8) {
        per = 4 - f.AI_fight;
    } else if (f.region_base_plan[region] == 2) {
        per = 1;
    }
    if (f.player_flags & PFLAG_STRAT_DEF_OBJECTIVES) per = 1;
    return (f.region_total_bases[region] - 1 + per) / per;
}

void GAMEAPI set_course(int32_t veh_id, int32_t type, int32_t x, int32_t y) {
    const VEH& v = Vehs[veh_id];
    int32_t veh_region = tile_region(v.x, v.y);
    int32_t base_id = base_at(v.x, v.y);
    MAP* target = map_tile(x, y);
    int32_t target_region = target->region;
    if (veh_region == target_region && map_is_ocean(v.x, v.y)) {
        go_to(veh_id, type, x, y);
        return;
    }
    // Best ocean tile within radius 2 of the target, by the number of free land tiles of the
    // target's region next to it.
    int32_t best = ((target->climate & 0xE0) >= 0x60) - 1;
    for (int32_t i = 0; i < 21; i++) {
        int32_t sx = map_wrap_x(TableOffsetX[i] + x);
        int32_t sy = TableOffsetY[i] + y;
        if (!map_in_bounds(sx, sy)) continue;
        MAP* sq = map_tile(sx, sy);
        if ((sq->climate & 0xE0) >= 0x60) continue;
        if (base_id < 0) {
            if (sq->region != veh_region) continue;
        } else if (!base_on_sea(base_id, sq->region)) {
            continue;
        }
        int32_t count = 0;
        for (int32_t j = 0; j < 8; j++) {
            int32_t nx = map_wrap_x(NearOffsetX[j] + sx);
            int32_t ny = NearOffsetY[j] + sy;
            if (!map_in_bounds(nx, ny)) continue;
            MAP* n = map_tile(nx, ny);
            if ((n->climate & 0xE0) >= 0x60 && n->region == target_region &&
                (!(n->items & BIT_BASE_IN_TILE) || (n->val2 & 0xF) > 7)) {
                count++;
            }
        }
        if (count > best) {
            best = count;
            go_to(veh_id, type, sx, sy);
        }
    }
}

void GAMEAPI assemble_passengers(int32_t base_id, int32_t plan, int32_t priority) {
    const BASE& b = Bases[base_id];
    int32_t faction_id = (uint8_t)b.faction_id;
    int32_t x = b.x, y = b.y;
    int32_t region = tile_region(x, y);
    int32_t enemies = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i != faction_id && (Factions[faction_id].diplo_status[i] & DIPLO_VENDETTA) &&
            (!Factions[i].region_total_bases[region] || !contiguous(i, faction_id, region, 1))) {
            enemies++;
        }
    }
    Bases[base_id].state_flags |= BSTATE_UNK_1;
    Bases[base_id].ai_plan_status = 2;
    if (plan != PLAN_COLONY || (Factions[faction_id].region_flags[region] & 0x800)) {
        int32_t want = Factions[faction_id].AI_growth - Factions[faction_id].AI_fight - CurrentTurn / 100 + 2;
        if (want < 0) want = 0;
        else if (want > 99) want = 99;
        if (enemies < want) {
            int32_t p = stack_check(veh_at(x, y), 2, PLAN_COLONY, faction_id, -1) ? 1 : priority;
            add_goal(faction_id, AI_GOAL_COLONIZE, p, x, y, -1);
        }
        if (plan < 3) goto done;
    }
    add_goal(faction_id, AI_GOAL_DEFEND, priority, x, y, -1);
    add_goal(faction_id, AI_GOAL_ATTACK, priority, x, y, -1);
    add_goal(faction_id, AI_GOAL_UNK_3, priority, x, y, -1);
done:
    if (plan == PLAN_NAVAL_TRANSPORT) add_goal(faction_id, AI_GOAL_SCOUT, 1, x, y, -1);
}

int32_t GAMEAPI can_convoy(int32_t x, int32_t y) {
    if (stack_check(veh_at(x, y), 9, 3, -1, -1)) return 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        const BASE& b = Bases[i];
        if (tile_dist(x, y, b.x, b.y) < 3) {
            int32_t r = radius_move_2(b.x, b.y, x, y, 21);
            if (r >= 0 && ((1u << (r & 31)) & (uint32_t)b.worked_tiles)) return 0;
        }
    }
    return 1;
}

int32_t GAMEAPI good_sensor(int32_t faction_id, int32_t x, int32_t y) {
    if (whose_territory(faction_id, x, y, nullptr, 0) != faction_id) return 0;
    MAP* sq = map_tile(x, y);
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 && !has_tech(Rules.tech_preq_improv_fungus, faction_id)) {
        return 0;
    }
    uint32_t flags = 0;
    for (int32_t i = 0; i < 25; i++) {
        int32_t sx = map_wrap_x(TableOffsetX[i] + x);
        int32_t sy = TableOffsetY[i] + y;
        if (!map_in_bounds(sx, sy)) continue;
        if (is_sensor(sx, sy) && whose_territory(faction_id, sx, sy, nullptr, 0) == faction_id) return 0;
        if (i < 9) {
            int32_t base_id = base_at(sx, sy);
            if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id) {
                uint32_t next = flags | 6;
                for (int32_t j = 0; j < 25; j++) {
                    int32_t bx = map_wrap_x(TableOffsetX[j] + sx);
                    int32_t by = TableOffsetY[j] + sy;
                    if (map_in_bounds(bx, by) && is_sensor(bx, by) &&
                        whose_territory(faction_id, bx, by, nullptr, 0) == faction_id) {
                        next = flags | 2;
                        break;
                    }
                }
                flags = next;
            }
            if (whose_territory(faction_id, sx, sy, nullptr, 0) != faction_id) flags |= 1;
        }
    }
    if (!flags || bonus_at(x, y)) return 0;
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) <= 7) return 0;
    uint32_t items = sq->items;
    if (items & (BIT_MONOLITH | BIT_CONDENSER | BIT_THERMAL_BORE)) return 0;
    if (items & (BIT_MINE | BIT_SOLAR)) return 0;
    if ((sq->val3 & 0xC0) > 0x40 || !(sq->climate & 0x18) || ((items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40)) {
        return 1;
    }
    return 0;
}

int32_t GAMEAPI ocean_access_near_base(int32_t faction_id, int32_t x, int32_t y) {
    int32_t base_found = -1;
    if (bonus_at(x, y) == 3) return 1;
    for (int32_t i = 0; i < 8; i++) {
        int32_t sx = map_wrap_x(NearOffsetX[i] + x);
        int32_t sy = NearOffsetY[i] + y;
        if (!map_in_bounds(sx, sy)) continue;
        int32_t base_id = base_at(sx, sy);
        if (base_id < 0) continue;
        uint32_t owner = (uint8_t)Bases[base_id].faction_id;
        if (owner != (uint32_t)faction_id && !(Factions[faction_id].diplo_status[owner] & DIPLO_PACT)) continue;
        base_found = base_id;
        if (owner == (uint32_t)faction_id) break;
    }
    if (base_found >= 0) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t sx = map_wrap_x(NearOffsetX[i] + Bases[base_found].x);
            int32_t sy = NearOffsetY[i] + Bases[base_found].y;
            if ((sx == x && sy == y) || !map_in_bounds(sx, sy)) continue;
            MAP* sq = map_tile(sx, sy);
            int32_t alt = sq->climate & 0xE0;
            if (alt < 0x60 && (!(sq->items & BIT_FUNGUS) || alt < 0x40)) return 1;
        }
    }
    return 0;
}

int32_t GAMEAPI get_there(int32_t veh_id, int32_t x, int32_t y) {
    const VEH& v = Vehs[veh_id];
    int32_t triad = veh_triad(veh_id);
    if (triad == TRIAD_AIR) return 1;
    int32_t region = (int8_t)tile_region(v.x, v.y);
    int32_t region_tgt = (int8_t)tile_region(x, y);
    if (triad != TRIAD_LAND) {
        int32_t base_id = base_at(v.x, v.y);
        int32_t base_id_tgt = base_at(x, y);
        if (base_id < 0) {
            if (base_id_tgt >= 0) return base_on_sea(base_id_tgt, region);
        } else if (base_id_tgt >= 0) {
            return port_to_port(base_id, base_id_tgt);
        }
        if (!map_is_ocean(x, y)) {
            if (base_id >= 0) return port_to_coast(base_id, region_tgt);
            return sea_coast(region_tgt, region);
        }
        if (base_id >= 0) return base_on_sea(base_id, region_tgt);
    }
    return region == region_tgt;
}

int32_t GAMEAPI coast_or_border(int32_t x, int32_t y, int32_t x_dst, int32_t y_dst, int32_t faction_id) {
    if (whose_territory(faction_id, x, y, nullptr, 0) != faction_id) return 0;
    int32_t region = (int8_t)tile_region(x_dst, y_dst);
    for (int32_t i = 1; i < 9; i++) {
        int32_t sx = map_wrap_x(TableOffsetX[i] + x);
        int32_t sy = TableOffsetY[i] + y;
        if (!map_in_bounds(sx, sy)) continue;
        // BUG(original): compares the region of (x, y), not of the neighbour.
        if (map_is_ocean(sx, sy) || whose_territory(faction_id, sx, sy, nullptr, 0) != faction_id ||
            (int8_t)tile_region(x, y) != region) {
            return i;
        }
    }
    return 0;
}

int32_t GAMEAPI alien_base(int32_t veh_id, int32_t x, int32_t y) {
    int32_t best_dist = 9999;
    BaseFindDist = 9999;
    int32_t best = -1;
    int32_t region = tile_region(x, y);
    for (int32_t i = 0; i < BaseCount; i++) {
        const BASE& b = Bases[i];
        bool ok = region > 63 || region == tile_region(b.x, b.y);
        if (!ok && Vehs[veh_id].unit_id == BSC_SEALURK) ok = base_on_sea(i, region) != 0;
        if (!ok) continue;
        int32_t dist = tile_dist(x, y, b.x, b.y) * 32 / (b.mineral_intake_2 + 32 + b.energy_intake_2);
        if (stack_check(veh_at(b.x, b.y), 2, PLAN_ARTIFACT, -1, -1)) dist /= 2;
        if (i == Vehs[veh_id].home_base_id) dist /= 2;
        if ((uint32_t)Vehs[veh_id].order_auto_type != (uint8_t)b.faction_id && Factions[(uint8_t)b.faction_id].SE_planet > 0 &&
            b.eco_damage == 0) {
            dist *= 2;
        }
        if (dist <= best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    if (best >= 0) BaseFindDist = best_dist;
    return best;
}

int32_t GAMEAPI compute_odds(int32_t odds, int32_t faction_id, int32_t veh_id, int32_t veh_id_tgt, int32_t base_id) {
    int32_t tx = Vehs[veh_id_tgt].x;
    int32_t ty = Vehs[veh_id_tgt].y;
    int32_t unit_id = Vehs[veh_id].unit_id;
    int32_t unit_tgt = Vehs[veh_id_tgt].unit_id;
    int32_t triad = unit_triad(unit_id);
    uint8_t plan = (uint8_t)Units[unit_id].plan;
    const Faction& f = Factions[faction_id];
    if (base_id < -1) base_id = base_at(tx, ty);
    int32_t region_plan = f.region_base_plan[tile_region(tx, ty)];
    if (base_id < 0) {
        int32_t cost = Units[unit_tgt].cost;
        int32_t total = stack_check(veh_id_tgt, 7, -1, -1, -1);
        odds = ((total - cost) / 5 + cost) * odds;
        if (stack_check(veh_id_tgt, 2, PLAN_ARTIFACT, -1, -1)) odds = odds * 3 / 2;
    } else {
        odds = Units[unit_tgt].cost * odds;
    }
    odds = odds / (int32_t)Units[unit_id].cost;
    if ((uint8_t)Units[unit_tgt].plan > 6) odds *= 2;
    if (plan < 2 && region_plan == 0 && triad == TRIAD_LAND) odds = odds * 3 / 2;
    if (base_id >= 0 || at_goal(faction_id, AI_GOAL_ATTACK, tx, ty)) {
        if ((triad == TRIAD_SEA) == map_is_ocean(tx, ty) && triad != TRIAD_AIR) {
            odds *= 2;
        } else if (base_id >= 0) {
            int32_t a = stack_check(veh_id_tgt, 0x13, -1, -1, -1);
            int32_t n = stack_check(veh_id_tgt, 1, -1, -1, -1);
            if (n - a < 2) odds *= 2;
        }
    }
    if (Vehs[veh_id_tgt].flags & VFLAG_IS_OBJECTIVE) odds *= 4;
    int32_t result;
    if (triad == TRIAD_AIR) {
        bool missile = Chassis[Units[unit_id].chassis_id].missile != 0;
        if (missile && map_is_ocean(tx, ty) && base_id < 0) odds *= 2;
        if (stack_check(veh_id_tgt, 2, PLAN_TERRAFORM, -1, -1)) {
            if (!missile) odds *= 2;
            if (map_is_ocean(tx, ty) && (f.player_flags & PFLAG_UNK_80000)) odds *= 2;
            if (Vehs[veh_id_tgt].order == ORDER_TERRAFORM_UP) odds *= 2;
            if (Vehs[veh_id_tgt].order == ORDER_TERRAFORM_DOWN) odds *= 2;
        }
        result = odds;
        if (base_id >= 0) {
            for (int32_t i = 0; i < 8; i++) {
                int32_t sx = map_wrap_x(NearOffsetX[i] + tx);
                int32_t sy = NearOffsetY[i] + ty;
                if (!map_in_bounds(sx, sy)) continue;
                int32_t v = veh_at(sx, sy);
                if (Vehs[v].faction_id == (uint32_t)faction_id && stack_check(v, 3, 0, -1, -1)) {
                    odds *= 2;
                    break;
                }
            }
            int32_t a = stack_check(veh_id_tgt, 0x13, -1, -1, -1);
            int32_t n = stack_check(veh_id_tgt, 1, -1, -1, -1);
            result = odds;
            if (n - a <= f.units_active[unit_id] / 10 + 1) {
                for (int32_t i = 0; i < 64; i++) {
                    int32_t u = faction_id * 64 + i;
                    if ((Units[u].unit_flags & 1) && (Units[u].unit_flags & 4) && f.units_active[u] &&
                        has_abil(u, ABL_DROP_POD)) {
                        result = missile ? odds * 4 : odds * 2;
                        break;
                    }
                }
            }
        }
    } else {
        result = odds;
        if (base_id >= 0) {
            int32_t near = 0;
            for (int32_t i = 0; i < 8; i++) {
                int32_t sx = map_wrap_x(NearOffsetX[i] + tx);
                int32_t sy = NearOffsetY[i] + ty;
                if (!map_in_bounds(sx, sy)) continue;
                int32_t v = veh_at(sx, sy);
                // BUG(original): v may be -1 (no unit): Vehs[-1] lies in the Reactor table.
                uint32_t v_faction = (uint32_t)v < 2049 ? Vehs[v].faction_id
                                                        : rules_orig_byte(0x952828 + (uint32_t)v * 0x34 + offsetof(VEH, faction_id));
                if (v_faction == (uint32_t)faction_id) near += stack_check(v, 3, 0, -1, -1);
            }
            int32_t n = stack_check(veh_id_tgt, 0x10, -1, -1, -1);
            if (n) {
                while (n < near && odds < 1000000) {
                    near -= n;
                    odds *= 2;
                }
            }
            result = odds;
        }
    }
    odds = result;
    if (plan == PLAN_AIR_SUPERIORITY) {
        if (unit_triad(unit_tgt) != TRIAD_AIR && Weapon[Units[unit_tgt].weapon_id].offense_value != 0) return odds / 2;
    } else if (triad == TRIAD_AIR && plan == PLAN_OFFENSE && !Chassis[Units[unit_id].chassis_id].missile) {
        if (region_plan == 2) return odds * 2 / 3;
        return odds / 3;
    }
    return odds;
}

// ---- turn driver --------------------------------------------------------------------------------

int32_t GAMEAPI enemy_veh(int32_t veh_id) {
    int32_t faction_id = Vehs[veh_id].faction_id;
    bool synch = faction_id == CurrentPlayerFaction && MultiplayerActive;
    if (MultiplayerActive) VehAttackFlags = 3;
    if (g_harness_enemy_veh_pump) {
        g_harness_enemy_veh_pump();
    } else {
        do_all_non_input();
    }
    VEH& v = Vehs[veh_id];
    if (!v.moves_spent || v.order != ORDER_MOVE_TO || Units[v.unit_id].plan == PLAN_NAVAL_TRANSPORT ||
        (v.state & 0x4000) || zoc_any(v.x, v.y, faction_id)) {
        if (enemy_move(veh_id)) return 1;
        if (veh_moves_done(veh_id)) {
            if (MultiplayerActive) log_say_2("Enemy Vehicle DONE", veh_id, 0, 0);
            if (synch) {
                synch_veh(veh_id);
                NetDaemon_await_synch(&NetState);
                NetDaemon_unlock_veh(&NetState);
            }
            return 1;
        }
    }
    if (synch) {
        synch_veh(veh_id);
        NetDaemon_await_synch(&NetState);
        if (NetDaemon_action(&NetState, veh_id, 1)) return 1;
    } else {
        if (MultiplayerActive) {
            // The original appends the number to a 1-byte string on its stack (overrunning it);
            // multiplayer logging only.
            char num[80];
            crt_itoa(veh_id, num, 10);
            log_say("Enemy Vehicle ACTION", num, (int8_t)v.order, v.waypoint_x[0], v.waypoint_y[0]);
        }
        if (!action(veh_id)) veh_skip(veh_id);
        if (MultiplayerActive) {
            char label[2] = {(char)v.status_icon, 0};
            log_say("Enemy Vehicle", label, veh_id, v.x, v.y);
        }
    }
    return 0;
}

void GAMEAPI enemy_turn(int32_t faction_id) {
    for (int32_t pass = 0; pass < 10; pass++) {
        for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
            if (veh_id >= VehCount || Vehs[veh_id].faction_id != (uint32_t)faction_id) continue;
            int32_t unit_id = Vehs[veh_id].unit_id;
            bool filter = true;  // common filter: no drop pods, air superiority, group 0x15
            switch (pass) {
            case 0:
                if (!can_arty(unit_id, 1)) continue;
                break;
            case 1:
                if (Units[unit_id].plan != PLAN_DEFENSE &&
                    (Units[unit_id].plan != PLAN_NAVAL_TRANSPORT || !stack_check(veh_id, 3, 0, -1, -1))) {
                    continue;
                }
                break;
            case 2:
                if (unit_triad(unit_id) != TRIAD_AIR || Units[unit_id].plan == PLAN_AIR_SUPERIORITY) continue;
                break;
            case 3:
                if (Units[unit_id].plan != PLAN_COMBAT) continue;
                break;
            case 4:
                if (Vehs[veh_id].damage_taken) continue;
                break;
            default:
                filter = pass < 7;
                break;
            }
            if (filter) {
                if (has_abil(Vehs[veh_id].unit_id, ABL_DROP_POD) || Units[Vehs[veh_id].unit_id].plan == PLAN_AIR_SUPERIORITY ||
                    Units[Vehs[veh_id].unit_id].group_id == 0x15) {
                    continue;
                }
            }
            if (pass == 8 && (Units[Vehs[veh_id].unit_id].plan != PLAN_NAVAL_TRANSPORT || !faction_id)) continue;
            int32_t count = 0;
            while (Vehs[veh_id].order != ORDER_SENTRY_BOARD) {
                if ((pass != 8 || count) && veh_moves_done(veh_id)) break;
                int32_t veh_count = VehCount;
                int32_t done = g_harness_enemy_veh ? g_harness_enemy_veh(veh_id) : enemy_veh(veh_id);
                if (done || veh_count != VehCount) break;
                if (Vehs[veh_id].order != ORDER_MOVE_TO && veh_triad(veh_id) != TRIAD_AIR) count++;
                count++;
                if (count > 32) veh_skip(veh_id);
            }
        }
    }
    Factions[faction_id].player_flags &= ~PFLAG_UNK_10000;
}

// ---- terraforming -------------------------------------------------------------------------------

int32_t GAMEAPI can_terraform(int32_t faction_id, int32_t x, int32_t y, int32_t flag, int32_t base_id) {
    MAP* sq = map_tile(x, y);
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return 0;
    uint32_t items = sq->items;
    int32_t fungus = (items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40;
    uint32_t mine = items & BIT_MINE;
    uint32_t solar = items & BIT_SOLAR;
    uint32_t farm = items & BIT_FARM;
    uint32_t enricher = items & BIT_SOIL_ENRICHER;
    uint32_t forest = items & BIT_FOREST;
    int32_t special = (items & (BIT_CONDENSER | BIT_ECH_MIRROR | BIT_THERMAL_BORE)) ||
                      ((sq->landmarks & 0x80000002) == 2 && !(sq->landmarks & 0xFF000000));
    const Faction& f = Factions[faction_id];
    int32_t fungus_tech = f.tech_fungus_energy + f.tech_fungus_mineral + f.tech_fungus_nutrient;
    int32_t forest_value = ResInfo.forest_sq.energy + ResInfo.forest_sq.mineral + ResInfo.forest_sq.nutrient;
    if (base_id >= 0) {
        int32_t offset, mask;
        bitmask(FAC_TREE_FARM, &offset, &mask);
        if (Bases[base_id].facilities_built[offset] & (uint8_t)mask) forest_value++;
        bitmask(FAC_HYBRID_FOREST, &offset, &mask);
        if (Bases[base_id].facilities_built[offset] & (uint8_t)mask) forest_value += 2;
    }
    bool human = (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0;
    if (f.AI_growth && !f.AI_power && !f.AI_wealth && !f.AI_tech && f.AI_fight < 0 && !human) forest_value++;
    uint32_t alt = sq->climate & 0xE0;
    if (alt < 0x60) {
        if (fungus) {
            if (fungus_tech > 3) return 0;
            if (human && !(GameMorePreferences & 8)) return 0;
            return has_tech(Terraform[FORMER_REMOVE_FUNGUS].preq_tech_sea, faction_id) ? ORDER_REMOVE_FUNGUS : 0;
        }
        if (alt < 0x40) return terrain_avail(FORMER_RAISE_LAND, 1, faction_id) ? ORDER_TERRAFORM_UP : 0;
        if (sq->items & BIT_THERMAL_BORE) return 0;
        if (!farm && has_tech(Terraform[FORMER_FARM].preq_tech_sea, faction_id)) return ORDER_FARM;
        if (!solar && !mine && !special) {
            bonus_at(x, y);
            if (base_id >= 0) {
                const BASE& b = Bases[base_id];
                if ((b.state_flags & BSTATE_UNK_2000) && has_tech(Terraform[FORMER_MINE].preq_tech_sea, faction_id)) {
                    return ORDER_MINE;
                }
                if ((b.state_flags & BSTATE_UNK_1000) && has_tech(Terraform[FORMER_SOLAR].preq_tech_sea, faction_id)) {
                    return ORDER_SOLAR_COLLECTOR;
                }
                if (((b.state_flags & BSTATE_UNK_4000) || b.nutrient_surplus < 2) &&
                    has_tech(Terraform[FORMER_SOLAR].preq_tech_sea, faction_id) && Rules.nutrient_effect_mine_sq < 0) {
                    return ORDER_SOLAR_COLLECTOR;
                }
                int32_t solars = 0;
                int32_t mines = Rules.nutrient_effect_mine_sq < 0;
                for (int32_t i = 0; i < 21; i++) {
                    int32_t sx = map_wrap_x(TableOffsetX[i] + b.x);
                    int32_t sy = TableOffsetY[i] + b.y;
                    if (!map_in_bounds(sx, sy) || !map_is_ocean(sx, sy)) continue;
                    uint32_t it = map_tile(sx, sy)->items;
                    if (it & BIT_MINE) mines++;
                    if (it & BIT_SOLAR) solars++;
                }
                int32_t no_mine_tech = has_tech(Terraform[FORMER_MINE].preq_tech_sea, faction_id) == 0;
                if (solars < no_mine_tech + mines && has_tech(Terraform[FORMER_SOLAR].preq_tech_sea, faction_id)) {
                    return ORDER_SOLAR_COLLECTOR;
                }
            }
            if (has_tech(Terraform[FORMER_MINE].preq_tech_sea, faction_id) &&
                (Rules.nutrient_effect_mine_sq >= 0 ||
                 (base_id >= 0 && Bases[base_id].nutrient_surplus > 1 && !(Bases[base_id].state_flags & BSTATE_UNK_4000)))) {
                return ORDER_MINE;
            }
        }
        if (farm && !enricher && has_tech(Terraform[FORMER_SOIL_ENR].preq_tech_sea, faction_id)) return ORDER_FARM;
        return 0;
    }
    int32_t rain = (sq->climate & 0x18) >> 3;
    int32_t rocky = sq->val3 >> 6;
    int32_t result = 0;
    int32_t food = crop_yield(faction_id, -1, x, y, 0);
    int32_t elev = (int32_t)(alt >> 5) - 2;
    if (elev < 0) elev = 0;
    int32_t bonus = bonus_at(x, y);
    int32_t value = elev + rain + 1 + rocky;
    int32_t value_farm = value;
    if (has_tech(Terraform[FORMER_SOIL_ENR].preq_tech, faction_id) && farm) value_farm = value + 1;
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (owner >= 0 && owner != faction_id && !(Factions[faction_id].diplo_status[owner] & DIPLO_PACT)) return 0;
    if (fungus) {
        if (base_id >= 0 && Bases[base_id].eco_damage / 2 + value_farm <= fungus_tech) return 0;
        if (human && !(GameMorePreferences & 8)) return 0;
        return has_tech(Terraform[FORMER_REMOVE_FUNGUS].preq_tech_sea, faction_id) ? ORDER_REMOVE_FUNGUS : 0;
    }
    if (map_tile(x, y)->items & BIT_MONOLITH) return 0;
    if (forest && value_farm <= forest_value) return 0;
    int32_t mine_tech = has_tech(Terraform[FORMER_MINE].preq_tech, faction_id);
    int32_t solar_tech = has_tech(Terraform[FORMER_SOLAR].preq_tech, faction_id);
    has_tech(Terraform[FORMER_FARM].preq_tech, faction_id);
    if (!mine && !solar && !special) {
        if (solar_tech && elev && (((rocky == 1 || elev >= 2) && food >= (farm != 0) + 1) || flag)) result = ORDER_SOLAR_COLLECTOR;
        if (mine_tech) {
            int32_t t;
            if (bonus == 2 || rocky == 2) result = ORDER_MINE;
            if (bonus == 2) {
                if (rocky == 2) goto mine_set;
            } else if (rocky == 2) {
                goto mine_checks;
            }
            if (Rules.nutrient_effect_mine_sq >= 0) goto mine_checks;
            t = (farm == 0) + food;
            if (t <= 1) goto mine_checks;
            if (rocky != 0 || t <= 2) goto solar_tail;
            goto mine_flag;
        mine_checks:
            if (rocky == 1 && elev == 0) result = ORDER_MINE;
            if (bonus != 3) goto mine_flag;
        mine_set:
            result = ORDER_MINE;
        mine_flag:
            if (!flag) goto base_wants;
            if (farm && !result) result = ORDER_MINE;
        }
    solar_tail:
        if (flag && farm && !result && elev && solar_tech) result = ORDER_SOLAR_COLLECTOR;
    base_wants:
        if (base_id >= 0) {
            uint32_t state = Bases[base_id].state_flags;
            if ((state & BSTATE_UNK_1000) && solar_tech && elev && ((farm != 0) + 1 <= food || elev > 1)) {
                result = ORDER_SOLAR_COLLECTOR;
            }
            if ((state & BSTATE_UNK_2000) && mine_tech) {
                if ((farm != 0) + 2 <= food) result = ORDER_MINE;
                if (((rocky > 1) || bonus == 2 || (rain && rocky < 2 && elev < 2)) && (farm || food > 1)) {
                    result = ORDER_MINE;
                }
            }
        }
    }
    if (has_tech(Terraform[FORMER_FARM].preq_tech, faction_id) && !farm && rocky < 2) {
        uint32_t it = map_tile(x, y)->items;
        if (!(it & BIT_THERMAL_BORE) && (map_tile(x, y)->landmarks & 0x80000002) != 2 && !(it & BIT_ECH_MIRROR) &&
            (food < 2 || has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id) || bonus == 1)) {
            if (solar || mine) result = ORDER_FARM;
            bool farm_now;
            if (base_id >= 0 && (Bases[base_id].state_flags & BSTATE_UNK_4000)) {
                result = ORDER_FARM;
                farm_now = rain < 2;
            } else if (result == ORDER_SOLAR_COLLECTOR) {
                if (food < 2) result = ORDER_FARM;
                farm_now = rain < 2;
            } else if (result != 0) {
                farm_now = rain < 2;
            } else {
                farm_now = true;
            }
            if (farm_now) result = ORDER_FARM;
        }
    }
    if (has_tech(Terraform[FORMER_SOIL_ENR].preq_tech, faction_id) && farm && !enricher) {
        bool set = false;
        if (food <= 1 || (food == 2 && !has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id))) {
            set = !result && (flag || food > 1);
        } else {
            if (solar || mine) result = ORDER_FARM;
            if (base_id >= 0 && (Bases[base_id].state_flags & BSTATE_UNK_4000)) {
                set = true;
            } else {
                set = !result && (flag || food > 1);
            }
        }
        if (set) result = ORDER_FARM;
    }
    if (has_tech(Terraform[FORMER_SOIL_ENR].preq_tech, faction_id) && farm) value++;
    if (base_id >= 0) {
        uint32_t state = Bases[base_id].state_flags;
        if (state & BSTATE_UNK_4000) {
            if (rain > 1) value++;
            if (rain == 0) value--;
        }
        if (state & BSTATE_UNK_1000) {
            if (elev > 1) value++;
            if (elev == 0) value--;
        }
        if ((state & BSTATE_UNK_2000) && rocky == 0) value--;
        if (!(state & (BSTATE_UNK_2000 | BSTATE_UNK_4000)) && solar) value++;
    }
    if (rain && elev && rocky == 1 && CurrentTurn < 50) value++;
    if (has_tech(Terraform[FORMER_FOREST].preq_tech, faction_id) && rocky < 2 && !mine && !forest &&
        (!farm || (elev == 0 && rain < 2 && !human)) && !special &&
        (map_tile(x, y)->landmarks & 0x80000002) != 2 && value < forest_value &&
        (!human || (faction_id == CurrentPlayerFaction && (GamePreferences & 0x1000000)))) {
        result = ORDER_PLANT_FOREST;
    }
    if (has_tech(Terraform[FORMER_PLANT_FUNGUS].preq_tech, faction_id) && !human) {
        if (rocky == 2) value += (mine != 0) + 1;
        if (!special && value + 1 < fungus_tech) result = ORDER_PLANT_FUNGUS;
    }
    return result;
}

// ---- native life --------------------------------------------------------------------------------

namespace {
// Bases[base_id] also for base_id -1 (the original reads the bytes before the table then).
inline BASE& base_any(int32_t base_id) { return (&Bases[0])[base_id]; }
// Tile with no unit/base of a non-native faction (owner nibble 0 or >= 8, or no such item bits).
inline bool tile_native_or_free(const MAP* sq, uint32_t bits) {
    int32_t owner = sq->val2 & 0xF;
    return !(sq->items & bits) || owner > 7 || owner == 0;
}
}  // namespace

int32_t GAMEAPI alien_move(int32_t veh_id) {
    VEH& v = Vehs[veh_id];
    if (veh_moves_done(veh_id)) return 0;
    if (v.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) {
        veh_skip(veh_id);
        return 0;
    }
    if (v.unit_id == BSC_UNITY_SCOUT_CHOPPER) {
        veh_skip(veh_id);
        return 0;
    }
    int32_t x = v.x, y = v.y;
    int32_t base_id = alien_base(veh_id, x, y);
    uint32_t target_faction;
    if (base_id < 0) {
        if (!zoc_any(x, y, 0)) {
            kill(veh_id);
            return 1;
        }
        target_faction = 0;
        BaseFindDist = 99;
    } else {
        target_faction = (uint8_t)Bases[base_id].faction_id;
    }
    BASE& base = base_any(base_id);
    int32_t base_region = tile_region(base.x, base.y);
    v.movement_turns++;
    int32_t dir = -1;
    int32_t best = 0;
    int32_t score = 0;  // BUG(original): read before written below (0x3039 test); never 0x3039 initially
    if (veh_cargo(veh_id) && stack_check(veh_id, 3, 0, -1, -1)) {
        // Isle of the Deep carrying units: land them next to the target base.
        if (v.movement_turns > 24) {
            kill(veh_id);
            return 1;
        }
        for (int32_t i = 0; i < 8; i++) {
            int32_t sx = map_wrap_x(NearOffsetX[i] + x);
            int32_t sy = NearOffsetY[i] + y;
            if (!map_in_bounds(sx, sy)) continue;
            MAP* sq = map_tile(sx, sy);
            if (tile_native_or_free(sq, BIT_VEH_IN_TILE)) {
                if ((sq->climate & 0xE0) < 0x60 || sq->region != base_region) continue;
                if (base_id >= 0) {
                    int32_t dx = std::abs(sx - base.x);
                    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
                    int32_t dy = std::abs(sy - base.y);
                    if ((int32_t)((dy + dx) & ~1u) > 12) continue;
                }
                uint8_t vis = map_tile(sx, sy)->visibility;
                for (int32_t u = stack_top(veh_id); u >= 0; u = Vehs[u].next_veh_id_stack) {
                    Vehs[u].order = ORDER_NONE;
                    Vehs[u].visibility |= vis;
                }
                veh_skip(veh_id);
                if (target_faction != (uint32_t)CurrentPlayerFaction) return 0;
                if ((v.faction_id != (uint32_t)CurrentPlayerFaction || (v.flags & 0x440) == 0x440) &&
                    !((uint8_t)(1 << (CurrentPlayerFaction & 31)) & v.visibility)) {
                    return 0;
                }
                if (!Console_focus(&MainConsole, sx, sy, CurrentPlayerFaction)) draw_tile(sx, sy, 2);
                parse_says(0, base.name, -1, -1);
                if (MultiplayerActive) {
                    NetMsg_pop(&NetMsg, v.unit_id == BSC_SPORE_LAUNCHER ? "SPORELAUNCHSWARM" : "MINDWORMSWARM", 3000, 0,
                               nullptr);
                } else if (v.unit_id == BSC_SPORE_LAUNCHER) {
                    popp(ScriptFileName, "SPORELAUNCHSWARM", 0, "sporlnch_sm.pcx", 0);
                } else {
                    popp(ScriptFileName, "MINDWORMSWARM", 0, "mindworm_sm.pcx", 0);
                }
                return 0;
            } else if ((sq->climate & 0xE0) < 0x60) {
                dir = i;  // attack the ship next to us
                goto move;
            }
        }
        set_course(veh_id, 99, base.x, base.y);
        if (v.order != ORDER_MOVE_TO) return 0;
        if (map_is_ocean(v.waypoint_x[0], v.waypoint_y[0])) return 0;
        {
            int32_t dx = std::abs(x - v.waypoint_x[0]);
            if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
            int32_t dy = std::abs(y - v.waypoint_y[0]);
            if ((int32_t)((dy + dx) & ~1u) > 2) return 0;
        }
        veh_skip(veh_id);
        return 0;
    }
    {
        int32_t triad = veh_triad(veh_id);
        if ((BaseFindDist > 7 || tile_region(x, y) != base_region) && !zoc_any(x, y, 0) &&
            (uint32_t)(triad != TRIAD_LAND ? 16 : 6) < v.movement_turns && ((CurrentTurn + veh_id) & 7) == 0) {
            kill(veh_id);
            return 1;
        }
        int32_t heading;
        if (base_id < 0 || Factions[target_faction].base_count < 3 || CurrentTurn < 51 ||
            (v.order_auto_type != target_faction && Factions[target_faction].SE_planet > 0 &&
             v.unit_id != BSC_SEALURK)) {
            heading = (int8_t)v.rotate_angle;
        } else {
            v.waypoint_x[0] = Bases[base_id].x;
            v.waypoint_y[0] = Bases[base_id].y;
            heading = Path_move(&Paths, veh_id, 0);
        }
        if (can_arty(v.unit_id, 1)) {
            int32_t arty_best = 0;
            int32_t arty = -1;
            for (int32_t i = 0; i < 25; i++) {
                int32_t sx = map_wrap_x(TableOffsetX[i] + x);
                int32_t sy = TableOffsetY[i] + y;
                if (!map_in_bounds(sx, sy)) continue;
                MAP* sq = map_tile(sx, sy);
                if (!(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) || (sq->val2 & 0xF) > 7) {
                    if ((sq->climate & 0xE0) >= 0x60 && (sq->items & 0x81208854)) {
                        score = (sq->items & BIT_SENSOR) ? 32 : 2;
                        if (i == 0) score = 0;
                        if (arty_best < score) {
                            arty = i;
                            arty_best = score;
                        }
                    }
                } else {
                    int32_t top = stack_fix(veh_at(sx, sy));
                    if (top >= 0) {
                        score = (stack_check(top, 2, PLAN_NAVAL_SUPERIORITY, -1, -1) + stack_check(top, 6, ABL_ARTILLERY, -1, -1)) * 8;
                        if (!zoc_veh(x, y, 0) || i < 9) score += stack_check(top, 4, -1, -1, -1);
                        int32_t dx = std::abs(x - sx);
                        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
                        int32_t dy = std::abs(y - sy);
                        if (Rules.artillery_max_rng < (dy + dx) >> 1 && score > 1) score = 1;
                        if (i == 0) score = 0;
                        if (arty_best < score) {
                            arty = i;
                            arty_best = score;
                        }
                    }
                }
            }
            if (arty < 0) {
                stack_check(veh_id, 0xF, -1, -1, -1);
            } else {
                int32_t tx = map_wrap_x(TableOffsetX[arty] + x);
                int32_t ty = TableOffsetY[arty] + y;
                int32_t dx = std::abs(x - tx);
                if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
                int32_t dy = std::abs(y - ty);
                if ((dy + dx) >> 1 <= Rules.artillery_max_rng) {
                    MAP* sq = map_tile(tx, ty);
                    if (!(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) || (sq->val2 & 0xF) > 7) {
                        log_say_2("Enemy doing ARTY destroy 1...", veh_id, tx, ty);
                        action_destroy(veh_id, 0, tx, ty);
                        for (int32_t f = 1; f < 8; f++) synch_bit(tx, ty, f);
                        draw_tile(tx, ty, 2);
                        return 1;
                    }
                    if (arty > 8) {
                        log_say_2("Enemy doing ARTY...", veh_id, arty, 0);
                        battle_fight(veh_id, arty, 1, 1, nullptr);
                        return 1;
                    }
                }
            }
        }
        dir = -1;
        best = 0;
        int32_t morale = morale_alien(-1, 0);
        bool here_land = (map_tile(x, y)->climate & 0xE0) >= 0x60;
        for (int32_t i = 0; i < 8; i++) {
            int32_t sx = map_wrap_x(NearOffsetX[i] + x);
            int32_t sy = NearOffsetY[i] + y;
            if (!map_in_bounds(sx, sy)) continue;
            MAP* sq = map_tile(sx, sy);
            if ((sq->climate & 0xE0) < 0x60) {
                if (veh_triad(veh_id) == TRIAD_LAND) continue;
            } else {
                if (veh_triad(veh_id) == TRIAD_SEA) {
                    if (v.unit_id != BSC_SEALURK || base_at(sx, sy) == -1) continue;
                    score = 0x3039;
                }
                if (!here_land && veh_triad(veh_id) == TRIAD_LAND && !tile_native_or_free(sq, BIT_VEH_IN_TILE)) continue;
            }
            if (score != 0x3039) score = 0;
            if (tile_native_or_free(sq, BIT_BASE_IN_TILE)) {
                int32_t other = veh_at(sx, sy);
                if (other >= 0 && Vehs[other].faction_id != 0) {
                    int32_t u = Vehs[other].unit_id;
                    if ((u < 64 && Weapon[Units[u].weapon_id].offense_value < 0) ||
                        (unit_triad(u) == TRIAD_AIR && veh_triad(veh_id) != TRIAD_AIR)) {
                        continue;
                    }
                    score += 999;
                }
            } else {
                score += 9999;
            }
            uint32_t items = map_tile(sx, sy)->items;
            int32_t s = score + bit_count(items & 0x80208054) * 4;
            if (DiffLevel > 2) s += ((items & BIT_MONOLITH) && !(v.state & 0x100)) ? 8 : 0;
            s += (int32_t)game_rand() % 8;
            int32_t turn = (int32_t)(((uint32_t)heading ^ 0xFFFFFFFCu) - (uint32_t)i) & 7;
            if (turn > 4) turn = 8 - turn;
            score = s + (morale + 1) * turn;
            if (i == heading) score += morale * morale;
            if (best < score) {
                dir = i;
                best = score;
            }
        }
        uint32_t aggression = morale == 0 ? 0 : morale == 1 ? 1 : morale == 2 ? 2 : 4;
        if (v.damage_taken) aggression *= 2;
        int32_t owner = (uint8_t)base.faction_id;
        if ((FactionStatus[0] & (uint8_t)(1 << (owner & 31))) && aggression) {
            int32_t r = (int32_t)game_rand() % 4;
            if (r < (((best < 999) * aggression) != 0) + 1) {
                int32_t left = speed(veh_id, 0) - v.moves_spent;
                if (left < 0) left = 0;
                else if (left > 999) left = 999;
                if (left <= Rules.move_rate_roads && veh_triad(veh_id) != TRIAD_AIR) {
                    int32_t level = (GameRules & 0x40) ? 5 : Factions[owner].diff_level;
                    if (level > 0 && (int32_t)game_rand() % (level + 1) != 0 &&
                        (map_tile(x, y)->items & 0x80E88058)) {
                        action_destroy(veh_id, 0, -1, -1);
                        return 0;
                    }
                }
            }
        }
        if (best < 999 && v.moves_spent == 0 && v.damage_taken) {
            MAP* sq = map_tile(x, y);
            if (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40) aggression = (aggression + 1) / 2;
            if (aggression && (int32_t)game_rand() % (int32_t)(aggression + 1) != 0) dir = -1;
        }
        if (best < 99 && veh_cargo(veh_id) && !stack_check(veh_id, 3, 0, -1, -1)) {
            kill(veh_id);
            return 1;
        }
    }
move:
    if (dir < 0) {
        v.order = ORDER_NONE;
        v.state &= ~0x10000u;
        veh_skip(veh_id);
        return 0;
    }
    v.order = ORDER_AI_GO_TO;
    v.waypoint_x[0] = (int16_t)map_wrap_x(NearOffsetX[dir] + x);
    v.waypoint_y[0] = (int16_t)(NearOffsetY[dir] + y);
    return 0;
}

// ---- planet busters -----------------------------------------------------------------------------

int32_t GAMEAPI enemy_planet_buster(int32_t veh_id) {
    uint32_t faction_id = Vehs[veh_id].faction_id;
    if (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) return -1;
    const Faction& f = Factions[faction_id];
    int32_t building = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        int32_t item = Bases[i].queue_items[0];
        if ((uint8_t)Bases[i].faction_id == faction_id && item >= 0 && Weapon[Units[item].weapon_id].offense_value > 'b') {
            building++;
        }
    }
    int32_t busters = 0;
    for (int32_t i = 0; i < VehCount; i++) {
        if (Vehs[i].faction_id == faction_id && Weapon[Units[Vehs[i].unit_id].weapon_id].offense_value > 'b') busters++;
    }
    int32_t range = TableRange[planet_buster(veh_id)];
    int32_t own_base = 0;  // pass 1: own base in reach of the target (always set before use)
    for (int32_t pass = 0; pass < 2; pass++) {
        int32_t best = 0;
        int32_t best_base = -1;
        int32_t best_own = 0;
        for (int32_t target = 0; target < BaseCount; target++) {
            uint32_t enemy = (uint8_t)Bases[target].faction_id;
            if (faction_id == enemy) continue;
            uint32_t status = f.diplo_status[enemy];
            if (status & DIPLO_PACT) continue;
            if ((status & 7) && !((status & 0xC20) && at_climax(faction_id))) continue;
            int32_t value = 0;
            if (pass == 0) {
                int32_t dist = std::abs(Vehs[veh_id].x - Bases[target].x);
                if (!(MapToggleFlat & 1) && dist > MapHalfX) dist = MapAreaX - dist;
                dist += std::abs(Vehs[veh_id].y - Bases[target].y);
                if (dist >> 1 > speed(veh_id, 0) / Rules.move_rate_roads) continue;
            } else {
                bool reach = false;
                for (int32_t j = 0; j < BaseCount; j++) {
                    if ((uint8_t)Bases[j].faction_id == faction_id) {
                        int32_t dist = std::abs(Bases[j].x - Bases[target].x);
                        if (!(MapToggleFlat & 1) && dist > MapHalfX) dist = MapAreaX - dist;
                        int32_t dy = std::abs(Bases[j].y - Bases[target].y);
                        if ((dy + dist) >> 1 <= speed(veh_id, 0) / Rules.move_rate_roads) {
                            reach = true;
                            own_base = j;
                        }
                    }
                    if ((uint8_t)Bases[j].faction_id == enemy && (uint8_t)Bases[j].faction_id_former == faction_id) value++;
                }
                if (!reach) continue;
            }
            if (at_climax(faction_id)) value += 2;
            const Faction& e = Factions[enemy];
            if (!(f.diplo_status[enemy] & DIPLO_WANT_REVENGE) && !(f.player_flags & PFLAG_COMMIT_ATROCITIES_WANTONLY)) {
                int32_t level = (GameRules & 0x40) ? 6 : e.diff_level;
                if (value < 9 - level &&
                    (!(FactionStatus[0] & (uint8_t)(1 << (enemy & 31))) ||
                     (!(GameRules & 0x40) && (ProposalPassCount[8] <= ProposalPassCount[9] || f.AI_fight < 1)) ||
                     e.ranking < f.ranking || reputation(enemy, faction_id) == 0)) {
                    continue;
                }
            }
            if (!at_climax(faction_id) || f.AI_fight < 0 || !(GameRules & 0x40)) {
                if (!(f.diplo_status[enemy] & DIPLO_MAJOR_ATROCITY_VICTIM)) {
                    if (!(f.player_flags & PFLAG_COMMIT_ATROCITIES_WANTONLY)) {
                        int32_t level = (GameRules & 0x40) ? 6 : e.diff_level;
                        if (e.integrity_blemishes + (ProposalPassCount[9] < ProposalPassCount[8]) * 4 + level + value < 11 ||
                            (e.mil_strength_1 <= f.mil_strength_1 * 2 && (value < 4 || e.ranking < f.ranking))) {
                            continue;
                        }
                    }
                } else if (e.major_atrocities <= f.major_atrocities && e.mil_strength_1 <= f.mil_strength_1 * 2 &&
                           !at_climax(faction_id) &&
                           ((value < 4 && ProposalPassCount[8] <= ProposalPassCount[9]) || e.ranking < f.ranking)) {
                    continue;
                }
            }
            // Value of the blast area: enemy bases and units minus own units.
            value = 0;
            for (int32_t k = 0; k < range; k++) {
                int32_t sx = map_wrap_x(TableOffsetX[k] + Bases[target].x);
                int32_t sy = TableOffsetY[k] + Bases[target].y;
                if (!map_in_bounds(sx, sy)) continue;
                int32_t base_id = base_at(sx, sy);
                if (base_id >= 0) {
                    uint32_t owner = (uint8_t)Bases[base_id].faction_id;
                    if (owner == faction_id || (f.diplo_status[owner] & 7)) {
                        value = -9999;
                        break;
                    }
                    int32_t s = value + Factions[owner].ranking * Bases[base_id].pop_size;
                    int32_t offset, mask;
                    bitmask(FAC_HEADQUARTERS, &offset, &mask);
                    if ((Bases[base_id].facilities_built[offset] & (uint8_t)mask) &&
                        CurrentTurn < Factions[(uint8_t)Bases[base_id].faction_id].corner_market_turn) {
                        s += 2000;
                    }
                    if (Bases[base_id].queue_items[0] == -0x66) s += Bases[base_id].minerals_accumulated * 2;
                    if (SecretProjects[15] == base_id) s += 1000;
                    if (SecretProjects[24] == base_id) s += 500;
                    for (int32_t p = 0; p < 64; p++) {
                        if (SecretProjects[p] == base_id) s += Factions[(uint8_t)Bases[base_id].faction_id].ranking * 60;
                    }
                    value = s;
                }
                int32_t prev = value;
                int32_t top = stack_fix(veh_at(sx, sy));
                if (top < 0) {
                    if (k == 0) {
                        value = -9999;
                        break;
                    }
                } else {
                    int32_t v;
                    if (Vehs[top].faction_id == faction_id) {
                        v = -(stack_check(top, 7, -1, -1, -1) * f.ranking);
                    } else {
                        v = stack_check(top, 7, -1, -1, -1) * Factions[Vehs[top].faction_id].ranking;
                        int32_t region = tile_region(Vehs[top].x, Vehs[top].y);
                        if (region < 0x40) {
                            if (f.region_base_plan[region] == 2) {
                                v *= 2;
                            } else if (f.region_base_plan[region] != 0) {
                                v /= 2;
                                if (f.region_total_bases[region] == 0) v /= 2;
                            }
                        }
                    }
                    value = prev + v;
                }
            }
            int32_t odp = e.satellites_ODP - e.ODP_deployed;
            if (busters < odp + 1) {
                if (building) continue;
                value /= 2;
                if (busters < odp / 2) value /= 2;
            }
            if (best < value) {
                best_base = target;
                best_own = own_base;
                best = value;
            }
        }
        if (pass == 0) {
            if ((int32_t)Units[Vehs[veh_id].unit_id].cost * f.ranking <= best) {
                const BASE& b = base_any(best_base);
                for (int32_t i = 0; i < 8; i++) {
                    int32_t sx = map_wrap_x(NearOffsetX[i] + b.x);
                    int32_t sy = NearOffsetY[i] + b.y;
                    if (!map_in_bounds(sx, sy)) continue;
                    MAP* sq = map_tile(sx, sy);
                    if (!(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) || (sq->val2 & 0xF) >= 8) {
                        veh_drop(veh_lift(veh_id), sx, sy);
                        Vehs[veh_id].visibility = 0;
                        return i ^ 4;
                    }
                }
            }
        } else if (best_own >= 0 && (uint8_t)Bases[best_own].faction_id == faction_id) {
            // BUG(original): best_own stays 0 when no target was found (moves to base 0 if own).
            veh_drop(veh_lift(veh_id), Bases[best_own].x, Bases[best_own].y);
            Vehs[veh_id].visibility = 0;
        }
    }
    return -1;
}

// ---- diplomacy ----------------------------------------------------------------------------------

void GAMEAPI enemy_diplomacy(int32_t faction_id) {
    if (faction_id == 0) return;
    Faction& f = Factions[faction_id];
    for (int32_t i = 1; i < 8; i++) {
        f.diplo_status[i] &= ~0x100u;
        if (f.diplo_agenda[i] & 0x20) {
            if (!(f.diplo_agenda[i] & 0x40)) {
                f.diplo_agenda[i] |= 0x40;
                Factions[i].diplo_agenda[faction_id] |= 0x40;
            } else {
                f.diplo_agenda[i] &= ~0x20u;
                Factions[i].diplo_agenda[faction_id] &= ~0x20u;
            }
        }
    }
    auto bit = [](int32_t n) { return (uint8_t)(1 << (n & 31)); };
    if (FactionStatus[0] & bit(faction_id)) return;
    int32_t wars = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || !(FactionStatus[1] & bit(i))) continue;
        uint32_t& s = f.diplo_status[i];
        uint32_t& o = Factions[i].diplo_status[faction_id];
        if (s & DIPLO_VENDETTA) wars++;
        if (f.diplo_merc[i] != 0 && --f.diplo_merc[i] == 0) s &= ~0x40u;
        if ((CurrentTurn & 1) && f.diplo_patience[i] > 0) f.diplo_patience[i]--;
        if (s & DIPLO_SHALL_BETRAY) {
            int32_t chance = 8 - ((f.diplo_agenda[i] >> 9) & 2) - Factions[i].integrity_blemishes;
            if (chance < 1) chance = 1;
            else if (chance > 8) chance = 8;
            if (chance - 1 <= 0 || (int32_t)game_rand() % chance == 0 || (s & DIPLO_PACT)) {
                if (!(s & DIPLO_PACT)) {
                    if (Factions[i].target_x < 0) {
                        int32_t& spoke = Factions[i].diplo_spoke[faction_id];
                        int32_t t = CurrentTurn - 8;
                        if (t < spoke) t = spoke;
                        spoke = t;
                        s = (s & 0xFFFDFBFF) | 0x840;
                        f.diplo_merc[i] = 50;
                    } else {
                        s |= 0x20000;
                    }
                } else {
                    cause_friction(faction_id, i, 20);
                    s &= 0xFFFDF3FF;
                    treaty_on(faction_id, i, DIPLO_WANT_TO_TALK);
                }
            }
        }
        if ((CurrentTurn & 0xF) == faction_id * 2 + 1) {
            s &= ~0x8000u;
            o &= ~0x8000u;
        }
        uint8_t humans = FactionStatus[0];
        if (!(humans & bit(i)) || SunspotDuration > 0) {
            if (i < faction_id && SunspotDuration < 1 && (((CurrentTurn + i + faction_id) & 7) == 0 || (s & 0x2000))) {
                if (!(s & 0x80000000) || (s & 0x2000)) {
                    s &= ~0x2000u;
                    o &= ~0x2000u;
                    if (s & DIPLO_PACT) {
                        enemies_trade_tech(faction_id, i);
                    } else if (((s & DIPLO_TREATY) && (GameState & 0x200)) ||
                               (SecretProjects[4] >= 0 && (uint8_t)Bases[SecretProjects[4]].faction_id == (uint32_t)faction_id) ||
                               (SecretProjects[4] >= 0 && (uint8_t)Bases[SecretProjects[4]].faction_id == (uint32_t)i) ||
                               (s & 0x20000000) || ((s & DIPLO_COMMLINK) && (s & 0x10000000))) {
                        encounter(faction_id, i, -1, -1, 0, 0);
                    }
                    treaty_on(faction_id, i, 0x80000000);
                    s &= ~0x4000000u;
                    o &= ~0x4000000u;
                } else {
                    s &= 0x7FFFFFFF;
                    o &= 0x7FFFFFFF;
                    for (int32_t k = 1; k < 8; k++) {
                        if ((humans & bit(k)) &&
                            ((f.diplo_status[k] & DIPLO_VENDETTA) || (Factions[i].diplo_status[k] & DIPLO_VENDETTA))) {
                            s &= ~0x4000000u;
                            o &= ~0x4000000u;
                        }
                    }
                }
            }
            continue;
        }
        // Human faction i: offers to talk, battle plans against common enemies.
        bool talk = false;
        bool flagged = false;  // skip to the state check (0x55FDF2)
        if (i == CurrentPlayerFaction && (s & 0x40018) == DIPLO_COMMLINK) {
            uint8_t alive = FactionStatus[1];
            for (int32_t k = 1; k < 8; k++) {
                if (!(alive & bit(k)) || k == faction_id || k == i || (f.diplo_status[k] & 0x8018) != 0x8018 ||
                    (Factions[i].diplo_status[k] & 0x18) != DIPLO_COMMLINK ||
                    Factions[k].mil_strength_1 <= f.mil_strength_1) {
                    continue;
                }
                if (!(s & DIPLO_PACT) && reputation(i, faction_id) < 5 &&
                    ((CurrentTurn - Factions[i].diplo_spoke[faction_id] > 8 && (CurrentTurn & 0xF) == faction_id * 2) ||
                     (s & 0x2000))) {
                    DiploStateA[faction_id] |= 0x41;
                    DiploStateB[i][faction_id] |= 0x41;
                    if (MultiplayerActive || PbemActive) {
                        flagged = true;
                    } else {
                        talk = true;
                    }
                }
                break;
            }
            if (!talk && !flagged && !(s & 0x20) &&
                (CurrentTurn - Factions[i].diplo_spoke[faction_id] > 32 ||
                 ((s & 0x20000000) && CurrentTurn == faction_id + 8) || ((s & DIPLO_PACT) && (s & 0x2000))) &&
                (game_random(0, 16) == 0 || (s & 0x2000) || CurrentTurn == faction_id + 8)) {
                if (!MultiplayerActive && !PbemActive) {
                    talk = true;
                } else {
                    DiploStateA[faction_id] |= 1;
                    DiploStateB[i][faction_id] |= 1;
                }
            }
            if (talk) {
                communicate(i, faction_id, 0);
                treaty_on(i, faction_id, 0x88000008);
            }
        }
        int32_t state = PbemActive ? DiploStateB[i][faction_id] : DiploStateA[faction_id];
        if (state == 0) {
            diplomacy_check(i, faction_id, 0);
            if (DiploAttackChance && ((s & 0x20) || ((uint8_t)f.player_flags_ext & 0x20) ||
                                      (Factions[i].diff_level > 4 && (GameRules & 0x40)))) {
                s = (s & 0xFFFDFBFF) | 0x840;
                f.diplo_merc[i] = 50;
            }
        }
    }
    int32_t last = 0;  // territory's first output (the original keeps it in one local)
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[0] & bit(i))) continue;
        uint32_t& s = f.diplo_status[i];
        if (!(s & DIPLO_COMMLINK)) continue;
        int32_t n;
        if (CurrentTurn - Factions[i].diplo_spoke[faction_id] < 4 && SunspotDuration < 1 && !(s & 0x20)) {
            n = 0;
        } else {
            int32_t other = 0;
            n = territory(faction_id, i, 1, &last, &other);
            if (n && i == CurrentPlayerFaction && !(s & DIPLO_VENDETTA)) {
                if (SunspotDuration < 1) {
                    uint32_t flags = 0;
                    if (s & DIPLO_TREATY) {
                        if (n >= 5) {
                            DiploStateB[i][faction_id] |= 4;
                            DiploStateA[faction_id] |= 4;
                        } else if (((CurrentTurn + faction_id) & 3) == 0) {
                            flags = 0x20;
                        }
                    } else if ((f.diplo_agenda[i] & 0x800) || ((f.diplo_agenda[i] & 0x400) && other)) {
                        flags = 0x18;
                    } else {
                        int32_t r = -1;
                        if ((s & 0x20) && other == 0) {
                            DiploStateA[faction_id] |= 8;
                            DiploStateB[i][faction_id] |= 8;
                            r = game_random(0, n + (Factions[i].integrity_blemishes + 1) * 5);
                        } else if (((CurrentTurn + faction_id) & 3) == 0) {
                            DiploStateA[faction_id] |= 8;
                            DiploStateB[i][faction_id] |= 8;
                            r = game_random(0, ((s & 0x20) ? 5 : 0) + n + (Factions[i].integrity_blemishes + other) * 5);
                        }
                        if (r > 10) {
                            if (i == CurrentPlayerFaction) DiploStateA[faction_id] |= 0x10;
                            DiploStateB[i][faction_id] |= 0x10;
                        }
                    }
                    if (flags) {
                        DiploStateA[faction_id] |= flags;
                        DiploStateB[i][faction_id] |= flags;
                    }
                    if (DiploStateA[faction_id] != 0 && !MultiplayerActive && !PbemActive) {
                        communicate(i, faction_id, 0);
                        treaty_on(i, faction_id, 0x88000008);
                    }
                } else {
                    diplomacy_check(i, faction_id, 0);
                    if (DiploHostility || (s & 0x20) ||
                        (9 - f.AI_fight) * 2 <= DiploFriction + Factions[i].integrity_blemishes * 2 + n) {
                        if (s & DIPLO_TREATY) {
                            s |= 0x800;
                        } else if (!(s & 0xC00)) {
                            s |= (s & 0x20) ? 0x800 : 0x400;
                        }
                    }
                }
            }
        }
        const Faction& e = Factions[i];
        n = ((CurrentTurn & 1) + 3 + n) / 5;
        if (!(s & DIPLO_PACT)) {
            if ((GameRules & 0x40) && (CurrentTurn & 1)) n++;
            int32_t mil = e.mil_strength_1;
            int32_t a = mil * 3;
            a = a < 1 ? 1 : a > 99999 ? 99999 : a;
            int32_t b = f.mil_strength_1 * 3;
            b = b < 1 ? 1 : b > 99999 ? 99999 : b;
            n += f.mil_strength_1 * 2 / a + (mil * -2) / b;
            if (great_satan(i, 0)) n++;
            if (aah_ooga(i, -1)) n++;
        } else {
            if (wars == 0) {
                int32_t own = f.tech_ranking / 2;
                int32_t theirs = e.tech_ranking / 2;
                if (f.AI_tech - f.AI_power - f.AI_fight + own + 5 < theirs) n++;
                if (own > theirs + 5) n--;
                if (f.ranking + 1 < e.ranking && e.diff_level != 0) n++;
            } else {
                n += 1 - wars;
            }
            if (!(s & 0x1000000)) {
                for (int32_t k = 1; k < 8; k++) {
                    if ((f.diplo_status[k] & DIPLO_VENDETTA) && (e.diplo_status[k] & DIPLO_COMMLINK) &&
                        !(e.diplo_status[k] & DIPLO_VENDETTA)) {
                        n++;
                    }
                }
            }
            if ((GameRules & 0x1000) && f.ranking < 6 && CurrentTurn >= 200 && n >= 0) n = -1;
        }
        if (CurrentTurn & 1) {
            const MFaction& m = MFactions[faction_id];
            if (m.soc_priority_category >= 0 && m.soc_priority_model != 0) {
                int32_t model = (&e.SE_Politics_pending)[m.soc_priority_category];
                if (model == m.soc_priority_model) {
                    n--;
                } else if (model != 0) {
                    n++;
                }
            }
        }
        n += f.AI_fight;
        if ((CurrentTurn & 3) == 0) {
            if (s & DIPLO_TREATY) n--;
            if (f.ranking >= 6 && e.ranking >= 6) {
                n += 3;
            } else if (f.ranking >= 5 && e.ranking >= 5) {
                if (CurrentTurn >= 100) n++;
            } else {
                int32_t d = e.ranking - f.ranking;
                int32_t sgn = d > 0 ? 1 : d < 0 ? -1 : 0;
                n += sgn * 3 - (d + sgn) / 2;
            }
            if (f.energy_credits < e.energy_credits && !crt_strcmp(MFactions[faction_id].filename, "MORGAN")) n++;
            if (f.tech_ranking < e.tech_ranking) n += 1 + f.AI_power - f.AI_tech;
        }
        if ((s & DIPLO_PACT) && (GameRules & 0x1000) && f.ranking < e.ranking && CurrentTurn >= 200 && n > 0) n = 0;
        s &= ~0x200u;
        Factions[i].diplo_status[faction_id] &= ~0x200u;
        if (!(s & DIPLO_VENDETTA) || n >= 0) cause_friction(faction_id, i, n);
    }
    // Orbital defense pods: shoot down satellites of enemies.
    int32_t threat = 0;
    for (int32_t k = 1; k < 8; k++) {
        if (k == faction_id) continue;
        uint32_t s = f.diplo_status[k];
        if (s & DIPLO_PACT) continue;
        int32_t pb = Factions[k].planet_busters;
        bool halve = (s & DIPLO_TREATY) && !(s & 0x20);
        if (threat <= (halve ? pb / 2 : pb)) threat = halve ? pb / 2 : pb;
    }
    int32_t type = 0;
    while (f.satellites_ODP - f.ODP_deployed > threat && type >= 0) {
        type = -1;
        for (int32_t k = 1; k < 8; k++) {
            const Faction& e = Factions[k];
            if (!(f.diplo_status[k] & 0x810) || e.satellites_ODP >= f.satellites_ODP) continue;
            int32_t base_arg = last;
            if (e.satellites_ODP != 0 && e.satellites_ODP >= f.planet_busters) {
                type = 3;
            } else {
                int32_t pods = 0;
                for (last = 0; last < BaseCount; last++) {
                    if ((uint8_t)Bases[last].faction_id == (uint32_t)k && base_fac(last, FAC_GEOSYNC_SURVEY_POD)) pods++;
                }
                base_arg = last;
                if (e.satellites_mineral) {
                    type = 1;
                } else if (e.satellites_energy) {
                    type = 2;
                } else if (e.satellites_nutrient) {
                    type = 0;
                } else if (pods) {
                    type = 4;
                    for (last = 0; last < BaseCount; last++) {
                        if ((uint8_t)Bases[last].faction_id == (uint32_t)k && base_fac(last, FAC_GEOSYNC_SURVEY_POD)) break;
                    }
                    base_arg = last;
                } else if (e.satellites_ODP != 0) {
                    type = 3;
                } else {
                    continue;
                }
            }
            action_sat_attack(faction_id, k, type, base_arg);
            break;
        }
    }
}

// ---- strategy -----------------------------------------------------------------------------------

namespace {
// Tile with a base not owned by natives' "no owner" nibble (owner < 8).
inline bool tile_has_base(const MAP* sq) { return (sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8; }
// The original's inlined vector_dist with |dx| wrapped (same as veh_vector_dist).
inline int32_t vdist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return veh_vector_dist(x1, y1, x2, y2); }
inline int32_t div_4m(int32_t v) { return v / 0x400000; }  // signed (x + bias) >> 22
// Second half of enemy_strategy (region plans, attack target, unit goals); not an original function.
void enemy_strategy_regions(int32_t faction_id, int32_t abandon, int32_t target_region, int32_t colony_count,
                            int32_t attackers);
void strategy_unit_goals(int32_t faction_id);
}  // namespace

void GAMEAPI enemy_strategy(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    uint8_t self_bit = (uint8_t)(1 << (faction_id & 31));
    auto human = [&]() { return (FactionStatus[0] & self_bit) != 0; };
    int32_t colony_count = 0;  // local_64
    int32_t transports = 0;    // local_48
    int32_t transport_cap = 2;  // local_6c
    int32_t attackers = 0;     // local_78
    f.total_combat_units = 0;
    f.base_count = 0;
    f.mil_strength_1 = 0;
    f.mil_strength_2 = 0;
    f.pop_total = 0;
    f.unk_70 = 0;
    f.planet_busters = 0;
    f.unk_71 = 0;
    f.unk_72 = 0;
    for (int32_t r = 0; r < 128; r++) {
        f.region_total_combat_units[r] = 0;
        f.region_total_bases[r] = 0;
        f.region_force_rating[r] = 0;
        f.region_flags[r] = ((CurrentTurn + faction_id + r) & 0xF) != 0 ? (f.region_flags[r] & 8) : 0;
    }
    for (int32_t i = 0; i < 512; i++) {
        f.units_active[i] = 0;
        f.units_queue[i] = 0;
    }
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& b = Bases[i];
        int32_t x = b.x, y = b.y;
        uint32_t owner = (uint8_t)b.faction_id;
        if (owner == (uint32_t)faction_id) {
            b.state_flags &= ~0xC000000;
            if (b.ai_plan_status) b.ai_plan_status--;
            f.base_count++;
            f.pop_total += b.pop_size;
            int32_t top = stack_fix(veh_at(x, y));
            if (!stack_check(top, 0xE, faction_id, -1, -1) && !stack_check(top, 2, 3, faction_id, -1)) {
                add_goal(faction_id, AI_GOAL_DEFEND, 4, x, y, -1);
            }
            if (b.queue_size >= 0) {
                for (int32_t q = 0; q <= b.queue_size; q++) {
                    if (b.queue_items[q] >= 0) f.units_queue[b.queue_items[q]]++;
                }
            }
            int32_t region = tile_region(x, y);
            f.region_total_bases[region]++;
            if (is_coast(b.x, b.y, 0)) f.region_flags[region] |= 0x80;
            if (((CurrentTurn + i) & 3) == 0) b.event_flags &= ~0xFF;
        } else if (b.visibility & self_bit) {
            uint32_t s = f.diplo_status[owner];
            if ((s & 0xC10) && !(s & DIPLO_PACT)) {
                int32_t w = 1;
                MAP* sq = map_tile(x, y);
                if (!(sq->items & BIT_VEH_IN_TILE) || (sq->val2 & 0xF) > 7) {
                    w = 2;
                    add_goal(faction_id, AI_GOAL_DEFEND, 2, x, y, -1);
                    add_goal(faction_id, AI_GOAL_SCOUT, 2, x, y, -1);
                }
                if ((uint8_t)b.faction_id_former == (uint32_t)faction_id) w++;
                if (f.region_total_bases[tile_region(x, y)]) w++;
                if (!base_fac(i, FAC_PERIMETER_DEFENSE)) w++;
                if (b.queue_items[0] == -0x66) w *= 4;
                if (base_fac(i, FAC_HEADQUARTERS)) {
                    w++;
                    if (f.player_flags & PFLAG_STRAT_ATK_ENEMY_HQ) w *= 2;
                    if (CurrentTurn < Factions[(uint8_t)b.faction_id].corner_market_turn) w <<= 3;
                }
                if (f.base_id_attack_target == i && (f.diplo_status[(uint8_t)b.faction_id] & DIPLO_VENDETTA)) {
                    w <<= 2;
                } else if (is_objective(i)) {
                    w *= 4;
                }
                add_goal(faction_id, AI_GOAL_ATTACK, w, x, y, -1);
                int32_t ux = b.unk_x, uy = b.unk_y;
                if (uy >= 0 && uy < MapAreaY && ux >= 0 && ux < MapAreaX) {
                    int32_t near = base_find(ux, uy);
                    if (near >= 0 && Bases[near].faction_id == b.faction_id &&
                        terraform_cost(b.unk_x, b.unk_y, faction_id) <= f.energy_credits / 8) {
                        add_goal(faction_id, AI_GOAL_UNK_4, 2, b.unk_x, b.unk_y, -1);
                    }
                }
            }
        }
    }
    if (!human()) {
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id == (uint32_t)faction_id && Bases[i].queue_items[0] == -0x45) base_reset(i, 0);
        }
    }
    int32_t formers[128] = {};  // local_28c: formers per region
    enemy_capabilities(faction_id);
    f.unk_68 = 0;
    f.unk_66 = 0;
    f.unk_65 = 0;
    f.unk_64 = 0;
    f.unk_106 = 0;
    f.unk_105 = 0;
    for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
        if (veh_id >= VehCount) continue;
        VEH& v = Vehs[veh_id];
        if (v.faction_id == (uint32_t)faction_id) {
            int32_t x = v.x, y = v.y;
            int32_t unit_id = v.unit_id;
            MAP* sq = map_tile(x, y);
            int32_t region = sq->region;
            bool ocean = (sq->climate & 0xE0) < 0x60;
            int32_t plan = (uint8_t)Units[unit_id].plan;
            int32_t triad = unit_triad(unit_id);
            f.units_active[unit_id]++;
            if (v.year_end_lurking && --v.year_end_lurking == 0) v.flags &= 0xFBBF;
            // Disband obsolete units far from danger.
            bool obsolete = !human() && (Units[v.unit_id].obsolete_factions & self_bit) &&
                            !Console_upgrade(&MainConsole, veh_id) && ((CurrentTurn + veh_id) & 3) == 0 && CurrentTurn > 50 &&
                            v.status_icon != 'F' && v.status_icon != '1' && v.status_icon != '2';
            if (obsolete) {
                uint8_t p = (uint8_t)Units[v.unit_id].plan;
                obsolete = !(p == 1 && f.region_base_plan[region] <= 2) && p < 8 && p != f.region_base_plan[region] &&
                           !(p == 3 && f.region_base_plan[region] == 8 && v.order != ORDER_HOLD) &&
                           (!tile_has_base(sq) || stack_check(veh_id, 1, faction_id, -1, -1) > 1) && !ocean &&
                           f.region_base_plan[region] != 2 && zoc_sea(x, y, faction_id) == 0;
            }
            bool killed = false;
            if (obsolete) {
                uint8_t p = (uint8_t)Units[v.unit_id].plan;
                bool better = true;
                if (p <= 1) {
                    better = false;
                    for (int32_t k = 0; k < 64; k++) {
                        int32_t u = faction_id * 64 + k;
                        const UNIT& pu = Units[u];
                        if ((pu.unit_flags & 1) && !(self_bit & pu.obsolete_factions) && pu.plan == Units[v.unit_id].plan &&
                            unit_triad(u) == unit_triad(v.unit_id)) {
                            int32_t a = weap_val(u, faction_id);
                            if (weap_val(unit_id, faction_id) < a) better = true;
                        }
                    }
                } else if (p == 2) {
                    better = false;
                    for (int32_t k = 0; k < 64; k++) {
                        int32_t u = faction_id * 64 + k;
                        const UNIT& pu = Units[u];
                        if ((pu.unit_flags & 1) && !(self_bit & pu.obsolete_factions) && pu.plan == Units[v.unit_id].plan &&
                            unit_triad(u) == unit_triad(v.unit_id)) {
                            // BUG(original): arm_val takes an armor id but is passed unit ids.
                            int32_t own = arm_val(unit_id, faction_id);
                            if (own < arm_val(u, faction_id)) better = true;
                        }
                    }
                }
                if (better) {
                    if (!(v.flags & VFLAG_IS_OBJECTIVE)) kill(veh_id);
                    killed = true;
                }
            }
            if (killed) continue;
            // Scouts in settled regions.
            if (!human() && ((CurrentTurn + veh_id) & 3) != 0 && CurrentTurn > 25 && unit_id == BSC_SCOUT_PATROL &&
                (f.region_base_plan[region] == 7 || v.order == ORDER_HOLD) &&
                (!tile_has_base(sq) || stack_check(veh_id, 0xE, faction_id, -1, -1) > 1) && !ocean &&
                f.region_base_plan[region] != 2 && zoc_sea(x, y, faction_id) == 0 && v.status_icon != '1') {
                kill(veh_id);
                continue;
            }
            v.state &= 0xCFFFFFDB;
            if (!human() && v.order == ORDER_SENTRY_BOARD && (triad != TRIAD_LAND || !ocean)) v.order = ORDER_NONE;
            if (ocean && triad == TRIAD_LAND) {
                if (!tile_has_base(sq)) {
                    if (!stack_check(veh_id, 2, PLAN_NAVAL_TRANSPORT, -1, -1)) {
                        if (!(v.flags & VFLAG_IS_OBJECTIVE)) veh_kill(veh_id);
                        continue;
                    }
                    if (v.order != ORDER_SENTRY_BOARD) sleep(veh_id);
                }
            } else if (!ocean && triad == TRIAD_SEA && !tile_has_base(sq)) {
                if (!(v.flags & VFLAG_IS_OBJECTIVE)) veh_kill(veh_id);
                continue;
            }
            if (plan <= 1 && !(v.state & 0x40000)) {
                int32_t base_id = base_at(v.x, v.y);
                if (base_id < 0 && v.status_icon == 'F') base_id = v.home_base_id;
                if (base_id >= 0) {
                    int32_t want = garrison_check(base_id);
                    int32_t def = garrison_check(base_id);
                    if (def > 5) def--;
                    if (def > 2) def--;
                    int32_t have = stack_check(veh_at(Bases[base_id].x, Bases[base_id].y), 0xB, 0x10000000, 0x10000000, faction_id);
                    if (want > have + def) v.state |= 0x10000000;
                }
            }
            // Counting by plan (labels of the original: 0x56216D triad counts, 0x562277 combat units,
            // 0x5622F8 transports).
            int32_t step;  // 0: triad counts, 1: combat units, 2: transports
            if (v.status_icon == 'F' && plan == PLAN_DEFENSE) {
                int32_t home = v.home_base_id;
                if (home >= 0 && (Bases[home].x != v.x || Bases[home].y != v.y)) Bases[home].state_flags |= 0x4000000;
                step = 1;
            } else if (plan == PLAN_TERRAFORM) {
                v.flags &= ~0x800;
                formers[region]++;
                int32_t home = v.home_base_id;
                if (home >= 0) {
                    uint32_t mask = triad != TRIAD_SEA ? 0x20 : 0x80000;
                    if ((mask & Bases[home].state_flags) && !human()) {
                        int32_t best = 9999;
                        int32_t best_base = -1;
                        for (int32_t j = 0; j < BaseCount; j++) {
                            if (j == v.home_base_id) continue;
                            const BASE& b = Bases[j];
                            if (tile_region(b.x, b.y) != region || (b.state_flags & mask) || b.mineral_surplus >= 2) continue;
                            int32_t d = vdist(v.x, v.y, b.x, b.y);
                            if (d < best) {
                                best_base = j;
                                best = d;
                            }
                        }
                        if (best_base >= 0) {
                            Bases[best_base].state_flags |= mask;
                            v.home_base_id = (int16_t)best_base;
                        }
                    } else {
                        Bases[home].state_flags |= mask;
                    }
                }
                step = 0;
            } else if (plan == PLAN_COLONY) {
                uint16_t& rf = f.region_flags[region];
                if (rf & 2) rf |= 0x800;
                else rf |= 2;
                colony_count++;
                step = 0;
            } else if (plan == PLAN_PROBE) {
                f.region_flags[region] |= 0x1000;
                step = 0;
            } else if (plan == PLAN_RECON) {
                if (!tile_has_base(sq)) f.region_flags[region] |= 0x2000;
                step = 1;
            } else if (plan > 7) {
                step = 0;
            } else {
                step = 1;
            }
            if (step == 1) {
                f.total_combat_units++;
                if (triad != TRIAD_LAND) {
                    step = 0;
                } else {
                    if (!ocean) f.region_total_combat_units[region]++;
                    step = 2;
                }
            }
            bool to_transports = true;
            if (step == 0) {
                if (triad == TRIAD_SEA) {
                    f.unk_70++;
                } else if (triad == TRIAD_AIR) {
                    if (plan == PLAN_OFFENSE) {
                        f.unk_105++;
                        to_transports = false;
                    } else if (plan == PLAN_AIR_SUPERIORITY) {
                        f.unk_106++;
                        to_transports = false;
                    }
                }
            }
            if (to_transports && plan == PLAN_NAVAL_TRANSPORT) {
                transports++;
                if (transport_cap <= veh_cargo(veh_id)) transport_cap = veh_cargo(veh_id);
            }
            if (planet_buster(veh_id)) {
                f.planet_busters++;
                continue;
            }
            int32_t ws = weap_strat(Units[v.unit_id].weapon_id, faction_id);
            int32_t as = arm_strat(Units[v.unit_id].armor_id, faction_id);
            int32_t m = morale_veh(veh_id, 0, 0) + 6;
            f.mil_strength_1 += (as + ws) * m / 8;
            if (ws > as || v.order != ORDER_HOLD) {
                int32_t value = m * ws / 8;
                f.mil_strength_2 += value;
                f.region_force_rating[region] += (uint16_t)value;
            }
            uint32_t mode = v.state & 0x60000;
            if (mode == 0x40000) {
                int32_t tx = f.target_x, ty = f.target_y;
                if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX || region != tile_region(tx, ty)) {
                    v.state |= 0x20000;
                    attackers++;
                } else {
                    f.unk_71++;
                    if (plan == PLAN_PROBE) {
                        f.unk_72 |= 2;
                    } else if (ws < as) {
                        f.unk_72 |= 1;
                    }
                }
            } else if (mode == 0x60000) {
                attackers++;
            }
        } else {
            uint32_t s = f.diplo_status[v.faction_id];
            if ((s & DIPLO_COMMLINK) && (!(s & 3) || (s & 0xC20))) {
                int32_t unit_id = v.unit_id;
                int8_t plan = Units[unit_id].plan;
                if (plan == PLAN_NAVAL_TRANSPORT) f.unk_64++;
                if (plan == PLAN_NAVAL_SUPERIORITY) f.unk_65 += Units[unit_id].reactor_id;
                if (unit_triad(unit_id) == TRIAD_AIR) {
                    if (plan == PLAN_OFFENSE && !Chassis[Units[unit_id].chassis_id].missile &&
                        f.enemy_best_armor_value <= weap_val(unit_id, faction_id)) {
                        f.unk_66++;
                    }
                    if (Chassis[Units[v.unit_id].chassis_id].missile && planet_buster(veh_id) == 0) f.unk_68++;
                }
                if (Units[v.unit_id].plan == PLAN_TERRAFORM && unit_triad(v.unit_id) == TRIAD_SEA) {
                    MAP* sq = map_tile(v.x, v.y);
                    if ((sq->climate & 0xE0) < 0x60 && !tile_has_base(sq) &&
                        ((v.faction_id == (uint32_t)faction_id && (v.flags & 0x440) != 0x440) || (v.visibility & self_bit))) {
                        add_goal(faction_id, AI_GOAL_UNK_1, (f.player_flags & PFLAG_UNK_80000) ? 4 : 2, v.x, v.y, -1);
                    }
                }
            }
        }
    }
    f.unk_70 += (transport_cap * 0x800 + transports) * 0x800;
    {
        int32_t most = 0;
        for (int32_t k = 1; k < 8; k++) {
            if (k == faction_id) continue;
            uint32_t s = f.diplo_status[k];
            if ((s & DIPLO_COMMLINK) && !(s & DIPLO_PACT)) {
                int32_t v = div_4m(Factions[k].unk_70);
                if (most <= v) most = v;
            }
        }
        f.unk_64 += most * 0x800;
    }
    if (transports > 1) {
        for (int32_t i = 0; i < VehCount; i++) {
            if (Vehs[i].faction_id != (uint32_t)faction_id) continue;
            int32_t u = Vehs[i].unit_id;
            if (unit_triad(u) == TRIAD_SEA && Units[u].plan == PLAN_NAVAL_TRANSPORT &&
                (u == BSC_UNITY_FOIL || f.units_active[u] >= 2)) {
                Vehs[i].state |= 0x20000000;
                break;
            }
        }
    }
    for (int32_t i = 0; i < VehCount; i++) {
        VEH& v = Vehs[i];
        if (v.faction_id != (uint32_t)faction_id || Units[v.unit_id].plan != PLAN_TERRAFORM) continue;
        int32_t region = tile_region(v.x, v.y);
        if (region < 0x40 && formers[region] >= 3 && f.region_total_bases[region] >= 3) {
            v.flags |= 0x800;
            if (v.home_base_id >= 0) Bases[v.home_base_id].state_flags &= ~0x20;
            formers[region] = 0;
        }
    }
    // Current attack target: keep it or give it up.
    int32_t abandon = human() ? 1 : 0;  // local_3c
    int32_t target_region = -1;          // local_34
    bool reset_target = false;
    if (f.target_x < 0) {
        reset_target = abandon != 0;
    } else {
        int32_t base_id = base_at(f.target_x, f.target_y);
        target_region = tile_region(f.target_x, f.target_y);
        bool give_up = true;  // 0x5628AC
        if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id) {
            give_up = false;
            bool flag = abandon != 0;
            int32_t top = stack_fix(veh_at(Bases[base_id].x, Bases[base_id].y));
            bool done = false;
            if (top >= 0) {
                int32_t n = stack_check(top, 0xB, 0x60000, 0x40000, -1);
                int32_t lim = f.region_total_bases[target_region] * 2 / 3;
                lim = lim < 3 ? 3 : lim > 8 ? 8 : lim;
                if (n - bit_count(f.unk_72) >= lim) flag = true, abandon = 1;
                if (f.region_base_plan[target_region] == 2 || (f.region_flags[target_region] & 0x4200)) flag = true, abandon = 1;
                if (f.region_base_plan[target_region] > 2 && stack_check(top, 2, PLAN_NAVAL_TRANSPORT, -1, -1)) {
                    if (div_4m(f.unk_70) * 2 <= n) flag = true, abandon = 1;
                    if (stack_check(top, 2, PLAN_NAVAL_TRANSPORT, -1, -1) > 1) {
                        int32_t a = stack_check(top, 8, -1, -1, -1);
                        if (a + stack_check(top, 3, 0, -1, -1) <= n) {
                            abandon = 1;
                            done = true;
                        }
                    }
                }
            }
            if (done || flag) {
                reset_target = true;
            } else if (f.region_base_plan[target_region] <= 2) {
                f.player_flags &= ~PFLAG_UNK_800;
            } else if (f.region_base_plan[target_region] != 8 && is_coast(Bases[base_id].x, Bases[base_id].y, 0)) {
                f.player_flags |= PFLAG_UNK_800;
            } else {
                give_up = true;
            }
        }
        if (give_up) {
            abandon = 1;
            reset_target = true;
        }
    }
    if (reset_target) {
        for (int32_t i = 0; i < VehCount; i++) {
            if (Vehs[i].faction_id == (uint32_t)faction_id && (Vehs[i].state & 0x40000)) Vehs[i].state |= 0x20000;
        }
        f.target_y = -1;
        f.target_x = -1;
        f.unk_72 = 0;
        f.unk_71 = 0;
    }
    enemy_strategy_regions(faction_id, abandon, target_region, colony_count, attackers);
}

namespace {
// The abstract-map scan of enemy_strategy: best land tip of region next to another region, to raise
// land there (AI_GOAL_UNK_4).
void strategy_land_bridge(int32_t faction_id, int32_t region) {
    int32_t best = 0;
    int32_t best_x = 0, best_y = 0;
    int32_t cand_x = 0, cand_y = 0;  // the original keeps these across cells
    for (int32_t ay = 0; ay < MapAbstractY; ay++) {
        for (int32_t ax = ay & 1; ax < MapAbstractX; ax += 2) {
            if ((abstract_at(ax, ay) & 0xFF) != region) continue;
            int32_t score = 0;
            int32_t found = -1;
            for (int32_t dir = 0; dir < 8; dir++) {
                int32_t nx = ax + NearOffsetX[dir];
                if (!(MapToggleFlat & 1)) {
                    if (nx < 0) nx += MapAbstractX;
                    else if (nx >= MapAbstractX) nx -= MapAbstractX;
                }
                int32_t ny = ay + NearOffsetY[dir];
                if (nx < 0 || nx >= MapAbstractX || ny < 0 || ny >= MapAbstractY) continue;
                int32_t other = abstract_at(nx, ny) & 0xFF;
                if (other == region) {
                    score--;
                    continue;
                }
                score += 2;
                for (int32_t k = 0; k < 8; k++) {
                    int32_t tx = map_wrap_x(NearOffsetX[k] + ax * 5);
                    int32_t ty = NearOffsetY[k] + ay * 5;
                    if (!map_in_bounds(tx, ty) || map_is_ocean(tx, ty)) continue;
                    // Walk towards the other region while on land.
                    int32_t steps = 1;
                    bool stop = false;
                    for (;;) {
                        int32_t sx = map_wrap_x(tx + NearOffsetX[dir]);
                        int32_t sy = ty + NearOffsetY[dir];
                        if (!map_in_bounds(sx, sy) || map_is_ocean(sx, sy)) {
                            stop = true;
                            break;
                        }
                        tx = sx;
                        ty = sy;
                        if (++steps >= 100) break;
                    }
                    if (!stop) continue;
                    if (!tile_has_base(map_tile(tx, ty))) {
                        found = dir;
                        score += 4;
                        cand_x = tx;
                        cand_y = ty;
                        if (other != 0 && Continents[other].tile_count >= 40) score += 2;
                    }
                    break;
                }
            }
            if (score > best && found >= 0) {
                best = score;
                best_x = cand_x;
                best_y = cand_y;
            }
        }
    }
    if (best > 0 && terraform_cost(best_x, best_y, faction_id) <= Factions[faction_id].energy_credits / 8) {
        add_goal(faction_id, AI_GOAL_UNK_4, 2, best_x, best_y, -1);
    }
}

void enemy_strategy_regions(int32_t faction_id, int32_t abandon, int32_t target_region, int32_t colony_count,
                            int32_t attackers) {
    (void)colony_count;  // the original reuses this local for the per-region contact count
    Faction& f = Factions[faction_id];
    uint8_t self_bit = (uint8_t)(1 << (faction_id & 31));
    auto human = [&]() { return (FactionStatus[0] & self_bit) != 0; };
    int32_t best_score = 0;    // local_c
    int32_t best_region = -1;  // local_28
    for (int32_t r = 1; r < 128; r++) {
        f.region_total_offensive_units[r] = 0;
        if (((r & 0x3F) == 0x3F || (r & 0x3F) == 0) && f.region_total_bases[r] == 0) {
            f.region_base_plan[r] = 8;
            continue;
        }
        int32_t old_plan = f.region_base_plan[r];
        uint32_t threat = 0;  // local_8
        int32_t strong = 0;   // local_2c
        int32_t weak = 0;     // local_5c
        int32_t occupied = 0;  // local_70
        int32_t contact = 0;   // local_64
        int32_t wars = 0;      // local_18
        int32_t hostile = 0;   // local_60
        int32_t my_units = f.region_total_combat_units[r];
        int32_t my_force = f.region_force_rating[r];
        int32_t my_bases = f.region_total_bases[r];
        int32_t reach = 0, path = 0;  // local_48, local_30
        for (int32_t k = 1; k < 8; k++) {
            const Faction& e = Factions[k];
            uint32_t s = f.diplo_status[k];
            int32_t their_force = e.region_force_rating[r];
            int32_t their_units = e.region_total_combat_units[r];
            int32_t their_bases = e.region_total_bases[r];
            if (s & DIPLO_PACT) {
                their_units *= 2;
                their_bases *= 2;
            }
            reach = their_units;
            if (their_bases == 0) {
                if (!(their_units != 0 && my_force <= their_force * 5)) continue;
            } else {
                occupied++;
                f.region_flags[r] |= k != faction_id ? 0x20 : 0x10;
                if (s & DIPLO_VENDETTA) f.region_flags[r] |= 0x40;
            }
            if (k == faction_id || (s & DIPLO_PACT) || !(s & DIPLO_COMMLINK)) continue;
            bool check = false;
            if (CurrentTurn < 2 || (Continents[r].tile_count < 500 && e.region_total_bases[r] * my_bases < 50)) {
                check = true;
            } else {
                uint32_t t = Continents[r].tile_count < 1000 ? (uint32_t)(CurrentTurn + faction_id) & 7
                                                             : (uint32_t)(CurrentTurn + faction_id * 2) & 0xF;
                if (t == 0) {
                    check = true;
                } else if (their_bases == 0 || f.region_base_plan[r] < 3) {
                    path = 1;
                    reach = 1;
                } else {
                    path = 0;
                    reach = 0;
                }
            }
            if (check) {
                if (their_bases == 0 || (reach = contiguous(faction_id, k, r, 0)) != 0) {
                    reach = 1;
                    path = 1;
                } else {
                    path = contiguous(faction_id, k, r, 1) != 0;
                }
            }
            if (!(s & 0xC10)) {
                if (my_bases && (s & 0x20) && my_units < their_force) {
                    uint32_t level;
                    if ((their_units < my_force && their_bases && their_force < my_force * 2) || !path) {
                        level = 2;
                    } else {
                        level = 1;
                    }
                    threat |= level;
                }
            } else {
                if (path) contact++;
                if (reach && ++wars > 1) f.region_flags[r] |= 0x200;
                if ((their_units < my_force && their_force < my_force) || their_force < my_units || !my_bases || !path) {
                    weak++;
                } else {
                    strong++;
                }
                if (my_force * 3 / 2 <= their_force && their_bases < my_bases && path) f.region_flags[r] |= 0x4000;
                if ((FactionStatus[0] & (uint8_t)(1 << (k & 31))) && (reach || path)) {
                    for (int32_t i = 0; i < BaseCount; i++) {
                        const BASE& b = Bases[i];
                        if ((uint8_t)b.faction_id == (uint32_t)k && (uint8_t)b.faction_id_former == (uint32_t)faction_id &&
                            tile_region(b.x, b.y) == r) {
                            f.region_flags[r] |= 0x400;
                            break;
                        }
                    }
                }
            }
            if (!(s & 3) || ((wants_to_attack(faction_id, k, 0) || wants_to_attack(k, faction_id, 0)) && path)) hostile = 1;
        }
        int32_t unk6 = f.region_unk_6[r];
        int32_t pressure = (CurrentTurn > 75) + 3 + (MapAreaTiles > 3200) + occupied +
                           great_satan(FactionRankings[7], 0) + ((unk6 < 4) - (int32_t)((GameState >> 7) & 2)) +
                           (f.base_count > 4);
        uint32_t settle = 0;
        if (my_bases) {
            uint32_t full = (unk6 > 1 || f.region_total_bases[r] == 0) ? 0 : 1;
            int32_t free_tiles = f.region_good_tiles[r] - f.region_unk_5[r];
            full |= (f.region_flags[r] & 8) | (my_bases > 19) | (free_tiles < 4);
            int32_t a = (pressure + (f.AI_growth - f.AI_wealth)) * my_bases * 2;
            // BUG(original): adds faction 0's good tiles of the region.
            int32_t b = Factions[0].region_good_tiles[r] + f.region_good_tiles[r];
            uint32_t grow = (a > b && free_tiles / ((my_bases + 1) / 2) + unk6 < pressure * 2) ? 1 : 0;
            settle = full | grow;
        }
        if (hostile) f.region_flags[r] |= 4;
        if (settle) f.region_flags[r] |= 1;
        f.region_base_plan[r] = settle == 0 ? 8 : 7;
        if (contact == 0 && f.region_total_bases[r] != 0) {
            f.region_flags[r] |= 0x100;
        } else {
            if (weak != 0) {
                threat = 0;
                f.region_base_plan[r] = 0;
            } else if (threat & 2) {
                f.region_base_plan[r] = 0;
            }
            if (strong || (threat & 1)) f.region_base_plan[r] = 2;
        }
        if (r >= 0x40) {
            f.region_base_plan[r] = 7;
            continue;
        }
        if (!abandon && r == target_region && wars > 1) {
            for (int32_t i = 0; i < VehCount; i++) {
                if (Vehs[i].faction_id == (uint32_t)faction_id && (Vehs[i].state & 0x40000)) Vehs[i].state |= 0x20000;
            }
            f.target_y = -1;
            f.target_x = -1;
            f.unk_72 = 0;
            f.unk_71 = 0;
            target_region = -1;
        }
        if (f.target_x < 0 && !abandon && !human()) {
            if (f.region_base_plan[r] < 2 && wars < 2 && !(f.region_flags[r] & 0x4000)) {
                int32_t v = f.region_total_bases[r] + 1000;
                if (best_score < v) {
                    best_region = r;
                    best_score = v;
                    int32_t bat = f.base_id_attack_target;
                    if (bat >= 0 && (f.diplo_status[(uint8_t)Bases[bat].faction_id] & DIPLO_VENDETTA) &&
                        tile_region(Bases[bat].x, Bases[bat].y) == r) {
                        best_score = 0;
                    }
                }
            } else if (f.region_base_plan[r] == 7 && best_score < f.region_total_bases[r]) {
                best_region = r;
                best_score = f.region_total_bases[r];
            }
        }
        if ((!human() || (faction_id == CurrentPlayerFaction && (GamePreferences & 0x20000))) && f.region_total_bases[r] &&
            (f.region_base_plan[r] == 7 || (f.region_base_plan[r] == 8 && Continents[r].tile_count < 150))) {
            strategy_land_bridge(faction_id, r);
        }
        if (!human()) bases_reset(r, faction_id, f.region_base_plan[r] == old_plan);
    }
    // New attack target: nearest own base to an enemy base in the best region.
    if (best_region > 0 && best_region < 0x3F && !abandon && !human() && !(f.player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES)) {
        int32_t best = -1;
        int32_t enemy = -1;
        for (int32_t k = 1; k < 8; k++) {
            if (k == faction_id) continue;
            uint32_t s = f.diplo_status[k];
            int32_t bases = Factions[k].region_total_bases[best_region];
            if ((s & 0xC14) && ((s & 0x810) || (attackers <= 3 && bases != 0))) {
                int32_t v = (int32_t)(s & DIPLO_VENDETTA) * 0x80 + bases;
                if (best < v) {
                    best = v;
                    enemy = k;
                }
            }
        }
        if (enemy > 0) {
            int32_t coast_only = best == 0x800;
            int32_t best_dist = 9999;
            int32_t best_base = -1;
            for (int32_t i = 0; i < BaseCount; i++) {
                const BASE& b = Bases[i];
                if ((uint8_t)b.faction_id != (uint32_t)faction_id) continue;
                if (map_is_ocean(b.x, b.y) || tile_region(b.x, b.y) != best_region) continue;
                if (coast_only && !is_coast(b.x, b.y, 0)) continue;
                int32_t bat = f.base_id_attack_target;
                if (bat >= 0 && (f.diplo_status[(uint8_t)Bases[bat].faction_id] & 0x810) &&
                    tile_region(Bases[bat].x, Bases[bat].y) == best_region) {
                    BaseFindDist = vdist(b.x, b.y, Bases[bat].x, Bases[bat].y);
                } else if (base_find_3(b.x, b.y, enemy, best_region, -1, -1) < 0) {
                    base_find_2(b.x, b.y, enemy);
                    BaseFindDist *= 2;
                }
                if (BaseFindDist < best_dist) {
                    best_base = i;
                    best_dist = BaseFindDist;
                }
            }
            if (best_base >= 0) {
                f.target_x = Bases[best_base].x;
                f.target_y = Bases[best_base].y;
                if (coast_only) f.player_flags |= PFLAG_UNK_800;
                else f.player_flags &= ~PFLAG_UNK_800;
                for (int32_t i = 0; i < VehCount; i++) {
                    VEH& v = Vehs[i];
                    if (v.faction_id != (uint32_t)faction_id || (v.state & 0x60000)) continue;
                    int32_t u = v.unit_id;
                    if ((uint8_t)Units[u].plan > 2 || unit_triad(u) != TRIAD_LAND || has_abil(v.unit_id, ABL_DROP_POD)) continue;
                    if (tile_region(v.x, v.y) != best_region || (v.order != ORDER_NONE && v.order != ORDER_MOVE_TO)) continue;
                    base_find_2(v.x, v.y, enemy);
                    if (vdist(v.x, v.y, f.target_x, f.target_y) >= BaseFindDist) continue;
                    int32_t ws = weap_strat(Units[v.unit_id].weapon_id, faction_id);
                    int32_t as = arm_strat(Units[v.unit_id].armor_id, faction_id);
                    if (as <= ws || (!(f.unk_72 & 1) && !coast_only)) {
                        v.state = (v.state & ~0x20000u) | 0x40000;
                        f.unk_71++;
                        if (ws < as) f.unk_72 |= 1;
                        if (f.unk_71 > 7 || (coast_only && div_4m(f.unk_70 * 2) <= f.unk_71)) break;
                    }
                }
            }
        }
    }
    if (CurrentTurn == 0 || faction_id == 0) return;
    strategy_unit_goals(faction_id);
}
}  // namespace

namespace {
// Third part of enemy_strategy: send units to the faction's goals (two passes).
void strategy_unit_goals(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    uint8_t self_bit = (uint8_t)(1 << (faction_id & 31));
    auto human = [&]() { return (FactionStatus[0] & self_bit) != 0; };
    int32_t assigned[75] = {};  // local_1b8: units sent to each goal
    for (int32_t i = VehCount - 1; i >= 0; i--) {
        if (Vehs[i].faction_id == (uint32_t)faction_id && Vehs[i].status_icon == '1') Vehs[i].status_icon = '2';
    }
    for (int32_t pass = 0; pass < 2; pass++) {
        for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
            if (veh_id >= VehCount) continue;
            VEH& v = Vehs[veh_id];
            if (v.faction_id != (uint32_t)faction_id) continue;
            if (pass == 0 && v.status_icon != '2') continue;
            int32_t x = v.x, y = v.y;
            MAP* sq = map_tile(x, y);
            int32_t region = sq->region;
            int32_t plan = (uint8_t)Units[v.unit_id].plan;
            if (pass != 0 && v.status_icon == '1' && (plan == PLAN_TERRAFORM || (f.region_base_plan[region] <= 2 && plan <= 2))) {
                continue;
            }
            uint32_t state = v.state;
            v.state = state & ~0x40000000u;
            if ((int8_t)v.order >= 4 && (int8_t)v.order < 0x18) continue;
            if (human()) {
                if (!(state & 0x200)) continue;
                if (Units[v.unit_id].plan == PLAN_TERRAFORM && v.order_auto_type != 0) continue;
                if ((state & 0x1000200) == 0x1000200) continue;
                if ((state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) continue;
            }
            if (state & 0x10000000) {
                plan = PLAN_DEFENSE;
                f.region_total_offensive_units[region]++;
            } else if (region < 0x40) {
                int32_t per = 2 - f.AI_fight;
                if (f.region_base_plan[region] == 8) per += 2;
                else if (f.region_base_plan[region] == 2) per = 1;
                if (f.player_flags & PFLAG_STRAT_DEF_OBJECTIVES) per = 1;
                int32_t want = (f.region_total_bases[region] + per - 1) / per;
                if (want && plan <= 1) {
                    bool ok = true;
                    if (f.region_base_plan[region] <= 2) {
                        int32_t owner = (sq->items & BIT_BASE_IN_TILE) ? (sq->val2 & 0xF) : -1;
                        if (owner >= 8) owner = -1;
                        ok = owner == faction_id &&
                             (f.region_base_plan[region] == 2 || (f.region_flags[region] & 0x4000)) &&
                             stack_check(veh_id, 0xB, 0x10000000, 0x10000000, -1) < 1;
                    }
                    if (ok && !(v.state & 0x60000) && f.region_total_offensive_units[region] < want) {
                        plan = PLAN_DEFENSE;
                        f.region_total_offensive_units[region]++;
                        v.state |= 0x10000000;
                    }
                }
            }
            int32_t triad = veh_triad(veh_id);
            if (triad != TRIAD_AIR) {
                bool ocean = map_is_ocean(x, y);
                bool sea = triad == TRIAD_SEA;
                if (ocean != sea && !tile_has_base(sq)) continue;
                // (The original then searches the neighbours for a matching tile, comparing the
                // unit's own tile: never true.)
            }
            if (plan == PLAN_COMBAT) {
                if (f.region_base_plan[region] == 2) {
                    plan = PLAN_DEFENSE;
                } else {
                    plan = PLAN_OFFENSE;
                    if (f.region_base_plan[region] == 8) plan = PLAN_DEFENSE;
                }
            }
            if ((v.state & 0x60000) == 0x40000 && f.target_x >= 0) {
                v.order = ORDER_MOVE_TO;
                v.status_icon = '0';
                v.waypoint_x[0] = (int16_t)f.target_x;
                v.waypoint_y[0] = (int16_t)f.target_y;
                if (Units[v.unit_id].plan == PLAN_COLONY) v.movement_turns = 0;
                continue;
            }
            if (plan == PLAN_NAVAL_TRANSPORT && stack_check(veh_id, 8, -1, -1, -1) < veh_cargo(veh_id)) continue;
            if (v.status_icon == 'F' && plan == PLAN_DEFENSE) continue;
            int32_t retreat = 0;
            if ((v.state & 0x60000) == 0x60000) {
                plan = PLAN_OFFENSE;
                retreat = 1;
            }
            if (v.order == ORDER_HOLD && !retreat && tile_has_base(sq)) {
                int32_t art = stack_check(veh_id, 2, PLAN_ARTIFACT, -1, -1);
                if (stack_check(veh_id, 1, -1, -1, -1) - art <= 1) continue;
                if (plan == 2 || plan == 3 || plan == 1) {
                    int32_t base_id = base_at(x, y);
                    int32_t n = stack_check(veh_id, 0x10, -1, -1, -1);
                    if (n < garrison_check(base_id)) continue;
                }
            }
            if (plan == PLAN_TERRAFORM && (v.flags & 0x800)) continue;
            int32_t best = 99999;
            int32_t best_goal = -1;
            for (int32_t g = 0; g < 75; g++) {
                Goal& goal = f.goals[g];
                if (goal.priority <= 0 || goal.type < 0) continue;
                if ((goal.type & 0xF) != plan &&
                    (retreat || goal.type != 3 || plan > 3 || v.order == ORDER_HOLD)) {
                    continue;
                }
                int32_t gx = goal.x, gy = goal.y;
                if (gy < 0 || gy >= MapAreaY || gx < 0 || gx >= MapAreaX) {
                    goal.x = -1;
                    goal.y = -1;
                    continue;
                }
                MAP* gsq = map_tile(gx, gy);
                if (gsq->region != region) continue;
                int32_t u = v.unit_id;
                int32_t ut = unit_triad(u);
                if (ut == TRIAD_AIR) continue;
                if (((gsq->climate & 0xE0) < 0x60) != (ut == TRIAD_SEA)) continue;
                if ((uint8_t)Units[u].group_id == 0x15) continue;
                if (plan == PLAN_OFFENSE) {
                    if (gsq->items & BIT_MONOLITH) {
                        if (!want_monolith(veh_id) || retreat || assigned[g] != 0) continue;
                    }
                } else if (plan == PLAN_COLONY) {
                    if (base_territory(faction_id, goal.x, goal.y) >= 0) {
                        del_site(faction_id, AI_GOAL_COLONIZE, goal.x, goal.y, 0);
                        continue;
                    }
                } else if (plan == PLAN_RECON) {
                    if (has_abil(u, ABL_EMPATH)) continue;
                }
                int32_t goal_base = base_at(goal.x, goal.y);
                int32_t cost = vdist(x, y, goal.x, goal.y) * 0x100;
                int32_t taken = 0;
                if (!retreat && (goal_base < 0 || (uint8_t)Bases[goal_base].faction_id != (uint32_t)faction_id ||
                                 Bases[goal_base].ai_plan_status == 0)) {
                    taken = assigned[g];
                }
                int32_t left = goal.priority - taken;
                if (left <= 0) continue;
                if (retreat && !tile_has_base(map_tile(goal.x, goal.y))) continue;
                if (goal.type == 3 && plan != 3) {
                    cost *= 2;
                    if (v.state & 0x10000000) continue;
                    if (!tile_has_base(map_tile(goal.x, goal.y))) continue;
                }
                if (taken != 0 && (goal.type == 3 || goal.type == 8)) continue;
                int32_t limit = (6 / (taken + 1)) * Chassis[Units[v.unit_id].chassis_id].speed * left - taken + 4;
                if (cost > limit && f.region_base_plan[region] != plan && goal.type != 3 && !(taken == 0 && goal.type == 9)) {
                    continue;
                }
                if (goal.type == 8) {
                    MAP* t = map_tile(goal.x, goal.y);
                    int32_t mult = 1;
                    if (!(t->items & BIT_BASE_RADIUS)) {
                        int32_t site = t->val2 >> 4;
                        if (site == 0) {
                            site = world_site(goal.x, goal.y, 0);
                            site_set(goal.x, goal.y, site);
                        }
                        mult = site < 1 ? 1 : site > 15 ? 15 : site;
                    }
                    left *= mult;
                }
                if (v.state & 0x10000000) {
                    int32_t other = veh_at(goal.x, goal.y);
                    if (other >= 0 && stack_check(other, 0xB, 0x10000000, 0x10000000, faction_id) > (cost == 0)) continue;
                }
                if (goal.type == 9) {
                    int32_t gb = goal.base_id;
                    if (gb >= 0 && (Bases[gb].state_flags & 0x20) && v.home_base_id != gb) cost *= 3;
                    bool closer = false;
                    for (int32_t j = 0; j < VehCount; j++) {
                        const VEH& w = Vehs[j];
                        if (w.faction_id != (uint32_t)faction_id || Units[w.unit_id].plan != PLAN_TERRAFORM || j == veh_id) continue;
                        int32_t d = vdist(w.x, w.y, goal.x, goal.y) * 0x100;
                        if (gb >= 0 && (Bases[gb].state_flags & 0x20) && w.home_base_id != gb) d *= 3;
                        if (d < cost) {
                            closer = true;
                            break;
                        }
                    }
                    if (closer) continue;
                    if (assigned[g] != 0 && (gb < 0 || v.home_base_id != gb)) continue;
                }
                int32_t c = cost;
                int32_t owner = whose_territory(faction_id, goal.x, goal.y, nullptr, 0);
                if (owner >= 0 && owner != faction_id) {
                    uint32_t s = f.diplo_status[owner];
                    if (!(s & 0xC10)) {
                        if (s & 0x100000) continue;
                        if (!(s & DIPLO_PACT)) {
                            if (s & DIPLO_TREATY) continue;
                            c = cost * 2;
                        }
                    }
                }
                if (goal.type == 0x10) {
                    if (!has_abil(v.unit_id, ABL_ARTILLERY) || veh_triad(veh_id) != TRIAD_LAND) continue;
                    c /= 2;
                }
                if (c / left < best) {
                    best = c / left;
                    best_goal = g;
                }
            }
            if (best_goal < 0) continue;
            if (v.status_icon < '3' || v.status_icon > '5') assigned[best_goal]++;
            const Goal& goal = f.goals[best_goal];
            v.order = ORDER_MOVE_TO;
            v.status_icon = '1';
            v.waypoint_x[0] = (int16_t)goal.x;
            v.waypoint_y[0] = (int16_t)goal.y;
            if (Units[v.unit_id].plan == PLAN_COLONY) v.movement_turns = 0;
            if (goal.type == 3) v.state |= 0x40000000;
            if (goal.type == 2) {
                int32_t base_id = base_at(goal.x, goal.y);
                if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id) {
                    Bases[base_id].state_flags |= 0x8000000;
                    if (!human()) base_reset(base_id, 0);
                }
            }
        }
    }
    wipe_goals(faction_id);
}
}  // namespace

// ---- air units ----------------------------------------------------------------------------------

namespace {
// The original's rough tile distance for air units: (|dx| + |dy|) / 2 with wrapped dx.
inline int32_t air_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    int32_t dy = std::abs(y1 - y2);
    return (dy + dx) >> 1;
}
// The original's "(|dx| + |dy|) & ~1" closeness test.
inline int32_t air_dist2(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    int32_t dy = std::abs(y1 - y2);
    return (int32_t)((uint32_t)(dy + dx) & ~1u);
}
// No base or unit of a non-native faction on the tile.
inline bool tile_free(const MAP* sq) { return !(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) || (sq->val2 & 0xF) > 7; }
// Owner of the units on the tile (-1: none or natives' nibble).
inline int32_t tile_unit_owner(const MAP* sq) {
    if (!(sq->items & BIT_VEH_IN_TILE)) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner > 7 ? -1 : owner;
}
inline int32_t tile_base_owner(const MAP* sq) {
    if (!(sq->items & BIT_BASE_IN_TILE)) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner > 7 ? -1 : owner;
}
}  // namespace

int32_t GAMEAPI air_power(int32_t veh_id, int32_t dist_own, int32_t zoc, int32_t* px, int32_t* py) {
    VEH& v = Vehs[veh_id];
    auto plan_of = [&]() { return Units[v.unit_id].plan; };
    auto go = [&](int32_t icon, int32_t x, int32_t y) {
        v.order = ORDER_MOVE_TO;
        v.status_icon = (uint8_t)icon;
        v.waypoint_x[0] = (int16_t)x;
        v.waypoint_y[0] = (int16_t)y;
        if (plan_of() == PLAN_COLONY) v.movement_turns = 0;
    };
    auto strike = [&]() { return speed(veh_id, 0) / Rules.move_rate_roads - Rules.move_rate_roads; };
    if (v.order == ORDER_MOVE_TO && (v.waypoint_x[0] != *px || v.waypoint_y[0] != *py)) {
        if (zoc == 0) return -2;
        if (plan_of() == PLAN_AIR_SUPERIORITY) return -2;
        return -1;
    }
    uint32_t faction_id = v.faction_id;
    uint8_t self_bit = (uint8_t)(1 << (faction_id & 31));
    Faction& f = Factions[faction_id];
    int32_t here_region = tile_region(*px, *py);
    const CChassis& ch = Chassis[Units[v.unit_id].chassis_id];
    int32_t range1, range2;  // iVar11, iVar10 (move units, then tiles)
    if (ch.range == 0) {
        range2 = speed(veh_id, 0) * 6;
        range1 = range2;
    } else if (ch.range == 1) {
        int32_t left = speed(veh_id, 0) - v.moves_spent;
        left = left < 0 ? 0 : left > 999 ? 999 : left;
        range2 = left;
        range1 = left;
        if (!v.damage_taken) range1 = speed(veh_id, 0) + left;
    } else {
        int32_t left = speed(veh_id, 0) - v.moves_spent;
        left = left < 0 ? 0 : left > 999 ? 999 : left;
        range1 = speed(veh_id, 0) * ((int32_t)Chassis[Units[v.unit_id].chassis_id].range - v.movement_turns - 1) + left;
        range2 = range1;
    }
    range1 /= Rules.move_rate_roads;
    range2 /= Rules.move_rate_roads;
    if (Chassis[Units[v.unit_id].chassis_id].range != 0 && v.movement_turns != 0) {
        action_home(veh_id, 4);
        if (v.order == ORDER_MOVE_TO) return -2;
    }
    uint32_t state = v.state;
    uint32_t automated = state & 0x200;
    int32_t plan;
    int32_t bx = -1, by = -1;  // local_20 / local_24 (missile target)
    int32_t best;
    if (!automated || v.order_auto_type != 11) {
        if (automated) {
            if (v.order_auto_type == 12) goto auto_patrol;
            if (v.order_auto_type == 10) goto base_attack;
        }
        if ((state & 0x4000) || (!map_is_ocean(*px, *py) && f.region_base_plan[here_region] == 8)) {
            // Explore: the direction with most unseen tiles, else a random own base in range.
            int32_t ex = -1, ey = -1;
            best = 0;
            for (int32_t dir = 0; dir < 8; dir++) {
                int32_t cx = map_wrap_x(NearOffsetX[dir] * (range2 / 2) + *px);
                int32_t cy = NearOffsetY[dir] * (range2 / 2) + *py;
                if (!map_in_bounds(cx, cy)) continue;
                int32_t unseen = 0;
                for (int32_t k = 0; k < 9; k++) {
                    int32_t nx = map_wrap_x(NearOffsetX[k] + cx);
                    int32_t ny = NearOffsetY[k] + cy;
                    if (map_in_bounds(nx, ny) && !(f.player_flags & PFLAG_MAP_REVEALED) &&
                        !(map_tile(nx, ny)->visibility & self_bit)) {
                        unseen++;
                    }
                }
                if (best < unseen) {
                    best = unseen;
                    ex = cx;
                    ey = cy;
                }
            }
            if (ex >= 0) {
                go('a', ex, ey);
                return -2;
            }
            int32_t pick = -1;
            best = -1;
            for (int32_t i = 0; i < BaseCount; i++) {
                if ((uint8_t)Bases[i].faction_id != faction_id) continue;
                int32_t d = air_dist(*px, *py, Bases[i].x, Bases[i].y);
                if (d > range2) continue;
                int32_t r = d < 1 ? 0 : (int32_t)game_rand() % (d + 1);
                if (best < r) {
                    pick = i;
                    best = r;
                }
            }
            if (pick >= 0) {
                go('c', Bases[pick].x, Bases[pick].y);
                return -2;
            }
            if (v.state & 0x4000) return -3;
        }
        if (Chassis[Units[v.unit_id].chassis_id].missile) {
            if ((FactionStatus[0] & self_bit) && MultiplayerActive) return -3;
            if (plan_of() == PLAN_TECTONIC_MISSILE) {
                best = 0;
                for (int32_t g = 0; g < 75; g++) {
                    const Goal& goal = f.goals[g];
                    if (goal.type != AI_GOAL_UNK_4) continue;
                    int32_t s = goal.priority * 8;
                    if (strike() < air_dist(*px, *py, goal.x, goal.y)) s /= 2;
                    MAP* sq = map_tile(goal.x, goal.y);
                    if (tile_free(sq) && (sq->climate & 0xE0) >= 0x60 && best < s) {
                        by = goal.y;
                        bx = goal.x;
                        best = s;
                    }
                }
                for (int32_t i = 0; i < BaseCount; i++) {
                    const BASE& b = Bases[i];
                    uint32_t owner = (uint8_t)b.faction_id;
                    if (owner == faction_id || !(f.diplo_status[owner] & DIPLO_VENDETTA)) continue;
                    MAP* bsq = map_tile(b.x, b.y);
                    if ((bsq->climate & 0xE0) < 0x60) continue;
                    if (f.region_total_bases[bsq->region] != 0) {
                        base_find_2(b.x, b.y, faction_id);
                        if (BaseFindDist <= 9) continue;
                    }
                    int32_t s = (b.pop_size + 4) / 5 * 8;
                    int32_t tx = map_wrap_x(b.x + 4);
                    int32_t ty = b.y;
                    if (!map_in_bounds(tx, ty)) goto stockpile;
                    if (tile_free(map_tile(tx, ty))) {
                        if (strike() < air_dist(*px, *py, tx, ty)) s /= 2;
                        if (best < s) {
                            by = ty;
                            bx = tx;
                            best = s;
                        }
                    }
                }
                if (bx < 0) goto stockpile;
                if (air_dist(*px, *py, bx, by) <= strike()) {
                    go('m', bx, by);
                    return -3;
                }
                best = 9999;
                for (int32_t i = 0; i < 512; i++) {  // BUG(original): all base slots, not BaseCount
                    const BASE& b = Bases[i];
                    if ((uint8_t)b.faction_id != faction_id) continue;
                    if (air_dist(*px, *py, b.x, b.y) > strike()) continue;
                    int32_t d = air_dist(bx, by, b.x, b.y);
                    if (d < best) {
                        go('M', b.x, b.y);
                        best = d;
                    }
                }
                return -3;
            }
            if (plan_of() == PLAN_FUNGAL_MISSILE) {
                best = 0;
                if (BaseCount < 1) goto stockpile;
                for (int32_t i = 0; i < BaseCount; i++) {
                    const BASE& b = Bases[i];
                    uint32_t owner = (uint8_t)b.faction_id;
                    if (owner == faction_id || !(f.diplo_status[owner] & DIPLO_VENDETTA) || Factions[owner].SE_planet_pending >= 1) {
                        continue;
                    }
                    if (f.region_total_bases[tile_region(b.x, b.y)] != 0) {
                        base_find_2(b.x, b.y, faction_id);
                        if (BaseFindDist <= 4) continue;
                    }
                    int32_t s0 = b.pop_size << 3;
                    for (int32_t k = 0; k < 21; k++) {
                        int32_t tx = map_wrap_x(TableOffsetX[k] + b.x);
                        int32_t ty = TableOffsetY[k] + b.y;
                        if (!map_in_bounds(tx, ty)) continue;
                        MAP* sq = map_tile(tx, ty);
                        if (((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) || !tile_free(sq)) continue;
                        int32_t s = s0;
                        if (strike() < air_dist(*px, *py, tx, ty)) s = s0 / 2;
                        int32_t bits = bit_count(map_tile(tx, ty)->items & 0x1E8881C);
                        if (best < s + bits) {
                            by = ty;
                            bx = tx;
                            best = s + bits;
                        }
                    }
                }
                if (bx < 0) goto stockpile;
                if (strike() < air_dist(*px, *py, bx, by)) {
                    best = 9999;
                    for (int32_t i = 0; i < 512; i++) {  // BUG(original): all base slots
                        const BASE& b = Bases[i];
                        if ((uint8_t)b.faction_id != faction_id) continue;
                        if (air_dist(*px, *py, b.x, b.y) > strike()) continue;
                        int32_t d = air_dist(bx, by, b.x, b.y);
                        if (d < best) {
                            go('M', b.x, b.y);
                            best = d;
                        }
                    }
                } else {
                    go('m', bx, by);
                }
            } else {
                // Conventional missile: best enemy stack in reach.
                int32_t pods = 0;  // param_3: drop-pod units / threat counter
                for (int32_t k = 0, u = 64; k < 64; k++, u += faction_id) {  // BUG(original): steps by faction_id
                    if ((Units[u].unit_flags & 1) && has_abil(u, ABL_DROP_POD)) pods += f.units_active[u];
                }
                int32_t threshold = Units[v.unit_id].cost << 4;
                for (int32_t j = 0; j < VehCount; j++) {
                    VEH& t = Vehs[j];
                    uint32_t owner = t.faction_id;
                    if (owner == faction_id || !(f.diplo_status[owner] & DIPLO_VENDETTA)) continue;
                    int32_t base_id = base_at(t.x, t.y);
                    bool close = false;
                    int32_t near;
                    if (t.order == ORDER_TERRAFORM_DOWN && map_is_ocean(t.x, t.y) &&
                        (near = base_find_2(t.x, t.y, faction_id)) >= 0 &&
                        air_dist2(t.x, t.y, Bases[near].x, Bases[near].y) <= 4) {
                        close = true;
                    } else if ((t.faction_id != faction_id || (t.flags & 0x440) == 0x440) && !(self_bit & t.visibility) &&
                               (base_id < 0 || ((uint8_t)Bases[base_id].faction_id != faction_id &&
                                                !(Bases[base_id].visibility & self_bit)))) {
                        continue;
                    }
                    if (veh_triad(j) == TRIAD_AIR && base_id < 0) continue;
                    int32_t odds = compute_odds(battle_fight_2(veh_id, 0, t.x, t.y, 0, 0, nullptr), faction_id, veh_id, j, -2);
                    int32_t mult;
                    if (base_id < 0) {
                        odds /= 2;
                        mult = 4;
                        if (!stack_check(j, 2, PLAN_NAVAL_TRANSPORT, -1, -1) &&
                            (!stack_check(j, 2, PLAN_TERRAFORM, -1, -1) || !map_is_ocean(t.x, t.y)) &&
                            (uint32_t)whose_territory(faction_id, t.x, t.y, nullptr, 0) != faction_id) {
                            mult = 2;
                            odds /= 2;
                        } else {
                            pods++;
                        }
                    } else if (pods == 0) {
                        bool found = false;
                        for (int32_t k = 0; k < 25; k++) {
                            int32_t tx = map_wrap_x(TableOffsetX[k] + Bases[base_id].x);
                            int32_t ty = TableOffsetY[k] + Bases[base_id].y;
                            if (!map_in_bounds(tx, ty)) continue;
                            if ((uint32_t)tile_unit_owner(map_tile(tx, ty)) == faction_id) {
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                            pods = 1;
                            mult = 0x10;
                        } else {
                            mult = 4;
                            odds /= 2;
                        }
                    } else {
                        mult = 0x10;
                    }
                    int32_t art = stack_check(j, 0x13, -1, -1, -1);
                    int32_t n = stack_check(j, 1, -1, -1, -1);
                    if (n - art <= stack_check(j, 3, 2, -1, -1)) {
                        odds *= 2;
                        mult *= 2;
                        pods++;
                    }
                    if (pods) {
                        if (FactionStatus[0] & (uint8_t)(1 << (owner & 31))) mult *= 2;
                        if (Factions[owner].ranking == 7) mult *= 2;
                    }
                    if (close) odds <<= 3;
                    if (strike() < air_dist(*px, *py, t.x, t.y)) mult /= 2;
                    if (odds / 2 > 7 && threshold <= odds / 2 * mult) {
                        threshold = odds / 2 * mult;
                        if (strike() < air_dist(*px, *py, t.x, t.y)) {
                            best = 9999;
                            for (int32_t i = 0; i < 512; i++) {  // BUG(original): all base slots
                                const BASE& b = Bases[i];
                                if ((uint8_t)b.faction_id != faction_id) continue;
                                if (air_dist(*px, *py, b.x, b.y) > strike()) continue;
                                int32_t d = air_dist(t.x, t.y, b.x, b.y);
                                if (d < best) {
                                    go('M', b.x, b.y);
                                    best = d;
                                }
                            }
                        } else {
                            go('m', t.x, t.y);
                        }
                    }
                }
            }
            if (v.order == ORDER_MOVE_TO) {
                if ((uint32_t)tile_base_owner(map_tile(v.waypoint_x[0], v.waypoint_y[0])) == faction_id) {
                    *px = v.waypoint_x[0];
                    *py = v.waypoint_y[0];
                    v.order = ORDER_NONE;
                    v.visibility = 0;
                    veh_drop(veh_lift(veh_id), *px, *py);
                    return -3;
                }
                v.order = ORDER_NONE;
                if (plan_of() == PLAN_TECTONIC_MISSILE) {
                    veh_drop(veh_lift(veh_id), bx, by);
                    action_tectonic(veh_id, bx, by);
                    return -5;
                }
                if (plan_of() == PLAN_FUNGAL_MISSILE) {
                    veh_drop(veh_lift(veh_id), bx, by);
                    action_fungal(veh_id, bx, by);
                    return -5;
                }
                for (int32_t dir = 0; dir < 8; dir++) {
                    int32_t nx = map_wrap_x(NearOffsetX[dir] + v.waypoint_x[0]);
                    int32_t ny = NearOffsetY[dir] + v.waypoint_y[0];
                    if (!map_in_bounds(nx, ny) || !tile_free(map_tile(nx, ny))) continue;
                    *px = nx;
                    *py = ny;
                    v.visibility = 0;
                    veh_drop(veh_lift(veh_id), nx, ny);
                    v.rotate_angle = (int8_t)dir;
                    return dir ^ 4;
                }
                v.order = ORDER_NONE;
            }
        stockpile: {
            int32_t n = stack_check(veh_id, 0xD, -1, -1, -1);
            if (n < 2) return -3;
            for (int32_t i = 0; i < BaseCount; i++) {
                const BASE& b = Bases[i];
                if ((uint8_t)b.faction_id != faction_id) continue;
                if (stack_check(veh_at(b.x, b.y), 0xD, -1, -1, -1) >= n) continue;
                if (air_dist(*px, *py, b.x, b.y) > strike()) continue;
                veh_drop(veh_lift(veh_id), b.x, b.y);
                v.visibility = 0;
                *px = b.x;
                *py = b.y;
                return -3;
            }
            return -3;
        }
        }
    } else {
    auto_patrol:
        if (v.damage_taken) {
            action_home(veh_id, 0);
            return -2;
        }
    }
    plan = (int8_t)plan_of();
    if (plan == PLAN_AIR_SUPERIORITY || (automated && v.order_auto_type == 12) || plan == PLAN_OFFENSE) {
        // Interception / escort.
        int32_t target = -1;
        int32_t target_dist = 9999;  // local_14
        int32_t score_best = 9999;   // local_28
        for (int32_t j = 0; j < VehCount; j++) {
            VEH& t = Vehs[j];
            MAP* tsq = map_tile(t.x, t.y);
            if (faction_id == t.faction_id) {
                bool cand = plan_of() == PLAN_AIR_SUPERIORITY && Chassis[Units[v.unit_id].chassis_id].range != 1 &&
                            (!(tsq->items & BIT_BASE_IN_TILE) || (tsq->val2 & 0xF) > 7) && !(tsq->items & BIT_AIRBASE) &&
                            !stack_check(j, 6, ABL_CARRIER, -1, -1);
                if (!cand) continue;
                int32_t left = speed(j, 0) - t.moves_spent;
                if (!(left < 0 || (left < 1000 && left == 0))) continue;
                if (stack_check(j, 2, PLAN_AIR_SUPERIORITY, -1, -1)) continue;
                int32_t tu = t.unit_id;
                if (!((unit_triad(tu) == TRIAD_AIR && Units[tu].plan != PLAN_AIR_SUPERIORITY) || Units[tu].plan == PLAN_NAVAL_TRANSPORT)) {
                    continue;
                }
                int32_t d = air_dist(*px, *py, t.x, t.y);
                int32_t s = d * 2 + 1;
                if (speed(veh_id, 0) / Rules.move_rate_roads < d) s *= 4;
                s /= stack_check(j, 1, -1, -1, -1) + 1;
                if (s <= score_best) {
                    target = j;
                    score_best = s;
                    target_dist = d;
                }
                continue;
            }
            bool close = false;
            int32_t near;
            if (t.order == ORDER_TERRAFORM_DOWN && map_is_ocean(t.x, t.y) &&
                (near = base_find_2(t.x, t.y, faction_id)) >= 0 &&
                air_dist2(t.x, t.y, Bases[near].x, Bases[near].y) <= 4) {
                close = true;
            } else if ((t.faction_id != faction_id || (t.flags & 0x440) == 0x440) && !(self_bit & t.visibility)) {
                continue;
            }
            if (f.diplo_status[t.faction_id] & 7) continue;
            bool consider = false;
            if (veh_triad(j) == TRIAD_AIR) {
                if (plan_of() == PLAN_AIR_SUPERIORITY && (!(tsq->items & BIT_BASE_IN_TILE) || (tsq->val2 & 0xF) > 7) &&
                    !(tsq->items & BIT_AIRBASE)) {
                    consider = true;
                }
            } else if (plan_of() != PLAN_AIR_SUPERIORITY || !(v.state & 0x200) || v.order_auto_type != 11) {
                int32_t odds = compute_odds(battle_fight_2(veh_id, 0, t.x, t.y, 0, 0, nullptr), faction_id, veh_id, j, -2);
                MAP* sq = map_tile(t.x, t.y);
                if (tile_base_owner(sq) >= 0) {
                    if (plan_of() == PLAN_OFFENSE) continue;
                    int32_t art = stack_check(j, 0x13, -1, -1, -1);
                    if (stack_check(j, 1, -1, -1, -1) - art != 1) odds /= 2;
                }
                int32_t s = odds * 2 / 3;
                if (close) s *= 8;
                if (plan_of() == PLAN_AIR_SUPERIORITY || !(v.state & 0x200) || v.order_auto_type != 12) s /= 2;
                consider = s > 7;
            }
            if (!consider) continue;
            {
                bool ok = f.unk_67 == 0 || f.unk_67 * 2 <= f.unk_105 ||
                          (uint32_t)whose_territory(faction_id, t.x, t.y, nullptr, 0) == faction_id;
                if (!ok) {
                    int32_t bf = base_find(t.x, t.y);
                    ok = bf < 0 || (uint8_t)Bases[bf].faction_id == faction_id;
                }
                if (!ok) continue;
                bool score_it;
                if (!(v.state & 0x200) || v.order_auto_type != 12) {
                    score_it = true;
                } else if (map_is_ocean(t.x, t.y)) {
                    base_find_2(t.x, t.y, faction_id);
                    score_it = BaseFindDist < 3;
                } else {
                    score_it = (uint32_t)whose_territory(faction_id, t.x, t.y, nullptr, 0) == faction_id;
                }
                if (!score_it) continue;
                int32_t d = air_dist(*px, *py, t.x, t.y);
                int32_t s;
                if (range1 / 2 < d) {
                    s = d * 2;
                } else {
                    int32_t r = tile_region(t.x, t.y);
                    if (f.region_base_plan[r] == 2) {
                        s = d >> 1;
                    } else {
                        s = d;
                        if (f.region_total_bases[r] == 0) s = d * 2;
                    }
                    if (close) {
                        s = 0;
                        d = 0;
                    }
                }
                if (s <= score_best) {
                    target = j;
                    score_best = s;
                    target_dist = d;
                }
            }
        }
        if (target >= 0) {
            const VEH& t = Vehs[target];
            if (t.faction_id == faction_id) target_dist = (target_dist - 8) / 2;
            if (target_dist <= range1 / 2) {
                go('a', t.x, t.y);
                if (*px != t.x) return -2;
                if (*py != t.y) return -2;
                return -1;
            }
            if ((!(v.state & 0x200) || v.order_auto_type != 11) &&
                (plan_of() != PLAN_OFFENSE || ((v.state & 0x200) && v.order_auto_type == 12))) {
                int32_t tx = t.x, ty = t.y;
                for (int32_t i = 0; i < BaseCount; i++) {
                    const BASE& b = Bases[i];
                    uint32_t owner = (uint8_t)b.faction_id;
                    if (owner != faction_id && !(f.diplo_status[owner] & DIPLO_PACT)) continue;
                    int32_t d = air_dist(tx, ty, b.x, b.y);
                    if (d <= range2 && d < target_dist) {
                        go('a', b.x, b.y);
                        target_dist = d;
                    }
                }
                if (v.order == ORDER_MOVE_TO) return -2;
            }
        }
        plan = 0;
        if (plan_of() != PLAN_OFFENSE) {
            if (dist_own == 0) return -3;
            action_home(veh_id, 0);
            return -2;
        }
    }
    automated = v.state & 0x200;
    if (automated && v.order_auto_type == 11) {
        // Air patrol: strike the best enemy unit next to us, else return to the patrol point.
        if ((plan == 0 || plan == 6) && v.order != ORDER_MOVE_TO) {
            best = 0;
            int32_t count = range1 < 2 ? 8 : 24;
            for (int32_t k = 1; k <= count; k++) {
                int32_t tx = map_wrap_x(TableOffsetX[k] + *px);
                int32_t ty = TableOffsetY[k] + *py;
                if (!map_in_bounds(tx, ty)) continue;
                int32_t top = stack_fix(veh_at(tx, ty));
                if (top < 0) continue;
                uint32_t owner = Vehs[top].faction_id;
                if (owner == faction_id || !(f.diplo_status[owner] & DIPLO_VENDETTA) || veh_triad(top) == TRIAD_AIR) continue;
                MAP* sq = map_tile(tx, ty);
                bool seen = veh_triad(veh_id) == TRIAD_AIR ||
                            (((f.player_flags & PFLAG_MAP_REVEALED) || (sq->visibility & self_bit)) &&
                             (tile_base_owner(sq) >= 0 || (Vehs[top].visibility & self_bit)) &&
                             (((sq->climate & 0xE0) < 0x60) == (veh_triad(veh_id) == TRIAD_SEA)));
                if (!seen) continue;
                int32_t o = offense_proto(Vehs[top].unit_id, -1, 0);
                if (veh_triad(top) == TRIAD_LAND) o *= 3;
                int32_t s = o;
                if ((map_tile(tx, ty)->climate & 0xE0) < 0x60 && stack_check(top, 2, PLAN_TERRAFORM, -1, -1)) {
                    s = o * 2;
                    if (f.player_flags & PFLAG_UNK_80000) s = o * 4;
                }
                if (best < s) {
                    go('L', tx, ty);
                    best = s;
                }
            }
            if (v.order == ORDER_MOVE_TO) return (zoc != 0) - 2;
        }
        if (v.order != ORDER_MOVE_TO && (v.x != v.waypoint_x[1] || v.y != v.waypoint_y[1])) {
            go('l', v.waypoint_x[1], v.waypoint_y[1]);
            return -2;
        }
        return -4;
    }
base_attack:
    if (Units[v.unit_id].plan == PLAN_OFFENSE || (automated && v.order_auto_type == 10)) {
        int32_t tx = -1, ty = -1;
        int32_t no_drop = 0;  // local_4c
        best = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            const BASE& b = Bases[i];
            uint32_t owner = (uint8_t)b.faction_id;
            if (owner == faction_id || (f.diplo_status[owner] & 7)) continue;
            bool patrol = (v.state & 0x200) && v.order_auto_type == 10;
            if (patrol && (b.x != v.waypoint_x[1] || b.y != v.waypoint_y[1])) continue;
            int32_t top = stack_fix(veh_at(b.x, b.y));
            if (top < 0) continue;
            if (!patrol) {
                int32_t odds = compute_odds(battle_fight_2(veh_id, 0, Vehs[top].x, Vehs[top].y, 0, 0, nullptr), faction_id,
                                            veh_id, top, -2);
                MAP* sq = map_tile(Vehs[top].x, Vehs[top].y);
                if (tile_base_owner(sq) < 0) {
                    odds /= 2;
                } else {
                    int32_t art = stack_check(top, 0x13, -1, -1, -1);
                    if (stack_check(top, 1, -1, -1, -1) - art != 1) odds /= 2;
                }
                if (odds < 8) continue;
            }
            int32_t region = tile_region(b.x, b.y);
            int32_t base_score = b.pop_size + f.region_total_bases[region] * 2;
            int32_t value = base_score * 0x20;
            bool support = false;
            for (int32_t k = 1; k < 9; k++) {
                int32_t nx = map_wrap_x(TableOffsetX[k] + b.x);
                int32_t ny = TableOffsetY[k] + b.y;
                if (!map_in_bounds(nx, ny)) continue;
                MAP* sq = map_tile(nx, ny);
                if (sq->region != region) continue;
                if ((uint32_t)tile_unit_owner(sq) == faction_id) {
                    support = true;
                    value = base_score * 0x40 + 1;
                    break;
                }
            }
            int32_t weak = 0;  // local_30
            if (!support && !patrol) {
                weak = f.unk_105 < stack_check(veh_at(b.x, b.y), 1, -1, -1, -1) * 2;
                int32_t idx = abil_index(ABL_DROP_POD);
                if (!has_tech(Ability[idx].preq_tech, faction_id)) {
                    weak = 1;
                    value /= 8;
                } else if (weak) {
                    value /= 8;
                }
            }
            if (b.event_flags & (1 << (faction_id & 31))) value *= 4;
            if (b.state_flags & 8) value *= 3;
            if (b.state_flags & 0x100000) value *= 2;
            if (i == f.base_id_attack_target) value *= 2;
            if (region == here_region) value *= 2;
            if (f.region_base_plan[region] < 3) value <<= 2;
            int32_t step = range1 / 2;
            step = step < 1 ? 1 : step > 99 ? 99 : step;
            int32_t s = value / ((air_dist(*px, *py, b.x, b.y) - 1) / step + 1);
            if (best <= s) {
                no_drop = weak;
                tx = b.x;
                ty = b.y;
                best = s;
            }
        }
        if (ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX) {
            if (range1 / 2 < air_dist(*px, *py, tx, ty)) {
                int32_t bd = 9999;
                for (int32_t i = 0; i < BaseCount; i++) {
                    const BASE& b = Bases[i];
                    uint32_t owner = (uint8_t)b.faction_id;
                    if (owner != faction_id && !(f.diplo_status[owner] & DIPLO_PACT)) continue;
                    int32_t d = air_dist(tx, ty, b.x, b.y);
                    if (d <= range2 && d < bd) {
                        go('a', b.x, b.y);
                        bd = d;
                    }
                }
            } else if (no_drop == 0) {
                go('a', tx, ty);
            }
        }
        if (v.order == ORDER_MOVE_TO) {
            if (zoc != 0 && (!(v.state & 0x200) || v.order_auto_type != 10)) return -1;
            return -2;
        }
    }
    return -3;
}

// ---- Console_upgrade (ui module 0x4D06C0): unit upgrade ----------------------------------------------------

// The engine part of the upgrade command. For AI factions (enemy_strategy) the first prototype that
// may replace the unit is bought if the faction can afford it; human factions get the original
// dialog (Console_upgrade_human, UI bridge). The original builds its popup objects in both cases
// (no game state).
int32_t THISCALL Console_upgrade(uint8_t* console, int32_t veh_id) {
    if (veh_id < 0) return 0;
    int32_t faction_id = Vehs[veh_id].faction_id;
    uint8_t self_bit = (uint8_t)(1 << faction_id);
    if (FactionStatus[0] & self_bit) return Console_upgrade_human(console, veh_id);
    int32_t unit_id = Vehs[veh_id].unit_id;
    if (unit_id < 64 && (Weapon[Units[unit_id].weapon_id].offense_value < 0 || unit_id == 15)) return 0;
    for (int32_t i = 0; i < 64; i++) {
        int32_t new_id = faction_id * 64 + i;
        const UNIT& u = Units[new_id];
        if (!(u.unit_flags & 1) || !(u.unit_flags & 4) || (u.obsolete_factions & self_bit)) continue;
        unit_id = Vehs[veh_id].unit_id;
        const UNIT& old = Units[unit_id];
        if (new_id == unit_id || u.group_id != old.group_id || u.chassis_id != old.chassis_id) continue;
        int32_t abil_new = has_abil(new_id, 0x8000);
        if (abil_new != has_abil(unit_id, 0x8000)) continue;
        if (u.plan != old.plan) continue;
        int8_t offense = Weapon[old.weapon_id].offense_value;
        if (offense == 0 ? old.weapon_id != u.weapon_id : offense > Weapon[u.weapon_id].offense_value) continue;
        if (Armor[old.armor_id].defense_value > Armor[u.armor_id].defense_value) continue;
        // The first candidate decides.
        int32_t cost = upgrade_cost(faction_id, new_id, unit_id) * 10;
        Faction& f = Factions[faction_id];
        int32_t discount = DiffLevel * cost / 10 / (8 - Factions[RankingFactionIDUnk1].ranking);
        int32_t credits = f.energy_credits;
        if (credits > Factions[RankingFactionIDUnk2].energy_credits) {
            if (credits >= 1000) discount /= 2;
            if (credits >= 2000) discount = 0;
        }
        cost -= discount;
        if (cost >= credits / ((u.plan != 2) - f.AI_power + f.AI_wealth + 1 + f.base_count)) return 0;
        f.energy_credits = credits - cost;
        f.units_active[unit_id]--;
        Vehs[veh_id].unit_id = (int16_t)new_id;
        f.units_active[new_id]++;
        if (unit_triad(new_id) != 2) veh_skip(veh_id);
        return 1;
    }
    return 0;
}
