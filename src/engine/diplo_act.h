// Intervention, base capture and coast helpers (original module around 0x50B8A0-0x50E890).
#pragma once
#include "engine/globals.h"

// Splits a bit number into a byte offset and a mask within that byte.
ADDR(0x50BA00) void GAMEAPI bitmask(int32_t bit, int32_t* offset, int32_t* mask);
// The pact partners of faction_id_with that have no treaty or vendetta with faction_id declare
// vendetta on faction_id (double_cross).
ADDR(0x50BA50) void GAMEAPI intervention(int32_t faction_id, int32_t faction_id_with);
// Number of set bits.
ADDR(0x50BA30) int32_t GAMEAPI bit_count(uint32_t value);
// Number of sea regions bordering a land region (coast helpers).
ADDR(0x50DE00) int32_t GAMEAPI sea_coasts(int32_t region);
// Whether a supply crawler can convoy resources from the base's area (0x50E5C0).
ADDR(0x50E5C0) int32_t GAMEAPI convoy(int32_t veh_id, int32_t base_id);
// Sea region a base borders (0 none, 0x3F several), with flags in the upper bits.
ADDR(0x50DF30) int32_t GAMEAPI base_coast(int32_t base_id);
// Whether a base may build transports / naval combat units (sea access).
ADDR(0x50E310) int32_t GAMEAPI transport_base(int32_t base_id);
ADDR(0x50E3C0) int32_t GAMEAPI naval_base(int32_t base_id);
// Waits (message pump) for the given number of milliseconds.
ADDR(0x50DD90) void GAMEAPI clock_wait(int32_t msec);
// Whether sea region region_sea borders land region region_land (coast table).
ADDR(0x50DDC0) int32_t GAMEAPI sea_coast(int32_t region_land, int32_t region_sea);
// Whether a port base can reach the coast of land region region by sea.
ADDR(0x50E030) int32_t GAMEAPI port_to_coast(int32_t base_id, int32_t region);
