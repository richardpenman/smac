// Platform layer of the native UI: what the original gets from DirectDraw/GDI (window, 8-bit
// palettized screen, text rasterisation), user32 (input messages) and WinMM (clock).
//
// The reconstructed UI (src/ui) draws into 8-bit Buffers exactly as the original does into its
// DIB sections; the platform only presents a finished 8-bit frame with its palette, delivers
// input as Windows-style messages (virtual-key codes, client coordinates) and rasterises text.
// The SDL2 backend is src/platform/sdl/ui_platform_sdl.cpp. No SDL types appear here, so other
// backends can be added without touching src/ui.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct Rgb {
    uint8_t r, g, b;
};

// ---- window / screen (DirectDraw primary surface, or the GDI window in windowed mode) ----
// Creates the game window with a client area of width x height pixels. hidden: no visible window
// (offscreen rendering for screenshots/tests; use SDL_VIDEODRIVER=dummy without a display).
bool video_init(const char* title, int32_t width, int32_t height, bool hidden);
void video_shutdown();
// Shows an 8-bit frame (top-down rows of `pitch` bytes) through `palette` (256 entries).
void video_present(const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height, const Rgb* palette);
// Writes an 8-bit frame as a 24-bit BMP. Returns false on error.
bool save_bmp(const char* path, const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height,
              const Rgb* palette);

// ---- input (the window procedure's messages) ----
enum EventType : int32_t {
    EV_NONE = 0,
    EV_QUIT,         // WM_CLOSE
    EV_KEY_DOWN,     // WM_KEYDOWN: key = Windows virtual-key code, mods = MOD_* bits
    EV_KEY_UP,       // WM_KEYUP
    EV_CHAR,         // WM_CHAR: key = character (Windows-1252)
    EV_MOUSE_MOVE,   // WM_MOUSEMOVE: x, y client coordinates
    EV_LBUTTON_DOWN, // WM_LBUTTONDOWN
    EV_LBUTTON_UP,
    EV_RBUTTON_DOWN,
    EV_RBUTTON_UP,
    EV_MOUSE_WHEEL,  // WM_MOUSEWHEEL: key = +1 (up) / -1 (down)
    EV_EXPOSE,       // WM_PAINT: the window needs to be presented again
};
enum : int32_t { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };
struct Event {
    int32_t type = EV_NONE;
    int32_t key = 0;
    int32_t mods = 0;
    int32_t x = 0, y = 0;
};
// Next pending event (PeekMessage); false when none.
bool poll_event(Event* ev);
// Waits up to timeout_ms for an event (MsgWaitForMultipleObjects); false on timeout.
bool wait_event(Event* ev, int32_t timeout_ms);
// GetAsyncKeyState for a virtual-key code: true while held.
bool key_down(int32_t vk);

// ---- clock (timeGetTime) ----
uint32_t ticks_ms();
void delay_ms(uint32_t ms);

// ---- fonts (GDI CreateFontIndirect / GetTextMetrics / TextOut on an 8-bit DIB) ----
struct FontMetrics {
    int32_t height;            // tmHeight
    int32_t ascent;            // tmAscent
    int32_t descent;           // tmDescent
    int32_t internal_leading;  // tmInternalLeading
    int32_t external_leading;  // tmExternalLeading
};
// Opens face `face` (e.g. "Arial") with a character height of `pixels` (lfHeight = -pixels),
// bold/italic/underline as in LOGFONT. Returns a handle > 0, or 0 when no font file was found.
int32_t font_open(const char* face, int32_t pixels, bool bold, bool italic, bool underline);
void font_close(int32_t handle);
bool font_metrics(int32_t handle, FontMetrics* out);
// GetTextExtentPoint32 width of text.
int32_t font_text_width(int32_t handle, const char* text, int32_t len);
// Rasterises text without antialiasing (as GDI on an 8-bit DIB): mask is w*h bytes, 1 = ink.
// The mask is one text cell high (tmHeight); x advances by the text extent.
bool font_render(int32_t handle, const char* text, int32_t len, std::vector<uint8_t>* mask, int32_t* w,
                 int32_t* h);

// ---- files ----
// Directory searched first for fonts the game ships (the game directory).
void set_game_dir(const std::string& dir);

}  // namespace platform

// Windows virtual-key codes (VK_*) used by the original's key handlers. Named VKEY_* so they do
// not collide with <windows.h> macros in a native Windows build.
enum : int32_t {
    VKEY_BACK = 0x08, VKEY_TAB = 0x09, VKEY_RETURN = 0x0D, VKEY_SHIFT = 0x10, VKEY_CONTROL = 0x11,
    VKEY_MENU = 0x12, VKEY_ESCAPE = 0x1B, VKEY_SPACE = 0x20, VKEY_PRIOR = 0x21, VKEY_NEXT = 0x22,
    VKEY_END = 0x23, VKEY_HOME = 0x24, VKEY_LEFT = 0x25, VKEY_UP = 0x26, VKEY_RIGHT = 0x27, VKEY_DOWN = 0x28,
    VKEY_INSERT = 0x2D, VKEY_DELETE = 0x2E, VKEY_NUMPAD0 = 0x60, VKEY_MULTIPLY = 0x6A, VKEY_ADD = 0x6B,
    VKEY_SUBTRACT = 0x6D, VKEY_DIVIDE = 0x6F, VKEY_F1 = 0x70, VKEY_F12 = 0x7B, VKEY_OEM_PLUS = 0xBB,
    VKEY_OEM_MINUS = 0xBD,
};
