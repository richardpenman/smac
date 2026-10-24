#include "engine/battle.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/action.h"
#include "engine/base_misc.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
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

#ifdef SMACX_HYBRID
extern "C" int32_t GAMEAPI morale_alien_impl_c(int32_t veh_id, int32_t faction_id_vs_native);
#endif

// ---- helpers (not original functions) -----------------------------------------------------------

namespace {
// Raw view of the vehicle table: the original reads Vehs[-1] where a vehicle id may be -1.
const uint8_t* veh_raw(int32_t veh_id) { return reinterpret_cast<const uint8_t*>(&Vehs[0]) + veh_id * (int32_t)sizeof(VEH); }
int32_t veh_unit_raw(int32_t veh_id) { return *reinterpret_cast<const int16_t*>(veh_raw(veh_id) + offsetof(VEH, unit_id)); }
uint32_t veh_faction_raw(int32_t veh_id) { return veh_raw(veh_id)[offsetof(VEH, faction_id)]; }
uint32_t base_faction_raw(int32_t base_id) {
    return reinterpret_cast<const uint8_t*>(&Bases[0])[base_id * (int32_t)sizeof(BASE) + offsetof(BASE, faction_id)];
}

// Hit points of a unit design: 1 for artifacts, else reactor (1..100) x 10.
int32_t unit_max_hp(int32_t unit_id) {
    if (Units[unit_id].plan == PLAN_ARTIFACT) return 1;
    int32_t reactor = Units[unit_id].reactor_id;
    reactor = reactor < 1 ? 1 : reactor > 100 ? 100 : reactor;
    return reactor * 10;
}
// Remaining hit points of a vehicle (0..9999).
int32_t veh_hp(int32_t veh_id) {
    int32_t hp = unit_max_hp(Vehs[veh_id].unit_id) - Vehs[veh_id].damage_taken;
    return hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
}
int32_t chassis_triad(int32_t unit_id) { return Chassis[Units[unit_id].chassis_id].triad; }
bool unit_missile(int32_t unit_id) { return Chassis[Units[unit_id].chassis_id].missile != 0; }
int32_t weapon_value(int32_t unit_id) { return Weapon[Units[unit_id].weapon_id].offense_value; }
int32_t armor_value(int32_t unit_id) { return Armor[Units[unit_id].armor_id].defense_value; }
// Whether faction_id owns secret project sp (SecretProjects index).
bool owns_project(int32_t sp, uint32_t faction_id) {
    return SecretProjects[sp] >= 0 && (uint8_t)Bases[SecretProjects[sp]].faction_id == faction_id;
}
// Facility test through bitmask(), as the original does.
bool bat_base_fac(int32_t base_id, int32_t facility_id) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
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
    if (!(sq->items & 2)) return -1;
    uint32_t faction = sq->val2 & 0xF;
    return faction > 7 ? -1 : (int32_t)faction;
}

int32_t GAMEAPI has_fac_built(int32_t facility_id) {
    int32_t base_id = CurrentBaseID;
    if (facility_id > 0x40) return 0;
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}

int32_t GAMEAPI base_project(int32_t project_id) { return SecretProjects[project_id]; }

uint32_t GAMEAPI has_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t treaty) {
    return Factions[faction_id].diplo_status[faction_id_with] & treaty;
}

int32_t GAMEAPI drop_range(int32_t faction_id) {
    if (!has_tech(Rules.tech_preq_orb_insert_wo_space, faction_id)) {
        int32_t base_id = SecretProjects[27]  /* The Space Elevator */;
        if (base_id < 0 || (uint8_t)Bases[base_id].faction_id != (uint32_t)faction_id)
            return Rules.max_airdrop_rng_wo_orbital_insert;
    }
    return MapAreaX > MapAreaY ? MapAreaX : MapAreaY;
}

void GAMEAPI drop_cover_check(int32_t faction_id, int32_t x, int32_t y) {
    int32_t range;
    if (!has_tech(Rules.tech_preq_orb_insert_wo_space, faction_id) &&
        (SecretProjects[27] < 0 || (uint8_t)Bases[SecretProjects[27]].faction_id != (uint32_t)faction_id))
        range = Rules.max_airdrop_rng_wo_orbital_insert;
    else
        range = MapAreaX > MapAreaY ? MapAreaX : MapAreaY;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& veh = Vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id || !has_abil(veh.unit_id, ABL_DROP_POD) || (veh.state & 0x20) ||
            veh.order != 0)
            continue;
        int32_t dx = std::abs(x - veh.x);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
        int32_t dy = std::abs(y - veh.y);
        if (((dy + dx) >> 1) > range) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return;
        if (sq->items & 0x40000) return;  // airbase
    }
}

// ---- planet busters ---------------------------------------------------------------------------

int32_t GAMEAPI planet_buster2(int32_t unit_id) {
    if (Units[unit_id].plan == PLAN_PLANET_BUSTER) return Units[unit_id].reactor_id;
    return 0;
}

int32_t GAMEAPI planet_buster(int32_t veh_id) {
    int32_t unit_id = Vehs[veh_id].unit_id;
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
        if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id != (uint32_t)faction_id_atk &&
            bat_base_fac(base_id, FAC_FLECHETTE_DEFENSE_SYS))
            flechette = 1;
    }
    Faction& def = Factions[faction_id_def];
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
            if (base_id < 0 || (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id_atk) continue;
            if (bat_base_fac(base_id, FAC_FLECHETTE_DEFENSE_SYS) && game_rand() % 100 < 50) {
                parse_says(0, Bases[base_id].name, -1, -1);
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, MFactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "WESHOTIT2";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 1;
                    parse_says(1, MFactions[(uint8_t)Bases[base_id].faction_id].adj_name_faction, -1, -1);
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
                    parse_says(0, Bases[base_id].name, -1, -1);
                    parse_says(1, MFactions[faction_id_atk].adj_name_faction, -1, -1);
                    if (popp(ScriptFileName, "BUSTERSAC", 0, "space_sm.pcx", 0) != 0) failed = true;
                } else if (faction_id_atk == CurrentPlayerFaction) {
                    GenderDefault = MFactions[faction_id_def].noun_gender;
                    PluralDefault = MFactions[faction_id_def].is_noun_plural;
                    parse_says(0, MFactions[faction_id_def].noun_faction, -1, -1);
                    popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
                }
            } else if (faction_id_def == CurrentPlayerFaction) {
                popp(ScriptFileName, "WEMUSTSAC", 0, "space_sm.pcx", 0);
            } else if (faction_id_atk == CurrentPlayerFaction) {
                GenderDefault = MFactions[faction_id_def].noun_gender;
                PluralDefault = MFactions[faction_id_def].is_noun_plural;
                parse_says(0, MFactions[faction_id_def].noun_faction, -1, -1);
                popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
            }
        }
        if (failed) {
            if (flechette) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, MFactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    text = "WEFAILEDFLECHETTE";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 0;
                    parse_says(1, MFactions[faction_id_def].adj_name_faction, -1, -1);
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
    int32_t base_id = base_find_2(x, y, faction_id_def);
    if (base_id < 0) base_id = base_find(x, y);
    parse_says(0, Bases[base_id].name, -1, -1);
    parse_num(0, def.satellites_ODP);
    parse_num(1, remaining);
    if (faction_id_def == CurrentPlayerFaction) {
        parse_says(1, MFactions[faction_id_atk].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        text = "WESHOTIT";
    } else {
        if (faction_id_atk != CurrentPlayerFaction) return 1;
        parse_says(1, MFactions[faction_id_def].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        text = "THEYSHOTIT";
    }
    popp(ScriptFileName, text, 0, pcx, 0);
    return 1;
}

void GAMEAPI planet_busting(int32_t veh_id, int32_t x, int32_t y) {
    int32_t shot_down = 0;
    uint32_t faction_id = Vehs[veh_id].faction_id;
    Console_focus(&MainConsole, x, y, faction_id);
    int32_t unit_id = Vehs[veh_id].unit_id;
    int32_t power = Units[unit_id].plan == PLAN_PLANET_BUSTER ? Units[unit_id].reactor_id : 0;
    int32_t tiles = TableRange[power];
    int32_t base_id = base_find(x, y);
    if (faction_id != (uint32_t)CurrentPlayerFaction && base_id >= 0) {
        MFaction& m = MFactions[faction_id];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        parse_says(3, Bases[base_id].name, -1, -1);
        int32_t dx = std::abs(Bases[base_id].x - x);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
        int32_t dy = std::abs(Bases[base_id].y - y);
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
            uint32_t faction2 = Vehs[veh2].faction_id;
            if (faction_id == faction2 || (Factions[faction_id].diplo_status[faction2] & DIPLO_PACT) || tried[faction2])
                continue;
            shot_down = shoot(faction_id, faction2, x, y);
            if (shot_down) break;
            tried[faction2] = 1;
        } else {
            uint32_t faction2 = (uint8_t)Bases[base2].faction_id;
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
    dword_9B22E0 = -1;
    FX_play(&Sounds, 0x3B);
    PlanetBusterBoomSize = power;
    boom(x, y, 0x20);
    for (int32_t i = 0; i < tiles; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!map_in_bounds(x2, y2)) continue;
        int32_t base2 = base_at(x2, y2);
        if (base2 >= 0) {
            if (victim < 0 && (uint8_t)Bases[base2].faction_id != faction_id) victim = (uint8_t)Bases[base2].faction_id;
            base_kill(base2);
        }
        int32_t veh2 = stack_fix(veh_at(x2, y2));
        if (veh2 >= 0) {
            if (victim_veh < 0 && Vehs[veh2].faction_id != faction_id) victim_veh = Vehs[veh2].faction_id;
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
        uint32_t atk_faction = Vehs[veh_id_atk].faction_id;
        if (atk_faction == 0 || owns_project(16, atk_faction) /* Pholus Mutagen */ ||
            (weapon_value(Vehs[veh_id_atk].unit_id) < 0 && Vehs[veh_id_atk].unit_id < 64))
            return 2;
    }
    VehBattleDisplayTerrain = TextLabels[91];  // rocky
    int32_t bonus = (sq->val3 & 0xC0) > 0x40;
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F && !bonus) {
        int32_t def_unit = Vehs[veh_id_def].unit_id;
        if (chassis_triad(def_unit) != TRIAD_AIR &&
            (veh_id_atk < 0 ||
             ((weapon_value(Vehs[veh_id_atk].unit_id) >= 0 || Vehs[veh_id_atk].unit_id >= 64) &&
              (SecretProjects[16] < 0 ||
               (uint8_t)Bases[SecretProjects[16]].faction_id != Vehs[veh_id_atk].faction_id)))) {
            bonus = 1;
            if ((SecretProjects[16] >= 0 && (uint8_t)Bases[SecretProjects[16]].faction_id == (uint32_t)faction_id) ||
                (def_unit < 64 && weapon_value(def_unit) < 0))
                bonus = 2;
            VehBattleDisplayTerrain = TextLabels[338];  // fungus
        }
    }
    if ((sq->items & 0x200000) && bonus == 0 &&  // forest
        (veh_id_atk < 0 || chassis_triad(Vehs[veh_id_atk].unit_id) == TRIAD_LAND)) {
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
        morale += Factions[faction_id_vs_native].major_atrocities != 0;
        morale += TectonicDetonationCount[faction_id_vs_native] != 0;
    }
    if (veh_id >= 0) {
        const VEH& veh = Vehs[veh_id];
        if (veh.state & VSTATE_MONOLITH_UPGRADED) morale++;
        if (veh.unit_id == BSC_LOCUSTS_OF_CHIRON) morale++;
        morale += (veh.flags & 0xFF) >> 3 & 3;
    }
    // BUG(original): the Fungal Tower test also runs for veh_id -1, reading Vehs[-1] (the upper half
    // of a Reactor name pointer, never 19 in practice).
    if (veh_unit_raw(veh_id) == BSC_FUNGAL_TOWER) {
        // A tower's morale: adjacent fungus tiles (not deep ocean) minus 2.
        int32_t fungus = 0;
        for (int32_t i = 1; i < 9; i++) {
            int32_t x = map_wrap_x(TableOffsetX[i] + Vehs[veh_id].x);
            int32_t y = TableOffsetY[i] + Vehs[veh_id].y;
            if (!map_in_bounds(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F) fungus++;
        }
        morale = fungus - 2;
    }
    return morale < 0 ? 0 : morale > 6 ? 6 : morale;
}

int32_t GAMEAPI psi_factor(int32_t value, int32_t faction_id, int32_t is_attack, int32_t is_fungal_twr) {
    int32_t rule_psi = MFactions[faction_id].rule_psi;
    if (rule_psi) value = (rule_psi + 100) * value / 100;
    if (!is_attack) {
        if (owns_project(9, faction_id)) value += value / 2;  // Neural Amplifier
    } else {
        if (SecretProjects[19] < 0) return value;  // Dream Twister
        is_fungal_twr = (uint8_t)Bases[SecretProjects[19]].faction_id == (uint32_t)faction_id;
    }
    if (is_fungal_twr) value += value / 2;
    return value;
}

int32_t GAMEAPI get_basic_offense(int32_t veh_id, int32_t veh_id_def, int32_t psi_combat, int32_t is_bombard,
                                  int32_t is_defender) {
    uint32_t faction_id = Vehs[veh_id].faction_id;
    int32_t base_morale;
    if (faction_id == 0)
        base_morale = morale_alien(veh_id, veh_id_def < 0 ? -1 : (int32_t)Vehs[veh_id_def].faction_id);
    else
        base_morale = morale_veh(veh_id, 1, 0);
    int32_t morale = base_morale;
    int32_t base_id = base_at(Vehs[veh_id].x, Vehs[veh_id].y);
    if (base_id >= 0) {
        if (!bat_base_fac(base_id, FAC_CHILDREN_CRECHE)) {
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(Vehs[veh_id].unit_id))
                morale = base_morale + 1 - clamp_morale_bonus(Factions[faction_id].SE_morale, 4);
        } else {
            morale = base_morale + 1 - clamp_morale_bonus(Factions[faction_id].SE_morale, 4);
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(Vehs[veh_id].unit_id)) morale++;
        }
        if (is_defender) {
            int32_t pending = Factions[faction_id].SE_morale_pending;
            if (pending > 1 && pending < 4) morale++;
            if (veh_id_def >= 0) {
                if (Vehs[veh_id_def].faction_id == 0) {
                    morale++;
                } else {
                    int32_t unit_id = Vehs[veh_id].unit_id;
                    if (!unit_is_native(unit_id) && !has_abil(unit_id, ABL_DISSOCIATIVE_WAVE) &&
                        has_abil(Vehs[veh_id_def].unit_id, ABL_SOPORIFIC_GAS))
                        morale -= 2;
                }
            }
        }
    }
    if (is_defender) morale = morale < 1 ? 1 : morale > 6 ? 6 : morale;
    int32_t offense = offense_proto(Vehs[veh_id].unit_id, veh_id_def, is_bombard);
    if (psi_combat) {
        int32_t rule_psi = MFactions[faction_id].rule_psi;
        if (rule_psi) offense = (rule_psi + 100) * offense / 100;
        if (owns_project(19, faction_id)) offense += offense / 2;  // Dream Twister
    }
    VehBasicBattleMorale[is_defender != 0] = morale;
    return offense * (morale + 6) * 4;
}

int32_t GAMEAPI get_basic_defense(int32_t veh_id, int32_t veh_id_atk, int32_t psi_combat, int32_t is_bombard) {
    uint32_t faction_id = Vehs[veh_id].faction_id;
    int32_t morale;
    if (faction_id == 0)
        morale = morale_alien(veh_id, veh_id_atk < 0 ? -1 : (int32_t)Vehs[veh_id_atk].faction_id);
    else
        morale = morale_veh(veh_id, 1, 0);
    int32_t base_id = base_at(Vehs[veh_id].x, Vehs[veh_id].y);
    bool skip_gas = false;
    if (base_id >= 0) {
        if (!bat_base_fac(base_id, FAC_CHILDREN_CRECHE)) {
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(Vehs[veh_id].unit_id))
                morale = morale + 1 - clamp_morale_bonus(Factions[faction_id].SE_morale, 4);
        } else {
            morale = morale + 1 - clamp_morale_bonus(Factions[faction_id].SE_morale, 0);
            if (bat_base_fac(base_id, FAC_BROOD_PIT) && unit_is_native(Vehs[veh_id].unit_id)) morale++;
        }
        int32_t pending = Factions[faction_id].SE_morale_pending;
        if (pending > 1 && pending < 4) morale++;
        if (veh_id_atk < 0) skip_gas = true;
        else if (Vehs[veh_id_atk].faction_id == 0) morale++;
    }
    if (!skip_gas && veh_id_atk >= 0 && Vehs[veh_id_atk].faction_id != 0) {
        int32_t unit_id = Vehs[veh_id].unit_id;
        if (!unit_is_native(unit_id) && !has_abil(unit_id, ABL_DISSOCIATIVE_WAVE) &&
            has_abil(Vehs[veh_id_atk].unit_id, ABL_SOPORIFIC_GAS))
            morale -= 2;
    }
    morale = morale < 1 ? 1 : morale > 6 ? 6 : morale;
    VehBasicBattleMorale[1] = morale;
    int32_t unit_id = Vehs[veh_id].unit_id;
    if (Units[unit_id].plan == PLAN_ARTIFACT) return 1;
    // BUG(original): without an attacker, Vehs[-1] is read for its unit's plan.
    if (Units[unit_id].plan == PLAN_PROBE && armor_value(unit_id) == 1 &&
        Units[veh_unit_raw(veh_id_atk)].plan != PLAN_PROBE)
        return 1;
    int32_t defense = armor_proto(unit_id, veh_id_atk, is_bombard);
    if (psi_combat) {
        int32_t rule_psi = MFactions[faction_id].rule_psi;
        if (rule_psi) defense = (rule_psi + 100) * defense / 100;
        if (owns_project(9, faction_id)) defense += defense / 2;  // Neural Amplifier
        if (Vehs[veh_id].unit_id == BSC_FUNGAL_TOWER) defense += defense / 2;
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

int32_t GAMEAPI battle_compute(int32_t veh_id_atk, int32_t veh_id_def, int32_t* offense_out, int32_t* defense_out,
                               int32_t combat_type) {
    int32_t offense = 8;
    int32_t defense = 8;
    VehBattleModCount[0] = 0;
    VehBattleModCount[1] = 0;
    VehBattleState[0] = 0;
    VehBattleState[1] = 0;
    int32_t is_arty = combat_type & 1;
    // BUG(original): without an attacker (veh_id_atk -1), Vehs[-1] is read in several tests below.
    int32_t atk_unit_raw = veh_unit_raw(veh_id_atk);
    int32_t is_bombard = (is_arty || unit_missile(atk_unit_raw)) ? 1 : 0;
    int32_t psi = 0;
    if (!is_bombard || atk_unit_raw == BSC_SPORE_LAUNCHER || veh_unit_raw(veh_id_def) == BSC_SPORE_LAUNCHER) {
        if (veh_id_atk >= 0 && weapon_value(Vehs[veh_id_atk].unit_id) < 0) psi = 1;
        if (veh_id_def >= 0 && armor_value(Vehs[veh_id_def].unit_id) < 0) psi |= 2;
        if (psi) {
            uint32_t faction_id = veh_faction_raw(veh_id_atk);
            if (MFactions[faction_id].rule_psi) {
                char* text = label(0x558);
                bat_line(0, MFactions[faction_id].rule_psi, text);
            }
            if (owns_project(19, faction_id)) bat_line(0, 50, label(0x55C));  // Dream Twister
        }
        uint32_t faction_def = veh_faction_raw(veh_id_def);
        if (psi) {
            if (MFactions[faction_def].rule_psi) {
                char* text = label(0x558);
                bat_line(1, MFactions[faction_def].rule_psi, text);
            }
            if (owns_project(9, faction_def)) bat_line(1, 50, label(0x560));  // Neural Amplifier
            if (veh_id_def >= 0 && Vehs[veh_id_def].unit_id == BSC_FUNGAL_TOWER)
                bat_line(1, 50, Units[BSC_FUNGAL_TOWER].name);
        }
    }
    if (veh_id_atk >= 0) {
        offense = get_basic_offense(veh_id_atk, veh_id_def, psi, is_bombard, 0);
        int32_t atk_unit = Vehs[veh_id_atk].unit_id;
        uint32_t faction_atk = Vehs[veh_id_atk].faction_id;
        if (Units[atk_unit].plan != PLAN_PROBE) {
            if (veh_id_def >= 0 && combat_type == 0) {
                MAP* sq = map_tile(Vehs[veh_id_def].x, Vehs[veh_id_def].y);
                if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F &&
                    ((atk_unit < 64 && weapon_value(atk_unit) < 0) || owns_project(16, faction_atk)) &&
                    weapon_value(Vehs[veh_id_def].unit_id) >= 0) {
                    offense += offense / 2;  // fungus
                    bat_line(0, 50, label(0x548));
                }
                int32_t psi_def = psi & 2;
                if (psi_def && (Units[Vehs[veh_id_atk].unit_id].weapon_id == WPN_RESONANCE_LASER ||
                                Units[Vehs[veh_id_atk].unit_id].weapon_id == WPN_RESONANCE_BOLT)) {
                    offense = offense * 125 / 100;
                    bat_line(0, 25, label(0x1158));
                }
                if (!has_abil(Vehs[veh_id_def].unit_id, ABL_DISSOCIATIVE_WAVE) && psi_def &&
                    has_abil(Vehs[veh_id_atk].unit_id, ABL_EMPATH) && Rules.combat_bonus_empath_song_vs_psi) {
                    offense = (Rules.combat_bonus_empath_song_vs_psi + 100) * offense / 100;
                    char* text = Strings_get(&StringTable, Ability[abil_index(ABL_EMPATH)].name);
                    bat_line(0, Rules.combat_bonus_empath_song_vs_psi, text);
                }
            }
            if ((Vehs[veh_id_atk].state & VSTATE_MADE_AIRDROP) && has_abil(Vehs[veh_id_atk].unit_id, ABL_DROP_POD) &&
                Rules.combat_penalty_atk_airdrop) {
                offense = (100 - Rules.combat_penalty_atk_airdrop) * offense / 100;
                int32_t faction = (int8_t)Vehs[veh_id_atk].faction_id;
                int32_t range;
                if (has_tech(Rules.tech_preq_orb_insert_wo_space, faction) ||
                    (range = Rules.max_airdrop_rng_wo_orbital_insert,
                     SecretProjects[27] >= 0 && Bases[SecretProjects[27]].faction_id == faction))
                    range = MapAreaX > MapAreaY ? MapAreaX : MapAreaY;
                char* text = label(Rules.max_airdrop_rng_wo_orbital_insert < range ? 0x6D8 : 0x6D4);
                bat_line(0, -Rules.combat_penalty_atk_airdrop, text);
            }
            if ((MFactions[Vehs[veh_id_atk].faction_id].rule_flags & 0x400) && Rules.combat_bonus_fanatic &&
                combat_type == 0 && psi == 0) {
                offense = (Rules.combat_bonus_fanatic + 100) * offense / 100;
                char* text = label(0x840);
                bat_line(0, Rules.combat_bonus_fanatic, text);
            }
            uint32_t faction = Vehs[veh_id_atk].faction_id;
            for (int32_t i = 0; i < MFactions[faction].faction_bonus_count; i++) {
                if (MFactions[faction].faction_bonus_id[i] == 0x12) {  // offense bonus
                    int32_t value = MFactions[faction].faction_bonus_val1[i];
                    offense = value * offense / 100;
                    bat_line(0, value - 100, label(0x1150));
                }
            }
            if (psi && Vehs[veh_id_atk].faction_id != 0) {
                uint32_t f = Vehs[veh_id_atk].faction_id;
                if (Factions[f].SE_planet != 0 && Rules.combat_psi_bonus_per_planet) {
                    char* text = label(0x9C4);
                    bat_line(0, Factions[f].SE_planet * Rules.combat_psi_bonus_per_planet, text);
                    offense = (Factions[f].SE_planet * Rules.combat_psi_bonus_per_planet + 100) * offense / 100;
                }
            }
        }
    }
    if (combat_type & 2) {
        // Artillery duel: the defender fires back with its offense.
        if (veh_id_def < 0) goto done;
        defense = get_basic_offense(veh_id_def, veh_id_atk, psi, is_bombard, 1);
        if (combat_type & 0x18) goto done;
        int32_t triad_def = chassis_triad(Vehs[veh_id_def].unit_id);
        int32_t triad_atk = chassis_triad(Vehs[veh_id_atk].unit_id);
        if (triad_def == TRIAD_SEA && triad_atk == TRIAD_LAND) {
            if (!Rules.combat_land_vs_sea_artillery) goto done;
            offense = (Rules.combat_land_vs_sea_artillery + 100) * offense / 100;
            char* text = label(0x6CC);
            bat_line(0, Rules.combat_land_vs_sea_artillery, text);
        } else if (triad_atk == TRIAD_SEA && triad_def == TRIAD_LAND) {
            if (!Rules.combat_land_vs_sea_artillery) goto done;
            defense = (Rules.combat_land_vs_sea_artillery + 100) * defense / 100;
            char* text = label(0x6CC);
            bat_line(1, Rules.combat_land_vs_sea_artillery, text);
        } else {
            if (!Rules.combat_artillery_bonus_altitude) goto done;
            uint32_t alt_atk = map_tile(Vehs[veh_id_atk].x, Vehs[veh_id_atk].y)->climate >> 5;
            uint32_t alt_def = map_tile(Vehs[veh_id_def].x, Vehs[veh_id_def].y)->climate >> 5;
            if (alt_def < alt_atk) {
                int32_t value = (int32_t)(alt_atk - alt_def) * Rules.combat_artillery_bonus_altitude;
                offense = (value + 100) * offense / 100;
                char* text = label(0x900);
                bat_line(0, value, text);
            } else if (alt_atk < alt_def) {
                int32_t value = (int32_t)(alt_def - alt_atk) * Rules.combat_artillery_bonus_altitude;
                defense = (value + 100) * defense / 100;
                char* text = label(0x900);
                bat_line(1, value, text);
            }
        }
        goto done;
    }
    {
        if (veh_id_def < 0) goto done;
        defense = get_basic_defense(veh_id_def, veh_id_atk, psi, is_bombard);
        uint32_t faction_def = Vehs[veh_id_def].faction_id;
        for (int32_t i = 0; i < MFactions[faction_def].faction_bonus_count; i++) {
            if (MFactions[faction_def].faction_bonus_id[i] == 0x11) {  // defense bonus
                defense = MFactions[faction_def].faction_bonus_val1[i] * defense / 100;
                char* text = label(0x1154);
                bat_line(1, MFactions[faction_def].faction_bonus_val1[i] - 100, text);
            }
        }
        if (veh_id_atk >= 0 && Units[Vehs[veh_id_atk].unit_id].plan == PLAN_PROBE) {
            defense *= 4;
            goto done;
        }
        int32_t x = Vehs[veh_id_def].x;
        int32_t y = Vehs[veh_id_def].y;
        int32_t base_id = base_at(x, y);
        int32_t def_unit = Vehs[veh_id_def].unit_id;
        if (psi == 0) {
            int32_t terrain;
            if (chassis_triad(def_unit) == TRIAD_AIR)
                terrain = 2;
            else
                terrain = defense_value(Vehs[veh_id_def].faction_id, x, y, veh_id_def, veh_id_atk);
            if (veh_id_atk >= 0 && chassis_triad(Vehs[veh_id_atk].unit_id) == TRIAD_LAND) {
                int32_t atk_unit = Vehs[veh_id_atk].unit_id;
                MAP* sq_atk = map_tile(Vehs[veh_id_atk].x, Vehs[veh_id_atk].y);
                MAP* sq_def = map_tile(x, y);
                if (combat_type != 0 && Rules.combat_artillery_bonus_altitude &&
                    chassis_triad(def_unit) == TRIAD_LAND) {
                    uint32_t alt_atk = sq_atk->climate >> 5;
                    uint32_t alt_def = sq_def->climate >> 5;
                    if (alt_def < alt_atk) {
                        int32_t diff = (int32_t)(alt_atk - alt_def);
                        offense = (diff * Rules.combat_artillery_bonus_altitude + 100) * offense / 100;
                        char* text = label(0x900);
                        bat_line(0, diff * Rules.combat_artillery_bonus_altitude, text);
                    }
                }
                if (Rules.combat_mobile_open_ground && combat_type == 0 && base_id < 0 && terrain == 2 &&
                    (sq_def->val3 & 0xC0) < 0x80 && speed_proto(atk_unit) > Rules.move_rate_roads) {
                    int32_t speed_atk = speed_proto(atk_unit);
                    int32_t speed_def = speed_proto(def_unit);
                    if (speed_def <= speed_atk) {
                        offense = (Rules.combat_mobile_open_ground + 100) * offense / 100;
                        char* text = label(0x98C);
                        bat_line(0, Rules.combat_mobile_open_ground, text);
                    }
                }
                if (Rules.combat_mobile_def_in_rough && combat_type == 0 && (terrain > 2 || base_id >= 0) &&
                    speed_proto(atk_unit) > Rules.move_rate_roads) {
                    defense = (Rules.combat_mobile_def_in_rough + 100) * defense / 100;
                    char* text = label(base_id < 0 ? 0x890 : 0x990);
                    bat_line(1, Rules.combat_mobile_def_in_rough, text);
                }
                if (Rules.combat_bonus_atk_road && combat_type == 0) {
                    offense = (Rules.combat_bonus_atk_road + 100) * offense / 100;
                    char* text = label(0x978);
                    bat_line(0, Rules.combat_bonus_atk_road, text);
                }
                if (Rules.combat_penalty_atk_lower_elevation && combat_type == 0 &&
                    (sq_atk->climate & 0xE0) < (sq_def->climate & 0xE0) && !has_abil(atk_unit, ABL_ANTIGRAV_STRUTS)) {
                    char* text = label(0x6E4);
                    bat_line(1, Rules.combat_penalty_atk_lower_elevation, text);
                    defense = (Rules.combat_penalty_atk_lower_elevation + 100) * defense / 100;
                }
                if (Rules.combat_bonus_atk_higher_elevation && combat_type == 0 &&
                    (sq_def->climate & 0xE0) < (sq_atk->climate & 0xE0) && !has_abil(def_unit, ABL_ANTIGRAV_STRUTS)) {
                    char* text = label(0x528);
                    bat_line(0, Rules.combat_bonus_atk_higher_elevation, text);
                    offense = (Rules.combat_bonus_atk_higher_elevation + 100) * offense / 100;
                }
            }
            defense = terrain * defense;
            if (terrain > 2) {
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, label(0x52C));
                crt_strcat(StrBuffer, " ");
                crt_strcat(StrBuffer, "(");
                crt_strcat(StrBuffer, Strings_get(&StringTable, VehBattleDisplayTerrain));
                crt_strcat(StrBuffer, ")");
                bat_line(1, (terrain * 5 - 10) * 10, StrBuffer);
            }
            // Bunker, base defenses
            MAP* sq = map_tile(x, y);
            char* text_ptr = nullptr;
            int32_t mult = 2;
            if ((sq->items & 0x800) && (veh_id_atk < 0 || chassis_triad(Vehs[veh_id_atk].unit_id) != TRIAD_AIR)) {
                text_ptr = TextLabels[0x598 / 4];  // bunker
                mult = 3;
            }
            if (base_id >= 0) {
                text_ptr = TextLabels[0x530 / 4];
                mult = 2;
                int32_t level = veh_id_atk;  // BUG(original): -1 without an attacker (see below)
                if (veh_id_atk >= 0) {
                    int32_t triad = chassis_triad(Vehs[veh_id_atk].unit_id);
                    level = 2;
                    if (triad == TRIAD_LAND) {
                        if (bat_base_fac(base_id, FAC_PERIMETER_DEFENSE) || owns_project(5, faction_def)) {
                            text_ptr = TextLabels[0x588 / 4];
                            level = 4;
                        }
                    } else if (triad == TRIAD_SEA) {
                        if (bat_base_fac(base_id, FAC_NAVAL_YARD) ||
                            (SecretProjects[10] >= 0 &&
                             Bases[SecretProjects[10]].faction_id == Bases[base_id].faction_id)) {
                            text_ptr = Facility[FAC_NAVAL_YARD].name;
                            level = 4;
                        }
                    } else if (triad == TRIAD_AIR) {
                        if (bat_base_fac(base_id, FAC_AEROSPACE_COMPLEX) ||
                            (SecretProjects[35] >= 0 &&
                             Bases[SecretProjects[35]].faction_id == Bases[base_id].faction_id)) {
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
                    (veh_id_atk < 0 || !has_abil(Vehs[veh_id_atk].unit_id, ABL_BLINK_DISPLACER)) &&
                    Rules.combat_bonus_intrinsic_base_def) {
                    char* text = label(0x530);
                    bat_line(1, Rules.combat_bonus_intrinsic_base_def, text);
                    defense = (Rules.combat_bonus_intrinsic_base_def + 100) * defense / 100;
                }
            }
            if (veh_id_atk >= 0 && has_abil(Vehs[veh_id_atk].unit_id, ABL_BLINK_DISPLACER)) {
                if (mult > 2) mult = 2;
                text_ptr = TextLabels[0x6B0 / 4];
                Strings_get(&StringTable, text_ptr);
            }
            if (Rules.combat_infantry_vs_base && combat_type == 0 && base_id >= 0 &&
                veh_faction_raw(veh_id_atk) != 0) {
                int32_t unit = veh_unit_raw(veh_id_atk);
                if (!unit_is_native(unit) && chassis_triad(unit) == TRIAD_LAND && Chassis[Units[unit].chassis_id].speed == 1) {
                    offense = (Rules.combat_infantry_vs_base + 100) * offense / 100;
                    char* text = label(0x88C);
                    bat_line(0, Rules.combat_infantry_vs_base, text);
                }
            }
            if (is_arty && mult < 3 && base_id < 0 && (sq->val3 & 0xC0) < 0x80 && !(sq->items & 0x200000) &&
                (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40)) {
                mult = 3;
                text_ptr = TextLabels[0x834 / 4];  // open ground vs artillery
            }
            defense = mult * defense;
            if (mult > 2) {
                char* text = Strings_get(&StringTable, text_ptr);
                bat_line(1, (mult * 5 - 10) * 10, text);
            }
        } else {
            if (base_id >= 0 && Rules.combat_bonus_intrinsic_base_def) {
                char* text = label(0x530);
                bat_line(1, Rules.combat_bonus_intrinsic_base_def, text);
                defense = (Rules.combat_bonus_intrinsic_base_def + 100) * defense / 100;
            }
            defense = defense * 4;
        }
        // Flechette Defense Systems of nearby friendly bases against missiles.
        if (Vehs[veh_id_def].faction_id != 0 && veh_id_atk >= 0 && unit_missile(Vehs[veh_id_atk].unit_id)) {
            for (int32_t i = 0; i < 25; i++) {
                int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
                int32_t y2 = TableOffsetY[i] + y;
                if (!map_in_bounds(x2, y2)) continue;
                int32_t base2 = base_at(x2, y2);
                if (base2 < 0 || (uint8_t)Bases[base2].faction_id != Vehs[veh_id_def].faction_id) continue;
                if (bat_base_fac(base2, FAC_FLECHETTE_DEFENSE_SYS)) {
                    defense = defense * 3 / 2;
                    bat_line(1, 50, label(0x1164));
                }
            }
        }
        // Sensors (land tiles only).
        if ((map_tile(x, y)->climate & 0xE0) > 0x5F) {
            int32_t sensors = 0;
            if (Vehs[veh_id_def].faction_id != 0) {
                for (int32_t i = 0; i < 25; i++) {
                    int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
                    int32_t y2 = TableOffsetY[i] + y;
                    if (!map_in_bounds(x2, y2) || !is_sensor(x2, y2)) continue;
                    bool own;
                    if ((map_tile(x2, y2)->climate & 0xE0) < 0x60) {
                        // BUG(original): base_find may return -1 (no bases): Bases[-1] is read.
                        own = base_faction_raw(base_find(x2, y2)) == Vehs[veh_id_def].faction_id;
                    } else {
                        int32_t owner = whose_territory(Vehs[veh_id_def].faction_id, x2, y2, nullptr, 0);
                        own = owner < 0 || (uint32_t)owner == Vehs[veh_id_def].faction_id;
                    }
                    if (own) {
                        sensors |= 1;
                        if (is_sensor(x2, y2) > 1) sensors |= 2;
                    }
                }
            }
            if (sensors & 1) {
                defense = (Rules.combat_defend_sensor + 100) * defense / 100;
                char* text = label(0x994);
                bat_line(1, Rules.combat_defend_sensor, text);
            }
            if (sensors & 2) {
                defense = (Rules.combat_defend_sensor + 100) * defense / 100;
                char* text = label(0x118C);
                bat_line(1, Rules.combat_defend_sensor, text);
            }
        }
        int32_t atk_unit = veh_unit_raw(veh_id_atk);
        if (!has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && Rules.combat_bonus_trance_vs_psi && veh_id_atk >= 0 &&
            (psi & 1) && has_abil(Vehs[veh_id_def].unit_id, ABL_TRANCE)) {
            defense = (Rules.combat_bonus_trance_vs_psi + 100) * defense / 100;
            char* text = label(0x524);
            bat_line(1, Rules.combat_bonus_trance_vs_psi, text);
        }
        if (psi & 1) {
            if (veh_id_atk < 0) goto done;
            int32_t armor = Units[Vehs[veh_id_def].unit_id].armor_id;
            if (armor == ARM_RESONANCE_3_ARMOR || armor == ARM_RESONANCE_8_ARMOR) {
                defense = defense * 125 / 100;
                bat_line(1, 25, label(0x115C));
            }
        }
        if (veh_id_atk < 0) goto done;
        def_unit = Vehs[veh_id_def].unit_id;
        atk_unit = Vehs[veh_id_atk].unit_id;
        if (chassis_triad(atk_unit) == TRIAD_AIR && has_abil(atk_unit, ABL_AIR_SUPERIORITY) && psi == 0) {
            if (chassis_triad(def_unit) == TRIAD_AIR) {
                if (Rules.combat_bonus_air_supr_vs_air && !has_abil(def_unit, ABL_AIR_SUPERIORITY)) {
                    offense = (Rules.combat_bonus_air_supr_vs_air + 100) * offense / 100;
                    char* text = label(0x704);
                    bat_line(0, Rules.combat_bonus_air_supr_vs_air, text);
                }
            } else if (Rules.combat_penalty_air_supr_vs_ground) {
                offense = (100 - Rules.combat_penalty_air_supr_vs_ground) * offense / 100;
                char* text = label(0x700);
                bat_line(0, -Rules.combat_penalty_air_supr_vs_ground, text);
            }
        }
        if (chassis_triad(atk_unit) == TRIAD_AIR && chassis_triad(def_unit) == TRIAD_AIR &&
            has_abil(def_unit, ABL_AIR_SUPERIORITY) && !has_abil(atk_unit, ABL_AIR_SUPERIORITY) && psi == 0 &&
            !unit_missile(atk_unit) && !unit_missile(def_unit) && Rules.combat_bonus_air_supr_vs_air) {
            defense = (Rules.combat_bonus_air_supr_vs_air + 100) * defense / 100;
            char* text = label(0x704);
            bat_line(1, Rules.combat_bonus_air_supr_vs_air, text);
        }
        if (weapon_value(def_unit) == 0 && armor_value(def_unit) == 1 &&
            (Vehs[veh_id_atk].faction_id != 0 || base_id < 0) && Rules.combat_penalty_non_combat_vs_combat) {
            defense = (100 - Rules.combat_penalty_non_combat_vs_combat) * defense / 100;
            char* text = label(0x6DC);
            bat_line(1, -Rules.combat_penalty_non_combat_vs_combat, text);
        }
        if (chassis_triad(atk_unit) != TRIAD_SEA && chassis_triad(def_unit) == TRIAD_SEA && base_id >= 0 &&
            Rules.combat_bonus_vs_ship_port) {
            offense = (Rules.combat_bonus_vs_ship_port + 100) * offense / 100;
            char* text = label(0x53C);
            bat_line(0, Rules.combat_bonus_vs_ship_port, text);
        }
        int32_t armor = Units[def_unit].armor_id;
        if ((armor == ARM_PULSE_3_ARMOR || armor == ARM_PULSE_8_ARMOR) && chassis_triad(atk_unit) == TRIAD_LAND &&
            Chassis[Units[atk_unit].chassis_id].speed > 1) {
            defense = defense * 125 / 100;
            bat_line(1, 25, label(0x1160));
        }
        if (!has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && has_abil(def_unit, ABL_COMM_JAMMER) &&
            chassis_triad(atk_unit) == TRIAD_LAND && Chassis[Units[atk_unit].chassis_id].speed > 1 &&
            Rules.combat_comm_jammer_vs_mobile) {
            defense = (Rules.combat_comm_jammer_vs_mobile + 100) * defense / 100;
            char* text = label(0x540);
            bat_line(1, Rules.combat_comm_jammer_vs_mobile, text);
        }
        if (!has_abil(atk_unit, ABL_DISSOCIATIVE_WAVE) && has_abil(def_unit, ABL_AAA) &&
            chassis_triad(atk_unit) == TRIAD_AIR && Rules.combat_aaa_bonus_vs_air) {
            defense = (Rules.combat_aaa_bonus_vs_air + 100) * defense / 100;
            char* text = label(0x544);
            bat_line(1, Rules.combat_aaa_bonus_vs_air, text);
        }
    }
done:
    if (offense_out) *offense_out = offense;
    if (defense_out) *defense_out = defense;
    return 0;
}

int32_t GAMEAPI best_defender(int32_t veh_id_def, int32_t veh_id_atk, int32_t check_arty) {
    int32_t offense_atk = veh_id_atk < 0 ? 8 : get_basic_offense(veh_id_atk, veh_id_def, 0, 0, 0);
    int32_t x = Vehs[veh_id_def].x;
    int32_t y = Vehs[veh_id_def].y;
    uint8_t climate = map_tile(x, y)->climate;
    int32_t base_id = base_at(x, y);
    int32_t best_score = -999;
    int32_t best_id = veh_id_def;
    if (veh_id_def < 0) return best_id;
    int32_t veh_id = veh_id_def;
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    // BUG(original): without an attacker (veh_id_atk -1), Vehs[-1] is read for its unit.
    int32_t atk_unit = veh_unit_raw(veh_id_atk);
    for (; veh_id >= 0; veh_id = Vehs[veh_id].next_veh_id_stack) {
        int32_t unit_id = Vehs[veh_id].unit_id;
        if (chassis_triad(unit_id) == TRIAD_LAND && (climate & 0xE0) < 0x60 && base_id < 0) continue;
        if (Units[atk_unit].plan == PLAN_PROBE && Units[unit_id].plan != PLAN_PROBE) continue;
        int32_t combat_type = (can_arty(unit_id, 1) && can_arty(atk_unit, 1)) ? 2 : 0;
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
                             chassis_triad(unit_id) != TRIAD_AIR || base_id >= 0 || (sq->items & 0x40000) ||
                             stack_check(veh_id, 6, 0x80, -1, -1) || unit_missile(unit_id) || unit_missile(atk_unit);
            air_vs_interceptor = !sheltered;
        }
        if (air_vs_interceptor) {
            priority = true;
        } else if (!check_arty) {
            MAP* sq = map_tile(x, y);
            priority = (!stack_check(veh_id, 3, 2, -1, -1) || base_id >= 0 || (sq->items & 0x40000) ||
                        stack_check(veh_id, 6, 0x80, -1, -1)) &&
                       (Vehs[veh_id].state & VSTATE_DESIGNATE_DEFENDER);
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
    uint32_t faction_id = Vehs[veh_id].faction_id;
    int32_t unit_id = Vehs[veh_id].unit_id;
    Factions[faction_id].units_lost[unit_id]++;
    if (veh_id_killer >= 0 && veh_id_killer != veh_id && Vehs[veh_id_killer].faction_id != 0) {
        int32_t ky = Vehs[veh_id_killer].y;
        int32_t kx = Vehs[veh_id_killer].x;
        // An alien artifact is captured by a (non-missile) killer on land, carrying it or as an objective.
        if (ky >= 0 && ky < MapAreaY && kx >= 0 && kx < MapAreaX && Units[unit_id].plan == PLAN_ARTIFACT &&
            !unit_missile(Vehs[veh_id_killer].unit_id) &&
            ((map_tile(kx, ky)->climate & 0xE0) > 0x5F || stack_check(veh_id_killer, 8, -1, -1, -1) > 0 ||
             (Vehs[veh_id].flags & VFLAG_IS_OBJECTIVE))) {
            veh_lift(veh_id);
            int32_t unit2 = Vehs[veh_id].unit_id;
            uint32_t new_faction = Vehs[veh_id_killer].faction_id;
            Vehs[veh_id].damage_taken = 0;
            Vehs[veh_id].faction_id = (uint8_t)new_faction;
            Vehs[veh_id].order = 0;
            Factions[faction_id].units_active[unit2]--;
            Vehs[veh_id].state &= 0xFCFFBDFF;
            Factions[new_faction].units_active[unit2]++;
            veh_drop(veh_id, Vehs[veh_id_killer].x, Vehs[veh_id_killer].y);
            veh_skip(veh_id);
            if (capture_count) (*capture_count)++;
            return 0;
        }
    }
    if ((Vehs[veh_id].flags & VFLAG_IS_OBJECTIVE) && faction_id_killer >= 0) Factions[faction_id_killer].unk_101++;
    if ((Vehs[veh_id].state & VSTATE_ASSISTANT_WORM) && veh_id_killer >= 0 && !(GamePreferences & 0x40000)) {
        int32_t base_id = Vehs[veh_id_killer].home_base_id;
        if (base_id >= 0 ||
            (base_id = base_find_2(Vehs[veh_id_killer].x, Vehs[veh_id_killer].y, Vehs[veh_id_killer].faction_id)) >= 0) {
            PluralDefault = 0;
            uint32_t killer = Vehs[veh_id_killer].faction_id;
            Bases[base_id].state_flags |= 0x40000;
            Factions[killer].player_flags |= 0x1000;
            GenderDefault = MFactions[killer].is_leader_female;
            parse_says(6, MFactions[killer].name_leader, -1, -1);
            killer = Vehs[veh_id_killer].faction_id;
            GenderDefault = MFactions[killer].noun_gender;
            PluralDefault = MFactions[killer].is_noun_plural;
            parse_says(7, MFactions[killer].noun_faction, -1, -1);
            interlude(6, base_name_addr(base_id), 1, 0);
        }
    }
    // BUG(original): without a killer (veh_id_killer -1), Vehs[-1] is read for its faction.
    if (Vehs[veh_id].faction_id == 0 && veh_faction_raw(veh_id_killer) != 0 && energy)
        *energy += (morale_veh(veh_id, 1, 0) + 1) * 10;
    if (kill_count) (*kill_count)++;
    veh_kill(veh_id);
    return 1;
}

void GAMEAPI battle_kill_stack(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                               int32_t veh_id_killer, int32_t faction_id_killer) {
    if (veh_id < 0) return;
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    while (veh_id >= 0) {
        int32_t next = Vehs[veh_id].next_veh_id_stack;
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
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    while (veh_id >= 0) {
        int32_t next = Vehs[veh_id].next_veh_id_stack;
        int32_t hp = unit_max_hp(Vehs[veh_id].unit_id) - Vehs[veh_id].damage_taken;
        if ((hp < 1 || Vehs[veh_id].faction_id == 0) &&
            battle_kill(veh_id, kill_count, capture_count, energy, veh_id_killer, faction_id_killer)) {
            if (veh_id < veh_id_killer) veh_id_killer--;
            if (veh_id < next) next--;
        }
        veh_id = next;
    }
}

void GAMEAPI promote(int32_t veh_id) {
    int32_t unit_id = Vehs[veh_id].unit_id;
    if (unit_missile(unit_id)) return;
    if (!((uint8_t)Units[unit_id].plan < 8 || armor_value(unit_id) > 1)) return;
    if (morale_veh(veh_id, 1, 0) >= 6 || Vehs[veh_id].morale >= 6) return;
    if (morale_veh(veh_id, 1, 0) > 2 && game_rand() % 2 != 0) return;
    int32_t before = morale_veh(veh_id, 1, 0);
    uint32_t viewer = CurrentPlayerFaction;
    Vehs[veh_id].morale++;
    if (Vehs[veh_id].faction_id == viewer && (FactionStatus[0] & (uint8_t)(1 << (Vehs[veh_id].faction_id & 31))) &&
        before != morale_veh(veh_id, 1, 0)) {
        parse_says(1, Units[Vehs[veh_id].unit_id].name, -1, -1);
        if (Vehs[veh_id].unit_id < 64 && weapon_value(Vehs[veh_id].unit_id) < 0) {
            parse_say(0, Morale[morale_veh(veh_id, 1, 0)].name_lifecycle, -1, -1);
            NetMsg_pop(&NetMsg, "MORALE2", -5000, 0, "batwon_sm.pcx");
            return;
        }
        parse_say(0, Morale[morale_veh(veh_id, 1, 0)].name, -1, -1);
        NetMsg_pop(&NetMsg, "MORALE", -5000, 0, "batwon_sm.pcx");
    }
}

void GAMEAPI invasions(int32_t base_id) {
    int32_t bx = Bases[base_id].x;
    int32_t by = Bases[base_id].y;
    uint32_t base_faction = (uint8_t)Bases[base_id].faction_id;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& veh = Vehs[veh_id];
        uint32_t faction_id = veh.faction_id;
        if (faction_id == 0 || (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) || faction_id == base_faction ||
            (Factions[faction_id].diplo_status[base_faction] & DIPLO_TREATY) ||
            (map_tile(veh.x, veh.y)->climate & 0xE0) >= 0x60 || !veh_cargo(veh_id) ||
            !stack_check(veh_id, 3, 0, -1, -1))
            continue;
        int32_t dx = std::abs(bx - veh.x);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
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
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& veh = Vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id_def || chassis_triad(veh.unit_id) != TRIAD_AIR ||
            !has_abil(veh.unit_id, ABL_AIR_SUPERIORITY))
            continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if (!((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) && !(sq->items & 0x40000) &&
            !stack_check(veh_id, 6, 0x80, -1, -1))
            continue;
        int32_t dx = std::abs(veh.x - x);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
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
        parse_says(0, MFactions[faction_id_def].adj_name_faction, -1, -1);
        int32_t cy = Vehs[best_id].y;
        int32_t cx = Vehs[best_id].x;
        // BUG(original): base_find may return -1 (no bases): the name before Bases[0] is used.
        parse_says(1, reinterpret_cast<const char*>(&Bases[0]) + base_find(cx, cy) * (int32_t)sizeof(BASE) +
                          offsetof(BASE, name), -1, -1);
        Console_focus(&MainConsole, cx, cy, faction_id_def);
        NetMsg_pop(&NetMsg, "AIRSCRAMBLE", 5000, 0, nullptr);
        // Show the flight to the target, one tile at a time.
        for (;;) {
            int32_t dx = std::abs(cx - x);
            if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
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
                if (!(MapToggleFlat & 1) && ddx > MapHalfX) ddx = MapAreaX - ddx;
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
    Vehs[best_id].order = 0;
    Vehs[best_id].state &= 0xFCFFBDFF;
    return 1;
}

// ---- battle ---------------------------------------------------------------------------------------

int32_t GAMEAPI battle_fight(int32_t veh_id, int32_t dir, int32_t is_arty, int32_t is_real, int32_t* def_out) {
    // Artillery fire selects among all tiles within range (TableOffset), a melee attack a neighbour.
    int32_t x = Vehs[veh_id].x + (is_arty ? TableOffsetX[dir] : NearOffsetX[dir]);
    x = map_wrap_x(x);
    int32_t y = Vehs[veh_id].y + (is_arty ? TableOffsetY[dir] : NearOffsetY[dir]);
    return battle_fight_2(veh_id, dir, x, y, is_arty, is_real, def_out);
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
    GenderDefault = MFactions[faction_id].noun_gender;
    PluralDefault = MFactions[faction_id].is_noun_plural;
    return MFactions[faction_id].noun_faction;
}
// FUN_0050B9C0 / NetMsg_pop_2 (0x50B9E0): message box with a 5 s / -5 s delay.
void bat_msg(const char* text, const char* pcx) { NetMsg_pop(&NetMsg, text, 5000, 0, pcx); }
void bat_msg_2(const char* text, const char* pcx) { NetMsg_pop(&NetMsg, text, -5000, 0, pcx); }
// Draws the battle status line (StatusWin_draw on the info window) and marks it shown.
void bat_status(int32_t veh_id_atk, int32_t veh_id_def, int32_t offense, int32_t defense, int32_t flag) {
    StatusWin_draw(&InfoWin, veh_id_atk, veh_id_def, offense, defense, flag);
}
int32_t head_of_stack(int32_t veh_id) {
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
}  // namespace

void GAMEAPI mon_enemy_destroyed(int32_t faction_id, int32_t faction_id_killed) {
    int32_t year = game_year(CurrentTurn);
    MonumentEvent& ev = Monuments[faction_id].events[6];
    if ((MFactions[faction_id].rule_flags & 0x80) || ev.achieved || !faction_id_killed) return;
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

int32_t GAMEAPI battle_fight_2(int32_t veh_id, int32_t dir, int32_t x, int32_t y, int32_t is_arty, int32_t is_real,
                               int32_t* def_out) {
    int32_t report_type = 0;      // pact/vendetta battle report for the viewer
    int32_t disengaged = 0;       // the defender withdrew
    int32_t show_interlude = 0;
    int32_t intercepted = 0;
    int32_t captured = 0;         // marine detachment captured the defender's stack
    int32_t arty = is_arty;
    {
        int32_t unit_id = Vehs[veh_id].unit_id;
        arty = (arty == 0 && (!can_arty(unit_id, 1) || chassis_triad(unit_id) != TRIAD_LAND)) ? 0 : 1;
    }
    int32_t ax = Vehs[veh_id].x;
    int32_t ay = Vehs[veh_id].y;
    uint32_t atk_f = Vehs[veh_id].faction_id;
    int32_t top = veh_at(x, y);
    MAP* sq = map_tile(x, y);
    // A ship bombards a land tile where the units are mostly non-combat (or a base).
    if (can_arty(Vehs[veh_id].unit_id, 1) && veh_triad(veh_id) == TRIAD_SEA && (sq->climate & 0xE0) > 0x5F) {
        int32_t combat = stack_check(top, 3, 2, -1, -1);
        int32_t all = stack_check(top, 1, -1, -1, -1);
        if (combat < all || ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8)) arty = 1;
    }
    int32_t def_top = stack_fix(top);
    if (veh_triad(veh_id) == TRIAD_AIR && !unit_missile(Vehs[veh_id].unit_id) &&
        !stack_check(def_top, 6, 0x20, -1, -1) && is_real && (VehAttackFlags & 2))
        intercepted = interceptor(veh_faction_raw(def_top), atk_f, x, y);
    int32_t def_id = best_defender(def_top, veh_id, arty);
    int32_t veh_id_def = def_id;
    uint32_t def_f = Vehs[veh_id_def].faction_id;
    int32_t base_id = base_at(x, y);
    if (can_arty(Vehs[veh_id].unit_id, 1) && can_arty(Vehs[veh_id_def].unit_id, 1)) {
        if (veh_triad(veh_id) == TRIAD_LAND)
            arty |= 2;
        else if (arty || (sq->climate & 0xE0) > 0x5F)
            arty |= 2;
    }
    if (has_abil(Vehs[veh_id_def].unit_id, ABL_AIR_SUPERIORITY) && veh_triad(veh_id) == TRIAD_AIR &&
        !unit_missile(Vehs[veh_id].unit_id) && !unit_missile(Vehs[veh_id_def].unit_id) &&
        veh_triad(veh_id_def) == TRIAD_AIR)
        arty |= 0x1A;
    int32_t moves_cost;
    if ((arty & 3) == 0 || arty > 3) {
        moves_cost = Rules.move_rate_roads;
    } else {
        moves_cost = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
        moves_cost = moves_cost < 0 ? 0 : moves_cost > 999 ? 999 : moves_cost;
    }
    int32_t offense, defense;
    battle_compute(veh_id, veh_id_def, &offense, &defense, arty);
    int32_t atk_hp = veh_hp(veh_id);
    int32_t def_hp = veh_hp(veh_id_def);
    int32_t atk_unit = Vehs[veh_id].unit_id;
    int32_t def_unit = Vehs[veh_id_def].unit_id;
    // Psi combat: each round's damage is the opponent's reactor (hit points are reactor x 10).
    bool psi = weapon_value(atk_unit) < 0 || armor_value(def_unit) < 0;
    int32_t def_power = psi ? Units[def_unit].reactor_id : 1;
    int32_t atk_power = psi ? Units[atk_unit].reactor_id : 1;
    if (!is_real) {
        int32_t max_atk = unit_max_hp(atk_unit);
        int32_t max_def = unit_max_hp(def_unit);
        if (def_out) *def_out = veh_id_def;
        return ((arty & 1) ? 4 : 8) * (atk_hp * def_power * offense / max_atk) /
               (def_hp * atk_power * defense / max_def + 1);
    }
    uint32_t atk_bit = 1u << (atk_f & 31);
    if ((FactionStatus[0] & (uint8_t)atk_bit) && Units[atk_unit].plan != PLAN_PROBE &&
        (Units[def_unit].plan != PLAN_ARTIFACT || base_id >= 0) && (VehAttackFlags & 1) &&
        (!MultiplayerActive || !intercepted) && break_treaty(atk_f, def_f, 0xB))
        return 2;
    // Nerve gas
    int32_t nerve = 0;
    if (has_abil(Vehs[veh_id].unit_id, ABL_NERVE_GAS) && weapon_value(Vehs[veh_id].unit_id) >= 0 &&
        armor_value(Vehs[veh_id_def].unit_id) >= 0) {
        uint32_t humans = *reinterpret_cast<uint32_t*>(&FactionStatus[0]);
        if (!MultiplayerActive || (VehAttackFlags & 1)) {
            uint32_t state = Vehs[veh_id].state & ~VSTATE_USED_NERVE_GAS;
            Vehs[veh_id].state = state;
            if (atk_f == (uint32_t)CurrentPlayerFaction && (atk_bit & (uint8_t)humans)) {
                parse_says(0, Units[Vehs[veh_id].unit_id].name, -1, -1);
                parse_say(1, Ability[abil_index(ABL_NERVE_GAS)].name, -1, -1);
                BattleWin_stop_timer(&BattleWin);
                if (popp(ScriptFileName, "USENERVE", 0, "biohazd_sm.pcx", 0) != 0)
                    Vehs[veh_id].state |= VSTATE_USED_NERVE_GAS;
                BattleWin_pulse_timer(&BattleWin);
            } else {
                humans &= 0xFF;
                if (!(atk_bit & humans)) {
                    bool use = false;
                    uint32_t status = Factions[atk_f].diplo_status[def_f];
                    uint32_t def_human = (1u << (def_f & 31)) & humans;
                    if (!(status & 0x40000) && !(Factions[atk_f].player_flags & 0x1000000) &&
                        (def_human == 0 || ProposalPassCount[8] <= ProposalPassCount[9])) {
                        if (def_human && (status & 0x20)) {
                            int32_t extra;
                            if (!(GameRules & 0x40) ||
                                (Factions[def_f].ranking <= Factions[atk_f].ranking &&
                                 Factions[def_f].diplo_combat_count[atk_f] <= Factions[atk_f].diplo_combat_count[def_f]))
                                extra = 0;
                            else
                                extra = (Factions[atk_f].AI_fight + 1) * 3;
                            if (Factions[def_f].integrity_blemishes + extra > 5) use = true;
                        }
                    } else {
                        use = true;
                    }
                    if (use) Vehs[veh_id].state = state | VSTATE_USED_NERVE_GAS;
                }
            }
            if (MultiplayerActive) synch_veh(veh_id);
        }
        if ((VehAttackFlags & 2) && (Vehs[veh_id].state & VSTATE_USED_NERVE_GAS)) nerve = 1;
    }
    if (base_id >= 0) Vehs[veh_id].state &= ~0x40000u;
    if (!(Vehs[veh_id].state & 0x1000) && (VehAttackFlags & 2)) {
        if (atk_f != 0 && Vehs[veh_id].unit_id != BSC_SPORE_LAUNCHER && Vehs[veh_id].unit_id != BSC_MIND_WORMS) {
            // A unit with less than a full move left attacks at reduced strength.
            int32_t moves = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
            moves = moves < 0 ? 0 : moves > 999 ? 999 : moves;
            if (Rules.move_rate_roads < 0) moves = 0;
            else if (moves > Rules.move_rate_roads) moves = Rules.move_rate_roads;
            offense = moves * offense / Rules.move_rate_roads;
            char* text = label(0x54C);
            bat_line(0, (moves - Rules.move_rate_roads) * 100 / Rules.move_rate_roads, text);
        }
        if (arty != 1) Vehs[veh_id].moves_spent += (uint8_t)moves_cost;
    }
    Vehs[veh_id].state = (Vehs[veh_id].state & ~0x1000u) | 4;
    Vehs[veh_id_def].state |= 4;
    if (!(VehAttackFlags & 2)) return 0;
    Vehs[veh_id].state |= 0x40;
    // Diplomatic consequences
    if (Units[Vehs[veh_id].unit_id].plan != PLAN_PROBE && Units[Vehs[veh_id_def].unit_id].plan != PLAN_ARTIFACT) {
        bool surprise = false;
        if (FactionStatus[0] & (uint8_t)(1 << (def_f & 31))) {
            uint32_t status = Factions[def_f].diplo_status[atk_f];
            surprise = (status & 6) ||
                       !(((status & 8) == 0 && !(Factions[atk_f].diplo_status[def_f] & 0x800)) || (status & 0x10));
        }
        if (!surprise) {
            if (((Factions[atk_f].diplo_status[def_f] & 2) || (Factions[def_f].diplo_status[atk_f] & 2)) &&
                atk_f != (uint32_t)CurrentPlayerFaction && def_f != (uint32_t)CurrentPlayerFaction) {
                if (!(Factions[CurrentPlayerFaction].diplo_status[def_f] & 1)) {
                    if (!(Factions[CurrentPlayerFaction].diplo_status[atk_f] & 1)) {
                        if (GameState & 0x80) report_type = 1;
                    } else {
                        report_type = 3;
                    }
                } else {
                    report_type = 2;
                }
            }
        } else {
            MFaction& m = MFactions[atk_f];
            if (def_f == (uint32_t)CurrentPlayerFaction) {
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(0, m.title_leader, -1, -1);
                GenderDefault = m.is_leader_female;
                PluralDefault = 0;
                parse_says(1, m.name_leader, -1, -1);
                GenderDefault = m.noun_gender;
                PluralDefault = m.is_noun_plural;
                parse_says(2, m.noun_faction, -1, -1);
                NetMsg_pop(&NetMsg, "SURPRISE", -5000, 0, nullptr);
            } else if (PbemActive) {
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(0, m.title_leader, -1, -1);
                GenderDefault = m.is_leader_female;
                PluralDefault = 0;
                parse_says(1, m.name_leader, -1, -1);
                GenderDefault = m.noun_gender;
                PluralDefault = m.is_noun_plural;
                parse_says(2, m.noun_faction, -1, -1);
                POP2("SURPRISE", nullptr, -1 - (int32_t)def_f);
            }
            Factions[def_f].diplo_spoke[atk_f] = CurrentTurn;
        }
        act_of_aggression(atk_f, def_f);
        // Pact members of the attacker whose territory the attack came from annoy the defender.
        int8_t owner = map_tile(ax, ay)->owner;
        for (int32_t i = 1; i < 8; i++) {
            if ((uint32_t)i == atk_f || (uint32_t)i == def_f || owner != i) continue;
            if ((Factions[atk_f].diplo_status[i] & 1) && !(Factions[def_f].diplo_status[i] & 0x10) &&
                (Factions[def_f].diplo_status[i] & 8) && Factions[i].mil_strength_1 < Factions[def_f].mil_strength_1) {
                treaty_on(i, def_f, 0x2000);
                if (Factions[i].diplo_status[def_f] & 0x800000) cause_friction(def_f, i, 3);
            }
        }
    }
    if (nerve) {
        offense += offense / 2;
        bat_line(0, 50, label(0x554));
    }
    if (MultiplayerActive) log_say_2("Battle Resolution!", veh_id, veh_id_def, 0);
    int32_t atk_unit_saved = Vehs[veh_id].unit_id;
    int32_t def_unit_saved = Vehs[veh_id_def].unit_id;
    if ((atk_f == (uint32_t)CurrentPlayerFaction || def_f == (uint32_t)CurrentPlayerFaction) && !MultiplayerActive &&
        (weapon_value(atk_unit_saved) < 0 || armor_value(def_unit_saved) < 0) && tut_check2(2))
        TutWin_tut_map(&TutWin, "PSICOMBAT", x, y, atk_f != (uint32_t)CurrentPlayerFaction ? veh_id_def : veh_id,
                       nullptr, 1, -1, -1);
    // Difficulty adjustments against human players.
    int32_t level = 3 - (atk_f != 0);
    uint8_t humans = FactionStatus[0];
    if (Factions[def_f].diff_level < level) {
        if ((humans & (1 << (def_f & 31))) && !(atk_bit & humans))
            offense = (Factions[def_f].diff_level + 1) * offense / 4;
    }
    if (Factions[atk_f].diff_level < level && (atk_bit & humans) && !(humans & (1 << (def_f & 31))))
        offense = (4 - Factions[atk_f].diff_level) * offense / 2;
    if (atk_f == 0) {
        if (!(FactionStatus[0] & (uint8_t)(1 << (def_f & 31)))) {
            if (Units[Vehs[veh_id_def].unit_id].plan == PLAN_TERRAFORM) {
                offense /= 2;
                if (!(GameState & 0x80) && !MultiplayerActive &&
                    (Vehs[veh_id_def].faction_id != (uint32_t)CurrentPlayerFaction ||
                     (Vehs[veh_id_def].flags & 0x440) == 0x440) &&
                    !(Vehs[veh_id_def].visibility & (1 << (CurrentPlayerFaction & 31))))
                    offense /= 2;
            } else {
                offense = (Factions[def_f].ranking + 8) * offense / 16;
            }
        }
        if (base_id >= 0) {
            if (def_f == (uint32_t)CurrentPlayerFaction) show_interlude = 1;
            if (Factions[def_f].base_count < 2) offense /= 2;
            if (CurrentTurn < 50) offense /= 2;
            if (bat_base_fac(base_id, FAC_HEADQUARTERS) && Factions[def_f].base_count < 4) offense /= 2;
        }
    }
    if (CurrentTurn < 15) {
        if (atk_f == 0) offense /= 3;
        if (def_f == 0) defense /= 2;
    }
    if (base_id >= 0 && veh_triad(veh_id) == TRIAD_AIR) Bases[base_id].state_flags |= 0x100000;
    // Human attackers are warned about bad odds (and may see them).
    if ((VehAttackFlags & 1) && !intercepted && (FactionStatus[0] & (uint8_t)atk_bit) &&
        atk_f == (uint32_t)CurrentPlayerFaction && arty == 0 && !unit_missile(Vehs[veh_id].unit_id) &&
        !(Vehs[veh_id].state & 0x4200)) {
        bool ask = true;
        const char* text;
        if (atk_hp * def_power * offense * 2 < def_hp * atk_power * defense) {
            SubInterface_set_iface_mode(&BattleWin);
            BattleWin_clear(&BattleWin);
            bat_status(veh_id, veh_id_def, offense, defense, 0);
            InfoWinBattleShown = 1;
            MFaction& m = MFactions[CurrentPlayerFaction];
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(0, m.title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(1, m.name_leader, -1, -1);
            BattleWin_stop_timer(&BattleWin);
            text = "BADIDEA";
        } else if (GameMorePreferences & 0x8000) {
            SubInterface_set_iface_mode(&BattleWin);
            BattleWin_clear(&BattleWin);
            bat_status(veh_id, veh_id_def, offense, defense, 0);
            InfoWinBattleShown = 1;
            MFaction& m = MFactions[CurrentPlayerFaction];
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(0, m.title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(1, m.name_leader, -1, -1);
            char num[80];
            StrBuffer[0] = 0;
            crt_itoa(offense >> 8, num, 10);
            crt_strcat(StrBuffer, num);
            crt_strcat(StrBuffer, ".");
            crt_itoa((int32_t)((offense & 0xFF) * 10) >> 8, num, 10);
            crt_strcat(StrBuffer, num);
            parse_says(2, StrBuffer, -1, -1);
            StrBuffer[0] = 0;
            crt_itoa(defense >> 8, num, 10);
            crt_strcat(StrBuffer, num);
            crt_strcat(StrBuffer, ".");
            crt_itoa((int32_t)((defense & 0xFF) * 10) >> 8, num, 10);
            crt_strcat(StrBuffer, num);
            parse_says(3, StrBuffer, -1, -1);
            // Odds as a reduced fraction.
            int32_t a = veh_hp(veh_id) * offense;
            int32_t b = veh_hp(veh_id_def) * defense;
            for (int32_t d = 2;; d++) {
                int32_t lo = a <= b ? a : b;  // min(a, b)
                lo = b <= a ? b : a;
                if (lo <= d) break;
                for (; a != 0 && b != 0 && a % d == 0 && b % d == 0; b /= d) a /= d;
            }
            for (; a > 19999 && b > 19999; b /= 10000) a /= 10000;
            for (; a > 9999 && b > 9999; b /= 5000) a /= 5000;
            for (; a > 1999 && b > 1999; b /= 1000) a /= 1000;
            for (; a > 999 && b > 999; b /= 500) a /= 500;
            for (; a > 199 && b > 199; b /= 100) a /= 100;
            for (; a > 99 && b > 99; b /= 50) a /= 50;
            BattleWin_stop_timer(&BattleWin);
            parse_num(0, a);
            parse_num(1, b);
            text = (Vehs[veh_id].damage_taken || Vehs[veh_id_def].damage_taken) ? "GOODIDEA2" : "GOODIDEA";
        } else {
            ask = false;
        }
        if (ask) {
            if (popp(ScriptFileName, text, 0, "hasty_sm.pcx", 0) == 0) {
                BattleWin_pulse_timer(&BattleWin);
                if (VehAttackFlags & 2) Vehs[veh_id].moves_spent -= (uint8_t)moves_cost;
                Vehs[veh_id].order = 0;
                Vehs[veh_id].state &= 0xFCFFBDFF;
                StatusWin_reset(&InfoWin);
                StatusWin_redraw(&InfoWin);
                return 1;
            }
            BattleWin_pulse_timer(&BattleWin);
        }
    }
    char nerve_base[80];
    if (nerve) {
        atrocity(atk_f, def_f, base_id >= 0, 0);
        nerve_base[0] = 0;
        if (base_id >= 0) crt_strcpy(nerve_base, Bases[base_id].name);
    }
    if (Vehs[veh_id].order == ORDER_MOVE_TO) {
        Vehs[veh_id].order = 0;
        Vehs[veh_id].state &= 0xFCFFBDFF;
    }
    int32_t husk_refresh = 0;
    uint32_t viewer = CurrentPlayerFaction;
    // Whether the battle is shown: the viewer takes part (or the omniscient view), or it is visible
    // and the viewer's preferences show battles of others.
    int32_t display = (atk_f == viewer || def_f == viewer || (GameState & 0x80)) ? 1 : 0;
    uint8_t vbit = (uint8_t)(1 << (viewer & 31));
    int32_t visible = ((Vehs[veh_id].faction_id == viewer && (Vehs[veh_id].flags & 0x440) != 0x440) ||
                       (Vehs[veh_id].visibility & vbit) ||
                       (Vehs[veh_id_def].faction_id == viewer && (Vehs[veh_id_def].flags & 0x440) != 0x440) ||
                       (Vehs[veh_id_def].visibility & vbit))
                          ? 1
                          : 0;
    uint32_t pref = (Factions[viewer].diplo_status[atk_f] & 1) ? (GamePreferences & 0x8000) : (GamePreferences & 4);
    if (pref) display |= visible;
    if (arty == 1) {
        // ---- bombardment: every defender of the stack takes damage, none is killed outright ----
        add_goal(def_f, 0x10, 4, x, y, -1);
        add_goal(def_f, 0, 4, ax, ay, -1);
        if (base_id >= 0) {
            Bases[base_id].state_flags |= 0x200000;
            Bases[base_id].event_flags |= 0x100;
        }
        if (def_id >= 0) {
            bool paused = true;
            int32_t v = head_of_stack(veh_id_def);
            while (v >= 0) {
                int32_t v_unit = Vehs[v].unit_id;
                if (chassis_triad(v_unit) != TRIAD_AIR || has_abil(Vehs[veh_id].unit_id, ABL_AIR_SUPERIORITY) ||
                    base_id >= 0 || (map_tile(x, y)->items & 0x40000)) {
                    veh_skip(veh_id);
                    battle_compute(veh_id, v, &offense, &defense, 1);
                    int32_t den = Rules.artillery_dmg_denominator * defense;
                    den = den < 1 ? 1 : den > 9999999 ? 9999999 : den;
                    int32_t rounds = Rules.artillery_dmg_numerator * offense / den + 1;
                    rounds = rounds < 1 ? 1 : rounds > 999 ? 999 : rounds;
                    int32_t damage = rounds - 1 > 0 ? game_rand() % rounds : 0;
                    // Maximum damage: a share of the hit points that bombardment cannot remove.
                    int32_t keep;
                    if (base_id < 0 && !(map_tile(x, y)->items & 0x800)) {
                        int32_t pct = chassis_triad(Vehs[v].unit_id) != TRIAD_LAND ? Rules.max_dmg_percent_arty_sea
                                                                                    : Rules.max_dmg_percent_arty_open;
                        keep = (100 - pct) * unit_max_hp(Vehs[v].unit_id) + 99;
                    } else {
                        keep = (100 - Rules.max_dmg_percent_arty_base_bunker) * unit_max_hp(Vehs[v].unit_id) + 99;
                    }
                    keep /= 100;
                    for (; damage > 0; damage--)
                        if (keep <= veh_hp(v) - damage) break;
                    if (display) {
                        paused = false;
                        VehDrawDefendID = v;
                        Vehs[veh_id].visibility |= (uint8_t)(1 << (def_f & 31));
                        VehDrawAttackID = veh_id;
                        if (!Console_focus(&MainConsole, x, y, atk_f)) {
                            draw_tile(ax, ay, 2);
                            draw_tile(x, y, 2);
                        }
                        SubInterface_set_iface_mode(&BattleWin);
                        BattleWin_clear(&BattleWin);
                        bat_status(veh_id, v, offense, defense, 1);
                        InfoWinBattleShown = 1;
                        flush_input();
                        if (keep == 0 || Vehs[v].damage_taken != (uint32_t)keep || v < 0 ||
                            Vehs[v].next_veh_id_stack < 0) {
                            boom(x, y, 0);
                            paused = true;
                        }
                    }
                    Vehs[v].damage_taken += (uint8_t)damage;
                    Vehs[v].state |= 4;
                    if (Units[Vehs[v].unit_id].plan == PLAN_TERRAFORM) Vehs[v].movement_turns = 0;
                    if (base_id < 0) Vehs[v].state |= 8;
                    if (display) {
                        bat_status(veh_id, v, offense, defense, 1);
                        draw_tile(x, y, 2);
                        VehDrawAttackID = -1;
                        VehDrawDefendID = -1;
                        if (!(GameMorePreferences & 0x10)) {
                            if (!ui_shift_pressed() && !(GamePreferences & 8) && !MultiplayerActive && paused)
                                clock_wait(200);
                        } else {
                            BattleWin_stop_timer(&BattleWin);
                            popp(ScriptFileName, "BATTLEPAUSE", 0, "batwon_sm.pcx", 0);
                            BattleWin_pulse_timer(&BattleWin);
                            if (DialogToggle) {
                                GameMorePreferences &= ~0x10;
                                prefs_save(0);
                            }
                        }
                    }
                }
                if (v < 0) break;
                v = Vehs[v].next_veh_id_stack;
            }
        }
        int32_t v = stack_fix(veh_at(x, y));
        while (v >= 0) {
            if (unit_max_hp(Vehs[v].unit_id) - Vehs[v].damage_taken < 1) {
                stack_veh(v, 2);
                stack_kill(v);
                v = stack_fix(veh_at(x, y));
                Factions[atk_f].diplo_combat_total[def_f]++;
                Factions[atk_f].diplo_combat_count[def_f]++;
            } else {
                v = Vehs[v].next_veh_id_stack;
            }
        }
        draw_tile_fixup(x, y, 1, 2);
        if (base_id >= 0 && !(FactionStatus[0] & (uint8_t)(1 << (def_f & 31)))) base_reset(base_id, 0);
        return 0;
    }
    if (arty) add_goal(def_f, 0x10, 2, x, y, -1);
    if (display) {
        if (MultiplayerActive && BaseWinMultiplayerRefresh) husk_refresh = 1;
        Vehs[veh_id].visibility |= (uint8_t)(1 << (def_f & 31));
        VehDrawAttackID = veh_id;
        VehDrawDefendID = veh_id_def;
        if (!Console_focus(&MainConsole, x, y, atk_f)) {
            draw_tile(ax, ay, 2);
            draw_tile(x, y, 2);
        }
        stack_veh(veh_id, 1);
        SubInterface_set_iface_mode(&BattleWin);
        BattleWin_clear(&BattleWin);
        bat_status(veh_id, veh_id_def, offense, defense, arty);
        InfoWinBattleShown = 1;
        flush_input();
        if (!ui_shift_pressed() && !(arty & 1)) veh_scoot(veh_id, ax, ay, dir, 1);
        stack_put(veh_id, ax, ay);
        draw_tile(ax, ay, 2);
        draw_tile(x, y, 2);
    }
    // A faster defender in the open may withdraw after losing half its hit points.
    int32_t retreat = 0;
    {
        int32_t speed_def = speed(veh_id_def, 1);
        int32_t speed_atk = speed(veh_id, 1);
        int32_t du = Vehs[veh_id_def].unit_id;
        if (speed_atk < speed_def && arty == 0 && weapon_value(du) != 0 && !(Vehs[veh_id_def].state & 0x40) &&
            veh_triad(veh_id) != TRIAD_AIR && chassis_triad(du) != TRIAD_AIR &&
            (!has_abil(Vehs[veh_id].unit_id, ABL_COMM_JAMMER) ||
             has_abil(Vehs[veh_id_def].unit_id, ABL_DISSOCIATIVE_WAVE)) &&
            Vehs[veh_id_def].order != ORDER_HOLD && base_id < 0 && !(map_tile(x, y)->items & 0x40800) &&
            stack_check(veh_id_def, 1, -1, -1, -1) == 1)
            retreat = (veh_hp(veh_id_def) + 1) / 2;
    }
    // Marine detachment: may capture a ship at sea.
    int32_t marine = 0;
    if (has_abil(Vehs[veh_id].unit_id, ABL_MARINE_DETACHMENT) && veh_triad(veh_id) == TRIAD_SEA &&
        (map_tile(x, y)->climate & 0xE0) < 0x60) {
        int32_t du = Vehs[veh_id_def].unit_id;
        if (chassis_triad(du) == TRIAD_SEA && !unit_is_native(du) && base_id < 0)
            marine = veh_hp(veh_id_def) * 2 / 3;
    }
    if (Units[Vehs[veh_id].unit_id].plan == PLAN_PLANET_BUSTER && Units[Vehs[veh_id].unit_id].reactor_id) {
        planet_busting(veh_id, x, y);
        GameDrawState |= 4;
        return 0;
    }
    int32_t round_div = (unit_missile(Vehs[veh_id].unit_id) || MultiplayerActive) ? 0 : 3;
    int32_t last_hit = 0;
    if (unit_missile(Vehs[veh_id].unit_id) || MultiplayerActive) boom(x, y, 2 - (veh_hp(veh_id_def) != 0));
    // Artillery duel between a ship and a land unit: one exchange only (when displayed, see below).
    int32_t single = ((arty & 2) && ((veh_triad(veh_id) == TRIAD_SEA) != (veh_triad(veh_id_def) == TRIAD_SEA))) ? 1 : 0;
    int32_t rx = 0, ry = 0;  // retreat destination
    for (;;) {
        if (veh_hp(veh_id) == 0 || veh_hp(veh_id_def) == 0) break;
        int32_t roll_atk = offense - 1 > 0 ? game_rand() % offense : 0;
        int32_t roll_def = defense - 1 > 0 ? game_rand() % defense : 0;
        if (roll_atk <= roll_def) {
            uint8_t damage = (uint8_t)(Vehs[veh_id].damage_taken + atk_power);
            Vehs[veh_id].damage_taken = damage;
            // BUG(original): the single exchange of a ship/land artillery duel ends early only when
            // the battle is displayed.
            if (!display || !round_div ||
                (int32_t)damage / (round_div * atk_power) == (int32_t)(damage - atk_power) / (round_div * atk_power))
                continue;
            last_hit = 1;
            bat_status(veh_id, veh_id_def, offense, defense, arty);
            boom(ax, ay, 9);
            if (veh_hp(veh_id) > 0) {
                draw_tile(ax, ay, 2);
                draw_tile(x, y, 2);
            }
            if (!single) continue;
            break;
        }
        uint8_t damage = (uint8_t)(Vehs[veh_id_def].damage_taken + def_power);
        Vehs[veh_id_def].damage_taken = damage;
        if (display && round_div &&
            (int32_t)damage / (round_div * def_power) != (int32_t)(damage - def_power) / (round_div * def_power)) {
            last_hit = 2;
            bat_status(veh_id, veh_id_def, offense, defense, arty);
            boom(x, y, 1);
            if (veh_hp(veh_id_def) > 0) {
                draw_tile(ax, ay, 2);
                draw_tile(x, y, 2);
            }
            if (single) break;
        }
        if (marine && veh_hp(veh_id_def) > 0 && (marine -= def_power) < 1 && game_rand() % 100 < 50) {
            // The marines take over the defender's stack.
            uint32_t old_f = Vehs[veh_id_def].faction_id;
            captured = Vehs[veh_id].faction_id;
            if (def_id >= 0) {
                for (int32_t v = head_of_stack(veh_id_def); v >= 0; v = Vehs[v].next_veh_id_stack) {
                    Vehs[v].faction_id = Vehs[veh_id].faction_id;
                    parse_says(0, Units[Vehs[v].unit_id].name, -1, -1);
                    int32_t b = base_find(x, y);
                    // BUG(original): base_find may return -1 (no bases): Bases[-1] is read.
                    const uint8_t* braw = reinterpret_cast<const uint8_t*>(&Bases[0]) + b * (int32_t)sizeof(BASE);
                    if (braw[offsetof(BASE, faction_id)] == Vehs[veh_id].faction_id &&
                        (int8_t)braw[offsetof(BASE, pop_size)] > 2)
                        Vehs[v].home_base_id = (int16_t)b;
                    else
                        Vehs[v].home_base_id = -1;
                    veh_skip(v);
                }
            }
            bit_set(x, y, 0x100000, 1);
            owner_set(x, y, Vehs[veh_id].faction_id);
            if (Vehs[veh_id].faction_id == (uint32_t)CurrentPlayerFaction)
                draw_tile_fixup(x, y, 1, 2);
            else
                draw_tile(x, y, 2);
            if ((uint32_t)captured == (uint32_t)CurrentPlayerFaction) {
                parse_says(1, MFactions[old_f].adj_name_faction, -1, -1);
                popp(ScriptFileName, "WEMARINEDETACH", 0, "navun_sm.pcx", 0);
            }
            if (old_f == (uint32_t)CurrentPlayerFaction) {
                parse_says(1, MFactions[captured].adj_name_faction, -1, -1);
                popp(ScriptFileName, "THEYMARINEDETACH", 0, "navun_sm.pcx", 0);
            }
            log_say_2("Detachment", 0, 0, 0);
            captured = 1;
            break;
        }
        if (retreat && veh_hp(veh_id_def) > 0 && (retreat -= def_power) < 1) {
            // Withdraw to the free tile most opposite to the attack.
            int32_t best_dir = -1;
            uint32_t best = 0;
            for (int32_t d = 0; d < 8; d++) {
                int32_t x2 = map_wrap_x(NearOffsetX[d] + x);
                int32_t y2 = NearOffsetY[d] + y;
                rx = x2;
                ry = y2;
                if (!map_in_bounds(x2, y2) || zoc_move(x2, y2, def_f)) continue;
                int32_t du = Vehs[veh_id_def].unit_id;
                MAP* sq2 = map_tile(x2, y2);
                if (!(chassis_triad(du) == TRIAD_AIR || ((sq2->climate & 0xE0) < 0x60) == (chassis_triad(du) == TRIAD_SEA)))
                    continue;
                if (goody_at(x2, y2)) continue;
                if (!(!(sq2->items & BIT_FUNGUS) || (sq2->climate & 0xE0) < 0x40 || owns_project(16, def_f) ||
                      unit_is_native(du)))
                    continue;
                uint32_t occupant = sq2->val2 & 0xF;
                if ((sq2->items & 3) && occupant <= 7 && occupant != def_f &&
                    !(Factions[occupant].diplo_status[def_f] & 1))
                    continue;
                uint32_t diff = (uint32_t)(d - (dir ^ 4));
                if ((int32_t)diff < 0) diff = (uint32_t)((dir ^ 4) - d);
                diff &= 7;
                if (diff > 4) diff = 8 - diff;
                if ((int32_t)diff > 2 && (int32_t)best < (int32_t)diff) {
                    best = diff;
                    best_dir = d;
                }
            }
            if (best_dir >= 0) {
                rx = map_wrap_x(NearOffsetX[best_dir] + x);
                ry = NearOffsetY[best_dir] + y;
                stack_put(stack_veh(veh_id_def, 1), rx, ry);
                disengaged = 1;
                break;
            }
            retreat = 0;
        }
    }
    // ---- outcome ----
    int32_t atk_left = veh_hp(veh_id);
    if (display || visible) {
        draw_tile_fixup_2(ax, ay, x, y, 2, 2);
        if (disengaged && (ax != rx || ay != ry) && (x != rx || y != ry)) draw_tile_fixup(rx, ry, 2, 2);
        if (display) {
            bat_status(veh_id, veh_id_def, offense, defense, arty);
            if (!disengaged) {
                if (atk_left == 0) {
                    if (!single) {
                        if (last_hit == 1 || MultiplayerActive) {
                            if (!(veh_hp(veh_id) > 0 && !MultiplayerActive))
                                boom(ax, ay, veh_hp(veh_id) != 0 ? 0 : 2);
                        } else {
                            boom(ax, ay, veh_hp(veh_id) != 0 ? 1 : 2);
                        }
                    } else {
                        if (last_hit != 2 && !MultiplayerActive) boom(x, y, veh_hp(veh_id_def) != 0);
                        boom(ax, ay, veh_hp(veh_id) != 0 ? 1 : 2);
                    }
                } else if (!single) {
                    if (last_hit == 2 ? (MultiplayerActive || veh_hp(veh_id_def) == 0) : MultiplayerActive)
                        boom(x, y, (veh_hp(veh_id_def) != 0 ? 0 : 2) | 8);
                    else if (last_hit != 2)
                        boom(x, y, (2 - (veh_hp(veh_id_def) != 0)) | 8);
                } else {
                    if (last_hit != 1 && !MultiplayerActive) boom(ax, ay, (2 - (veh_hp(veh_id) != 0)) | 8);
                    boom(x, y, (2 - (veh_hp(veh_id_def) != 0)) | 8);
                }
            }
            VehDrawAttackID = -1;
            VehDrawDefendID = -1;
        }
    }
    if (single && veh_hp(veh_id) > 0 && veh_hp(veh_id_def) > 0) {
        // Both survive the duel.
        if (MultiplayerActive && def_f == (uint32_t)CurrentPlayerFaction) {
            parse_says(0, Units[Vehs[veh_id_def].unit_id].name, -1, -1);
            StrBuffer[0] = 0;
            say_stats_3(StrBuffer, veh_id_def);
            parse_says(1, StrBuffer, -1, -1);
            parse_says(2, MFactions[atk_f].adj_name_faction, -1, -1);
            parse_says(3, Units[Vehs[veh_id].unit_id].name, -1, -1);
            StrBuffer[0] = 0;
            say_stats_3(StrBuffer, Vehs[veh_id].unit_id);
            parse_says(4, StrBuffer, -1, -1);
            int32_t b = base_find_3(x, y, -1, -1, -1, CurrentPlayerFaction);
            if (b >= 0) parse_says(5, Bases[b].name, -1, -1);
            bat_msg("COMBATSURVIVE", nullptr);
        }
        return 0;
    }
    // A battle on a third faction's land: it loses patience with the attacker.
    {
        MAP* sq2 = map_tile(x, y);
        int32_t owner = sq2->owner;
        if ((sq2->climate & 0xE0) > 0x5F && (uint32_t)owner != atk_f && (uint32_t)owner != def_f &&
            (diplo_status_raw(owner, def_f) & 0x10) && (diplo_status_raw(owner, atk_f) & 7)) {
            // BUG(original): owner -1 (unowned land) indexes Factions[-1] / diplo_gifts[-1].
            int32_t* gifts = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Factions[atk_f].diplo_gifts[0]) +
                                                        owner * 4);
            if (*gifts == 0) *gifts = 1;
            cause_friction(owner, atk_f, -1);
        }
    }
    // Promotion of the winner.
    if (atk_left == 0) {
        offense = (int32_t)((uint32_t)Units[Vehs[veh_id].unit_id].reactor_id * atk_hp * offense) /
                  unit_max_hp(Vehs[veh_id].unit_id);
        defense = (int32_t)((uint32_t)Units[Vehs[veh_id_def].unit_id].reactor_id * veh_hp(veh_id_def) * defense) / def_hp;
        if (Units[Vehs[veh_id].unit_id].plan != PLAN_ARTIFACT) {
            bool promo = true;
            if (morale_veh(veh_id_def, 1, 0) > 1) {
                int32_t sum = offense + defense;
                int32_t roll = sum - 1 > 0 ? game_rand() % sum : 0;
                if (offense < roll && (base_id < 0 || morale_veh(veh_id_def, 1, 0) > 3)) promo = false;
            }
            if (promo) promote(veh_id_def);
        }
        if (def_f == 0) Vehs[veh_id_def].home_base_id = Vehs[veh_id].home_base_id;
    } else {
        offense = (int32_t)((uint32_t)Units[Vehs[veh_id].unit_id].reactor_id * veh_hp(veh_id) * offense) / atk_hp;
        defense = (int32_t)((uint32_t)Units[Vehs[veh_id_def].unit_id].reactor_id * def_hp * defense) /
                  unit_max_hp(Vehs[veh_id_def].unit_id);
        if (Units[Vehs[veh_id_def].unit_id].plan != PLAN_ARTIFACT) {
            bool promo = true;
            if (morale_veh(veh_id, 1, 0) > 1) {
                int32_t sum = offense + defense;
                int32_t roll = sum - 1 > 0 ? game_rand() % sum : 0;
                if (defense < roll) promo = false;
            }
            if (promo) promote(veh_id);
        }
        if (atk_f == 0) Vehs[veh_id].home_base_id = Vehs[veh_id_def].home_base_id;
    }
    int32_t kills = 0, captures = 0, energy = 0, collateral = 0;
    if (!captured && !disengaged) {
        if (atk_left == 0) {
            // ---- the attacker is destroyed ----
            FactionCombatWin[def_f]++;
            FactionCombatLoss[atk_f]++;
            if (!MultiplayerActive || def_f != (uint32_t)CurrentPlayerFaction) {
                if (PbemActive && (FactionStatus[0] & (uint8_t)(1 << (def_f & 31)))) {
                    parse_says(0, Units[Vehs[veh_id_def].unit_id].name, -1, -1);
                    clear();
                    say_stats_3(StrBuffer, Vehs[veh_id_def].unit_id);
                    parse_says(1, StrBuffer, -1, -1);
                    parse_says(2, MFactions[atk_f].adj_name_faction, -1, -1);
                    parse_says(3, Units[Vehs[veh_id].unit_id].name, -1, -1);
                    clear();
                    say_stats_3(StrBuffer, Vehs[veh_id].unit_id);
                    parse_says(4, StrBuffer, -1, -1);
                    int32_t b = base_find_3(x, y, -1, -1, -1, def_f);
                    if (b >= 0) parse_says(5, Bases[b].name, -1, -1);
                    POP3("COMBATWIN", 0, def_f, x, y);
                }
            } else {
                parse_says(0, Units[Vehs[veh_id_def].unit_id].name, -1, -1);
                clear();
                say_stats_3(StrBuffer, Vehs[veh_id_def].unit_id);
                parse_says(1, StrBuffer, -1, -1);
                parse_says(2, MFactions[atk_f].adj_name_faction, -1, -1);
                parse_says(3, Units[Vehs[veh_id].unit_id].name, -1, -1);
                clear();
                say_stats_3(StrBuffer, Vehs[veh_id].unit_id);
                parse_says(4, StrBuffer, -1, -1);
                int32_t b = base_find_3(x, y, -1, -1, -1, CurrentPlayerFaction);
                if (b >= 0) parse_says(5, Bases[b].name, -1, -1);
                bat_msg("COMBATWIN", nullptr);
            }
            stack_veh(veh_id, 1);
            battle_kill_stack(veh_id, &kills, &captures, &energy, -1, def_f);
            Factions[def_f].diplo_combat_total[atk_f]++;
            Factions[def_f].diplo_combat_count[atk_f]++;
            if (atk_f != 0 && def_f != 0) mon_enemy_destroyed(def_f, atk_f);
        } else {
            // ---- the defender is destroyed ----
            Factions[atk_f].diplo_combat_total[def_f]++;
            FactionCombatWin[atk_f]++;
            Factions[atk_f].diplo_combat_count[def_f]++;
            FactionCombatLoss[def_f]++;
            if (!MultiplayerActive || def_f != (uint32_t)CurrentPlayerFaction) {
                if (PbemActive && (FactionStatus[0] & (uint8_t)(1 << (def_f & 31)))) {
                    parse_says(0, Units[Vehs[veh_id_def].unit_id].name, -1, -1);
                    StrBuffer[0] = 0;
                    say_stats_3(StrBuffer, Vehs[veh_id_def].unit_id);
                    parse_says(1, StrBuffer, -1, -1);
                    parse_says(2, MFactions[atk_f].adj_name_faction, -1, -1);
                    parse_says(3, Units[Vehs[veh_id].unit_id].name, -1, -1);
                    StrBuffer[0] = 0;
                    say_stats_3(StrBuffer, Vehs[veh_id].unit_id);
                    parse_says(4, StrBuffer, -1, -1);
                    int32_t b = base_find_3(x, y, -1, -1, -1, def_f);
                    if (b >= 0) parse_says(5, Bases[b].name, -1, -1);
                    POP3("COMBATLOSS", 0, def_f, x, y);
                }
            } else {
                parse_says(0, Units[Vehs[veh_id_def].unit_id].name, -1, -1);
                StrBuffer[0] = 0;
                say_stats_3(StrBuffer, Vehs[veh_id_def].unit_id);
                parse_says(1, StrBuffer, -1, -1);
                parse_says(2, MFactions[atk_f].adj_name_faction, -1, -1);
                parse_says(3, Units[Vehs[veh_id].unit_id].name, -1, -1);
                StrBuffer[0] = 0;
                say_stats_3(StrBuffer, Vehs[veh_id].unit_id);
                parse_says(4, StrBuffer, -1, -1);
                int32_t b = base_find_3(x, y, -1, -1, -1, CurrentPlayerFaction);
                if (b >= 0) parse_says(5, Bases[b].name, -1, -1);
                NetMsg_pop(&NetMsg, "COMBATLOSS", 5000, 0, nullptr);
            }
            MAP* sq2 = map_tile(x, y);
            int32_t du = Vehs[veh_id_def].unit_id;
            int32_t au = Vehs[veh_id].unit_id;
            bool whole_stack =
                (base_id < 0 && !(sq2->items & 0x800) && chassis_triad(du) != TRIAD_AIR &&
                 chassis_triad(au) != TRIAD_AIR && Units[au].plan != PLAN_PROBE && Units[du].plan != 6 &&
                 (!stack_check(veh_id_def, 2, 8, -1, -1) || (sq2->climate & 0xE0) < 0x60)) ||
                Units[du].plan == PLAN_ARTIFACT;
            if (!whole_stack && atk_f != 0) {
                if (veh_cargo(veh_id_def) && base_id < 0 && Units[Vehs[veh_id].unit_id].plan != PLAN_PROBE) {
                    whole_stack = true;
                } else if (Units[Vehs[veh_id].unit_id].plan != PLAN_PROBE) {
                    int32_t c12 = stack_check(veh_id_def, 2, 0xC, -1, -1);
                    int32_t c1 = stack_check(veh_id_def, 1, -1, -1, -1);
                    int32_t c8 = stack_check(veh_id_def, 2, 8, -1, -1);
                    int32_t c11 = stack_check(veh_id_def, 2, 0xB, -1, -1);
                    int32_t c10 = stack_check(veh_id_def, 2, 10, -1, -1);
                    if (c1 - c12 - c8 - c11 - c10 < 2) whole_stack = true;
                }
            }
            int32_t remaining = def_id;  // the original keeps the defender id (stale after a kill)
            if (whole_stack) {
                // Collateral damage to the whole stack; land units at sea outside a transport die.
                stack_veh(veh_id_def, 0);
                if (veh_id_def >= 0) {
                    for (int32_t v = head_of_stack(veh_id_def); v >= 0; v = Vehs[v].next_veh_id_stack) {
                        int32_t vu = Vehs[v].unit_id;
                        MAP* sq3 = map_tile(x, y);
                        bool hit;
                        if (chassis_triad(vu) == TRIAD_LAND && (sq3->climate & 0xE0) < 0x60 &&
                            (!(sq3->items & BIT_BASE_IN_TILE) || (sq3->val2 & 0xF) > 7) &&
                            (Vehs[v].order != ORDER_SENTRY_BOARD || Vehs[v].waypoint_x[0] == veh_id_def ||
                             Vehs[v].waypoint_x[0] < 0)) {
                            int32_t hp = unit_max_hp(vu) - Vehs[v].damage_taken;
                            hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
                            Vehs[v].damage_taken += (uint8_t)hp;
                            hit = true;
                        } else {
                            int8_t plan = Units[vu].plan;
                            hit = v == veh_id_def || (plan != PLAN_ARTIFACT && plan != PLAN_COLONY &&
                                                      plan != PLAN_PROBE && plan != PLAN_SUPPLY);
                        }
                        if (hit) {
                            int32_t au2 = Vehs[veh_id].unit_id;
                            uint8_t power = weapon_value(au2) < 0 ? Units[Vehs[v].unit_id].reactor_id
                                                                  : Units[au2].reactor_id;
                            Vehs[v].damage_taken += (uint8_t)(power * 3);
                            collateral++;
                            Vehs[v].state |= 4;
                        }
                    }
                }
                int32_t killer = veh_id;
                if (veh_id_def >= 0) {
                    int32_t v = head_of_stack(veh_id_def);
                    while (v >= 0) {
                        int32_t next = Vehs[v].next_veh_id_stack;
                        int32_t hp = range(unit_max_hp(Vehs[v].unit_id) - Vehs[v].damage_taken, 0, 9999);
                        if ((hp < 1 || Vehs[v].faction_id == 0) &&
                            battle_kill(v, &kills, &captures, &energy, killer, atk_f)) {
                            if (v < killer) killer--;
                            if (v < next) next--;
                        }
                        v = next;
                    }
                }
                collateral -= captures + kills;
                if (collateral < 0) collateral = 0;
                remaining = stack_fix(veh_at(x, y));
                if (remaining >= 0) {
                    // Only non-combat units left (artifacts, colony pods, probes, supply): they are lost too.
                    int32_t c12 = stack_check(remaining, 2, 0xC, -1, -1);
                    int32_t c1 = stack_check(remaining, 1, -1, -1, -1);
                    int32_t c8 = stack_check(remaining, 2, 8, -1, -1);
                    int32_t c11 = stack_check(remaining, 2, 0xB, -1, -1);
                    int32_t c10 = stack_check(remaining, 2, 10, -1, -1);
                    if (c1 - c12 - c8 - c11 == c10) {
                        battle_kill_stack(remaining, &kills, &captures, &energy, veh_id, atk_f);
                        collateral = 0;
                    }
                }
            } else {
                battle_kill(veh_id_def, &kills, &captures, &energy, veh_id, atk_f);
            }
            if (base_id >= 0) {
                BASE& base = Bases[base_id];
                base.state_flags |= 8;
                // BUG(original): the weapon test reads the vehicle id of the defender or of the stack
                // left behind, which a kill may have renumbered (or -1: Vehs[-1]).
                int32_t base_now = base_id;
                if (!is_ocean(ax, ay) && (!is_objective(base_now) || base.pop_size > 1) && !has_fac(4, base_now, 0) &&
                    !has_fac(5, base_now, 0) && !has_project(5, def_f) &&
                    weapon_value(veh_unit_raw(remaining)) != 0 && atk_f != 0 && veh_who(x, y) < 0 &&
                    (Factions[def_f].diff_level != 0 || !(FactionStatus[0] & (uint8_t)(1 << (def_f & 31))))) {
                    // The attack kills a citizen of an undefended base.
                    int8_t pop = --base.pop_size;
                    bool destroyed = false;
                    if (pop > 0) {
                        base.factions_pop_size_intel[atk_f] = pop;
                    } else {
                        nerve = 0;
                        base_kill(base_now);
                        base_id = -1;
                        for (int32_t i = 0; i < 8; i++) synch_bit(x, y, i);
                        draw_map(1);
                        destroyed = true;
                    }
                    if (destroyed) {
                        base_now = -1;
                        if (eliminate_player(def_f, atk_f)) report_type = 0;
                    } else {
                        goto base_survives;
                    }
                } else {
                base_survives:
                    if (!(FactionStatus[0] & (uint8_t)(1 << (def_f & 31)))) base_reset(base_now, 0);
                    if (nerve) {
                        nerve = (Bases[base_now].pop_size + 1) / 2;
                        Bases[base_now].pop_size -= (int8_t)nerve;
                        if (is_objective(base_now) && Bases[base_now].pop_size < 1) Bases[base_now].pop_size = 1;
                        if (Bases[base_now].pop_size < 1) {
                            base_kill(base_now);
                            base_id = -1;
                            for (int32_t i = 0; i < 8; i++) synch_bit(x, y, i);
                            draw_map(1);
                            base_now = -1;
                            if (eliminate_player(def_f, atk_f)) report_type = 0;
                        } else {
                            spot_base(base_now, atk_f);
                        }
                    }
                }
                if (veh_who(x, y) < 0 && (FactionStatus[0] & (uint8_t)(1 << (def_f & 31))) && base_now >= 0)
                    invasions(base_now);
            }
            if (atk_f != 0 && def_f != 0) mon_enemy_destroyed(atk_f, def_f);
        }
        if (captures && !(FactionStatus[0] & (uint8_t)(1 << (def_f & 31))) && !has_treaty(atk_f, def_f, 0x10)) {
            cause_friction(def_f, atk_f, 5);
            treaty_on(atk_f, def_f, 0x2000);
        }
    }
    // Mind worm husks: energy for the winner, and nearby natives grow bolder.
    if (atk_left != 0 && energy != 0) {
        Factions[atk_f].energy_credits += energy;
        Console_update_data(&MainConsole, 0);
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(NearOffsetX[i] + x);
            int32_t y2 = NearOffsetY[i] + y;
            if (!on_map(x2, y2)) continue;
            for (int32_t v = veh_at(x2, y2); v >= 0; v = Vehs[v].next_veh_id_stack) {
                if (Vehs[v].faction_id != 0) continue;
                uint16_t flags = Vehs[v].flags;
                uint32_t level2 = (flags >> 3) & 3;
                flags &= (uint16_t)~(level2 * 8);
                Vehs[v].flags = flags;
                Vehs[v].flags |= (uint16_t)(range(level2 + 1, 0, 3) << 3);
                draw_tile(x2, y2, 2);
            }
        }
    }
    if (display) {
        if (atk_left == 0) {
            draw_tile_fixup(ax, ay, 2, 1);
            draw_tile(x, y, 1);
        } else {
            draw_tile(ax, ay, 1);
            draw_tile_fixup(x, y, 2, 1);
        }
        do_all_draws();
        if (atk_left == 0 || energy == 0 || atk_f != (uint32_t)CurrentPlayerFaction) {
            if ((kills > 1 || collateral != 0) &&
                (atk_f == (uint32_t)CurrentPlayerFaction || def_f == (uint32_t)CurrentPlayerFaction)) {
                parse_num(0, kills);
                parse_num(1, collateral);
                clear();
                says((atk_f == (uint32_t)CurrentPlayerFaction) == (atk_left != 0) ? "NUMGOT" : "NUMLOST");
                if (kills < 2) {
                    if (collateral) says("1");
                } else if (collateral) {
                    says("2");
                }
                bat_msg(StrBuffer, nullptr);
            }
        } else {
            parse_num(0, energy);
            bat_msg("HUSKS", nullptr);
        }
        if (captures) {
            parse_num(0, captures);
            if (def_f == (uint32_t)CurrentPlayerFaction) {
                clear();
                says("THEIRBOOTY");
                if (!has_treaty(atk_f, def_f, 0x10)) {
                    parse_says(0, bat_noun(atk_f), -1, -1);
                    bat_say_num(3);
                } else {
                    bat_say_num(range(captures, 1, 2));
                }
                bat_msg(StrBuffer, nullptr);
            } else if (atk_f == (uint32_t)CurrentPlayerFaction) {
                clear();
                says("OURBOOTY");
                bat_say_num(range(captures, 1, 2));
                bat_msg(StrBuffer, nullptr);
            }
        }
        if (nerve && crt_strlen(nerve_base)) {
            parse_says(0, nerve_base, -1, -1);
            bat_msg("NERVEGAS", nullptr);
        }
        if (disengaged) {
            parse_says(0, Units[Vehs[def_id].unit_id].name, -1, -1);
            parse_says(1, MFactions[def_f].adj_name_faction, -1, -1);
            if (def_f == (uint32_t)CurrentPlayerFaction)
                bat_msg("WEDISENGAGED", nullptr);
            else if (atk_f == (uint32_t)CurrentPlayerFaction)
                bat_msg("THEYDISENGAGED", nullptr);
        }
        StatusWin_redraw(&InfoWin);
        if (husk_refresh) {
            BaseWin_focus(&BaseWin);
            GraphicWin_redraw(&MainInterfaceWin);
            do_all_draws();
        }
        if (GameMorePreferences & 0x10) {
            BattleWin_stop_timer(&BattleWin);
            popp(ScriptFileName, "BATTLEPAUSE", 0, "batwon_sm.pcx", 0);
            BattleWin_pulse_timer(&BattleWin);
            if (DialogToggle) {
                GameMorePreferences &= ~0x10;
                prefs_save(0);
            }
        }
    }
    if (report_type == 1) {
        parse_says(0, MFactions[atk_f].adj_name_faction, -1, -1);
        parse_says(1, bat_noun(def_f), -1, -1);
        bat_msg_2("VENDETTAREPORT", nullptr);
    } else if (report_type == 2) {
        parse_says(0, MFactions[atk_f].adj_name_faction, -1, -1);
        parse_says(1, bat_noun(def_f), -1, -1);
        bat_msg_2("PACTATTACKED", nullptr);
    } else if (report_type == 3) {
        parse_says(0, bat_noun(atk_f), -1, -1);
        parse_says(1, bat_noun(def_f), -1, -1);
        bat_msg_2("PACTATTACKING", nullptr);
    }
    Units[atk_unit_saved].combat_factions |= (uint8_t)(1 << (def_f & 31));
    Units[def_unit_saved].combat_factions |= (uint8_t)(1 << (atk_f & 31));
    if (show_interlude) interlude(0, 0, 1, 0);
    if (atk_left == 0) return 0;
    // The winner: air units (except missiles' range 1 chassis) and ships end their move.
    int32_t v = CurrentVehID;
    int32_t vu = Vehs[v].unit_id;
    if (chassis_triad(vu) == TRIAD_AIR && Chassis[Units[vu].chassis_id].range != 1) {
        veh_skip(v);
        Vehs[v].moves_spent += (uint8_t)Rules.move_rate_roads;
    } else if (chassis_triad(vu) == TRIAD_SEA) {
        veh_skip(v);
        Vehs[v].moves_spent += (uint8_t)Rules.move_rate_roads;
    }
    int32_t missile = Chassis[Units[Vehs[v].unit_id].chassis_id].missile;
    if (missile) kill(v);
    // BUG(original): after kill(v), Vehs[v] is another vehicle (or past the end of the table).
    if (!(FactionStatus[0] & (uint8_t)atk_bit) && base_id >= 0 && veh_who(x, y) < 0 &&
        chassis_triad(Vehs[v].unit_id) == TRIAD_AIR && Chassis[Units[Vehs[v].unit_id].chassis_id].range != 0 &&
        Vehs[v].movement_turns + 1 >= (int32_t)Chassis[Units[Vehs[v].unit_id].chassis_id].range)
        return 1;
    return missile == 0;
}

#ifdef SMACX_HYBRID
// morale_alien runs inside the original morale_veh/speed (whose dead stack an uninitialised read in
// the AI depends on): keep the original's stack image (see hybrid_shadow.h).
HYBRID_SHADOW_THUNK(int32_t, morale_alien, (int32_t veh_id, int32_t faction_id_vs_native), 2, 0x501356,
                    "push %ebp\n mov %esp, %ebp\n sub $8, %esp")
#endif
