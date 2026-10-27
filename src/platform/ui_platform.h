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
#include <memory>
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
// Per-pixel palettes (the original's windows each have their own DIB colour table under GDI: the
// opening screens show their pictures through their own palettes): while set, a pixel whose layer
// (top-down rows of `pitch` bytes, as the frame's) is n > 0 and n < count takes its colour from
// palettes[n] instead of the frame's palette. layers null: off.
void set_layer_palettes(const uint8_t* layers, int32_t pitch, const Rgb* const* palettes, int32_t count);

// ---- input (the window procedure's messages) ----
enum EventType : int32_t {
    EV_NONE = 0,
    EV_QUIT,  // WM_CLOSE
    EV_KEY_DOWN,  // WM_KEYDOWN: key = Windows virtual-key code, mods = MOD_* bits
    EV_KEY_UP,  // WM_KEYUP
    EV_CHAR,  // WM_CHAR: key = character (Windows-1252)
    EV_MOUSE_MOVE,  // WM_MOUSEMOVE: x, y client coordinates
    EV_LBUTTON_DOWN,  // WM_LBUTTONDOWN
    EV_LBUTTON_UP,
    EV_RBUTTON_DOWN,
    EV_RBUTTON_UP,
    EV_MOUSE_WHEEL,  // WM_MOUSEWHEEL: key = +1 (up) / -1 (down)
    EV_EXPOSE,  // WM_PAINT: the window needs to be presented again
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
    int32_t height;  // tmHeight
    int32_t ascent;  // tmAscent
    int32_t descent;  // tmDescent
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
// Text extents as Wine's GetTextExtentPoint32 gives them to the original (the advances of the
// normal hinting target, while its TextOut draws with mono hinting): only for pixel comparisons
// with the original's dumps under Wine (smacx --dump / --basedump). Otherwise a text is measured
// exactly as wide as it is drawn, as GDI does on Windows.
void font_set_wine_extents(bool on);
// Rasterises text without antialiasing (as GDI on an 8-bit DIB): mask is w*h bytes, 1 = ink.
// The mask is one text cell high (tmHeight); x advances by the text extent.
bool font_render(int32_t handle, const char* text, int32_t len, std::vector<uint8_t>* mask, int32_t* w, int32_t* h);

// ---- files ----
// Directory searched first for fonts the game ships (the game directory).
void set_game_dir(const std::string& dir);

// ---- true-colour frames (the movie player's DirectDraw surface) ----
// Shows a frame of 0xAARRGGBB pixels (rows of `width`), scaled to fit the window and centred on
// black, keeping its aspect ratio.
void video_present_rgb(const uint32_t* pixels, int32_t width, int32_t height);

// ---- audio (DirectSound behind soundx.dll) ----
// A mixer of sample voices and streams. Nothing here touches game state: playback runs on the
// audio thread (SDL_AUDIODRIVER chooses the device: dummy, disk, alsa...).
struct Pcm {
    std::vector<int16_t> samples;  // interleaved
    int32_t channels = 1;
    int32_t rate = 22050;
    uint32_t ms() const { return rate ? (uint32_t)((uint64_t)samples.size() / channels * 1000 / rate) : 0; }
};
struct VoiceParams {
    float volume = 1.0f;  // linear gain
    float pan = 0.0f;  // -1 left .. +1 right
    float pitch = 1.0f;  // playback rate factor
    bool loop = false;
};
// Opens the audio device; false (and every call below a no-op) when disabled or unavailable.
bool audio_init(bool enabled);
void audio_shutdown();
bool audio_enabled();
// Starts a voice; returns its handle (> 0), 0 when nothing plays.
int32_t audio_play(const std::shared_ptr<const Pcm>& pcm, const VoiceParams& params);
void audio_set(int32_t voice, const VoiceParams& params);
// Stops a voice now, or fades it out over fade_ms.
void audio_stop(int32_t voice, int32_t fade_ms = 0);
bool audio_playing(int32_t voice);
// Streams: fn runs on the audio thread and adds `frames` stereo frames (interleaved floats, full
// scale 1.0) at `rate` Hz to out. Returns false when the stream has ended (it is then removed).
using StreamFn = bool (*)(void* user, float* out, int32_t frames, int32_t rate);
int32_t audio_stream_open(StreamFn fn, void* user);
void audio_stream_close(int32_t id);
// Excludes the audio thread (for state shared with stream callbacks).
void audio_lock();
void audio_unlock();

}  // namespace platform

// Windows virtual-key codes (VK_*) used by the original's key handlers. Named VKEY_* so they do
// not collide with <windows.h> macros in a native Windows build.
enum : int32_t {
    VKEY_BACK = 0x08,
    VKEY_TAB = 0x09,
    VKEY_RETURN = 0x0D,
    VKEY_SHIFT = 0x10,
    VKEY_CONTROL = 0x11,
    VKEY_MENU = 0x12,
    VKEY_ESCAPE = 0x1B,
    VKEY_SPACE = 0x20,
    VKEY_PRIOR = 0x21,
    VKEY_NEXT = 0x22,
    VKEY_END = 0x23,
    VKEY_HOME = 0x24,
    VKEY_LEFT = 0x25,
    VKEY_UP = 0x26,
    VKEY_RIGHT = 0x27,
    VKEY_DOWN = 0x28,
    VKEY_INSERT = 0x2D,
    VKEY_DELETE = 0x2E,
    VKEY_NUMPAD0 = 0x60,
    VKEY_MULTIPLY = 0x6A,
    VKEY_ADD = 0x6B,
    VKEY_SUBTRACT = 0x6D,
    VKEY_DIVIDE = 0x6F,
    VKEY_F1 = 0x70,
    VKEY_F12 = 0x7B,
    VKEY_OEM_PLUS = 0xBB,
    VKEY_OEM_MINUS = 0xBD,
};
