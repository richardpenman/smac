// Graphics classes of the original UI (gfx/gfx2 modules): Buffer (an 8-bit DIB section the UI
// draws into), Palette, Font, Sprite (transparent bitmaps) and Texture (bitmaps mapped onto the
// terrain polygons).
//
// The original objects hold Windows handles and heap pointers, so they live in native memory
// (not in the engine memory image); their fields keep the original meaning and the original byte
// offset is noted per field. Functions keep the original names and addresses (ADDR_NATIVE: hybrid
// builds always call the original UI). Methods are free functions taking the object first.
// Drawing semantics follow the asm; where a routine is approximated (not bit-exact yet) its
// comment says so.
#pragma once
#include <cstdint>
#include <vector>

#include "engine/memmap.h"

struct UiRect {  // RECT; right and bottom are exclusive
    int32_t left, top, right, bottom;
};

// ---- Palette (0x5FE2A0..; global game palette object at 0x94C590) ----
struct Palette {
    uint8_t rgb[256][3];  // PALETTEENTRY r, g, b
};
extern Palette GamePalette;  // 0x94C590, loaded by load_palette
// Builds the game palette: Windows static colours in 0..9 and 246..255, PALETTE.PCX entries
// 0..235 in 10..245 (the pixel offset every art file is loaded with), then gamma correction
// ("Gamma Correction" preference, percent). The shadow translation table is derived from it.
ADDR_NATIVE(0x451A10) void GAMEAPI load_palette();
// Colour tables (shadow, tints) and the nearest-colour searches are in ui/colortables.h.

// ---- palette cycles (Palette +0x404: 5 slots of id, timer, first entry, count; saved colours at
// +0x410) ----
// Palette_init_cycle: cycle `id` rotates entries first..first+count-1 every `period` ms (a Time
// object with Palette_timer_callback). Returns 0, 3 (bad range or id -1). The cycles are kept for
// GamePalette only. load_cycling (system.txt #CYCLING) sets them up; the shipped file has none.
ADDR_NATIVE(0x5FE700)
int32_t THISCALL Palette_init_cycle(Palette* self, int32_t id, int32_t first, int32_t count, int32_t period);
// Starts the cycle's timer (0; 0xB: no such cycle).
ADDR_NATIVE(0x5FE8B0) int32_t THISCALL Palette_start_cycle(Palette* self, int32_t id);
// Palette_timer_callback (0x5FEAD0): every entry of the cycle takes the next one's colour, the
// last takes the first's.
void palette_cycle_rotate(Palette* self, int32_t id);
// The timers: runs the callbacks that are due (the message pump); true when the palette changed.
bool palette_cycles_tick(uint32_t now_ms);
// Test aid (PALTICK): n steps of every running cycle, then each cycle's colours on stderr.
void palette_cycles_step(int32_t n);
// Whether entry i belongs to a cycle (the nearest-colour searches skip it).
bool palette_cycle_excluded(const Palette* self, int32_t i);

// ---- Font (0x2C bytes; Font_ctor 0x618EA0) ----
// All fields are 32-bit: the HFONT is a platform font handle, so Font objects can also live in
// the memory image (MapWin's map fonts at +0x1EEC8, +0x1EEF4, +0x1EF20).
struct Font {
    int32_t field_0;  // +0x00: -1 after Font_init
    int32_t flags;  // +0x04: bit 0: resource font
    int32_t handle;  // +0x08: HFONT (platform font handle)
    int32_t height;  // +0x0C: tmHeight + tmExternalLeading (line height)
    int32_t ascent_above;  // +0x10: tmAscent - tmInternalLeading
    int32_t internal_leading;  // +0x14
    int32_t ascent;  // +0x18
    int32_t descent;  // +0x1C
    int32_t field_20;
    int32_t resource;  // +0x24: added font resource
    int32_t size;  // +0x28: size last passed by pfncZoomProcessing (cache)
};
static_assert(sizeof(Font) == 0x2C, "Font layout");
ADDR_NATIVE(0x618EA0) void THISCALL Font_ctor(Font* self);
// Creates the font: face, character height in pixels, style bits (1 bold, 2 italic, 4 underline).
ADDR_NATIVE(0x618F40) int32_t THISCALL Font_init(Font* self, const char* face, int32_t size, int32_t style);
ADDR_NATIVE(0x619280) int32_t THISCALL Font_width(Font* self, const char* text);
ADDR_NATIVE(0x6192F0) int32_t THISCALL Font_width_2(Font* self, const char* text, int32_t len);

// ---- Buffer (gfx 0x5D7210..; an 8-bit top-down DIB section) ----
struct Buffer {
    UiRect clip{};  // +0x20: clipping rectangle
    uint8_t* data = nullptr;  // +0x50: bits
    int32_t width = 0;  // +0x80 (BITMAPINFOHEADER biWidth)
    int32_t height = 0;  // +0x84 holds -height (top-down); positive here
    uint8_t colors[256][3]{};  // +0xA4: DIB colour table
    int32_t stride = 0;  // +0x4A8: bytes per row
    int32_t font_index = 0;  // +0x510: current font of a multi-font string
    Font* fonts[4]{};  // +0x52C: fonts of {}/[] text runs
    int32_t text_color[4]{};  // +0x53C: palette index
    int32_t shadow_color[4]{-1, -1, -1, -1};  // +0x54C: -1: no shadow
    int32_t shadow_dx[4]{};  // +0x55C
    int32_t shadow_dy[4]{};  // +0x56C
    uint8_t pixel_offset = 0;  // +0x580: offset added to pixels by Buffer_load_pcx
    std::vector<uint8_t> storage;  // native: owns the bits
};
ADDR_NATIVE(0x5D7210) void THISCALL Buffer_ctor(Buffer* self);
// Allocates a width x height canvas (cleared to 0) and resets the clip rectangle.
ADDR_NATIVE(0x5D7670)
int32_t THISCALL Buffer_init(Buffer* self, int32_t width, int32_t height, int32_t bpp, int32_t flags);
// Loads a PCX file (".pcx" appended when there is no extension). Pixels get `offset` added
// (mod 256); file palette entries 0..count-1 go to colours offset..offset+count-1, and when
// `pal` is given they are copied into it. Returns 0, 3 (bad file) or 6 (not found).
// Writes the canvas as a PCX file (".pcx" appended without an extension): 0, 3, 4 (no file).
ADDR_NATIVE(0x5DF590) int32_t THISCALL Buffer_write_pcx(Buffer* self, const char* name);
ADDR_NATIVE(0x5D7DE0)
int32_t THISCALL Buffer_load_pcx(Buffer* self, const char* file, Palette* pal, int32_t offset, int32_t count);
ADDR_NATIVE(0x5E2690)
int32_t THISCALL
    Buffer_load_pcx_2(Buffer* self, const uint8_t* data, int32_t size, Palette* pal, int32_t offset, int32_t count);
// Replaces every pixel of value `from` by `to` (both taken mod 256).
ADDR_NATIVE(0x5DE580) void THISCALL Buffer_change_color(Buffer* self, int32_t from, int32_t to);
ADDR_NATIVE(0x5E2210) int32_t THISCALL Buffer_get_pixel(Buffer* self, int32_t x, int32_t y);
ADDR_NATIVE(0x5D8000) void THISCALL Buffer_set_clip(Buffer* self, const UiRect* rect);
// Fills the rectangle (clipped) with a palette index.
ADDR_NATIVE(0x5DFCD0) void THISCALL Buffer_fill_3(Buffer* self, const UiRect* rect, int32_t color);
ADDR_NATIVE(0x5E1A80) void THISCALL Buffer_hline_2(Buffer* self, int32_t x1, int32_t x2, int32_t y, int32_t color);
ADDR_NATIVE(0x5E1BF0) void THISCALL Buffer_vline_2(Buffer* self, int32_t x, int32_t y1, int32_t y2, int32_t color);
// Fills the clip rectangle.
ADDR_NATIVE(0x5DFB50) void THISCALL Buffer_fill_2(Buffer* self, int32_t color);
// Fills (x, y, w, h) (clipped).
ADDR_NATIVE(0x5D8240)
void THISCALL Buffer_fill(Buffer* self, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color);
// Copies the w x h rectangle at (sx, sy) of self (clipped to self's size) to (dx, dy) of `dest`
// (clipped to dest's clip rectangle); dest may be self (overlapping copies move correctly).
ADDR_NATIVE(0x5DFF00)
int32_t THISCALL
    Buffer_copy_4(Buffer* self, Buffer* dest, int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w, int32_t h);
// Draws a one-pixel frame: top/left in color_tl, bottom/right in color_br.
ADDR_NATIVE(0x5E3203) void THISCALL Buffer_box_2(Buffer* self, const UiRect* rect, int32_t color_tl, int32_t color_br);
// Line from (x1, y1) to (x2, y2) (Bresenham from the left end; clipped to the clip rectangle).
ADDR_NATIVE(0x5E15F0)
void THISCALL Buffer_line_2(Buffer* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color);
// Filled polygon in one colour (Buffer_poly -> polygon 0x626620): pts[n] (x, y); rows from the
// top vertex to the row before the bottom one, x from the left edge to the right one (excluded);
// clipped to the clip rectangle minus its last row and column.
ADDR_NATIVE(0x5DA680) void THISCALL Buffer_poly(Buffer* self, const int32_t* pts, int32_t n, int32_t color);
ADDR_NATIVE(0x5DAC70) int32_t THISCALL Buffer_set_font(Buffer* self, Font* font, Font* font1, Font* font2, Font* font3);
// Text colour and shadow (colour -1: none; offset dx, dy) of the current font slot.
ADDR_NATIVE(0x5DACB0)
void THISCALL Buffer_set_text_color(Buffer* self, int32_t color, int32_t shadow, int32_t dx, int32_t dy);
// Writes len characters at (x, y + font ascent above the cell top); returns the end x.
ADDR_NATIVE(0x5DBD00)
int32_t THISCALL Buffer_write_raw_l(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len);
ADDR_NATIVE(0x5DCEA0)
int32_t THISCALL Buffer_write_l_3(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len);
// Centred in the rectangle (horizontally by the text extent, vertically by the font's ascent
// above the internal leading).
ADDR_NATIVE(0x5DD130)
int32_t THISCALL Buffer_write_cent_l_7(Buffer* self, const char* text, const UiRect* rect, int32_t len);
// Centred on x.
ADDR_NATIVE(0x5DD250)
int32_t THISCALL Buffer_write_cent_l_8(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len);

// ---- Sprite (0x2C bytes; gfx 0x5E37E0..) ----
// A transparent bitmap trimmed to its non-transparent bounding box when extracted.
struct Sprite {
    std::vector<uint8_t> pixels;  // +0x04: bits (stride bytes per row)
    int32_t key = 0;  // +0x08 (byte): transparent index given at extraction
    int32_t stride = 0;  // +0x0C
    int32_t width = 0;  // +0x10: trimmed size
    int32_t height = 0;  // +0x14
    int32_t full_width = 0;  // +0x18: extracted rectangle
    int32_t full_height = 0;  // +0x1C
    int32_t x_offset = 0;  // +0x20: position of the trimmed box in the rectangle
    int32_t y_offset = 0;  // +0x24
};
// Copies the w x h rectangle at (x, y) of a canvas into the sprite, trimmed to the pixels that
// are not `key` (0: ok; 3: bad arguments or the rectangle leaves the canvas; 9: all transparent).
// Sprite_draw_multi_table_dest_2 (0x5E8FF5): the sprite blended into dest through a group of 16
// colour tables (dest = table[pixel * 256 + dest]), scaled by BlitDestScale / BlitSourceScale.
ADDR_NATIVE(0x5E8FF5)
int32_t THISCALL
    Sprite_draw_multi_table_dest_2(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table);
// Buffer_draw_multi_table_dest_2 (0x5D94B0): the same for a whole buffer at dest_scale / source_scale
// (approximated with the sprite blitter's scaling).
ADDR_NATIVE(0x5D94B0)
int32_t THISCALL Buffer_draw_multi_table_dest_2(Buffer* self, Buffer* dest, int32_t key, int32_t x, int32_t y,
                                                int32_t dest_scale, int32_t source_scale, const uint8_t* table);
ADDR_NATIVE(0x5E39A0)
int32_t THISCALL pfncSpriteFromCanvasRectTrans(Sprite* self, Buffer* canvas, int32_t key, int32_t x, int32_t y,
                                               int32_t w, int32_t h, int32_t flags);
// Scale table of the blitters (0x9B3AD8, 800 entries) for BlitDestScale / BlitSourceScale
// (piDestScale/piSourceScale): destination pixels per source pixel (expanding) or source pixels
// per destination pixel (shrinking), Bresenham distributed.
GLOBAL(int32_t[800], ScaleTable, 0x9B3AD8);
GLOBAL(int32_t, ScaleTableDest, 0x696D20);  // scale the table was built for
GLOBAL(int32_t, ScaleTableSource, 0x696D24);
ADDR_NATIVE(0x5EB20F) void GAMEAPI Sprite_fill_scaling_table();
// Draws the sprite at (x, y) + its offsets, scaled by BlitDestScale/BlitSourceScale (at most
// 4x): pixels equal to `key` are skipped, pixel 0 maps the destination through `table`
// (shadow), others are copied.
ADDR_NATIVE(0x5E5833)
int32_t THISCALL Sprite_draw_dest(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table);
// As Sprite_draw_dest, every non-key pixel copied.
ADDR_NATIVE(0x5E4B9A)
int32_t THISCALL pfncSpriteStretchCopyToCanvas(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y);
// As Sprite_draw_dest, every non-key pixel drawn in `color` (the silhouette).
ADDR_NATIVE(0x5E3E50)
int32_t THISCALL Sprite_draw_mono_2(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, int32_t color);
// As Sprite_draw_dest, non-key pixels translated: dest = table[pixel].
ADDR_NATIVE(0x5E9D44)
int32_t THISCALL Sprite_draw_4(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table);

// ---- Texture (0x70 bytes; gfx2 0x619650..) ----
struct Texture {
    std::vector<uint8_t> pixels;  // +0x00: bits (stride = width)
    int32_t width = 0;  // +0x04
    int32_t height = 0;  // +0x08
    int32_t points[12][2]{};  // +0x0C: reference points (corners, edge midpoints)
};
// Copies a w x h rectangle of a canvas into the texture.
ADDR_NATIVE(0x619710)
int32_t THISCALL
    pfncImageFromCanvas(Texture* self, Buffer* canvas, int32_t x, int32_t y, int32_t w, int32_t h, int32_t flags);
// The texture blitters (Texture_draw_*) are in ui/texture.h.
