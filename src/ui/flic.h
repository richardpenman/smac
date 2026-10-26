// FLC animations (Autodesk FLIC, the original's Flic class at 0x629D60..: DirectDraw / GDI player).
// The frames are decoded (COLOR_256, COLOR_64, BRUN, COPY, BLACK, SS2/DELTA_FLC, LC chunks) and
// their colours mapped to the nearest entries of a target palette (the game palette, or the
// palette of the window that shows them).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/gfx.h"

struct Flc {
    int32_t width = 0, height = 0, speed = 100;
    std::vector<std::vector<uint8_t>> frames;  // target palette indices
};

// Decodes every frame of `name` (a game file) with its colours mapped to `target` (null: the raw
// indices, for a window whose colour table the animation was drawn for).
bool flc_load(const std::string& name, Flc* out, const Palette* target);
// The animation `name` in the game palette (cached); null when missing.
const Flc* flc_get(const std::string& name);
// Draws frame `frame` (modulo the frame count) at (x0, y0), clipped to the buffer.
void flc_draw(Buffer* s, const Flc* f, int32_t x0, int32_t y0, int32_t frame);
