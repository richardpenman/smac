// StatusWin (ui 0x4B4990..0x4BA1A0): the console's left panes, the selected unit (or the units of
// the tile under the cursor) and the tile under the cursor, with the unit stack row below; and the
// unit text helpers of the ui module it uses (say_orders).
//
// The StatusWin object of the original is InfoWin (0x8C5568, engine/ui_bridge.h); its integer
// fields are used in place. Drawing goes into the main interface's canvas (console_frame.cpp).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Appends the description of a unit's orders (automation, go-to targets, convoy, patrol
// waypoints) to buffer.
ADDR_NATIVE(0x4B43E0) void GAMEAPI say_orders(char* buffer, int32_t veh_id);
// say_orders into StrBuffer.
ADDR_NATIVE(0x4B4970) void GAMEAPI say_orders_2(int32_t veh_id);
