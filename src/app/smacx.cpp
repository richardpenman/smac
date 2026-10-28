// smacx: the native game with the SDL2 UI.
//
//   smacx [--new [k=v ...] | --save FILE.sav] [--human N] [--viewer N] [--seed CRT,GAME|clock]
//         [--keys K1,K2,...] [--exit-after-keys] [--animate] [--turns N] [--shot FILE.bmp] [--snapshots DIR]
//         [--rng FILE] [--zoom Z] [--center X,Y] [--size WxH] [--hidden] [--outdir DIR]
//         [--gamedir DIR] [--image FILE] [--ini FILE] [--nosound] [--original-bugs]
//
// The original's bugs (engine/compat.h, docs/COMPAT.md): smacx plays with them fixed unless the
// ini says "Original Bugs=1" ([Alpha Centauri] section) or --original-bugs is given (the tests
// whose references come from the original, the harness). --original-bugs also overrides the
// setting a hotseat or PBEM save carries; in an online game the host's setting applies.
//
// Files. Played as a game (no --outdir): the settings (Alpha Centauri.Ini, seeded from the default
// ini next to the executable), saves (the game's "saves" folder), the hall of fame, screenshots and
// the data image built from terranx.exe go to the per-user directory (SDL_GetPrefPath: Linux
// ~/.local/share/smacx, macOS ~/Library/Application Support/smacx, Windows %APPDATA%\smacx); the
// game folder is found at start-up (app/gamedata.h: the remembered folder, the standard install
// locations, else the player is asked) and remembered in smacx.cfg there. --gamedir, --image and
// --ini replace each of these. With --outdir (tests, the harness) nothing is looked for: the
// defaults are the repository's (game, src/data/data_image.bin,
// harness/oracle/runtime/Alpha Centauri.Ini), everything is written below --outdir and the game's
// saves folder is the game directory's; the ini is copied from --ini at every start.
//
// The reconstructed engine runs as in the original: control_game boots, the main menu (top_menu,
// src/ui/topmenu.cpp: the opening screens, the game setup dialogs, LOAD GAME's FileBox) sets up or
// loads a game, and control_turn plays the turns. --new (the harness's newgame keys: size, width,
// height, ocean, erosion, orbit, clouds, natives, diff, rules, factions=a,b,..., random) or --save
// take the place of the main menu. A game set up in the main menu with --seed is seeded at the menu
// and again when its world is built (as the harness's seed command before newgame), so it equals
// the --new game of the same parameters and seed (tests/menu/run.sh).
//
// With --human N (default 1 for --new; 0: none) faction N is played by a human: its turns run the
// reconstructed Console_human_turn (src/ui/console.cpp) with the original key bindings, e.g.
//   keypad 1-9 / arrows / Home End PgUp PgDn   move the unit      space  skip     w  wait
//   l sentry   h hold   b build base   f farm   m mine   s solar   r road   F forest   k bunker
//   v view/unit mode   Enter next turn (or the base at the cursor)   ctrl+Enter end the turn
//   z / x zoom   c centre   Esc quit
// The computer players move with the engine. Without a human (--save, no --human) every faction is
// a computer player and the UI pauses before each turn (observer, the first slice):
//   arrows scroll   + / - zoom   Enter / space next turn   F12 / S screenshot   Esc quit
//
// --keys: scripted input, consumed before real input. Tokens: a character (b, B = shift+b, ., [ ...),
// LEFT RIGHT UP DOWN HOME END PGUP PGDN ENTER SPACE TAB BACK ESC PAUSE F1..F12, KP1..KP9 (keypad),
// C-x (ctrl), S-x (shift), CLICK:XxY (a left click in a popup; S-CLICK:XxY with shift held), RCLICK:XxY (a right click; S-RCLICK /
// C-RCLICK with shift / ctrl held: the map's right-click menu), NETMSG:LABEL (a network game's NetMsg box),
// POPUP:FILE/LABEL[/FLAGS[/PCX]]
// (shows a script popup, a test aid), PLAYSOUND:FILE (decodes and plays a WAV / MP3 file on the
// voice object, logging its length), MUSICRENDER:FILE:SECONDS (the music's next seconds into a WAV
// file in --outdir), MOVIE:NAME (plays movies\NAME.wve), BASEDUMP:FILE (the open base window's canvases in the
// oracle's basedump format), BASE:N (opens base N's window, a test aid), BASESTATE (the open
// base's production, queue, workers and stores on stderr), CMD:ID (Console_menu_proc's command ID,
// e.g. CMD:0x605, a test aid), EDITSTATE (the scenario editor's pane), SCREENSTATE (the open report / social
// engineering / monuments screen: its texts and state), FACTIONSTATE (the viewer's social choice,
// energy allocation, credits and upheaval paid), TECHSTATE (the viewer's research goal and
// points, blind research priorities), PROTOSTATE (the viewer's prototypes), MENUSTATE (the open
// menu column of the opening screens: caption, items, selection), INFOSTATE (the info pane's
// displays and texts), INFOTICK:N (N steps of the info pane's 50 ms timer), PALTICK:N (N steps of
// every palette cycle's timer, then the cycles' colours on stderr), PLANSTATE (the go-to list's
// map preview: its zoom, centre and route lines), and the commands SHOT[:FILE]
// (screenshot), SAVE:FILE (save the game
// and its generators in FILE.rng, relative to --outdir), UNITS (list the human's units on
// stderr), PIXEL:XxY (the presented screen's palette index and colour at X,Y), WAITFOR:TEXT (no
// scripted input until the online game noted a line containing TEXT ("net: ..." on stderr, after
// the last line a WAITFOR matched; 300 s at most), WAIT:MS (no
// scripted input for MS ms: the waiting loop runs its timers and animations as for a player who
// waits), QUIT. --exit-after-keys (default with --hidden) quits when they run out.
// Such unattended runs skip the animations (window slides, unit moves, booms, battle reports,
// movies); --animate (or SMACX_ANIMATE=1) plays them all with their waits as an interactive game
// does and still quits at the end of the keys (the test suites' default; TEST_FAST=1: unattended).
// Environment: SMACX_KEYLOG=1 logs every key dispatched to the console; SMACX_ALLUNITS=1 makes
// UNITS list every faction's units.
// --seed CRT,GAME: the generators' seeds (the harness's seed command); default 0,0 with --keys or
// --hidden, otherwise the clock. The original
// seeds both from the clock (game_init: timeGetTime), and a loaded save keeps the CRT generator's
// state from that seed (load_daemon reseeds it only under GameRules 0x400): without a fixed seed
// the same save and keys play differently. --seed clock seeds from the clock as the original.
// SMACX_TRACE=1 logs the generators and the logic clock at every scripted key and popup.
// SMACX_PREFS_SET=BITS: GamePreferences bits set after the test preferences (e.g. 0x20 tutorial hints).
// SMACX_SOUNDLOG=1 logs the sound, voice, music and movie calls ("sound: T<turn> FX_play 15"; ui/sound.h).
// --nosound: no audio device (the calls still run and are logged); SDL_AUDIODRIVER chooses the
// device otherwise (the test scripts use "dummy").
// --snapshots DIR: turn_NNN.state/.rng/.sav before every turn_upkeep, as the harness writes them
// (compare runs with harness/compare.py); --rng FILE restores a snapshot's generators after --save.
// --shot: observer mode: after --turns N turns (default 0) write the screenshot and exit (with
// --hidden and SDL_VIDEODRIVER=dummy this needs no display).
// --dump FILE: observer mode: instead of the screenshot, the main map view's canvas after a full
// redraw (format of the oracle's mapdump); --like REF takes the view of an oracle dump (canvas
// size, viewer, zoom, centre, tile address); --scroll-from X,Y makes it a scrolled redraw from a
// view centred there (the oracle's mapdump scrollfrom=); --dump-options HEX ORs display options
// into the window for it (options=: 0x2000 AI goals, 0x20000 keypad legend, 0x100000 diplomatic
// table); --dump-radius X,Y,R follows the full redraw by MapWin_draw_radius (radius=). Compare
// with work/ui/mapcmp.py.
// --basedump BASE:FILE[:TILES]: observer mode: the base window of base BASE as its owner sees it, in the
// format of the oracle's basedump (and the screen in FILE.bmp; tests/basewin/compare.sh); TILES:
// the oracle's tile array address in hex (terrain variants).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "app/gamedata.h"
#include "engine/clock.h"
#include "engine/compat.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/setup.h"
#include "engine/state_io.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/world.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/console.h"
#include "ui/diplowin.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/colortables.h"
#include "ui/sprites.h"
#include "ui/vehdraw.h"
#include "ui/basewin.h"
#include "ui/sound.h"
#include "ui/topmenu.h"
#include "ui/win.h"

namespace fs = std::filesystem;

namespace {

struct Options {
    std::string save, gamedir, image, ini, outdir, shot, snapshots, rng;  // defaults: main
    std::string dump, like;  // --dump FILE [--like REF]: map canvas dump (ui_mapdump_like)
    std::string basedump;  // --basedump BASE:FILE: the base window's canvases (the oracle's basedump)
    int scroll_x = -1, scroll_y = -1;  // --scroll-from X,Y: the dump's redraw scrolls from there
    unsigned dump_options = 0;  // --dump-options HEX: display options OR'd in for the dump
    int radius_x = 0, radius_y = 0, radius_r = -1;  // --dump-radius X,Y,R: then a partial redraw
    bool new_game = false;
    std::string new_args;
    int human = -1, viewer = -1, zoom = 0x7FFF, cx = -1, cy = -1, width = 1024, height = 768, turns = 0;
    unsigned seed_crt = 0, seed_game = 0;
    bool seeded = false, seed_given = false, hidden = false, exit_after_keys = false, nosound = false, animate = false;
    bool original_bugs = false;  // --original-bugs
    std::vector<ScriptedKey> keys;
} g_opt;

int g_turns_done = 0;
bool g_quit = false;

// ---- snapshots (--snapshots DIR): as the harness takes them before each turn_upkeep ----

void write_state(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    state_regions_write([&](uint32_t addr, const uint8_t* data, uint32_t size) {
        std::fwrite(&addr, 4, 1, f);
        std::fwrite(&size, 4, 1, f);
        if (size) std::fwrite(data, 1, size, f);
    });
    std::fclose(f);
}

// turn_NNN.state / .rng / .sav in the harness's format (harness/compare.py).
void snapshot() {
    char name[32];
    std::snprintf(name, sizeof(name), "turn_%03d", CurrentTurn);
    std::string base = (fs::path(g_opt.snapshots) / name).string();
    write_state(base + ".state");
    uint32_t crt = crt_rand_state(), game = game_random_state();
    if (FILE* f = std::fopen((base + ".rng").c_str(), "w")) {
        std::fprintf(f, "crt %u\ngame %u\nclock %u\nturn %d\n", crt, game, engine_logic_clock_state(), CurrentTurn);
        std::fclose(f);
    }
    save_daemon((base + ".sav").c_str());
    crt_set_rand_state(crt);
    game_reseed(game);
}

// The oracle's observer preferences (harness/oracle/oracle.cpp apply_observer_prefs): no
// tutorial, no autosave, no pauses, so that turns play as in the reference runs.
void apply_observer_prefs() {
    const uint32_t clear = 0x1 | 0x2 | 0x20 | 0x80 | 0x10000000;
    const uint32_t set = 0x40000 | 0x8;
    GamePreferences = (int32_t)(((uint32_t)GamePreferences & ~clear) | set);
    AlphaIniPrefs.preferences = (int32_t)(((uint32_t)AlphaIniPrefs.preferences & ~clear) | set);
    GameMorePreferences = (int32_t)((uint32_t)GameMorePreferences & ~(0x10u | 0x4000u | 0x8000u | 0x20000u));
    // A human player's turn ends with Enter / ctrl+Enter only ("pause at end of turn"), so that
    // scripted keys stay in step with the turns.
    if (g_opt.human > 0) {
        GamePreferences |= PREF_BSC_PAUSE_END_TURN;
        AlphaIniPrefs.preferences |= 1;
    }
    // Test aid: SMACX_PREFS_SET=BITS sets GamePreferences bits afterwards (e.g. 0x20 the tutorial
    // hints).
    if (const char* e = std::getenv("SMACX_PREFS_SET")) {
        uint32_t bits = (uint32_t)std::strtoul(e, nullptr, 0);
        GamePreferences = (int32_t)((uint32_t)GamePreferences | bits);
        AlphaIniPrefs.preferences = (int32_t)((uint32_t)AlphaIniPrefs.preferences | bits);
    }
}

// ---- observer mode (no human player): the turn pause of the first slice ----

void zoom_step(int dir) {  // pfncProcZoomKey 0x5150D0: steps of 2 between -14 and 16
    int32_t& z = map_win(&MainConsole).zoom;
    if (dir > 0 && z < 0x10) z += 2;
    if (dir < 0 && -0xE < z) z -= 2;
    ui_invalidate_map();
}

void scroll(int dx, int dy) {
    int32_t x = map_win(&MainConsole).center_x + dx, y = map_win(&MainConsole).center_y + dy;
    if ((MapToggleFlat & 1) == 0) x = (x % MapAreaX + MapAreaX) % MapAreaX;
    y = std::max(0, std::min(MapAreaY - 1, y));
    if (((x ^ y) & 1) != 0) x = (MapToggleFlat & 1) == 0 ? (x + 1) % MapAreaX : x + 1;
    pfncMoveMap(&MainConsole, x, y, 0);
    ui_invalidate_map();
}

// Shows the map and handles input until the next turn is requested.
void observer_pause() {
    ui_mapwin_clear_cache(&MainConsole);
    ui_invalidate_map();
    ui_present();
    if (g_turns_done < g_opt.turns) {
        g_turns_done++;
        return;
    }
    if (!g_opt.dump.empty()) {
        bool ok = ui_mapdump_like(&MainConsole, g_opt.dump.c_str(), g_opt.like.c_str(), g_opt.scroll_x, g_opt.scroll_y,
                                  g_opt.dump_options, g_opt.radius_x, g_opt.radius_y, g_opt.radius_r);
        std::fprintf(stderr, "dump %s: %s\n", g_opt.dump.c_str(), ok ? "ok" : "failed");
        g_quit = true;
        return;
    }
    if (!g_opt.basedump.empty()) {
        // As the oracle's basedump: the base's owner views it, the window opens, its canvases and
        // the screen are written, it closes (basewin.h).
        int32_t base_id = std::atoi(g_opt.basedump.c_str());
        std::string file = g_opt.basedump.substr(g_opt.basedump.find(':') + 1);
        // BASE:FILE:TILES: the address of the oracle's tile array (the terrain variants' seed).
        size_t colon = file.rfind(':');
        if (colon != std::string::npos && colon > 1) {
            TileVariantBase = (uint32_t)std::strtoul(file.c_str() + colon + 1, nullptr, 16);
            file = file.substr(0, colon);
        }
        int32_t viewer = CurrentPlayerFaction;
        CurrentPlayerFaction = (uint8_t)Bases[base_id].faction_id;
        BaseWin_zoom(&BaseWin, base_id, 0);
        basewin_dump(file.c_str());
        ui_screenshot(file + ".bmp");
        BaseWin_exit(&BaseWin);
        CurrentPlayerFaction = viewer;
        g_quit = true;
        return;
    }
    if (!g_opt.shot.empty()) {
        ui_screenshot(g_opt.shot);
        g_quit = true;
        return;
    }
    // An unattended run (--hidden, no scripted keys) of --turns N turns ends there.
    if (g_opt.turns > 0 && g_opt.hidden && g_opt.keys.empty()) {
        g_quit = true;
        return;
    }
    for (;;) {
        int32_t key = ui_wait_key();
        int32_t vk = key & 0xFFFF;
        switch (vk) {
        case 0:
        case VKEY_ESCAPE: g_quit = true; return;
        case VKEY_RETURN:
        case VKEY_SPACE: return;
        case VKEY_LEFT: scroll(-2, 0); break;
        case VKEY_RIGHT: scroll(2, 0); break;
        case VKEY_UP: scroll(0, -2); break;
        case VKEY_DOWN: scroll(0, 2); break;
        case VKEY_ADD:
        case VKEY_OEM_PLUS: zoom_step(1); break;
        case VKEY_SUBTRACT:
        case VKEY_OEM_MINUS: zoom_step(-1); break;
        case VKEY_F12:
        case 'S': {
            char name[64];
            std::snprintf(name, sizeof(name), "smacx_%03d.bmp", g_ui_shots++);
            ui_screenshot((fs::path(g_opt.outdir) / name).string());
            break;
        }
        default: break;
        }
        if (ControlTurnA) {
            g_quit = true;
            return;
        }
    }
}

int32_t harness_turn_upkeep() {
    apply_observer_prefs();
    if (!g_opt.snapshots.empty()) snapshot();
    if (g_opt.human <= 0) {
        observer_pause();
        if (g_quit) {
            ControlTurnA = 1;  // leave control_turn; control_game then quits
            return 0;
        }
    }
    turn_upkeep();
    return 0;
}

// ---- starting the game (in place of the main menu) ----

const char* kDefaultFactions[7] = {"gaians", "hive", "univ", "morgan", "spartans", "believe", "peace"};
const int kWorldSize[5][2] = {{24, 48}, {32, 64}, {40, 80}, {44, 90}, {64, 128}};

// A new game without dialogs, as the harness's newgame command (src/app/headless.cpp), with the
// human player's faction set before the world and the factions are set up.
void new_game(std::string args) {
    int size = 2, ocean = 1, erosion = 1, orbit = 1, clouds = 1, natives = 1, diff = 3, width = 0, height = 0;
    uint32_t rules = 0;
    bool rules_set = false;
    int random_leaders = 0;
    std::string factions[7];
    for (int i = 0; i < 7; i++) factions[i] = kDefaultFactions[i];
    args += " ";
    for (size_t p = 0, e; (e = args.find(' ', p)) != std::string::npos; p = e + 1) {
        std::string tok = args.substr(p, e - p);
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        int n = std::atoi(v.c_str());
        if (k == "size") size = n;
        else if (k == "width") width = n;
        else if (k == "height") height = n;
        else if (k == "ocean") ocean = n;
        else if (k == "erosion") erosion = n;
        else if (k == "orbit") orbit = n;
        else if (k == "clouds") clouds = n;
        else if (k == "natives") natives = n;
        else if (k == "diff") diff = n;
        else if (k == "random") random_leaders = n;
        else if (k == "rules") {
            rules = (uint32_t)std::strtoul(v.c_str(), nullptr, 0);
            rules_set = true;
        } else if (k == "factions") {
            size_t q = 0;
            for (int i = 0; i < 7; i++) {
                size_t comma = v.find(',', q);
                factions[i] = v.substr(q, comma - q);
                if (comma == std::string::npos) break;
                q = comma + 1;
            }
        }
    }
    if (!width) {
        width = kWorldSize[size][0];
        height = kWorldSize[size][1];
    }
    MapGenerate = 1;
    MapSizePlanet = size;
    MapAreaX = 2 * width;
    MapAreaY = height;
    map_init();
    Path_init(&Paths);
    MapOceanCoverage = ocean;
    MapLandCoverage = 2 - ocean;
    MapErosiveForces = erosion;
    MapPlanetaryOrbit = orbit;
    MapCloudCover = clouds;
    MapNativeLifeForms = natives;
    DiffLevel = diff;
    GameRules = (int32_t)(((uint32_t)GameRules & 0xFFFF0000u) | (rules_set ? rules : default_rules()));
    GameState =
        (int32_t)((uint32_t)GameState & ~(STATE_RAND_FAC_LEADER_PERSONALITIES | STATE_RAND_FAC_LEADER_SOCIAL_AGENDA));
    if (random_leaders & 1) GameState |= STATE_RAND_FAC_LEADER_PERSONALITIES;
    if (random_leaders & 2) GameState |= STATE_RAND_FAC_LEADER_SOCIAL_AGENDA;
    for (int i = 1; i < 8; i++) {
        std::memset(MFactions[i].filename, 0, sizeof(MFactions[i].filename));
        std::snprintf(MFactions[i].filename, sizeof(MFactions[i].filename), "%s", factions[i - 1].c_str());
        read_faction(&MFactions[i], 0);
    }
    int human = g_opt.human;
    FactionStatus[0] = human > 0 ? (uint8_t)(1 << human) : 0;
    CurrentPlayerFaction = human > 0 ? human : 1;
    GameTimeControl = 0;
    VehCount = 0;
    world_build();
    setup_game(0);
    if (ExpansionEnabled) alien_start();
    std::fprintf(stderr, "new game: %dx%d, human faction %d (%s)\n", MapAreaX, MapAreaY, human,
                 human > 0 ? MFactions[human].noun_faction : "none");
}

void apply_seed() {
    if (!g_opt.seeded) return;
    game_srand(g_opt.seed_crt);
    game_reseed(g_opt.seed_game);
    engine_set_logic_clock_state(0);
}

// The game's main view once a game exists (desktop_init's part that smacx does: the art of the
// factions, the main view reset and centred on the cursor, its display options).
void show_game() {
    for (int32_t f = 1; f < 8; f++) load_faction_art(f);
    load_flags();
    // desktop_init(ControlTurnMove == 0) -> mapwin_system_init: the main view is reset and
    // centred on the cursor; MapWin_init_2 then sets its display options (main view of a
    // single-player game: 0x60010EE3, see 0x470A90).
    if (ControlTurnMove == 0) MapWin_clear(&MainConsole, 0);
    map_win(&MainConsole).options = 0x60010EE3;
    if (g_opt.zoom != 0x7FFF) map_win(&MainConsole).zoom = g_opt.zoom;
    if (g_opt.cx >= 0) {
        map_win(&MainConsole).center_x = g_opt.cx;
        map_win(&MainConsole).center_y = g_opt.cy;
    }
    ui_invalidate_map();
    std::fprintf(stderr, "view: zoom %d centre %d,%d options %08X viewer %d\n", map_win(&MainConsole).zoom,
                 map_win(&MainConsole).center_x, map_win(&MainConsole).center_y,
                 (uint32_t)map_win(&MainConsole).options, CurrentPlayerFaction);
}

// No --new / --save: the original's main menu (top_menu, src/ui/topmenu.cpp). With --seed the
// generators are seeded at the menu and again when the world of a new game is built (config_game's
// hook), as the harness's seed command before newgame, so a game set up in the menu equals the
// --new game of the same parameters and seed.
int32_t main_menu() {
    // Back from a game (Start New Game, a retired or finished game: control_game's reload) the
    // menu comes up again, as in the original.
    static bool started = false;
    if (!started) apply_seed();
    started = true;
    g_ui_before_world_build = apply_seed;
    int32_t r = top_menu(0);
    g_ui_before_world_build = nullptr;
    if (r) return r;
    // The game's human player (none: an observer game).
    int32_t f = CurrentPlayerFaction;
    g_opt.human = (FactionStatus[0] & (1 << f)) ? f : 0;
    std::fprintf(stderr, "game from the menu: turn %d, human faction %d (%s)\n", CurrentTurn, g_opt.human,
                 g_opt.human > 0 ? MFactions[g_opt.human].noun_faction : "none");
    if (g_opt.human <= 0 && g_opt.viewer >= 0) CurrentPlayerFaction = g_opt.viewer;
    apply_observer_prefs();
    show_game();
    return 0;
}

int32_t start_game_menu() {
    static bool started = false;
    if (started) return 1;  // back from a game: quit
    started = true;
    apply_seed();
    if (g_opt.new_game) {
        new_game(g_opt.new_args);
    } else {
        int r = load_daemon(g_opt.save.c_str(), 0);
        std::fprintf(stderr, "load %s -> %d (turn %d)\n", g_opt.save.c_str(), r, CurrentTurn);
        if (r) return 1;
        if (!g_opt.rng.empty()) {
            // The generators and the logic clock of a snapshot (its .rng file).
            unsigned crt = 0, game = 0, clock = 0;
            if (FILE* f = std::fopen(g_opt.rng.c_str(), "r")) {
                if (std::fscanf(f, "crt %u game %u clock %u", &crt, &game, &clock) == 3) {
                    crt_set_rand_state(crt);
                    game_reseed(game);
                    engine_set_logic_clock_state(clock);
                }
                std::fclose(f);
            }
        }
        if (g_opt.human > 0) {
            FactionStatus[0] = (uint8_t)(1 << g_opt.human);
            CurrentPlayerFaction = g_opt.human;
        } else {
            FactionStatus[0] = 0;  // every faction plays as a computer player (observer)
        }
    }
    // Observer: the viewer (as the harness: 0 after a new game, the save's after a load).
    if (g_opt.human <= 0) {
        if (g_opt.viewer >= 0) CurrentPlayerFaction = g_opt.viewer;
        else if (g_opt.new_game) CurrentPlayerFaction = 0;
    }
    apply_observer_prefs();
    show_game();
    return 0;
}

// ---- command line ----

bool parse_key(std::string tok, ScriptedKey* out) {
    out->vk = 0;
    out->command.clear();
    if (tok.rfind("SHOT", 0) == 0) {
        out->command = "shot" + (tok.size() > 5 ? " " + tok.substr(5) : std::string());
        return true;
    }
    if (tok.rfind("SAVE:", 0) == 0) {
        out->command = "save " + tok.substr(5);
        return true;
    }
    if (tok.rfind("BASEDUMP:", 0) == 0) {  // BASEDUMP:FILE, the base window's canvases (basewin.h)
        out->command = "basedump " + tok.substr(9);
        return true;
    }
    if (tok.rfind("CMD:", 0) == 0) {  // CMD:ID, Console_menu_proc(ID) (a menu command by its id, test aid)
        out->command = "cmd " + tok.substr(4);
        return true;
    }
    if (tok.rfind("WORKSHOP:", 0) == 0) {  // WORKSHOP:PICK, design_workshop(viewer, PICK) (unreachable in play, test aid)
        out->command = "workshop " + tok.substr(9);
        return true;
    }
    if (tok == "EDITSTATE") {  // the scenario editor's pane (editpane.cpp, test aid)
        out->command = "editstate";
        return true;
    }
    if (tok.rfind("BASE:", 0) == 0) {  // BASE:N, opens the base window of base N (test aid)
        out->command = "base " + tok.substr(5);
        return true;
    }
    if (tok == "SCREENSTATE" || tok == "FACTIONSTATE") {  // the open screen / the viewer's faction (test aids)
        out->command = tok == "SCREENSTATE" ? "screenstate" : "factionstate";
        return true;
    }
    if (tok == "PROTOSTATE") {  // the viewer's prototypes (designwin.cpp, test aid)
        out->command = "protostate";
        return true;
    }
    if (tok == "DIPLOSTATE") {  // the viewer's treaties, energy and techs (diplowin.h, test aid)
        out->command = "diplostate";
        return true;
    }
    if (tok == "INFOSTATE") {  // the info pane's displays and texts (infowin.h, test aid)
        out->command = "infostate";
        return true;
    }
    if (tok.rfind("INFOTICK:", 0) == 0) {  // INFOTICK:N, N steps of the info pane's timer (test aid)
        out->command = "infotick " + tok.substr(9);
        return true;
    }
    if (tok.rfind("PALTICK:", 0) == 0) {  // PALTICK:N, N steps of every palette cycle, then their colours (test aid)
        out->command = "paltick " + tok.substr(8);
        return true;
    }
    if (tok == "PLANSTATE") {  // the go-to list's map preview (planwin.h, test aid)
        out->command = "planstate";
        return true;
    }
    if (tok == "MENUSTATE") {  // the open menu column of the opening screens (topmenu.h, test aid)
        out->command = "menustate";
        return true;
    }
    if (tok == "TECHSTATE") {  // the viewer's research (techwin.h, test aid)
        out->command = "techstate";
        return true;
    }
    if (tok == "BASESTATE") {  // the open base's production, queue and workers on stderr (test aid)
        out->command = "basestate";
        return true;
    }
    if (tok.rfind("PLAYSOUND:", 0) == 0) {  // PLAYSOUND:FILE, decodes and plays a sound file (test aid)
        out->command = "playsound " + tok.substr(10);
        return true;
    }
    if (tok.rfind("MUSICRENDER:", 0) == 0) {  // MUSICRENDER:FILE:SECONDS, the music into a WAV (test aid)
        out->command = "musicrender " + tok.substr(12);
        return true;
    }
    if (tok.rfind("MOVIE:", 0) == 0) {  // MOVIE:NAME, amovie_project_name(NAME) (test aid)
        out->command = "movie " + tok.substr(6);
        return true;
    }
    if (tok.rfind("NETMSG:", 0) == 0) {  // NETMSG:LABEL, a network game's NetMsg box (test aid)
        out->command = "netmsg " + tok.substr(7);
        return true;
    }
    if (tok.rfind("POPUP:", 0) == 0) {  // POPUP:FILE/LABEL[/FLAGS[/PCX]], a popup by label (test aid)
        out->command = "popup " + tok.substr(6);
        return true;
    }
    if (tok.rfind("CLICK:", 0) == 0 || tok.rfind("DCLICK:", 0) == 0) {  // CLICK:XxY, a left click at screen
        // (X, Y) for a popup; DCLICK:XxY, the second click of a double click (modal windows)
        int32_t x = 0, y = 0;
        bool dbl = tok[0] == 'D';
        if (std::sscanf(tok.c_str() + (dbl ? 7 : 6), "%dx%d", &x, &y) != 2) return false;
        out->command = (dbl ? "dclick " : "click ") + std::to_string(x) + " " + std::to_string(y);
        return true;
    }
    if (tok.rfind("S-CLICK:", 0) == 0) {  // a left click with shift held
        if (!parse_key(tok.substr(2), out)) return false;
        out->vk |= 0x10000;
        return true;
    }
    if (tok.rfind("S-RCLICK:", 0) == 0 || tok.rfind("C-RCLICK:", 0) == 0) {  // with shift / ctrl held
        if (!parse_key(tok.substr(2), out)) return false;
        out->vk |= tok[0] == 'S' ? 0x10000 : 0x20000;
        return true;
    }
    if (tok.rfind("RCLICK:", 0) == 0 || tok.rfind("MOVE:", 0) == 0) {  // a right click / mouse move
        int32_t x = 0, y = 0;
        size_t c = tok.find(':');
        if (std::sscanf(tok.c_str() + c + 1, "%dx%d", &x, &y) != 2) return false;
        out->command = (tok[0] == 'R' ? "rclick " : "move ") + std::to_string(x) + " " + std::to_string(y);
        return true;
    }
    if (tok.rfind("ZOOMAT:", 0) == 0) {  // ZOOMAT:XxY, zoom out at the mouse there (test aid, win.cpp)
        int32_t x = 0, y = 0;
        if (std::sscanf(tok.c_str() + 7, "%dx%d", &x, &y) != 2) return false;
        out->command = "zoomat " + std::to_string(x) + " " + std::to_string(y);
        return true;
    }
    if (tok.rfind("DRAG:", 0) == 0) {  // DRAG:X1xY1:X2xY2, the go-to drag of a held left button
        int32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (std::sscanf(tok.c_str() + 5, "%dx%d:%dx%d", &x1, &y1, &x2, &y2) != 4) return false;
        out->command = "drag " + std::to_string(x1) + " " + std::to_string(y1) + " " + std::to_string(x2) + " " +
                       std::to_string(y2);
        // DRAG:X1xY1:X2xY2:FILE: a screenshot before the button is released (the path preview).
        size_t c3 = tok.find(':', tok.find(':', 5) + 1);
        if (c3 != std::string::npos) out->command += " " + tok.substr(c3 + 1);
        return true;
    }
    if (tok.rfind("WAITFOR:", 0) == 0) {  // WAITFOR:TEXT, until a network note contains TEXT (test aid)
        out->command = "waitfor " + tok.substr(8);
        return true;
    }
    if (tok.rfind("WAIT:", 0) == 0) {  // WAIT:MS, real time passing, then a present (test aid)
        out->command = "wait " + tok.substr(5);
        return true;
    }
    if (tok.rfind("PIXEL:", 0) == 0) {  // PIXEL:XxY, the screen's colour there (test aid)
        int32_t x = 0, y = 0;
        if (std::sscanf(tok.c_str() + 6, "%dx%d", &x, &y) != 2) return false;
        out->command = "pixel " + std::to_string(x) + " " + std::to_string(y);
        return true;
    }
    if (tok == "QUIT" || tok == "UNITS") {
        out->command = tok == "QUIT" ? "quit" : "units";
        return true;
    }
    int32_t mods = 0;
    while (tok.size() > 2 && tok[1] == '-' && (tok[0] == 'C' || tok[0] == 'S' || tok[0] == 'A')) {
        mods |= tok[0] == 'C' ? 0x20000 : (tok[0] == 'S' ? 0x10000 : 0x40000);
        tok = tok.substr(2);
    }
    struct Named {
        const char* name;
        int32_t vk;
    };
    static const Named kNamed[] = {{"LEFT", VKEY_LEFT},   {"RIGHT", VKEY_RIGHT}, {"UP", VKEY_UP},
                                   {"DOWN", VKEY_DOWN},   {"HOME", VKEY_HOME},   {"END", VKEY_END},
                                   {"PGUP", VKEY_PRIOR},  {"PGDN", VKEY_NEXT},   {"ENTER", VKEY_RETURN},
                                   {"SPACE", VKEY_SPACE}, {"TAB", VKEY_TAB},     {"BACK", VKEY_BACK},
                                   {"ESC", VKEY_ESCAPE},  {"PLUS", VKEY_ADD},    {"MINUS", VKEY_SUBTRACT},
                                   {"COMMA", 0xBC},       {"ALT", VKEY_MENU},    {"PAUSE", 0x13}};
    for (const Named& n : kNamed) {
        if (tok == n.name) {
            out->vk = n.vk | mods;
            return true;
        }
    }
    if (tok.size() >= 2 && tok[0] == 'F' && std::isdigit((unsigned char)tok[1])) {
        out->vk = (VKEY_F1 + std::atoi(tok.c_str() + 1) - 1) | mods;
        return true;
    }
    if (tok.size() == 3 && tok.rfind("KP", 0) == 0 && std::isdigit((unsigned char)tok[2])) {
        out->vk = (VKEY_NUMPAD0 + tok[2] - '0') | mods;
        return true;
    }
    if (tok.size() != 1) return false;
    char c = tok[0];
    if (c >= 'a' && c <= 'z') out->vk = (c - 'a' + 'A') | mods;
    else if (c >= 'A' && c <= 'Z') out->vk = c | mods | 0x10000;
    else if (c >= '0' && c <= '9') out->vk = c | mods;
    else {
        // US keyboard: the key (and shift) typing the character.
        static const char kPlain[] = ";=,-./`[\\]'";
        static const char kShift[] = ":+<_>?~{|}\"";
        static const int32_t kVk[] = {0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE};
        const char* p = std::strchr(kPlain, c);
        const char* s = std::strchr(kShift, c);
        if (p && *p) out->vk = kVk[p - kPlain] | mods;
        else if (s && *s) out->vk = kVk[s - kShift] | mods | 0x10000;
        else return false;
    }
    return true;
}

int usage() {
    std::fprintf(
        stderr,
        "usage: smacx [--new [k=v ...] | --save FILE.sav] [--human N] [--viewer N] [--seed CRT,GAME|clock]\n"
        "             [--keys K1,K2,..] [--exit-after-keys] [--animate] [--turns N] [--shot FILE.bmp] [--snapshots DIR] [--rng "
        "FILE]\n"
        "             [--zoom Z] [--center X,Y] [--size WxH] [--hidden] [--outdir DIR] [--gamedir DIR]\n"
        "             [--image FILE] [--ini FILE] [--dump FILE.smxmap [--like REF.smxmap] [--scroll-from X,Y]\n"
        "             [--dump-options HEX] [--dump-radius X,Y,R]]\n"
        "             [--nosound] [--original-bugs]\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--save") g_opt.save = next();
        else if (a == "--new") {
            g_opt.new_game = true;
            while (i + 1 < argc && std::strchr(argv[i + 1], '=') && argv[i + 1][0] != '-')
                g_opt.new_args += std::string(argv[++i]) + " ";
        } else if (a == "--human") g_opt.human = std::atoi(next().c_str());
        else if (a == "--seed") {
            std::string v = next();
            g_opt.seed_given = true;
            g_opt.seeded = v != "clock";
            if (g_opt.seeded && std::sscanf(v.c_str(), "%u,%u", &g_opt.seed_crt, &g_opt.seed_game) != 2) {
                std::fprintf(stderr, "--seed: CRT,GAME or clock\n");
                return 2;
            }
        } else if (a == "--gamedir") g_opt.gamedir = next();
        else if (a == "--image") g_opt.image = next();
        else if (a == "--ini") g_opt.ini = next();
        else if (a == "--outdir") g_opt.outdir = next();
        else if (a == "--viewer") g_opt.viewer = std::atoi(next().c_str());
        else if (a == "--zoom") g_opt.zoom = std::atoi(next().c_str());
        else if (a == "--turns") g_opt.turns = std::atoi(next().c_str());
        else if (a == "--shot") g_opt.shot = next();
        else if (a == "--dump") g_opt.dump = fs::absolute(next()).string();
        else if (a == "--basedump") g_opt.basedump = next();
        else if (a == "--like") g_opt.like = fs::absolute(next()).string();
        else if (a == "--scroll-from") std::sscanf(next().c_str(), "%d,%d", &g_opt.scroll_x, &g_opt.scroll_y);
        else if (a == "--dump-options") g_opt.dump_options = (unsigned)std::strtoul(next().c_str(), nullptr, 16);
        else if (a == "--dump-radius")
            std::sscanf(next().c_str(), "%d,%d,%d", &g_opt.radius_x, &g_opt.radius_y, &g_opt.radius_r);
        else if (a == "--snapshots") g_opt.snapshots = next();
        else if (a == "--rng") g_opt.rng = fs::absolute(next()).string();
        else if (a == "--hidden") g_opt.hidden = true;
        else if (a == "--nosound") g_opt.nosound = true;
        else if (a == "--exit-after-keys") g_opt.exit_after_keys = true;
        else if (a == "--animate") g_opt.animate = true;
        else if (a == "--original-bugs") g_opt.original_bugs = true;
        else if (a == "--keys") {
            std::string list = next() + ",";
            for (size_t p = 0, c; (c = list.find(',', p)) != std::string::npos; p = c + 1) {
                std::string tok = list.substr(p, c - p);
                if (tok.empty()) continue;
                ScriptedKey k;
                if (!parse_key(tok, &k)) {
                    std::fprintf(stderr, "unknown key '%s'\n", tok.c_str());
                    return 2;
                }
                g_opt.keys.push_back(k);
            }
        } else if (a == "--center") std::sscanf(next().c_str(), "%d,%d", &g_opt.cx, &g_opt.cy);
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &g_opt.width, &g_opt.height);
        else return usage();
    }
    if (!g_opt.save.empty() && g_opt.new_game) return usage();
    bool menu = g_opt.save.empty() && !g_opt.new_game;  // the main menu
    // Scripted or hidden runs (tests) default to fixed seeds; interactive play seeds from the clock.
    if (!g_opt.seed_given) g_opt.seeded = !g_opt.keys.empty() || g_opt.hidden;
    if (g_opt.new_game && g_opt.human < 0) g_opt.human = 1;
    if (!g_opt.save.empty()) g_opt.save = fs::absolute(g_opt.save).string();
    if (!g_opt.shot.empty()) g_opt.shot = fs::absolute(g_opt.shot).string();
    if (!g_opt.snapshots.empty()) {
        g_opt.snapshots = fs::absolute(g_opt.snapshots).string();
        fs::create_directories(g_opt.snapshots);
    }
    std::string ini_copy;  // the ini the game reads and writes
    if (!g_opt.outdir.empty()) {
        // Tests and the harness: the repository's files; the ini is a fresh copy of --ini at every
        // start, so that a run's preferences never carry over into the next run in the same outdir.
        if (g_opt.gamedir.empty()) g_opt.gamedir = "game";
        if (g_opt.image.empty()) g_opt.image = "src/data/data_image.bin";
        if (g_opt.ini.empty()) g_opt.ini = "harness/oracle/runtime/Alpha Centauri.Ini";
        g_opt.outdir = fs::absolute(g_opt.outdir).string();
        fs::create_directories(g_opt.outdir);
        ini_copy = (fs::path(g_opt.outdir) / "Alpha Centauri.Ini").string();
        std::error_code ec;
        fs::copy_file(g_opt.ini, ini_copy, fs::copy_options::overwrite_existing, ec);
        // Saved games too go below the outdir (the game folder is only read).
        platform::set_saves_dir((fs::path(g_opt.outdir) / "saves").string().c_str());
    } else {
        // The game as players run it: the per-user directory and the installed game.
        std::string udir = gamedata::user_dir();
        if (udir.empty()) {
            gamedata::show_error("Cannot create the per-user settings folder.");
            return 1;
        }
        g_opt.outdir = fs::absolute(udir).string();
        while (g_opt.outdir.size() > 1 && (g_opt.outdir.back() == '/' || g_opt.outdir.back() == '\\'))
            g_opt.outdir.pop_back();
        if (g_opt.gamedir.empty()) {
            g_opt.gamedir = gamedata::find_game_dir(udir);
            if (g_opt.gamedir.empty()) return 3;
        } else {
            std::string why;
            if (!gamedata::check_game_dir(g_opt.gamedir, &why)) {
                gamedata::show_error(why);
                return 3;
            }
        }
        if (g_opt.image.empty()) {
            g_opt.image = gamedata::data_image(g_opt.gamedir, udir);
            if (g_opt.image.empty()) return 1;
        }
        if (g_opt.ini.empty()) {
            ini_copy = (fs::path(udir) / "Alpha Centauri.Ini").string();
            gamedata::seed_ini(ini_copy, udir);
        } else {
            ini_copy = fs::absolute(g_opt.ini).string();
        }
        platform::set_saves_dir((fs::path(udir) / "saves").string().c_str());
    }
    if (!mem::init(fs::absolute(g_opt.image).string().c_str())) {
        std::fprintf(stderr, "cannot load data image %s\n", g_opt.image.c_str());
        return 2;
    }
    fs::current_path(g_opt.gamedir);
    platform::set_game_dir(".");
    engine_static_init(ini_copy.c_str());
    // The original's bugs or their fixes (engine/compat.h): --original-bugs, else the ini's key.
    {
        char buf[16] = "";
        win32::GetPrivateProfileStringA("Alpha Centauri", "Original Bugs", "0", buf, sizeof(buf), ini_copy.c_str());
        compat_set_original_bugs(g_opt.original_bugs || std::atoi(buf) != 0);
        g_original_bugs_forced = g_opt.original_bugs;
    }
    // The harness's deterministic clock (engine/clock.h): the same keys give the same game.
    engine_set_deterministic(true);
    // WinMain: the screen size (GetSystemMetrics) the windows are laid out for; config_popups
    // copies it into CanvasWidth/CanvasHeight.
    ScreenWidth = g_opt.width;
    ScreenHeight = g_opt.height;
    if (!platform::video_init("Sid Meier's Alpha Centauri", g_opt.width, g_opt.height, g_opt.hidden)) return 1;
    // WinMain's init_sound (soundx.dll, DirectSound): the SDL audio device unless --nosound.
    sound_startup(!g_opt.nosound);
    // system_init's UI part (load_fixed_sprites -> load_palette, load_sprites -> load_terrain)
    // and mapwin_system_init's lighting table.
    load_palette();
    load_cycling();
    load_voxels();
    colortables_init();
    load_terrain();
    load_vehicles();
    MapWin_compute_lighting_table();
    ui_screen_init(g_opt.width, g_opt.height);
    g_ui_outdir = g_opt.outdir;
    g_ui_exit_when_script_done = g_opt.exit_after_keys || g_opt.hidden;
    if (const char* e = std::getenv("SMACX_ANIMATE")) g_opt.animate |= std::atoi(e) != 0;
    g_ui_unattended = g_ui_exit_when_script_done && !g_opt.animate;
    diplowin_set_animate(!g_opt.seeded || g_opt.animate);
    ui_script_keys(g_opt.keys);
    g_harness_top_menu = menu ? main_menu : start_game_menu;
    g_harness_turn_upkeep = harness_turn_upkeep;
    // An online game: the state (no save) before each turn_upkeep, to compare the players' copies.
    g_harness_net_turn = [] {
        if (g_opt.snapshots.empty()) return;
        char name[32];
        std::snprintf(name, sizeof(name), "turn_%03d", CurrentTurn);
        std::string base = (fs::path(g_opt.snapshots) / name).string();
        write_state(base + ".state");
        if (FILE* f = std::fopen((base + ".rng").c_str(), "w")) {
            std::fprintf(f, "crt %u\ngame %u\nturn %d\n", crt_rand_state(), game_random_state(), CurrentTurn);
            std::fclose(f);
        }
        // SMACX_NET_DESYNC=TURN (test aid): this copy's last unit changes its morale at that turn, a desync
        // the other players' checksums find.
        const char* d = std::getenv("SMACX_NET_DESYNC");
        if (d && std::atoi(d) == CurrentTurn && VehCount > 0) {
            Vehs[VehCount - 1].morale ^= 1;
            std::fprintf(stderr, "net: desync provoked (unit %d, turn %d)\n", VehCount - 1, CurrentTurn);
        }
    };
    // Observer games: the pause is taken before turn_upkeep (harness_turn_upkeep), not in the
    // round-end Console_human_turn.
    g_harness_observer_pause = [] { return 0; };
    // The end of a game (show_replay): the harness's runs exit; a game from the main menu goes back to
    // it (control_game's reload, ControlTurnB).
    if (menu) g_harness_game_end = [] { std::fprintf(stderr, "game over (turn %d)\n", CurrentTurn); };
    else
        g_harness_game_end = [] {
            std::fprintf(stderr, "game over (turn %d)\n", CurrentTurn);
            std::exit(0);
        };
    control_game();
    sound_shutdown();
    platform::video_shutdown();
    return 0;
}
