// Entries of the game-state tables at any index, as the original reads them: it indexes Bases,
// Vehs, Factions, Units and MFactions without range checks (compat out_of_range_reads), and an
// index outside the table reads the original's memory there (Bases[-1] is the end of Factions[7],
// Factions[-1] lies in Vehs, Vehs[-1] in the Reactor table). Inside the table this is the entry;
// outside it, the bytes at the original's address (mem::host: the record or the image holding
// it), so the read lands where the original's would although the tables are separate native
// objects (engine/state.h).
#pragma once
#include <cstddef>
#include <cstdint>

#include "engine/memmap.h"

// The original's addresses of the tables.
constexpr uint32_t kVehsAddr = 0x952828;
constexpr uint32_t kFactionsAddr = 0x96C9E0;
constexpr uint32_t kBasesAddr = 0x97D040;
constexpr uint32_t kUnitsAddr = 0x9AB868;
constexpr uint32_t kMFactionsAddr = 0x946A50;

// Entry id of table (at the original's address table_addr). Out of range: the bytes the original
// reads there, which must lie in one record or in the image (true of every case known).
template <class T, size_t N>
T& table_at(T (&table)[N], uint32_t table_addr, int32_t id) {
    if (static_cast<uint32_t>(id) < N) return table[id];
    return *reinterpret_cast<T*>(mem::host(table_addr + static_cast<uint32_t>(id) * static_cast<uint32_t>(sizeof(T))));
}
// The bytes of entry id (any index), as a byte pointer.
template <class T, size_t N>
uint8_t* table_bytes(T (&table)[N], uint32_t table_addr, int32_t id) {
    return reinterpret_cast<uint8_t*>(&table_at(table, table_addr, id));
}
