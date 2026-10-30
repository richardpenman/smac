/* Hash of the compared game state: FNV-1a 64 over the regions of state_regions.h (the bytes a
 * .state snapshot holds), masked bytes (MASK: heap pointers) read as 0. Built into hybrid.dll
 * (fcompare's golden records) and opensmac_headless (their replay, --golden), so both sides can tell
 * that a record is replayed on the state it was made on. The hybrid build reads the regions from
 * the original's memory (needs engine/globals.h and engine/memmap.h); native builds hash the
 * serialised records (engine/state_io.h), the bytes the snapshots hold. */
#pragma once
#include <cstdint>
#ifndef OPENSMAC_HYBRID
#include "engine/state_io.h"
#endif

inline uint64_t state_hash() {
    struct Mask {
        uint32_t addr, size;
    } masks[16];
    int nmask = 0;
#define REGION(name, addr, size)
#define REGION_VAR(name, addr, count_addr, elem)
#define MASK(addr, size) \
    if (nmask < 16) masks[nmask++] = Mask{addr, size};
#include "state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
    uint64_t h = 0xCBF29CE484222325ull;
    auto mix = [&](uint32_t addr, const uint8_t* p, uint32_t size) {
        for (int k = 0; k < 4; k++) h = (h ^ ((addr >> (8 * k)) & 0xFF)) * 0x100000001B3ull;
        for (int k = 0; k < 4; k++) h = (h ^ ((size >> (8 * k)) & 0xFF)) * 0x100000001B3ull;
        for (uint32_t i = 0; i < size; i++) {
            uint8_t b = p[i];
            for (int m = 0; m < nmask; m++)
                if (addr + i >= masks[m].addr && addr + i < masks[m].addr + masks[m].size) b = 0;
            h = (h ^ b) * 0x100000001B3ull;
        }
    };
#ifdef OPENSMAC_HYBRID  // the original's memory
#define REGION(name, addr, size) mix(addr, mem::host(addr), size);
#define REGION_VAR(name, addr, count_addr, elem)                                                     \
    {                                                                                                \
        uint32_t n = (uint32_t)(*reinterpret_cast<int32_t*>(mem::host(count_addr))) * (elem);        \
        const uint8_t* src = (addr) == 0x10000000   ? reinterpret_cast<const uint8_t*>(MapTiles)     \
                             : (addr) == 0x20000000 ? reinterpret_cast<const uint8_t*>(MapAbstract) \
                                                    : mem::host(addr);                               \
        mix(addr, src, src ? n : 0);                                                                 \
    }
#define MASK(addr, size)
#include "state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
#else  // the records serialised (engine/state_io.h), as the .state snapshots hold them
    state_regions_write(mix);
#endif
    return h;
}
