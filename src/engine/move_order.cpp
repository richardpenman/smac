// order_veh (0x5947C0): one step of a unit in a direction, with everything that can happen on the
// way (move module; kept in its own file for its size). The original's join points (named
// L_<address> in the comments) are the steps of a small driver over an OrderVeh object, whose
// members are the original's locals.
#include "engine/compat.h"
#include "engine/game_state.h"
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
#include "engine/move_rules.h"
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
#include "engine/table_at.h"

namespace {
inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
// other may be -1 (order_veh, no target faction): the original then reads the field before
// diplo_status (diplo_status_raw).
inline uint32_t dstat(int32_t faction_id, int32_t other) { return diplo_status_raw(faction_id, other); }
inline const UNIT& veh_unit(int32_t veh_id) { return Units[game().vehs[veh_id].unit_id]; }
inline int32_t veh_chassis_triad(int32_t veh_id) { return Chassis[veh_unit(veh_id).chassis_id].triad; }
inline int32_t veh_chassis_range(int32_t veh_id) { return Chassis[veh_unit(veh_id).chassis_id].range; }
inline int32_t veh_offense(int32_t veh_id) { return Weapon[veh_unit(veh_id).weapon_id].offense_value; }
// Movement points left (0-999).
int32_t moves_left(int32_t veh_id) {
    int32_t m = speed(veh_id, 0) - game().vehs[veh_id].moves_spent;
    return m < 0 ? 0 : m > 999 ? 999 : m;
}
// Owner of a base on the tile (0-7), or -1.
inline int32_t tile_base_owner(const MAP* sq) {
    if (!(sq->items & BIT_BASE_IN_TILE)) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner < 8 ? owner : -1;
}
// Top of a vehicle stack (inline in the original).
inline int32_t top_of(int32_t veh_id) {
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
inline const char* faction_adj(int32_t faction_id) { return game().mfactions[faction_id].adj_name_faction; }
// Leader title of a faction as parse slot n (sets the gender/plural defaults like the original).
void say_title(int32_t n, int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    parse_says(n, game().mfactions[faction_id].title_leader, -1, -1);
}
const char* ability_name(uint32_t flag) { return Ability[abil_index(flag)].name; }
// The original's Popup object on order_veh's stack, constructed first and destroyed on return.
struct StackPopup {
    alignas(8) uint8_t obj[0x5410];
    StackPopup() { Popup_ctor(obj); }
    ~StackPopup() { Popup_dtor(obj); }
};
void net_synch_veh(int32_t veh_id) {
    synch_veh(veh_id);
    NetDaemon_await_synch(&NetState);
}

// Next step of order_veh: the original's join points, the end of the call, or a return that skips
// the end's processing (`done`, with the result in OrderVeh::result).
enum class Step {
    start,  // L_5949b3: (re)start, also with a disembarking passenger as the unit
    land_on_sea,  // L_595083: land units moving onto a sea tile
    zoc_check,  // L_59533d: zones of control, supply crawlers, native life
    target,  // L_59512c: a unit on the target tile?
    movement,  // L_596052: movement checks
    enter_base,  // L_597345: entering a base
    move,  // L_59763b: the move itself
    after_move,  // L_598cb9: after the move (or a won battle)
    no_direction,  // L_5962f2: dir 8, the end of the unit's move
    end,  // L_599b1a
    done,
};

// State of one order_veh call: the unit, its tile and the target tile (with the stack, base and
// faction there), and the original's locals, with their stack slots.
struct OrderVeh {
    int32_t veh_id, dir;
    int32_t result = 0;  // [ebp-0x58]: the vehicle moved (or acted) and may continue
    int32_t unload = 0;  // [ebp-0x68]: land unit boarding a transport at sea (no base)
    int32_t fought = 0;  // [ebp-0x74]: won a battle and moves on
    int32_t attacked = 0;  // [ebp-0x6c]: a battle was fought this step
    int32_t probed = 0;  // [ebp-0x60]: probe action / units spotted
    // [ebp-0x24]: the move is shown to the viewer. The original reuses the slot (target stack
    // offset); paths that do not set it see the earlier value (stack residue #5, only display
    // timing depends on it).
    int32_t visible = 0;
    int32_t delayed = 0;  // [ebp-0x30]: the unit is exploring or on alert (checks part)
    int32_t& scroll = delayed;  // same slot in the movement part: the map follows the unit
    int32_t triad = 0;  // [ebp-0x4c]
    int32_t veh_count = 0;
    int32_t x = 0, y = 0, faction_id = 0;
    int32_t tx = 0, ty = 0;
    // The target tile.
    int32_t tgt_region = 0, stack_id = -1, base_id = -1, tgt_faction = -1;
    int32_t veh_sea = 0, tgt_sea = 0, goody = 0;
    uint8_t fbit = 0;

    OrderVeh(int32_t veh, int32_t dir_) : veh_id(veh), dir(dir_) {}
    // The order came from the viewer's own unit (its messages are shown).
    bool viewer_order() const { return faction_id == CurrentPlayerFaction && (VehAttackFlags & 1); }
    Step finish(int32_t value) {
        result = value;
        return Step::done;
    }
    Step begin(int32_t flags);
    Step start();
    void artifact_menu();
    Step probe_team();
    bool probe_pact_declined();
    Step probe_base();
    Step land_on_sea();
    Step transports_full();
    Step amphibious_base();
    Step zoc_check();
    Step zoc_blocked();
    Step native_life();
    Step natives_spawned();
    Step target();
    Step units_spotted();
    Step fighter_rule();
    Step attack_checks();
    Step enemy_probe();
    Step probe_captured();
    Step prepare_attack();
    Step capture_natives();
    bool natives_captured(const Faction& f);
    void take_natives(Faction& f);
    Step attack();
    Step movement();
    Step disembark();
    Step enter_base();
    Step ask_goody();
    Step move();
    bool partial_move_succeeds(int32_t cost);
    Step move_failed();
    int32_t surprise_attack();
    bool move_shown(int32_t cp);
    void follow_unit(int32_t cost);
    void leave_tile();
    void enter_bunker();
    Step natives_attack_base();
    void arrive();
    void coastal_artillery();
    void wake_passengers();
    void wake_at_coast();
    Step after_move();
    bool air_fuel();
    bool keeps_flying();
    bool viewer_auto_home() const;
    void refuel();
    void use_monolith();
    void artifact_taken();
    bool use_artifact();
    void board_transport();
    void colony_pod_at_sea();
    void keep_moving();
    Step no_direction();
    int32_t end();
};

// The target tile of the step; Step::end when it is off the map.
Step OrderVeh::begin(int32_t flags) {
    VehAttackFlags = flags;
    if (flags & 1) MapWinField23BE4 = 0;
    else MapWinField23BF0 = 0;
    veh_count = game().veh_count;
    x = game().vehs[veh_id].x;
    y = game().vehs[veh_id].y;
    faction_id = game().vehs[veh_id].faction_id;
    if (dir < 0) dir = 8;
    tx = map_wrap_x(x + NearOffsetX[dir]);
    ty = y + NearOffsetY[dir];
    if (!map_in_bounds(tx, ty)) return Step::end;
    tgt_region = map_tile(tx, ty)->region;
    stack_id = stack_fix(veh_at(tx, ty));
    base_id = base_at(tx, ty);
    tgt_faction = base_id >= 0    ? (uint8_t)game().bases[base_id].faction_id
                  : stack_id >= 0 ? (int32_t)game().vehs[stack_id].faction_id
                                  : -1;
    veh_sea = map_is_ocean(x, y);
    tgt_sea = map_is_ocean(tx, ty);
    goody = goody_at(tx, ty);
    fbit = (uint8_t)(1u << (faction_id & 31));
    return Step::start;
}

// L_5949b3: (re)start, also with a disembarking passenger as veh_id.
Step OrderVeh::start() {
    delayed = (game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) ? 1 : 0;
    triad = veh_chassis_triad(veh_id);
    if (!(FactionStatus[0] & fbit) && game().vehs[veh_id].order == ORDER_MOVE_TO) {
        // AI units do not walk into the territory of factions they are at peace with.
        int32_t owner = whose_territory(faction_id, game().vehs[veh_id].waypoint_x[0],
                                        game().vehs[veh_id].waypoint_y[0], nullptr, 0);
        if (owner >= 0 && owner != faction_id && !(dstat(faction_id, owner) & 0xC11)) {
            game().vehs[veh_id].order = ORDER_NONE;
            return Step::end;
        }
    }
    if (veh_unit(veh_id).plan == 12 && !(game().vehs[veh_id].flags & VFLAG_IS_OBJECTIVE) && (VehAttackFlags & 1))
        artifact_menu();
    if (dir >= 8) return Step::no_direction;
    game().vehs[veh_id].state |= VSTATE_HAS_MOVED;
    if (veh_unit(veh_id).plan == 11 && tgt_faction >= 0 && faction_id != tgt_faction) return probe_team();
    return Step::land_on_sea;
}

// Alien artifact entering a base: link it to the network node or a secret project.
void OrderVeh::artifact_menu() {
    game().vehs[veh_id].movement_turns = 0;
    if (base_id < 0 || (uint8_t)game().bases[base_id].faction_id != (uint32_t)faction_id) return;
    BASE& base = game().bases[base_id];
    parse_says(0, base.name, -1, -1);
    parse_num(0, base.minerals_accumulated);
    int32_t item = base.queue_items[0];
    int32_t cost = item < 0 ? cost_factor(faction_id, 1, -1) * Facility[-item].cost
                            : veh_cost(item, base_id, nullptr) * cost_factor(faction_id, 1, -1);
    parse_num(1, cost);
    if (X_text_open(ScriptFileName, "ARTIFACTMENU")) return;
    StrBuffer[0] = 0;
    parse_string(text_get(), StrBuffer);
    int32_t items = 1;
    char* text = text_get();
    int32_t offset, mask;
    bitmask(8, &offset, &mask);  // network node
    if ((base.facilities_built[offset] & (uint8_t)mask) &&
        SecretProjects[FAC_UNIVERSAL_TRANSLATOR - SP_ID_First] != base_id &&
        !(base.state_flags & BSTATE_ARTIFACT_ALREADY_LINKED)) {
        StrBuffer[0] = 0;
        parse_string(text, StrBuffer);
        items = 2;
        if (!(FactionStatus[0] & fbit)) game().vehs[veh_id].movement_turns = 1;
    }
    text = text_get();
    if (SecretProjects[FAC_UNIVERSAL_TRANSLATOR - SP_ID_First] == base_id) {
        StrBuffer[0] = 0;
        parse_string(text, StrBuffer);
        items++;
        if (!(FactionStatus[0] & fbit)) game().vehs[veh_id].movement_turns = 1;
    }
    text = text_get();
    item = base.queue_items[0];
    if (item <= -70) {
        StrBuffer[0] = 0;
        parse_say(1, Facility[item < 0 ? -item : item].name, -1, -1);
        parse_string(text, StrBuffer);
        items++;
        if (!(FactionStatus[0] & fbit)) game().vehs[veh_id].movement_turns = 2;
    }
    text = text_get();
    item = base.queue_items[0];
    if (item >= 0 && item >= 64 && !(Units[item].unit_flags & UNIT_PROTOTYPED)) {
        StrBuffer[0] = 0;
        parse_says(2, Units[item].name, -1, -1);
        parse_string(text, StrBuffer);
        items++;
    }
    if (items > 1 && faction_id == CurrentPlayerFaction && (FactionStatus[0] & fbit)) {
        game().vehs[veh_id].movement_turns = (uint8_t)ui_artifact_menu(veh_id, base_id, items);
        net_synch_veh(veh_id);
    }
}

// Probe team: probe actions against other factions.
Step OrderVeh::probe_team() {
    if (VehAttackFlags & 1) {
        game().vehs[veh_id].flags &= ~VFLAG_PROBE_PACT_OPERATIONS;
        if ((dstat(faction_id, tgt_faction) & 1) && !delayed && faction_id == CurrentPlayerFaction &&
            probe_pact_declined())
            return Step::land_on_sea;
    }
    // L_594f3c
    if (base_id >= 0) {
        if (stack_id >= 0 && stack_check(stack_id, 2, 11, -1, -1)) return Step::land_on_sea;
        if (FactionStatus[0] & fbit) return probe_base();
        if (!(dstat(faction_id, tgt_faction) & 1)) return probe_base();
        if (game().vehs[veh_id].waypoint_x[0] == game().bases[base_id].x &&
            game().vehs[veh_id].waypoint_y[0] == game().bases[base_id].y)
            return probe_base();
        return Step::land_on_sea;
    }
    if (dstat(faction_id, tgt_faction) & 1) return Step::land_on_sea;
    if (stack_id >= 0 && stack_check(stack_id, 2, 11, -1, -1)) return Step::land_on_sea;
    probed = 1;
    if (probe(veh_id, -1, stack_id, 1) && MultiplayerActive && !ControlTurnC && (VehAttackFlags & 1) &&
        !(VehAttackFlags & 2)) {
        net_synch_veh(veh_id);  // L_5966e0
        return finish(1);
    }
    if (game().veh_count != veh_count) return finish(0);
    if (stack_id >= 0) tgt_faction = game().vehs[stack_id].faction_id;
    return Step::land_on_sea;
}

// The viewer's probe team against a pact partner: asked first. True when the viewer declines;
// when it agrees the team may act against the partner.
bool OrderVeh::probe_pact_declined() {
    if (base_id >= 0) {
        parse_says(0, game().bases[base_id].name, -1, -1);
        parse_says(1, faction_adj(tgt_faction), -1, -1);
        if (!X_pop("PROBEPACT", 0)) return true;
    } else {
        if (!stack_check(stack_id, 2, 11, tgt_faction, -1)) return false;
        parse_says(0, faction_adj(tgt_faction), -1, -1);
        if (!X_pop("PROBEPACTTEAM", 0)) return true;
    }
    game().vehs[veh_id].flags |= VFLAG_PROBE_PACT_OPERATIONS;  // probe operations against a pact partner
    return false;
}

// L_5965a3: probe team enters a base.
Step OrderVeh::probe_base() {
    if (tgt_faction == CurrentPlayerFaction) {
        Console_focus(&MainConsole, x, y, faction_id);
        if (!MultiplayerActive) clock_wait(400);
        stack_veh(veh_id, 1);
        veh_scoot(veh_id, x, y, dir, 0);
        stack_put(veh_id, x, y);
        draw_tile(tx, ty, 2);
    }
    if (!probe(veh_id, base_id, -1, 1)) return Step::end;
    if (!MultiplayerActive || ControlTurnC || !(VehAttackFlags & 1) || (VehAttackFlags & 2)) return Step::end;
    net_synch_veh(veh_id);
    return finish(1);
}

// L_595083: land unit onto a sea tile.
Step OrderVeh::land_on_sea() {
    if (triad != 0 || !tgt_sea) return Step::zoc_check;
    if (faction_id == tgt_faction) {
        if (stack_check(stack_id, 8, faction_id, -1, -1) <= 0) {
            if (base_id < 0) return transports_full();
            if (!has_abil(game().vehs[veh_id].unit_id, 8) && !stack_check(stack_id, 2, 7, -1, -1) &&
                !stack_check(veh_id, 2, 7, -1, -1)) {
                // L_5967cf: AMPHIBBASE2 (re-tests the same conditions)
                if (base_id < 0 || has_abil(game().vehs[veh_id].unit_id, 8) || stack_check(stack_id, 2, 7, -1, -1) ||
                    stack_check(veh_id, 2, 7, -1, -1))
                    return transports_full();
                return amphibious_base();
            }
        }
        if (base_id < 0) unload = 1;
        return Step::zoc_check;
    }
    if (dstat(faction_id, tgt_faction) & 1) return Step::end;
    if (base_id >= 0) {
        if (!has_abil(game().vehs[veh_id].unit_id, 8)) {
            if (viewer_order()) {
                parse_say(0, ability_name(8), -1, -1);
                NetMsg_pop(&NetMsg, "AMPHIBBASE", 5000, 0, nullptr);
            }
            return Step::end;
        }
        return Step::zoc_check;
    }
    // Air superiority units attack aircraft over the sea.
    if (stack_id < 0 || !has_abil(game().vehs[veh_id].unit_id, 0x20) || !stack_check(stack_id, 0x12, -1, -1, -1) ||
        stack_check(stack_id, 6, 0x80, -1, -1))
        return Step::end;
    return Step::target;
}

// L_596884: transports full.
Step OrderVeh::transports_full() {
    if (!stack_check(stack_id, 2, 7, -1, -1)) return Step::end;
    if (viewer_order()) {
        int32_t transports = stack_check(stack_id, 2, 7, -1, -1);
        NetMsg_pop(&NetMsg, transports > 1 ? "FULLTRANSPORTS" : "FULLTRANSPORT", 5000, 0, nullptr);
    }
    return Step::end;
}

// L_5972bd: a land unit without the amphibious ability leaving a base by sea.
Step OrderVeh::amphibious_base() {
    if (viewer_order()) {
        parse_say(0, ability_name(8), -1, -1);
        NetMsg_pop(&NetMsg, "AMPHIBBASE2", 5000, 0, nullptr);
    }
    return Step::end;
}

// L_59533d: zones of control, supply crawlers, native life.
Step OrderVeh::zoc_check() {
    if (!veh_sea && !tgt_sea && triad == 0 && !(veh_unit(veh_id).ability_flags & 4) && veh_unit(veh_id).plan != 11 &&
        stack_id < 0 && zoc_move(x, y, faction_id) && zoc_move(tx, ty, faction_id))
        return zoc_blocked();
    if (veh_unit(veh_id).plan == 10 && base_id >= 0 && !MultiplayerActive) {
        int32_t r = supply_options(veh_id, base_id);
        if (VehAttackFlags & 2) {
            if (r) return Step::end;
        } else {
            if (!r) return Step::end;
            if (MultiplayerActive && !ControlTurnC) {
                net_synch_veh(veh_id);  // L_596b1f
                return finish(1);
            }
        }
    }
    // Native life may appear in fungus.
    if (!(VehAttackFlags & 2) || veh_chassis_triad(veh_id) == 2 || stack_id >= 0 || faction_id == 0)
        return Step::target;
    return native_life();
}

// L_5968f2: zone of control. The viewer is told (tutorial) and the enemy units around are spotted.
Step OrderVeh::zoc_blocked() {
    if (faction_id != CurrentPlayerFaction || delayed || !(VehAttackFlags & 1)) return Step::end;
    FX_play(&Sounds, 13);
    if (faction_id == CurrentPlayerFaction) {
        if (MultiplayerActive) return Step::end;
        if ((GamePreferences & PREF_BSC_TUTORIAL_MSGS) ||
            (game().factions[faction_id].diff_level <= 1 && ((++TutZocMessageCount) & 3) == 1)) {
            int32_t ax = 0, ay = 0, bx = 0, by = 0;
            parse_say(0, ability_name(4), -1, -1);
            quick_zoc(x, y, faction_id, tx, ty, &ax, &ay);
            quick_zoc(tx, ty, faction_id, x, y, &bx, &by);
            TutWin_tut_map(&TutWin, "ZOCMSG", ax, ay, veh_id, nullptr, 0, bx, by);
            if (MultiplayerActive) return Step::end;
        }
    }
    probed = 0;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
        int32_t y2 = ty + NearOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        int32_t s = stack_fix(veh_at(x2, y2));
        if (s < 0 || game().vehs[s].faction_id == (uint32_t)faction_id) continue;
        if (dstat(faction_id, game().vehs[s].faction_id) & 1) continue;
        if (fbit & game().vehs[s].visibility) continue;
        spot_loc(x2, y2, faction_id);
        probed = 1;
    }
    if (probed) draw_radius(tx, ty, 1, 2);
    return Step::end;
}

// Native life may appear in fungus in front of the unit (random); stack_id becomes the natives.
Step OrderVeh::native_life() {
    const MAP* sq = map_tile(tx, ty);
    if (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40 || (sq->items & BIT_MAGTUBE)) return Step::target;
    if ((veh_offense(veh_id) == 0 || game().vehs[veh_id].order == ORDER_MOVE_TO) &&
        SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First] >= 0 &&
        (uint8_t)game().bases[SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First]].faction_id == (uint32_t)faction_id)
        return Step::target;
    if (veh_sea != tgt_sea) return Step::target;
    if (goody_at(tx, ty)) return Step::target;
    if (GameRules & RULES_SCN_NO_NATIVE_LIFE) return Step::target;
    base_find(tx, ty);
    if (BaseFindDist <= 1) return Step::target;
    uint32_t items = map_tile(tx, ty)->items;
    int32_t unit_id = game().vehs[veh_id].unit_id;
    int32_t chance = move_rules::native_life_chance(
        BaseFindDist, MapNativeLifeForms, (items & BIT_SUPPLY_REMOVE) != 0, tgt_sea != 0, (items & BIT_ROAD) != 0,
        unit_id < 64 && (Weapon[Units[unit_id].weapon_id].offense_value < 0 || unit_id == 15));
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
        int32_t y2 = ty + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        if (is_sensor(x2, y2) || tile_base_owner(map_tile(x2, y2)) >= 0)
            chance = move_rules::native_life_cover(chance, i);
    }
    if (game_rand() % 100 >= chance) return Step::target;
    int32_t spawn;
    if (tgt_sea) spawn = 9;
    else if (game_rand() % 5 == 0 && ExpansionEnabled) spawn = 15;
    else spawn = 8;
    stack_id = veh_init(spawn, 0, tx, ty);
    if (stack_id >= 0) return natives_spawned();
    return Step::movement;
}

// L_596bd9: native life spawned in front of the unit.
Step OrderVeh::natives_spawned() {
    game().vehs[veh_id].order = ORDER_NONE;
    spot_stack(stack_id, faction_id);
    draw_tile(tx, ty, 2);
    bit_set(tx, ty, 0x100000, 1);
    if (faction_id != CurrentPlayerFaction) return Step::end;
    FX_play(&Sounds, 13);
    parse_says(0, table_at(game().mfactions, kMFactionsAddr, tgt_faction).adj_name_faction, -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    NetMsg_pop(&NetMsg, tgt_sea ? "ISLESPOTTED" : "WORMSSPOTTED", 5000, 0, nullptr);
    return Step::end;
}

// L_59512c: a unit on the target tile? Own and allied units are joined, unseen ones spotted, the
// others attacked.
Step OrderVeh::target() {
    if (stack_id < 0) return Step::movement;
    // L_595137
    visible = stack_id * 0x34;  // [ebp-0x24] holds the target's stack offset (residue #5)
    const VEH& tgt = game().vehs[stack_id];
    if (!(tgt.flags & 0x400)) {
        if (faction_id == tgt_faction) return Step::movement;
        if (dstat(faction_id, tgt_faction) & 1) {
            if (veh_unit(veh_id).plan != 11 || !(game().vehs[veh_id].flags & VFLAG_PROBE_PACT_OPERATIONS))
                return Step::movement;
        }
    }
    if (probed) return Step::end;
    if (tgt.flags & 0x400) return units_spotted();  // unseen units (L_596c9f)
    if (!((tgt.faction_id == (uint32_t)faction_id && (tgt.flags & 0x440) != 0x440) || (fbit & tgt.visibility)) &&
        (FactionStatus[0] & fbit)) {
        if (base_id >= 0) return attack_checks();
        if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction) return units_spotted();  // L_596d5d
    }
    if (base_id >= 0) return attack_checks();
    if (map_tile(tx, ty)->items & BIT_AIRBASE) return attack_checks();
    if (stack_check(stack_id, 0x12, -1, -1, -1) && !has_abil(game().vehs[veh_id].unit_id, 0x20) &&
        !stack_check(stack_id, 6, 0x80, -1, -1))
        return fighter_rule();  // only fighters attack air units
    return attack_checks();
}

// L_596c9f, L_596d5d: unseen units in the way are spotted, the move stops. (The second label is
// reached for the viewer only, where the viewer test below holds.)
Step OrderVeh::units_spotted() {
    game().vehs[veh_id].order = ORDER_NONE;
    spot_stack(stack_id, faction_id);
    draw_tile(tx, ty, 2);
    if (faction_id != CurrentPlayerFaction) return Step::end;
    FX_play(&Sounds, 13);
    parse_says(0, faction_adj(tgt_faction), -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    NetMsg_pop(&NetMsg, tgt_faction ? "SPOTTED" : "SPOTTED0", 5000, 0, nullptr);
    return Step::end;
}

// L_596e0b: only air superiority units attack aircraft.
Step OrderVeh::fighter_rule() {
    if (viewer_order()) {
        parse_say(0, ability_name(0x20), -1, -1);
        NetMsg_pop(&NetMsg, "FIGHTERRULE", 5000, 0, nullptr);
    }
    if (game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) veh_skip(veh_id);
    return Step::end;
}

// L_595734: may the unit attack the target?
Step OrderVeh::attack_checks() {
    if (veh_sea) {
        if (game().vehs[veh_id].unit_id != 14 && !tgt_sea && triad == 1 && !can_arty(game().vehs[veh_id].unit_id, 1) &&
            veh_offense(veh_id) != 0) {
            // L_596e85: ships attack land only with artillery (the Sealurk, unit 14, excepted).
            if (viewer_order()) NetMsg_pop(&NetMsg, "PSIBOMB", 5000, 0, nullptr);
            return Step::end;
        }
        if (triad == 0 && !has_abil(game().vehs[veh_id].unit_id, 8) && veh_unit(veh_id).plan != 11) {
            // L_596ec0: a land unit attacking from a ship needs the amphibious ability.
            int32_t amphibious = abil_index(8);
            if (viewer_order()) {
                parse_say(0, Ability[amphibious].name, -1, -1);
                NetMsg_pop(&NetMsg, tile_base_owner(map_tile(x, y)) >= 0 ? "AMPHIBBASE" : "NOTAMPHIB", 5000, 0,
                           nullptr);
            }
            return Step::end;
        }
    }
    // L_5957d7
    if (Weapon[veh_unit(veh_id).weapon_id].mode == 11 && stack_check(stack_id, 2, 11, -1, -1)) return prepare_attack();
    if (veh_offense(veh_id) == 0) {
        // L_596f75
        if (viewer_order()) {
            FX_play(&Sounds, 0x33);
            NetMsg_pop(&NetMsg, "NONCOMBATANT", 5000, 0, nullptr);
        }
        return Step::end;
    }
    if (veh_unit(stack_id).plan == 11 && stack_check(stack_id, 1, -1, -1, -1) == 1 && !MultiplayerActive)
        return enemy_probe();
    return prepare_attack();
}

// An enemy probe team alone: captured (the viewer is asked), or attacked.
Step OrderVeh::enemy_probe() {
    uint32_t d = dstat(faction_id, tgt_faction);
    if (!(d & 6) || (d & 1)) return prepare_attack();
    int32_t near = base_find(tx, ty);
    if (near >= 0 && (uint8_t)game().bases[near].faction_id == (uint32_t)faction_id) {
        parse_says(0, faction_adj(tgt_faction), -1, -1);
        parse_says(1, veh_unit(stack_id).name, -1, -1);
        say_title(2, faction_id);
        if (!(FactionStatus[0] & fbit)) return probe_captured();
        int32_t choice = (game().mfactions[tgt_faction].rule_flags & 0x80)
                             ? popp(ScriptFileName, "ENEMYPROBE", 0, "al_cap_sm.pcx", 0)
                             : popp(ScriptFileName, "ENEMYPROBE", 0, "capture_sm.pcx", 0);
        if (!choice) return Step::end;
        if (choice == 1) return probe_captured();
        return prepare_attack();
    }
    if (faction_id != CurrentPlayerFaction) return prepare_attack();
    parse_says(0, faction_adj(tgt_faction), -1, -1);
    parse_says(1, veh_unit(stack_id).name, -1, -1);
    say_title(2, faction_id);
    int32_t choice = (game().mfactions[tgt_faction].rule_flags & 0x80)
                         ? popp(ScriptFileName, "ENEMYPROBE2", 0, "al_cap_sm.pcx", 0)
                         : popp(ScriptFileName, "ENEMYPROBE2", 0, "capture_sm.pcx", 0);
    if (!choice) return Step::end;
    return prepare_attack();
}

// L_596fbc: enemy probe team captured: sent back to its biggest base.
Step OrderVeh::probe_captured() {
    if (faction_id == CurrentPlayerFaction || tgt_faction == CurrentPlayerFaction) {
        Console_focus(&MainConsole, x, y, faction_id);
        stack_veh(veh_id, 1);
        veh_scoot(veh_id, x, y, dir, 0);
        stack_put(veh_id, x, y);
        draw_tile(x, y, 2);
    }
    int32_t best = -1, best_pop = -1;
    for (int32_t b = 0; b < game().base_count; b++) {
        if ((uint8_t)game().bases[b].faction_id == (uint32_t)tgt_faction && game().bases[b].pop_size > best_pop) {
            best_pop = game().bases[b].pop_size;
            best = b;
        }
    }
    if (best >= 0) {
        stack_put(stack_id, game().bases[best].x, game().bases[best].y);
        game().vehs[stack_id].visibility = 0;
        parse_says(2, game().bases[best].name, -1, -1);
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
    return Step::end;
}

// L_595a8f, L_595a92: attacking with less than a full move, then the attack.
Step OrderVeh::prepare_attack() {
    if (moves_left(veh_id) < Rules.move_rate_roads && game().vehs[veh_id].unit_id != 15 &&
        game().vehs[veh_id].unit_id != 8) {
        if (faction_id == CurrentPlayerFaction && !delayed && (VehAttackFlags & 1)) {
            parse_says(0, veh_unit(veh_id).name, -1, -1);
            parse_num(0, moves_left(veh_id));
            parse_num(1, Rules.move_rate_roads);
            if (popp(ScriptFileName, "HASTY", 0, "bad_sm.pcx", 0)) return Step::end;
        } else if (!(FactionStatus[0] & fbit)) {
            if (moves_left(veh_id) <= Rules.move_rate_roads / 2) return Step::end;
        }
    }
    CurrentVehID = veh_id;
    if (!(VehAttackFlags & 2)) {
        // L_59729d: attack, not moving on (VehAttackFlags bit 1 clear)
        result = battle_fight(veh_id, dir, 0, 1, nullptr) == 0;
        return Step::end;
    }
    return capture_natives();
}

// Attacks on native life may capture it instead (PLANET).
Step OrderVeh::capture_natives() {
    if (tgt_faction != 0) return attack();
    if (veh_chassis_triad(veh_id) == 2) return attack();
    if (game().vehs[stack_id].unit_id == 7 || game().vehs[stack_id].unit_id == 19) return attack();
    Faction& f = game().factions[faction_id];
    if (f.SE_planet <= 0) return attack();
    if (moves_left(veh_id) < Rules.move_rate_roads) {
        if (f.SE_planet_base <= 0) return attack();
        if (f.units_active[game().vehs[stack_id].unit_id]) return attack();
    }
    int32_t near = base_find_faction(tx, ty, faction_id);
    if (!(VehAttackFlags & 2)) return attack();
    if (game().vehs[stack_id].order_auto_type == (uint32_t)faction_id ||
        (near >= 0 && game().bases[near].eco_damage != 0)) {
        // L_597216
        log_say_values("Agitated", 0, 0, 0);
        if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "WORMSAGITATED", 5000, 0, nullptr);
        return attack();
    }
    log_say_values("Precapture who, enemy", faction_id, stack_id, 0);
    log_say_values("Precapture fac, proto", f.SE_planet_base, f.units_active[game().vehs[stack_id].unit_id], 0);
    log_say_values("Precapture type, soc2", game().vehs[stack_id].unit_id, f.SE_planet, 0);
    if (!(game().vehs[stack_id].flags & 0x2000) && natives_captured(f)) {
        take_natives(f);
        return Step::movement;
    }
    // L_5971ce: no capture: the natives fight
    game().vehs[stack_id].flags |= 0x2000;
    if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "WORMNOCAPTURE", 5000, 0, nullptr);
    log_say_values("No Capture", 0, 0, 0);
    return attack();
}

// Are the natives captured (random, then the faction's PLANET rating and its units of the kind)?
bool OrderVeh::natives_captured(const Faction& f) {
    if (game_rand() % 4 < f.SE_planet) {
        int32_t n = CurrentTurn / 5 + 100;
        int32_t roll = n - 1 <= 0 ? 0 : game_rand() % n;
        if (roll >= f.units_active[game().vehs[stack_id].unit_id] * 10) return true;
        if (game_rand() % 10 == 0) return true;
    }
    return f.SE_planet_base > 0 && !f.units_active[game().vehs[stack_id].unit_id];
}

// L_595e1b: the native stack joins the faction.
void OrderVeh::take_natives(Faction& f) {
    if (stack_id >= 0) {
        for (int32_t v = top_of(stack_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            game().vehs[v].faction_id = (uint8_t)faction_id;
            game().vehs[v].morale = (uint8_t)morale_alien(v, 0);
            int32_t b = base_find(tx, ty);
            const BASE& base = table_at(game().bases, kBasesAddr, b);
            if ((uint8_t)base.faction_id == (uint32_t)faction_id && base.pop_size >= 3)
                game().vehs[v].home_base_id = (int16_t)b;
            else game().vehs[v].home_base_id = -1;
            veh_skip(v);
            if (v < 0) break;
        }
    }
    bit_set(tx, ty, 0x100000, 1);
    owner_set(tx, ty, faction_id);
    if (faction_id == CurrentPlayerFaction) draw_tile_fixup(tx, ty, 1, 2);
    else draw_tile(tx, ty, 2);
    if (faction_id == CurrentPlayerFaction) {
        int32_t unit_id = game().vehs[stack_id].unit_id;
        parse_says(0, Units[unit_id].name, -1, -1);
        if (unit_id == 15) {
            if (!f.units_active[15])  // first capture of the kind: full popup
                popp(ScriptFileName, "WORMCAPTURE", 0, "sporlnch_sm.pcx", 0);
            else NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
        } else if (unit_id == 14) {
            if (!f.units_active[14]) popp(ScriptFileName, "WORMCAPTURE", 0, "sealrk_sm.pcx", 0);
            else NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
        } else if (!f.units_active[unit_id]) {
            popp(ScriptFileName, "WORMCAPTURE", 0, map_is_ocean(tx, ty) ? "isle_sm.pcx" : "mindworm_sm.pcx", 0);
        } else {
            NetMsg_pop(&NetMsg, "WORMCAPTURE", 5000, 0, nullptr);
        }
    }
    log_say_values("Captured", 0, 0, 0);
}

// L_59724d: attack. A won battle continues with the post-move processing on the unit's own tile.
Step OrderVeh::attack() {
    attacked = 1;
    int32_t outcome = battle_fight(veh_id, dir, 0, 1, nullptr);
    if (outcome == 0) return Step::end;
    if (outcome != 1) {
        result = 0;
        return Step::end;
    }
    veh_id = CurrentVehID;
    tx = x;
    ty = y;
    fought = 1;
    goody = 0;
    // BUG(original): the post-move code reads visible ([ebp-0x24]), which still holds the target's
    // stack offset here (stack residue #5). Fixed (compat residue.order_veh_visible): whether the
    // viewer sees the unit, as for a move.
    if (!g_compat.residue.order_veh_visible) move_shown(CurrentPlayerFaction);
    return Step::after_move;
}

// L_596052: movement checks.
Step OrderVeh::movement() {
    if (veh_sea) {
        if (tgt_sea) return Step::enter_base;
        if (triad == 0 && stack_id < 0 && !has_abil(game().vehs[veh_id].unit_id, 8) &&
            tile_base_owner(map_tile(x, y)) >= 0 && !stack_check(veh_id, 2, 7, -1, -1))
            return amphibious_base();
    } else if (tgt_sea) {
        return Step::enter_base;
    }
    if (base_id >= 0) return Step::enter_base;  // L_597350
    if (triad != 1) return Step::enter_base;
    if (MultiplayerActive) return Step::end;
    if (!viewer_order()) return Step::end;
    return disembark();
}

// A ship moving to land: a passenger disembarks (the viewer chooses; the step restarts with it).
Step OrderVeh::disembark() {
    game().vehs[veh_id].order = ORDER_NONE;
    parse_says(0, veh_unit(veh_id).name, -1, -1);
    if (X_text_open(ScriptFileName, "DISEMBARKERS")) return Step::end;
    text_get();
    stack_veh(veh_id, 0);
    int32_t chosen = -1, count = 0;
    if (veh_id < 0) return Step::end;
    for (int32_t v = top_of(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
        if (game().vehs[v].order == ORDER_SENTRY_BOARD && game().vehs[v].waypoint_x[0] == veh_id) {
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
        return Step::end;
    }
    int32_t choice = ui_disembark_menu(veh_id) - 1;
    if (choice < 0) return Step::end;
    veh_id = choice;
    game().vehs[choice].order = ORDER_NONE;
    return Step::start;
}

// L_597345: entering a base. Only some units may take an enemy base; at peace, AIs stop and the
// viewer is asked to break the treaty.
Step OrderVeh::enter_base() {
    if (base_id < 0) return ask_goody();
    // L_597350
    if (tgt_faction == faction_id || (dstat(faction_id, tgt_faction) & 1)) return ask_goody();
    if (triad == 2) {
        if (veh_chassis_range(veh_id)) {
            if (viewer_order()) {
                parse_say(0, Chassis[veh_unit(veh_id).chassis_id].offsv1_name, -1, -1);
                NetMsg_pop(&NetMsg, "AIRCONQUER", 5000, 0, nullptr);
            }
            return Step::end;
        }
    } else if (triad == 1) {
        if (!tgt_sea) {
            if (viewer_order()) NetMsg_pop(&NetMsg, "SEACONQUER", 5000, 0, nullptr);
            return Step::end;
        }
    } else if (triad == 0 && tgt_sea && !has_abil(game().vehs[veh_id].unit_id, 8)) {
        if (viewer_order()) NetMsg_pop(&NetMsg, "LANDCONQUER", 5000, 0, nullptr);
        return Step::end;
    }
    if (veh_offense(veh_id) == 0) {
        if (viewer_order()) NetMsg_pop(&NetMsg, "COMBATCONQUER", 5000, 0, nullptr);
        return Step::end;
    }
    if (dstat(faction_id, tgt_faction) & 7) {
        if (!(FactionStatus[0] & fbit)) {
            game().vehs[veh_id].order = ORDER_NONE;
            return Step::end;
        }
        if (faction_id != CurrentPlayerFaction) return Step::move;
        if (VehAttackFlags & 1) {
            if (break_treaty(faction_id, tgt_faction, 7)) return Step::end;
        }
    }
    return ask_goody();
}

// L_597564: the viewer's unit on its way elsewhere is asked before it opens a supply pod.
Step OrderVeh::ask_goody() {
    if (faction_id == CurrentPlayerFaction && (FactionStatus[0] & fbit) && (VehAttackFlags & 1) &&
        game().vehs[veh_id].order == ORDER_MOVE_TO &&
        !(game().vehs[veh_id].waypoint_x[0] == tx && game().vehs[veh_id].waypoint_y[0] == ty) &&
        veh_chassis_triad(veh_id) != 2 && !(game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) &&
        goody_at(tx, ty) && popp(ScriptFileName, "GOTOGOODY", 0, "supply_sm.pcx", 0)) {
        game().vehs[veh_id].order = ORDER_NONE;
        return Step::end;
    }
    return Step::move;
}

// L_59763b: the move itself.
Step OrderVeh::move() {
    if (moves_left(veh_id) == 0) return Step::end;
    int32_t cost = hex_cost(game().vehs[veh_id].unit_id, faction_id, x, y, tx, ty, 0);
    uint8_t spent = game().vehs[veh_id].moves_spent;
    if (cost > moves_left(veh_id) && triad == 0 && !partial_move_succeeds(cost)) return move_failed();
    // L_597b24
    result = 1;
    if (!(VehAttackFlags & 2)) return Step::end;
    game().vehs[veh_id].moves_spent = (uint8_t)(spent + cost);
    game().vehs[veh_id].state &= 0xFFFF7FBF;
    int32_t cp = surprise_attack();
    if (move_shown(cp)) follow_unit(cost);
    else scroll = 0;  // L_59814a
    // L_5980a8
    leave_tile();
    enter_bunker();
    if (base_id >= 0 && faction_id != tgt_faction && faction_id == 0) return natives_attack_base();
    stack_put(veh_id, tx, ty);
    arrive();
    wake_passengers();
    wake_at_coast();
    if (triad == 0 && veh_sea && !tgt_sea) game().factions[faction_id].region_total_combat_units[tgt_region]++;
    return Step::after_move;
}

// Not enough movement points: a land unit may still make it (random). True when it moves.
bool OrderVeh::partial_move_succeeds(int32_t cost) {
    if (moves_left(veh_id) >= Rules.move_rate_roads) {
        const MAP* sq = map_tile(tx, ty);
        if (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40) return true;
        int32_t unit_id = game().vehs[veh_id].unit_id;
        if (unit_id < 64 && Weapon[Units[unit_id].weapon_id].offense_value < 0) return true;
        if (Units[unit_id].plan == 9 || Units[unit_id].plan == 12) return true;
        if (stack_id >= 0) return true;
    }
    if (!(VehAttackFlags & 1) && (FactionStatus[0] & fbit)) return true;
    int32_t roll;
    if (ControlTurnC) roll = cost - 1 <= 0 ? 0 : game_rand() % cost;
    else roll = game_random(0, cost);
    int32_t limit = moves_left(veh_id) + ((game().vehs[veh_id].state & 0x8000) ? Rules.move_rate_roads : 0);
    return roll < limit;
}

// The unit did not make it: it tries again next turn with a bonus (the viewer sees the attempt and
// may get a tutorial message).
Step OrderVeh::move_failed() {
    if (faction_id == CurrentPlayerFaction) {
        if (game().vehs[veh_id].order == ORDER_NONE && !ui_shift_pressed() && (VehAttackFlags & 1) &&
            !(game().vehs[veh_id].state & VSTATE_ON_ALERT) && !(GameMorePreferences & MPREF_ADV_QUICK_MOVE_ALL_VEH)) {
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
            bool fungus = (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40;
            if (fungus && game().vehs[veh_id].order == ORDER_NONE && veh_unit(veh_id).plan != 9 &&
                veh_unit(veh_id).plan != 12 && (VehAttackFlags & 1)) {
                if (tut_check(0x1000)) {
                    TutSlowFungusCount++;
                    FX_play(&Sounds, 9);
                    TutWin_tut_map(&TutWin, "SLOWFUNGUS", tx, ty, -1, nullptr, 0, -1, -1);
                    game().vehs[veh_id].order = ORDER_NONE;
                    return Step::end;
                }
                if (!delayed) TutSlowFungusCount++;
            }
            if (faction_id == CurrentPlayerFaction && game().vehs[veh_id].order == ORDER_NONE && !TutDisabledFlag &&
                (VehAttackFlags & 1) &&
                !((map_tile(tx, ty)->items & BIT_FUNGUS) && (map_tile(tx, ty)->climate & 0xE0) >= 0x40) &&
                tut_check(0x4000)) {
                TutWin_tut_map(&TutWin, "FRACTIONALMOVE", tx, ty, veh_id, nullptr, 0, x, y);
                game().vehs[veh_id].order = ORDER_NONE;
                return Step::end;
            }
        }
    }
    veh_skip(veh_id);
    game().vehs[veh_id].state |= 0x8000;  // try again next turn with the bonus
    if ((VehAttackFlags & 1) && MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
    result = 1;
    return Step::end;
}

// A unit entering a base of a faction it is not allied with: the owner (the viewer) is told of a
// surprise attack, the viewer sees units moving into bases it can see. Returns the viewer.
int32_t OrderVeh::surprise_attack() {
    int32_t cp = CurrentPlayerFaction;
    if (base_id >= 0 && tgt_faction != faction_id && !(dstat(faction_id, tgt_faction) & 1)) {
        if ((dstat(tgt_faction, faction_id) & 7) && tgt_faction == cp) {
            PluralDefault = 0;
            GenderDefault = game().mfactions[faction_id].is_leader_female;
            parse_says(0, game().mfactions[faction_id].title_leader, -1, -1);
            GenderDefault = game().mfactions[faction_id].is_leader_female;
            PluralDefault = 0;
            parse_says(1, game().mfactions[faction_id].name_leader, -1, -1);
            GenderDefault = game().mfactions[faction_id].noun_gender;
            PluralDefault = game().mfactions[faction_id].is_noun_plural;
            parse_says(2, game().mfactions[faction_id].noun_faction, -1, -1);
            NetMsg_pop(&NetMsg, "SURPRISE", 5000, 0, nullptr);
            cp = CurrentPlayerFaction;
        }
        if (faction_id != cp && tgt_faction != cp) {
            const BASE& base = game().bases[base_id];
            if (((uint8_t)base.faction_id == (uint32_t)cp || (base.visibility & (uint8_t)(1u << (cp & 31)))) &&
                faction_id != 0) {
                spot_stack(veh_id, cp);
                cp = CurrentPlayerFaction;
            }
        }
    }
    return cp;
}

// Is the move shown to the viewer cp? Sets visible.
bool OrderVeh::move_shown(int32_t cp) {
    if (faction_id == cp) {
        visible = 1;
    } else {
        const VEH& v = game().vehs[veh_id];
        if ((v.faction_id == (uint32_t)cp && (v.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
            ((1u << (cp & 31)) & v.visibility))
            visible = 1;
        else visible = (GameState & STATE_OMNISCIENT_VIEW) ? 1 : 0;
    }
    if (!(GameState & STATE_OMNISCIENT_VIEW) && !(game().factions[cp].player_flags & PFLAG_MAP_REVEALED) &&
        !(map_tile(tx, ty)->visibility & (uint8_t)(1u << (cp & 31))))
        return visible != 0;
    if (!visible) {
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
                owner =
                    ((n->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (n->val2 & 0xF) < 8) ? (n->val2 & 0xF) : -1;
            else owner = ((n->items & BIT_BASE_IN_TILE) && (n->val2 & 0xF) < 8) ? (n->val2 & 0xF) : -1;
            if (owner == CurrentPlayerFaction) visible = 1;
        }
    }
    return visible != 0;
}

// The move is visible: does the map follow the unit (scroll), or does the cursor jump?
void OrderVeh::follow_unit(int32_t cost) {
    int32_t cp = CurrentPlayerFaction;
    if (faction_id == cp) {
        scroll = 1;
    } else if ((GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ENEMY_VEH) &&
               !(game().factions[cp].diplo_status[faction_id] & DIPLO_PACT)) {
        scroll = 1;
    } else if ((GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ALLY_VEH) &&
               (game().factions[cp].diplo_status[faction_id] & DIPLO_PACT)) {
        scroll = 1;
    } else if (base_id >= 0 && (uint8_t)game().bases[base_id].faction_id != (uint32_t)faction_id &&
               !(dstat(faction_id, (uint8_t)game().bases[base_id].faction_id) & 1)) {
        scroll = 1;
    } else {
        scroll = 0;
    }
    bool shift = ui_shift_pressed();
    if (shift) scroll = 0;
    bool jump;
    if (cost == 0 && !(faction_id == cp && !(game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) &&
                       game().vehs[veh_id].order != ORDER_MOVE_TO)) {
        jump = true;
    } else if (shift || (GameMorePreferences & MPREF_ADV_QUICK_MOVE_ALL_VEH)) {
        jump = true;
    } else if (!(GameMorePreferences & MPREF_ADV_QUICK_MOVE_VEH_ORDERS)) {
        jump = false;
    } else if (faction_id == cp) {
        jump = (game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)) ||
               game().vehs[veh_id].order == ORDER_MOVE_TO;
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
    if (visible && !ui_ctrl_pressed() && (scroll || !(GamePreferences & PREF_ADV_NO_CENTER_VEH_ORDERS)))
        Console_focus(&MainConsole, x, y, faction_id);
}

// L_5980a8: the unit leaves its tile (animated when shown); who sees it now.
void OrderVeh::leave_tile() {
    stack_veh(veh_id, 1);
    if (scroll) {
        if (faction_id == CurrentPlayerFaction)
            FX_engine_pitch(&Sounds,
                            (map_tile(tx, ty)->climate & 0xE0) <= (map_tile(x, y)->climate & 0xE0) ? 200 : 300);
        veh_scoot(veh_id, x, y, dir, 0);
        if (faction_id == CurrentPlayerFaction) FX_engine_pitch(&Sounds, 0);
    } else if (visible) {
        veh_scoot(veh_id, x, y, dir, 0x4000);
    } else {
        game().vehs[veh_id].rotate_angle = (int8_t)dir;
    }
    if (faction_id != 0) {
        unspot_stack(veh_id);
    } else {
        // Natives are seen only near the bases or territory of a faction.
        game().vehs[veh_id].visibility &= map_tile(tx, ty)->visibility;
        for (int32_t f = 1; f < 8; f++) {
            if (!(game().vehs[veh_id].visibility & (1u << f))) continue;
            base_find_faction(tx, ty, f);
            if (BaseFindDist >= 8 && whose_territory(f, tx, ty, nullptr, 0) != f)
                game().vehs[veh_id].visibility &= (uint8_t)~(1u << f);
        }
    }
}

// Entering a foreign bunker.
void OrderVeh::enter_bunker() {
    if ((map_tile(tx, ty)->items & 0x800) && stack_id < 0 && veh_chassis_triad(veh_id) == 0 &&
        veh_unit(veh_id).plan != 11) {
        int32_t owner = whose_territory(faction_id, tx, ty, nullptr, 0);
        if (owner >= 0 && owner != faction_id && !(dstat(faction_id, owner) & 1)) {
            veh_skip(veh_id);
            if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "BUNKERCAPTURED", 5000, 0, nullptr);
        }
    }
}

// Native life attacks a base: destroys a facility (or production) and population.
Step OrderVeh::natives_attack_base() {
    int32_t tries, fac = 0;
    for (tries = 0; tries <= 100; tries++) {
        fac = game_rand() % 70;
        int32_t allowed = 1;
        const MFaction& natives = game().mfactions[0];
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
        unload = (game().bases[base_id].facilities_built[offset] & (uint8_t)mask) ? 1 : 0;
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
        parse_says(0, game().bases[base_id].name, -1, -1);
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
        if (v < 0 || game().vehs[v].faction_id != 0) continue;
        for (v = top_of(v); v >= 0;) {
            int32_t next = game().vehs[v].next_veh_id_stack;
            if (kept < game().factions[tgt_faction].clean_minerals_modifier) kept++;
            else veh_kill(v);
            v = next;
        }
        veh_id = v;
    }
    if (CurrentTurn >= 10) {
        BASE& base = game().bases[base_id];
        base.pop_size--;
        if (is_objective(base_id)) base.pop_size = (int8_t)(base.pop_size < 1 ? 1 : base.pop_size);
        if (base.pop_size == 0) {
            base_kill(base_id);
            draw_map(1);
            eliminate_player(tgt_faction, 0);
            draw_map(1);
            return Step::end;
        }
    }
    if (tries > 100) game().bases[base_id].minerals_accumulated = 0;
    draw_tile(game().bases[base_id].x, game().bases[base_id].y, 2);
    BaseWin_check_base(&BaseWin, base_id);
    return Step::end;
}

// On the new tile: an enemy base is captured; ships passing coastal artillery of enemies they are
// visible to lose moves.
void OrderVeh::arrive() {
    if (base_id >= 0) {
        if (faction_id != tgt_faction && !(dstat(faction_id, tgt_faction) & 1)) {
            // Capture.
            if (ExpansionEnabled) {
                int32_t unit_id = game().vehs[veh_id].unit_id;
                if (unit_id != 16 && unit_id != 17 && unit_id != 18) game().vehs[veh_id].damage_taken = 0;
            }
            CurrentVehID = veh_id;
            act_of_aggression(faction_id, tgt_faction);
            capture_base(base_id, faction_id, 0);
            veh_id = CurrentVehID;
        }
    } else if (tgt_sea && veh_chassis_triad(veh_id) == 1) {
        coastal_artillery();
    }
}

void OrderVeh::coastal_artillery() {
    int32_t count = 0, enemy = 0;
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
        int32_t y2 = ty + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        for (int32_t v = stack_fix(veh_at(x2, y2)); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            if (!can_arty(game().vehs[v].unit_id, 1) || veh_chassis_triad(v) != 0) continue;
            int32_t vf = game().vehs[v].faction_id;
            if (vf == faction_id || !(dstat(faction_id, vf) & 0x10)) continue;
            const VEH& me = game().vehs[veh_id];
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
        game().vehs[veh_id].moves_spent =
            (uint8_t)(game().vehs[veh_id].moves_spent + (uint8_t)(Rules.move_rate_roads * count));
        if (faction_id == CurrentPlayerFaction) {
            parse_says(0, faction_adj(enemy), -1, -1);
            NetMsg_pop(&NetMsg, "GIBRALTAR", 5000, 0, nullptr);
        }
    }
}

// Transports: passengers wake up at the destination.
void OrderVeh::wake_passengers() {
    if (!veh_cargo(veh_id)) return;
    if (base_id >= 0 && (faction_id == CurrentPlayerFaction || (MultiplayerActive && (FactionStatus[0] & fbit))) &&
        (game().vehs[veh_id].order != ORDER_MOVE_TO ||
         (game().vehs[veh_id].waypoint_x[0] == tx && game().vehs[veh_id].waypoint_y[0] == ty))) {
        for (int32_t v = top_of(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            VEH& p = game().vehs[v];
            if (p.faction_id == (uint32_t)faction_id && p.order == 1 &&
                (p.waypoint_x[0] < 0 || p.waypoint_x[0] == veh_id))
                p.order = 0;
        }
    }
}

// The viewer's transport out of moves next to land: the passengers get the focus.
void OrderVeh::wake_at_coast() {
    if (faction_id == CurrentPlayerFaction && (GamePreferences & PREF_AUTO_WAKE_VEH_TRANS_REACH_LAND) &&
        veh_chassis_triad(veh_id) == 1 && veh_cargo(veh_id) && tgt_sea && moves_left(veh_id) == 0 &&
        (game().vehs[veh_id].order != ORDER_MOVE_TO ||
         (game().vehs[veh_id].waypoint_x[0] == tx && game().vehs[veh_id].waypoint_y[0] == ty))) {
        bool coast = false;
        for (int32_t i = 0; i < 8 && !coast; i++) {
            int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
            int32_t y2 = ty + NearOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            const MAP* n = map_tile(x2, y2);
            coast = (n->climate & 0xE0) >= 0x60 || tile_base_owner(n) >= 0;
        }
        if (coast) {
            for (int32_t v = top_of(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
                if (veh_chassis_triad(v) == 0 && game().vehs[v].order == ORDER_SENTRY_BOARD && moves_left(v) > 0) {
                    game().vehs[v].order = ORDER_NONE;
                    MapWinVehA = v;
                }
            }
        }
    }
}

// L_598cb9: after the move (or a won battle).
Step OrderVeh::after_move() {
    if (veh_offense(veh_id) == 0) veh_demote(veh_id);
    if (faction_id == 0) {
        const MAP* sq = map_tile(tx, ty);
        if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 && game().vehs[veh_id].damage_taken > 0)
            game().vehs[veh_id].damage_taken--;  // natives heal in fungus
    }
    spot_all(veh_id, 1);
    if (faction_id != CurrentPlayerFaction && !(FactionStatus[0] & fbit) && !(GameState & STATE_OMNISCIENT_VIEW) &&
        visible && !MapWinField23BF0 && !ui_shift_pressed() && !MultiplayerActive)
        clock_wait(scroll ? (MapWinField23BF0 ? 5 : 100) : 5);
    if (veh_chassis_triad(veh_id) == 2 && air_fuel()) return Step::end;
    // L_599154
    use_monolith();
    if (goody && triad != 2 && faction_id != 0) {
        if (goody_box(veh_id, goody)) return Step::end;
    }
    if (veh_unit(veh_id).plan == 12 && base_id < 0 && stack_check(veh_id, 1, -1, -1, -1) == 1 && dir < 8)
        artifact_taken();
    if (veh_unit(veh_id).plan == 12 && base_id >= 0 && game().vehs[veh_id].movement_turns) {
        if (use_artifact()) return Step::end;
    }
    if (veh_sea && !tgt_sea && veh_unit(veh_id).plan == 8)
        game().factions[faction_id].region_flags[tgt_region] |= 2;  // a landing
    if (unload) board_transport();
    if (!(FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) && veh_unit(veh_id).plan == 7 &&
        map_is_ocean(tx, ty) && base_id < 0 && moves_left(veh_id) == 0)
        colony_pod_at_sea();
    if (faction_id == CurrentPlayerFaction && (game().vehs[veh_id].order == ORDER_MOVE_TO ||
                                               (game().vehs[veh_id].state & (VSTATE_ON_ALERT | VSTATE_EXPLORE))))
        keep_moving();
    if (dir >= 0 && dir < 8) {
        game().vehs[veh_id].rotate_angle = (int8_t)dir;
        game().vehs[veh_id].state |= 0x10000;
    } else {
        game().vehs[veh_id].state &= ~0x10000u;
    }
    return Step::end;
}

// Air units: refuel at bases, airbases and carriers, else spend fuel (out of fuel they take
// damage, at half the range they head home). True when the unit crashed.
bool OrderVeh::air_fuel() {
    const MAP* sq = map_tile(tx, ty);
    int32_t no_refuel = (tile_base_owner(sq) < 0 && !(sq->items & BIT_AIRBASE)) ? 1 : 0;
    for (int32_t v = veh_id < 0 ? -1 : top_of(veh_id); no_refuel && v >= 0; v = game().vehs[v].next_veh_id_stack) {
        if (has_abil(game().vehs[v].unit_id, 0x80)) no_refuel = 0;
    }
    if (!no_refuel && (!(FactionStatus[0] & fbit) || !keeps_flying())) {
        refuel();
        return false;
    }
    // L_598f9e
    if (veh_chassis_range(veh_id) == 0) return false;
    if (!(FactionStatus[0] & fbit) || viewer_auto_home()) action_home(veh_id, 1);
    // L_599037
    if (moves_left(veh_id) > 0) return false;
    if (game().vehs[veh_id].movement_turns + 1 >= veh_chassis_range(veh_id)) {
        // Out of fuel.
        game().vehs[veh_id].state |= VSTATE_HAS_MOVED;
        if (visible) boom(game().vehs[veh_id].x, game().vehs[veh_id].y, 0x80);
        const UNIT& u = veh_unit(veh_id);
        if (veh_chassis_range(veh_id) == 1 && u.chassis_id == 6) {
            game().vehs[veh_id].damage_taken =
                (uint8_t)(game().vehs[veh_id].damage_taken + (uint8_t)(u.reactor_id * 3));
        } else {
            int32_t d = move_rules::unit_hit_points(u.plan, u.reactor_id) - game().vehs[veh_id].damage_taken;
            d = d < 0 ? 0 : d > 9999 ? 9999 : d;
            game().vehs[veh_id].damage_taken = (uint8_t)(game().vehs[veh_id].damage_taken + d);
        }
        const UNIT& u2 = veh_unit(veh_id);
        int32_t left = move_rules::unit_hit_points(u2.plan, u2.reactor_id) - game().vehs[veh_id].damage_taken;
        if (left <= 0) {
            kill(veh_id);
            if (visible && faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "PLANECRASH", 5000, 0, nullptr);
            return true;
        }
        game().vehs[veh_id].movement_turns = 0;
        draw_tile(game().vehs[veh_id].x, game().vehs[veh_id].y, 2);
        if (visible && faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "CHOPPERFUEL", 5000, 0, nullptr);
        return false;
    }
    if (game().vehs[veh_id].movement_turns + 1 >= veh_chassis_range(veh_id) / 2) {
        // Half the range used: head home.
        if (!(FactionStatus[0] & fbit) || (viewer_auto_home() && game().vehs[veh_id].order != ORDER_MOVE_TO))
            action_home(veh_id, 0);
    }
    // L_59943e
    game().vehs[veh_id].movement_turns++;
    return false;
}

// A human player's aircraft at a refuelling place: does it fly on instead of refuelling?
bool OrderVeh::keeps_flying() {
    if (veh_chassis_range(veh_id) == 1 && moves_left(veh_id) > 0 && fought) return true;
    if (moves_left(veh_id) == 0) return false;
    if (moves_left(veh_id) > Rules.move_rate_roads &&
        game().vehs[veh_id].movement_turns + 1 < veh_chassis_range(veh_id))
        return true;
    return zoc_veh(tx, ty, faction_id) != 0;
}

// A human player's aircraft heads home by itself only for the viewer, with the preference or on
// alert or exploring, and not on patrol.
bool OrderVeh::viewer_auto_home() const {
    const VEH& v = game().vehs[veh_id];
    if (faction_id != CurrentPlayerFaction) return false;
    if (!(GamePreferences & PREF_AUTO_AIR_VEH_RET_HOME_FUEL_RNG) && !(v.state & (VSTATE_ON_ALERT | VSTATE_EXPLORE)))
        return false;
    if ((v.state & 0x1000200) == 0x1000200) return false;
    if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) return false;
    return true;
}

// L_599121: refuel.
void OrderVeh::refuel() {
    game().vehs[veh_id].movement_turns = 0;
    veh_skip(veh_id);
    veh_demote(veh_id);
    draw_tile(game().vehs[veh_id].x, game().vehs[veh_id].y, 2);
}

// Monoliths (not for air units, not after a battle).
void OrderVeh::use_monolith() {
    const MAP* sq = map_tile(tx, ty);
    if ((sq->items & BIT_MONOLITH) && triad != 2) {
        bool use = true;
        if (dir < 0) {
            if (veh_offense(veh_id) == 0) use = false;
            else if (game().vehs[veh_id].damage_taken == 0 && (game().vehs[veh_id].state & VSTATE_MONOLITH_UPGRADED))
                use = false;
        }
        if (use && !attacked) monolith(veh_id);
    }
}

// A lone artifact next to enemy units is taken.
void OrderVeh::artifact_taken() {
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(tx + NearOffsetX[i]);
        int32_t y2 = ty + NearOffsetY[i];
        if (!map_in_bounds(x2, y2) || map_is_ocean(x2, y2)) continue;
        int32_t s = stack_fix(veh_at(x2, y2));
        if (s < 0) continue;
        int32_t sf = game().vehs[s].faction_id;
        if (sf == faction_id || sf == 0 || (dstat(faction_id, sf) & 1)) continue;
        int32_t artifacts = stack_check(s, 2, 12, -1, -1);
        if (stack_check(s, 1, -1, -1, -1) <= artifacts) continue;
        game().vehs[veh_id].faction_id = (uint8_t)sf;
        game().vehs[veh_id].order = ORDER_NONE;
        draw_tile(tx, ty, 2);
        owner_set(tx, ty, game().vehs[veh_id].faction_id);
        if (faction_id == CurrentPlayerFaction) {
            MapWinVehA = -1;
            GenderDefault = game().mfactions[sf].noun_gender;
            PluralDefault = game().mfactions[sf].is_noun_plural;
            parse_says(0, game().mfactions[sf].noun_faction, -1, -1);
            popp(ScriptFileName, "TOOKARTIFACT", 0, "artstolen_sm.pcx", 0);
        } else if (sf == CurrentPlayerFaction) {
            parse_says(0, Units[game().vehs[s].unit_id].name, -1, -1);
            GenderDefault = game().mfactions[faction_id].noun_gender;
            PluralDefault = game().mfactions[faction_id].is_noun_plural;
            parse_says(1, game().mfactions[faction_id].noun_faction, -1, -1);
            popp(ScriptFileName, "TAKENARTIFACT", 0, "artstolen_sm.pcx", 0);
        }
        faction_id = game().vehs[veh_id].faction_id;
        break;
    }
}

// An artifact in a base does what the artifact menu chose (movement_turns 1: link to the network
// node or study it; 2: hurry a secret project). True when the step ends here.
bool OrderVeh::use_artifact() {
    if (game().vehs[veh_id].movement_turns == 1) {
        if (study_artifact(veh_id)) return true;
    } else if (game().vehs[veh_id].movement_turns == 2) {
        // Artifact used to hurry a secret project.
        BASE& base = game().bases[base_id];
        base.minerals_accumulated += (veh_unit(veh_id).cost * 10) / 2;
        base.minerals_accumulated_2 = base.minerals_accumulated;
        base.production_id_last = base.queue_items[0];
        kill(veh_id);
        BaseWin_check_base(&BaseWin, base_id);
    }
    return false;
}

// Land unit boarded a transport at sea.
void OrderVeh::board_transport() {
    sleep(veh_id);
    veh_skip(veh_id);
    if (veh_id >= 0) {
        for (int32_t v = top_of(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            if (veh_cargo(v)) continue;
            if (game().vehs[v].moves_spent > speed(v, 0)) {
                game().vehs[v].order = ORDER_NONE;
                continue;
            }
            stack_veh(v, 0);
            if (game().vehs[veh_id].waypoint_x[0] == v) {
                MapWinVehA = v;
                game().vehs[v].order = ORDER_NONE;
                break;
            }
        }
    }
    draw_tile(tx, ty, 2);
}

// AI colony pods at sea out of moves: a transport is asked for (goal) on the way.
void OrderVeh::colony_pod_at_sea() {
    VEH& v = game().vehs[veh_id];
    if (zoc_any(v.x, v.y, faction_id) || (FactionStatus[0] & v.visibility) || v.status_icon == 'u') {
        for (int32_t w = veh_id < 0 ? -1 : top_of(veh_id); w >= 0; w = game().vehs[w].next_veh_id_stack) {
            if (game().vehs[w].order == ORDER_SENTRY_BOARD && game().vehs[w].waypoint_x[0] == veh_id)
                v.order = ORDER_NONE;
        }
    }
    int32_t gx = tx, gy = ty;
    if (v.order == ORDER_MOVE_TO) {
        int32_t mx, my;
        move_rules::pod_meeting_point(tx, ty, v.waypoint_x[0], v.waypoint_y[0], mx, my);
        if (map_is_ocean(mx, my)) {
            gx = mx;
            gy = my;
        }
    }
    if (map_is_ocean(gx, gy)) add_goal(faction_id, 6, 2, gx, gy, -1);
}

// Long journeys: ask the viewer whether to keep moving.
void OrderVeh::keep_moving() {
    VEH& v = game().vehs[veh_id];
    v.iter_count++;
    if ((v.state & 0x10000) && dir == ((int32_t)v.rotate_angle ^ 4)) v.iter_count += 8;
    if (v.iter_count >= 0x20) {
        parse_says(0, veh_unit(veh_id).name, -1, -1);
        if (MultiplayerActive || X_pop("KEEPMOVING", 0)) {
            v.order = ORDER_NONE;
            v.state &= 0xFCFFBDFF;
        }
        v.iter_count = 0;
    }
}

// L_5962f2: no direction (dir 8): end of the unit's move.
Step OrderVeh::no_direction() {
    if (veh_chassis_triad(veh_id) == 2 && veh_chassis_range(veh_id) && viewer_order()) {
        const MAP* sq = map_tile(tx, ty);
        if (tile_base_owner(sq) < 0 && !(sq->items & BIT_AIRBASE)) {
            // An aircraft of the viewer ending its move away from a base heads home.
            bool carried = false;
            for (int32_t v = veh_id < 0 ? -1 : top_of(veh_id); v >= 0 && !carried;
                 v = game().vehs[v].next_veh_id_stack) {
                // BUG(original): tests the moving unit's abilities, not those of the stack member.
                if (has_abil(game().vehs[g_compat.carried_ability_mover ? veh_id : v].unit_id, 0x80)) carried = true;
                else if (veh_unit(v).plan == 10 && veh_chassis_triad(v) == 2) carried = true;
            }
            if (!carried && moves_left(veh_id) > 0 &&
                (veh_chassis_range(veh_id) == 1 || game().vehs[veh_id].movement_turns)) {
                action_home(veh_id, 0);
                if (game().vehs[veh_id].order == ORDER_MOVE_TO) {
                    if (MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
                    return finish(0);
                }
            }
        }
    }
    result = 1;
    if (!(VehAttackFlags & 2)) return Step::end;
    veh_skip(veh_id);
    game().vehs[veh_id].state &= ~0x40u;
    // BUG(original): the post-move code reads visible ([ebp-0x24]), never set on this path (0 here,
    // the original's slot is unset), and scroll, which holds delayed (stack residue #5). Fixed
    // (compat residue.order_veh_visible): whether the viewer sees the unit, as for a move.
    if (!g_compat.residue.order_veh_visible) move_shown(CurrentPlayerFaction);
    return Step::after_move;
}

// L_599b1a: the end of the step. A unit that could not move loses its order (AIs and units on
// alert or exploring are skipped after a while); a unit that reached its waypoint in a multiplayer
// game goes on with its patrol.
int32_t OrderVeh::end() {
    if (game().veh_count != veh_count) return 0;
    if (!result) {
        VEH& v = game().vehs[veh_id];
        bool human = (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0;
        v.order = ORDER_NONE;
        if (!human || (v.state & (VSTATE_ON_ALERT | VSTATE_EXPLORE))) {
            v.iter_count++;
            if (v.iter_count >= 0x18 || human) {
                v.iter_count = 0;
                veh_skip(veh_id);
            }
        }
        if (!(VehAttackFlags & 2) && MultiplayerActive && !ControlTurnC) net_synch_veh(veh_id);
        return 0;
    }
    if (VehAttackFlags & 2) {
        VEH& v = game().vehs[veh_id];
        if (dir >= 0 && dir < 8 && (v.state & 8) && v.x == tx && v.y == ty && !zoc_veh(tx, ty, faction_id))
            v.state &= ~8u;
        if (MultiplayerActive && !ControlTurnC && v.order == ORDER_MOVE_TO && v.x == v.waypoint_x[0] &&
            v.y == v.waypoint_y[0]) {
            v.order = ORDER_NONE;
            if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) {
                v.patrol_current_point++;
                v.order = ORDER_MOVE_TO;
                v.waypoint_x[0] = v.waypoint_x[v.patrol_current_point];
                v.waypoint_y[0] = v.waypoint_y[v.patrol_current_point];
                v.state &= 0xFDFEFDFF;
            }
        }
    }
    return result;
}
}  // namespace

int32_t GAMEAPI order_veh(int32_t veh_id, int32_t dir, int32_t flags) {
    StackPopup popup;
    OrderVeh o(veh_id, dir);
    Step step = o.begin(flags);
    for (;;) {
        switch (step) {
        case Step::start: step = o.start(); break;
        case Step::land_on_sea: step = o.land_on_sea(); break;
        case Step::zoc_check: step = o.zoc_check(); break;
        case Step::target: step = o.target(); break;
        case Step::movement: step = o.movement(); break;
        case Step::enter_base: step = o.enter_base(); break;
        case Step::move: step = o.move(); break;
        case Step::after_move: step = o.after_move(); break;
        case Step::no_direction: step = o.no_direction(); break;
        case Step::end: return o.end();
        case Step::done: return o.result;
        }
    }
}
