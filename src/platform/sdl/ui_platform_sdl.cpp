// SDL2 backend of the native UI platform layer (platform/ui_platform.h).
//
// Replaces what the original takes from Windows: the DirectDraw/GDI window and 8-bit palettized
// screen (an SDL window whose texture receives the 8-bit frame converted through the palette),
// the window procedure's input messages (SDL events translated to Windows virtual-key codes),
// timeGetTime (SDL_GetTicks) and GDI text output (SDL_ttf, rendered without antialiasing as GDI
// does on an 8-bit DIB). Portable to Linux, macOS and Windows; nothing X11-specific.
#include "platform/ui_platform.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace platform {
namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
std::vector<uint32_t> g_rgb;  // frame converted to ARGB8888
int32_t g_width = 0, g_height = 0;
std::string g_game_dir = ".";

uint32_t argb(const Rgb& c) { return 0xFF000000u | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b; }

const uint8_t* g_layers = nullptr;  // set_layer_palettes
int32_t g_layer_pitch = 0;
std::vector<std::vector<uint32_t>> g_layer_luts;

void convert(const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height, const Rgb* palette) {
    uint32_t lut[256];
    for (int32_t i = 0; i < 256; i++) lut[i] = argb(palette[i]);
    g_rgb.resize((size_t)width * height);
    int32_t n = (int32_t)g_layer_luts.size();
    for (int32_t y = 0; y < height; y++) {
        const uint8_t* src = pixels + (size_t)y * pitch;
        uint32_t* dst = &g_rgb[(size_t)y * width];
        const uint8_t* lay = g_layers ? g_layers + (size_t)y * g_layer_pitch : nullptr;
        for (int32_t x = 0; x < width; x++) {
            int32_t l = lay ? lay[x] : 0;
            dst[x] = (l > 0 && l < n && !g_layer_luts[l].empty()) ? g_layer_luts[l][src[x]] : lut[src[x]];
        }
    }
}

// SDL keycode -> Windows virtual-key code.
int32_t to_vk(SDL_Keycode k) {
    if (k >= 'a' && k <= 'z') return (int32_t)(k - 'a' + 'A');
    if (k >= '0' && k <= '9') return (int32_t)k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return VKEY_F1 + (int32_t)(k - SDLK_F1);
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return VKEY_NUMPAD0 + 1 + (int32_t)(k - SDLK_KP_1);
    switch (k) {
    case SDLK_BACKSPACE: return VKEY_BACK;
    case SDLK_TAB: return VKEY_TAB;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return VKEY_RETURN;
    case SDLK_ESCAPE: return VKEY_ESCAPE;
    case SDLK_SPACE: return VKEY_SPACE;
    case SDLK_PAGEUP: return VKEY_PRIOR;
    case SDLK_PAGEDOWN: return VKEY_NEXT;
    case SDLK_END: return VKEY_END;
    case SDLK_HOME: return VKEY_HOME;
    case SDLK_LEFT: return VKEY_LEFT;
    case SDLK_UP: return VKEY_UP;
    case SDLK_RIGHT: return VKEY_RIGHT;
    case SDLK_DOWN: return VKEY_DOWN;
    case SDLK_INSERT: return VKEY_INSERT;
    case SDLK_DELETE: return VKEY_DELETE;
    case SDLK_KP_0: return VKEY_NUMPAD0;
    case SDLK_KP_MULTIPLY: return VKEY_MULTIPLY;
    case SDLK_KP_PLUS: return VKEY_ADD;
    case SDLK_KP_MINUS: return VKEY_SUBTRACT;
    case SDLK_KP_DIVIDE: return VKEY_DIVIDE;
    case SDLK_PLUS:
    case SDLK_EQUALS: return VKEY_OEM_PLUS;
    case SDLK_MINUS: return VKEY_OEM_MINUS;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return VKEY_SHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return VKEY_CONTROL;
    case SDLK_LALT:
    case SDLK_RALT: return VKEY_MENU;
    default: return 0;
    }
}

int32_t to_mods(Uint16 m) {
    return ((m & KMOD_SHIFT) ? MOD_SHIFT : 0) | ((m & KMOD_CTRL) ? MOD_CTRL : 0) | ((m & KMOD_ALT) ? MOD_ALT : 0);
}

// The mouse's client (frame) coordinates. The renderer's logical size (SDL_RenderSetLogicalSize)
// already maps the mouse events of a scaled or letterboxed window (a resized window, macOS
// fullscreen) to the frame's coordinates; scaling them again by the window's size sent every click
// of a window not 1024x768 elsewhere (the console's MENU button missed). Only the letterbox
// margins are clamped.
void to_client(int32_t wx, int32_t wy, int32_t* x, int32_t* y) {
    *x = std::clamp(wx, 0, std::max(0, g_width - 1));
    *y = std::clamp(wy, 0, std::max(0, g_height - 1));
}

bool translate(const SDL_Event& e, Event* ev) {
    *ev = Event{};
    switch (e.type) {
    case SDL_QUIT: ev->type = EV_QUIT; return true;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        ev->type = e.type == SDL_KEYDOWN ? EV_KEY_DOWN : EV_KEY_UP;
        ev->key = to_vk(e.key.keysym.sym);
        ev->mods = to_mods(e.key.keysym.mod);
        // A held Alt (macOS: Option) repeats; the menu column toggles on each Alt press.
        if (e.key.repeat && ev->key == VKEY_MENU) return false;
        return ev->key != 0;
    case SDL_TEXTINPUT:
        if ((uint8_t)e.text.text[0] < 0x80 && e.text.text[0]) {
            ev->type = EV_CHAR;
            ev->key = (uint8_t)e.text.text[0];
            return true;
        }
        return false;
    case SDL_MOUSEMOTION:
        ev->type = EV_MOUSE_MOVE;
        to_client(e.motion.x, e.motion.y, &ev->x, &ev->y);
        return true;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        bool down = e.type == SDL_MOUSEBUTTONDOWN;
        if (e.button.button == SDL_BUTTON_LEFT) ev->type = down ? EV_LBUTTON_DOWN : EV_LBUTTON_UP;
        else if (e.button.button == SDL_BUTTON_RIGHT) ev->type = down ? EV_RBUTTON_DOWN : EV_RBUTTON_UP;
        else return false;
        to_client(e.button.x, e.button.y, &ev->x, &ev->y);
        ev->mods = to_mods(SDL_GetModState());
        return true;
    }
    case SDL_MOUSEWHEEL:
        ev->type = EV_MOUSE_WHEEL;
        ev->key = e.wheel.y > 0 ? 1 : -1;
        return e.wheel.y != 0;
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            ev->type = EV_EXPOSE;
            return true;
        }
        return false;
    default: return false;
    }
}

// ---- fonts ----
// A glyph's ink as GDI draws it on an 8-bit DIB: 1 = ink, rows of w, the cell's rows (baseline
// at the cell's ascent), x relative to the pen position.
struct GlyphMask {
    int32_t x0 = 0, w = 0, advance = 0;
    std::vector<uint8_t> bits;
};
struct FontEntry {
    TTF_Font* font = nullptr;
    int32_t pixels = 0;  // em height (-lfHeight)
    // GDI text metrics (computed as GDI does from the VDMX, OS/2 and hhea tables).
    int32_t ascent = 0, descent = 0, internal_leading = 0, external_leading = 0;
    int32_t refs = 1;  // font_open calls of the same font (one handle; font_close releases one)
    std::string key;
};

// The font's vertical metrics in font units (OS/2 usWinAscent/usWinDescent, hhea ascender,
// descender and line gap, head unitsPerEm) and, when its VDMX table has an entry for the size,
// the pixel yMax / yMin GDI takes as the ascent and descent. False when the file can't be parsed.
struct FontUnits {
    int32_t upem = 0, win_ascent = 0, win_descent = 0, ascender = 0, descender = 0, line_gap = 0;
    bool vdmx = false;
    int32_t vdmx_ymax = 0, vdmx_ymin = 0;
};
bool read_font_units(const std::vector<uint8_t>& d, int32_t ppem, FontUnits* u) {
    auto u8 = [&](size_t o) { return o < d.size() ? (int32_t)d[o] : 0; };
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? (int32_t)(d[o] << 8 | d[o + 1]) : 0; };
    auto s16 = [&](size_t o) { return (int32_t)(int16_t)u16(o); };
    auto u32 = [&](size_t o) {
        return o + 4 <= d.size() ? (uint32_t)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) : 0u;
    };
    int32_t tables = u16(4);
    size_t head = 0, hhea = 0, os2 = 0, vdmx = 0;
    for (int32_t i = 0; i < tables; i++) {
        size_t rec = 12 + (size_t)i * 16;
        uint32_t tag = u32(rec), off = u32(rec + 8);
        if (tag == 0x68656164) head = off;  // 'head'
        else if (tag == 0x68686561) hhea = off;  // 'hhea'
        else if (tag == 0x4F532F32) os2 = off;  // 'OS/2'
        else if (tag == 0x56444D58) vdmx = off;  // 'VDMX'
    }
    if (!head || !hhea || !os2) return false;
    u->upem = u16(head + 18);
    u->ascender = s16(hhea + 4);
    u->descender = s16(hhea + 6);
    u->line_gap = s16(hhea + 8);
    u->win_ascent = u16(os2 + 74);
    u->win_descent = u16(os2 + 76);
    // VDMX (GDI's load_VDMX for a character height, lfHeight < 0): the first ratio record for a
    // 1:1 device (or the catch-all 0:0:0), its group's entry for exactly this ppem when the group's
    // range holds it.
    if (vdmx) {
        int32_t ratios = u16(vdmx + 4);
        int32_t group = -1;
        for (int32_t i = 0; i < ratios; i++) {
            size_t r = vdmx + 6 + (size_t)i * 4;
            int32_t xr = u8(r + 1), ys = u8(r + 2), ye = u8(r + 3);
            if ((xr == 0 && ys == 0 && ye == 0) || (xr == 1 && ys <= 1 && ye >= 1)) {
                group = u16(vdmx + 6 + (size_t)ratios * 4 + (size_t)i * 2);
                break;
            }
        }
        if (group >= 0) {
            size_t g = vdmx + (size_t)group;
            int32_t recs = u16(g), start = u8(g + 2), end = u8(g + 3);
            if (ppem >= start && ppem <= end)
                for (int32_t i = 0; i < recs; i++) {
                    size_t e = g + 4 + (size_t)i * 6;
                    int32_t h = u16(e);
                    if (h > ppem) break;
                    if (h == ppem) {
                        u->vdmx = true;
                        u->vdmx_ymax = s16(e + 2);
                        u->vdmx_ymin = s16(e + 4);
                        break;
                    }
                }
        }
    }
    return u->upem > 0;
}

// FreeType's FT_MulFix (16.16, rounded), which Wine's GDI scales the font units with.
int64_t ft_mulfix(int64_t a, int64_t b) {
    int64_t p = a * b;
    return p >= 0 ? (p + 0x8000) >> 16 : -((-p + 0x8000) >> 16);
}
std::map<int32_t, FontEntry> g_fonts;
// The open TrueType instances by file, size and style, shared by every handle of that font. Each
// TTF_Font keeps its file open, and the game creates fonts freely (GDI fonts are cheap): many of
// the reconstructed windows' Font objects are locals whose handles are never closed (the original's
// Font destructors), and a battle report alone made three a frame. Opening a file per handle ran
// out of file descriptors on macOS (256 by default) after a few battles, and every later
// text_open failed: blank 100-pixel popups. The instances stay open until video_shutdown, read
// from memory (no file kept open), and the same font opened again gets the same handle (counted),
// so a Font never closed costs nothing more.
std::map<std::string, TTF_Font*> g_ttf_cache;
// The glyphs drawn so far, by instance and character code (font_render).
std::map<std::pair<TTF_Font*, uint32_t>, GlyphMask> g_glyphs;
std::map<std::string, int32_t> g_font_handles;  // the open handle of each font (file, size, style)
// The font files' bytes: the instances read them from memory, holding no file open.
std::map<std::string, std::vector<uint8_t>> g_ttf_files;
const std::vector<uint8_t>& font_file(const std::string& path) {
    auto it = g_ttf_files.find(path);
    if (it == g_ttf_files.end()) {
        std::vector<uint8_t> bytes;
        if (FILE* fp = std::fopen(path.c_str(), "rb")) {
            uint8_t buf[65536];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            std::fclose(fp);
        }
        it = g_ttf_files.emplace(path, std::move(bytes)).first;
    }
    return it->second;
}
TTF_Font* open_ttf(const std::string& path, int32_t pixels) {
    const std::vector<uint8_t>& bytes = font_file(path);
    if (bytes.empty()) return nullptr;
    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), (int)bytes.size());
    return rw ? TTF_OpenFontRW(rw, 1, pixels) : nullptr;
}
int32_t g_next_font = 1;

bool file_ok(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

// GDI face name -> font file. Arial is a Windows system font; Wine substitutes the metrically
// compatible Liberation Sans, which is what the oracle screenshots show. The game ships Arial
// Narrow (arialn*.ttf), used as the fallback.
std::string find_font_file(const char* face, bool bold, bool italic) {
    std::string f = face ? face : "";
    for (auto& c : f) c = (char)std::tolower((unsigned char)c);
    std::vector<std::string> names;
    // The title screen's credits font (topmenu.cpp), which the game's installer puts in the system's
    // fonts (the game asks for it by name only).
    if (f == "alpha centauri") names.push_back("ALPHC___.TTF");
    if (f == "arial narrow") {
        names.push_back(bold ? (italic ? "arialnbi.ttf" : "arialnb.ttf") : (italic ? "arialni.ttf" : "arialn.ttf"));
        names.push_back(bold ? "LiberationSansNarrow-Bold.ttf" : "LiberationSansNarrow-Regular.ttf");
    }
    names.push_back(bold ? (italic ? "arialbi.ttf" : "arialbd.ttf") : (italic ? "ariali.ttf" : "arial.ttf"));
    names.push_back(bold ? (italic ? "Arial Bold Italic.ttf" : "Arial Bold.ttf")
                         : (italic ? "Arial Italic.ttf" : "Arial.ttf"));
    names.push_back(bold ? (italic ? "LiberationSans-BoldItalic.ttf" : "LiberationSans-Bold.ttf")
                         : (italic ? "LiberationSans-Italic.ttf" : "LiberationSans-Regular.ttf"));
    names.push_back(bold ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf");
    names.push_back(bold ? "arialnb.ttf" : "arialn.ttf");
    const char* dirs[] = {
        nullptr,  // game directory
        "/usr/share/fonts/truetype/msttcorefonts",
        "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/truetype/liberation2",
        "/usr/share/fonts/liberation",
        "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype/dejavu",
        "/usr/share/fonts/dejavu",
        "/Library/Fonts",
        "/System/Library/Fonts/Supplemental",
        "C:/Windows/Fonts",
    };
    for (const auto& n : names) {
        for (const char* d : dirs) {
            std::string p = (d ? std::string(d) : g_game_dir) + "/" + n;
            if (file_ok(p)) return p;
        }
    }
    return "";
}

}  // namespace

void set_game_dir(const std::string& dir) { g_game_dir = dir; }

bool video_init(const char* title, int32_t width, int32_t height, bool hidden) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return false;
    }
    g_width = width;
    g_height = height;
    if (hidden) return true;  // offscreen: frames are only saved
    g_window =
        SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    // No vsync: the game paces its animations itself (window slides 20 ms a frame, unit moves 16,
    // the menu's 10), as the original's blits did. A present waiting for the display's refresh
    // (16.7 ms at 60 Hz) made those frames land 17 or 33 ms apart in turn: jerky slides and moves.
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED);
    if (!g_renderer) g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(g_renderer, width, height);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    SDL_StartTextInput();
    return g_texture != nullptr;
}

void video_shutdown() {
    g_glyphs.clear();
    for (auto& f : g_ttf_cache)
        if (f.second) TTF_CloseFont(f.second);
    g_ttf_cache.clear();
    g_font_handles.clear();
    g_fonts.clear();
    if (g_texture) SDL_DestroyTexture(g_texture);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    g_texture = nullptr;
    g_renderer = nullptr;
    g_window = nullptr;
    TTF_Quit();
    SDL_Quit();
}

void video_present(const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height, const Rgb* palette) {
    if (!g_texture) return;
    convert(pixels, pitch, width, height, palette);
    SDL_UpdateTexture(g_texture, nullptr, g_rgb.data(), width * 4);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

void video_present_rgb(const uint32_t* pixels, int32_t width, int32_t height) {
    static SDL_Texture* tex = nullptr;
    static int32_t tw = 0, th = 0;
    if (!g_renderer || width <= 0 || height <= 0) return;
    if (!tex || tw != width || th != height) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
        tw = width;
        th = height;
        if (!tex) return;
    }
    SDL_UpdateTexture(tex, nullptr, pixels, width * 4);
    // Scaled to the window's width or height (whichever fits first), centred on black.
    int32_t w = g_width, h = (int32_t)((int64_t)height * g_width / width);
    if (h > g_height) {
        h = g_height;
        w = (int32_t)((int64_t)width * g_height / height);
    }
    SDL_Rect dst{(g_width - w) / 2, (g_height - h) / 2, w, h};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, tex, nullptr, &dst);
    SDL_RenderPresent(g_renderer);
}

void set_layer_palettes(const uint8_t* layers, int32_t pitch, const Rgb* const* palettes, int32_t count) {
    g_layers = layers;
    g_layer_pitch = pitch;
    g_layer_luts.clear();
    if (!layers) return;
    g_layer_luts.resize((size_t)count);
    for (int32_t l = 1; l < count; l++) {
        if (!palettes[l]) continue;
        g_layer_luts[l].resize(256);
        for (int32_t i = 0; i < 256; i++) g_layer_luts[l][i] = argb(palettes[l][i]);
    }
}

bool save_bmp(const char* path, const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height,
              const Rgb* palette) {
    convert(pixels, pitch, width, height, palette);
    SDL_Surface* s =
        SDL_CreateRGBSurfaceWithFormatFrom(g_rgb.data(), width, height, 32, width * 4, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    SDL_Surface* rgb24 = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
    bool ok = rgb24 && SDL_SaveBMP(rgb24, path) == 0;
    if (rgb24) SDL_FreeSurface(rgb24);
    SDL_FreeSurface(s);
    return ok;
}

bool poll_event(Event* ev) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
        if (translate(e, ev)) return true;
    return false;
}

bool wait_event(Event* ev, int32_t timeout_ms) {
    SDL_Event e;
    uint32_t end = SDL_GetTicks() + (uint32_t)timeout_ms;
    for (;;) {
        int32_t left = (int32_t)(end - SDL_GetTicks());
        if (left < 0) left = 0;
        if (!SDL_WaitEventTimeout(&e, left)) return false;
        if (translate(e, ev)) return true;
        if (left == 0) return false;
    }
}

bool key_down(int32_t vk) {
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    switch (vk) {
    case VKEY_SHIFT: return state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];
    case VKEY_CONTROL: return state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_RCTRL];
    case VKEY_MENU: return state[SDL_SCANCODE_LALT] || state[SDL_SCANCODE_RALT];
    default: return false;
    }
}

uint32_t ticks_ms() { return SDL_GetTicks(); }
void delay_ms(uint32_t ms) { SDL_Delay(ms); }

namespace {

// The style a font file has itself (OS/2 fsSelection, else head macStyle): GDI synthesises only
// what the file lacks.
void file_style(const std::vector<uint8_t>& d, bool* bold, bool* italic) {
    *bold = *italic = false;
    auto u16 = [&](size_t o) { return o + 2 <= d.size() ? (int32_t)(d[o] << 8 | d[o + 1]) : 0; };
    auto u32 = [&](size_t o) {
        return o + 4 <= d.size() ? (uint32_t)(d[o] << 24 | d[o + 1] << 16 | d[o + 2] << 8 | d[o + 3]) : 0u;
    };
    size_t head = 0, os2 = 0;
    for (int32_t i = 0, n = u16(4); i < n; i++) {
        size_t rec = 12 + (size_t)i * 16;
        if (u32(rec) == 0x68656164) head = u32(rec + 8);
        else if (u32(rec) == 0x4F532F32) os2 = u32(rec + 8);
    }
    if (os2) {
        int32_t fs = u16(os2 + 62);
        *bold = (fs & 0x20) != 0;
        *italic = (fs & 0x01) != 0;
    } else if (head) {
        int32_t mac = u16(head + 44);
        *bold = (mac & 1) != 0;
        *italic = (mac & 2) != 0;
    }
}

// The ANSI code page (cp1252, GDI's ANSI_CHARSET) to Unicode: 0x80..0x9F differ from Latin-1.
uint32_t cp1252(uint8_t c) {
    static const uint16_t k80[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                     0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                     0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                     0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    return c >= 0x80 && c < 0xA0 ? k80[c - 0x80] : c;
}

// One glyph as GDI draws it without antialiasing: FreeType's monochrome hinting (TrueType
// bytecode, the hdmx widths), the ink placed relative to the pen on the cell's rows.
const GlyphMask& glyph(const FontEntry& e, uint8_t ch) {
    uint32_t code = cp1252(ch);
    auto key = std::make_pair(e.font, code);
    auto it = g_glyphs.find(key);
    if (it != g_glyphs.end()) return it->second;
    GlyphMask& g = g_glyphs[key];
    int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
    if (TTF_GlyphMetrics32(e.font, code, &minx, &maxx, &miny, &maxy, &advance) != 0) return g;
    g.advance = advance;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderGlyph32_Solid(e.font, code, white);
    if (!surf) return g;
    // SDL_ttf lays the glyph out from the pen at x = max(0, -minx) on rows with the baseline at
    // TTF_FontAscent; GDI's cell has it at the GDI ascent.
    g.x0 = minx < 0 ? minx : 0;
    g.w = surf->w;
    int32_t cell_h = e.ascent + e.descent;
    int32_t shift = TTF_FontAscent(e.font) - e.ascent;  // surface row = cell row + shift
    g.bits.assign((size_t)g.w * cell_h, 0);
    SDL_LockSurface(surf);
    for (int32_t y = 0; y < cell_h; y++) {
        int32_t sy = y + shift;
        if (sy < 0 || sy >= surf->h) continue;
        const uint8_t* row = (const uint8_t*)surf->pixels + (size_t)sy * surf->pitch;
        for (int32_t x = 0; x < g.w; x++) g.bits[(size_t)y * g.w + x] = row[x] ? 1 : 0;
    }
    SDL_UnlockSurface(surf);
    SDL_FreeSurface(surf);
    return g;
}

}  // namespace

int32_t font_open(const char* face, int32_t pixels, bool bold, bool italic, bool underline) {
    std::string path = find_font_file(face, bold, italic);
    if (path.empty() || pixels <= 0) return 0;
    const std::vector<uint8_t>& bytes = font_file(path);
    int style = TTF_STYLE_NORMAL;
    // Bold / italic come from the file when it has them; synthesised otherwise (as GDI does).
    bool file_bold = false, file_italic = false;
    file_style(bytes, &file_bold, &file_italic);
    if (bold && !file_bold) style |= TTF_STYLE_BOLD;
    if (italic && !file_italic) style |= TTF_STYLE_ITALIC;
    if (underline) style |= TTF_STYLE_UNDERLINE;
    std::string key = path + "|" + std::to_string(pixels) + "|" + std::to_string(style);
    auto open = g_font_handles.find(key);
    if (open != g_font_handles.end()) {
        g_fonts[open->second].refs++;
        return open->second;
    }
    TTF_Font* f = nullptr;
    auto cached = g_ttf_cache.find(key);
    if (cached != g_ttf_cache.end()) {
        f = cached->second;
    } else {
        f = open_ttf(path, pixels);
        if (!f) return 0;
        // GDI on an 8-bit DIB draws and measures with the monochrome hinting (Windows' widths are
        // the font's hdmx ones, which FreeType's v35 bytecode hinting gives too); no kerning.
        TTF_SetFontHinting(f, TTF_HINTING_MONO);
        TTF_SetFontKerning(f, 0);
        TTF_SetFontStyle(f, style);
        g_ttf_cache[key] = f;
    }
    int32_t h = g_next_font++;
    g_font_handles[key] = h;
    FontEntry& e = g_fonts[h];
    e.key = key;
    e.font = f;
    e.pixels = pixels;
    FontUnits u;
    if (read_font_units(bytes, pixels, &u)) {
        // GDI's text metrics for lfHeight = -pixels (Wine's get_text_metrics, the same values as
        // Windows'): the VDMX entry of this ppem when the font has one, otherwise the OS/2 Windows
        // ascent and descent scaled by MulDiv(ppem, 0x10000, unitsPerEm) and FT_MulFix.
        int64_t scale = ((int64_t)pixels * 65536 + u.upem / 2) / u.upem;
        auto px = [&](int64_t v) { return (int32_t)ft_mulfix(v, scale); };
        if (u.vdmx) {
            e.ascent = u.vdmx_ymax;
            e.descent = -u.vdmx_ymin;
            e.internal_leading = e.ascent + e.descent - pixels;
        } else {
            e.ascent = px(u.win_ascent);
            e.descent = px(u.win_descent);
            e.internal_leading = px(u.win_ascent + u.win_descent - u.upem);
        }
        int64_t gap = u.line_gap - ((u.win_ascent + u.win_descent) - (u.ascender - u.descender));
        e.external_leading = gap > 0 ? px(gap) : 0;
    } else {
        e.ascent = TTF_FontAscent(f);
        e.descent = -TTF_FontDescent(f);
    }
    return h;
}

void font_close(int32_t handle) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end()) return;
    if (--it->second.refs > 0) return;
    g_font_handles.erase(it->second.key);
    g_fonts.erase(it);  // the TTF instances stay shared (g_ttf_cache)
}

bool font_metrics(int32_t handle, FontMetrics* out) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end()) return false;
    const FontEntry& e = it->second;
    out->ascent = e.ascent;
    out->descent = e.descent;
    out->height = e.ascent + e.descent;
    out->internal_leading = e.internal_leading;
    out->external_leading = e.external_leading;
    return true;
}

int32_t font_text_width(int32_t handle, const char* text, int32_t len) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end() || !text) return 0;
    // GetTextExtentPoint32: the sum of the glyphs' advances, the same ones font_render places the
    // glyphs with, so a text is exactly as wide as measured.
    int32_t w = 0;
    for (int32_t i = 0; i < len; i++) w += glyph(it->second, (uint8_t)text[i]).advance;
    return w;
}

bool font_render(int32_t handle, const char* text, int32_t len, std::vector<uint8_t>* mask, int32_t* w, int32_t* h) {
    auto it = g_fonts.find(handle);
    *w = *h = 0;
    if (it == g_fonts.end() || !text || len <= 0) return false;
    const FontEntry& e = it->second;
    // TextOut: each glyph at the pen, the pen advanced by the glyph's advance. The mask runs from
    // the pen's start to the furthest ink or advance (ink left of the start is dropped).
    int32_t pen = 0, right = 0;
    for (int32_t i = 0; i < len; i++) {
        const GlyphMask& g = glyph(e, (uint8_t)text[i]);
        right = std::max(right, std::max(pen + g.advance, pen + g.x0 + g.w));
        pen += g.advance;
    }
    int32_t cell_h = e.ascent + e.descent;
    if (right <= 0 || cell_h <= 0) return false;
    *w = right;
    *h = cell_h;
    mask->assign((size_t)right * cell_h, 0);
    pen = 0;
    for (int32_t i = 0; i < len; i++) {
        const GlyphMask& g = glyph(e, (uint8_t)text[i]);
        for (int32_t y = 0; y < cell_h && !g.bits.empty(); y++)
            for (int32_t x = 0; x < g.w; x++) {
                int32_t mx = pen + g.x0 + x;
                if (mx >= 0 && g.bits[(size_t)y * g.w + x]) (*mask)[(size_t)y * right + mx] = 1;
            }
        pen += g.advance;
    }
    return true;
}

}  // namespace platform
