// NetDaemon and synchronisation (original module around 0x52DBA0-0x538F30).
#include "engine/net.h"

#include "engine/map.h"

// Tiles are visited in storage order: row y holds x = (y & 1), (y & 1) + 2, ...
void GAMEAPI rebuild_vehicle_bits() {
    MAP* sq = MapTiles;
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2, sq++) {
            uint32_t items = sq->items & ~2u;
            sq->items = items;
            for (int32_t i = 0; i < VehCount; i++) {
                if (Vehs[i].x == x && Vehs[i].y == y) {
                    sq->items = items | 2;
                    if (!(items & 1)) {  // a base keeps its owner nibble
                        sq->val2 &= 0xF0;
                        sq->val2 = (uint8_t)((Vehs[i].faction_id & 0xF) | sq->val2);
                    }
                    break;
                }
            }
        }
    }
}

void GAMEAPI rebuild_base_bits() {
    MAP* sq = MapTiles;
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2, sq++) {
            sq->items &= ~1u;
            for (int32_t i = 0; i < BaseCount; i++) {
                if (Bases[i].x == x && Bases[i].y == y) {
                    sq->items |= 1;
                    sq->val2 &= 0xF0;
                    sq->val2 = (uint8_t)((Bases[i].faction_id & 0xF) | sq->val2);
                    break;
                }
            }
        }
    }
}
