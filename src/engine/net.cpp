// NetDaemon and synchronisation (original module around 0x52DBA0-0x538F30).
#include "engine/net.h"

#include "engine/game_state.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"

// NetDaemon_synch(&NetState, type 0xC / 0xD, faction_id, 0, 0, 0, 1, 0x2101); it returns at once
// without multiplayer.
void GAMEAPI synch_alloc(int32_t faction_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 0xC, faction_id, 0, 0, nullptr, 1, 0x2101);
}

void GAMEAPI synch_soc(int32_t faction_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 0xD, faction_id, 0, 0, nullptr, 1, 0x2101);
}

void GAMEAPI synch_research(int32_t faction_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 9, faction_id, 0, 0, nullptr, 1, 0x2101);
}

void GAMEAPI synch_ai(int32_t faction_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 8, faction_id, 0, 0, nullptr, 1, 0x2101);
}

void GAMEAPI synch_proto(int32_t unit_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 0xE, unit_id, 0, 0, nullptr, 1, 0x2101);
}

void GAMEAPI synch_obs(int32_t unit_id) {
    if (MultiplayerActive) NetDaemon_synch(&NetState, 0x10, unit_id, 0, 0, nullptr, 1, 0x2101);
}

// Tiles are visited in storage order: row y holds x = (y & 1), (y & 1) + 2, ...
void GAMEAPI rebuild_vehicle_bits() {
    MAP* sq = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, sq++) {
            uint32_t items = sq->items & ~BIT_VEH_IN_TILE;
            sq->items = items;
            for (int32_t i = 0; i < game().veh_count; i++) {
                if (game().vehs[i].x == x && game().vehs[i].y == y) {
                    sq->items = items | 2;
                    if (!(items & 1)) {  // a base keeps its owner nibble
                        sq->val2 &= 0xF0;
                        sq->val2 = (uint8_t)((game().vehs[i].faction_id & 0xF) | sq->val2);
                    }
                    break;
                }
            }
        }
    }
}

void GAMEAPI rebuild_base_bits() {
    MAP* sq = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, sq++) {
            sq->items &= ~BIT_BASE_IN_TILE;
            for (int32_t i = 0; i < game().base_count; i++) {
                if (game().bases[i].x == x && game().bases[i].y == y) {
                    sq->items |= BIT_BASE_IN_TILE;
                    sq->val2 &= 0xF0;
                    sq->val2 = (uint8_t)((game().bases[i].faction_id & 0xF) | sq->val2);
                    break;
                }
            }
        }
    }
}
