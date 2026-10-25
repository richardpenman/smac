// Text layout of the original UI's Buffer (gfx module 0x5DAC70..0x5DE1D0, Font 0x619370): text in
// several fonts ("{bold}", "[italic]", "$LINK<text=target>" hyperlinks; "{{", "[[" etc. are the
// literal characters), word wrapping, and the "flying" writer that flows consecutive strings into
// one paragraph (Buffer_write_strings: popups, datalinks, monuments).
//
// The Buffer fields this needs beyond those in gfx.h (the font slot of the line-break and width
// scans, the hyperlink state, the multi-font switch, the hanging indent and the line counter) are
// kept in a side table (BufferText) keyed by the Buffer, with the original offsets noted. The
// running position of the flying writer is global, as in the original (0x9B3A90..0x9B3A9C,
// 0x9B33CC).
//
// Glyphs are rasterised by the platform layer (platform::font_render, GDI TextOut without
// antialiasing on an 8-bit DIB; the one place where the original's font rendering is
// approximated). Widths are GetTextExtentPoint32 (platform::font_text_width).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Buffer fields kept outside gfx.h's Buffer.
struct BufferText {
    int32_t hang = 0;         // +0x10: indent of the lines after the first (wrap writers)
    int32_t indent = 0;       // +0x14: indent still to apply at the next line break
    int32_t lines = 0;        // +0x18: lines written by the last wrap
    int32_t links = 0;        // +0x4AC: hyperlinks registered
    int32_t break_font = 0;   // +0x514: font slot of the line-break scan
    int32_t width_font = 0;   // +0x518: font slot of the width scan
    int32_t link_state = 0;   // +0x51C: 1 inside "$LINK<", 2 at its "=target>"
    int32_t multi_font = 1;   // +0x520: {} [] $LINK markup is interpreted (0x696BF0: 1)
};
BufferText& buffer_text(Buffer* self);

// A list of strings with a layout mode each, as the original's StringStruct/StringList (heap
// lists of {text, mode}): mode 0 continues the paragraph, 1 starts a new line ("^"), 2 is a
// centred line ("^^"), 3 an empty line ("^" alone).
struct TextLine {
    char* text;
    int32_t mode;
};

// The flying writer's state (0x9B3A90 x, 0x9B3A94 y, 0x9B3A98 width, 0x9B3A9C width used on the
// current line, 0x9B33CC the pending text of the line).
extern int32_t FlyX, FlyY, FlyWidth, FlyUsed;
extern char FlyLine[1024];

// Splits text[0..len) at a word boundary so that it fits in *width pixels of `font` (spaces
// belong to the following word). Returns the start of the next line, or null when everything
// fits; *width becomes the width left (0 when broken). Adds the width used to FlyUsed.
ADDR_NATIVE(0x619370) const char* THISCALL Font_find_line_break_l(Font* self, const char* text, int32_t* width,
                                                                int32_t len);
// As Font_find_line_break_l across the font switches of a multi-font string.
ADDR_NATIVE(0x5DC410) const char* THISCALL Buffer_find_line_break_l(Buffer* self, const char* text, int32_t* width,
                                                                  int32_t len);
ADDR_NATIVE(0x5DC790) int32_t THISCALL Buffer_text_width(Buffer* self, const char* text);
ADDR_NATIVE(0x5DC7C0) int32_t THISCALL Buffer_text_width_2(Buffer* self, const char* text, int32_t len);
// Line height of the main font (tmHeight + tmExternalLeading) / its ascent above the internal
// leading.
ADDR_NATIVE(0x5DCAB0) int32_t THISCALL Buffer_text_line_height(Buffer* self);
ADDR_NATIVE(0x5DCA80) int32_t THISCALL Buffer_text_height(Buffer* self);
// Writes len characters at (x, y), switching fonts at the markup; returns the end x.
ADDR_NATIVE(0x5DCAE0) int32_t THISCALL Buffer_write_multi_font_raw_l(Buffer* self, const char* text, int32_t x,
                                                                    int32_t y, int32_t len);
// Left aligned, vertically centred in rect.
ADDR_NATIVE(0x5DCF40) int32_t THISCALL Buffer_write_l_4(Buffer* self, const char* text, const UiRect* rect,
                                                       int32_t len);
// Centred in width w starting at x.
ADDR_NATIVE(0x5DD020) int32_t THISCALL Buffer_write_cent_l_5(Buffer* self, const char* text, int32_t x, int32_t y,
                                                            int32_t w, int32_t len);
// (Buffer_write_cent_l_7, centred in a rectangle, is in ui/gfx.h.)
// Right aligned, vertically centred in rect.
ADDR_NATIVE(0x5DD450) int32_t THISCALL Buffer_write_right_l_6(Buffer* self, const char* text, const UiRect* rect,
                                                             int32_t len);
// Word-wrapped from the top left of rect; returns the y below the last line.
ADDR_NATIVE(0x5DD530) int32_t THISCALL Buffer_wrap_2(Buffer* self, const char* text, const UiRect* rect);
// Word-wrapped, each line centred in width w; returns the y below the last line.
ADDR_NATIVE(0x5DD920) int32_t THISCALL Buffer_wrap_cent_4(Buffer* self, const char* text, int32_t x, int32_t y,
                                                         int32_t w);
// Height of text wrapped to width w.
ADDR_NATIVE(0x5DC360) int32_t THISCALL Buffer_wrap_height(Buffer* self, const char* text, int32_t w);
// The flying writer's measuring twins: start (width w), continue with more text, finish.
ADDR_NATIVE(0x5DDDA0) void THISCALL Buffer_wrap_height_flying(Buffer* self, const char* text, int32_t w);
ADDR_NATIVE(0x5DDDE0) int32_t THISCALL Buffer_wrap_height_flying_2(Buffer* self, const char* text);
ADDR_NATIVE(0x5DDEF0) int32_t THISCALL Buffer_wrap_height_flying_flush(Buffer* self);
// The flying writer: start at (x, y) with width w, continue, finish (writes the pending line).
ADDR_NATIVE(0x5DDF60) void THISCALL Buffer_wrap_flying(Buffer* self, const char* text, int32_t x, int32_t y,
                                                      int32_t w);
ADDR_NATIVE(0x5DDFB0) int32_t THISCALL Buffer_wrap_flying_2(Buffer* self, const char* text);
ADDR_NATIVE(0x5DE1D0) int32_t THISCALL Buffer_wrap_flying_flush(Buffer* self);
// Writes the strings (modes as TextLine) flowing into width w from (x, y); returns the y below.
// flags bit 1: continue from the list's current entry (not used by popups).
ADDR_NATIVE(0x5DB040) int32_t THISCALL Buffer_write_strings(Buffer* self, const TextLine* lines, int32_t count,
                                                           int32_t x, int32_t y, int32_t w, int32_t flags);
// Its measuring twin (Buffer_write_strings_height 0x5DAD70 / BasePop_calculate_dimensions): the
// height of the strings flowed into width w.
int32_t buffer_strings_height(Buffer* self, const TextLine* lines, int32_t count, int32_t w);

// Text colours of the font slots 1..3 (Buffer_set_text_color sets slot 0).
ADDR_NATIVE(0x5DACE0) void THISCALL Buffer_set_text_color2(Buffer* self, int32_t color, int32_t shadow, int32_t dx,
                                                          int32_t dy);
ADDR_NATIVE(0x5DAD10) void THISCALL Buffer_set_text_color3(Buffer* self, int32_t color, int32_t shadow, int32_t dx,
                                                          int32_t dy);
ADDR_NATIVE(0x5DAD40) void THISCALL Buffer_set_text_color_hyper(Buffer* self, int32_t color, int32_t shadow,
                                                               int32_t dx, int32_t dy);
