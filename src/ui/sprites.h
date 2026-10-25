// Map art loaded at startup (load_sprites 0x455EB0 -> load_terrain, load_vehicles, ...) and per
// faction (load_faction_art). Native objects at the original's addresses (noted per array).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Transparent index of the texture/sprite blitters (DAT_009BB548; raw 255 of the art + 10).
constexpr int32_t kArtKey = 9;

// ---- terrain textures (TEXTURE.PCX, 56x56 cells) ----
extern Texture TexRocky[4];      // 0x76E9E0: rocky overlays, [rand + 2 * (rocky - 1)]
extern Texture TexWater[2];      // 0x7A7820: shallow, deep water
extern Texture TexArid;          // 0x799E48: arid land
extern Texture TexMoist[16];     // 0x799EB8: moist land, transition variants
extern Texture TexRainy[16];     // 0x799738: rainy land, transition variants
extern Texture TexFarm[9];       // 0x799238: farmland under soil enrichers etc.
extern Texture TexForest[16];   // 0x78A758: forest, by diagonal forest neighbours (4-bit mask)
extern Texture TexJungle[15];   // 0x789C28: Monsoon Jungle landmark, transition variants
extern Texture TexDune;          // 0x7AC220: dunes landmark
extern Texture TexFungus[30];    // 0x776A80: xenofungus transition variants, land (0..14) and sea (15..29)

// ---- terrain improvement sprites (TER1.PCX, 100x62 cells) ----
extern Sprite SprMine, SprMineSea;          // 0x776A50, 0x789B68 (mining platform)
extern Sprite SprSolar, SprSolarSea;        // 0x7991F8, 0x78A5B0 (tidal harness)
extern Sprite SprKelp;                      // 0x7A6860: kelp farm
extern Sprite SprBunker, SprAirbase, SprSensor;  // 0x7AD010, 0x7ACC60, 0x7A6830
extern Sprite SprCondenser, SprMirror;      // 0x779390, 0x7ABF60
extern Sprite SprBorehole, SprBoreholeGeo;  // 0x78DD20, 0x7ACB88 (geothermal landmark)
extern Sprite SprMonolith;                  // 0x75B098
extern Sprite SprFarm[4];                   // 0x791FC8: by nutrients (1..4+)
extern Sprite SprEnricher[4];               // 0x79A710: farm with soil enricher

// ---- base sprites (faction PCX, 100x75 cells) ----
// [faction][(water * 4 + size) * 3 + defense]; 37 sprites per faction in the original (0x78E978,
// stride 0x65C), of which the first 24 are the base images.
extern Sprite BaseSprites[8][37];
// Faction colours read from the faction PCX (load_faction_art): base box fill, frame, text, shadow.
GLOBAL(int32_t[8], FactionColorFill, 0x68F9F0);
GLOBAL(int32_t[8], FactionColorFrame, 0x68FA10);
GLOBAL(int32_t[8], FactionColorText, 0x68FA30);
GLOBAL(int32_t[8], FactionColorShadow, 0x68FA50);

// Loads the terrain textures (partial reconstruction of 0x451C50: the textures the map slice
// draws; improvement and landmark sprites are not loaded yet).
ADDR_NATIVE(0x451C50) void GAMEAPI load_terrain();

namespace native_ui {
// load_faction_art (0x453710; declared in engine/ui_bridge.h, headless no-op): faction colours
// and base sprites from <faction>.pcx (faction.pcx when missing). Portraits and logos are not
// loaded yet.
void GAMEAPI load_faction_art(int32_t faction_id);
}  // namespace native_ui
