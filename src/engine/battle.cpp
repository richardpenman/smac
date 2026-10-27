#include "engine/battle.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/action.h"
#include "engine/base_misc.h"
#include "engine/battle_rules.h"
#include "engine/compat.h"
#include "engine/compat_reads.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/hybrid_shadow.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/zoc.h"
#include "engine/table_at.h"

#ifdef SMACX_HYBRID
extern "C" int32_t GAMEAPI morale_alien_impl_c(int32_t veh_id, int32_t faction_id_vs_native);
#endif

// ---- helpers (not original functions) -----------------------------------------------------------

namespace {
// Raw view of the vehicle table: the original reads Vehs[-1] where a vehicle id may be -1.
// Vehs[-1] lies in the Reactor table (string pointers in the original): read through
// rules_orig_byte.
uint32_t veh_raw_addr(int32_t veh_id, uint32_t offset) { return 0x952828 + (uint32_t)veh_id * 0x34 + offset; }
// Fixed (compat out_of_range_reads): an id outside the table reads as "none": no unit (-1),
// faction 0 (no faction's bonuses), no base's faction (0xFF).
int32_t veh_unit_raw(int32_t veh_id) { return compat_veh_unit(veh_id); }
uint32_t veh_faction_raw(int32_t veh_id) { return compat_veh_faction(veh_id, 0); }
uint32_t base_faction_raw(int32_t base_id) { return compat_base_faction(base_id, 0xFF); }

// Hit points of a unit design: 1 for artifacts, else reactor (1..100) x 10.
int32_t unit_max_hp(int32_t unit_id) {
    if (Units[unit_id].plan == PLAN_ARTIFACT) return 1;
    int32_t reactor = Units[unit_id].reactor_id;
    reactor = reactor < 1 ? 1 : reactor > 100 ? 100 : reactor;
    return reactor * 10;
}
// Remaining hit points of a vehicle (0..9999).
int32_t veh_hp(int32_t veh_id) {
    int32_t hp = unit_max_hp(game().vehs[veh_id].unit_id) - game().vehs[veh_id].damage_taken;
    return hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
}
int32_t chassis_triad(int32_t unit_id) { return Chassis[Units[unit_id].chassis_id].triad; }
bool unit_missile(int32_t unit_id) { return Chassis[Units[unit_id].chassis_id].missile != 0; }
// A unit's properties where the unit may be "none" (-1: veh_unit_raw of no vehicle, fixed): no
// plan, not a missile, no artillery, no ability. With the original's bugs the unit is whatever
// the original read and is looked up as it is.
bool unit_none(int32_t unit_id) { return unit_id < 0 && !g_compat.out_of_range_reads; }
int32_t opt_plan(int32_t unit_id) { return unit_none(unit_id) ? -1 : (int32_t)Units[unit_id].plan; }
bool opt_missile(int32_t unit_id) { return !unit_none(unit_id) && unit_missile(unit_id); }
bool opt_can_arty(int32_t unit_id) { return !unit_none(unit_id) && can_arty(unit_id, 1); }
bool opt_has_abil(int32_t unit_id, uint32_t ability) { return !unit_none(unit_id) && has_abil(unit_id, ability); }

int32_t weapon_value(int32_t unit_id) { return Weapon[Units[unit_id].weapon_id].offense_value; }
int32_t armor_value(int32_t unit_id) { return Armor[Units[unit_id].armor_id].defense_value; }
// Whether faction_id owns secret project sp (SecretProjects index).
bool owns_project(int32_t sp, uint32_t faction_id) {
    return SecretProjects[sp] >= 0 && (uint8_t)game().bases[SecretProjects[sp]].faction_id == faction_id;
}
// Facility test through bitmask(), as the original does.
bool bat_base_fac(int32_t base_id, int32_t facility_id) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (game().bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}
int32_t clamp_morale_bonus(int32_t value, int32_t hi) {
    // SE morale effect undone by a Children's Creche / Brood Pit: clamped, -2..-4 count one less.
    if (value < -4) value = -4;
    else if (value > hi) value = hi;
    if (value < -1) value++;
    return value;
}
// Battle display line (inline version of add_bat).
void bat_line(int32_t side, int32_t value, const char* text) {
    if (value != 0 && VehBattleModCount[side] < 4) {
        int32_t i = VehBattleModCount[side]++;
        VehBattleModifier[side][i] = value;
        crt_strcpy(VehBattleDisplay[side][i], text);
    }
}
char* label(int32_t byte_offset) { return label_get(byte_offset / 4); }
// Image address of a base name (interlude's text argument).
int32_t base_name_addr(int32_t base_id) { return (int32_t)(0x97D040 + base_id * 0x134 + 0x13); }
}  // namespace

// ---- small accessors (0x500120-0x500380) ------------------------------------------------------

void GAMEAPI clear() { StrBuffer[0] = 0; }

void GAMEAPI says(const char* text) { std::strcat(StrBuffer, text); }

int32_t GAMEAPI alt_at(int32_t x, int32_t y) { return map_alt(x, y); }

int32_t GAMEAPI alt_detail_at(int32_t x, int32_t y) { return map_tile(x, y)->contour; }

uint32_t GAMEAPI bit_at(int32_t x, int32_t y) { return map_tile(x, y)->items; }

int32_t GAMEAPI is_ocean(int32_t x, int32_t y) { return map_is_ocean(x, y); }

int32_t GAMEAPI region_at(int32_t x, int32_t y) { return map_tile(x, y)->region; }

int32_t GAMEAPI veh_who(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    if (!(sq->items & BIT_VEH_IN_TILE)) return -1;
    uint32_t faction = sq->val2 & 0xF;
    return faction > 7 ? -1 : (int32_t)faction;
}

int32_t GAMEAPI has_fac_built(int32_t facility_id) {
    int32_t base_id = CurrentBaseID;
    if (facility_id > 0x40) return 0;
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (game().bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}

int32_t GAMEAPI base_project(int32_t project_id) { return SecretProjects[project_id]; }

uint32_t GAMEAPI has_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t treaty) {
    return game().factions[faction_id].has_treaty(faction_id_with, treaty);
}

int32_t GAMEAPI drop_range(int32_t faction_id) {
    if (!has_tech(Rules.tech_preq_orb_insert_wo_space, faction_id)) {
        int32_t base_id = SecretProjects[FAC_SPACE_ELEVATOR - SP_ID_First] /* The Space Elevator */;
        if (base_id < 0 || (uint8_t)game().bases[base_id].faction_id != (uint32_t)faction_id)
            return Rules.max_airdrop_rng_wo_orbital_insert;
    }
    return game().map.x > game().map.y ? game().map.x : game().map.y;
}

void GAMEAPI drop_cover_check(int32_t faction_id, int32_t x, int32_t y) {
    int32_t range;
    if (!has_tech(Rules.tech_preq_orb_insert_wo_space, faction_id) &&
        (SecretProjects[FAC_SPACE_ELEVATOR - SP_ID_First] < 0 ||
         (uint8_t)game().bases[SecretProjects[FAC_SPACE_ELEVATOR - SP_ID_First]].faction_id != (uint32_t)faction_id))
        range = Rules.max_airdrop_rng_wo_orbital_insert;
    else range = game().map.x > game().map.y ? game().map.x : game().map.y;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        VEH& veh = game().vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id || !has_abil(veh.unit_id, ABL_DROP_POD) ||
            (veh.state & VSTATE_MADE_AIRDROP) || veh.order != ORDER_NONE)
            continue;
        int32_t dx = std::abs(x - veh.x);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        int32_t dy = std::abs(y - veh.y);
        if (((dy + dx) >> 1) > range) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return;
        if (sq->items & BIT_AIRBASE) return;  // airbase
    }
}

// ---- planet busters ---------------------------------------------------------------------------

int32_t GAMEAPI planet_buster2(int32_t unit_id) {
    if (Units[unit_id].plan == PLAN_PLANET_BUSTER) return Units[unit_id].reactor_id;
    return 0;
}

int32_t GAMEAPI planet_buster(int32_t veh_id) {
    int32_t unit_id = game().vehs[veh_id].unit_id;
    if (Units[unit_id].plan == PLAN_PLANET_BUSTER) return Units[unit_id].reactor_id;
    return 0;
}

int32_t GAMEAPI shoot(int32_t faction_id_atk, int32_t faction_id_def, int32_t x, int32_t y) {
    // Flechette Defense System of a defender's base within two tiles?
    int32_t flechette = 0;
    for (int32_t i = 0; i < 25; i++) {
        if (flechette) continue;
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!map_in_bounds(x2, y2)) continue;
        int32_t base_id = base_at(x2, y2);
        if (base_id >= 0 && (uint8_t)game().bases[base_id].faction_id != (uint32_t)faction_id_atk &&
            bat_base_fac(base_id, FAC_FLECHETTE_DEFENSE_SYS))
            flechette = 1;
    }
    Faction& def = game().factions[faction_id_def];
    if (def.satellites_ODP == 0 && !flechette) return 0;
    // Orbital Defense Pods not yet used this turn fire with 50% each.
    int32_t hit = 0;
    int32_t shots = def.satellites_ODP - def.ODP_deployed;
    for (int32_t i = 0; i < shots; i++) {
        def.ODP_deployed++;
        if (game_rand() % 100 >= 50) {
            hit = 1;
            break;
        }
    }
    int32_t remaining = def.satellites_ODP - def.ODP_deployed;
    remaining = remaining < 0 ? 0 : remaining > 9999 ? 9999 : remaining;
    const char* pcx;
    const char* text;
    if (!hit) {
        for (int32_t i = 0; i < 25; i++) {
            int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
            int32_t y2 = TableOffsetY[i] + y;
            if (!map_in_bounds(x2, y2)) continue;
            int32_t base_id = base_at(x2, y2);
            if (base_id < 0 || (uint8_t)game().bases[base_id].faction_id == (uint32_t)faction_id_atk) continue;
            if (bat_base_fac(base_id, FAC_FLECHETTE_DEFENSE_SYS) && game_rand() % 100 < 50) {
                parse_says(0, game().bases[base_id].name, -1, -1);
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "WESHOTIT2";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 1;
                    parse_says(1, game().mfactions[(uint8_t)game().bases[base_id].faction_id].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "THEYSHOTIT2";
                }
                popp(ScriptFileName, text, 0, pcx, 0);
                return 1;
            }
        }
        bool failed = def.satellites_ODP == 0;
        if (!failed) {
            // Nothing hit: the defender may sacrifice an Orbital Defense Pod.
            if (!MultiplayerActive && (FactionStatus[0] & (1 << (faction_id_def & 31)))) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_num(0, def.satellites_ODP);
                    int32_t base_id = base_find(x, y);
                    parse_says(0, game().bases[base_id].name, -1, -1);
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    if (popp(ScriptFileName, "BUSTERSAC", 0, "space_sm.pcx", 0) != 0) failed = true;
                } else if (faction_id_atk == CurrentPlayerFaction) {
                    GenderDefault = game().mfactions[faction_id_def].noun_gender;
                    PluralDefault = game().mfactions[faction_id_def].is_noun_plural;
                    parse_says(0, game().mfactions[faction_id_def].noun_faction, -1, -1);
                    popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
                }
            } else if (faction_id_def == CurrentPlayerFaction) {
                popp(ScriptFileName, "WEMUSTSAC", 0, "space_sm.pcx", 0);
            } else if (faction_id_atk == CurrentPlayerFaction) {
                GenderDefault = game().mfactions[faction_id_def].noun_gender;
                PluralDefault = game().mfactions[faction_id_def].is_noun_plural;
                parse_says(0, game().mfactions[faction_id_def].noun_faction, -1, -1);
                popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
            }
        }
        if (failed) {
            if (flechette) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "WEFAILEDFLECHETTE";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 0;
                    parse_says(1, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "THEYFAILEDFLECHETTE";
                }
                popp(ScriptFileName, text, 0, pcx, 0);
            }
            return 0;
        }
        int32_t count = --def.satellites_ODP;
        if (count - def.ODP_deployed < 0) def.ODP_deployed = count;
    }
    int32_t base_id = base_find_faction(x, y, faction_id_def);
    if (base_id < 0) base_id = base_find(x, y);
    parse_says(0, game().bases[base_id].name, -1, -1);
    parse_num(0, def.satellites_ODP);
    parse_num(1, remaining);
    if (faction_id_def == CurrentPlayerFaction) {
        parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        text = "WESHOTIT";
    } else {
        if (faction_id_atk != CurrentPlayerFaction) return 1;
        parse_says(1, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        text = "THEYSHOTIT";
    }
    popp(ScriptFileName, text, 0, pcx, 0);
    return 1;
}

void GAMEAPI planet_busting(int32_t veh_id, int32_t x, int32_t y) {
    int32_t shot_down = 0;
    uint32_t faction_id = game().vehs[veh_id].faction_id;
    Console_focus(&MainConsole, x, y, faction_id);
    int32_t unit_id = game().vehs[veh_id].unit_id;
    int32_t power = Units[unit_id].plan == PLAN_PLANET_BUSTER ? Units[unit_id].reactor_id : 0;
    int32_t tiles = TableRange[power];
    int32_t base_id = base_find(x, y);
    if (faction_id != (uint32_t)CurrentPlayerFaction && base_id >= 0) {
        MFaction& m = game().mfactions[faction_id];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        parse_says(3, game().bases[base_id].name, -1, -1);
        int32_t dx = std::abs(game().bases[base_id].x - x);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        int32_t dy = std::abs(game().bases[base_id].y - y);
        popp(ScriptFileName, "PLANETBUSTER", 0, power < (dy + dx) >> 1 ? "astp_sm.pcx" : "baseobl_sm.pcx", 0);
    }
    // Orbital defense of every faction hit (each faction gets one chance).
    int32_t tried[8] = {};
    for (int32_t i = 0; i < tiles; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!map_in_bounds(x2, y2)) continue;
        int32_t base2 = base_at(x2, y2);
        if (base2 < 0) {
            int32_t veh2 = stack_fix(veh_at(x2, y2));
            if (veh2 < 0) continue;
            uint32_t faction2 = game().vehs[veh2].faction_id;
            if (faction_id == faction2 || (game().factions[faction_id].diplo_status[faction2] & DIPLO_PACT) ||
                tried[faction2])
                continue;
            shot_down = shoot(faction_id, faction2, x, y);
            if (shot_down) break;
            tried[faction2] = 1;
        } else {
            uint32_t faction2 = (uint8_t)game().bases[base2].faction_id;
            if (faction_id == faction2 || tried[faction2]) continue;
            shot_down = shoot(faction_id, faction2, x2, y2);
            if (shot_down) break;
            tried[faction2] = 1;
        }
    }
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (owner >= 0 && (uint32_t)owner != faction_id && !tried[owner]) shot_down = shoot(faction_id, owner, x, y);
    kill(veh_id);
    draw_radius(x, y, 0, 2);
    if (shot_down) return;
    VehBattleState[0] = 1;
    int32_t victim_veh = -1;
    int32_t victim = -1;
    for (int32_t n = power; n > 0; n--) world_lower_alt(x, y);
    world_climate();
    RainfallChangedBase = -1;
    FX_play(&Sounds, 0x3B);
    PlanetBusterBoomSize = power;
    boom(x, y, 0x20);
    for (int32_t i = 0; i < tiles; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!map_in_bounds(x2, y2)) continue;
        int32_t base2 = base_at(x2, y2);
        if (base2 >= 0) {
            if (victim < 0 && (uint8_t)game().bases[base2].faction_id != faction_id)
                victim = (uint8_t)game().bases[base2].faction_id;
            base_kill(base2);
        }
        int32_t veh2 = stack_fix(veh_at(x2, y2));
        if (veh2 >= 0) {
            if (victim_veh < 0 && game().vehs[veh2].faction_id != faction_id) victim_veh = game().vehs[veh2].faction_id;
            stack_kill(veh2);
        }
    }
    VehBattleState[0] = 0;
    if (victim < 0) victim = victim_veh;
    draw_map(0);
    boom(x, y, 0x40);
    ui_battle_wait_sound();
    FX_play(&Sounds, 0x3C);
    boom(x, y, 0x11);
    boom(x, y, 0x12);
    major_atrocity(faction_id, victim);
}

// ---- combat strength ----------------------------------------------------------------------------

int32_t GAMEAPI defense_value(int32_t faction_id, int32_t x, int32_t y, int32_t veh_id_def, int32_t veh_id_atk) {
    MAP* sq = map_tile(x, y);
    if ((sq->climate & 0xE0) < 0x60) return 2;
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return 2;
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F && veh_id_atk >= 0) {
        uint32_t atk_faction = game().vehs[veh_id_atk].faction_id;
        if (atk_faction == 0 || owns_project(16, atk_faction) /* Pholus Mutagen */ ||
            (weapon_value(game().vehs[veh_id_atk].unit_id) < 0 && game().vehs[veh_id_atk].unit_id < 64))
            return 2;
    }
    VehBattleDisplayTerrain = TextLabels[91];  // rocky
    int32_t bonus = (sq->val3 & 0xC0) > 0x40;
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F && !bonus) {
        int32_t def_unit = game().vehs[veh_id_def].unit_id;
        if (chassis_triad(def_unit) != TRIAD_AIR &&
            (veh_id_atk < 0 ||
             ((weapon_value(game().vehs[veh_id_atk].unit_id) >= 0 || game().vehs[veh_id_atk].unit_id >= 64) &&
              (SecretProjects[FAC_PHOLUS_MUTAGEN - SP_ID_First] < 0 ||
               (uint8_t)game().bases[SecretProjects[FAC_PHOLUS_MUTAGEN - SP_ID_First]].faction_id !=
                   game().vehs[veh_id_atk].faction_id)))) {
            bonus = 1;
            if ((SecretProjects[FAC_PHOLUS_MUTAGEN - SP_ID_First] >= 0 &&
                 (uint8_t)game().bases[SecretProjects[FAC_PHOLUS_MUTAGEN - SP_ID_First]].faction_id ==
                     (uint32_t)faction_id) ||
                (def_unit < 64 && weapon_value(def_unit) < 0))
                bonus = 2;
            VehBattleDisplayTerrain = TextLabels[338];  // fungus
        }
    }
    if ((sq->items & BIT_FOREST) && bonus == 0 &&  // forest
        (veh_id_atk < 0 || chassis_triad(game().vehs[veh_id_atk].unit_id) == TRIAD_LAND)) {
        bonus = 1;
        VehBattleDisplayTerrain = TextLabels[291];
    }
    return bonus + 2;
}

int32_t GAMEAPI SHADOWABLE(morale_alien)(int32_t veh_id, int32_t faction_id_vs_native) {
    int32_t morale;
    if (CurrentTurn < 45) morale = 0;
    else if (CurrentTurn < 90) morale = 1;
    else if (CurrentTurn < 170) morale = 2;
    else if (CurrentTurn < 250) morale = 3;
    else morale = CurrentTurn > 329 ? 6 : 4;
    if (faction_id_vs_native > 0) {
        morale += game().factions[faction_id_vs_native].major_atrocities != 0;
        morale += TectonicDetonationCount[faction_id_vs_native] != 0;
    }
    if (veh_id >= 0) {
        const VEH& veh = game().vehs[veh_id];
        if (veh.state & VSTATE_MONOLITH_UPGRADED) morale++;
        if (veh.unit_id == BSC_LOCUSTS_OF_CHIRON) morale++;
        morale += (veh.flags & 0xFF) >> 3 & 3;
    }
    // BUG(original): the Fungal Tower test also runs for veh_id -1, reading Vehs[-1] (the upper half
    // of a Reactor name pointer, never 19 in practice).
    // (compat out_of_range_reads: fixed in veh_unit_raw, no vehicle is no unit.)
    if (veh_unit_raw(veh_id) == BSC_FUNGAL_TOWER) {
        // A tower's morale: adjacent fungus tiles (not deep ocean) minus 2.
        int32_t fungus = 0;
        for (int32_t i = 1; i < 9; i++) {
            int32_t x = map_wrap_x(TableOffsetX[i] + game().vehs[veh_id].x);
            int32_t y = TableOffsetY[i] + game().vehs[veh_id].y;
            if (!map_in_bounds(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F) fungus++;
        }
        morale = fungus - 2;
    }
    return morale < 0 ? 0 : morale > 6 ? 6 : morale;
}

int32_t GAMEAPI psi_factor(int32_t value, int32_t faction_id, int32_t is_attack, int32_t is_fungal_twr) {
    int32_t rule_psi = game().mfactions[faction_id].rule_psi;
    if (rule_psi) value = (rule_psi + 100) * value / 100;
    if (!is_attack) {
        if (owns_project(9, faction_id)) value += value / 2;  // Neural Amplifier
    } else {
        if (SecretProjects[FAC_DREAM_TWISTER - SP_ID_First] < 0) return value;  // Dream Twister
        is_fungal_twr =
            (uint8_t)game().bases[SecretProjects[FAC_DREAM_TWISTER - SP_ID_First]].faction_id == (uint32_t)faction_id;
    }
    if (is_fungal_twr) value += value / 2;
    return value;
}

int32_t GAMEAPI get_basic_offense(int32_t veh_id, int32_t veh_id_def, int32_t psi_combat, int32_t is_bombard,
                                  int32_t is_defender) {
    uint32_t faction_id = game().vehs[veh_id].faction_id;
    int32_t base_morale;
    if (faction_id == 0)
        base_morale = morale_alien(veh_id, veh_id_def < 0 ? -1 : (int32_t)game().vehs[veh_id_def].faction_id);
    else base_morale = morale_veh(veh_id, 1, 0);
    int32_t morale = base_morale;
    int32_t base_id = base_at(game().vehs[veh_id].x, game().vehs[veh_id].y);
    if (base_id >= 0) {
        if (!bat_base_fac(base_id, FAC_CHILDREN_CRECHE)) {
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(game().vehs[veh_id].unit_id))
                morale = base_morale + 1 - clamp_morale_bonus(game().factions[faction_id].SE_morale, 4);
        } else {
            morale = base_morale + 1 - clamp_morale_bonus(game().factions[faction_id].SE_morale, 4);
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(game().vehs[veh_id].unit_id)) morale++;
        }
        if (is_defender) {
            int32_t pending = game().factions[faction_id].SE_morale_pending;
            if (pending > 1 && pending < 4) morale++;
            if (veh_id_def >= 0) {
                if (game().vehs[veh_id_def].faction_id == 0) {
                    morale++;
                } else {
                    int32_t unit_id = game().vehs[veh_id].unit_id;
                    if (!unit_is_native(unit_id) && !has_abil(unit_id, ABL_DISSOCIATIVE_WAVE) &&
                        has_abil(game().vehs[veh_id_def].unit_id, ABL_SOPORIFIC_GAS))
                        morale -= 2;
                }
            }
        }
    }
    if (is_defender) morale = morale < 1 ? 1 : morale > 6 ? 6 : morale;
    int32_t offense = offense_proto(game().vehs[veh_id].unit_id, veh_id_def, is_bombard);
    if (psi_combat) {
        int32_t rule_psi = game().mfactions[faction_id].rule_psi;
        if (rule_psi) offense = (rule_psi + 100) * offense / 100;
        if (owns_project(19, faction_id)) offense += offense / 2;  // Dream Twister
    }
    VehBasicBattleMorale[is_defender != 0] = morale;
    return offense * (morale + 6) * 4;
}

int32_t GAMEAPI get_basic_defense(int32_t veh_id, int32_t veh_id_atk, int32_t psi_combat, int32_t is_bombard) {
    uint32_t faction_id = game().vehs[veh_id].faction_id;
    int32_t morale;
    if (faction_id == 0)
        morale = morale_alien(veh_id, veh_id_atk < 0 ? -1 : (int32_t)game().vehs[veh_id_atk].faction_id);
    else morale = morale_veh(veh_id, 1, 0);
    int32_t base_id = base_at(game().vehs[veh_id].x, game().vehs[veh_id].y);
    bool skip_gas = false;
    if (base_id >= 0) {
        if (!bat_base_fac(base_id, FAC_CHILDREN_CRECHE)) {
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(game().vehs[veh_id].unit_id))
                morale = morale + 1 - clamp_morale_bonus(game().factions[faction_id].SE_morale, 4);
        } else {
            morale = morale + 1 - clamp_morale_bonus(game().factions[faction_id].SE_morale, 0);
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(game().vehs[veh_id].unit_id)) morale++;
        }
        int32_t pending = game().factions[faction_id].SE_morale_pending;
        if (pending > 1 && pending < 4) morale++;
        if (veh_id_atk < 0) skip_gas = true;
        else if (game().vehs[veh_id_atk].faction_id == 0) morale++;
    }
    if (!skip_gas && veh_id_atk >= 0 && game().vehs[veh_id_atk].faction_id != 0) {
        int32_t unit_id = game().vehs[veh_id].unit_id;
        if (!unit_is_native(unit_id) && !has_abil(unit_id, ABL_DISSOCIATIVE_WAVE) &&
            has_abil(game().vehs[veh_id_atk].unit_id, ABL_SOPORIFIC_GAS))
            morale -= 2;
    }
    morale = morale < 1 ? 1 : morale > 6 ? 6 : morale;
    VehBasicBattleMorale[1] = morale;
    int32_t unit_id = game().vehs[veh_id].unit_id;
    if (Units[unit_id].plan == PLAN_ARTIFACT) return 1;
    // BUG(original): without an attacker, Vehs[-1] is read for its unit's plan.
    // (compat out_of_range_reads: fixed, no attacker is not a probe team.)
    if (Units[unit_id].plan == PLAN_PROBE && armor_value(unit_id) == 1 &&
        opt_plan(veh_unit_raw(veh_id_atk)) != PLAN_PROBE)
        return 1;
    int32_t defense = armor_proto(unit_id, veh_id_atk, is_bombard);
    if (psi_combat) {
        int32_t rule_psi = game().mfactions[faction_id].rule_psi;
        if (rule_psi) defense = (rule_psi + 100) * defense / 100;
        if (owns_project(9, faction_id)) defense += defense / 2;  // Neural Amplifier
        if (game().vehs[veh_id].unit_id == BSC_FUNGAL_TOWER) defense += defense / 2;
    }
    return defense * (morale + 6);
}

void GAMEAPI battle_init() {
    VehBattleModCount[0] = 0;
    VehBattleModCount[1] = 0;
    VehBattleState[0] = 0;
    VehBattleState[1] = 0;
}

void GAMEAPI add_bat(int32_t side, int32_t value, const char* text) {
    if (value != 0) bat_line(side, value, text);
}

namespace {
// State of one battle_compute call: the combatants, the combat type and the two strengths.
struct Strength {
    int32_t veh_id_atk, veh_id_def, combat_type;
    int32_t is_arty, is_bombard;
    int32_t psi = 0;  // bit 1: psi attack (the attacker's weapon), bit 2: psi defense (the defender's armor)
    int32_t offense = 8, defense = 8;
};

// Psi combat flags, with the factions' psi bonus lines (rule_psi, Dream Twister, Neural Amplifier,
// Fungal Tower). A bombardment is psi combat only with a spore launcher on either side.
int32_t psi_combat_lines(const Strength& s, int32_t atk_unit_raw) {
    int32_t psi = 0;
    if (s.is_bombard && atk_unit_raw != BSC_SPORE_LAUNCHER && veh_unit_raw(s.veh_id_def) != BSC_SPORE_LAUNCHER)
        return psi;
    if (s.veh_id_atk >= 0 && weapon_value(game().vehs[s.veh_id_atk].unit_id) < 0) psi = 1;
    if (s.veh_id_def >= 0 && armor_value(game().vehs[s.veh_id_def].unit_id) < 0) psi |= 2;
    if (psi) {
        uint32_t faction_id = veh_faction_raw(s.veh_id_atk);
        if (game().mfactions[faction_id].rule_psi) {
            char* text = label(0x558);
            bat_line(0, game().mfactions[faction_id].rule_psi, text);
        }
        if (owns_project(19, faction_id)) bat_line(0, 50, label(0x55C));  // Dream Twister
    }
    uint32_t faction_def = veh_faction_raw(s.veh_id_def);
    if (psi) {
        if (game().mfactions[faction_def].rule_psi) {
            char* text = label(0x558);
            bat_line(1, game().mfactions[faction_def].rule_psi, text);
        }
        if (owns_project(9, faction_def)) bat_line(1, 50, label(0x560));  // Neural Amplifier
        if (s.veh_id_def >= 0 && game().vehs[s.veh_id_def].unit_id == BSC_FUNGAL_TOWER)
            bat_line(1, 50, Units[BSC_FUNGAL_TOWER].name);
    }
    return psi;
}
// Attacks on a defender (combat type 0): fungus, resonance weapons and empaths against psi.
void attack_on_defender(Strength& s) {
    int32_t atk_unit = game().vehs[s.veh_id_atk].unit_id;
    uint32_t faction_atk = game().vehs[s.veh_id_atk].faction_id;
    MAP* sq = map_tile(game().vehs[s.veh_id_def].x, game().vehs[s.veh_id_def].y);
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F &&
        ((atk_unit < 64 && weapon_value(atk_unit) < 0) || owns_project(16, faction_atk)) &&
        weapon_value(game().vehs[s.veh_id_def].unit_id) >= 0) {
        s.offense += s.offense / 2;  // fungus
        bat_line(0, 50, label(0x548));
    }
    int32_t psi_def = s.psi & 2;
    if (psi_def && (Units[game().vehs[s.veh_id_atk].unit_id].weapon_id == WPN_RESONANCE_LASER ||
                    Units[game().vehs[s.veh_id_atk].unit_id].weapon_id == WPN_RESONANCE_BOLT)) {
        s.offense = s.offense * 125 / 100;
        bat_line(0, 25, label(0x1158));
    }
    if (!has_abil(game().vehs[s.veh_id_def].unit_id, ABL_DISSOCIATIVE_WAVE) && psi_def &&
        has_abil(game().vehs[s.veh_id_atk].unit_id, ABL_EMPATH) && Rules.combat_bonus_empath_song_vs_psi) {
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_empath_song_vs_psi);
        char* text = Strings_get(&StringTable, Ability[abil_index(ABL_EMPATH)].name);
        bat_line(0, Rules.combat_bonus_empath_song_vs_psi, text);
    }
}
// The attacker's own modifiers: basic offense, airdrop penalty, fanatic and faction offense
// bonuses, PLANET rating in psi combat (probe teams have none of them).
void attacker_modifiers(Strength& s) {
    s.offense = get_basic_offense(s.veh_id_atk, s.veh_id_def, s.psi, s.is_bombard, 0);
    int32_t atk_unit = game().vehs[s.veh_id_atk].unit_id;
    if (Units[atk_unit].plan == PLAN_PROBE) return;
    if (s.veh_id_def >= 0 && s.combat_type == 0) attack_on_defender(s);
    if ((game().vehs[s.veh_id_atk].state & VSTATE_MADE_AIRDROP) &&
        has_abil(game().vehs[s.veh_id_atk].unit_id, ABL_DROP_POD) && Rules.combat_penalty_atk_airdrop) {
        s.offense = battle_rules::percent_bonus(s.offense, -Rules.combat_penalty_atk_airdrop);
        int32_t faction = (int8_t)game().vehs[s.veh_id_atk].faction_id;
        int32_t range;
        if (has_tech(Rules.tech_preq_orb_insert_wo_space, faction) ||
            (range = Rules.max_airdrop_rng_wo_orbital_insert,
             SecretProjects[FAC_SPACE_ELEVATOR - SP_ID_First] >= 0 &&
                 game().bases[SecretProjects[FAC_SPACE_ELEVATOR - SP_ID_First]].faction_id == faction))
            range = game().map.x > game().map.y ? game().map.x : game().map.y;
        char* text = label(Rules.max_airdrop_rng_wo_orbital_insert < range ? 0x6D8 : 0x6D4);
        bat_line(0, -Rules.combat_penalty_atk_airdrop, text);
    }
    if ((game().mfactions[game().vehs[s.veh_id_atk].faction_id].rule_flags & 0x400) && Rules.combat_bonus_fanatic &&
        s.combat_type == 0 && s.psi == 0) {
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_fanatic);
        char* text = label(0x840);
        bat_line(0, Rules.combat_bonus_fanatic, text);
    }
    uint32_t faction = game().vehs[s.veh_id_atk].faction_id;
    for (int32_t i = 0; i < game().mfactions[faction].faction_bonus_count; i++) {
        if (game().mfactions[faction].faction_bonus_id[i] == 0x12) {  // offense bonus
            int32_t value = game().mfactions[faction].faction_bonus_val1[i];
            s.offense = value * s.offense / 100;
            bat_line(0, value - 100, label(0x1150));
        }
    }
    if (s.psi && game().vehs[s.veh_id_atk].faction_id != 0) {
        uint32_t f = game().vehs[s.veh_id_atk].faction_id;
        if (game().factions[f].SE_planet != 0 && Rules.combat_psi_bonus_per_planet) {
            char* text = label(0x9C4);
            bat_line(0, game().factions[f].SE_planet * Rules.combat_psi_bonus_per_planet, text);
            s.offense = battle_rules::percent_bonus(s.offense,
                                                    game().factions[f].SE_planet * Rules.combat_psi_bonus_per_planet);
        }
    }
}
// Artillery duel (combat type bit 2): the defender fires back with its offense; land/sea and
// altitude modifiers.
void artillery_duel(Strength& s) {
    if (s.veh_id_def < 0) return;
    s.defense = get_basic_offense(s.veh_id_def, s.veh_id_atk, s.psi, s.is_bombard, 1);
    if (s.combat_type & 0x18) return;
    int32_t triad_def = chassis_triad(game().vehs[s.veh_id_def].unit_id);
    int32_t triad_atk = chassis_triad(game().vehs[s.veh_id_atk].unit_id);
    if (triad_def == TRIAD_SEA && triad_atk == TRIAD_LAND) {
        if (!Rules.combat_land_vs_sea_artillery) return;
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_land_vs_sea_artillery);
        char* text = label(0x6CC);
        bat_line(0, Rules.combat_land_vs_sea_artillery, text);
    } else if (triad_atk == TRIAD_SEA && triad_def == TRIAD_LAND) {
        if (!Rules.combat_land_vs_sea_artillery) return;
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_land_vs_sea_artillery);
        char* text = label(0x6CC);
        bat_line(1, Rules.combat_land_vs_sea_artillery, text);
    } else {
        if (!Rules.combat_artillery_bonus_altitude) return;
        uint32_t alt_atk = map_tile(game().vehs[s.veh_id_atk].x, game().vehs[s.veh_id_atk].y)->climate >> 5;
        uint32_t alt_def = map_tile(game().vehs[s.veh_id_def].x, game().vehs[s.veh_id_def].y)->climate >> 5;
        if (alt_def < alt_atk) {
            int32_t value = (int32_t)(alt_atk - alt_def) * Rules.combat_artillery_bonus_altitude;
            s.offense = battle_rules::percent_bonus(s.offense, value);
            char* text = label(0x900);
            bat_line(0, value, text);
        } else if (alt_atk < alt_def) {
            int32_t value = (int32_t)(alt_def - alt_atk) * Rules.combat_artillery_bonus_altitude;
            s.defense = battle_rules::percent_bonus(s.defense, value);
            char* text = label(0x900);
            bat_line(1, value, text);
        }
    }
}
// A land attacker against a defender at (x, y): altitude (artillery), mobile units in the open or
// in rough terrain, roads, elevation.
void land_attack_modifiers(Strength& s, int32_t x, int32_t y, int32_t base_id, int32_t def_unit, int32_t terrain) {
    int32_t atk_unit = game().vehs[s.veh_id_atk].unit_id;
    MAP* sq_atk = map_tile(game().vehs[s.veh_id_atk].x, game().vehs[s.veh_id_atk].y);
    MAP* sq_def = map_tile(x, y);
    if (s.combat_type != 0 && Rules.combat_artillery_bonus_altitude && chassis_triad(def_unit) == TRIAD_LAND) {
        uint32_t alt_atk = sq_atk->climate >> 5;
        uint32_t alt_def = sq_def->climate >> 5;
        if (alt_def < alt_atk) {
            int32_t diff = (int32_t)(alt_atk - alt_def);
            s.offense = battle_rules::percent_bonus(s.offense, diff * Rules.combat_artillery_bonus_altitude);
            char* text = label(0x900);
            bat_line(0, diff * Rules.combat_artillery_bonus_altitude, text);
        }
    }
    if (Rules.combat_mobile_open_ground && s.combat_type == 0 && base_id < 0 && terrain == 2 &&
        (sq_def->val3 & 0xC0) < 0x80 && speed_proto(atk_unit) > Rules.move_rate_roads) {
        int32_t speed_atk = speed_proto(atk_unit);
        int32_t speed_def = speed_proto(def_unit);
        if (speed_def <= speed_atk) {
            s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_mobile_open_ground);
            char* text = label(0x98C);
            bat_line(0, Rules.combat_mobile_open_ground, text);
        }
    }
    if (Rules.combat_mobile_def_in_rough && s.combat_type == 0 && (terrain > 2 || base_id >= 0) &&
        speed_proto(atk_unit) > Rules.move_rate_roads) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_mobile_def_in_rough);
        char* text = label(base_id < 0 ? 0x890 : 0x990);
        bat_line(1, Rules.combat_mobile_def_in_rough, text);
    }
    if (Rules.combat_bonus_atk_road && s.combat_type == 0) {
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_atk_road);
        char* text = label(0x978);
        bat_line(0, Rules.combat_bonus_atk_road, text);
    }
    if (Rules.combat_penalty_atk_lower_elevation && s.combat_type == 0 &&
        (sq_atk->climate & 0xE0) < (sq_def->climate & 0xE0) && !has_abil(atk_unit, ABL_ANTIGRAV_STRUTS)) {
        char* text = label(0x6E4);
        bat_line(1, Rules.combat_penalty_atk_lower_elevation, text);
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_penalty_atk_lower_elevation);
    }
    if (Rules.combat_bonus_atk_higher_elevation && s.combat_type == 0 &&
        (sq_def->climate & 0xE0) < (sq_atk->climate & 0xE0) && !has_abil(def_unit, ABL_ANTIGRAV_STRUTS)) {
        char* text = label(0x528);
        bat_line(0, Rules.combat_bonus_atk_higher_elevation, text);
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_atk_higher_elevation);
    }
}
// Defense multiplier of the defender's tile (x2 normal, x3 bunker, base defenses up to x4 with a
// Tachyon Field), and the intrinsic base defense; also infantry against bases and artillery
// against units in the open.
void tile_defense(Strength& s, int32_t x, int32_t y, int32_t base_id, uint32_t faction_def) {
    MAP* sq = map_tile(x, y);
    char* text_ptr = nullptr;
    int32_t mult = 2;
    if ((sq->items & 0x800) && (s.veh_id_atk < 0 || chassis_triad(game().vehs[s.veh_id_atk].unit_id) != TRIAD_AIR)) {
        text_ptr = TextLabels[0x598 / 4];  // bunker
        mult = 3;
    }
    if (base_id >= 0) {
        text_ptr = TextLabels[0x530 / 4];
        mult = 2;
        // BUG(original): -1 without an attacker (see below). Fixed: the level of an ordinary attack.
        int32_t level = g_compat.level_without_attacker ? s.veh_id_atk : 2;
        if (s.veh_id_atk >= 0) {
            int32_t triad = chassis_triad(game().vehs[s.veh_id_atk].unit_id);
            level = 2;
            if (triad == TRIAD_LAND) {
                if (bat_base_fac(base_id, FAC_PERIMETER_DEFENSE) || owns_project(5, faction_def)) {
                    text_ptr = TextLabels[0x588 / 4];
                    level = 4;
                }
            } else if (triad == TRIAD_SEA) {
                if (bat_base_fac(base_id, FAC_NAVAL_YARD) ||
                    (SecretProjects[FAC_MARITIME_CONTROL_CENTER - SP_ID_First] >= 0 &&
                     game().bases[SecretProjects[FAC_MARITIME_CONTROL_CENTER - SP_ID_First]].faction_id ==
                         game().bases[base_id].faction_id)) {
                    text_ptr = Facility[FAC_NAVAL_YARD].name;
                    level = 4;
                }
            } else if (triad == TRIAD_AIR) {
                if (bat_base_fac(base_id, FAC_AEROSPACE_COMPLEX) ||
                    (SecretProjects[FAC_CLOUDBASE_ACADEMY - SP_ID_First] >= 0 &&
                     game().bases[SecretProjects[FAC_CLOUDBASE_ACADEMY - SP_ID_First]].faction_id ==
                         game().bases[base_id].faction_id)) {
                    text_ptr = Facility[FAC_AEROSPACE_COMPLEX].name;
                    level = 4;
                }
            }
        }
        if (bat_base_fac(base_id, FAC_TACHYON_FIELD)) {
            level += 2;
            text_ptr = TextLabels[0x594 / 4];
        }
        // The Tachyon Field counts only with an attacker (level -1 + 2 stays below 2).
        if ((level < 2 || (mult = level, level == 2)) &&
            (s.veh_id_atk < 0 || !has_abil(game().vehs[s.veh_id_atk].unit_id, ABL_BLINK_DISPLACER)) &&
            Rules.combat_bonus_intrinsic_base_def) {
            char* text = label(0x530);
            bat_line(1, Rules.combat_bonus_intrinsic_base_def, text);
            s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_bonus_intrinsic_base_def);
        }
    }
    if (s.veh_id_atk >= 0 && has_abil(game().vehs[s.veh_id_atk].unit_id, ABL_BLINK_DISPLACER)) {
        if (mult > 2) mult = 2;
        text_ptr = TextLabels[0x6B0 / 4];
        Strings_get(&StringTable, text_ptr);
    }
    if (Rules.combat_infantry_vs_base && s.combat_type == 0 && base_id >= 0 && veh_faction_raw(s.veh_id_atk) != 0) {
        int32_t unit = veh_unit_raw(s.veh_id_atk);
        if (!unit_is_native(unit) && chassis_triad(unit) == TRIAD_LAND && Chassis[Units[unit].chassis_id].speed == 1) {
            s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_infantry_vs_base);
            char* text = label(0x88C);
            bat_line(0, Rules.combat_infantry_vs_base, text);
        }
    }
    if (s.is_arty && mult < 3 && base_id < 0 && (sq->val3 & 0xC0) < 0x80 && !(sq->items & BIT_FOREST) &&
        (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40)) {
        mult = 3;
        text_ptr = TextLabels[0x834 / 4];  // open ground vs artillery
    }
    s.defense = mult * s.defense;
    if (mult > 2) {
        char* text = Strings_get(&StringTable, text_ptr);
        bat_line(1, (mult * 5 - 10) * 10, text);
    }
}
// Conventional (non-psi) defense: terrain, the land attacker's modifiers, the tile's defenses.
void conventional_defense(Strength& s, int32_t x, int32_t y, int32_t base_id, int32_t def_unit, uint32_t faction_def) {
    int32_t terrain;
    if (chassis_triad(def_unit) == TRIAD_AIR) terrain = 2;
    else terrain = defense_value(game().vehs[s.veh_id_def].faction_id, x, y, s.veh_id_def, s.veh_id_atk);
    if (s.veh_id_atk >= 0 && chassis_triad(game().vehs[s.veh_id_atk].unit_id) == TRIAD_LAND)
        land_attack_modifiers(s, x, y, base_id, def_unit, terrain);
    s.defense = terrain * s.defense;
    if (terrain > 2) {
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, label(0x52C));
        crt_strcat(StrBuffer, " ");
        crt_strcat(StrBuffer, "(");
        crt_strcat(StrBuffer, Strings_get(&StringTable, VehBattleDisplayTerrain));
        crt_strcat(StrBuffer, ")");
        bat_line(1, (terrain * 5 - 10) * 10, StrBuffer);
    }
    tile_defense(s, x, y, base_id, faction_def);
}
// Flechette Defense Systems of the defender's bases within two tiles, against missiles.
void flechette_defense(Strength& s, int32_t x, int32_t y) {
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!map_in_bounds(x2, y2)) continue;
        int32_t base2 = base_at(x2, y2);
        if (base2 < 0 || (uint8_t)game().bases[base2].faction_id != game().vehs[s.veh_id_def].faction_id) continue;
        if (bat_base_fac(base2, FAC_FLECHETTE_DEFENSE_SYS)) {
            s.defense = s.defense * 3 / 2;
            bat_line(1, 50, label(0x1164));
        }
    }
}
// Own sensors within two tiles of a land tile (x, y): +combat_defend_sensor, once more for a
// sensor that counts twice.
void sensor_defense(Strength& s, int32_t x, int32_t y) {
    int32_t sensors = 0;
    if (game().vehs[s.veh_id_def].faction_id != 0) {
        for (int32_t i = 0; i < 25; i++) {
            int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
            int32_t y2 = TableOffsetY[i] + y;
            if (!map_in_bounds(x2, y2) || !is_sensor(x2, y2)) continue;
            bool own;
            if ((map_tile(x2, y2)->climate & 0xE0) < 0x60) {
                // BUG(original): base_find may return -1 (no bases): Bases[-1] is read.
                // (compat out_of_range_reads: fixed in base_faction_raw, no base is no faction's.)
                own = base_faction_raw(base_find(x2, y2)) == game().vehs[s.veh_id_def].faction_id;
            } else {
                int32_t owner = whose_territory(game().vehs[s.veh_id_def].faction_id, x2, y2, nullptr, 0);
                own = owner < 0 || (uint32_t)owner == game().vehs[s.veh_id_def].faction_id;
            }
            if (own) {
                sensors |= 1;
                if (is_sensor(x2, y2) > 1) sensors |= 2;
            }
        }
    }
    if (sensors & 1) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_defend_sensor);
        char* text = label(0x994);
        bat_line(1, Rules.combat_defend_sensor, text);
    }
    if (sensors & 2) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_defend_sensor);
        char* text = label(0x118C);
        bat_line(1, Rules.combat_defend_sensor, text);
    }
}
// Abilities and unit types of both sides: air superiority, non-combat units, ships in port,
// pulse armor, comm jammer and AAA tracking against their targets.
void ability_modifiers(Strength& s, int32_t base_id) {
    int32_t def_unit = game().vehs[s.veh_id_def].unit_id;
    int32_t atk_unit = game().vehs[s.veh_id_atk].unit_id;
    if (chassis_triad(atk_unit) == TRIAD_AIR && has_abil(atk_unit, ABL_AIR_SUPERIORITY) && s.psi == 0) {
        if (chassis_triad(def_unit) == TRIAD_AIR) {
            if (Rules.combat_bonus_air_supr_vs_air && !has_abil(def_unit, ABL_AIR_SUPERIORITY)) {
                s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_air_supr_vs_air);
                char* text = label(0x704);
                bat_line(0, Rules.combat_bonus_air_supr_vs_air, text);
            }
        } else if (Rules.combat_penalty_air_supr_vs_ground) {
            s.offense = battle_rules::percent_bonus(s.offense, -Rules.combat_penalty_air_supr_vs_ground);
            char* text = label(0x700);
            bat_line(0, -Rules.combat_penalty_air_supr_vs_ground, text);
        }
    }
    if (chassis_triad(atk_unit) == TRIAD_AIR && chassis_triad(def_unit) == TRIAD_AIR &&
        has_abil(def_unit, ABL_AIR_SUPERIORITY) && !has_abil(atk_unit, ABL_AIR_SUPERIORITY) && s.psi == 0 &&
        !unit_missile(atk_unit) && !unit_missile(def_unit) && Rules.combat_bonus_air_supr_vs_air) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_bonus_air_supr_vs_air);
        char* text = label(0x704);
        bat_line(1, Rules.combat_bonus_air_supr_vs_air, text);
    }
    if (weapon_value(def_unit) == 0 && armor_value(def_unit) == 1 &&
        (game().vehs[s.veh_id_atk].faction_id != 0 || base_id < 0) && Rules.combat_penalty_non_combat_vs_combat) {
        s.defense = battle_rules::percent_bonus(s.defense, -Rules.combat_penalty_non_combat_vs_combat);
        char* text = label(0x6DC);
        bat_line(1, -Rules.combat_penalty_non_combat_vs_combat, text);
    }
    if (chassis_triad(atk_unit) != TRIAD_SEA && chassis_triad(def_unit) == TRIAD_SEA && base_id >= 0 &&
        Rules.combat_bonus_vs_ship_port) {
        s.offense = battle_rules::percent_bonus(s.offense, Rules.combat_bonus_vs_ship_port);
        char* text = label(0x53C);
        bat_line(0, Rules.combat_bonus_vs_ship_port, text);
    }
    int32_t armor = Units[def_unit].armor_id;
    if ((armor == ARM_PULSE_3_ARMOR || armor == ARM_PULSE_8_ARMOR) && chassis_triad(atk_unit) == TRIAD_LAND &&
        Chassis[Units[atk_unit].chassis_id].speed > 1) {
        s.defense = s.defense * 125 / 100;
        bat_line(1, 25, label(0x1160));
    }
    if (!has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && has_abil(def_unit, ABL_COMM_JAMMER) &&
        chassis_triad(atk_unit) == TRIAD_LAND && Chassis[Units[atk_unit].chassis_id].speed > 1 &&
        Rules.combat_comm_jammer_vs_mobile) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_comm_jammer_vs_mobile);
        char* text = label(0x540);
        bat_line(1, Rules.combat_comm_jammer_vs_mobile, text);
    }
    if (!has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && has_abil(def_unit, ABL_AAA) &&
        chassis_triad(atk_unit) == TRIAD_AIR && Rules.combat_aaa_bonus_vs_air) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_aaa_bonus_vs_air);
        char* text = label(0x544);
        bat_line(1, Rules.combat_aaa_bonus_vs_air, text);
    }
}
// The defender's strength in a normal attack: basic defense, faction bonus, terrain and tile,
// Flechettes, sensors, psi defense (trance, resonance armor), then the abilities.
void defender_modifiers(Strength& s) {
    if (s.veh_id_def < 0) return;
    s.defense = get_basic_defense(s.veh_id_def, s.veh_id_atk, s.psi, s.is_bombard);
    uint32_t faction_def = game().vehs[s.veh_id_def].faction_id;
    for (int32_t i = 0; i < game().mfactions[faction_def].faction_bonus_count; i++) {
        if (game().mfactions[faction_def].faction_bonus_id[i] == 0x11) {  // defense bonus
            s.defense = game().mfactions[faction_def].faction_bonus_val1[i] * s.defense / 100;
            char* text = label(0x1154);
            bat_line(1, game().mfactions[faction_def].faction_bonus_val1[i] - 100, text);
        }
    }
    if (s.veh_id_atk >= 0 && Units[game().vehs[s.veh_id_atk].unit_id].plan == PLAN_PROBE) {
        s.defense *= 4;
        return;
    }
    int32_t x = game().vehs[s.veh_id_def].x;
    int32_t y = game().vehs[s.veh_id_def].y;
    int32_t base_id = base_at(x, y);
    int32_t def_unit = game().vehs[s.veh_id_def].unit_id;
    if (s.psi == 0) {
        conventional_defense(s, x, y, base_id, def_unit, faction_def);
    } else {
        if (base_id >= 0 && Rules.combat_bonus_intrinsic_base_def) {
            char* text = label(0x530);
            bat_line(1, Rules.combat_bonus_intrinsic_base_def, text);
            s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_bonus_intrinsic_base_def);
        }
        s.defense = s.defense * 4;
    }
    if (game().vehs[s.veh_id_def].faction_id != 0 && s.veh_id_atk >= 0 &&
        unit_missile(game().vehs[s.veh_id_atk].unit_id))
        flechette_defense(s, x, y);
    if ((map_tile(x, y)->climate & 0xE0) > 0x5F) sensor_defense(s, x, y);  // land tiles only
    int32_t atk_unit = veh_unit_raw(s.veh_id_atk);
    if (!opt_has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && Rules.combat_bonus_trance_vs_psi && s.veh_id_atk >= 0 &&
        (s.psi & 1) && has_abil(game().vehs[s.veh_id_def].unit_id, ABL_TRANCE)) {
        s.defense = battle_rules::percent_bonus(s.defense, Rules.combat_bonus_trance_vs_psi);
        char* text = label(0x524);
        bat_line(1, Rules.combat_bonus_trance_vs_psi, text);
    }
    if (s.veh_id_atk < 0) return;
    if (s.psi & 1) {
        int32_t armor = Units[game().vehs[s.veh_id_def].unit_id].armor_id;
        if (armor == ARM_RESONANCE_3_ARMOR || armor == ARM_RESONANCE_8_ARMOR) {
            s.defense = s.defense * 125 / 100;
            bat_line(1, 25, label(0x115C));
        }
    }
    ability_modifiers(s, base_id);
}
}  // namespace

// Offense and defense of an attack (combat_type bit 1: artillery, bit 2: artillery duel, bits
// 8/0x10: air-to-air), and the modifier lines of the battle window. Always returns 0.
int32_t GAMEAPI battle_compute(int32_t veh_id_atk, int32_t veh_id_def, int32_t* offense_out, int32_t* defense_out,
                               int32_t combat_type) {
    Strength s{veh_id_atk, veh_id_def, combat_type, 0, 0};
    VehBattleModCount[0] = 0;
    VehBattleModCount[1] = 0;
    VehBattleState[0] = 0;
    VehBattleState[1] = 0;
    s.is_arty = combat_type & 1;
    // BUG(original): without an attacker (veh_id_atk -1), Vehs[-1] is read in several tests below.
    // (compat out_of_range_reads: fixed, no attacker has no unit and faction 0.)
    int32_t atk_unit_raw = veh_unit_raw(veh_id_atk);
    s.is_bombard = (s.is_arty || opt_missile(atk_unit_raw)) ? 1 : 0;
    s.psi = psi_combat_lines(s, atk_unit_raw);
    if (veh_id_atk >= 0) attacker_modifiers(s);
    if (combat_type & 2) artillery_duel(s);
    else defender_modifiers(s);
    if (offense_out) *offense_out = s.offense;
    if (defense_out) *defense_out = s.defense;
    return 0;
}

int32_t GAMEAPI best_defender(int32_t veh_id_def, int32_t veh_id_atk, int32_t check_arty) {
    int32_t offense_atk = veh_id_atk < 0 ? 8 : get_basic_offense(veh_id_atk, veh_id_def, 0, 0, 0);
    // Without a defender (veh_id_def -1) the original first reads Vehs[-1].x/y (the halves of the
    // Reactor table's first name pointer: y >= 0x1FB), the tile there and base_at (-1, y out of
    // range), all unused: returning first is equivalent and keeps native reads in bounds.
    if (veh_id_def < 0) return veh_id_def;
    int32_t x = game().vehs[veh_id_def].x;
    int32_t y = game().vehs[veh_id_def].y;
    uint8_t climate = map_tile(x, y)->climate;
    int32_t base_id = base_at(x, y);
    int32_t best_score = -999;
    int32_t best_id = veh_id_def;
    int32_t veh_id = veh_id_def;
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    // BUG(original): without an attacker (veh_id_atk -1), Vehs[-1] is read for its unit.
    // (compat out_of_range_reads: fixed, no attacker has no unit.)
    int32_t atk_unit = veh_unit_raw(veh_id_atk);
    for (; veh_id >= 0; veh_id = game().vehs[veh_id].next_veh_id_stack) {
        int32_t unit_id = game().vehs[veh_id].unit_id;
        if (chassis_triad(unit_id) == TRIAD_LAND && (climate & 0xE0) < 0x60 && base_id < 0) continue;
        if (opt_plan(atk_unit) == PLAN_PROBE && Units[unit_id].plan != PLAN_PROBE) continue;
        int32_t combat_type = (can_arty(unit_id, 1) && opt_can_arty(atk_unit)) ? 2 : 0;
        if (veh_id_atk >= 0 && chassis_triad(atk_unit) == TRIAD_AIR && has_abil(unit_id, ABL_AIR_SUPERIORITY)) {
            if (!unit_missile(unit_id) && !unit_missile(atk_unit) && weapon_value(atk_unit) > 0 &&
                armor_value(atk_unit) > 0 && weapon_value(unit_id) > 0) {
                combat_type = 10;
                if (chassis_triad(unit_id) == TRIAD_AIR) combat_type = 0x1A;
            }
        }
        int32_t offense, defense;
        battle_compute(veh_id_atk, veh_id, &offense, &defense, combat_type);
        if (offense == 0) return best_id;
        uint8_t plan = (uint8_t)Units[unit_id].plan;
        int32_t max_hp = unit_max_hp(unit_id);
        defense = veh_hp(veh_id) * defense / max_hp;
        int32_t ratio = defense * offense_atk / offense;
        int32_t score = ratio / 8 - weapon_value(unit_id);
        if (plan < 8 || plan == 9) score *= 16;
        bool priority;
        bool air_vs_interceptor =
            veh_id_atk >= 0 &&
            !(chassis_triad(atk_unit) != TRIAD_AIR || !has_abil(unit_id, ABL_AIR_SUPERIORITY) ||
              chassis_triad(unit_id) != TRIAD_AIR || unit_missile(unit_id) || unit_missile(atk_unit));
        if (veh_id_atk >= 0 && !air_vs_interceptor) {
            // An air superiority attacker against an aircraft in the open (no base, airbase or carrier).
            MAP* sq = map_tile(x, y);
            bool sheltered = chassis_triad(atk_unit) != TRIAD_AIR || !has_abil(atk_unit, ABL_AIR_SUPERIORITY) ||
                             chassis_triad(unit_id) != TRIAD_AIR || base_id >= 0 || (sq->items & BIT_AIRBASE) ||
                             stack_check(veh_id, 6, 0x80, -1, -1) || unit_missile(unit_id) || unit_missile(atk_unit);
            air_vs_interceptor = !sheltered;
        }
        if (air_vs_interceptor) {
            priority = true;
        } else if (!check_arty) {
            MAP* sq = map_tile(x, y);
            priority = (!stack_check(veh_id, 3, 2, -1, -1) || base_id >= 0 || (sq->items & BIT_AIRBASE) ||
                        stack_check(veh_id, 6, 0x80, -1, -1)) &&
                       (game().vehs[veh_id].state & VSTATE_DESIGNATE_DEFENDER);
        } else {
            priority = can_arty(unit_id, 1) != 0;
        }
        if (priority) score += 0x80000;
        int32_t value = score * 0x800 + veh_id;
        if (best_score < value) {
            best_score = value;
            best_id = veh_id;
        }
    }
    return best_id;
}

// ---- casualties and promotion -------------------------------------------------------------------

int32_t GAMEAPI battle_kill(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                            int32_t veh_id_killer, int32_t faction_id_killer) {
    uint32_t faction_id = game().vehs[veh_id].faction_id;
    int32_t unit_id = game().vehs[veh_id].unit_id;
    game().factions[faction_id].units_lost[unit_id]++;
    if (veh_id_killer >= 0 && veh_id_killer != veh_id && game().vehs[veh_id_killer].faction_id != 0) {
        int32_t ky = game().vehs[veh_id_killer].y;
        int32_t kx = game().vehs[veh_id_killer].x;
        // An alien artifact is captured by a (non-missile) killer on land, carrying it or as an objective.
        if (ky >= 0 && ky < game().map.y && kx >= 0 && kx < game().map.x && Units[unit_id].plan == PLAN_ARTIFACT &&
            !unit_missile(game().vehs[veh_id_killer].unit_id) &&
            ((map_tile(kx, ky)->climate & 0xE0) > 0x5F || stack_check(veh_id_killer, 8, -1, -1, -1) > 0 ||
             (game().vehs[veh_id].flags & VFLAG_IS_OBJECTIVE))) {
            veh_lift(veh_id);
            int32_t unit2 = game().vehs[veh_id].unit_id;
            uint32_t new_faction = game().vehs[veh_id_killer].faction_id;
            game().vehs[veh_id].damage_taken = 0;
            game().vehs[veh_id].faction_id = (uint8_t)new_faction;
            game().vehs[veh_id].order = ORDER_NONE;
            game().factions[faction_id].units_active[unit2]--;
            game().vehs[veh_id].state &= 0xFCFFBDFF;
            game().factions[new_faction].units_active[unit2]++;
            veh_drop(veh_id, game().vehs[veh_id_killer].x, game().vehs[veh_id_killer].y);
            veh_skip(veh_id);
            if (capture_count) (*capture_count)++;
            return 0;
        }
    }
    if ((game().vehs[veh_id].flags & VFLAG_IS_OBJECTIVE) && faction_id_killer >= 0)
        game().factions[faction_id_killer].unk_101++;
    if ((game().vehs[veh_id].state & VSTATE_ASSISTANT_WORM) && veh_id_killer >= 0 &&
        !(GamePreferences & PREF_AV_INTERLUDES_DISABLED)) {
        int32_t base_id = game().vehs[veh_id_killer].home_base_id;
        if (base_id >= 0 || (base_id = base_find_faction(game().vehs[veh_id_killer].x, game().vehs[veh_id_killer].y,
                                                         game().vehs[veh_id_killer].faction_id)) >= 0) {
            PluralDefault = 0;
            uint32_t killer = game().vehs[veh_id_killer].faction_id;
            game().bases[base_id].state_flags |= BSTATE_ASSISTANT_KILLER_HOME;
            game().factions[killer].player_flags |= 0x1000;
            GenderDefault = game().mfactions[killer].is_leader_female;
            parse_says(6, game().mfactions[killer].name_leader, -1, -1);
            killer = game().vehs[veh_id_killer].faction_id;
            GenderDefault = game().mfactions[killer].noun_gender;
            PluralDefault = game().mfactions[killer].is_noun_plural;
            parse_says(7, game().mfactions[killer].noun_faction, -1, -1);
            interlude(6, base_name_addr(base_id), 1, 0);
        }
    }
    // BUG(original): without a killer (veh_id_killer -1), Vehs[-1] is read for its faction.
    // (compat out_of_range_reads: fixed in veh_faction_raw, no killer is faction 0: no reward.)
    if (game().vehs[veh_id].faction_id == 0 && veh_faction_raw(veh_id_killer) != 0 && energy)
        *energy += (morale_veh(veh_id, 1, 0) + 1) * 10;
    if (kill_count) (*kill_count)++;
    veh_kill(veh_id);
    return 1;
}

void GAMEAPI battle_kill_stack(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                               int32_t veh_id_killer, int32_t faction_id_killer) {
    if (veh_id < 0) return;
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    while (veh_id >= 0) {
        int32_t next = game().vehs[veh_id].next_veh_id_stack;
        if (battle_kill(veh_id, kill_count, capture_count, energy, veh_id_killer, faction_id_killer)) {
            // veh_kill renumbers the vehicles after veh_id.
            if (veh_id < veh_id_killer) veh_id_killer--;
            if (veh_id < next) next--;
        }
        veh_id = next;
    }
}

void GAMEAPI battle_kill_chaff(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                               int32_t veh_id_killer, int32_t faction_id_killer) {
    if (veh_id < 0) return;
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    while (veh_id >= 0) {
        int32_t next = game().vehs[veh_id].next_veh_id_stack;
        int32_t hp = unit_max_hp(game().vehs[veh_id].unit_id) - game().vehs[veh_id].damage_taken;
        if ((hp < 1 || game().vehs[veh_id].faction_id == 0) &&
            battle_kill(veh_id, kill_count, capture_count, energy, veh_id_killer, faction_id_killer)) {
            if (veh_id < veh_id_killer) veh_id_killer--;
            if (veh_id < next) next--;
        }
        veh_id = next;
    }
}

void GAMEAPI promote(int32_t veh_id) {
    int32_t unit_id = game().vehs[veh_id].unit_id;
    if (unit_missile(unit_id)) return;
    if (!((uint8_t)Units[unit_id].plan < 8 || armor_value(unit_id) > 1)) return;
    if (morale_veh(veh_id, 1, 0) >= 6 || game().vehs[veh_id].morale >= 6) return;
    if (morale_veh(veh_id, 1, 0) > 2 && game_rand() % 2 != 0) return;
    int32_t before = morale_veh(veh_id, 1, 0);
    uint32_t viewer = CurrentPlayerFaction;
    game().vehs[veh_id].morale++;
    if (game().vehs[veh_id].faction_id == viewer &&
        (FactionStatus[0] & (uint8_t)(1 << (game().vehs[veh_id].faction_id & 31))) &&
        before != morale_veh(veh_id, 1, 0)) {
        parse_says(1, Units[game().vehs[veh_id].unit_id].name, -1, -1);
        if (game().vehs[veh_id].unit_id < 64 && weapon_value(game().vehs[veh_id].unit_id) < 0) {
            parse_say(0, Morale[morale_veh(veh_id, 1, 0)].name_lifecycle, -1, -1);
            NetMsg_pop(&NetMsg, "MORALE2", -5000, 0, "batwon_sm.pcx");
            return;
        }
        parse_say(0, Morale[morale_veh(veh_id, 1, 0)].name, -1, -1);
        NetMsg_pop(&NetMsg, "MORALE", -5000, 0, "batwon_sm.pcx");
    }
}

void GAMEAPI invasions(int32_t base_id) {
    int32_t bx = game().bases[base_id].x;
    int32_t by = game().bases[base_id].y;
    uint32_t base_faction = (uint8_t)game().bases[base_id].faction_id;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        VEH& veh = game().vehs[veh_id];
        uint32_t faction_id = veh.faction_id;
        if (faction_id == 0 || (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) || faction_id == base_faction ||
            (game().factions[faction_id].diplo_status[base_faction] & DIPLO_TREATY) ||
            (map_tile(veh.x, veh.y)->climate & 0xE0) >= 0x60 || !veh_cargo(veh_id) ||
            !stack_check(veh_id, 3, 0, -1, -1))
            continue;
        int32_t dx = std::abs(bx - veh.x);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        int32_t dy = std::abs(by - veh.y);
        dx = std::abs(dx);
        dy = std::abs(dy);
        int32_t big = dx < dy ? dy : dx;
        int32_t small = dy < dx ? dy : dx;
        if (big - (((((dx + dy) >> 1) - small) + 1) >> 1) <= speed(veh_id, 0)) {
            veh.order = ORDER_MOVE_TO;
            veh.waypoint_x[0] = (int16_t)bx;
            veh.waypoint_y[0] = (int16_t)by;
        }
    }
}

int32_t GAMEAPI interceptor(int32_t faction_id_def, int32_t faction_id_atk, int32_t x, int32_t y) {
    int32_t best_id = -1;
    int32_t best_value = 0;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        VEH& veh = game().vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id_def || chassis_triad(veh.unit_id) != TRIAD_AIR ||
            !has_abil(veh.unit_id, ABL_AIR_SUPERIORITY))
            continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if (!((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) && !(sq->items & BIT_AIRBASE) &&
            !stack_check(veh_id, 6, 0x80, -1, -1))
            continue;
        int32_t dx = std::abs(veh.x - x);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        int32_t dy = std::abs(veh.y - y);
        if ((int32_t)((dy + dx) & ~1u) >= 5) continue;
        int32_t weapon = weapon_value(veh.unit_id) > 0 ? weapon_value(veh.unit_id) : 1;
        int32_t value = weapon * veh_hp(veh_id);
        if (best_value <= value) {
            best_id = veh_id;
            best_value = value;
        }
    }
    if (best_id < 0) return 0;
    if (faction_id_def == CurrentPlayerFaction || faction_id_atk == CurrentPlayerFaction) {
        parse_says(0, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
        int32_t cy = game().vehs[best_id].y;
        int32_t cx = game().vehs[best_id].x;
        // BUG(original): base_find may return -1 (no bases): the name before Bases[0] is used.
        int32_t near_base = base_find(cx, cy);
        if (near_base < 0 && !g_compat.out_of_range_reads) parse_says(1, "", -1, -1);  // fixed: no name
        else parse_says(1, table_at(game().bases, kBasesAddr, near_base).name, -1, -1);
        Console_focus(&MainConsole, cx, cy, faction_id_def);
        NetMsg_pop(&NetMsg, "AIRSCRAMBLE", 5000, 0, nullptr);
        // Show the flight to the target, one tile at a time.
        for (;;) {
            int32_t dx = std::abs(cx - x);
            if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
            int32_t dy = std::abs(cy - y);
            if (((dy + dx) & ~1u) == 0) break;
            int32_t best_dir = -1;
            int32_t best_dist = 999;
            int32_t nx = 0, ny = 0;
            for (int32_t dir = 0; dir < 8; dir++) {
                int32_t x2 = map_wrap_x(NearOffsetX[dir] + cx);
                int32_t y2 = NearOffsetY[dir] + cy;
                if (!map_in_bounds(x2, y2)) continue;
                int32_t ddx = std::abs(x2 - x);
                if (!(game().map.flat & 1) && ddx > game().map.half_x) ddx = game().map.x - ddx;
                int32_t dist = (std::abs(y2 - y) + ddx) >> 1;
                if (dist < best_dist) {
                    best_dir = dir;
                    ny = y2;
                    nx = x2;
                    best_dist = dist;
                }
            }
            if (best_dir < 0) break;
            veh_scoot(best_id, cx, cy, best_dir, 0);
            cx = nx;
            cy = ny;
        }
    }
    veh_drop(veh_lift(best_id), x, y);
    game().vehs[best_id].order = ORDER_NONE;
    game().vehs[best_id].state &= 0xFCFFBDFF;
    return 1;
}

// ---- battle ---------------------------------------------------------------------------------------

int32_t GAMEAPI battle_fight(int32_t veh_id, int32_t dir, int32_t is_arty, int32_t is_real, int32_t* def_out) {
    // With the fix of compat out_of_range_reads, enemy_move can rate the way back from a tile whose
    // battle had no defender (veh_id -1): no unit, no battle.
    if (veh_id < 0 && !g_compat.out_of_range_reads) return 0;
    // Artillery fire selects among all tiles within range (TableOffset), a melee attack a neighbour.
    int32_t dx, dy;
    if (dir == -1) {
        // BUG(original): enemy_move rates an attack on its own tile (index 0 of its 9-tile loop) with
        // dir -1. The original reads the dword before each table: NearOffsetX[-1] (0x66EF4C) = 0,
        // NearOffsetY[-1] = NearOffsetX[8] = 0, TableOffsetX[-1] (0x66EFB8) = 5,
        // TableOffsetY[-1] = TableOffsetX[288] = -1.
        // Fixed (compat out_of_range_reads): the own tile, (0, 0) in both tables.
        dx = is_arty && g_compat.out_of_range_reads ? 5 : 0;
        dy = is_arty && g_compat.out_of_range_reads ? -1 : 0;
    } else {
        dx = is_arty ? TableOffsetX[dir] : NearOffsetX[dir];
        dy = is_arty ? TableOffsetY[dir] : NearOffsetY[dir];
    }
    int32_t x = map_wrap_x(game().vehs[veh_id].x + dx);
    int32_t y = game().vehs[veh_id].y + dy;
    return battle_resolve(veh_id, dir, x, y, is_arty, is_real, def_out);
}

// ---- inline equivalents of the diplo_act text wrappers (0x50B8A0-0x50B9FE) -----------------------
namespace {
// say_num (0x50B8A0): appends a number to StrBuffer.
void bat_say_num(int32_t value) {
    char buf[80];
    crt_itoa(value, buf, 10);
    crt_strcat(StrBuffer, buf);
}
// get_noun (0x50B930): the faction's noun, with its gender and plurality as parse defaults.
const char* bat_noun(int32_t faction_id) {
    GenderDefault = game().mfactions[faction_id].noun_gender;
    PluralDefault = game().mfactions[faction_id].is_noun_plural;
    return game().mfactions[faction_id].noun_faction;
}
// FUN_0050B9C0 / NetMsg_pop_2 (0x50B9E0): message box with a 5 s / -5 s delay.
void bat_msg(const char* text, const char* pcx) { NetMsg_pop(&NetMsg, text, 5000, 0, pcx); }
void bat_msg_2(const char* text, const char* pcx) { NetMsg_pop(&NetMsg, text, -5000, 0, pcx); }
// Draws the battle status line (StatusWin_draw on the info window) and marks it shown.
void bat_status(int32_t veh_id_atk, int32_t veh_id_def, int32_t offense, int32_t defense, int32_t flag) {
    StatusWin_draw(&InfoWin, veh_id_atk, veh_id_def, offense, defense, flag);
}
int32_t head_of_stack(int32_t veh_id) {
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
}  // namespace

void GAMEAPI mon_enemy_destroyed(int32_t faction_id, int32_t faction_id_killed) {
    int32_t year = game_year(CurrentTurn);
    MonumentEvent& ev = Monuments[faction_id].events[6];
    if ((game().mfactions[faction_id].rule_flags & 0x80) || ev.achieved || !faction_id_killed) return;
    ev.prev_count = 0;
    for (int32_t i = 0; i < 8; i++)
        if (i != faction_id && Monuments[i].events[6].achieved) ev.prev_count++;
    int32_t first;
    if (ev.prev_count == 0) {
        ev.prev_count = 1;
        first = faction_id;
    } else {
        ev.prev_count = 0;
        first = faction_id_killed;
        for (int32_t i = 0; i < 8; i++)
            if (i != faction_id && Monuments[i].events[6].achieved && Monuments[i].events[6].prev_count) first = i;
    }
    ev.achieved = 1;
    ev.year = year;
    ev.faction_id = first;
    ev.param2 = faction_id_killed;
    Monuments[faction_id].latest_event_id = 6;
    if (faction_id == CurrentPlayerFaction) monument(1);
}

namespace {
// State of one battle_resolve call, shared by its phases.
struct Battle {
    int32_t veh_id, dir, x, y, is_real;
    int32_t arty = 0;  // artillery flags: 1 bombardment, 2 duel, 0x18 air-to-air
    int32_t ax = 0, ay = 0;  // the attacker's tile
    uint32_t atk_f = 0, def_f = 0;
    uint32_t atk_bit = 0;
    int32_t def_id = -1;  // best_defender's choice
    int32_t veh_id_def = -1;  // the defender (def_id; -1 with no unit on the tile)
    int32_t def_unit = 0, atk_unit = 0;
    int32_t base_id = -1;
    int32_t moves_cost = 0;
    int32_t offense = 0, defense = 0;
    int32_t atk_hp = 0, def_hp = 0;
    int32_t atk_power = 1, def_power = 1;  // damage per round dealt to the attacker / defender
    int32_t report_type = 0;  // pact/vendetta battle report for the viewer
    int32_t disengaged = 0;  // the defender withdrew
    int32_t show_interlude = 0;
    int32_t intercepted = 0;
    int32_t captured = 0;  // marine detachment captured the defender's stack
    int32_t nerve = 0;
    char nerve_base[80];
    int32_t atk_unit_saved = 0, def_unit_saved = 0;
    int32_t husk_refresh = 0, display = 0, visible = 0;
    int32_t retreat = 0, marine = 0;  // hit points the defender loses before withdrawing / capture
    int32_t single = 0;  // ship/land artillery duel: one exchange when displayed
    int32_t round_div = 0, last_hit = 0;
    int32_t rx = 0, ry = 0;  // retreat destination
    int32_t atk_left = 0;
    int32_t kills = 0, captures = 0, energy = 0, collateral = 0;
    Battle(int32_t veh, int32_t direction, int32_t tx, int32_t ty, int32_t real)
        : veh_id(veh), dir(direction), x(tx), y(ty), is_real(real) {}
    bool human(uint32_t faction_id) const { return (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0; }
};

// The combatants: artillery flags, interceptors, the best defender, the strengths and hit points.
void prepare_battle(Battle& b, int32_t is_arty) {
    int32_t veh_id = b.veh_id;
    b.arty = is_arty;
    {
        int32_t unit_id = game().vehs[veh_id].unit_id;
        b.arty = (b.arty == 0 && (!can_arty(unit_id, 1) || chassis_triad(unit_id) != TRIAD_LAND)) ? 0 : 1;
    }
    b.ax = game().vehs[veh_id].x;
    b.ay = game().vehs[veh_id].y;
    b.atk_f = game().vehs[veh_id].faction_id;
    int32_t top = veh_at(b.x, b.y);
    MAP* sq = map_tile(b.x, b.y);
    // A ship bombards a land tile where the units are mostly non-combat (or a base).
    if (can_arty(game().vehs[veh_id].unit_id, 1) && veh_triad(veh_id) == TRIAD_SEA && (sq->climate & 0xE0) > 0x5F) {
        int32_t combat = stack_check(top, 3, 2, -1, -1);
        int32_t all = stack_check(top, 1, -1, -1, -1);
        if (combat < all || ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8)) b.arty = 1;
    }
    int32_t def_top = stack_fix(top);
    if (veh_triad(veh_id) == TRIAD_AIR && !unit_missile(game().vehs[veh_id].unit_id) &&
        !stack_check(def_top, 6, 0x20, -1, -1) && b.is_real && (VehAttackFlags & 2))
        b.intercepted = interceptor(veh_faction_raw(def_top), b.atk_f, b.x, b.y);
    b.def_id = best_defender(def_top, veh_id, b.arty);
    b.veh_id_def = b.def_id;
    // BUG(original): with no unit on the target tile (veh_id_def -1; enemy_move rates such attacks
    // with is_real 0), Vehs[-1] is read for the defender's faction, unit and damage.
    // Fixed (compat out_of_range_reads): no defender, no battle (battle_resolve returns 0).
    b.def_f = veh_faction_raw(b.veh_id_def);
    b.def_unit = veh_unit_raw(b.veh_id_def);
    if (b.veh_id_def < 0 && !g_compat.out_of_range_reads) return;
    b.base_id = base_at(b.x, b.y);
    if (can_arty(game().vehs[veh_id].unit_id, 1) && can_arty(b.def_unit, 1)) {
        if (veh_triad(veh_id) == TRIAD_LAND) b.arty |= 2;
        else if (b.arty || (sq->climate & 0xE0) > 0x5F) b.arty |= 2;
    }
    if (has_abil(b.def_unit, ABL_AIR_SUPERIORITY) && veh_triad(veh_id) == TRIAD_AIR &&
        !unit_missile(game().vehs[veh_id].unit_id) && !unit_missile(b.def_unit) &&
        chassis_triad(b.def_unit) == TRIAD_AIR)
        b.arty |= 0x1A;
    if ((b.arty & 3) == 0 || b.arty > 3) {
        b.moves_cost = Rules.move_rate_roads;
    } else {
        b.moves_cost = speed(veh_id, 0) - game().vehs[veh_id].moves_spent;
        b.moves_cost = b.moves_cost < 0 ? 0 : b.moves_cost > 999 ? 999 : b.moves_cost;
    }
    battle_compute(veh_id, b.veh_id_def, &b.offense, &b.defense, b.arty);
    b.atk_hp = veh_hp(veh_id);
    if (b.veh_id_def >= 0) {
        b.def_hp = veh_hp(b.veh_id_def);
    } else {
        b.def_hp = unit_max_hp(b.def_unit) - rules_orig_byte(veh_raw_addr(b.veh_id_def, offsetof(VEH, damage_taken)));
        b.def_hp = b.def_hp < 0 ? 0 : b.def_hp > 9999 ? 9999 : b.def_hp;
    }
    b.atk_unit = game().vehs[veh_id].unit_id;
    // Psi combat: each round's damage is the opponent's reactor (hit points are reactor x 10).
    bool psi = weapon_value(b.atk_unit) < 0 || armor_value(b.def_unit) < 0;
    b.def_power = psi ? Units[b.def_unit].reactor_id : 1;
    b.atk_power = psi ? Units[b.atk_unit].reactor_id : 1;
}
// A human attacker who would break a treaty or pact may call the attack off (break_treaty).
bool treaty_stops_attack(const Battle& b) {
    return (FactionStatus[0] & (uint8_t)b.atk_bit) && Units[b.atk_unit].plan != PLAN_PROBE &&
           (Units[b.def_unit].plan != PLAN_ARTIFACT || b.base_id >= 0) && (VehAttackFlags & 1) &&
           (!MultiplayerActive || !b.intercepted) && break_treaty(b.atk_f, b.def_f, 0xB);
}
// Whether the AI uses nerve gas: with diplo bit 0x40000 toward the defender, when it commits
// atrocities wantonly, or against a human defender while proposal 8 passed more often than 9;
// otherwise only against a human defender with bit 0x20 whose integrity blemishes (plus a margin
// under intense rivalry) exceed 5.
bool ai_uses_nerve_gas(const Battle& b, uint32_t humans) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    bool use = false;
    uint32_t status = game().factions[atk_f].diplo_status[def_f];
    uint32_t def_human = (1u << (def_f & 31)) & humans;
    if (!(status & 0x40000) && !(game().factions[atk_f].player_flags & PFLAG_COMMIT_ATROCITIES_WANTONLY) &&
        (def_human == 0 || ProposalPassCount[8] <= ProposalPassCount[9])) {
        if (def_human && (status & 0x20)) {
            int32_t extra;
            if (!(GameRules & RULES_INTENSE_RIVALRY) ||
                (game().factions[def_f].ranking <= game().factions[atk_f].ranking &&
                 game().factions[def_f].diplo_combat_count[atk_f] <= game().factions[atk_f].diplo_combat_count[def_f]))
                extra = 0;
            else extra = (game().factions[atk_f].AI_fight + 1) * 3;
            if (game().factions[def_f].integrity_blemishes + extra > 5) use = true;
        }
    } else {
        use = true;
    }
    return use;
}
// Nerve gas pods: the human viewer is asked, the AI decides (ai_uses_nerve_gas). Sets b.nerve.
void decide_nerve_gas(Battle& b) {
    int32_t veh_id = b.veh_id;
    if (!has_abil(game().vehs[veh_id].unit_id, ABL_NERVE_GAS) || weapon_value(game().vehs[veh_id].unit_id) < 0 ||
        armor_value(game().vehs[b.veh_id_def].unit_id) < 0)
        return;
    uint32_t humans = *reinterpret_cast<uint32_t*>(&FactionStatus[0]);
    if (!MultiplayerActive || (VehAttackFlags & 1)) {
        uint32_t state = game().vehs[veh_id].state & ~VSTATE_USED_NERVE_GAS;
        game().vehs[veh_id].state = state;
        if (b.atk_f == (uint32_t)CurrentPlayerFaction && (b.atk_bit & (uint8_t)humans)) {
            parse_says(0, Units[game().vehs[veh_id].unit_id].name, -1, -1);
            parse_say(1, Ability[abil_index(ABL_NERVE_GAS)].name, -1, -1);
            BattleWin_stop_timer(&BattleWin);
            if (popp(ScriptFileName, "USENERVE", 0, "biohazd_sm.pcx", 0) != 0)
                game().vehs[veh_id].state |= VSTATE_USED_NERVE_GAS;
            BattleWin_pulse_timer(&BattleWin);
        } else {
            humans &= 0xFF;
            if (!(b.atk_bit & humans) && ai_uses_nerve_gas(b, humans))
                game().vehs[veh_id].state = state | VSTATE_USED_NERVE_GAS;
        }
        if (MultiplayerActive) synch_veh(veh_id);
    }
    if ((VehAttackFlags & 2) && (game().vehs[veh_id].state & VSTATE_USED_NERVE_GAS)) b.nerve = 1;
}
// The attack costs moves; a unit with less than a full move left attacks at reduced strength.
void spend_attack_moves(Battle& b) {
    int32_t veh_id = b.veh_id;
    if (b.base_id >= 0) game().vehs[veh_id].state &= ~0x40000u;
    if (!(game().vehs[veh_id].state & 0x1000) && (VehAttackFlags & 2)) {
        if (b.atk_f != 0 && game().vehs[veh_id].unit_id != BSC_SPORE_LAUNCHER &&
            game().vehs[veh_id].unit_id != BSC_MIND_WORMS) {
            int32_t moves = speed(veh_id, 0) - game().vehs[veh_id].moves_spent;
            moves = moves < 0 ? 0 : moves > 999 ? 999 : moves;
            moves = battle_rules::attack_moves(moves, Rules.move_rate_roads);
            b.offense = moves * b.offense / Rules.move_rate_roads;
            char* text = label(0x54C);
            bat_line(0, (moves - Rules.move_rate_roads) * 100 / Rules.move_rate_roads, text);
        }
        if (b.arty != 1) game().vehs[veh_id].moves_spent += (uint8_t)b.moves_cost;
    }
    game().vehs[veh_id].state = (game().vehs[veh_id].state & ~0x1000u) | 4;
    game().vehs[b.veh_id_def].state |= VSTATE_HAS_MOVED;
}
// Leader title, name and faction noun of a faction as $TITLE0, $NAME1, $FACTION2.
void say_leader(const MFaction& m) {
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.name_leader, -1, -1);
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(2, m.noun_faction, -1, -1);
}
// Diplomatic consequences of the attack: a surprise attack on a human player, battle reports of
// treaty partners for the viewer, the act of aggression, and pact partners of the attacker whose
// territory the attack came from.
void diplomatic_consequences(Battle& b) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    bool surprise = false;
    if (b.human(def_f)) {
        uint32_t status = game().factions[def_f].diplo_status[atk_f];
        surprise = (status & 6) ||
                   !(((status & 8) == 0 && !(game().factions[atk_f].diplo_status[def_f] & 0x800)) || (status & 0x10));
    }
    if (!surprise) {
        if (((game().factions[atk_f].diplo_status[def_f] & DIPLO_TREATY) ||
             (game().factions[def_f].diplo_status[atk_f] & DIPLO_TREATY)) &&
            atk_f != (uint32_t)CurrentPlayerFaction && def_f != (uint32_t)CurrentPlayerFaction) {
            if (!(game().factions[CurrentPlayerFaction].diplo_status[def_f] & DIPLO_PACT)) {
                if (!(game().factions[CurrentPlayerFaction].diplo_status[atk_f] & DIPLO_PACT)) {
                    if (GameState & STATE_OMNISCIENT_VIEW) b.report_type = 1;
                } else {
                    b.report_type = 3;
                }
            } else {
                b.report_type = 2;
            }
        }
    } else {
        MFaction& m = game().mfactions[atk_f];
        if (def_f == (uint32_t)CurrentPlayerFaction) {
            say_leader(m);
            NetMsg_pop(&NetMsg, "SURPRISE", -5000, 0, nullptr);
        } else if (PbemActive) {
            say_leader(m);
            POP2("SURPRISE", nullptr, -1 - (int32_t)def_f);
        }
        game().factions[def_f].diplo_spoke[atk_f] = CurrentTurn;
    }
    act_of_aggression(atk_f, def_f);
    // Pact members of the attacker whose territory the attack came from annoy the defender.
    int8_t owner = map_tile(b.ax, b.ay)->owner;
    for (int32_t i = 1; i < 8; i++) {
        if ((uint32_t)i == atk_f || (uint32_t)i == def_f || owner != i) continue;
        if ((game().factions[atk_f].diplo_status[i] & DIPLO_PACT) &&
            !(game().factions[def_f].diplo_status[i] & DIPLO_VENDETTA) &&
            (game().factions[def_f].diplo_status[i] & DIPLO_COMMLINK) &&
            game().factions[i].mil_strength_1 < game().factions[def_f].mil_strength_1) {
            treaty_on(i, def_f, 0x2000);
            if (game().factions[i].diplo_status[def_f] & 0x800000) cause_friction(def_f, i, 3);
        }
    }
}
// Difficulty adjustments against (and for) human players, natives' strength, and the first turns.
void difficulty_modifiers(Battle& b) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    int32_t level = 3 - (atk_f != 0);
    uint8_t humans = FactionStatus[0];
    if (game().factions[def_f].diff_level < level) {
        if ((humans & (1 << (def_f & 31))) && !(b.atk_bit & humans))
            b.offense = (game().factions[def_f].diff_level + 1) * b.offense / 4;
    }
    if (game().factions[atk_f].diff_level < level && (b.atk_bit & humans) && !(humans & (1 << (def_f & 31))))
        b.offense = (4 - game().factions[atk_f].diff_level) * b.offense / 2;
    if (atk_f == 0) {
        if (!b.human(def_f)) {
            if (Units[game().vehs[b.veh_id_def].unit_id].plan == PLAN_TERRAFORM) {
                b.offense /= 2;
                if (!(GameState & STATE_OMNISCIENT_VIEW) && !MultiplayerActive &&
                    (game().vehs[b.veh_id_def].faction_id != (uint32_t)CurrentPlayerFaction ||
                     (game().vehs[b.veh_id_def].flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) == 0x440) &&
                    !(game().vehs[b.veh_id_def].visibility & (1 << (CurrentPlayerFaction & 31))))
                    b.offense /= 2;
            } else {
                b.offense = (game().factions[def_f].ranking + 8) * b.offense / 16;
            }
        }
        if (b.base_id >= 0) {
            if (def_f == (uint32_t)CurrentPlayerFaction) b.show_interlude = 1;
            if (game().factions[def_f].base_count < 2) b.offense /= 2;
            if (CurrentTurn < 50) b.offense /= 2;
            if (bat_base_fac(b.base_id, FAC_HEADQUARTERS) && game().factions[def_f].base_count < 4) b.offense /= 2;
        }
    }
    if (CurrentTurn < 15) {
        if (atk_f == 0) b.offense /= 3;
        if (def_f == 0) b.defense /= 2;
    }
}
// Strength as "N.M" ($slot): the 8.8 fixed point value with one decimal.
void say_strength(int32_t slot, int32_t value) {
    char num[80];
    StrBuffer[0] = 0;
    crt_itoa(value >> 8, num, 10);
    crt_strcat(StrBuffer, num);
    crt_strcat(StrBuffer, ".");
    crt_itoa((int32_t)((value & 0xFF) * 10) >> 8, num, 10);
    crt_strcat(StrBuffer, num);
    parse_says(slot, StrBuffer, -1, -1);
}
// The battle window with the odds, before the viewer's question.
void show_odds(const Battle& b) {
    SubInterface_set_iface_mode(&BattleWin);
    BattleWin_clear(&BattleWin);
    bat_status(b.veh_id, b.veh_id_def, b.offense, b.defense, 0);
    InfoWinBattleShown = 1;
    MFaction& m = game().mfactions[CurrentPlayerFaction];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(1, m.name_leader, -1, -1);
}
// The human viewer is warned about bad odds (BADIDEA), or shown the odds when the preference asks
// for it (GOODIDEA). Returns false when the attack is called off (the moves are refunded).
bool confirm_attack(Battle& b) {
    int32_t veh_id = b.veh_id;
    const char* text;
    if (b.atk_hp * b.def_power * b.offense * 2 < b.def_hp * b.atk_power * b.defense) {
        show_odds(b);
        BattleWin_stop_timer(&BattleWin);
        text = "BADIDEA";
    } else if (GameMorePreferences & MPREF_ADV_CONFIRM_ODDS_BF_ATTACKING) {
        show_odds(b);
        say_strength(2, b.offense);
        say_strength(3, b.defense);
        int32_t a = veh_hp(veh_id) * b.offense;
        int32_t d = veh_hp(b.veh_id_def) * b.defense;
        battle_rules::reduce_odds(a, d);
        BattleWin_stop_timer(&BattleWin);
        parse_num(0, a);
        parse_num(1, d);
        text = (game().vehs[veh_id].damage_taken || game().vehs[b.veh_id_def].damage_taken) ? "GOODIDEA2" : "GOODIDEA";
    } else {
        return true;
    }
    if (popp(ScriptFileName, text, 0, "hasty_sm.pcx", 0) == 0) {
        BattleWin_pulse_timer(&BattleWin);
        if (VehAttackFlags & 2) game().vehs[veh_id].moves_spent -= (uint8_t)b.moves_cost;
        game().vehs[veh_id].order = ORDER_NONE;
        game().vehs[veh_id].state &= 0xFCFFBDFF;
        StatusWin_reset(&InfoWin);
        StatusWin_redraw(&InfoWin);
        return false;
    }
    BattleWin_pulse_timer(&BattleWin);
    return true;
}
// Whether the battle is shown: the viewer takes part (or the omniscient view), or it is visible
// and the viewer's preferences show battles of others.
void battle_visibility(Battle& b) {
    uint32_t viewer = CurrentPlayerFaction;
    b.display = (b.atk_f == viewer || b.def_f == viewer || (GameState & STATE_OMNISCIENT_VIEW)) ? 1 : 0;
    uint8_t vbit = (uint8_t)(1 << (viewer & 31));
    const VEH& atk = game().vehs[b.veh_id];
    const VEH& def = game().vehs[b.veh_id_def];
    b.visible = ((atk.faction_id == viewer && (atk.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
                 (atk.visibility & vbit) ||
                 (def.faction_id == viewer && (def.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
                 (def.visibility & vbit))
                    ? 1
                    : 0;
    uint32_t pref = (game().factions[viewer].diplo_status[b.atk_f] & DIPLO_PACT)
                        ? (GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ALLY_VEH)
                        : (GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ENEMY_VEH);
    if (pref) b.display |= b.visible;
}
// The pause after a displayed battle (BATTLEPAUSE), switched off by its dialog toggle.
void battle_pause() {
    BattleWin_stop_timer(&BattleWin);
    popp(ScriptFileName, "BATTLEPAUSE", 0, "batwon_sm.pcx", 0);
    BattleWin_pulse_timer(&BattleWin);
    if (DialogToggle) {
        GameMorePreferences &= ~MPREF_ADV_PAUSE_AFTER_BATTLES;
        prefs_save(0);
    }
}
// Bombardment of one defender v: random damage, never below the hit points that bombardment
// cannot remove (more in bases and bunkers); shown to the viewer.
void bombard_unit(Battle& b, int32_t v, bool& paused) {
    int32_t veh_id = b.veh_id, x = b.x, y = b.y;
    veh_skip(veh_id);
    battle_compute(veh_id, v, &b.offense, &b.defense, 1);
    int32_t rounds = battle_rules::bombard_damage_range(Rules.artillery_dmg_numerator, Rules.artillery_dmg_denominator,
                                                        b.offense, b.defense);
    int32_t damage = rounds - 1 > 0 ? game_rand() % rounds : 0;
    int32_t keep;
    if (b.base_id < 0 && !(map_tile(x, y)->items & 0x800)) {
        int32_t pct = chassis_triad(game().vehs[v].unit_id) != TRIAD_LAND ? Rules.max_dmg_percent_arty_sea
                                                                          : Rules.max_dmg_percent_arty_open;
        keep = battle_rules::bombard_hp_floor(pct, unit_max_hp(game().vehs[v].unit_id));
    } else {
        keep =
            battle_rules::bombard_hp_floor(Rules.max_dmg_percent_arty_base_bunker, unit_max_hp(game().vehs[v].unit_id));
    }
    for (; damage > 0; damage--)
        if (keep <= veh_hp(v) - damage) break;
    if (b.display) {
        paused = false;
        VehDrawDefendID = v;
        game().vehs[veh_id].visibility |= (uint8_t)(1 << (b.def_f & 31));
        VehDrawAttackID = veh_id;
        if (!Console_focus(&MainConsole, x, y, b.atk_f)) {
            draw_tile(b.ax, b.ay, 2);
            draw_tile(x, y, 2);
        }
        SubInterface_set_iface_mode(&BattleWin);
        BattleWin_clear(&BattleWin);
        bat_status(veh_id, v, b.offense, b.defense, 1);
        InfoWinBattleShown = 1;
        flush_input();
        if (keep == 0 || game().vehs[v].damage_taken != (uint32_t)keep || v < 0 ||
            game().vehs[v].next_veh_id_stack < 0) {
            boom(x, y, 0);
            paused = true;
        }
    }
    game().vehs[v].damage_taken += (uint8_t)damage;
    game().vehs[v].state |= VSTATE_HAS_MOVED;
    if (Units[game().vehs[v].unit_id].plan == PLAN_TERRAFORM) game().vehs[v].movement_turns = 0;
    if (b.base_id < 0) game().vehs[v].state |= 8;
    if (b.display) {
        bat_status(veh_id, v, b.offense, b.defense, 1);
        draw_tile(x, y, 2);
        VehDrawAttackID = -1;
        VehDrawDefendID = -1;
        if (!(GameMorePreferences & MPREF_ADV_PAUSE_AFTER_BATTLES)) {
            if (!ui_shift_pressed() && !(GamePreferences & PREF_ADV_FAST_BATTLE_RESOLUTION) && !MultiplayerActive &&
                paused)
                clock_wait(200);
        } else {
            battle_pause();
        }
    }
}
// Bombardment: every defender of the stack takes damage (air units only from air superiority
// units, or in a base or airbase), none is killed outright; then the units without hit points
// die.
void bombard(Battle& b) {
    int32_t x = b.x, y = b.y;
    add_goal(b.def_f, 0x10, 4, x, y, -1);
    add_goal(b.def_f, 0, 4, b.ax, b.ay, -1);
    if (b.base_id >= 0) {
        game().bases[b.base_id].state_flags |= 0x200000;
        game().bases[b.base_id].event_flags |= 0x100;
    }
    if (b.def_id >= 0) {
        bool paused = true;
        int32_t v = head_of_stack(b.veh_id_def);
        while (v >= 0) {
            int32_t v_unit = game().vehs[v].unit_id;
            if (chassis_triad(v_unit) != TRIAD_AIR || has_abil(game().vehs[b.veh_id].unit_id, ABL_AIR_SUPERIORITY) ||
                b.base_id >= 0 || (map_tile(x, y)->items & BIT_AIRBASE))
                bombard_unit(b, v, paused);
            if (v < 0) break;
            v = game().vehs[v].next_veh_id_stack;
        }
    }
    int32_t v = stack_fix(veh_at(x, y));
    while (v >= 0) {
        if (unit_max_hp(game().vehs[v].unit_id) - game().vehs[v].damage_taken < 1) {
            stack_veh(v, 2);
            stack_kill(v);
            v = stack_fix(veh_at(x, y));
            game().factions[b.atk_f].diplo_combat_total[b.def_f]++;
            game().factions[b.atk_f].diplo_combat_count[b.def_f]++;
        } else {
            v = game().vehs[v].next_veh_id_stack;
        }
    }
    draw_tile_fixup(x, y, 1, 2);
    if (b.base_id >= 0 && !b.human(b.def_f)) base_reset(b.base_id, 0);
}
// The viewer's battle window at the start of the fight; the attacker steps toward the defender.
void show_battle_start(Battle& b) {
    int32_t veh_id = b.veh_id;
    if (MultiplayerActive && BaseWinMultiplayerRefresh) b.husk_refresh = 1;
    game().vehs[veh_id].visibility |= (uint8_t)(1 << (b.def_f & 31));
    VehDrawAttackID = veh_id;
    VehDrawDefendID = b.veh_id_def;
    if (!Console_focus(&MainConsole, b.x, b.y, b.atk_f)) {
        draw_tile(b.ax, b.ay, 2);
        draw_tile(b.x, b.y, 2);
    }
    stack_veh(veh_id, 1);
    SubInterface_set_iface_mode(&BattleWin);
    BattleWin_clear(&BattleWin);
    bat_status(veh_id, b.veh_id_def, b.offense, b.defense, b.arty);
    InfoWinBattleShown = 1;
    flush_input();
    if (!ui_shift_pressed() && !(b.arty & 1)) veh_scoot(veh_id, b.ax, b.ay, b.dir, 1);
    stack_put(veh_id, b.ax, b.ay);
    draw_tile(b.ax, b.ay, 2);
    draw_tile(b.x, b.y, 2);
}
// Hit points after which a faster defender alone in the open withdraws (half of its own), else 0.
int32_t withdraw_threshold(const Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def;
    int32_t speed_def = speed(veh_id_def, 1);
    int32_t speed_atk = speed(veh_id, 1);
    int32_t du = game().vehs[veh_id_def].unit_id;
    if (speed_atk < speed_def && b.arty == 0 && weapon_value(du) != 0 && !(game().vehs[veh_id_def].state & 0x40) &&
        veh_triad(veh_id) != TRIAD_AIR && chassis_triad(du) != TRIAD_AIR &&
        (!has_abil(game().vehs[veh_id].unit_id, ABL_COMM_JAMMER) ||
         has_abil(game().vehs[veh_id_def].unit_id, ABL_DISSOCIATIVE_WAVE)) &&
        game().vehs[veh_id_def].order != ORDER_HOLD && b.base_id < 0 && !(map_tile(b.x, b.y)->items & 0x40800) &&
        stack_check(veh_id_def, 1, -1, -1, -1) == 1)
        return (veh_hp(veh_id_def) + 1) / 2;
    return 0;
}
// Hit points after which a marine detachment may capture a ship at sea (two thirds), else 0.
int32_t marine_threshold(const Battle& b) {
    int32_t veh_id = b.veh_id;
    if (!has_abil(game().vehs[veh_id].unit_id, ABL_MARINE_DETACHMENT) || veh_triad(veh_id) != TRIAD_SEA ||
        (map_tile(b.x, b.y)->climate & 0xE0) >= 0x60)
        return 0;
    int32_t du = game().vehs[b.veh_id_def].unit_id;
    if (chassis_triad(du) == TRIAD_SEA && !unit_is_native(du) && b.base_id < 0) return veh_hp(b.veh_id_def) * 2 / 3;
    return 0;
}
// The marines take over the defender's stack.
void capture_by_marines(Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def, x = b.x, y = b.y;
    uint32_t old_f = game().vehs[veh_id_def].faction_id;
    b.captured = game().vehs[veh_id].faction_id;
    if (b.def_id >= 0) {
        for (int32_t v = head_of_stack(veh_id_def); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            game().vehs[v].faction_id = game().vehs[veh_id].faction_id;
            parse_says(0, Units[game().vehs[v].unit_id].name, -1, -1);
            int32_t base = base_find(x, y);
            // BUG(original): base_find may return -1 (no bases): Bases[-1] is read.
            const uint8_t* braw = table_bytes(game().bases, kBasesAddr, base);
            if ((base >= 0 || g_compat.out_of_range_reads) &&  // fixed: no base, no home base
                braw[offsetof(BASE, faction_id)] == game().vehs[veh_id].faction_id &&
                (int8_t)braw[offsetof(BASE, pop_size)] > 2)
                game().vehs[v].home_base_id = (int16_t)base;
            else game().vehs[v].home_base_id = -1;
            veh_skip(v);
        }
    }
    bit_set(x, y, 0x100000, 1);
    owner_set(x, y, game().vehs[veh_id].faction_id);
    if (game().vehs[veh_id].faction_id == (uint32_t)CurrentPlayerFaction) draw_tile_fixup(x, y, 1, 2);
    else draw_tile(x, y, 2);
    if ((uint32_t)b.captured == (uint32_t)CurrentPlayerFaction) {
        parse_says(1, game().mfactions[old_f].adj_name_faction, -1, -1);
        popp(ScriptFileName, "WEMARINEDETACH", 0, "navun_sm.pcx", 0);
    }
    if (old_f == (uint32_t)CurrentPlayerFaction) {
        parse_says(1, game().mfactions[b.captured].adj_name_faction, -1, -1);
        popp(ScriptFileName, "THEYMARINEDETACH", 0, "navun_sm.pcx", 0);
    }
    log_say_values("Detachment", 0, 0, 0);
    b.captured = 1;
}
// The defender withdraws to the free tile that turns most away from the attack (more than 90
// degrees). Returns true if it found one (b.rx, b.ry; the last tile examined otherwise).
bool withdraw_defender(Battle& b) {
    int32_t x = b.x, y = b.y;
    uint32_t def_f = b.def_f;
    int32_t best_dir = -1;
    uint32_t best = 0;
    for (int32_t d = 0; d < 8; d++) {
        int32_t x2 = map_wrap_x(NearOffsetX[d] + x);
        int32_t y2 = NearOffsetY[d] + y;
        b.rx = x2;
        b.ry = y2;
        if (!map_in_bounds(x2, y2) || zoc_move(x2, y2, def_f)) continue;
        int32_t du = game().vehs[b.veh_id_def].unit_id;
        MAP* sq2 = map_tile(x2, y2);
        if (!(chassis_triad(du) == TRIAD_AIR || ((sq2->climate & 0xE0) < 0x60) == (chassis_triad(du) == TRIAD_SEA)))
            continue;
        if (goody_at(x2, y2)) continue;
        if (!(!(sq2->items & BIT_FUNGUS) || (sq2->climate & 0xE0) < 0x40 || owns_project(16, def_f) ||
              unit_is_native(du)))
            continue;
        uint32_t occupant = sq2->val2 & 0xF;
        if ((sq2->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && occupant <= 7 && occupant != def_f &&
            !(game().factions[occupant].diplo_status[def_f] & DIPLO_PACT))
            continue;
        uint32_t diff = (uint32_t)battle_rules::withdraw_angle(d, b.dir);
        if ((int32_t)diff > 2 && (int32_t)best < (int32_t)diff) {
            best = diff;
            best_dir = d;
        }
    }
    if (best_dir < 0) return false;
    b.rx = map_wrap_x(NearOffsetX[best_dir] + x);
    b.ry = NearOffsetY[best_dir] + y;
    stack_put(stack_veh(b.veh_id_def, 1), b.rx, b.ry);
    b.disengaged = 1;
    return true;
}
// The combat rounds until one side has no hit points left (or the marines capture the defender,
// or it withdraws, or a displayed duel ends after one exchange): each round, the side with the
// lower random roll below its strength takes the other's power in damage.
void fight_rounds(Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def;
    for (;;) {
        if (veh_hp(veh_id) == 0 || veh_hp(veh_id_def) == 0) break;
        int32_t roll_atk = b.offense - 1 > 0 ? game_rand() % b.offense : 0;
        int32_t roll_def = b.defense - 1 > 0 ? game_rand() % b.defense : 0;
        if (roll_atk <= roll_def) {
            uint8_t damage = (uint8_t)(game().vehs[veh_id].damage_taken + b.atk_power);
            game().vehs[veh_id].damage_taken = damage;
            // BUG(original): the single exchange of a ship/land artillery duel ends early only when
            // the battle is displayed.
            // Fixed: the exchange ends alike, the hit only drawn when displayed.
            if ((!b.display && g_compat.duel_ends_if_displayed) || !b.round_div ||
                (int32_t)damage / (b.round_div * b.atk_power) ==
                    (int32_t)(damage - b.atk_power) / (b.round_div * b.atk_power))
                continue;
            b.last_hit = 1;
            if (b.display) {
                bat_status(veh_id, veh_id_def, b.offense, b.defense, b.arty);
                boom(b.ax, b.ay, 9);
                if (veh_hp(veh_id) > 0) {
                    draw_tile(b.ax, b.ay, 2);
                    draw_tile(b.x, b.y, 2);
                }
            }
            if (!b.single) continue;
            break;
        }
        uint8_t damage = (uint8_t)(game().vehs[veh_id_def].damage_taken + b.def_power);
        game().vehs[veh_id_def].damage_taken = damage;
        if ((b.display || !g_compat.duel_ends_if_displayed) && b.round_div &&
            (int32_t)damage / (b.round_div * b.def_power) !=
                (int32_t)(damage - b.def_power) / (b.round_div * b.def_power)) {
            b.last_hit = 2;
            if (b.display) {
                bat_status(veh_id, veh_id_def, b.offense, b.defense, b.arty);
                boom(b.x, b.y, 1);
                if (veh_hp(veh_id_def) > 0) {
                    draw_tile(b.ax, b.ay, 2);
                    draw_tile(b.x, b.y, 2);
                }
            }
            if (b.single) break;
        }
        if (b.marine && veh_hp(veh_id_def) > 0 && (b.marine -= b.def_power) < 1 && game_rand() % 100 < 50) {
            capture_by_marines(b);
            break;
        }
        if (b.retreat && veh_hp(veh_id_def) > 0 && (b.retreat -= b.def_power) < 1) {
            if (withdraw_defender(b)) break;
            b.retreat = 0;
        }
    }
}
// The explosions of the battle's end, for the viewer.
void show_outcome(const Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def, ax = b.ax, ay = b.ay, x = b.x, y = b.y;
    draw_tile_fixup_pair(ax, ay, x, y, 2, 2);
    if (b.disengaged && (ax != b.rx || ay != b.ry) && (x != b.rx || y != b.ry)) draw_tile_fixup(b.rx, b.ry, 2, 2);
    if (!b.display) return;
    bat_status(veh_id, veh_id_def, b.offense, b.defense, b.arty);
    if (!b.disengaged) {
        if (b.atk_left == 0) {
            if (!b.single) {
                if (b.last_hit == 1 || MultiplayerActive) {
                    if (!(veh_hp(veh_id) > 0 && !MultiplayerActive)) boom(ax, ay, veh_hp(veh_id) != 0 ? 0 : 2);
                } else {
                    boom(ax, ay, veh_hp(veh_id) != 0 ? 1 : 2);
                }
            } else {
                if (b.last_hit != 2 && !MultiplayerActive) boom(x, y, veh_hp(veh_id_def) != 0);
                boom(ax, ay, veh_hp(veh_id) != 0 ? 1 : 2);
            }
        } else if (!b.single) {
            if (b.last_hit == 2 ? (MultiplayerActive || veh_hp(veh_id_def) == 0) : MultiplayerActive)
                boom(x, y, (veh_hp(veh_id_def) != 0 ? 0 : 2) | 8);
            else if (b.last_hit != 2) boom(x, y, (2 - (veh_hp(veh_id_def) != 0)) | 8);
        } else {
            if (b.last_hit != 1 && !MultiplayerActive) boom(ax, ay, (2 - (veh_hp(veh_id) != 0)) | 8);
            boom(x, y, (2 - (veh_hp(veh_id_def) != 0)) | 8);
        }
    }
    VehDrawAttackID = -1;
    VehDrawDefendID = -1;
}
// The units of a battle message: $UNIT0 / stats1 (the defender's), $FACTION2, $UNIT3 / stats4
// (the attacker's), $BASENAME5 (the base nearest the battle among `faction_id`'s). The defender's
// stats are those of `def_stats_id` (a unit id, except in COMBATSURVIVE: the vehicle id).
void say_combat_units(const Battle& b, int32_t def_stats_id, int32_t faction_id) {
    parse_says(0, Units[game().vehs[b.veh_id_def].unit_id].name, -1, -1);
    StrBuffer[0] = 0;
    say_stats_short(StrBuffer, def_stats_id);
    parse_says(1, StrBuffer, -1, -1);
    parse_says(2, game().mfactions[b.atk_f].adj_name_faction, -1, -1);
    parse_says(3, Units[game().vehs[b.veh_id].unit_id].name, -1, -1);
    StrBuffer[0] = 0;
    say_stats_short(StrBuffer, game().vehs[b.veh_id].unit_id);
    parse_says(4, StrBuffer, -1, -1);
    int32_t base = base_find_filtered(b.x, b.y, -1, -1, -1, faction_id);
    if (base >= 0) parse_says(5, game().bases[base].name, -1, -1);
}
// A battle on a third faction's land: it loses patience with the attacker.
void third_faction_friction(const Battle& b) {
    MAP* sq = map_tile(b.x, b.y);
    int32_t owner = sq->owner;
    // BUG(original): owner -1 (unowned land) indexes Factions[-1] / diplo_gifts[-1]. Fixed (compat
    // out_of_range_reads): unowned land has no faction to lose patience.
    if (owner < 0 && !g_compat.out_of_range_reads) return;
    if ((sq->climate & 0xE0) > 0x5F && (uint32_t)owner != b.atk_f && (uint32_t)owner != b.def_f &&
        (diplo_status_raw(owner, b.def_f) & 0x10) && (diplo_status_raw(owner, b.atk_f) & 7)) {
        int32_t* gifts = reinterpret_cast<int32_t*>(
            reinterpret_cast<uint8_t*>(&game().factions[b.atk_f].diplo_gifts[0]) + owner * 4);
        if (*gifts == 0) *gifts = 1;
        cause_friction(owner, b.atk_f, -1);
    }
}
// Promotion of the winner: certain unless the loser was veteran or better and the winner's
// strength loses a roll against the sum of both (a defending base needs an elite attacker).
void promote_winner(Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def;
    if (b.atk_left == 0) {
        b.offense = (int32_t)((uint32_t)Units[game().vehs[veh_id].unit_id].reactor_id * b.atk_hp * b.offense) /
                    unit_max_hp(game().vehs[veh_id].unit_id);
        b.defense =
            (int32_t)((uint32_t)Units[game().vehs[veh_id_def].unit_id].reactor_id * veh_hp(veh_id_def) * b.defense) /
            b.def_hp;
        if (Units[game().vehs[veh_id].unit_id].plan != PLAN_ARTIFACT) {
            bool promo = true;
            if (morale_veh(veh_id_def, 1, 0) > 1) {
                int32_t sum = b.offense + b.defense;
                int32_t roll = sum - 1 > 0 ? game_rand() % sum : 0;
                if (b.offense < roll && (b.base_id < 0 || morale_veh(veh_id_def, 1, 0) > 3)) promo = false;
            }
            if (promo) promote(veh_id_def);
        }
        if (b.def_f == 0) game().vehs[veh_id_def].home_base_id = game().vehs[veh_id].home_base_id;
    } else {
        b.offense =
            (int32_t)((uint32_t)Units[game().vehs[veh_id].unit_id].reactor_id * veh_hp(veh_id) * b.offense) / b.atk_hp;
        b.defense = (int32_t)((uint32_t)Units[game().vehs[veh_id_def].unit_id].reactor_id * b.def_hp * b.defense) /
                    unit_max_hp(game().vehs[veh_id_def].unit_id);
        if (Units[game().vehs[veh_id_def].unit_id].plan != PLAN_ARTIFACT) {
            bool promo = true;
            if (morale_veh(veh_id, 1, 0) > 1) {
                int32_t sum = b.offense + b.defense;
                int32_t roll = sum - 1 > 0 ? game_rand() % sum : 0;
                if (b.defense < roll) promo = false;
            }
            if (promo) promote(veh_id);
        }
        if (b.atk_f == 0) game().vehs[veh_id].home_base_id = game().vehs[veh_id_def].home_base_id;
    }
}
// The attacker is destroyed (with its stack): messages, counts, monument.
void attacker_destroyed(Battle& b) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    FactionCombatWin[def_f]++;
    FactionCombatLoss[atk_f]++;
    if (!MultiplayerActive || def_f != (uint32_t)CurrentPlayerFaction) {
        if (PbemActive && b.human(def_f)) {
            say_combat_units(b, game().vehs[b.veh_id_def].unit_id, def_f);
            POP3("COMBATWIN", 0, def_f, b.x, b.y);
        }
    } else {
        say_combat_units(b, game().vehs[b.veh_id_def].unit_id, CurrentPlayerFaction);
        bat_msg("COMBATWIN", nullptr);
    }
    stack_veh(b.veh_id, 1);
    battle_kill_stack(b.veh_id, &b.kills, &b.captures, &b.energy, -1, def_f);
    game().factions[def_f].diplo_combat_total[atk_f]++;
    game().factions[def_f].diplo_combat_count[atk_f]++;
    if (atk_f != 0 && def_f != 0) mon_enemy_destroyed(def_f, atk_f);
}
// Whether the defender's whole stack takes the defeat (collateral damage): in the open (not in a
// base or bunker, no air units, a sea battle or no transport) or an artifact; for the AI and
// humans alike also a loaded transport in the open, or a stack of at most one combat unit.
bool defeat_hits_stack(const Battle& b) {
    MAP* sq = map_tile(b.x, b.y);
    int32_t du = game().vehs[b.veh_id_def].unit_id;
    int32_t au = game().vehs[b.veh_id].unit_id;
    bool whole_stack = (b.base_id < 0 && !(sq->items & 0x800) && chassis_triad(du) != TRIAD_AIR &&
                        chassis_triad(au) != TRIAD_AIR && Units[au].plan != PLAN_PROBE && Units[du].plan != 6 &&
                        (!stack_check(b.veh_id_def, 2, 8, -1, -1) || (sq->climate & 0xE0) < 0x60)) ||
                       Units[du].plan == PLAN_ARTIFACT;
    if (!whole_stack && b.atk_f != 0) {
        if (veh_cargo(b.veh_id_def) && b.base_id < 0 && Units[game().vehs[b.veh_id].unit_id].plan != PLAN_PROBE) {
            whole_stack = true;
        } else if (Units[game().vehs[b.veh_id].unit_id].plan != PLAN_PROBE) {
            int32_t c12 = stack_check(b.veh_id_def, 2, 0xC, -1, -1);
            int32_t c1 = stack_check(b.veh_id_def, 1, -1, -1, -1);
            int32_t c8 = stack_check(b.veh_id_def, 2, 8, -1, -1);
            int32_t c11 = stack_check(b.veh_id_def, 2, 0xB, -1, -1);
            int32_t c10 = stack_check(b.veh_id_def, 2, 10, -1, -1);
            if (c1 - c12 - c8 - c11 - c10 < 2) whole_stack = true;
        }
    }
    return whole_stack;
}
// Collateral damage to the defender's whole stack; land units at sea outside a transport die.
// Returns the stack left on the tile (-1 if none).
int32_t damage_stack(Battle& b) {
    int32_t veh_id = b.veh_id, veh_id_def = b.veh_id_def, x = b.x, y = b.y;
    stack_veh(veh_id_def, 0);
    if (veh_id_def >= 0) {
        for (int32_t v = head_of_stack(veh_id_def); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            int32_t vu = game().vehs[v].unit_id;
            MAP* sq3 = map_tile(x, y);
            bool hit;
            if (chassis_triad(vu) == TRIAD_LAND && (sq3->climate & 0xE0) < 0x60 &&
                (!(sq3->items & BIT_BASE_IN_TILE) || (sq3->val2 & 0xF) > 7) &&
                (game().vehs[v].order != ORDER_SENTRY_BOARD || game().vehs[v].waypoint_x[0] == veh_id_def ||
                 game().vehs[v].waypoint_x[0] < 0)) {
                int32_t hp = unit_max_hp(vu) - game().vehs[v].damage_taken;
                hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
                game().vehs[v].damage_taken += (uint8_t)hp;
                hit = true;
            } else {
                int8_t plan = Units[vu].plan;
                hit = v == veh_id_def ||
                      (plan != PLAN_ARTIFACT && plan != PLAN_COLONY && plan != PLAN_PROBE && plan != PLAN_SUPPLY);
            }
            if (hit) {
                int32_t au2 = game().vehs[veh_id].unit_id;
                uint8_t power =
                    weapon_value(au2) < 0 ? Units[game().vehs[v].unit_id].reactor_id : Units[au2].reactor_id;
                game().vehs[v].damage_taken += (uint8_t)(power * 3);
                b.collateral++;
                game().vehs[v].state |= VSTATE_HAS_MOVED;
            }
        }
    }
    int32_t killer = veh_id;
    if (veh_id_def >= 0) {
        int32_t v = head_of_stack(veh_id_def);
        while (v >= 0) {
            int32_t next = game().vehs[v].next_veh_id_stack;
            int32_t hp = range(unit_max_hp(game().vehs[v].unit_id) - game().vehs[v].damage_taken, 0, 9999);
            if ((hp < 1 || game().vehs[v].faction_id == 0) &&
                battle_kill(v, &b.kills, &b.captures, &b.energy, killer, b.atk_f)) {
                if (v < killer) killer--;
                if (v < next) next--;
            }
            v = next;
        }
    }
    b.collateral -= b.captures + b.kills;
    if (b.collateral < 0) b.collateral = 0;
    int32_t remaining = stack_fix(veh_at(x, y));
    if (remaining >= 0) {
        // Only non-combat units left (artifacts, colony pods, probes, supply): they are lost too.
        int32_t c12 = stack_check(remaining, 2, 0xC, -1, -1);
        int32_t c1 = stack_check(remaining, 1, -1, -1, -1);
        int32_t c8 = stack_check(remaining, 2, 8, -1, -1);
        int32_t c11 = stack_check(remaining, 2, 0xB, -1, -1);
        int32_t c10 = stack_check(remaining, 2, 10, -1, -1);
        if (c1 - c12 - c8 - c11 == c10) {
            battle_kill_stack(remaining, &b.kills, &b.captures, &b.energy, veh_id, b.atk_f);
            b.collateral = 0;
        }
    }
    return remaining;
}
// The base loses its defender: an undefended base may lose a citizen (and die), nerve gas kills
// half its citizens; human bases see the invasion.
void base_lost_defender(Battle& b, int32_t remaining) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    int32_t x = b.x, y = b.y;
    BASE& base = game().bases[b.base_id];
    base.state_flags |= BSTATE_COMBAT_LOSS_LAST_TURN;
    // BUG(original): the weapon test reads the vehicle id of the defender or of the stack
    // left behind, which a kill may have renumbered (or -1: Vehs[-1]).
    // Fixed (compat read_after_kill): the defender's unit, saved before the battle.
    const int32_t lost_unit = g_compat.read_after_kill ? veh_unit_raw(remaining) : b.def_unit_saved;
    int32_t base_now = b.base_id;
    bool survives = true;
    if (!is_ocean(b.ax, b.ay) && (!is_objective(base_now) || base.pop_size > 1) &&
        !has_fac(FAC_PERIMETER_DEFENSE, base_now, 0) && !has_fac(FAC_TACHYON_FIELD, base_now, 0) &&
        !has_project(FAC_CITIZENS_DEFENSE_FORCE - SP_ID_First, def_f) && weapon_value(lost_unit) != 0 &&
        atk_f != 0 && veh_who(x, y) < 0 && (game().factions[def_f].diff_level != 0 || !b.human(def_f))) {
        // The attack kills a citizen of an undefended base.
        int8_t pop = --base.pop_size;
        if (pop > 0) {
            base.factions_pop_size_intel[atk_f] = pop;
        } else {
            b.nerve = 0;
            base_kill(base_now);
            b.base_id = -1;
            for (int32_t i = 0; i < 8; i++) synch_bit(x, y, i);
            draw_map(1);
            survives = false;
            base_now = -1;
            if (eliminate_player(def_f, atk_f)) b.report_type = 0;
        }
    }
    if (survives) {
        if (!b.human(def_f)) base_reset(base_now, 0);
        if (b.nerve) {
            b.nerve = (game().bases[base_now].pop_size + 1) / 2;
            game().bases[base_now].pop_size -= (int8_t)b.nerve;
            if (is_objective(base_now) && game().bases[base_now].pop_size < 1) game().bases[base_now].pop_size = 1;
            if (game().bases[base_now].pop_size < 1) {
                base_kill(base_now);
                b.base_id = -1;
                for (int32_t i = 0; i < 8; i++) synch_bit(x, y, i);
                draw_map(1);
                base_now = -1;
                if (eliminate_player(def_f, atk_f)) b.report_type = 0;
            } else {
                spot_base(base_now, atk_f);
            }
        }
    }
    if (veh_who(x, y) < 0 && b.human(def_f) && base_now >= 0) invasions(base_now);
}
// The defender is destroyed (with its stack when the defeat hits it): messages, counts, the
// base's losses, monument.
void defender_destroyed(Battle& b) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    game().factions[atk_f].diplo_combat_total[def_f]++;
    FactionCombatWin[atk_f]++;
    game().factions[atk_f].diplo_combat_count[def_f]++;
    FactionCombatLoss[def_f]++;
    if (!MultiplayerActive || def_f != (uint32_t)CurrentPlayerFaction) {
        if (PbemActive && b.human(def_f)) {
            say_combat_units(b, game().vehs[b.veh_id_def].unit_id, def_f);
            POP3("COMBATLOSS", 0, def_f, b.x, b.y);
        }
    } else {
        say_combat_units(b, game().vehs[b.veh_id_def].unit_id, CurrentPlayerFaction);
        NetMsg_pop(&NetMsg, "COMBATLOSS", 5000, 0, nullptr);
    }
    int32_t remaining = b.def_id;  // the original keeps the defender id (stale after a kill)
    if (defeat_hits_stack(b)) remaining = damage_stack(b);
    else battle_kill(b.veh_id_def, &b.kills, &b.captures, &b.energy, b.veh_id, atk_f);
    if (b.base_id >= 0) base_lost_defender(b, remaining);
    if (atk_f != 0 && def_f != 0) mon_enemy_destroyed(atk_f, def_f);
}
// Mind worm husks: energy for the winner, and nearby natives grow bolder.
void collect_husks(const Battle& b) {
    game().factions[b.atk_f].energy_credits += b.energy;
    Console_update_data(&MainConsole, 0);
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(NearOffsetX[i] + b.x);
        int32_t y2 = NearOffsetY[i] + b.y;
        if (!on_map(x2, y2)) continue;
        for (int32_t v = veh_at(x2, y2); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            if (game().vehs[v].faction_id != 0) continue;
            uint16_t flags = game().vehs[v].flags;
            uint32_t level2 = (flags >> 3) & 3;
            flags &= (uint16_t)~(level2 * 8);
            game().vehs[v].flags = flags;
            game().vehs[v].flags |= (uint16_t)(range(level2 + 1, 0, 3) << 3);
            draw_tile(x2, y2, 2);
        }
    }
}
// The viewer's messages after the battle: kills and collateral damage, husks, captured units
// (booty), nerve gas, the defender's withdrawal; the pause after battles.
void report_battle(const Battle& b) {
    uint32_t atk_f = b.atk_f, def_f = b.def_f;
    if (b.atk_left == 0) {
        draw_tile_fixup(b.ax, b.ay, 2, 1);
        draw_tile(b.x, b.y, 1);
    } else {
        draw_tile(b.ax, b.ay, 1);
        draw_tile_fixup(b.x, b.y, 2, 1);
    }
    do_all_draws();
    if (b.atk_left == 0 || b.energy == 0 || atk_f != (uint32_t)CurrentPlayerFaction) {
        if ((b.kills > 1 || b.collateral != 0) &&
            (atk_f == (uint32_t)CurrentPlayerFaction || def_f == (uint32_t)CurrentPlayerFaction)) {
            parse_num(0, b.kills);
            parse_num(1, b.collateral);
            clear();
            says((atk_f == (uint32_t)CurrentPlayerFaction) == (b.atk_left != 0) ? "NUMGOT" : "NUMLOST");
            if (b.kills < 2) {
                if (b.collateral) says("1");
            } else if (b.collateral) {
                says("2");
            }
            bat_msg(StrBuffer, nullptr);
        }
    } else {
        parse_num(0, b.energy);
        bat_msg("HUSKS", nullptr);
    }
    if (b.captures) {
        parse_num(0, b.captures);
        if (def_f == (uint32_t)CurrentPlayerFaction) {
            clear();
            says("THEIRBOOTY");
            if (!has_treaty(atk_f, def_f, 0x10)) {
                parse_says(0, bat_noun(atk_f), -1, -1);
                bat_say_num(3);
            } else {
                bat_say_num(range(b.captures, 1, 2));
            }
            bat_msg(StrBuffer, nullptr);
        } else if (atk_f == (uint32_t)CurrentPlayerFaction) {
            clear();
            says("OURBOOTY");
            bat_say_num(range(b.captures, 1, 2));
            bat_msg(StrBuffer, nullptr);
        }
    }
    if (b.nerve && crt_strlen(b.nerve_base)) {
        parse_says(0, b.nerve_base, -1, -1);
        bat_msg("NERVEGAS", nullptr);
    }
    if (b.disengaged) {
        parse_says(0, Units[game().vehs[b.def_id].unit_id].name, -1, -1);
        parse_says(1, game().mfactions[def_f].adj_name_faction, -1, -1);
        if (def_f == (uint32_t)CurrentPlayerFaction) bat_msg("WEDISENGAGED", nullptr);
        else if (atk_f == (uint32_t)CurrentPlayerFaction) bat_msg("THEYDISENGAGED", nullptr);
    }
    StatusWin_redraw(&InfoWin);
    if (b.husk_refresh) {
        BaseWin_focus(&BaseWin);
        GraphicWin_redraw(&MainInterfaceWin);
        do_all_draws();
    }
    if (GameMorePreferences & MPREF_ADV_PAUSE_AFTER_BATTLES) battle_pause();
}
// Battle reports of treaty partners for the viewer (see diplomatic_consequences).
void report_treaty_battle(const Battle& b) {
    if (b.report_type == 1) {
        parse_says(0, game().mfactions[b.atk_f].adj_name_faction, -1, -1);
        parse_says(1, bat_noun(b.def_f), -1, -1);
        bat_msg_2("VENDETTAREPORT", nullptr);
    } else if (b.report_type == 2) {
        parse_says(0, game().mfactions[b.atk_f].adj_name_faction, -1, -1);
        parse_says(1, bat_noun(b.def_f), -1, -1);
        bat_msg_2("PACTATTACKED", nullptr);
    } else if (b.report_type == 3) {
        parse_says(0, bat_noun(b.atk_f), -1, -1);
        parse_says(1, bat_noun(b.def_f), -1, -1);
        bat_msg_2("PACTATTACKING", nullptr);
    }
}
// The winning attacker: air units (except range 1 chassis) and ships end their move, a missile
// is spent. Returns battle_resolve's result: 1 if the attacker cannot act any more.
int32_t winner_ends_move(const Battle& b) {
    int32_t v = CurrentVehID;
    int32_t vu = game().vehs[v].unit_id;
    if (chassis_triad(vu) == TRIAD_AIR && Chassis[Units[vu].chassis_id].range != 1) {
        veh_skip(v);
        game().vehs[v].moves_spent += (uint8_t)Rules.move_rate_roads;
    } else if (chassis_triad(vu) == TRIAD_SEA) {
        veh_skip(v);
        game().vehs[v].moves_spent += (uint8_t)Rules.move_rate_roads;
    }
    int32_t missile = Chassis[Units[game().vehs[v].unit_id].chassis_id].missile;
    if (missile) kill(v);
    // BUG(original): after kill(v), Vehs[v] is another vehicle (or past the end of the table).
    if (missile && !g_compat.read_after_kill) return 0;  // fixed: the spent missile is gone
    if (!(FactionStatus[0] & (uint8_t)b.atk_bit) && b.base_id >= 0 && veh_who(b.x, b.y) < 0 &&
        chassis_triad(game().vehs[v].unit_id) == TRIAD_AIR &&
        Chassis[Units[game().vehs[v].unit_id].chassis_id].range != 0 &&
        game().vehs[v].movement_turns + 1 >= (int32_t)Chassis[Units[game().vehs[v].unit_id].chassis_id].range)
        return 1;
    return missile == 0;
}
}  // namespace

int32_t GAMEAPI battle_resolve(int32_t veh_id, int32_t dir, int32_t x, int32_t y, int32_t is_arty, int32_t is_real,
                               int32_t* def_out) {
    Battle b(veh_id, dir, x, y, is_real);
    prepare_battle(b, is_arty);
    int32_t veh_id_def = b.veh_id_def;
    if (veh_id_def < 0 && !g_compat.out_of_range_reads) {  // fixed: no defender, no battle
        if (def_out) *def_out = veh_id_def;
        return 0;
    }
    if (!is_real) {
        if (def_out) *def_out = veh_id_def;
        return battle_rules::attack_odds(b.atk_hp, unit_max_hp(b.atk_unit), b.atk_power, b.offense, b.def_hp,
                                         unit_max_hp(b.def_unit), b.def_power, b.defense, (b.arty & 1) != 0);
    }
    b.atk_bit = 1u << (b.atk_f & 31);
    if (treaty_stops_attack(b)) return 2;
    decide_nerve_gas(b);
    spend_attack_moves(b);
    if (!(VehAttackFlags & 2)) return 0;
    game().vehs[veh_id].state |= 0x40;
    if (Units[game().vehs[veh_id].unit_id].plan != PLAN_PROBE &&
        Units[game().vehs[veh_id_def].unit_id].plan != PLAN_ARTIFACT)
        diplomatic_consequences(b);
    if (b.nerve) {
        b.offense += b.offense / 2;
        bat_line(0, 50, label(0x554));
    }
    if (MultiplayerActive) log_say_values("Battle Resolution!", veh_id, veh_id_def, 0);
    b.atk_unit_saved = game().vehs[veh_id].unit_id;
    b.def_unit_saved = game().vehs[veh_id_def].unit_id;
    if ((b.atk_f == (uint32_t)CurrentPlayerFaction || b.def_f == (uint32_t)CurrentPlayerFaction) &&
        !MultiplayerActive && (weapon_value(b.atk_unit_saved) < 0 || armor_value(b.def_unit_saved) < 0) &&
        tut_check2(2))
        TutWin_tut_map(&TutWin, "PSICOMBAT", x, y, b.atk_f != (uint32_t)CurrentPlayerFaction ? veh_id_def : veh_id,
                       nullptr, 1, -1, -1);
    difficulty_modifiers(b);
    if (b.base_id >= 0 && veh_triad(veh_id) == TRIAD_AIR) game().bases[b.base_id].state_flags |= 0x100000;
    // Human attackers are warned about bad odds (and may see them).
    if ((VehAttackFlags & 1) && !b.intercepted && (FactionStatus[0] & (uint8_t)b.atk_bit) &&
        b.atk_f == (uint32_t)CurrentPlayerFaction && b.arty == 0 && !unit_missile(game().vehs[veh_id].unit_id) &&
        !(game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) && !confirm_attack(b))
        return 1;
    if (b.nerve) {
        atrocity(b.atk_f, b.def_f, b.base_id >= 0, 0);
        b.nerve_base[0] = 0;
        if (b.base_id >= 0) crt_strcpy(b.nerve_base, game().bases[b.base_id].name);
    }
    if (game().vehs[veh_id].order == ORDER_MOVE_TO) {
        game().vehs[veh_id].order = ORDER_NONE;
        game().vehs[veh_id].state &= 0xFCFFBDFF;
    }
    battle_visibility(b);
    if (b.arty == 1) {
        bombard(b);
        return 0;
    }
    if (b.arty) add_goal(b.def_f, 0x10, 2, x, y, -1);
    if (b.display) show_battle_start(b);
    b.retreat = withdraw_threshold(b);
    b.marine = marine_threshold(b);
    if (Units[game().vehs[veh_id].unit_id].plan == PLAN_PLANET_BUSTER &&
        Units[game().vehs[veh_id].unit_id].reactor_id) {
        planet_busting(veh_id, x, y);
        GameDrawState |= 4;
        return 0;
    }
    b.round_div = (unit_missile(game().vehs[veh_id].unit_id) || MultiplayerActive) ? 0 : 3;
    if (unit_missile(game().vehs[veh_id].unit_id) || MultiplayerActive) boom(x, y, 2 - (veh_hp(veh_id_def) != 0));
    // Artillery duel between a ship and a land unit: one exchange only (when displayed, see below).
    b.single = ((b.arty & 2) && ((veh_triad(veh_id) == TRIAD_SEA) != (veh_triad(veh_id_def) == TRIAD_SEA))) ? 1 : 0;
    fight_rounds(b);
    // ---- outcome ----
    b.atk_left = veh_hp(veh_id);
    if (b.display || b.visible) show_outcome(b);
    if (b.single && veh_hp(veh_id) > 0 && veh_hp(veh_id_def) > 0) {
        // Both survive the duel.
        if (MultiplayerActive && b.def_f == (uint32_t)CurrentPlayerFaction) {
            say_combat_units(b, veh_id_def, CurrentPlayerFaction);
            bat_msg("COMBATSURVIVE", nullptr);
        }
        return 0;
    }
    third_faction_friction(b);
    promote_winner(b);
    if (!b.captured && !b.disengaged) {
        if (b.atk_left == 0) attacker_destroyed(b);
        else defender_destroyed(b);
        if (b.captures && !b.human(b.def_f) && !has_treaty(b.atk_f, b.def_f, 0x10)) {
            cause_friction(b.def_f, b.atk_f, 5);
            treaty_on(b.atk_f, b.def_f, 0x2000);
        }
    }
    if (b.atk_left != 0 && b.energy != 0) collect_husks(b);
    if (b.display) report_battle(b);
    report_treaty_battle(b);
    Units[b.atk_unit_saved].combat_factions |= (uint8_t)(1 << (b.def_f & 31));
    Units[b.def_unit_saved].combat_factions |= (uint8_t)(1 << (b.atk_f & 31));
    if (b.show_interlude) interlude(0, 0, 1, 0);
    if (b.atk_left == 0) return 0;
    return winner_ends_move(b);
}

#ifdef SMACX_HYBRID
// morale_alien runs inside the original morale_veh/speed (whose dead stack an uninitialised read in
// the AI depends on): keep the original's stack image (see hybrid_shadow.h).
HYBRID_SHADOW_THUNK(int32_t, morale_alien, (int32_t veh_id, int32_t faction_id_vs_native), 2, 0x501356,
                    "push %ebp\n mov %esp, %ebp\n sub $8, %esp")
#endif
