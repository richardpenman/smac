// order_veh (0x5947C0): one step of a unit in a direction, with everything that can happen on the
// way (move module; kept in its own file for its size). Structure follows the original's control
// flow closely; labels named L_<address> are the original's join points.
#include "engine/move.h"

#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/goody.h"
#include "engine/log.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/diplo_act.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/zoc.h"

namespace {
inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
inline uint32_t dstat(int32_t faction_id, int32_t other) { return Factions[faction_id].diplo_status[other]; }
inline const UNIT& veh_unit(int32_t veh_id) { return Units[Vehs[veh_id].unit_id]; }
inline int32_t veh_chassis_triad(int32_t veh_id) { return Chassis[veh_unit(veh_id).chassis_id].triad; }
inline int32_t veh_chassis_range(int32_t veh_id) { return Chassis[veh_unit(veh_id).chassis_id].range; }
inline int32_t veh_offense(int32_t veh_id) { return Weapon[veh_unit(veh_id).weapon_id].offense_value; }
// Movement points left (0-999).
int32_t moves_left(int32_t veh_id) {
    int32_t m = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
    return m < 0 ? 0 : m > 999 ? 999 : m;
}
// Owner of a base on the tile (0-7), or -1.
inline int32_t tile_base_owner(const MAP* sq) {
    if (!(sq->items & 1)) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner < 8 ? owner : -1;
}
// Top of a vehicle stack (inline in the original).
inline int32_t top_of(int32_t veh_id) {
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
inline const char* faction_adj(int32_t faction_id) { return MFactions[faction_id].adj_name_faction; }
// Leader title of a faction as parse slot n (sets the gender/plural defaults like the original).
void say_title(int32_t n, int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = MFactions[faction_id].is_leader_female;
    parse_says(n, MFactions[faction_id].title_leader, -1, -1);
}
const char* ability_name(uint32_t flag) { return Ability[abil_index(flag)].name; }
void net_synch_veh(int32_t veh_id) {
    synch_veh(veh_id);
    NetDaemon_await_synch(&NetState);
}
}  // namespace
#ifdef OV_DEBUG
#include <cstdio>
#define OV_TRACE(a) do { if (FILE* f_ = std::fopen("ov_trace.txt", "a")) { std::fprintf(f_, "%X ", a); std::fclose(f_); } } while (0)
#else
#define OV_TRACE(a) do { } while (0)
#endif

int32_t GAMEAPI order_veh(int32_t veh_id, int32_t dir, int32_t flags) {
    int32_t result = 0;       // [ebp-0x58]: the vehicle moved (or acted) and may continue
    int32_t unload = 0;       // [ebp-0x68]: land unit boarding a transport at sea (no base)
    int32_t fought = 0;       // [ebp-0x74]: won a battle and moves on
    int32_t attacked = 0;     // [ebp-0x6c]: a battle was fought this step
    int32_t probed = 0;       // [ebp-0x60]: probe action / units spotted
    // [ebp-0x24]: the move is shown to the viewer. The original reuses the slot (target stack
    // offset); paths that do not set it see the earlier value (only display timing depends on it).
    int32_t visible = 0;
    int32_t delayed = 0;      // [ebp-0x30]: the unit is exploring or on alert (checks part)
    int32_t& scroll = delayed;  // same slot in the movement part: the map follows the unit
    int32_t triad;            // [ebp-0x4c]
    VehAttackFlags = flags;
#ifdef OV_DEBUG
    if (FILE* f_ = std::fopen("ov_trace.txt", "a")) { std::fprintf(f_, "\nCALL %d %d %d: ", veh_id, dir, flags); std::fclose(f_); }
#endif
    if (flags & 1)
        MapWinField23BE4 = 0;
    else
        MapWinField23BF0 = 0;
    int32_t veh_count = VehCount;
    int32_t x = Vehs[veh_id].x, y = Vehs[veh_id].y;
    int32_t faction_id = Vehs[veh_id].faction_id;
    if (dir < 0) dir = 8;
    int32_t tx = map_wrap_x(x + NearOffsetX[dir]);
    int32_t ty = y + NearOffsetY[dir];
    if (!map_in_bounds(tx, ty)) goto L_599b1a;
    {
    int32_t tgt_region = map_tile(tx, ty)->region;
    int32_t stack_id = stack_fix(veh_at(tx, ty));
    int32_t base_id = base_at(tx, ty);
    int32_t tgt_faction = base_id >= 0 ? (uint8_t)Bases[base_id].faction_id
                                       : stack_id >= 0 ? (int32_t)Vehs[stack_id].faction_id : -1;
    int32_t veh_sea = map_is_ocean(x, y);
    int32_t tgt_sea = map_is_ocean(tx, ty);
    int32_t goody = goody_at(tx, ty);
    uint8_t fbit = (uint8_t)(1u << (faction_id & 31));
    int32_t tmp, tmp2;

L_5949b3:  // (re)start, also with a disembarking passenger as veh_id
    OV_TRACE(0x5949b3);
    delayed = (Vehs[veh_id].state & 0x4200) ? 1 : 0;
    triad = veh_chassis_triad(veh_id);
    if (!(FactionStatus[0] & fbit) && Vehs[veh_id].order == 0x18) {
        // AI units do not walk into the territory of factions they are at peace with.
        int32_t owner = whose_territory(faction_id, Vehs[veh_id].waypoint_x[0], Vehs[veh_id].waypoint_y[0], nullptr, 0);
        if (owner >= 0 && owner != faction_id && !(dstat(faction_id, owner) & 0xC11)) {
            Vehs[veh_id].order = 0;
            goto L_599b1a;
        }
    }
    if (veh_unit(veh_id).plan == 12 && !(Vehs[veh_id].flags & 0x20) && (VehAttackFlags & 1)) {
        // Alien artifact entering a base: link it to the network node or a secret project.
        Vehs[veh_id].movement_turns = 0;
        if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id) {
            BASE& base = Bases[base_id];
            parse_says(0, base.name, -1, -1);
            parse_num(0, base.minerals_accumulated);
            int32_t item = base.queue_items[0];
            int32_t cost = item < 0 ? cost_factor(faction_id, 1, -1) * Facility[-item].cost
                                    : veh_cost(item, base_id, nullptr) * cost_factor(faction_id, 1, -1);
            parse_num(1, cost);
            if (!X_text_open(ScriptFileName, "ARTIFACTMENU")) {
                StrBuffer[0] = 0;
                parse_string(text_get(), StrBuffer);
                int32_t items = 1;
                char* text = text_get();
                int32_t offset, mask;
                bitmask(8, &offset, &mask);  // network node
                if ((base.facilities_built[offset] & (uint8_t)mask) && SecretProjects[20] != base_id &&
                    !(base.state_flags & 0x800)) {
                    StrBuffer[0] = 0;
                    parse_string(text, StrBuffer);
                    items = 2;
                    if (!(FactionStatus[0] & fbit)) Vehs[veh_id].movement_turns = 1;
                }
                text = text_get();
                if (SecretProjects[20] == base_id) {
                    StrBuffer[0] = 0;
                    parse_string(text, StrBuffer);
                    items++;
                    if (!(FactionStatus[0] & fbit)) Vehs[veh_id].movement_turns = 1;
                }
                text = text_get();
                item = base.queue_items[0];
                if (item <= -70) {
                    StrBuffer[0] = 0;
                    parse_say(1, Facility[item < 0 ? -item : item].name, -1, -1);
                    parse_string(text, StrBuffer);
                    items++;
                    if (!(FactionStatus[0] & fbit)) Vehs[veh_id].movement_turns = 2;
                }
                text = text_get();
                item = base.queue_items[0];
                if (item >= 0 && item >= 64 && !(Units[item].unit_flags & 4)) {
                    StrBuffer[0] = 0;
                    parse_says(2, Units[item].name, -1, -1);
                    parse_string(text, StrBuffer);
                    items++;
                }
                if (items > 1 && faction_id == CurrentPlayerFaction && (FactionStatus[0] & fbit)) {
                    Vehs[veh_id].movement_turns = (uint8_t)ui_artifact_menu(veh_id, base_id, items);
                    net_synch_veh(veh_id);
                }
            }
        }
    }
    if (dir >= 8) goto L_5962f2;
    Vehs[veh_id].state |= 4;
    if (veh_unit(veh_id).plan == 11 && tgt_faction >= 0 && faction_id != tgt_faction) {
        // Probe team: probe actions against other factions.
        if (VehAttackFlags & 1) {
            Vehs[veh_id].flags &= ~4;
            if ((dstat(faction_id, tgt_faction) & 1) && !delayed && faction_id == CurrentPlayerFaction) {
                if (base_id >= 0) {
                    parse_says(0, Bases[base_id].name, -1, -1);
                    parse_says(1, faction_adj(tgt_faction), -1, -1);
                    if (!X_pop("PROBEPACT", 0)) goto L_595083;
                } else {
                    if (!stack_check(stack_id, 2, 11, tgt_faction, -1)) goto L_594f3c;
                    parse_says(0, faction_adj(tgt_faction), -1, -1);
                    if (!X_pop("PROBEPACTTEAM", 0)) goto L_595083;
                }
                Vehs[veh_id].flags |= 4;  // probe operations against a pact partner
            }
        }
    L_594f3c:
    OV_TRACE(0x594f3c);
        if (base_id >= 0) {
            if (stack_id >= 0 && stack_check(stack_id, 2, 11, -1, -1)) goto L_595083;
            if (FactionStatus[0] & fbit) goto L_5965a3;
            if (!(dstat(faction_id, tgt_faction) & 1)) goto L_5965a3;
            if (Vehs[veh_id].waypoint_x[0] == Bases[base_id].x && Vehs[veh_id].waypoint_y[0] == Bases[base_id].y)
                goto L_5965a3;
            goto L_595083;
        }
        if (dstat(faction_id, tgt_faction) & 1) goto L_595083;
        if (stack_id >= 0 && stack_check(stack_id, 2, 11, -1, -1)) goto L_595083;
        probed = 1;
        if (probe(veh_id, -1, stack_id, 1) && MultiplayerActive && !ControlTurnC && (VehAttackFlags & 1) &&
            !(VehAttackFlags & 2))
            goto L_5966e0;
        if (VehCount != veh_count) goto L_return0;
        if (stack_id >= 0) tgt_faction = Vehs[stack_id].faction_id;
    }
L_595083:
    OV_TRACE(0x595083);
    if (triad == 0 && tgt_sea) {
        // Land unit onto a sea tile.
        if (faction_id == tgt_faction) {
            if (stack_check(stack_id, 8, faction_id, -1, -1) <= 0) {
                if (base_id < 0) goto L_596884;
                if (!has_abil(Vehs[veh_id].unit_id, 8) && !stack_check(stack_id, 2, 7, -1, -1) &&
                    !stack_check(veh_id, 2, 7, -1, -1)) {
                    // L_5967cf: AMPHIBBASE2 (re-tests the same conditions)
                    if (base_id < 0 || has_abil(Vehs[veh_id].unit_id, 8) || stack_check(stack_id, 2, 7, -1, -1) ||
                        stack_check(veh_id, 2, 7, -1, -1))
                        goto L_596884;
                    if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
                    parse_say(0, ability_name(8), -1, -1);
                    NetMsg_pop(&NetMsg, "AMPHIBBASE2", 5000, 0, nullptr);
                    goto L_599b1a;
                }
            }
            if (base_id < 0) unload = 1;
        } else {
            if (dstat(faction_id, tgt_faction) & 1) goto L_599b1a;
            if (base_id >= 0) {
                if (!has_abil(Vehs[veh_id].unit_id, 8)) {
                    if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
                    parse_say(0, ability_name(8), -1, -1);
                    NetMsg_pop(&NetMsg, "AMPHIBBASE", 5000, 0, nullptr);
                    goto L_599b1a;
                }
            } else {
                // Air superiority units attack aircraft over the sea.
                if (stack_id < 0 || !has_abil(Vehs[veh_id].unit_id, 0x20) ||
                    !stack_check(stack_id, 0x12, -1, -1, -1) || stack_check(stack_id, 6, 0x80, -1, -1))
                    goto L_599b1a;
                goto L_59512c;
            }
        }
    }
    // L_59533d: zones of control.
    if (!veh_sea && !tgt_sea && triad == 0 && !(veh_unit(veh_id).ability_flags & 4) && veh_unit(veh_id).plan != 11 &&
        stack_id < 0 && zoc_move(x, y, faction_id) && zoc_move(tx, ty, faction_id))
        goto L_5968f2;
    if (veh_unit(veh_id).plan == 10 && base_id >= 0 && !MultiplayerActive) {
        int32_t r = supply_options(veh_id, base_id);
        if (VehAttackFlags & 2) {
            if (r) goto L_599b1a;
        } else {
            if (!r) goto L_599b1a;
            if (MultiplayerActive && !ControlTurnC) goto L_596b1f;
        }
    }
    // Native life may appear in fungus.
    if (!(VehAttackFlags & 2)) goto L_59512c;
    if (veh_chassis_triad(veh_id) == 2) goto L_59512c;
    if (stack_id >= 0) goto L_595137;
    if (faction_id == 0) goto L_59512c;
    {
        const MAP* sq = map_tile(tx, ty);
        if (!(sq->items & 0x20) || (sq->climate & 0xE0) < 0x40 || (sq->items & 8)) goto L_59512c;
        if ((veh_offense(veh_id) == 0 || Vehs[veh_id].order == 0x18) && SecretProjects[8] >= 0 &&
            (uint8_t)Bases[SecretProjects[8]].faction_id == (uint32_t)faction_id)
            goto L_59512c;
        if (veh_sea != tgt_sea) goto L_59512c;
        if (goody_at(tx, ty)) goto L_59512c;
        if (GameRules & 0x200000) goto L_59512c;
        base_find(tx, ty);
        if (BaseFindDist <= 1) goto L_59512c;
        int32_t chance = BaseFindDist < 0 ? 0 : BaseFindDist > 32 ? 32 : BaseFindDist;
        chance += MapNativeLifeForms * 10;
        uint32_t items = map_tile(tx, ty)->items;
        if (items & 0x100000) {
            chance /= 2;
            if (!MapNativeLifeForms) chance /= 2;
        }
        if (tgt_sea) {
            chance /= 2;
            if (!MapNativeLifeForms) chance /= 2;
        }
        if (items & 4) chance /= 2;
        int32_t unit_id = Vehs[veh_id].unit_id;
        if (unit_id < 64 && (Weapon[Units[unit_id].weapon_id].offense_value < 0 || unit_id == 15)) chance /= 2;
        for (int32_t i = 0; i < 25; i++) {
            int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
            int32_t y2 = ty + TableOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            if (is_sensor(x2, y2) || tile_base_owner(map_tile(x2, y2)) >= 0) {
                if (i < 9)
                    chance /= 4;
                else
                    chance /= 2;
            }
        }
        if (game_rand() % 100 >= chance) goto L_59512c;
        int32_t spawn;
        if (tgt_sea)
            spawn = 9;
        else if (game_rand() % 5 == 0 && ExpansionEnabled)
            spawn = 15;
        else
            spawn = 8;
        stack_id = veh_init(spawn, 0, tx, ty);
        if (stack_id >= 0) goto L_596bd9;
        goto L_596052;
    }

L_59512c:  // a unit on the target tile?
    OV_TRACE(0x59512c);
    if (stack_id < 0) goto L_596052;
L_595137: {
    OV_TRACE(0x595137);
        visible = stack_id * 0x34;
        const VEH& tgt = Vehs[stack_id];
        if (!(tgt.flags & 0x400)) {
            if (faction_id == tgt_faction) goto L_596052;
            if (dstat(faction_id, tgt_faction) & 1) {
                if (veh_unit(veh_id).plan != 11 || !(Vehs[veh_id].flags & 4)) goto L_596052;
            }
        }
        if (probed) goto L_599b1a;
        if (tgt.flags & 0x400) goto L_596c9f;  // unseen units
        if (!((tgt.faction_id == (uint32_t)faction_id && (tgt.flags & 0x440) != 0x440) || (fbit & tgt.visibility)) &&
            (FactionStatus[0] & fbit)) {
            if (base_id >= 0) goto L_595734;
            if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction) goto L_596d5d;
        }
        if (base_id >= 0) goto L_595734;
        if (map_tile(tx, ty)->items & 0x40000) goto L_595734;
        if (stack_check(stack_id, 0x12, -1, -1, -1) && !has_abil(Vehs[veh_id].unit_id, 0x20) &&
            !stack_check(stack_id, 6, 0x80, -1, -1))
            goto L_596e0b;  // only fighters attack air units
    }
L_595734:
    OV_TRACE(0x595734);
    if (Vehs[veh_id].unit_id != 14) {  // Sealurk
        if (!veh_sea) goto L_5957d7;
        if (!tgt_sea && triad == 1 && !can_arty(Vehs[veh_id].unit_id, 1) && veh_offense(veh_id) != 0) goto L_596e85;
    }
    if (veh_sea && triad == 0 && !has_abil(Vehs[veh_id].unit_id, 8) && veh_unit(veh_id).plan != 11) goto L_596ec0;
L_5957d7:
    OV_TRACE(0x5957d7);
    if (Weapon[veh_unit(veh_id).weapon_id].mode == 11 && stack_check(stack_id, 2, 11, -1, -1)) goto L_595a8f;
    if (veh_offense(veh_id) == 0) goto L_596f75;
    if (veh_unit(stack_id).plan == 11 && stack_check(stack_id, 1, -1, -1, -1) == 1 && !MultiplayerActive) {
        // An enemy probe team alone: capture it.
        uint32_t d = dstat(faction_id, tgt_faction);
        if (!(d & 6) || (d & 1)) goto L_595a92;
        int32_t near = base_find(tx, ty);
        if (near >= 0 && (uint8_t)Bases[near].faction_id == (uint32_t)faction_id) {
            parse_says(0, faction_adj(tgt_faction), -1, -1);
            parse_says(1, veh_unit(stack_id).name, -1, -1);
            say_title(2, faction_id);
            if (!(FactionStatus[0] & fbit)) goto L_596fbc;
            int32_t choice = (MFactions[tgt_faction].rule_flags & 0x80)
                                 ? popp(ScriptFileName, "ENEMYPROBE", 0, "al_cap_sm.pcx", 0)
                                 : popp(ScriptFileName, "ENEMYPROBE", 0, "capture_sm.pcx", 0);
            if (!choice) goto L_599b1a;
            if (choice == 1) goto L_596fbc;
            goto L_595a92;
        }
        if (faction_id != CurrentPlayerFaction) goto L_595a92;
        parse_says(0, faction_adj(tgt_faction), -1, -1);
        parse_says(1, veh_unit(stack_id).name, -1, -1);
        say_title(2, faction_id);
        {
            int32_t choice = (MFactions[tgt_faction].rule_flags & 0x80)
                                 ? popp(ScriptFileName, "ENEMYPROBE2", 0, "al_cap_sm.pcx", 0)
                                 : popp(ScriptFileName, "ENEMYPROBE2", 0, "capture_sm.pcx", 0);
            if (!choice) goto L_599b1a;
        }
    }
L_595a8f:
    OV_TRACE(0x595a8f);
L_595a92:
    OV_TRACE(0x595a92);
    // Attacking with less than a full move.
    if (moves_left(veh_id) < Rules.move_rate_roads && Vehs[veh_id].unit_id != 15 && Vehs[veh_id].unit_id != 8) {
        if (faction_id == CurrentPlayerFaction && !delayed && (VehAttackFlags & 1)) {
            parse_says(0, veh_unit(veh_id).name, -1, -1);
            parse_num(0, moves_left(veh_id));
            parse_num(1, Rules.move_rate_roads);
            if (popp(ScriptFileName, "HASTY", 0, "bad_sm.pcx", 0)) goto L_599b1a;
        } else if (!(FactionStatus[0] & fbit)) {
            if (moves_left(veh_id) <= Rules.move_rate_roads / 2) goto L_599b1a;
        }
    }
    CurrentVehID = veh_id;
    if (!(VehAttackFlags & 2)) goto L_59729d;
    // Attacks on native life may capture it instead (PLANET).
    if (tgt_faction != 0) goto L_59724d;
    if (veh_chassis_triad(veh_id) == 2) goto L_59724d;
    if (Vehs[stack_id].unit_id == 7 || Vehs[stack_id].unit_id == 19) goto L_59724d;
    {
        Faction& f = Factions[faction_id];
        if (f.SE_planet <= 0) goto L_59724d;
        if (moves_left(veh_id) < Rules.move_rate_roads) {
            if (f.SE_planet_base <= 0) goto L_59724d;
            if (f.units_active[Vehs[stack_id].unit_id]) goto L_59724d;
        }
        int32_t near = base_find_2(tx, ty, faction_id);
        if (!(VehAttackFlags & 2)) goto L_59724d;
        if (Vehs[stack_id].order_auto_type == (uint32_t)faction_id) goto L_597216;
        if (near >= 0 && Bases[near].eco_damage != 0) goto L_597216;
        log_say_2("Precapture who, enemy", faction_id, stack_id, 0);
        log_say_2("Precapture fac, proto", f.SE_planet_base, f.units_active[Vehs[stack_id].unit_id], 0);
        log_say_2("Precapture type, soc2", Vehs[stack_id].unit_id, f.SE_planet, 0);
        if (Vehs[stack_id].flags & 0x2000) goto L_5971ce;
        if (game_rand() % 4 < f.SE_planet) {
            int32_t n = CurrentTurn / 5 + 100;
            int32_t roll = n - 1 <= 0 ? 0 : game_rand() % n;
            if (roll >= f.units_active[Vehs[stack_id].unit_id] * 10) goto L_595e1b;
            if (game_rand() % 10 == 0) goto L_595e1b;
        }
        if (f.SE_planet_base <= 0) goto L_5971ce;
        if (f.units_active[Vehs[stack_id].unit_id]) goto L_5971ce;
    L_595e1b:
    OV_TRACE(0x595e1b);
        if (stack_id >= 0) {
            for (int32_t v = top_of(stack_id); v >= 0; v = Vehs[v].next_veh_id_stack) {
                Vehs[v].faction_id = (uint8_t)faction_id;
                Vehs[v].morale = (uint8_t)morale_alien(v, 0);
                int32_t b = base_find(tx, ty);
                const BASE& base = (&Bases[0])[b];
                if ((uint8_t)base.faction_id == (uint32_t)faction_id && base.pop_size >= 3)
                    Vehs[v].home_base_id = (int16_t)b;
                else
                    Vehs[v].home_base_id = -1;
                veh_skip(v);
                if (v < 0) break;
            }
        }
        bit_set(tx, ty, 0x100000, 1);
        owner_set(tx, ty, faction_id);
        if (faction_id == CurrentPlayerFaction)
            draw_tile_fixup(tx, ty, 1, 2);
        else
            draw_tile(tx, ty, 2);
        if (faction_id == CurrentPlayerFaction) {
            int32_t unit_id = Vehs[stack_id].unit_id;
            parse_says(0, Units[unit_id].name, -1, -1);
            if (unit_id == 15) {
                if (!f.units_active[15])  // first capture of the kind: full popup
                    popp(ScriptFileName, "WORMCAPTURE", 0, "sporlnch_sm.pcx", 0);
                else
                    NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
            } else if (unit_id == 14) {
                if (!f.units_active[14])
                    popp(ScriptFileName, "WORMCAPTURE", 0, "sealrk_sm.pcx", 0);
                else
                    NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
            } else if (!f.units_active[unit_id]) {
                popp(ScriptFileName, "WORMCAPTURE", 0, map_is_ocean(tx, ty) ? "isle_sm.pcx" : "mindworm_sm.pcx", 0);
            } else {
                NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
            }
        }
        log_say_2("Captured", 0, 0, 0);
        goto L_596052;
    }
L_5971ce:  // no capture: the natives fight
    OV_TRACE(0x5971ce);
    Vehs[stack_id].flags |= 0x2000;
    if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "WORMNOCAPTURE", 5000, 0, nullptr);
    log_say_2("No Capture", 0, 0, 0);
    goto L_59724d;
L_597216:
    OV_TRACE(0x597216);
    log_say_2("Agitated", 0, 0, 0);
    if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "WORMSAGITATED", 5000, 0, nullptr);
L_59724d:  // attack
    OV_TRACE(0x59724d);
    attacked = 1;
    tmp = battle_fight(veh_id, dir, 0, 1, nullptr);
    if (tmp == 0) goto L_599b1a;
    if (tmp != 1) {
        result = 0;
        goto L_599b1a;
    }
    // The attacker won and stays: post-move processing on its own tile.
    veh_id = CurrentVehID;
    tx = x;
    ty = y;
    fought = 1;
    goody = 0;
    goto L_598cb9;
L_59729d:  // attack, not moving on (VehAttackFlags bit 1 clear)
    OV_TRACE(0x59729d);
    result = battle_fight(veh_id, dir, 0, 1, nullptr) == 0;
    goto L_599b1a;

L_596052:  // movement checks
    OV_TRACE(0x596052);
    if (veh_sea) {
        if (tgt_sea) goto L_597345;
        if (triad == 0 && stack_id < 0 && !has_abil(Vehs[veh_id].unit_id, 8) &&
            tile_base_owner(map_tile(x, y)) >= 0 && !stack_check(veh_id, 2, 7, -1, -1))
            goto L_5972bd;
    } else if (tgt_sea) {
        goto L_597345;
    }
    if (base_id >= 0) goto L_597350;
    if (triad != 1) goto L_597345;
    if (MultiplayerActive) goto L_599b1a;
    if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
    {
        // A ship moving to land: a passenger disembarks.
        Vehs[veh_id].order = 0;
        parse_says(0, veh_unit(veh_id).name, -1, -1);
        if (X_text_open(ScriptFileName, "DISEMBARKERS")) goto L_599b1a;
        text_get();
        stack_veh(veh_id, 0);
        int32_t chosen = -1, count = 0;
        if (veh_id < 0) goto L_599b1a;
        for (int32_t v = top_of(veh_id); v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (Vehs[v].order == 1 && Vehs[v].waypoint_x[0] == veh_id) {
                count++;
                if (moves_left(v) > 0) {
                    chosen = v;
                    StrBuffer[0] = 0;
                    parse_says(1, veh_unit(v).name, -1, -1);
                    parse_string(TextBufferGetPtr, StrBuffer);
                }
            }
        }
        if (chosen < 0) {
            if (count) NetMsg_pop(&NetMsg, "DISEMBARKFAIL", 5000, 0, nullptr);
            goto L_599b1a;
        }
        int32_t choice = ui_disembark_menu(veh_id) - 1;
        if (choice < 0) goto L_599b1a;
        veh_id = choice;
        Vehs[choice].order = 0;
        goto L_5949b3;
    }
L_5972bd:
    OV_TRACE(0x5972bd);
    if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
    parse_say(0, ability_name(8), -1, -1);
    NetMsg_pop(&NetMsg, "AMPHIBBASE2", 5000, 0, nullptr);
    goto L_599b1a;

L_597345:  // entering a base?
    OV_TRACE(0x597345);
    if (base_id < 0) goto L_597564;
L_597350:
    OV_TRACE(0x597350);
    if (tgt_faction == faction_id || (dstat(faction_id, tgt_faction) & 1)) goto L_597564;
    // An enemy base: only some units may take it.
    if (triad == 2) {
        if (veh_chassis_range(veh_id)) {
            if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
            parse_say(0, Chassis[veh_unit(veh_id).chassis_id].offsv1_name, -1, -1);
            NetMsg_pop(&NetMsg, "AIRCONQUER", 5000, 0, nullptr);
            goto L_599b1a;
        }
    } else if (triad == 1) {
        if (!tgt_sea) {
            if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
            NetMsg_pop(&NetMsg, "SEACONQUER", 5000, 0, nullptr);
            goto L_599b1a;
        }
    } else if (triad == 0 && tgt_sea && !has_abil(Vehs[veh_id].unit_id, 8)) {
        if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
        NetMsg_pop(&NetMsg, "LANDCONQUER", 5000, 0, nullptr);
        goto L_599b1a;
    }
    if (veh_offense(veh_id) == 0) {
        if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
        NetMsg_pop(&NetMsg, "COMBATCONQUER", 5000, 0, nullptr);
        goto L_599b1a;
    }
    if (dstat(faction_id, tgt_faction) & 7) {
        // At peace with the owner: AIs stop, the human player is asked to break the treaty.
        if (!(FactionStatus[0] & fbit)) {
            Vehs[veh_id].order = 0;
            goto L_599b1a;
        }
        if (faction_id != CurrentPlayerFaction) goto L_59763b;
        if (VehAttackFlags & 1) {
            if (break_treaty(faction_id, tgt_faction, 7)) goto L_599b1a;
        }
    }
L_597564:
    OV_TRACE(0x597564);
    if (faction_id == CurrentPlayerFaction && (FactionStatus[0] & fbit) && (VehAttackFlags & 1) &&
        Vehs[veh_id].order == 0x18 && !(Vehs[veh_id].waypoint_x[0] == tx && Vehs[veh_id].waypoint_y[0] == ty) &&
        veh_chassis_triad(veh_id) != 2 && !(Vehs[veh_id].state & 0x4200) && goody_at(tx, ty) &&
        popp(ScriptFileName, "GOTOGOODY", 0, "supply_sm.pcx", 0)) {
        Vehs[veh_id].order = 0;
        goto L_599b1a;
    }
L_59763b: {  // the move itself
    OV_TRACE(0x59763b);
        if (moves_left(veh_id) == 0) goto L_599b1a;
        int32_t cost = hex_cost(Vehs[veh_id].unit_id, faction_id, x, y, tx, ty, 0);
        uint8_t spent = Vehs[veh_id].moves_spent;
        if (cost <= moves_left(veh_id) || triad != 0) goto L_597b24_move;
        // Not enough movement points: a land unit may still make it (random).
        if (moves_left(veh_id) >= Rules.move_rate_roads) {
            const MAP* sq = map_tile(tx, ty);
            if (!(sq->items & 0x20) || (sq->climate & 0xE0) < 0x40) goto L_597b24_move;
            int32_t unit_id = Vehs[veh_id].unit_id;
            if (unit_id < 64 && Weapon[Units[unit_id].weapon_id].offense_value < 0) goto L_597b24_move;
            if (Units[unit_id].plan == 9 || Units[unit_id].plan == 12) goto L_597b24_move;
            if (stack_id >= 0) goto L_597b24_move;
        }
        if (!(VehAttackFlags & 1) && (FactionStatus[0] & fbit)) goto L_597b24_move;
        {
            int32_t roll;
            if (ControlTurnC)
                roll = cost - 1 <= 0 ? 0 : game_rand() % cost;
            else
                roll = game_random(0, cost);
            int32_t limit = moves_left(veh_id) + ((Vehs[veh_id].state & 0x8000) ? Rules.move_rate_roads : 0);
            if (roll < limit) goto L_597b24_move;
        }
        if (faction_id == CurrentPlayerFaction) {
            if (Vehs[veh_id].order == 0 && !ui_shift_pressed() && (VehAttackFlags & 1) && !(Vehs[veh_id].state & 0x200) &&
                !(GameMorePreferences & 0x80)) {
                // Show the failed attempt.
                Console_focus(&MainConsole, x, y, CurrentPlayerFaction);
                stack_veh(veh_id, 1);
                veh_scoot(veh_id, x, y, dir + 8, 0);
                stack_put(veh_id, x, y);
                for (int32_t i = 0; i < 8; i++) {
                    int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
                    int32_t y2 = ty + NearOffsetY[i];
                    if (map_in_bounds(x2, y2)) spot_loc(x2, y2, faction_id);
                }
                draw_tiles(tx, ty, 2);
            }
            if (faction_id == CurrentPlayerFaction) {
                const MAP* sq = map_tile(tx, ty);
                bool fungus = (sq->items & 0x20) && (sq->climate & 0xE0) >= 0x40;
                if (fungus && Vehs[veh_id].order == 0 && veh_unit(veh_id).plan != 9 && veh_unit(veh_id).plan != 12 &&
                    (VehAttackFlags & 1)) {
                    if (tut_check(0x1000)) {
                        TutSlowFungusCount++;
                        FX_play(&Sounds, 9);
                        TutWin_tut_map(&TutWin, "SLOWFUNGUS", tx, ty, -1, nullptr, 0, -1, -1);
                        Vehs[veh_id].order = 0;
                        goto L_599b1a;
                    }
                    if (!delayed) TutSlowFungusCount++;
                }
                if (faction_id == CurrentPlayerFaction && Vehs[veh_id].order == 0 && !TutDisabledFlag &&
                    (VehAttackFlags & 1) && !((map_tile(tx, ty)->items & 0x20) && (map_tile(tx, ty)->climate & 0xE0) >= 0x40) &&
                    tut_check(0x4000)) {
                    TutWin_tut_map(&TutWin, "FRACTIONALMOVE", tx, ty, veh_id, nullptr, 0, x, y);
                    Vehs[veh_id].order = 0;
                    goto L_599b1a;
                }
            }
        }
        veh_skip(veh_id);
        Vehs[veh_id].state |= 0x8000;  // try again next turn with the bonus
        if ((VehAttackFlags & 1) && MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
        result = 1;
        goto L_599b1a;

    L_597b24_move:
    OV_TRACE(0x597b24);
        result = 1;
        if (!(VehAttackFlags & 2)) goto L_599b1a;
        Vehs[veh_id].moves_spent = (uint8_t)(spent + cost);
        Vehs[veh_id].state &= 0xFFFF7FBF;
        int32_t cp = CurrentPlayerFaction;
        if (base_id >= 0 && tgt_faction != faction_id && !(dstat(faction_id, tgt_faction) & 1)) {
            if ((dstat(tgt_faction, faction_id) & 7) && tgt_faction == cp) {
                PluralDefault = 0;
                GenderDefault = MFactions[faction_id].is_leader_female;
                parse_says(0, MFactions[faction_id].title_leader, -1, -1);
                GenderDefault = MFactions[faction_id].is_leader_female;
                PluralDefault = 0;
                parse_says(1, MFactions[faction_id].name_leader, -1, -1);
                GenderDefault = MFactions[faction_id].noun_gender;
                PluralDefault = MFactions[faction_id].is_noun_plural;
                parse_says(2, MFactions[faction_id].noun_faction, -1, -1);
                NetMsg_pop(&NetMsg, "SURPRISE", 5000, 0, nullptr);
                cp = CurrentPlayerFaction;
            }
            if (faction_id != cp && tgt_faction != cp) {
                const BASE& base = Bases[base_id];
                if (((uint8_t)base.faction_id == (uint32_t)cp || (base.visibility & (uint8_t)(1u << (cp & 31)))) &&
                    faction_id != 0) {
                    spot_stack(veh_id, cp);
                    cp = CurrentPlayerFaction;
                }
            }
        }
        // Is the move shown to the viewer?
        if (faction_id == cp) {
            visible = 1;
        } else {
            const VEH& v = Vehs[veh_id];
            if ((v.faction_id == (uint32_t)cp && (v.flags & 0x440) != 0x440) || ((1u << (cp & 31)) & v.visibility))
                visible = 1;
            else
                visible = (GameState & 0x80) ? 1 : 0;
        }
        if (!(GameState & 0x80) && !(Factions[cp].player_flags & 0x200) &&
            !(map_tile(tx, ty)->visibility & (uint8_t)(1u << (cp & 31)))) {
            if (!visible) goto L_59814a;
        } else if (!visible) {
            for (int32_t i = 0; i < 21 && !visible; i++) {
                int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
                int32_t y2 = ty + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                const MAP* n = map_tile(x2, y2);
                const MAP* t = map_tile(tx, ty);
                bool n_sea = (n->climate & 0xE0) < 0x60;
                bool t_sea = (t->climate & 0xE0) < 0x60;
                if (t_sea == n_sea && t->region != n->region) continue;
                if (is_sensor(x2, y2) && whose_territory(CurrentPlayerFaction, x2, y2, nullptr, 0) == CurrentPlayerFaction)
                    visible = 1;
                int32_t owner;
                if (i < 9)
                    owner = ((n->items & 3) && (n->val2 & 0xF) < 8) ? (n->val2 & 0xF) : -1;
                else
                    owner = ((n->items & 1) && (n->val2 & 0xF) < 8) ? (n->val2 & 0xF) : -1;
                if (owner == CurrentPlayerFaction) visible = 1;
            }
            if (!visible) goto L_59814a;
        }
        {
            // Visible: does the map follow the unit?
            cp = CurrentPlayerFaction;
            if (faction_id == cp) {
                scroll = 1;
            } else if ((GamePreferences & 4) && !(Factions[cp].diplo_status[faction_id] & 1)) {
                scroll = 1;
            } else if ((GamePreferences & 0x8000) && (Factions[cp].diplo_status[faction_id] & 1)) {
                scroll = 1;
            } else if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id != (uint32_t)faction_id &&
                       !(dstat(faction_id, (uint8_t)Bases[base_id].faction_id) & 1)) {
                scroll = 1;
            } else {
                scroll = 0;
            }
            bool shift = ui_shift_pressed();
            if (shift) scroll = 0;
            bool jump;
            if (cost == 0 && !(faction_id == cp && !(Vehs[veh_id].state & 0x4200) && Vehs[veh_id].order != 0x18)) {
                jump = true;
            } else if (shift || (GameMorePreferences & 0x80)) {
                jump = true;
            } else if (!(GameMorePreferences & 0x40)) {
                jump = false;
            } else if (faction_id == cp) {
                jump = (Vehs[veh_id].state & 0x4200) || Vehs[veh_id].order == 0x18;
            } else {
                jump = triad == 2;
            }
            if (jump) {
                // Fast moves: no animation, the cursor jumps.
                scroll = 0;
                MapWinField23BF0 = 1;
                if (faction_id == cp) {
                    MapWinField23BF0 = 2;
                    Console_cursor_next(&MainConsole, tx, ty);
                }
            }
            if (visible && !ui_ctrl_pressed() && (scroll || !(GamePreferences & 0x80000)))
                Console_focus(&MainConsole, x, y, faction_id);
        }
        goto L_5980a8;
    L_59814a:
    OV_TRACE(0x59814a);
        scroll = 0;
    L_5980a8:
    OV_TRACE(0x5980a8);
        stack_veh(veh_id, 1);
        if (scroll) {
            if (faction_id == CurrentPlayerFaction)
                FX_engine_pitch(&Sounds, (map_tile(tx, ty)->climate & 0xE0) <= (map_tile(x, y)->climate & 0xE0) ? 200 : 300);
            veh_scoot(veh_id, x, y, dir, 0);
            if (faction_id == CurrentPlayerFaction) FX_engine_pitch(&Sounds, 0);
        } else if (visible) {
            veh_scoot(veh_id, x, y, dir, 0x4000);
        } else {
            Vehs[veh_id].rotate_angle = (int8_t)dir;
        }
        if (faction_id != 0) {
            unspot_stack(veh_id);
        } else {
            // Natives are seen only near the bases or territory of a faction.
            Vehs[veh_id].visibility &= map_tile(tx, ty)->visibility;
            for (int32_t f = 1; f < 8; f++) {
                if (!(Vehs[veh_id].visibility & (1u << f))) continue;
                base_find_2(tx, ty, f);
                if (BaseFindDist >= 8 && whose_territory(f, tx, ty, nullptr, 0) != f)
                    Vehs[veh_id].visibility &= (uint8_t)~(1u << f);
            }
        }
        // Entering a foreign bunker.
        if ((map_tile(tx, ty)->items & 0x800) && stack_id < 0 && veh_chassis_triad(veh_id) == 0 &&
            veh_unit(veh_id).plan != 11) {
            int32_t owner = whose_territory(faction_id, tx, ty, nullptr, 0);
            if (owner >= 0 && owner != faction_id && !(dstat(faction_id, owner) & 1)) {
                veh_skip(veh_id);
                if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "BUNKERCAPTURED", 5000, 0, nullptr);
            }
        }
        if (base_id >= 0 && faction_id != tgt_faction && faction_id == 0) {
            // Native life attacks a base: destroys a facility (or production) and population.
            int32_t tries, fac = 0;
            for (tries = 0; tries <= 100; tries++) {
                fac = game_rand() % 70;
                int32_t allowed = 1;
                const MFaction& natives = MFactions[0];
                for (int32_t i = 0; i < natives.faction_bonus_count; i++) {
                    int32_t id = natives.faction_bonus_id[i];
                    if ((id == 2 || (id == 12 && has_tech(Facility[natives.faction_bonus_val1[i]].preq_tech, 0))) &&
                        fac == natives.faction_bonus_val1[i]) {
                        allowed = 0;
                        break;
                    }
                }
                if (fac == 26) {
                    if (map_is_ocean(tx, ty)) allowed = 0;
                } else if (fac <= 1 || fac >= 65) {
                    continue;
                }
                int32_t offset, mask;
                bitmask(fac, &offset, &mask);
                unload = (Bases[base_id].facilities_built[offset] & (uint8_t)mask) ? 1 : 0;
                if (unload && allowed) break;
            }
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "WORMSATTACK");
            parse_says(2, veh_unit(veh_id).name, -1, -1);
            if (tries <= 100) {
                crt_strcat(StrBuffer, "1");
                set_fac(fac, base_id, 0);
                parse_say(1, Facility[fac].name, -1, -1);
            }
            if (visible && tgt_faction == CurrentPlayerFaction) {
                parse_says(0, Bases[base_id].name, -1, -1);
                NetMsg_pop(&NetMsg, StrBuffer, 5000, 0, nullptr);
            }
            kill(veh_id);
            int32_t kept = 0;
            for (int32_t i = 0; i < 25; i++) {
                int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
                int32_t y2 = ty + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                int32_t v = stack_fix(veh_at(x2, y2));
                veh_id = v;
                if (v < 0 || Vehs[v].faction_id != 0) continue;
                for (v = top_of(v); v >= 0;) {
                    int32_t next = Vehs[v].next_veh_id_stack;
                    if (kept < Factions[tgt_faction].clean_minerals_modifier)
                        kept++;
                    else
                        veh_kill(v);
                    v = next;
                }
                veh_id = v;
            }
            if (CurrentTurn >= 10) {
                BASE& base = Bases[base_id];
                base.pop_size--;
                if (is_objective(base_id)) base.pop_size = (int8_t)(base.pop_size < 1 ? 1 : base.pop_size);
                if (base.pop_size == 0) {
                    base_kill(base_id);
                    draw_map(1);
                    eliminate_player(tgt_faction, 0);
                    draw_map(1);
                    goto L_599b1a;
                }
            }
            if (tries > 100) Bases[base_id].minerals_accumulated = 0;
            draw_tile(Bases[base_id].x, Bases[base_id].y, 2);
            BaseWin_check_base(&BaseWin, base_id);
            goto L_599b1a;
        }
        stack_put(veh_id, tx, ty);
        if (base_id >= 0) {
            if (faction_id != tgt_faction && !(dstat(faction_id, tgt_faction) & 1)) {
                // Capture.
                if (ExpansionEnabled) {
                    int32_t unit_id = Vehs[veh_id].unit_id;
                    if (unit_id != 16 && unit_id != 17 && unit_id != 18) Vehs[veh_id].damage_taken = 0;
                }
                CurrentVehID = veh_id;
                act_of_aggression(faction_id, tgt_faction);
                capture_base(base_id, faction_id, 0);
                veh_id = CurrentVehID;
            }
        } else if (tgt_sea && veh_chassis_triad(veh_id) == 1) {
            // Ships passing coastal artillery of enemies they are visible to lose moves.
            int32_t count = 0, enemy = 0;
            for (int32_t i = 0; i < 25; i++) {
                int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
                int32_t y2 = ty + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                for (int32_t v = stack_fix(veh_at(x2, y2)); v >= 0; v = Vehs[v].next_veh_id_stack) {
                    if (!can_arty(Vehs[v].unit_id, 1) || veh_chassis_triad(v) != 0) continue;
                    int32_t vf = Vehs[v].faction_id;
                    if (vf == faction_id || !(dstat(faction_id, vf) & 0x10)) continue;
                    const VEH& me = Vehs[veh_id];
                    if (me.faction_id == (uint32_t)vf) {
                        if ((me.flags & 0x440) == 0x440 && !(me.visibility & (1u << vf))) continue;
                    } else if (!(me.visibility & (1u << vf))) {
                        continue;
                    }
                    count++;
                    enemy = vf;
                    spot_stack(v, faction_id);
                }
            }
            if (count) {
                Vehs[veh_id].moves_spent = (uint8_t)(Vehs[veh_id].moves_spent + (uint8_t)(Rules.move_rate_roads * count));
                if (faction_id == CurrentPlayerFaction) {
                    parse_says(0, faction_adj(enemy), -1, -1);
                    NetMsg_pop(&NetMsg, "GIBRALTAR", 5000, 0, nullptr);
                }
            }
        }
        // Transports: passengers wake up at the destination.
        if (veh_cargo(veh_id)) {
            if (base_id >= 0 &&
                (faction_id == CurrentPlayerFaction || (MultiplayerActive && (FactionStatus[0] & fbit))) &&
                (Vehs[veh_id].order != 0x18 ||
                 (Vehs[veh_id].waypoint_x[0] == tx && Vehs[veh_id].waypoint_y[0] == ty))) {
                for (int32_t v = top_of(veh_id); v >= 0; v = Vehs[v].next_veh_id_stack) {
                    VEH& p = Vehs[v];
                    if (p.faction_id == (uint32_t)faction_id && p.order == 1 &&
                        (p.waypoint_x[0] < 0 || p.waypoint_x[0] == veh_id))
                        p.order = 0;
                }
            }
        }
        if (faction_id == CurrentPlayerFaction && (GamePreferences & 0x80000000) && veh_chassis_triad(veh_id) == 1 &&
            veh_cargo(veh_id) && tgt_sea && moves_left(veh_id) == 0 &&
            (Vehs[veh_id].order != 0x18 ||
             (Vehs[veh_id].waypoint_x[0] == tx && Vehs[veh_id].waypoint_y[0] == ty))) {
            // Out of moves next to land: the passengers get the focus.
            bool coast = false;
            for (int32_t i = 0; i < 8 && !coast; i++) {
                int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
                int32_t y2 = ty + NearOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                const MAP* n = map_tile(x2, y2);
                coast = (n->climate & 0xE0) >= 0x60 || tile_base_owner(n) >= 0;
            }
            if (coast) {
                for (int32_t v = top_of(veh_id); v >= 0; v = Vehs[v].next_veh_id_stack) {
                    if (veh_chassis_triad(v) == 0 && Vehs[v].order == 1 && moves_left(v) > 0) {
                        Vehs[v].order = 0;
                        MapWinVehA = v;
                    }
                }
            }
        }
        if (triad == 0 && veh_sea && !tgt_sea) Factions[faction_id].region_total_combat_units[tgt_region]++;
    }
L_598cb9:  // after the move (or a won battle)
    OV_TRACE(0x598cb9);
    if (veh_offense(veh_id) == 0) veh_demote(veh_id);
    if (faction_id == 0) {
        const MAP* sq = map_tile(tx, ty);
        if ((sq->items & 0x20) && (sq->climate & 0xE0) >= 0x40 && Vehs[veh_id].damage_taken > 0)
            Vehs[veh_id].damage_taken--;  // natives heal in fungus
    }
    spot_all(veh_id, 1);
    if (faction_id != CurrentPlayerFaction && !(FactionStatus[0] & fbit) && !(GameState & 0x80) && visible &&
        !MapWinField23BF0 && !ui_shift_pressed() && !MultiplayerActive)
        clock_wait(scroll ? (MapWinField23BF0 ? 5 : 100) : 5);
    if (veh_chassis_triad(veh_id) == 2) {
        // Air units: refuel at bases, airbases and carriers.
        const MAP* sq = map_tile(tx, ty);
        int32_t no_refuel = (tile_base_owner(sq) < 0 && !(sq->items & 0x40000)) ? 1 : 0;
        for (int32_t v = veh_id < 0 ? -1 : top_of(veh_id); no_refuel && v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (has_abil(Vehs[v].unit_id, 0x80)) no_refuel = 0;
        }
        if (!(FactionStatus[0] & fbit)) {
            if (!no_refuel) goto L_599121;
        } else if (!no_refuel) {
            if (veh_chassis_range(veh_id) == 1 && moves_left(veh_id) > 0 && fought) goto L_598f9e;
            if (moves_left(veh_id) == 0) goto L_599121;
            if (moves_left(veh_id) > Rules.move_rate_roads &&
                Vehs[veh_id].movement_turns + 1 < veh_chassis_range(veh_id))
                goto L_598f9e;
            if (!zoc_veh(tx, ty, faction_id)) goto L_599121;
        }
    L_598f9e:
    OV_TRACE(0x598f9e);
        if (veh_chassis_range(veh_id) == 0) goto L_599154;
        if (FactionStatus[0] & fbit) {
            const VEH& v = Vehs[veh_id];
            if (faction_id != CurrentPlayerFaction) goto L_599037;
            if (!(GamePreferences & 0x10000) && !(v.state & 0x4200)) goto L_599037;
            if ((v.state & 0x1000200) == 0x1000200) goto L_599037;
            if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) goto L_599037;
        }
        action_home(veh_id, 1);
    L_599037:
    OV_TRACE(0x599037);
        if (moves_left(veh_id) > 0) goto L_599154;
        if (Vehs[veh_id].movement_turns + 1 >= veh_chassis_range(veh_id)) {
            // Out of fuel.
            Vehs[veh_id].state |= 4;
            if (visible) boom(Vehs[veh_id].x, Vehs[veh_id].y, 0x80);
            const UNIT& u = veh_unit(veh_id);
            if (veh_chassis_range(veh_id) == 1 && u.chassis_id == 6) {
                Vehs[veh_id].damage_taken = (uint8_t)(Vehs[veh_id].damage_taken + (uint8_t)(u.reactor_id * 3));
            } else {
                int32_t power = u.plan == 12 ? 1 : (u.reactor_id < 1 ? 1 : u.reactor_id > 100 ? 100 : u.reactor_id) * 10;
                int32_t d = power - Vehs[veh_id].damage_taken;
                d = d < 0 ? 0 : d > 9999 ? 9999 : d;
                Vehs[veh_id].damage_taken = (uint8_t)(Vehs[veh_id].damage_taken + d);
            }
            const UNIT& u2 = veh_unit(veh_id);
            int32_t power = u2.plan == 12 ? 1 : (u2.reactor_id < 1 ? 1 : u2.reactor_id > 100 ? 100 : u2.reactor_id) * 10;
            int32_t left = power - Vehs[veh_id].damage_taken;
            if (left <= 0) {
                kill(veh_id);
                if (visible && faction_id == CurrentPlayerFaction)
                    NetMsg_pop(&NetMsg, "PLANECRASH", 5000, 0, nullptr);
                goto L_599b1a;
            }
            Vehs[veh_id].movement_turns = 0;
            draw_tile(Vehs[veh_id].x, Vehs[veh_id].y, 2);
            if (visible && faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "CHOPPERFUEL", 5000, 0, nullptr);
            goto L_599154;
        }
        if (Vehs[veh_id].movement_turns + 1 >= veh_chassis_range(veh_id) / 2) {
            // Half the range used: head home.
            if (FactionStatus[0] & fbit) {
                const VEH& v = Vehs[veh_id];
                if (faction_id != CurrentPlayerFaction) goto L_59943e;
                if (!(GamePreferences & 0x10000) && !(v.state & 0x4200)) goto L_59943e;
                if ((v.state & 0x1000200) == 0x1000200) goto L_59943e;
                if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) goto L_59943e;
                if (v.order == 0x18) goto L_59943e;
            }
            action_home(veh_id, 0);
        }
    L_59943e:
    OV_TRACE(0x59943e);
        Vehs[veh_id].movement_turns++;
        goto L_599154;
    L_599121:  // refuel
    OV_TRACE(0x599121);
        Vehs[veh_id].movement_turns = 0;
        veh_skip(veh_id);
        veh_demote(veh_id);
        draw_tile(Vehs[veh_id].x, Vehs[veh_id].y, 2);
    }
L_599154: {
    OV_TRACE(0x599154);
        const MAP* sq = map_tile(tx, ty);
        if ((sq->items & 0x2000) && triad != 2) {  // monolith
            bool use = true;
            if (dir < 0) {
                if (veh_offense(veh_id) == 0)
                    use = false;
                else if (Vehs[veh_id].damage_taken == 0 && (Vehs[veh_id].state & 0x100))
                    use = false;
            }
            if (use && !attacked) monolith(veh_id);
        }
    }
    if (goody && triad != 2 && faction_id != 0) {
        if (goody_box(veh_id, goody)) goto L_599b1a;
    }
    if (veh_unit(veh_id).plan == 12 && base_id < 0 && stack_check(veh_id, 1, -1, -1, -1) == 1 && dir < 8) {
        // A lone artifact next to enemy units is taken.
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
            int32_t y2 = ty + NearOffsetY[i];
            if (!map_in_bounds(x2, y2) || map_is_ocean(x2, y2)) continue;
            int32_t s = stack_fix(veh_at(x2, y2));
            if (s < 0) continue;
            int32_t sf = Vehs[s].faction_id;
            if (sf == faction_id || sf == 0 || (dstat(faction_id, sf) & 1)) continue;
            int32_t artifacts = stack_check(s, 2, 12, -1, -1);
            if (stack_check(s, 1, -1, -1, -1) <= artifacts) continue;
            Vehs[veh_id].faction_id = (uint8_t)sf;
            Vehs[veh_id].order = 0;
            draw_tile(tx, ty, 2);
            owner_set(tx, ty, Vehs[veh_id].faction_id);
            if (faction_id == CurrentPlayerFaction) {
                MapWinVehA = -1;
                GenderDefault = MFactions[sf].noun_gender;
                PluralDefault = MFactions[sf].is_noun_plural;
                parse_says(0, MFactions[sf].noun_faction, -1, -1);
                popp(ScriptFileName, "TOOKARTIFACT", 0, "artstolen_sm.pcx", 0);
            } else if (sf == CurrentPlayerFaction) {
                parse_says(0, Units[Vehs[s].unit_id].name, -1, -1);
                GenderDefault = MFactions[faction_id].noun_gender;
                PluralDefault = MFactions[faction_id].is_noun_plural;
                parse_says(1, MFactions[faction_id].noun_faction, -1, -1);
                popp(ScriptFileName, "TAKENARTIFACT", 0, "artstolen_sm.pcx", 0);
            }
            faction_id = Vehs[veh_id].faction_id;
            break;
        }
    }
    if (veh_unit(veh_id).plan == 12 && base_id >= 0 && Vehs[veh_id].movement_turns) {
        if (Vehs[veh_id].movement_turns == 1) {
            if (study_artifact(veh_id)) goto L_599b1a;
        } else if (Vehs[veh_id].movement_turns == 2) {
            // Artifact used to hurry a secret project.
            BASE& base = Bases[base_id];
            base.minerals_accumulated += (veh_unit(veh_id).cost * 10) / 2;
            base.minerals_accumulated_2 = base.minerals_accumulated;
            base.production_id_last = base.queue_items[0];
            kill(veh_id);
            BaseWin_check_base(&BaseWin, base_id);
        }
    }
    if (veh_sea && !tgt_sea && veh_unit(veh_id).plan == 8)
        Factions[faction_id].region_flags[tgt_region] |= 2;  // a landing
    if (unload) {
        // Land unit boarded a transport at sea.
        sleep(veh_id);
        veh_skip(veh_id);
        if (veh_id >= 0) {
            for (int32_t v = top_of(veh_id); v >= 0; v = Vehs[v].next_veh_id_stack) {
                if (veh_cargo(v)) continue;
                if (Vehs[v].moves_spent > speed(v, 0)) {
                    Vehs[v].order = 0;
                    continue;
                }
                stack_veh(v, 0);
                if (Vehs[veh_id].waypoint_x[0] == v) {
                    MapWinVehA = v;
                    Vehs[v].order = 0;
                    break;
                }
            }
        }
        draw_tile(tx, ty, 2);
    }
    if (!(FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) && veh_unit(veh_id).plan == 7 && map_is_ocean(tx, ty) &&
        base_id < 0 && moves_left(veh_id) == 0) {
        // AI colony pods at sea.
        VEH& v = Vehs[veh_id];
        if (zoc_any(v.x, v.y, faction_id) || (FactionStatus[0] & v.visibility) || v.status_icon == 'u') {
            for (int32_t w = veh_id < 0 ? -1 : top_of(veh_id); w >= 0; w = Vehs[w].next_veh_id_stack) {
                if (Vehs[w].order == 1 && Vehs[w].waypoint_x[0] == veh_id) v.order = 0;
            }
        }
        int32_t gx = tx, gy = ty;
        if (v.order == 0x18) {
            int32_t mx = (v.waypoint_x[0] + tx) / 2;
            int32_t my = (v.waypoint_y[0] + ty) / 2;
            if (mx & 1) mx--;
            if (my & 1) mx++;
            if (map_is_ocean(mx, my)) {
                gx = mx;
                gy = my;
            }
        }
        if (map_is_ocean(gx, gy)) add_goal(faction_id, 6, 2, gx, gy, -1);
    }
    if (faction_id == CurrentPlayerFaction && (Vehs[veh_id].order == 0x18 || (Vehs[veh_id].state & 0x4200))) {
        // Long journeys: ask the viewer whether to keep moving.
        VEH& v = Vehs[veh_id];
        v.iter_count++;
        if ((v.state & 0x10000) && dir == ((int32_t)v.rotate_angle ^ 4)) v.iter_count += 8;
        if (v.iter_count >= 0x20) {
            parse_says(0, veh_unit(veh_id).name, -1, -1);
            if (MultiplayerActive || X_pop("KEEPMOVING", 0)) {
                v.order = 0;
                v.state &= 0xFCFFBDFF;
            }
            v.iter_count = 0;
        }
    }
    if (dir >= 0 && dir < 8) {
        Vehs[veh_id].rotate_angle = (int8_t)dir;
        Vehs[veh_id].state |= 0x10000;
    } else {
        Vehs[veh_id].state &= ~0x10000u;
    }
    goto L_599b1a;

L_5962f2:  // no direction (dir 8): end of the unit's move
    OV_TRACE(0x5962f2);
    if (veh_chassis_triad(veh_id) == 2 && veh_chassis_range(veh_id) && (VehAttackFlags & 1) &&
        faction_id == CurrentPlayerFaction) {
        const MAP* sq = map_tile(tx, ty);
        if (tile_base_owner(sq) < 0 && !(sq->items & 0x40000)) {
            // An aircraft of the viewer ending its move away from a base heads home.
            bool carried = false;
            for (int32_t v = veh_id < 0 ? -1 : top_of(veh_id); v >= 0 && !carried; v = Vehs[v].next_veh_id_stack) {
                // BUG(original): tests the moving unit's abilities, not those of the stack member.
                if (has_abil(Vehs[veh_id].unit_id, 0x80)) carried = true;
                else if (veh_unit(v).plan == 10 && veh_chassis_triad(v) == 2) carried = true;
            }
            if (!carried && moves_left(veh_id) > 0 &&
                (veh_chassis_range(veh_id) == 1 || Vehs[veh_id].movement_turns)) {
                action_home(veh_id, 0);
                if (Vehs[veh_id].order == 0x18) {
                    if (MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
                    return 0;
                }
            }
        }
    }
    result = 1;
    if (!(VehAttackFlags & 2)) goto L_599b1a;
    veh_skip(veh_id);
    Vehs[veh_id].state &= ~0x40u;
    goto L_598cb9;

L_5965a3:  // probe team enters a base
    OV_TRACE(0x5965a3);
    if (tgt_faction == CurrentPlayerFaction) {
        Console_focus(&MainConsole, x, y, faction_id);
        if (!MultiplayerActive) clock_wait(400);
        stack_veh(veh_id, 1);
        veh_scoot(veh_id, x, y, dir, 0);
        stack_put(veh_id, x, y);
        draw_tile(tx, ty, 2);
    }
    if (!probe(veh_id, base_id, -1, 1)) goto L_599b1a;
    if (!MultiplayerActive || ControlTurnC || !(VehAttackFlags & 1) || (VehAttackFlags & 2)) goto L_599b1a;
    net_synch_veh(veh_id);
    return 1;
L_5966e0:
    OV_TRACE(0x5966e0);
    net_synch_veh(veh_id);
    return 1;
L_596b1f:
    OV_TRACE(0x596b1f);
    net_synch_veh(veh_id);
    return 1;

L_596884:  // transports full
    OV_TRACE(0x596884);
    tmp = stack_check(stack_id, 2, 7, -1, -1);
    if (!tmp) goto L_599b1a;
    if (faction_id == CurrentPlayerFaction && (VehAttackFlags & 1)) {
        tmp2 = stack_check(stack_id, 2, 7, -1, -1);
        NetMsg_pop(&NetMsg, tmp2 > 1 ? "FULLTRANSPORTS" : "FULLTRANSPORT", 5000, 0, nullptr);
    }
    goto L_599b1a;

L_5968f2:  // zone of control
    OV_TRACE(0x5968f2);
    if (faction_id != CurrentPlayerFaction || delayed || !(VehAttackFlags & 1)) goto L_599b1a;
    FX_play(&Sounds, 13);
    if (faction_id == CurrentPlayerFaction) {
        if (MultiplayerActive) goto L_599b1a;
        if ((GamePreferences & 0x20) ||
            (Factions[faction_id].diff_level <= 1 && ((++TutZocMessageCount) & 3) == 1)) {
            int32_t ax = 0, ay = 0, bx = 0, by = 0;
            parse_say(0, ability_name(4), -1, -1);
            quick_zoc(x, y, faction_id, tx, ty, &ax, &ay);
            quick_zoc(tx, ty, faction_id, x, y, &bx, &by);
            TutWin_tut_map(&TutWin, "ZOCMSG", ax, ay, veh_id, nullptr, 0, bx, by);
            if (MultiplayerActive) goto L_599b1a;
        }
    }
    probed = 0;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
        int32_t y2 = ty + NearOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        int32_t s = stack_fix(veh_at(x2, y2));
        if (s < 0 || Vehs[s].faction_id == (uint32_t)faction_id) continue;
        if (dstat(faction_id, Vehs[s].faction_id) & 1) continue;
        if (fbit & Vehs[s].visibility) continue;
        spot_loc(x2, y2, faction_id);
        probed = 1;
    }
    if (probed) draw_radius(tx, ty, 1, 2);
    goto L_599b1a;

L_596bd9:  // native life spawned in front of the unit
    OV_TRACE(0x596bd9);
    Vehs[veh_id].order = 0;
    spot_stack(stack_id, faction_id);
    draw_tile(tx, ty, 2);
    bit_set(tx, ty, 0x100000, 1);
    if (faction_id != CurrentPlayerFaction) goto L_599b1a;
    FX_play(&Sounds, 13);
    parse_says(0, (&MFactions[0])[tgt_faction].adj_name_faction, -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    NetMsg_pop(&NetMsg, tgt_sea ? "ISLESPOTTED" : "WORMSSPOTTED", 5000, 0, nullptr);
    goto L_599b1a;

L_596c9f:  // unseen units in the way
    OV_TRACE(0x596c9f);
    Vehs[veh_id].order = 0;
    spot_stack(stack_id, faction_id);
    draw_tile(tx, ty, 2);
    if (faction_id != CurrentPlayerFaction) goto L_599b1a;
    FX_play(&Sounds, 13);
    parse_says(0, faction_adj(tgt_faction), -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    NetMsg_pop(&NetMsg, tgt_faction ? "SPOTTED" : "SPOTTED0", 5000, 0, nullptr);
    goto L_599b1a;
L_596d5d:
    OV_TRACE(0x596d5d);
    Vehs[veh_id].order = 0;
    spot_stack(stack_id, faction_id);
    draw_tile(tx, ty, 2);
    FX_play(&Sounds, 13);
    parse_says(0, faction_adj(tgt_faction), -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    NetMsg_pop(&NetMsg, tgt_faction ? "SPOTTED" : "SPOTTED0", 5000, 0, nullptr);
    goto L_599b1a;

L_596e0b:
    OV_TRACE(0x596e0b);
    if (faction_id == CurrentPlayerFaction && (VehAttackFlags & 1)) {
        parse_say(0, ability_name(0x20), -1, -1);
        NetMsg_pop(&NetMsg, "FIGHTERRULE", 5000, 0, nullptr);
    }
    if (Vehs[veh_id].state & 0x4200) veh_skip(veh_id);
    goto L_599b1a;
L_596e85:
    OV_TRACE(0x596e85);
    if (faction_id == CurrentPlayerFaction && (VehAttackFlags & 1))
        NetMsg_pop(&NetMsg, "PSIBOMB", 5000, 0, nullptr);
    goto L_599b1a;
L_596ec0:
    OV_TRACE(0x596ec0);
    tmp = abil_index(8);
    if (faction_id != CurrentPlayerFaction || !(VehAttackFlags & 1)) goto L_599b1a;
    parse_say(0, Ability[tmp].name, -1, -1);
    NetMsg_pop(&NetMsg, tile_base_owner(map_tile(x, y)) >= 0 ? "AMPHIBBASE" : "NOTAMPHIB", 5000, 0, nullptr);
    goto L_599b1a;
L_596f75:
    OV_TRACE(0x596f75);
    if (!(VehAttackFlags & 1) || faction_id != CurrentPlayerFaction) goto L_599b1a;
    FX_play(&Sounds, 0x33);
    NetMsg_pop(&NetMsg, "NONCOMBATANT", 5000, 0, nullptr);
    goto L_599b1a;

L_596fbc: {  // enemy probe team captured: sent back to its biggest base
    OV_TRACE(0x596fbc);
        if (faction_id == CurrentPlayerFaction || tgt_faction == CurrentPlayerFaction) {
            Console_focus(&MainConsole, x, y, faction_id);
            stack_veh(veh_id, 1);
            veh_scoot(veh_id, x, y, dir, 0);
            stack_put(veh_id, x, y);
            draw_tile(x, y, 2);
        }
        int32_t best = -1, best_pop = -1;
        for (int32_t b = 0; b < BaseCount; b++) {
            if ((uint8_t)Bases[b].faction_id == (uint32_t)tgt_faction && Bases[b].pop_size > best_pop) {
                best_pop = Bases[b].pop_size;
                best = b;
            }
        }
        if (best >= 0) {
            stack_put(stack_id, Bases[best].x, Bases[best].y);
            Vehs[stack_id].visibility = 0;
            parse_says(2, Bases[best].name, -1, -1);
        } else if (!is_human(tgt_faction)) {
            veh_kill(stack_id);
        }
        draw_tile(tx, ty, 2);
        if (tgt_faction == CurrentPlayerFaction) {
            parse_says(0, faction_adj(faction_id), -1, -1);
            say_title(1, tgt_faction);
            NetMsg_pop(&NetMsg, "GOTMYPROBE", 5000, 0, nullptr);
        } else if (faction_id == CurrentPlayerFaction) {
            parse_says(0, faction_adj(tgt_faction), -1, -1);
            say_title(1, faction_id);
            NetMsg_pop(&NetMsg, "GOTYOURPROBE", 5000, 0, nullptr);
        }
        goto L_599b1a;
    }
    }  // scope of the target-tile locals

L_599b1a:  // end
    OV_TRACE(0x599b1a);
    if (VehCount != veh_count) goto L_return0;
    if (!result) {
        VEH& v = Vehs[veh_id];
        bool human = (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0;
        v.order = 0;
        if (!human || (v.state & 0x4200)) {
            v.iter_count++;
            if (v.iter_count >= 0x18 || !human) {
                v.iter_count = 0;
                veh_skip(veh_id);
            }
        }
        if (!(VehAttackFlags & 2) && MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
        return 0;
    }
    if (VehAttackFlags & 2) {
        VEH& v = Vehs[veh_id];
        if (dir >= 0 && dir < 8 && (v.state & 8) && v.x == tx && v.y == ty && !zoc_veh(tx, ty, faction_id))
            v.state &= ~8u;
        if (MultiplayerActive && !ControlTurnC && v.order == 0x18 && v.x == v.waypoint_x[0] &&
            v.y == v.waypoint_y[0]) {
            v.order = 0;
            if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) {
                v.patrol_current_point++;
                v.order = 0x18;
                v.waypoint_x[0] = v.waypoint_x[v.patrol_current_point];
                v.waypoint_y[0] = v.waypoint_y[v.patrol_current_point];
                v.state &= 0xFDFEFDFF;
            }
        }
    }
    return result;
L_return0:
    return 0;
}
