// Window layer of smacx (see win.h): screen, message pump, keyboard, placeholder console panel.
#include "ui/infowin.h"
#include "ui/tutwin.h"
#include "ui/diplowin.h"
#include "ui/win.h"
#include "ui/pulldown.h"
#include "ui/texture.h"
#include "ui/sprites.h"
#include "ui/sound.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>

#include "engine/clock.h"
#include "engine/random.h"
#include "engine/save.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "ui/console.h"
#include "ui/basewin.h"
#include "ui/console_frame.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/techwin.h"
#include "ui/topmenu.h"

bool g_ui_exit_when_script_done = false;
std::string g_ui_outdir = ".";
int32_t g_ui_shots = 0;

namespace {

// Height of the console panel below the main view (the original's console/world window layout,
// MapWin_UNK5 0x46FA20, is not reconstructed; 168 pixels match the oracle's view at 1024x768).
constexpr int32_t kPanelHeight = 168;

Buffer g_canvas;  // the window
Buffer g_view;    // the main view's GraphicWin canvas
bool g_ready = false;
bool g_map_dirty = true;
bool g_dirty = true;
uint32_t g_last_present = 0;
std::deque<ScriptedKey> g_script;
bool g_script_given = false;
std::deque<platform::Event> g_input;  // input events read while pumping non-input messages
std::string g_message;
Font g_font;
bool g_font_ok = false;

// GetKeyState bits of the modifiers while a scripted key is dispatched.
int32_t g_script_mods = 0;
// The tile under a go-to drag (ui_set_drag_target).
int32_t g_drag_x = -1, g_drag_y = -1;

// Desktop mode (the opening screens, before a game): no map view nor console.
bool g_desktop = false;
// Palette layers (ui_layer_mark): per pixel of the screen, the palette it is shown through.
std::vector<uint8_t> g_layers;
const Palette* g_layer_pals[4] = {};

// ---- keyboard: the original's Win_on_key (0x5F5D10) with TranslateMessage for a US layout ----

// MapVirtualKey(vk, MAPVK_VK_TO_CHAR): non-zero for keys that produce a character. Keypad keys
// (0x60..0x6F) reach the windows through Win_key_down_event (their WM_CHAR are discarded).
bool vk_has_char(int32_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return true;
    switch (vk) {
    case 0x08: case 0x09: case 0x0D: case 0x1B: case 0x20:
    case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: case 0xC0:
    case 0xDB: case 0xDC: case 0xDD: case 0xDE:
        return true;
    default: return false;
    }
}

// WM_CHAR of a key (TranslateMessage); 0: none.
int32_t vk_char(int32_t vk, bool shift, bool ctrl) {
    if (vk >= 'A' && vk <= 'Z') return ctrl ? vk - 0x40 : (shift ? vk : vk + 0x20);
    if (vk >= '0' && vk <= '9') {
        if (ctrl) return 0;
        return shift ? ")!@#$%^&*("[vk - '0'] : vk;
    }
    struct Oem { int32_t vk; char plain, shifted; };
    static const Oem kOem[] = {{0xBA, ';', ':'}, {0xBB, '=', '+'}, {0xBC, ',', '<'}, {0xBD, '-', '_'},
                               {0xBE, '.', '>'}, {0xBF, '/', '?'}, {0xC0, '`', '~'}, {0xDB, '[', '{'},
                               {0xDC, '\\', '|'}, {0xDD, ']', '}'}, {0xDE, '\'', '"'}};
    for (const Oem& o : kOem)
        if (o.vk == vk) return ctrl ? 0 : (shift ? o.shifted : o.plain);
    switch (vk) {
    case 0x08: return ctrl ? 0x7F : 0x08;
    case 0x09: return ctrl ? 0 : 0x09;
    case 0x0D: return ctrl ? 0x0A : 0x0D;
    case 0x1B: return 0x1B;
    case 0x20: return ' ';
    default: return 0;
    }
}

// A key press: WM_KEYDOWN with the modifier bits (Win_on_key), then its WM_CHAR.
void dispatch_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    static const bool log = std::getenv("SMACX_KEYLOG") != nullptr;
    if (log) {
        std::fprintf(stderr, "key %05X (turn %d, unit %d, mode %d)\n", (uint32_t)key, CurrentTurn, MapWinVehA,
                     MapWinInVehMode);
    }
    if (console_menu_key(key)) return;  // the menu bar (Alt, Alt+letter) and an open pull-down
    if (key & 0x40000) return;          // other Alt keys: WM_SYSKEYDOWN
    // Keys without a character reach the focused window from Win_key_down_event as (0, key).
    if (!vk_has_char(vk)) {
        Console_on_key_click(&MainConsole, 0, key);
        return;
    }
    if (vk >= 0x60 && vk <= 0x6F) return;
    int32_t ch = vk_char(vk, key & 0x10000, key & 0x20000);
    if (ch) Console_on_key_click(&MainConsole, ch, key);  // WM_CHAR with the last key down
}

int32_t event_key(const platform::Event& ev) {
    int32_t key = ev.key;
    if (ev.mods & platform::MOD_SHIFT) key |= 0x10000;
    if (ev.mods & platform::MOD_CTRL) key |= 0x20000;
    if (ev.mods & platform::MOD_ALT) key |= 0x40000;
    return key;
}

}  // namespace

// A key as the window layer delivers it (menu commands send their shortcut keys).
void ui_dispatch_key(int32_t key) { dispatch_key(key); }

namespace {

void quit_requested() {
    std::fprintf(stderr, "window closed: quitting\n");
    ControlTurnA = 1;
}

// Runs a scripted command; returns false when it is unknown.
bool run_command(const std::string& cmd) {
    std::string arg = cmd.find(' ') == std::string::npos ? "" : cmd.substr(cmd.find(' ') + 1);
    if (cmd.rfind("shot", 0) == 0) {
        std::string path = arg;
        if (path.empty()) {
            char name[64];
            std::snprintf(name, sizeof(name), "smacx_%03d.bmp", g_ui_shots++);
            path = name;
        }
        if (path[0] != '/') path = g_ui_outdir + "/" + path;
        ui_screenshot(path);
        return true;
    }
    if (cmd.rfind("save", 0) == 0) {
        std::string path = arg[0] == '/' ? arg : g_ui_outdir + "/" + arg;
        // As the harness's save command: the random generators are left as they were.
        uint32_t crt = crt_rand_state(), game = game_random_state();
        int32_t r = save_daemon(path.c_str());
        crt_set_rand_state(crt);
        game_reseed(game);
        std::fprintf(stderr, "save %s (turn %d) -> %d\n", path.c_str(), CurrentTurn, r);
        // The generators and the logic clock as they are now (smacx --rng restores them).
        if (FILE* f = std::fopen((path + ".rng").c_str(), "w")) {
            std::fprintf(f, "crt %u\ngame %u\nclock %u\nturn %d\n", crt_rand_state(), game_random_state(),
                         engine_logic_clock_state(), CurrentTurn);
            std::fclose(f);
        }
        return true;
    }
    if (cmd == "units") {
        // The viewer's units (scripted tests).
        for (int32_t i = 0; i < VehCount; i++) {
            const VEH& v = Vehs[i];
            if (v.faction_id != CurrentPlayerFaction && !std::getenv("SMACX_ALLUNITS")) continue;
            std::fprintf(stderr, "unit %d f%d %s (%d,%d) order %d moves %d state %08X %s\n", i, v.faction_id, Units[v.unit_id].name,
                         v.x, v.y, v.order, v.moves_spent, v.state, veh_ready(i) ? "ready" : "");
        }
        return true;
    }
    if (cmd.rfind("basedump", 0) == 0) {  // test aid: the base window's canvases (basewin.h)
        basewin_dump((arg[0] == '/' ? arg : g_ui_outdir + "/" + arg).c_str());
        return true;
    }
    if (cmd.rfind("basestate", 0) == 0) {  // test aid: the open base's state (basewin.h)
        basewin_log_state();
        return true;
    }
    if (cmd.rfind("base ", 0) == 0) {  // test aid: opens a base window (basewin.h)
        int32_t id = std::atoi(arg.c_str());
        if (id >= 0 && id < BaseCount) BaseWin_zoom(&BaseWin, id, 0);
        return true;
    }
    if (cmd == "menustate") {  // test aid: the open menu column of the opening screens (topmenu.h)
        topmenu_log_state();
        return true;
    }
    if (cmd == "screenstate") {  // test aid: the open screen's texts and state (screenwin.h)
        screens_log_state();
        return true;
    }
    if (cmd == "protostate") {  // test aid: the viewer's prototypes (techwin.h)
        designwin_log_protos();
        return true;
    }
    if (cmd == "techstate") {  // test aid: the viewer's research (techwin.h)
        techwin_log_state();
        return true;
    }
    if (cmd == "diplostate") {  // test aid: the viewer's treaties, energy and technologies (diplowin.h)
        diplowin_log_state();
        return true;
    }
    if (cmd == "infostate") {  // test aid: the info pane's displays and texts (infowin.h)
        infowin_log_state();
        return true;
    }
    if (cmd.rfind("infotick", 0) == 0) {  // test aid: N steps of the info pane's timer (infowin.h)
        for (int32_t i = std::atoi(arg.c_str()); i > 0; i--) infowin_tick();
        return true;
    }
    if (cmd == "factionstate") {  // test aid: the viewer's social choice and allocation (screenwin.h)
        screens_log_faction();
        return true;
    }
    if (cmd == "quit") {
        ControlTurnA = 1;
        return true;
    }
    if (cmd.rfind("playsound", 0) == 0) {  // test aid: decodes and plays a sound file (sound.h)
        auto pcm = sound_load_file(arg.c_str());
        std::fprintf(stderr, "playsound %s: %s", arg.c_str(), pcm ? "" : "not decoded\n");
        if (pcm) {
            std::fprintf(stderr, "%d Hz, %d channels, %u ms\n", pcm->rate, pcm->channels, pcm->ms());
            Wave_unload(&FacilityVoice);
            Wave_load(&FacilityVoice, arg.c_str());
            Wave_play_2(&FacilityVoice);
        }
        return true;
    }
    if (cmd.rfind("musicrender", 0) == 0) {  // test aid: the music's next seconds into a WAV (sound.h)
        size_t c = arg.rfind(':');
        std::string file = c == std::string::npos ? arg : arg.substr(0, c);
        int32_t secs = c == std::string::npos ? 10 : std::atoi(arg.c_str() + c + 1);
        if (file.find('/') != 0) file = g_ui_outdir + "/" + file;
        if (!music_render(file.c_str(), secs)) std::fprintf(stderr, "musicrender: no music\n");
        return true;
    }
    if (cmd.rfind("movie", 0) == 0) {  // test aid: a movie by name (movie.cpp)
        amovie_project_2(arg.c_str());
        return true;
    }
    if (cmd.rfind("popup", 0) == 0) {  // test aid: a popup by label (win_popup.cpp)
        ui_popup_command(arg);
        return true;
    }
    std::fprintf(stderr, "unknown scripted command '%s'\n", cmd.c_str());
    return false;
}

// Reads platform events into g_input; handles the non-input ones. Returns true if any arrived.
bool pump_platform(int32_t timeout_ms) {
    platform::Event ev;
    bool any = false;
    bool got = timeout_ms > 0 ? platform::wait_event(&ev, timeout_ms) : platform::poll_event(&ev);
    while (got) {
        any = true;
        if (ev.type == platform::EV_QUIT) quit_requested();
        else if (ev.type == platform::EV_EXPOSE) g_dirty = true;
        else if (ev.type == platform::EV_KEY_DOWN || ev.type == platform::EV_LBUTTON_DOWN ||
                 ev.type == platform::EV_LBUTTON_UP || ev.type == platform::EV_RBUTTON_DOWN ||
                 ev.type == platform::EV_RBUTTON_UP) {
            // The window procedure's input_callback (Win_OnKey, Win_OnLButtonDown,
            // Win_OnRButtonDown): the music hears every key and click.
            if (ev.type != platform::EV_LBUTTON_UP && ev.type != platform::EV_RBUTTON_UP) input_callback();
            g_input.push_back(ev);
        }
        else if (ev.type == platform::EV_MOUSE_MOVE) {  // the latest position only
            if (!g_input.empty() && g_input.back().type == platform::EV_MOUSE_MOVE) g_input.back() = ev;
            else g_input.push_back(ev);
        }
        got = platform::poll_event(&ev);
    }
    return any;
}

// ---- drawing ----

// The map cursor as MapWin_draw_radius (0x46A2A0) draws it over the cursor tile: in view mode,
// while the human gives orders (kConWaiting), when shown (the blink's kConCursorShown, or 0x23C00),
// the cursor texture (0x787FB8; 0x75B858 zoomed out below -7) over the tile's corners as the last
// redraw of the tile computed them (CursorTileCorners).
void draw_map_cursor() {
    uint8_t* win = MapWins[0];
    uint8_t* con = &MainConsole;
    if (!win || !ui_mapwin_native(win) || MapWinInVehMode) return;
    if (MultiplayerActive && ControlTurnC) return;
    if (!mw(con, kConCursorShown) && !mw(con, kConRedraw)) return;
    if (!mw(con, kConWaiting) && !(MultiplayerActive && (GameState & 2))) return;
    int32_t x = MapWinCursorX[MapWinCursorIndex], y = MapWinCursorY[MapWinCursorIndex];
    if (y < mw(win, kMwOriginY) || y >= mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) + mw(win, kMwOriginY)) return;
    if (!(MapToggleFlat & 1)) {
        if (x < mw(win, kMwOriginX)) x += MapAreaX;
        if (mw(win, kMwColsEven) + mw(win, kMwColsOdd) + mw(win, kMwOriginX) <= x) x -= MapAreaX;
    }
    if (x < mw(win, kMwOriginX) || x >= mw(win, kMwColsEven) + mw(win, kMwColsOdd) + mw(win, kMwOriginX)) return;
    Texture* t = ArtTexture(mw(win, kMwZoom) < -7 ? 0x75B858 : 0x787FB8);
    UiRect clip = {0, 0, g_view.width, g_view.height};
    Buffer_set_clip(&g_canvas, &clip);
    Texture_draw_trans_3(t, &g_canvas, &CursorTileCorners[0][0], 0);
    Buffer_set_clip(&g_canvas, nullptr);
}

}  // namespace

// ---- screen ----

void ui_screen_init(int32_t width, int32_t height) {
    Buffer_ctor(&g_canvas);
    Buffer_init(&g_canvas, width, height, 8, 0);
    Buffer_ctor(&g_view);
    Buffer_init(&g_view, width, std::max(height - kPanelHeight, 100), 8, 0);
    ui_mapwin_attach(&MainConsole, kConsoleVBase, &g_view);
    Font_ctor(&g_font);
    g_font_ok = Font_init(&g_font, "Arial", 14, 0) == 0;
    g_ready = true;
}

Buffer* ui_screen_canvas() { return &g_canvas; }

static Buffer g_desktop_buf;
static bool g_desktop_shown = false;
Buffer* ui_desktop_canvas() {
    if (!g_desktop_buf.data || g_desktop_buf.width != g_canvas.width || g_desktop_buf.height != g_canvas.height) {
        Buffer_ctor(&g_desktop_buf);
        Buffer_init(&g_desktop_buf, g_canvas.width, g_canvas.height, 8, 0);
        Buffer_fill_2(&g_desktop_buf, 9);
    }
    return &g_desktop_buf;
}
void ui_desktop_show(bool on) {
    g_desktop_shown = on;
    g_dirty = true;
}
void ui_set_drag_target(int32_t x, int32_t y) {
    g_drag_x = x;
    g_drag_y = y;
    g_dirty = true;
}
void ui_invalidate_map() { g_map_dirty = g_dirty = true; }
void ui_invalidate() { g_dirty = true; }
bool ui_map_dirty() { return g_map_dirty; }
void ui_set_message(const std::string& text) {
    g_message = text;
    g_dirty = true;
}

void ui_set_desktop(bool on) {
    g_desktop = on;
    g_dirty = true;
}
bool ui_desktop() { return g_desktop; }

void ui_set_layer_palette(int32_t layer, const Palette* palette) {
    if (layer > 0 && layer < 4) g_layer_pals[layer] = palette;
}

void ui_layer_mark(int32_t x0, int32_t y0, const Buffer* src, uint8_t layer, int32_t key) {
    if (!src->data || g_layers.empty()) return;
    for (int32_t y = 0; y < src->height; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= g_canvas.height) continue;
        const uint8_t* s = src->data + (size_t)y * src->stride;
        uint8_t* d = &g_layers[(size_t)sy * g_canvas.stride];
        for (int32_t x = 0; x < src->width; x++) {
            int32_t sx = x0 + x;
            if (sx < 0 || sx >= g_canvas.width || s[x] == key) continue;
            d[sx] = layer;
        }
    }
}

// The palettes of the screen's layers for the platform (none when every pixel is of layer 0).
static void set_layers() {
    bool any = false;
    for (uint8_t l : g_layers)
        if (l) {
            any = true;
            break;
        }
    if (!any) {
        platform::set_layer_palettes(nullptr, 0, nullptr, 0);
        return;
    }
    const platform::Rgb* pals[4] = {};
    for (int32_t i = 1; i < 4; i++)
        if (g_layer_pals[i]) pals[i] = reinterpret_cast<const platform::Rgb*>(g_layer_pals[i]->rgb);
    platform::set_layer_palettes(g_layers.data(), g_canvas.stride, pals, 4);
}

void ui_present() {
    if (!g_ready) return;
    g_layers.assign((size_t)g_canvas.stride * g_canvas.height, 0);
    if (g_desktop) {
        // The opening screens: black behind the windows (the desktop window, the menus).
        std::memset(g_canvas.data, 0, (size_t)g_canvas.stride * g_canvas.height);
    } else {
        if (g_map_dirty) {
            g_map_dirty = false;
            pfncRedrawMap(MapWins[0], 1);
        }
        for (int32_t y = 0; y < g_canvas.height; y++) {
            uint8_t* dst = g_canvas.data + (size_t)y * g_canvas.stride;
            if (y < g_view.height) std::memcpy(dst, g_view.data + (size_t)y * g_view.stride, (size_t)g_view.width);
        }
        draw_map_cursor();
        console_frame_draw(&g_canvas);
        if (g_desktop_shown && g_desktop_buf.data) {  // DesktopWin over the windows (go-to drag)
            for (int32_t y = 0; y < g_canvas.height; y++) {
                const uint8_t* s = g_desktop_buf.data + (size_t)y * g_desktop_buf.stride;
                uint8_t* d = g_canvas.data + (size_t)y * g_canvas.stride;
                for (int32_t x = 0; x < g_canvas.width; x++)
                    if (s[x] != 9) d[x] = s[x];
            }
        }
    }
    screens_compose(&g_canvas);  // full-screen windows (reports, social engineering; screenwin.h)
    basewin_compose(&g_canvas);  // the base window over the console frame
    console_menu_draw(&g_canvas);
    pulldown_draw_windows(&g_canvas);
    tutwin_draw_lines(&g_canvas);  // TutWin's pointer lines (Win_window_line_raw on the screen)
    popup_draw_windows(&g_canvas);
    console_draw_cursor(&g_canvas);
    set_layers();
    platform::video_present(g_canvas.data, g_canvas.stride, g_canvas.width, g_canvas.height,
                            reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
    g_dirty = false;
    g_last_present = platform::ticks_ms();
}

bool ui_screenshot(const std::string& path) {
    ui_present();
    set_layers();
    bool ok = platform::save_bmp(path.c_str(), g_canvas.data, g_canvas.stride, g_canvas.width, g_canvas.height,
                                 reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
    std::fprintf(stderr, "screenshot %s (turn %d): %s\n", path.c_str(), CurrentTurn, ok ? "ok" : "failed");
    return ok;
}

int32_t ui_key_state(int32_t vk) {
    bool held = false;
    if (vk == VKEY_SHIFT) held = (g_script_mods & 0x10000) || platform::key_down(vk);
    else if (vk == VKEY_CONTROL) held = (g_script_mods & 0x20000) || platform::key_down(vk);
    else held = platform::key_down(vk);
    return held ? 0x8000 : 0;
}

void ui_trace(const char* what) {
    static int on = -1;
    if (on < 0) on = std::getenv("SMACX_TRACE") ? 1 : 0;
    if (!on) return;
    std::fprintf(stderr, "trace %s: turn %d crt %08X game %08X clock %u\n", what, CurrentTurn, crt_rand_state(),
                 game_random_state(), engine_logic_clock_state());
}

void ui_script_keys(const std::vector<ScriptedKey>& keys) {
    g_script.assign(keys.begin(), keys.end());
    g_script_given = !keys.empty();
}

// ---- message pump ----

// Mouse input of the main screen: the base window when it is open, else the console.
static void ui_mouse(int32_t type, int32_t x, int32_t y) {
    if (basewin_mouse(type, x, y, false, type == UiInput::RDOWN || type == UiInput::RUP)) return;
    console_ui_mouse(type, x, y);
}

void GAMEAPI wait_task() {
    // do_video: the window is presented when it changed.
    if (g_dirty || g_map_dirty) ui_present();
    if (!g_script.empty()) {
        ScriptedKey k = g_script.front();
        g_script.pop_front();
        ui_trace(("wait_task key " + std::to_string(k.vk) + " " + k.command).c_str());
        int32_t x1, y1, x2, y2;
        if (std::sscanf(k.command.c_str(), "click %d %d", &x1, &y1) == 2) {
            ui_mouse(UiInput::MOVE, x1, y1);
            ui_mouse(UiInput::LDOWN, x1, y1);
            ui_mouse(UiInput::LUP, x1, y1);
        } else if (std::sscanf(k.command.c_str(), "rclick %d %d", &x1, &y1) == 2) {
            ui_mouse(UiInput::MOVE, x1, y1);
            g_script_mods = k.vk & 0x30000;  // S-RCLICK / C-RCLICK: shift / ctrl held
            ui_mouse(UiInput::RDOWN, x1, y1);
            g_script_mods = 0;
            ui_mouse(UiInput::RUP, x1, y1);
        } else if (std::sscanf(k.command.c_str(), "drag %d %d %d %d", &x1, &y1, &x2, &y2) == 4) {
            ui_mouse(UiInput::MOVE, x1, y1);
            char shot[256] = "";
            std::sscanf(k.command.c_str(), "drag %*d %*d %*d %*d %255s", shot);
            console_ui_drag(x1, y1, x2, y2, shot[0] ? g_ui_outdir + "/" + shot : std::string());
        } else if (std::sscanf(k.command.c_str(), "move %d %d", &x1, &y1) == 2) {
            ui_mouse(UiInput::MOVE, x1, y1);
        } else if (!k.command.empty()) {
            run_command(k.command);
        } else {
            g_script_mods = k.vk & 0x30000;
            dispatch_key(k.vk);
            g_script_mods = 0;
        }
        return;
    }
    console_ui_tick();
    if (g_script_given && g_ui_exit_when_script_done) {
        std::fprintf(stderr, "scripted keys done (turn %d): quitting\n", CurrentTurn);
        ControlTurnA = 1;
        return;
    }
    if (g_input.empty()) pump_platform(20);
    if (g_input.empty()) return;
    platform::Event ev = g_input.front();
    g_input.pop_front();
    switch (ev.type) {
    case platform::EV_KEY_DOWN: dispatch_key(event_key(ev)); break;
    case platform::EV_LBUTTON_DOWN: ui_mouse(UiInput::LDOWN, ev.x, ev.y); break;
    case platform::EV_LBUTTON_UP: ui_mouse(UiInput::LUP, ev.x, ev.y); break;
    case platform::EV_RBUTTON_DOWN: ui_mouse(UiInput::RDOWN, ev.x, ev.y); break;
    case platform::EV_RBUTTON_UP: ui_mouse(UiInput::RUP, ev.x, ev.y); break;
    case platform::EV_MOUSE_MOVE: ui_mouse(UiInput::MOVE, ev.x, ev.y); break;
    default: break;
    }
}

int32_t GAMEAPI do_non_input() {
    // Presents at most every 50 ms while the engine runs (the AI's turns call this per unit).
    if ((g_dirty || g_map_dirty) && platform::ticks_ms() - g_last_present >= 50) ui_present();
    return pump_platform(0) ? 1 : 0;
}

void GAMEAPI do_all_non_input() {
    while (do_non_input()) {
    }
}

void GAMEAPI flush_input() {
    pump_platform(0);
    g_input.clear();
}

int32_t ui_wait_key() {
    for (;;) {
        ui_present();
        if (!g_script.empty()) {
            ScriptedKey k = g_script.front();
            g_script.pop_front();
            ui_trace(("wait_key key " + std::to_string(k.vk) + " " + k.command).c_str());
            if (!k.command.empty()) {
                run_command(k.command);
                continue;
            }
            return k.vk;
        }
        if (g_script_given && g_ui_exit_when_script_done) return 0;
        if (ControlTurnA) return 0;
        if (g_input.empty()) pump_platform(50);
        if (g_input.empty()) continue;
        platform::Event ev = g_input.front();
        g_input.pop_front();
        if (ev.type == platform::EV_KEY_DOWN) return event_key(ev);
    }
}

bool ui_wait_input(UiInput* in) {
    static int32_t pending_up = -1, pending_x = 0, pending_y = 0;
    static uint32_t last_down = 0;
    static int32_t last_x = -100, last_y = -100;
    *in = UiInput();
    if (pending_up >= 0) {  // the release of a scripted click
        in->type = pending_up == 2 ? UiInput::RUP : UiInput::LUP;
        in->x = pending_x;
        in->y = pending_y;
        pending_up = -1;
        return true;
    }
    for (;;) {
        ui_present();
        if (!g_script.empty()) {
            ScriptedKey k = g_script.front();
            g_script.pop_front();
            ui_trace(("wait_input key " + std::to_string(k.vk) + " " + k.command).c_str());
            int32_t x, y;
            if (std::sscanf(k.command.c_str(), "rclick %d %d", &x, &y) == 2) {
                in->type = UiInput::RDOWN;
                in->x = pending_x = x;
                in->y = pending_y = y;
                pending_up = 2;
                return true;
            }
            if (std::sscanf(k.command.c_str(), "move %d %d", &x, &y) == 2) {  // MOVE:XxY
                in->type = UiInput::MOVE;
                in->x = x;
                in->y = y;
                return true;
            }
            bool dbl = std::sscanf(k.command.c_str(), "dclick %d %d", &x, &y) == 2;
            if (dbl || std::sscanf(k.command.c_str(), "click %d %d", &x, &y) == 2) {
                in->type = UiInput::LDOWN;
                in->x = pending_x = x;
                in->y = pending_y = y;
                in->double_click = dbl;  // DCLICK: the second press of a double click
                pending_up = 1;
                return true;
            }
            if (!k.command.empty()) {
                run_command(k.command);
                continue;
            }
            in->key = k.vk;
            return true;
        }
        if (g_script_given && g_ui_exit_when_script_done) return false;
        if (ControlTurnA) return false;
        if (g_input.empty()) pump_platform(50);
        if (g_input.empty()) continue;
        platform::Event ev = g_input.front();
        g_input.pop_front();
        if (ev.type == platform::EV_KEY_DOWN) {
            in->key = event_key(ev);
            return true;
        }
        in->x = ev.x;
        in->y = ev.y;
        if (ev.type == platform::EV_LBUTTON_DOWN) {
            in->type = UiInput::LDOWN;
            uint32_t now = platform::ticks_ms();
            in->double_click = now - last_down < 500 && std::abs(ev.x - last_x) < 4 && std::abs(ev.y - last_y) < 4;
            last_down = now;
            last_x = ev.x;
            last_y = ev.y;
            return true;
        }
        if (ev.type == platform::EV_LBUTTON_UP) {
            in->type = UiInput::LUP;
            return true;
        }
        if (ev.type == platform::EV_MOUSE_MOVE) {
            in->type = UiInput::MOVE;
            return true;
        }
        if (ev.type == platform::EV_RBUTTON_DOWN || ev.type == platform::EV_RBUTTON_UP) {
            in->type = ev.type == platform::EV_RBUTTON_DOWN ? UiInput::RDOWN : UiInput::RUP;
            return true;
        }
    }
}

// ---- placeholders of original functions called by the console (console.h) ----

namespace {
std::set<std::string>& reported() {
    static std::set<std::string> names;
    return names;
}
}  // namespace

void console_not_reconstructed_quiet(const char* name) {
    if (reported().insert(name).second) std::fprintf(stderr, "not reconstructed: %s\n", name);
}

void console_not_reconstructed(const char* name) {
    console_not_reconstructed_quiet(name);
    ui_set_message(std::string("(not reconstructed yet: ") + name + ")");
}

