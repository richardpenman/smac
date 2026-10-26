// Colour tables (ui 0x422F20.., gfx2 Palette_*): translations of the game palette used by the
// blitters: the shadow table, faction tints (territory borders, unit colouring) and others,
// computed from the palette in HSV (Palette_create_table) or by RGB blending
// (Palette_create_table_from_color). The original caches them in *.tmp files; the result is
// the same, so they are always computed here.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

struct Hsv {  // 0x18 bytes: hue in degrees (-1: grey), saturation, value
    double h, s, v;
};
ADDR_NATIVE(0x628DB0) void GAMEAPI RGB_to_HSV(const uint8_t* rgb, Hsv* out);
// Nearest palette index by squared RGB distance: all 256 entries, or (game_only) 10..245.
ADDR_NATIVE(0x5FF470)
int32_t THISCALL Palette_get_nearest_palette_index(Palette* pal, int32_t r, int32_t g, int32_t b, int32_t game_only);
// Nearest palette index in the HSV cone (v, s cos h, s sin h): all 256 entries or 10..245.
ADDR_NATIVE(0x5FF630)
int32_t THISCALL
    Palette_get_nearest_palette_index_2(Palette* pal, const Hsv* target, const Hsv* table, int32_t game_only);
// out[i] = i outside [start, start + count); inside, the colour with its value scaled by
// (100 + percent)%.
ADDR_NATIVE(0x5FED40)
int32_t THISCALL Palette_create_table(Palette* pal, uint8_t* out, int32_t start, int32_t count, int32_t percent);
// out[i] = i outside [start, start + count); inside, the colour blended with palette[color] by
// weights (wa, wb).
ADDR_NATIVE(0x5FEFF0)
int32_t THISCALL Palette_create_table_from_color(Palette* pal, int32_t color, uint8_t* out, int32_t start,
                                                 int32_t count, int32_t wa, int32_t wb);

// Shadow table (darker by 43%) of Sprite_draw_dest (unk_6F107C).
GLOBAL(uint8_t[256], ShadowTable, 0x6F107C);
// The tint tables of the opening screens' menu column (SetupWin: Win +0xA0, pixels 0 of the window
// show the table's colour of what is under them): redtint (Crossfire) and bluetint.
GLOBAL(uint8_t[256], RedTintTable, 0x6F117C);
GLOBAL(uint8_t[256], BlueTintTable, 0x6F1A7C);
// Per faction tint of the territory borders (colortables_init_faction).
GLOBAL(uint8_t[8][256], FactionColorTable, 0x6F077C);

// The tables built at boot (load_fixed_sprites): shadow and the other fixed tables.
ADDR_NATIVE(0x423570) void GAMEAPI colortables_init();
// The faction's tint tables from <faction>.pcx (load_faction_art).
ADDR_NATIVE(0x423000) void GAMEAPI colortables_init_faction(int32_t faction_id);
