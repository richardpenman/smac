// The multiplayer locks (game module 0x58FD70..0x590500): the Lock object (NetLock, NetState
// +0x14A0) holds per faction (1..7, 0x1C bytes each) a PlayerLock: an active byte and two
// SquareLocks {x, y, flags} (flags 1: held; 4: with the surrounding tiles, 8: radius 4 (81 tiles)
// else 2 (25), 0x10: the centre only), the tiles' lock owner kept in the map (lock_map, bits 3..5 of
// val3); +0xE0 the faction holding the global (diplomatic) lock, +0xE4 the lock pending, +0xE8.
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/game_state.h"
#include "engine/turn.h"

namespace {
int32_t& fld(uint8_t* p, uint32_t off) { return *reinterpret_cast<int32_t*>(p + off); }
uint8_t* square(uint8_t* lock, int32_t faction, int32_t i) { return lock + faction * 0x1C + 4 + i * 0xC; }
// The tiles of a square lock (TableOffset order).
int32_t square_tiles(uint32_t flags) {
    if (!(flags & 4) || (flags & 0x10)) return 1;
    return (flags & 8) ? 0x51 : 0x19;
}
bool any_square(uint8_t* lock, int32_t faction) {
    for (int32_t i = 0; i < 2; i++) {
        if (fld(square(lock, faction, i), 8) & 1) return true;
    }
    return false;
}
}  // namespace

void THISCALL SquareLock_clear(uint8_t* sq) {
    fld(sq, 8) = 0;
    fld(sq, 4) = -1;
    fld(sq, 0) = -1;
}

void THISCALL SquareLock_unlock(uint8_t* sq, int32_t faction_id) {
    uint32_t flags = (uint32_t)fld(sq, 8);
    if (!(flags & 1)) return;
    int32_t x = fld(sq, 0), y = fld(sq, 4);
    if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
        int32_t n = square_tiles(flags);
        for (int32_t i = 0; i < n; i++) {
            int32_t tx = TableOffsetX[i] + x;
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            int32_t ty = TableOffsetY[i] + y;
            if (ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX) unlock_map(tx, ty, faction_id);
        }
    }
    fld(sq, 8) = 0;
    fld(sq, 4) = -1;
    fld(sq, 0) = -1;
}

int32_t THISCALL SquareLock_lock(uint8_t* sq, int32_t faction_id, uint32_t flags, int32_t x, int32_t y) {
    fld(sq, 8) = (int32_t)(flags | 1);
    fld(sq, 0) = x;
    fld(sq, 4) = y;
    if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
        int32_t n = square_tiles(flags);
        for (int32_t i = 0; i < n; i++) {
            int32_t tx = TableOffsetX[i] + x;
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            int32_t ty = TableOffsetY[i] + y;
            if (ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX && lock_map(tx, ty, faction_id)) return 1;
        }
    }
    return 0;
}

void THISCALL PlayerLock_clear(uint8_t* pl) {
    for (int32_t i = 0; i < 2; i++) SquareLock_clear(pl + 4 + i * 0xC);
    pl[0] = 0;
}

int32_t THISCALL PlayerLock_active(uint8_t* pl) {
    for (int32_t i = 0; i < 2; i++) {
        if (fld(pl + 4 + i * 0xC, 8) & 1) return 1;
    }
    return 0;
}

void THISCALL PlayerLock_unlock(uint8_t* pl, int32_t faction_id) {
    for (int32_t i = 0; i < 2; i++) SquareLock_unlock(pl + 4 + i * 0xC, faction_id);
    pl[0] = 0;
}

int32_t THISCALL PlayerLock_lock(uint8_t* pl, int32_t faction_id, uint32_t flags1, int32_t x1, int32_t y1,
                                 uint32_t flags2, int32_t x2, int32_t y2) {
    PlayerLock_clear(pl);
    if (!SquareLock_lock(pl + 4, faction_id, flags1, x1, y1) && !SquareLock_lock(pl + 0x10, faction_id, flags2, x2, y2))
        return 0;
    PlayerLock_unlock(pl, faction_id);
    return 1;
}

void THISCALL PlayerLock_add_lock(uint8_t* pl, int32_t faction_id, uint32_t flags, int32_t x, int32_t y) {
    SquareLock_lock(pl + 0x10, faction_id, flags | 0x10, x, y);
}

void THISCALL Lock_reset_map(uint8_t*) {
    for (int32_t i = 0; i < MapAreaTiles; i++) game().map.tiles[i].val3 &= 0xC7;
}

void THISCALL Lock_clear(uint8_t* lock) {
    fld(lock, 0xE0) = 0;
    fld(lock, 0xE4) = 0;
    fld(lock, 0xE8) = 0;
    for (int32_t f = 0; f < 8; f++) PlayerLock_clear(lock + f * 0x1C);
    Lock_reset_map(lock);
}

void THISCALL Lock_unlock(uint8_t* lock, int32_t faction_id) {
    if (fld(lock, 0xE0) == faction_id) {
        fld(lock, 0xE0) = 0;
        fld(lock, 0xE4) = 0;
    }
    PlayerLock_unlock(lock + faction_id * 0x1C, faction_id);
}

// The global lock granted (message 0x1205) once no other faction holds a square lock.
void THISCALL Lock_check_global(uint8_t* lock) {
    if (!current_server() || !fld(lock, 0xE4)) return;
    for (int32_t f = 1; f < 8; f++) {
        if (f != fld(lock, 0xE0) && any_square(lock, f)) return;
    }
    fld(lock, 0xE4) = 0;
    message_data(0x1205, fld(lock, 0xE0), 0, 0, 0, 0);
}

int32_t THISCALL Lock_check_global_2(uint8_t* lock, int32_t faction_id) {
    if (!current_server()) return 0;
    if (!fld(lock, 0xE4) || faction_id != fld(lock, 0xE0)) return 0;
    for (int32_t f = 1; f < 8; f++) {
        if (f != fld(lock, 0xE0) && any_square(lock, f)) return 0;
    }
    fld(lock, 0xE4) = 0;
    return 1;
}

int32_t THISCALL Lock_global_lock(uint8_t* lock, int32_t faction_id) {
    if (fld(lock, 0xE0) && fld(lock, 0xE0) != faction_id) return 1;
    fld(lock, 0xE0) = faction_id;
    fld(lock, 0xE4) = 1;
    return 0;
}

int32_t THISCALL Lock_lock(uint8_t* lock, int32_t faction_id, uint32_t flags1, int32_t x1, int32_t y1, uint32_t flags2,
                           int32_t x2, int32_t y2) {
    int32_t owner = fld(lock, 0xE0);
    if (owner && owner != faction_id) return 1;
    if (flags1 & 2) {
        fld(lock, 0xE0) = faction_id;
        fld(lock, 0xE4) = 1;
    }
    uint8_t* pl = lock + faction_id * 0x1C;
    PlayerLock_clear(pl);
    if (!SquareLock_lock(pl + 4, faction_id, flags1, x1, y1) && !SquareLock_lock(pl + 0x10, faction_id, flags2, x2, y2)) {
        if ((flags1 & 2) && current_server() && fld(lock, 0xE4)) {
            for (int32_t f = 1; f < 8; f++) {
                if (f != fld(lock, 0xE0) && any_square(lock, f)) return 0;
            }
            fld(lock, 0xE4) = 0;
            message_data(0x1205, fld(lock, 0xE0), 0, 0, 0, 0);
        }
        return 0;
    }
    PlayerLock_unlock(pl, faction_id);
    if (fld(lock, 0xE0) == faction_id) {
        fld(lock, 0xE4) = 0;
        fld(lock, 0xE0) = 0;
    }
    return 1;
}

void THISCALL Lock_add_lock(uint8_t* lock, int32_t faction_id, uint32_t flags, int32_t x, int32_t y) {
    SquareLock_lock(lock + faction_id * 0x1C + 0x10, faction_id, flags | 0x10, x, y);
}

int32_t THISCALL Lock_any_locks(uint8_t* lock) {
    if (fld(lock, 0xE0)) return 1;
    for (int32_t f = 1; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if ((FactionStatus[0] & bit) && (FactionStatus[1] & bit) && any_square(lock, f)) return 1;
    }
    return 0;
}
