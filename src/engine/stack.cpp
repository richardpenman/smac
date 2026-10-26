// Spotting and unit stacks (see stack.h).
#include "engine/stack.h"

#include <cstdlib>

#include "engine/base.h"
#include "engine/design.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"

namespace {
bool in_map(int32_t x, int32_t y) { return y >= 0 && y < game().map.y && x >= 0 && x < game().map.x; }

// The faction's view of the tile items: visible_items[faction_id - 1]. The original also reads it
// for faction 0, which aliases the landmarks field.
uint32_t& tile_view(MAP* sq, int32_t faction_id) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(sq) + offsetof(MAP, landmarks) + faction_id * 4);
}

// Whether newly seen tiles of faction_id are highlighted (the viewer, not in the scenario editor
// or omniscient view).
bool marks_discovery(int32_t faction_id) {
    return faction_id == CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) &&
           !(game().factions[faction_id].player_flags & PFLAG_MAP_REVEALED);
}

// Body of spot_tile for a tile on the map (inlined in many places of the original).
void spot_tile_in_map(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    uint8_t bit = (uint8_t)(1 << (faction_id & 31));
    if (faction_id == CurrentPlayerFaction && !(sq->visibility & bit) && !(GameState & STATE_OMNISCIENT_VIEW) &&
        !(game().factions[faction_id].player_flags & PFLAG_MAP_REVEALED)) {
        sq->landmarks |= 0x400000;
        GameDrawState |= 1;
    }
    sq->visibility |= bit;
    synch_bit(x, y, faction_id);
}

// faction_id sees every vehicle of the stack (bottom loop of spot_stack, inlined in the original).
void spot_stack_vehs(int32_t veh_id, int32_t faction_id) {
    for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
        game().vehs[v].visibility |= (uint8_t)(1 << (faction_id & 31));
        if (faction_id) game().vehs[v].flags &= 0xFBBF;
    }
}

// Same, starting from veh_top() (spot_all calls the goals helper there).
void spot_stack_vehs_top(int32_t veh_id, int32_t faction_id) {
    for (int32_t v = veh_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
        game().vehs[v].visibility |= (uint8_t)(1 << (faction_id & 31));
        if (faction_id) game().vehs[v].flags &= 0xFBBF;
    }
}

// spot_base body (inlined by the original).
void spot_base_body(int32_t base_id, int32_t faction_id, bool call_on_map) {
    BASE& base = game().bases[base_id];
    base.visibility |= (uint8_t)(1 << (faction_id & 31));
    base.factions_pop_size_intel[faction_id] = base.pop_size;
    int32_t x = base.x, y = base.y;
    if (call_on_map ? on_map(x, y) != 0 : in_map(x, y)) spot_tile_in_map(x, y, faction_id);
}
}  // namespace

void GAMEAPI spot_tile(int32_t x, int32_t y, int32_t faction_id) {
    if (in_map(x, y)) spot_tile_in_map(x, y, faction_id);
}

void GAMEAPI spot_base(int32_t base_id, int32_t faction_id) { spot_base_body(base_id, faction_id, false); }

void GAMEAPI spot_stack(int32_t veh_id, int32_t faction_id) {
    if (veh_id >= 0) spot_tile(game().vehs[veh_id].x, game().vehs[veh_id].y, faction_id);
    if (veh_id >= 0) spot_stack_vehs(veh_id, faction_id);
}

void GAMEAPI unspot_stack(int32_t veh_id) {
    if (veh_id < 0) return;
    for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) game().vehs[v].visibility = 0;
}

void GAMEAPI spot_loc(int32_t x, int32_t y, int32_t faction_id) {
    spot_tile(x, y, faction_id);
    int32_t base_id = base_at(x, y);
    if (base_id < 0) {
        int32_t veh_id = veh_at(x, y);
        if (veh_id >= 0) spot_tile(game().vehs[veh_id].x, game().vehs[veh_id].y, faction_id);
        if (veh_id >= 0) spot_stack_vehs(veh_id, faction_id);
    } else {
        spot_base_body(base_id, faction_id, false);
    }
}

void GAMEAPI veh_alert_wake(int32_t veh_id) {
    VEH& veh = game().vehs[veh_id];
    if ((veh.state & 0x2000200) == 0x2000200 && veh.patrol_current_point < veh.waypoint_count) veh.state &= 0xFDFFFDFF;
    if ((veh.state & 0x1000200) == 0x1000200) veh.state &= 0xFEFFFDFF;
    veh.state &= 0xFFFFBFFF;
    if (veh.order == ORDER_MOVE_TO) veh.order = ORDER_NONE;
}

uint32_t GAMEAPI want_to_wake(int32_t faction_id, int32_t veh_id, int32_t spotted_veh_id) {
    VEH& veh = game().vehs[veh_id];
    if (veh_triad(veh_id) == 2 && veh.movement_turns && veh.order == ORDER_MOVE_TO) {
        // aircraft flying to a friendly base
        MAP* sq = map_tile(veh.waypoint_x[0], veh.waypoint_y[0]);
        uint32_t owner = sq->val2 & 0xF;
        if ((sq->items & BIT_BASE_IN_TILE) && owner < 8 &&
            (owner == veh.faction_id || (game().factions[veh.faction_id].diplo_status[owner] & DIPLO_PACT)))
            return 0;
    }
    uint32_t status = game().factions[veh.faction_id].diplo_status[faction_id];
    uint32_t wake;
    if (status & 1) {  // pact
        if (MultiplayerActive) {
            wake = 0;
            goto check_triad;
        }
        wake = GamePreferences & PREF_AUTO_END_MOVE_SPOT_VEH_PACT;
    } else if (status & 2) {  // treaty
        if (MultiplayerActive) {
            wake = 1;
            goto check_triad;
        }
        wake = GamePreferences & PREF_AUTO_END_MOVE_SPOT_VEH_TREATY;
    } else if (status & 4) {  // truce
        if (MultiplayerActive) {
            wake = 1;
            goto check_triad;
        }
        wake = GamePreferences & PREF_AUTO_END_MOVE_SPOT_VEH_TRUCE;
    } else {
        if (MultiplayerActive) {
            wake = 1;
            goto check_triad;
        }
        wake = GamePreferences & PREF_AUTO_END_MOVE_SPOT_VEH_WAR;
    }
    if (!(GamePreferences & PREF_AUTO_DONT_END_MOVE_DIFF_TRIAD)) return wake;
check_triad:
    if (veh_id >= 0 && spotted_veh_id >= 0 && veh_triad(veh_id) != veh_triad(spotted_veh_id) &&
        ((veh.state & 0x1000200) != 0x1000200 || veh_triad(spotted_veh_id) == 0))
        wake = 0;
    return wake;
}

void GAMEAPI wake_stack(int32_t veh_id) {
    if (veh_id < 0) return;
    for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
        VEH& veh = game().vehs[v];
        if (veh.order != ORDER_SENTRY_BOARD) continue;
        bool wake = veh_triad(v) != 0;
        if (!wake) {
            MAP* sq = map_tile(veh.x, veh.y);
            if ((sq->climate & 0xE0) >= 0x60) {  // on land
                int32_t transport = veh.waypoint_x[0];
                wake = transport < 0 || veh_triad(transport) != 2 ||
                       ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) || (sq->items & BIT_AIRBASE);
            }
        }
        if (wake) {
            veh.order = ORDER_NONE;
            veh.state &= 0xFFFFBFFF;
        }
    }
}

int32_t GAMEAPI veh_stealth(int32_t veh_id) {
    if (has_abil(game().vehs[veh_id].unit_id, 0x40)) return 1;
    if (has_abil(game().vehs[veh_id].unit_id, 4)) return 1;
    if (game().vehs[veh_id].flags & VFLAG_INVISIBLE) return 1;
    MAP* sq = map_tile(game().vehs[veh_id].x, game().vehs[veh_id].y);
    if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 && game().vehs[veh_id].unit_id != 19) return 2;
    return 0;
}

void GAMEAPI spot_all(int32_t veh_id, int32_t flag) {
    int32_t temples = 0;     // monoliths around
    int32_t saw_tower = 0;   // met natives: fungal tower
    int32_t saw_native = 0;  // met natives: other native units
    if (!MultiplayerActive) flag = 1;
    VEH& veh = game().vehs[veh_id];
    const int32_t y = veh.y;
    const int32_t x = veh.x;
    const int32_t triad = veh_triad(veh_id);
    const int32_t stealth = veh_stealth(veh_id);
    const int32_t faction_id = veh.faction_id;
    const uint8_t fbit = (uint8_t)(1 << (faction_id & 31));
    int32_t sensor;
    if (has_abil(veh.unit_id, 2) || (is_sensor(x, y) && whose_territory(faction_id, x, y, nullptr, 0) == faction_id))
        sensor = 1;
    else
        sensor = 0;
    MAP* here = map_tile(x, y);
    const int32_t at_sea = (here->climate & 0xE0) < 0x60;
    uint32_t met = 0;     // factions encountered (bit mask)
    int32_t redraw = 0;   // redraw level of the area
    if (faction_id == 0) veh.visibility &= here->visibility;

    // Viewer's unit with the "highlight unseen" preference: redraw level of the area only.
    if ((GameMorePreferences & MPREF_MAP_SHOW_FOG_WAR) && faction_id == CurrentPlayerFaction &&
        !(GameState & STATE_OMNISCIENT_VIEW)) {
        const int32_t outer = sensor ? 49 : 25;
        for (int32_t i = 0; i < outer; i++) {
            int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
            int32_t y2 = TableOffsetY[i] + y;
            if (!in_map(x2, y2)) continue;
            const int32_t inner = sensor ? 25 : 9;
            int32_t seen = 0;
            if (i < inner) {
                seen = 1;
            } else {
                if (map_tile(x2, y2)->landmarks & 0x200000) continue;
                for (int32_t j = 0; j < 25; j++) {
                    int32_t x3 = map_wrap_x(TableOffsetX[j] + x2);
                    int32_t y3 = TableOffsetY[j] + y2;
                    if (!in_map(x3, y3)) continue;
                    if (j < 9) {
                        MAP* sq = map_tile(x3, y3);
                        int32_t owner = ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8)
                                            ? sq->val2 & 0xF
                                            : -1;
                        if (owner == faction_id) {
                            seen = 1;
                            break;
                        }
                        if (diplo_status_raw(faction_id, owner) & 1) {
                            bool own = false;
                            for (int32_t v = veh_at(x3, y3); v >= 0; v = game().vehs[v].next_veh_id_stack) {
                                if (game().vehs[v].faction_id == faction_id) {
                                    own = true;
                                    break;
                                }
                            }
                            if (own) {
                                seen = 1;
                                break;
                            }
                            if (seen) break;
                        }
                    }
                    if (is_sensor(x3, y3)) {
                        if (map_tile(x3, y3)->owner == faction_id) {
                            seen = 1;
                            break;
                        }
                        // BUG(original): tests the base nearest to the unit, not to the sensor tile
                        int32_t base_id = base_find(x, y);
                        if (base_id != -1) {
                            BASE& base = game().bases[base_id];
                            int32_t dx = std::abs(x - base.x);
                            if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
                            int32_t dy = y - base.y;
                            if (dy < 0) dy = base.y - y;
                            if ((dx == 0 || dx == 2) && (dy == 0 || dy == 2) && base_has_fac(base_id, 0x29)) {
                                seen = 1;
                                break;
                            }
                        }
                    }
                    MAP* sq = map_tile(x3, y3);
                    int32_t base_owner = ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) ? sq->val2 & 0xF : -1;
                    if (base_owner == faction_id) {
                        seen = 1;
                        break;
                    }
                    int32_t veh_owner = ((sq->items & BIT_VEH_IN_TILE) && (sq->val2 & 0xF) < 8) ? sq->val2 & 0xF : -1;
                    if (veh_owner == faction_id || (diplo_status_raw(veh_owner, faction_id) & 1)) {
                        int32_t v = veh_at(x3, y3);
                        if (v >= 0) {
                            for (v = stack_top(v); v >= 0; v = game().vehs[v].next_veh_id_stack) {
                                if (game().vehs[v].faction_id == faction_id && has_abil(game().vehs[v].unit_id, 2)) {
                                    int32_t t = veh_triad(v);
                                    if (t == 2 || map_is_ocean(x2, y2) == (t == 1)) {
                                        seen = 1;  // deep radar (the search goes on)
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            int32_t level = i < 9 ? 1 : (i > 24) + 2;
            bool marked = (map_tile(x2, y2)->landmarks & 0x200000) != 0;
            if (marked ? seen != 0 : seen == 0) {
                if (redraw <= level) redraw = level;
            }
        }
    }

    // Reveal the inner radius (1, or 2 with a sensor).
    const int32_t inner = sensor ? 25 : 9;
    for (int32_t i = 0; i < inner; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!in_map(x2, y2)) continue;
        MAP* sq = map_tile(x2, y2);
        if ((sq->landmarks & 0x80002000) == 0x2000) temples++;
        if (i < 9 || triad == 2 || ((sq->climate & 0xE0) < 0x60) == (triad == 1)) {
            if (marks_discovery(faction_id)) {
                if (!(fbit & sq->visibility)) {
                    sq->landmarks |= 0x400000;
                    GameDrawState |= 1;
                }
                if (((tile_view(sq, faction_id) ^ sq->items) & 0xFFFFFFFC) || !(sq->visibility & fbit)) {
                    int32_t level = (i > 8) + 2;
                    if (redraw <= level) redraw = level;
                }
            }
            if (faction_id) tile_view(sq, faction_id) = sq->items;
            sq->visibility |= fbit;
        }
    }

    // Outer ring: tiles mostly surrounded by seen tiles become seen.
    const int32_t outer = sensor ? 49 : 25;
    for (int32_t i = inner; i < outer; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!in_map(x2, y2)) continue;
        MAP* sq = map_tile(x2, y2);
        if (sq->visibility & fbit) continue;
        int32_t unseen = 0;
        for (int32_t k = 0; k < 8; k++) {
            int32_t x3 = map_wrap_x(NearOffsetX[k] + x2);
            int32_t y3 = NearOffsetY[k] + y2;
            if (in_map(x3, y3) && !(map_tile(x3, y3)->visibility & fbit)) {
                unseen += (NearOffsetX[k] == 0 || NearOffsetY[k] == 0) ? 2 : 1;
            }
        }
        int32_t threshold = (sensor || (sq->climate & 0xE0) < 0x60) ? 6 : 4;
        if (unseen < threshold) {
            if (marks_discovery(faction_id)) {
                sq->landmarks |= 0x400000;
                GameDrawState |= 1;
            }
            if (faction_id) tile_view(sq, faction_id) = sq->items;
            sq->visibility |= fbit;
            int32_t base_id = base_at(x2, y2);
            if (base_id >= 0) spot_base_body(base_id, faction_id, true);
            int32_t level = (sensor != 0) + 2;
            if (redraw <= level) redraw = level;
        }
    }

    // Others in the unit's own stack see it.
    if (veh_id >= 0) {
        for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            int32_t other = game().vehs[v].faction_id;
            if (other == faction_id) continue;
            if (in_map(game().vehs[v].x, game().vehs[v].y)) spot_tile(game().vehs[v].x, game().vehs[v].y, other);
            spot_stack_vehs(v, other);
        }
    }

    // Adjacent tiles: sensors, bases and units.
    for (int32_t i = 0; i < 9; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!in_map(x2, y2)) continue;
        if (is_sensor(x2, y2)) {
            MAP* sq = map_tile(x2, y2);
            int32_t owner = sq->owner;
            if (owner > 0 && owner != faction_id && stealth != 1 &&
                (!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7)) {
                // an enemy sensor sees the unit
                if (veh_id >= 0 && in_map(veh.x, veh.y)) spot_tile(veh.x, veh.y, owner);
                if (veh_id >= 0) spot_stack_vehs(veh_id, owner);
                if (!(game().factions[faction_id].diplo_status[owner] & (DIPLO_PACT | DIPLO_TREATY | DIPLO_TRUCE)) &&
                    flag) {
                    if (triad == 0) {
                        add_goal(owner, 0, 2, x2, y2, -1);
                        add_goal(owner, 2, 1, x2, y2, -1);
                    } else {
                        add_goal(owner, 6, 2, x2, y2, -1);
                    }
                }
            }
        }
        if (i == 0) continue;
        int32_t base_id = base_at(x2, y2);
        if (base_id >= 0) {
            BASE& base = game().bases[base_id];
            uint8_t base_faction = (uint8_t)base.faction_id;
            if (faction_id != base_faction) {
                if (triad != 2 || Chassis[Units[veh.unit_id].chassis_id].range == 0) {
                    if (!at_sea && !stealth) met |= 1u << (base_faction & 31);
                    else if (!(game().factions[faction_id].diplo_status[base_faction] & DIPLO_COMMLINK))
                        met |= 1u << (base_faction & 31);
                }
                if (faction_id == CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) {
                    if (!(fbit & base.visibility)) {
                        if (redraw < 3) redraw = 2;
                    } else if ((uint8_t)base.factions_pop_size_intel[faction_id] != base.pop_size && redraw < 2) {
                        redraw = 1;
                    }
                }
                spot_base_body(base_id, faction_id, true);
                if (!stealth ||
                    !(game().factions[faction_id].diplo_status[(uint8_t)base.faction_id] & DIPLO_COMMLINK)) {
                    // the base sees the unit
                    int32_t owner = (uint8_t)base.faction_id;
                    if (veh_id >= 0 && in_map(veh.x, veh.y)) spot_tile(veh.x, veh.y, owner);
                    if (veh_id >= 0) spot_stack_vehs(veh_id, owner);
                }
                if (triad == 0 &&
                    !(game().factions[(uint8_t)base.faction_id].diplo_status[faction_id] &
                      (DIPLO_PACT | DIPLO_TREATY | DIPLO_TRUCE)) &&
                    flag) {
                    add_goal((uint8_t)base.faction_id, 0, 2, x2, y2, -1);
                    add_goal((uint8_t)base.faction_id, 2, 1, x2, y2, -1);
                }
            }
        }
        const int32_t dest_sea = map_is_ocean(x2, y2);
        if (has_abil(veh.unit_id, 0x40) && !dest_sea) continue;  // submerged units only meet sea units
        int32_t top = stack_fix(veh_at(x2, y2));
        if (top < 0) continue;
        VEH& other = game().vehs[top];
        int32_t other_id = other.faction_id;
        if (other_id == faction_id) continue;
        if (!stealth || has_abil(other.unit_id, 2) ||
            !(game().factions[faction_id].diplo_status[other.faction_id] & DIPLO_COMMLINK)) {
            // they see the unit
            if (base_id < 0) {
                if (veh_id >= 0 && in_map(veh.x, veh.y) && on_map(veh.x, veh.y))
                    spot_tile_in_map(veh.x, veh.y, other_id);
                spot_stack_vehs_top(veh_id, other_id);
            }
            uint32_t status = game().factions[faction_id].diplo_status[other_id];
            if (!(status & 1) && flag) {
                wake_stack(top);
                if (!((1 << (other_id & 31)) & FactionStatus[0])) {
                    if (!(status & 7) && (dest_sea || !at_sea)) veh_alert_wake(top);
                } else if (at_sea == dest_sea && want_to_wake(faction_id, top, veh_id)) {
                    veh_alert_wake(top);
                }
            }
        }
        bool visible;
        if (!has_abil(other.unit_id, 0x40) && !has_abil(other.unit_id, 4) && !(other.flags & VFLAG_INVISIBLE)) {
            MAP* sq = map_tile(other.x, other.y);
            visible = !(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40 || other.unit_id == 19;
        } else {
            visible = false;
        }
        if (visible || sensor || stack_check(top, 1, -1, -1, -1) != 1 ||
            !(game().factions[faction_id].diplo_status[other.faction_id] & DIPLO_COMMLINK)) {
            // the unit sees them
            if (base_id < 0) {
                if (faction_id == CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) &&
                    !(fbit & other.visibility) && redraw < 2)
                    redraw = 1;
                int32_t tx = other.x, ty = other.y;
                if (in_map(tx, ty) && on_map(tx, ty)) spot_tile_in_map(tx, ty, faction_id);
                spot_stack_vehs_top(top, faction_id);
            }
            uint32_t status = game().factions[faction_id].diplo_status[other_id];
            if (!(fbit & FactionStatus[0])) {
                if (!(status & 7) && (at_sea || !dest_sea || triad == 2)) veh_alert_wake(veh_id);
            } else if ((at_sea == dest_sea || triad == 2) && want_to_wake(other_id, veh_id, top)) {
                veh_alert_wake(veh_id);
            }
            if (dest_sea && other_id && !(status & 7)) {
                int32_t submarine = has_abil(other.unit_id, 0x44);
                add_goal(faction_id, 6, submarine ? 3 : 2, x2, y2, -1);
            }
            if (!(status & 7)) add_goal(faction_id, 0, faction_id ? 3 : 1, x2, y2, -1);
            if ((uint8_t)Units[veh.unit_id].plan > 7 && !(status & 1) && whose_territory(other_id, x2, y2, nullptr, 0) == other_id &&
                (uint8_t)Units[other.unit_id].plan > 7) {
                add_goal(other_id, 2, 2, x2, y2, -1);
                add_goal(other_id, 0, 1, x2, y2, -1);
                add_goal(other_id, 3, 1, x2, y2, -1);
            }
            if (triad != 2 || Chassis[Units[veh.unit_id].chassis_id].range == 0) {
                int32_t unit_id = other.unit_id;
                if ((unit_triad(unit_id) != 2 || Chassis[Units[unit_id].chassis_id].range == 0) &&
                    (at_sea == dest_sea || (base_id >= 0 && !at_sea) || !(status & 8))) {
                    if (unit_id == 19) saw_tower = 1;
                    else if (unit_is_native(unit_id)) saw_native = 1;
                    met |= 1u << (other_id & 31);
                }
            }
        }
    }

    // Second ring: sensors, deep radar, sensor-assisted spotting.
    for (int32_t i = 9; i < 25; i++) {
        int32_t x2 = map_wrap_x(TableOffsetX[i] + x);
        int32_t y2 = TableOffsetY[i] + y;
        if (!in_map(x2, y2)) continue;
        if (is_sensor(x2, y2)) {
            MAP* sq = map_tile(x2, y2);
            int32_t owner = sq->owner;
            if (owner > 0 && owner != faction_id && stealth != 1 &&
                (!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7)) {
                // an enemy sensor sees the unit
                if (veh_id >= 0 && in_map(veh.x, veh.y) && on_map(veh.x, veh.y)) spot_tile_in_map(veh.x, veh.y, owner);
                if (veh_id >= 0) spot_stack_vehs(veh_id, owner);
                if (!(game().factions[faction_id].diplo_status[owner] & (DIPLO_PACT | DIPLO_TREATY | DIPLO_TRUCE)) &&
                    flag) {
                    if (triad == 0) {
                        add_goal(owner, 0, 2, x2, y2, -1);
                        add_goal(owner, 2, 1, x2, y2, -1);
                    } else {
                        add_goal(owner, 6, 2, x2, y2, -1);
                    }
                }
            }
        }
        MAP* sq = map_tile(x2, y2);
        int32_t owner =
            ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8) ? sq->val2 & 0xF : -1;
        if (owner == faction_id) continue;
        int32_t top = stack_fix(veh_at(x2, y2));
        int32_t base_id = base_at(x2, y2);
        bool sensor_view = sensor && (triad == 2 || map_is_ocean(x2, y2) == (triad == 1));
        if (sensor_view) {
            if (base_id >= 0) {
                BASE& base = game().bases[base_id];
                if (faction_id == CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) {
                    if (!(base.visibility & fbit)) {
                        if (redraw < 4) redraw = 3;
                    } else if ((uint8_t)base.factions_pop_size_intel[faction_id] != base.pop_size && redraw < 3) {
                        redraw = 2;
                    }
                }
                spot_base_body(base_id, faction_id, false);
            }
        }
        if (sensor_view && top >= 0 && base_id < 0) {
            VEH& other = game().vehs[top];
            if (!has_abil(other.unit_id, 0x40) && !has_abil(other.unit_id, 4) && !(other.flags & VFLAG_INVISIBLE)) {
                MAP* osq = map_tile(other.x, other.y);
                if (!(osq->items & BIT_FUNGUS) || (osq->climate & 0xE0) < 0x40 || other.unit_id == 19) {
                    // BUG(original): tests the visibility of the spotting unit, not of the spotted one
                    if (faction_id == CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) &&
                        !(veh.visibility & fbit) && redraw < 3)
                        redraw = 2;
                    int32_t tx = other.x, ty = other.y;
                    if (in_map(tx, ty) && on_map(tx, ty)) spot_tile_in_map(tx, ty, faction_id);
                    spot_stack_vehs(top, faction_id);
                    if (map_is_ocean(x2, y2) && owner != 0 && !(diplo_status_raw(faction_id, owner) & 2))
                        add_goal(faction_id, 6, 2, x2, y2, -1);
                }
            }
        }
        if (!sensor_view || top >= 0) {
            // their deep radar sees the unit
            if (top >= 0 && !stealth && has_abil(game().vehs[top].unit_id, 2) &&
                (veh_triad(top) == 2 || at_sea == (veh_triad(top) == 1))) {
                if (!(diplo_status_raw(faction_id, owner) & 1) && flag) wake_stack(top);
                MAP* hsq = map_tile(x, y);
                if (!(hsq->items & BIT_BASE_IN_TILE) || (hsq->val2 & 0xF) > 7) {
                    if (veh_id >= 0 && in_map(veh.x, veh.y)) spot_tile_in_map(veh.x, veh.y, owner);
                    if (veh_id >= 0) spot_stack_vehs(veh_id, owner);
                }
                if (!map_is_ocean(x2, y2) && owner != 0 && !(diplo_status_raw(owner, faction_id) & 2))
                    add_goal(owner, 6, 2, x, y, -1);
            }
        }
        if (base_id >= 0) {
            // the base sees the unit
            uint32_t owner_hidden = game().factions[owner].player_flags & PFLAG_MAP_REVEALED;
            uint8_t obit = (uint8_t)(1 << (owner & 31));
            MAP* hsq = map_tile(x, y);
            MAP* bsq = map_tile(x2, y2);
            if ((owner_hidden || (hsq->visibility & obit)) && !stealth &&
                (at_sea != ((bsq->climate & 0xE0) < 0x60) || hsq->region == bsq->region)) {
                if (veh_id >= 0 && in_map(veh.x, veh.y)) {
                    MAP* vsq = map_tile(veh.x, veh.y);
                    if (owner == CurrentPlayerFaction && !(obit & vsq->visibility) &&
                        !(GameState & STATE_OMNISCIENT_VIEW) && !owner_hidden) {
                        vsq->landmarks |= 0x400000;
                        GameDrawState |= 1;
                    }
                    vsq->visibility |= obit;
                    synch_bit(veh.x, veh.y, owner);
                }
                if (veh_id >= 0) spot_stack_vehs(veh_id, owner);
                Faction& f = game().factions[owner];
                if (triad == 0 && hsq->region == bsq->region && x2 == f.target_x && y2 == f.target_y) {
                    f.target_y = -1;
                    f.target_x = -1;
                    for (int32_t v = veh_at(x2, y2); v >= 0; v = game().vehs[v].next_veh_id_stack) {
                        if (game().vehs[v].state & 0x40000) game().vehs[v].state |= 0x60000;
                    }
                }
                if (Units[veh.unit_id].plan == 11 && !(game().factions[faction_id].diplo_status[owner] & DIPLO_PACT)) {
                    add_goal(owner, 0, 2, x, y, -1);
                    add_goal(owner, 2, 1, x, y, -1);
                    add_goal(owner, 3, 1, x, y, -1);
                }
            }
        }
    }

    if (CurrentTurn) {
        draw_radius(x, y, faction_id == CurrentPlayerFaction ? redraw : 0, 2);
        if (faction_id == CurrentPlayerFaction && redraw) GraphicWin_redraw(&WorldWin);
        if (temples > 8 && faction_id == CurrentPlayerFaction && (game().mfactions[faction_id].rule_flags & 0x80))
            interlude(0x29, 0, 1, 0);
        if (met & 1) encounter(faction_id, 0, x, y, saw_tower, saw_native);
        if (faction_id && flag) {
            for (int32_t i = 1; i < 8; i++) {
                if (i != faction_id && (met & (1u << i))) encounter(faction_id, i, x, y, 0, 0);
            }
        }
    }
}

void GAMEAPI stack_put(int32_t veh_id, int32_t x, int32_t y) {
    if (veh_id < 0) return;
    for (int32_t v = stack_top(veh_id); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        veh_drop(veh_lift(v), x, y);
        v = next;
    }
}

void GAMEAPI stack_sort(int32_t veh_id) {
    int32_t moved = -1;
    int32_t x = game().vehs[veh_id].x;
    int32_t y = game().vehs[veh_id].y;
    if (veh_id < 0) return;
    for (int32_t v = stack_top(veh_id); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        if (veh_cargo(v) || has_abil(game().vehs[v].unit_id, 0x80)) {
            veh_drop(veh_lift(v), -3, -3);
            moved = v;
        }
        v = next;
    }
    if (moved < 0) return;
    for (int32_t v = stack_top(moved); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        veh_drop(veh_lift(v), x, y);
        v = next;
    }
}

void GAMEAPI stack_sort_2(int32_t veh_id) {
    int32_t moved = -1;
    int32_t x = game().vehs[veh_id].x;
    int32_t y = game().vehs[veh_id].y;
    if (veh_id >= 0) {
        for (int32_t v = stack_top(veh_id); v >= 0;) {
            int32_t next = game().vehs[v].next_veh_id_stack;
            if (Units[game().vehs[v].unit_id].plan == 8) {  // colony pods
                veh_drop(veh_lift(v), -3, -3);
                moved = v;
            }
            v = next;
        }
    }
    for (int32_t v = veh_at(x, y); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        if ((uint8_t)Units[game().vehs[v].unit_id].plan < 2) {  // combat units
            veh_drop(veh_lift(v), -3, -3);
            moved = v;
        }
        v = next;
    }
    if (moved < 0) return;
    for (int32_t v = stack_top(moved); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        veh_drop(veh_lift(v), x, y);
        v = next;
    }
}

int32_t GAMEAPI stack_fix(int32_t veh_id) {
    if (veh_id < 0 || !MultiplayerActive) return veh_id;
    if (game().vehs[veh_id].next_veh_id_stack < 0 && game().vehs[veh_id].prev_veh_id_stack < 0) return veh_id;
    int32_t x = game().vehs[veh_id].x;
    int32_t y = game().vehs[veh_id].y;
    for (int32_t i = 0; i < game().veh_count; i++) {
        // BUG(original): tests the position of veh_id instead of vehicle i
        if (game().vehs[veh_id].x == x && game().vehs[veh_id].y == y) {
            veh_promote(i);
            stack_sort(veh_id);
        }
    }
    return stack_top(veh_id);
}

int32_t GAMEAPI stack_veh(int32_t veh_id, int32_t mode) {
    if (veh_id < 0) return mode ? veh_id : 0;
    VEH& veh = game().vehs[veh_id];
    if (!veh_cargo(veh_id) && !has_abil(veh.unit_id, 0x80)) {
        if (!mode) return 0;
        return veh_drop(veh_lift(veh_id), -2, -2);
    }
    if (MultiplayerActive) stack_fix(veh_id);
    if (mode == 0) {
        for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack)
            game().vehs[v].state &= 0xFFFFFFFE;
    } else {
        if (mode > 1) {
            // BUG(original): no map bounds check
            MAP* sq = map_tile(veh.x, veh.y);
            if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return veh_drop(veh_lift(veh_id), -2, -2);
        }
        stack_sort(veh_id);
    }
    bool at_sea = false;  // on an ocean tile outside a base
    if (in_map(veh.x, veh.y)) {
        MAP* sq = map_tile(veh.x, veh.y);
        at_sea = (sq->climate & 0xE0) < 0x60 && !((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8);
    }
    const uint32_t faction_id = veh.faction_id;
    const bool is_air = veh_triad(veh_id) == 2;
    int32_t capacity = veh_cargo(veh_id);
    int32_t air_capacity;  // carriers: aircraft
    if (has_abil(veh.unit_id, 0x80)) {
        air_capacity = veh_cargo(veh_id) < 1 ? 1 : veh_cargo(veh_id);
    } else {
        air_capacity = 0;
    }
    if (faction_id == 0) capacity = 99;
    const int32_t passes = (mode <= 1) + 1;
    for (int32_t pass = 0; pass < passes; pass++) {
        int32_t v = veh_id;
        if (mode == 0) v = stack_top(veh_id);
        while (v >= 0) {
            if (!capacity && !air_capacity) break;
            int32_t next = game().vehs[v].next_veh_id_stack;
            VEH& cargo = game().vehs[v];
            bool load = false;
            if (cargo.faction_id == faction_id) {
                bool fits = (capacity && veh_triad(v) == 0 && veh_cargo(v) == 0) ||
                            (air_capacity && veh_triad(v) == 2 && !has_abil(cargo.unit_id, 0x80));
                if (fits) {
                    if (pass == 0) {
                        load = cargo.order == 1 && cargo.waypoint_x[0] == veh_id;  // already aboard
                    } else if (pass == 1) {
                        bool allowed = true;
                        if (is_air) {
                            // BUG(original): no map bounds check
                            MAP* sq = map_tile(veh.x, veh.y);
                            uint32_t owner = sq->val2 & 0xF;
                            if (!((sq->items & BIT_BASE_IN_TILE) && owner < 8 && owner > 0) &&
                                !(sq->items & BIT_AIRBASE))
                                allowed = false;
                        }
                        if (allowed) {
                            uint32_t cargo_faction = cargo.faction_id;
                            if (((1 << (cargo_faction & 31)) & FactionStatus[0]) &&
                                !((veh.state & VSTATE_ON_ALERT) && (cargo.state & 0x200))) {
                                // human player: sentried units, or any unit at sea
                                if (cargo.order == 1) load = cargo.waypoint_x[0] < 0;
                                else load = at_sea;
                            } else if (cargo.state & 0x40000) {
                                if (cargo.state & 0x20000) {
                                    // BUG(original): no map bounds check
                                    load = game()
                                               .factions[cargo_faction]
                                               .region_base_plan[map_tile(cargo.x, cargo.y)->region] == 7;
                                }
                            } else {
                                uint8_t plan = (uint8_t)Units[cargo.unit_id].plan;
                                if (cargo.order == 2 || plan == 12 || plan == 9) {
                                    load = at_sea;
                                } else if (at_sea) {
                                    load = true;
                                } else {
                                    load = !(in_map(cargo.x, cargo.y) &&
                                             game().factions[cargo_faction]
                                                     .region_base_plan[map_tile(cargo.x, cargo.y)->region] == 2 &&
                                             plan < 8);
                                }
                            }
                        }
                    }
                }
            }
            if (load && (mode || !(cargo.state & 1))) {
                sleep(v);
                cargo.waypoint_x[0] = (int16_t)veh_id;
                if (mode) {
                    veh_drop(veh_lift(v), -2, -2);
                    cargo.state &= 0xFFFEFFFF;
                } else {
                    cargo.state |= 1;
                }
                if (air_capacity > 0) air_capacity--;
                if (capacity > 0) capacity--;
            }
            v = next;
        }
    }
    if (mode == 0) return veh_cargo(veh_id) ? capacity : air_capacity;
    return veh_drop(veh_lift(veh_id), -2, -2);
}

void GAMEAPI stack_kill(int32_t veh_id) {
    if (veh_id < 0) return;
    for (int32_t v = stack_top(veh_id); v >= 0;) {
        int32_t next = game().vehs[v].next_veh_id_stack;
        veh_kill(v);
        if (v < next) next--;
        v = next;
    }
}

int32_t GAMEAPI stack_check(int32_t veh_id, int32_t type, int32_t cond1, int32_t cond2, int32_t cond3) {
    int32_t count = 0;
    if (veh_id < 0) return 0;
    for (int32_t v = stack_top(veh_id); v >= 0; v = game().vehs[v].next_veh_id_stack) {
        VEH& veh = game().vehs[v];
        int32_t unit_id = veh.unit_id;
        UNIT& u = Units[unit_id];
        int32_t faction_id = veh.faction_id;
        switch (type) {
        case 0:  // unit_id == cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && unit_id == cond1) count++;
            break;
        case 1:  // units (of faction cond1)
            if (cond1 < 0 || faction_id == cond1) count++;
            break;
        case 2:  // plan == cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && (uint8_t)u.plan == (uint32_t)cond1) count++;
            break;
        case 3:  // triad == cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && unit_triad(unit_id) == cond1) count++;
            break;
        case 4:  // sum of offense values
            if (cond1 < 0 || faction_id == cond1) count += Weapon[u.weapon_id].offense_value;
            break;
        case 5:  // sum of defense values
            if (cond1 < 0 || faction_id == cond1) count += Armor[u.armor_id].defense_value;
            break;
        case 6:  // has ability cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && has_abil(unit_id, (uint32_t)cond1)) count++;
            break;
        case 7:  // sum of costs
            if (cond1 < 0 || faction_id == cond1) count += u.cost;
            break;
        case 8:  // sea transport capacity minus land units
            if (cond1 < 0 || faction_id == cond1) {
                int32_t triad = unit_triad(unit_id);
                if (triad == 0) count--;
                else if (triad == 1) count += veh_cargo(v);
            }
            break;
        case 9:  // order == cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && (int8_t)veh.order == cond1) count++;
            break;
        case 10:  // units of faction cond1
            if (faction_id == cond1) count++;
            break;
        case 11:  // (state & cond1) == cond2 (faction cond3)
            if ((cond3 < 0 || faction_id == cond3) && (veh.state & (uint32_t)cond1) == (uint32_t)cond2) count++;
            break;
        case 12:  // defensive units (armor better than weapon)
            if (cond1 < 0 || faction_id == cond1) {
                int32_t arm = arm_strat(u.armor_id, faction_id);
                int32_t weap = weap_strat(u.weapon_id, faction_id);
                if (weap < arm) count++;
            }
            break;
        case 13:  // missiles
            if ((cond1 < 0 || faction_id == cond1) && Chassis[u.chassis_id].missile) count++;
            break;
        case 14:  // combat or defensive plan
            if ((cond1 < 0 || faction_id == cond1) && (u.plan == 2 || u.plan == 1)) count++;
            break;
        case 15:  // artillery
            if ((cond1 < 0 || faction_id == cond1) && can_arty(unit_id, 1)) count++;
            break;
        case 16:  // combat, defensive or recon plan
            if ((cond1 < 0 || faction_id == cond1) && (u.plan == 2 || u.plan == 3 || u.plan == 1)) count++;
            break;
        case 17:  // group == cond1 (faction cond2)
            if ((cond2 < 0 || faction_id == cond2) && (uint8_t)u.group_id == (uint32_t)cond1) count++;
            break;
        case 18:  // aircraft with range > 1
            if ((cond1 < 0 || faction_id == cond1) && unit_triad(unit_id) == 2 && Chassis[u.chassis_id].range > 1) count++;
            break;
        case 19:  // unarmed
            if ((cond1 < 0 || faction_id == cond1) && Weapon[u.weapon_id].offense_value == 0) count++;
            break;
        default:
            break;
        }
    }
    return count;
}
