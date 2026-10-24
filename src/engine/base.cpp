#include "engine/base.h"

#include <cstdlib>
#include <cstring>

#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/goals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/zoc.h"
#include "platform/win32.h"

#ifdef FC_ORIG_BASE_BUILD  // Test builds only (-DFC_ORIG_BASE_BUILD): route base_build to the original, to verify this module apart from base_misc
#define base_build(a, b, c, d) (((int32_t(__cdecl*)(int32_t, int32_t*, int32_t*, int32_t*))0x4F81A0)(a, b, c, d))
#endif

namespace {
// base_energy reads base_minerals' stack residue (see base_energy): it is never tail-called, so it
// runs at the stack depth of the base_minerals call before it.
inline void base_energy_call() {
    base_energy();
    __asm__ volatile("");
}
// Raw byte view of the base table (the original indexes it without range checks).
uint8_t* base_raw(int32_t base_id) {
    return reinterpret_cast<uint8_t*>(&Bases[0]) + base_id * (int32_t)sizeof(BASE);
}
// Wrapped horizontal distance |x1 - x2| as the original computes it.
int32_t wrap_dx(int32_t x1, int32_t x2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    return dx;
}
// The distance measure base_find and friends inline (see veh_vector_dist).
int32_t base_dist(int32_t x, int32_t y, int32_t x2, int32_t y2) {
    int32_t dx = wrap_dx(x, x2);
    int32_t dy = std::abs(y - y2);
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t big = a > b ? a : b;
    int32_t small = a < b ? a : b;
    return big - ((((a + b) >> 1) - small + 1) >> 1);
}
int32_t base_dist(int32_t x, int32_t y, const BASE& base) { return base_dist(x, y, base.x, base.y); }
void fac_set_bit(int32_t base_id, int32_t facility_id) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    Bases[base_id].facilities_built[offset] |= (uint8_t)mask;
}
// Wrapped x of a radius table entry (the original's single correction).
int32_t wrap_x(int32_t x) { return map_wrap_x(x); }
}  // namespace

void GAMEAPI set_base(int32_t base_id) {
    CurrentBaseID = base_id;
    CurrentBasePtr = kBasesAddr + (uint32_t)base_id * (uint32_t)sizeof(BASE);
}

void GAMEAPI say_base(char* buffer, int32_t base_id) {
    if (base_id < 0) {
        std::strcat(buffer, label_get(25));
        return;
    }
    std::strcat(buffer, Bases[base_id].name);
}

int32_t GAMEAPI base_at(int32_t x, int32_t y) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX || !(map_tile(x, y)->items & 1)) return -1;
    for (int32_t i = 0; i < BaseCount; i++)
        if (Bases[i].x == x && Bases[i].y == y) return i;
    if (!BaseBitErrorShown) log_say_2("Base Bits Error  (x, y)", x, y, 0);
    if (!(GameState & 0x1000)) {
        rebuild_base_bits();
        return -1;
    }
    if (!BaseBitErrorShown) {
        BaseBitErrorShown = 1;
        if (MultiDebugActive) {
            parse_num(0, x);
            parse_num(1, y);
            X_pop("BASEBIT", 0);
        }
        BaseBitErrorShown = 0;
        // (The original polls GetAsyncKeyState(VK_CONTROL) here when DialogToggle is 0 and ignores
        // the result.)
        rebuild_base_bits();
        Console_focus(&MainConsole, x, y, CurrentPlayerFaction);
    }
    return -1;
}

int32_t GAMEAPI base_find(int32_t x, int32_t y) {
    int32_t best = 9999, found = -1;
    for (int32_t i = 0; i < BaseCount; i++) {
        int32_t dist = base_dist(x, y, Bases[i]);
        if (dist <= best) {
            found = i;
            best = dist;
        }
    }
    if (found >= 0) BaseFindDist = best;
    return found;
}

int32_t GAMEAPI base_find_2(int32_t x, int32_t y, int32_t faction_id) {
    int32_t best = 9999, found = -1;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id) continue;
        int32_t dist = base_dist(x, y, Bases[i]);
        if (dist <= best) {
            found = i;
            best = dist;
        }
    }
    if (found >= 0) BaseFindDist = best;
    return found;
}

int32_t GAMEAPI base_find_3(int32_t x, int32_t y, int32_t faction_id, int32_t region, int32_t faction_id_2,
                            int32_t faction_id_3) {
    int32_t best = 9999, found = -1;
    BaseFindDist = 9999;
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& base = Bases[i];
        int32_t owner = (uint8_t)base.faction_id;
        if (region >= 0 && map_tile(base.x, base.y)->region != region) continue;
        if (faction_id >= 0) {
            if (owner != faction_id) {
                if (faction_id_2 == -2) {
                    if (!(Factions[faction_id].diplo_status[owner] & 1)) continue;
                } else if (faction_id_2 < 0 || faction_id_2 != owner) {
                    continue;
                }
            }
        } else if (faction_id_2 >= 0 && owner == faction_id_2) {
            continue;
        }
        if (faction_id_3 >= 0 && owner != faction_id_3 && !(base.visibility & (uint8_t)(1 << faction_id_3)))
            continue;
        int32_t dist = base_dist(x, y, base);
        if (dist <= best) {
            found = i;
            best = dist;
        }
    }
    if (found >= 0) BaseFindDist = best;
    return found;
}

int32_t GAMEAPI whose_territory(int32_t faction_id, int32_t x, int32_t y, int32_t* base_id, int32_t ignore_comm) {
    MAP* sq = map_tile(x, y);
    int32_t owner = sq->owner;
    if (owner < 1) return -1;
    if (owner == faction_id) return owner;
    if (!ignore_comm && !(GameState & 0x80) &&
        (Factions[faction_id].diplo_status[owner] & 0x8000008) != 0x8000008)
        return -1;
    if (base_id) *base_id = base_find_3(x, y, -1, sq->region, -1, -1);
    return owner;
}

int32_t GAMEAPI base_territory(int32_t faction_id, int32_t x, int32_t y) {
    int32_t base_id = y;  // the original passes the address of its y argument as the base_id output
    int32_t owner = whose_territory(faction_id, x, y, &base_id, 0);
    if (owner < 0 || owner == faction_id) return -1;
    uint32_t humans = FactionStatus[0];
    if (!(humans & (1u << faction_id)) && !(humans & (1u << owner))) return -1;
    if (Factions[faction_id].diplo_status[owner] & 0x10) return -1;
    return base_id;
}

int32_t GAMEAPI best_specialist() {
    int32_t best = -999, choice = 0;
    for (int32_t i = 0; i < 7; i++) {
        if (!has_tech(Citizen[i].preq_tech, (uint8_t)cur_base().faction_id)) continue;
        int32_t value = Citizen[i].psych_bonus * 3;
        if (cur_base().pop_size >= Rules.min_base_size_specialists)
            value += Citizen[i].econ_bonus + Citizen[i].labs_bonus;
        if (value > best) {
            best = value;
            choice = i;
        }
    }
    return choice;
}

namespace {
// Reads lines of the open text section until an empty line or "#END": returns the number read
// (stops at limit when limit >= 0).
bool text_line_valid() {
    return std::strlen(TextBufferGetPtr) != 0 && crt_strnicmp(TextBufferGetPtr, "#END", 4) != 0;
}
}  // namespace

void GAMEAPI name_base(int32_t faction_id, char* name, int32_t is_final, int32_t is_sea) {
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    if (is_sea && !text_open(m.filename, "WATERBASES")) {
        int32_t i = 0;
        int32_t count = f.base_sea_name_offset + 1;
        for (; i < count; i++) {
            text_get();
            if (!text_line_valid()) break;
        }
        if (i == count) {
            std::strncpy(name, text_item(), 24);
            name[23] = 0;
            if (is_final) f.base_sea_name_offset = count;
            text_close();
            return;
        }
    }
    int32_t number = f.base_name_offset;
    name[0] = 0;
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    std::strcat(name, m.noun_faction);
    std::strcat(name, " ");
    number++;
    char digits[80];
    crt_itoa(number, digits, 10);
    std::strcat(name, digits);
    if (text_open(m.filename, "BASES")) return;
    int32_t names = 0;
    for (;;) {
        text_get();
        if (!text_line_valid()) break;
        names++;
    }
    int32_t target = number;
    if (number > 1 && number <= names) {
        // Pseudo-random permutation of the names (linear feedback shift register).
        int32_t k = 1;
        int32_t v = (int32_t)(((uint32_t)MapRandomSeed + (uint32_t)faction_id) & 0xFE) | 1;
        for (;;) {
            if (v & 1) v ^= 0x170;
            v >>= 1;
            if (v >= names) continue;
            if (++k == number) break;
        }
        target = v + 1;
    }
    if (text_open(m.filename, "BASES")) return;
    int32_t i = 0;
    for (; i < target; i++) {
        text_get();
        if (!text_line_valid()) break;
    }
    if (i != target) {
        if (text_open("BASENAME", "GENERIC")) return;
        for (; i < target; i++) {
            text_get();
            if (!text_line_valid()) return;  // the original returns without text_close
        }
    }
    std::strncpy(name, text_item(), 24);
    name[23] = 0;
    if (is_final) f.base_name_offset = number;
    text_close();
}

void GAMEAPI base_mark(int32_t base_id) {
    int32_t x = 0, y = 0;
    for (int32_t i = 0; i < 49; i++) {
        x = wrap_x(Bases[base_id].x + TableOffsetX[i]);
        y = Bases[base_id].y + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        if (i < 21) {
            bit_set(x, y, 0x1000, 1);
            using_set(x, y, (uint8_t)Bases[base_id].faction_id);
        } else {
            site_set(x, y, 0);
        }
    }
    // BUG(original): the sites are deleted around the last radius tile instead of the base.
    for (int32_t f = 0; f < 8; f++) del_site(f, 8, x, y, 3);
}

int32_t GAMEAPI cost_factor(int32_t faction_id, int32_t is_mineral, int32_t base_id) {
    int32_t factor;
    uint32_t humans = FactionStatus[0];
    if (humans & (1u << faction_id)) {
        factor = 10;
    } else {
        int32_t top = cs.FactionRankings[7];
        int32_t human_top = (!MultiplayerActive && (humans & (1u << top))) ? 1 : 0;
        int32_t satan = great_satan(top, 0) != 0;
        factor = CostRatioByDiff[DiffLevel] - satan - human_top;
    }
    int32_t multi = is_mineral ? Rules.mineral_cost_multi : Rules.nutrient_cost_multi;
    if (multi != 10) factor = multi * factor / 10;
    if (MapSizePlanet == 1) factor = factor * 9 / 10;
    else if (MapSizePlanet == 0) factor = factor * 8 / 10;
    if (!is_mineral) {
        int32_t growth = Factions[faction_id].SE_growth_pending;
        if (base_id >= 0) {
            if (base_fac(base_id, FAC_CHILDREN_CRECHE)) growth += 2;
            if (Bases[base_id].state_flags & BSTATE_GOLDEN_AGE_ACTIVE) growth += 2;
        }
        if (growth < -2) growth = -2;
        else if (growth > 5) growth = 5;
        return ((10 - growth) * factor + 9) / 10;
    }
    if (is_mineral != 1) return factor;
    switch (Factions[faction_id].SE_industry_pending) {
    case -7: case -6: case -5: case -4: case -3: return (factor * 13 + 9) / 10;
    case -2: return (factor * 6 + 4) / 5;
    case -1: return (factor * 11 + 9) / 10;
    case 0: return factor;
    case 1: return (factor * 9 + 9) / 10;
    case 2: return (factor * 4 + 4) / 5;
    case 3: return (factor * 7 + 9) / 10;
    case 4: return (factor * 3 + 4) / 5;
    default: return (factor + 1) / 2;
    }
}

int32_t GAMEAPI base_making(int32_t item_id, int32_t base_id) {
    int32_t strictness = Rules.retool_strictness;
    if (base_fac(base_id, FAC_SKUNKWORKS) && strictness >= 1) strictness = 1;
    int32_t current = Bases[base_id].queue_items[0];
    if (item_id < 0 && current < 0 && -current < 65 && base_fac(base_id, -current)) return -1;
    if (strictness == 0) return 0;
    if (strictness == 2) return item_id <= -70 ? -70 : item_id;
    if (strictness > 1) return item_id;
    if (item_id >= 0) return 0;
    if (item_id <= -70) return 1;
    return (item_id > -65) + 2;
}

int32_t GAMEAPI base_lose_minerals(int32_t base_id, int32_t) {
    if (!Rules.retool_penalty_prod_change) return 0;
    BASE& base = Bases[base_id];
    if (!((uint8_t)(1 << ((uint8_t)base.faction_id & 31)) & FactionStatus[0])) return 0;
    int32_t last = base_making(base.production_id_last, base_id);
    if (base_making(base.queue_items[0], base_id) == last) return 0;
    int32_t minerals = base.minerals_accumulated_2;
    int32_t exempt = Rules.retool_exemption;
    if (minerals <= exempt) return 0;
    return minerals - (minerals - exempt) * (100 - Rules.retool_penalty_prod_change) / 100 - exempt;
}

void GAMEAPI set_fac(int32_t facility_id, int32_t base_id, int32_t add) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    uint8_t& byte = base_raw(base_id)[offsetof(BASE, facilities_built) + offset];
    if (add) byte |= (uint8_t)mask;
    else byte &= (uint8_t)~mask;
}

int32_t GAMEAPI has_fac_announced(int32_t faction_id, int32_t facility_id) {
    if (facility_id > 70) return 1;
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Factions[faction_id].facility_announced[offset] & (uint8_t)mask) != 0;
}

void GAMEAPI set_fac_announced(int32_t faction_id, int32_t facility_id, int32_t add) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    uint8_t& byte = Factions[faction_id].facility_announced[offset];
    if (add) byte |= (uint8_t)mask;
    else byte &= (uint8_t)~mask;
}

int32_t GAMEAPI is_coast(int32_t x, int32_t y, int32_t is_base_radius) {
    int32_t limit = is_base_radius ? 21 : 9;
    for (int32_t i = 1; i < limit; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (map_in_bounds(x2, y2) && map_is_ocean(x2, y2)) return i;
    }
    return 0;
}

void GAMEAPI base_first(int32_t base_id) {
    BASE& base = Bases[base_id];
    int32_t best = -1, choice = 2;
    base.queue_items[0] = 2;
    for (int32_t unit_id = 0; unit_id < 512; unit_id++) {
        if (!veh_avail(unit_id, (uint8_t)base.faction_id, base_id)) continue;
        UNIT& u = Units[unit_id];
        if (unit_id >= 64 && !(u.unit_flags & 4)) continue;
        int32_t value = Armor[u.armor_id].defense_value << 5;
        if (u.plan == 2) value <<= 2;
        if (u.plan == 1) value *= 3;
        if (u.plan == 3) value *= 2;
        if (Chassis[u.chassis_id].triad) value /= 4;
        value -= u.cost;
        if (value >= best) {
            best = value;
            choice = unit_id;
        }
    }
    base.queue_items[0] = choice;
}

int32_t GAMEAPI base_init(int32_t faction_id, int32_t x, int32_t y) {
    if (BaseCount >= 512) return -1;
    int32_t region = map_tile(x, y)->region;
    bit_set(x, y, 1, 1);
    owner_set(x, y, faction_id);
    synch_bit(x, y, faction_id);
    int32_t base_id = BaseCount++;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    f.base_count++;
    f.region_total_bases[region]++;
    replay_base(0, x, y, faction_id);
    BASE& base = Bases[base_id];
    base.x = (int16_t)x;
    base.y = (int16_t)y;
    base.faction_id_former = (int8_t)faction_id;
    base.faction_id = (int8_t)faction_id;
    base.nerve_staple_turns_left = 0;
    base.assimilation_turns_left = 0;
    base.ai_plan_status = 0;
    base.pop_size = has_sp(FAC_PLANETARY_TRANSIT_SYSTEM - 70, faction_id) ? 3 : 1;
    MAP* sq = map_tile(x, y);
    base.visibility = sq->visibility | (uint8_t)(1 << faction_id);
    base.name[0] = 0;
    name_base(faction_id, base.name, 1, (sq->climate & 0xE0) < 0x60);
    base.state_flags = 0;
    base.event_flags = 0;
    base.governor_flags = f.base_governor_adv;
    base.minerals_accumulated = 0;
    base.nutrients_accumulated = 0;
    base.nerve_staple_count = 0;
    base.autoforward_land_base_id = -1;
    base.autoforward_sea_base_id = -1;
    base.autoforward_air_base_id = -1;
    base.queue_size = 0;
    base_first(base_id);
    if (CurrentTurn == 0) {
        int32_t item = has_tech(Units[1].preq_tech, faction_id) ? 1 : (f.diff_level > 1 ? 2 : 0);
        base.queue_items[0] = item;
        base.minerals_accumulated = cost_factor(faction_id, 1, -1) * Units[item].cost / 2;
    } else {
        base.minerals_accumulated = f.SE_support < -1 ? 0 : Rules.retool_exemption;
    }
    base.worked_tiles = 0;
    base.specialist_total = 0;
    base.specialist_adjust = 0;
    base.eco_damage = 0;
    base.unk_y = -1;
    base.unk_x = -1;
    std::memset(base.facilities_built, 0, 9);
    base.random_event_turns = 0;
    base.nerve_staple_count = 0;
    std::memset(&base.defend_goal, 0, 4);
    base.pad_8 = 0;
    if (f.base_count == 1) {
        fac_set_bit(base_id, FAC_HEADQUARTERS);
        if (MultiplayerActive) {
            base.pop_size = 3;
            if (m.rule_talent || f.SE_planet_base > 0) base.pop_size = 4;
            if (m.rule_drone || m.rule_population) base.pop_size--;
            fac_set_bit(base_id, FAC_RECYCLING_TANKS);
            if (DiffLevel >= 3) fac_set_bit(base_id, FAC_RECREATION_COMMONS);
            if (f.SE_economy_base > 0) fac_set_bit(base_id, FAC_ENERGY_BANK);
        } else {
            int32_t size = (CurrentTurn + 49) / 50;
            if (size < 1) size = 1;
            else if (size > 5) size = 5;
            base.pop_size = (int8_t)size;
            if (CurrentTurn >= 75) fac_set_bit(base_id, FAC_RECYCLING_TANKS);
            if (CurrentTurn >= 100) fac_set_bit(base_id, FAC_RECREATION_COMMONS);
            if (CurrentTurn >= 150) fac_set_bit(base_id, FAC_ENERGY_BANK);
        }
    }
    if ((map_tile(x, y)->climate & 0xE0) < 0x60) fac_set_bit(base_id, FAC_PRESSURE_DOME);
    for (int32_t i = 0; i < 8; i++) {
        if (i == faction_id || (base.visibility & (uint8_t)(1 << i))) base.factions_pop_size_intel[i] = base.pop_size;
        else base.factions_pop_size_intel[i] = 0;
    }
    // Faction bonuses: free facilities (FREEFAC 2) and facilities free with their tech (12).
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] != 2) continue;
        int32_t fac = m.faction_bonus_val1[i];
        if (fac < 1) fac = 1;
        else if (fac > 69) fac = 69;
        fac_set_bit(base_id, fac);
    }
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] != 12) continue;
        if (!has_tech(Facility[m.faction_bonus_val1[i]].preq_tech, faction_id)) continue;
        int32_t fac = m.faction_bonus_val1[i];
        if (fac < 1) fac = 1;
        else if (fac > 69) fac = 69;
        fac_set_bit(base_id, fac);
    }
    for (int32_t fac = 1; fac < 70; fac++)
        if (has_tech(Facility[fac].free_tech, faction_id)) fac_set_bit(base_id, fac);
    base.production_id_last = base.queue_items[0];
    base.minerals_accumulated_2 = base.minerals_accumulated;
    base.mineral_surplus_final = 0;
    base_mark(base_id);
    reset_territory();
    CurrentBaseID = base_id;
    CurrentBasePtr = kBasesAddr + (uint32_t)base_id * (uint32_t)sizeof(BASE);
    ComputeBaseID = base_id;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy_call();
    GameDrawState |= 2;
    return base_id;
}

void GAMEAPI base_kill(int32_t base_id) {
    BASE& base = Bases[base_id];
    int32_t x = base.x, y = base.y;
    int32_t faction_id = (uint8_t)base.faction_id;
    Faction& f = Factions[faction_id];
    if (base_fac(base_id, FAC_HEADQUARTERS) && CurrentTurn < f.corner_market_turn && f.corner_market_cost) {
        f.corner_market_turn = 0;
        f.corner_market_cost = 0;
        if (faction_id == CurrentPlayerFaction) {
            popp(ScriptFileName, "CORNERFOILED", 0, "econwin_sm.pcx", 0);
        } else {
            parse_says(0, MFactions[faction_id].adj_name_faction, -1, -1);
            popp(ScriptFileName, "CORNERTHEMFOILED", 0, "econwin_sm.pcx", 0);
        }
    }
    if (base.pop_size < 0) base.pop_size = 0;
    replay_base(2, x, y, faction_id);
    int32_t region = map_tile(x, y)->region;
    f.base_count--;
    f.region_total_bases[region]--;
    // Units supported by the base move to the nearest other base of their faction (or die);
    // foreign units lose their home.
    for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
        if (veh_id >= VehCount || Vehs[veh_id].home_base_id != base_id) continue;
        if (Vehs[veh_id].faction_id != faction_id) {
            Vehs[veh_id].home_base_id = -1;
            continue;
        }
        base.faction_id = (int8_t)0xFF;  // excluded from the search
        int32_t home = base_find_2(x, y, faction_id);
        base.faction_id = (int8_t)faction_id;
        if (home < 0) kill(veh_id);
        else Vehs[veh_id].home_base_id = (int16_t)home;
    }
    bit_set(x, y, 1, 0);
    if (base_id < BaseCount - 1)
        std::memcpy(&Bases[base_id], &Bases[base_id + 1], (BaseCount - base_id - 1) * sizeof(BASE));
    int32_t count = BaseCount - 1;
    if (base_id == ComputeBaseID) ComputeBaseID = -1;
    BaseCount = count;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++)
        if (Vehs[veh_id].home_base_id > base_id) Vehs[veh_id].home_base_id--;
    for (int32_t i = 0; i < count; i++) {
        int16_t* forward = &Bases[i].autoforward_land_base_id;
        for (int32_t k = 0; k < 3; k++) {
            if (forward[k] == base_id) forward[k] = -1;
            else if (forward[k] > base_id) forward[k]--;
        }
    }
    ui_messages_base_killed(base_id);
    for (int32_t i = 0; i < 64; i++) {
        if (SecretProjects[i] == base_id) SecretProjects[i] = -2;
        else if (SecretProjects[i] > base_id) SecretProjects[i]--;
    }
    for (int32_t i = 1; i < 8; i++) {
        if (Factions[i].base_id_attack_target == base_id) Factions[i].base_id_attack_target = -1;
        else if (Factions[i].base_id_attack_target > base_id) Factions[i].base_id_attack_target--;
    }
    for (int32_t i = 0; i < 49; i++) {
        int32_t x2 = wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
        site_set(x2, y2, 0);
        if (i >= 21) continue;
        bit_set(x2, y2, 0x1000, 0);
        if ((map_tile(x2, y2)->val3 & 7) == faction_id) using_set(x2, y2, 0);
    }
    if ((map_tile(x, y)->climate & 0xE0) < 0x60) {
        // Sea base: transports (plan 7) at the base tile release their passengers, then the units
        // that cannot stay (land units not held by a transport) die.
        for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
            if (Units[Vehs[veh_id].unit_id].plan != 7) continue;
            if (((std::abs(Vehs[veh_id].y - y) + wrap_dx(Vehs[veh_id].x, x)) & ~1) == 0) stack_veh(veh_id, 0);
        }
        for (int32_t veh_id = VehCount; veh_id >= 0; veh_id--) {
            VEH& v = Vehs[veh_id];
            if (Chassis[Units[v.unit_id].chassis_id].triad != 0) continue;
            if (((std::abs(v.y - y) + wrap_dx(v.x, x)) & ~1) == 0 && (v.order != 1 || v.waypoint_x[0] < 0))
                kill(veh_id);
        }
    } else {
        for (int32_t veh_id = VehCount; veh_id >= 0; veh_id--) {
            VEH& v = Vehs[veh_id];
            if (Chassis[Units[v.unit_id].chassis_id].triad != 1) continue;
            if (((std::abs(v.y - y) + wrap_dx(v.x, x)) & ~1) == 0) kill(veh_id);
        }
    }
    BaseWin_base_killed(&BaseWin, base_id);
    for (int32_t i = 0; i < BaseCount; i++) base_mark(i);
    reset_territory();
    GameDrawState |= 2;
}

void GAMEAPI base_change(int32_t base_id, int32_t item_id) {
    if (item_id == 99999) return;
    BASE& base = Bases[base_id];
    int32_t old = base.queue_items[0];
    base.queue_items[0] = item_id;
    if (item_id == old) return;
    int32_t faction_id = (uint8_t)base.faction_id;
    Faction& f = Factions[faction_id];
    if (old >= 0) f.units_queue[old]--;
    if (item_id >= 0) f.units_queue[item_id]++;
    int32_t penalty = 0;
    if (Rules.retool_penalty_prod_change && (FactionStatus[0] & (uint8_t)(1 << faction_id))) {
        int32_t last = base_making(base.production_id_last, base_id);
        if (base_making(base.queue_items[0], base_id) != last) {
            int32_t minerals = base.minerals_accumulated_2;
            int32_t exempt = Rules.retool_exemption;
            if (minerals > exempt)
                penalty = minerals - (minerals - exempt) * (100 - Rules.retool_penalty_prod_change) / 100 - exempt;
        }
    }
    base.minerals_accumulated = base.minerals_accumulated_2 - penalty;
    draw_radius(base.x, base.y, 2, 2);
    if (faction_id == CurrentPlayerFaction || SkipTechScreenB) return;
    // Announcements of secret projects begun or switched by other factions.
    int32_t current = base.queue_items[0];
    bool new_project = current <= -70 && SecretProjects[-current - 70] == -1;
    bool old_project = false;
    if (old <= -70 && (new_project || SecretProjects[-old - 70] == -1)) {
        old_project = true;
        int32_t owner_base = SecretProjects[std::abs(old) - 70];
        if (owner_base >= 0 && (uint8_t)Bases[owner_base].faction_id == faction_id) old_project = false;
    }
    if (!new_project && !old_project) return;
    if ((GameState & 1) && !(GameState & 8)) return;
    int32_t current_fac = std::abs(current);
    int32_t current_sp = current_fac - 70;
    int32_t offset, mask;
    bitmask(new_project ? current_sp : std::abs(old) - 70, &offset, &mask);
    int32_t others_old = 0, others_new = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id != base.faction_id || i == base_id) continue;
        if (Bases[i].queue_items[0] == old) others_old++;
        if (Bases[i].queue_items[0] == base.queue_items[0]) others_new++;
    }
    if (!new_project || others_new) return;
    MFaction& m = MFactions[faction_id];
    if (old_project && (!others_old || SecretProjects[-old - 70] != -1)) {
        // The project moves to this base: every base of the faction building the old one switches.
        for (int32_t i = 0; i < BaseCount; i++)
            if ((uint8_t)Bases[i].faction_id == faction_id && Bases[i].queue_items[0] == old)
                Bases[i].queue_items[0] = base.queue_items[0];
        if (SunspotDuration > 0) return;
        parse_says(0, m.adj_name_faction, -1, -1);
        parse_say(1, Facility[std::abs(old)].name, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        parse_say(3, Facility[current_fac].name, -1, -1);
        popp(ScriptFileName, "CHANGEPROJECT", 0, "secproj_sm.pcx", 0);
        f.secret_project_intel[offset] |= (uint32_t)mask;
        bitmask(std::abs(old) - 70, &offset, &mask);
        f.secret_project_intel[offset] &= ~(uint32_t)mask;
        return;
    }
    if (!(f.secret_project_intel[offset] & (uint32_t)mask)) {
        if (SunspotDuration > 0 && current_sp != 31) return;
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        parse_say(3, Facility[current_fac].name, -1, -1);
        wave_it((Factions[CurrentPlayerFaction].diplo_status[faction_id] & 1) ? 0x24 : 0x26);
        popp(ScriptFileName, "BEGINPROJECT", 0, "secproj_sm.pcx", 0);
        f.secret_project_intel[offset] |= (uint32_t)mask;
    }
    if (current_sp == 31) {  // The Voice of Planet
        interlude(11, 0, 1, 0);
        GameInterludeState |= 0x400;
    }
}

void GAMEAPI base_reset(int32_t base_id, int32_t has_gov) {
    BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    uint8_t bit = (uint8_t)(1 << faction_id);
    int32_t tries = 0;
    if ((bit & FactionStatus[0]) && (base.governor_flags & GOV_ACTIVE) && has_gov) {
        // Governor: one choice per AI priority, repeated (at most 8 times) while two coincide.
        int32_t flags = base.governor_flags;
        for (int32_t i = 0; i < 4; i++) {
            BasePriorityChoice[i] = 0;
            BasePriorityScore[i] = 999999;
        }
        if (flags & 0xF000000) {
            for (;;) {
                base.governor_flags = (base.governor_flags & 0xF1FFFFFF) | GOV_PRIORITY_EXPLORE;
                BasePriorityChoice[0] = base_build(base_id, nullptr, nullptr, &BasePriorityScore[0]);
                base.governor_flags = (base.governor_flags & 0xF2FFFFFF) | GOV_PRIORITY_DISCOVER;
                BasePriorityChoice[1] = base_build(base_id, nullptr, nullptr, &BasePriorityScore[1]);
                if (BasePriorityChoice[0] == BasePriorityChoice[1] && ++tries < 8) continue;
                base.governor_flags = (base.governor_flags & 0xF4FFFFFF) | GOV_PRIORITY_BUILD;
                BasePriorityChoice[2] = base_build(base_id, nullptr, nullptr, &BasePriorityScore[2]);
                if ((BasePriorityChoice[0] == BasePriorityChoice[2] || BasePriorityChoice[1] == BasePriorityChoice[2]) &&
                    ++tries < 8)
                    continue;
                base.governor_flags = (base.governor_flags & 0xF8FFFFFF) | GOV_PRIORITY_CONQUER;
                BasePriorityChoice[3] = base_build(base_id, nullptr, nullptr, &BasePriorityScore[3]);
                if ((BasePriorityChoice[0] == BasePriorityChoice[3] || BasePriorityChoice[1] == BasePriorityChoice[3] ||
                     BasePriorityChoice[2] == BasePriorityChoice[3]) &&
                    ++tries < 8)
                    continue;
                break;
            }
            base.governor_flags = flags;
        }
        base_change(base_id, base_build(base_id, nullptr, nullptr, &tries));
        return;
    }
    int32_t old = base.queue_items[0];
    base_change(base_id, base_build(base_id, nullptr, nullptr, nullptr));
    if (!(bit & FactionStatus[0]) || !(base.governor_flags & GOV_ACTIVE) || old <= -70) return;
    int32_t current = base.queue_items[0];
    if (current > -70 || MultiplayerActive || (GameState & 1)) return;
    // A human's governor chose a secret project: ask.
    int32_t fac = current < 0 ? -current : current;
    int32_t choice;
    for (;;) {
        parse_says(0, base.name, -1, -1);
        parse_say(1, Facility[fac].name, -1, -1);
        choice = popp(ScriptFileName, "GOVPROJECT", 0, "secproj_sm.pcx", 0);
        if (choice != 3) break;
        help_topic(11, fac);
    }
    if (!choice) return;
    base_change(base_id, base_build(-1 - base_id, nullptr, nullptr, nullptr));
    if (choice > 1) {
        cur_base().governor_flags &= ~GOV_MAY_PROD_SP;
        Factions[faction_id].base_governor_adv &= ~GOV_MAY_PROD_SP;
    }
}

void GAMEAPI bases_reset(int32_t region, int32_t faction_id, int32_t no_units) {
    uint8_t bit = (uint8_t)(1 << (faction_id & 31));
    if (bit & FactionStatus[0]) return;
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& base = Bases[i];
        if ((uint8_t)base.faction_id != (uint32_t)faction_id) continue;
        if (region >= 0 && map_tile(base.x, base.y)->region != region) continue;
        if (no_units) {
            MAP* sq = map_tile(base.x, base.y);
            if ((sq->items & 2) && (sq->val2 & 0xF) < 8) continue;
        }
        if ((bit & FactionStatus[0]) && (cur_base().state_flags & BSTATE_UNK_80000000)) continue;
        base_reset(i, 0);
    }
}

int32_t GAMEAPI morale_mod(int32_t base_id, int32_t faction_id, int32_t triad) {
    uint32_t bonus = 0;
    int32_t fac = -1, project = -1;
    if (triad == 0) {
        fac = FAC_COMMAND_CENTER;
        project = FAC_COMMAND_NEXUS - 70;
    } else if (triad == 1) {
        fac = FAC_NAVAL_YARD;
        project = FAC_MARITIME_CONTROL_CENTER - 70;
    } else if (triad == 2) {
        fac = FAC_AEROSPACE_COMPLEX;
        project = FAC_CLOUDBASE_ACADEMY - 70;
    }
    if (fac >= 0 && (base_fac(base_id, fac) || has_sp(project, faction_id))) bonus = 2;
    if (base_fac(base_id, FAC_BIOENHANCEMENT_CENTER) || has_sp(FAC_CYBORG_FACTORY - 70, faction_id)) bonus += 2;
    if (Factions[faction_id].SE_morale_pending < -1) bonus /= 2;
    return (int32_t)bonus;
}

int32_t GAMEAPI breed_mod(int32_t base_id, int32_t faction_id) {
    int32_t bonus = 0;
    if (has_sp(FAC_XENOEMPATHY_DOME - 70, faction_id)) bonus = 1;
    if (has_sp(FAC_PHOLUS_MUTAGEN - 70, faction_id)) bonus++;
    if (has_sp(FAC_VOICE_OF_PLANET - 70, faction_id)) bonus++;
    if (base_fac(base_id, FAC_CENTAURI_PRESERVE)) bonus++;
    if (base_fac(base_id, FAC_TEMPLE_OF_PLANET)) bonus++;
    if (base_fac(base_id, FAC_BIOLOGY_LAB)) bonus++;
    if (base_fac(base_id, FAC_BIOENHANCEMENT_CENTER) || has_sp(FAC_CYBORG_FACTORY - 70, faction_id)) bonus++;
    return bonus;
}

int32_t GAMEAPI worm_mod(int32_t base_id, int32_t faction_id) {
    int32_t bonus = 0;
    if (has_sp(FAC_XENOEMPATHY_DOME - 70, faction_id)) bonus = 1;
    if (has_sp(FAC_PHOLUS_MUTAGEN - 70, faction_id)) bonus++;
    if (has_sp(FAC_VOICE_OF_PLANET - 70, faction_id)) bonus++;
    if (has_fac(FAC_CENTAURI_PRESERVE, base_id, 0)) bonus++;
    if (has_fac(FAC_TEMPLE_OF_PLANET, base_id, 0)) bonus++;
    if (has_fac(FAC_BIOLOGY_LAB, base_id, 0)) bonus++;
    if (has_fac(FAC_BIOENHANCEMENT_CENTER, base_id, 0) || has_sp(FAC_CYBORG_FACTORY - 70, faction_id)) bonus++;
    if (MFactions[faction_id].rule_psi) bonus++;
    if (has_sp(FAC_DREAM_TWISTER - 70, faction_id)) bonus++;
    if (has_sp(FAC_NEURAL_AMPLIFIER - 70, faction_id)) bonus++;
    return bonus;
}

int32_t GAMEAPI has_fac(int32_t facility_id, int32_t base_id, int32_t queue_count) {
    if (facility_id >= 65) return 0;
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    if (base_raw(base_id)[offsetof(BASE, facilities_built) + offset] & (uint8_t)mask) return 1;
    if (!queue_count) return 0;
    for (int32_t i = 0; i < queue_count; i++)
        if (Bases[base_id].queue_items[i] == -facility_id) return 1;
    return 0;
}

// ---- tile yields --------------------------------------------------------------------------------

namespace {
// Base tile as the yield functions see it: the base bit with a unit owner nibble below 8.
bool tile_is_base(const MAP* sq) { return (sq->items & 1) && (sq->val2 & 0xF) < 8; }
// Fungus yield: the faction's fungus tech value lowered by a negative PLANET rating (at most -3).
int32_t fungus_yield(int32_t tech_value, int32_t planet) {
    int32_t adjust = planet < -3 ? -3 : (planet > 0 ? 0 : planet);
    int32_t value = tech_value + adjust;
    if (value < 0) value = 0;
    else if (value > 99) value = 99;
    return value;
}
}  // namespace

int32_t GAMEAPI crop_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag) {
    MAP* sq = map_tile(x, y);
    uint32_t items = sq->items;
    bool ocean = (sq->climate & 0xE0) < 0x60;
    bool is_base = tile_is_base(sq);
    bool bonus = bonus_at(x, y) == 1;
    uint32_t lm = sq->landmarks;
    // Jungle (land) or freshwater (sea) landmark.
    bool special = ((lm & 0x80000004) == 4 && !ocean) || ((lm & 0x80) && ocean);
    int32_t value;
    if (is_base) {
        value = ResInfo.base_sq.nutrient;
        if (bonus) value *= 2;
        if (special) value++;
        if (base_id < 0) return value;
        if (base_fac(base_id, FAC_RECYCLING_TANKS) || base_fac(base_id, FAC_PRESSURE_DOME))
            value += ResInfo.recycling_tanks.nutrient;
    } else if (items & BIT_THERMAL_BORE) {
        value = ResInfo.borehole_sq.nutrient;
        if (value) value += ResInfo.bonus_sq.nutrient;
    } else if (items & BIT_MONOLITH) {
        value = ResInfo.monolith_sq.nutrient;
        if (bonus) value += ResInfo.bonus_sq.nutrient;
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id) && Factions[faction_id].SE_planet_pending > 0) value++;
    } else if ((items & BIT_FUNGUS) && !flag && (sq->climate & 0xE0) >= 0x40) {
        int32_t planet = Factions[faction_id].SE_planet_pending;
        value = fungus_yield(Factions[faction_id].tech_fungus_nutrient, planet);
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id) && planet > 0) value++;
    } else {
        bool check_mine = true;
        if (ocean) {
            value = ResInfo.ocean_sq.nutrient;
            if (bonus) value += ResInfo.bonus_sq.nutrient;
            if (special) value++;
            if ((sq->climate & 0xE0) == 0x40 || ExpansionEnabled) {
                if (items & BIT_FARM) {
                    value += ResInfo.improved_sea.nutrient;
                    if (base_fac(base_id, FAC_AQUAFARM)) value++;
                }
            } else {
                check_mine = false;
            }
        } else if (items & BIT_FOREST) {
            value = (bonus ? ResInfo.bonus_sq.nutrient : 0) + ResInfo.forest_sq.nutrient;
            if (base_id >= 0) {
                if (base_fac(base_id, FAC_HYBRID_FOREST)) value++;
                if (base_fac(base_id, FAC_TREE_FARM)) value++;
            }
            if (special) value++;
            check_mine = false;
        } else {
            int32_t rocky = sq->val3 >> 6;
            value = (sq->climate & 0x18) >> 3;
            if (rocky > 1) value = 0;
            if (bonus) value += ResInfo.bonus_sq.nutrient;
            if (special) value++;
            if (value < 0) value = 0;
            if ((items & BIT_FARM) && rocky < 2) value += ResInfo.improved_land.nutrient;
        }
        if (check_mine && (items & BIT_MINE) && value > 1) value += Rules.nutrient_effect_mine_sq;
        if (items & BIT_SOIL_ENRICHER) value += value / 2;
        uint32_t condenser = items & BIT_CONDENSER;
        if (condenser) value += value / 2;
        if (value > 2 && !has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id) && !bonus && !condenser) {
            BaseTerraformReduce += value - 2;
            value = 2;
        }
    }
    if (base_id >= 0) {
        if (Bases[base_id].event_flags & BEVENT_BUMPER) value++;
        if ((Bases[base_id].event_flags & BEVENT_FAMINE) && value) value--;
    }
    return value;
}

int32_t GAMEAPI mine_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag) {
    MAP* sq = map_tile(x, y);
    int32_t lm_code = (int32_t)sq->landmarks >> 24;
    uint32_t items = sq->items;
    bool bonus = bonus_at(x, y) == 2;
    uint32_t lm = sq->landmarks;
    // Volcano, crater, fossil field landmarks (inner tiles), canyon.
    bool special = ((lm & 0x80000002) == 2 && lm_code < 9) || ((lm & 0x80000001) == 1 && lm_code < 9) ||
                   ((lm & 0x80008000) == 0x8000 && lm_code < 6) || (lm & 0x80000200) == 0x200;
    int32_t value = (bonus ? ResInfo.bonus_sq.mineral : 0) + (special ? 1 : 0);
    if (tile_is_base(sq)) {
        value += ResInfo.base_sq.mineral;
        if (base_id < 0) return value;
        if (base_fac(base_id, FAC_RECYCLING_TANKS) || base_fac(base_id, FAC_PRESSURE_DOME))
            value += ResInfo.recycling_tanks.mineral;
    } else if (items & BIT_MONOLITH) {
        value = ResInfo.monolith_sq.mineral;
        if (bonus) value += ResInfo.bonus_sq.mineral;
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id) && Factions[faction_id].SE_planet_pending > 1) value++;
    } else if ((items & BIT_FUNGUS) && !(items & BIT_THERMAL_BORE) && (sq->climate & 0xE0) >= 0x40) {
        int32_t planet = Factions[faction_id].SE_planet_pending;
        value = fungus_yield(Factions[faction_id].tech_fungus_mineral, planet);
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id) && planet > 1) value++;
    } else {
        if (items & BIT_THERMAL_BORE) {
            value = ResInfo.borehole_sq.mineral;
            if (bonus) value += ResInfo.bonus_sq.mineral;
        } else if ((sq->climate & 0xE0) < 0x60) {
            int32_t alt = sq->climate & 0xE0;
            value += ResInfo.ocean_sq.mineral;
            // The original reads the owner of Bases[base_id] (Bases[-1] for no base).
            int32_t owner = (uint8_t)base_raw(base_id)[offsetof(BASE, faction_id)];
            if ((MFactions[owner].rule_flags & 0x100) && alt == 0x40) value++;  // aquatic faction
            if ((alt == 0x40 || ExpansionEnabled) && ((items & BIT_MINE) || flag)) {
                value += ResInfo.improved_sea.mineral;
                if (has_tech(Rules.tech_preq_mining_platform_bonus, faction_id)) value++;
                if (base_raw(base_id)[offsetof(BASE, facilities_built) + (FAC_SUBSEA_TRUNKLINE >> 3)] &
                    (1 << (FAC_SUBSEA_TRUNKLINE & 7)))
                    value++;
            }
        } else {
            int32_t rocky = sq->val3 >> 6;
            if (items & BIT_FOREST) {
                value += ResInfo.forest_sq.mineral;
            } else if (!(items & (BIT_MINE | BIT_THERMAL_BORE)) && !flag) {
                if (rocky) value++;
            } else {
                value += rocky;
                int32_t mine = rocky ? rocky : 1;
                if (bonus || special) mine++;
                if (!(items & BIT_ROAD) && !flag && mine > Rules.limit_mineral_inc_for_mine_wo_road) {
                    BaseTerraformReduce += mine - Rules.limit_mineral_inc_for_mine_wo_road;
                    mine = Rules.limit_mineral_inc_for_mine_wo_road;
                }
                value += mine;
            }
        }
        if (value > 2 && !has_tech(Rules.tech_preq_allow_3_minerals_sq, faction_id) && !bonus) {
            BaseTerraformReduce += value - 2;
            value = 2;
        }
    }
    if (base_id >= 0) {
        if (Bases[base_id].event_flags & BEVENT_INDUSTRY) value++;
        if ((Bases[base_id].event_flags & BEVENT_BUST) && value) value--;
    }
    return value;
}

int32_t GAMEAPI energy_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag) {
    MAP* sq = map_tile(x, y);
    uint32_t items = sq->items;
    int32_t lm_code = (int32_t)sq->landmarks >> 24;
    bool ocean = (sq->climate & 0xE0) < 0x60;
    bool is_base = tile_is_base(sq);
    bool bonus = bonus_at(x, y) == 3;
    uint32_t lm = sq->landmarks;
    // Volcano (inner), uranium flats, geothermal, ridge landmarks.
    bool special = ((lm & 0x80000002) == 2 && lm_code < 9) || (lm & 0x80000008) == 8 ||
                   (lm & 0x80000400) == 0x400 || (lm & 0x80000800) == 0x800;
    Faction& f = Factions[faction_id];
    // The original tests the headquarters bit of Bases[base_id] without a range check.
    auto hq = [&]() {
        return (base_raw(base_id)[offsetof(BASE, facilities_built)] & (1 << FAC_HEADQUARTERS)) != 0;
    };
    int32_t value = 0;
    bool fungus = false;
    if (is_base) {
        value = ResInfo.base_sq.energy;
        if (hq()) value++;
        int32_t golden = 0;
        if (base_id >= 0) {
            if (base_fac(base_id, FAC_RECYCLING_TANKS) || base_fac(base_id, FAC_PRESSURE_DOME))
                value += ResInfo.recycling_tanks.energy;
            golden = (Bases[base_id].state_flags & BSTATE_GOLDEN_AGE_ACTIVE) ? 1 : 0;
        }
        int32_t economy = f.SE_economy_pending + golden;
        if (economy < 0) {
            if (!hq() || economy < -1 || f.base_count == 1) economy++;
        } else if (economy > 1) {
            if (economy > 4) economy = 4;
            economy = economy * 2 - 4;
        }
        value += economy;
        if (GovernorFaction == faction_id) value++;
    } else if (items & BIT_MONOLITH) {
        value = ResInfo.monolith_sq.energy;
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id)) {
            int32_t planet = f.SE_planet_pending;
            if (planet >= 0) value++;
            if (planet >= 3) value++;
        }
    } else if (items & BIT_THERMAL_BORE) {
        value = ResInfo.borehole_sq.energy;
    } else if ((items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) {
        int32_t planet = f.SE_planet_pending;
        value = fungus_yield(f.tech_fungus_energy, planet);
        if (has_sp(FAC_MANIFOLD_HARMONICS - 70, faction_id)) {
            if (planet >= 0) value++;
            if (planet >= 3) value++;
        }
        fungus = true;
    } else if (ocean) {
        if ((sq->climate & 0xE0) == 0x40 || ExpansionEnabled) {
            value = ResInfo.ocean_sq.energy;
            if ((items & BIT_SOLAR) || flag) {
                value += ResInfo.improved_sea.energy;
                if (base_fac(base_id, FAC_THERMOCLINE_TRANSDUCER)) value++;
            }
        }
        if (items & BIT_FOREST) value--;
    } else if (items & BIT_FOREST) {
        value = ResInfo.forest_sq.energy;
        if (base_id >= 0 && base_fac(base_id, FAC_HYBRID_FOREST)) value++;
    } else {
        int32_t alt_bonus = (sq->climate >> 5) - 2;
        if (alt_bonus < 0) alt_bonus = 0;
        value = ((items & (BIT_SOLAR | BIT_ECH_MIRROR | BIT_THERMAL_BORE)) || flag) ? alt_bonus : 0;
        if (items & BIT_SOLAR) {
            for (int32_t i = 0; i < 8; i++) {
                int32_t x2 = wrap_x(x + NearOffsetX[i]);
                int32_t y2 = y + NearOffsetY[i];
                if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
                MAP* sq2 = map_tile(x2, y2);
                if ((sq2->items & BIT_ECH_MIRROR) && sq2->owner == (uint8_t)cur_base().faction_id) value++;
            }
        }
    }
    if (!fungus) {
        if ((items & BIT_RIVER) && !ocean) value++;
        if (bonus) value += ResInfo.bonus_sq.energy;
        if (special) value++;
        int32_t golden = 0;
        if (base_id >= 0) {
            if (SecretProjects[FAC_MERCHANT_EXCHANGE - 70] == base_id) value++;
            golden = (Bases[base_id].state_flags & BSTATE_GOLDEN_AGE_ACTIVE) ? 1 : 0;
        }
        if (f.SE_economy_pending + golden >= 2) value++;
        if (is_base) {
            if (value < 0) value = 0;
            else if (value > 99) value = 99;
            BaseTerraformEnergy = value;
        } else if (value > 2 && !has_tech(Rules.tech_preq_allow_3_energy_sq, faction_id) && !bonus) {
            BaseTerraformReduce += value - 2;
            value = 2;
        }
    }
    if (base_id >= 0) {
        if (Bases[base_id].event_flags & BEVENT_HEAT_WAVE) value++;
        if ((Bases[base_id].event_flags & BEVENT_CLOUD_COVER) && value) value--;
    }
    if (DustCloudDuration && value) value--;
    int32_t flares = ControlUpkeepA ? (SolarFlaresEvent & 2) : (SolarFlaresEvent & 1);
    return flares ? value * 3 : value;
}

int32_t GAMEAPI resource_yield(int32_t type, int32_t faction_id, int32_t base_id, int32_t x, int32_t y) {
    switch (type) {
    case 0: return crop_yield(faction_id, base_id, x, y, 0);
    case 1: return mine_yield(faction_id, base_id, x, y, 0);
    case 2: return energy_yield(faction_id, base_id, x, y, 0);
    case 3: BaseTerraformReduce = 0; return 0;
    default: return 0;
    }
}

namespace {
// Shared body of the two unused radius-tile helpers: adds (sign 1) or removes (-1) the yields of
// base radius tile i of the current base to/from its intake (nutrient_intake_2...).
int32_t tile_intake_change(int32_t i, int32_t sign) {
    int32_t x = wrap_x(cur_base().x + TableOffsetX[i]);
    int32_t y = cur_base().y + TableOffsetY[i];
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return 0;
    if ((map_tile(x, y)->climate & 0xE0) < 0x60) cur_base().state_flags |= BSTATE_UNK_8000;
    int32_t total = 0;
    for (int32_t type = 0; type < 4; type++) {
        int32_t faction_id = (uint8_t)cur_base().faction_id;
        int32_t value;
        switch (type) {
        case 0: value = crop_yield(faction_id, CurrentBaseID, x, y, 0); break;
        case 1: value = mine_yield(faction_id, CurrentBaseID, x, y, 0); break;
        case 2: value = energy_yield(faction_id, CurrentBaseID, x, y, 0); break;
        default: value = 0; BaseTerraformReduce = 0; break;
        }
        (&cur_base().nutrient_intake_2)[type] += sign * value;
        total += value;
    }
    return total;
}
}  // namespace

// 0x4E7E50 (no callers): adds the yields of radius tile i to the current base's intake.
int32_t GAMEAPI base_unused_helper_1(int32_t i) { return tile_intake_change(i, 1); }
// 0x4E7F80 (no callers): removes the yields of radius tile i from the current base's intake.
int32_t GAMEAPI base_unused_helper_2(int32_t i) { return tile_intake_change(i, -1); }

// ---- base tiles and yields ----------------------------------------------------------------------

namespace {
// Specialist type nibbles of the current base (the first 16 specialists).
int32_t spec_type(int32_t i) {
    return (cur_base().specialist_types[i >> 3] >> ((i & 7) * 4)) & 0xF;
}
void spec_set(int32_t i, int32_t type) {
    int32_t shift = (i & 7) * 4;
    cur_base().specialist_types[i >> 3] &= ~(0xF << shift);
    cur_base().specialist_types[i >> 3] |= type << shift;
}
// Type of specialist i (beyond the stored 16: the best available one).
int32_t spec_type_or_best(int32_t i) { return i < 16 ? spec_type(i) : best_specialist(); }
// Adds (sign 1) or removes (-1) the yields of radius tile i to/from the current base's intake
// (marks the base as coastal when the tile is ocean). Returns false if the tile is off the map.
void tile_intake(int32_t i, int32_t sign) {
    int32_t x = wrap_x(cur_base().x + TableOffsetX[i]);
    int32_t y = cur_base().y + TableOffsetY[i];
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    if ((map_tile(x, y)->climate & 0xE0) < 0x60) cur_base().state_flags |= BSTATE_UNK_8000;
    for (int32_t type = 0; type < 4; type++) {
        int32_t value = resource_yield(type, (uint8_t)cur_base().faction_id, CurrentBaseID, x, y);
        (&cur_base().nutrient_intake_2)[type] += sign * value;
    }
}
}  // namespace

void GAMEAPI farm_compute() {
    int32_t base_x = cur_base().x, base_y = cur_base().y;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    for (int32_t i = 0; i < 25; i++) {
        if (i >= 21) {
            BaseTileFlags[i] = BR_NOT_AVAILABLE;
            continue;
        }
        int32_t x = wrap_x(base_x + TableOffsetX[i]);
        int32_t y = base_y + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) {
            BaseTileFlags[i] = BR_NOT_VISIBLE;
            continue;
        }
        if (BaseUpkeepState == 1 && goody_at(x, y) && (map_tile(x, y)->climate & 0xE0) >= 0x60) {
            add_goal(faction_id, 3, 1, x, y, -1);
            add_goal(faction_id, 2, 1, x, y, -1);
            add_goal(faction_id, 0, 1, x, y, -1);
        }
        MAP* sq = map_tile(x, y);
        if (!(Factions[faction_id].player_flags & 0x200) && !(sq->visibility & (uint8_t)(1 << faction_id))) {
            BaseTileFlags[i] = BR_NOT_VISIBLE;
            continue;
        }
        BaseTileFlags[i] = 0;
        if (tile_is_base(sq)) BaseTileFlags[i] = BR_BASE_IN_TILE;
        int32_t veh_id = stack_fix(veh_at(x, y));
        if (veh_id >= 0) {
            if (stack_check(veh_id, 9, 3, -1, -1)) BaseTileFlags[i] |= BR_VEH_IN_TILE;
            for (veh_id = stack_top(veh_id); veh_id >= 0; veh_id = Vehs[veh_id].next_veh_id_stack) {
                int32_t owner = Vehs[veh_id].faction_id;
                if (owner == faction_id) continue;
                if (!(Vehs[veh_id].visibility & (uint8_t)(1 << faction_id))) continue;
                if (!(Factions[faction_id].diplo_status[owner] & 3)) BaseTileFlags[i] |= BR_VEH_IN_TILE;
            }
        }
        int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
        if (owner >= 0 && owner != faction_id &&
            ((Factions[faction_id].diplo_status[owner] & 3) || veh_id < 0 || Vehs[veh_id].faction_id != faction_id))
            BaseTileFlags[i] |= BR_FOREIGN_TILE;
    }
    // Tiles worked by nearby bases (own bases; ocean tiles of other factions' bases).
    for (int32_t b = 0; b < BaseCount; b++) {
        if (b == CurrentBaseID) continue;
        BASE& other = Bases[b];
        if (base_dist(cur_base().x, cur_base().y, other) > 5) continue;
        for (int32_t k = 0; k < 21; k++) {
            if (k && !(other.worked_tiles & (1 << k))) continue;
            int32_t x = wrap_x(other.x + TableOffsetX[k]);
            int32_t y = other.y + TableOffsetY[k];
            if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
            if ((uint8_t)other.faction_id != faction_id && (map_tile(x, y)->climate & 0xE0) >= 0x60) continue;
            int32_t dx = wrap_dx(base_x, x);
            int32_t dy = std::abs(base_y - y);
            int32_t a = std::abs(dx), c = std::abs(dy);
            int32_t big = a > c ? a : c, small = a < c ? a : c;
            if (big - ((((a + c) >> 1) - small + 1) >> 1) > 2) continue;
            for (int32_t j = 0; j < 21; j++)
                if (wrap_x(base_x + TableOffsetX[j]) == x && base_y + TableOffsetY[j] == y)
                    BaseTileFlags[j] |= BR_WORKER_ACTIVE;
        }
    }
    int32_t reserved = 0;
    for (int32_t i = 0; i < 21; i++)
        if (BaseTileFlags[i]) reserved |= 1 << i;
    BaseReservedTiles = reserved;
}

void GAMEAPI base_yield() {
    farm_compute();
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    BaseWorkersLeft = cur_base().pop_size + 1;
    uint8_t bit = (uint8_t)(1 << faction_id);
    // Citizens are managed automatically for AI bases and governed human bases.
    int32_t gov = cur_base().governor_flags;
    bool automatic = !(bit & FactionStatus[0]) || ((gov & GOV_MANAGE_CITIZENS) && (gov & GOV_ACTIVE));
    uint32_t worked = 0;
    cur_base().state_flags &= ~BSTATE_UNK_8000;
    cur_base().specialist_adjust = 0;
    cur_base().worked_tiles &= ~BaseReservedTiles | 1;
    for (int32_t r = 0; r < 4; r++) {
        int32_t& intake = (&cur_base().nutrient_intake_2)[r];
        if (r < 3) {
            intake = (&Factions[faction_id].satellites_nutrient)[r];
            if (!base_fac(CurrentBaseID, FAC_AEROSPACE_COMPLEX) && !has_sp(FAC_SPACE_ELEVATOR - 70, faction_id) &&
                !has_sp(FAC_CLOUDBASE_ACADEMY - 70, faction_id))
                intake = (intake + 1) / 2;
            if (intake > cur_base().pop_size) intake = cur_base().pop_size;
        } else {
            intake = 0;
        }
    }
    bool done = false;
    bool keep = true;  // the current worked tiles are taken over first
    if (automatic) {
        if (ControlUpkeepA) {
            keep = false;
        } else {
            for (int32_t i = 0; i < cur_base().specialist_total; i++)
                if (!Citizen[spec_type_or_best(i)].psych_bonus) cur_base().specialist_adjust++;
            cur_base().specialist_total -= cur_base().specialist_adjust;
        }
    }
    if (keep && bit_count(cur_base().worked_tiles) <= BaseWorkersLeft) {
        for (int32_t i = 0; i < 21; i++) {
            if (i && !(cur_base().worked_tiles & (1 << i))) continue;
            worked |= 1 << i;
            tile_intake(i, 1);
            if (--BaseWorkersLeft == 0) break;
        }
        if (BaseWorkersLeft == cur_base().specialist_adjust + cur_base().specialist_total) done = true;
    }
    if (!done) {
        // Doctors (psych specialists) are kept; the others are released.
        if (automatic && cur_base().specialist_total && cur_base().specialist_adjust + cur_base().specialist_total > 0) {
            for (int32_t i = 0; i < cur_base().specialist_adjust + cur_base().specialist_total; i++) {
                if (Citizen[spec_type_or_best(i)].psych_bonus) continue;
                for (int32_t j = i; j < cur_base().specialist_adjust + cur_base().specialist_total; j++) {
                    int32_t type = spec_type_or_best(j + 1);
                    if (j < 16) spec_set(j, type);
                }
                if (cur_base().specialist_adjust) cur_base().specialist_adjust--;
            }
        }
        if (!(worked & 1)) {
            tile_intake(0, 1);
            worked |= 1;
            BaseWorkersLeft--;
        }
        BaseYieldFoodShort = 0;
        // Priorities: AI personality, or the governor's (human bases).
        int32_t growth, tech, wealth, power, fight;
        if ((FactionStatus[0] & bit) && (cur_base().governor_flags & GOV_ACTIVE)) {
            uint32_t flags = (uint32_t)cur_base().governor_flags;
            growth = (flags >> 24) & 1;
            tech = (flags >> 24) & 2;
            wealth = (flags >> 25) & 2;
            power = (flags >> 26) & 2;
            if (power) fight = 2;
            else if (growth || wealth) fight = 0;
            else if (tech) fight = -2;
            else fight = Factions[faction_id].AI_fight;
        } else if (FactionStatus[0] & bit) {
            growth = tech = wealth = power = fight = 0;
        } else {
            Faction& f = Factions[faction_id];
            growth = f.AI_growth;
            tech = f.AI_tech;
            wealth = f.AI_wealth;
            power = f.AI_power;
            fight = f.AI_fight;
        }
        for (;;) {
            bool saturated = BaseWorkersLeft <= cur_base().specialist_adjust + cur_base().specialist_total;
            int32_t best_idx = 0, best = 0, max_food = 0;
            uint32_t skip = saturated ? 0 : worked;
            int32_t worst = 9999, worst_idx = -1;
            for (int32_t i = 1; i < 21; i++) {
                uint32_t tile_bit = 1u << i;
                if (tile_bit & (skip | (uint32_t)BaseReservedTiles)) continue;
                int32_t x = wrap_x(cur_base().x + TableOffsetX[i]);
                int32_t y = cur_base().y + TableOffsetY[i];
                if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
                int32_t food = crop_yield((uint8_t)cur_base().faction_id, CurrentBaseID, x, y, 0);
                int32_t prod = mine_yield((uint8_t)cur_base().faction_id, CurrentBaseID, x, y, 0);
                int32_t energy = energy_yield((uint8_t)cur_base().faction_id, CurrentBaseID, x, y, 0);
                if (food > max_food) max_food = food;
                int32_t extra = 0;
                int32_t need = cur_base().pop_size * Rules.nutrient_intake_req_citizen;
                if (BaseWorkersLeft / Rules.nutrient_intake_req_citizen + cur_base().nutrient_intake_2 < need) {
                    extra = cur_base().nutrient_intake_2 + BaseWorkersLeft >= need ? 2 : 8;
                    if (food >= 0 && food <= extra) food *= 2;
                    else if (food >= 0) food += extra;
                } else {
                    BaseYieldFoodShort = 1;
                }
                int32_t pop = cur_base().pop_size;
                food *= 2;
                prod *= 2;
                energy *= 2;
                if (cur_base().nutrient_intake_2 <= pop + need && !food) {
                    prod /= 2;
                    energy /= 2;
                }
                if (!prod && !extra) food /= 2;
                int32_t div_food = (cur_base().nutrient_intake_2 - pop / 3 - need + BaseWorkersLeft) * 2 - 2;
                if (div_food < 1) div_food = 1;
                else if (div_food > 99) div_food = 99;
                int32_t score = (growth + 4) * food * 256 / div_food;
                int32_t k = cur_base().queue_items[0] < -70 ? 2 : -tech;
                int32_t div_prod = (cur_base().mineral_intake_2 - BaseForcesMaintCost + 1) / 2;
                if (div_prod < 1) div_prod = 1;
                else if (div_prod > 99) div_prod = 99;
                score += (k + fight + power + wealth + 6) * pop * prod * 64 / div_prod;
                int32_t pop_e = pop - power;
                if (pop_e < 1) pop_e = 1;
                else if (pop_e > 99) pop_e = 99;
                int32_t div_energy = cur_base().energy_intake_2;
                if (div_energy < 1) div_energy = 1;
                else if (div_energy > 99) div_energy = 99;
                score += (wealth + tech * 2 + 1) * pop_e * energy * 64 / div_energy;
                if (worked & tile_bit) {
                    if (score < worst) {
                        worst = score;
                        worst_idx = i;
                    }
                } else if (score > best) {
                    best = score;
                    best_idx = i;
                }
            }
            if (automatic && cur_base().pop_size >= 5 &&
                (cur_base().nutrient_intake_2 > cur_base().pop_size * Rules.nutrient_intake_req_citizen || !max_food) &&
                CurrentTurn > 100) {
                // Specialists compete with the tiles.
                for (int32_t t = 0; t < 7; t++) {
                    if (!has_tech(Citizen[t].preq_tech, faction_id) || has_tech(Citizen[t].obsol_tech, faction_id))
                        continue;
                    int32_t div = cur_base().energy_intake_2 + cur_base().specialist_adjust;
                    if (div < 1) div = 1;
                    else if (div > 99) div = 99;
                    int32_t pop = cur_base().pop_size;
                    int32_t score = Citizen[t].psych_bonus * 64 + (wealth + 2) * pop * Citizen[t].econ_bonus * 64 / div +
                                    (tech + 2) * pop * Citizen[t].labs_bonus * 64 / div;
                    if (score > best) {
                        best = score;
                        best_idx = -1 - t;
                    }
                }
            }
            if (saturated) {
                // Swap the worst worked tile for the best free one if that keeps the base growing.
                if (best_idx <= 0 || best <= worst || worst_idx <= 0) break;
                uint32_t worst_bit = 1u << worst_idx, best_bit = 1u << best_idx;
                worked = (worked & ~worst_bit) | best_bit;
                tile_intake(worst_idx, -1);
                tile_intake(best_idx, 1);
                if (cur_base().nutrient_intake_2 < (cur_base().pop_size + 1) * Rules.nutrient_intake_req_citizen) {
                    worked = (worked & ~best_bit) | worst_bit;
                    tile_intake(best_idx, -1);
                    tile_intake(worst_idx, 1);
                }
                break;
            }
            if (best_idx < 0) {
                int32_t n = cur_base().specialist_adjust;
                if (n < 16) spec_set(n, -1 - best_idx);
                cur_base().specialist_adjust++;
            } else {
                if (best_idx == 0) break;
                worked |= 1u << best_idx;
                tile_intake(best_idx, 1);
                BaseWorkersLeft--;
            }
            if (BaseWorkersLeft <= cur_base().specialist_adjust + cur_base().specialist_total &&
                cur_base().nutrient_intake_2 >= cur_base().pop_size * Rules.nutrient_intake_req_citizen)
                break;
        }
    }
    for (int32_t i = cur_base().specialist_total; i < BaseWorkersLeft; i++) {
        int32_t type = best_specialist();
        if (i < 16) spec_set(i, type);
    }
    cur_base().worked_tiles = (int32_t)worked;
    cur_base().specialist_total = BaseWorkersLeft;
    cur_base().state_flags &= ~BSTATE_UNK_100;
    for (int32_t r = 0; r < 4; r++) (&cur_base().nutrient_intake)[r] = (&cur_base().nutrient_intake_2)[r];
    cur_base().eco_damage = 0;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x = wrap_x(cur_base().x + TableOffsetX[i]);
        int32_t y = cur_base().y + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        uint32_t items = map_tile(x, y)->items;
        int32_t damage = bit_count(items & 0x1C8805C);
        if (cur_base().worked_tiles & (1 << i)) damage *= 2;
        if (items & BIT_THERMAL_BORE) damage += 8;
        if (items & BIT_ECH_MIRROR) damage += 6;
        if (items & BIT_CONDENSER) damage += 4;
        if (items & BIT_FOREST) damage--;
        if (base_fac(CurrentBaseID, FAC_TREE_FARM) && base_fac(CurrentBaseID, FAC_HYBRID_FOREST)) damage = 0;
        if (base_fac(CurrentBaseID, FAC_TREE_FARM) || base_fac(CurrentBaseID, FAC_HYBRID_FOREST)) damage /= 2;
        cur_base().eco_damage += damage;
    }
}

// ---- support, nutrients, minerals ---------------------------------------------------------------

void GAMEAPI base_support() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    for (int32_t i = 0; i < 4; i++) {
        BaseResourceConvoyTo[i] = 0;
        BaseResourceConvoyFrom[i] = 0;
    }
    BaseVehPacifismCount = 0;
    BaseForcesMaintCount = 0;
    BaseForcesSupported = 0;
    BaseForcesMaintCost = 0;
    // Units supported for free by SUPPORT rating -4..3.
    int32_t free_units[8];
    for (int32_t support = -4; support <= 3; support++) {
        int32_t n;
        if (support == 3) n = cur_base().pop_size > 4 ? cur_base().pop_size : 4;
        else if (support < -2) n = 0;
        else if (support == -2) n = 1;
        else n = support + 2;
        free_units[support + 4] = n;
    }
    // The original indexes the convoy arrays with the raw resource type byte.
    auto convoy_to = [](int32_t type) -> int32_t& { return (&BaseResourceConvoyTo[0])[type]; };
    auto convoy_from = [](int32_t type) -> int32_t& { return (&BaseResourceConvoyFrom[0])[type]; };
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& v = Vehs[veh_id];
        if (v.faction_id != faction_id) continue;
        if (v.home_base_id != CurrentBaseID) {
            // Supply convoys of other bases delivering here.
            if (Units[v.unit_id].plan == 10 && v.x == cur_base().x && v.y == cur_base().y && v.order == 3 &&
                v.home_base_id >= 0)
                convoy_to(v.order_auto_type)++;
            continue;
        }
        if (Units[v.unit_id].plan == 10 && !tile_is_base(map_tile(v.x, v.y)) && v.order == 3) {
            int32_t value;
            switch (v.order_auto_type) {
            case 0: value = crop_yield(faction_id, CurrentBaseID, v.x, v.y, 0); break;
            case 1: value = mine_yield(faction_id, CurrentBaseID, v.x, v.y, 0); break;
            case 2: value = energy_yield(faction_id, CurrentBaseID, v.x, v.y, 0); break;
            case 3: BaseTerraformReduce = 0; value = 0; break;
            default: value = 0; break;
            }
            convoy_to(v.order_auto_type) += value;
        }
        int32_t offense = Weapon[Units[v.unit_id].weapon_id].offense_value;
        int32_t plan = Units[v.unit_id].plan;
        if (offense && (offense > 0 || v.unit_id >= 64) && plan != 3 && plan != 5) {
            f.unk_46++;  // combat units
            if (plan == 0 || plan == 1) f.unk_46++;
        }
        v.state &= 0xFF9FFFEF;
        plan = Units[v.unit_id].plan;
        if (plan == 10 && v.order == 3 && tile_is_base(map_tile(v.x, v.y))) convoy_from(v.order_auto_type)++;
        if ((uint8_t)plan > 9) continue;
        if (!has_abil(v.unit_id, ABL_CLEAN_REACTOR)) {
            // Native life on fungus needs no support.
            MAP* sq = map_tile(v.x, v.y);
            bool native_free = v.unit_id < 64 && (Weapon[Units[v.unit_id].weapon_id].offense_value < 0 || v.unit_id == 15) &&
                               (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40;
            if (!native_free) {
                BaseForcesSupported++;
                int32_t support = f.SE_support_pending;
                int32_t index = support + 4;
                if (index < 0) index = 0;
                else if (index > 7) index = 7;
                if (BaseForcesSupported > free_units[index] && !has_abil(v.unit_id, ABL_CLEAN_REACTOR)) {
                    BaseForcesMaintCount++;
                    BaseForcesMaintCost += (support <= -4) + 1;
                    v.state |= 0x10;
                }
            }
            if (BaseUpkeepState == 1) {
                // AI estimate of the cost of each SUPPORT rating.
                for (int32_t support = -4; support <= 3; support++) {
                    if (BaseForcesSupported <= free_units[support + 4]) continue;
                    int32_t cost = has_abil(v.unit_id, ABL_CLEAN_REACTOR) ? 0 : (support <= -4) + 1;
                    int32_t minerals = cur_base().mineral_intake_2;
                    if (cost >= minerals) cost = cost + (cost - minerals / 2) * 2;
                    else if (cost > minerals / 2) cost = cost * 2 - minerals / 2;
                    f.social_support[support + 4] += cost;
                }
            }
        }
        if (!Weapon[Units[v.unit_id].weapon_id].offense_value) continue;
        // Units away from home territory (or air units) cause unhappiness under low POLICE.
        bool away = (!tile_is_base(map_tile(v.x, v.y)) && whose_territory(faction_id, v.x, v.y, nullptr, 0) != faction_id) ||
                    (Chassis[Units[v.unit_id].chassis_id].triad == 2 && Units[v.unit_id].plan != 4);
        if (!away) continue;
        BaseVehPacifismCount++;
        int32_t police = f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0);
        if (police == -3) {
            v.state |= BaseVehPacifismCount == 1 ? 0x400000 : 0x200000;
        } else if (f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0) < -3) {
            v.state |= 0x200000;
        }
    }
}

void GAMEAPI base_nutrient() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    BaseGrowthRate = Factions[faction_id].SE_growth_pending;
    if (base_fac(CurrentBaseID, FAC_CHILDREN_CRECHE)) BaseGrowthRate += 2;
    if (cur_base().state_flags & BSTATE_GOLDEN_AGE_ACTIVE) BaseGrowthRate += 2;
    cur_base().nutrient_intake_2 += BaseResourceConvoyTo[0];
    cur_base().nutrient_consumption = cur_base().pop_size * Rules.nutrient_intake_req_citizen + BaseResourceConvoyFrom[0];
    cur_base().nutrient_surplus = cur_base().nutrient_intake_2 - cur_base().nutrient_consumption;
    if (cur_base().nutrient_surplus >= 0) {
        if (cur_base().nutrients_accumulated < 0) cur_base().nutrients_accumulated = 0;
    } else if (!cur_base().nutrients_accumulated) {
        cur_base().nutrients_accumulated = -1;
    }
    if (BaseUpkeepState == 1) {
        int32_t surplus = cur_base().nutrient_surplus;
        if (surplus < 0) surplus = 0;
        else if (surplus > 99) surplus = 99;
        Factions[faction_id].nutrient_surplus_total += surplus;
    }
}

// base_energy reads a local it never writes for alien factions without trade partners (see
// base_energy below): the slot holds base_minerals' last local (its eco-damage divisor), as every
// caller runs base_minerals immediately before base_energy. Native builds pass the value through
// this variable; hybrid builds capture/write the real stack slot with thunks.
#ifndef SMACX_HYBRID
namespace {
int32_t g_minerals_residue = 0;  // base_minerals' [ebp-4] when it returns
}  // namespace
#endif

#ifdef SMACX_HYBRID
extern "C" {
int32_t base_minerals_residue_out;
int32_t base_energy_residue_in;
void __cdecl base_minerals_body();
void __cdecl base_energy_body();
}
// [esp-8] at entry is the callee's [ebp-4].
__attribute__((naked))
void GAMEAPI base_minerals() {
    __asm__("call _base_minerals_body\n mov _base_minerals_residue_out, %eax\n mov %eax, -8(%esp)\n ret\n");
}
__attribute__((naked))
void GAMEAPI base_energy() {
    __asm__("mov -8(%esp), %eax\n mov %eax, _base_energy_residue_in\n jmp _base_energy_body\n");
}
#define BASE_MINERALS_DEF extern "C" void __cdecl base_minerals_body()
#define BASE_ENERGY_DEF extern "C" void __cdecl base_energy_body()
#define MINERALS_RESIDUE_SET(v) (base_minerals_residue_out = (v))
#define ENERGY_RESIDUE_GET() (base_energy_residue_in)
#else
#define BASE_MINERALS_DEF void GAMEAPI base_minerals()
#define BASE_ENERGY_DEF void GAMEAPI base_energy()
#define MINERALS_RESIDUE_SET(v) (g_minerals_residue = (v))
#define ENERGY_RESIDUE_GET() (g_minerals_residue)
#endif

BASE_MINERALS_DEF {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    cur_base().mineral_intake_2 += BaseResourceConvoyTo[1];
    int32_t bonus = 0;
    if (has_fac(FAC_QUANTUM_CONVERTER, CurrentBaseID, 0) || has_sp(FAC_SINGULARITY_INDUCTOR - 70, faction_id)) bonus = 1;
    if (has_fac(FAC_ROBOTIC_ASSEMBLY_PLANT, CurrentBaseID, 0)) bonus++;
    if (has_fac(FAC_GENEJACK_FACTORY, CurrentBaseID, 0)) bonus++;
    if (has_fac(FAC_NANOREPLICATOR, CurrentBaseID, 0)) bonus++;
    if (has_sp(FAC_BULK_MATTER_TRANSMITTER - 70, faction_id)) bonus++;
    cur_base().mineral_intake_2 = (bonus + 2) * cur_base().mineral_intake_2 / 2;
    cur_base().mineral_consumption = BaseResourceConvoyFrom[1] + BaseForcesMaintCost;
    cur_base().mineral_surplus = cur_base().mineral_intake_2 - cur_base().mineral_consumption;
    cur_base().mineral_inefficiency = 0;
    cur_base().mineral_surplus -= cur_base().mineral_inefficiency;
    cur_base().mineral_surplus_final = cur_base().mineral_surplus;
    // Ecological damage: terraforming damage (from base_yield) plus mineral production.
    cur_base().eco_damage >>= 3;
    int32_t clean = f.clean_minerals_modifier + 16;
    if (cur_base().eco_damage > 0) {
        int32_t used = cur_base().eco_damage < clean ? cur_base().eco_damage : clean;
        clean -= used;
        cur_base().eco_damage -= used;
    }
    int32_t reduce = 1;
    if (has_fac(FAC_NANOREPLICATOR, CurrentBaseID, 0) || has_sp(FAC_SINGULARITY_INDUCTOR - 70, faction_id)) reduce = 2;
    if (base_fac(CurrentBaseID, FAC_CENTAURI_PRESERVE)) reduce++;
    if (base_fac(CurrentBaseID, FAC_TEMPLE_OF_PLANET)) reduce++;
    if (has_sp(FAC_PHOLUS_MUTAGEN - 70, faction_id)) reduce++;
    int32_t satellites = f.satellites_mineral;
    int32_t pop = cur_base().pop_size;
    int32_t orbital = (satellites < 0 || pop < 0) ? 0 : (satellites > pop ? pop : satellites);
    cur_base().eco_damage += (cur_base().mineral_intake_2 - orbital - clean) / reduce;
    uint8_t bit = (uint8_t)(1 << faction_id);
    if (FactionStatus[0] & bit) {
        // Human players are also punished for atrocities and tectonic payloads.
        for (int32_t pass = 0; pass < 2; pass++) {
            if (!(FactionStatus[0] & bit)) break;
            int32_t sea = MapSeaLevel;
            if (sea < 0) sea = 0;
            else if (sea > 100) sea = 100;
            int32_t rises = WorldBuilder.sea_level_rises;
            if (rises < 1) rises = 1;
            else if (rises > 100) rises = 100;
            int32_t div = sea / rises + reduce;
            int32_t count = pass == 0 ? f.major_atrocities : TectonicDetonationCount[faction_id];
            cur_base().eco_damage += count * 5 / div;
        }
    }
    if (cur_base().eco_damage < 0) cur_base().eco_damage = 0;
    if (SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1 && (GameRules & 0x800)) cur_base().eco_damage *= 2;
    if (GameState & 0x100000) cur_base().eco_damage *= 2;
    int32_t diff;
    if (FactionStatus[0] & bit) {
        diff = f.diff_level == 0 ? 2 : (f.diff_level <= 3 ? 3 : 5);
    } else {
        diff = 6 - DiffLevel;
        if (diff < 1) diff = 1;
        else if (diff > 3) diff = 3;
    }
    int32_t planet = f.SE_planet_pending;
    if (planet < -3) planet = -3;
    else if (planet > 2) planet = 2;
    cur_base().eco_damage = (f.tech_ranking - f.tech_count_transcendent) * (3 - planet) * (MapNativeLifeForms + 1) *
                            cur_base().eco_damage * diff / 6;
    cur_base().eco_damage = (cur_base().eco_damage + 50) / 100;
    if (has_sp(FAC_SPACE_ELEVATOR - 70, faction_id)) {
        int32_t item = cur_base().queue_items[0];
        if (item == -FAC_SKY_HYDRO_LAB || item == -FAC_NESSUS_MINING_STATION || item == -FAC_ORBITAL_POWER_TRANS ||
            item == -FAC_ORBITAL_DEFENSE_POD) {
            cur_base().mineral_intake_2 *= 2;
            cur_base().mineral_surplus = cur_base().mineral_intake_2 - cur_base().mineral_consumption;
        }
    }
    MINERALS_RESIDUE_SET(reduce);
}

// ---- psych, energy ------------------------------------------------------------------------------

int32_t GAMEAPI black_market(int32_t energy) {
    if (energy <= 0) return 0;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    // Distance to the faction's headquarters (16 without one).
    int32_t dist = 999;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != faction_id || !base_fac(i, FAC_HEADQUARTERS)) continue;
        int32_t d = base_dist(Bases[i].x, Bases[i].y, cur_base());
        if (d < dist) dist = d;
    }
    if (dist == 999) dist = 16;
    else if (!dist) return 0;
    Faction& f = Factions[faction_id];
    if (BaseUpkeepState == 1) {
        // AI estimate of the loss for each EFFICIENCY rating.
        for (int32_t k = 0; k <= 8; k++) {
            int32_t level = k, scale = -8 * k;
            if (base_fac(CurrentBaseID, FAC_CHILDREN_CRECHE)) {
                level = k - 2;
                scale += 16;
            }
            if (level >= 8) f.social_effic[k] += energy;
            else f.social_effic[k] += dist * energy / (scale + 64);
        }
    }
    int32_t level = 4 - f.SE_effic_pending;
    if (base_fac(CurrentBaseID, FAC_CHILDREN_CRECHE)) level -= 2;
    if (level >= 8) return energy;
    int32_t loss = dist * energy / ((8 - level) * 8);
    if (loss < 0) return 0;
    return loss > energy ? energy : loss;
}

void GAMEAPI psych_check(int32_t faction_id, int32_t* drones, int32_t* talents) {
    int32_t diff = (FactionStatus[0] & (uint8_t)(1 << faction_id)) ? Factions[faction_id].diff_level : 3;
    *drones = 6 - diff;
    int32_t effic = Factions[faction_id].SE_effic_pending;
    if (effic < 0) effic = 0;
    *talents = (*drones + 2) * (effic + 4) * MapAreaSqRoot / 56 / 2;
}

namespace {
// Clamps v to 0..max (0 when either is negative), as the original's range checks.
int32_t clamp_pop(int32_t v, int32_t max) {
    if (v < 0 || max < 0) return 0;
    return v > max ? max : v;
}
// Removes talents/drones until talents + drones + superdrones fit the working citizens.
void psych_balance() {
    for (;;) {
        BASE& b = cur_base();
        int32_t avail = b.pop_size - b.specialist_total;
        if (b.superdrone_total + b.talent_total + b.drone_total <= avail) return;
        if (b.talent_total) {
            b.talent_total--;
            if (b.superdrone_total) b.superdrone_total--;
            else if (b.drone_total) b.drone_total--;
        } else if (b.drone_total > avail) {
            b.drone_total--;
        } else {
            b.superdrone_total = clamp_pop(b.superdrone_total, b.drone_total);
            return;
        }
    }
}
void psych_clamp() {
    cur_base().drone_total = clamp_pop(cur_base().drone_total, cur_base().pop_size);
    cur_base().talent_total = clamp_pop(cur_base().talent_total, cur_base().pop_size);
}
void psych_row(int32_t row) {
    BasePsychTalents[row] = cur_base().talent_total;
    BasePsychNDrones[row] = cur_base().drone_total;
    BasePsychSDrones[row] = cur_base().superdrone_total;
}
// Police and native units that count as police (pass 0 also takes units with the police ability
// and, for RFLAG_WORMPOLICE factions, native land units).
bool psych_police_first(int32_t unit_id, int32_t faction_id) {
    if (has_abil(unit_id, ABL_POLICE_2X)) return true;
    return (MFactions[faction_id].rule_flags & RFLAG_WORMPOLICE) && unit_is_native(unit_id) &&
           Chassis[Units[unit_id].chassis_id].triad == 0;
}
}  // namespace

void GAMEAPI base_psych() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    bool human = (FactionStatus[0] & (uint8_t)(1 << faction_id)) != 0;
    int32_t diff = human ? f.diff_level : 3;
    int32_t base_drones = 6 - diff;
    // Extra drones for empire size.
    int32_t effic = f.SE_effic_pending;
    if (effic < 0) effic = 0;
    int32_t empire = (effic + 4) * (base_drones + 2) * MapAreaSqRoot / 56 / 2;
    if (empire) {
        int32_t pop = cur_base().pop_size;
        int32_t n = (f.base_count + CurrentBaseID % empire - empire) / empire;
        empire = clamp_pop(n, pop);
    }
    int32_t talents_rule = 0;
    int32_t diff2 = human ? f.diff_level : 3;
    int32_t pop = cur_base().pop_size;
    int32_t drones = clamp_pop(pop - base_drones, pop);
    int32_t extra = (pop + diff2 - 2) / 4;
    int32_t cap = (cur_base().assimilation_turns_left + 9) / 10;
    if (cap < 0 || extra < 0) extra = 0;
    else if (extra > cap) extra = cap;
    drones += extra;
    if (m.rule_drone) drones += pop / m.rule_drone;
    if (m.rule_talent) talents_rule = (m.rule_talent + pop - 1) / m.rule_talent;
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] != 14) continue;  // NODRONE
        drones -= m.faction_bonus_val1[i];
        if (drones < 0) drones = 0;
        break;
    }
    int32_t fac_drones = 0, fac_talents = 0;
    if (has_fac(FAC_GENEJACK_FACTORY, CurrentBaseID, 0)) fac_drones = Rules.drones_induced_genejack_factory;
    if (has_fac(FAC_RECREATION_COMMONS, CurrentBaseID, 0)) fac_drones -= 2;
    if ((has_fac(FAC_NETWORK_NODE, CurrentBaseID, 0) && has_sp(FAC_VIRTUAL_WORLD - 70, faction_id)) ||
        has_fac(FAC_HOLOGRAM_THEATRE, CurrentBaseID, 0))
        fac_drones -= 2;
    if (has_sp(FAC_PLANETARY_TRANSIT_SYSTEM - 70, faction_id) && cur_base().pop_size <= 3) fac_drones--;
    if (has_fac(FAC_PARADISE_GARDEN, CurrentBaseID, 0)) fac_talents = 2;
    if (base_fac(CurrentBaseID, FAC_RESEARCH_HOSPITAL)) fac_drones--;
    if (base_fac(CurrentBaseID, FAC_NANOHOSPITAL)) fac_drones--;
    int32_t project_talents = 0;
    if (has_sp(FAC_HUMAN_GENOME_PROJECT - 70, faction_id)) project_talents = 1;
    if (has_sp(FAC_CLINICAL_IMMORTALITY - 70, faction_id)) project_talents += 2;
    bool punishment = base_fac(CurrentBaseID, FAC_PUNISHMENT_SPHERE);
    // The real values come from (talent_set 0, police_set 0). With BaseUpkeepState 1 the AI also
    // evaluates every TALENT (-3..4) and POLICE (-5..3) rating into social_psych.
    for (int32_t talent_set = 8; talent_set >= 0; talent_set--) {
        int32_t talent;
        if (talent_set == 0) talent = f.SE_talent_pending;
        else if (BaseUpkeepState == 1) talent = talent_set - 4;
        else continue;
        for (int32_t police_set = 9; police_set >= 0; police_set--) {
            int32_t police;
            if (police_set == 0) {
                police = f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0);
                if (talent_set) continue;
            } else if (BaseUpkeepState == 1 && talent_set) {
                police = police_set - 6;
            } else {
                continue;
            }
            int32_t police_units = police + 2;
            if (police_units < 0) police_units = 0;
            else if (police_units > 1) police_units = (police_units > 4 ? 4 : police_units) - 1;
            BASE& b = cur_base();
            b.drone_total = drones;
            b.talent_total = talents_rule;
            if (talent < 0) b.drone_total -= talent;
            else b.talent_total += talent;
            b.drone_total = clamp_pop(b.drone_total, b.pop_size) + empire;
            b.superdrone_total = clamp_pop(b.drone_total - b.pop_size, b.pop_size);
            b.drone_total = clamp_pop(b.drone_total, b.pop_size);
            psych_balance();
            psych_row(0);
            b.talent_total += b.psych_total / 2;
            psych_clamp();
            psych_balance();
            psych_row(1);
            b.drone_total += fac_drones;
            b.talent_total += fac_talents;
            psych_clamp();
            psych_balance();
            psych_row(2);
            if (police_units > 0) {
                int32_t used = 0;
                if (has_sp(FAC_SELF_AWARE_COLONY - 70, faction_id)) {
                    used = 1;
                    if (b.drone_total) b.drone_total--;
                    if (police >= 3 && b.drone_total) b.drone_total--;
                }
                for (int32_t pass = 0; pass < 2 && used < police_units; pass++) {
                    for (int32_t veh_id = stack_fix(veh_at(b.x, b.y)); veh_id >= 0;
                         veh_id = Vehs[veh_id].next_veh_id_stack) {
                        int32_t unit_id = Vehs[veh_id].unit_id;
                        if ((uint8_t)Units[unit_id].plan > 3) continue;
                        if (psych_police_first(unit_id, faction_id) != (pass == 0)) continue;
                        if (b.drone_total) b.drone_total--;
                        if (has_abil(Vehs[veh_id].unit_id, ABL_POLICE_2X) && b.drone_total) b.drone_total--;
                        if (police >= 3 && b.drone_total) b.drone_total--;
                        unit_id = Vehs[veh_id].unit_id;
                        if ((m.rule_flags & RFLAG_WORMPOLICE) && unit_is_native(unit_id) &&
                            Chassis[Units[unit_id].chassis_id].triad == 0 && b.drone_total)
                            b.drone_total--;
                        if (++used >= police_units) break;
                    }
                }
            }
            if (police < -2) {
                if (police == -4) b.drone_total += BaseVehPacifismCount;
                else if (police == -3) {
                    if (BaseVehPacifismCount > 1) b.drone_total += BaseVehPacifismCount - 1;
                } else {
                    b.drone_total += BaseVehPacifismCount * 2;
                }
            }
            psych_clamp();
            psych_balance();
            psych_row(3);
            b.talent_total += project_talents;
            if (has_sp(FAC_LONGEVITY_VACCINE - 70, faction_id)) {
                if (f.SE_Economics_pending == 2) b.drone_total--;
                if (f.SE_Economics_pending != 1) b.drone_total--;
            }
            if (punishment || b.nerve_staple_turns_left) {
                b.drone_total = 0;
                b.talent_total = 0;
            }
            psych_clamp();
            psych_balance();
            psych_row(4);
            if (talent_set) {
                int32_t value = b.talent_total - b.drone_total;
                if (value > 0 && (b.drone_total || b.talent_total * 2 < b.pop_size)) value /= 2;
                if (value < 0) {
                    value -= b.pop_size;
                    if (b.pop_size < 6) value *= 2;
                }
                f.social_psych[talent_set - 1][police_set - 1] += value;
            }
        }
    }
}


BASE_ENERGY_DEF {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    cur_base().energy_intake_2 += BaseResourceConvoyTo[2];
    cur_base().energy_consumption = BaseResourceConvoyFrom[2];
    cur_base().energy_inefficiency = black_market(cur_base().energy_intake_2 - cur_base().energy_consumption);
    cur_base().energy_surplus =
        cur_base().energy_intake_2 - cur_base().energy_consumption - cur_base().energy_inefficiency;
    // Rank of the base among its faction's bases by energy (for trade), and its index.
    int32_t rank = 0, index = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != faction_id) continue;
        if ((Bases[i].energy_intake << 9) + i > (cur_base().energy_intake << 9) + CurrentBaseID) rank++;
        if (kBasesAddr + (uint32_t)i * (uint32_t)sizeof(BASE) == CurrentBasePtr) index = i;
    }
    // BUG(original): the alien bonus below starts from a local that only the commerce loop writes
    // (residue of base_minerals otherwise).
    int32_t value = ENERGY_RESIDUE_GET();
    for (int32_t i = 1; i < 8; i++) {
        BaseCommerceImport[i] = 0;
        BaseCommerceExport[i] = 0;
        if (i == faction_id) continue;
        if (!(f.diplo_status[i] & 2) && !(Factions[i].diplo_status[faction_id] & 2)) continue;
        if (!Factions[i].base_count) continue;
        int32_t partner = base_rank(i, rank);
        if (partner < 0) continue;
        value = (Bases[partner].energy_intake + cur_base().energy_intake + 7) / 8;
        if (ProposalPassCount[3] > ProposalPassCount[4]) value *= 2;  // global trade pact
        int32_t techs = TechCommerceCount + 1;
        int32_t exported = ((Factions[i].tech_commerce_bonus + 1) * value + techs / 2) / techs;
        value = ((f.tech_commerce_bonus + 1) * value + techs / 2) / techs;
        if (!(f.diplo_status[i] & 1)) {
            value /= 2;
            exported /= 2;
        }
        if (GovernorFaction == faction_id) value++;
        if (GovernorFaction == i) exported++;
        if (f.sanction_turns || Factions[i].sanction_turns || (m.rule_flags & RFLAG_ALIEN) ||
            (MFactions[i].rule_flags & RFLAG_ALIEN)) {
            exported = 0;
            value = 0;
        }
        BaseCommerceImport[i] = value;
        BaseCommerceExport[i] = exported;
        cur_base().energy_intake_2 += value;
    }
    if (ExpansionEnabled && (m.rule_flags & RFLAG_ALIEN)) {
        // Aliens: energy from facilities (and 5 per secret project) of the base.
        for (int32_t fac = 0; fac < 70; fac++)
            if (cur_base().facilities_built[fac / 8] & (1 << (fac % 8))) value++;
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] == index) value += 5;
        value /= 2;
        if (f.sanction_turns) value = 0;
        cur_base().energy_intake_2 += value;
    }
    if (BaseUpkeepState == 1) {
        int32_t surplus = cur_base().energy_surplus;
        if (surplus < 0) surplus = 0;
        else if (surplus > 99999) surplus = 99999;
        f.energy_surplus_total += surplus;
    }
    cur_base().energy_inefficiency = black_market(cur_base().energy_intake_2 - cur_base().energy_consumption);
    cur_base().energy_surplus =
        cur_base().energy_intake_2 - cur_base().energy_consumption - cur_base().energy_inefficiency;
    // Allocation to psych, economy, labs.
    int32_t energy = cur_base().energy_surplus;
    if (energy < 0) energy = 0;
    else if (energy > 9999) energy = 9999;
    cur_base().psych_total = clamp_pop((f.SE_alloc_psych * energy + 4) / 10, energy);
    int32_t rest = energy - cur_base().psych_total;
    int32_t economy = ((10 - f.SE_alloc_labs - f.SE_alloc_psych) * energy + 4) / 10;
    cur_base().economy_total = (economy < 0 || rest < 0) ? 0 : (economy > rest ? rest : economy);
    cur_base().labs_total = energy - cur_base().psych_total - cur_base().economy_total;
    rest = energy - cur_base().psych_total;
    int32_t unk = ((9 - f.SE_alloc_labs - f.SE_alloc_psych) * energy + 4) / 10;
    cur_base().unk_total = (unk < 0 || rest < 0) ? 0 : (unk > rest ? rest : unk);
    // Specialists (obsolete types are upgraded to an available type at least as good).
    for (int32_t i = 0; i < cur_base().specialist_total; i++) {
        int32_t type = spec_type_or_best(i);
        if (type < 0) type = 0;
        else if (type > 6) type = 6;
        if (has_tech(Citizen[type].obsol_tech, faction_id)) {
            for (int32_t k = 0; k < 7; k++) {
                if (!has_tech(Citizen[k].preq_tech, faction_id) || has_tech(Citizen[k].obsol_tech, faction_id))
                    continue;
                if (Citizen[k].econ_bonus < Citizen[type].econ_bonus || Citizen[k].psych_bonus < Citizen[type].psych_bonus ||
                    Citizen[k].labs_bonus < Citizen[type].labs_bonus)
                    continue;
                type = k;
                if (i < 16) spec_set(i, k);
                break;
            }
        }
        cur_base().economy_total += Citizen[type].econ_bonus;
        cur_base().psych_total += Citizen[type].psych_bonus;
        cur_base().labs_total += Citizen[type].labs_bonus;
    }
    cur_base().labs_total += m.rule_research;
    if (base_fac(CurrentBaseID, FAC_BIOLOGY_LAB)) cur_base().labs_total += 2;
    int32_t econ_mult = 4, labs_mult = 4, psych_mult = 4;
    if (base_fac(CurrentBaseID, FAC_ENERGY_BANK) || has_sp(FAC_PLANETARY_ENERGY_GRID - 70, faction_id)) econ_mult = 6;
    if (base_fac(CurrentBaseID, FAC_NETWORK_NODE)) labs_mult = 6;
    if (base_fac(CurrentBaseID, FAC_HOLOGRAM_THEATRE) ||
        (base_fac(CurrentBaseID, FAC_NETWORK_NODE) && has_sp(FAC_VIRTUAL_WORLD - 70, faction_id)))
        psych_mult = 6;
    if (base_fac(CurrentBaseID, FAC_RESEARCH_HOSPITAL)) {
        psych_mult++;
        labs_mult += 2;
    }
    if (base_fac(CurrentBaseID, FAC_NANOHOSPITAL)) {
        psych_mult++;
        labs_mult += 2;
    }
    if (base_fac(CurrentBaseID, FAC_TREE_FARM)) {
        psych_mult += 2;
        econ_mult += 2;
    }
    if (base_fac(CurrentBaseID, FAC_HYBRID_FOREST)) {
        psych_mult += 2;
        econ_mult += 2;
    }
    if (base_fac(CurrentBaseID, FAC_FUSION_LAB)) {
        econ_mult += 2;
        labs_mult += 2;
    }
    if (base_fac(CurrentBaseID, FAC_QUANTUM_LAB)) {
        econ_mult += 2;
        labs_mult += 2;
    }
    cur_base().psych_total = (cur_base().psych_total * psych_mult + 3) / 4;
    cur_base().labs_total = (cur_base().labs_total * labs_mult + 3) / 4;
    if (SecretProjects[FAC_SUPERCOLLIDER - 70] == CurrentBaseID) cur_base().labs_total *= 2;
    if (SecretProjects[FAC_THEORY_OF_EVERYTHING - 70] == CurrentBaseID) cur_base().labs_total *= 2;
    cur_base().economy_total = (cur_base().economy_total * econ_mult + 3) / 4;
    if (SecretProjects[FAC_SPACE_ELEVATOR - 70] == CurrentBaseID) cur_base().economy_total *= 2;
    if (SecretProjects[FAC_LONGEVITY_VACCINE - 70] == CurrentBaseID && f.SE_Economics_pending == 1)
        cur_base().economy_total += cur_base().economy_total / 2;
    cur_base().unk_total = (cur_base().unk_total * econ_mult + 3) / 4;
    if (SecretProjects[FAC_NETWORK_BACKBONE - 70] == CurrentBaseID) {
        for (int32_t i = 0; i < BaseCount; i++)
            if (base_fac(i, FAC_NETWORK_NODE)) cur_base().labs_total++;
        if (m.rule_flags & RFLAG_ALIEN) cur_base().labs_total += value;
        for (int32_t i = 1; i < 8; i++)
            if (!f.sanction_turns && !Factions[i].sanction_turns && !(m.rule_flags & RFLAG_ALIEN) &&
                !(MFactions[i].rule_flags & RFLAG_ALIEN))
                cur_base().labs_total += BaseCommerceImport[i];
    }
    // Inefficiency of unbalanced allocations under low EFFICIENCY.
    int32_t labs_alloc = f.SE_alloc_labs, psych_alloc = f.SE_alloc_psych;
    int32_t imbalance = psych_alloc + labs_alloc * 2 - 10;
    if (imbalance < 0) imbalance = (5 - labs_alloc) * 2 - psych_alloc;
    imbalance /= 2;
    int32_t effic = f.SE_effic_pending;
    if (effic < -4) effic = -4;
    else if (effic > 4) effic = 4;
    int32_t penalty = (effic - 4 >= 0 ? effic - 4 : 4 - effic) * 2;
    if (imbalance && penalty) {
        int32_t pct = penalty * imbalance;
        int32_t small = pct < 0 ? 0 : (pct > 40 ? 40 : pct);
        int32_t large = pct * 2 < 0 ? 0 : (pct * 2 > 80 ? 80 : pct * 2);
        if (labs_alloc > 10 - labs_alloc - psych_alloc) {
            cur_base().labs_total = ((100 - small) * cur_base().labs_total + 50) / 100;
            cur_base().economy_total = ((100 - large) * cur_base().economy_total + 50) / 100;
        } else {
            cur_base().labs_total = ((100 - large) * cur_base().labs_total + 50) / 100;
            cur_base().economy_total = ((100 - small) * cur_base().economy_total + 50) / 100;
        }
    }
    // Stockpile energy.
    if (cur_base().queue_items[0] == -FAC_STOCKPILE_ENERGY && cur_base().mineral_surplus > 0 &&
        cur_base().production_id_last == -FAC_STOCKPILE_ENERGY) {
        int32_t minerals = cur_base().mineral_surplus;
        int32_t grid = SecretProjects[FAC_PLANETARY_ENERGY_GRID - 70];
        if (grid >= 0 && Bases[grid].faction_id == cur_base().faction_id)
            cur_base().economy_total += ((minerals + 1) / 2 * 5 + 3) / 4;
        else
            cur_base().economy_total += (minerals + 1) / 2;
    }
    base_psych();
}

void GAMEAPI base_compute(int32_t force) {
    if (!force && ComputeBaseID == CurrentBaseID) return;
    ComputeBaseID = CurrentBaseID;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy_call();
}

int32_t GAMEAPI base_rank(int32_t faction_id, int32_t position) {
    // One frame block as in the original (values below ids): a negative position reads values[]
    // (BUG(original); never passed by the game). Zeroed like the oracle's poisoned stack.
    int32_t block[1024] = {};
    int32_t* values = block;
    int32_t* ids = block + 512;
    int32_t count = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != faction_id) continue;
        ids[count] = i;
        values[count] = (Bases[i].energy_intake << 9) + i;
        count++;
    }
    if (position >= count) return -1;
    for (bool swapped = true; swapped;) {
        swapped = false;
        for (int32_t i = 0; i < count - 1; i++) {
            if (values[i] >= values[i + 1]) continue;
            swapped = true;
            int32_t t = values[i];
            values[i] = values[i + 1];
            values[i + 1] = t;
            t = ids[i];
            ids[i] = ids[i + 1];
            ids[i + 1] = t;
        }
    }
    return ids[position];
}

// ---- small helpers of the upkeep ----------------------------------------------------------------

int32_t GAMEAPI crappy(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    int32_t rain = sq->climate & 0x18;
    int32_t value = rain ? 0 : 1;
    int32_t rocky = sq->val3 & 0xC0;
    if (rocky == 0x80) value++;
    if (!rocky && rain < 0x10) value++;
    return value;
}

int32_t GAMEAPI pop_goal_fac(int32_t base_id) {
    BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    int32_t bonus = has_sp(FAC_ASCETIC_VIRTUES - 70, faction_id) ? 2 : 0;
    int32_t size = base.pop_size - bonus + MFactions[faction_id].rule_population;
    if (size >= Rules.pop_limit_wo_hab_complex && !base_fac(base_id, FAC_HAB_COMPLEX)) return FAC_HAB_COMPLEX;
    size = base.pop_size - bonus + MFactions[faction_id].rule_population;
    if (size >= Rules.pop_limit_wo_hab_dome && !base_fac(base_id, FAC_HABITATION_DOME)) return FAC_HABITATION_DOME;
    return 0;
}

int32_t GAMEAPI pop_goal(int32_t base_id) {
    BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    int32_t bonus = has_sp(FAC_ASCETIC_VIRTUES - 70, faction_id) ? 2 : 0;
    int32_t pop = base.pop_size;
    int32_t goal = (36 - pop) / 6 + pop;
    if (goal <= 6) goal = 6;
    if (!base_fac(base_id, FAC_HAB_COMPLEX)) {
        int32_t limit = Rules.pop_limit_wo_hab_complex - MFactions[faction_id].rule_population + bonus;
        if (goal >= limit) goal = limit;
    }
    if (!base_fac(base_id, FAC_HABITATION_DOME)) {
        int32_t limit = Rules.pop_limit_wo_hab_dome - MFactions[faction_id].rule_population + bonus;
        if (goal >= limit) return limit;
    }
    return goal;
}

int32_t GAMEAPI base_queue(int32_t base_id) {
    BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    if (!base.queue_size) return 0;
    for (;;) {
        if (base.queue_items[0] >= 0) Factions[faction_id].units_queue[base.queue_items[0]]--;
        // BUG(original): with a full queue the shift reads the word after queue_items (worked_tiles).
        int32_t* items = base.queue_items;
        for (int32_t i = 0; i < base.queue_size; i++) items[i] = items[i + 1];
        base.queue_size--;
        int32_t item = base.queue_items[0];
        bool ok = item >= 0 ? veh_avail(item, faction_id, base_id) != 0
                            : facility_avail(-item, faction_id, base_id, 0) != 0;
        if (ok) break;
        if (!base.queue_size) return 0;
    }
    if (base.queue_items[0] <= -70) wave_it(0x24);
    draw_radius(base.x, base.y, 2, 2);
    return 1;
}

void GAMEAPI base_energy_costs() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    if (cur_base().energy_surplus >= 0) return;
    // Energy convoys stop when the base runs an energy deficit.
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& v = Vehs[veh_id];
        if (v.faction_id != faction_id || Units[v.unit_id].plan != 10 || v.order != 3 || v.order_auto_type != 2)
            continue;
        if (tile_is_base(map_tile(v.x, v.y))) v.order = 0;
    }
}

void GAMEAPI base_research() {
    if (GameRules & 0x4000000) return;  // scenario: no tech advances
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    int32_t labs = cur_base().labs_total;
    if (base_fac(CurrentBaseID, FAC_PUNISHMENT_SPHERE)) labs /= 2;
    if (CurrentTurn < -5 * f.SE_research_pending) labs = 0;
    if (CurrentTurn < 5 && f.diff_level < 2) labs = 0;
    f.labs_total += labs;
    int32_t research = f.SE_research_pending;
    if (research < -5) research = -5;
    else if (research > 5) research = 5;
    int32_t points = (research + 10) * labs * 10;
    f.net_random_event += points % 100;  // fractional research carried over
    if (f.net_random_event >= 100) {
        f.net_random_event -= 100;
        points += 100;
    }
    tech_research(faction_id, points / 100);
}

int32_t GAMEAPI fac_maint(int32_t facility_id, int32_t faction_id) {
    if (facility_id == FAC_COMMAND_CENTER) {
        int32_t cap = (Factions[faction_id].diff_level + 1) / 2;
        int32_t reactor = best_reactor(faction_id);
        if (reactor < 0 || cap < 0) return 0;
        return reactor > cap ? cap : reactor;
    }
    MFaction& m = MFactions[faction_id];
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] == 2 ||
            (m.faction_bonus_id[i] == 12 && has_tech(Facility[m.faction_bonus_val1[i]].preq_tech, faction_id))) {
            if (m.faction_bonus_val1[i] == facility_id) return 0;
        }
    }
    return Facility[facility_id].maint;
}

void GAMEAPI base_maint() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    for (int32_t fac = 1; fac < 65; fac++) {
        if (!base_fac(CurrentBaseID, fac)) continue;
        int32_t maint = fac_maint(fac, faction_id);
        if (has_sp(FAC_SELF_AWARE_COLONY - 70, faction_id)) {
            // Half maintenance; the odd halves alternate through player flag 0x20.
            maint += (f.player_flags >> 5) & 1;
            if (maint & 1) f.player_flags |= 0x20;
            else f.player_flags &= ~0x20u;
            maint /= 2;
        }
        f.energy_credits -= maint;
        f.facility_maint_total += maint;
        if (f.energy_credits >= 0) continue;
        if (f.diff_level <= 1 || cur_base().queue_items[0] == -fac) {
            f.energy_credits = 0;
            continue;
        }
        // Out of energy: the facility is sold.
        int32_t offset, mask;
        bitmask(fac, &offset, &mask);
        Bases[CurrentBaseID].facilities_built[offset] &= (uint8_t)~mask;
        f.energy_credits = cost_factor((uint8_t)cur_base().faction_id, 1, -1) * Facility[fac].cost;
        parse_say(1, Facility[fac].name, -1, -1);
        popb("POWERSHORT", 0x10000, 14, "genwarning_sm.pcx", 0);
    }
}

void GAMEAPI make_base_unique(int32_t base_id) {
    int32_t same = 0;
    for (int32_t i = 0; i < BaseCount; i++)
        if (i != base_id && !std::strcmp(Bases[i].name, Bases[base_id].name)) same++;
    if (!same) return;
    char* name = Bases[base_id].name;
    name[21] = 0;
    std::strcat(name, "-");
    char digits[80];
    crt_itoa(same + 1, digits, 10);
    std::strcat(name, digits);
}

int32_t GAMEAPI upgrade_cost(int32_t faction_id, int32_t new_unit_id, int32_t old_unit_id) {
    UNIT& n = Units[new_unit_id];
    UNIT& o = Units[old_unit_id];
    int32_t cost = n.cost;
    if (new_unit_id >= 64 && !(n.unit_flags & 4)) {  // prototype not built yet
        int32_t c = cost + (cost + 1) / 2;
        cost = c * (c + 1);
    }
    int32_t weapon = Weapon[n.weapon_id].cost - Weapon[o.weapon_id].cost;
    if (weapon < 0) weapon = 0;
    int32_t armor = Armor[n.armor_id].cost - Armor[o.armor_id].cost;
    if (armor < 0) armor = 0;
    int32_t speed = Chassis[n.chassis_id].speed - Chassis[o.chassis_id].speed;
    if (speed < 0) speed = 0;
    cost += speed + weapon + armor;
    if (has_sp(FAC_NANO_FACTORY - 70, faction_id)) cost /= 2;
    return cost;
}

// ---- growth ---------------------------------------------------------------------------------------

namespace {
// The compute sequence the upkeep functions inline (base_compute(1) without the call).
void base_compute_inline() {
    ComputeBaseID = CurrentBaseID;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy_call();
}
}  // namespace

int32_t GAMEAPI base_growth() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    int32_t x = cur_base().x, y = cur_base().y;
    bool alien = (MFactions[faction_id].rule_flags & RFLAG_ALIEN) != 0;
    if (cur_base().nutrients_accumulated < 0) {
        // Starvation.
        if (!(GameState & 1) || (GameState & 8)) {
            if (BaseResourceConvoyFrom[0]) {
                for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
                    VEH& v = Vehs[veh_id];
                    if (v.home_base_id != CurrentBaseID || Units[v.unit_id].plan != 10 || v.order != 3 ||
                        v.order_auto_type != 0)
                        continue;
                    if (tile_is_base(map_tile(v.x, v.y))) v.order = 0;
                }
            }
            if (alien) popb("STARVE", 0x4000, 14, "al_starv_sm.pcx", 0);
            else popb("STARVE", 0x4000, 14, "starv_sm.pcx", 0);
            if (!is_objective(CurrentBaseID) || cur_base().pop_size > 1) {
                cur_base().pop_size--;
                if (cur_base().pop_size <= 0) {
                    base_kill(CurrentBaseID);
                    draw_map(1);
                    eliminate_player(faction_id, 0);
                    return 1;
                }
            }
        }
        cur_base().nutrients_accumulated = cur_base().nutrient_surplus;
        draw_tile(x, y, 2);
        return 0;
    }
    int32_t box = cost_factor(faction_id, 0, CurrentBaseID) * (cur_base().pop_size + 1);
    auto hab_limit = [&](int32_t base_id) -> int32_t {
        BASE& base = Bases[base_id];
        int32_t owner = (uint8_t)base.faction_id;
        int32_t bonus = has_sp(FAC_ASCETIC_VIRTUES - 70, owner) ? 2 : 0;
        int32_t size = base.pop_size - bonus + MFactions[owner].rule_population;
        if (size >= Rules.pop_limit_wo_hab_complex && !has_fac(FAC_HAB_COMPLEX, base_id, 0)) return FAC_HAB_COMPLEX;
        size = base.pop_size - bonus + MFactions[owner].rule_population;
        if (size >= Rules.pop_limit_wo_hab_dome && !has_fac(FAC_HABITATION_DOME, base_id, 0)) return FAC_HABITATION_DOME;
        return 0;
    };
    if ((has_sp(FAC_CLONING_VATS - 70, faction_id) || BaseGrowthRate >= 6) && Rules.nutrient_intake_req_citizen &&
        cur_base().nutrient_surplus >= Rules.nutrient_intake_req_citizen && !hab_limit(CurrentBaseID)) {
        // Population boom.
        if (cur_base().pop_size < 127) cur_base().pop_size++;
        base_compute_inline();
        draw_tile(x, y, 2);
        if (cur_base().nutrients_accumulated > box) cur_base().nutrients_accumulated = box;
        return 0;
    }
    if (cur_base().nutrients_accumulated >= box) {
        if (BaseGrowthRate <= -3) {
            cur_base().nutrients_accumulated = box;
            return 0;
        }
        int32_t limit = hab_limit(CurrentBaseID);
        if (limit) {
            cur_base().nutrients_accumulated =
                cost_factor((uint8_t)cur_base().faction_id, 0, CurrentBaseID) * (cur_base().pop_size + 1) / 2;
            parse_say(1, Facility[limit].name, -1, -1);
            if (alien) popb("POPULATIONLIMIT", 0x800, 0x10, "al_pop_sm.pcx", 0);
            else popb("POPULATIONLIMIT", 0x800, 0x10, "pop_sm.pcx", 0);
        } else {
            cur_base().nutrients_accumulated = 0;
            cur_base().pop_size++;
            base_compute_inline();
            draw_tile(x, y, 2);
        }
    }
    cur_base().nutrients_accumulated += cur_base().nutrient_surplus;
    int32_t surplus = cur_base().nutrient_surplus;
    if (surplus >= 0) return 0;
    int32_t stock = cur_base().nutrients_accumulated;
    if (stock - surplus < 0 || stock + 5 * surplus >= 0) return 0;
    if (((uint32_t)cur_base().governor_flags & 0x80000040u) == 0x80000040u) return 0;
    // Low nutrients warning: count the tiles that farms could improve.
    int32_t improvable = 0, unimproved = 0;
    for (int32_t i = 1; i < 21; i++) {
        int32_t x2 = wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
        MAP* sq = map_tile(x2, y2);
        uint32_t items = sq->items;
        bool worked = (cur_base().worked_tiles & (1 << i)) != 0;
        if ((sq->climate & 0xE0) < 0x60) {
            if (worked && !(items & BIT_FARM)) unimproved++;
            continue;
        }
        if (items & BIT_FARM) continue;
        if ((items & BIT_FOREST) && base_fac(CurrentBaseID, FAC_TREE_FARM)) continue;
        bool fungus = (items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40;
        if (fungus && Factions[faction_id].tech_fungus_nutrient > 1) continue;
        int32_t rain = (sq->climate >> 3) & 3;
        if (worked && (fungus || (items & BIT_FOREST) || (sq->val3 & 0xC0) == 0x80 || !rain)) {
            unimproved++;
            continue;
        }
        if (rain == 1 || has_tech(Rules.tech_preq_allow_3_nutrients_sq, faction_id)) improvable++;
    }
    (void)unimproved;  // counted but unused by the original
    const char* pcx = alien ? "al_starv_sm.pcx" : "starv_sm.pcx";
    if (improvable && has_tech(Units[1].preq_tech, faction_id)) {
        if (Factions[faction_id].units_active[1]) popb("LOWNUTRIENTFARMS", 0, -1, pcx, 0);
        else popb("LOWNUTRIENTFORMERS", 0, -1, pcx, 0);
        return 0;
    }
    int32_t specialists = cur_base().specialist_total;
    if (specialists <= 1 && (!specialists || cur_base().pop_size >= 3)) {
        popb("LOWNUTRIENT", 0x80, 0x10, pcx, 0);
        return 0;
    }
    if (BasePsychNDrones[0]) popb("LOWNUTRIENTSPEC1", 0, -1, pcx, 0);
    else popb("LOWNUTRIENTSPEC2", 0, -1, pcx, 0);
    return 0;
}

// ---- prototype upgrades -----------------------------------------------------------------------------

void GAMEAPI do_upgrade(int32_t faction_id, int32_t new_unit_id, int32_t old_unit_id) {
    Faction& f = Factions[faction_id];
    int32_t cost = upgrade_cost(faction_id, new_unit_id, old_unit_id) * f.units_active[old_unit_id] * 10;
    f.energy_credits -= cost;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& v = Vehs[veh_id];
        if (v.faction_id != faction_id || v.unit_id != old_unit_id) continue;
        f.units_active[old_unit_id]--;
        f.units_active[new_unit_id]++;
        v.unit_id = (int16_t)new_unit_id;
        if (has_abil(new_unit_id, ABL_TRAINED) && !has_abil(old_unit_id, ABL_TRAINED)) {
            int32_t morale = v.morale + 1;
            v.morale = (uint8_t)(morale > 6 ? 6 : morale);
        }
        if (!has_abil(new_unit_id, ABL_TRAINED) && has_abil(old_unit_id, ABL_TRAINED)) {
            int32_t morale = v.morale - 1;
            v.morale = (uint8_t)(morale < 0 ? 0 : (morale > 6 ? 6 : morale));
        }
    }
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& base = Bases[i];
        if ((uint8_t)base.faction_id != faction_id || base.queue_size < 0) continue;
        // BUG(original): also checks the slot after the queue (queue_items[queue_size]).
        int32_t* items = base.queue_items;
        for (int32_t j = 0; j <= base.queue_size; j++)
            if (items[j] == old_unit_id) items[j] = new_unit_id;
    }
    if (new_unit_id >= 64 && cost) Units[new_unit_id].unit_flags |= 4;
    if (old_unit_id >= 64) Units[old_unit_id].unit_flags &= ~1;
    if (FactionStatus[0] & (uint8_t)(1 << faction_id)) draw_map(1);
    if (faction_id == CurrentPlayerFaction) Console_update_data(&MainConsole, 0);
}

namespace {
// "<name> (<stats> <abilities>)" of a unit into StrBuffer (upgrade dialog).
void say_unit_upgrade(int32_t unit_id) {
    StrBuffer[0] = 0;
    std::strcat(StrBuffer, Units[unit_id].name);
    std::strcat(StrBuffer, " ");
    std::strcat(StrBuffer, "(");
    say_stats_2(StrBuffer, unit_id);
    if (Units[unit_id].ability_flags) {
        std::strcat(StrBuffer, " ");
        for (int32_t i = 0; i < 29; i++)
            if (has_abil(unit_id, 1u << i)) std::strcat(StrBuffer, Strings_get(&StringTable, Ability[i].abbreviation));
    }
    std::strcat(StrBuffer, ")");
}
}  // namespace

int32_t GAMEAPI upgrade_prototype(int32_t faction_id, int32_t new_unit_id, int32_t old_unit_id, int32_t automatic) {
    if ((GameState & 1) && !(GameState & 8)) return 0;
    Faction& f = Factions[faction_id];
    int32_t cost = upgrade_cost(faction_id, new_unit_id, old_unit_id) * f.units_active[old_unit_id] * 10;
    uint8_t bit = (uint8_t)(1 << faction_id);
    if (!cost && (automatic || !(FactionStatus[0] & bit))) return 0;
    if (!(FactionStatus[0] & bit)) {
        // AI: affordable relative to its reserves and priorities.
        int32_t discount = DiffLevel * cost / 10 / (8 - Factions[RankingFactionIDUnk1].ranking);
        int32_t credits = f.energy_credits;
        if (credits > Factions[RankingFactionIDUnk2].energy_credits) {
            if (credits >= 1000) discount /= 2;
            if (credits >= 2000) discount = 0;
        }
        int32_t budget = (((Units[new_unit_id].plan == 2) + f.AI_power + f.AI_tech) * 2 + 1) * credits / 8;
        if (cost - discount > budget) return 0;
    } else {
        if (faction_id != CurrentPlayerFaction) return 0;
        if (cost) {
            say_unit_upgrade(old_unit_id);
            parse_says(0, StrBuffer, -1, -1);
            say_unit_upgrade(new_unit_id);
            parse_says(1, StrBuffer, -1, -1);
            parse_num(0, cost);
            parse_num(1, f.energy_credits);
            parse_num(2, f.units_active[old_unit_id]);
            bool built = (Units[new_unit_id].unit_flags & 4) != 0;
            int32_t triad = Chassis[Units[new_unit_id].chassis_id].triad;
            bool alien = (MFactions[faction_id].rule_flags & RFLAG_ALIEN) != 0;
            if (cost > f.energy_credits) {
                if (automatic && !(GameWarnings & 4)) return 0;
                const char* pcx = triad == 2 ? (alien ? "alair_sm.pcx" : "air_sm.pcx")
                                             : (triad == 1 ? "navun_sm.pcx" : (alien ? "alunit_sm.pcx" : "unit_sm.pcx"));
                popp(ScriptFileName, built ? "UPGRADEPROTOCOST" : "UPGRADEPROTOCOST2", 0, pcx, 0);
                return 0;
            }
            if (!UpgradeAllProtos) {
                const char* pcx = triad == 2 ? (alien ? "alair_sm.pcx" : "air_sm.pcx")
                                             : (triad == 1 ? "navun_sm.pcx" : (alien ? "alunit_sm.pcx" : "unit_sm.pcx"));
                int32_t choice = popp(ScriptFileName, built ? "UPGRADEPROTO" : "UPGRADEPROTO2", 0, pcx, 0);
                if (!choice) return 0;
                if (choice > 1) UpgradeAllProtos = 1;
            }
        }
    }
    if ((FactionStatus[0] & bit) && MultiplayerActive) {
        message_data(0x2411, 0, faction_id, new_unit_id, old_unit_id, 0);
        if (NetDaemon_receive(&NetState))
            while (NetDaemon_receive(&NetState)) {
            }
        return 1;
    }
    do_upgrade(faction_id, new_unit_id, old_unit_id);
    return 1;
}

void GAMEAPI upgrade_prototypes(int32_t faction_id, int32_t unit_id) {
    UNIT& u = Units[unit_id];
    u.unit_flags &= ~0x100;
    for (int32_t k = 0; k < 128; k++) {
        int32_t old_id = k < 64 ? k : (k % 64) + faction_id * 64;
        if (old_id == unit_id) continue;
        UNIT& o = Units[old_id];
        if (!(o.unit_flags & 1) || !(o.obsolete_factions & (1 << faction_id))) continue;
        if (o.plan != u.plan || o.group_id != u.group_id) continue;
        if (Chassis[o.chassis_id].triad != Chassis[u.chassis_id].triad) continue;
        if (!Factions[faction_id].units_active[old_id]) continue;
        int32_t offense = Weapon[u.weapon_id].offense_value;
        if (offense) {
            if (offense > Weapon[o.weapon_id].offense_value) continue;
        } else if (u.weapon_id != o.weapon_id) {
            continue;
        }
        int32_t abil_old = has_abil(old_id, 0x408418);
        if (has_abil(unit_id, 0x408418) != abil_old) continue;
        if (Armor[u.armor_id].defense_value > Armor[o.armor_id].defense_value) continue;
        if (Chassis[u.chassis_id].speed > Chassis[o.chassis_id].speed) continue;
        if (u.chassis_id != o.chassis_id) continue;
        upgrade_prototype(faction_id, unit_id, old_id, 1);
    }
}

void GAMEAPI upgrade_any_prototypes(int32_t faction_id) {
    UpgradeAllProtos = 0;
    if ((FactionStatus[0] & (uint8_t)(1 << faction_id)) && !(GameMorePreferences & 0x100000)) return;
    for (int32_t k = 0; k < 128; k++) {
        int32_t unit_id = k < 64 ? k : (k % 64) + faction_id * 64;
        uint16_t flags = Units[unit_id].unit_flags;
        if ((flags & 1) && (flags & 4) && (flags & 0x100)) upgrade_prototypes(faction_id, unit_id);
    }
}

// ---- road links ---------------------------------------------------------------------------------------

int32_t GAMEAPI base_connect(int32_t priority) {
    MAP* home = map_tile(cur_base().x, cur_base().y);
    if ((home->climate & 0xE0) < 0x60) return 0;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    int32_t region = home->region;
    int32_t best = 9999, goal_x = -1, goal_y = -1;
    bool connected = false;
    for (int32_t b = 0; b < BaseCount; b++) {
        if (b == CurrentBaseID) continue;
        BASE& other = Bases[b];
        if (map_tile(other.x, other.y)->region != region) continue;
        int32_t dist = (std::abs(cur_base().y - other.y) + wrap_dx(cur_base().x, other.x)) >> 1;
        if (other.faction_id == cur_base().faction_id) {
            if (dist > 8 && !base_fac(b, FAC_HEADQUARTERS)) {
                // Far bases only connect to the largest base (or the headquarters) of the region.
                bool skip = false;
                for (int32_t j = 0; j < BaseCount && !skip; j++) {
                    if (Bases[j].faction_id != cur_base().faction_id || map_tile(Bases[j].x, Bases[j].y)->region != region)
                        continue;
                    if (Bases[j].pop_size > other.pop_size || base_fac(j, FAC_HEADQUARTERS)) skip = true;
                }
                if (skip) continue;
            }
        } else {
            if (dist > 5 || !(Factions[faction_id].diplo_status[(uint8_t)other.faction_id] & 1)) continue;
            dist += MapAreaX;
        }
        if (dist >= best) continue;
        int32_t steps = 0;
        int32_t x = cur_base().x, y = cur_base().y;
        auto next_step = [&]() {
            int32_t flags = has_tech(Rules.tech_preq_build_road_fungus, (uint8_t)cur_base().faction_id) ? 0 : 4;
            return Path_find(&Paths, x, y, other.x, other.y, 0, -1, flags, -1);
        };
        for (int32_t dir = next_step(); dir >= 0;) {
            x = wrap_x(x + NearOffsetX[dir]);
            y += NearOffsetY[dir];
            if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) break;
            if (x == other.x && y == other.y) {
                connected = true;
                break;
            }
            // First tile of the path that still needs a road.
            int32_t f = (uint8_t)cur_base().faction_id;
            MAP* sq = map_tile(x, y);
            bool ok = !tile_is_base(sq);
            if (ok && (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40)
                ok = has_tech(Rules.tech_preq_build_road_fungus, f) || has_tech(Terraform[10].preq_tech, f);
            if (ok && (sq->items & BIT_VEH_IN_TILE)) {
                uint32_t owner = sq->val2 & 0xF;
                ok = owner > 7 || owner == (uint32_t)f || (Factions[f].diplo_status[owner] & 1);
            }
            if (ok) ok = zoc_sea(x, y, f) < 1;
            if (ok) {
                int32_t owner = whose_territory(f, x, y, nullptr, 0);
                ok = owner < 0 || owner == f || (Factions[owner].diplo_status[f] & 1);
            }
            if (ok) {
                uint32_t items = map_tile(x, y)->items;
                ok = !(items & BIT_ROAD) || ((items & BIT_MAGTUBE) && has_tech(Terraform[6].preq_tech, f));
            }
            if (ok) {
                best = dist;
                goal_x = x;
                goal_y = y;
                break;
            }
            if (++steps >= MapAreaX) break;
            dir = next_step();
        }
    }
    int32_t x = cur_base().x, y = cur_base().y;
    uint8_t bit = (uint8_t)(1 << (uint8_t)cur_base().faction_id);
    bool allowed = !(bit & FactionStatus[0]) || (GameMorePreferences & 0x20);
    if (goal_y < 0 || goal_y >= MapAreaY || goal_x < 0 || goal_x >= MapAreaX) {
        if (!allowed) return 0;
        if (Path_sensors(&Paths, (uint8_t)cur_base().faction_id, &x, &y)) return 0;
    } else if (connected && allowed && !Path_sensors(&Paths, (uint8_t)cur_base().faction_id, &x, &y) &&
               base_dist(cur_base().x, cur_base().y, (int16_t)x, (int16_t)y) <
                   base_dist(cur_base().x, cur_base().y, (int16_t)goal_x, (int16_t)goal_y)) {
        // A sensor site closer than the road site.
    } else {
        x = goal_x;
        y = goal_y;
    }
    add_goal((uint8_t)cur_base().faction_id, 9, priority, x, y, CurrentBaseID);
    return 1;
}

// ---- support check, drones ----------------------------------------------------------------------------

void GAMEAPI base_check_support() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    // Too many units for the minerals: disband (AI) or kill the farthest supported units.
    while (BaseForcesMaintCost > cur_base().mineral_intake_2) {
        int32_t best = -1, best_id = -1;
        for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
            VEH& v = Vehs[veh_id];
            if (v.faction_id != (uint8_t)cur_base().faction_id || v.home_base_id != CurrentBaseID) continue;
            int32_t plan = Units[v.unit_id].plan;
            int32_t score;
            if (plan == 10) {
                if (v.order != 3 || v.order_auto_type != 1 || !tile_is_base(map_tile(v.x, v.y))) continue;
                score = 99999;  // mineral convoys first
            } else {
                if (!(v.state & 0x10) || (uint8_t)plan > 9) continue;
                score = base_dist(v.x, v.y, cur_base());
            }
            if (score > best) {
                best = score;
                best_id = veh_id;
            }
        }
        if (best < 0) break;
        VEH& v = Vehs[best_id];
        int32_t plan = Units[v.unit_id].plan;
        if (plan == 10) {
            v.order = 0;
            BaseForcesMaintCost--;
            continue;
        }
        if (FactionStatus[0] & (uint8_t)(1 << faction_id)) {
            parse_says(1, Units[v.unit_id].name, -1, -1);
            popb("NOSUPPORT", 0x8000, 13, "genwarning_sm.pcx", 0);
        } else if (plan == 8) {
            v.state &= ~0x10u;
            int32_t support = f.SE_support_pending;
            BaseForcesMaintCost -= has_abil(v.unit_id, ABL_CLEAN_REACTOR) ? 0 : (support <= -4) + 1;
        }
        int32_t support = f.SE_support_pending;
        if (has_abil(Vehs[best_id].unit_id, ABL_CLEAN_REACTOR)) break;
        BaseForcesMaintCost -= (support <= -4) + 1;
        kill(best_id);
    }
    // Drone riots under low POLICE: a unit away from home deserts.
    uint8_t bit = (uint8_t)(1 << faction_id);
    int32_t gov = cur_base().governor_flags;
    if ((!(FactionStatus[0] & bit) || ((gov & GOV_MANAGE_CITIZENS) && (gov & GOV_ACTIVE))) &&
        (cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE)) {
        int32_t police = f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0);
        if (police > -3) return;
        if (cur_base().drone_total <= cur_base().talent_total && cur_base().nutrient_surplus >= 0) return;
        int32_t best = -1, best_id = -1;
        for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
            VEH& v = Vehs[veh_id];
            if (v.faction_id != faction_id || v.home_base_id != CurrentBaseID || !(v.state & 0x200000)) continue;
            if (FactionStatus[0] & bit) {
                uint32_t state = v.state;
                if (!(state & 0x200) || (state & 0x1000200) == 0x1000200) continue;
                if ((state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) continue;
                if (v.order_auto_type == 11 || v.order_auto_type == 10 || v.order_auto_type) continue;
            }
            int32_t m = f.AI_fight + 2;
            if (m - 1 > 0 && game_rand() % m) continue;
            int32_t dx = wrap_dx(v.x, cur_base().x);
            int32_t cost = Units[v.unit_id].cost;
            if (cost < 1) cost = 1;
            else if (cost > 99) cost = 99;
            int32_t score = ((std::abs(v.y - cur_base().y) + dx) >> 1) * 100 / cost;
            if (Units[v.unit_id].plan == 2) score *= 2;
            if (Units[v.unit_id].plan == 1) score /= 2;
            if (score > best) {
                best = score;
                best_id = veh_id;
            }
        }
        if (best_id >= 0) {
            kill(best_id);
            cur_base().state_flags &= ~BSTATE_DRONE_RIOTS_ACTIVE;
        }
    }
}

void GAMEAPI base_drones() {
    BASE& b = cur_base();
    bool alien = (MFactions[(uint8_t)b.faction_id].rule_flags & RFLAG_ALIEN) != 0;
    const char* pcx = alien ? "Alopdir.pcx" : "talent_sm.pcx";
    if (!b.drone_total && b.pop_size > 2 && (b.pop_size + 1) / 2 <= b.talent_total) {
        if (!(b.state_flags & BSTATE_GOLDEN_AGE_ACTIVE)) popb("GOLDENAGE", 0x20, -1, pcx, 0);
        cur_base().state_flags |= BSTATE_GOLDEN_AGE_ACTIVE;
    } else if (b.state_flags & BSTATE_GOLDEN_AGE_ACTIVE) {
        b.state_flags &= ~BSTATE_GOLDEN_AGE_ACTIVE;
        if (b.drone_total <= b.talent_total) popb("GOLDENAGEOVER", 0x40, -1, pcx, 0);
    }
    if (cur_base().talent_total < cur_base().drone_total &&
        !has_sp(FAC_TELEPATHIC_MATRIX - 70, (uint8_t)cur_base().faction_id) && !cur_base().nerve_staple_turns_left) {
        drone_riot();
        return;
    }
    if (!(cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE)) return;
    cur_base().state_flags &= ~BSTATE_DRONE_RIOTS_ACTIVE;
    if (!(cur_base().state_flags & BSTATE_GOLDEN_AGE_ACTIVE)) {
        int32_t gov = cur_base().governor_flags;
        bool governed = (gov & GOV_MANAGE_CITIZENS) && (gov & GOV_ACTIVE);
        popb(governed ? "DRONERIOTSOVER2" : "DRONERIOTSOVER", governed ? 0 : 0x10, 0x33, pcx, 0);
    }
    if (!(FactionStatus[0] & (uint8_t)(1 << (uint8_t)cur_base().faction_id))) base_reset(CurrentBaseID, 0);
    draw_tile(cur_base().x, cur_base().y, 2);
}

// ---- drone riots ------------------------------------------------------------------------------------------

void GAMEAPI drone_riot() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    MFaction& m = MFactions[faction_id];
    bool alien = (m.rule_flags & RFLAG_ALIEN) != 0;
    bool started = !(cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE);
    cur_base().state_flags |= BSTATE_DRONE_RIOTS_ACTIVE;
    int32_t gov = 0;
    if (started) {
        draw_tile(cur_base().x, cur_base().y, 2);
        gov = cur_base().governor_flags;
    }
    bool governed = started && (cur_base().governor_flags & GOV_MANAGE_CITIZENS) && (cur_base().governor_flags & GOV_ACTIVE);
    (void)gov;
    if (governed) popb("DRONERIOTS2", 0, 0x11, alien ? "ALdrone_sm.pcx" : "drone_sm.pcx", 0);
    else popb("DRONERIOTS", 8, 0x11, alien ? "ALdrone_sm.pcx" : "drone_sm.pcx", 0);
    if (!(cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE)) return;
    uint8_t bit = (uint8_t)(1 << faction_id);
    if (started) {
        if (!(FactionStatus[0] & bit)) base_reset(CurrentBaseID, 0);
        return;
    }
    Faction& f = Factions[faction_id];
    // Continued riots: facilities may be destroyed.
    int32_t odds = f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0) + 7;
    if (odds - 1 > 0 && game_rand() % odds) return;
    if (base_fac(CurrentBaseID, FAC_HEADQUARTERS) || f.base_count <= 1 || cur_base().assimilation_turns_left >= 45)
        return;
    if (cur_base().pop_size < 4 && game_rand() % 3) return;
    int32_t tries = cur_base().pop_size - f.diff_level - f.ranking + 20;
    for (int32_t k = 0; k <= tries; k++) {
        int32_t fac = game_rand() % 70;
        bool destructible = true;
        for (int32_t i = 0; i < m.faction_bonus_count; i++) {
            if ((m.faction_bonus_id[i] == 2 ||
                 (m.faction_bonus_id[i] == 12 && has_tech(Facility[m.faction_bonus_val1[i]].preq_tech, faction_id))) &&
                fac == m.faction_bonus_val1[i]) {
                destructible = false;
                break;
            }
        }
        if (fac > 1 && fac < 65 && base_fac(CurrentBaseID, fac) && destructible) {
            int32_t offset, mask;
            bitmask(fac, &offset, &mask);
            Bases[CurrentBaseID].facilities_built[offset] &= (uint8_t)~mask;
            parse_say(1, Facility[fac].name, -1, -1);
            popb("DRONERIOT", 8, -1, alien ? "ALdrone_sm.pcx" : "drone_sm.pcx", 0);
            return;
        }
    }
    // Human bases under a police state may revolt to another faction.
    if (f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0) > -3 || alien) return;
    if (!(FactionStatus[0] & bit)) return;
    int32_t target;
    if ((uint8_t)cur_base().faction_id_former != faction_id) {
        target = (uint8_t)cur_base().faction_id_former;
    } else {
        target = -1;
        int32_t best = 9999;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id == faction_id) continue;
            int32_t dist = base_dist(Bases[i].x, Bases[i].y, cur_base());
            // BUG(original): the diplomatic weighting uses the best faction found so far (-1 at first).
            uint32_t status = diplo_status_raw(faction_id, target);
            if (status & 1) dist *= 2;
            if (status & 2) dist = dist * 3 / 2;
            if (status & 4) dist /= 2;
            if (dist < best) {
                best = dist;
                target = (uint8_t)Bases[i].faction_id;
            }
        }
    }
    // Factions with a REVOLT bonus attract the revolt.
    // BUG(original): the shares of factions without the bonus are never written (stack residue); the
    // oracle's poisoned stack makes them 0, which this emulates.
    int32_t shares[8] = {};
    int32_t total = 0;
    for (int32_t i = 1; i < 8; i++) {
        MFaction& mi = MFactions[i];
        for (int32_t k = 0; k < mi.faction_bonus_count; k++) {
            if (mi.faction_bonus_id[k] != 13) continue;
            int32_t v = mi.faction_bonus_val1[k];
            if (v < 0) v = 0;
            else if (v > 100) v = 100;
            shares[i] = v;
            total += v;
            break;
        }
    }
    double chance = 1.0;  // FP: x87 at 53-bit precision; same operation order
    for (int32_t i = 1; i < 8; i++)
        if (shares[i] != -1) chance = (1.0 - (double)shares[i] * 0.01) * chance;
    int32_t clock = (int32_t)engine_logic_time();
    int32_t limit = (int32_t)(chance * 100.0);  // FP: _ftol truncates
    if (clock % 100 <= limit) {
        int32_t pick = clock / 100 % total;
        for (int32_t i = 1; i < 8; i++) {
            if (shares[i] == -1) continue;
            target = i;
            if (pick < shares[i]) break;
            pick -= shares[i];
        }
    }
    if (target <= 0) return;
    if (!(FactionStatus[0] & bit) && !(FactionStatus[0] & (1u << target))) return;
    if (MFactions[target].rule_flags & RFLAG_ALIEN) return;
    // The base defects.
    int32_t x = cur_base().x, y = cur_base().y;
    cur_base().faction_id = (int8_t)target;
    for (int32_t i = 1; i < 8; i++)
        if (Factions[i].base_id_attack_target == CurrentBaseID) Factions[i].base_id_attack_target = -1;
    cur_base().assimilation_turns_left = (uint8_t)cur_base().faction_id_former != target ? 50 : 0;
    Factions[target].last_base_turn = CurrentTurn;
    cur_base().minerals_accumulated_2 = 0;
    cur_base().minerals_accumulated = 0;
    if (cur_base().queue_size >= 0) {
        int32_t* items = cur_base().queue_items;
        for (int32_t j = 0; j <= cur_base().queue_size; j++)  // BUG(original): one past the queue
            if (items[j] >= 0) f.units_queue[items[j]]--;
    }
    cur_base().queue_size = 0;
    cur_base().production_id_last = -FAC_STOCKPILE_ENERGY;
    cur_base().queue_items[0] = -FAC_STOCKPILE_ENERGY;
    f.base_count--;
    Factions[target].base_count++;
    replay_base(1, x, y, target);
    int32_t best = 0, best_base = -1;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (i == CurrentBaseID || (uint8_t)Bases[i].faction_id != faction_id) continue;
        int32_t value = Bases[i].pop_size;
        if (base_fac(i, FAC_HEADQUARTERS)) value *= 2;
        if (value > best) {
            best = value;
            best_base = i;
        }
    }
    if (best_base >= 0) cur_base().governor_flags = Bases[best_base].governor_flags;
    cur_base().state_flags &= 0xFFDEF7B1;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x2 = wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
        using_set(x2, y2, target);
        map_tile(x2, y2)->visibility |= (uint8_t)(1 << target);
        for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
            VEH& v = Vehs[veh_id];
            if (v.x != x2 || v.y != y2) continue;
            if (v.faction_id == target) {
                v.state &= ~0x40000u;
            } else if (v.faction_id == faction_id && i < 9) {
                // Units in the inner ring defect too.
                v.faction_id = (uint8_t)target;
                f.units_active[v.unit_id]--;
                if (v.unit_id > 64) {
                    UNIT& u = Units[v.unit_id];
                    int32_t unit_id = propose_proto(target, u.chassis_id, u.weapon_id, u.armor_id, u.ability_flags,
                                                    u.reactor_id, -2, u.name);
                    if (unit_id >= 0) v.unit_id = (int16_t)unit_id;
                }
                v.home_base_id = (int16_t)CurrentBaseID;
                Factions[target].units_active[v.unit_id]++;
                owner_set(x2, y2, target);
                if (Chassis[Units[v.unit_id].chassis_id].range) v.movement_turns = 0;
                if (v.order != 2) v.order = 0;
                v.state &= 0xFCFFBDFF;
            }
        }
    }
    spot_base(CurrentBaseID, faction_id);
    reset_territory();
    for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--)
        if (veh_id < VehCount && Vehs[veh_id].faction_id == faction_id && Vehs[veh_id].home_base_id == CurrentBaseID)
            kill(veh_id);
    // BUG(original): both passes test the bonus techs of the old faction.
    for (int32_t pass = 0; pass < 2; pass++) {
        MFaction& mb = MFactions[pass == 0 ? faction_id : target];
        for (int32_t k = 0; k < mb.faction_bonus_count; k++) {
            if (mb.faction_bonus_id[k] != 2 &&
                !(mb.faction_bonus_id[k] == 12 && has_tech(Facility[mb.faction_bonus_val1[k]].preq_tech, faction_id)))
                continue;
            int32_t fac = mb.faction_bonus_val1[k];
            if (fac < 1) fac = 1;
            else if (fac > 69) fac = 69;
            fac_set_bit(CurrentBaseID, fac);
        }
    }
    owner_set(x, y, target);
    base_compute_inline();
    if (faction_id == CurrentPlayerFaction || target == CurrentPlayerFaction ||
        (uint8_t)Bases[CurrentBaseID].faction_id == CurrentPlayerFaction ||
        (Bases[CurrentBaseID].visibility & (uint8_t)(1 << CurrentPlayerFaction))) {
        pfncMoveMap(MapWins[0], x, y, 1);
        draw_map(1);
        parse_says(0, cur_base().name, -1, -1);
        parse_says(2, MFactions[target].adj_name_faction, -1, -1);
        if (!(GameState & 1) || (GameState & 8)) popp(ScriptFileName, "DRONEREVOLT", 0, "baseobl_sm.pcx", 0);
    }
}

// ---- doctors ------------------------------------------------------------------------------------------------

void GAMEAPI base_doctors() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    // During riots under low POLICE the base may need extra contentment to keep growing.
    int32_t must = 0;
    if ((cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE) &&
        f.SE_police_pending + has_fac(FAC_BROOD_PIT, CurrentBaseID, 0) * 2 < -1) {
        BASE& base = Bases[CurrentBaseID];
        int32_t owner = (uint8_t)base.faction_id;
        int32_t bonus = has_sp(FAC_ASCETIC_VIRTUES - 70, owner) ? 2 : 0;
        bool limited =
            (base.pop_size - bonus + MFactions[owner].rule_population >= Rules.pop_limit_wo_hab_complex &&
             !has_fac(FAC_HAB_COMPLEX, CurrentBaseID, 0)) ||
            (base.pop_size - bonus + MFactions[owner].rule_population >= Rules.pop_limit_wo_hab_dome &&
             !has_fac(FAC_HABITATION_DOME, CurrentBaseID, 0));
        if (!limited) {
            if ((has_sp(FAC_CLONING_VATS - 70, faction_id) || BaseGrowthRate >= 6) && Rules.nutrient_intake_req_citizen &&
                cur_base().nutrient_surplus >= Rules.nutrient_intake_req_citizen)
                must = 1;
            else if (cost_factor(faction_id, 0, CurrentBaseID) * (cur_base().pop_size + 1) <=
                     cur_base().nutrient_surplus + cur_base().nutrients_accumulated)
                must = 1;
        }
    }
    bool matrix = has_sp(FAC_TELEPATHIC_MATRIX - 70, faction_id);
    uint8_t bit = (uint8_t)(1 << faction_id);
    auto add_doctor = [&]() {
        int32_t type = best_specialist();
        int32_t n = cur_base().specialist_total++;
        if (n < 16) spec_set(n, type);
    };
    for (int32_t iter = 1; iter < 10; iter++) {
        if ((FactionStatus[0] & bit) &&
            (!(cur_base().governor_flags & GOV_MANAGE_CITIZENS) || !(cur_base().governor_flags & GOV_ACTIVE)))
            return;
        if (BaseYieldFoodShort && cur_base().drone_total + must > cur_base().talent_total && !matrix) {
            // Add a doctor.
            add_doctor();
            base_compute_inline();
            if (cur_base().drone_total + must > cur_base().talent_total) continue;
            if (!ControlUpkeepA || faction_id != CurrentPlayerFaction || f.SE_alloc_psych >= 2) return;
            if (cur_base().specialist_total - cur_base().specialist_adjust <= 1) return;
            int32_t item = cur_base().queue_items[0];
            if (item == -FAC_RECREATION_COMMONS || item == -FAC_HOLOGRAM_THEATRE || item == -FAC_PARADISE_GARDEN ||
                item == -FAC_RESEARCH_HOSPITAL || item == -FAC_NANOHOSPITAL)
                return;
            // BUG(original): compares with unit 8 instead of the Network Node.
            if (item == 8 && has_sp(FAC_VIRTUAL_WORLD - 70, faction_id)) return;
            if (MFactions[faction_id].rule_flags & RFLAG_ALIEN) popb("PSYCHREQUEST", 0, -1, "Alopdir.pcx", 0);
            else popb("PSYCHREQUEST", 0, -1, "talent_sm.pcx", 0);
            return;
        }
        // Release a doctor if the base stays content.
        int32_t total = cur_base().specialist_total;
        if (total - cur_base().specialist_adjust <= 0) return;
        if (cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE) return;
        if (cur_base().drone_total && cur_base().drone_total + must >= cur_base().talent_total && !matrix) return;
        cur_base().specialist_total = total - 1;
        base_compute_inline();
        if (cur_base().drone_total + must > cur_base().talent_total && !matrix) {
            add_doctor();
            return;
        }
        if (total <= cur_base().specialist_total) return;
    }
}

// ---- production -------------------------------------------------------------------------------------------

namespace {
bool gov_active(const BASE& base) { return (base.governor_flags & 1) && (base.governor_flags & (int32_t)0x80000000); }
// "Q" (queued: $NUM1 = queue length) or "G" (governor) suffix of the production messages.
void say_queue_suffix(int32_t queued) {
    if (queued) {
        parse_num(1, cur_base().queue_size + 1);
        crt_strcat(StrBuffer, "Q");
    } else if (gov_active(cur_base())) {
        crt_strcat(StrBuffer, "G");
    }
}
// Small picture of a completed unit's message.
const char* unit_pcx(int32_t unit_id, bool alien) {
    const UNIT& u = Units[unit_id];
    int32_t triad = Chassis[u.chassis_id].triad;
    if (unit_id < 64 && (Weapon[u.weapon_id].offense_value < 0 || unit_id == 15)) {
        if (triad == 2) return "chiron_sm.pcx";
        if (triad == 1) return unit_id == 14 ? "sealrk_sm.pcx" : "isle_sm.pcx";
        return "native_sm.pcx";
    }
    if (triad == 2) return alien ? "alair_sm.pcx" : "air_sm.pcx";
    if (triad == 1) return "navun_sm.pcx";
    return alien ? "alunit_sm.pcx" : "unit_sm.pcx";
}
// Morale of a new unit (the original's inline computation).
int32_t new_unit_morale(int32_t unit_id, int32_t faction_id, bool new_proto) {
    const UNIT& u = Units[unit_id];
    int32_t base_id = CurrentBaseID;
    int32_t morale = 0;
    bool psi = unit_id < 64 && Weapon[u.weapon_id].offense_value < 0;
    if (psi) {
        if (has_sp(8, faction_id)) morale = 1;
        if (has_sp(16, faction_id)) morale++;
        if (has_project(31, faction_id)) morale++;
        if (has_fac(FAC_CENTAURI_PRESERVE, base_id, 0)) morale++;
        if (has_fac(FAC_TEMPLE_OF_PLANET, base_id, 0)) morale++;
        if (has_fac(FAC_BIOLOGY_LAB, base_id, 0)) morale++;
        if (has_fac(FAC_BIOENHANCEMENT_CENTER, base_id, 0) || has_project(17, faction_id)) morale++;
    } else {
        morale = morale_mod(base_id, faction_id, Chassis[u.chassis_id].triad);
        if (u.weapon_id == 0x15) {
            int32_t offset, mask;
            bitmask(0x22, &offset, &mask);
            if (Bases[base_id].facilities_built[offset] & (uint8_t)mask) morale += 2;
            if (has_sp(34, faction_id)) morale++;
        }
        if (has_abil(unit_id, 0x4000)) morale++;
        if (new_proto) morale++;
    }
    morale++;
    return morale < 0 ? 0 : (morale > 6 ? 6 : morale);
}
// Mind worm boil and fungus spread of the Voice of Planet. `tries` is the original's attempt
// counter, shared by all bases and never initialised (see base_production).
void voice_of_planet(int32_t tries) {
    int32_t done[8];
    for (int32_t i = 0; i < 8; i++) done[i] = !(FactionStatus[1] & (1 << i));
    auto fungus = [](int32_t x, int32_t y) {
        bit_set(x, y, 0x20, 1);
        bit_set(x, y, FungusClearItems, 0);
        synch_bit(x, y, CurrentPlayerFaction);
        draw_tile(x, y, 2);
    };
    for (int32_t b = 0; b < BaseCount; b++) {
        const BASE& base = Bases[b];
        int32_t faction_id = (uint8_t)base.faction_id;
        if (done[faction_id] || (map_tile(base.x, base.y)->climate & 0xE0) < 0x60) continue;
        done[faction_id] = 1;
        int32_t x, y;
        for (;;) {
            int32_t r = game_rand() % 288;
            x = wrap_x(TableOffsetX[r] + base.x);
            y = TableOffsetY[r] + base.y;
            if (++tries >= 1000) break;
            if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
            if (tile_is_base(map_tile(x, y))) continue;
            if (whose_territory(faction_id, x, y, nullptr, 0) == faction_id) break;
        }
        if (tries >= 1000) continue;
        if (faction_id == CurrentPlayerFaction) Console_focus(&MainConsole, x, y, CurrentPlayerFaction);
        for (int32_t i = 1; i < 9; i++) {
            int32_t x2 = wrap_x(TableOffsetX[i] + x);
            int32_t y2 = TableOffsetY[i] + y;
            if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
            if (tile_is_base(map_tile(x2, y2))) continue;
            fungus(x2, y2);
        }
    }
    for (int32_t n = MapAreaTiles / 10; n > 0; n--) {
        int32_t x = MapAreaX - 1 > 0 ? game_rand() % MapAreaX : 0;
        int32_t y = MapAreaY - 1 > 0 ? game_rand() % MapAreaY : 0;
        int32_t x2 = x;
        if (x & 1) x2 = x - 1;
        if (y & 1) x2++;
        if (y < 0 || y >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
        if (tile_is_base(map_tile(x2, y))) continue;
        fungus(x2, y);
    }
}
}  // namespace

// Completes the current base's production (units, facilities, projects) when enough minerals are
// accumulated; also the queue advance, the AI colony pod rule and the project side effects.
// The original keeps a Popup object on its stack for the announcement blurbs (ui_production_*).
void GAMEAPI base_production() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    uint8_t bit = (uint8_t)(1 << faction_id);
    auto is_human = [&]() { return (FactionStatus[0] & bit) != 0; };
    // The original's [ebp-0x10]: the last facility bitmask offset of this call, else stack residue.
    // UNINIT(original): read uninitialised by the Voice of Planet attempt counter; taken as 0.
    int32_t slot10 = 0;
    cur_base().state_flags &= ~0x800000;
    int32_t best = cur_base().mineral_intake_2 + cur_base().mineral_surplus * 2;
    if (f.best_mineral_output > best) best = f.best_mineral_output;
    f.best_mineral_output = best;
    if (cur_base().nutrients_accumulated < 0) return;
    if (cur_base().mineral_surplus >= 0 && cur_base().queue_items[0] != -69 && !(cur_base().state_flags & 2))
        cur_base().minerals_accumulated += cur_base().mineral_surplus;
    {
        BASE& base = cur_base();
        bool human = is_human();
        if (!human || (gov_active(base) && !(base.state_flags & (int32_t)0x80000000))) {
            MAP* sq = map_tile(base.x, base.y);
            bool own_unit = (sq->items & 2) && (sq->val2 & 0xF) < 8;
            if ((base.state_flags & 8) || (!own_unit && !human)) base_reset(CurrentBaseID, 0);
        }
    }
    int32_t fac = 0, sp = -1, cost = 0;
    bool is_sat = false;
    for (;;) {
        int32_t item = cur_base().queue_items[0];
        if (item == -69) return;
        if (item >= 0) break;
        fac = -item;
        sp = fac >= 70 ? fac - 70 : -1;
        is_sat = fac >= 65 && fac < 69;
        cost = cost_factor(faction_id, 1, -1) * Facility[fac].cost;
        if (cur_base().minerals_accumulated >= cost && sp == 32 && !ascending()) {
            popb("ASCENTNOTYET", 0x1000, -1, "asctran_sm.pcx", 0);
            return;
        }
        if (sp >= 0) {
            if (SecretProjects[sp] != -1) {
                if (is_human() && !gov_active(cur_base())) {
                    parse_say(1, Facility[fac].name, -1, -1);
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "ALREADYPROJECT");
                    int32_t owner = SecretProjects[sp];
                    if (owner >= 0) {
                        parse_says(2, Bases[owner].name, -1, -1);
                        int32_t of = (uint8_t)Bases[owner].faction_id;
                        PluralDefault = MFactions[of].is_noun_plural;
                        GenderDefault = MFactions[of].noun_gender;
                        parse_says(3, MFactions[of].noun_faction, -1, -1);
                        crt_strcat(StrBuffer, of == faction_id ? "1" : "2");
                    } else {
                        crt_strcat(StrBuffer, "0");
                    }
                    popb(StrBuffer, 0x200, -1, "secproj_sm.pcx", 0);
                } else {
                    base_reset(CurrentBaseID, 0);
                }
                return;
            }
            break;
        }
        if (fac >= 65) break;
        int32_t offset, mask;
        bitmask(fac, &offset, &mask);
        slot10 = offset;
        if (!(Bases[CurrentBaseID].facilities_built[offset] & (uint8_t)mask)) break;
        if (!base_queue(CurrentBaseID)) {
            if (is_human() && !gov_active(cur_base())) {
                parse_say(1, Facility[fac].name, -1, -1);
                if (m.rule_flags & 0x80)
                    popb("ALREADYFACILITY", 0x200, -1, "alfacblt_sm.pcx", 0);
                else
                    popb("ALREADYFACILITY", 0x200, -1, "facblt_sm.pcx", 0);
            } else {
                base_reset(CurrentBaseID, 0);
            }
            return;
        }
    }

    if (cur_base().queue_items[0] >= 0) {
        // ---- unit ----
        int32_t unit_id = cur_base().queue_items[0];
        const UNIT& u = Units[unit_id];
        cost = cost_factor(faction_id, 1, -1) * veh_cost(unit_id, CurrentBaseID, nullptr);
        if (cur_base().minerals_accumulated < cost) return;
        if (u.plan == 8 && cur_base().pop_size == 1 && f.diff_level > 1) {
            if (!is_human()) {
                if (DiffLevel > 3) cur_base().nutrients_accumulated += DiffLevel - 2;
                return;
            }
            if (gov_active(cur_base())) return;
            if ((GameState & 1) && !(GameState & 8)) return;
            parse_says(0, cur_base().name, -1, -1);
            parse_says(1, Units[unit_id].name, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "ABANDONBASE");
            int32_t growth = cost_factor(faction_id, 0, CurrentBaseID) * (cur_base().pop_size + 1);
            if (cur_base().nutrient_surplus > 0) {
                crt_strcat(StrBuffer, "1");
                int32_t turns = (growth - cur_base().nutrients_accumulated) / cur_base().nutrient_surplus;
                parse_num(0, turns < 0 ? 0 : (turns > 9999 ? 9999 : turns));
            }
            int32_t choice = 0;
            if (!MultiplayerActive) {
                if (MFactions[faction_id].rule_flags & 0x80)
                    choice = popp(ScriptFileName, "ABANDONBASE", 0, "Alopdir.pcx", 0);
                else
                    choice = popp(ScriptFileName, "ABANDONBASE", 0, "talent_sm.pcx", 0);
            }
            if (MultiplayerActive || choice < 2) {
                if (choice) BaseUpkeepFlag = 1;
                return;
            }
        }
        bool new_proto = false;
        if (unit_id >= 64) {
            uint16_t flags = Units[unit_id].unit_flags;
            Units[unit_id].unit_flags = flags | 4;
            new_proto = !(flags & 4);
            if (new_proto) {
                int32_t id = CurrentBaseID;
                prune_protos(faction_id, unit_id, 0);
                CurrentBaseID = id;
                CurrentBasePtr = kBasesAddr + id * (uint32_t)sizeof(BASE);
                if (ComputeBaseID != id) {
                    ComputeBaseID = id;
                    base_support();
                    base_yield();
                    base_nutrient();
                    base_minerals();
                    base_energy_call();
                }
            }
        }
        BASE* base = &cur_base();
        base->minerals_accumulated -= cost;
        base = &cur_base();
        int32_t exempt = Rules.retool_exemption;
        int32_t limit = exempt <= base->mineral_surplus ? exempt : base->mineral_surplus;
        if (base->minerals_accumulated > limit && is_human())
            base->minerals_accumulated = limit < 0 ? 0 : (exempt < base->mineral_surplus ? exempt : base->mineral_surplus);
        base->minerals_accumulated_2 = base->minerals_accumulated;
        if (dword_90EA40) return;
        int32_t veh_id = veh_init(unit_id, faction_id, cur_base().x, cur_base().y);
        if (veh_id < 0) return;
        if (u.plan == 8) {
            if ((cur_base().state_flags & 0x2000000) || !f.AI_power || f.base_count > 8)
                cur_base().state_flags |= 0x1000000;
            cur_base().state_flags |= 0x2000000;
        }
        VEH& veh = Vehs[veh_id];
        if (is_human() && (cur_base().governor_flags & 0x80) && (cur_base().governor_flags & (int32_t)0x80000000)) {
            veh.state |= 0x200;
            veh.order_auto_type = 0;
        }
        if (unit_id == 8 && faction_id == CurrentPlayerFaction && !(GameInterludeState & 0x10)) {
            interlude(4, (int32_t)(CurrentBasePtr + offsetof(BASE, name)), 1, 0);
            veh.state |= 0x800000;
        }
        veh.morale = (uint8_t)new_unit_morale(unit_id, faction_id, new_proto);
        veh.state |= 0x2000;
        if (faction_id == CurrentPlayerFaction) veh.flags |= 0x1000;
        if (u.plan == 8) {
            if (cur_base().nutrients_accumulated < 0) cur_base().nutrients_accumulated = 0;
            if (cur_base().pop_size > 1 || (f.diff_level > 1 && !is_objective(CurrentBaseID))) cur_base().pop_size--;
            if (cur_base().pop_size <= 0) {
                veh.home_base_id = -1;
                base_kill(CurrentBaseID);
                draw_map(1);
                eliminate_player(faction_id, 0);
                return;
            }
        }
        cur_base().state_flags |= 0x800000;
        cur_base().state_flags &= 0x7FFFFFFF;
        if (!is_human()) {
            base_reset(CurrentBaseID, 0);
        } else {
            int32_t queued = base_queue(CurrentBaseID);
            parse_says(1, Units[unit_id].name, -1, -1);
            int32_t flags;
            bool gw4 = new_proto && (GameWarnings & 4);
            if (queued || gov_active(cur_base())) {
                if (!gw4 && !(GameWarnings & 0x2000)) {
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "PRODUCE");
                    flags = 0;
                    goto unit_message;
                }
            }
            if (gw4) {
                parse_num(0, cost_factor((uint8_t)cur_base().faction_id, 1, -1) * veh_cost(unit_id, CurrentBaseID, nullptr));
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "PRODUCEPROTO");
                flags = 4;
            } else {
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "PRODUCE");
                flags = (uint8_t)Units[unit_id].plan < 8 ? 0x400 : 2;
            }
        unit_message:
            say_queue_suffix(queued);
            popb(StrBuffer, flags, -1, unit_pcx(unit_id, (MFactions[faction_id].rule_flags & 0x80) != 0), 0);
            if (!queued && gov_active(cur_base())) base_reset(CurrentBaseID, 0);
        }
        if (new_proto) {
            mon_prototype_built(faction_id);
            if (MultiplayerActive && is_human()) {
                Units[unit_id].unit_flags |= 0x100;
            } else {
                UpgradeAllProtos = 0;
                if (!is_human() || (GameMorePreferences & 0x100000)) {
                    for (int32_t k = 0; k < 128; k++) {
                        int32_t id = k < 64 ? k : (k % 64) + faction_id * 64;
                        uint16_t flags = Units[id].unit_flags;
                        if ((flags & 1) && (flags & 4) && (flags & 0x100)) upgrade_prototypes(faction_id, id);
                    }
                }
            }
        }
        if (unit_id < 64 && (Weapon[u.weapon_id].offense_value < 0 || unit_id == 15)) {
            mon_native_life_bred(faction_id);
        } else {
            int32_t triad = Chassis[u.chassis_id].triad;
            if (triad == 1 && Weapon[u.weapon_id].offense_value) mon_naval_unit_built(faction_id);
            if (Chassis[u.chassis_id].triad == 2) mon_air_unit_built(faction_id);
        }
        return;
    }

    // ---- facility, satellite or secret project ----
    if (cur_base().minerals_accumulated < cost) return;
    if (sp >= 0 && !is_human()) {
        int32_t item = cur_base().queue_items[0];
        for (int32_t i = 0; i < BaseCount; i++) {
            const BASE& other = Bases[i];
            int32_t of = (uint8_t)other.faction_id;
            if (!(FactionStatus[0] & (1 << of)) || other.queue_items[0] != item) continue;
            if (other.queue_items[0] == -102 && !ascending()) continue;
            // Another faction's human player completes the same project this turn: let them.
            if (other.mineral_surplus + other.minerals_accumulated >= cost_factor(of, 1, -1) * Facility[fac].cost)
                return;
        }
    }
    if (fac <= 0) return;
    {
        BASE* base = &cur_base();
        base->minerals_accumulated -= cost;
        int32_t exempt = Rules.retool_exemption;
        int32_t limit = exempt <= base->mineral_surplus ? exempt : base->mineral_surplus;
        if (base->minerals_accumulated > limit && is_human())
            base->minerals_accumulated = limit < 0 ? 0 : (exempt < base->mineral_surplus ? exempt : base->mineral_surplus);
        base->minerals_accumulated_2 = base->minerals_accumulated;
        base->state_flags |= 0x800000;
        base->state_flags &= 0x7FFFFFFF;
    }
    if (fac == FAC_HEADQUARTERS) {
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id != faction_id) continue;
            int32_t offset, mask;
            bitmask(FAC_HEADQUARTERS, &offset, &mask);
            slot10 = offset;
            Bases[i].facilities_built[offset] &= (uint8_t)~mask;
        }
    }
    if (sp >= 0) {
        SecretProjects[sp] = CurrentBaseID;
    } else if (is_sat) {
        (&f.satellites_nutrient)[fac - 65]++;
        if (!(f.player_flags & 0x200)) {
            // The first satellite reveals the map.
            MAP* sq = &MapTiles[0];
            for (int32_t i = 0; i < MapAreaTiles; i++, sq++) {
                sq->visibility |= bit;
                if (faction_id) sq->visible_items[faction_id - 1] = sq->items;
            }
            for (int32_t i = 0; i < BaseCount; i++) {
                Bases[i].visibility |= bit;
                Bases[i].factions_pop_size_intel[faction_id] = Bases[i].pop_size;
            }
            f.player_flags |= 0x200;
            if (faction_id == CurrentPlayerFaction && !(GameState & 1)) {
                popp(ScriptFileName, "SEEMAP", 0, "space_sm.pcx", 0);
                set_dirty();
                draw_map(1);
                GraphicWin_redraw(&WorldWindow);
            }
        }
    } else {
        int32_t offset, mask;
        bitmask(fac, &offset, &mask);
        slot10 = offset;
        Bases[CurrentBaseID].facilities_built[offset] |= (uint8_t)mask;
    }
    if (fac == FAC_NETWORK_NODE) {
        cur_base().state_flags &= ~0xC00;
        for (int32_t v = stack_fix(veh_at(cur_base().x, cur_base().y)); v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (Units[Vehs[v].unit_id].plan == 12) Vehs[v].order = 0;
        }
    }
    draw_tile(cur_base().x, cur_base().y, 2);
    int32_t queued = base_queue(CurrentBaseID);
    const char* base_name_ptr = cur_base().name;
    if (faction_id == CurrentPlayerFaction || fac >= 40) {
        parse_say(1, Facility[fac].name, -1, -1);
        parse_says(2, m.adj_name_faction, -1, -1);
        if (faction_id == CurrentPlayerFaction) {
            if (fac == FAC_SUBSPACE_GENERATOR) {
                interlude(0x26, 0, 1, 0);
                int32_t extra;
                parse_num(0, generators(faction_id, &extra));
                parse_num(1, extra);
                parse_num(2, Rules.subspace_gen_req);
                parse_num(3, Rules.base_size_subspace_gen);
                parse_says(0, cur_base().name, -1, -1);
                popp(ScriptFileName, "GENERATORME", 0, "subgen_sm.pcx", 0);
            } else {
                bool blurb = false;
                if (fac < 70) {
                    int32_t offset, mask;
                    bitmask(fac, &offset, &mask);
                    slot10 = (f.facility_announced[offset] & (uint8_t)mask) != 0;
                    blurb = !slot10 && !(GameState & 1) && (GameWarnings & 1);
                }
                char num[80];
                if (blurb) {
                    // First facility of its kind: blurb, voice clip and message.
                    parse_says(0, cur_base().name, -1, -1);
                    ui_production_popup_open(500);
                    if (GameWarnings & 1) {
                        StrBuffer[0] = 0;
                        if (sp >= 0) {
                            crt_strcat(StrBuffer, "PROJECT");
                            crt_itoa(sp, num, 10);
                            crt_strcat(StrBuffer, num);
                        } else {
                            crt_strcat(StrBuffer, "FAC");
                            crt_itoa(fac, num, 10);
                            crt_strcat(StrBuffer, num);
                            int32_t offset, mask;
                            bitmask(fac, &offset, &mask);
                            slot10 = offset;
                            f.facility_announced[offset] |= (uint8_t)mask;
                        }
                        ui_production_popup_blurb(false);
                    }
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "voices\\fac");
                    crt_itoa(fac >= 65 && fac <= 69 ? fac - 31 : fac, num, 10);
                    crt_strcat(StrBuffer, num);
                    if (fac >= 34 && fac <= 41) {
                        crt_itoa(0, num, 10);
                        crt_strcat(StrBuffer, num);
                    }
                    crt_strcat(StrBuffer, ".mp3");
                    if ((GameMorePreferences & 0x4000) && faction_id == CurrentPlayerFaction && is_human()) {
                        Wave_unload(&FacilityVoice);
                        Wave_load(&FacilityVoice, StrBuffer);
                        Wave_play_2(&FacilityVoice);
                    }
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "PRODUCEX");
                    say_queue_suffix(queued);
                    popb(StrBuffer, 1, -1, (m.rule_flags & 0x80) ? "alfacblt_sm.pcx" : "facblt_sm.pcx", 0);
                    if ((GameMorePreferences & 0x4000) && (GameMorePreferences & 0x20000)) Sound_fade_2(&FacilityVoice, 2000);
                } else {
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "PRODUCE");
                    say_queue_suffix(queued);
                    const char* pcx;
                    if (m.rule_flags & 0x80)
                        pcx = is_sat ? "space_sm.pcx" : (fac >= 70 ? "secproj_sm.pcx" : "alfacblt_sm.pcx");
                    else
                        pcx = is_sat ? "space_sm.pcx" : (fac >= 70 ? "secproj_sm.pcx" : "facblt_sm.pcx");
                    int32_t flags = 1;
                    if ((queued || gov_active(cur_base())) && !(GameWarnings & 0x2000)) flags = 0;
                    popb(StrBuffer, flags, -1, pcx, 0);
                }
            }
        } else if (sp >= 0) {
            // Another faction's project: blurb shown to the viewer.
            parse_says(0, cur_base().name, -1, -1);
            ui_production_popup_open(600);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "PROJECT");
            char num[80];
            crt_itoa(sp, num, 10);
            crt_strcat(StrBuffer, num);
            ui_production_popup_blurb(true);
            if (!(GameState & 1) || (GameState & 8)) ui_production_popup_exec();
        } else if (fac == FAC_SUBSPACE_GENERATOR) {
            if (!(GameState & 1) || (GameState & 8)) {
                if (!(MFactions[CurrentPlayerFaction].rule_flags & 0x80))
                    interlude(0x1A, (int32_t)(CurrentBasePtr + offsetof(BASE, name)), 1, 0);
                int32_t extra;
                parse_num(0, generators(faction_id, &extra));
                parse_num(1, extra);
                parse_num(2, Rules.subspace_gen_req);
                parse_num(3, Rules.base_size_subspace_gen);
                parse_says(0, cur_base().name, -1, -1);
                GenderDefault = m.is_leader_female;
                PluralDefault = 0;
                parse_says(1, m.title_leader, -1, -1);
                GenderDefault = m.is_leader_female;
                PluralDefault = 0;
                parse_says(2, m.name_leader, -1, -1);
                PluralDefault = m.is_noun_plural;
                GenderDefault = m.noun_gender;
                parse_says(3, m.noun_faction, -1, -1);
                popp(ScriptFileName, "GENERATOR", 0, "subgen_sm.pcx", 0);
            }
        } else {
            parse_says(0, cur_base().name, -1, -1);
            if (!(GameState & 1) || (GameState & 8))
                popp(ScriptFileName, is_sat ? "PRODUCE3" : "PRODUCE2", 0, is_sat ? "space_sm.pcx" : "secproj_sm.pcx", 0);
        }
    }
    if (is_sat)
        mon_first_in_space(faction_id);
    else if (fac < 70)
        mon_facility_built(faction_id, base_name_ptr);
    if (fac == FAC_CENTAURI_PRESERVE) {
        mon_built_preserve(faction_id);
        if (faction_id == CurrentPlayerFaction) interlude((GameInterludeState & 0x20) ? 9 : 5, 0, 1, 0);
    }
    if (fac == FAC_CENTAURI_PRESERVE || fac == FAC_TEMPLE_OF_PLANET || fac == FAC_TREE_FARM || fac == FAC_HYBRID_FOREST) {
        if (f.clean_minerals_modifier) f.clean_minerals_modifier++;
        f.player_flags &= ~0x4000;
    }
    if (sp >= 0) {
        if (sp == 31) interlude(0xC, 0, 1, 0);
        if (GamePreferences & 0x10000000) {
            if (!MultiplayerActive && faction_id == CurrentPlayerFaction) amovie_project(sp);
            if (!MultiplayerActive && faction_id != CurrentPlayerFaction && sp == 32) amovie_project_2("theyTranscend");
        }
        if (faction_id == CurrentPlayerFaction) help_project(sp);
        if (sp == 31) {
            voice_of_planet(slot10);
            interlude(0xD, 0, 1, 0);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(5, m.name_leader, -1, -1);
            win32::CharUpperA(ParseStrBuffer[5]);
            interlude(0xE, 0, 0, 0);
        }
        mon_secret_project(faction_id, sp);
        if (sp == 32) {
            // Ascent to Transcendence: the game ends.
            mon_winning_trans(faction_id, -1);
            if (GameRules & 0x1000) {
                for (int32_t i = 1; i < 8; i++) {
                    if (i == faction_id || !(f.diplo_status[i] & 1)) continue;
                    if (!(Factions[i].diplo_status[faction_id] & 0x2000000) || (FactionStatus[0] & (1 << i)))
                        mon_winning_trans(i, faction_id);
                }
            }
            if (faction_id == CurrentPlayerFaction) {
                interlude(0x11, 0, 2, 0);
                GameVictoryType = 1;
            } else {
                GameVictoryType = 3;
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(5, m.name_leader, -1, -1);
                parse_num(2, game_year(CurrentTurn + 100));
                parse_num(3, (CurrentTurn + 100) / 100);
                if (m.rule_flags & 0x80)
                    interlude(0x1B, 0, 4, 0);
                else
                    interlude(0x12, 0, 3, 0);
            }
            GameState |= 1;
            return;
        }
        if (sp == 20) {
            tech_advance(faction_id);
            tech_advance(faction_id);
        } else if (sp == 7) {
            for (int32_t i = 0; i < BaseCount; i++) {
                BASE& b = Bases[i];
                if ((uint8_t)b.faction_id != faction_id || b.pop_size >= 3) continue;
                b.pop_size = 3;
                if (faction_id == CurrentPlayerFaction) draw_tile(b.x, b.y, 2);
            }
        }
    }
    if ((gov_active(cur_base()) && !queued) || !is_human()) base_reset(CurrentBaseID, 0);
    if (is_human() && (cur_base().governor_flags & (int32_t)0x80000001) != (int32_t)0x80000001 &&
        cur_base().queue_items[0] == -fac && !cur_base().queue_size) {
        cur_base().queue_items[0] = -69;
        draw_radius(cur_base().x, cur_base().y, 2, 2);
    }
    if (sp < 0) return;
    // Other bases building the completed project switch to a project of a sister base (the one
    // with the least progress) or are reset.
    int32_t cur_id = CurrentBaseID;
    int32_t item = -fac;
    for (int32_t b = 0; b < BaseCount; b++) {
        if (b == cur_id) continue;
        BASE& base = Bases[b];
        int32_t bf = (uint8_t)base.faction_id;
        if ((FactionStatus[0] & (1 << bf)) && !gov_active(base)) continue;
        slot10 = item;
        if (base.queue_items[0] != item) continue;
        int32_t best_id = -1;
        int32_t best_score = base.minerals_accumulated + base.mineral_surplus * 10;
        for (int32_t j = 0; j < BaseCount; j++) {
            if (j == b || j == cur_id) continue;
            const BASE& o = Bases[j];
            if ((uint8_t)o.faction_id != (uint8_t)bf || o.queue_items[0] == item || o.queue_items[0] > -70) continue;
            int32_t score = o.minerals_accumulated + o.mineral_surplus * 10;
            if (score < best_score) {
                best_score = score;
                best_id = j;
            }
        }
        if (best_id >= 0) {
            base.queue_items[0] = Bases[best_id].queue_items[0];
            Bases[best_id].queue_items[0] = item;
        } else {
            base_reset(b, 0);
        }
    }
    CurrentBaseID = cur_id;
    CurrentBasePtr = kBasesAddr + cur_id * (uint32_t)sizeof(BASE);
    if (ComputeBaseID != cur_id) {
        ComputeBaseID = cur_id;
        base_support();
        base_yield();
        base_nutrient();
        base_minerals();
        base_energy_call();
    }
    return;
}

// ---- hurry ----------------------------------------------------------------------------------------------------

void GAMEAPI base_hurry() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    uint8_t bit = (uint8_t)(1 << faction_id);
    uint32_t gov = (uint32_t)cur_base().governor_flags;
    if ((FactionStatus[0] & bit) && (!(gov & GOV_ACTIVE) || !(gov & GOV_MAY_HURRY_PRODUCTION))) return;
    if (!cur_base().minerals_accumulated) return;
    int32_t item = cur_base().queue_items[0];
    int32_t fac = item < 0 ? -item : 0;
    int32_t cost = fac ? Facility[fac].cost : veh_cost(item, CurrentBaseID, nullptr);
    int32_t total = cost_factor(faction_id, 1, -1) * cost;
    int32_t x = cur_base().x, y = cur_base().y;
    MAP* sq = map_tile(x, y);
    int32_t region = sq->region;
    int32_t spend = 0;
    if (item >= 0) {
        UNIT& u = Units[item];
        if (u.plan == 8 && cur_base().pop_size < 2) return;
        if ((sq->items & BIT_VEH_IN_TILE) && (sq->val2 & 0xF) < 8 && !(cur_base().state_flags & BSTATE_COMBAT_LOSS_LAST_TURN) &&
            !at_goal(faction_id, 0x10, x, y)) {
            if (item >= 64 && !(u.unit_flags & 4)) {
                spend = f.energy_credits / ((f.AI_wealth + 2) * 10);
            } else {
                uint8_t plan = (uint8_t)u.plan;
                uint8_t region_plan = f.region_base_plan[region];
                if (region_plan == plan || (plan == 9 && region_plan == 8) || (plan == 1 && region_plan <= 2)) {
                    spend = f.energy_credits / ((f.AI_wealth + 2) * 25 - f.AI_power * 10);
                    if (region_plan == 8) {
                        spend = (f.AI_growth + 5) * spend / 4;
                        if (f.energy_credits > CurrentTurn) spend *= 2;
                    }
                }
            }
        } else {
            spend = f.energy_credits / 10;
        }
        if (planet_buster2(item) && !f.planet_busters) {
            for (int32_t i = 1; i < 8; i++) {
                if (i == faction_id || !(f.diplo_status[i] & 0x10) || !Factions[i].major_atrocities) continue;
                if (f.major_atrocities <= Factions[i].major_atrocities) spend = f.energy_credits / 10;
            }
        }
    } else if ((cur_base().state_flags & BSTATE_DRONE_RIOTS_ACTIVE) && fac) {
        spend = f.energy_credits / ((f.AI_power + f.AI_wealth + 1) * 5);
        if (cur_base().drone_total > cur_base().talent_total &&
            f.SE_police_pending + (base_fac(CurrentBaseID, FAC_BROOD_PIT) ? 2 : 0) <= -2 && fac < 70) {
            int32_t cap = f.energy_credits * 3 / 4;
            int32_t want = (total - cur_base().minerals_accumulated) * ((FactionStatus[0] & bit) ? 2 : 0);
            spend = (want < 0 || cap < 0) ? 0 : (want > cap ? cap : want);
        }
    } else if (fac == FAC_HEADQUARTERS) {
        spend = f.energy_credits / 10;
    } else if (fac == FAC_PERIMETER_DEFENSE || fac == FAC_TACHYON_FIELD) {
        int32_t credits = f.energy_credits;
        spend = credits / 50;
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id || !(f.diplo_status[i] & 0x40030)) continue;
            // BUG(original): the byte tested is region_total_bases[i] of faction i.
            if (Factions[i].region_total_bases[i] || (FactionStatus[0] & (1u << i))) spend = credits / 10;
        }
    } else if (fac >= 70) {
        spend = f.energy_credits / 50;
        if (f.energy_credits >= 2000) spend *= 4;
        if (fac == FAC_ASCENT_TO_TRANSCENDENCE) spend *= 4;
    } else if (fac == FAC_PSI_GATE) {
        spend = f.energy_credits / 50;
        if (at_climax(faction_id)) spend *= 2;
    }
    int32_t credits = f.energy_credits;
    if (credits >= 4800) spend += credits / 25;
    else if (credits > 3200) spend += credits / 100;
    else if (credits > 2000) spend += credits / 250;
    spend = (spend < 0 || credits < 0) ? 0 : (spend > credits ? credits : spend);
    int32_t need = total - cur_base().minerals_accumulated;
    if (spend >= 0 && need >= 0) {
        if (spend > need) spend = need;
        if (spend) {
            if (SecretProjects[FAC_VOICE_OF_PLANET - 70] >= 0) {
                spend /= 2;
            } else if (!std::strcmp(MFactions[faction_id].filename, "MORGAN")) {
                spend = spend * 2 / 3;
            } else if (!std::strcmp(MFactions[faction_id].filename, "PEACE")) {
                spend = spend * 3 / 2;
            }
            int32_t rate = MFactions[faction_id].rule_hurry;
            if (rate != 100 && spend < f.energy_credits / 10) spend = spend * 100 / rate;
            int32_t minerals = cur_base().minerals_accumulated;
            need = total - minerals;
            int32_t price = need * need / 20 + need * 2;
            if (cur_base().queue_items[0] <= -70) price *= 2;
            if (SecretProjects[FAC_VOICE_OF_PLANET - 70] >= 0) price *= 2;
            int32_t bought = price ? need * spend / price : 0;
            cur_base().minerals_accumulated = bought * 100 / rate + minerals;
            int32_t refund = (FactionStatus[0] & bit)
                                 ? 0
                                 : DiffLevel * spend / ((8 - Factions[RankingFactionIDUnk1].ranking) * 10);
            f.energy_credits += refund - spend;
        }
    }
    if (FactionStatus[0] & bit || fac < 70 || SecretProjects[fac - 70] != -1 || SunspotDuration > 0 || (GameState & 1))
        return;
    if (cost_factor((uint8_t)cur_base().faction_id, 1, -1) * Facility[fac].cost >
        cur_base().mineral_surplus + cur_base().minerals_accumulated)
        return;
    parse_says(0, MFactions[(uint8_t)cur_base().faction_id].adj_name_faction, -1, -1);
    parse_say(1, Facility[fac].name, -1, -1);
    popp(ScriptFileName, "DONEPROJECT", 0, "secproj_sm.pcx", 0);
}

// ---- ecology ----------------------------------------------------------------------------------------------------

void GAMEAPI base_ecology() {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    int32_t div = 4 - f.SE_planet_pending;
    if (div < 1) div = 1;
    else if (div > 99) div = 99;
    f.unk_47 += (cur_base().eco_damage << 2) / div;
    int32_t roll = game_rand() % 100;
    if (roll >= cur_base().eco_damage / ((GameState & 0x100000) ? 2 : 1)) return;
    if (GameRules & 0x200000) return;
    if ((GameState & 1) && !(GameState & 8)) return;
    // Pick the tile where fungus grows: next to fungus, near this base.
    int32_t best = 0, best_idx = -1;
    for (int32_t i = 1; i < 49; i++) {
        int32_t x = wrap_x(cur_base().x + TableOffsetX[i]);
        int32_t y = cur_base().y + TableOffsetY[i];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        MAP* sq = map_tile(x, y);
        if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) continue;
        if (tile_is_base(sq)) continue;
        int32_t weight = 0;
        if (i >= 25) {
            // Outer ring: only while nothing was found, and only tiles nearest to this base.
            if (best) continue;
            int32_t dist = 9999, nearest = -1;
            for (int32_t b = 0; b < BaseCount; b++) {
                int32_t d = vector_dist(wrap_dx(x, Bases[b].x), std::abs(y - Bases[b].y));
                if (d <= dist) {
                    dist = d;
                    nearest = b;
                }
            }
            if (nearest >= 0) BaseFindDist = dist;
            if (nearest != CurrentBaseID) continue;
            weight = 1;
        }
        for (int32_t k = 0; k < 4; k++) {
            int32_t x2 = wrap_x(x + DiagOffsetX[k]);
            int32_t y2 = y + DiagOffsetY[k];
            if (y2 < 0 || y2 >= MapAreaY || x2 < 0 || x2 >= MapAreaX) continue;
            MAP* sq2 = map_tile(x2, y2);
            if ((sq2->items & BIT_FUNGUS) && (sq2->climate & 0xE0) >= 0x40) weight += 2;
        }
        if (!weight) continue;
        if (weight > 1) {
            weight += bit_count(map_tile(x, y)->items & FungusClearItems);
            if (i <= 8) weight++;
        }
        if ((map_tile(x, y)->climate & 0xE0) < 0x60) weight = (weight + 1) / 2;
        weight *= 10;
        int32_t value = weight - 1 > 0 ? game_rand() % weight : 0;
        if (value > best) {
            best = value;
            best_idx = i;
        }
    }
    if (best_idx <= 0) return;
    bool shown = false;
    int32_t improvement = -1;
    int32_t pops = ++f.clean_minerals_modifier;
    int32_t tx = wrap_x(cur_base().x + TableOffsetX[best_idx]);
    int32_t ty = cur_base().y + TableOffsetY[best_idx];
    int32_t count = 1;
    if ((map_tile(tx, ty)->climate & 0xE0) < 0x60 && pops >= 4) {
        int32_t n = 10 - pops / 8 - f.diff_level;
        if (n - 1 <= 0 || !(game_rand() % n)) count = 9;
    }
    for (int32_t k = 0; k < count; k++) {
        int32_t x = wrap_x(tx + TableOffsetX[k]);
        int32_t y = ty + TableOffsetY[k];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        if (k && (map_tile(x, y)->climate & 0xE0) >= 0x60) continue;
        MAP* sq = map_tile(x, y);
        uint32_t items = sq->items;
        if ((items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) continue;
        if (tile_is_base(sq) || (sq->landmarks & 0x80000002) == 2) continue;
        if (improvement < 0) {
            // BUG(original): this search reuses the loop counter, which ends the spreading loop.
            for (k = 0; k < 19; k++)
                if (items & Terraform[k].bit & FungusClearItems) improvement = k;
        }
        bit_set(x, y, BIT_FUNGUS, 1);
        bit_set(x, y, FungusClearItems, 0);
        if ((map_tile(x, y)->climate & 0xE0) < 0x40) world_alt_set(x, y, 2, 0);
        if ((Factions[CurrentPlayerFaction].player_flags & 0x200) ||
            (map_tile(x, y)->visibility & (uint8_t)(1 << CurrentPlayerFaction)) || (GameState & 0x80)) {
            shown = true;
            if (faction_id == CurrentPlayerFaction) {
                Console_focus(&MainConsole, x, y, faction_id);
                if (f.clean_minerals_modifier < 2) interlude(3, 0, 1, 0);
                boom(x, y, 0x80);
            }
            synch_bit(x, y, CurrentPlayerFaction);
            draw_tile(x, y, 2);
        }
    }
    if (shown) {
        if (!ui_shift_pressed() && !MultiplayerActive) clock_wait(20);
        bool notify = faction_id == CurrentPlayerFaction;
        if (!notify) {
            MAP* sq = map_tile(tx, ty);
            int32_t owner = ((sq->items & BIT_VEH_IN_TILE) && (sq->val2 & 0xF) < 8) ? (sq->val2 & 0xF) : -1;
            notify = owner == CurrentPlayerFaction;
        }
        if (notify) {
            pfncMoveMap(MapWins[0], tx, ty, 1);
            Console_focus(&MainConsole, tx, ty, faction_id);
            parse_says(0, cur_base().name, -1, -1);
            if (improvement >= 0 && faction_id == CurrentPlayerFaction) {
                CTerraform& t = Terraform[improvement];
                parse_say(1, (map_tile(tx, ty)->climate & 0xE0) < 0x60 ? t.name_sea : t.name, -1, -1);
                POP2("FUNGUSGROWS2", "fung_sm.pcx", CurrentBaseID);
            } else {
                POP2("FUNGUSGROWS", "fung_sm.pcx", -1);
            }
        }
        if (faction_id == CurrentPlayerFaction && f.clean_minerals_modifier >= 2)
            interlude(5, (int32_t)(CurrentBasePtr + offsetof(BASE, name)), 1, 0);  // base name (image address)
    }
    uint8_t bit = (uint8_t)(1 << faction_id);
    if (!(FactionStatus[0] & bit)) {
        if (!(game_rand() % 2)) {
            int32_t n = DiffLevel + 1;
            if (n - 1 > 0 && game_rand() % n) {
                f.clean_minerals_modifier--;
                return;
            }
        }
    }
    if (f.player_flags & 0x4000) {
        f.player_flags &= ~0x4000u;
        return;
    }
    if (f.clean_minerals_modifier < 7 || SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1) return;
    // Planet strikes back: native life around the fungus.
    if (faction_id == CurrentPlayerFaction) interlude(8, 0, 1, 0);
    if (!(game_rand() % 4) && ExpansionEnabled && (map_tile(tx, ty)->climate & 0xE0) >= 0x60) veh_init(19, 0, tx, ty);
    int32_t spawned = 0, natives = 0;
    for (int32_t k = 0; k < (f.clean_minerals_modifier < 9 ? f.clean_minerals_modifier : 9); k++) {
        int32_t x = wrap_x(tx + TableOffsetX[k]);
        int32_t y = ty + TableOffsetY[k];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        MAP* sq = map_tile(x, y);
        if (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40) continue;
        int32_t dist = 9999, nearest = -1;
        for (int32_t b = 0; b < BaseCount; b++) {
            int32_t d = base_dist(x, y, Bases[b]);
            if (d <= dist) {
                dist = d;
                nearest = b;
            }
        }
        if (nearest < 0) continue;
        BaseFindDist = dist;
        if ((uint8_t)Bases[nearest].faction_id != faction_id) continue;
        if ((sq->items & BIT_VEH_IN_TILE) && (sq->val2 & 0xF) < 8 && (sq->val2 & 0xF) > 0) continue;
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8 && (sq->val2 & 0xF) > 0) continue;
        int32_t region = sq->region;
        int32_t unit_id;
        if (region >= 0x40) unit_id = (spawned % 3) ? 9 : 10;
        else unit_id = f.region_total_bases[region] ? 8 : 10;
        if (unit_id != 8) {
            spawned++;
            if (spawned >= f.base_count * 2 / 3 || spawned >= f.clean_minerals_modifier / 3) continue;
        }
        natives++;
        if (unit_id == 8 && !(game_rand() % 5) && ExpansionEnabled) unit_id = 15;
        int32_t veh_id = veh_init(unit_id, 0, x, y);
        if (veh_id >= 0) Vehs[veh_id].order_auto_type = (uint8_t)faction_id;
        draw_tile(x, y, 2);
    }
    MAP* target = map_tile(tx, ty);
    if (!(target->items & 3) || (target->val2 & 0xF) >= 8 || (target->val2 & 0xF) <= 0) {
        int32_t unit_id = f.region_total_bases[target->region] ? (target->region >= 0x40 ? 9 : 8) : 10;
        while (natives < f.clean_minerals_modifier / 3 && natives < cur_base().pop_size) {
            if (unit_id == 8 && !(game_rand() % 5) && ExpansionEnabled) unit_id = 15;
            veh_init(unit_id, 0, tx, ty);
            natives++;
            if (unit_id == 15) unit_id = 8;
        }
    }
    if ((FactionStatus[0] & bit) || DiffLevel >= 4) {
        // Global warming.
        ClimateLevel++;
        int32_t sea = MapSeaLevel;
        if (sea < 0) sea = 0;
        else if (sea > 100) sea = 100;
        int32_t rises = WorldBuilder.sea_level_rises;
        int32_t r = rises < 1 ? 1 : (rises > 99 ? 99 : rises);
        if (Rules.freq_global_warming_numerator * ClimateLevel / Rules.freq_global_warming_denominator >= sea * 3 / r + 6) {
            ClimateLevel = 0;
            int32_t d = f.clean_minerals_modifier / 6 - 1;
            if (d < 1) d = 1;
            else if (d > 3) d = 3;
            MapSeaLevelCouncil += d;
            ClimateFutureChange += rises * d / 3;
            ClimateValueA = ClimateFutureChange < 0 ? -ClimateFutureChange : ClimateFutureChange;
            ClimateValueB = 20;
            ClimateValueC = 0;
            parse_num(0, 20);
            int32_t change = ClimateFutureChange < 0 ? -ClimateFutureChange : ClimateFutureChange;
            parse_num(1, change * 1000 / 15);
            if (faction_id == CurrentPlayerFaction) {
                popp(ScriptFileName, "TRIGGERWARMING", 0, "searis_sm.pcx", 0);
            } else {
                GenderDefault = MFactions[faction_id].noun_gender;
                PluralDefault = MFactions[faction_id].is_noun_plural;
                parse_says(0, MFactions[faction_id].noun_faction, -1, -1);
                popp(ScriptFileName, "TRIGGERWARMING2", 0, "searis_sm.pcx", 0);
            }
            return;
        }
    }
    if (f.clean_minerals_modifier < 10 || (GameState & 0x20000)) return;
    // Volcano at a random ocean tile with no units around.
    int32_t rx = MapAreaX - 1 > 0 ? game_rand() % MapAreaX : 0;
    int32_t ry = MapAreaY - 1 > 0 ? game_rand() % MapAreaY : 0;
    int32_t vx = rx;
    if (rx & 1) vx = rx - 1;
    if (ry & 1) vx++;
    if (ry < 0 || ry >= MapAreaY || vx < 0 || vx >= MapAreaX) return;
    MAP* vsq = map_tile(vx, ry);
    if ((vsq->climate & 0xE0) >= 0x60) return;
    int32_t k = 0;
    for (; k < 49; k++) {
        int32_t x = wrap_x(vx + TableOffsetX[k]);
        int32_t y = ry + TableOffsetY[k];
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        MAP* sq = map_tile(x, y);
        if ((sq->items & 3) && (sq->val2 & 0xF) < 8) break;
    }
    if (k != 49) return;
    if ((Factions[CurrentPlayerFaction].player_flags & 0x200) || (vsq->visibility & (uint8_t)(1 << CurrentPlayerFaction)))
        Console_focus(&MainConsole, vx, ry, CurrentPlayerFaction);
    GameState |= 0x20000;
    world_volcano(vx, ry, 1);
    world_climate();
    draw_map(1);
    clock_wait(1000);
    MFaction& mp = MFactions[CurrentPlayerFaction];
    PluralDefault = 0;
    GenderDefault = mp.is_leader_female;
    parse_says(0, mp.title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = MFactions[CurrentPlayerFaction].is_leader_female;
    parse_says(1, MFactions[CurrentPlayerFaction].name_leader, -1, -1);
    if ((Factions[CurrentPlayerFaction].player_flags & 0x200) ||
        (map_tile(vx, ry)->visibility & (uint8_t)(1 << CurrentPlayerFaction)))
        popp(ScriptFileName, "VOLCANO", 0, "volc_sm.pcx", 0);
}

// ---- upkeep -------------------------------------------------------------------------------------------------------

int32_t GAMEAPI base_upkeep(int32_t base_id) {
    set_base(base_id);
    uint32_t base_ptr = CurrentBasePtr;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    BaseUpkeepFlag = 0;
    ComputeBaseID = base_id;
    Bases[base_id].state_flags &= 0xDFFFFDFF;
    base_support();
    base_yield();
    base_nutrient();
    base_minerals();
    base_energy_call();
    base_production();
    base_hurry();
    cur_base().minerals_accumulated_2 = cur_base().minerals_accumulated;
    cur_base().production_id_last = cur_base().queue_items[0];
    if (base_growth()) return 1;
    if (CurrentTurn > 1) base_check_support();
    BaseUpkeepState = 1;
    base_compute_inline();
    uint8_t region_plan = f.region_base_plan[map_tile(cur_base().x, cur_base().y)->region];
    int32_t weight = (region_plan == 7 && f.AI_fight <= 0) ? 4 : (region_plan != 2) + 1;
    f.unk_45 += bit_count(cur_base().worked_tiles) * weight;
    BaseUpkeepState = 2;
    if (CurrentTurn > 1) {
        base_drones();
        base_doctors();
        if ((uint8_t)cur_base().faction_id != faction_id) return 1;
        base_energy_costs();
        if (CurrentTurn > 5 || MultiplayerActive || !(GamePreferences & 0x20)) base_research();
    }
    CurrentBaseID = base_id;
    CurrentBasePtr = base_ptr;
    if (ComputeBaseID != base_id) {
        ComputeBaseID = base_id;
        base_support();
        base_yield();
        base_nutrient();
        base_minerals();
        base_energy_call();
    }
    base_ecology();
    f.energy_credits += cur_base().economy_total;
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || (m.rule_flags & RFLAG_ALIEN) || (MFactions[i].rule_flags & RFLAG_ALIEN)) continue;
        f.turn_commerce_income += (!f.sanction_turns && !Factions[i].sanction_turns) ? BaseCommerceImport[i] : 0;
    }
    if (m.rule_flags & RFLAG_ALIEN) {
        int32_t value = 0;
        for (int32_t fac = 0; fac < 70; fac++)
            if (cur_base().facilities_built[fac / 8] & (1 << (fac % 8))) value++;
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] == base_id) value += 5;
        value /= 2;
        if (f.sanction_turns) value = 0;
        f.turn_commerce_income += value;
    }
    if (CurrentTurn > 1) base_maint();
    base_terraform(0);
    if ((cur_base().state_flags & BSTATE_RENAME_BASE) && !(game_rand() % 3) && !(GameState & 1)) {
        // A captured base takes a name of its new owner.
        BASE& b = Bases[CurrentBaseID];
        if ((uint8_t)b.faction_id == CurrentPlayerFaction || (b.visibility & (uint8_t)(1 << CurrentPlayerFaction))) {
            if (!Console_focus(&MainConsole, cur_base().x, cur_base().y, CurrentPlayerFaction))
                draw_tiles(cur_base().x, cur_base().y, 2);
        }
        parse_says(1, cur_base().name, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(0, m.noun_faction, -1, -1);
        name_base(faction_id, cur_base().name, 1, (map_tile(cur_base().x, cur_base().y)->climate & 0xE0) < 0x60);
        parse_says(2, cur_base().name, -1, -1);
        BASE& b2 = Bases[CurrentBaseID];
        if ((uint8_t)b2.faction_id == CurrentPlayerFaction || (b2.visibility & (uint8_t)(1 << CurrentPlayerFaction))) {
            draw_map(1);
            if (m.rule_flags & RFLAG_ALIEN) popp(ScriptFileName, "RENAMEBASE", 0, "al_col_sm.pcx", 0);
            else popp(ScriptFileName, "RENAMEBASE", 0, "colfoun_sm.pcx", 0);
        }
        cur_base().state_flags &= ~BSTATE_RENAME_BASE;
        cur_base().state_flags |= BSTATE_SKIP_RENAME;
    }
    ComputeBaseID = -1;
    if (CurrentBaseID == BaseUpkeepDrawID) BaseWin_on_redraw(&BaseWin);
    if (BaseUpkeepFlag && (!(GameState & 1) || (GameState & 8))) {
        log_say_2("UPKEEP zoom-to-base", base_id, 0, 0);
        BaseWin_zoom(&BaseWin, base_id, 1);
        log_say_2("FINISHED zoom-to-base", base_id, 0, 0);
    }
    BaseUpkeepState = 0;
    BASE& b = cur_base();
    if (b.assimilation_turns_left) {
        b.assimilation_turns_left--;
        if (b.faction_id_former == b.faction_id && b.assimilation_turns_left) b.assimilation_turns_left--;
        if (!b.assimilation_turns_left) b.faction_id_former = b.faction_id;
    }
    if (b.nerve_staple_turns_left) b.nerve_staple_turns_left--;
    b.state_flags &= 0xFFDFFFF7;
    if (!((CurrentBaseID + CurrentTurn) & 7)) cur_base().state_flags &= ~BSTATE_UNK_100000;
    return 0;
}
