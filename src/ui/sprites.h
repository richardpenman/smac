// Map art loaded at startup (load_sprites 0x455EB0 -> load_terrain, load_vehicles, ...) and per
// faction (load_faction_art).
//
// The original keeps its art in global Texture (0x70 bytes) and Sprite (0x2C bytes) objects in
// the data segment and indexes them by address arithmetic (`base + i * 0x70`). Their native
// counterparts are kept by original address: ArtTexture(addr) / ArtSprite(addr), with the array
// bases named below, so the drawing code reads like the original.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Transparent index of the texture/sprite blitters (DAT_009BB548; raw 255 of the art + 10).
constexpr int32_t kArtKey = 9;

// The texture / sprite object of the original at `addr` (created empty on first use).
Texture* ArtTexture(uint32_t addr);
Sprite* ArtSprite(uint32_t addr);
inline Texture* ArtTexture(uint32_t base, int32_t index) { return ArtTexture(base + (uint32_t)index * 0x70); }
inline Sprite* ArtSprite(uint32_t base, int32_t index) { return ArtSprite(base + (uint32_t)index * 0x2C); }

// ---- terrain textures (TEXTURE.PCX, 56x56 cells; load_terrain) ----
constexpr uint32_t kTexRocky = 0x76E9E0;  // [4]: rocky overlays, [rand + 2 * rocky - 2]
constexpr uint32_t kTexWater = 0x7A7820;  // [2]: shallow, deep water
constexpr uint32_t kTexArid = 0x799E48;  // arid land
constexpr uint32_t kTexMoist = 0x799EB8;  // [16]: moist land, by rain transition
constexpr uint32_t kTexRainy = 0x799738;  // [16]: rainy land, by rain transition
constexpr uint32_t kTexJungle = 0x789C28;  // [15]: Monsoon Jungle
constexpr uint32_t kTexDune = 0x7AC220;  // land texture of landmark bit 0x40 (dunes)
constexpr uint32_t kTexMesa = 0x7797F8;  // [8]: mesa overlays (landmark 0x100)
constexpr uint32_t kTexRoad = 0x792218;  // [9]: road centre, then the 8 directions
constexpr uint32_t kTexMagTube = 0x798E08;  // [9]: mag tube centre, then the 8 directions
constexpr uint32_t kTexRiver = 0x7A7AA0;  // [16]: rivers by diagonal river neighbours
constexpr uint32_t kTexVolcano = 0x78A340;  // [3]: volcano overlays (landmark 2)
constexpr uint32_t kTexCrater = 0x788100;  // [3]: crater overlays (landmark 1)
constexpr uint32_t kTexFungus = 0x776A80;  // [15]: xenofungus by fungus transition
constexpr uint32_t kTexSeaFungus = 0x777110;  // [15]: sea fungus
constexpr uint32_t kTexFarm = 0x799238;  // [9]: farmland [3 * rand + rain]
constexpr uint32_t kTexForest = 0x78A758;  // [16]: forest by diagonal forest neighbours
constexpr uint32_t kTexCoastShape = 0x76E8A0;  // [2]: coast shapes (RAINFALL.PCX through 0x6846F8)
constexpr uint32_t kTexBorder = 0x78DD80;  // territory border directions (0..8 per texel)
// Colours sampled from RAINFALL.PCX (load_terrain): 4 x 3 at (0x82 + 14i, 0x158 + 17j).
GLOBAL(uint8_t[12], CoastColors, 0x787E54);

// ---- terrain sprites (TER1.PCX etc., 100x62 cells) ----
constexpr uint32_t kSprMine = 0x776A50, kSprMineSea = 0x789B68;  // mine, mining platform
constexpr uint32_t kSprSolar = 0x7991F8, kSprSolarSea = 0x78A5B0;  // solar, tidal harness
constexpr uint32_t kSprKelp = 0x7A6860;  // kelp farm
constexpr uint32_t kSprBunker = 0x7AD010, kSprAirbase = 0x7ACC60, kSprSensor = 0x7A6830;
constexpr uint32_t kSprCondenser = 0x779390, kSprMirror = 0x7ABF60;
constexpr uint32_t kSprBorehole = 0x78DD20, kSprBoreholeGeo = 0x7ACB88;
constexpr uint32_t kSprMonolith = 0x75B098;
constexpr uint32_t kSprFarm = 0x791FC8;  // [4] farms by nutrients
constexpr uint32_t kSprEnricher = 0x79A710;  // [4] farms with soil enricher
constexpr uint32_t kSprSpecial = 0x78A700;  // [2] land / sea bonus 4 (landmarks 8, 0x400)
constexpr uint32_t kSprBonus = 0x75B230;  // [12] resource icons [((bonus * 2 + sea) * 2 - 4) + parity]
constexpr uint32_t kSprPod = 0x77AFF8;  // [6] supply pods [sea + 2 * ((7x + 5y) % 3)]
constexpr uint32_t kSprLandmark2000 = 0x7AC098;  // landmark 0x2000 sprites
constexpr uint32_t kSprLandmark4000 = 0x776758;  // expansion landmarks 0x4000
constexpr uint32_t kSprLandmark8000 = 0x7ACD70;  // expansion landmarks 0x8000
constexpr uint32_t kSprElevation = 0x791C58;  // [6] map option 8
constexpr uint32_t kSprRainfall = 0x7A99A0;  // [9] map option 0x10

// ---- base sprites (faction PCX, 100x75 cells) ----
// [faction][(water * 4 + size) * 3 + defense]; 37 sprites per faction in the original (0x78E978,
// stride 0x65C), of which the first 24 are the base images.
extern Sprite BaseSprites[8][37];
// Faction colours read from the faction PCX (load_faction_art): base box fill, frame, text, shadow.
GLOBAL(int32_t[8], FactionColorFill, 0x68F9F0);
GLOBAL(int32_t[8], FactionColorFrame, 0x68FA10);
GLOBAL(int32_t[8], FactionColorText, 0x68FA30);
GLOBAL(int32_t[8], FactionColorShadow, 0x68FA50);

// Loads the terrain textures and sprites (TEXTURE.PCX, RAINFALL.PCX, glow.pcx, TER1WRECK.PCX,
// TER1.PCX).
ADDR_NATIVE(0x451C50) void GAMEAPI load_terrain();

// Palette cycles and the ocean depth -> light table (OceanDepthLight) from system.txt
// (#CYCLING, #OCEANOFFSETS). Palette cycling itself is not reconstructed (the shipped
// system.txt defines no cycles).
ADDR_NATIVE(0x455400) void GAMEAPI load_cycling();

// load_faction_art (0x453710; declared in engine/ui_bridge.h; replaces the headless "art" area in
// smacx): faction colours and base sprites from <faction>.pcx (faction.pcx when missing).
// Portraits and logos are not loaded yet.
void GAMEAPI load_faction_art(int32_t faction_id);
