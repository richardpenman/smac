// Map art loading (see sprites.h).
#include "ui/sprites.h"

#include <cstring>
#include <string>

#include "engine/globals.h"

Texture TexRocky[4];
Texture TexWater[2];
Texture TexArid;
Texture TexMoist[16];
Texture TexRainy[16];
Texture TexFarm[9];
Texture TexDune;
Texture TexJungle[15];
Texture TexForest[16];
Texture TexFungus[30];
Sprite BaseSprites[8][37];
Sprite SprMine, SprMineSea, SprSolar, SprSolarSea, SprKelp, SprBunker, SprAirbase, SprSensor;
Sprite SprCondenser, SprMirror, SprBorehole, SprBoreholeGeo, SprMonolith;
Sprite SprFarm[4], SprEnricher[4];

// The loading canvas (poLoadingCanvas, 0x798668).
static Buffer LoadingCanvas;

void GAMEAPI load_terrain() {
    TextureKey = kArtKey;
    Buffer_load_pcx(&LoadingCanvas, "TEXTURE.PCX", nullptr, 10, 0xEC);
    // Rocky overlays: two per row pass, the second of a pair two slots further.
    for (int32_t pass = 0, x = 1; pass < 2; pass++) {
        for (int32_t k = 0; k < 2; k++, x += 0x39) pfncImageFromCanvas(&TexRocky[pass + 2 * k], &LoadingCanvas, x, 1, 0x38, 0x38, 0);
    }
    for (int32_t i = 0, y = 0x4F; i < 2; i++, y += 0x39) pfncImageFromCanvas(&TexWater[i], &LoadingCanvas, 0x118, y, 0x38, 0x38, 0);
    pfncImageFromCanvas(&TexArid, &LoadingCanvas, 1, 0x3A, 0x38, 0x38, 0);
    for (int32_t i = 0; i < 16; i++)
        pfncImageFromCanvas(&TexMoist[i], &LoadingCanvas, 1 + (i & 3) * 0x39, 0x73 + (i >> 2) * 0x39, 0x38, 0x38, 0);
    for (int32_t i = 0; i < 16; i++)
        pfncImageFromCanvas(&TexRainy[i], &LoadingCanvas, 1 + (i & 3) * 0x39, 0x157 + (i >> 2) * 0x39, 0x38, 0x38, 0);
    for (int32_t i = 0; i < 9; i++)
        pfncImageFromCanvas(&TexFarm[i], &LoadingCanvas, 0x307 + (i % 3) * 0x39, 0xDB + (i / 3) * 0x39, 0x38, 0x38, 0);
    for (int32_t r = 0, i = 0; r < 4; r++)
        for (int32_t c = 0; c < 4 && !(r == 3 && c == 3); c++, i++)
            pfncImageFromCanvas(&TexJungle[i], &LoadingCanvas, 0x20E + c * 0x39, 0x103 + r * 0x39, 0x38, 0x38, 0);
    pfncImageFromCanvas(&TexDune, &LoadingCanvas, 0xE5, 1, 0x38, 0x38, 0);
    for (int32_t i = 0; i < 16; i++)
        pfncImageFromCanvas(&TexForest[i], &LoadingCanvas, 0x20E + (i & 3) * 0x39, 6 + (i >> 2) * 0x39, 0x38, 0x38, 0);
    // Xenofungus: 4x4 grids minus the last cell, land at (0x118, 0x204), sea at (0x1FC, 0x204).
    for (int32_t r = 0, i = 0; r < 4; r++) {
        for (int32_t c = 0; c < 4; c++) {
            if (r == 3 && c == 3) break;
            pfncImageFromCanvas(&TexFungus[i], &LoadingCanvas, 0x118 + c * 0x39, 0x204 + r * 0x39, 0x38, 0x38, 0);
            pfncImageFromCanvas(&TexFungus[15 + i], &LoadingCanvas, 0x1FC + c * 0x39, 0x204 + r * 0x39, 0x38, 0x38, 0);
            i++;
        }
    }
    // TER1.PCX: the purple tile diamond of each cell (colours 6, 7) becomes transparent.
    Buffer_load_pcx(&LoadingCanvas, "TER1.PCX", nullptr, 10, 0xEC);
    auto cell = [](Sprite* s, int32_t x, int32_t y) {
        Buffer_change_color(&LoadingCanvas, 0x107, 0x109);
        Buffer_change_color(&LoadingCanvas, 0x106, 0x109);
        pfncSpriteFromCanvasRectTrans(s, &LoadingCanvas, 0x109 & 0xFF, x, y, 100, 0x3E, 0);
    };
    cell(&SprMine, 0x25F, 0x40);
    cell(&SprMineSea, 0x1FA, 0x40);
    cell(&SprSolar, 0x25F, 0x7F);
    cell(&SprSolarSea, 0x1FA, 0x7F);
    cell(&SprKelp, 0x25F, 0xBE);
    cell(&SprBunker, 0x1FA, 0x13C);
    cell(&SprAirbase, 0x25F, 0x13C);
    cell(&SprSensor, 0x2C4, 0x13C);
    for (int32_t k = 0, y = 0x1C5; k < 4; k++, y += 0x3F) {
        cell(&SprEnricher[k], 0x336, y);
        cell(&SprFarm[k], 0x39B, y);
    }
    cell(&SprCondenser, 0x1FA, 0xFD);
    cell(&SprMirror, 0x25F, 0xFD);
    cell(&SprBorehole, 0x2C4, 0xFD);
    cell(&SprBoreholeGeo, 0x2C4, 0xBE);
    cell(&SprMonolith, 0x130, 1);
}

namespace native_ui {

void GAMEAPI load_faction_art(int32_t faction_id) {
    std::string file = std::string(MFactions[faction_id].filename) + ".pcx";
    if (Buffer_load_pcx(&LoadingCanvas, file.c_str(), nullptr, 10, 0xEC))
        Buffer_load_pcx(&LoadingCanvas, "faction.pcx", nullptr, 10, 0xEC);
    FactionColorFill[faction_id] = Buffer_get_pixel(&LoadingCanvas, 4, 0x2E3);
    FactionColorFrame[faction_id] = Buffer_get_pixel(&LoadingCanvas, 4, 0x2EB);
    FactionColorText[faction_id] = Buffer_get_pixel(&LoadingCanvas, 4, 0x2F3);
    FactionColorShadow[faction_id] = Buffer_get_pixel(&LoadingCanvas, 4, 0x2FB);
    // Land then water bases: rows are the defence levels, columns the sizes.
    for (int32_t water = 0, y = 1; water < 2; water++) {
        for (int32_t def = 0; def < 3; def++, y += 0x4C) {
            for (int32_t size = 0, x = 1; size < 4; size++, x += 0x65)
                pfncSpriteFromCanvasRectTrans(&BaseSprites[faction_id][water * 12 + size * 3 + def], &LoadingCanvas,
                                              kArtKey, x, y, 100, 0x4B, 0);
        }
    }
}

}  // namespace native_ui
