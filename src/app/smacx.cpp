// smacx: the native game with the SDL2 UI (first slice: the main map of a saved game).
//
//   smacx --save FILE.sav [--gamedir DIR] [--viewer N] [--zoom Z] [--center X,Y] [--size WxH]
//         [--turns N] [--shot FILE.bmp] [--hidden] [--outdir DIR] [--image FILE] [--ini FILE]
//
// The reconstructed engine runs as in the original: control_game boots, the main menu (top_menu)
// is replaced by loading the save, and control_turn plays the turns. Before every turn_upkeep
// (where the harness takes its snapshots) the UI shows the map and waits:
//   arrows      scroll           + / -        zoom in / out
//   Enter/Space next turn        F12 / S      screenshot (smacx_NNN.bmp in the output directory)
//   Esc         quit
// --keys K1,K2,...: scripted keys handled before real input (testing), e.g. RIGHT,ENTER,F12,ESC.
// All factions are computer players for now (the human turn is the next slice); the engine's UI
// calls still go to the headless implementations (src/headless), as in smacx_headless.
// --shot: after --turns N turns (default 0) write the screenshot and exit (with --hidden and
// SDL_VIDEODRIVER=dummy this needs no display).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/clock.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/memmap.h"
#include "engine/prefs.h"
#include "engine/save.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string save, gamedir = "game", image = "src/data/data_image.bin";
    std::string ini = "harness/oracle/runtime/Alpha Centauri.Ini", outdir = "smacx_out", shot;
    int viewer = -1, zoom = 0x7FFF, cx = -1, cy = -1, width = 1024, height = 768, turns = 0;
    bool hidden = false;
    std::vector<int> keys;  // --keys: scripted key presses, consumed before real input
} g_opt;

Buffer g_canvas;  // the screen: the main view's GraphicWin canvas
// Height of the main map view's client area. The original sizes it from the console/world
// window layout (MapWin_UNK5 0x46FA20: screen height minus the world window's extent); that
// layout is not reconstructed yet. 168 pixels less than the screen matches the oracle's view at
// 1024x768 (same tile rows; the console covers the bottom of the screen).
constexpr int kConsoleReserve = 168;
Buffer g_view;
int g_shots = 0;
int g_turns_done = 0;
bool g_quit = false;

// The screen: the map view at the top, the console area (not reconstructed yet) black.
void compose() {
    for (int32_t y = 0; y < g_canvas.height; y++) {
        uint8_t* dst = g_canvas.data + (size_t)y * g_canvas.stride;
        if (y < g_view.height) std::memcpy(dst, g_view.data + (size_t)y * g_view.stride, (size_t)g_view.width);
        else std::memset(dst, 0, (size_t)g_canvas.width);
    }
}

void present() { platform::video_present(g_canvas.data, g_canvas.stride, g_canvas.width, g_canvas.height, reinterpret_cast<const platform::Rgb*>(GamePalette.rgb)); }

void redraw() {
    native_ui::pfncRedrawMap(&MainConsole, 1);
    compose();
    present();
}

bool screenshot(const std::string& path) {
    bool ok = platform::save_bmp(path.c_str(), g_canvas.data, g_canvas.stride, g_canvas.width, g_canvas.height,
                                 reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
    std::fprintf(stderr, "screenshot %s (turn %d): %s\n", path.c_str(), CurrentTurn, ok ? "ok" : "failed");
    return ok;
}

// The oracle's observer preferences (harness/oracle/oracle.cpp apply_observer_prefs): no
// tutorial, no autosave, no pauses, so that turns play as in the reference runs.
void apply_observer_prefs() {
    const uint32_t clear = 0x1 | 0x2 | 0x20 | 0x80 | 0x10000000;
    const uint32_t set = 0x40000 | 0x8;
    GamePreferences = (int32_t)(((uint32_t)GamePreferences & ~clear) | set);
    AlphaIniPrefs.preferences = (int32_t)(((uint32_t)AlphaIniPrefs.preferences & ~clear) | set);
    GameMorePreferences = (int32_t)((uint32_t)GameMorePreferences & ~(0x10u | 0x4000u | 0x8000u | 0x20000u));
}

// Zoom keys (pfncProcZoomKey 0x5150D0): steps of 2 between -14 and 16.
void zoom_step(int dir) {
    int32_t& z = mw(&MainConsole, kMwZoom);
    if (dir > 0 && z < 0x10) z += 2;
    if (dir < 0 && -0xE < z) z -= 2;
}

void scroll(int dx, int dy) {
    int32_t x = mw(&MainConsole, kMwCenterX) + dx, y = mw(&MainConsole, kMwCenterY) + dy;
    if ((MapToggleFlat & 1) == 0) x = (x % MapAreaX + MapAreaX) % MapAreaX;
    y = std::max(0, std::min(MapAreaY - 1, y));
    if (((x ^ y) & 1) != 0) x = (MapToggleFlat & 1) == 0 ? (x + 1) % MapAreaX : x + 1;
    native_ui::pfncMoveMap(&MainConsole, x, y, 0);
}

// The UI's turn pause (where the original's observer sees the "turn complete" state): show the
// map and handle input until the next turn is requested.
void ui_pause() {
    // The engine's MapWin_clear_terrain / clear_alt calls go to the headless no-ops: the terrain
    // cache is rebuilt every turn instead.
    ui_mapwin_clear_cache(&MainConsole);
    redraw();
    if (g_turns_done < g_opt.turns) {
        g_turns_done++;
        return;
    }
    if (!g_opt.shot.empty()) {
        screenshot(g_opt.shot);
        g_quit = true;
        return;
    }
    for (;;) {
        platform::Event ev;
        if (!g_opt.keys.empty()) {
            ev.type = platform::EV_KEY_DOWN;
            ev.key = g_opt.keys.front();
            g_opt.keys.erase(g_opt.keys.begin());
        } else if (!platform::wait_event(&ev, 100)) {
            continue;
        }
        if (ev.type == platform::EV_QUIT) {
            g_quit = true;
            return;
        }
        if (ev.type == platform::EV_EXPOSE) present();
        if (ev.type != platform::EV_KEY_DOWN) continue;
        switch (ev.key) {
        case VKEY_ESCAPE: g_quit = true; return;
        case VKEY_RETURN:
        case VKEY_SPACE: return;
        case VKEY_LEFT: scroll(-2, 0); break;
        case VKEY_RIGHT: scroll(2, 0); break;
        case VKEY_UP: scroll(0, -2); break;
        case VKEY_DOWN: scroll(0, 2); break;
        case VKEY_ADD: case VKEY_OEM_PLUS: zoom_step(1); break;
        case VKEY_SUBTRACT: case VKEY_OEM_MINUS: zoom_step(-1); break;
        case VKEY_F12: case 'S': {
            char name[64];
            std::snprintf(name, sizeof(name), "smacx_%03d.bmp", g_shots++);
            screenshot((fs::path(g_opt.outdir) / name).string());
            continue;
        }
        default: continue;
        }
        redraw();
    }
}

int32_t harness_turn_upkeep() {
    apply_observer_prefs();
    ui_pause();
    if (g_quit) {
        ControlTurnA = 1;  // leave control_turn; control_game then quits
        return 0;
    }
    turn_upkeep();
    return 0;
}

// Replaces the main menu: loads the save and sets up the view.
int32_t load_game_menu() {
    static bool loaded = false;
    if (loaded) return 1;  // back from a game: quit
    loaded = true;
    int r = load_daemon(g_opt.save.c_str(), 0);
    std::fprintf(stderr, "load %s -> %d (turn %d)\n", g_opt.save.c_str(), r, CurrentTurn);
    if (r) return 1;
    FactionStatus[0] = 0;  // every faction plays as a computer player (observer)
    if (g_opt.viewer >= 0) CurrentPlayerFaction = g_opt.viewer;
    apply_observer_prefs();
    for (int32_t f = 1; f < 8; f++) native_ui::load_faction_art(f);
    ui_mapwin_attach(&MainConsole, kConsoleVBase, &g_view);
    // desktop_init(ControlTurnMove == 0) -> mapwin_system_init: the main view is reset and
    // centred on the cursor; MapWin_init_2 then sets its display options (main view of a
    // single-player game: 0x60010EE3, see 0x470A90).
    if (ControlTurnMove == 0) MapWin_clear(&MainConsole, 0);
    mw(&MainConsole, kMwOptions) = 0x60010EE3;
    if (g_opt.zoom != 0x7FFF) mw(&MainConsole, kMwZoom) = g_opt.zoom;
    if (g_opt.cx >= 0) {
        mw(&MainConsole, kMwCenterX) = g_opt.cx;
        mw(&MainConsole, kMwCenterY) = g_opt.cy;
    }
    std::fprintf(stderr, "view: zoom %d centre %d,%d options %08X viewer %d\n", mw(&MainConsole, kMwZoom),
                 mw(&MainConsole, kMwCenterX), mw(&MainConsole, kMwCenterY), (uint32_t)mw(&MainConsole, kMwOptions),
                 CurrentPlayerFaction);
    return 0;
}

int usage() {
    std::fprintf(stderr,
                 "usage: smacx --save FILE.sav [--gamedir DIR] [--viewer N] [--zoom Z] [--center X,Y] [--size WxH]\n"
                 "             [--turns N] [--shot FILE.bmp] [--keys K1,K2,..] [--hidden] [--outdir DIR] [--image FILE]\n"
                 "             [--ini FILE]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--save") g_opt.save = next();
        else if (a == "--gamedir") g_opt.gamedir = next();
        else if (a == "--image") g_opt.image = next();
        else if (a == "--ini") g_opt.ini = next();
        else if (a == "--outdir") g_opt.outdir = next();
        else if (a == "--viewer") g_opt.viewer = std::atoi(next().c_str());
        else if (a == "--zoom") g_opt.zoom = std::atoi(next().c_str());
        else if (a == "--turns") g_opt.turns = std::atoi(next().c_str());
        else if (a == "--shot") g_opt.shot = next();
        else if (a == "--hidden") g_opt.hidden = true;
        else if (a == "--keys") {
            // Comma-separated: LEFT RIGHT UP DOWN ENTER SPACE PLUS MINUS F12 ESC or a letter.
            std::string list = next() + ",";
            for (size_t p = 0, c; (c = list.find(',', p)) != std::string::npos; p = c + 1) {
                std::string k = list.substr(p, c - p);
                int vk = k == "LEFT" ? VKEY_LEFT : k == "RIGHT" ? VKEY_RIGHT : k == "UP" ? VKEY_UP : k == "DOWN" ? VKEY_DOWN
                       : k == "ENTER" ? VKEY_RETURN : k == "SPACE" ? VKEY_SPACE : k == "PLUS" ? VKEY_ADD
                       : k == "MINUS" ? VKEY_SUBTRACT : k == "F12" ? VKEY_F12 : k == "ESC" ? VKEY_ESCAPE
                       : k.size() == 1 ? std::toupper((unsigned char)k[0]) : 0;
                if (vk) g_opt.keys.push_back(vk);
            }
        }
        else if (a == "--center") std::sscanf(next().c_str(), "%d,%d", &g_opt.cx, &g_opt.cy);
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &g_opt.width, &g_opt.height);
        else return usage();
    }
    if (g_opt.save.empty()) return usage();
    g_opt.save = fs::absolute(g_opt.save).string();
    g_opt.outdir = fs::absolute(g_opt.outdir).string();
    if (!g_opt.shot.empty()) g_opt.shot = fs::absolute(g_opt.shot).string();
    if (!mem::init(fs::absolute(g_opt.image).string().c_str())) {
        std::fprintf(stderr, "cannot load data image %s\n", g_opt.image.c_str());
        return 2;
    }
    // The game writes its preferences: work on a copy.
    fs::create_directories(g_opt.outdir);
    std::string ini_copy = (fs::path(g_opt.outdir) / "Alpha Centauri.Ini").string();
    std::error_code ec;
    if (!fs::exists(ini_copy)) fs::copy_file(g_opt.ini, ini_copy, ec);
    fs::current_path(g_opt.gamedir);
    platform::set_game_dir(".");
    engine_static_init(ini_copy.c_str());
    // WinMain: the screen size (GetSystemMetrics) the windows are laid out for; config_popups
    // copies it into CanvasWidth/CanvasHeight.
    ScreenWidth = g_opt.width;
    ScreenHeight = g_opt.height;
    if (!platform::video_init("Sid Meier's Alpha Centauri", g_opt.width, g_opt.height, g_opt.hidden)) return 1;
    // system_init's UI part (load_fixed_sprites -> load_palette, load_sprites -> load_terrain)
    // and mapwin_system_init's lighting table.
    load_palette();
    load_terrain();
    MapWin_compute_lighting_table();
    Buffer_ctor(&g_canvas);
    Buffer_init(&g_canvas, g_opt.width, g_opt.height, 8, 0);
    Buffer_ctor(&g_view);
    Buffer_init(&g_view, g_opt.width, std::max(g_opt.height - kConsoleReserve, 100), 8, 0);
    g_harness_top_menu = load_game_menu;
    g_harness_turn_upkeep = harness_turn_upkeep;
    g_harness_game_end = [] { std::fprintf(stderr, "game over (turn %d)\n", CurrentTurn); std::exit(0); };
    control_game();
    platform::video_shutdown();
    return 0;
}
