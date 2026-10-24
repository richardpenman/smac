#include "engine/veh.h"

#include "engine/hybrid_shadow.h"
#ifdef SMACX_HYBRID
extern "C" int32_t GAMEAPI speed_impl_c(int32_t veh_id, int32_t skip_morale);
extern "C" int32_t GAMEAPI speed_proto_impl_c(int32_t unit_id);
extern "C" int32_t GAMEAPI morale_veh_impl_c(int32_t veh_id, int32_t check_drone_riot, int32_t faction_id_vs_native);
#endif

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/action.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"

#ifdef SMACX_HYBRID
// Hybrid builds: stack image of the original has_abil.
//
// Some original AI code (below enemy_veh) reads an uninitialised local whose stack slot was last
// written by a has_abil call, so the stack contents has_abil leaves behind influence the game.
// The hybrid build therefore reproduces them: an asm thunk builds the original frame (saved
// ebp/ebx/esi/edi, unit_id * 0x34 at [ebp-4], loop counter in the unit_id argument slot), the
// stack below it is saved before the C++ body runs and restored afterwards, and the frame of the
// original's last has_tech call is rewritten. Native builds have no such dependency to reproduce
// until the AI is reconstructed.
namespace {
constexpr int kSaveBytes = 1024;
}
extern "C" {
uint32_t has_abil_saved_stack[kSaveBytes / 4];
int32_t has_abil_result;
int32_t has_abil_loop;          // final loop counter, -1 if the bonus loop was not entered
int32_t has_abil_tech_called;   // the bonus loop called has_tech
uint32_t has_abil_tech_args[4]; // edi, esi, preq_tech, faction (pushed order reversed)
int32_t __cdecl has_abil_body(int32_t unit_id, uint32_t ability_flag);
}

#ifdef SMACX_UNINIT_DEBUG
// Debug variant (uninitialised-read investigation only): HA_MASK (environment, hex) selects which
// parts of the original stack image the thunk reproduces. bit0: [ebp-4]; bit1: restore the stack
// below the frame; bit2: loop counter in the argument slot; bit3: has_tech frame. Default 0xF.
#include <cstdlib>
// bit4: after returning, overwrite the saved-EDI slot ([ebp-0x10]) with HA_EDI (hex).
extern "C" uint32_t has_abil_dbg_mask = 0xF;
extern "C" uint32_t has_abil_dbg_edi = 0;
__attribute__((constructor)) static void has_abil_dbg_init() {
    if (const char* m = getenv("HA_MASK")) has_abil_dbg_mask = (uint32_t)strtoul(m, nullptr, 16);
    if (const char* m = getenv("HA_EDI")) has_abil_dbg_edi = (uint32_t)strtoul(m, nullptr, 16);
}
#define HA_DBG(bit, label) "testl $" #bit ", _has_abil_dbg_mask\n jz " #label "\n"
#define HA_DBG_ONLY(code) code
#else
#define HA_DBG(bit, label) ""
#define HA_DBG_ONLY(code) ""
#endif
__attribute__((naked)) int32_t GAMEAPI has_abil(int32_t unit_id, uint32_t ability_flag) {
    __asm__(
        "push %ebp\n mov %esp, %ebp\n push %ecx\n push %ebx\n push %esi\n push %edi\n"
        HA_DBG(1, 5f) "mov 8(%ebp), %eax\n imul $0x34, %eax, %eax\n mov %eax, -4(%ebp)\n5:\n"
        // save the stack below the frame
        "lea -1024(%esp), %esi\n mov $_has_abil_saved_stack, %edi\n mov $256, %ecx\n rep movsl\n"
        "push 12(%ebp)\n push 8(%ebp)\n call _has_abil_body\n add $8, %esp\n"
        "mov %eax, _has_abil_result\n"
        // restore it
        HA_DBG(2, 6f) "mov $_has_abil_saved_stack, %esi\n lea -1024(%esp), %edi\n mov $256, %ecx\n rep movsl\n6:\n"
        // loop counter in the unit_id argument slot
        HA_DBG(4, 1f) "mov _has_abil_loop, %eax\n test %eax, %eax\n js 1f\n mov %eax, 8(%ebp)\n1:\n"
        // frame of the last has_tech call: faction, preq_tech, return address, ebp, esi, edi
        HA_DBG(8, 2f) "cmpl $0, _has_abil_tech_called\n je 2f\n"
        "mov _has_abil_tech_args+12, %eax\n mov %eax, -20(%ebp)\n"
        "mov _has_abil_tech_args+8, %eax\n mov %eax, -24(%ebp)\n"
        "movl $0x5BF291, -28(%ebp)\n"
        "mov %ebp, -32(%ebp)\n"
        "mov _has_abil_tech_args+4, %eax\n mov %eax, -36(%ebp)\n"
        "mov _has_abil_tech_args, %eax\n mov %eax, -40(%ebp)\n"
        "2:\n"
        "mov _has_abil_result, %eax\n"
        "pop %edi\n"
        HA_DBG_ONLY(HA_DBG(16, 7f) "mov _has_abil_dbg_edi, %ecx\n mov %ecx, -16(%ebp)\n7:\n")
        "pop %esi\n pop %ebx\n mov %ebp, %esp\n pop %ebp\n ret\n");
}
#define HA_RECORD_LOOP(i) (has_abil_loop = (i))
#define HA_RECORD_TECH(f, i, preq, faction)                                                         \
    (has_abil_tech_called = 1, has_abil_tech_args[0] = (uint32_t)(f) * 0x59C,                      \
     has_abil_tech_args[1] = (uint32_t)(uintptr_t)&MFactions[0].faction_bonus_val1[0] + (f) * 0x59C + (i) * 4, \
     has_abil_tech_args[2] = (uint32_t)(preq), has_abil_tech_args[3] = (uint32_t)(faction))
extern "C" int32_t __cdecl has_abil_body(int32_t unit_id, uint32_t ability_flag) {
    has_abil_loop = -1;
    has_abil_tech_called = 0;
#else
#define HA_RECORD_LOOP(i) ((void)0)
#define HA_RECORD_TECH(f, i, preq, faction) ((void)0)
// Whether unit_id has ability_flag (see veh.h).
int32_t GAMEAPI has_abil(int32_t unit_id, uint32_t ability_flag) {
#endif
    int32_t faction_id = unit_id / 64;
    if (Units[unit_id].ability_flags & ability_flag) return 1;
    if (!faction_id) return 0;
    MFaction& m = MFactions[faction_id];
    if ((m.rule_flags & 0x80) && ability_flag == 2) return 1;
    int32_t i = 0;
    for (HA_RECORD_LOOP(0); i < m.faction_bonus_count; i++, HA_RECORD_LOOP(i)) {
        if (m.faction_bonus_id[i] == 15) {
            int32_t preq = Ability[m.faction_bonus_val1[i]].preq_tech;
            HA_RECORD_TECH(faction_id, i, preq, faction_id);
            if (has_tech(preq, faction_id) && (ability_flag & (1u << (m.faction_bonus_val1[i] & 31)))) return 1;
        }
    }
    if (Units[unit_id].weapon_id == 21 && ability_flag == 0x10000000 && SecretProjects[34] >= 0 &&
        (uint8_t)Bases[SecretProjects[34]].faction_id == faction_id)
        return 1;
    return 0;
}

// ---- helpers ----------------------------------------------------------------------------------

int32_t veh_vector_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    int32_t dy = std::abs(y1 - y2);
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t big = a > b ? a : b;
    int32_t small = a < b ? a : b;
    return big - ((((a + b) >> 1) - small + 1) >> 1);
}

namespace {
// Raw view of the vehicle table: the original reads Vehs[-1] in a few places.
uint8_t* veh_raw(int32_t veh_id) { return reinterpret_cast<uint8_t*>(&Vehs[0]) + veh_id * (int32_t)sizeof(VEH); }
int16_t veh_unit_raw(int32_t veh_id) { return *reinterpret_cast<int16_t*>(veh_raw(veh_id) + offsetof(VEH, unit_id)); }
// Wrapped horizontal distance |x1 - x2| as the original computes it.
int32_t wrap_dx(int32_t x1, int32_t x2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    return dx;
}
}  // namespace

// ---- veh module ---------------------------------------------------------------------------------

int32_t GAMEAPI do_unity_crash(int32_t x, int32_t y, int32_t faction_id) {
    uint32_t landmarks = map_tile(x, y)->landmarks;
    if ((landmarks & 0x80004000) != 0x4000) return 0;
    switch ((int32_t)landmarks >> 24) {
    case 0:
    case 8: {
        int32_t unit_id;
        if (((int32_t)landmarks >> 24) == 0) {
            unit_id = 20;  // Unity Mining Laser
            if (faction_id == CurrentPlayerFaction) {
                FX_play(&Sounds, 0x16);
                NetMsg_pop(&NetMsg, "UNITYMINER", -5000, 0, "unitfound_sm.pcx");
                FX_fade(&Sounds, 0x16);
            }
        } else {
            unit_id = 12;  // Unity Scout Chopper
            if (faction_id == CurrentPlayerFaction) {
                FX_play(&Sounds, 0x16);
                NetMsg_pop(&NetMsg, "UNITYCHOPPER", -5000, 0, "unitfound_sm.pcx");
                FX_fade(&Sounds, 0x16);
            }
        }
        int32_t veh_id = veh_init(unit_id, faction_id, x, y);
        veh_raw(veh_id)[offsetof(VEH, morale)] = 2;  // BUG(original): writes Vehs[-1] when the table is full
        return 1;
    }
    case 10: {
        int32_t credits = CurrentTurn > 49 ? 300 : 150;
        Factions[faction_id].energy_credits += credits;
        parse_num(0, credits);
        InfoWin_on_redraw(&InfoWindow);
        if (faction_id != CurrentPlayerFaction) return 1;
        FX_play(&Sounds, 0x16);
        NetMsg_pop(&NetMsg, "UNITYENGINE", -5000, 0, "foundeng_sm.pcx");
        FX_fade(&Sounds, 0x16);
        return 1;
    }
    case 19:
        for (int32_t base_id = 0; base_id < BaseCount; base_id++) {
            BASE& base = Bases[base_id];
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, MFactions[(uint8_t)base.faction_id].filename);
            for (char* c = StrBuffer; *c; c++) *c = (char)std::toupper((uint8_t)*c);  // CharUpperA
            if (base_has_fac(base_id, 1) && !(MFactions[(uint8_t)base.faction_id].rule_flags & 0x80) &&
                std::strcmp(StrBuffer, "FUNGBOY") != 0)
                spot_loc(base.x, base.y, faction_id);
        }
        if (faction_id != CurrentPlayerFaction) return 1;
        draw_map(1);
        GameDrawState |= 4;
        GraphicWin_redraw(&WorldWin);
        FX_play(&Sounds, 0x16);
        NetMsg_pop(&NetMsg, "UNITYRADAR", -5000, 0, "foundradar_sm.pcx");
        FX_fade(&Sounds, 0x16);
        return 1;
    default:
        return 0;
    }
}

int32_t GAMEAPI is_sensor(int32_t x, int32_t y) {
    if (map_tile(x, y)->items & 0x80000000) return 1;
    int32_t base_id = base_find(x, y);
    if (base_id != -1) {
        BASE& base = Bases[base_id];
        if (wrap_dx(x, base.x) != 0 && wrap_dx(x, base.x) != 2) return 0;
        int32_t dy = y - base.y;
        if (dy != 0) {
            if (dy < 0) dy = base.y - y;
            if (dy != 2) return 0;
        }
        if (base_has_fac(base_id, 0x29)) return 2;
    }
    return 0;
}

int32_t GAMEAPI has_temple(int32_t faction_id) {
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2) {
            MAP* sq = map_tile(x, y);
            uint32_t landmarks = sq->landmarks;
            if ((landmarks & 0x80002000) == 0x2000 && (landmarks & 0xFF000000) == 0) {
                if (whose_territory(faction_id, x, y, nullptr, 0) == faction_id &&
                    (map_tile(x, y)->visibility & (uint8_t)(1 << (faction_id & 31))))
                    return 1;
            }
        }
    }
    return 0;
}

int32_t GAMEAPI veh_at(int32_t x, int32_t y) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX || (map_tile(x, y)->items & 2)) {
        for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
            if (Vehs[veh_id].x == x && Vehs[veh_id].y == y) return stack_top(veh_id);
        }
        if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
            if (!VehBitError) log_say_2("Vehicle Bit Error  (x, y)", x, y, 0);
            if ((GameState & 0x40) || (GameState & 0x1000) || MultiplayerActive) {
                if (VehBitError) return -1;
                VehBitError = 1;
            }
            rebuild_vehicle_bits();
        }
    }
    return -1;
}

int32_t GAMEAPI veh_lift(int32_t veh_id) {
    VEH& veh = Vehs[veh_id];
    int16_t prev = veh.prev_veh_id_stack;
    if (prev >= 0) Vehs[prev].next_veh_id_stack = veh.next_veh_id_stack;
    if (veh.next_veh_id_stack < 0) {
        if (prev < 0) {  // last vehicle of the tile
            int32_t x = veh.x, y = veh.y;
            if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) bit_set(x, y, 2, 0);
        }
    } else {
        Vehs[veh.next_veh_id_stack].prev_veh_id_stack = veh.prev_veh_id_stack;
    }
    VehLiftX = veh.x;
    VehLiftY = veh.y;
    VehDropLiftVehID = veh_id;
    veh.x = -1;
    veh.y = -1;
    veh.next_veh_id_stack = -1;
    veh.prev_veh_id_stack = -1;
    return veh_id;
}

int32_t GAMEAPI veh_drop(int32_t veh_id, int32_t x, int32_t y) {
    int32_t top = veh_at(x, y);
    VEH& veh = Vehs[veh_id];
    veh.next_veh_id_stack = (int16_t)top;
    veh.prev_veh_id_stack = -1;
    veh.x = (int16_t)x;
    veh.y = (int16_t)y;
    VehDropLiftVehID = -1;
    if (top < 0) {
        if (y < 0) return veh_id;
        if (y < MapAreaY && x >= 0 && x < MapAreaX && !(map_tile(x, y)->items & 1))
            owner_set(x, y, veh.faction_id);
    } else {
        Vehs[top].prev_veh_id_stack = (int16_t)veh_id;
    }
    if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
        uint32_t bits = (veh.faction_id == 0 || unit_triad(veh.unit_id) == 2) ? 0 : 0x100000;
        bit_set(x, y, bits | 2, 1);
    }
    return veh_id;
}

void GAMEAPI sleep(int32_t veh_id) {
    Vehs[veh_id].order = 1;
    Vehs[veh_id].waypoint_x[0] = -1;
    Vehs[veh_id].waypoint_y[0] = 0;
}

void GAMEAPI veh_demote(int32_t veh_id) {
    if (veh_id < 0 || Vehs[veh_id].next_veh_id_stack < 0) return;
    int32_t last = Vehs[veh_id].next_veh_id_stack;
    while (Vehs[last].next_veh_id_stack >= 0) last = Vehs[last].next_veh_id_stack;
    if (last != veh_id) {
        veh_lift(veh_id);
        Vehs[last].next_veh_id_stack = (int16_t)veh_id;
        Vehs[veh_id].prev_veh_id_stack = (int16_t)last;
        Vehs[veh_id].next_veh_id_stack = -1;
        Vehs[veh_id].x = Vehs[last].x;
        Vehs[veh_id].y = Vehs[last].y;
    }
}

void GAMEAPI veh_promote(int32_t veh_id) {
    int32_t top = stack_top(veh_id);
    if (top != veh_id) {
        veh_lift(veh_id);
        veh_drop(veh_id, Vehs[top].x, Vehs[top].y);
    }
}

void GAMEAPI veh_clear(int32_t veh_id, int32_t unit_id, int32_t faction_id) {
    VEH& veh = Vehs[veh_id];
    veh.x = -4;
    veh.y = -4;
    veh.year_end_lurking = 0;
    veh.pad_0 = 0;
    veh.flags = 0;
    veh.faction_id = (uint8_t)faction_id;
    veh.unit_id = (int16_t)unit_id;
    veh.next_veh_id_stack = -1;
    veh.prev_veh_id_stack = -1;
    veh.waypoint_count = 0;
    veh.patrol_current_point = 0;
    veh.order = 0;
    for (int32_t i = 0; i < 4; i++) {
        veh.waypoint_x[i] = -1;
        veh.waypoint_y[i] = -1;
    }
    veh.state = 0;
    veh.moves_spent = 0;
    veh.damage_taken = 0;
    veh.order_auto_type = 0;
    veh.movement_turns = 0;
    veh.iter_count = 0;
    veh.status_icon = 0;
    veh.visibility = 0;
    veh.home_base_id = -1;
    veh.morale = (uint8_t)(MFactions[faction_id].rule_morale + 1);
    veh.rotate_angle = 2;
    veh.probe_action = 0;
    veh.probe_sabotage_id = 0;
}

int32_t GAMEAPI veh_init(int32_t unit_id, int32_t faction_id, int32_t x, int32_t y) {
    int32_t veh_id = VehCount;
    if (veh_id >= 0x800) return -1;
    VehCount = veh_id + 1;
    log_say_2("Adding Vehicle (v,who,v2)", veh_id, faction_id, unit_id);
    Faction& f = Factions[faction_id];
    if ((uint8_t)Units[unit_id].plan < 8) f.total_combat_units++;
    f.units_active[unit_id]++;
    veh_clear(veh_id, unit_id, faction_id);
    int32_t base_id = base_find(x, y);
    if (Units[unit_id].plan == 12) {  // artifact
        if (GameState & 0x40000) Vehs[veh_id].flags |= 0x20;
    } else if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id == (uint32_t)faction_id) {
        Vehs[veh_id].home_base_id = (int16_t)base_id;
        if (ComputeBaseID == base_id) ComputeBaseID = -1;
    }
    veh_drop(veh_id, x, y);
    spot_all(veh_id, 1);
    // AI: units created on the continent of the faction's target may be assigned to the attack.
    if (f.target_x < 0 || map_is_ocean(x, y)) return veh_id;
    VEH& veh = Vehs[veh_id];
    uint8_t plan = (uint8_t)Units[veh.unit_id].plan;
    if (plan > 2 && plan != 11) return veh_id;
    int32_t region = map_tile(x, y)->region;
    if (map_tile(f.target_x, f.target_y)->region != region) return veh_id;
    int32_t enemy_id = -1;
    int32_t best = 0;
    for (int32_t i = 1; i < 8; i++) {
        // BUG(original): compares the faction's own base count in the region, not the enemy's
        if (i != faction_id && (f.diplo_status[i] & 0x10) && f.region_total_bases[region] > best) {
            best = f.region_total_bases[region];
            enemy_id = i;
        }
    }
    if (enemy_id >= 0) {
        base_find_3(x, y, enemy_id, region, -1, -1);
        if (veh_vector_dist(x, y, f.target_x, f.target_y) > BaseFindDist) return veh_id;
    }
    int32_t limit = f.region_total_bases[region] * 2 / 3;
    limit = limit < 3 ? 3 : limit > 8 ? 8 : limit;
    if (f.region_base_plan[region] == 7) {
        int32_t cap = f.unk_70 * 2 / 0x400000;
        if (limit >= cap) limit = cap;
    }
    if (f.unk_71 - bit_count((uint32_t)f.unk_72) >= limit) return veh_id;
    int32_t weap = weap_strat(Units[veh.unit_id].weapon_id, faction_id);
    int32_t arm = arm_strat(Units[veh.unit_id].armor_id, faction_id);
    if (unit_triad(veh.unit_id) != 0 || has_abil(veh.unit_id, 0x10)) return veh_id;
    if (Units[veh.unit_id].plan == 11) {  // probe team
        if (!(f.unk_72 & 2)) {
            f.unk_71++;
            f.unk_72 |= 2;
            veh.state |= 0x40000;
        }
        return veh_id;
    }
    if (weap < arm) {
        if (f.unk_72 & 1) return veh_id;
        if (f.region_base_plan[region] == 7) return veh_id;
    } else if (f.region_base_plan[region] != 7) {
        int32_t odds = f.region_total_bases[region] * 2 / 3;
        odds = odds < 3 ? 3 : odds > 8 ? 8 : odds;
        if (map_rand_mod(odds + 2) == 0) {
            veh.state |= 0x20000;
            return veh_id;
        }
    }
    veh.state |= 0x40000;
    f.unk_71++;
    if (weap < arm) f.unk_72 |= 1;
    return veh_id;
}

void GAMEAPI veh_kill(int32_t veh_id) {
    if (veh_id < 0) return;
    if (MultiplayerActive) DeletionList_add_deletion(&DeleteList, veh_id);
    log_say_2("Killing vehicle (v,who,v2)", veh_id, Vehs[veh_id].faction_id, Vehs[veh_id].unit_id);
    int32_t x = Vehs[veh_id].x;
    int32_t home_base_id = Vehs[veh_id].home_base_id;
    int32_t y = Vehs[veh_id].y;
    int32_t unit_id = Vehs[veh_id].unit_id;
    uint8_t faction_id = Vehs[veh_id].faction_id;
    if ((uint8_t)Units[unit_id].plan < 8) Factions[faction_id].total_combat_units--;
    Factions[faction_id].units_active[unit_id]--;
    veh_lift(veh_id);
    if (veh_id < VehCount - 1)
        std::memmove(&Vehs[veh_id], &Vehs[veh_id + 1], (size_t)(VehCount - veh_id - 1) * sizeof(VEH));
    if (veh_id < 0x800) VehCount--;
    for (int32_t i = 0; i < VehCount; i++) {
        VEH& veh = Vehs[i];
        if (veh_id < veh.prev_veh_id_stack) veh.prev_veh_id_stack--;
        if (veh_id < veh.next_veh_id_stack) veh.next_veh_id_stack--;
        if (veh.order == 1) {  // sentry/board: waypoint_x[0] is the transport
            if (veh.waypoint_x[0] == veh_id) {
                veh.order = 0;
            } else if (veh_id < veh.waypoint_x[0]) {
                veh.waypoint_x[0]--;
            }
        }
    }
    if (MapWinVehA == veh_id) MapWinVehA = -1;
    else if (veh_id < MapWinVehA) MapWinVehA--;
    if (MapWinVehB == veh_id) MapWinVehB = -1;
    else if (veh_id < MapWinVehB) MapWinVehB--;
    if (StatusWinVehA == veh_id) StatusWinVehA = -1;
    else if (veh_id < StatusWinVehA) StatusWinVehA--;
    if (StatusWinVehB == veh_id) StatusWinVehB = -1;
    else if (veh_id < StatusWinVehB) StatusWinVehB--;
    if (CurrentVehID && veh_id <= CurrentVehID) CurrentVehID--;
    if (MultiplayerActive && NetVehIDPtr && NetVehIDPtr != &MapWinVehA && NetVehIDPtr != &CurrentVehID &&
        NetVehIDPtr != &MapWinVehB && veh_id <= *NetVehIDPtr)
        (*NetVehIDPtr)--;
    BaseWin_check_loc(&BaseWin, x, y, 0);
    BaseWin_check_base(&BaseWin, home_base_id);
}

void GAMEAPI kill(int32_t veh_id) {
    int32_t x = Vehs[veh_id].x;
    int32_t y = Vehs[veh_id].y;
    uint8_t faction_id = Vehs[veh_id].faction_id;
    int32_t deep_radar = has_abil(Vehs[veh_id].unit_id, 2);
    int32_t home_base_id = Vehs[veh_id].home_base_id;
    bool on_map_tile = y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX;
    if (veh_cargo(veh_id) == 0 || (on_map_tile && !map_is_ocean(x, y))) {
        veh_kill(veh_id);
    } else if (!on_map_tile && x == -2) {
        stack_kill(veh_id);
        return;
    } else {
        stack_veh(veh_id, 2);
        stack_kill(veh_id);
    }
    BaseWin_check_base(&BaseWin, home_base_id);
    if (!VehBattleState[0] && on_map_tile) {
        if (faction_id == CurrentPlayerFaction) {
            if (stack_check(veh_at(x, y), 1, faction_id, -1, -1) == 0 || deep_radar) {
                draw_radius(x, y, 2, 2);
                return;
            }
        }
        draw_tile(x, y, 2);
    }
}

int32_t GAMEAPI veh_find(int32_t x, int32_t y, int32_t faction_id, int32_t veh_id_skip) {
    int32_t best_dist = 9999;
    int32_t best_id = -1;
    VehFindDist = 9999;
    if (VehCount <= 0) return -1;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        if ((faction_id < 0 || Vehs[veh_id].faction_id == faction_id) && veh_id != veh_id_skip) {
            int32_t dist = veh_vector_dist(x, y, Vehs[veh_id].x, Vehs[veh_id].y);
            if (dist <= best_dist) {
                best_id = veh_id;
                best_dist = dist;
            }
        }
    }
    if (best_id >= 0) VehFindDist = best_dist;
    return best_id;
}

int32_t GAMEAPI can_arty(int32_t unit_id, int32_t sea_triad_retn) {
    if (Weapon[Units[unit_id].weapon_id].offense_value > 0 || unit_id == 15) {
        if (unit_psi_armor(unit_id) && unit_id != 15) return 0;
        int32_t triad = unit_triad(unit_id);
        if (triad == 1) return sea_triad_retn;
        if (triad != 2) return has_abil(unit_id, 0x8000);
    }
    return 0;
}

int32_t GAMEAPI SHADOWABLE(morale_veh)(int32_t veh_id, int32_t check_drone_riot, int32_t faction_id_vs_native) {
    VEH& veh = Vehs[veh_id];
    uint32_t faction_id = veh.faction_id;
    if (faction_id == 0) return morale_alien(veh_id, faction_id_vs_native);
    int32_t unit_id = veh.unit_id;
    MFaction& m = MFactions[faction_id];
    if (Units[unit_id].plan == 11) {  // probe team
        int32_t level = Factions[faction_id].SE_probe;
        level = level < 0 ? 0 : level > 3 ? 3 : level;
        if (SecretProjects[30] >= 0 && (uint8_t)Bases[SecretProjects[30]].faction_id == faction_id) level += 2;
        for (int32_t tech_id = 0; tech_id < 89; tech_id++) {
            if ((Tech[tech_id].flags & 2) && has_tech(tech_id, faction_id)) level++;
        }
        level += veh.morale;
        return level < 2 ? 2 : level > 6 ? 6 : level;
    }
    if (unit_id < 64 && unit_psi_weapon(unit_id)) return veh.morale > 6 ? 6 : veh.morale;
    int32_t bonus = Factions[faction_id].SE_morale;
    bonus = bonus < -4 ? -4 : bonus > 4 ? 4 : bonus;
    if (bonus < -1) bonus++;
    else if (bonus >= 2) bonus--;
    if (m.rule_morale < 0) bonus += m.rule_morale;
    int32_t home_base_id = veh.home_base_id;
    if (home_base_id >= 0) {
        if (base_has_fac(home_base_id, 2) && bonus < 0) bonus /= 2;
        if (base_has_fac(home_base_id, 0x23) && unit_is_native(veh.unit_id) && bonus < 0) bonus /= 2;
    }
    if (m.rule_morale > 0) bonus += m.rule_morale;
    if ((m.rule_flags & 0x8000) && bonus < 0) bonus = 0;
    if (check_drone_riot && home_base_id >= 0 && (Bases[home_base_id].state_flags & 2) && !(m.rule_flags & 0x8000)) {
        bonus--;
        bonus = bonus < 0 ? 0 : bonus > 6 ? 6 : bonus;
    }
    int32_t morale = bonus + veh.morale;
    return morale < 0 ? 0 : morale > 6 ? 6 : morale;
}

int32_t GAMEAPI offense_proto(int32_t unit_id, int32_t veh_id_def, int32_t is_bombard) {
    CWeapon& weapon = Weapon[Units[unit_id].weapon_id];
    if (weapon.mode == 11 && veh_id_def >= 0 && Units[Vehs[veh_id_def].unit_id].plan == 11) return 16;
    if ((is_bombard || (weapon.offense_value >= 0 &&
                        (veh_id_def < 0 || !unit_psi_armor(Vehs[veh_id_def].unit_id)))) &&
        veh_unit_raw(veh_id_def) != 15 && unit_id != 15) {  // reads Vehs[-1] without a defender
        int32_t value = weapon.offense_value < 0 ? -weapon.offense_value : weapon.offense_value;
        if (Chassis[Units[unit_id].chassis_id].missile && weapon.offense_value < 99) value = value * 3 / 2;
        if (veh_id_def >= 0) value <<= 3;
        return value;
    }
    if (veh_id_def < 0) return Rules.psi_combat_ratio_atk[0];
    return Rules.psi_combat_ratio_atk[veh_triad(veh_id_def)] << 3;
}

int32_t GAMEAPI armor_proto(int32_t unit_id, int32_t veh_id_atk, int32_t is_bombard) {
    if (Weapon[Units[unit_id].weapon_id].mode == 11 && veh_id_atk >= 0 &&
        Units[Vehs[veh_id_atk].unit_id].plan == 11)
        return 16;
    if ((!is_bombard || veh_unit_raw(veh_id_atk) == 15 || unit_id == 15) &&
        (unit_psi_armor(unit_id) || (veh_id_atk >= 0 && unit_psi_weapon(Vehs[veh_id_atk].unit_id)))) {
        if (veh_id_atk < 0) return Rules.psi_combat_ratio_def[0];
        return Rules.psi_combat_ratio_def[unit_triad(unit_id)] << 3;
    }
    int32_t value = Armor[Units[unit_id].armor_id].defense_value;
    value = value < 1 ? 1 : value > 9999 ? 9999 : value;
    if (veh_id_atk >= 0) return value << 3;
    return value;
}

int32_t GAMEAPI SHADOWABLE(speed_proto)(int32_t unit_id) {
    UNIT& u = Units[unit_id];
    int32_t triad = Chassis[u.chassis_id].triad;
    int32_t value = Chassis[u.chassis_id].speed;
    if (unit_id == 19) return 0;  // Fungal Tower
    if (triad == 2) value += u.reactor_id * 2;
    if (Weapon[u.weapon_id].mode == 7) value--;  // transport
    if (has_abil(unit_id, 0x800000)) value--;    // slow
    if (has_abil(unit_id, 0x400)) {               // antigrav struts
        if (Chassis[u.chassis_id].triad == 2) value += u.reactor_id * 2;
        else value++;
    }
    if (Chassis[u.chassis_id].triad == 2) {
        if (has_abil(unit_id, 0x8000000)) value += 2;  // fuel nanocells
        if (SecretProjects[35] >= 0 && (uint8_t)Bases[SecretProjects[35]].faction_id == unit_id / 64) value += 2;
        if (has_abil(unit_id, 0x20)) value = value * 4 / 5;  // air superiority
        if (Weapon[u.weapon_id].mode == 7) value /= 2;
    }
    if (value < 1) value = 1;
    else if (value > 99) return Rules.move_rate_roads * 99;
    return Rules.move_rate_roads * value;
}

int32_t SpeedStackResidue = 0;

// What the original speed() leaves in the dead stack slot Path_find reads (see SpeedStackResidue):
// has_abil's saved EDI (unit_id * 0x34, from speed_proto) unless morale_veh then calls something
// (morale_alien, has_tech, bitmask), whose frame leaves a non-zero value there. speed_proto returns
// before calling has_abil for unit 19, which leaves older content.
static void speed_note_stack_residue(int32_t veh_id) {
    const VEH& veh = Vehs[veh_id];
    int32_t unit_id = veh.unit_id;
    bool morale_calls = false;
    if (veh.faction_id == 0) {
        morale_calls = true;
    } else if (Units[unit_id].plan == 11) {
        for (int32_t tech_id = 0; tech_id < 89 && !morale_calls; tech_id++)
            if (Tech[tech_id].flags & 2) morale_calls = true;
    } else if (!(unit_id < 64 && unit_psi_weapon(unit_id))) {
        morale_calls = veh.home_base_id >= 0;
    }
    if (morale_calls)
        SpeedStackResidue = 1;
    else if (unit_id != 19)
        SpeedStackResidue = unit_id * 0x34;
}

int32_t GAMEAPI SHADOWABLE(speed)(int32_t veh_id, int32_t skip_morale) {
    VEH& veh = Vehs[veh_id];
    int32_t value = speed_proto(veh.unit_id);
    if (veh_triad(veh_id) == 1 && SecretProjects[10] >= 0 &&
        Bases[SecretProjects[10]].faction_id == (int8_t)veh.faction_id)
        value += Rules.move_rate_roads * 2;
    speed_note_stack_residue(veh_id);
    if (morale_veh(veh_id, 1, 0) == 6 && !skip_morale && (veh.unit_id >= 64 || !unit_psi_weapon(veh.unit_id)))
        value += Rules.move_rate_roads;
    uint8_t damage = veh.damage_taken;
    if (damage) {
        UNIT& u = Units[veh.unit_id];
        int32_t triad = Chassis[u.chassis_id].triad;
        if (triad != 2) {
            int32_t reactor = u.reactor_id < 1 ? 1 : u.reactor_id > 100 ? 100 : u.reactor_id;
            int32_t max_hp = u.plan == 12 ? 1 : reactor * 10;
            int32_t divisor = max_hp < 1 ? 1 : max_hp > 99 ? 99 : max_hp;
            int32_t hp = max_hp - damage;
            hp = hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
            int32_t moves = (hp * (value / Rules.move_rate_roads) - 1 + divisor) / divisor;
            int32_t minimum = triad == 1 ? 2 : 1;
            if (moves < minimum) moves = minimum;
            else if (moves > 999) moves = 999;
            value = Rules.move_rate_roads * moves;
        }
    }
    return veh.unit_id != 19 ? value : 0;
}

int32_t GAMEAPI veh_cargo(int32_t veh_id) {
    int32_t unit_id = Vehs[veh_id].unit_id;
    if (Units[unit_id].carry_capacity && unit_is_native(unit_id)) return Vehs[veh_id].morale + 1;
    return Units[unit_id].carry_capacity;
}

int32_t GAMEAPI prototype_factor(int32_t unit_id) {
    int32_t faction_id = unit_id / 64;
    if (MFactions[faction_id].rule_flags & 0x200) return 0;
    if (Factions[faction_id].diff_level < 2) return 0;
    int32_t triad = unit_triad(unit_id);
    if (triad == 1) return Rules.extra_cost_prototype_sea;
    if (triad != 2) return Rules.extra_cost_prototype_land;
    return Rules.extra_cost_prototype_air;
}

int32_t GAMEAPI veh_cost(int32_t unit_id, int32_t base_id, int32_t* has_proto_cost) {
    int32_t cost = Units[unit_id].cost;
    // BUG(original): the Brood Pit check reads Bases[-1] without a base
    if (unit_is_native(unit_id) && base_has_fac(base_id, 0x23)) cost = (cost * 3) >> 2;
    if (Units[unit_id].plan == 8 && base_id >= 0) cost = cost < 1 ? 1 : cost > 999 ? 999 : cost;  // colony pod
    if (unit_id < 64 || (Units[unit_id].unit_flags & 4)) {
        if (has_proto_cost) *has_proto_cost = 0;
        return cost;
    }
    int32_t faction_id = unit_id / 64;
    int32_t factor;
    if (MFactions[faction_id].rule_flags & 0x200) {
        factor = 0;
    } else if (Factions[faction_id].diff_level < 2) {
        factor = 0;
    } else {
        int32_t triad = unit_triad(unit_id);
        factor = triad == 1 ? Rules.extra_cost_prototype_sea
                 : triad == 2 ? Rules.extra_cost_prototype_air : Rules.extra_cost_prototype_land;
    }
    int32_t extra = (base_cost(unit_id) * factor + 50) / 100;
    if (base_id >= 0 && base_has_fac(base_id, 10)) extra = 0;  // Skunkworks
    cost += extra;
    if (has_proto_cost) *has_proto_cost = extra != 0;
    return cost;
}

namespace {
// Common test of veh_selectable/veh_unmoved/veh_ready: the viewer's own, selectable vehicle.
bool veh_is_viewers(int32_t veh_id) {
    VEH& veh = Vehs[veh_id];
    return veh.y >= 0 && veh.y < MapAreaY && veh.x >= 0 && veh.x < MapAreaX &&
           veh.faction_id == CurrentPlayerFaction && (!(veh.flags & 0x40) || !(veh.flags & 0x400));
}
}  // namespace

int32_t GAMEAPI veh_selectable(int32_t veh_id) {
    if (veh_id < 0) return 0;
    VEH& veh = Vehs[veh_id];
    if (veh.y < 0 || veh.y >= MapAreaY || veh.x < 0 || veh.x >= MapAreaX) return 0;
    if (veh.faction_id != CurrentPlayerFaction) return 0;
    if ((veh.flags & 0x40) && (veh.flags & 0x400)) return 0;
    return veh_jail(veh_id) == 0;
}

int32_t GAMEAPI veh_unmoved(int32_t veh_id) {
    if (veh_id < 0 || !veh_is_viewers(veh_id) || veh_jail(veh_id)) return 0;
    VEH& veh = Vehs[veh_id];
    int8_t order = (int8_t)veh.order;
    if ((veh.state & 0x4000000) || order < 4 || order > 23) {
        int32_t moves = speed(veh_id, 0);
        int32_t left = moves - veh.moves_spent;
        if (left < 0 || (left < 1000 && moves == veh.moves_spent)) return 0;
    }
    return 1;
}

int32_t GAMEAPI veh_ready(int32_t veh_id) {
    if (veh_id < 0 || !veh_is_viewers(veh_id) || veh_jail(veh_id)) return 0;
    VEH& veh = Vehs[veh_id];
    if (!(veh.state & 0x4200) && (veh.order == 1 || veh.order == 2 || veh.order == 3)) return 0;
    int32_t moves = speed(veh_id, 0);
    int32_t left = moves - veh.moves_spent;
    return left >= 0 && (left > 999 || moves != veh.moves_spent);
}

int32_t GAMEAPI veh_jail(int32_t veh_id) {
    VEH& veh = Vehs[veh_id];
    if (veh_triad(veh_id) == 0 && veh.order == 1 && veh.waypoint_x[0] >= 0 && veh_triad(veh.waypoint_x[0]) == 2) {
        MAP* sq = map_tile(veh.x, veh.y);
        if ((!(sq->items & 1) || (sq->val2 & 0xF) > 7 || (sq->val2 & 0xF) == 0) && !(sq->items & 0x40000)) return 1;
    }
    return 0;
}

void GAMEAPI veh_skip(int32_t veh_id) { Vehs[veh_id].moves_spent = (uint8_t)speed(veh_id, 0); }

int32_t GAMEAPI veh_fake(int32_t unit_id, int32_t faction_id) {
    veh_clear(0x800, unit_id, faction_id);
    return 0x800;
}

int32_t GAMEAPI veh_wake(int32_t veh_id) {
    VEH& veh = Vehs[veh_id];
    int8_t order = (int8_t)veh.order;
    if (order > 3 && order < 24 && !(veh.state & 0x4000000)) {  // terraforming
        veh.moves_spent = (uint8_t)(speed(veh_id, 0) - Rules.move_rate_roads);
        uint8_t turns = veh.movement_turns;
        if (turns) {
            int32_t left = turns - contribution(veh_id, (int8_t)veh.order - 4);
            if (left < 0) left = 0;
            veh.movement_turns = (uint8_t)left;
        }
    }
    if ((veh.state & 0x200) && veh.order_auto_type == 11 && !(veh.state & 4)) veh.moves_spent = 0;
    veh.order = 0;
    veh.state &= 0xF4FFBDFF;
    return veh_id;
}

#ifdef SMACX_HYBRID
// Hybrid: these functions run the original code a second time so that the dead stack they leave
// is the original's (Path_find reads it uninitialised, see "Known uninitialised reads").
HYBRID_SHADOW_THUNK(int32_t, speed, (int32_t veh_id, int32_t skip_morale), 2, 0x5C1545,
                    "push %ebp\n mov %esp, %ebp\n push %ebx\n push %esi")
HYBRID_SHADOW_THUNK(int32_t, speed_proto, (int32_t unit_id), 1, 0x5C13B7,
                    "push %ebp\n mov %esp, %ebp\n push %ebx\n mov 8(%ebp), %ebx")
HYBRID_SHADOW_THUNK(int32_t, morale_veh, (int32_t veh_id, int32_t check_drone_riot, int32_t faction_id_vs_native),
                    3, 0x5C0E46, "push %ebp\n mov %esp, %ebp\n sub $8, %esp")
#endif
