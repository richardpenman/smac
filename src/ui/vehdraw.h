// Units on the map: the unit art (load_vehicles 0x454270, load_flags 0x453F70) and veh_draw
// (0x5594F0, misfiled in the diplomacy module): the unit itself, its flag (faction, morale,
// moves left; a silhouette behind it for stacks), its health bar and its order letter.
//
// How the original draws a unit (veh_draw): the sprite index is the chassis (0..8) unless the
// weapon or the unit has an icon (9 + icon). Indexes 9..18 are pre-rendered sprites from
// UNITS.PCX (native life, special units; up to 7 variants by morale). Chassis 0..8 are 3D models
// rendered at draw time by vehdraw_draw: the unit's voxel models (*.cvr, load_voxels) assembled
// by vehdraw_construct_vehicle, lit and projected by the software renderer of gfx2
// (0x617230, 0x639E60..), facing the unit's direction (VEH rotate_angle), tilted with the
// terrain, scaled by the zoom.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Sprite offsets per sprite index (chassis 0..8, special sprites 9..18) as the original keeps
// them in the chassis table (Chassis[i] + 0x4E: 8 arrays of 8 bytes: flag x, flag y, ... by
// variant), set by load_vehicles from marker pixels in UNITS.PCX. The chassis table has 9
// entries; the original writes entries 9..18 past its end.
GLOBAL(int32_t, VehMovingID, 0x689F30);  // unit being animated (-1: none)

extern int8_t VehSpriteOffsets[19][8][8];
constexpr uint32_t kSprUnits = 0x7777A0;  // [19 * 8] UNITS.PCX unit sprites [index * 8 + variant]
constexpr uint32_t kSprFlags = 0x78B778;  // [8 * 14] flags [faction * 14 + morale * 2 + moved]

ADDR_NATIVE(0x454270) void GAMEAPI load_vehicles();
// load_flags (engine/ui_bridge.h; replaces the headless "art" area in smacx).
void GAMEAPI load_flags();
// The unit of a stack that is drawn: the selected unit, else the one with the highest
// triad + 4 (own units) value, bottom of the stack first.
ADDR_NATIVE(0x5593E0) int32_t GAMEAPI pick_top_veh(int32_t veh_id);
// Draws unit veh_id at (px, py) (the tile's top-left, lifted half a tile). mode bits: 1 the top
// unit of its stack, 2 a stack silhouette behind the flag, 8/0x100 the small-zoom box, 0x10/0x20
// animations, 0x40/0x400 no flag, 0x80 no model, 0x200 box frame, 0x800 flag only, 0x1000 no
// flag offset. Returns whether a 3D model was drawn.
ADDR_NATIVE(0x5594F0) int32_t GAMEAPI veh_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t zoom,
                                              int32_t mode, int32_t a7);
// The 3D model of a unit (chassis 0..8) with its centre at (px + zoom unit, py + zoom unit).
// Not reconstructed yet: nothing is drawn (see vehdraw.cpp).
ADDR_NATIVE(0x4C12A0) void GAMEAPI vehdraw_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t mode,
                                               int32_t zoom, int32_t a7);
