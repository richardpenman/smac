// Game year and zones of control (see zoc.h).
#include "engine/zoc.h"

#include "engine/crt.h"
#include "engine/game_state.h"
#include "engine/map.h"
#include "engine/veh.h"

int32_t GAMEAPI game_year(int32_t turn) { return turn + StartingMissionYear; }

void GAMEAPI say_year(char* buffer) {
    char year[80];
    crt_itoa(StartingMissionYear + CurrentTurn, year, 10);
    crt_strcat(buffer, year);
}

namespace {
// Owner of the units/base (items mask) on an adjacent tile if it is a non-pact faction, else -1.
int32_t zoc_owner(int32_t x, int32_t y, uint32_t items, int32_t faction_id) {
    if (!map_in_bounds(x, y)) return -1;
    MAP* sq = map_tile(x, y);
    if (!(sq->items & items)) return -1;
    uint32_t owner = sq->val2 & 0xF;
    if (owner >= 8 || owner == (uint32_t)faction_id || (game().factions[faction_id].diplo_status[owner] & DIPLO_PACT))
        return -1;
    return (int32_t)owner;
}
}  // namespace

int32_t GAMEAPI zoc_any(int32_t x, int32_t y, int32_t faction_id) {
    for (int32_t i = 0; i < 8; i++) {
        int32_t owner = zoc_owner(map_wrap_x(x + NearOffsetX[i]), y + NearOffsetY[i], 3, faction_id);
        if (owner >= 0) return owner + 1;
    }
    return 0;
}

int32_t GAMEAPI zoc_veh(int32_t x, int32_t y, int32_t faction_id) {
    int32_t zoc = 0;
    for (int32_t i = 0; i < 8; i++) {
        int32_t owner = zoc_owner(map_wrap_x(x + NearOffsetX[i]), y + NearOffsetY[i], 2, faction_id);
        if (owner >= 0 && zoc <= owner + 1) zoc = owner + 1;
    }
    return zoc;
}

int32_t GAMEAPI zoc_sea(int32_t x, int32_t y, int32_t faction_id) {
    bool ocean = map_is_ocean(x, y);
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
        int32_t y2 = y + NearOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        MAP* sq = map_tile(x2, y2);
        if (!(sq->items & BIT_VEH_IN_TILE)) continue;
        uint32_t owner = sq->val2 & 0xF;
        if (owner >= 8 || owner == (uint32_t)faction_id || map_is_ocean(x2, y2) != ocean ||
            (game().factions[faction_id].diplo_status[owner] & DIPLO_PACT))
            continue;
        for (int32_t v = veh_at(x2, y2); v >= 0; v = game().vehs[v].next_veh_id_stack) {
            VEH& veh = game().vehs[v];
            if (veh.faction_id != faction_id && ((veh.visibility & (uint8_t)(1 << (faction_id & 31))) ||
                                                 (!MultiplayerActive && !(veh.flags & VFLAG_INVISIBLE))))
                return (int32_t)owner + 1;
        }
    }
    return 0;
}

int32_t GAMEAPI zoc_move(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return 0;
    return zoc_sea(x, y, faction_id);
}
