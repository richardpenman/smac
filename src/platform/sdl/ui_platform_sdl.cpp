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

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace platform {
namespace {

SDL_Window* g_window = nullptr;
SDL_Renderer* g_renderer = nullptr;
SDL_Texture* g_texture = nullptr;
std::vector<uint32_t> g_rgb;  // frame converted to ARGB8888
int32_t g_width = 0, g_height = 0;
std::string g_game_dir = ".";

uint32_t argb(const Rgb& c) { return 0xFF000000u | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b; }

void convert(const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height, const Rgb* palette) {
    uint32_t lut[256];
    for (int32_t i = 0; i < 256; i++) lut[i] = argb(palette[i]);
    g_rgb.resize((size_t)width * height);
    for (int32_t y = 0; y < height; y++) {
        const uint8_t* src = pixels + (size_t)y * pitch;
        uint32_t* dst = &g_rgb[(size_t)y * width];
        for (int32_t x = 0; x < width; x++) dst[x] = lut[src[x]];
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
    case SDLK_RETURN: case SDLK_KP_ENTER: return VKEY_RETURN;
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
    case SDLK_PLUS: case SDLK_EQUALS: return VKEY_OEM_PLUS;
    case SDLK_MINUS: return VKEY_OEM_MINUS;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VKEY_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return VKEY_CONTROL;
    case SDLK_LALT: case SDLK_RALT: return VKEY_MENU;
    default: return 0;
    }
}

int32_t to_mods(Uint16 m) {
    return ((m & KMOD_SHIFT) ? MOD_SHIFT : 0) | ((m & KMOD_CTRL) ? MOD_CTRL : 0) | ((m & KMOD_ALT) ? MOD_ALT : 0);
}

// Window coordinates -> client (frame) coordinates when the window is scaled.
void to_client(int32_t wx, int32_t wy, int32_t* x, int32_t* y) {
    int32_t ww = g_width, wh = g_height;
    if (g_window) SDL_GetWindowSize(g_window, &ww, &wh);
    *x = ww > 0 ? wx * g_width / ww : wx;
    *y = wh > 0 ? wy * g_height / wh : wy;
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
struct FontEntry {
    TTF_Font* font = nullptr;
    int32_t pixels = 0;  // em height (-lfHeight)
};
std::map<int32_t, FontEntry> g_fonts;
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
    if (f == "arial narrow") {
        names.push_back(bold ? (italic ? "arialnbi.ttf" : "arialnb.ttf") : (italic ? "arialni.ttf" : "arialn.ttf"));
        names.push_back(bold ? "LiberationSansNarrow-Bold.ttf" : "LiberationSansNarrow-Regular.ttf");
    }
    names.push_back(bold ? (italic ? "arialbi.ttf" : "arialbd.ttf") : (italic ? "ariali.ttf" : "arial.ttf"));
    names.push_back(bold ? (italic ? "Arial Bold Italic.ttf" : "Arial Bold.ttf") : (italic ? "Arial Italic.ttf" : "Arial.ttf"));
    names.push_back(bold ? (italic ? "LiberationSans-BoldItalic.ttf" : "LiberationSans-Bold.ttf")
                         : (italic ? "LiberationSans-Italic.ttf" : "LiberationSans-Regular.ttf"));
    names.push_back(bold ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf");
    names.push_back(bold ? "arialnb.ttf" : "arialn.ttf");
    const char* dirs[] = {
        nullptr,  // game directory
        "/usr/share/fonts/truetype/msttcorefonts", "/usr/share/fonts/truetype/liberation",
        "/usr/share/fonts/truetype/liberation2", "/usr/share/fonts/liberation", "/usr/share/fonts/TTF",
        "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/dejavu", "/Library/Fonts",
        "/System/Library/Fonts/Supplemental", "C:/Windows/Fonts",
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
    g_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                                SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
    for (auto& f : g_fonts)
        if (f.second.font) TTF_CloseFont(f.second.font);
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

bool save_bmp(const char* path, const uint8_t* pixels, int32_t pitch, int32_t width, int32_t height,
              const Rgb* palette) {
    convert(pixels, pitch, width, height, palette);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(g_rgb.data(), width, height, 32, width * 4,
                                                        SDL_PIXELFORMAT_ARGB8888);
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

int32_t font_open(const char* face, int32_t pixels, bool bold, bool italic, bool underline) {
    std::string path = find_font_file(face, bold, italic);
    if (path.empty() || pixels <= 0) return 0;
    TTF_Font* f = TTF_OpenFont(path.c_str(), pixels);
    if (!f) return 0;
    TTF_SetFontHinting(f, TTF_HINTING_MONO);
    int style = TTF_STYLE_NORMAL;
    // Bold/italic come from the file when a matching one exists; synthesise them otherwise.
    if (bold && path.find("old") == std::string::npos && path.find("bd") == std::string::npos &&
        path.find("nb") == std::string::npos)
        style |= TTF_STYLE_BOLD;
    if (italic && path.find("talic") == std::string::npos && path.find("i.ttf") == std::string::npos)
        style |= TTF_STYLE_ITALIC;
    if (underline) style |= TTF_STYLE_UNDERLINE;
    TTF_SetFontStyle(f, style);
    int32_t h = g_next_font++;
    g_fonts[h].font = f;
    g_fonts[h].pixels = pixels;
    return h;
}

void font_close(int32_t handle) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end()) return;
    if (it->second.font) TTF_CloseFont(it->second.font);
    g_fonts.erase(it);
}

bool font_metrics(int32_t handle, FontMetrics* out) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end()) return false;
    TTF_Font* f = it->second.font;
    out->ascent = TTF_FontAscent(f);
    out->descent = -TTF_FontDescent(f);
    out->height = out->ascent + out->descent;
    // GDI: lfHeight = -pixels gives an em height of `pixels`; the rest of the cell is internal leading.
    out->internal_leading = out->height - it->second.pixels;
    if (out->internal_leading < 0) out->internal_leading = 0;
    out->external_leading = TTF_FontLineSkip(f) - TTF_FontHeight(f);
    if (out->external_leading < 0) out->external_leading = 0;
    return true;
}

int32_t font_text_width(int32_t handle, const char* text, int32_t len) {
    auto it = g_fonts.find(handle);
    if (it == g_fonts.end() || !text) return 0;
    std::string s(text, (size_t)len);
    int w = 0, h = 0;
    TTF_SizeText(it->second.font, s.c_str(), &w, &h);
    return w;
}

bool font_render(int32_t handle, const char* text, int32_t len, std::vector<uint8_t>* mask, int32_t* w,
                 int32_t* h) {
    auto it = g_fonts.find(handle);
    *w = *h = 0;
    if (it == g_fonts.end() || !text || len <= 0) return false;
    std::string s(text, (size_t)len);
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* surf = TTF_RenderText_Solid(it->second.font, s.c_str(), white);
    if (!surf) return false;
    *w = surf->w;
    *h = surf->h;
    mask->assign((size_t)surf->w * surf->h, 0);
    SDL_LockSurface(surf);
    for (int32_t y = 0; y < surf->h; y++) {
        const uint8_t* row = (const uint8_t*)surf->pixels + (size_t)y * surf->pitch;
        for (int32_t x = 0; x < surf->w; x++) (*mask)[(size_t)y * surf->w + x] = row[x] ? 1 : 0;
    }
    SDL_UnlockSurface(surf);
    SDL_FreeSurface(surf);
    return true;
}

}  // namespace platform
