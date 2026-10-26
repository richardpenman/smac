#include <cstdio>
// Map art loading (see sprites.h).
#include "ui/sprites.h"

#include <cstring>
#include <map>
#include <string>

#include "engine/ui_bridge.h"
#include "engine/globals.h"
#include "engine/text.h"
#include "ui/mapwin.h"
#include "ui/colortables.h"
#include "ui/texture.h"

Sprite BaseSprites[8][37];

namespace {
std::map<uint32_t, Texture> g_textures;
std::map<uint32_t, Sprite> g_sprites;
// The loading canvas (poLoadingCanvas, 0x798668).
Buffer LoadingCanvas;
}  // namespace

Texture* ArtTexture(uint32_t addr) { return &g_textures[addr]; }
Sprite* ArtSprite(uint32_t addr) { return &g_sprites[addr]; }

namespace {

void tex(uint32_t addr, Buffer* canvas, int32_t x, int32_t y, int32_t w = 0x38, int32_t h = 0x38) {
    pfncImageFromCanvas(ArtTexture(addr), canvas, x, y, w, h, 0);
}

// TER1-style cell: the purple tile outline (raw colours 252, 253) becomes transparent first.
void cell(uint32_t addr, int32_t x, int32_t y, int32_t w = 100, int32_t h = 0x3E) {
    Buffer_change_color(&LoadingCanvas, 0x107, 0x109);
    Buffer_change_color(&LoadingCanvas, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(ArtSprite(addr), &LoadingCanvas, 0x109, x, y, w, h, 0);
}

}  // namespace

void GAMEAPI load_terrain() {
    Buffer work;  // local Buffer of the original
    Buffer_ctor(&work);
    TextureKey = kArtKey;
    Buffer_load_pcx(&LoadingCanvas, "TEXTURE.PCX", nullptr, 10, 0xEC);
    Buffer_init(&work, 0x1C, 0x1C, 0, 0);
    tex(0x787FB8, &LoadingCanvas, 1, 0x23B);
    tex(0x75B858, &LoadingCanvas, 0x3A, 0x23B);
    // Rocky: x advances through both passes, the second texture of a pass two slots further.
    for (int32_t pass = 0, x = 1; pass < 2; pass++)
        for (int32_t k = 0; k < 2; k++, x += 0x39) tex(kTexRocky + (pass + 2 * k) * 0x70, &LoadingCanvas, x, 1);
    for (int32_t i = 0; i < 2; i++) tex(kTexWater + i * 0x70, &LoadingCanvas, 0x118, 0x4F + i * 0x39);
    tex(kTexArid, &LoadingCanvas, 1, 0x3A);
    for (int32_t i = 0; i < 16; i++) tex(kTexMoist + i * 0x70, &LoadingCanvas, 1 + (i & 3) * 0x39, 0x73 + (i >> 2) * 0x39);
    for (int32_t i = 0; i < 16; i++) tex(kTexRainy + i * 0x70, &LoadingCanvas, 1 + (i & 3) * 0x39, 0x157 + (i >> 2) * 0x39);
    for (int32_t r = 0; r < 4; r++)
        for (int32_t c = 0; c < 4 && !(r == 3 && c == 3); c++)
            tex(kTexJungle + (c + r * 4) * 0x70, &LoadingCanvas, 0x20E + c * 0x39, 0x103 + r * 0x39);
    tex(kTexDune, &LoadingCanvas, 0xE5, 1);
    for (int32_t i = 0; i < 8; i++) tex(kTexMesa + i * 0x70, &LoadingCanvas, 0x300 + (i & 3) * 0x39, 0xF + (i >> 2) * 0x39);
    for (int32_t i = 0; i < 9; i++) tex(kTexRoad + i * 0x70, &LoadingCanvas, 0x307 + (i % 3) * 0x39, 0x18B + (i / 3) * 0x39);
    for (int32_t i = 0; i < 9; i++) tex(kTexMagTube + i * 0x70, &LoadingCanvas, 0x307 + (i % 3) * 0x39, 0x236 + (i / 3) * 0x39);
    for (int32_t i = 0; i < 16; i++) tex(kTexRiver + i * 0x70, &LoadingCanvas, 0x118 + (i & 3) * 0x39, 0x103 + (i >> 2) * 0x39);
    for (int32_t i = 0; i < 3; i++) tex(kTexVolcano + i * 0x70, &LoadingCanvas, 1 + i * 0x39, 0x274);
    for (int32_t i = 0; i < 3; i++) tex(kTexCrater + i * 0x70, &LoadingCanvas, 1 + i * 0x39, 0x2AD);
    for (int32_t r = 0; r < 4; r++)
        for (int32_t c = 0; c < 4 && !(r == 3 && c == 3); c++)
            tex(kTexFungus + (c + r * 4) * 0x70, &LoadingCanvas, 0x118 + c * 0x39, 0x204 + r * 0x39);
    for (int32_t r = 0; r < 4; r++)
        for (int32_t c = 0; c < 4 && !(r == 3 && c == 3); c++)
            tex(kTexSeaFungus + (c + r * 4) * 0x70, &LoadingCanvas, 0x1FC + c * 0x39, 0x204 + r * 0x39);
    for (int32_t i = 0; i < 9; i++) tex(kTexFarm + i * 0x70, &LoadingCanvas, 0x307 + (i % 3) * 0x39, 0xDB + (i / 3) * 0x39);
    for (int32_t i = 0; i < 16; i++) tex(kTexForest + i * 0x70, &LoadingCanvas, 0x20E + (i & 3) * 0x39, 6 + (i >> 2) * 0x39);

    // RAINFALL.PCX: the coast shapes and the territory border directions are drawn through
    // colour -> index tables into the work buffer and taken as textures.
    Buffer_load_pcx(&LoadingCanvas, "RAINFALL.PCX", nullptr, 10, 0xEC);
    Buffer_change_color(&LoadingCanvas, 0x107, 0x109);
    Buffer_change_color(&LoadingCanvas, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(ArtSprite(0x7ACBB8), &LoadingCanvas, 0x109, 1, 0x34, 100, 100, 0);
    Sprite shape;
    pfncSpriteFromCanvasRectTrans(&shape, &LoadingCanvas, 0x109, 2, 0x14D, 0x71, 0x38, 0);
    for (int32_t j = 0, n = 0; j < 3; j++)
        for (int32_t i = 0; i < 4; i++) CoastColors[n++] = (uint8_t)Buffer_get_pixel(&LoadingCanvas, 0x82 + i * 0xE, 0x158 + j * 0x11);
    uint8_t table[256];
    std::memset(table, 0, sizeof(table));
    const uint8_t* shape_colors = mem::host(0x6846F8);
    for (int32_t i = 0; i < 0x35; i++) table[(uint8_t)(shape_colors[i] + 10)] = (uint8_t)i;
    Buffer_init(&work, 0xAA, 0x3C, 0, 0);
    Sprite_draw_4(&shape, &work, shape.key & 0xFF, 0, 0, table);
    tex(kTexCoastShape, &work, 0, 0);
    tex(kTexCoastShape + 0x70, &work, 0x39, 0);
    static const uint8_t kBorderColors[9] = {0xD3, 0xD0, 0x29, 0x8F, 0x74, 0x97, 0xCE, 0x5F, 0xC0};
    pfncSpriteFromCanvasRectTrans(&shape, &LoadingCanvas, 0x109, 2, 0xFE, 0x3C, 0x3C, 0);
    std::memset(table, 0, sizeof(table));
    for (int32_t i = 0; i < 9; i++) table[(uint8_t)(kBorderColors[i] + 10)] = (uint8_t)i;
    UiRect all{0, 0, work.width, work.height};
    Buffer_fill_3(&work, &all, 9);  // Buffer_fill_2(9)
    Sprite_draw_4(&shape, &work, shape.key & 0xFF, 0, 0, table);
    tex(kTexBorder, &work, 0, 0, 0x3C, 0x3C);

    Buffer_load_pcx(&LoadingCanvas, "glow.pcx", nullptr, 0, 0x100);
    cell(0x779770, 1, 1);
    cell(0x77979C, 0x66, 1);

    Buffer_load_pcx(&LoadingCanvas, "TER1WRECK.PCX", nullptr, 10, 0xEC);
    static const struct { uint32_t addr; int16_t x, y; } kWreck[] = {
        {0x776994, 0x1A, 0x15},  {0x776910, 0x7F, 0x15},  {0x7769C0, 0xE4, 0x15},  {0x776860, 0x1A, 0x54},
        {0x77688C, 0x7F, 0x54},  {0x7768B8, 0xE4, 0x54},  {0x776834, 0x1A, 0x93},  {0x776758, 0x7F, 0x93},
        {0x776784, 0xE4, 0x93},  {0x776808, 0x1A, 0xD2},  {0x7767DC, 0x7F, 0xD2},  {0x7767B0, 0xE4, 0xD2},
        {0x776968, 0x1A, 0x111}, {0x7768E4, 0x7F, 0x111}, {0x77693C, 0xE4, 0x111}, {0x799658, 0x149, 0x93},
        {0x799684, 0x1AE, 0x93}, {0x7996B0, 0x149, 0xD2}, {0x7996DC, 0x1AE, 0xD2}, {0x7ACE4C, 0x149, 0x15},
        {0x7ACD70, 0x1AE, 0x15}, {0x7ACD9C, 0x213, 0x15}, {0x7ACE20, 0x149, 0x54}, {0x7ACDF4, 0x1AE, 0x54},
        {0x7ACDC8, 0x213, 0x54},
    };
    for (const auto& c : kWreck) cell(c.addr, c.x, c.y);

    Buffer_load_pcx(&LoadingCanvas, "TER1.PCX", nullptr, 10, 0xEC);
    Buffer_init(&work, 100, 0x3E, 0, 0);
    for (int32_t i = 0; i < 3; i++) {
        cell(0x791C58 + i * 0x58, 1 + i * 0x65, 0x1BF);
        cell(0x791C84 + i * 0x58, 1 + i * 0x65, 0x243);
    }
    for (int32_t i = 0; i < 3; i++) {
        cell(0x7A99A0 + i * 0x84, 1 + i * 0x65, 0x1FE);
        cell(0x7A99CC + i * 0x84, 1 + i * 0x65, 0x282);
        cell(0x7A99F8 + i * 0x84, 1 + i * 0x65, 0x2C1);
    }
    static const struct { uint32_t addr; int16_t x, y; } kTer1[] = {
        {0x77944C, 0x1FA, 1},    {kSprMine, 0x25F, 0x40},   {kSprMineSea, 0x1FA, 0x40}, {kSprSolar, 0x25F, 0x7F},
        {kSprSolarSea, 0x1FA, 0x7F}, {0x798C40, 0x1FA, 0xBE}, {kSprKelp, 0x25F, 0xBE},  {kSprBunker, 0x1FA, 0x13C},
        {kSprAirbase, 0x25F, 0x13C}, {kSprSensor, 0x2C4, 0x13C},
    };
    for (const auto& c : kTer1) cell(c.addr, c.x, c.y);
    for (int32_t k = 0; k < 5; k++) {
        int32_t y = 0x1C5 + k * 0x3F;
        cell(0x7792B0 + k * 0x2C, 0x26C, y);
        cell(kSprEnricher + k * 0x2C, 0x336, y);
        cell(kSprFarm + k * 0x2C, 0x39B, y);
    }
    static const struct { uint32_t addr; int16_t x, y; } kTer1b[] = {
        {kSprCondenser, 0x1FA, 0xFD}, {kSprMirror, 0x25F, 0xFD}, {kSprBorehole, 0x2C4, 0xFD},
        {kSprBoreholeGeo, 0x2C4, 0xBE}, {0x78A700, 0x336, 0xFD}, {0x78A72C, 0x336, 0xBE},
        {0x7AC11C, 0x1DE, 0x278}, {0x7AC0F0, 0x1DE, 0x2B7}, {0x7AC148, 0x243, 0x278},
        {0x7AC098, 0x243, 0x2B7}, {0x7AC174, 0x2A8, 0x278}, {0x7AC0C4, 0x2A8, 0x2B7},
    };
    for (const auto& c : kTer1b) cell(c.addr, c.x, c.y);
    for (int32_t r = 0; r < 3; r++) {
        int32_t y = 0xFD + r * 0x3F;
        uint32_t base = 0x75B2B4 + r * 0xB0;
        cell(base - 0x2C, 1, y);
        cell(base, 0x66, y);
        cell(base - 0x84, 0xCB, y);
        cell(base - 0x58, 0x130, y);
    }
    for (int32_t i = 0; i < 6; i++) cell(kSprPod + ((~i & 1) + (i >> 1) * 2) * 0x2C, 0x1A2 + i * 0x65, 0x17B);
    cell(kSprMonolith, 0x130, 1);
}

void GAMEAPI load_cycling() {
    if (text_open("system", "cycling") != 0) return;
    text_get();
    int32_t count = text_item_number();
    for (int32_t i = 0; i < count; i++) {
        // Palette_init_cycle(i, first + 10, length, period); Palette_start_cycle(i): not
        // reconstructed (no cycles in the shipped system.txt).
        text_get();
    }
    if (text_open("system", "oceanoffsets") != 0) return;
    // Every following line that starts with a digit gives the next entry, until the table is
    // full (also past the end of the section).
    int32_t n = 0;
    for (char* line = text_get(); line; line = text_get()) {
        if (n >= 80) return;
        if (*line > '/' && *line < ':') OceanDepthLight[n++] = text_item_number();
    }
}

void GAMEAPI load_faction_art(int32_t faction_id) {
    colortables_init_faction(faction_id);
    std::string file = std::string(MFactions[faction_id].filename) + ".pcx";
    if (Buffer_load_pcx(&LoadingCanvas, file.c_str(), nullptr, 10, 0xEC))
        Buffer_load_pcx(&LoadingCanvas, "faction.pcx", nullptr, 10, 0xEC);
    // FactionPortraits[faction_id] (0x6846D8) points to the faction's sprite 28 (0x130, 0x1C9,
    // 0x83 x 0x9D), the popups' leader picture; here a name BasePop_set_picture recognises.
    {
        static char portrait[8][16];
        std::snprintf(portrait[faction_id & 7], sizeof(portrait[0]), "faction:%d", faction_id);
        FactionPortraits[faction_id & 7] = portrait[faction_id & 7];
    }
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
    // The faction's other pictures (24..36): the emblems, the leader, the logos.
    static const int16_t kPic[13][4] = {{1, 0x1C9, 0x56, 0x48},     {1, 0x212, 0x56, 0x48},     {0x58, 0x1C9, 0x60, 0x73},
                                        {0xB9, 0x1C9, 0x76, 0x90},  {0x130, 0x1C9, 0x83, 0x9D}, {0x1B4, 0x1C9, 0x41, 0x32},
                                        {0x1F6, 0x1C9, 0xE9, 0x7C}, {0x213, 1, 0xA4, 0xC6},     {0x2BD, 1, 0x46, 0x46},
                                        {0x2BD, 0x48, 0x46, 0x46},  {0x304, 1, 0x25, 0x1D},     {0x304, 0x1F, 0x25, 0x1D},
                                        {0x304, 0x3D, 0x25, 0x1D}};
    for (int32_t i = 0; i < 13; i++)
        pfncSpriteFromCanvasRectTrans(&BaseSprites[faction_id][24 + i], &LoadingCanvas, kArtKey, kPic[i][0], kPic[i][1],
                                      kPic[i][2], kPic[i][3], 0);
}
