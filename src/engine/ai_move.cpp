// enemy_move (0x56B5B0): the AI's decision routine for one unit (ai module; kept in its own file
// for its size, about 56 KB of machine code in the original).
//
// The original is a single function whose role branches (colony pods, formers, transports, probes,
// combat units, artifacts, sea and air units, ...) share one large stack frame and jump into each
// other's code. The reconstruction keeps the original's control flow: every join point that is
// reached from more than one helper is a `Flow` value (named after the original address), and
// enemy_move runs the helpers as a small state machine. Labels L_<address> inside a helper are the
// original's local join points.
//
// All locals live in `EM`, one field per stack slot of the original frame: the compiler reused
// slots for different roles, and some paths read a slot that another role (or no code at all in
// this call) wrote. See docs/RECONSTRUCTION.md, "Known uninitialised reads" #2 and #3.
#include "engine/ai.h"

#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/base_misc.h"
#include "engine/battle.h"
#include "engine/compat.h"
#include "engine/diplomacy.h"
#include "engine/diplo_act.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/goody.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/path.h"
#include "engine/probe.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "engine/zoc.h"

namespace {

constexpr int32_t kProjectNanoFactory = 22;  // has_project ids (SP index)

// ---- small helpers (inline code of the original) ------------------------------------------------

inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
// diplo_status[other] for any index, as the original reads it (other -1: the field before).
inline uint32_t dstat_raw(int32_t faction_id, int32_t other) {
    uint32_t r;
    std::memcpy(&r,
                reinterpret_cast<const uint8_t*>(&game().factions[0].diplo_status[0]) +
                    faction_id * (int32_t)sizeof(Faction) + 4 * other,
                4);
    return r;
}
// The original has no range check here: other may be the faction of Bases[-1] (255) or a zoc
// result of 8 (fields after diplo_status, in the same record), so read through dstat_raw.
inline uint32_t dstat(int32_t faction_id, int32_t other) { return dstat_raw(faction_id, other); }
inline const UNIT& unit_of(int32_t veh_id) { return Units[game().vehs[veh_id].unit_id]; }
inline int32_t unit_plan(int32_t veh_id) { return (uint8_t)unit_of(veh_id).plan; }
inline int32_t veh_triad_of(int32_t veh_id) { return Chassis[unit_of(veh_id).chassis_id].triad; }
inline int32_t veh_offense(int32_t veh_id) { return Weapon[unit_of(veh_id).weapon_id].offense_value; }
// base_id may be -1 (no base) where the original reads Bases[-1]: the last 0x134 bytes of
// Factions[7] (plain data in the memory image), so read through the raw table.
inline const uint8_t* base_raw(int32_t base_id) {
    return reinterpret_cast<const uint8_t*>(&game().bases[0]) + base_id * (int32_t)sizeof(BASE);
}
inline int32_t base_faction(int32_t base_id) { return base_raw(base_id)[offsetof(BASE, faction_id)]; }
inline int32_t base_x_raw(int32_t base_id) {
    int16_t v;
    std::memcpy(&v, base_raw(base_id) + offsetof(BASE, x), 2);
    return v;
}
inline int32_t base_y_raw(int32_t base_id) {
    int16_t v;
    std::memcpy(&v, base_raw(base_id) + offsetof(BASE, y), 2);
    return v;
}
inline MAP* sq_at(int32_t x, int32_t y) { return map_tile(x, y); }
inline bool sq_ocean(const MAP* sq) { return (sq->climate & 0xE0) < 0x60; }
inline int32_t sq_ocean_i(const MAP* sq) { return sq_ocean(sq) ? 1 : 0; }
inline int32_t tile_region(int32_t x, int32_t y) { return map_tile(x, y)->region; }
inline int32_t base_region(int32_t base_id) { return tile_region(game().bases[base_id].x, game().bases[base_id].y); }
inline bool sq_has_base(const MAP* sq) { return (sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8; }
// Preq tech of a terraform (sea: the ocean variant).
inline int32_t tf_preq(int32_t terraform_id, int32_t sea) {
    return sea ? Terraform[terraform_id].preq_tech_sea : Terraform[terraform_id].preq_tech;
}
// The usual inline distance: (|dy| + x_dist) / 2.
inline int32_t hdist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dy = std::abs(y1 - y2);
    return (dy + x_dist(x1, x2)) >> 1;
}
// Unhalved: |dy| + x_dist.
inline int32_t hdist2(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dy = std::abs(y1 - y2);
    return dy + x_dist(x1, x2);
}
// vector_dist with the wrapped dx (x1, x2 order as pushed by the original).
inline int32_t vdist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dy = std::abs(y1 - y2);
    return vector_dist(x_dist(x1, x2), dy);
}
inline int32_t top_of(int32_t veh_id) {
    while (game().vehs[veh_id].prev_veh_id_stack >= 0) veh_id = game().vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
// The waypoint arrays as the original addresses them: the shifts below run one element past
// waypoint_x / waypoint_y (into waypoint_y[0] / morale and movement_turns).
inline int16_t wp_get(const VEH& v, int32_t i) {
    int16_t r;
    std::memcpy(&r, reinterpret_cast<const uint8_t*>(&v.waypoint_x[0]) + 2 * i, 2);
    return r;
}
inline void wp_set(VEH& v, int32_t i, int16_t value) {
    std::memcpy(reinterpret_cast<uint8_t*>(&v.waypoint_x[0]) + 2 * i, &value, 2);
}
// waypoint i <- waypoint i+1 for i < waypoint_count (x and y lists).
void wp_shift(VEH& v) {
    for (int32_t i = 0; i < v.waypoint_count; i++) {
        wp_set(v, i, wp_get(v, i + 1));
        wp_set(v, i + 4, wp_get(v, i + 5));
    }
}

// ---- state ---------------------------------------------------------------------------------------

// Join points of the original reached from more than one helper (L_<address>).
enum Flow {
    RET0,  // return 0
    RET1,  // return 1
    RETV,  // return EM::retval (alien_move)
    F_56b970,  // orders of the unit (normal mode)
    F_574a7e,  // no moves left / end of land unit planning
    F_574a88,  // sea units: transports and naval planning
    F_577071,  // return 0 if out of moves, else F_57707f
    F_57707f,  // land units: F_57708a, others F_57717f
    F_57708a,  // coast goal of pioneering units, then F_57717f
    F_57717f,  // order checks, then the tactical move (F_577361)
    F_577361,  // tactical move: score the nine directions
    F_578b29,  // step towards EM::best_dir (< 0: F_578b34)
    F_578b34,  // clear the order
    F_578f46,  // common end: idle units, waypoints reached, former/colony pod paths
    F_578d67,  // multiplayer: wait for the remote execution, return 1
    F_56c2d1,  // drop automation (order 0), return 0
    F_56c9c2,  // same (other register state)
    F_56c9e1,  // planning of AI units
    F_56cdc1,  // giving units away to friendly factions, then damage handling
    F_56e011,  // probe teams
    F_56e507,  // land units: drop pods; others F_56ed87
    F_56ed87,  // sea and air units
    F_56ef2f,  // naval hunting, artillery
    F_56f8ad,  // colony pods and exploration sites
    F_570002,  // colony pods / scouts without a site
    F_57000c,  // sea colony pods: ocean sites
    F_5702f7,  // exploration sites
    F_570c89,  // improvements to destroy, supply pods
    F_57137d,  // best_dir = -1, then F_57138d
    F_57138d,  // garrisons, defence, gates (also the hint mode entry)
    F_571977,
    F_571982,
    F_571d57,
    F_572922,  // formers
    F_5741cf,  // coast patrol, exploration
    F_57460a,  // exploring land units
    F_56e62d,
    F_575b27,  // transports: end of the passenger logic
    F_57666a,  // other sea units
    F_56e693,
};

struct EM {
    int32_t retval;
    int32_t veh;  // [ebp+8]
    int32_t tx, ty;  // [-4], [-8] (local_8, local_c): candidate tile
    int32_t fac;  // [-0xc]
    int32_t x, y;  // [-0x18], [-0x14]
    int32_t other;  // [-0x1c] local_20: unit on a tile / loop vehicle
    int32_t val;  // [-0x20] local_24: candidate score
    int32_t base_id;  // [-0x24] local_28
    int32_t best;  // [-0x28] local_2c: best score
    int32_t s2c;  // [-0x2c] local_30: loop index, counter, direction
    int32_t s30;  // [-0x30] local_34
    int32_t region;  // [-0x34] local_38
    int32_t s38;  // [-0x38] local_3c: distance temp (uninitialised read #3)
    int32_t triad;  // [-0x40] local_44
    int32_t s44;  // [-0x44] local_48: recon empath flag, later loop index
    int32_t s48;  // [-0x48] local_4c (uninitialised read #2)
    int32_t s4c;  // [-0x4c] local_50: in own territory, later temp
    int32_t plan;  // [-0x50] local_54
    int32_t s54;  // [-0x54] local_58
    int32_t s58;  // [-0x58] local_5c
    int32_t base_own;  // [-0x5c] local_60: nearest own base
    int32_t s60;  // [-0x60] local_64: territory owner of the tile, later temp
    int32_t dist_own;  // [-0x64] local_68
    int32_t s68;  // [-0x68] local_6c: faction bit, later a region
    int32_t s6c;  // [-0x6c] local_70: hint mode (negative argument), later loop index
    int32_t s70;  // [-0x70] local_74: faction temp
    int32_t at_sea;  // [-0x74] local_78: the unit's tile is ocean
    int32_t best_idx;  // [-0x78] local_7c
    int32_t dist_any;  // [-0x7c] local_80
    int32_t best_dir;  // [-0x80] local_84
    int32_t s84;  // [-0x84] local_88: next to a hostile base, later tile is ocean
    int32_t region_plan;  // [-0x88] local_8c
    int32_t stay;  // [-0x8c] local_90
    int32_t dmg;  // [-0x90] local_94: damage level 0-4
    int32_t zocv;  // [-0x94] local_98: zoc_veh (faction + 1)
    int32_t st4;  // [-0x98] local_9c: state & 0x60000 == 0x40000
    int32_t base_any;  // [-0x9c] local_a0: nearest base
    int32_t mp_own;  // [-0xa0] local_a4: multiplayer, own unit
    int32_t sa4;  // [-0xa4] local_a8: altitude, later temp
    int32_t st6;  // [-0xa8] local_ac: state & 0x60000 == 0x60000
    int32_t unit;  // [-0xac] local_b0
    int32_t st4000;  // [-0xb0] local_b4: state & 0x4000
    int32_t defend;  // [-0xb4] local_b8
    int32_t sb8;  // [-0xb8] local_bc: out of moves, later a plan
    int32_t zocm;  // [-0xbc] local_c0: zoc_move
    int32_t coast_goal;  // [-0xc0] local_c4
    int32_t keep_wp;  // [-0xc4] local_c8
    int32_t ys[64];  // [-0x1c4] local_1c8
    int32_t xs[64];  // [-0x2c4] local_2c8
    VEH& v() const { return game().vehs[veh]; }
    Faction& f() const { return game().factions[fac]; }
    bool human() const { return is_human(fac); }
};

Flow em_auto_roads(EM& m);
Flow em_auto_former(EM& m, uint32_t state);

// ---- common end (0x578B29-0x579207) --------------------------------------------------------------

// L_578b29: step in direction best_dir (an "AI move" order 0x58 to the neighbour), else clear.
Flow em_step(EM& m) {
    if (m.best_dir >= 0) {
        VEH& v = m.v();
        int32_t tx = map_wrap_x(m.x + NearOffsetX[m.best_dir]);
        int32_t ty = NearOffsetY[m.best_dir] + m.y;
        v.order = ORDER_AI_GO_TO;
        v.waypoint_x[0] = (int16_t)tx;
        m.tx = tx;
        m.ty = ty;
        v.waypoint_y[0] = (int16_t)ty;
        return F_578f46;
    }
    return F_578b34;
}

// L_578b34
Flow em_clear_order(EM& m) {
    VEH& v = m.v();
    v.state &= ~0x10000u;
    v.order = ORDER_NONE;
    return F_578f46;
}

// L_578f46: idle units hold or sentry, reached waypoints end the order; colony pods and formers
// on a waypoint order check their next step for adjacent enemies.
int32_t em_finish(EM& m) {
    VEH& v = m.v();
    if ((int8_t)v.order < ORDER_MOVE_TO) v.state &= ~0x10000u;
    if (v.order == ORDER_NONE || v.order == ORDER_HOLD) {
        if (m.triad == TRIAD_AIR && m.dist_own != 0 && !(sq_at(m.x, m.y)->items & BIT_AIRBASE)) {
            action_home(m.veh, 0);
        } else {
            v.order = (!m.human() || (v.state & VSTATE_ON_ALERT)) ? 2 : 0;
            veh_skip(m.veh);
        }
    }
    if (v.order == ORDER_MOVE_TO && v.waypoint_x[0] == m.x && v.waypoint_y[0] == m.y) {
        v.order = ORDER_NONE;
        veh_skip(m.veh);
    }
    uint8_t order = v.order;
    if ((order == 0x18 || order == 0x58) && (m.plan == PLAN_TERRAFORM || m.plan == PLAN_COLONY)) {
        int32_t faction = (order & 0x40) ? -1 : (int32_t)v.faction_id;
        int32_t dir = Path_move(&Paths, m.veh, faction);
        if (dir < 0) {
            v.order = ORDER_NONE;
            return 0;
        }
        m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
        m.ty = NearOffsetY[dir] + m.y;
        if (base_who(m.tx, m.ty) >= 0) return 0;
        for (int32_t i = 0; i < 8; i++) {
            int32_t ax = map_wrap_x(m.tx + NearOffsetX[i]);
            int32_t ay = NearOffsetY[i] + m.ty;
            if (!on_map(ax, ay)) continue;
            if (sq_ocean(sq_at(ax, ay)) != sq_ocean(sq_at(m.tx, m.ty))) continue;
            m.s4c = std::abs(ay - m.y);
            if (((m.s4c + x_dist(ax, m.x)) & ~1) <= 2) continue;
            m.other = stack_fix(veh_at(ax, ay));
            if (m.other < 0) continue;
            int32_t of = game().vehs[m.other].faction_id;
            if (of == m.fac || !(dstat(m.fac, of) & 0x10)) continue;
            if (veh_offense(m.other) != 0 || stack_check(m.other, 0xE, -1, -1, -1)) {
                v.order = ORDER_NONE;
                return 0;
            }
        }
        return 0;
    }
    return 0;
}

// ---- entry ---------------------------------------------------------------------------------------

// 0x56B5B0-0x56B970: unit, tile, damage level, nearest bases. Returns RETV for native units.
Flow em_setup(EM& m, int32_t veh_id) {
    m.best_dir = -1;
    m.s84 = 0;
    m.keep_wp = 0;
    m.defend = 0;
    m.stay = 0;
    m.coast_goal = 0;
    m.sb8 = 0;
    if (veh_id < 0) {
        // Hint mode (Console_give_hints passes -1 - veh_id): only the garrison checks run.
        veh_id = -1 - veh_id;
        m.s6c = 1;
    } else {
        m.s6c = 0;
    }
    m.veh = veh_id;
    m.mp_own = (MultiplayerActive && game().vehs[veh_id].faction_id == (uint32_t)CurrentPlayerFaction) ? 1 : 0;
    do_all_non_input();
    VEH& v = game().vehs[veh_id];
    if (v.faction_id == 0 && !m.mp_own) {
        m.retval = alien_move(veh_id);
        return RETV;
    }
    m.unit = v.unit_id;
    m.fac = v.faction_id;
    m.y = v.y;
    m.x = v.x;
    m.plan = (uint8_t)Units[m.unit].plan;
    m.triad = Chassis[Units[m.unit].chassis_id].triad;
    MAP* sq = sq_at(m.x, m.y);
    m.sa4 = sq->climate >> 5;
    m.at_sea = sq_ocean_i(sq);
    m.region = sq->region;
    m.st4 = (v.state & 0x60000) == 0x40000;
    m.st6 = (v.state & 0x60000) == 0x60000;
    m.s44 = (m.plan == PLAN_RECON && has_abil(m.unit, ABL_EMPATH)) ? 1 : 0;
    m.region_plan = m.f().region_base_plan[m.region];
    int32_t damage = v.damage_taken;
    int32_t max_hp = Units[v.unit_id].plan == PLAN_ARTIFACT ? 1 : range(Units[v.unit_id].reactor_id, 1, 100) * 10;
    if (damage >= max_hp / 2) {
        m.dmg = 4;
    } else {
        max_hp = Units[v.unit_id].plan == PLAN_ARTIFACT ? 1 : range(Units[v.unit_id].reactor_id, 1, 100) * 10;
        if (damage > max_hp / 3) m.dmg = 3;
        else if (damage > Units[v.unit_id].reactor_id * 2) m.dmg = 2;
        else m.dmg = damage != 0;
    }
    if (planet_buster(m.veh)) m.dmg = 0;
    if (m.unit == BSC_BATTLE_OGRE_MK1 || m.unit == BSC_BATTLE_OGRE_MK2 || m.unit == BSC_BATTLE_OGRE_MK3) m.dmg = 0;
    m.zocm = zoc_move(m.x, m.y, m.fac);
    m.zocv = zoc_veh(m.x, m.y, m.fac);
    m.base_any = base_find_filtered(m.x, m.y, -1, m.triad ? -1 : m.region, -1, -1);
    m.dist_any = BaseFindDist;
    m.base_own = base_find_filtered(m.x, m.y, m.fac, m.triad ? -1 : m.region, -1, -1);
    m.dist_own = BaseFindDist;
    m.s60 = whose_territory(m.fac, m.x, m.y, nullptr, 0);
    m.s4c = m.s60 == m.fac;
    m.st4000 = v.state & VSTATE_EXPLORE;
    return m.s6c ? F_57138d : F_56b970;
}

// 0x578DE9: waypoint lists (patrols and queued waypoints).
Flow em_waypoint_list(EM& m) {
    VEH& v = m.v();
    if (v.order == ORDER_MOVE_TO) return F_578f46;
    uint32_t state = v.state;
    if ((state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count &&
        (state & 0x1000200) != 0x1000200) {
        v.order = ORDER_MOVE_TO;
        wp_shift(v);
        if (--v.waypoint_count == 0) v.state &= 0xFDFEFDFFu;
        return F_578f46;
    }
    int32_t wx = v.waypoint_x[0], wy = v.waypoint_y[0];
    m.tx = wx;
    m.ty = wy;
    wp_shift(v);
    wp_set(v, v.waypoint_count, (int16_t)wx);
    wp_set(v, v.waypoint_count + 4, (int16_t)wy);
    v.state &= ~0x10000u;
    if (v.waypoint_x[0] == wx && v.waypoint_y[0] == wy && v.waypoint_count == 1) {
        v.order = ORDER_NONE;
        v.state &= 0xFEFFFDFFu;
    } else {
        v.order = ORDER_MOVE_TO;
    }
    return F_578f46;
}

// 0x56B976-0x56BE20: orders of units that are not planned here (ZOC escapes, stacks waking up
// from an attack target, waypoint lists).
Flow em_orders(EM& m) {
    VEH& v = m.v();
    if ((v.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) && !m.human()) {
        veh_skip(m.veh);
        return RET0;
    }
    if (range(speed(m.veh, 0) - v.moves_spent, 0, 999) == 0) {
        m.sb8 = 1;
        return F_574a7e;
    }
    int8_t order = (int8_t)v.order;
    if (order >= 4 && order < 0x18) {
        // Terraforming or similar: stay unless an enemy zone of control threatens.
        if (m.zocv == 0) return F_578f46;
        if (m.zocv > 1 && (!m.human() || !(dstat(m.fac, m.zocv - 1) & 0x10))) return F_578f46;
        if ((v.state & VSTATE_ON_ALERT) &&
            (v.order_auto_type == ORDERA_TERRA_FARM_SOLAR_ROAD || v.order_auto_type == ORDERA_TERRA_FARM_MINE_ROAD))
            return F_56c2d1;
        for (int32_t dir = 0; dir < 8; dir++) {
            int32_t tx = map_wrap_x(m.x + NearOffsetX[dir]);
            int32_t ty = NearOffsetY[dir] + m.y;
            m.tx = tx;
            m.ty = ty;
            if (ty < 0 || ty >= game().map.y || tx < 0 || tx >= game().map.x) continue;
            if (m.triad != TRIAD_AIR && sq_ocean_i(sq_at(tx, ty)) != (m.triad == TRIAD_SEA)) continue;
            MAP* sq = sq_at(tx, ty);
            if (!(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE))) {
                m.s70 = -1;
            } else {
                int32_t owner = sq->val2 & 0xF;
                if (owner >= 8) {
                    m.s70 = -1;
                } else {
                    m.s70 = owner;
                    if (owner != m.fac && !(dstat(m.fac, owner) & 1)) continue;
                }
            }
            if (zoc_veh(tx, ty, m.fac)) continue;
            if (m.zocm && zoc_move(tx, ty, m.fac)) continue;
            m.best_dir = dir;
            return F_578b29;
        }
        if (v.state & VSTATE_ON_ALERT) return F_56c9c2;
    }
    // L_56bbd5: a stack that reached its attack target wakes up when an enemy is adjacent.
    bool reached = false;
    if (m.zocv > 1 && m.st4 && (dstat(m.fac, m.zocv - 1) & 0x10)) {
        m.st4 = 0;
        v.state &= ~0x40000u;
        if (m.x == m.f().target_x && m.y == m.f().target_y) {
            m.f().target_x = -1;
            m.st6 = 1;
            m.other = top_of(m.veh);
            for (int32_t o = m.other; o >= 0;) {
                if (o == m.veh || (game().vehs[o].state & 0x40000)) game().vehs[o].state |= 0x60000;
                o = game().vehs[o].next_veh_id_stack;
                m.other = o;
            }
            reached = true;
        }
    }
    if (reached || m.st6) {
        // L_56bcf8
        if ((int32_t)v.moves_spent >= Rules.move_rate_roads && m.zocv == 0 && !m.human() &&
            stack_check(m.veh, 0xE, -1, -1, -1)) {
            veh_skip(m.veh);
            return F_578f46;
        }
    }
    // L_56bd5d
    if (m.st4 && v.x == m.f().target_x && v.y == m.f().target_y) {
        m.defend = 1;
        m.stay = 1;
        return F_57707f;
    }
    uint32_t state = v.state;
    if ((state & 0x1000200) == 0x1000200 ||
        ((state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count))
        return em_waypoint_list(m);
    m.s68 = 1 << m.fac;
    if (!m.human() && (v.flags & 0x800)) {
        if (has_tech(Terraform[6].preq_tech, m.fac)) v.order_auto_type = ORDERA_TERRA_AUTO_MAGTUBE;
        return em_auto_roads(m);
    }
    if (!(state & 0x200) || m.plan != PLAN_TERRAFORM) return F_56c9e1;
    return em_auto_former(m, state);
}

// L_56c2d1 / L_56c9c2: the automation gives up.
Flow em_drop_automation(EM& m) {
    VEH& v = m.v();
    v.order = ORDER_NONE;
    v.state &= ~VSTATE_ON_ALERT;
    return RET0;
}

// L_56c30f: automated road / mag tube building (order_auto_type 1 / 2, also AI units with
// flags 0x800): connect the nearest base of the region whose road (tube) link is incomplete.
Flow em_auto_roads(EM& m) {
    VEH& v = m.v();
    if (v.order == ORDER_ROAD_TO || v.order == ORDER_MAGTUBE_TO) {
        int32_t b = base_at(v.waypoint_x[0], v.waypoint_y[0]);
        if (b >= 0 && base_faction(b) == m.fac) return F_578f46;
        v.order = ORDER_NONE;
    }
    m.best = 9999;
    m.s30 = (v.order_auto_type == ORDERA_TERRA_AUTO_MAGTUBE) + 0x1B;
    m.best_idx = -1;
    m.base_id = 0;
    for (; m.base_id < game().base_count; m.base_id++) {
        int32_t b = m.base_id;
        int32_t bf = base_faction(b);
        if (bf != m.fac) {
            if (!(dstat(m.fac, bf) & 1)) continue;
            if (m.dist_any == 0 && base_faction(m.base_any) != m.fac) continue;
        }
        m.s58 = game().bases[b].y;
        int32_t bx = game().bases[b].x;
        if (tile_region(bx, m.s58) != m.region) continue;
        int32_t px = m.x, py = m.y;
        m.s54 = 1;
        m.s2c = 0;
        m.s38 = 0;
        bool evaluate = false;
        for (;;) {
            int32_t flags =
                (has_tech(Rules.tech_preq_build_road_fungus, m.fac) || has_tech(Terraform[10].preq_tech, m.fac)) ? 0
                                                                                                                 : 4;
            int32_t dir = Path_find(&Paths, px, py, bx, m.s58, 0, -1, flags, -1);
            m.best_dir = dir;
            if (dir < 0) break;
            px = map_wrap_x(px + NearOffsetX[dir]);
            py += NearOffsetY[dir];
            m.s38++;
            if (px == bx && py == m.s58) break;
            MAP* sq = sq_at(px, py);
            if (!sq_has_base(sq)) {
                if (!(sq->items & BIT_ROAD) ||
                    (!(sq->items & BIT_MAGTUBE) && v.order_auto_type == ORDERA_TERRA_AUTO_MAGTUBE))
                    m.s54 = 0;
            }
            if (++m.s2c > 100) break;
            if (!m.s54) {
                evaluate = true;
                break;
            }
        }
        if (!evaluate && m.s54) continue;
        if (bf != m.fac) m.s38 *= 2;
        int32_t owner = whose_territory(m.fac, px, py, nullptr, 0);
        if (owner >= 0 && owner != m.fac && !(dstat(m.fac, owner) & 1)) m.s38 += 100;
        if (zoc_sea(px, py, m.fac)) m.s38 += 10;
        if (m.s38 < m.best) {
            m.best = m.s38;
            m.best_idx = m.base_id;
        }
    }
    if (m.best_idx >= 0) {
        v.status_icon = 0x52;
        v.order = (uint8_t)m.s30;
        v.waypoint_x[0] = game().bases[m.best_idx].x;
        v.waypoint_y[0] = game().bases[m.best_idx].y;
        return F_578f46;
    }
    if (!m.human() && (v.flags & 0x800)) return F_56c9e1;
    v.order = ORDER_NONE;
    v.state &= ~VSTATE_ON_ALERT;
    veh_skip(m.veh);
    if (m.fac != CurrentPlayerFaction) return RET0;
    Console_focus(&MainConsole, m.x, m.y, m.fac);
    X_pop(v.order_auto_type == ORDERA_TERRA_AUTO_ROAD ? "AUTOROADSDONE" : "AUTOTUBESDONE", 0);
    return RET0;
}

// 0x56C6D3: automated farming / mining (order_auto_type 4 / 5) of the current tile.
Flow em_auto_improve(EM& m) {
    VEH& v = m.v();
    MAP* sq = sq_at(m.x, m.y);
    if ((sq->climate & 0xE0) < 0x40) return F_56c9c2;
    m.s4c = (int32_t)sq->items;
    if (sq->items & BIT_MONOLITH) return F_56c9c2;
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return F_56c9c2;
    uint32_t items = (uint32_t)m.s4c;
    if (items & 0x20) {
        if (has_tech(tf_preq(10, m.at_sea), m.fac)) {
            v.order = ORDER_REMOVE_FUNGUS;
            return F_578f46;
        }
    } else if (!m.at_sea && !(items & 0x8000) && (sq->val3 & 0xC0) < 0x80 &&
               ((sq->climate & 0x18) < 0x10 || (m.sa4 - 2 >= 0 && m.sa4 - 2 > 1))) {
        if (has_tech(Terraform[0].preq_tech, m.fac)) {
            v.order = ORDER_FARM;
            return F_578f46;
        }
    }
    sq = sq_at(m.x, m.y);
    if (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40 || has_tech(Rules.tech_preq_improv_fungus, m.fac)) {
        if (v.order_auto_type == ORDERA_TERRA_FARM_SOLAR_ROAD && !(sq_at(m.x, m.y)->items & BIT_SOLAR) &&
            energy_yield(m.fac, -1, m.x, m.y, 1) && has_tech(tf_preq(3, m.at_sea), m.fac)) {
            v.order = ORDER_SOLAR_COLLECTOR;
            return F_578f46;
        }
        if (v.order_auto_type == ORDERA_TERRA_FARM_MINE_ROAD && !(sq_at(m.x, m.y)->items & BIT_MINE) &&
            has_tech(tf_preq(2, m.at_sea), m.fac)) {
            v.order = ORDER_MINE;
            return F_578f46;
        }
    }
    sq = sq_at(m.x, m.y);
    if (!(sq->items & BIT_ROAD)) {
        if (m.at_sea) return F_56c9c2;
        if ((!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40 ||
             has_tech(Rules.tech_preq_build_road_fungus, m.fac)) &&
            has_tech(Terraform[5].preq_tech, m.fac)) {
            v.order = ORDER_ROAD;
            return F_578f46;
        }
    } else if (m.at_sea) {
        return F_56c9c2;
    }
    sq = sq_at(m.x, m.y);
    if ((sq->items & BIT_FARM) || (sq->val3 & 0xC0) >= 0x80 || !has_tech(Terraform[0].preq_tech, m.fac))
        return F_56c9c2;
    v.order = ORDER_FARM;
    return F_578f46;
}

// 0x56BE29: automated formers (order_auto_type: 3 base area, 4/5 farm/mine, 6/fungicidal
// fungus removal, 7 sensors); state 0x200 is "automated".
Flow em_auto_former(EM& m, uint32_t state) {
    VEH& v = m.v();
    uint8_t at = v.order_auto_type;
    if (at == 3) {
        m.base_id = v.home_base_id;
        if (m.base_id >= 0) return F_572922;
        m.base_id = m.base_own;
        if (m.base_id >= 0) return F_572922;
        v.state = state & ~0x200u;
        veh_skip(m.veh);
        return RET0;
    }
    if (at == 4 || at == 5) return em_auto_improve(m);
    if (at == 1) return em_auto_roads(m);
    if (at == 2) {
        if (!has_tech(Terraform[6].preq_tech, m.fac)) v.order_auto_type = ORDERA_TERRA_AUTO_ROAD;
        return em_auto_roads(m);
    }
    if (at != 6 && !(has_abil(v.unit_id, ABL_FUNGICIDAL) && v.order_auto_type == ORDERA_TERRA_AUTO_FULL &&
                     (GameMorePreferences & MPREF_AUTO_FORMER_REMOVE_FUNGUS))) {
        if (v.order_auto_type != ORDERA_TERRA_AUTO_SENSOR) return F_56c9e1;
        if (good_sensor(m.fac, m.x, m.y)) {
            v.order = ORDER_SENSOR_ARRAY;
            return F_578f46;
        }
        m.tx = m.x;
        m.ty = m.y;
        if (Path_sensors(&Paths, m.fac, &m.tx, &m.ty)) return F_56c2d1;
        if (m.tx == m.x && m.ty == m.y) {
            v.order = ORDER_SENSOR_ARRAY;
            return F_578f46;
        }
        go_to(m.veh, 0x73, m.tx, m.ty);
        return RET0;
    }
    // L_56bf5f: fungus removal.
    if (!has_tech(tf_preq(10, m.triad == TRIAD_SEA), m.fac) && !has_abil(v.unit_id, ABL_FUNGICIDAL) &&
        v.order_auto_type == ORDERA_TERRA_AUTO_FUNGUS_REM)
        return F_56c2d1;
    if (v.order == ORDER_MOVE_TO) return F_578f46;
    MAP* sq = sq_at(m.x, m.y);
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) {
        v.order = ORDER_REMOVE_FUNGUS;
        return F_578f46;
    }
    if (m.triad == TRIAD_SEA && !sq_ocean(sq)) return F_57717f;
    m.best = 99999;
    m.s48 = 2;
    for (;;) {
        int32_t limit = v.order_auto_type == ORDERA_TERRA_AUTO_FUNGUS_REM ? 6 : 3;
        if (m.s48 > limit) break;
        if (v.order == ORDER_MOVE_TO) return F_578f46;
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            if (m.triad == TRIAD_SEA ? !base_on_sea(m.base_id, m.region) : tile_region(base.x, base.y) != m.region)
                continue;
            int32_t i = m.s48 == 2 ? 1 : TableRange[m.s48 - 1];
            for (; i < TableRange[m.s48]; i++) {
                int32_t tx = map_wrap_x(TableOffsetX[i] + base.x);
                int32_t ty = TableOffsetY[i] + base.y;
                m.tx = tx;
                m.ty = ty;
                if (ty < 0 || ty >= game().map.y || tx < 0 || tx >= game().map.x) continue;
                MAP* t = sq_at(tx, ty);
                if (t->region != m.region || !(t->items & BIT_FUNGUS) || (t->climate & 0xE0) < 0x40) continue;
                if (!(t->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE))) {
                    m.s70 = -1;
                } else {
                    int32_t owner = t->val2 & 0xF;
                    if (owner >= 8) {
                        m.s70 = -1;
                    } else {
                        m.s70 = owner;
                        // BUG(original): reads [ebp-0x38], not written on this path in this call.
                        COMPAT_ORIGINAL(residue.enemy_move_s38);
                        if (owner != m.fac || m.s38 != 0) continue;
                    }
                }
                int32_t d = vdist(m.x, m.y, tx, ty);
                if (!zoc_move(tx, ty, m.fac) && d < m.best) {
                    m.best = d;
                    v.order = ORDER_MOVE_TO;
                    v.status_icon = 0x66;
                    v.waypoint_x[0] = (int16_t)tx;
                    v.waypoint_y[0] = (int16_t)ty;
                    if (unit_plan(m.veh) == PLAN_COLONY) v.movement_turns = 0;
                }
            }
        }
        m.s48++;
    }
    if (v.order == ORDER_MOVE_TO) return F_578f46;
    if (v.order_auto_type == ORDERA_TERRA_AUTO_FUNGUS_REM) return F_56c2d1;
    return F_56c9e1;
}

// ---- planning of AI units (0x56C9E1-0x56E011) ---------------------------------------------------

Flow em_damaged(EM& m);

// L_56c9e1: next to a hostile base, units in a treaty partner's territory, artifacts, supply.
Flow em_plan(EM& m) {
    VEH& v = m.v();
    int32_t ba = m.base_any;
    if (m.dist_any == 1) {
        int32_t bf = base_faction(ba);
        if (bf != m.fac && m.triad != TRIAD_AIR && veh_offense(m.veh) != 0) {
            MAP* bsq = sq_at(game().bases[ba].x, game().bases[ba].y);
            bool defended = (bsq->items & BIT_VEH_IN_TILE) && (bsq->val2 & 0xF) < 8;
            if (!defended && !(dstat(m.fac, bf) & 7) && sq_ocean_i(bsq) == (m.triad == TRIAD_SEA) &&
                m.plan <= PLAN_TERRAFORM) {
                // An empty base of a faction without any treaty: attack it.
                m.s84 = 1;
                m.dmg = 0;
                v.order = ORDER_NONE;
            }
        }
    }
    if (ba != m.base_own) {
        int32_t bf = base_faction(ba);
        uint32_t d = dstat(m.fac, bf);
        if ((d & 2) && !m.at_sea && !(d & 1) && v.order != ORDER_MOVE_TO) {
            // In a treaty partner's territory: go home (or step aside onto an own stack).
            if (whose_territory(m.fac, m.x, m.y, nullptr, 0) == bf && m.f().diplo_friction[bf] < 10 &&
                !(dstat(m.fac, bf) & 0xC00) &&
                ((v.faction_id == (uint32_t)bf && (v.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
                 (v.visibility & (uint8_t)(1u << bf)))) {
                if (m.base_own >= 0 && base_region(m.base_own) == m.region) {
                    go_to(m.veh, 0x77, game().bases[m.base_own].x, game().bases[m.base_own].y);
                    return F_57707f;
                }
                if (veh_triad_of(m.veh) == TRIAD_LAND) {
                    for (int32_t dir = 0; dir < 8; dir++) {
                        m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
                        m.ty = NearOffsetY[dir] + m.y;
                        if (!on_map(m.tx, m.ty) || !sq_ocean(sq_at(m.tx, m.ty))) continue;
                        m.other = stack_fix(veh_at(m.tx, m.ty));
                        if (m.other < 0 || game().vehs[m.other].faction_id != (uint32_t)m.fac) continue;
                        if (stack_check(m.other, 8, -1, -1, -1) > 0) {
                            m.best_dir = dir;
                            return F_578b29;
                        }
                    }
                }
            }
        }
    }
    // L_56cd0c
    if ((v.state & VSTATE_ON_ALERT) && v.order_auto_type == ORDERA_ON_ALERT && m.triad != TRIAD_AIR) {
        m.stay = 1;
        return F_57707f;
    }
    if (v.status_icon == 0x46) {
        if (v.order != ORDER_MOVE_TO) v.status_icon = 0x2D;
        if ((v.state & VSTATE_MONOLITH_UPGRADED) && m.plan == PLAN_DEFENSE) {
            // Garrison unit called home.
            v.status_icon = 0x2D;
            if (v.home_base_id >= 0) {
                m.base_id = v.home_base_id;
                BASE& home = game().bases[m.base_id];
                if (m.x == home.x && m.y == home.y) {
                    home.state_flags &= 0xFBFFFFFF;
                } else if (tile_region(home.x, home.y) == m.region) {
                    go_to(m.veh, 0x46, home.x, home.y);
                    return F_57707f;
                }
            }
            return F_56cdc1;
        }
    }
    // L_56cfc4
    if (m.plan == PLAN_ARTIFACT) {
        if (!(v.flags & VFLAG_IS_OBJECTIVE)) {
            if (m.dist_own == 0) {
                if (m.mp_own) {
                    veh_skip(m.veh);
                    synch_veh(m.veh);
                    return RET1;
                }
                int32_t b = m.base_own;
                if (((has_fac(FAC_NETWORK_NODE, b, 0) && !(game().bases[b].state_flags & BSTATE_ARTIFACT_LINKED)) ||
                     SecretProjects[FAC_UNIVERSAL_TRANSLATOR - SP_ID_First] == b) &&
                    study_artifact(m.veh))
                    return RET1;
            }
            int32_t b = m.base_own;
            if (!m.human() && m.dist_own == 0 && game().bases[b].queue_items[0] <= -70 &&
                game().bases[b].minerals_accumulated >= 100) {
                // Add to a secret project.
                int32_t minerals = Units[v.unit_id].cost * 10 / 2 + game().bases[b].minerals_accumulated;
                game().bases[b].minerals_accumulated = minerals;
                game().bases[b].minerals_accumulated_2 = minerals;
                game().bases[b].production_id_last = game().bases[b].queue_items[0];
                kill(m.veh);
                return RET1;
            }
        }
        m.best = 9999;
        m.best_idx = -1;
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            if (tile_region(base.x, base.y) != m.region) continue;
            int32_t d = vdist(m.x, m.y, base.x, base.y);
            if (SecretProjects[FAC_UNIVERSAL_TRANSLATOR - SP_ID_First] != m.base_id &&
                (base.queue_items[0] >= -70 || base.minerals_accumulated < 100 || m.human())) {
                if (!has_fac(FAC_NETWORK_NODE, m.base_id, 0)) d += 1000;
                if (base.state_flags & BSTATE_ARTIFACT_LINKED) {
                    d += 2000;
                } else {
                    int32_t n = stack_check(veh_at(base.x, base.y), 2, PLAN_ARTIFACT, -1, -1);
                    if (n > ((m.x == base.x && m.y == base.y) ? 1 : 0)) d += 2000;
                }
            }
            if (d < m.best) {
                m.best = d;
                m.best_idx = m.base_id;
            }
        }
        if (m.best_idx < 0) return F_57707f;
        go_to(m.veh, 0x74, game().bases[m.best_idx].x, game().bases[m.best_idx].y);
        return F_578f46;
    }
    if (m.plan != PLAN_SUPPLY) return F_56cdc1;
    if (!m.human()) {
        m.best = 9999;
        m.best_idx = -1;
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            if (tile_region(base.x, base.y) != m.region) continue;
            m.s4c = m.x;
            int32_t d = hdist(m.x, m.y, base.x, base.y);
            if (base.queue_items[0] <= -70 && d < m.best) {
                m.best = d;
                m.best_idx = m.base_id;
                if (d == 0) {
                    // At a base building a secret project: contribute the crawler.
                    BASE& b = game().bases[m.best_idx];
                    int32_t minerals = b.minerals_accumulated + veh_cost(v.unit_id, -1, nullptr) * 10;
                    b.minerals_accumulated = minerals;
                    b.minerals_accumulated_2 = minerals;
                    b.production_id_last = b.queue_items[0];
                    kill(m.veh);
                    return RET1;
                }
            }
        }
        if (m.best_idx >= 0) {
            go_to(m.veh, 0x56, game().bases[m.best_idx].x, game().bases[m.best_idx].y);
            return F_57707f;
        }
    }
    // L_56d3d1
    if (v.order == ORDER_CONVOY) {
        // Convoying: ask for formers to improve the tile.
        MAP* sq = sq_at(m.x, m.y);
        switch (v.order_auto_type) {
        case 0:
            if (sq->items & BIT_FARM) return F_578f46;
            if ((sq->val3 & 0xC0) >= 0x80) return F_578f46;
            break;
        case 1:
            if (sq->items & BIT_MINE) return F_578f46;
            break;
        case 2:
            if (sq->items & BIT_SOLAR) return F_578f46;
            if (m.sa4 - 2 <= 0) return F_578f46;
            break;
        default: return F_578f46;
        }
        add_goal(m.fac, 9, 1, m.x, m.y, -1);
        return F_578f46;
    }
    if (v.order == ORDER_MOVE_TO) return F_57707f;
    m.best = 0;
    for (m.s44 = 0; m.s44 < 0x484; m.s44 += 4) {
        int32_t i = m.s44 / 4;
        m.tx = map_wrap_x(m.x + TableOffsetX[i]);
        m.ty = TableOffsetY[i] + m.y;
        if (!on_map(m.tx, m.ty)) continue;
        MAP* sq = sq_at(m.tx, m.ty);
        if (sq->region != m.region) continue;
        if (m.triad != TRIAD_AIR && sq_ocean_i(sq) != (m.triad == TRIAD_SEA)) continue;
        if (!can_convoy(m.tx, m.ty)) continue;
        if (!bonus_at(m.tx, m.ty)) {
            MAP* t = sq_at(m.tx, m.ty);
            if ((t->val3 & 0xC0) < 0x80 && (t->climate & 0xE0) < 0xC0) continue;
        }
        if (whose_territory(m.fac, m.tx, m.ty, nullptr, 0) != m.fac) continue;
        base_find_faction(m.tx, m.ty, m.fac);
        if (BaseFindDist <= 2) continue;
        bool taken = false;
        int32_t o = 0;
        for (; o < game().veh_count; o++) {
            const VEH& w = game().vehs[o];
            if (w.faction_id != (uint32_t)m.fac || Units[w.unit_id].plan != PLAN_SUPPLY || o == m.veh) continue;
            if ((w.x == m.tx && w.y == m.ty && w.order == 3) ||
                (w.order == 0x18 && v.waypoint_x[0] == m.tx && v.waypoint_y[0] == m.ty)) {
                taken = true;
                break;
            }
        }
        m.other = o;
        if (taken) continue;
        int32_t d = hdist(m.x, m.y, m.tx, m.ty);
        int32_t best_yield = 1;
        m.best_idx = 0;
        for (int32_t type = 0; type < 3; type++) {
            int32_t yield = resource_yield(type, m.fac, v.home_base_id, m.tx, m.ty) * 2;
            if (type == 1) yield++;
            if (yield >= best_yield) {
                best_yield = yield;
                m.best_idx = type;
            }
        }
        int32_t score = (best_yield << 9) - d;
        if (score > m.best) {
            m.best = score;
            go_to(m.veh, 0x76, m.tx, m.ty);
            if (m.s44 == 0) {
                v.order = ORDER_CONVOY;
                v.order_auto_type = (uint8_t)m.best_idx;
                return F_578f46;
            }
        }
    }
    return F_57707f;
}

// L_56cdc1: units in the territory of a friendly faction may be given to it.
Flow em_gift(EM& m) {
    VEH& v = m.v();
    int32_t t = m.s60;
    m.s70 = t;
    if (m.human() || t == m.fac || t <= 0) return em_damaged(m);
    m.s48 = m.fac * 0x833 + t;
    uint32_t d = dstat(m.fac, t);
    if (!(d & 8)) return em_damaged(m);
    if (veh_offense(m.veh) <= 2 && m.plan != PLAN_DEFENSE) return em_damaged(m);
    if (m.plan == PLAN_RECON) return em_damaged(m);
    if (d & 0x40C30) return em_damaged(m);
    if (!(d & 3)) return em_damaged(m);
    if (game().factions[t].integrity_blemishes > 2) return em_damaged(m);
    m.s70 = t;
    if (v.order == ORDER_MOVE_TO) {
        int32_t t2 = whose_territory(m.fac, v.waypoint_x[0], v.waypoint_y[0], nullptr, 0);
        if (t2 != m.fac && t2 != t && t2 > 0 && m.plan <= PLAN_COMBAT && m.triad == TRIAD_LAND &&
            (game().factions[t].ranking < m.f().ranking ||
             (int32_t)(((~dstat(m.fac, t) & 1) | 2) * game().factions[t].mil_strength_1) / 2 <
                 game().factions[t2].mil_strength_1) &&
            (dstat(m.fac, t2) & 0x10) && (dstat(t, t2) & 0x10)) {
            action_give(m.veh, t);
            return RET1;
        }
    }
    // L_56d762
    uint8_t their_plan = game().factions[t].region_base_plan[m.region];
    if (their_plan == 2 && m.plan <= PLAN_DEFENSE && (dstat(m.fac, t) & 1)) {
        action_give(m.veh, t);
        return RET1;
    }
    if (m.f().region_total_bases[m.region] == 0 && game().factions[t].region_total_bases[m.region] != 0 &&
        m.triad == TRIAD_LAND) {
        bool give = false;
        if (their_plan == 2 && ((dstat(m.fac, t) & 1) || game().factions[t].ranking < 7)) give = true;
        else if (their_plan != 8 && (dstat(m.fac, t) & 1)) give = true;
        if (give) {
            action_give(m.veh, t);
            return RET1;
        }
    }
    return em_damaged(m);
}

// L_56d7fc: damaged units retreat to a base (or a monolith / transport) to repair.
Flow em_damaged(EM& m) {
    VEH& v = m.v();
    int32_t dmg = m.dmg;
    if (dmg == 0 || (m.st6 && dmg <= 3)) return F_56e011;
    int32_t ba = m.base_any;
    if (m.dist_any == 0 || (m.triad == TRIAD_AIR && (sq_at(m.x, m.y)->items & BIT_AIRBASE))) {
        if ((!(v.state & 8) && !(game().bases[ba].state_flags & 0x200000)) ||
            (dmg >= 4 && veh_moves(m.veh) <= Rules.move_rate_roads)) {
            v.status_icon = 0x64;
            m.stay = 1;
            m.defend = 1;
            return F_57138d;
        }
    }
    if (has_project(kProjectNanoFactory, m.fac) && m.s84 == 0 && m.triad != TRIAD_AIR && v.moves_spent == 0 &&
        (!m.zocm || dmg > 3) && !(v.state & 8)) {
        v.status_icon = 0x64;
        m.stay = 1;
        return m.triad == TRIAD_SEA ? F_574a88 : F_57707f;
    }
    int32_t bo = m.base_own;
    if (m.dist_own == 1 && (dmg >= 4 || !(game().bases[bo].state_flags & 0x200000))) {
        go_to(m.veh, 100, game().bases[bo].x, game().bases[bo].y);
        return F_578f46;
    }
    bool go_any = false;
    if (m.dist_any <= (m.triad + 1) * (m.triad + 1) * dmg * 2 && !(game().bases[ba].state_flags & 0x200000) &&
        get_there(m.veh, game().bases[ba].x, game().bases[ba].y) && ba == bo)
        go_any = true;
    if (!go_any) {
        m.s58 = ba * 0x134;
        m.s30 = m.fac * 0x833;
        if (dstat(m.fac, base_faction(ba)) & 1) go_any = true;
    }
    if (go_any) {
        go_to(m.veh, 100, game().bases[ba].x, game().bases[ba].y);
        return m.triad == TRIAD_SEA ? F_574a88 : F_57707f;
    }
    // L_56da35: a monolith nearby repairs too.
    if (m.dmg >= 3 && veh_offense(m.veh) != 0 && m.s84 == 0 && m.dist_any != 0 && m.triad == TRIAD_LAND) {
        for (int32_t i = 0; i < 9; i++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[i]);
            m.ty = TableOffsetY[i] + m.y;
            if (!on_map(m.tx, m.ty)) continue;
            MAP* sq = sq_at(m.tx, m.ty);
            if (sq_ocean(sq) || !(sq->items & BIT_MONOLITH)) continue;
            if (ExpansionEnabled && (v.unit_id == BSC_BATTLE_OGRE_MK1 || v.unit_id == BSC_BATTLE_OGRE_MK2 ||
                                     v.unit_id == BSC_BATTLE_OGRE_MK3))
                continue;
            m.s70 = anything_at(m.tx, m.ty);
            if (m.s70 >= 0 && m.s70 != m.fac && !(dstat(m.fac, m.s70) & 1)) continue;
            if (i == 0) {
                m.stay = 1;
                v.status_icon = 0x64;
                v.damage_taken = 0;
            } else {
                go_to(m.veh, 100, m.tx, m.ty);
            }
            return F_57708a;
        }
    }
    // L_56db67: fast units out of danger simply wait a turn.
    if (m.dmg >= 4 && m.s84 == 0 && m.dist_any != 0 && v.moves_spent == 0 &&
        Chassis[unit_of(m.veh).chassis_id].range == 0 && veh_moves(m.veh) == Rules.move_rate_roads && m.zocm == 0 &&
        !(v.state & 8) && speed_proto(v.unit_id) > Rules.move_rate_roads && v.unit_id != BSC_BATTLE_OGRE_MK1 &&
        v.unit_id != BSC_BATTLE_OGRE_MK2 && v.unit_id != BSC_BATTLE_OGRE_MK3) {
        v.status_icon = 0x64;
        if (m.triad != TRIAD_SEA) return F_578b34;
        m.stay = 1;
        return F_574a88;
    }
    // L_56dc96: board an adjacent transport.
    if (m.dmg >= 3 && m.s84 == 0 && m.triad == TRIAD_LAND && m.dist_any != 0 && !veh_cargo(m.veh) && !(v.state & 8)) {
        for (int32_t dir = 0; dir < 8; dir++) {
            m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
            m.ty = NearOffsetY[dir] + m.y;
            if (!on_map(m.tx, m.ty) || !sq_ocean(sq_at(m.tx, m.ty))) continue;
            m.other = stack_fix(veh_at(m.tx, m.ty));
            for (int32_t o = m.other; o >= 0;) {
                if (game().vehs[o].faction_id == (uint32_t)m.fac && unit_plan(o) == PLAN_NAVAL_TRANSPORT &&
                    stack_check(o, 8, -1, -1, -1) > 0) {
                    m.best_dir = dir;
                    v.status_icon = 0x64;
                    return F_578b29;
                }
                o = game().vehs[o].next_veh_id_stack;
                m.other = o;
            }
        }
    }
    // L_56ddda: land units on a transport stay aboard unless the transport is damaged.
    if (m.dmg > 2 && m.at_sea && m.triad == TRIAD_LAND) {
        if (m.dist_any == 0) {
            v.status_icon = 0x64;
            return F_578b34;
        }
        bool ok = true;
        m.other = veh_top(m.veh);
        for (int32_t o = m.other; o >= 0;) {
            if (unit_plan(o) == PLAN_NAVAL_TRANSPORT && game().vehs[o].damage_taken > proto_power(o) / 3) ok = false;
            o = game().vehs[o].next_veh_id_stack;
            m.other = o;
        }
        if (ok) {
            v.status_icon = 0x64;
            return F_578b34;
        }
    }
    // L_56deb8
    bool hold = true;
    if (!m.s4c) {
        // m.s58 / m.s30 hold base_any * 0x134 / fac * 0x833 here (set at L_56d9cd).
        int32_t bf = base_faction(m.base_any);
        uint32_t d = dstat(m.fac, bf);
        if ((d & 2) && !(d & 1) && reputation(bf, m.fac) < 6) hold = false;
    }
    if (hold && v.moves_spent == 0 && m.zocv == 0 && veh_triad_of(m.veh) == TRIAD_LAND && !(v.state & 8) &&
        (m.dmg > 1 || has_project(kProjectNanoFactory, m.fac))) {
        v.status_icon = 0x64;
        return F_578b34;
    }
    // L_56df59
    if (m.dmg >= 3 && m.triad != TRIAD_LAND) {
        action_home(m.veh, 2);
        if (v.order == ORDER_MOVE_TO) {
            v.status_icon = 0x64;
            return m.triad == TRIAD_SEA ? F_574a88 : F_57707f;
        }
    }
    int32_t bo2 = m.base_own;
    if (bo2 >= 0 && ((v.state & 8) || m.zocv) && m.dmg >= 4 &&
        get_there(m.veh, game().bases[bo2].x, game().bases[bo2].y)) {
        go_to(m.veh, 100, game().bases[bo2].x, game().bases[bo2].y);
        return m.triad == TRIAD_SEA ? F_574a88 : F_57707f;
    }
    return F_56e011;
}

// ---- probe teams (0x56E011-0x56E507) -------------------------------------------------------------

// L_56e011: probe teams act on adjacent targets, else head for the most interesting base;
// a probe team without a target is disbanded.
Flow em_probe(EM& m) {
    VEH& v = m.v();
    if (m.plan != PLAN_PROBE) return F_56e507;
    if (m.mp_own) {
        v.order = ORDER_NONE;
        v.state &= ~VSTATE_ON_ALERT;
        synch_veh(m.veh);
        NetDaemon_await_synch(&NetState);
        NetDaemon_unlock_veh(&NetState);
        return RET1;
    }
    if (m.st4000) return F_56e507;
    m.s48 = m.triad == TRIAD_SEA;
    if (m.at_sea != m.s48) return F_56e507;
    for (m.s58 = 0; m.s58 < 0x54; m.s58 += 4) {
        int32_t i = m.s58 / 4;
        m.tx = map_wrap_x(m.x + TableOffsetX[i]);
        m.ty = TableOffsetY[i] + m.y;
        if (!on_map(m.tx, m.ty)) continue;
        if (m.triad != TRIAD_AIR && sq_ocean_i(sq_at(m.tx, m.ty)) != m.s48) continue;
        if (tile_region(m.tx, m.ty) != m.region) continue;
        m.other = stack_fix(veh_at(m.tx, m.ty));
        if (m.other < 0 || game().vehs[m.other].faction_id == (uint32_t)m.fac) continue;
        if (!stack_check(m.other, 2, PLAN_PROBE, -1, -1)) {
            if (base_who(m.tx, m.ty) >= 0) continue;
            if (!probe(m.veh, -1, m.other, 0)) continue;
        }
        uint32_t d = dstat(m.fac, game().vehs[m.other].faction_id);
        if ((d & 1) && (d & 0x2000000)) continue;
        if ((d & 0x30) || whose_territory(m.fac, m.tx, m.ty, nullptr, 0) == m.fac) {
            go_to(m.veh, 0x50, m.tx, m.ty);
            return F_57707f;
        }
    }
    m.best = 99999;
    for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
        const BASE& base = game().bases[m.base_id];
        m.s4c = base.x;
        if (tile_region(base.x, base.y) != m.region) continue;
        m.s60 = m.x;
        int32_t score = hdist(m.x, m.y, base.x, base.y) << 4;
        int32_t bf = base_faction(m.base_id);
        if (bf == m.fac) {
            // Own bases: those with few probe teams nearby.
            base_find_filtered(base.x, base.y, -1, -1, m.fac, -1);
            int32_t n = stack_check(veh_at(base.x, base.y), 2, PLAN_PROBE, -1, -1);
            score += ((n + (score != 0)) * 64 + BaseFindDist) * 64;
        } else {
            if (has_project(FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First, bf)) continue;  // Hunter-Seeker Algorithm
            if (base.state_flags & BSTATE_RESEARCH_DATA_STOLEN) {
                m.s4c = bf;
                uint32_t d = dstat(m.fac, bf);
                if (d & 7) {
                    if (!(d & 0x20)) continue;
                    if (m.f().region_total_bases[m.region] != 0 &&
                        (int32_t)game().factions[bf].region_force_rating[m.region] >
                            (int32_t)((m.f().region_force_rating[m.region] >> 1) +
                                      m.f().region_total_combat_units[m.region]))
                        continue;
                }
                score *= 2;
            }
            m.s30 = m.fac * 0x833;
            uint32_t d = dstat(m.fac, bf);
            m.s4c = (int32_t)d;
            if (d & 0x10) score /= 2;
            m.s4c = d & 1;
            if (d & 1) {
                score *= 2;
                if (game().factions[bf].ranking < 6) continue;
                if (d & 0x1000000) continue;
            }
            if (d & 2) score *= 2;
            if ((d & 1) && (d & 0x2000000)) continue;
            if ((d & 7) && !(d & 0x20)) {
                if (m.f().tech_ranking >= game().factions[bf].tech_ranking) continue;
                if (!at_climax(m.fac) && (dstat(m.fac, bf) & 0x1000000)) continue;
            }
        }
        if (score < m.best) {
            m.best = score;
            go_to(m.veh, 0x70, base.x, base.y);
        }
    }
    if (v.order != ORDER_MOVE_TO) {
        kill(m.veh);
        return RET1;
    }
    return F_57707f;
}

// ---- land units: drop pods (0x56E507-0x56ED87) ---------------------------------------------------

Flow em_drop_pod(EM& m);

// L_56e507
Flow em_land(EM& m) {
    if (m.triad != TRIAD_LAND) return F_56ed87;
    VEH& v = m.v();
    if (has_abil(v.unit_id, ABL_DROP_POD) && v.moves_spent == 0 && veh_offense(m.veh) != 0 &&
        stack_check(m.veh, 2, 2, -1, -1) && !(v.state & VSTATE_MADE_AIRDROP) && !m.human() && m.dmg < 4) {
        if (m.dist_any == 0 || (sq_at(m.x, m.y)->items & BIT_AIRBASE)) {
            Flow f = em_drop_pod(m);
            if (f != F_56e62d) return f;
        } else if (whose_territory(m.fac, m.x, m.y, nullptr, 0) == m.fac && v.order != ORDER_MOVE_TO &&
                   m.base_own >= 0) {
            go_to(m.veh, 0x49, game().bases[m.base_own].x, game().bases[m.base_own].y);
        }
    }
    // L_56e62d
    if (m.dist_own != 0 || m.region_plan == m.plan) return F_56ed87;
    if (game().bases[m.base_own].ai_plan_status == 0) {
        if (!stack_check(m.veh, 2, PLAN_NAVAL_TRANSPORT, -1, -1) || m.plan == PLAN_TERRAFORM) return F_56ed87;
        if (m.plan == PLAN_COMBAT && m.region_plan <= 2) return F_56ef2f;
    }
    return F_56e693;
}

// L_56e6a9: drop pod units at a base or airbase: drop next to a weakly defended enemy base.
Flow em_drop_pod(EM& m) {
    VEH& v = m.v();
    m.s38 = drop_range(m.fac);
    m.best = 99999;
    m.best_idx = -1;
    int32_t b = 0;
    for (; b < game().base_count; b++) {
        int32_t bf = base_faction(b);
        if (bf == m.fac) continue;
        uint32_t d = dstat(m.fac, bf);
        if ((d & 1) || !(d & 0xC10)) continue;
        if (has_fac(FAC_AEROSPACE_COMPLEX, b, 0)) continue;
        if (has_project(FAC_CLOUDBASE_ACADEMY - SP_ID_First, base_faction(b))) continue;  // Cloudbase Academy
        if (veh_who(game().bases[b].x, game().bases[b].y) >= 0) continue;
        m.s60 = m.x;
        m.s58 = game().bases[b].x;
        int32_t dist = hdist(game().bases[b].x, game().bases[b].y, m.x, m.y);
        if (dist <= m.s38) {
            m.base_id = b;
            action_airdrop(m.veh, game().bases[b].x, game().bases[b].y, 3);
            return RET1;
        }
        if (dist < m.best) {
            m.best = dist;
            m.best_idx = b;
        }
    }
    m.base_id = b;
    if (m.best_idx >= 0) {
        // Move to an own base within drop range of the target.
        m.base_id = m.best_idx;
        m.best = 99999;
        int32_t t = m.best_idx;
        for (m.s54 = 0; m.s54 < game().base_count; m.s54++) {
            const BASE& base = game().bases[m.s54];
            if (base_faction(m.s54) != m.fac) continue;
            m.s60 = std::abs(base.y - game().bases[t].y);
            if (hdist(base.x, base.y, game().bases[t].x, game().bases[t].y) > m.s38) continue;
            if (stack_check(veh_at(base.x, base.y), 6, 0x10, -1, -1)) continue;
            int32_t d2 = hdist(base.x, base.y, m.x, m.y);
            m.s60 = std::abs(base.y - m.y);
            if (d2 <= m.s38) {
                veh_skip(m.veh);
                action_airdrop(m.veh, base.x, base.y, 3);
                return RET1;
            }
            if (tile_region(base.x, base.y) != m.region) continue;
            m.s60 = m.x;
            int32_t d3 = hdist(base.x, base.y, m.x, m.y);
            if (d3 < m.best) {
                m.best = d3;
                go_to(m.veh, 0x69, base.x, base.y);
            }
        }
        return F_56e62d;
    }
    // L_56e9aa: no target in range: drop next to any enemy base.
    // BUG(original): `best` is not reset (still 99999 from the search above), so no tile ever
    // scores higher and this search never drops.
    COMPAT_ORIGINAL(drop_best_not_reset);
    v.order = ORDER_NONE;
    if (m.f().units_active[m.unit] <= 1) return F_56e62d;
    m.s58 = -1;
    m.s60 = -1;
    for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
        const BASE& base = game().bases[m.base_id];
        m.s48 = 0x97D044 + m.base_id * 0x134;  // the original keeps a pointer here
        m.s70 = base_faction(m.base_id);
        int32_t bf = m.s70;
        if (bf == m.fac) continue;
        uint32_t d = dstat(m.fac, bf);
        if ((d & 1) || !(d & 0xC10)) continue;
        if (has_fac(FAC_AEROSPACE_COMPLEX, m.base_id, 0)) continue;
        if (has_project(FAC_CLOUDBASE_ACADEMY - SP_ID_First, base_faction(m.base_id))) continue;
        m.val = base.pop_size;
        uint8_t rp = game().factions[bf].region_base_plan[m.region];
        if (rp == 7) m.val = m.val * 3;
        if (rp == 8) m.val = m.val * 2;
        if (rp == 0) m.val = m.val / 2;
        m.other = stack_fix(veh_at(base.x, base.y));
        if (stack_check(m.other, 2, 4, -1, -1)) continue;
        m.s30 = stack_check(m.other, 0x10, -1, -1, -1);
        for (m.s54 = 0; m.s54 < 0x60; m.s54 += 4) {
            int32_t i = m.s54 / 4 + 1;
            m.tx = map_wrap_x(TableOffsetX[i] + base.x);
            m.ty = TableOffsetY[i] + base.y;
            if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
            if (hdist(m.x, m.y, m.tx, m.ty) > m.s38) continue;
            int32_t who = anything_at(m.tx, m.ty);
            if (who >= 0 && who != m.fac) continue;
            int32_t score = rnd(8) + m.val;
            if (who == m.fac) score = score * 3 / 4;
            if (m.s54 < 0x20 && m.s30 > 1) score /= 2;
            uint32_t items = sq_at(m.tx, m.ty)->items;
            if (items & 0x1608050) score *= 2;
            if (items & 4) score = (score << 2) / 3;
            for (m.s2c = 0; m.s2c < 0x20; m.s2c += 4) {
                int32_t ax = map_wrap_x(m.tx + NearOffsetX[m.s2c / 4]);
                int32_t ay = NearOffsetY[m.s2c / 4] + m.ty;
                if (!on_map(ax, ay)) continue;
                if (whose_territory(m.fac, ax, ay, nullptr, 0) == m.fac) score = 0;
                m.other = stack_fix(veh_at(ax, ay));
                if (m.other < 0) continue;
                int32_t of = game().vehs[m.other].faction_id;
                if (of == m.fac) {
                    if (has_abil(game().vehs[m.other].unit_id, ABL_DROP_POD)) score += 0x20;
                } else if (of == m.s70 && (uint8_t)unit_plan(m.other) <= 1) {
                    score /= 2;
                }
            }
            if (score > m.best) {
                m.best = score;
                m.s60 = m.tx;
                m.s58 = m.ty;
            }
        }
    }
    if (on_map(m.s60, m.s58)) {
        action_airdrop(m.veh, m.s60, m.s58, 3);
        return RET1;
    }
    return F_56e62d;
}

// ---- sea and air units, naval hunting, artillery (0x56ED87-0x56F8AD) -----------------------------

// L_56ed87: air units (and automated sea units on "air patrol") are moved by air_power.
Flow em_air(EM& m) {
    VEH& v = m.v();
    if (!((m.plan == PLAN_NAVAL_SUPERIORITY && (v.state & VSTATE_ON_ALERT) && v.order_auto_type == ORDERA_ON_ALERT) ||
          m.triad == TRIAD_AIR))
        return F_56ef2f;
    if (planet_buster(m.veh) && m.triad == TRIAD_AIR) {
        m.best_dir = enemy_planet_buster(m.veh);
        m.x = v.x;
        m.y = v.y;
        return F_578b29;
    }
    int32_t r = air_power(m.veh, m.dist_own, m.zocm, &m.x, &m.y);
    if (r == -5) return RET1;
    if (r == -1) {
        if (v.x == v.waypoint_x[0] && v.y == v.waypoint_y[0]) {
            m.stay = 1;
            return F_57707f;
        }
        m.s70 = anything_at(v.waypoint_x[0], v.waypoint_y[0]);
        if (m.s70 >= 0 && m.s70 == m.fac) return F_578f46;
        if (dstat_raw(m.fac, m.s70) & 1) return F_578f46;
        return F_57707f;
    }
    if (r == -2) {
        if (v.x != v.waypoint_x[0] || v.y != v.waypoint_y[0]) return F_578f46;
        m.stay = 1;
        return F_57707f;
    }
    if (r == -4) {
        m.stay = 1;
        return F_57707f;
    }
    if (r == -3) {
        if ((v.state & VSTATE_ON_ALERT) && v.order_auto_type == ORDERA_BOMBING_RUN) {
            v.order = ORDER_NONE;
            v.state &= ~VSTATE_ON_ALERT;
            return RET0;
        }
        return F_578b29;
    }
    if (r >= 0) m.best_dir = r;
    return F_578b29;
}

// L_56ef2f: naval superiority units hunt transports and visible enemy ships; artillery fires.
Flow em_naval(EM& m) {
    VEH& v = m.v();
    if (m.st4000) return F_57460a;
    if (m.plan == PLAN_NAVAL_SUPERIORITY) {
        m.best = v.order == ORDER_MOVE_TO ? hdist(m.x, m.y, v.waypoint_x[0], v.waypoint_y[0]) : 9999;
        for (m.s2c = 0; m.s2c < TableRange[range(veh_moves(m.veh) / Rules.move_rate_roads, 1, 8)]; m.s2c++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
            m.ty = TableOffsetY[m.s2c] + m.y;
            if (!on_map(m.tx, m.ty)) continue;
            MAP* sq = sq_at(m.tx, m.ty);
            if (!sq_ocean(sq)) continue;
            if (m.dist_own == 0 ? !base_on_sea(m.base_own, sq->region) : sq->region != m.region) continue;
            if (base_who(m.tx, m.ty) >= 0) continue;
            m.other = stack_fix(veh_at(m.tx, m.ty));
            if (m.other < 0) continue;
            for (;;) {
                if (veh_triad_of(m.other) == TRIAD_SEA) break;
                if (m.other < 0) break;
                m.other = game().vehs[m.other].next_veh_id_stack;
                if (m.other < 0) break;
            }
            if (m.other < 0) continue;
            int32_t d = hdist(m.x, m.y, m.tx, m.ty);
            m.s38 = d;
            const VEH& o = game().vehs[m.other];
            if (o.faction_id == (uint32_t)m.fac) {
                // An own transport out of moves: escort it.
                if (unit_plan(m.other) != PLAN_NAVAL_TRANSPORT) continue;
                if (veh_moves(m.other) != 0) continue;
                int32_t here = (o.x == m.x && o.y == m.y) ? 1 : 0;
                if (stack_check(m.other, 2, PLAN_NAVAL_SUPERIORITY, -1, -1) > here) continue;
            } else if (!(dstat(m.fac, o.faction_id) & 0x10)) {
                continue;
            }
            if (d < m.best) {
                m.best = d;
                go_to(m.veh, 0x6E, m.tx, m.ty);
            }
        }
        for (m.other = 0; m.other < game().veh_count; m.other++) {
            const VEH& o = game().vehs[m.other];
            if (o.faction_id == (uint32_t)m.fac) continue;
            if (!(dstat(m.fac, o.faction_id) & 0xC30)) continue;
            if (!((uint8_t)m.s68 & o.visibility)) continue;
            if (!sq_ocean(sq_at(o.x, o.y))) continue;
            if (base_who(o.x, o.y) >= 0) continue;
            int32_t d = hdist(m.x, m.y, o.x, o.y);
            if (unit_plan(m.other) != PLAN_NAVAL_TRANSPORT) d *= 2;
            m.s38 = d;
            if (stack_check(m.other, 1, -1, -1, -1) > 1) {
                d /= 2;
                m.s38 = d;
            }
            if (d < m.best) {
                add_goal(m.fac, 6, 2, o.x, o.y, -1);
                go_to(m.veh, 0x4E, o.x, o.y);
            }
        }
    }
    // L_56f3a5: artillery.
    if (!can_arty(v.unit_id, 1)) return F_56f8ad;
    if (!(m.triad == TRIAD_SEA || !sq_ocean(sq_at(m.x, m.y)) || m.dist_any == 0)) return F_56f8ad;
    if (m.s84) return F_56f8ad;
    m.best = 0;
    m.best_idx = -1;
    m.s58 = m.dist_own ? 25 : 81;
    bool aborted = false;
    for (m.s2c = 0; m.s2c < m.s58; m.s2c++) {
        m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
        m.ty = TableOffsetY[m.s2c] + m.y;
        if (!on_map(m.tx, m.ty)) continue;
        if (anything_at(m.tx, m.ty) < 0) {
            // Enemy terrain improvements.
            if (sq_ocean(sq_at(m.tx, m.ty))) continue;
            int32_t t = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
            if (t < 0 || t == m.fac || !(dstat(m.fac, t) & 0x10)) continue;
            MAP* sq = sq_at(m.tx, m.ty);
            uint32_t items = sq->items;
            int32_t rr = sq->region;
            if (m.f().region_total_bases[rr] != 0 && m.f().region_base_plan[rr] != 2 && m.triad == TRIAD_LAND &&
                !(items & 0x80000000))
                continue;
            if (!(items & 0x80001000)) continue;
            if (!(items & 0x80000000) && m.triad == TRIAD_SEA && v.order == ORDER_MOVE_TO) continue;
            if (!(items & 0x81208854)) continue;
            int32_t score = (items & 0x80000000) ? 32 : 2;
            if (score <= m.best) continue;
            m.best = score;
            m.best_idx = m.s2c;
            continue;
        }
        m.other = stack_fix(veh_at(m.tx, m.ty));
        if (m.other < 0) continue;
        int32_t of = game().vehs[m.other].faction_id;
        if (of == m.fac || !(dstat(m.fac, of) & 0x10)) continue;
        int32_t score = (stack_check(m.other, 2, 6, -1, -1) + stack_check(m.other, 6, 0x8000, -1, -1)) * 8;
        if (m.triad == TRIAD_SEA) {
            if (sq_ocean(sq_at(m.tx, m.ty))) {
                if (base_who(m.tx, m.ty) >= 0) {
                    aborted = true;
                    break;
                }
                m.s60 = m.tx;
                int32_t d = hdist(m.x, m.y, m.tx, m.ty);
                if (veh_moves(m.veh) / Rules.move_rate_roads >= d) {
                    aborted = true;
                    break;
                }
            }
            if (v.order == ORDER_MOVE_TO) continue;
        }
        if (m.zocv == 0 || m.s2c < 9) score += stack_check(m.other, 4, -1, -1, -1);
        m.s60 = m.x;
        if (hdist(m.x, m.y, m.tx, m.ty) > Rules.artillery_max_rng && score > 1) score = 1;
        if (score > m.best) {
            m.best = score;
            m.best_idx = m.s2c;
        }
    }
    if (aborted) {
        m.best_idx = -1;
    } else if (m.best_idx >= 0) {
        int32_t i = m.best_idx;
        m.tx = map_wrap_x(m.x + TableOffsetX[i]);
        m.ty = TableOffsetY[i] + m.y;
        m.s60 = m.x;
        if (hdist(m.x, m.y, m.tx, m.ty) <= Rules.artillery_max_rng) {
            if (anything_at(m.tx, m.ty) < 0) {
                if (m.mp_own) {
                    log_say_values("Auto sending DESTROY2 message", m.veh, m.tx, m.ty);
                    message_data(0x2413, m.veh, m.tx, m.ty, 0, 0);
                    NetDaemon_await_exec(&NetState, 1);
                    return RET1;
                }
                log_say_values("Enemy doing ARTY destroy 1...", m.veh, m.tx, m.ty);
                action_destroy(m.veh, 0, m.tx, m.ty);
                return RET1;
            }
            if (i >= 9 || (m.triad == TRIAD_SEA && !sq_ocean(sq_at(m.tx, m.ty)))) {
                if (m.mp_own) {
                    log_say_values("Auto sending ARTY message", m.veh, m.fac, 0);
                    message_veh(0x2402, m.veh, i, 0);
                    return F_578d67;
                }
                log_say_values("Enemy doing ARTY...", m.veh, i, 0);
                battle_fight(m.veh, i, 1, 1, 0);
                return RET1;
            }
        }
        // L_56fb81
        if (m.dist_own == 0 && i >= 25 && (!m.st6 || m.region_plan != 0) && anything_at(m.tx, m.ty) >= 0)
            return F_578b34;
        return F_56f8ad;
    }
    // L_56f841: lone artillery in a base of a contested region stays.
    if (m.dist_own == 0 && !m.st6 && m.region_plan != 0 && stack_check(m.veh, 0xF, -1, -1, -1) <= 1 &&
        m.triad != TRIAD_SEA) {
        if ((sq_at(m.x, m.y)->items & BIT_CANAL_COAST) || m.region_plan != 2) return F_578b34;
    }
    return F_56f8ad;
}

// ---- colony pods, scouts: sites (0x56F8AD-0x570C89) ---------------------------------------------

// L_56f8ad: colony pods (and land units) look for the best base site within radius 2.
Flow em_colony(EM& m) {
    VEH& v = m.v();
    del_site(m.fac, 3, m.x, m.y, 0);
    if (!(m.plan == PLAN_COLONY || m.triad == TRIAD_LAND)) return F_570002;
    if (m.s84) return F_570002;
    m.s48 = m.triad == TRIAD_SEA;
    if (m.at_sea != m.s48) return F_570002;
    if (m.plan == PLAN_COLONY) del_site(m.fac, PLAN_COLONY, m.x, m.y, 1);
    m.base_id = 0;  // best site value
    if (m.plan == PLAN_COLONY && v.order == ORDER_MOVE_TO && vdist(m.x, m.y, v.waypoint_x[0], v.waypoint_y[0]) > 2)
        m.base_id = site_at(v.waypoint_x[0], v.waypoint_y[0], 0) - 1;
    for (m.s2c = 0; m.s2c < 25; m.s2c++) {
        m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
        m.ty = TableOffsetY[m.s2c] + m.y;
        if (!on_map(m.tx, m.ty)) continue;
        MAP* sq = sq_at(m.tx, m.ty);
        int32_t alt = sq->climate & 0xE0;
        if ((alt < 0x60 ? 1 : 0) != m.s48) continue;
        if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
        if ((sq->val3 & 0xC0) > 0x40 && alt >= 0x60) continue;
        if (sq->region != m.region) continue;
        if (sq->items & BIT_MONOLITH) continue;
        if (base_territory(m.fac, m.tx, m.ty) >= 0) continue;
        if (m.triad == TRIAD_LAND) {
            if (!(m.f().region_flags[m.region] & 0x80) && !is_coast(m.tx, m.ty, 0) &&
                Continents[m.region].open_terrain - m.f().region_unk_5[m.region] < 20)
                continue;
        } else if (m.triad == TRIAD_SEA) {
            bool bad = false;
            for (m.s30 = 0; m.s30 < 0x144; m.s30 += 4) {
                int32_t i = m.s30 / 4;
                int32_t ax = map_wrap_x(m.tx + TableOffsetX[i]);
                int32_t ay = TableOffsetY[i] + m.ty;
                if (!on_map(ax, ay)) continue;
                int32_t b = base_at(ax, ay);
                if (b >= 0 && base_faction(b) != m.fac && (dstat(m.fac, base_faction(b)) & 0x10)) {
                    if (is_port(b, 0) || m.s2c < 21) {
                        bad = true;
                        break;
                    }
                }
                m.other = stack_fix(veh_at(ax, ay));
                if (m.other >= 0) {
                    int32_t of = game().vehs[m.other].faction_id;
                    if (of != m.fac && (dstat(m.fac, of) & 0x10) && stack_check(m.other, 2, 6, -1, -1)) {
                        bad = true;
                        break;
                    }
                }
            }
            if (bad) continue;
        }
        // L_56fce4
        int32_t site = site_at(m.tx, m.ty, 0);
        if (site <= m.base_id) continue;
        base_find(m.tx, m.ty);
        bool accept = true;
        if (site < (8 - BaseFindDist) * 2 && m.f().region_total_bases[m.region] != 0 && m.f().base_count >= 2) {
            int32_t thr = m.region_plan != 8 ? 4 : 1;
            if ((int32_t)m.f().region_unk_6[m.region] > thr) thr = m.f().region_unk_6[m.region];
            if (site < thr) {
                if (m.plan != PLAN_COLONY || v.order == ORDER_MOVE_TO ||
                    Continents[m.region].open_terrain >= (m.f().region_total_bases[m.region] + 1) * 6)
                    accept = false;
            }
        }
        if (accept) {
            m.base_id = site;
            m.best_dir = m.s2c;
        }
    }
    if (m.base_id == 0) return F_570002;
    if (m.best_dir == 0 && m.plan == PLAN_COLONY) {
        v.movement_turns++;
        m.base_id += v.movement_turns;
    }
    if (m.base_id == 0 || m.best_dir < 0) return F_570002;
    if (m.zocm && !(dstat(m.fac, m.zocm - 1) & 7)) return F_570002;
    if (m.plan == PLAN_COLONY) {
        if (m.best_dir == 0) {
            if (m.mp_own) {
                log_say_values("Auto sending BUILD BASE message", m.veh, m.fac, 0);
                message_base(0x2408, m.veh, NetBaseName, 0);
                return F_578d67;
            }
            action_build(m.veh, nullptr);
            if (m.f().base_count != 1) return RET1;
            // First base: all formers come to help.
            for (int32_t i = 0; i < game().veh_count; i++) {
                if (game().vehs[i].faction_id == (uint32_t)m.fac && unit_plan(i) == PLAN_TERRAFORM)
                    go_to(i, 0x43, m.x, m.y);
            }
            return RET1;
        }
        v.status_icon = 0x62;
        if (m.best_dir < 9) {
            m.best_dir--;
            return F_578b29;
        }
        go_to(m.veh, 0x62, map_wrap_x(m.x + TableOffsetX[m.best_dir]), TableOffsetY[m.best_dir] + m.y);
        return F_57000c;
    }
    if (m.best_dir < 9) {
        // Other land units mark the site for colony pods.
        m.tx = map_wrap_x(m.x + TableOffsetX[m.best_dir]);
        m.ty = TableOffsetY[m.best_dir] + m.y;
        m.val = m.base_id < 6 ? 1 : 2;
        if (m.dist_any >= 0) {
            // BUG(original): base_any may be -1 here (Bases[-1], see base_raw).
            COMPAT_ORIGINAL(out_of_range_reads);
            m.s38 = hdist(m.tx, m.ty, base_x_raw(m.base_any), base_y_raw(m.base_any));
            if (m.s38 <= 4) m.val = 1;
            if (m.s38 >= 6 && m.s38 < 12 && m.base_id >= 10) m.val = 3;
        }
        del_site(m.fac, 8, m.x, m.y, 2);
        add_site(m.fac, 8, m.val, m.tx, m.ty);
    }
    return F_570002;
}

// L_570002
Flow em_colony_next(EM& m) {
    if (m.plan == PLAN_COLONY) return F_57000c;
    if (m.plan == PLAN_RECON) return F_5702f7;
    return F_570c89;
}

// L_57000c: sea colony pods scan the whole region for the best reachable ocean site.
Flow em_sea_sites(EM& m) {
    VEH& v = m.v();
    if (m.triad == TRIAD_SEA && v.order != ORDER_MOVE_TO && m.at_sea) {
        m.best = 0;
        for (m.ty = 0; m.ty < game().map.y; m.ty++) {
            for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                if (!on_map(m.tx, m.ty)) continue;
                if (tile_region(m.tx, m.ty) != m.region) continue;
                if (anything_at(m.tx, m.ty) >= 0) continue;
                MAP* sq = sq_at(m.tx, m.ty);
                if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) continue;
                int32_t site = site_at(m.tx, m.ty, 0);
                m.val = (site * 10000) / (vdist(m.x, m.y, m.tx, m.ty) + 1);
                m.base_id = base_find(m.tx, m.ty);
                if (m.base_id >= 0 && base_faction(m.base_id) != m.fac &&
                    (dstat(m.fac, base_faction(m.base_id)) & 0x10))
                    m.val /= 2;
                if (m.val < m.best) continue;
                bool bad = false;
                for (m.s48 = 0; m.s48 < 0x144; m.s48 += 4) {
                    int32_t i = m.s48 / 4;
                    int32_t ax = map_wrap_x(m.tx + TableOffsetX[i]);
                    int32_t ay = TableOffsetY[i] + m.ty;
                    if (!on_map(ax, ay) || !sq_ocean(sq_at(ax, ay))) continue;
                    // Note: the hostility is checked from the other faction's side here.
                    m.base_id = base_at(ax, ay);
                    if (m.base_id >= 0 && base_faction(m.base_id) != m.fac &&
                        (dstat(base_faction(m.base_id), m.fac) & 0x10)) {
                        bad = true;
                        break;
                    }
                    m.other = stack_fix(veh_at(ax, ay));
                    if (m.other >= 0 && game().vehs[m.other].faction_id != (uint32_t)m.fac &&
                        (dstat(game().vehs[m.other].faction_id, m.fac) & 0x10)) {
                        bad = true;
                        break;
                    }
                }
                if (bad) continue;
                m.best = m.val;
                go_to(m.veh, 0x63, m.tx, m.ty);
            }
        }
    }
    return F_5702f7;
}

// L_5702f7: land scouts and colony pods: exploration and base sites of the whole region.
Flow em_explore_sites(EM& m) {
    VEH& v = m.v();
    if (v.order == ORDER_MOVE_TO && m.dist_any != 0) return F_570c89;
    if (m.triad != TRIAD_LAND || m.at_sea) return F_570c89;
    m.s30 = 0;  // matching sites in the region
    m.s48 = 0;  // good ones
    for (m.s60 = 25; m.s60 != 0; m.s60--) {
        const Goal& site = m.f().sites[25 - m.s60];
        if (site.type != m.plan || site.priority < 1) continue;
        if (tile_region(site.x, site.y) != m.region) continue;
        m.s30++;
        if (m.plan == PLAN_RECON) {
            if (goody_at(site.x, site.y)) m.s48++;
        } else {
            int32_t thr = m.f().region_unk_6[m.region] <= 8 ? (int32_t)m.f().region_unk_6[m.region] : 8;
            if (m.region_plan != 8 && thr < 4) thr = 4;
            if (site_at(site.x, site.y, 0) >= thr) m.s48++;
        }
    }
    m.s2c = v.order == ORDER_MOVE_TO;
    if (m.s30 == 0 || ((m.fac + CurrentTurn) & (m.s48 ? 15 : 3)) == 0 || m.dist_any == 0) {
        m.best = 0;
        for (m.ty = 0; m.ty < game().map.y; m.ty++) {
            for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                if (!on_map(m.tx, m.ty)) continue;
                if (tile_region(m.tx, m.ty) != m.region) continue;
                m.s54 = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
                if (m.region_plan != 8 && m.s54 != m.fac && m.s54 >= 0) continue;
                if (base_territory(m.fac, m.tx, m.ty) >= 0) continue;
                if (m.s30 == 0 || ((CurrentTurn + m.fac) & 0xF) == 0) {
                    // Supply pods worth a visit.
                    if ((is_known(m.tx, m.ty, m.fac) || (DiffLevel > 3 && !m.human())) && goody_at(m.tx, m.ty)) {
                        int32_t b = base_find(m.tx, m.ty);
                        if ((b >= 0 && base_faction(b) == m.fac) || m.s54 == m.fac)
                            add_site(m.fac, 3, (BaseFindDist < 6) + 1, m.tx, m.ty);
                    }
                }
                MAP* sq = sq_at(m.tx, m.ty);
                int32_t alt = sq->climate & 0xE0;
                if (alt < 0x60) continue;
                if ((sq->items & BIT_FUNGUS) && alt >= 0x40) continue;
                if (sq->items & BIT_MONOLITH) continue;
                if ((sq->val3 & 0xC0) > 0x40) continue;
                if (m.s54 == m.fac || m.s54 < 0) {
                    if (!is_known(m.tx, m.ty, m.fac)) {
                        // Unexplored area: a site for scouts.
                        m.val = 0;
                        for (m.s48 = 0; m.s48 < 0x54; m.s48 += 4) {
                            int32_t i = m.s48 / 4;
                            int32_t ax = map_wrap_x(m.tx + TableOffsetX[i]);
                            int32_t ay = TableOffsetY[i] + m.ty;
                            if (!on_map(ax, ay) || tile_region(ax, ay) != m.region) continue;
                            if (at_site(m.fac, 3, ax, ay)) goto L_57072b;
                            if (!is_known(ax, ay, m.fac)) {
                                m.val++;
                                if (goody_at(ax, ay)) m.val++;
                            } else if ((DiffLevel > 3 && !m.human()) || m.s48 < 0x24) {
                                m.val |= 0x20;
                            }
                        }
                        if (m.val >= 0x21) add_site(m.fac, 3, m.val < 0x28 ? (m.val >= 0x23) + 1 : 3, m.tx, m.ty);
                    }
                }
            L_57072b:
                if (m.region_plan != 8 && m.s54 != m.fac) continue;
                m.s48 = (m.region + m.fac * 0x1066) * 2;
                if (!(m.f().region_flags[m.region] & 0x80) && !is_coast(m.tx, m.ty, 0) &&
                    Continents[m.region].open_terrain - m.f().region_unk_5[m.region] < 20)
                    continue;
                m.val = site_at(m.tx, m.ty, 0);
                if (m.val == 0) continue;
                {
                    uint8_t tb = m.f().region_total_bases[m.region];
                    if (m.val < (8 - BaseFindDist) * 2 - (tb < 2 ? 2 : 0) && m.val < m.f().region_unk_6[m.region] &&
                        tb != 0 && m.f().base_count >= 2)
                        continue;
                }
                // Not if a better site is close by.
                m.s58 = 1;
                for (m.s60 = 0; m.s60 < 0x60; m.s60 += 4) {
                    int32_t i = m.s60 / 4 + 1;
                    int32_t ax = map_wrap_x(m.tx + TableOffsetX[i]);
                    int32_t ay = TableOffsetY[i] + m.ty;
                    if (!on_map(ax, ay)) continue;
                    MAP* a = sq_at(ax, ay);
                    if (a->region != m.region) continue;
                    if ((a->items & BIT_FUNGUS) && (a->climate & 0xE0) >= 0x40) continue;
                    if (a->items & BIT_MONOLITH) continue;
                    if ((a->val3 & 0xC0) > 0x40) continue;
                    if (base_territory(m.fac, ax, ay) >= 0) continue;
                    if (sq_at(ax, ay)->items & BIT_BASE_RADIUS) continue;
                    int32_t s2 = site_at(ax, ay, 0);
                    if (s2 < m.val) continue;
                    if (s2 == m.val && !at_site(m.fac, 8, ax, ay)) continue;
                    if (!(m.f().region_flags[m.region] & 0x80) && !is_coast(ax, ay, 0) &&
                        Continents[m.region].open_terrain - m.f().region_unk_5[m.region] < 20)
                        continue;
                    if (m.region_plan != 8 && whose_territory(m.fac, ax, ay, nullptr, 0) != m.fac) continue;
                    m.s58 = 0;
                    break;
                }
                int32_t score = (m.val << 16) / (hdist(m.x, m.y, m.tx, m.ty) + 1);
                if (!m.s58) continue;
                if (score <= m.best && !is_known(m.tx, m.ty, m.fac) && !(DiffLevel > 3 && !m.human())) continue;
                {
                    int32_t owner = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
                    if (Path_find(&Paths, m.tx, m.ty, m.x, m.y, owner, m.fac, 0x201, -1) < 0) continue;
                }
                if (is_known(m.tx, m.ty, m.fac) || (DiffLevel > 3 && !m.human())) {
                    del_site(m.fac, 8, m.tx, m.ty, 2);
                    add_site(m.fac, 8, 2, m.tx, m.ty);
                    m.s30++;
                }
                if (score > m.best) {
                    m.best = score;
                    if (m.s2c == 0 && m.plan == PLAN_COLONY) go_to(m.veh, 0x63, m.tx, m.ty);
                }
            }
        }
        // L_570ad4
        if (m.plan != PLAN_RECON && m.s2c == 0) {
            int32_t found = m.s30;
            if (v.order == ORDER_MOVE_TO) {
                if (found == 0) found = 1;
                add_site(m.fac, 3, 2, v.waypoint_x[0], v.waypoint_y[0]);
                if (!is_known(v.waypoint_x[0], v.waypoint_y[0], m.fac) && (DiffLevel <= 3 || m.human())) {
                    v.order = ORDER_NONE;
                } else {
                    del_site(m.fac, 8, m.tx, m.ty, 2);
                    add_site(m.fac, 8, 3, v.waypoint_x[0], v.waypoint_y[0]);
                }
            }
            if (found == 0) {
                // No sites left in the region.
                m.f().region_good_tiles[m.region] -= 10;
                m.f().region_flags[m.region] |= 9;
                if (m.region_plan == 8) {
                    m.region_plan = 7;
                    m.f().region_base_plan[m.region] = 7;
                }
            }
        }
    }
    // L_570bdb: colony pods in a treaty partner's territory go home.
    if (m.plan == PLAN_COLONY && v.order != ORDER_MOVE_TO) {
        m.s70 = whose_territory(m.fac, m.x, m.y, nullptr, 0);
        if (m.s70 >= 0 && m.s70 != m.fac && (dstat(m.fac, m.s70) & 3)) {
            m.base_id = base_find_filtered(m.x, m.y, m.fac, m.region, -1, -1);
            if (m.base_id >= 0) {
                go_to(m.veh, 0x6B, game().bases[m.base_id].x, game().bases[m.base_id].y);
                return F_57717f;
            }
        }
    }
    return F_570c89;
}

// ---- improvements to destroy, supply pods nearby (0x570C89-0x57138D) -----------------------------

// L_570c89: slow units next to an enemy base destroy sensors and bunkers there; units on
// improvements of a human enemy destroy them; then supply pods within radius 3 (4) are visited.
Flow em_destroy(EM& m) {
    VEH& v = m.v();
    int32_t ba = m.base_any;
    bool skip_first = speed(m.veh, 0) > Rules.move_rate_roads || !(sq_at(m.x, m.y)->items & BIT_ECH_MIRROR) ||
                      veh_triad_of(m.veh) == TRIAD_AIR || ba < 0;
    if (!skip_first) {
        // A sensor next to an enemy base (the original looks 6 columns to the east).
        m.tx = map_wrap_x(m.x + 6);
        m.base_id = base_find(m.tx, m.y);
        bool destroy;
        if (m.base_id >= 0 && BaseFindDist <= 2) {
            destroy = (dstat(m.fac, base_faction(m.base_id)) & 0x10) != 0;
        } else {
            destroy = base_faction(ba) == m.fac && m.dist_any <= 2;
        }
        if (destroy) {
            if (m.mp_own) {
                log_say_values("Auto sending DESTROY message", m.veh, 0x800000, 0);
                message_data(0x2406, m.veh, 0x800000, 0, 0, 0);
                return F_578d67;
            }
            action_destroy(m.veh, 0x800000, -1, -1);
            return RET0;
        }
    }
    // L_570df9
    if (speed(m.veh, 0) <= Rules.move_rate_roads && (sq_at(m.x, m.y)->items & BIT_CONDENSER) &&
        veh_triad_of(m.veh) != TRIAD_AIR && ba >= 0 && m.dist_any <= 2 && base_faction(ba) != m.fac && m.dist_own > 3 &&
        (dstat(m.fac, base_faction(ba)) & 0x10)) {
        if (m.mp_own) {
            log_say_values("Auto sending DESTROY message", m.veh, 0x400000, 0);
            message_data(0x2406, m.veh, 0x400000, 0, 0, 0);
            return F_578d67;
        }
        action_destroy(m.veh, 0x400000, -1, -1);
        return RET0;
    }
    // L_570f01
    if (speed(m.veh, 0) <= Rules.move_rate_roads && ba >= 0 && is_human(base_faction(ba)) &&
        (dstat(m.fac, base_faction(ba)) & 0x10) && m.triad != TRIAD_AIR &&
        sq_ocean_i(sq_at(m.x, m.y)) == (m.triad == TRIAD_SEA) && veh_offense(m.veh) != 0 && !m.s84 &&
        whose_territory(m.fac, m.x, m.y, nullptr, 0) == base_faction(ba)) {
        uint32_t items = sq_at(m.x, m.y)->items;
        if ((items & 0x81208850) || ((items & 4) && m.f().region_total_bases[m.region] == 0)) {
            if (m.mp_own) {
                log_say_values("Auto sending DESTROY message", m.veh, 0, 0);
                message_data(0x2406, m.veh, 0, 0, 0, 0);
                return F_578d67;
            }
            log_say_values("Enemy doing DESTROY", m.veh, m.x, m.y);
            action_destroy(m.veh, 0, -1, -1);
            return F_578f46;
        }
    }
    // L_57109e: supply pods.
    int32_t low = 2;
    if (m.region_plan > 2 && !(m.f().region_flags[m.region] & 0x2000) && m.zocv == 0 && m.plan <= PLAN_RECON &&
        m.triad == TRIAD_LAND) {
        v.state |= 0x40000000;
    } else if (m.triad == TRIAD_AIR) {
        return F_57137d;
    }
    // L_571101
    if (m.plan == PLAN_COLONY || m.plan == PLAN_TERRAFORM) return F_57137d;
    if (m.dist_own == 0 && !(v.state & 0x40000000)) {
        bool check = true;
        if (m.plan == PLAN_RECON) {
            if (v.home_base_id != m.base_own && m.zocv == 0) check = false;
        } else if (m.plan != PLAN_DEFENSE && m.plan != PLAN_COMBAT) {
            low = m.triad == TRIAD_LAND;
        }
        if (check && (stack_check(m.veh, 0x10, -1, -1, -1) <= low || m.zocv != 0)) return F_57137d;
    }
    // L_57117a
    if (m.s84) return F_57137d;
    uint8_t order = v.order;
    bool to_1c6;
    if ((order == 0 && m.plan != PLAN_NAVAL_TRANSPORT) || m.plan == PLAN_RECON || (v.state & 0x40000000)) {
        to_1c6 = true;
    } else {
        if (m.triad == TRIAD_LAND) return F_57137d;
        if (m.plan == PLAN_NAVAL_TRANSPORT) {
            if (!(v.state & 0x20000000)) return F_57137d;
            to_1c6 = false;
        } else {
            to_1c6 = true;
        }
    }
    if (to_1c6 && m.plan == PLAN_NAVAL_SUPERIORITY && order == 0x18) return F_57137d;
    // L_5711d4
    if (CurrentTurn >= 100 && !m.s4c && m.dist_own > 6 && order == 0x18 && v.status_icon != 0x58 &&
        !(v.state & 0x40000000) && m.plan != PLAN_RECON)
        return F_57137d;
    m.s54 = (m.plan == PLAN_RECON || m.triad == TRIAD_SEA) ? 0x31 : 0x19;
    if (m.plan == PLAN_NAVAL_TRANSPORT && (stack_check(m.veh, 2, 8, -1, -1) || m.dmg > 1)) m.s54 = 9;
    if (CurrentTurn >= (m.plan != PLAN_NAVAL_SUPERIORITY ? 100 : 400) && v.order == ORDER_MOVE_TO &&
        v.status_icon != 0x58 && m.plan != PLAN_RECON)
        m.s54 = 9;
    m.s2c = 1;
    if (m.s54 > 1) {
        for (; m.s2c < m.s54; m.s2c++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
            m.ty = TableOffsetY[m.s2c] + m.y;
            if (!on_map(m.tx, m.ty) || !goody_at(m.tx, m.ty)) continue;
            if (!is_known(m.tx, m.ty, m.fac) && !(DiffLevel > 3 && !m.human())) continue;
            MAP* sq = sq_at(m.tx, m.ty);
            if (sq_ocean_i(sq) != (m.triad == TRIAD_SEA) || sq->region != m.region) continue;
            if (v.state & 0x40000000) m.f().region_flags[m.region] |= 0x2000;
            go_to(m.veh, 0x47, m.tx, m.ty);
            return F_57707f;
        }
    }
    return F_57137d;
}

// ---- garrisons, defence, gates, sieges (0x57138D-0x572922) ---------------------------------------

Flow em_siege(EM& m);

// L_57138d: also the entry of hint mode (s6c != 0), which only reports garrison duty.
Flow em_defend(EM& m) {
    VEH& v = m.v();
    m.s68 = 1 << m.fac;
    if (!m.human() && m.dist_own == 0 && m.s6c == 0 && m.zocv != 0 && !is_objective(m.base_own) &&
        (m.region_plan == 2 || m.f().region_total_bases[m.region] == 1 ||
         (m.f().player_flags & PFLAG_OBLITERATE_CAPTURED_BASES))) {
        // Obliterate an own base (captured from a human) about to be lost.
        BASE& b = game().bases[m.base_own];
        int32_t former = (uint8_t)b.faction_id_former;
        m.s70 = former;
        if (stack_check(m.veh, 1, -1, -1, -1) <= (b.pop_size + 1) / 2 &&
            stack_check(m.veh, 0x10, -1, -1, -1) < (v.damage_taken != 0) + 2 && former != m.fac && is_human(former)) {
            uint32_t d = dstat(m.fac, former);
            if ((d & 0x20) && !(d & 3) &&
                ((d & 0x40000) || game().factions[former].integrity_blemishes > 5 ||
                 m.f().diplo_betrayed[former] > 3 - m.f().AI_fight * 2)) {
                log_say_values("Enemy doing OBLIT", m.veh, m.x, m.y);
                action_oblit(m.veh, m.base_own);
                return RET1;
            }
        }
    }
    // L_571579
    bool defend_base = !(m.dist_any != 0 || (m.dist_own != 0 && m.dmg == 0) || m.st4000);
    int32_t n = 0;
    if (defend_base) {
        m.s48 = garrison_check(m.base_any);
        int32_t def = defensive_check(m.base_any);
        m.s30 = stack_check(m.veh, 0xB, 0x10000000, 0x10000000, -1);
        n = range(stack_check(m.veh, 2, 2, m.fac, -1), 0, def) + m.s30;
        if (v.state & 0x10000000) {
            int32_t t = m.s48 - def;
            if (t < 1) t = 1;
            if (m.s30 <= t) m.plan = PLAN_DEFENSE;
        }
        if (m.region_plan == 2 || m.region_plan == 8) {
            n += stack_check(m.veh, 2, 1, m.fac, -1);
            if (m.plan == PLAN_COMBAT) m.plan = PLAN_DEFENSE;
        }
        if (n == 0) n = stack_check(m.veh, 2, 3, m.fac, -1);
        if (!m.st4 && !m.st6) m.s48 += stack_check(m.veh, 0xB, 0x40000, 0x40000, -1);
        bool to_723;
        if (zoc_veh(m.x, m.y, m.fac) || m.region_plan <= 2 || m.s6c) {
            to_723 = true;
        } else if (m.plan == PLAN_RECON) {
            to_723 = false;
        } else if (m.plan > PLAN_DEFENSE) {
            to_723 = true;  // (0x57172C: the plan 2 test is skipped, same outcome)
        } else if (game().factions[m.fac].region_total_combat_units[m.region] >= 2) {
            to_723 = true;
        } else {
            to_723 = false;
        }
        if (!to_723) {
            // L_5716fa
            to_723 = (m.f().region_flags[m.region] & 0x2000) != 0;
        }
        if (to_723) {
            // L_571723
            bool garrison = m.plan == PLAN_DEFENSE || m.plan == PLAN_RECON ||
                            (m.plan == PLAN_COMBAT && (m.region_plan == 2 || m.region_plan == 8));
            if (!garrison) {
                if (stack_check(m.veh, 2, 2, m.fac, -1) || stack_check(m.veh, 2, 3, m.fac, -1) ||
                    stack_check(m.veh, 2, 1, m.fac, -1) > (m.plan == PLAN_COMBAT))
                    goto L_571977;
                {
                    int32_t type = range(game().bases[m.base_any].pop_size, 1, 3);
                    add_goal(m.fac, 2, stack_check(m.veh, type, -1, -1, -1), m.x, m.y, -1);
                }
                if (veh_offense(m.veh) == 0 && !zoc_sea(m.x, m.y, m.fac)) goto L_571977;
                m.defend = 1;
                m.stay = 1;
                if (m.s6c) return RET1;
                return F_57707f;
            }
            // L_571853
            if (n < m.s48) add_goal(m.fac, 2, 2, m.x, m.y, -1);
            bool keep;
            if (n > m.s48 && (m.plan != PLAN_DEFENSE || m.s30 == 0 || n > m.plan)) keep = false;
            else keep = !(m.st4 || m.st6);
            if (!keep && n > 1 && (m.s44 == 0 || m.zocv == 0)) goto L_571977;
            // L_5718ba: stay as a garrison.
            if (m.dist_own == 0 && v.home_base_id >= 0 && !m.human()) v.home_base_id = (int16_t)m.base_own;
            v.order = ORDER_NONE;
            m.stay = 1;
            m.defend = (n <= 1) + 1;
            if (m.s6c) return RET1;
            COMPAT_ORIGINAL(region_faction_0_units);
            if (m.plan == PLAN_DEFENSE && m.region_plan > 2 && n >= m.s48 &&
                !(game().bases[m.base_own].state_flags & 0x4000000) &&
                (n > 1 ||
                 (game().factions[0].region_total_combat_units[m.region] == 0 && !(m.f().player_flags & 0x10000))))
                return F_571982;  // BUG(original), compat region_faction_0_units: faction 0's region count
            return F_57707f;
        }
        return F_571982;
    }
L_571977:
    if (m.s6c) return RET0;
    return F_571982;
}

// L_571982: defenders and empath scouts pick a tile of own territory to cover; then gates,
// attack targets and sieges.
Flow em_cover(EM& m) {
    VEH& v = m.v();
    if (v.order != ORDER_MOVE_TO && (m.s44 || m.plan == PLAN_DEFENSE)) {
        v.order = ORDER_NONE;
        for (m.s6c = 0; m.s6c < 0x484; m.s6c += 4) {
            int32_t i = m.s6c / 4;
            m.tx = map_wrap_x(m.x + TableOffsetX[i]);
            m.ty = TableOffsetY[i] + m.y;
            if (!on_map(m.tx, m.ty) || tile_region(m.tx, m.ty) != m.region) continue;
            if (whose_territory(m.fac, m.tx, m.ty, nullptr, 0) != m.fac) continue;
            m.s38 = vdist(m.x, m.y, m.tx, m.ty);
            m.other = stack_fix(veh_at(m.tx, m.ty));
            bool go = false;
            if (m.other < 0 || game().vehs[m.other].faction_id == (uint32_t)m.fac) {
                // L_571b51: a monolith.
                if ((sq_at(m.tx, m.ty)->items & BIT_MONOLITH) && !(v.state & VSTATE_MONOLITH_UPGRADED) &&
                    want_monolith(m.veh)) {
                    m.s38 += 1000;
                    go = true;
                }
            } else if (m.s44) {
                // Empaths hunt native or psi units.
                const VEH& o = game().vehs[m.other];
                if (o.faction_id != 0) {
                    if (stack_check(m.other, 1, -1, -1, -1) > 1) continue;
                    if (!(dstat(m.fac, o.faction_id) & 0x10)) continue;
                    if (Weapon[unit_of(m.other).weapon_id].offense_value >= 0) continue;
                }
                if (veh_triad_of(m.other) == TRIAD_LAND) go = true;
            }
            if (!go) continue;
            if (m.s38 < 9999) {
                m.s38 = 9999;
                int32_t type = 0x46;
                if (v.home_base_id >= 0) {
                    m.base_id = v.home_base_id;
                    if ((game().bases[m.base_id].state_flags & 0x4000000) && v.status_icon != 0x46) type = 0x79;
                }
                go_to(m.veh, type, m.tx, m.ty);
            }
        }
        if (v.order == ORDER_MOVE_TO) {
            m.stay = 0;
            m.defend = 0;
            return F_57707f;
        }
        if (m.defend) return F_57707f;
        if (m.s44) {
            for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
                const BASE& base = game().bases[m.base_id];
                if (base_faction(m.base_id) != m.fac || tile_region(base.x, base.y) != m.region) continue;
                int32_t d = vdist(m.x, m.y, base.x, base.y);
                if (m.base_id != v.home_base_id) d += 1000;
                m.s38 = d;
                if (stack_check(veh_at(base.x, base.y), 0x11, 0x13, m.fac, -1)) {
                    d *= 2;
                    m.s38 = d;
                }
                if (d < 9999) {
                    m.s38 = 9999;
                    go_to(m.veh, 0x66, base.x, base.y);
                }
            }
        }
    }
    // L_571d57: psi gates to a region with the same plan.
    int32_t bo = m.base_own;
    if (m.plan != m.region_plan && m.region_plan > 2 && m.dist_own == 0 && has_fac(FAC_PSI_GATE, bo, 0) &&
        !(game().bases[bo].state_flags & BSTATE_PSI_GATE_USED)) {
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            COMPAT_ORIGINAL(psi_gate_own_base);
            if (!has_fac(FAC_PSI_GATE, bo, 0)) continue;  // BUG(original): tests the own base again
            if (base.state_flags & BSTATE_PSI_GATE_USED) continue;
            int32_t r = tile_region(base.x, base.y);
            if (r == m.region) continue;
            uint8_t rp = m.f().region_base_plan[r];
            if (!(rp == m.plan || (m.plan <= 2 && rp <= 2))) continue;
            if (!m.mp_own) {
                log_say_values("Enemy doing GATE", m.veh, m.base_id, 0);
                action_gate(m.veh, m.base_id);
                return RET1;
            }
            if (!NetDaemon_add_lock(&NetState, 0, base.x, base.y)) {
                message_veh(0x2410, m.veh, m.base_id, 0);
                return F_578d67;
            }
        }
    }
    // L_571e8e
    if (!m.st4 && !m.st6 && m.x == m.f().target_x && m.y == m.f().target_y) {
        const UNIT& u = unit_of(m.veh);
        if ((u.plan == 0 || u.plan == 1) && Chassis[u.chassis_id].triad == TRIAD_LAND &&
            !has_abil(v.unit_id, ABL_DROP_POD))
            v.state |= 0x40000;
    }
    if ((m.plan > PLAN_COMBAT && !m.st6) || v.order != ORDER_MOVE_TO) return F_572922;
    m.s4c = std::abs(m.y - v.waypoint_y[0]);
    if ((hdist2(m.x, m.y, v.waypoint_x[0], v.waypoint_y[0]) & ~1) != 4) return F_572922;
    m.base_id = base_at(v.waypoint_x[0], v.waypoint_y[0]);
    if (m.base_id < 0) return F_572922;
    return em_siege(m);
}

// 0x571F96: a stack two tiles from an enemy base gathers before the attack.
Flow em_siege(EM& m) {
    VEH& v = m.v();
    int32_t b = m.base_id;
    m.s6c = b * 0x134;
    m.s30 = m.fac * 0x833;
    if (!(dstat(m.fac, base_faction(b)) & 0x10)) return F_572922;
    BASE& base = game().bases[b];
    base.event_flags |= (uint32_t)m.s68;
    m.s2c = 0;  // own combat units next to the base
    m.s54 = 0;  // fresh units two tiles away
    m.s44 = 0;  // moved units two tiles away
    auto attacker = [&](int32_t o) {
        return game().vehs[o].faction_id == (uint32_t)m.fac && (uint8_t)unit_plan(o) <= 1 &&
               !has_abil(game().vehs[o].unit_id, 0x8000);
    };
    for (int32_t i = 1; i < 9; i++) {
        m.tx = map_wrap_x(TableOffsetX[i] + base.x);
        m.ty = TableOffsetY[i] + base.y;
        if (!on_map(m.tx, m.ty)) continue;
        m.other = stack_fix(veh_at(m.tx, m.ty));
        if (m.other < 0) continue;
        for (m.other = veh_top(m.other); m.other >= 0; m.other = game().vehs[m.other].next_veh_id_stack) {
            if (attacker(m.other)) m.s2c++;
        }
    }
    for (int32_t i = 9; i < 25; i++) {
        m.tx = map_wrap_x(TableOffsetX[i] + base.x);
        m.ty = TableOffsetY[i] + base.y;
        if (!on_map(m.tx, m.ty)) continue;
        m.other = stack_fix(veh_at(m.tx, m.ty));
        if (m.other < 0) continue;
        for (m.other = veh_top(m.other); m.other >= 0; m.other = game().vehs[m.other].next_veh_id_stack) {
            if (!attacker(m.other)) continue;
            const VEH& o = game().vehs[m.other];
            if ((o.status_icon >= 0x33 && o.status_icon <= 0x35) ||
                (o.waypoint_x[0] == v.waypoint_x[0] && o.waypoint_y[0] == v.waypoint_y[0])) {
                if (o.moves_spent == 0) m.s54++;
                else m.s44++;
            }
        }
    }
    for (int32_t i = 25; i < 25 + 0x90; i++) {
        m.tx = map_wrap_x(TableOffsetX[i] + base.x);
        m.ty = TableOffsetY[i] + base.y;
        if (!on_map(m.tx, m.ty)) continue;
        m.other = stack_fix(veh_at(m.tx, m.ty));
        if (m.other < 0) continue;
        for (m.other = veh_top(m.other); m.other >= 0; m.other = game().vehs[m.other].next_veh_id_stack) {
            if (!attacker(m.other)) continue;
            const VEH& o = game().vehs[m.other];
            // BUG(original): the upper bound is tested on this unit's status, not the other's.
            COMPAT_ORIGINAL(status_icon_wrong_unit);
            if (o.status_icon >= 0x33 && v.status_icon <= 0x35) {
                m.s44++;
            } else if ((base_who(m.tx, m.ty) < 0 || (o.state & 0x10000)) && o.waypoint_x[0] == v.waypoint_x[0] &&
                       o.waypoint_y[0] == v.waypoint_y[0]) {
                m.s44++;
            }
        }
    }
    if (m.s54 + m.s2c < 8 && m.s44 > m.s2c) {
        // Wait for the others: step to a tile two from the base.
        m.s30 = stack_check(m.veh, 2, 1, -1, -1) + stack_check(m.veh, 2, 0, -1, -1);
        if (zoc_move(m.x, m.y, m.fac) || m.s30 <= 1) {
            m.stay = 1;
            v.status_icon = 0x34;
            return F_57707f;
        }
        m.best_idx = -1;
        m.best = 0;
        for (m.s2c = 0; m.s2c < 8; m.s2c++) {
            m.tx = map_wrap_x(m.x + NearOffsetX[m.s2c]);
            m.ty = NearOffsetY[m.s2c] + m.y;
            if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
            if ((hdist2(m.tx, m.ty, base.x, base.y) & ~1) != 4) continue;
            m.s70 = anything_at(m.tx, m.ty);
            if (m.s70 >= 0 && m.s70 != m.fac) continue;
            int32_t score = 10;
            if (zoc_sea(m.tx, m.ty, m.fac)) {
                if (hex_cost(m.unit, m.fac, m.x, m.y, m.tx, m.ty, 0) >= veh_moves(m.veh) - 1) continue;
                score = 20;
            }
            int32_t o = veh_at(m.tx, m.ty);
            if (o >= 0) {
                int32_t k = stack_check(o, 2, 1, -1, -1) + stack_check(o, 2, 0, -1, -1);
                if (k >= m.s30) continue;
                score -= k;
            }
            if (score > m.best) {
                m.best = score;
                m.best_idx = m.s2c;
            }
        }
        if (m.best_idx >= 0) {
            m.best_dir = m.best_idx;
            v.status_icon = 0x35;
            return F_578b29;
        }
        m.stay = 1;
        v.status_icon = 0x34;
        return F_57707f;
    }
    // L_5725e2: attack position next to the base: the best defended tile.
    if (zoc_move(m.x, m.y, m.fac)) return F_572922;
    m.best = -9999;
    for (m.s2c = 0; m.s2c < 8; m.s2c++) {
        m.tx = map_wrap_x(m.x + NearOffsetX[m.s2c]);
        m.ty = NearOffsetY[m.s2c] + m.y;
        if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
        if ((hdist2(m.tx, m.ty, base.x, base.y) & ~1) != 2) continue;
        m.val = defense_value(m.fac, m.tx, m.ty, m.veh, -1) << 3;
        for (int32_t i = 0; i < 8; i++) {
            int32_t ax = map_wrap_x(m.tx + NearOffsetX[i]);
            int32_t ay = NearOffsetY[i] + m.ty;
            if (!on_map(ax, ay) || (ax == base.x && ay == base.y)) continue;
            m.other = stack_fix(veh_at(ax, ay));
            if (m.other < 0) continue;
            int32_t of = game().vehs[m.other].faction_id;
            if (of == m.fac || (dstat(m.fac, of) & 7)) continue;
            if (stack_check(m.other, 4, -1, -1, -1)) m.val /= 2;
        }
        int32_t score = m.val;
        if (v.moves_spent == 0 && (sq_at(m.x, m.y)->items & BIT_ROAD) && (sq_at(m.tx, m.ty)->items & BIT_ROAD))
            score += Chassis[unit_of(m.veh).chassis_id].speed > 1 ? 0x100 : 8;
        MAP* t = sq_at(m.tx, m.ty);
        if ((t->items & BIT_FUNGUS) && (t->climate & 0xE0) >= 0x40) {
            if (!has_project(FAC_XENOEMPATHY_DOME - SP_ID_First, m.fac) &&
                (veh_offense(m.veh) >= 0 || m.unit >= 0x40)) {  // Xenoempathy Dome
                if (!has_tech(Rules.tech_preq_ease_fungus_mov, m.fac)) score -= 0x100;
            } else if (v.unit_id < 0x40 && (veh_offense(m.veh) < 0 || v.unit_id == BSC_SPORE_LAUNCHER)) {
                score += 0x100;
            } else {
                score += 8;
            }
        }
        m.other = veh_at(m.tx, m.ty);
        score -= stack_check(m.other, 1, -1, -1, -1) * 8;
        if (score >= m.best) {
            m.best = score;
            m.best_dir = m.s2c;
        }
    }
    if (m.best_dir >= 0) {
        v.status_icon = 0x33;
        return F_578b29;
    }
    return F_572922;
}

// ---- formers (0x572922-0x5741CF) -----------------------------------------------------------------

// Whether another former of the faction is on (or heading to) (x, y). Leaves the index in m.other.
bool former_taken(EM& m, int32_t x, int32_t y) {
    for (m.other = 0; m.other < game().veh_count; m.other++) {
        const VEH& o = game().vehs[m.other];
        if (o.faction_id != (uint32_t)m.fac || unit_plan(m.other) != PLAN_TERRAFORM || m.other == m.veh) continue;
        if ((o.x == x && o.y == y) || (o.order == 0x18 && o.waypoint_x[0] == x && o.waypoint_y[0] == y)) return true;
    }
    return false;
}

// L_572922: formers: terraforming goals, the work on the current tile, then a tile near a base.
Flow em_former(EM& m) {
    VEH& v = m.v();
    if (m.st4000 || (m.plan & 0xF) != 9) return F_5741cf;
    if (m.zocv == 1 || (m.zocv != 0 && m.human() && (dstat(m.fac, m.zocv - 1) & 0x10))) return F_57707f;
    m.s30 = ((v.state & VSTATE_ON_ALERT) && unit_plan(m.veh) == PLAN_TERRAFORM &&
             v.order_auto_type == ORDERA_TERRA_AUTOIMPROVE_BASE)
                ? 1
                : 0;
    m.s4c = 0;
    int32_t b;  // edi: the base whose area is worked
    if (v.order == ORDER_MOVE_TO) {
        b = m.base_id;
    } else {
        bool check_home;
        if (m.s30 == 0 && m.base_own >= 0) {
            b = m.base_own;
            m.base_id = b;
            check_home = true;
        } else {
            b = v.home_base_id;
            m.base_id = b;
            if (m.s30 == 0) {
                check_home = true;
            } else {
                check_home = false;
                if (b < 0) {
                    b = m.base_own;
                    m.base_id = b;
                }
            }
        }
        if (check_home) {
            // L_5729eb: the home base if it is not much farther.
            int32_t home = v.home_base_id;
            if (home >= 0 && b != home) {
                if (b < 0) goto L_572d66;
                if (vdist(m.x, m.y, game().bases[home].x, game().bases[home].y) < m.dist_own * 2) {
                    b = v.home_base_id;
                    m.base_id = b;
                }
            }
        }
        // L_572a5c: terraforming goals of the faction near the base.
        if (b >= 0) {
            for (m.s44 = 0; m.s44 < 75; m.s44++) {
                const Goal& g = m.f().goals[m.s44];
                if (g.type < 0 || (g.type & 0xF) != 9 || g.type == 0x29) continue;
                if (!on_map(g.x, g.y) || g.priority == 0) continue;
                m.s60 = g.x;
                if (vdist(g.x, g.y, game().bases[b].x, game().bases[b].y) > 2 && !(m.s30 == 0 && g.base_id == b))
                    continue;
                m.s60 = m.x;
                if ((hdist2(m.x, m.y, g.x, g.y) & ~1) == 0) {
                    // At the goal.
                    v.order = ORDER_NONE;
                    m.s4c = 1;
                    if (m.dist_own == 0) return F_578b34;
                    goto L_572cd6;
                }
                MAP* sq = sq_at(g.x, g.y);
                int32_t ocean = sq_ocean_i(sq);
                if (ocean != (m.triad == TRIAD_SEA)) continue;
                if (sq->region != m.region && m.at_sea == ocean) continue;
                m.s70 = anything_at(g.x, g.y);
                if (m.s70 >= 0 && m.s70 != m.fac && !(dstat(m.fac, m.s70) & 1)) continue;
                if (former_taken(m, g.x, g.y)) continue;
                go_to(m.veh, 0x67, m.f().goals[m.s44].x, m.f().goals[m.s44].y);
                break;
            }
            b = m.base_id;
        }
    }
L_572cd6:
    if (m.s30 && !m.s4c) {
        m.s4c = std::abs(m.y - game().bases[b].y);
        if (((m.s4c + x_dist(m.x, game().bases[b].x)) & ~1) > 4 && v.order != ORDER_MOVE_TO) {
            go_to(m.veh, 0x68, game().bases[b].x, game().bases[b].y);
            if (v.order == ORDER_MOVE_TO) return F_57707f;
        }
    }
L_572d66:
    if (m.triad == TRIAD_SEA && v.order != ORDER_MOVE_TO) {
        // Sea formers: the best shelf tile around an own base.
        m.best = 9999999;
        m.base_id = 0;
        for (; m.base_id < game().base_count; m.base_id++) {
            m.s44 = 0x97D042 + m.base_id * 0x134;  // the original keeps a pointer here
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            if (m.s30) {
                if (v.home_base_id >= 0 ? m.base_id != v.home_base_id : m.base_id != m.base_own) continue;
            }
            for (m.s6c = 0; m.s6c < 0x54; m.s6c += 4) {
                int32_t i = m.s6c / 4;
                m.tx = map_wrap_x(TableOffsetX[i] + base.x);
                m.ty = TableOffsetY[i] + base.y;
                if (!on_map(m.tx, m.ty)) continue;
                int32_t alt = sq_at(m.tx, m.ty)->climate & 0xE0;
                if (alt >= 0x60 || alt < 0x40) continue;
                m.s70 = anything_at(m.tx, m.ty);
                if (m.s70 >= 0 && m.s70 != m.fac && !(dstat(m.fac, m.s70) & 1)) continue;
                if (!can_terraform(m.fac, m.tx, m.ty, 0, m.base_id)) continue;
                int32_t d = vdist(m.x, m.y, m.tx, m.ty) << 3;
                m.s38 = d;
                if (sq_ocean(sq_at(base.x, base.y))) {
                    d /= 2;
                    m.s38 = d;
                }
                if (m.base_id == v.home_base_id) {
                    d /= 2;
                    m.s38 = d;
                }
                if (bonus_at(m.tx, m.ty)) {
                    d /= 2;
                    m.s38 = d;
                }
                if (former_taken(m, m.tx, m.ty)) continue;
                if (d < m.best) {
                    m.best = d;
                    go_to(m.veh, 0x6F, m.tx, m.ty);
                }
            }
        }
    }
    // L_57302d: without a base, formers follow a colony pod.
    m.s4c = 0;
    if (m.f().base_count == 0) {
        for (m.other = 0; m.other < game().veh_count; m.other++) {
            if (game().vehs[m.other].faction_id == (uint32_t)m.fac && unit_plan(m.other) == PLAN_COLONY) {
                go_to(m.veh, 0x23, game().vehs[m.other].x, game().vehs[m.other].y);
                return F_57707f;
            }
        }
    }
    {
        int32_t flag = (at_goal(m.fac, 9, m.x, m.y) || m.s30) ? 1 : 0;
        m.s44 = can_terraform(m.fac, m.x, m.y, flag, m.base_own);
    }
    if (m.s84 == 0 && (veh_triad_of(m.veh) == TRIAD_AIR || m.at_sea == (veh_triad_of(m.veh) == TRIAD_SEA)) &&
        m.dist_own != 0) {
        // Work on the current tile.
        int32_t sea = m.at_sea;
        del_site(m.fac, sea ? 0x19 : 9, m.x, m.y, 0);
        if (at_goal(m.fac, 0x79, m.x, m.y) && !is_sensor(m.x, m.y)) {
            v.order = ORDER_SENSOR_ARRAY;
            return F_578f46;
        }
        if (at_goal(m.fac, 0x29, m.x, m.y) && !m.s30 && m.sa4 <= 5) {
            // Raise the land (paid from the reserves).
            int32_t cost = range(terraform_cost(m.x, m.y, m.fac), 0, m.f().energy_credits / 4);
            m.f().energy_credits = range(m.f().energy_credits - cost, 0, 99999);
            v.order = ORDER_TERRAFORM_UP;
            del_site(m.fac, 8, m.x, m.y, 1);
            return F_578f46;
        }
        if (at_goal(m.fac, 0x49, m.x, m.y) && terrain_avail(12, sea, m.fac)) {
            v.order = ORDER_CONDENSER;
            return F_578f46;
        }
        if (at_goal(m.fac, 0xD, m.x, m.y) && terrain_avail(13, sea, m.fac)) {
            v.order = ORDER_ECHELON_MIRROR;
            return F_578f46;
        }
        if (at_goal(m.fac, 0x69, m.x, m.y) && terrain_avail(14, sea, m.fac)) {
            v.order = ORDER_THERMAL_BOREHOLE;
            return F_578f46;
        }
        if (!sea && !m.s30 && !m.human() && terrain_avail(16, sea, m.fac)) {
            int32_t tiles = Continents[m.region].tile_count;
            if ((tiles < 50 || (tiles < 150 && BaseFindDist >= 4)) && m.dist_any >= 2 &&
                (sq_at(m.x, m.y)->climate & 0xE0) < 0xC0) {
                bool high = false;
                for (int32_t i = 0; i < 49; i++) {
                    m.tx = map_wrap_x(m.x + TableOffsetX[i]);
                    m.ty = TableOffsetY[i] + m.y;
                    if (on_map(m.tx, m.ty) && (sq_at(m.tx, m.ty)->climate & 0xE0) >= 0xC0) {
                        high = true;
                        break;
                    }
                }
                if (!high) {
                    v.order = ORDER_TERRAFORM_UP;
                    return F_578f46;
                }
            }
        }
        // L_5733d7
        int32_t bo = m.base_own;
        if (m.dist_own <= 2) {
            MAP* sq = sq_at(m.x, m.y);
            uint32_t items = sq->items;
            if (((items & 0x10) || (items & 0x200000)) && !(items & 4) &&
                !((items & 0x20) && (sq->climate & 0xE0) >= 0x40) && !m.at_sea &&
                (!m.human() || !(GameMorePreferences & MPREF_AUTO_FORMER_CANT_BUILD_ROADS))) {
                v.order = ORDER_ROAD;
                return F_578f46;
            }
            m.s54 = radius_move_tiles(game().bases[bo].x, game().bases[bo].y, m.x, m.y, 0x15);
        } else {
            m.s54 = 0;
        }
        if (at_goal(m.fac, m.at_sea ? 0x19 : 9, m.x, m.y) || (m.x == v.waypoint_x[0] && m.y == v.waypoint_y[0]) ||
            stack_check(m.veh, 9, 3, -1, -1) || (m.s54 != 0 && (game().bases[bo].worked_tiles & (1u << (m.s54 & 31)))))
            m.s2c = 1;
        else m.s2c = 0;
        MAP* sq = sq_at(m.x, m.y);
        if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 && m.s44 &&
            (has_abil(v.unit_id, ABL_FUNGICIDAL) || has_tech(tf_preq(10, sq_ocean_i(sq_at(m.x, m.y))), m.fac)) &&
            (m.s2c || (m.dist_own <= 2 && (game().bases[bo].eco_damage != 0 || game().bases[bo].pop_size >= 6)))) {
            v.order = ORDER_REMOVE_FUNGUS;
            return F_578f46;
        }
        if (m.s44 == 4 && (m.s2c || (m.s54 != 0 && game().bases[bo].worked_tiles != 0))) {
            v.order = ORDER_FARM;
            v.status_icon = 0x2B;
            return F_578f46;
        }
        bool to_762 = true;
        if (!m.at_sea && (!m.human() || !(GameMorePreferences & MPREF_AUTO_FORMER_CANT_BUILD_ROADS))) {
            bool to_6cb = false;
            if (m.s2c) {
                to_6cb = true;
            } else if (m.dist_own <= 2) {
                uint32_t it = sq_at(m.x, m.y)->items;
                if ((it & 0x10) || (it & 0x8000)) {
                    to_6cb = true;
                } else if (!(it & 0x200000) && m.s44 == 0) {
                    to_762 = false;  // L_5737ad
                } else {
                    MAP* t = sq_at(m.x, m.y);
                    to_6cb = !((t->items & BIT_FUNGUS) && (t->climate & 0xE0) >= 0x40);
                }
            }
            if (to_6cb) {
                // L_5736cb: roads (mag tubes).
                MAP* t = sq_at(m.x, m.y);
                uint32_t it = t->items;
                if (it & 4) {
                    if (has_tech(Terraform[6].preq_tech, m.fac) && m.s2c && !(it & 8)) {
                        v.order = ORDER_ROAD;
                        v.status_icon = 0x52;
                        return F_578f46;
                    }
                } else if (!((it & 0x20) && (t->climate & 0xE0) >= 0x40) ||
                           has_tech(Rules.tech_preq_improv_fungus, m.fac)) {
                    v.order = ORDER_ROAD;
                    v.status_icon = 0x72;
                    return F_578f46;
                }
            }
        }
        if (to_762) {
            // L_573762
            if (m.s44 && (m.dist_own <= 2 || stack_check(m.veh, 9, 3, -1, -1)) && (m.s2c || v.order == ORDER_NONE)) {
                v.order = (uint8_t)m.s44;
                v.status_icon = 0x2B;
                return F_578f46;
            }
        }
        // L_5737ad: sensors.
        if ((!m.human() || (GameMorePreferences & MPREF_AUTO_FORMER_BUILD_SENSORS)) &&
            (m.s2c || v.order == ORDER_NONE)) {
            if (is_sensor(m.x, m.y) && whose_territory(m.fac, m.x, m.y, nullptr, 0) == m.fac && m.dist_own > 1 &&
                !(sq_at(m.x, m.y)->items & 0x800) && has_tech(Terraform[7].preq_tech, m.fac)) {
                v.order = ORDER_BUNKER;
                return F_578f46;
            }
            if (good_sensor(m.fac, m.x, m.y)) {
                v.order = ORDER_SENSOR_ARRAY;
                return F_578f46;
            }
            if (m.dist_own > 1 && m.dist_own <= 3) {
                uint32_t it = sq_at(m.x, m.y)->items;
                if ((it & 4) && !(it & 0x800) && has_tech(Terraform[7].preq_tech, m.fac)) {
                    bool near_bunker = false;
                    for (int32_t i = 0; i < 9; i++) {
                        m.tx = map_wrap_x(m.x + TableOffsetX[i]);
                        m.ty = TableOffsetY[i] + m.y;
                        if (on_map(m.tx, m.ty) && (sq_at(m.tx, m.ty)->items & 0x800)) {
                            near_bunker = true;
                            break;
                        }
                    }
                    if (!near_bunker) {
                        v.order = ORDER_BUNKER;
                        return F_578f46;
                    }
                }
            }
            if (m.s2c && has_tech(Terraform[9].preq_tech, m.fac)) {
                m.tx = m.x;
                m.ty = m.y;
                m.s4c = 1;
                if (!Path_sensors(&Paths, m.fac, &m.tx, &m.ty) && m.x == m.tx && m.y == m.ty) {
                    v.order = ORDER_SENSOR_ARRAY;
                    return F_578f46;
                }
            }
        }
    }
    // L_5739aa: a tile near a base to improve.
    if (v.order == ORDER_MOVE_TO || m.s84) return F_5741cf;
    // BUG(original): the base may be -1 (no home base, no own base): Bases[-1] (see base_raw).
    COMPAT_ORIGINAL(out_of_range_reads);
    if (m.s30) {
        m.base_id = v.home_base_id;
        m.sa4 = base_x_raw(m.base_id);
        m.s58 = base_y_raw(m.base_id);
    } else {
        m.base_id = m.base_own;
        if (v.state & VSTATE_ON_ALERT) {
            m.sa4 = base_x_raw(m.base_id);
            m.s58 = base_y_raw(m.base_id);
        } else {
            m.sa4 = m.x;
            m.s58 = m.y;
        }
        m.val = 0;
        m.s60 = m.base_id * 0x134;
        const int32_t bx = base_x_raw(m.base_id), by = base_y_raw(m.base_id);
        for (int32_t i = 1; i < 21; i++) {
            m.tx = map_wrap_x(TableOffsetX[i] + bx);
            m.ty = TableOffsetY[i] + by;
            if (!on_map(m.tx, m.ty) || tile_region(m.tx, m.ty) != m.region) continue;
            m.other = veh_at(m.tx, m.ty);
            if (m.other < 0 || game().vehs[m.other].faction_id != (uint32_t)m.fac) continue;
            m.val += stack_check(m.other, 2, 9, -1, -1);
            if (m.tx == m.x && m.ty == m.y) m.val--;
        }
        if (m.val != 0) {
            // Other formers already there: the base of the region with the fewest.
            m.best = 9999;
            for (m.s54 = 0; m.s54 < game().base_count; m.s54++) {
                m.s6c = 0x97D042 + m.s54 * 0x134;  // the original keeps a pointer here
                const BASE& base = game().bases[m.s54];
                if (base_faction(m.s54) != m.fac || tile_region(base.x, base.y) != m.region) continue;
                int32_t n = 0;
                for (m.s44 = 0; m.s44 < 0x64; m.s44 += 4) {
                    int32_t i = m.s44 / 4;
                    int32_t ax = map_wrap_x(TableOffsetX[i] + base.x);
                    int32_t ay = TableOffsetY[i] + base.y;
                    if (!on_map(ax, ay) || tile_region(ax, ay) != m.region) continue;
                    m.other = veh_at(ax, ay);
                    if (m.other >= 0) n += stack_check(m.other, 2, 9, -1, -1);
                }
                if (n < m.val) {
                    n++;
                    m.s38 = vdist(m.x, m.y, base.x, base.y) * n * n;
                    if (m.s38 < m.best) {
                        m.best = m.s38;
                        m.base_id = m.s54;
                    }
                }
            }
        }
    }
    // L_573ca0
    if (m.base_id >= 0) {
        m.best = 9999;
        for (m.s2c = 1; m.s2c < 21; m.s2c++) {
            int32_t i = (m.s2c + CurrentTurn) % 21;
            m.tx = map_wrap_x(m.sa4 + TableOffsetX[i]);
            m.ty = TableOffsetY[i] + m.s58;
            if (!on_map(m.tx, m.ty) || base_who(m.tx, m.ty) >= 0) continue;
            MAP* sq = sq_at(m.tx, m.ty);
            int32_t ocean = sq_ocean_i(sq);
            if (ocean != (m.triad == TRIAD_SEA)) continue;
            if (sq->region != m.region && m.at_sea == ocean) continue;
            if (zoc_move(m.tx, m.ty, m.fac)) continue;
            m.other = veh_at(m.tx, m.ty);
            if (m.other >= 0 &&
                (game().vehs[m.other].faction_id != (uint32_t)m.fac || stack_check(m.other, 2, 9, -1, -1)))
                continue;
            if (!can_terraform(m.fac, m.tx, m.ty, (m.human() && m.s30) ? 1 : 0, m.base_id)) continue;
            const BASE& base = game().bases[m.base_id];
            int32_t d = vdist(base.x, base.y, m.tx, m.ty);
            m.val = d;
            if (d > 2) continue;
            if (!(base.worked_tiles & (1u << (m.s2c & 31)))) {
                MAP* t = sq_at(m.tx, m.ty);
                if (!((t->items & BIT_FUNGUS) && (t->climate & 0xE0) >= 0x40)) {
                    d *= 3;
                    m.val = d;
                }
            }
            if (d >= m.best) continue;
            if (former_taken(m, m.tx, m.ty)) continue;
            m.best = d;
            go_to(m.veh, 0x77, m.tx, m.ty);
        }
        if (v.order == ORDER_MOVE_TO) return F_57707f;
    }
    // L_573f61: a sensor site.
    if (m.s4c == 0 && (!m.human() || (GameMorePreferences & MPREF_AUTO_FORMER_BUILD_SENSORS))) {
        m.tx = m.x;
        m.ty = m.y;
        if (!Path_sensors(&Paths, m.fac, &m.tx, &m.ty)) {
            if (m.tx == m.x && m.ty == m.y) {
                v.order = ORDER_SENSOR_ARRAY;
                return F_578f46;
            }
            if (!(m.s30 && vdist(m.x, m.y, m.tx, m.ty) > 2) && !former_taken(m, m.tx, m.ty)) {
                go_to(m.veh, 0x73, m.tx, m.ty);
                return F_57707f;
            }
        }
    }
    // L_57408e: back to the home base area.
    if (m.dist_own != 0 && !m.at_sea && m.f().region_total_bases[m.region] != 0) {
        int32_t b2 = v.home_base_id >= 0 ? (int32_t)v.home_base_id : m.base_own;
        if (b2 >= 0) {
            if (base_region(b2) != m.region) b2 = m.base_own;
            if (b2 >= 0) {
                int32_t d = vdist(m.x, m.y, game().bases[b2].x, game().bases[b2].y);
                if (d > (CurrentTurn <= 50) + 3 || m.zocm) {
                    go_to(m.veh, 0x62, game().bases[b2].x, game().bases[b2].y);
                    return F_57707f;
                }
            }
        }
    }
    if (m.s30 && v.order == ORDER_NONE) {
        v.state &= ~VSTATE_ON_ALERT;
        v.order = ORDER_NONE;
        return RET0;
    }
    return F_5741cf;
}

// ---- coast and border patrols, exploration (0x5741CF-0x574A7E) ----------------------------------

// C remainder of the original's `& 0x80000007` idiom for (a mod 8).
inline int32_t rem8(int32_t a) { return a % 8; }

// L_5741cf: land units of an explored region patrol its coast or borders, or move to the tile
// of own territory farthest from the frontier.
Flow em_patrol(EM& m) {
    VEH& v = m.v();
    if (m.region_plan > 2 && m.f().region_visible_tiles[m.region] >= Continents[m.region].tile_count &&
        m.plan <= PLAN_RECON && m.triad == TRIAD_LAND && v.order != ORDER_MOVE_TO && !m.at_sea) {
        if (coast_or_border(m.x, m.y, m.x, m.y, m.fac)) {
            int32_t d = rem8(v.rotate_angle + 4);
            m.tx = map_wrap_x(m.x + NearOffsetX[d]);
            m.ty = NearOffsetY[d] + m.y;
            if (!coast_or_border(m.tx, m.ty, m.tx, m.ty, m.fac)) {
                if (m.veh & 1) v.state |= 0x80000000u;
                else v.state &= 0x7FFFFFFFu;
            }
            // Walk along the edge: first leave it, then step back onto own land.
            m.s30 = 0;
            m.s6c = 0;
            for (m.s4c = 3; m.s4c > -5; m.s4c--, m.s6c++) {
                int32_t a = m.s4c;
                if (v.state & 0x80000000u) a = m.s6c - 3;
                d = rem8(v.rotate_angle + a + 8);
                m.tx = map_wrap_x(m.x + NearOffsetX[d]);
                m.ty = NearOffsetY[d] + m.y;
                if (!m.s30) {
                    if (on_map(m.tx, m.ty) && (tile_region(m.tx, m.ty) != m.region ||
                                               whose_territory(m.fac, m.tx, m.ty, nullptr, 0) != m.fac))
                        m.s30 = 1;
                } else if (on_map(m.tx, m.ty) && tile_region(m.tx, m.ty) == m.region &&
                           whose_territory(m.fac, m.tx, m.ty, nullptr, 0) == m.fac) {
                    m.best_dir = d;
                    v.status_icon = 0x70;
                    return F_578b29;
                }
            }
        }
        m.best = 0;
        for (m.ty = 0; m.ty < game().map.y; m.ty++) {
            for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                if (tile_region(m.tx, m.ty) != m.region) continue;
                if (whose_territory(m.fac, m.tx, m.ty, nullptr, 0) != m.fac) continue;
                m.val = 1;
                for (m.s58 = 0; m.s58 < 0x24; m.s58 += 4) {
                    int32_t i = m.s58 / 4;
                    int32_t ax = map_wrap_x(m.tx + TableOffsetX[i]);
                    int32_t ay = TableOffsetY[i] + m.ty;
                    if (!on_map(ax, ay)) continue;
                    if (tile_region(ax, ay) != m.region) {
                        m.val++;
                    } else {
                        if (whose_territory(m.fac, ax, ay, nullptr, 0) != m.fac) m.val += 3;
                        if (anything_at(ax, ay) == m.fac) m.val -= 6;
                    }
                }
                int32_t score = hdist(m.x, m.y, m.tx, m.ty) * m.val;
                m.other = 0;
                if (game().veh_count > 0) {
                    m.other = game().veh_count;
                    for (int32_t o = 0; o < game().veh_count; o++) {
                        const VEH& w = game().vehs[o];
                        if (w.faction_id == (uint32_t)m.fac && w.waypoint_x[0] == m.tx && w.waypoint_y[0] == m.ty)
                            score /= 2;
                    }
                    m.s4c = 0;
                }
                if (score > m.best) {
                    m.best = score;
                    go_to(m.veh, 0x50, m.tx, m.ty);
                }
            }
        }
    }
    // L_5745e9
    if (m.st4000 || (m.plan == PLAN_RECON && CurrentTurn < 100)) return F_57460a;
    return F_574a7e;
}

// L_57460a: exploration: the unexplored tile with the best ratio of new tiles to distance,
// searched ring by ring.
Flow em_explore(EM& m) {
    VEH& v = m.v();
    if (m.triad == TRIAD_AIR || (v.order == ORDER_MOVE_TO && v.status_icon == 0x2F)) return F_574a7e;
    m.s4c = 0;
    m.best = 0;
    m.s60 = 0;
    for (m.s54 = 0; m.s54 < 9; m.s54++) {
        m.s44 = 0;
        for (m.s2c = m.s54 ? TableRange[m.s54 - 1] : 0; m.s2c < TableRange[m.s54]; m.s2c++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
            m.ty = TableOffsetY[m.s2c] + m.y;
            if (!on_map(m.tx, m.ty) || tile_region(m.tx, m.ty) != m.region) continue;
            m.s70 = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
            if (m.s70 >= 0 && m.s70 != m.fac && (dstat(m.fac, m.s70) & 7)) continue;
            int32_t score;
            if (!is_known(m.tx, m.ty, m.fac)) score = 0;
            else score = goody_at(m.tx, m.ty) ? 0x200 : 0;
            // BUG(original): the nine "neighbours" are all the same tile (the ring offset is
            // added again instead of the neighbour offset).
            COMPAT_ORIGINAL(ring_offset_twice);
            for (m.s30 = 0; m.s30 < 9; m.s30++) {
                int32_t ax = map_wrap_x(m.tx + TableOffsetX[m.s2c]);
                int32_t ay = TableOffsetY[m.s2c] + m.ty;
                if (!on_map(ax, ay)) continue;
                if (is_known(ax, ay, m.fac)) {
                    m.s70 = veh_who(ax, ay);
                    if (m.s70 >= 0 && m.s70 != m.fac) {
                        score = 0;
                        break;
                    }
                } else {
                    score++;
                    if (tile_region(ax, ay) == m.region) score += 0xF;
                }
            }
            MAP* sq = sq_at(m.tx, m.ty);
            if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 && m.f().SE_planet_pending <= 0) score /= 2;
            m.sa4 = score;
            if (m.base_own >= 0) {
                const BASE& bo = game().bases[m.base_own];
                int32_t t = 16 - hdist(m.tx, m.ty, bo.x, bo.y);
                if (t < 0) t = 0;
                score += t;
            }
            m.val = (score << 8) / (hdist(m.x, m.y, m.tx, m.ty) + 2);
            if (m.val > m.s44) m.s44 = m.val;
            if (m.val <= m.best) continue;
            bool covered = false;
            for (m.other = 0; m.other < game().veh_count; m.other++) {
                const VEH& o = game().vehs[m.other];
                if (o.faction_id != (uint32_t)m.fac || m.other == m.veh || o.order != 0x18) continue;
                m.s38 = hdist(m.tx, m.ty, o.waypoint_x[0], o.waypoint_y[0]);
                if (m.s38 > 1) continue;
                if (hdist(m.x, m.y, m.tx, m.ty) >= hdist(o.x, o.y, m.tx, m.ty) - 1) {
                    covered = true;
                    break;
                }
            }
            if (covered || m.val == 0) continue;
            m.best = m.val;
            m.s4c = m.sa4;
            go_to(m.veh, 0x2F, m.tx, m.ty);
        }
        if (v.order == ORDER_MOVE_TO && m.s44 < m.s60 && m.s4c >= 0x10) break;
        m.s60 = m.s44;
    }
    if (v.order == ORDER_MOVE_TO) return F_57707f;
    return F_574a7e;
}

// ---- sea transports (0x574A88-0x575B27) ----------------------------------------------------------

Flow em_transport_unload(EM& m, int32_t found);

// L_574a88: sea units; transports manage their passengers: which ones stay aboard, where to pick
// up more, where to unload.
Flow em_transport(EM& m) {
    VEH& v = m.v();
    if (m.plan == PLAN_NAVAL_SUPERIORITY) del_site(m.fac, 6, m.x, m.y, 0);
    int32_t count = 0;  // ebx: passengers aboard that want to land
    m.s6c = 0;
    m.s44 = 0;  // passengers kept sentried
    m.s2c = 0;  // plan bits of those passengers
    m.s54 = 0;  // damaged passengers
    m.s30 = 0;
    if (m.plan != PLAN_NAVAL_TRANSPORT) {
        if (m.stay) return F_577071;
        return F_57666a;
    }
    if (m.dist_own == 0) stack_sort_2(m.veh);
    m.s58 = 0;
    m.other = veh_top(m.veh);
    if (m.other >= 0) {
        for (int32_t o = m.other; o >= 0;) {
            VEH& p = game().vehs[o];
            bool wake = false;
            if (o == m.veh || veh_triad_of(o) != TRIAD_LAND) goto next;
            if (veh_cargo(o)) goto next;
            if (p.faction_id != (uint32_t)m.fac) goto next;
            if ((p.state & 0x40000) && m.dist_any == 0 && !((p.state & 0x20000) && m.region_plan == 7)) goto next;
            {
                bool healthy = (int32_t)p.damage_taken <= proto_power(o) / 3;
                int32_t pplan = (uint8_t)Units[p.unit_id].plan;
                if (p.order == 2) {
                    // L_574d2e: sentried passengers in port.
                    if (m.dist_own != 0) goto next;
                    if (pplan == PLAN_ARTIFACT && !m.at_sea) goto next;
                    if (!healthy) goto next;
                    bool keep;
                    if (pplan > 8) {
                        keep = false;
                    } else if (pplan == PLAN_DEFENSE || (p.state & 0x10000000)) {
                        keep = true;
                    } else if (!stack_check(o, 2, 2, -1, -1) &&
                               (unit_plan(o) == PLAN_COMBAT || unit_plan(o) == PLAN_RECON)) {
                        keep = true;
                    } else {
                        keep = !stack_check(o, 0x10, -1, -1, -1);
                    }
                    if (keep) {
                        // BUG(original): m.s48 ([ebp-0x48]) is not written on most paths that
                        // reach here (uninitialised read #2).
                        COMPAT_ORIGINAL(residue.enemy_move_s48);
                        if (++m.s44 <= m.s48) goto next;
                        if (m.region_plan <= 2 && m.s58 != 0) goto next;
                        m.s58++;
                    }
                    wake = true;
                } else if (m.at_sea) {
                    if ((p.order == 1 && (p.waypoint_x[0] < 0 || p.waypoint_x[0] == m.veh)) ||
                        (p.order == 0 && m.dist_any != 0)) {
                        // Aboard: counts as wanting to land.
                        count++;
                        if (!healthy) m.s54++;
                        else if (pplan == PLAN_COMBAT) m.s2c |= 7;
                        else m.s2c |= 1 << (pplan & 31);
                        goto next;
                    }
                    goto L_574c42;
                } else if (pplan == PLAN_ARTIFACT) {
                    p.order = 2;
                    goto next;
                } else {
                L_574c42:
                    if (unit_plan(o) == PLAN_TERRAFORM) goto next;
                    COMPAT_ORIGINAL(residue.enemy_move_s48);
                    if (unit_plan(o) == PLAN_DEFENSE && m.s44 < m.s48) {  // BUG(original): see above
                        if (m.dist_own == 0) {
                            m.s44++;
                            p.order = 2;
                            goto next;
                        }
                    } else if (m.dist_own == 0 && !healthy) {
                        p.order = 2;
                        goto next;
                    }
                    // L_574ce8
                    count++;
                    p.order = 0;
                    if (!healthy) goto next;
                    if (pplan == PLAN_COMBAT) m.s2c |= 7;
                    else m.s2c |= 1 << (pplan & 31);
                    goto next;
                }
                if (wake) {
                    count++;
                    p.order = 0;
                    int32_t wp = unit_plan(o);
                    if (wp == PLAN_COMBAT) m.s2c |= 7;
                    else m.s2c |= 1 << (wp & 31);
                }
            }
        next:
            o = game().vehs[o].next_veh_id_stack;
            m.other = o;
        }
        m.s6c = count;
    }
    if (count == 0) {
        // L_574ca7: nothing to carry.
        if (v.order == ORDER_MOVE_TO &&
            (v.status_icon == 0x4A || v.status_icon == 0x72 || v.status_icon == 0x49 || v.status_icon == 0x45))
            v.order = ORDER_NONE;
    }
    // L_574e50
    if ((m.s2c & 0xF) || (m.s2c & 0x800)) {
        m.s30 = 2;
        if ((m.s2c & 0x100) && !(m.s2c & 3)) m.s30 = 1;
    }
    if ((v.status_icon == 0x45 || v.status_icon == 0x72 || v.status_icon == 0x49) &&
        (m.s30 == 0 || v.order != ORDER_MOVE_TO))
        v.status_icon = 0x2D;
    if (m.stay == 0 && !(v.order == ORDER_MOVE_TO && v.status_icon == 0x64)) {
        if (((m.s2c & 0x1000) && m.dist_any != 0) ||
            (m.s54 != 0 && m.s54 >= count && (m.dist_any <= 8 || CurrentTurn < 50))) {
            // Damaged passengers: to the nearest port.
            m.best = 9999;
            for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
                const BASE& base = game().bases[m.base_id];
                if (base_faction(m.base_id) != m.fac || !base_on_sea(m.base_id, m.region)) continue;
                int32_t d = vdist(m.x, m.y, base.x, base.y);
                m.s38 = d;
                if (d == 0) return F_578b34;
                if (base.state_flags & BSTATE_ARTIFACT_LINKED) {
                    d += 1000;
                    m.s38 = d;
                }
                if (d < m.best) {
                    m.best = d;
                    go_to(m.veh, 0x61, base.x, base.y);
                }
            }
        }
        // L_574fb5
        int32_t n = veh_cargo(m.veh) - m.s6c;
        if ((v.state & 0x20000000) && m.s6c == 0) n = 0;
        if (n == veh_cargo(m.veh) || (m.dist_own == 0 && n >= veh_cargo(m.veh) / 2 && !(m.s2c & 0x100))) {
            // Room aboard: a port whose region needs units.
            m.best = 9999;
            m.s60 = 0;
            for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
                const BASE& base = game().bases[m.base_id];
                if (base_faction(m.base_id) != m.fac || !is_port(m.base_id, 0)) continue;
                int32_t rr = tile_region(base.x, base.y);
                m.s68 = rr;
                if (m.f().region_base_plan[rr] == 2) continue;
                m.s4c = std::abs(m.y - base.y);
                m.s44 = (m.s4c + x_dist(m.x, base.x)) >> 1;
                m.s38 = m.s44;
                int32_t tgx = m.f().target_x;
                if (tgx >= 0 && tile_region(tgx, m.f().target_y) == rr) {
                    m.s4c = 1;
                    if (!(m.f().player_flags & 0x800)) continue;
                    if (base.x != tgx || base.y != m.f().target_y) continue;
                    int32_t need = (m.s44 == 0 || (v.waypoint_x[0] == base.x && v.waypoint_y[0] == base.y)) ? 2 : 1;
                    if (stack_check(veh_at(base.x, base.y), 2, PLAN_NAVAL_TRANSPORT, -1, -1) >= need) continue;
                } else {
                    if (base.ai_plan_status != 0 &&
                        !(v.order == ORDER_MOVE_TO && v.waypoint_x[0] == base.x && v.waypoint_y[0] == base.y)) {
                        if (m.dist_own != 0 || m.base_own != m.base_id ||
                            stack_check(m.veh, 2, PLAN_NAVAL_TRANSPORT, -1, -1) > 1)
                            continue;
                    }
                    int32_t need = (m.s44 == 0 || (v.waypoint_x[0] == base.x && v.waypoint_y[0] == base.y)) ? 2 : 1;
                    if (stack_check(veh_at(base.x, base.y), 2, PLAN_NAVAL_TRANSPORT, -1, -1) >= need) continue;
                    m.s4c = 0;
                }
                // L_575257
                if (m.dist_own == 0 ? !port_to_port(m.base_own, m.base_id) : !base_on_sea(m.base_id, m.region))
                    continue;
                const Faction& f = m.f();
                if (!((int32_t)f.region_total_combat_units[rr] > f.region_total_bases[rr] * 2 + 1 ||
                      (f.region_flags[rr] & 3) || (f.region_flags[rr] & 0x1800)))
                    continue;
                int32_t score = m.s44 + 4;
                m.s38 = score;
                if (f.region_base_plan[rr] != 7 && !(f.region_flags[rr] & 0x800) &&
                    !(v.waypoint_x[0] == base.x && v.waypoint_y[0] == base.y)) {
                    if ((CurrentTurn + m.base_id + m.veh) & 7) continue;
                    score *= 2;
                }
                if (stack_check(veh_at(base.x, base.y), 2, PLAN_NAVAL_TRANSPORT, m.fac, -1) > (m.s44 == 0)) score *= 2;
                score += f.region_total_bases[rr] * 2 - f.region_total_combat_units[rr];
                m.s38 = score;
                if (score < m.best) {
                    m.best_idx = m.base_id;
                    m.best = score;
                    m.s58 = f.region_base_plan[rr];
                    m.s60 = m.s4c;
                    go_to(m.veh, 0x65, base.x, base.y);
                }
            }
            int32_t want = (m.s60 != 0 || m.s58 == 7) ? 6 : 2;
            int32_t amount = range(want, 0, veh_cargo(m.veh));
            if (m.s60 != 0 || m.best <= veh_moves(m.veh) * amount / 2) assemble_passengers(m.best_idx, m.s58, amount);
        }
    }
    // L_575442
    uint8_t icon = v.status_icon;
    if (icon == 0x65 && v.waypoint_x[0] == m.x && v.waypoint_y[0] == m.y) game().bases[m.base_any].state_flags |= 1;
    if (m.s6c > m.s54 && icon != 0x45) {
        // L_57549b: a neighbouring land tile to unload onto.
        for (m.s58 = 0; m.s58 < 0x20; m.s58 += 4) {
            int32_t dir = m.s58 / 4;
            m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
            m.ty = NearOffsetY[dir] + m.y;
            if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
            int32_t rr = tile_region(m.tx, m.ty);
            m.s68 = rr;
            if ((rr & 0x3F) == 0x3F || (rr & 0x3F) == 0) continue;
            m.s54 = m.dmg > 2;
            m.s70 = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
            int32_t t = m.s70;
            if (t >= 0 && t != m.fac) {
                uint32_t d = dstat(m.fac, t);
                bool to_634 = false;
                if (d & 1) continue;
                if (d & 0xC10) {
                    if (m.s30) m.s54 = 1;
                } else {
                    if (d & 2) {
                        if (!is_human(t) || !m.s30 || CurrentTurn - game().factions[t].diplo_spoke[m.fac] < 0x10)
                            continue;
                        diplomacy_check(t, m.fac, 0);
                        if (DiploHostility == 0 && DiploGreatSatan == 0) continue;
                    } else if (d & 4) {
                        if (!is_human(t) || CurrentTurn - game().factions[t].diplo_spoke[m.fac] < 0x10) continue;
                    }
                    to_634 = true;
                }
                if (to_634 && !(dstat(m.fac, t) & 8) && m.s30 && !(m.s2c & 0x100)) m.s54 = 1;
            }
            // L_57565d
            m.s70 = veh_who(m.tx, m.ty);
            if (m.s70 >= 0 && m.s70 != m.fac) continue;
            const Faction& f = m.f();
            bool to_77c;
            if ((m.s2c & 0x100) && Continents[rr].open_terrain >= 4 &&
                (f.region_total_bases[rr] == 0 || f.region_base_plan[rr] == 8)) {
                // Colony pods aboard: unexplored land.
                if (m.s30 || f.region_base_plan[m.region] > 2) {
                    m.s54 = 1;
                    to_77c = false;
                } else {
                    to_77c = true;
                }
            } else {
                to_77c = false;
            }
            if (!to_77c) {
                // L_5756ce
                if (m.s30 == 0) {
                    to_77c = true;
                } else {
                    if (m.s54 == 0 && f.region_total_bases[rr] == 0 && f.region_base_plan[rr] == 8) {
                        for (m.s70 = 1; m.s70 < 8; m.s70++) {
                            uint32_t d = dstat(m.fac, m.s70);
                            if (game().factions[m.s70].region_total_bases[rr] != 0 &&
                                ((d & 0x10) || !(d & 8) ||
                                 CurrentTurn - game().factions[m.s70].diplo_spoke[m.fac] > 0x10))
                                m.s54 = 1;
                        }
                    }
                    if (f.region_base_plan[rr] <= 2) m.s54 = 1;
                    else to_77c = true;
                }
            }
            if (to_77c && m.s54 == 0) continue;
            // L_575787
            if (!(m.s2c & (1 << (f.region_base_plan[rr] & 31))) && m.dmg <= 2 && !(GameState & 0x100) &&
                (v.order != ORDER_MOVE_TO || tile_region(v.waypoint_x[0], v.waypoint_y[0]) != rr ||
                 v.status_icon == 0x65)) {
                // A passenger going home to this region.
                bool home_here = false;
                for (m.other = veh_top(m.veh); m.other >= 0;) {
                    const VEH& p = game().vehs[m.other];
                    if (veh_triad_of(m.other) == TRIAD_LAND && p.order == 1 &&
                        (p.waypoint_x[0] < 0 || p.waypoint_x[0] == m.veh) && p.home_base_id >= 0 &&
                        base_region(p.home_base_id) == rr) {
                        home_here = true;
                        break;
                    }
                    m.other = p.next_veh_id_stack;
                }
                if (home_here) {
                    // L_5758ee
                    m.s70 = zoc_veh(m.tx, m.ty, m.fac);
                    if (m.s70 <= 1) continue;
                    uint32_t d = dstat(m.fac, m.s70 - 1);
                    if (!((d & 0x10) && m.s30 && (m.s2c & 3)) && (d & 8)) continue;
                    if (m.s54 == 0) continue;
                    return em_transport_unload(m, -1);
                }
            }
            if (m.s54 == 0) continue;
            return em_transport_unload(m, m.s54);
        }
    }
    return F_575b27;
}

// L_575949: unload: land the passengers (towards the tile with the most land and bases nearby).
Flow em_transport_unload(EM& m, int32_t found) {
    VEH& v = m.v();
    m.best_dir = -1;
    if (veh_moves(m.veh) >= Rules.move_rate_roads && m.dist_any != 0 && found > 0) {
        m.best = -1;
        for (m.s30 = 0; m.s30 < 9; m.s30++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[m.s30]);
            m.ty = TableOffsetY[m.s30] + m.y;
            if (!on_map(m.tx, m.ty) || tile_region(m.tx, m.ty) != m.region) continue;
            if (veh_who(m.tx, m.ty) >= 0 && veh_who(m.tx, m.ty) != m.fac) continue;
            m.val = 0;
            for (int32_t i = 1; i < 9; i++) {
                int32_t ax = map_wrap_x(TableOffsetX[i] + m.tx);
                int32_t ay = TableOffsetY[i] + m.ty;
                if (!on_map(ax, ay) || tile_region(ax, ay) != m.s68) continue;
                if (base_who(ax, ay) > 0) m.val += 4;
                m.val++;
            }
            if (m.val > m.best) {
                m.best = m.val;
                m.best_dir = m.s30 - 1;
            }
        }
    }
    for (m.other = veh_top(m.veh); m.other >= 0; m.other = game().vehs[m.other].next_veh_id_stack) {
        if (veh_triad_of(m.other) == TRIAD_LAND) game().vehs[m.other].order = ORDER_NONE;
    }
    v.status_icon = 0x75;
    if (m.sb8) return RET0;
    return F_578b29;
}

// L_575b27: transports: colony pods aboard look for empty land; then ports of defended regions,
// enemy coasts to land on, other own ports.
Flow em_transport_routes(EM& m) {
    VEH& v = m.v();
    if (m.sb8) return RET0;
    if (m.stay) return F_57717f;
    if (v.order == ORDER_MOVE_TO) {
        if (v.status_icon == 0x64) return F_57717f;
    } else if ((m.s2c & 0x100) && m.s30 < 1) {
        // Only colony pods aboard: the nearest large empty landmass (by its coast in the polar
        // rows or the first one found).
        for (int32_t i = 0; i < 64; i++) {
            m.ys[i] = -1;
            m.xs[i] = -1;
        }
        for (m.ty = 1; m.ty < game().map.y - 1; m.ty++) {
            for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                int32_t r = tile_region(m.tx, m.ty);
                if (r >= 0x40) continue;
                if ((m.ys[r] < 10 || m.ys[r] > game().map.y - 10) && is_coast(m.tx, m.ty, 0)) {
                    m.xs[r] = m.tx;
                    m.ys[r] = m.ty;
                }
            }
        }
        m.best_idx = -1;
        m.best = 0;
        for (m.s68 = 1, m.s58 = 0; m.s58 < 0xFC; m.s68++, m.s58 += 4) {
            int32_t r = m.s68;
            m.s44 = 0x9AA730 + r * 0x1C;  // the original keeps a pointer here
            int32_t tiles = Continents[r].tile_count;
            if (tiles < 10 || m.f().region_total_bases[r] != 0 || !on_map(m.xs[r], m.ys[r])) continue;
            int32_t c = 0;
            for (m.s70 = 1; m.s70 < 8; m.s70++) {
                int32_t tb = game().factions[m.s70].region_total_bases[r];
                if (tb == 0) continue;
                c++;
                if (tb * 20 > tiles / 4) c += 2;
                if (dstat(m.fac, m.s70) & 7) c += 2;
            }
            if (c > 1) continue;
            int32_t score = tiles * 100 / (vdist(m.x, m.y, m.xs[r], m.ys[r]) + 1);
            if (score >= m.best) {
                m.best = score;
                m.best_idx = r;
            }
        }
        if (m.best_idx >= 0) set_course(m.veh, 0x63, m.xs[m.best_idx], m.ys[m.best_idx]);
    }
    // L_575dbb
    if (v.order != ORDER_MOVE_TO && m.s30 != 0) {
        // Combat units aboard: a port in a region to defend.
        m.best = 9999;
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            int32_t rr = tile_region(base.x, base.y);
            m.s68 = rr;
            if (m.f().region_base_plan[rr] != 2) continue;
            if (m.dist_own == 0 ? !port_to_port(m.base_own, m.base_id) : !base_on_sea(m.base_id, m.region)) continue;
            m.s4c = m.x;
            int32_t d = hdist(m.x, m.y, base.x, base.y);
            m.s38 = d;
            if (veh_who(base.x, base.y) < 0) {
                d /= 2;
                m.s38 = d;
            }
            if (at_goal(m.fac, 2, base.x, base.y)) {
                d /= 2;
                m.s38 = d;
            }
            int32_t t = m.f().base_id_attack_target;
            if (t >= 0 && (dstat(m.fac, base_faction(t)) & 0x10) && base_region(t) == rr) {
                d = 0;
                m.s38 = 0;
            }
            if (d < m.best) {
                m.best = d;
                go_to(m.veh, 0x45, base.x, base.y);
            }
        }
        if (v.order != ORDER_MOVE_TO && m.s30 > 1) {
            // An enemy base to land next to.
            m.best = 9999;
            for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
                m.s48 = 0x97D044 + m.base_id * 0x134;  // the original keeps a pointer here
                const BASE& base = game().bases[m.base_id];
                int32_t bf = base_faction(m.base_id);
                if (bf == m.fac) continue;
                if (!is_objective(m.base_id) && !(m.f().player_flags & PFLAG_STRAT_ATK_ENEMY_HQ) &&
                    m.f().base_id_attack_target != m.base_id && ((CurrentTurn + m.base_id) & 3))
                    continue;
                m.s30 = m.fac * 0x833;
                uint32_t d = dstat(m.fac, bf);
                if (d & 1) continue;
                if ((d & 2) && (!is_human(bf) || !(d & 0xC00))) continue;
                if ((d & 4) &&
                    (!is_human(bf) || (!(d & 0xC00) && CurrentTurn - game().factions[bf].diplo_spoke[m.fac] < 0x10)))
                    continue;
                int32_t rr = tile_region(base.x, base.y);
                if (rr == m.region && !m.at_sea) continue;
                if (m.dist_own == 0 ? !port_to_coast(m.base_own, rr) : !sea_coast(rr, m.region)) continue;
                // Not a region a passenger comes from, unless at war or the plan needs it.
                m.s54 = 1;
                for (m.other = veh_top(m.veh); m.other >= 0; m.other = game().vehs[m.other].next_veh_id_stack) {
                    int32_t home = game().vehs[m.other].home_base_id;
                    if (home >= 0 && base_region(home) == rr) m.s54 = 0;
                }
                if (m.s54 == 0) {
                    uint32_t d2 = dstat(m.fac, bf);
                    if (!(d2 & 0x10) && (d2 & 8) && !(GameState & 0x100) &&
                        !(m.s2c & (1 << (m.f().region_base_plan[rr] & 31))))
                        continue;
                }
                int32_t dd = hdist(m.x, m.y, base.x, base.y);
                int32_t score = dd << 5;
                m.s38 = score;
                if (bf != m.fac && !(base.visibility & (uint8_t)(1u << m.fac))) {
                    score = dd * 0x40;
                    m.s38 = score;
                }
                if (base.queue_items[0] == -0x66) {
                    score /= 4;
                    m.s38 = score;
                }
                if (has_fac(FAC_HEADQUARTERS, m.base_id, 0)) {
                    score /= 2;
                    if (m.f().player_flags & PFLAG_STRAT_ATK_ENEMY_HQ) score /= 2;
                    m.s38 = score;
                    if (CurrentTurn < game().factions[bf].corner_market_turn) {
                        score /= 8;
                        m.s38 = score;
                    }
                }
                if (is_objective(m.base_id)) {
                    score /= 2;
                    m.s38 = score;
                    if (m.f().player_flags & PFLAG_STRAT_ATK_OBJECTIVES) {
                        score /= 2;
                        m.s38 = score;
                    }
                }
                uint32_t d3 = dstat(m.fac, bf);
                if (d3 & 0x40) {
                    score /= 2;
                    m.s38 = score;
                }
                if (d3 & 0x10) {
                    score /= 2;
                    m.s38 = score;
                    if (veh_who(base.x, base.y) < 0) {
                        score /= 2;
                        m.s38 = score;
                    }
                    if (m.f().base_id_attack_target == m.base_id) {
                        score = 0;
                        m.s38 = 0;
                    }
                }
                if (has_fac(FAC_HEADQUARTERS, m.base_id, 0)) {
                    score /= 2;
                    m.s38 = score;
                }
                m.other = veh_at(base.x, base.y);
                bool weak;
                if (m.other < 0) {
                    weak = true;
                } else {
                    m.s4c = stack_check(m.other, 0xE, -1, -1, -1);
                    weak = m.s4c < garrison_check(m.base_id);
                }
                if (weak && is_human(bf) &&
                    (game().factions[bf].diff_level >= 4 || (m.f().player_flags & PFLAG_MAP_REVEALED) ||
                     (dstat(m.fac, bf) & 0x1000))) {
                    score /= 2;
                    m.s38 = score;
                }
                if (score < m.best) {
                    m.best = score;
                    set_course(m.veh, 0x49, base.x, base.y);
                }
            }
        }
    }
    // L_576448: colony pods / formers aboard: an own port in another region with that plan.
    if (v.order != ORDER_MOVE_TO) {
        m.best = 9999;
        for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
            const BASE& base = game().bases[m.base_id];
            if (base_faction(m.base_id) != m.fac) continue;
            int32_t rr = tile_region(base.x, base.y);
            if (rr == m.region) continue;
            uint8_t rp = m.f().region_base_plan[rr];
            if (rp == 7 || (m.s2c & 0x100) || !(m.s2c & (1 << (rp & 31)))) continue;
            if (m.dist_own == 0 ? !port_to_port(m.base_own, m.base_id) : !base_on_sea(m.base_id, m.region)) continue;
            m.s4c = std::abs(m.y - base.y);
            int32_t d = (m.s4c + x_dist(m.x, base.x)) >> 1;
            m.s38 = d;
            if (d < m.best) {
                m.best = d;
                go_to(m.veh, 0x72, base.x, base.y);
            }
        }
    }
    // L_57657b: a coast of an empty region two tiles out.
    if (v.order != ORDER_MOVE_TO) {
        for (int32_t i = 1; i < 0x121; i++) {
            m.tx = map_wrap_x(m.x + TableOffsetX[i] * 2);
            m.ty = m.y + TableOffsetY[i] * 2;
            if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
            int32_t rr = tile_region(m.tx, m.ty);
            if ((rr & 0x3F) == 0x3F || (rr & 0x3F) == 0) continue;
            if (m.f().region_total_bases[rr] != 0) continue;
            // BUG(original): the plan bit is taken from the transport's own region.
            COMPAT_ORIGINAL(plan_bit_own_region);
            if (!(m.s2c & (1 << (m.f().region_base_plan[m.region] & 31)))) continue;
            if (m.dist_own == 0 ? port_to_coast(m.base_own, rr) : sea_coast(rr, m.region)) {
                set_course(m.veh, 0x4A, m.tx, m.ty);
                break;
            }
        }
    }
    return F_57666a;
}

// L_57666a: other sea units: escort transports, raid enemy ports, explore the sea.
Flow em_sea(EM& m) {
    VEH& v = m.v();
    if (v.order == ORDER_MOVE_TO && v.status_icon == 0x64) return F_577071;
    if (m.plan == PLAN_NAVAL_SUPERIORITY) {
        if (unit_of(m.veh).group_id == 0x15) {
            // Escort: the nearest own transport at sea.
            m.best = 9999;
            for (m.other = 0; m.other < game().veh_count; m.other++) {
                const VEH& o = game().vehs[m.other];
                if (o.faction_id != (uint32_t)m.fac || unit_plan(m.other) != PLAN_NAVAL_TRANSPORT) continue;
                m.tx = o.x;
                m.ty = o.y;
                if (!sq_ocean(sq_at(m.tx, m.ty)) || base_who(m.tx, m.ty) >= 0) continue;
                int32_t d = hdist(m.x, m.y, m.tx, m.ty);
                m.s38 = d;
                if (d != 0) {
                    if (stack_check(m.other, 2, 6, -1, -1)) continue;
                    d /= stack_check(m.other, 1, -1, -1, -1);
                    m.s38 = d;
                }
                if (d < m.best) {
                    m.best = d;
                    go_to(m.veh, 0x43, m.tx, m.ty);
                }
            }
            if (v.order == ORDER_MOVE_TO) {
                // BUG(original): EDI holds [ebp-0x38] here; on the paths where no transport was
                // examined in this call that slot was never written (uninitialised read #3).
                COMPAT_ORIGINAL(residue.enemy_move_edi);
                return m.s38 ? F_577071 : F_578b34;
            }
        } else if (v.order == ORDER_MOVE_TO && v.status_icon != 0x36) {
            goto L_576a95;
        }
        // L_576847: raiding factions: enemy ports.
        if (game().mfactions[m.fac].rule_flags & 0x100) {
            m.best = 0;
            m.best_idx = -1;
            for (m.base_id = 0; m.base_id < game().base_count; m.base_id++) {
                const BASE& base = game().bases[m.base_id];
                int32_t bf = base_faction(m.base_id);
                if (bf == m.fac || !(dstat(m.fac, bf) & 0x10)) continue;
                // BUG(original): checks the own base's sea access instead of the target's.
                COMPAT_ORIGINAL(sea_access_own_base);
                if (m.dist_own == 0 ? !port_to_port(m.base_own, m.base_id) : !base_on_sea(m.base_own, m.region))
                    continue;
                m.s4c = base.x;
                m.val = base.pop_size * base.pop_size * 32 / (hdist(m.x, m.y, base.x, base.y) + 1);
                for (m.other = 0; m.other < game().veh_count; m.other++) {
                    const VEH& o = game().vehs[m.other];
                    if (m.other == m.veh || o.faction_id != (uint32_t)m.fac ||
                        unit_plan(m.other) != PLAN_NAVAL_SUPERIORITY)
                        continue;
                    m.s4c = std::abs(o.y - base.y);
                    if (((m.s4c + x_dist(o.x, base.x)) & ~1) <= 4 ||
                        (o.status_icon == 0x36 && o.waypoint_x[0] == base.x && o.waypoint_y[0] == base.y))
                        m.val /= 2;
                }
                if (m.val > m.best) {
                    m.best = m.val;
                    m.best_idx = m.base_id;
                    go_to(m.veh, 0x36, base.x, base.y);
                }
            }
            if (m.best_idx >= 0 &&
                (hdist2(m.x, m.y, game().bases[m.best_idx].x, game().bases[m.best_idx].y) & ~1) <= 2) {
                v.order = ORDER_NONE;
                return F_577071;
            }
        }
    L_576a95:
        if (v.order != ORDER_MOVE_TO && ((m.veh + CurrentTurn) & 3) == 0) {
            // Coasts between a well settled region and a weak enemy one: bombardment sites.
            m.best = -9999;
            for (m.ty = 0; m.ty < game().map.y; m.ty++) {
                for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                    MAP* sq = sq_at(m.tx, m.ty);
                    if (!sq_ocean(sq)) continue;
                    if (m.dist_own == 0 ? !base_on_sea(m.base_own, sq->region) : sq->region != m.region) continue;
                    if ((hdist2(m.x, m.y, m.tx, m.ty) & ~1) < 0x10) continue;
                    bool taken = false;
                    for (m.other = 0; m.other < game().veh_count; m.other++) {
                        const VEH& o = game().vehs[m.other];
                        if (o.faction_id == (uint32_t)m.fac && m.other != m.veh && o.waypoint_x[0] == m.tx &&
                            o.waypoint_y[0] == m.ty) {
                            taken = true;
                            break;
                        }
                    }
                    if (taken) continue;
                    m.val = 0;
                    for (m.s44 = 0; m.s44 < 0x120; m.s44 += 4) {
                        int32_t k = m.s44 / 4 + 9;
                        int32_t ax = map_wrap_x(m.tx + TableOffsetX[k]);
                        int32_t ay = TableOffsetY[k] + m.ty;
                        if (!on_map(ax, ay) || sq_ocean(sq_at(ax, ay))) continue;
                        int32_t bx = map_wrap_x(m.tx - TableOffsetX[k]);
                        m.s4c = m.ty - TableOffsetY[k];
                        if (!on_map(bx, m.s4c) || sq_ocean(sq_at(bx, m.s4c))) continue;
                        int32_t ra = tile_region(ax, ay);
                        int32_t rb = tile_region(bx, m.s4c);
                        if (m.f().region_total_bases[ra] <= 1 || m.f().region_total_bases[rb] > 1) continue;
                        for (m.s70 = 1; m.s70 < 8; m.s70++) {
                            if (m.s70 == m.fac || !(dstat(m.fac, m.s70) & 0xC10)) continue;
                            if (game().factions[m.s70].region_total_bases[ra] <= 1 &&
                                game().factions[m.s70].region_total_bases[rb] > 1)
                                m.val++;
                        }
                    }
                    if (m.val == 0) continue;
                    int32_t score = (m.val << 8) / vdist(m.x, m.y, m.tx, m.ty);
                    if (score > m.best) {
                        m.best = score;
                        go_to(m.veh, 0x44, m.tx, m.ty);
                    }
                }
            }
        }
    }
    // L_576daf: sea exploration.
    if ((v.order != ORDER_MOVE_TO ||
         ((m.f().player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES) && stack_check(m.veh, 1, -1, -1, -1) == 1)) &&
        ((m.veh + CurrentTurn) & 3) == 0) {
        m.best = -9999;
        for (m.ty = 0; m.ty < game().map.y; m.ty++) {
            for (m.tx = m.ty & 1; m.tx < game().map.x; m.tx += 2) {
                if (!is_known(m.tx, m.ty, m.fac)) continue;
                MAP* sq = sq_at(m.tx, m.ty);
                if (!sq_ocean(sq)) continue;
                if (m.dist_own == 0 ? !base_on_sea(m.base_own, sq->region) : sq->region != m.region) continue;
                m.val = 0;
                if (m.f().player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES) {
                    uint32_t items = sq_at(m.tx, m.ty)->items;
                    if (!(items & 0x100000)) {
                        m.val = 1;
                        if ((items & 0x20) && (sq_at(m.tx, m.ty)->climate & 0xE0) >= 0x40) m.val = 2;
                        if (items & 0x2000) m.val++;
                    }
                }
                for (int32_t i = 0; i < 8; i++) {
                    int32_t ax = map_wrap_x(m.tx + NearOffsetX[i]);
                    int32_t ay = NearOffsetY[i] + m.ty;
                    if (!on_map(ax, ay)) continue;
                    if ((m.f().player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES) &&
                        !(sq_at(ax, ay)->items & BIT_SUPPLY_REMOVE)) {
                        // counted without the knowledge test
                    } else if (is_known(ax, ay, m.fac)) {
                        continue;
                    }
                    m.val += 2;
                    if (ay >= 2 && ay < game().map.y - 3) m.val += 4;
                }
                if (m.val == 0) continue;
                int32_t d = m.base_own >= 0 ? vdist(game().bases[m.base_own].x, game().bases[m.base_own].y, m.tx, m.ty)
                                            : vdist(m.x, m.y, m.tx, m.ty);
                if (d == 0) continue;
                int32_t score = (m.val << 8) / d;
                if (score > m.best) {
                    m.best = score;
                    go_to(m.veh, 0x58, m.tx, m.ty);
                }
            }
        }
    }
    return F_577071;
}

// ---- tactical move (0x57707F-0x578B29) -----------------------------------------------------------

// Faction of vehicle `veh_id` as the original reads it (also for -1: the bytes before Vehs).
inline uint8_t veh_faction_raw(int32_t veh_id) {
    if ((uint32_t)veh_id < 2049) return game().vehs[veh_id].faction_id;
    return rules_orig_byte(0x952828 + (uint32_t)veh_id * 0x34 + offsetof(VEH, faction_id));
}

// L_57707f / L_57708a: pioneering land units in a region without own bases ask for a transport
// at the coast.
Flow em_coast_goal(EM& m, bool land_only) {
    if (land_only && m.triad != TRIAD_LAND) return F_57717f;
    if (m.plan == PLAN_COLONY) return F_57717f;
    const Faction& f = m.f();
    if (f.region_total_bases[m.region] != 0 || (f.region_flags[m.region] & 6) || f.region_base_plan[m.region] <= 2)
        return F_57717f;
    if (!((m.y > 0 && m.y < game().map.y - 1) || m.plan == PLAN_ARTIFACT)) return F_57717f;
    for (int32_t dir = 0; dir < 8; dir++) {
        m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
        m.ty = NearOffsetY[dir] + m.y;
        if (on_map(m.tx, m.ty) && sq_ocean(sq_at(m.tx, m.ty))) {
            add_goal(m.fac, 7, 1, m.tx, m.ty, -1);
            m.coast_goal = 1;
            break;
        }
    }
    return F_57717f;
}

// L_57717f: units on a waypoint order follow it (Path_move) unless an enemy blocks the way.
Flow em_follow(EM& m) {
    VEH& v = m.v();
    if (v.order != ORDER_MOVE_TO) return F_577361;
    if (veh_offense(m.veh) != 0 && (v.moves_spent == 0 || speed(m.veh, 0) <= Rules.move_rate_roads ||
                                    m.plan != PLAN_OFFENSE || m.triad != TRIAD_LAND))
        return F_577361;
    if (v.state & 8) return F_577361;
    m.s4c = std::abs(m.y - v.waypoint_y[0]);
    if (((m.s4c + x_dist(m.x, v.waypoint_x[0])) & ~1) <= 2) return F_577361;
    int32_t dir = Path_move(&Paths, m.veh, m.fac);
    m.best_dir = dir;
    if (dir < 0) {
        if (m.zocv == 1) {
            v.order = ORDER_NONE;
            v.state &= 0xFCFFBDFFu;
        }
        return F_577361;
    }
    m.tx = map_wrap_x(m.x + NearOffsetX[dir]);
    m.ty = NearOffsetY[dir] + m.y;
    m.s70 = zoc_sea(m.tx, m.ty, m.fac);
    if (m.s70 != 1) {
        if (m.s70 < 1) return F_577361;
        if (!(dstat(m.fac, m.s70) & 0x10)) return F_577361;  // (this test indexes by zoc_sea's value itself)
        if (m.plan == PLAN_PROBE && veh_moves(m.veh) - hex_cost(m.unit, m.fac, m.x, m.y, m.tx, m.ty, 0) >= 1)
            return F_577361;
    }
    if (m.zocv == 0 || (m.plan != PLAN_PROBE && veh_offense(m.veh) == 0)) v.order = ORDER_NONE;
    if (m.zocv != 0) return F_577361;
    return F_578b34;
}

// L_577361: the move of the turn: every direction (and the tile itself) gets a score; the best
// one is taken. Attacks, captures and probe actions are scored with the real battle odds.
Flow em_move(EM& m) {
    VEH& v = m.v();
    if (v.order == ORDER_MOVE_TO && m.s84 == 0 && m.stay == 0) {
        if (!zoc_veh(m.x, m.y, m.fac)) return F_578f46;
        m.s4c = std::abs(m.y - v.waypoint_y[0]);
        if (((m.s4c + x_dist(m.x, v.waypoint_x[0])) & ~1) == 2) {
            m.s70 = anything_at(v.waypoint_x[0], v.waypoint_y[0]);
            if (m.s70 >= 0 && m.s70 != m.fac && !(dstat(m.fac, m.s70) & 1) && v.status_icon != 0x70)
                v.order = ORDER_NONE;
        }
        if (v.order == ORDER_MOVE_TO) m.keep_wp = 1;
    }
    m.best = -9999;
    m.sa4 = 0;
    if (zoc_sea(m.x, m.y, m.fac) || m.keep_wp || m.st4 || m.st6) {
        m.st4 = 0;
    } else {
        m.st4 = 1;
        if (m.plan == PLAN_ARTIFACT) m.st4 = 0;
    }
    m.s44 = rnd(9);
    for (m.s6c = 0; m.s6c < 9; m.s6c++) {
        m.s2c = (m.s6c + m.s44) % 9;
        m.tx = map_wrap_x(m.x + TableOffsetX[m.s2c]);
        m.ty = TableOffsetY[m.s2c] + m.y;
        if (!on_map(m.tx, m.ty)) continue;
        if (m.s2c == 0 && m.zocv == 0 && m.plan != PLAN_DEFENSE) continue;
        {
            m.s84 = sq_ocean_i(sq_at(m.tx, m.ty));
            m.other = stack_fix(veh_at(m.tx, m.ty));
            m.base_id = base_at(m.tx, m.ty);
            m.s68 = tile_region(m.tx, m.ty);
            if (m.triad == TRIAD_SEA && m.s84 == 0) {
                // A ship next to land: only to attack from the sea.
                if (m.other < 0 || m.s70 == m.fac || !m.at_sea) continue;
            } else if (!(m.at_sea || m.s84 || !m.zocm || m.triad != TRIAD_LAND || m.other >= 0 ||
                         (unit_of(m.veh).ability_flags & ABL_CLOAKED) || unit_plan(m.veh) == PLAN_PROBE)) {
                if (zoc_move(m.tx, m.ty, m.fac)) continue;
            }
            // L_57762a: unarmed units avoid enemy zones of control.
            if (veh_offense(m.veh) == 0 && !(m.other >= 0 && m.s2c != 0) && m.base_id < 0 && !(v.state & 8)) {
                m.s70 = zoc_sea(m.tx, m.ty, m.fac);
                if (m.s70 > 0) {
                    if (m.s70 == 1) continue;
                    uint32_t d = dstat(m.fac, m.s70 - 1);
                    if ((d & 1) && (d & 0x2000000)) continue;
                    bool check = true;
                    if (m.plan == PLAN_PROBE) {
                        check = false;
                        if (v.moves_spent != 0 &&
                            veh_moves(m.veh) - hex_cost(m.unit, m.fac, m.x, m.y, m.tx, m.ty, 0) <= 0)
                            check = true;
                    }
                    if (check && (d & 0x10)) continue;
                }
            }
            // L_577723
            m.val = rnd(6);
            if (m.s84 && m.triad == TRIAD_LAND && !(m.base_id >= 0 && has_abil(v.unit_id, ABL_AMPHIBIOUS))) {
                // Land unit to an ocean tile: only onto an own transport with room.
                if (m.other < 0 || game().vehs[m.other].faction_id != (uint32_t)m.fac) continue;
                if (stack_check(m.other, 8, -1, -1, -1) < 1) continue;
                if ((int32_t)game().vehs[m.other].damage_taken > Units[game().vehs[m.other].unit_id].reactor_id * 2)
                    continue;
                const Faction& f = m.f();
                if (m.plan == PLAN_ARTIFACT) {
                    m.val += 0x63;
                } else if (m.plan == PLAN_COLONY) {
                    if (f.region_base_plan[m.region] != 7) continue;
                    m.val += 0x10;
                } else {
                    if (m.plan == PLAN_TERRAFORM) continue;
                    uint8_t rp = f.region_base_plan[m.region];
                    if (!(rp == 7 && m.plan < 2)) {
                        if (f.region_total_bases[m.region] != 0 || (f.region_flags[m.region] & 6) || rp <= 2) continue;
                    }
                    m.val += 0x10;
                }
            }
            // L_577865
            if (m.at_sea && !m.s84 && m.triad == TRIAD_LAND && m.dist_own == 0 &&
                !has_abil(v.unit_id, ABL_AMPHIBIOUS) && !stack_check(m.veh, 2, PLAN_NAVAL_TRANSPORT, -1, -1))
                continue;
            if (m.at_sea == m.s84 && (v.state & 0x10000) &&
                (m.zocv == 0 || (m.zocv > 1 && (dstat(m.fac, m.zocv - 1) & 7)))) {
                // Keep the heading of the last move.
                if (m.s2c == 0) {
                    m.val -= 0xC;
                } else {
                    int32_t a = v.rotate_angle - m.s2c + 1;
                    if (a < 0) a = m.s2c - v.rotate_angle - 1;
                    if (a > 4) a = 8 - a;
                    m.val -= a * 4;
                    if (a == 4) m.val -= 0x100;
                }
            }
            if (m.plan == PLAN_ARTIFACT) {
                int32_t pen = 0;
                if (zoc_veh(m.tx, m.ty, m.fac)) pen = -0x20;
                if (!is_coast(m.tx, m.ty, 0)) pen -= 0x20;
                if (m.s2c == 0 && pen == 0) {
                    m.val += m.coast_goal ? 0x20 : 8;
                    base_find_faction(m.tx, m.ty, m.fac);
                    if (BaseFindDist <= m.dist_own) m.val += 8;
                }
                m.val += pen;
            } else if (m.coast_goal && m.s2c == 0) {
                m.val += 8;
            }
            if (m.f().player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES) {
                MAP* t = sq_at(m.tx, m.ty);
                uint32_t items = t->items;
                if (!(items & 0x100000)) {
                    m.val += 8;
                    if ((items & 0x20) && (t->climate & 0xE0) >= 0x40) m.val += 8;
                    if (items & 0x2000) m.val += 8;
                }
            }
            m.s70 = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
            if (m.s70 >= 0 && m.s70 != m.fac && unit_plan(m.veh) != PLAN_PROBE) {
                uint32_t d = dstat(m.fac, m.s70);
                if (!(d & 0xC10)) {
                    // Not at war: respect treaties.
                    if (m.f().diplo_agenda[m.s70] & 2) continue;
                    if (d & 0x100000) continue;
                    if (d & 4) m.val -= 6;
                    if (d & 2) {
                        m.val -= 0xC;
                        if (!(d & 1)) {
                            if (m.at_sea) continue;
                            if (!(d & 0x2020)) continue;
                            if (SunspotDuration > 0) continue;
                        }
                    }
                }
            }
            if (m.st6 && m.plan == PLAN_DEFENSE && m.other >= 0 && m.s70 == m.fac && zoc_veh(m.tx, m.ty, m.fac))
                m.val -= stack_check(m.veh, 1, -1, -1, -1) * 2;
            if (m.at_sea && m.triad == TRIAD_LAND && m.dist_own != 0) {
                uint8_t rp = m.f().region_base_plan[m.s68];
                if (rp == m.plan) m.val += 0x20;
                if (rp != 7) m.val += 0x20;
                base_find(m.tx, m.ty);
                m.val += 0x20 - BaseFindDist;
                m.val -= stack_check(m.other, 1, -1, -1, -1);
            }
            if (goody_at(m.tx, m.ty)) {
                if (m.plan == PLAN_NAVAL_TRANSPORT && stack_check(m.veh, 1, -1, -1, -1) > 1) continue;
                m.val += 0x63;
            }
            bool score_it = true;
            if (!m.at_sea && !m.s84 && m.region != m.s68 && m.f().region_total_bases[m.region] != 0) {
                // Leaving a settled region by land.
                if (veh_offense(m.veh) == 0) {
                    if (m.plan != PLAN_COLONY) continue;
                    if (m.f().region_unk_6[m.region] != 0) continue;
                } else if (m.plan != PLAN_RECON) {
                    if (m.region_plan <= 2) continue;
                    if (m.f().region_flags[m.region] & 0x20) continue;
                }
            }
            // L_577ca2
            if (m.st4 && m.st4000) score_it = false;
            if (score_it) {
                m.sb8 = m.st6 ? 2 : m.plan;
                bool friendly;
                if (m.other < 0) {
                    friendly = false;
                } else {
                    int32_t of = game().vehs[m.other].faction_id;
                    friendly = of == m.fac || (dstat(m.fac, of) & 1);
                }
                if (m.s2c == 0 && stack_check(m.other, 1, m.other, -1, -1) == 1) friendly = false;
                if (m.at_sea && m.triad == TRIAD_LAND && m.sb8 != 8 && veh_faction_raw(m.other) == (uint32_t)m.fac) {
                    if (stack_check(m.other, 2, 8, -1, -1)) m.val += 8;
                    if (m.sb8 <= 1 || m.sb8 == 0xB) {
                        uint16_t fl = m.f().region_flags[m.s68];
                        if (fl & 0x20) m.val += 0x10;
                        if (fl & 0x40) m.val += 0x10;
                    }
                }
                switch (m.sb8) {
                case 0:
                    if (friendly && !m.st4) m.val += stack_check(m.other, 2, 2, -1, -1) * 4;
                    {
                        int32_t k = stack_check(m.other, 2, 0, -1, -1);
                        m.val += -k * 2;
                        m.val -= hex_cost(m.unit, m.fac, m.x, m.y, m.tx, m.ty, 0) / Rules.move_rate_roads;
                    }
                    break;
                case 1:
                    if (zoc_veh(m.tx, m.ty, m.fac)) m.val += defense_value(m.fac, m.tx, m.ty, m.veh, -1) * 2;
                    break;
                case 2:
                    if (zoc_veh(m.tx, m.ty, m.fac)) m.val += defense_value(m.fac, m.tx, m.ty, m.veh, -1) * 2;
                    if (friendly && !m.st4) {
                        int32_t a = stack_check(m.other, 2, 0, -1, -1);
                        int32_t b = stack_check(m.other, 2, 2, -1, -1);
                        m.val += a * 4 - b * 2;
                    }
                    break;
                case 3: m.val -= hex_cost(m.unit, m.fac, m.x, m.y, m.tx, m.ty, 0) / Rules.move_rate_roads; break;
                case 6: {
                    m.val += stack_check(m.other, 2, 7, -1, -1) * 4;
                    int32_t k = stack_check(m.other, 2, 6, -1, -1);
                    m.val += -k * 2;
                    break;
                }
                case 7:
                    if (!m.st4) m.val += stack_check(m.other, 2, 6, -1, -1) * 4;
                    m.val -= stack_check(m.other, 2, 7, -1, -1) * 8;
                    break;
                case 8:
                    if (m.at_sea && zoc_sea(m.tx, m.ty, m.fac)) continue;
                    if (m.s70 >= 0 && m.s70 != m.fac && (dstat(m.fac, m.s70) & 3) && v.order != ORDER_MOVE_TO)
                        m.val -= 0x10;
                    if (m.at_sea && m.f().region_base_plan[m.s68] == 8) {
                        m.val += Continents[m.s68].open_terrain + 0x10;
                        uint16_t fl = m.f().region_flags[m.s68];
                        if (!(fl & 0x20)) m.val += 8;
                        if (!(fl & 0x10)) m.val += 0x10;
                    }
                    break;
                case 0xB:
                    if (friendly) m.val += stack_check(m.other, 0xE, -1, -1, -1) * 2;
                    break;
                default: break;
                }
                // Higher ground.
                if ((sq_at(m.tx, m.ty)->climate & 0xE0) > (sq_at(m.x, m.y)->climate & 0xE0) &&
                    (Rules.combat_bonus_atk_higher_elevation || Rules.combat_penalty_atk_lower_elevation))
                    m.val += 2;
            }
            // L_5780c8: units, bases and monoliths on the tile.
            m.s48 = 0;
            if (m.other >= 0) {
                int32_t of = game().vehs[m.other].faction_id;
                m.s70 = of;
                if (m.triad == TRIAD_AIR && Chassis[unit_of(m.veh).chassis_id].range != 0 && v.movement_turns != 0 &&
                    v.order == ORDER_MOVE_TO && !(v.waypoint_x[0] == m.tx && v.waypoint_y[0] == m.ty))
                    continue;
                if (stack_check(m.other, 1, -1, -1, -1) == 1 && !(dstat(m.fac, of) & 0x11) &&
                    unit_plan(m.other) == PLAN_ARTIFACT && veh_offense(m.veh) != 0 && m.base_id < 0) {
                    // A lone artifact: capture it.
                    if (!(dstat(m.fac, of) & 0x20) && whose_territory(m.fac, m.tx, m.ty, nullptr, 0) != m.fac &&
                        !rnd(2))
                        continue;
                    m.s48 = 1;
                    m.val += 8;
                    goto L_57883f;
                }
                if (m.plan == PLAN_PROBE) {
                    if (of == m.fac) goto L_57883f;
                    if (stack_check(m.other, 2, PLAN_PROBE, -1, -1)) {
                        uint32_t d = dstat(m.fac, of);
                        if (!((d & 1) && (d & 0x2000000))) {
                            // Enemy probe teams: fight them.
                            m.s48 = 1;
                            int32_t odds = battle_fight(m.veh, m.s2c - 1, 0, 0, &m.other);
                            if (whose_territory(m.fac, m.tx, m.ty, nullptr, 0) == m.fac) odds *= 2;
                            else if (m.base_id < 0 && (dstat(m.fac, of) & 7)) odds /= 2;
                            if (odds >= 8) {
                                m.val += odds;
                                goto L_57883f;
                            }
                            if (m.base_id < 0) continue;
                            goto L_57883f;
                        }
                    }
                }
                // L_5782ee
                if (of == m.fac) goto L_57883f;
                m.s4c = m.fac * 0x833 + of;
                if (dstat(m.fac, of) & 1) goto L_57883f;
                if (m.at_sea && !m.s84 && m.triad == TRIAD_SEA && !can_arty(m.unit, 1)) continue;
                if (stack_check(m.other, 1, -1, -1, -1) == 1 && unit_plan(m.other) == PLAN_PROBE &&
                    (dstat(m.fac, game().vehs[m.other].faction_id) & 6) &&
                    whose_territory(m.fac, game().vehs[m.other].x, game().vehs[m.other].y, nullptr, 0) == m.fac) {
                    // A lone probe team in own territory.
                    m.s48 = 1;
                    m.val += 0x63;
                    goto L_57883f;
                }
                if (m.plan == PLAN_NAVAL_TRANSPORT) {
                    if (stack_check(m.veh, 1, -1, -1, -1) > 1) continue;
                } else if (m.plan >= PLAN_COLONY && m.plan != PLAN_PROBE) {
                    continue;
                }
                // L_578404
                if (m.at_sea && m.triad == TRIAD_LAND && !has_abil(v.unit_id, ABL_AMPHIBIOUS)) continue;
                if (stack_check(m.other, 3, 2, -1, -1) && !has_abil(v.unit_id, ABL_AIR_SUPERIORITY) && m.base_id < 0 &&
                    !(sq_at(m.tx, m.ty)->items & BIT_AIRBASE) && !stack_check(m.other, 6, 0x80, -1, -1))
                    continue;
                if (m.plan == PLAN_PROBE) {
                    if (stack_check(m.other, 1, -1, -1, -1) > 1) continue;
                    if (base_who(game().vehs[m.other].x, game().vehs[m.other].y) >= 0) continue;
                    int32_t count = game().veh_count;
                    if (!probe(m.veh, -1, m.other, 1)) continue;
                    if (count != game().veh_count) return RET1;
                    m.best = 999;
                    m.sa4 = 1;
                    m.best_dir = m.s2c - 1;
                    continue;
                }
                if (veh_offense(m.veh) == 0) continue;
                if (!(dstat(m.fac, m.s70) & 0x810)) continue;  // (index kept in m.s4c)
                // Attack: the odds of the battle.
                m.s48 = 1;
                int32_t odds = battle_fight(m.veh, m.s2c - 1, 0, 0, &m.other);
                bool rate;
                if (m.defend == 0 && (veh_moves(m.veh) >= Rules.move_rate_roads || m.unit == BSC_MIND_WORMS) &&
                    (m.dist_own != 0 || m.plan < 1)) {
                    rate = true;
                } else {
                    int32_t back = battle_fight(m.other, (m.s2c + 3) & 7, 0, 0, nullptr);
                    if (back != 0) {
                        int32_t k = m.defend ? m.defend + 1 : 0;
                        rate = (4 - k) * back >= odds * 4 && back >= 8 && odds >= m.defend + 6;
                        if (!rate) continue;
                    } else {
                        rate = stack_check(m.veh, 1, -1, -1, -1) != 1 || unit_plan(m.other) == PLAN_PROBE;
                        if (!rate) continue;
                    }
                }
                {
                    int32_t r = compute_odds(odds, m.fac, m.veh, m.other, m.base_id);
                    if (m.s70 == 0 && (m.base_own < 0 || game().bases[m.base_own].eco_damage == 0) &&
                        game().vehs[m.other].order_auto_type != (uint32_t)m.fac)
                        r = m.f().SE_planet > 2 ? r << 3 : r * 2;
                    if (r < 8) continue;
                    m.val += r;
                    goto L_57883f;
                }
            }
            if (m.base_id >= 0 && base_faction(m.base_id) != m.fac) {
                // An enemy base (empty).
                int32_t bf = base_faction(m.base_id);
                COMPAT_ORIGINAL(dstat_territory_owner);
                uint32_t d = dstat_raw(m.fac, m.s70);  // BUG(original): the territory owner, not bf
                if (!(d & 1)) {
                    if (m.plan == PLAN_PROBE && !(unit_of(m.veh).ability_flags & 0x10000000) &&
                        has_project(FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First, bf))
                        continue;
                    d = dstat_raw(m.fac, m.s70);
                    if ((d & 7) && !(d & 0x800)) {
                        if (!(d & 0x20)) continue;
                        if (d & 2) {
                            int32_t r = rnd(6) - m.f().AI_fight;
                            if (r > reputation(m.s70, m.fac)) continue;
                        }
                    }
                    if (veh_offense(m.veh) == 0) continue;
                    m.s48 = 1;
                    m.val += 9999;
                }
            }
            // L_578801
            if ((sq_at(m.tx, m.ty)->items & BIT_MONOLITH) && want_monolith(m.veh)) m.val += 0x63;
        L_57883f:
            if ((m.st4 || m.st4000) && m.s2c != 0 && (m.triad == TRIAD_AIR || m.s84 == (m.triad == TRIAD_SEA))) {
                // Exploring: count unknown tiles along the direction.
                if (m.st4000) {
                    base_find_faction(m.tx, m.ty, m.fac);
                    m.val += BaseFindDist * 2;
                }
                for (m.s30 = 0; m.s30 < 8; m.s30++) {
                    m.s4c = map_wrap_x(m.tx + TableOffsetX[m.s2c] * m.s30);
                    m.s60 = TableOffsetY[m.s2c] * m.s30 + m.ty;
                    if (!on_map(m.s4c, m.s60)) continue;
                    if (m.triad != TRIAD_AIR && sq_ocean_i(sq_at(m.s4c, m.s60)) != (m.triad == TRIAD_SEA)) continue;
                    for (m.s58 = 0; m.s58 < 0x20; m.s58 += 4) {
                        int32_t ax = map_wrap_x(m.s4c + NearOffsetX[m.s58 / 4]);
                        int32_t ay = NearOffsetY[m.s58 / 4] + m.s60;
                        if (!on_map(ax, ay)) continue;
                        if (m.f().player_flags & PFLAG_STRAT_SEARCH_OBJECTIVES) {
                            MAP* t = sq_at(ax, ay);
                            uint32_t items = t->items;
                            if (!(items & 0x100000)) {
                                m.val += 8;
                                if ((items & 0x20) && (t->climate & 0xE0) >= 0x40) m.val += 8;
                                if (items & 0x2000) m.val += 8;
                            }
                        }
                        if (!is_known(ax, ay, m.fac)) {
                            m.val += m.st4000 * 2 + 2;
                            if (m.triad == TRIAD_SEA) {
                                MAP* t = sq_at(ax, ay);
                                if (!sq_ocean(t) && m.f().region_total_bases[t->region] == 0) m.val += 8;
                            }
                        }
                        if (goody_at(ax, ay)) m.val += m.st4000 * 2 + 2;
                        if (m.plan == PLAN_COLONY) {
                            m.s70 = whose_territory(m.fac, ax, ay, nullptr, 0);
                            if (m.s70 >= 0 && m.s70 != m.fac && (dstat(m.fac, m.s70) & 3)) m.val -= 6;
                        }
                    }
                }
            }
            if (m.val > m.best) {
                m.best = m.val;
                m.best_dir = m.s2c - 1;
                m.sa4 = m.s48;
            }
        }
    }
    if (m.sa4 == 0) {
        if (m.stay) {
            m.best_dir = -1;
            v.order = ORDER_NONE;
        }
        if (m.keep_wp && v.order == ORDER_MOVE_TO) return F_578f46;
        if (can_arty(m.unit, 1) && v.order == ORDER_NONE &&
            (m.triad == TRIAD_SEA || !sq_ocean(sq_at(m.x, m.y)) || m.dist_any == 0)) {
            // Artillery against enemy improvements.
            for (m.s4c = 0; m.s4c < 0x64; m.s4c += 4) {
                int32_t i = m.s4c / 4;
                m.tx = map_wrap_x(m.x + TableOffsetX[i]);
                m.ty = TableOffsetY[i] + m.y;
                if (!on_map(m.tx, m.ty) || sq_ocean(sq_at(m.tx, m.ty))) continue;
                if (anything_at(m.tx, m.ty) >= 0) continue;
                int32_t t = whose_territory(m.fac, m.tx, m.ty, nullptr, 0);
                if (t < 0 || t == m.fac || !(dstat(m.fac, t) & 0x10)) continue;
                int32_t rr = tile_region(m.tx, m.ty);
                if (m.f().region_total_bases[rr] != 0 && m.f().region_base_plan[rr] != 2 && m.triad == TRIAD_LAND)
                    continue;
                // BUG(original): the improvements are looked up on the unit's own tile.
                COMPAT_ORIGINAL(destroy_own_tile_items);
                uint32_t items = sq_at(m.x, m.y)->items;
                if (!(items & 0x1000) || !(items & 0x81208854)) continue;
                if (m.mp_own) {
                    log_say_values("Auto sending DESTROY2 message", m.veh, m.tx, m.ty);
                    message_data(0x2413, m.veh, m.tx, m.ty, 0, 0);
                    return F_578d67;
                }
                log_say_values("Enemy doing ARTY DESTROY 2", m.veh, m.tx, m.ty);
                action_destroy(m.veh, 0, m.tx, m.ty);
                return RET1;
            }
        }
    } else if (planet_buster(m.veh)) {
        m.best_dir = -1;
        v.order = ORDER_NONE;
    }
    v.status_icon = 0x78;
    return F_578b29;
}

}  // namespace

int32_t GAMEAPI enemy_move(int32_t veh_id) {
    EM m;
    // The original's frame: slots it reads before writing hold the canonical value of the oracle's
    // stack poisoning (0). See docs/RECONSTRUCTION.md, "Known uninitialised reads" #2 and #3.
    std::memset(&m, 0, sizeof(m));
    Flow f = em_setup(m, veh_id);
    for (;;) {
        switch (f) {
        case RET0: return 0;
        case RET1: return 1;
        case RETV: return m.retval;
        case F_56b970: f = em_orders(m); break;
        case F_56c2d1:
        case F_56c9c2: f = em_drop_automation(m); break;
        case F_56c9e1: f = em_plan(m); break;
        case F_56cdc1: f = em_gift(m); break;
        case F_56e011: f = em_probe(m); break;
        case F_56e507: f = em_land(m); break;
        case F_56e62d: f = F_56ed87; break;  // (only reached inside em_land)
        case F_56e693:
            m.stay = 1;
            m.defend = 1;
            f = F_57708a;
            break;
        case F_56ed87: f = em_air(m); break;
        case F_56ef2f: f = em_naval(m); break;
        case F_56f8ad: f = em_colony(m); break;
        case F_570002: f = em_colony_next(m); break;
        case F_57000c: f = em_sea_sites(m); break;
        case F_5702f7: f = em_explore_sites(m); break;
        case F_570c89: f = em_destroy(m); break;
        case F_57137d:
            m.best_dir = -1;
            f = F_57138d;
            break;
        case F_57138d: f = em_defend(m); break;
        case F_571977:
        case F_571982: f = em_cover(m); break;
        case F_571d57: f = em_cover(m); break;  // (only reached inside em_cover)
        case F_572922: f = em_former(m); break;
        case F_5741cf: f = em_patrol(m); break;
        case F_57460a: f = em_explore(m); break;
        case F_574a7e: f = m.triad != TRIAD_SEA ? F_577071 : F_574a88; break;
        case F_574a88: f = em_transport(m); break;
        case F_575b27: f = em_transport_routes(m); break;
        case F_57666a: f = em_sea(m); break;
        case F_577071: f = m.sb8 ? RET0 : F_57707f; break;
        case F_57707f: f = em_coast_goal(m, true); break;
        case F_57708a: f = em_coast_goal(m, false); break;
        case F_57717f: f = em_follow(m); break;
        case F_577361: f = em_move(m); break;
        case F_578b29: f = em_step(m); break;
        case F_578b34: f = em_clear_order(m); break;
        case F_578f46: return em_finish(m);
        case F_578d67: NetDaemon_await_exec(&NetState, 1); return 1;
        }
    }
}
