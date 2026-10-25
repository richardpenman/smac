#include "engine/base_misc.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/util.h"
#include "engine/veh.h"

int32_t GAMEAPI vector_dist(int32_t dx, int32_t dy) {
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t big = a > b ? a : b;
    int32_t small = a < b ? a : b;
    return big - ((((a + b) >> 1) - small + 1) >> 1);
}

int32_t GAMEAPI has_project(int32_t project_id, int32_t faction_id) {
    int32_t base_id = SecretProjects[project_id];
    if (base_id < 0) return 0;
    return (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id;
}

int32_t GAMEAPI base_claimed(int32_t item_id, int32_t score, int32_t skip_0, int32_t skip_1, int32_t skip_2,
                             int32_t skip_3) {
    if (!skip_0 && item_id == BasePriorityChoice[0] && score >= BasePriorityScore[0]) return 1;
    if (!skip_1 && item_id == BasePriorityChoice[1] && score >= BasePriorityScore[1]) return 1;
    if (!skip_2 && item_id == BasePriorityChoice[2] && score >= BasePriorityScore[2]) return 1;
    if (!skip_3 && item_id == BasePriorityChoice[3] && score >= BasePriorityScore[3]) return 1;
    return 0;
}

namespace {
// Plan/triad statistics tables of base_build: index triad + plan * 3 (plans 0-17: the unit plans
// plus 15 small transports, 16 terraformer abilities on land, 17 group 21 units).
constexpr int kPlanTriads = 54;
inline int32_t pt(int32_t triad, int32_t plan) { return triad + plan * 3; }
// Wrapped horizontal distance |x1 - x2| as the original computes it.
int32_t wrap_dx(int32_t x1, int32_t x2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    return dx;
}

// Raw reads of the original tables (base_build indexes them with ids out of range, see the BUG
// notes at the call sites). Hybrid builds read the original memory; native builds read the
// memory image (tables with pointers live outside it, so out-of-range reads there differ).
int16_t raw_veh_unit_id(int32_t veh_id) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&Vehs[0]) + veh_id * (int32_t)sizeof(VEH);
    return *reinterpret_cast<const int16_t*>(p + offsetof(VEH, unit_id));
}
uint8_t raw_unit_chassis(int32_t unit_id) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&Units[0]) + unit_id * (int32_t)sizeof(UNIT);
    return p[offsetof(UNIT, chassis_id)];
}
uint8_t raw_chassis_speed(int32_t chassis_id) {
#ifdef SMACX_HYBRID
    return reinterpret_cast<const uint8_t*>(&Chassis[0])[chassis_id * 0x90 + 0x48];
#else
    if (chassis_id < 9) return Chassis[chassis_id].speed;
    return *mem::host(0x94A330 + chassis_id * 0x90 + 0x48);
#endif
}
}  // namespace

// AI production choice (see base_misc.h). Returns the item (unit id >= 0, -facility id).
//
// The function scores every available unit and facility; lower scores are better. Most of the
// state below is derived statistics about the faction's bases and units: counts of production
// and vehicles per plan/triad, facility counts, threats, and the AI/governor priorities.
int32_t GAMEAPI base_build(int32_t base_param, int32_t* out_a2, int32_t* out_a3, int32_t* score_out) {
    int32_t no_gov = base_param < 0;  // [ebp-0x108]
    int32_t base_id = base_param < 0 ? -1 - base_param : base_param;
    set_base(base_id);
    base_compute(1);
    const int32_t bx = Bases[base_id].x;
    const int32_t by = Bases[base_id].y;
    const int32_t faction_id = (uint8_t)Bases[base_id].faction_id;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    const int32_t is_sea = (map_tile(bx, by)->climate & 0xE0) < 0x60;
    const int32_t region = map_tile(bx, by)->region;
    const int32_t plan_region = f.region_base_plan[region];
    const int32_t best_reac = best_reactor(faction_id);
    const uint32_t fbit = 1u << faction_id;
    auto human = [](int32_t id) -> bool { return (FactionStatus[0] & (uint8_t)(1u << (id & 31))) != 0; };
    const bool is_human = (FactionStatus[0] & (uint8_t)fbit) != 0;

    // AI priorities (or the governor's priorities for human bases).
    int32_t ai_growth, ai_tech, ai_wealth, ai_power, ai_fight;
    int32_t gov_explore, gov_discover, gov_build, gov_conquer;
    if (is_human && (cur_base().governor_flags & GOV_ACTIVE)) {
        uint32_t gov = (uint32_t)cur_base().governor_flags;
        if (!(gov & 0xF200000) && CurrentBaseID >= 8) {
            uint8_t bp = f.region_base_plan[region];
            if (bp <= 2) gov |= GOV_PRIORITY_CONQUER;
            if (bp == 8) gov |= GOV_PRIORITY_EXPLORE;
            if (bp == 7) gov |= GOV_PRIORITY_DISCOVER;
            gov |= 0x1000000u << (CurrentBaseID & 3);
        }
        ai_growth = (gov >> 24) & 1;
        ai_tech = (gov >> 25) & 1;
        ai_wealth = (gov >> 26) & 1;
        ai_power = (gov >> 27) & 1;
        if (ai_power) ai_fight = 1;
        else if (ai_growth) ai_fight = 0;
        else if (!ai_tech && !ai_wealth) ai_fight = f.AI_fight;
        else ai_fight = -1;
        gov_explore = ai_growth;
        gov_discover = ai_tech;
        gov_build = ai_wealth;
        gov_conquer = ai_power;
        if (ai_growth && ai_tech && ai_wealth && ai_power) {
            gov_conquer = gov_build = gov_discover = gov_explore = 0;
        }
    } else {
        ai_growth = f.AI_growth;
        ai_tech = f.AI_tech;
        ai_wealth = f.AI_wealth;
        ai_power = f.AI_power;
        ai_fight = f.AI_fight;
        gov_conquer = gov_build = gov_discover = gov_explore = 0;
    }
    int32_t regions_with_bases = 0;  // [ebp-0xfc]
    for (int32_t i = 0; i < 128; i++)
        if (f.region_total_bases[i]) regions_with_bases++;

    // Production of the other bases and vehicles, per unit and per plan/triad.
    static int32_t queued_region[512];   // [ebp-0xF6C] own bases in the region building the unit
    static int32_t queued_all[512];      // [ebp-0x1F6C] own other bases building the unit
    static int32_t units_region[512];    // [ebp-0x176C] own vehicles in the region
    int32_t veh_region[kPlanTriads];     // [ebp-0x4BC]
    int32_t veh_all[kPlanTriads];        // [ebp-0x2EC]
    int32_t prod_region[kPlanTriads];    // [ebp-0x594]
    int32_t prod_all[kPlanTriads];       // [ebp-0x3C4]
    int32_t veh_home[kPlanTriads];       // [ebp-0x76C]
    int32_t prod_triad_region[3] = {0, 0, 0};  // [ebp-0x1DC]
    std::memset(queued_region, 0, sizeof(queued_region));
    std::memset(queued_all, 0, sizeof(queued_all));
    std::memset(units_region, 0, sizeof(units_region));
    std::memset(veh_region, 0, sizeof(veh_region));
    std::memset(veh_all, 0, sizeof(veh_all));
    std::memset(prod_region, 0, sizeof(prod_region));
    std::memset(prod_all, 0, sizeof(prod_all));
    std::memset(veh_home, 0, sizeof(veh_home));
    int32_t hq_count = 0, bioenh_count = 0, cmdcenter_count = 0, preserve_count = 0;
    int32_t navalyard_count = 0, aerospace_count = 0, skunk_count = 0;
    int32_t target_flags = 0;  // [ebp-0x104]: 2 attack target in the region, 1 elsewhere (plan 7)
    const int32_t has_skunk = base_fac(CurrentBaseID, FAC_SKUNKWORKS);  // [ebp-0x124]
    int32_t former_count = 0, same_prod = 0, same_prod_foreign = 0, faster_project = 0;
    int32_t projects_region = 0, prototypes = 0, max_pop_other = 0, psigate_count = 0;
    int32_t ascent_count = 0, ascent_human = 0, sea_bases = 0, vendetta_plan7 = 0, punish_count = 0;
    // Counted by the original but never read afterwards.
    (void)preserve_count, (void)same_prod_foreign, (void)psigate_count, (void)ascent_count;
    int32_t threat = (plan_region == 0 || plan_region == 2) ? 1 : 0;  // [ebp-0x70]
    int32_t pact_faction = -1;
    for (int32_t i = 1; i < 8; i++) {
        if (faction_id != i && (f.diplo_status[i] & 1)) {
            if (human(i) || pact_faction < 0) pact_faction = i;
        }
    }
    int32_t enemy_region = -1, enemy_elsewhere = -1, seen_by_enemy = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (faction_id == i) continue;
        uint32_t st = f.diplo_status[i];
        uint32_t vendetta = st & 0x10;
        if (vendetta || (human(i) && (st & 8) && !(st & 1))) {
            for (int32_t b = 0; b < BaseCount; b++) {
                int32_t owner = (uint8_t)Bases[b].faction_id;
                if (owner == faction_id && (owner == i || (Bases[b].visibility & (uint8_t)(1 << i)))) {
                    seen_by_enemy = 1;
                    break;
                }
            }
        }
        if (!vendetta) continue;
        if (plan_region == 7) vendetta_plan7 = 1;
        if ((pact_faction >= 0 && (Factions[pact_faction].diplo_status[i] & 0x10)) ||
            (human(i) && (GameRules & 0x40)) || ((st & 0x20) && Factions[i].diff_level >= 3) || (st & 0x40) ||
            (Factions[i].ranking > f.ranking && ai_fight >= 0)) {
            if (plan_region == 7) threat = 1;
            if (Factions[i].region_total_bases[region]) enemy_region = i;
            else if (plan_region == 7) enemy_elsewhere = i;
        }
    }
    if (plan_region == 7 && vendetta_plan7 && (GameState & 0x200)) threat = 2;
    int32_t target_here = 0;  // [ebp-0x138]
    if (f.target_x >= 0) {
        if (map_tile(f.target_x, f.target_y)->region == region) target_here = 1;
        if (bx == f.target_x && by == f.target_y) target_here = 2;
    }
    int32_t colony_sites = 0;  // [ebp-0x150]
    for (int32_t i = 0; i < 25; i++) {
        Goal& site = f.sites[i];
        if (site.type == 3 && site.priority > 0 && map_tile(site.x, site.y)->region == region) {
            int32_t owner = whose_territory(faction_id, site.x, site.y, nullptr, 0);
            if (owner < 0 || owner == faction_id) colony_sites++;
        }
    }
    int32_t psych_need = 0;  // [ebp-0x74]
    {
        BASE& cb = cur_base();
        if (cb.drone_total) {
            psych_need = cb.drone_total >= cb.talent_total;
            if (!is_human || cb.minerals_accumulated <= Rules.retool_exemption) {
                if ((cb.state_flags & BSTATE_DRONE_RIOTS_ACTIVE) ||
                    (psych_need && cb.specialist_total - cb.specialist_adjust > 1))
                    psych_need++;
            }
        }
    }
    const int32_t mineral_surplus = cur_base().mineral_surplus;
    const int32_t mineral_output = cur_base().mineral_intake_2 + mineral_surplus * 2;  // [ebp-0xE0]
    int32_t building_project;  // [ebp-0xB0]
    if (score_out)
        building_project = cur_base().production_id_last <= -70 && cur_base().minerals_accumulated > Rules.retool_exemption;
    else
        building_project = cur_base().queue_items[0] <= -70;
    int32_t projects_by_group[8] = {0};  // [ebp-0x3E4] (counted, unused)
    uint32_t project_builders[64] = {0};  // [ebp-0x694] factions building each project
    int32_t projects_here = 0;  // [ebp-0x148]
    for (int32_t i = 0; i < 64; i++) {
        int32_t b = SecretProjects[i];
        if (b == CurrentBaseID) projects_here++;
        if (b >= 0 && (uint8_t)Bases[b].faction_id == (uint32_t)faction_id) projects_by_group[i / 8]++;
    }
    if (building_project && cur_base().nutrient_surplus > 0 && mineral_surplus > 0 &&
        cur_base().specialist_total - cur_base().specialist_adjust <= cur_base().pop_size / 4 + 1)
        psych_need = 1;

    int32_t naval_yard_dist = 24, aerospace_dist = 24;
    int32_t unk1_flag = 0;          // [ebp-0x6C]: a base of the region has state flag 1 (2: this base)
    int32_t flag_100000_count = 0;  // [ebp-0xD0]
    int32_t undergarrisoned = 0;    // [ebp-0x160]
    int32_t home_defenders = 0;     // [ebp-0xC4] (base loop)
    int32_t max_pop = 0;            // [ebp-0xD8]
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& base = Bases[i];
        int32_t owner = (uint8_t)base.faction_id;
        if (faction_id != owner) {
            if (f.base_id_attack_target == i && (f.diplo_status[owner] & 0x10)) {
                if (map_tile(base.x, base.y)->region == region) target_flags |= 2;
                else if (plan_region == 7) target_flags |= 1;
            }
            int32_t item = base.queue_items[0];
            if (item == -FAC_ASCENT_TO_TRANSCENDENCE) {
                ascent_count++;
                if (human(owner)) ascent_human++;
            }
            if (item == cur_base().queue_items[0]) same_prod_foreign++;
            if (item <= -70) project_builders[-item - 70] |= 1u << owner;
            if (human(owner) && (uint8_t)base.faction_id_former == (uint32_t)faction_id &&
                map_tile(base.x, base.y)->region == region && !(f.diplo_status[owner] & 1)) {
                if (++former_count > 1 - ai_fight) threat = 1;
            }
            continue;
        }
        if (base.pop_size > max_pop) max_pop = base.pop_size;
        if ((map_tile(base.x, base.y)->climate & 0xE0) < 0x60) sea_bases++;
        int32_t dist = vector_dist(wrap_dx(bx, base.x), std::abs(by - base.y));
        if (base_fac(i, FAC_NAVAL_YARD) && dist < naval_yard_dist) naval_yard_dist = dist;
        if ((base_fac(i, FAC_AEROSPACE_COMPLEX) ||
             (SecretProjects[FAC_CLOUDBASE_ACADEMY - 70] >= 0 &&
              Bases[SecretProjects[FAC_CLOUDBASE_ACADEMY - 70]].faction_id == base.faction_id)) &&
            dist < aerospace_dist)
            aerospace_dist = dist;
        if (base_fac(i, FAC_PSI_GATE)) psigate_count++;
        if (base_fac(i, FAC_PUNISHMENT_SPHERE)) punish_count++;
        if (base_fac(i, FAC_HEADQUARTERS) || base.queue_items[0] == -1) hq_count++;
        if (i == CurrentBaseID) continue;
        if (base.state_flags & 0x100000) flag_100000_count++;
        if (base.pop_size > max_pop_other) max_pop_other = base.pop_size;
        BASE& cb = cur_base();
        if (base.queue_items[0] == cb.queue_items[0]) {
            same_prod++;
            if (cb.queue_items[0] <= -70) {
                if (cb.mineral_surplus_final <= 0) {
                    if (base.mineral_surplus_final > 0) faster_project++;
                } else if (base.mineral_surplus_final > 0) {
                    int32_t cost = cost_factor((uint8_t)cb.faction_id, 1, -1) * Facility[-cb.queue_items[0]].cost;
                    int32_t turns_here = (cost - cb.minerals_accumulated) / cb.mineral_surplus_final;
                    if ((cost - base.minerals_accumulated) / base.mineral_surplus_final < turns_here) faster_project++;
                }
            }
        }
        int32_t item = base.queue_items[0];
        if (item <= -70) project_builders[-item - 70] |= fbit;
        // BUG(original): for facilities (item < 0) the plan and triad are read from Units[item]
        // (memory before the unit table) and has_abil(item, ...) may be called; the values are only
        // used for units, so they are computed for unit items only here.
        int32_t plan = 0, triad = 0;
        if (item >= 0) {
            plan = Units[item].plan;
            triad = Chassis[Units[item].chassis_id].triad;
            if (plan == 7 && (Units[item].carry_capacity < 2 || Units[item].carry_capacity < f.unk_70 / 0x400000))
                plan = 15;
            if (triad == 0 && has_abil(item, 0x8000)) plan = 16;
            prod_all[pt(triad, plan)] += (Units[item].reactor_id >= best_reac) + 1;
            queued_all[item]++;
        }
        if (map_tile(base.x, base.y)->region != region) continue;
        if (base.state_flags & 1) unk1_flag = 1;
        if (base.queue_items[0] <= -70) projects_region++;
        if (base_fac(i, FAC_COMMAND_CENTER) || base.queue_items[0] == -FAC_COMMAND_CENTER) cmdcenter_count++;
        if (base_fac(i, FAC_BIOENHANCEMENT_CENTER) || base.queue_items[0] == -FAC_BIOENHANCEMENT_CENTER)
            bioenh_count++;
        if (base_fac(i, FAC_NAVAL_YARD) || base.queue_items[0] == -FAC_NAVAL_YARD) navalyard_count++;
        if (base_fac(i, FAC_AEROSPACE_COMPLEX) || base.queue_items[0] == -FAC_AEROSPACE_COMPLEX) aerospace_count++;
        if (base_fac(i, FAC_SKUNKWORKS) || base.queue_items[0] == -FAC_SKUNKWORKS) skunk_count++;
        if (base_fac(i, FAC_CENTAURI_PRESERVE)) preserve_count++;
        if (item >= 0) {
            queued_region[item]++;
            prod_triad_region[triad]++;
            prod_region[pt(triad, plan)] += (Units[item].reactor_id >= best_reac) + 1;
            if (item >= 64 && !(Units[item].unit_flags & 4)) prototypes++;
        }
        if (!base_fac(CurrentBaseID, FAC_COMMAND_CENTER) || base_fac(i, FAC_COMMAND_CENTER)) continue;
        // Garrison of the other bases of the region (counted when this base has a command center).
        int32_t stack = stack_fix(veh_at(base.x, base.y));
        int32_t garrison = garrison_check(i);
        int32_t defensive = defensive_check(i);
        int32_t sc11 = stack_check(stack, 11, 0x10000000, 0x10000000, faction_id);
        int32_t defenders = stack_check(stack, 2, 2, faction_id, -1);
        int32_t count;
        if (defenders < 0 || defensive < 0) {
            count = 0;
        } else {
            count = defenders <= defensive ? defenders : defensive;
            if (count && sc11) count++;
        }
        if (f.region_base_plan[region] == 2 || f.region_base_plan[region] == 8)
            count += stack_check(stack, 2, 1, faction_id, -1);
        if (f.best_armor_value < 2) count += stack_check(stack, 2, 3, -1, -1);
        else if (!count) count = stack_check(stack, 2, 3, -1, -1) != 0;
        int32_t own_home = 0;
        for (int32_t v = 0; v < VehCount; v++) {
            VEH& veh = Vehs[v];
            if (veh.faction_id != faction_id) continue;
            if (Units[veh.unit_id].plan != 2 && !(veh.state & 0x10000000)) continue;
            MAP* sq = map_tile(veh.x, veh.y);
            if (sq->region != region) continue;
            if ((sq->items & 1) && (sq->val2 & 0xF) < 8) continue;
            int32_t home = veh.home_base_id;
            if (home != i && home != CurrentBaseID) continue;
            if (home == CurrentBaseID) {
                if (++own_home > home_defenders) continue;
                home_defenders++;
            }
            count++;
        }
        if (count < garrison) {
            int32_t q = base.queue_items[0];
            if (!sc11 && garrison != defensive) {
                if (q >= 0 && (uint8_t)Units[q].plan <= 1 && ++count >= garrison) continue;
            } else {
                if (q >= 0 && Units[q].plan == 2 && ++count >= garrison) continue;
            }
            undergarrisoned++;
        }
    }
    if (cur_base().state_flags & 1) unk1_flag = 2;
    int32_t own_vehs = 0;      // [ebp-0x68]
    int32_t probes_home = 0;   // [ebp-0xE4]: plan 3 units with ability 0x40000 supported here
    int32_t free_units = 0;    // [ebp-0xE8]: own units of plans 0-3 in the region outside bases
    for (int32_t v = 0; v < VehCount; v++) {
        VEH& veh = Vehs[v];
        if (veh.faction_id != faction_id) continue;
        own_vehs++;
        int32_t u = veh.unit_id;
        int32_t plan = (uint8_t)Units[u].plan;
        int32_t triad = Chassis[Units[u].chassis_id].triad;
        if (plan == 7 && (veh_cargo(v) < 2 || veh_cargo(v) < f.unk_70 / 0x400000)) plan = 15;
        if (triad == 0 && has_abil(u, 0x8000)) plan = 16;
        if (Units[u].group_id == 21) plan = 17;
        if (veh.state & 0x10000000) plan = 2;
        int32_t idx = pt(triad, plan);
        veh_all[idx] += (Units[u].reactor_id >= best_reac) + 1;
        if (veh.home_base_id == CurrentBaseID) {
            veh_home[idx]++;
            if (Units[u].plan == 3 && has_abil(u, 0x40000) && map_tile(veh.x, veh.y)->region == region)
                probes_home++;
        }
        MAP* sq = map_tile(veh.x, veh.y);
        if (sq->region == region) {
            units_region[u]++;
            veh_region[idx] += (Units[u].reactor_id >= best_reac) + 1;
            if ((!(sq->items & 1) || (sq->val2 & 0xF) >= 8) && plan <= 3) free_units++;
        }
    }
    for (int32_t i = 0; i < 51; i++) {
        prod_all[i] /= 2;
        prod_region[i] /= 2;
        veh_all[i] /= 2;
        veh_region[i] /= 2;
    }
    int32_t naval_goal = 0;  // [ebp-0xC4]
    for (int32_t i = 0; i < 64; i++)
        if (f.region_total_bases[i] >= 3) naval_goal++;
    if (f.base_count >= 10) naval_goal++;
    if (f.base_count >= 20) naval_goal++;
    naval_goal += (sea_bases + 2) / 3;
    if (has_sp(FAC_MARITIME_CONTROL_CENTER - 70, faction_id)) naval_goal = naval_goal * 2 + 1;
    if (f.SE_police_pending + base_fac(CurrentBaseID, FAC_BROOD_PIT) * 2 <= -3) naval_goal = (naval_goal + 1) / 2;
    if (f.SE_police_pending + base_fac(CurrentBaseID, FAC_BROOD_PIT) * 2 <= -5) naval_goal = naval_goal / 2;
    int32_t enemy_near = 0;  // [ebp-0x94]
    int32_t farm_tiles = 0;  // [ebp-0x84]
    for (int32_t i = 0; i < 21; i++) {
        int32_t x = map_wrap_x(bx + TableOffsetX[i]);
        int32_t y = by + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        if (map_tile(x, y)->items & 0x200000) farm_tiles += (cur_base().worked_tiles & (1 << i)) ? 2 : 1;
        int32_t v = veh_at(x, y);
        if (v >= 0) {
            int32_t owner = Vehs[v].faction_id;
            if (owner != faction_id && (f.diplo_status[owner] & 0x30)) enemy_near += stack_check(v, 1, -1, -1, -1);
        }
    }
    // Defenders of this base and the resulting need for defense (0-3).
    int32_t stack = stack_fix(veh_at(cur_base().x, cur_base().y));  // [ebp-0x4C]
    const int32_t garrison = garrison_check(CurrentBaseID);                 // [ebp-0xF0]
    const int32_t defensive = defensive_check(CurrentBaseID);               // [ebp-0xC8]
    const int32_t sc11 = stack_check(stack, 11, 0x10000000, 0x10000000, faction_id);  // [ebp-0x5C]
    int32_t defense;  // [ebp-0x88]
    {
        int32_t defenders = stack_check(stack, 2, 2, faction_id, -1);
        if (defenders < 0 || defensive < 0) {
            defense = 0;
        } else {
            defense = defenders <= defensive ? defenders : defensive;
            if (defense && sc11) defense++;
        }
        if ((cur_base().state_flags & 0x4000000) && f.region_base_plan[region] > 2) defense++;
        if (f.region_base_plan[region] == 2 || f.region_base_plan[region] == 8)
            defense += stack_check(stack, 2, 1, faction_id, -1);
        if (f.best_armor_value < 2) defense += stack_check(stack, 2, 3, -1, -1);
        else if (!defense) defense = stack_check(stack, 2, 3, -1, -1) != 0;
        stack_check(stack, 2, 7, faction_id, -1);
        stack_check(stack, 2, 6, faction_id, -1);
        if (fbit & Units[2].obsolete_factions) {
            int32_t n = stack_check(stack, 0, 2, faction_id, -1);  // unit 2 (obsolete) defenders
            if (n < 0) n = 0;
            else if (n > 1) n = 1;
            defense -= n;
        }
        if (f.region_base_plan[region] > 2 && (cur_base().state_flags & 0x8000000)) defense++;
    }
    // [ebp-0x9C]: tie-break of the primary choice (set to 999999 before the unit loop; the
    // original's slot holds earlier temporaries when the loop is skipped).
    int32_t tie = (int32_t)(FactionStatus[0] & fbit);
    int32_t need;  // [ebp-0x28]
    if (defense < 1) need = 3;
    else if (defense < garrison - 1) need = 2;
    else need = defense < garrison;
    bool normal_need = true;
    if (is_human && cur_base().minerals_accumulated > Rules.retool_exemption && !(cur_base().state_flags & 8)) {
        if (!gov_explore && !gov_discover && !gov_build) {
            if (gov_conquer) {
                if (need > 1) need = 1;
                normal_need = false;
            }
        } else {
            if (gov_conquer || gov_explore) {
                if (need > 1) need = 1;
            } else {
                need = 0;
            }
            normal_need = false;
        }
    }
    if (normal_need) {
        bool check = false;
        if (plan_region == 8 || (plan_region == 7 && (f.base_count < 4 || !seen_by_enemy))) check = true;
        else if (is_human && !gov_conquer) check = true;
        if (check) {
            int32_t safe = 1;  // [ebp-0x34]: no (possibly) hostile units or bases in the region
            for (int32_t i = 1; i < 8; i++) {
                if (faction_id == i) continue;
                if (!Factions[i].region_total_combat_units[region] && !Factions[i].region_total_bases[region]) continue;
                uint32_t st = f.diplo_status[i];
                if (st & 0x30) {
                    safe = 0;
                } else if (human(i) && (Factions[i].diplo_status[faction_id] & 8)) {
                    if (!(st & 2) || Factions[i].integrity_blemishes) safe = 0;
                }
            }
            if (need && safe) {
                bool lower = false;
                if (is_human && !gov_conquer) lower = true;
                else if (cur_base().pop_size < 3 && (ai_growth || f.base_count < 3) &&
                         !(cur_base().state_flags & 0x1000000))
                    lower = true;
                if (lower) {
                    need--;
                    tie = cur_base().x;
                    int32_t cx = cur_base().x, cy = cur_base().y;
                    for (int32_t i = 0; i < 121; i++) {
                        int32_t x = map_wrap_x(cx + TableOffsetX[i]);
                        int32_t y = cy + TableOffsetY[i];
                        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
                        MAP* sq = map_tile(x, y);
                        if (sq->region != region) continue;
                        if (!(f.player_flags & 0x200) && !(sq->visibility & (uint8_t)fbit)) continue;
                        if (!(sq->items & 3)) continue;
                        int32_t owner = sq->val2 & 0xF;
                        if (owner >= 8 || owner == faction_id) continue;
                        if (owner == 0 || (f.diplo_status[owner] & 0x30)) {
                            need++;
                            break;
                        }
                    }
                }
            }
        }
    }
    // Frontier: unexplored or foreign (unfriendly) tiles in the base radius.
    int32_t frontier = 0;  // [ebp-0xC0]
    for (int32_t i = 0; i < 49; i++) {
        int32_t x = map_wrap_x(cur_base().x + TableOffsetX[i]);
        int32_t y = cur_base().y + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        MAP* sq = map_tile(x, y);
        if (sq->region != region) continue;
        if (!(f.player_flags & 0x200) && !(sq->visibility & (uint8_t)fbit)) {
            frontier = 1;
            break;
        }
        if (sq->items & 3) {
            int32_t owner = sq->val2 & 0xF;
            if (owner < 8 && owner > 0 && owner != faction_id) {
                frontier = 1;
                break;
            }
        }
        int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
        if (owner <= 0 || owner == faction_id) continue;
        if (human(owner) ? !(f.diplo_status[owner] & 1) : !(f.diplo_status[owner] & 2)) {
            frontier = 1;
            break;
        }
    }

    // ---- units ----
    // Best choices: [0] the primary (score and tie-break), [1] combat/support units, [2] others
    // (when flag78 marks a unit as an alternative only). Lower scores are better.
    int32_t choice[3], score[3];  // [ebp-0x130], [ebp-0x114]
    for (int32_t i = 0; i < 3; i++) {
        score[i] = 999999;
        choice[i] = i ? 99999 : -69;
    }
    int32_t prio = 0;  // [ebp-0x18]: multiplier of the unit's score (higher: less wanted)
    if (f.total_combat_units < 512) {
        tie = 999999;
        for (int32_t u = 0; u < 512; u++) {
            if (!veh_avail(u, faction_id, CurrentBaseID)) continue;
            UNIT& unit = Units[u];
            const int32_t triad = Chassis[unit.chassis_id].triad;
            int32_t plan = (uint8_t)unit.plan;  // [ebp-0x2C] (16: terraforming ability on land)
            if (triad == 0 && has_abil(u, 0x8000)) plan = 16;
            int32_t flag78 = 0;  // [ebp-0x78]: only an alternative choice
            int32_t category = (uint8_t)unit.plan < 8;  // [ebp-0x90]
            int32_t w = (unit.cost + 2) << 8;           // [ebp-0x34]
            int32_t base_rank = 0;                      // edi before [ebp-0x18]
            bool skip = false;
            switch (triad) {
            case 2: {
                if (plan_region != 7 && !is_sea &&
                    f.region_total_combat_units[region] < f.region_total_bases[region] * 2 + 2) {
                    skip = true;
                    break;
                }
                int32_t a = prod_all[pt(2, plan)], b = veh_all[pt(2, plan)];
                int32_t n = (a + b != 0) + 1;
                if (plan_region != 7 && plan != 5 && !is_sea) {
                    if (f.target_x >= 0) n++;
                    if (plan_region == 2) n++;
                    if (f.region_flags[region] & 0x400) n++;
                    n += prod_triad_region[2];
                }
                int32_t v = n * w;
                w = v;
                if (base_fac(CurrentBaseID, FAC_AEROSPACE_COMPLEX)) {
                    v = (aerospace_count + 2) * v / (aerospace_count + 3);
                    w = v;
                }
                if (!is_human && (f.player_flags & 0x10000000)) w = v / 2;
                n -= 2;
                int32_t c = n < 0 ? 0 : (n > 99 ? 99 : n);
                base_rank = ((plan != 4) + 1) * a + c + b;
                int32_t s = range(8 - regions_with_bases, 4, 8) * w / 8;
                int32_t cost = veh_cost(u, CurrentBaseID, 0);
                cost = cost * cost_factor(faction_id, 1, -1) * s;
                w = range(cost / (range(cur_base().mineral_surplus, 1, 999) * 15), s, s * 10);
                if (is_sea) w /= 2;
                prio = base_rank + 1;
                break;
            }
            case 1: {
                if (plan_region != 7 && !is_sea &&
                    f.region_total_combat_units[region] < f.region_total_bases[region] * 2 + 2) {
                    skip = true;
                    break;
                }
                if (!is_coast(cur_base().x, cur_base().y, 0)) {
                    skip = true;
                    break;
                }
                int32_t coast = base_coast(CurrentBaseID);
                if ((coast & 0x3F) == 0x3F || (coast & 0x3F) == 0) {
                    skip = true;
                    break;
                }
                if (sea_coasts(coast) < 2 && !is_sea && (plan == 7 || Continents[coast].tile_count < 48)) {
                    skip = true;
                    break;
                }
                prio = veh_all[pt(1, plan)] + prod_all[pt(1, plan)] * 2;
                if (plan_region != 7 && !is_sea) {
                    if (plan_region == 2) prio++;
                    if (f.region_flags[region] & 0x400) prio++;
                    prio += prod_triad_region[1];
                }
                if (!is_human && (f.player_flags & 0x8000000)) w /= 2;
                if (!is_human && (m.rule_flags & 0x100)) {
                    w /= 2;
                    regions_with_bases++;  // BUG(original): persists for the following units
                }
                int32_t s = range(8 - regions_with_bases, 4, 8) * w / 8;
                w = s;
                int32_t r = range(cur_base().mineral_surplus, 1, 999);
                int32_t cost = veh_cost(u, CurrentBaseID, 0) * cost_factor(faction_id, 1, -1);
                cost = cost * s / (r * 15);
                if (cost >= s && s * 10 >= s) w = cost <= s * 10 ? cost : s * 10;
                prio++;
                break;
            }
            case 0:
                if (Weapon[unit.weapon_id].mode <= 2) {
                    int32_t lim = 99;
                    if ((uint8_t)unit.plan <= 1) {
                        if (f.unk_71) lim = 1;
                        else if (plan_region == 2) lim = (f.region_flags[region] & 0x400) ? 1 : 2;
                        else if (f.region_flags[region] & 0x600) lim = 2;
                    }
                    int32_t extra = 0;
                    if (plan_region != 2 && !(f.region_flags[region] & 0x400)) extra = prod_region[pt(0, plan)];
                    int32_t cnt = units_region[u] + prod_all[pt(0, plan)] + veh_region[pt(0, plan)];
                    if (cnt < 0 || lim < 0) cnt = 0;
                    else if (cnt > lim) cnt = lim;
                    base_rank = (queued_region[u] + cnt + extra + 1) / 2;
                    if (base_fac(CurrentBaseID, FAC_COMMAND_CENTER))
                        w = (cmdcenter_count + 2) * w / (cmdcenter_count + 3);
                    if (!is_human && (f.player_flags & 0x4000000)) w /= 2;
                } else if (plan == 8 && plan_region == 8 && !(cur_base().state_flags & 0x2000000) && DiffLevel >= 3) {
                    base_rank = 0;
                } else {
                    base_rank = veh_region[pt(0, plan)];
                    if (!(plan == 8 && plan_region == 8 && !(cur_base().state_flags & 0x1000000) && DiffLevel >= 3)) {
                        base_rank += queued_region[u] + prod_region[pt(0, plan)];
                        if (plan == 8) base_rank += prod_all[pt(0, 8)];
                    }
                }
                prio = base_rank + 1;
                break;
            default:
                prio++;  // (no such triad; the original reuses the previous unit's value)
                break;
            }
            if (skip) continue;
            switch ((uint8_t)unit.plan) {
            case 0: {  // offense
                int32_t mult = (plan_region <= 2 || ((plan_region == 7 || is_sea) && vendetta_plan7)) ? 32 : 64;
                w *= mult;
                if (defense && !sc11 && f.region_total_offensive_units[region] < guard_check(faction_id, region))
                    w /= 2;
                if (target_flags & 2) w /= 2;
                else if (target_flags & 1) w = w * 3 / 4;
                if (is_sea && triad == 0) w *= 2;
                if (gov_conquer && (triad == 2 || !is_sea)) {
                    w /= 2;
                    if (CurrentTurn < 50) w /= 2;
                }
                if (triad == 2 && plan_region != 2) {
                    int32_t mx = f.unk_66 > f.unk_67 ? f.unk_66 : f.unk_67;
                    int32_t s = prod_all[pt(2, plan)] + veh_all[pt(2, plan)];
                    if (mx > s) w /= 2;
                    if (mx * 2 > s) w /= 2;
                }
                if (!is_human && Chassis[unit.chassis_id].missile && (f.player_flags_ext & 0x10)) w /= 3;
                break;
            }
            case 1: {  // combat
                int32_t mult = (plan_region <= 2 || ((plan_region == 7 || is_sea) && vendetta_plan7)) ? 24 : 32;
                w *= mult;
                if (defense && !sc11 && f.region_total_offensive_units[region] < guard_check(faction_id, region))
                    w /= 2;
                if (is_sea && triad == 0) w *= 2;
                if (gov_conquer && !is_sea) {
                    w /= 2;
                    if (CurrentTurn > 50) w /= 2;
                }
                break;
            }
            case 2:  // defense
                w <<= 4;
                if (undergarrisoned && !need && has_fac_built(FAC_COMMAND_CENTER)) w /= 2;
                if (defense > 1 && !sc11 && f.region_total_offensive_units[region] < guard_check(faction_id, region))
                    w *= 2;
                if (has_abil(u, 0x40000) && !stack_check(stack, 6, 0x40000, -1, -1) && f.clean_minerals_modifier >= 4) {
                    w = w * 2 / 3;
                    if (f.SE_morale_pending <= 0) w = w * 2 / 3;
                }
                if (has_abil(u, 0x100)) {
                    if (cur_base().state_flags & 0x100000) {
                        w /= 2;
                    } else {
                        if (stack_check(veh_at(cur_base().x, cur_base().y), 6, 0x100, -1, -1)) w *= 3;
                        if (flag_100000_count) w /= 2;
                        else if (!f.unk_66 && !f.unk_68) w <<= 2;
                    }
                }
                if (gov_conquer || gov_discover || gov_explore) {
                    if (CurrentTurn > 25) w *= 2;
                    if (CurrentTurn > 75) w *= 2;
                }
                break;
            case 3: {  // reconnaissance
                int32_t probe = has_abil(u, 0x40000);  // [ebp-0x48]
                if (!need && !(!probes_home && probe)) {
                    int32_t lim = -1;  // -1: by the continent size
                    if (gov_explore || plan_region) {
                        if (plan_region == 2) flag78 = 1;
                        if (plan_region == 7) lim = 1;
                    } else if (units_region[u] || queued_region[u]) {
                        flag78 = 1;
                    }
                    if (lim < 0) lim = (Continents[region].tile_count + 199) / 200;
                    if (units_region[u] + queued_region[u] * 2 > lim && plan_region != 8) flag78 = 1;
                    int32_t r = range(CurrentTurn / 50, 0, f.base_count - 1);
                    if (units_region[u] > f.base_count - r) flag78 = 1;
                }
                bool done = false;
                if (plan_region == 8 || !free_units) {
                    if (free_units < colony_sites + gov_explore * 2 &&
                        (gov_explore || (!gov_conquer && !gov_build && !gov_discover)) &&
                        free_units < (f.base_count + 1) / ((gov_explore == 0) + 1)) {
                        w <<= 4;
                        done = true;
                    }
                }
                if (!done) {
                    if (probe && !probes_home) {
                        w <<= 5;
                        if (cur_base().eco_damage) {
                            if (plan_region != 2) {
                                w /= 2;
                                if (plan_region > 2) w /= 2;
                            }
                        } else if (plan_region > 2) {
                            w /= 2;
                        }
                    } else {
                        w *= (!need && !gov_explore && f.best_armor_value >= 2) ? 0x80 : 0x20;
                    }
                }
                if (gov_conquer && f.best_armor_value < 2 && !is_sea) w /= 2;
                break;
            }
            case 4: {  // air superiority
                w <<= 6;
                int32_t idx = pt(triad, plan);
                if (f.unk_106 * 2 >= f.unk_105) {
                    if (prod_all[idx] + veh_all[idx] > f.unk_66 * 4 / 3) w *= 2;
                } else if (f.unk_106 < f.unk_105 / 2) {
                    w /= 2;
                    if (f.unk_106 < f.unk_105 / 4) w /= 2;
                }
                int32_t c = prod_all[idx] + veh_all[idx];
                int32_t a = veh_all[idx];
                if (f.unk_66 > c) {
                    if (!a) w /= 2;
                    w /= 2;
                    if (plan_region != 2) {
                        if (!a) w /= 2;
                        if (f.unk_66 >= c * 2) w /= 2;
                        w /= 2;
                    }
                }
                if (!(cur_base().state_flags & 0x100000) && flag_100000_count) w /= 2;
                break;
            }
            case 5: {  // planet buster
                w <<= 6;
                if (gov_conquer) {
                    w /= 2;
                    if (gov_build) w /= 2;
                }
                int32_t idx = pt(triad, plan);
                if (prod_region[idx] >= 2) {
                    skip = true;
                    break;
                }
                if (!prod_all[idx] && veh_all[idx] < 2) w /= 2;
                int32_t max_odp = 4, marked = 0;
                for (int32_t i = 1; i < 8; i++) {
                    if (i == faction_id) continue;
                    if (has_treaty(faction_id, i, 1) && !human(i)) continue;
                    if (Factions[i].satellites_ODP >= max_odp) max_odp = Factions[i].satellites_ODP;
                    if (!marked && (has_treaty(faction_id, i, 0x30) || Factions[i].major_atrocities) &&
                        f.planet_busters < Factions[i].satellites_ODP + 1) {
                        marked = 1;
                        w /= 2;
                    }
                }
                if (f.planet_busters > max_odp) {
                    skip = true;
                    break;
                }
                if (!is_human) {
                    if (f.player_flags_ext & 0x10) w /= 2;
                    if (f.player_flags & 0x1000000) w /= 2;
                }
                break;
            }
            case 6: {  // naval superiority
                if (!naval_base(CurrentBaseID)) {
                    skip = true;
                    break;
                }
                w = w * (threat != 1 ? 128 : 256) * 56 / MapAreaSqRoot;
                if (plan_region == 7) w = w * 2 / 3;
                if (has_fac_built(FAC_NAVAL_YARD)) w = (navalyard_count + 2) * w / (navalyard_count + 3);
                if (unit.group_id == 21) {
                    if (is_sea) w /= 2;
                    int32_t s = prod_all[pt(1, plan)] + veh_all[pt(1, plan)];
                    int32_t t = veh_all[pt(1, 7)];
                    if (s >= t) w *= 2;
                    if (t > s * 3 / 2) w /= 2;
                    break;
                }
                if (at_goal(faction_id, 6, cur_base().x, cur_base().y)) w /= 2;
                int32_t v = range((veh_all[pt(triad, 6)] - (f.unk_64 & 0x7FF)) * 2 + 8 - f.unk_65 / 2, 0, 8);
                w = (v + 8) * w / 16;
                if (veh_all[pt(1, 6)] + prod_all[pt(1, 6)] * 2 < naval_goal) {
                    w /= 2;
                    if (plan_region == 7) w /= 2;
                }
                if (prod_all[pt(1, 6)] + veh_all[pt(1, 6)] < prod_all[pt(1, 7)] + veh_all[pt(1, 7)]) {
                    w /= 2;
                    if (plan_region == 7) w /= 2;
                }
                if (gov_explore) w = w * 3 / 4;
                if (f.player_flags & 0x80000) w /= 2;
                if (m.rule_flags & 0x100) w /= 2;
                if (is_sea) w /= 2;
                if (gov_conquer && is_sea) w /= 2;
                break;
            }
            case 7: {  // transport
                if (triad == 1 && !transport_base(CurrentBaseID)) {
                    skip = true;
                    break;
                }
                w *= plan_region == 7 ? 16 : (plan_region != 8 ? 128 : 64);
                if (gov_explore || (gov_conquer && plan_region == 7)) {
                    w /= 2;
                    if (!prod_all[pt(triad, 7)] && !f.units_active[u] && plan == 7) w /= 4;
                }
                if (is_sea && need < 3) {
                    w /= 4;
                    if (!veh_all[pt(triad, 7)]) w /= 4;
                }
                if (has_abil(u, 0x80000)) w = w * 3 / 4;
                int32_t lim = f.base_count / 2;
                if (lim < 2) lim = 2;
                if (veh_all[pt(triad, 7)] >= lim) w *= 2;
                if (plan == 7) {
                    if (prod_all[pt(triad, 6)] + veh_all[pt(triad, 6)] + veh_all[pt(triad, 7)] +
                                prod_all[pt(triad, 7)] < 2 &&
                        CurrentTurn < 100)
                        w /= 2;
                    if (!veh_all[pt(triad, 7)] && !prod_all[pt(triad, 7)] &&
                        (veh_all[pt(triad, 8)] || prod_all[pt(triad, 8)]) && plan_region > 2)
                        w /= 2;
                }
                if (cur_base().ai_plan_status) w *= 2;
                if (plan_region == 7) {
                    if (need >= 2 || plan != 7) break;
                    int32_t cnt = 0;
                    for (int32_t i = 1; i < 128; i++)
                        if (f.region_total_bases[i]) cnt++;
                    int32_t want = (f.region_total_bases[region] + 3) / 4;
                    if (cnt > want) want = cnt;
                    if (veh_all[pt(triad, 7)] - (f.units_active[13] != 0) >= want) break;
                    if (prod_all[pt(triad, 7)]) break;
                    w /= 4;
                } else {
                    if (threat == 1) w *= 2;
                    if (is_human) break;
                    if (units_region[u] > 1 || plan_region == 2 || (f.region_flags[region] & 0x400)) flag78 = 1;
                }
                break;
            }
            case 8: {  // colony
                BASE& cb = cur_base();
                if (!(is_human && ((uint32_t)cb.governor_flags & 0xDFBE00) == 0x20000)) {
                    if (!(is_human && (cb.governor_flags & GOV_ACTIVE))) {
                        if ((is_human || need >= 3) && cb.pop_size < 2) {
                            skip = true;
                            break;
                        }
                        if (prod_region[pt(triad, 8)] + veh_region[pt(triad, 8)] > 1 && plan_region != 8) {
                            skip = true;
                            break;
                        }
                    }
                    if (cb.nutrient_surplus < 1 && (veh_all[pt(triad, 8)] || prod_region[pt(triad, 8)])) {
                        skip = true;
                        break;
                    }
                    if (triad) {
                        if (!is_sea && !(!is_human && (f.player_flags_ext & 4)) &&
                            (f.units_active[u] || queued_all[u])) {
                            skip = true;
                            break;
                        }
                    } else {
                        if (is_sea && (sea_bases < f.base_count || sea_bases < 3)) {
                            skip = true;
                            break;
                        }
                        if (plan_region == 2) {
                            skip = true;
                            break;
                        }
                    }
                }
                int32_t expanding = cb.state_flags & 0x1000000;
                if (expanding) {
                    int32_t v = f.base_count - CurrentTurn / (is_human ? 40 : 20) - ai_power - ai_growth - 1;
                    prio += v < 0 ? 0 : v;
                }
                w *= 5;
                if (cb.pop_size < 2 && (cb.nutrient_surplus < cb.mineral_surplus || cb.nutrient_surplus < 1)) {
                    w *= 2;
                } else if (plan_region > 2 && !expanding) {
                    bool divide = true;
                    if (is_human) {
                        if (plan_region == 7 && f.base_count >= BaseCount / 5 && f.base_count >= 10) {
                            divide = false;
                        } else if (cb.governor_flags & GOV_ACTIVE) {
                            divide = false;
                            if (gov_explore) w /= range(DiffLevel + 1, 2, 99);
                            else if (gov_build || gov_conquer) w /= 2;
                        }
                    }
                    if (divide) w /= DiffLevel + 1;
                }
                if (f.player_flags_ext & 1) w /= 2;
                uint16_t rf = f.region_flags[region];
                if (rf & 0x400) {
                    w *= 2;
                    flag78 = 1;
                }
                if (GameRules & 0x20000000) {
                    w /= 2;
                    if (plan_region == 8) w /= 2;
                }
                bool late = false;  // skips the base count bonuses (jump to 0x4FBA68)
                if (triad == 0) {
                    if (is_human && CurrentTurn > 100) {
                        skip = true;
                        break;
                    }
                    if (plan_region == 8 && (veh_region[pt(0, 3)] || prod_region[pt(0, 3)] || !f.region_territory_pods[region]) &&
                        (veh_region[pt(0, 9)] || prod_region[pt(0, 9)] || !has_tech(Units[1].preq_tech, faction_id)) &&
                        !(rf & 0x20))
                        w /= 2;
                    int32_t idx = pt(0, plan);
                    if (!veh_all[idx] && !prod_all[idx] && cb.nutrient_surplus >= 0 && plan_region != 2 &&
                        (!(rf & 0x401) || plan_region == 7))
                        w /= 2;
                    if (need < 3 && !veh_region[idx] && !prod_region[idx]) {
                        bool half = false;
                        if (plan_region == 8) {
                            half = true;
                        } else if (plan_region == 7) {
                            if ((cb.state_flags & 0x1000000) || MapOceanCoverage >= 1) late = true;
                            else half = true;
                        }
                        if (half) {
                            w /= 2;
                            if (f.base_count == 1) w /= 4;
                        }
                    }
                } else {
                    if (is_sea) {
                        w /= 2;
                        if (f.base_count < 4) w /= 2;
                        if (f.base_count < 8) w /= 2;
                        if (m.rule_flags & 0x100) w /= 2;
                    }
                    if (MapOceanCoverage > 1) {
                        w /= 2;
                        int32_t idx = pt(triad, plan);
                        if (need < 3 && !prod_region[idx] && !veh_region[idx] && !(cb.state_flags & 0x1000000)) w /= 2;
                    }
                    if (cb.nutrient_surplus >= Rules.nutrient_intake_req_citizen && (plan_region == 7 || is_sea)) w /= 2;
                    if (!is_sea && plan_region != 7 && (is_human || !(f.player_flags_ext & 4))) w *= 2;
                    if (!is_human && (f.player_flags_ext & 4)) w /= 2;
                }
                if (late || !(plan_region == 8 && f.region_total_bases[region] <= 1)) {
                    int32_t bc = f.base_count;
                    if (bc >= 3 && mineral_output == f.best_mineral_output) w *= 2;
                    if (bc >= 4 && cb.pop_size > max_pop_other) w *= 2;
                    if (is_human && bc >= (f.diff_level + 1) * CurrentTurn / 40) w *= 3;
                }
                if (!std::strcmp(m.filename, "BELIEVE") && !is_human && !(rf & 1)) w /= 2;
                if (gov_explore) w /= 2;
                break;
            }
            case 9: {  // terraforming
                w *= threat ? 6 : 4;
                if (gov_build) w /= 2;
                else if (gov_conquer) w *= 2;
                if (f.player_flags_ext & 2) w /= 2;
                if (triad == 1) {
                    if ((queued_region[u] || veh_all[pt(1, 9)] > sea_bases / 2) && !is_sea) {
                        skip = true;
                        break;
                    }
                    if (cur_base().state_flags & 0x80000) {
                        skip = true;
                        break;
                    }
                    if (!is_sea && (plan_region <= 2 || (plan_region != 7 && veh_all[pt(1, 9)]))) {
                        skip = true;
                        break;
                    }
                    if (vendetta_plan7 && !is_sea && veh_all[pt(1, 9)]) {
                        skip = true;
                        break;
                    }
                    if (!need) {
                        if (is_sea && units_region[u] < sea_bases) w /= 2;
                        if (units_region[u] < sea_bases / 2) w /= 2;
                        if (!veh_all[pt(1, 9)] && !vendetta_plan7) w /= 2;
                    }
                    if (veh_region[pt(1, 9)] >= sea_bases) {
                        skip = true;
                        break;
                    }
                    if (is_sea) w /= 2;
                    break;
                }
                if (is_sea) {
                    skip = true;
                    break;
                }
                int32_t formers = veh_region[pt(triad, 9)];  // [ebp-0x44]
                bool low = false;
                if (!formers && !queued_region[u] && CurrentTurn < 25 && !gov_conquer && !gov_explore) {
                    need = 0;  // BUG(original): persists for the following units
                    low = true;
                } else if (need < 3) {
                    low = true;
                }
                bool skip_region_check = false;
                if (low && !formers && !queued_region[u]) {
                    if (!is_human && (plan_region == 8 || CurrentTurn < 40)) w /= 16;
                    if (plan_region == 7) {
                        w /= 2;
                        skip_region_check = true;
                    }
                }
                if (!skip_region_check && plan_region == 2) {
                    skip = true;
                    break;
                }
                uint16_t rf = f.region_flags[region];
                if (rf & 0x400) w <<= 2;
                int32_t rtb = f.region_total_bases[region];
                if (!need && !gov_conquer) {
                    int32_t st = cur_base().state_flags;
                    if (!(st & 0x20) && veh_region[pt(triad, plan)] < rtb) {
                        if (plan_region != 2 && (ai_wealth || ai_tech || (!frontier && plan_region > 2))) w /= 2;
                        if (plan_region == 7 || (st & 0x2000000)) {
                            if (!ai_wealth && !ai_tech && frontier) w /= 2;
                            else w /= 4;
                        }
                    }
                    if (plan_region > 2) {
                        int32_t c = veh_region[pt(triad, plan)];
                        if (c <= rtb) w /= 2;
                        if (c <= (rtb + 1) / 2 && !((rf & 0x20) && frontier) && (ai_wealth || ai_tech || !frontier))
                            w /= 2;
                    }
                }
                if (cur_base().state_flags & 0x20) w *= 3;
                if (formers >= rtb) {
                    skip = true;
                    break;
                }
                if (formers >= rtb * 2 / 3 && (plan_region == 0 || plan_region == 2)) w = w * 3 / 2;
                if (!MapCloudCover) w /= 2;
                break;
            }
            case 10: {  // supply
                if (is_human || plan_region <= 2) {
                    skip = true;
                    break;
                }
                int32_t left = 3;
                for (int32_t v = 0; v < VehCount && !skip; v++) {
                    VEH& veh = Vehs[v];
                    if (veh.faction_id != faction_id || Units[veh.unit_id].plan != 10) continue;
                    if (veh.home_base_id == CurrentBaseID) w *= 2;
                    if (veh.order != 3) {
                        skip = true;
                        break;
                    }
                    if (left) left--;
                    w = w * 3 / 2;
                }
                if (skip) break;
                if (VehCount > 0 && !left) {
                    skip = true;
                    break;
                }
                if (!building_project && plan_region == 7 && f.AI_tech) w /= 2;
                w = (w * 3 << 8) / cur_base().pop_size;
                break;
            }
            case 11: {  // probe
                int32_t idx = pt(triad, plan);
                if (veh_region[idx] >= f.region_total_bases[region] ||
                    (is_human && prod_all[idx] + veh_all[idx] > 1)) {
                    skip = true;
                    break;
                }
                int32_t peace = 0;  // [ebp-0x54]
                if (need < 2 && DiffLevel > 2 && psych_need < 2) {
                    if (!std::strcmp(m.filename, "PEACE")) peace = 1;
                    if (at_climax(faction_id)) peace++;
                }
                if (f.player_flags_ext & 8) peace += 2;
                if (prod_region[idx] > peace) {
                    skip = true;
                    break;
                }
                int32_t target = -1, stolen = 0, mind = 0, contacts = 0;
                for (int32_t i = 1; i < 8; i++) {
                    if (i == faction_id) continue;
                    mind += f.diplo_mind_control[i];
                    stolen += f.diplo_stolen_techs[i];
                    if (!Factions[i].region_total_bases[region]) continue;
                    if (has_project(FAC_HUNTER_SEEKER_ALGORITHM - 70, i)) continue;
                    if (!has_treaty(faction_id, i, 8)) continue;
                    contacts++;
                    if (has_treaty(faction_id, i, 7)) {
                        if (!human(i) && !is_human) continue;
                        if (!peace) {
                            if (has_treaty(faction_id, i, 0x1000000)) {
                                if (Factions[i].ranking < f.ranking) continue;
                                int32_t d = (Factions[i].tech_ranking - f.tech_ranking) / 2;
                                if (d < (Factions[i].ranking != 7 ? 3 : 6)) continue;
                            } else if (Factions[i].ranking < f.ranking && Factions[i].tech_ranking < f.tech_ranking) {
                                continue;
                            }
                        }
                    }
                    target = i;
                    if (human(i) && !MultiplayerActive) break;
                }
                if (target < 0) {
                    if (((plan_region == 7 && DiffLevel > 1) || gov_discover) && !veh_region[idx])
                        target = cs.FactionRankings[7];
                    if (target < 0) {
                        skip = true;
                        break;
                    }
                }
                if (target == faction_id) {
                    skip = true;
                    break;
                }
                Faction& t = Factions[target];
                int32_t credit_ratio = is_human ? 0 : f.energy_credits / range(t.energy_credits, 100, 999999);
                int32_t mil_ratio =
                    is_human ? 0 : t.mil_strength_1 / range(f.mil_strength_1, f.base_count + 1, 999999);
                int32_t tdiff = (GameRules & 0x40) ? 5 : t.diff_level;
                int32_t k = is_human ? 4 : 2;
                int32_t bits = bit_count(has_treaty(faction_id, target, 7));
                int32_t v = f.tech_ranking / k - t.tech_ranking / k + t.SE_probe_pending - tdiff - mil_ratio -
                            credit_ratio - peace + veh_region[idx] + bits + 13;
                w = range(v, 1, 16) * w * 3 / (ai_tech - ai_power + 3);
                if (!is_human) {
                    auto behind = [&](int32_t tech) { return has_tech(tech, target) && !has_tech(tech, faction_id); };
                    if (behind(Chassis[5].preq_tech)) w /= 2;
                    if (behind(Chassis[8].preq_tech)) w /= 2;
                    if (behind(Weapon[16].preq_tech)) w /= 2;
                    if (behind(Facility[102].preq_tech)) w /= 2;
                    for (int32_t r = 1; r <= 3; r++)
                        if (behind(Reactor[r].preq_tech)) w /= 2;
                    if (t.best_weapon_value > f.best_weapon_value || t.best_armor_value > f.best_armor_value) w /= 2;
                }
                if (plan_region != 7 && (f.region_flags[region] & 0x20) && contacts) {
                    if (gov_discover && !veh_all[idx] && !prod_all[idx]) w /= 2;
                } else {
                    if (!has_tech(Chassis[3].preq_tech, faction_id) || (veh_region[idx] && mind + stolen < 3)) {
                        skip = true;
                        break;
                    }
                    if (is_human) w *= 2;
                    if (prod_all[idx] + veh_all[idx]) w *= 2;
                }
                if (!is_human && (f.player_flags_ext & 8)) w /= 4;
                if (threat == 1 && ai_fight > 0) prio *= 2;
                break;
            }
            case 13:  // tectonic payload
            case 14: {  // fungal payload
                w <<= 6;
                if ((uint8_t)unit.plan == 14 && f.SE_planet_pending > 0) w /= 2;
                if (prod_region[pt(triad, plan)] >= 2) {
                    skip = true;
                    break;
                }
                if (is_human) break;
                if ((uint8_t)unit.plan == 13 ? (ai_tech && !ai_power) : (ai_growth && !ai_power)) w /= 2;
                if (f.player_flags_ext & 0x10) w /= 2;
                break;
            }
            default:
                break;
            }
            if (skip) continue;
            // Prototypes: compared with the faction's best active designs.
            int32_t better = 0;  // [ebp-0x80]
            if (u >= 64 && !(unit.unit_flags & 4)) {
                int32_t speed_best[3] = {0, 0, 0};
                int32_t best_weap = 0, best_arm = 0;
                uint32_t abil_any = 0;
                for (int32_t k = 0; k < 128; k++) {
                    int32_t id = k < 64 ? k : faction_id * 64 + k - 64;
                    if (id == u) continue;
                    uint16_t fl = Units[id].unit_flags;
                    if (!(fl & 1) || (id >= 64 && !(fl & 4))) continue;
                    if (best_weap <= weap_val(id, faction_id)) best_weap = weap_val(id, faction_id);
                    // BUG(original): arm_val takes an armor id but is passed the unit id.
                    if (best_arm <= arm_val(id, faction_id)) best_arm = arm_val(id, faction_id);
                    abil_any |= Units[id].ability_flags;
                    int32_t t = Chassis[Units[id].chassis_id].triad;
                    int32_t sp = speed_best[t];
                    speed_best[t] = sp > speed_proto(id) ? sp : speed_proto(id);
                }
                (void)abil_any;
                int32_t wv = weap_val(u, faction_id);
                int32_t av = arm_val(u, faction_id);  // BUG(original): unit id as armor id
                int32_t sp = speed_proto(u);
                if (plan == 2) {
                    if (Armor[unit.armor_id].defense_value != 1 && av > best_arm &&
                        (av > best_arm + 1 || plan_region == 7)) {
                        w = w * 2 / 3;
                        better = 1;
                    }
                } else if (plan <= 1) {
                    int32_t add = (plan_region == 7 && seen_by_enemy) ? 0 : 1;
                    if (wv > best_weap + add) better = 1;
                }
                if (sp > speed_best[triad]) better = 1;
                if (!has_skunk) {
                    if (!(plan == 5 && !f.planet_busters)) {
                        if (!better)
                            prio += range((plan_region > 2) - ((gov_conquer || gov_discover) ? 1 : 0) + ai_growth, 0, 2);
                        if (plan_region == 8 || (plan_region == 7 && !(cur_base().state_flags & 0x1000000))) {
                            if (plan == 8) {
                                w /= 2;
                            } else {
                                if (!gov_conquer) {
                                    prio++;
                                    if (!seen_by_enemy && !is_human) w *= 2;
                                }
                                if (!better) {
                                    if (CurrentTurn < 50) w *= 2;
                                    if (CurrentTurn < 100 && !is_human) w *= 2;
                                }
                                if (!(cur_base().state_flags & 0x1000000)) w *= 2;
                            }
                        }
                    }
                    if (need >= 3) w <<= 3;
                }
                if (!queued_all[u]) {
                    int32_t bm = f.best_mineral_output;
                    int32_t lim = bm * 3 / 4;
                    if (bm - 1 < lim) lim = bm - 1;
                    bool by_plan;
                    int32_t rbp = f.region_base_plan[region];
                    if (mineral_output >= lim && cur_base().queue_items[0] > -70 && !has_skunk) {
                        if (skunk_count < 2) w /= 2;
                        if ((uint8_t)unit.plan == rbp) w /= 2;
                        if (unit.plan == 1 && rbp <= 2) w /= 2;
                        if (better && rbp == 7 && seen_by_enemy) w /= 2;
                        if (unit.plan == 1 && (ai_tech || ai_wealth)) w /= 2;
                        if (unit.plan == 0 && (ai_power || ai_growth)) w /= 2;
                    }
                    if (has_skunk) {
                        w = w * 2 / 3;
                        by_plan = true;
                    } else {
                        if (skunk_count) w = (skunk_count + 5) * w / 5;
                        by_plan = skunk_count < f.base_count / 4;
                    }
                    if (by_plan) {
                        int32_t p = (uint8_t)unit.plan;
                        switch (rbp) {
                        case 0:
                        case 2:
                            if (p == rbp) w /= 2;
                            if (p == 1) w = w * 3 / 4;
                            break;
                        case 8:
                            w = w * 3 / 4;
                            break;
                        case 7:
                            if (p == 7) w /= 2;
                            if (p == 6) w = w * 3 / 4;
                            break;
                        default:
                            break;
                        }
                    }
                } else if (!has_skunk) {
                    continue;
                }
                if (Weapon[unit.weapon_id].mode > 2 && !has_skunk) w *= 2;
                int32_t bm = f.best_mineral_output;
                if (mineral_output >= bm * 2 / 3 && plan_region != 8) w = w * 2 / 3;
                if (mineral_output < bm / 2) {
                    if (!has_skunk && !(better && plan == 2)) w *= 2;
                } else {
                    if (!prototypes && plan_region != 8) w = w * 3 / 4;
                    if (gov_discover && !has_skunk) w /= 2;
                }
            }
            // Defense needs and unit support.
            bool psych_cap = true;
            if (need) {
                bool to_support = false;  // jump to the defense adjustments (0x4FCD78)
                bool alert = false;       // need 3: mark as alternative (0x4FCD31)
                if (plan == 2) {
                    to_support = true;
                } else if (plan == 3) {
                    if (f.best_armor_value > 1 && need < 3) w *= 2;
                    to_support = true;
                } else if (plan == 0) {
                    if (has_abil(u, 0x8000) && (cur_base().state_flags & 0x200000)) alert = true;
                    else to_support = true;
                } else if (plan == 1) {
                    if (need != 3 && (sc11 + defensive < garrison ||
                                      f.region_total_offensive_units[region] < guard_check(faction_id, region))) {
                        to_support = true;
                    } else if ((plan_region != 0 && plan_region != 7) || sc11 + defensive < garrison) {
                        to_support = true;
                    } else {
                        alert = true;
                    }
                } else {
                    alert = true;
                }
                if (alert) {
                    psych_cap = false;
                    if (need == 3 && (!is_human || (cur_base().governor_flags & 0x1000))) flag78 = 1;
                } else if (to_support) {
                    if (!sc11 && defensive < garrison && (uint8_t)unit.plan > 1) w *= 2;
                    if (has_abil(u, 0x40000) && stack_check(stack, 6, 0x40000, -1, -1) < defensive / 2 - 1) w /= 2;
                    if (has_abil(u, 0x200) && stack_check(stack, 6, 0x200, -1, -1) < defensive / 2 - 1) w /= 2;
                    if (need == 3) {
                        w *= (unit.cost + 1) / 2;
                        if ((plan == 2 || f.best_armor_value == 1) && (u < 64 || (unit.unit_flags & 4))) {
                            prio = 0;
                            psych_cap = false;
                        }
                    }
                }
                if (psych_cap && psych_need < 2) {
                    int32_t lim = 1 << (3 - need);
                    if (prio >= lim) prio = lim;
                }
            } else if ((uint8_t)unit.plan <= 9 && cur_base().mineral_intake_2 &&
                       !(unit.plan == 7 && plan_region == 7)) {
                int32_t support = f.SE_support_pending;
                int32_t cons = cur_base().mineral_consumption;
                int32_t limit, add;
                if (support >= 3) {
                    limit = cur_base().pop_size;
                    add = 1;
                } else if (support <= -4) {
                    limit = 0;
                    add = 2;
                } else if (support <= -3) {
                    limit = 0;
                    add = 1;
                } else if (support == -2) {
                    limit = 1;
                    add = 1;
                } else {
                    limit = support + 2;
                    add = 1;
                }
                if (BaseForcesSupported >= limit) cons += add;
                int32_t k = (ai_wealth - ((threat || vendetta_plan7) ? 1 : 0) - ai_power + 3) * cons;
                int32_t intake = cur_base().mineral_intake_2;
                w = (k * k / 9 + intake) * w / intake;
            }
            uint16_t rf = f.region_flags[region];
            if (rf & 0x4200) {
                int32_t p = (uint8_t)unit.plan;
                if (p == 0 || p == 1 || p == 4 || p == 11 || p == 5) {
                    w /= 2;
                    if (plan_region == 2 && (rf & 0x4000)) w /= 2;
                }
            }
            if (target_here && (plan_region == 0 || enemy_elsewhere > 0)) {
                if (triad) {
                    if (target_here > 1) w *= 2;
                } else if ((uint8_t)unit.plan < 2) {
                    w = (4 - target_here) * w / 4;
                }
            }
            if (Weapon[unit.weapon_id].mode <= 2 && has_fac_built(FAC_BIOENHANCEMENT_CENTER))
                w = (bioenh_count + 2) * w / (bioenh_count + 3);
            if (f.unk_66 && f.unk_66 > f.unk_106 && f.unk_66 > f.unk_105) {
                if (has_abil(u, 0x120)) w /= 2;
                else if (has_abil(u, 0x8000)) w *= 2;
                else if (!triad && !plan) w = w * 2 / 3;
            }
            if (has_abil(u, 0x8000)) {
                if (!(f.player_flags_ext & 0x40) && !at_goal(faction_id, 16, cur_base().x, cur_base().y) &&
                    !(cur_base().state_flags & 0x200000))
                    prio += veh_region[pt(triad, plan)] + prod_region[pt(triad, plan)];
                if (!stack_check(stack, 15, -1, -1, -1)) {
                    if (at_goal(faction_id, 16, cur_base().x, cur_base().y) || (cur_base().state_flags & 0x200000))
                        w /= 3;
                    else if (cur_base().event_flags & 0x100)
                        w = w * 2 / 3;
                    if (plan_region <= 2) w = w * 2 / 3;
                }
                if ((enemy_region >= 0 || enemy_elsewhere >= 0) && (!stack_check(stack, 15, -1, -1, -1) || !need))
                    w = w * 2 / 3;
            } else if (enemy_elsewhere >= 0 && !need) {
                if (plan == 0) w /= 2;
                else if (plan == 1) w = w * 2 / 3;
            } else if (enemy_region >= 0 && !need) {
                if (plan == 0) w = w * 2 / 3;
                else if (plan == 1) w = w * 4 / 5;
            }
            if (has_abil(u, 8) && (is_sea || plan_region == 7)) w = w * 2 / 3;
            if (has_abil(u, 0x10)) {
                if (plan_region != 7 && plan_region != 0) {
                    w = w * 2 / 3;
                } else {
                    w /= 2;
                    if (!f.units_active[u] && !f.units_queue[u]) w /= 2;
                }
            }
            if (Weapon[unit.weapon_id].offense_value) {
                int32_t wm = worm_mod(CurrentBaseID, faction_id);
                int32_t mm = morale_mod(CurrentBaseID, faction_id, triad) + f.SE_morale_pending;
                if (plan == 2 && has_fac_built(FAC_CHILDREN_CRECHE)) {
                    int32_t v = mm - f.SE_morale_pending + 1;
                    if (mm <= v) mm = v;
                }
                if (Weapon[unit.weapon_id].offense_value < 0 && u < 64) {
                    prio += 2 - range(worm_mod(CurrentBaseID, faction_id), 0, 2);
                    if (mm > wm) prio += mm - wm;
                    w = w * 8 / range(wm - mm + 6, 1, 16);
                    if (m.rule_psi) w = w * 100 / range(m.rule_psi + 100, 50, 200);
                    if (is_human && mm >= 0 && wm < 3) {
                        prio++;
                        w *= 2;
                    }
                } else {
                    if (triad != 2 && wm > mm + 1) prio++;
                    w = w * 8 / range(wm - mm + 8, 1, 16);
                }
            }
            int32_t as = arm_strat(unit.armor_id, faction_id);
            int32_t ws = planet_buster2(u) ? 50 : weap_strat(unit.weapon_id, faction_id);
            if (need && (plan == 2 || plan == 3 || (plan == 1 && (plan_region == 2 || plan_region == 8)))) as *= 2;
            int32_t r = range(bit_count(unit.ability_flags) + ws + as, 1, 999);
            // BUG(original): the speed of the unit on top of the base tile (Vehs[-1] when there is none)
            // instead of the candidate's.
            int32_t spd = raw_chassis_speed(raw_unit_chassis(raw_veh_unit_id(stack))) / Rules.move_rate_roads + 1;
            int32_t val = w / (r * spd);
            if (vendetta_plan7 && plan_region == 7 && (uint8_t)unit.plan <= 1 && !need) {
                if (ai_fight == 0) val = val * 2 / 3;
                else if (ai_fight == 1) val /= 2;
                if (!prod_region[pt(triad, plan)]) val /= 2;
            }
            if (own_vehs >= 256) val *= 2;
            if (gov_conquer && Weapon[unit.weapon_id].offense_value && plan != 2) val /= 2;
            if (gov_discover) val *= 2;
            if (gov_build && unit.plan != 9) val *= plan != 2 ? 8 : 2;
            if (gov_explore) {
                int32_t p = (uint8_t)unit.plan;
                if (p <= 1) val *= 2;
                if (p >= 3) val /= 2;
            }
            int32_t lose = base_lose_minerals(CurrentBaseID, u);
            if (lose > 0 && (!is_human || (!score_out && (cur_base().governor_flags & GOV_ACTIVE))) &&
                plan_region != 2) {
                int32_t c = veh_cost(u, CurrentBaseID, 0) * cost_factor(faction_id, 1, -1);
                val = (lose + c) * val / c;
            }
            bool riot_adjust = false;
            if (unk1_flag == 1) {
                if (plan_region != 7) unk1_flag = 0;  // BUG(original): persists for the following units
                else riot_adjust = true;
            } else if (unk1_flag) {
                riot_adjust = true;
            }
            if (riot_adjust) {
                int32_t p = (uint8_t)unit.plan;
                bool divided = false;
                if (plan_region > 2 && !(p == 2 && !need) && (p <= 3 || p == 8 || p == 11)) {
                    val /= unk1_flag + 1;
                    divided = true;
                }
                if (!divided && p == 7 && unk1_flag == 2) val *= 2;
            }
            prio *= val;
            int32_t total = prio;
            if (!flag78) {
                int32_t s = total;
                if (is_human) {
                    uint32_t mask = 0;
                    uint32_t gov = (uint32_t)cur_base().governor_flags;
                    switch ((uint8_t)unit.plan) {
                    case 0: mask = 0x200; break;
                    case 1: mask = (gov & 0x200) ? 0x200 : 0x1000; break;
                    case 2: mask = 0x1000; break;
                    case 3: mask = ((gov & 0x1000) && !unit.chassis_id) ? 0x1000 : 0x400000; break;
                    case 4: mask = 0x2000; break;
                    case 5: mask = 0x800; break;
                    case 6: mask = 0x400; break;
                    case 7: mask = 0x800000; break;
                    case 8: mask = 0x20000; break;
                    case 9: mask = 0x8000; break;
                    case 10: mask = 0x800000; break;
                    case 11: mask = 0x100000; break;
                    default: mask = 0; break;
                    }
                    if (u >= 64 && !(unit.unit_flags & 4)) mask |= 0x80000;
                    if ((gov & mask) != mask) continue;
                }
                if (score_out && base_claimed(u, total, gov_explore, gov_discover, gov_build, gov_conquer))
                    s = total + 100000;
                if (s <= score[0]) {
                    if (s == score[0] && !((choice[0] >= 0 || need >= 3) && val < tie)) {
                        total = 999999;
                        prio = 999999;
                    } else {
                        tie = val;
                        score[0] = s;
                        choice[0] = u;
                    }
                }
            } else {
                category = 0;
                if (!(choice[1] >= 90000 || plan == 9 || plan == 8)) continue;
                if (unit.plan == 0 || plan == 4 || plan == 6) continue;
            }
            int32_t k = category ? 2 : 1;
            if (total > score[k]) continue;
            score[k] = total;
            choice[k] = u;
        }
    }

    // ---- facilities ----
    // BUG(original): a few cases read the population of Bases[BaseCount] (the base loop counter
    // left in the argument slot) instead of the current base.
    const int32_t stale_base = BaseCount > 0 ? BaseCount : 0;
    for (int32_t fac = 1; fac < 70; fac++) {
        if (!facility_avail(fac, faction_id, CurrentBaseID, 0)) continue;
        BASE& cb = cur_base();
        int32_t threat_flag = 0;   // [ebp-0x14]
        int32_t defensive_fac = 0; // [ebp-0x90]
        int32_t spec = cb.specialist_total - cb.specialist_adjust;  // [ebp-0x6C]
        int32_t bonus = gov_build; // ebx
        int32_t value = 999;       // esi (lower is better)
        uint32_t req = 0x10000;    // [ebp-0x24]: governor permissions needed
        int32_t w = 0x100;         // [ebp-0x34]
        switch (fac) {
        case FAC_HEADQUARTERS:
            if (plan_region == 2 || f.SE_effic_pending <= -4) break;
            if (hq_count && (hq_count != 1 || cb.queue_items[0] != -1)) break;
            value = 3 - cb.pop_size / 5;
            break;
        case FAC_CHILDREN_CRECHE: {
            int32_t eff = f.SE_effic_pending > -4 ? cb.energy_inefficiency : 0;
            int32_t mor = f.SE_morale_pending;
            int32_t adj = mor > 0 ? 1 : (mor >= 0 ? 0 : -1);
            int32_t d = range((mor + adj + 6) / 2, 1, 999);
            value = 12 - eff - cb.pop_size * ((plan_region <= 2) + 1) / d;
            if (!has_project(FAC_CLONING_VATS - 70, faction_id) && cb.mineral_surplus > 2 && value > 4) value = 4;
            if (m.rule_morale + f.SE_morale_pending < 0 && plan_region == 2) value /= 2;
            value = range(value, 1, 12);
            if (!has_fac_built(FAC_RECYCLING_TANKS) && cb.pop_size < 5) break;
            if (cb.energy_inefficiency < cb.pop_size / 2 && cb.energy_inefficiency < cb.energy_intake_2 / 2) break;
            bonus = gov_build * 2;
            break;
        }
        case FAC_RECYCLING_TANKS:
        case FAC_AQUAFARM:
        case FAC_SUBSEA_TRUNKLINE:
        case FAC_THERMOCLINE_TRANSDUCER: {
            int32_t v = ai_power - ai_wealth - ai_tech + 4;
            if (fac != FAC_RECYCLING_TANKS) v -= (m.rule_flags >> 7) & 2;
            if (plan_region == 7) v--;
            else if (plan_region <= 2) v++;
            int32_t st = cb.state_flags;
            if (st & 0x1000) v -= ResInfo.recycling_tanks.energy;
            if (st & 0x2000) v -= ResInfo.recycling_tanks.mineral;
            if (is_sea) {
                if ((st & 0x4000) && fac == FAC_AQUAFARM) v /= 2;
                if ((st & 0x2000) && fac == FAC_SUBSEA_TRUNKLINE) v /= 2;
                if ((st & 0x1000) && fac == FAC_THERMOCLINE_TRANSDUCER) v /= 2;
            }
            if (fac != FAC_RECYCLING_TANKS) {
                int32_t count = 0;
                for (int32_t i = 0; i < 21; i++) {
                    int32_t x = map_wrap_x(cur_base().x + TableOffsetX[i]);  // xrange
                    int32_t y = cur_base().y + TableOffsetY[i];
                    if (!on_map(x, y) || !is_ocean(x, y)) continue;
                    if (fac == FAC_AQUAFARM) {
                        if (bit_at(x, y) & 0x8000) count++;
                    } else if (fac == FAC_SUBSEA_TRUNKLINE) {
                        if (bit_at(x, y) & 0x10) count++;
                    } else if (fac == FAC_THERMOCLINE_TRANSDUCER) {
                        if (bit_at(x, y) & 0x40) count++;
                    }
                }
                if (!count) {
                    value = 999;
                    break;
                }
                v -= count;
            }
            if (cb.pop_size < 5) v--;
            value = range(v, 1, 4);
            int32_t expanding = cb.state_flags & 0x1000000;
            if (!expanding && (plan_region == 8 || f.units_active[0] + f.base_count < 4)) value *= 2;
            if (!gov_discover) break;
            bonus = (!expanding && plan_region == 8) ? 1 : 2;
            break;
        }
        case FAC_PERIMETER_DEFENSE:
        case FAC_TACHYON_FIELD:
        case FAC_FLECHETTE_DEFENSE_SYS: {
            int32_t b;
            defensive_fac = 1;
            if (fac == FAC_PERIMETER_DEFENSE) {
                if (has_project(FAC_CITIZENS_DEFENSE_FORCE - 70, faction_id)) {
                    defensive_fac = 0;
                    break;
                }
                b = range(8 - cb.pop_size - (has_fac_built(FAC_HEADQUARTERS) ? 2 : 0), 1, 10);
                for (int32_t i = 1; i < 8; i++) {
                    if (has_treaty(faction_id, i, 0x10)) continue;
                    uint32_t t = has_treaty(faction_id, i, 0x40020);
                    uint32_t c = t | (Factions[i].integrity_blemishes >= 6);
                    if (!c) continue;
                    b -= Factions[i].region_total_bases[region] ? bit_count(c) : 1;
                    if (f.region_total_bases[region] == 1) b--;
                }
                if (f.clean_minerals_modifier >= 4) b -= range(cb.eco_damage / 10, 1, 4);
            } else {
                b = range(10 - cb.pop_size / 2 - (has_fac_built(FAC_HEADQUARTERS) ? 2 : 0), 1, 10);
                for (int32_t i = 1; i < 8; i++) {
                    if (has_treaty(faction_id, i, 0x10)) continue;
                    uint32_t t = has_treaty(faction_id, i, 0x40020);
                    if (!t) continue;
                    b -= human(i) ? bit_count(t) : 1;
                }
                if (f.clean_minerals_modifier >= 4 && fac != FAC_FLECHETTE_DEFENSE_SYS)
                    b -= range(cur_base().eco_damage / 10, 1, 4);
            }
            if (b < 1 && !need) w = 0x80;
            value = range(b, 1, 10);
            bonus = (gov_conquer && plan_region == 2 && !need) ? (1 | gov_build) : gov_build;
            break;
        }
        case FAC_RECREATION_COMMONS:
            if (psych_need >= 2) {
                req = 0;
                value = -10;
                break;
            }
            value = (has_project(FAC_VIRTUAL_WORLD - 70, faction_id) ? 6 : 7) - (psych_need + 2) * spec;
            bonus = gov_build | gov_discover;
            if (f.diff_level > 3 && gov_discover) bonus = 2;
            if (has_project(FAC_CLONING_VATS - 70, faction_id)) value /= 2;
            break;
        case FAC_ENERGY_BANK:
            value = range((6 - ai_wealth) * 2 - cb.energy_intake_2 / 2, 1, 10);
            if (!has_fac_built(FAC_RECYCLING_TANKS) && cur_base().pop_size < 5) break;
            bonus = gov_build * 2;
            break;
        case FAC_NETWORK_NODE: {
            int32_t sc = stack_check(stack, 2, 12, faction_id, -1);
            value = range((5 - ai_tech - sc) * 2 - cb.energy_intake_2 / 3, 1, 10);
            if (has_project(FAC_VIRTUAL_WORLD - 70, faction_id)) {
                if (psych_need < 2) {
                    int32_t v = 6 - (psych_need + 2) * spec;
                    if (value >= v) value = v;
                } else {
                    req = 0;
                    value = -10;
                }
            }
            bonus = (has_project(FAC_VIRTUAL_WORLD - 70, faction_id) ? 3 : 1) * gov_discover;
            break;
        }
        case FAC_BIOLOGY_LAB: {
            value = (5 - ai_tech - ai_growth) * 2;
            if (m.rule_psi > 0) {
                value = value * 100 / range(m.rule_psi + 100, 50, 200);
                threat_flag = threat;
            }
            int32_t mor = f.SE_morale_pending;
            if (mor < 0) {
                value = (mor - 1) / 2 + value / 2;
                threat_flag = threat;
            }
            if (gov_discover || (gov_conquer && mor < 0 && plan_region != 2 && !(f.region_flags[region] & 0x4000)))
                bonus = 1;
            else
                bonus = 0;
            break;
        }
        case FAC_SKUNKWORKS: {
            int32_t fight = is_human ? 0 : ai_fight;
            value = (skunk_count - ai_power) * 2 + 6 - fight + same_prod;
            req = 0x90000;
            if (has_fac_built(FAC_COMMAND_CENTER)) value--;
            if (has_fac_built(FAC_BIOENHANCEMENT_CENTER)) value--;
            value = f.best_mineral_output * value / range(mineral_output, 1, 999);
            bonus = gov_discover;
            threat_flag = 1;
            break;
        }
        case FAC_HOLOGRAM_THEATRE:
            if (psych_need >= 2) {
                req = 0;
                value = -8;
                break;
            }
            value = 8 - (psych_need + 2) * spec;
            if (has_project(FAC_CLONING_VATS - 70, faction_id)) value /= 2;
            break;
        case FAC_PARADISE_GARDEN:
            if (psych_need >= 2) {
                req = 0;
                value = -6;
                break;
            }
            value = 8 - spec;
            break;
        case FAC_TREE_FARM:
            if (aah_ooga(faction_id, faction_id)) farm_tiles *= 2;
            value = range(12 - (farm_tiles + 2) / 4 - (cb.eco_damage + 1) / 2 - cb.energy_intake_2 / 3, 1, 10);
            break;
        case FAC_HYBRID_FOREST:
            value = range(12 - (farm_tiles + 2) / 4 - (cb.eco_damage + 2) / 3 - cb.energy_intake_2 / 4, 1, 10);
            break;
        case FAC_FUSION_LAB:
            value = range((6 - ai_tech) * 2 - cb.energy_intake_2 / 3, 1, 12);
            bonus = gov_build | gov_discover;
            break;
        case FAC_QUANTUM_LAB:
            value = range((6 - ai_tech) * 2 - cb.energy_intake_2 / 4, 1, 12);
            bonus = gov_build | gov_discover;
            break;
        case FAC_RESEARCH_HOSPITAL:
        case FAC_NANOHOSPITAL: {
            if (psych_need >= 2) {
                req = 0;
                value = fac == FAC_RESEARCH_HOSPITAL ? -4 : -2;
                break;
            }
            int32_t sc = stack_check(stack, 2, 12, faction_id, -1);
            if (fac == FAC_RESEARCH_HOSPITAL)
                value = range((5 - ai_tech - sc) * 2 - cb.energy_intake_2 / 4, 1, 10);
            else
                value = range((6 - ai_tech - sc) * 2 - cb.energy_intake_2 / 5, 1, 10);
            bonus = gov_discover;
            break;
        }
        case FAC_ROBOTIC_ASSEMBLY_PLANT:
            value = range(10 - cb.mineral_intake, 1, 10);
            bonus = (gov_conquer && plan_region > 2) ? (1 | gov_build) : gov_build;
            break;
        case FAC_NANOREPLICATOR:
        case FAC_QUANTUM_CONVERTER:
            value = range(10 - cb.mineral_intake / 2, 1, 10);
            break;
        case FAC_GENEJACK_FACTORY:
            if (is_human) break;
            value = range((cb.drone_total - cb.talent_total - has_fac_built(FAC_BROOD_PIT)) * 2 + 16 -
                              cb.mineral_intake - f.SE_police_pending,
                          1, 16);
            bonus = (gov_conquer && plan_region > 2) ? 1 : 0;
            break;
        case FAC_PUNISHMENT_SPHERE:
            if (is_human) break;
            if (f.SE_police_pending + has_fac_built(FAC_BROOD_PIT) * 2 <= 0 && cb.drone_total >= 2 &&
                plan_region <= 2 && f.SE_economy_pending > 1 && (cb.assimilation_turns_left || !punish_count))
                value = range((8 - cb.drone_total) * 2 - cb.pop_size / 2, 1, 16);
            bonus = gov_conquer;
            break;
        case FAC_HAB_COMPLEX:
        case FAC_HABITATION_DOME: {
            int32_t limit = (fac == FAC_HAB_COMPLEX ? Rules.pop_limit_wo_hab_complex : Rules.pop_limit_wo_hab_dome) -
                            m.rule_population;
            if (has_project(FAC_ASCETIC_VIRTUES - 70, faction_id)) limit += 2;
            int32_t r = range(Rules.nutrient_intake_req_citizen, 1, 10);
            if ((cb.nutrient_surplus + 1) / r + cb.pop_size >= limit) {
                int32_t r2 = range(Rules.nutrient_intake_req_citizen, 1, 10);
                value = range(limit - (Rules.nutrient_intake_req_citizen / 2 + cb.nutrient_surplus) / r2 -
                                  cb.pop_size + 4,
                              1, 10);
                if (plan_region > 2) value /= 2;
            }
            if (has_project(FAC_CLONING_VATS - 70, faction_id)) value /= 2;
            bonus = gov_build | gov_explore;
            if (cb.pop_size < limit) break;
            if (!gov_conquer) bonus *= 2;
            if (aah_ooga(faction_id, faction_id)) value /= 2;
            break;
        }
        case FAC_PRESSURE_DOME: {
            if (alt_at(cb.x, cb.y) != 3) break;
            if (alt_detail_at(cb.x, cb.y) - MapSeaLevel < AltNatural[3] + ClimateFutureChange) {
                value = -16;
                break;
            }
            if (f.player_flags & 0x80000) {
                value = 0;
                break;
            }
            bool project = false;
            for (int32_t i = 0; i < 64; i++) {
                if (base_project(i) == CurrentBaseID) {
                    project = true;
                    break;
                }
            }
            if (!project) break;
            if (alt_detail_at(cb.x, cb.y) - MapSeaLevel < WorldBuilder.sea_level_rises + ClimateFutureChange + AltNatural[3])
                value = range(8 - cur_base().pop_size / 3, is_human ? 4 : 2, 8);
            else
                value = range(20 - cur_base().pop_size / 2, is_human ? 6 : 4, 12);
            break;
        }
        case FAC_COMMAND_CENTER:
        case FAC_BIOENHANCEMENT_CENTER:
        case FAC_BROOD_PIT: {
            req = 0x10200;
            defensive_fac = 1;
            threat_flag = 1;
            int32_t cnt = fac == FAC_COMMAND_CENTER ? cmdcenter_count : bioenh_count;  // [ebp-0x68]
            int32_t v = same_prod + (ai_tech - ai_power) * 2 + 5 + (fac == FAC_BROOD_PIT ? 5 : cnt);
            if (!is_human) v -= ai_fight * 2;
            v = (fac_maint(fac, faction_id) + 4) * v / 4;
            if (m.rule_psi > 0) v = (m.rule_psi + 100) * v / 100;
            if (fac == FAC_COMMAND_CENTER && !cnt && plan_region != 7 && f.region_total_bases[region] >= 4) v /= 2;
            value = range(v, 1, 99);
            if (f.region_total_combat_units[region] < f.region_total_bases[region] * 2 ||
                (f.region_flags[region] & 0x4000) ||
                (CurrentTurn >= 100 && f.region_total_bases[region] <= 8 && plan_region != 7))
                bonus = 0;
            else
                bonus = gov_conquer;
            break;
        }
        case FAC_NAVAL_YARD: {
            if (naval_base(CurrentBaseID)) {
                req = 0x10400;
                defensive_fac = 1;
                int32_t c = (ai_tech - ai_fight - ai_power + ai_growth) * 2 + 24 - Bases[stale_base].pop_size / 3 -
                            f.unk_65 - naval_yard_dist / 2 + navalyard_count;
                if (plan_region == 7) c /= 2;
                value = range(c, 1, 24);
                if (m.rule_flags & 0x100) value /= 3;
            }
            bonus = (gov_conquer && plan_region == 7) ? 1 : 0;
            break;
        }
        case FAC_AEROSPACE_COMPLEX: {
            req = 0x10800;
            defensive_fac = 1;
            threat_flag = 1;
            int32_t c = (ai_tech - f.unk_66 - ai_fight + ai_growth) * 2 - Bases[stale_base].pop_size / 3 - f.unk_68 +
                        (8 - ai_power) * 3 - aerospace_dist / 3 + aerospace_count;
            if (plan_region == 7) c /= 2;
            if (cb.state_flags & 0x100000) c /= 2;
            if (best_reac < 2) c *= 2;
            value = range(c, 1, 24);
            bonus = (gov_conquer && plan_region != 2 && !(f.region_flags[region] & 0x4000)) ? 1 : 0;
            break;
        }
        case FAC_CENTAURI_PRESERVE:
        case FAC_TEMPLE_OF_PLANET: {
            value = 11 - cb.eco_damage;
            if (m.rule_psi > 0) {
                value = value * 100 / range(m.rule_psi + 100, 50, 200);
                threat_flag = threat;
            }
            int32_t mor = f.SE_morale_pending;
            if (mor < 0) {
                value = (mor - 1) / 2 + value / 2;
                threat_flag = threat;
                bonus = (gov_conquer && !(f.region_flags[region] & 0x4000)) ? (1 | gov_build) : gov_build;
            }
            value = range(value, 1, 12);
            break;
        }
        case FAC_PSI_GATE:
            value = range((climactic_battle() ? 2 : 4) - ascent_human - at_climax(faction_id), 0, 4);
            break;
        case FAC_COVERT_OPS_CENTER:
            value = (2 - ((f.player_flags_ext >> 3) & 1)) * 5;
            break;
        case FAC_SUBSPACE_GENERATOR: {
            if (!(m.rule_flags & 0x80)) break;
            if (generators(faction_id, nullptr) >= Rules.subspace_gen_req) break;
            int32_t p = Bases[stale_base].pop_size;
            if (p >= Rules.base_size_subspace_gen) value = 1;
            else if (p >= Rules.base_size_subspace_gen * 4 / 5) value = 6;
            break;
        }
        case FAC_GEOSYNC_SURVEY_POD:
            value = 7;
            // BUG(original): adjusts the stored base stack id instead of a score.
            if (f.AI_growth) stack++;
            if (f.AI_tech) stack--;
            if (f.AI_wealth) stack -= 3;
            if (f.AI_power) stack--;
            break;
        case FAC_SKY_HYDRO_LAB:
        case FAC_NESSUS_MINING_STATION:
        case FAC_ORBITAL_POWER_TRANS: {
            int32_t sat = (&f.satellites_nutrient)[fac - FAC_SKY_HYDRO_LAB];
            if (sat >= cb.pop_size) break;
            value = range((sat + (4 - ai_wealth) * 2 + ai_power) * 2 - cb.pop_size, 2, 16);
            if (fac == FAC_SKY_HYDRO_LAB && has_project(FAC_CLONING_VATS - 70, faction_id)) value /= 2;
            break;
        }
        case FAC_ORBITAL_DEFENSE_POD: {
            req = 0x12000;
            defensive_fac = 1;
            int32_t threat_pb = 1;
            for (int32_t i = 1; i < 8; i++) {
                if (i == faction_id || has_treaty(faction_id, i, 1)) continue;
                int32_t pb = Factions[i].planet_busters;
                int32_t lim = Factions[i].major_atrocities ? pb : (pb + 1) / 2;
                if (threat_pb <= lim) threat_pb = lim;
                if ((has_treaty(faction_id, i, 0x30) || human(i)) && threat_pb <= Factions[i].satellites_ODP)
                    threat_pb = Factions[i].satellites_ODP;
            }
            if (f.satellites_ODP < threat_pb + 2) {
                value = range((f.satellites_ODP - threat_pb) * 2 + 12 - cb.pop_size, 1, 12);
                Faction& r = Factions[RankingFactionIDUnk1];
                if (!r.planet_busters && !r.major_atrocities && r.satellites_ODP <= f.satellites_ODP) value *= 2;
            }
            threat_flag = 1;
            bonus = (gov_conquer && plan_region == 7) ? 1 : 0;
            break;
        }
        default:
            break;
        }
        // Common scoring.
        int32_t lose = base_lose_minerals(CurrentBaseID, -fac);
        if (lose > 0 && (!is_human || ((cur_base().governor_flags & GOV_ACTIVE) && !score_out))) {
            int32_t c = cost_factor(faction_id, 1, -1) * Facility[fac].cost;
            if (c) w = (lose + c) * w / c;
        }
        int32_t v = value * w * 32 / ((ai_wealth - ai_power + 3) * (bonus + 1));
        int32_t sv = v;  // [ebp-0x18]
        if ((threat_flag != 0) != (threat != 0) || (threat && need)) {
            v *= 2;
            sv = v;
        }
        if (plan_region == 2 || (f.region_flags[region] & 0x4000)) {
            v *= 2;
            sv = v;
        }
        if (plan_region <= 2 && !defensive_fac) {
            v *= 2;
            sv = v;
        }
        if (enemy_elsewhere >= 0 && !defensive_fac) {
            if (ai_fight > 0 || (human(enemy_elsewhere) && (ai_fight >= 0 || has_treaty(faction_id, enemy_elsewhere, 0x20))))
                sv = v * 2;
        }
        for (int32_t k = 0; k < 2; k++) {
            int32_t s = sv;
            int32_t idx;
            if (k) {
                idx = defensive_fac ? 2 : 1;
            } else {
                if (is_human) {
                    uint32_t gov = (uint32_t)cur_base().governor_flags;
                    if (!(gov & 0x10000)) continue;
                    if ((gov & req) != req) s += 100000;
                    if (score_out && base_claimed(-fac, s, gov_explore, gov_discover, gov_build, gov_conquer))
                        s += 100000;
                }
                idx = 0;
            }
            if (s < score[idx]) {
                score[idx] = s;
                choice[idx] = -fac;
            }
        }
    }
    // ---- secret projects ----
    bool run_projects = false;
    if (!no_gov) {
        BASE& cb = cur_base();
        int32_t rbp = f.region_base_plan[region];
        if (building_project && (rbp > 2 || projects_region < (rbp != 2) + 1)) {
            run_projects = true;
        } else if (is_human && ((uint32_t)cb.governor_flags & 0xDFBE00) == 0x40000) {
            run_projects = true;
        } else if (rbp != 2 && cb.pop_size >= 3 && need < 2 && !(need && defense <= 1 && !is_human) &&
                   mineral_output >= cb.pop_size * 2 && cb.drone_total <= cb.talent_total &&
                   !(cb.state_flags & BSTATE_DRONE_RIOTS_ACTIVE) &&
                   !(cb.drone_total && cb.drone_total >= cb.talent_total &&
                     cb.specialist_total > cb.specialist_adjust && cb.pop_size < 8) &&
                   !(cb.pop_size <= (max_pop + 1) / 2 && mineral_output <= (f.best_mineral_output + 1) / 2)) {
            int32_t cnt = plan_region == 7 ? f.base_count : f.region_total_bases[region];
            if (cnt - projects_region * 3 >= (plan_region == 8) - gov_discover - ai_tech + ai_power + 3)
                run_projects = true;
        }
    }
    for (int32_t p = 0; run_projects && p < 64; p++) {
        const int32_t fac = p + 70;
        if (!facility_avail(fac, faction_id, CurrentBaseID, 0)) continue;
        CFacility& F = Facility[fac];
        if (!is_human && DiffLevel < 3 && F.preq_tech >= 0 && !(TechOwners[F.preq_tech] & FactionStatus[0])) continue;
        int32_t own = (int32_t)(project_builders[p] & fbit);  // [ebp-0x84]
        int32_t threat_flag = 0;
        int32_t grp = projects_by_group[p / 8];
        int32_t v = projects_region * 2 + 8 - F.AI_fight * ai_fight - F.AI_power * ai_power - F.AI_wealth * ai_wealth -
                    F.AI_tech * ai_tech - F.AI_growth * ai_growth - (f.ranking >= 5) - cur_base().pop_size;
        v = v - CurrentTurn / 40 + grp + projects_here / 2 + enemy_near;
        int32_t w = 0x100;
        int32_t div = gov_build;  // [ebp-0x108]
        if (gov_discover) div = 2;
        uint16_t rf = f.region_flags[region];
        bool talent = false;
        if (p == 7 || p == 0) {
            if (plan_region == 8) w = 0xAA;
            if (p == 0) {
                if (m.rule_talent || f.SE_talent_pending > 0) w *= 2;
                talent = true;
            }
        }
        if (!talent) {
            if (p == 8 || p == 16 || p == 19 || p == 9) w = (m.rule_morale + f.SE_morale_pending + 8) * w / 8;
            if (p == 6 || p == 14 || p == 26 || p == 30) talent = true;
        }
        if (talent) {
            if (m.rule_talent) w = w * 3 / 2;
            if (m.rule_drone) w /= 2;
        }
        bool rf_check = false;
        if (p == 5) {
            threat_flag = threat;
            int32_t k = 0;
            for (; k < m.faction_bonus_count; k++) {
                int32_t id = m.faction_bonus_id[k];
                if ((id == 2 || id == 12) && m.faction_bonus_val1[k] == 4) break;
            }
            if (k < m.faction_bonus_count) continue;
            rf_check = true;
        } else if (p == 1) {
            rf_check = true;
        }
        if (rf_check) {
            if (rf & 0x40) w /= 2;
            else if (rf & 0x20) w = w * 2 / 3;
        }
        if ((p == 10 || p == 2) && plan_region == 7) w /= 2;
        if (p == 15) {
            if (!f.AI_power) w /= 2;
            if (f.AI_tech) w /= 2;
            if (f.SE_probe_base < 0) w /= 2;
            else if (f.SE_probe_base > 0) w *= 2;
        } else if (p == 34) {
            if (f.player_flags_ext & 8) w /= 2;
        } else {
            if ((p == 36 || p == 33) && f.AI_wealth) w /= 2;
            if (p == 35) {
                if (rf & 0x40) w /= 2;
                else if (rf & 0x20) w = w * 2 / 3;
            } else if (p == 11) {
                for (int32_t i = 1; i < 8; i++)
                    if (Factions[i].tech_ranking >= f.tech_ranking) w = w * 7 / 8;
            } else if (p == 4) {
                for (int32_t i = 1; i < 8; i++)
                    if (i != faction_id) w = (has_treaty(faction_id, i, 8) ? 9 : 7) * w / 8;
                if (GovernorFaction == faction_id) w /= 2;
                if (f.ranking == 7) w /= 2;
                for (int32_t k = 0; k < m.faction_bonus_count; k++) {
                    if (m.faction_bonus_id[k] == 11) {
                        w /= 2;
                        break;
                    }
                }
            }
        }
        if (!building_project && p < 31 && (plan_region == 2 || (rf & 0x4000))) w *= 2;
        if (p == 31 && !faster_project && (GameRules & 0x800)) {
            w /= 2;
            if (F.preq_tech >= 0) {
                for (int32_t i = 1; i < 8; i++) {
                    if (i == faction_id || Factions[i].ranking <= f.ranking) continue;
                    if (has_tech(F.preq_tech, i)) w *= 2;
                    else w /= 2;
                }
            }
        }
        if (!grp && !projects_region) w /= 4;
        if (p == 32 && !faster_project && mineral_output >= f.best_mineral_output / 2) w /= 4;
        clear();
        says(m.filename);
        for (char* c = StrBuffer; *c; c++) *c = (char)std::toupper((uint8_t)*c);  // CharUpperA
        if (!std::strcmp(StrBuffer, "CARETAKE") && p == 32) {
            v = 99;
            w = 0x400;
        }
        v = range(v, 1, 99);
        int32_t rank = v * w * 32;  // [ebp-0xAC]
        int32_t lose = base_lose_minerals(CurrentBaseID, -fac);
        if (lose > 0 && (!is_human || (!score_out && (cur_base().governor_flags & GOV_ACTIVE)))) {
            int32_t c = cost_factor(faction_id, 1, -1) * F.cost;
            w = (lose + c) * w / c;
        }
        w = (f.best_mineral_output + 8) * v * w * 32 / ((ai_tech - ai_power + 3) * (mineral_output + 8));
        if (threat_flag != threat) {
            int32_t c = cost_factor(faction_id, 1, -1) * F.cost + 1;
            w += (c - cur_base().minerals_accumulated) * w / c;
        }
        w = (w * (own ? 2 : faster_project + 1) << 10) / (f.energy_credits + 1024);
        int32_t r = range(cur_base().mineral_surplus / (CurrentTurn / 100 + 3), 0, 8);
        int32_t ws = w * 8 / (r + 8);
        if (div) ws /= div + 1;
        if (has_fac_built(FAC_HEADQUARTERS) && !own) ws = ws * 3 / 4;
        if (!is_human && cur_base().queue_items[0] == -fac) {
            rank /= 4;
            if (cur_base().minerals_accumulated >= cost_factor(faction_id, 1, -1) * F.cost) {
                ws /= 2;
                if (need < 3) need = 0;
            }
        }
        bool elsewhere = false;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id || i == CurrentBaseID ||
                Bases[i].queue_items[0] != -fac)
                continue;
            if (region_at(Bases[i].x, Bases[i].y) != region) {
                elsewhere = true;
                break;
            }
            if (!own || building_project) ws *= 2;
        }
        if (elsewhere || ws < 0) continue;
        int32_t lim = 9999;
        if (!has_fac_built(FAC_HAB_COMPLEX) && has_tech(Facility[FAC_HAB_COMPLEX].preq_tech, faction_id))
            lim = Rules.pop_limit_wo_hab_complex;
        else if (!has_fac_built(FAC_HABITATION_DOME) && has_tech(Facility[FAC_HABITATION_DOME].preq_tech, faction_id))
            lim = Rules.pop_limit_wo_hab_dome;
        lim -= m.rule_population;
        if (cur_base().pop_size >= lim && !building_project) continue;
        if (building_project) {
            int32_t rbp = f.region_base_plan[region];
            if (rbp == 2 && need) {
            } else if (rbp <= 2) {
                if (!need) ws /= 2;
            } else {
                ws /= 3;
                if (need < 3 && psych_need < 2) {
                    ws /= 2;
                    if (!(cur_base().state_flags & 8) && SecretProjects[p] == -1 && choice[0] > -70) {
                        score[0] = 9999999;
                        psych_need = 0;
                        need = 0;
                    }
                }
            }
        }
        if (own) {
            if (!projects_region || is_ocean(cur_base().x, cur_base().y) ||
                prod_all[pt(0, 10)] + veh_all[pt(0, 10)] > 2)
                continue;
        }
        int32_t item = fac;  // [ebp-0xD8]
        if (!has_fac_built(FAC_RECREATION_COMMONS) &&
            (!is_human || (cur_base().governor_flags & 0x10000))) {
            if (!has_tech(Facility[FAC_RECREATION_COMMONS].preq_tech, faction_id)) continue;
            item = FAC_RECREATION_COMMONS;
        }
        if (is_human) {
            if (own) continue;
        } else if (own && !veh_avail(5, faction_id, CurrentBaseID)) {
            own = 0;
        }
        bool gov_ok = !is_human || (cur_base().governor_flags & 0x40000);
        if (choice[0] > -70 || own) {
            for (int32_t k = 0; k < 2; k++) {
                if (!gov_ok) continue;
                int32_t s = ws;
                if (score_out && base_claimed(-fac, ws, gov_explore, gov_discover, gov_build, gov_conquer))
                    s = ws + 100000;
                if (s < score[k]) {
                    score[k] = s;
                    choice[k] = own ? 5 : -item;
                    tie = rank;
                }
            }
        } else {
            if (rank >= tie) continue;
            tie = rank;
            for (int32_t k = 0; k < 2; k++) {
                if (!gov_ok) continue;
                int32_t s = ws;
                if (score_out && base_claimed(-fac, ws, gov_explore, gov_discover, gov_build, gov_conquer))
                    s = ws + 100000;
                choice[k] = -item;
                if (ws < score[k]) score[k] = s;
            }
        }
    }
    if (out_a2) {
        *out_a2 = choice[1];
        parse_num(1, score[1]);
    }
    if (out_a3) {
        *out_a3 = choice[2];
        parse_num(0, score[2]);
    }
    int32_t result = choice[0];
    if ((result < 0 || !Weapon[Units[result].weapon_id].offense_value) && veh_who(cur_base().x, cur_base().y) < 0 &&
        psych_need < 2 && (!is_human || (cur_base().governor_flags & 0x1000))) {
        int32_t best = 99999;
        for (int32_t u = 0; u < 512; u++) {
            if (!veh_avail(u, faction_id, CurrentBaseID)) continue;
            int32_t p = (uint8_t)Units[u].plan;
            if ((p == 2 || p == 3 || p == 1) && (Units[u].unit_flags & 4) && Units[u].cost < best) {
                best = Units[u].cost;
                choice[0] = u;
            }
        }
        result = choice[0];
    }
    if (score_out) *score_out = score[0];
    return result;
}
