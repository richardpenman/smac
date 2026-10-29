// The game's screen size and scale for interactive play (smacx's display settings).
//
// The original runs at the size of the Windows desktop (WinMain, Win_init_class 0x5F01F0:
// GetSystemMetrics), 1024 x 768 or 800 x 600 under DirectDraw. smacx chooses a logical screen
// (ScreenWidth x ScreenHeight, the size the windows are laid out for) and an integer scale: each
// logical pixel is shown as scale x scale pixels of the display, so that the 8-bit art stays sharp
// on a large or HiDPI display. The ini's [Alpha Centauri] keys:
//   Screen Size = auto | WxH    (auto: the display's size divided by the scale)
//   UI Scale    = auto | 1 | 2 | 3 (auto: the largest whole s with display height / s >= 768)
//   Fullscreen  = 0 | 1         (1: the desktop's fullscreen, no mode change; Alt+Enter toggles it)
// The computation is here, without SDL, so that the unit tests can check it.
#pragma once
#include <cstdint>

namespace display {

constexpr int32_t kMinWidth = 1024, kMinHeight = 768;  // the console's width, the original's 1024 x 768

struct Settings {
    int32_t width = 0, height = 0;  // Screen Size; 0: auto
    int32_t scale = 0;  // UI Scale; 0: auto
    bool fullscreen = true;
};

// "auto" (or empty, or unreadable) gives 0 x 0; "WxH" its numbers.
void parse_size(const char* text, int32_t* width, int32_t* height);
// "auto" (or empty, or out of 1..8) gives 0; else the number.
int32_t parse_scale(const char* text);

struct Mode {
    int32_t width = kMinWidth, height = kMinHeight;  // the logical screen
    int32_t scale = 1;
};
// The logical screen and the scale for an area of area_w x area_h display pixels (the display in
// fullscreen, the room a window has otherwise). Auto scale: the largest s >= 1 with
// area_h / s >= 768 and area_w / s >= 1024. Auto size: the area divided by the scale, at least
// 1024 x 768. A size given keeps its value (at least 1024 x 768); its auto scale is the largest at
// which it fits the area.
Mode choose(const Settings& s, int32_t area_w, int32_t area_h);

}  // namespace display
