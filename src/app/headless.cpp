// Headless runner for the reconstructed engine.
//
// Executes the same command scripts as the oracle (harness/oracle/oracle.cpp) and writes snapshots
// in the same format, so harness/compare.py can diff the two. Usage:
//
//   smacx_headless <script.cmd> [--outdir DIR] [--gamedir DIR] [--image FILE]
//
// --gamedir is the original game's data directory (default: game); the runner chdirs into it.
// The game runs as in the original: control_game (0x52AA30) boots and calls the script in place of
// the main menu (top_menu).
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "engine/ai.h"
#include "engine/clock.h"
#include "engine/game.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/strings.h"
#include "engine/setup.h"
#include "engine/turn.h"
#include "engine/world.h"
#include "rules_dump.h"

namespace fs = std::filesystem;

static std::string g_outdir = "headless_out";
static int g_turn_limit = -1;
static int g_viewer = 0;
static FILE* g_log = nullptr;
static bool g_game_started = false;  // set by newgame/load

static void logf_(const char* fmt, ...) {
    if (!g_log) {
        fs::create_directories(g_outdir);
        g_log = std::fopen((g_outdir + "/oracle.log").c_str(), "w");
        if (!g_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fflush(g_log);
}

// Mirrors oracle.cpp apply_observer_prefs().
static void apply_observer_prefs() {
    const uint32_t clear = 0x1 | 0x2 | 0x20 | 0x80 | 0x10000000;
    const uint32_t set = 0x40000 | 0x8;
    GamePreferences = (int32_t)(((uint32_t)GamePreferences & ~clear) | set);
    AlphaIniPrefs.preferences = (int32_t)(((uint32_t)AlphaIniPrefs.preferences & ~clear) | set);
    GameMorePreferences = (int32_t)((uint32_t)GameMorePreferences & ~(0x10u | 0x4000u | 0x8000u | 0x20000u));
}

// ---- snapshots (format: see harness/state_regions.h) ----
static void write_state(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto put = [&](uint32_t addr, const void* data, uint32_t size) {
        std::fwrite(&addr, 4, 1, f);
        std::fwrite(&size, 4, 1, f);
        if (size) std::fwrite(data, 1, size, f);
    };
    auto i32 = [](uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); };
#define REGION(name, addr, size) put(addr, mem::host(addr), size);
#define REGION_VAR(name, addr, count_addr, elem)                                                     \
    {                                                                                                \
        uint32_t n = (uint32_t)i32(count_addr) * (elem);                                             \
        const void* src = (addr) == 0x10000000   ? (const void*)MapTiles                             \
                          : (addr) == 0x20000000 ? (const void*)MapAbstract                          \
                                                 : (const void*)mem::host(addr);                     \
        put(addr, src, src ? n : 0);                                                                 \
    }
#define MASK(addr, size)
#include "state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
    std::fclose(f);
}

static void snapshot(const std::string& name) {
    write_state(g_outdir + "/" + name + ".state");
    uint32_t crt = crt_rand_state(), game = game_random_state();
    if (FILE* f = std::fopen((g_outdir + "/" + name + ".rng").c_str(), "w")) {
        std::fprintf(f, "crt %u\ngame %u\nclock %u\nturn %d\n", crt, game, engine_logic_clock_state(), CurrentTurn);
        std::fclose(f);
    }
    // Before a game exists (boot snapshots) no .sav is written: compare.py ignores saves by default.
    int r = g_game_started ? save_daemon((g_outdir + "/" + name + ".sav").c_str()) : -1;
    crt_set_rand_state(crt);
    game_reseed(game);
    logf_("snapshot %s turn=%d crt=%u game=%u save=%d\n", name.c_str(), CurrentTurn, crt, game, r);
}

static void before_turn_upkeep() {
    apply_observer_prefs();
    char name[64];
    std::snprintf(name, sizeof(name), "turn_%03d", CurrentTurn);
    snapshot(name);
    if (g_turn_limit >= 0 && CurrentTurn >= g_turn_limit) {
        logf_("turn limit reached, exiting\nexit=0\n");
        std::exit(0);
    }
}

// ---- trace (mirrors oracle.cpp): 1 logs the RNGs at each faction step, 2 also dumps the state
// (step_TTT_F_{a_upkeep,b_enemy}), 3 <turn> dumps it before every enemy_veh call of that turn ----
static int g_trace = 0, g_trace_turn = -1, g_trace_seq = 0;

static void write_step(const char* name) {
    write_state(g_outdir + "/" + name + ".state");
    if (FILE* f = std::fopen((g_outdir + "/" + name + ".rng").c_str(), "w")) {
        std::fprintf(f, "crt %u\ngame %u\nclock %u\nturn %d\n", crt_rand_state(), game_random_state(),
                     engine_logic_clock_state(), CurrentTurn);
        std::fclose(f);
    }
}

static void trace_point(const char* what, int32_t f) {
    if (!g_trace || g_trace >= 4) return;
    logf_("%s %d turn=%d crt=%u game=%u\n", what, f, CurrentTurn, crt_rand_state(), game_random_state());
    if (g_trace >= 2) {
        char name[64];
        std::snprintf(name, sizeof(name), "step_%03d_%d_%s", CurrentTurn, f, what);
        write_step(name);
    }
}

static int32_t harness_faction_upkeep(int32_t f) {
    trace_point("a_upkeep", f);
    faction_upkeep(f);
    return 0;
}

static int32_t harness_enemy_turn(int32_t f) {
    trace_point("b_enemy", f);
    enemy_turn(f);
    return 0;
}

static int32_t harness_enemy_veh(int32_t veh_id) {
    if (g_trace == 5 && CurrentTurn == g_trace_turn) {
        // Not in the oracle: only logs the RNGs before each enemy_veh call (no state files).
        logf_("enemy_veh c%05d veh%d crt=%u game=%u\n", g_trace_seq++, veh_id, crt_rand_state(), game_random_state());
    } else if (g_trace >= 3 && g_trace < 5 && CurrentTurn == g_trace_turn) {
        char name[64];
        std::snprintf(name, sizeof(name), "step_%03d_9_c%05d_veh%d", CurrentTurn, g_trace_seq++, veh_id);
        write_step(name);
    }
    return enemy_veh(veh_id);
}

// ---- newgame (mirrors oracle.cpp cmd_newgame) ----
static const char* kDefaultFactions[7] = {"gaians", "hive", "univ", "morgan", "spartans", "believe", "peace"};
static const int kWorldSize[5][2] = {{24, 48}, {32, 64}, {40, 80}, {44, 90}, {64, 128}};

static void cmd_newgame(char* args) {
    int size = 2, ocean = 1, erosion = 1, orbit = 1, clouds = 1, natives = 1, diff = 3;
    int width = 0, height = 0;
    uint32_t rules = 0;
    bool rules_set = false;
    int random_leaders = 0;  // bit 0: random personalities (GameState 0x800000), bit 1: random agendas
    std::string factions[7];
    for (int i = 0; i < 7; i++) factions[i] = kDefaultFactions[i];
    for (char* tok = std::strtok(args, " \t"); tok; tok = std::strtok(nullptr, " \t")) {
        char* eq = std::strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        std::string k = tok, v = eq + 1;
        if (k == "size") size = std::atoi(v.c_str());
        else if (k == "width") width = std::atoi(v.c_str());
        else if (k == "height") height = std::atoi(v.c_str());
        else if (k == "ocean") ocean = std::atoi(v.c_str());
        else if (k == "erosion") erosion = std::atoi(v.c_str());
        else if (k == "orbit") orbit = std::atoi(v.c_str());
        else if (k == "clouds") clouds = std::atoi(v.c_str());
        else if (k == "natives") natives = std::atoi(v.c_str());
        else if (k == "diff") diff = std::atoi(v.c_str());
        else if (k == "random") random_leaders = std::atoi(v.c_str());
        else if (k == "rules") {
            rules = (uint32_t)std::strtoul(v.c_str(), nullptr, 0);
            rules_set = true;
        } else if (k == "factions") {
            size_t pos = 0;
            for (int i = 0; i < 7; i++) {
                size_t comma = v.find(',', pos);
                factions[i] = v.substr(pos, comma - pos);
                if (comma == std::string::npos) break;
                pos = comma + 1;
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
    GameState = (int32_t)((uint32_t)GameState & ~0x1800000u);
    if (random_leaders & 1) GameState |= STATE_RAND_FAC_LEADER_PERSONALITIES;
    if (random_leaders & 2) GameState |= STATE_RAND_FAC_LEADER_SOCIAL_AGENDA;
    for (int i = 1; i < 8; i++) {
        std::memset(MFactions[i].filename, 0, sizeof(MFactions[i].filename));
        std::snprintf(MFactions[i].filename, sizeof(MFactions[i].filename), "%s", factions[i - 1].c_str());
        read_faction(&MFactions[i], 0);
    }
    FactionStatus[0] = 0;
    CurrentPlayerFaction = 1;
    GameTimeControl = 0;
    VehCount = 0;
    logf_("newgame size=%d %dx%d ocean=%d erosion=%d orbit=%d clouds=%d natives=%d diff=%d rules=%08X crt=%u game=%u\n",
          size, MapAreaX, MapAreaY, ocean, erosion, orbit, clouds, natives, diff, (uint32_t)GameRules,
          crt_rand_state(), game_random_state());
    world_build();
    setup_game(0);
    if (ExpansionEnabled) alien_start();
    FactionStatus[0] = 0;
    CurrentPlayerFaction = g_viewer;
    apply_observer_prefs();
    logf_("newgame done mapseed=%d turn=%d crt=%u game=%u\n", MapRandomSeed, CurrentTurn, crt_rand_state(),
          game_random_state());
}

// The command script, run by control_game in place of the main menu (top_menu). Returns 0 to
// start the game loop ("turns"), 1 to quit.
static FILE* g_script = nullptr;
static int32_t run_script() {
    char line[16384];
    while (std::fgets(line, sizeof(line), g_script)) {
        line[std::strcspn(line, "\r\n")] = 0;
        char* cmd = line + std::strspn(line, " \t");
        if (!*cmd || *cmd == '#') continue;
        char* args = cmd + std::strcspn(cmd, " \t");
        if (*args) *args++ = 0;
        args += std::strspn(args, " \t");
        logf_("> %s %s\n", cmd, args);
        if (!std::strcmp(cmd, "outdir") || !std::strcmp(cmd, "patch")) {
            // outdir is set on the command line; patch only applies to the hybrid oracle
        } else if (!std::strcmp(cmd, "trace")) {
            std::sscanf(args, "%d %d", &g_trace, &g_trace_turn);
            if (g_trace) {
                g_harness_faction_upkeep = harness_faction_upkeep;
                g_harness_enemy_turn = harness_enemy_turn;
            }
            if (g_trace >= 3) g_harness_enemy_veh = harness_enemy_veh;
        } else if (!std::strcmp(cmd, "seed")) {
            unsigned a = 0, b = 0;
            std::sscanf(args, "%u %u", &a, &b);
            game_srand(a);
            game_reseed(b);
            engine_set_logic_clock_state(0);
        } else if (!std::strcmp(cmd, "newgame")) {
            g_game_started = true;
            cmd_newgame(args);
        } else if (!std::strcmp(cmd, "load")) {
            g_game_started = true;
            logf_("load %s -> %d\n", args, load_daemon(args, 0));
        } else if (!std::strcmp(cmd, "save")) {
            uint32_t crt = crt_rand_state(), game = game_random_state();
            int r = save_daemon(args);
            crt_set_rand_state(crt);
            game_reseed(game);
            logf_("save %s -> %d\n", args, r);
        } else if (!std::strcmp(cmd, "dumprules")) {
            FILE* df = std::fopen((g_outdir + "/" + args).c_str(), "w");
            if (df) {
                rules_dump(df);
                std::fclose(df);
            }
            logf_("dumprules %s -> %d\n", args, df != nullptr);
        } else if (!std::strcmp(cmd, "snapshot")) {
            snapshot(args);
        } else if (!std::strcmp(cmd, "viewer")) {
            g_viewer = std::atoi(args);
            CurrentPlayerFaction = g_viewer;
        } else if (!std::strcmp(cmd, "turns")) {
            g_turn_limit = CurrentTurn + std::atoi(args);
            return 0;  // into the game loop (desktop_init, control_turn)
        } else if (!std::strcmp(cmd, "quit")) {
            break;
        } else {
            logf_("unknown command %s\n", cmd);
        }
    }
    return 1;
}

// control_turn -> turn_upkeep: snapshot (and turn limit) first.
static int32_t harness_turn_upkeep() {
    before_turn_upkeep();
    turn_upkeep();
    return 0;
}

int main(int argc, char** argv) {
    std::string script, gamedir = "game", image = "src/data/data_image.bin";
    std::string ini = "harness/oracle/runtime/Alpha Centauri.Ini";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--outdir" && i + 1 < argc) g_outdir = argv[++i];
        else if (a == "--gamedir" && i + 1 < argc) gamedir = argv[++i];
        else if (a == "--image" && i + 1 < argc) image = argv[++i];
        else if (a == "--ini" && i + 1 < argc) ini = argv[++i];
        else script = a;
    }
    if (script.empty()) {
        std::fprintf(stderr, "usage: smacx_headless <script.cmd> [--outdir DIR] [--gamedir DIR] [--image FILE] [--ini FILE]\n");
        return 2;
    }
    g_outdir = fs::absolute(g_outdir).string();
    script = fs::absolute(script).string();
    if (!mem::init(fs::absolute(image).string().c_str())) {
        std::fprintf(stderr, "cannot load data image %s\n", image.c_str());
        return 2;
    }
    FILE* f = std::fopen(script.c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", script.c_str());
        return 2;
    }
    // The preferences file is modified by the game: work on a copy, as the oracle does.
    fs::create_directories(g_outdir);
    std::string ini_copy = g_outdir + "/Alpha Centauri.Ini";
    std::error_code ec;
    fs::copy_file(ini, ini_copy, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        std::fprintf(stderr, "cannot copy preferences %s: %s\n", ini.c_str(), ec.message().c_str());
        return 2;
    }
    fs::current_path(gamedir);
    engine_static_init(ini_copy.c_str());  // CRT static data + pfncWinMain state
    engine_set_deterministic(true);
    g_script = f;
    g_harness_turn_upkeep = harness_turn_upkeep;
    g_harness_top_menu = run_script;
    // pfncWinMain -> control_game: boot (system_init, game_init, preferences), then the script in
    // place of the main menu; "turns" returns into the game loop.
    control_game();
    std::fclose(f);
    logf_("exit=0\n");
    return 0;
}
