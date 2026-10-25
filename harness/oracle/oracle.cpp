/*
 * oracle.dll / hybrid.dll — injected into the original terranx.exe (v2.0 GOG) to make it
 * scriptable and deterministic for differential testing against the reconstruction.
 *
 * oracle.dll runs the original code only. hybrid.dll (ORACLE_WITH_ENGINE) additionally contains
 * the reconstructed engine and can jump-patch reconstructed functions over the originals.
 *
 * Instead of showing the top menu, control_game calls oracle_top_menu(), which executes the
 * command script `oracle.cmd`. The command language is shared with src/app/headless.cpp; see
 * harness/README.md for the full reference.
 */
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Rule-table dump (dumprules); included before the address macros below, which reuse some of
 * the engine's global names. */
#include "rules_dump.h"

#ifdef ORACLE_WITH_ENGINE
#include "engine/clock.h"
#include "engine/hybrid_patch.h"
#include "fcompare.h"
/* Harness hooks of the reconstructed control_game/control_turn (engine/turn.h): they make those
 * call the oracle's hooks below where the original's call sites are patched. */
extern int (*g_harness_turn_upkeep)();
extern int (*g_harness_faction_upkeep)(int);
extern int (*g_harness_enemy_turn)(int);
extern int (*g_harness_human_turn)();
extern int (*g_harness_observer_pause)();
extern int (*g_harness_top_menu)();
/* Reconstructed enemy_turn -> enemy_veh and enemy_veh's message pump (engine/ai.h). */
extern int (*g_harness_enemy_veh)(int);
extern void (*g_harness_enemy_veh_pump)();
/* Reconstructed first/single wants_to_attack calls (engine/diplomacy.h): oracle_wta_poison_thunk. */
extern int(__cdecl *g_harness_wants_to_attack_fresh)(int, int, int);
extern "C" void oracle_wta_poison_thunk();
static void hook_enemy_veh_pump_void();
/* Reconstructed CouncilWin_session -> the window's modal loop (engine/council.h). */
extern int (*g_harness_council_modal)(unsigned char *);
#endif

/* ---- original functions ---- */
#define ORIG(type, name, addr) static type const name = (type)(addr)
typedef int(__cdecl *f_none)(void);
typedef int(__cdecl *f_int)(int);
typedef int(__cdecl *f_ptr)(const char *);
typedef int(__cdecl *f_ptr_int)(const char *, int);
typedef int(__cdecl *f_ptr2)(void *, int);
typedef void *(__cdecl *f_getptd)(void);
typedef void(__thiscall *f_this)(void *);

ORIG(f_int, o_game_srand, 0x538FB0);
ORIG(f_int, o_game_reseed, 0x6257E0);
ORIG(f_getptd, o_crt_getptd, 0x6491C3);
ORIG(f_none, o_map_init, 0x590ED0);
ORIG(f_this, o_Path_init, 0x59A220);
ORIG(f_ptr2, o_read_faction, 0x586090);
ORIG(f_none, o_world_build, 0x5C86E0);
ORIG(f_int, o_setup_game, 0x5B3920);
ORIG(f_none, o_alien_start, 0x588F90);
ORIG(f_ptr, o_save_daemon, 0x5A94F0);
ORIG(f_ptr_int, o_load_daemon, 0x5A9760);
ORIG(f_none, o_turn_upkeep, 0x5258C0);
ORIG(f_none, o_default_rules, 0x59DB30);
ORIG(f_int, o_faction_upkeep, 0x527290);
ORIG(f_int, o_enemy_turn, 0x579510);
ORIG(f_none, o_Console_human_turn, 0x514050);

#define G(type, addr) (*(type *)(addr))
#define GameRandomState G(uint32_t, 0x9BB568)
#define CurrentPlayerFaction G(int32_t, 0x939284)
#define MFactions ((uint8_t *)0x946A50)
#define MFACTION_SIZE 0x59C
#define MapRandomSeed G(int32_t, 0x949878)
#define MapSizePlanet G(int32_t, 0x94A2A0)
#define MapOceanCoverage G(int32_t, 0x94A2A4)
#define MapLandCoverage G(int32_t, 0x94A2A8)
#define MapErosiveForces G(int32_t, 0x94A2AC)
#define MapPlanetaryOrbit G(int32_t, 0x94A2B0)
#define MapCloudCover G(int32_t, 0x94A2B4)
#define MapNativeLifeForms G(int32_t, 0x94A2B8)
#define MapGenerate G(int32_t, 0x945820) /* world_build requested by size_of_planet */
#define Paths ((void *)0x945B00)
#define ExpansionEnabled G(int32_t, 0x9A6488)
#define GameRules G(uint32_t, 0x9A649C)
#define GameState G(uint32_t, 0x9A64C0)
#define DiffLevel G(int32_t, 0x9A64C4)
#define VehCount G(int32_t, 0x9A64C8)
#define CurrentTurn G(int32_t, 0x9A64D4)
#define GameTimeControl G(int32_t, 0x9A64E0)
#define FactionStatus ((uint8_t *)0x9A64E8)
#define MapAreaX G(int32_t, 0x949870)
#define MapAreaY G(int32_t, 0x949874)
#define GamePreferences G(uint32_t, 0x9A6490)
#define GameMorePreferences G(uint32_t, 0x9A6494)
#define AlphaIniPrefsFlags G(uint32_t, 0x94B464)

/* ---- logging ---- */
static char g_outdir[MAX_PATH] = "oracle_out";
static FILE *g_log;
static int g_turn_limit = -1;
static int g_viewer = 0;
static int g_trace = 0;

static void logf_(const char *fmt, ...) {
    if (!g_log) {
        char p[MAX_PATH];
        CreateDirectoryA(g_outdir, NULL);
        snprintf(p, sizeof(p), "%s\\oracle.log", g_outdir);
        g_log = fopen(p, "a");
        if (!g_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static uint32_t crt_state() { return ((uint32_t *)o_crt_getptd())[5]; }
static void crt_set_state(uint32_t v) { ((uint32_t *)o_crt_getptd())[5] = v; }

#ifdef ORACLE_WITH_ENGINE
/* Interface for fcompare.cpp (see fcompare.h). */
void oracle_logf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logf_("%s", buf);
}
const char *oracle_outdir() { return g_outdir; }
uint32_t oracle_crt_state() { return crt_state(); }
void oracle_crt_set_state(uint32_t v) { crt_set_state(v); }
#define FCOMPARE_HOOK(where, arg) fcompare_hook(where, arg)
#define FCOMPARE_FINISH() fcompare_finish()
#else
#define FCOMPARE_HOOK(where, arg) ((void)0)
#define FCOMPARE_FINISH() ((void)0)
#endif

/* ---- code patching ---- */
static void patch_bytes(uint32_t site, const uint8_t *bytes, size_t n) {
    DWORD old;
    uint8_t *p = (uint8_t *)site;
    VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
}

static void write_rel(uint32_t site, uint8_t opcode, void *target, size_t total) {
    uint8_t b[8] = {opcode};
    int32_t rel = (int32_t)((uint32_t)target - (site + 5));
    memcpy(b + 1, &rel, 4);
    for (size_t i = 5; i < total; i++) b[i] = 0x90;
    patch_bytes(site, b, total);
}

static void write_call(uint32_t site, void *target) {
    if (*(uint8_t *)site != 0xE8) {
        logf_("patch error: no call at %08X\n", site);
        ExitProcess(3);
    }
    write_rel(site, 0xE8, target, 5);
}

/* ---- UI randomness isolation ----
 * The original shares the game LCG between gameplay and UI effects (palette cycling etc.) whose
 * call counts depend on wall-clock timers. UI call sites are redirected to a private LCG so the
 * gameplay random stream is a pure function of the seeds. */
#include "ui_rng_sites.h"
static uint32_t g_ui_rng = 0x12345678;
static int __cdecl ui_game_random(int lo, int hi) {
    if (lo > hi) {
        int t = lo;
        lo = hi;
        hi = t;
    }
    g_ui_rng = g_ui_rng * 1664525u + 1013904223u;
    return lo + (int)(((uint32_t)(hi - lo) * (g_ui_rng & 0xffff)) >> 16);
}

/* ---- observer preferences ----
 * Disable everything that would block an unattended run or depend on wall-clock time. */
static void apply_observer_prefs() {
    const uint32_t clear = 0x1 | 0x2 | 0x20 | 0x80 | 0x10000000; /* pause end turn, autosave, tutorial, map anim, SP movies */
    const uint32_t set = 0x40000 | 0x8;                            /* interludes disabled, fast battle resolution */
    GamePreferences = (GamePreferences & ~clear) | set;
    AlphaIniPrefsFlags = (AlphaIniPrefsFlags & ~clear) | set;
    GameMorePreferences &= ~(0x10u | 0x4000u | 0x8000u | 0x20000u); /* pause after battles, voice, confirm odds */
}

/* ---- logic clock ----
 * Engine sites that use timeGetTime() as a randomness source read a deterministic counter
 * (+7919 per read), identical to engine_logic_time() in the reconstruction. */
static const uint32_t kLogicClockSites[] = {
    0x4EE804, /* base_terraform */
    0x4F55F9, /* drone_riot */
    0x522779, /* alien_fauna */
    0x5A0040, /* probe */
    0x5A0607, /* probe */
    0x5A950A, /* save_daemon */
    0x5C5A7B, /* world_climate */
    0x5C7145, /* world_borehole */
};
#ifdef ORACLE_WITH_ENGINE
static uint32_t logic_clock() { return engine_logic_time(); }
static uint32_t logic_clock_state() { return engine_logic_clock_state(); }
static void logic_clock_reset() { engine_set_logic_clock_state(0); }
#else
static uint32_t g_logic_clock = 0;
static uint32_t logic_clock() { return g_logic_clock += 7919; }
static uint32_t logic_clock_state() { return g_logic_clock; }
static void logic_clock_reset() { g_logic_clock = 0; }
#endif

/* timeGetTime() import hook. Calls are classified by return address: the logic sites get the
 * deterministic logic clock; other reads inside engine functions only pace animations and delays
 * and get a clock that jumps 1 s per read (so busy-wait loops end immediately); UI code gets the
 * real time. */
#include "fast_clock_sites.h"
typedef DWORD(WINAPI *f_timeGetTime)(void);
static f_timeGetTime g_real_timeGetTime;
static uint32_t g_fast_clock = 0;
static DWORD WINAPI hook_timeGetTime() {
    uint32_t ret = (uint32_t)(uintptr_t)__builtin_return_address(0);
    for (uint32_t site : kLogicClockSites)
        if (ret == site + 6) return logic_clock();
    for (auto &r : fast_clock_funcs)
        if (ret >= r[0] && ret < r[1]) return g_fast_clock += 1000;
    return g_real_timeGetTime();
}
#define IAT_timeGetTime 0x669368

/* ---- reconstructed function patches (hybrid.dll) ---- */
#ifdef ORACLE_WITH_ENGINE
struct PatchState {
    const HybridPatch *p;
    uint8_t saved[5];
    bool active;
};
static std::vector<PatchState> g_patches;

static void patches_init() {
    for (const HybridPatch *p = kHybridPatches; p->func; p++) {
        PatchState s = {p, {}, false};
        memcpy(s.saved, (void *)p->addr, 5);
        g_patches.push_back(s);
    }
}

static void patch_set(PatchState &s, bool on) {
    if (s.active == on) return;
    if (on)
        write_rel(s.p->addr, 0xE9, s.p->func, 5);
    else
        patch_bytes(s.p->addr, s.saved, 5);
    s.active = on;
}

/* patch all | none | +name | -name ... */
static void cmd_patch(char *args) {
    for (char *tok = strtok(args, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (!strcmp(tok, "all") || !strcmp(tok, "none")) {
            for (auto &s : g_patches) patch_set(s, tok[0] == 'a');
            continue;
        }
        bool on = tok[0] != '-';
        const char *name = (tok[0] == '+' || tok[0] == '-') ? tok + 1 : tok;
        bool found = false;
        for (auto &s : g_patches)
            if (!strcmp(s.p->name, name)) {
                patch_set(s, on);
                found = true;
            }
        if (!found) logf_("patch: unknown function %s\n", name);
    }
    int n = 0;
    for (auto &s : g_patches) n += s.active;
    logf_("patch: %d of %d reconstructed functions active\n", n, (int)g_patches.size());
}

int oracle_patch_set(const char *name, int on) {
    for (auto &s : g_patches)
        if (!strcmp(s.p->name, name)) {
            int prev = s.active;
            patch_set(s, on != 0);
            return prev;
        }
    return -1;
}
#endif

static void trace_orders(const char *what, int arg);
static void poison_stack();
static volatile LONG g_progress = 0; /* steps of the game loop, for the hang watchdog */

/* ---- snapshots ---- */
/* Binary state dump: records {u32 addr, u32 size, bytes} for each region of state_regions.h. */
static void write_state(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    auto put = [&](uint32_t addr, const void *data, uint32_t size) {
        fwrite(&addr, 4, 1, f);
        fwrite(&size, 4, 1, f);
        if (size) fwrite(data, 1, size, f);
    };
#define REGION(name, addr, size) put(addr, (const void *)(uintptr_t)(addr), size);
#define REGION_VAR(name, addr, count_addr, elem)                                              \
    {                                                                                         \
        uint32_t n = (uint32_t)G(int32_t, count_addr) * (elem);                               \
        const void *src = (addr) == 0x10000000 ? (const void *)G(uintptr_t, 0x94A30C)         \
                          : (addr) == 0x20000000 ? (const void *)G(uintptr_t, 0x94A310)       \
                                                 : (const void *)(uintptr_t)(addr);           \
        put(addr, src, src ? n : 0);                                                          \
    }
#define MASK(addr, size)
#include "state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
    fclose(f);
}

static int snapshot(const char *name) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s.state", g_outdir, name);
    write_state(path);
    uint32_t crt = crt_state(), game = GameRandomState;
    snprintf(path, sizeof(path), "%s\\%s.rng", g_outdir, name);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "crt %u\ngame %u\nclock %u\nturn %d\n", crt, game, logic_clock_state(), CurrentTurn);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%s\\%s.sav", g_outdir, name);
    int r = o_save_daemon(path);
    crt_set_state(crt);
    GameRandomState = game;
    logf_("snapshot %s turn=%d crt=%u game=%u save=%d\n", name, CurrentTurn, crt, game, r);
    return r;
}

/* ---- popups ----
 * Announcements are shown to every player, including the observer, and wait for a click.
 * BasePop_exec_3 (0x602600, thiscall, 2 stack args) has a built-in no-display path when its first
 * argument is nonzero: it prepares the popup and returns 0 without a modal window. Unattended
 * runs force that path. The original entry is reached through a trampoline. */
static uint8_t *g_pop_trampoline;
typedef int(__thiscall *f_pop)(void *, int, int);
static int __thiscall hook_basepop_exec(void *self, int no_display, int arg2) {
    (void)no_display;
    return ((f_pop)g_pop_trampoline)(self, 1, arg2);
}
static void install_popup_hook() {
    const uint32_t entry = 0x602600, len = 8; /* mov eax,fs:[0]; push -1 */
    g_pop_trampoline = (uint8_t *)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    memcpy(g_pop_trampoline, (void *)entry, len);
    g_pop_trampoline[len] = 0xE9;
    int32_t rel = (int32_t)((entry + len) - ((uint32_t)(uintptr_t)g_pop_trampoline + len + 5));
    memcpy(g_pop_trampoline + len + 1, &rel, 4);
    write_rel(entry, 0xE9, (void *)hook_basepop_exec, 5);
}

/* ---- Planetary Council session ----
 * council (0x428350) opens the council window (CouncilWin, vtable 0x66AAE8) through
 * FUN_00427230, which computes every non-human faction's vote (council_get_vote) and then enters
 * the window's modal loop (Win_set_modal via vtable+0xE4, call at 0x4282F2). The loop has no
 * timeout in single-player: the session is decided only when the viewer votes (YEA/NAY/ABSTAIN
 * buttons, or VOTE + the GOVVOTE popup for elections; all end in FUN_00424720(viewer, vote),
 * which recomputes the AI votes and tallies them, FUN_00424410), and the window closes on Enter
 * (FUN_00425BA0: for a non-human viewer it releases the modal state and calls council_action when
 * the proposal passed). An unattended observer would wait forever. The oracle replaces the modal
 * loop by exactly these two steps: the viewer casts the vote the game already holds for its
 * faction (for faction 0 the tally ignores it: only factions 1-7 are counted; for an AI viewer it
 * is that faction's own AI vote), then presses Enter. See harness/README.md. */
typedef int(__thiscall *f_cw_vote)(void *, int, int);
typedef int(__thiscall *f_cw_key)(void *, int, int);
typedef int(__thiscall *f_cw_modal)(void *, int, int, int);
static f_cw_vote const o_councilwin_vote = (f_cw_vote)0x424720;
static f_cw_key const o_councilwin_key = (f_cw_key)0x425BA0;
#define MultiplayerActive G(int32_t, 0x93F660)
static int __thiscall hook_council_modal(void *self, int a, int b, int c) {
    int32_t *w = (int32_t *)self;
    int viewer = CurrentPlayerFaction;
    if (MultiplayerActive || (FactionStatus[0] & (1 << viewer)))
        return ((f_cw_modal)(*(uint32_t **)self)[0xE4 / 4])(self, a, b, c);
    int vote = w[0x295 + viewer];
    logf_("council turn=%d proposal=%d candidate=%d viewer=%d vote=%d votes=%d,%d,%d,%d,%d,%d,%d\n", CurrentTurn,
          w[0x287], w[0x288], viewer, vote, w[0x296], w[0x297], w[0x298], w[0x299], w[0x29A], w[0x29B], w[0x29C]);
    if (w[0x289] == 0) o_councilwin_vote(self, viewer, vote);
    int result = w[0x289], proposal = w[0x287], winner = w[0x288];
    o_councilwin_key(self, 0, 0x0D);
    logf_("council result=%d proposal=%d winner=%d governor=%d\n", result, proposal, winner, G(int32_t, 0x9A6614));
    return 0;
}
#ifdef ORACLE_WITH_ENGINE
/* The same for the reconstructed FUN_00427230 (CouncilWin_session), whose modal loop call is
 * ui_councilwin_modal (hybrid: this hook). */
static int council_modal_engine(unsigned char *win) {
    return hook_council_modal(win, 0, 0x5398E0, 0x7AE820);
}
#endif
static void install_council_hook() {
    const uint32_t site = 0x4282F2; /* FUN_00427230: call [eax+0xE4] (Win_set_modal) */
    const uint8_t expect[6] = {0xFF, 0x90, 0xE4, 0x00, 0x00, 0x00};
    if (memcmp((void *)site, expect, 6)) {
        logf_("patch error: no council modal call at %08X\n", site);
        ExitProcess(3);
    }
    write_rel(site, 0xE8, (void *)hook_council_modal, 6);
}

/* ---- end of game ----
 * When a game ends (time limit, victory), end_of_game (0x524870) shows the end screens after the
 * game-state part (victory type, monuments): show_credits, report_score(1), quayle (rating),
 * hall_of_fame(1) (reads/writes hallfame.jhh) and show_replay (saves "Alpha Centauri Autosave 1",
 * replays the map history, reloads that save), then asks GAMEOVERMAN; control_game then reloads
 * and returns to the main menu. These are UI screens with modal loops (report_score even crashes
 * with viewer 0: ReportIf_on_status indexes its per-faction buttons with faction 0). The oracle
 * skips the first four and, at show_replay, writes the final snapshot `game_end` and exits. */
static void __cdecl end_screen_skip() {
    logf_("end of game: end screen skipped (turn=%d)\n", CurrentTurn);
}
static void slotlog_flush();
static void __cdecl hook_show_replay() {
    snapshot("game_end");
    FCOMPARE_FINISH();
    slotlog_flush();
    logf_("game over: turn=%d GameState=%08X victory=%d, exiting\n", CurrentTurn, GameState, G(int32_t, 0x9B206C));
    if (g_log) fclose(g_log);
    ExitProcess(0);
}
static void install_end_of_game_hooks() {
    write_rel(0x428DF0, 0xE9, (void *)end_screen_skip, 5);  /* show_credits */
    write_rel(0x4ACB80, 0xE9, (void *)end_screen_skip, 5);  /* report_score */
    write_rel(0x496670, 0xE9, (void *)end_screen_skip, 5);  /* quayle */
    write_rel(0x44AE50, 0xE9, (void *)end_screen_skip, 5);  /* hall_of_fame */
    write_rel(0x5ADBF0, 0xE9, (void *)hook_show_replay, 5); /* show_replay */
}

/* ---- turn loop hooks ---- */
/* trace 1: log RNG state at each faction step; trace 2: also dump state (STEP_TTT_F_x.state) */
static void trace_point(const char *what, int f) {
    if (!g_trace || g_trace >= 4) return;
    logf_("%s %d turn=%d crt=%u game=%u\n", what, f, CurrentTurn, crt_state(), GameRandomState);
    if (g_trace >= 2) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\step_%03d_%d_%s.state", g_outdir, CurrentTurn, f, what);
        write_state(path);
        snprintf(path, sizeof(path), "%s\\step_%03d_%d_%s.rng", g_outdir, CurrentTurn, f, what);
        if (FILE *r = fopen(path, "w")) {
            fprintf(r, "crt %u\ngame %u\nclock %u\nturn %d\n", crt_state(), GameRandomState, logic_clock_state(), CurrentTurn);
            fclose(r);
        }
    }
}

static int __cdecl hook_faction_upkeep(int f) {
    g_progress++;
    FCOMPARE_HOOK("faction", f);
    trace_orders("faction_upkeep", f);
    trace_point("a_upkeep", f);
    poison_stack();
    return o_faction_upkeep(f);
}

/* Stack poisoning: fill the stack area the next callee will use with a fixed byte, so that any
 * uninitialised local read by the original code sees the same value in every run. */
static int g_poison = 0x00;
static uint32_t g_poison_rand = 0; /* poison rand <seed>: pseudo-random fill, new bytes every call */
__attribute__((noinline)) static void poison_stack() {
    volatile uint8_t buf[0x8000];
    if (g_poison_rand) {
        for (size_t i = 0; i < sizeof(buf); i++) {
            g_poison_rand = g_poison_rand * 1103515245u + 12345u;
            buf[i] = (uint8_t)(g_poison_rand >> 16);
        }
    } else {
        for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)g_poison;
    }
    __asm__ volatile("" ::"r"(buf) : "memory");
}

/* trace 4: log per-step changes of vehicle orders and a hash of the state regions */
static uint8_t g_prev_orders[2048];
static uint32_t state_hash() {
    uint32_t h = 2166136261u;
    auto mix = [&](const void *p, uint32_t n) {
        const uint8_t *b = (const uint8_t *)p;
        for (uint32_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    };
#define REGION(name, addr, size) mix((const void *)(uintptr_t)(addr), size);
#define REGION_VAR(name, addr, count_addr, elem)                                                \
    {                                                                                           \
        uint32_t n = (uint32_t)G(int32_t, count_addr) * (elem);                                 \
        const void *src = (addr) == 0x10000000 ? (const void *)G(uintptr_t, 0x94A30C)           \
                          : (addr) == 0x20000000 ? (const void *)G(uintptr_t, 0x94A310)         \
                                                 : (const void *)(uintptr_t)(addr);             \
        if (src) mix(src, n);                                                                   \
    }
#define MASK(addr, size)
#include "state_regions.h"
#undef REGION
#undef REGION_VAR
#undef MASK
    return h;
}
static void trace_orders(const char *what, int arg) {
    if (g_trace < 4) return;
    int n = VehCount;
    for (int i = 0; i < n && i < 2048; i++) {
        uint8_t o = *(uint8_t *)(0x952839 + 0x34 * i);
        if (o != g_prev_orders[i]) logf_("  veh %d order %d->%d\n", i, g_prev_orders[i], o);
        g_prev_orders[i] = o;
    }
    logf_("T%d %s %d hash=%08x crt=%u game=%u\n", CurrentTurn, what, arg, state_hash(), crt_state(), GameRandomState);
}

/* watch <turn> <addr>: hardware write watchpoint armed at the start of that turn; every write
 * logs the instruction address and the EBP call chain. */
static int g_watch_turn = -1;
static uint32_t g_watch_addr = 0;
static DWORD g_main_tid;
static DWORD WINAPI watch_thread(LPVOID) {
    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, g_main_tid);
    SuspendThread(th);
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(th, &ctx);
    ctx.Dr0 = g_watch_addr;
    ctx.Dr7 = 1 | (1u << 16); /* L0, RW0=01 (write), LEN0=00 (1 byte) */
    SetThreadContext(th, &ctx);
    ResumeThread(th);
    CloseHandle(th);
    return 0;
}
static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    logf_("WATCH write [%08X]=%02X eip=%08X turn=%d stack:", g_watch_addr, *(uint8_t *)g_watch_addr, (uint32_t)c->Eip,
          CurrentTurn);
    uint32_t *bp = (uint32_t *)c->Ebp;
    for (int i = 0; i < 12 && bp && (uint32_t)bp > 0x10000 && !IsBadReadPtr(bp, 8); i++) {
        logf_(" %08X", bp[1]);
        bp = (uint32_t *)bp[0];
    }
    logf_("\n");
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void watch_arm() {
    g_main_tid = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, watch_handler);
    HANDLE t = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
    logf_("watch armed on %08X\n", g_watch_addr);
}

/* logcalls: log every call (args, caller, turn) of selected cdecl functions. Each hooked function
 * gets a runtime thunk: push idx; call log_call; add esp,4; jmp trampoline(prologue copy). */
struct CallHook {
    uint32_t addr, prologue;
    const char *name;
};
static const CallHook kCallHooks[] = {
    {0x5B6260, 6, "spot_all"},  {0x5B6060, 6, "wake_stack"}, {0x5B5EA0, 5, "want_to_wake"},
    {0x5C1D70, 5, "veh_wake"},  {0x5B5AD0, 7, "spot_loc"},   {0x55A150, 9, "veh_scoot"},
    {0x5B58E0, 7, "spot_stack"},
};
static int g_logcalls_from_turn = -1;
static void __cdecl log_call(int idx, uint32_t ret, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    if (CurrentTurn >= g_logcalls_from_turn)
        logf_("CALL T%d %s(%d,%d,%d,%d) from %08X\n", CurrentTurn, kCallHooks[idx].name, a0, a1, a2, a3, ret);
}
static void install_call_hooks() {
    uint8_t *mem = (uint8_t *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    for (size_t i = 0; i < sizeof(kCallHooks) / sizeof(kCallHooks[0]); i++) {
        const CallHook &h = kCallHooks[i];
        uint8_t *tramp = mem + i * 64, *thunk = tramp + 24;
        memcpy(tramp, (void *)h.addr, h.prologue);
        tramp[h.prologue] = 0xE9;
        int32_t rel = (int32_t)((h.addr + h.prologue) - ((uint32_t)(uintptr_t)tramp + h.prologue + 5));
        memcpy(tramp + h.prologue + 1, &rel, 4);
        uint8_t *t = thunk;
        *t++ = 0x68; uint32_t idx = (uint32_t)i; memcpy(t, &idx, 4); t += 4;
        *t++ = 0xE8; rel = (int32_t)((uint32_t)(uintptr_t)log_call - ((uint32_t)(uintptr_t)t + 4)); memcpy(t, &rel, 4); t += 4;
        *t++ = 0x83; *t++ = 0xC4; *t++ = 0x04;
        *t++ = 0xE9; rel = (int32_t)((uint32_t)(uintptr_t)tramp - ((uint32_t)(uintptr_t)t + 4)); memcpy(t, &rel, 4); t += 4;
        write_rel(h.addr, 0xE9, thunk, 5);
    }
}

/* steptrace <turn> <veh|-1> [max]: single-step selected enemy_veh calls on that turn (all calls
 * of that vehicle, or every call with -1; at most max calls, default 1) and write the register
 * state before every executed terranx.exe instruction to step_TTT_cNNNNN_vehV.trc (records of
 * {eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi}). Calls into other modules (Windows, the
 * oracle DLL) are not traced: a hardware execute breakpoint on the return address resumes tracing;
 * such calls are logged as a record with eflags = 0xFFFFFFFF and eip = the external target.
 * Analyse with harness/scripts/uninit_reads.py. The exception frames built on the stack perturb
 * stack contents below ESP, so a traced run can diverge from the untraced one afterwards. */
ORIG(f_int, o_enemy_veh_st, 0x579240);
static int g_st_turn = -1, g_st_veh = -1, g_st_max = 1, g_st_done = 0;
static FILE *g_st_file;
static volatile int g_st_active;
static uint32_t g_st_entry_esp, g_st_ret_addr, g_st_ret_esp, g_st_steps, g_st_ext;
static PVOID g_st_handler;
static const uint32_t kImageCodeLo = 0x401000, kImageCodeHi = 0x669000;
static LONG CALLBACK steptrace_handler(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *c = e->ContextRecord;
    if (!g_st_active) {
        if (!(c->Dr6 & 1) || g_watch_addr) return EXCEPTION_CONTINUE_SEARCH;
        c->Dr7 &= ~3u; /* stale return breakpoint */
        c->Dr6 = 0;
        c->EFlags |= 0x10000;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    uint32_t eip = (uint32_t)c->Eip;
    if (g_st_ret_addr) {
        /* inside an external call: only the return breakpoint (at the original stack depth) resumes */
        if (eip != g_st_ret_addr || (uint32_t)c->Esp < g_st_ret_esp) { /* stdcall callees pop args */
            c->EFlags |= 0x10000; /* RF */
            c->Dr6 = 0;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        g_st_ret_addr = 0;
        c->Dr0 = 0;
        c->Dr7 &= ~3u;
        c->EFlags |= 0x10000;
    }
    if (!g_st_entry_esp) g_st_entry_esp = (uint32_t)c->Esp;
    c->Dr6 = 0;
    if (eip >= kImageCodeLo && eip < kImageCodeHi) {
        uint32_t rec[10] = {eip, (uint32_t)c->EFlags, (uint32_t)c->Eax, (uint32_t)c->Ecx, (uint32_t)c->Edx,
                            (uint32_t)c->Ebx, (uint32_t)c->Esp, (uint32_t)c->Ebp, (uint32_t)c->Esi, (uint32_t)c->Edi};
        fwrite(rec, 4, 10, g_st_file);
        g_st_steps++;
        c->EFlags |= 0x100;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if ((uint32_t)c->Esp > g_st_entry_esp) { /* returned from enemy_veh */
        g_st_active = 0;
        c->EFlags &= ~0x100u;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    /* external call: [esp] is the return address into terranx.exe */
    uint32_t rec[10] = {eip, 0xFFFFFFFFu, (uint32_t)c->Eax, (uint32_t)c->Ecx, (uint32_t)c->Edx, (uint32_t)c->Ebx,
                        (uint32_t)c->Esp, (uint32_t)c->Ebp, (uint32_t)c->Esi, (uint32_t)c->Edi};
    fwrite(rec, 4, 10, g_st_file);
    g_st_ext++;
    g_st_ret_addr = *(uint32_t *)c->Esp;
    g_st_ret_esp = (uint32_t)c->Esp + 4;
    c->Dr0 = g_st_ret_addr;
    c->Dr7 = (c->Dr7 & ~0x000F0003u) | 1u; /* L0, execute, 1 byte */
    c->EFlags &= ~0x100u;
    return EXCEPTION_CONTINUE_EXECUTION;
}
__attribute__((noinline)) static int steptrace_call(int veh) {
    int r;
    __asm__ volatile("push %1\n pushf\n orl $0x100, (%%esp)\n popf\n call *%2\n add $4, %%esp\n"
                     : "=a"(r)
                     : "r"(veh), "r"((uint32_t)0x579240)
                     : "ecx", "edx", "memory", "cc");
    return r;
}
static bool steptrace_want(int veh) {
    return CurrentTurn == g_st_turn && g_st_done < g_st_max && (g_st_veh < 0 || g_st_veh == veh);
}
static int steptrace_enemy_veh(int veh, int seq) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\step_%03d_c%05d_veh%d.trc", g_outdir, CurrentTurn, seq, veh);
    g_st_file = fopen(path, "wb");
    if (!g_st_file) return o_enemy_veh_st(veh);
    static char buf[1 << 20];
    setvbuf(g_st_file, buf, _IOFBF, sizeof(buf));
    if (!g_st_handler) g_st_handler = AddVectoredExceptionHandler(1, steptrace_handler);
    g_st_entry_esp = g_st_ret_addr = g_st_steps = g_st_ext = 0;
    g_st_active = 1;
    poison_stack();
    int r = steptrace_call(veh);
    g_st_active = 0;
    fclose(g_st_file);
    g_st_file = NULL;
    g_st_done++;
    logf_("steptrace turn=%d call=%d veh=%d steps=%u external=%u result=%d\n", CurrentTurn, seq, veh, g_st_steps,
          g_st_ext, r);
    return r;
}

/* slotlog <func> <prologue_len> <off>...: at every entry of the original function <func> (hex),
 * log the return address, the first 8 stack arguments, the dwords at entry ESP + <off> (hex,
 * may be negative: -30 is the callee's [ebp-0x2c]) to slotlog.txt, and EBX, ESI, EDI, EBP at entry. The entry thunk runs the
 * logger on a private stack, so the game's stack (including dead slots below ESP) is untouched.
 * <prologue_len> must cover whole instructions without relative jumps (Path_find: 6). */
static struct {
    uint32_t func, off[8];
    int noff;
} g_sl;
static uint32_t g_sl_saved_esp;
static const uint32_t *g_sl_regs;
static uint8_t g_sl_stack[65536] __attribute__((aligned(16)));
struct SlotRec {
    int32_t turn;
    uint32_t ret, args[8], vals[8], regs[4]; /* regs: EBX, ESI, EDI, EBP at entry */
};
static std::vector<SlotRec> g_sl_recs;
static void __cdecl slotlog_cb(uint32_t esp) {
    SlotRec r;
    r.turn = CurrentTurn;
    const uint32_t *sp = (const uint32_t *)esp;
    r.ret = sp[0];
    for (int i = 0; i < 8; i++) r.args[i] = sp[1 + i];
    for (int i = 0; i < g_sl.noff; i++) r.vals[i] = *(const uint32_t *)(esp + g_sl.off[i]);
    /* The thunk's pushfd/pushad frame: EFLAGS, EDI, ESI, EBP, ESP, EBX, ... (the registers at
     * entry; EBP is the caller's frame pointer). */
    const uint32_t *pa = g_sl_regs;
    r.regs[0] = pa[5];
    r.regs[1] = pa[2];
    r.regs[2] = pa[1];
    r.regs[3] = pa[3];
    if (g_sl_recs.capacity() == g_sl_recs.size()) return; /* never reallocate here */
    g_sl_recs.push_back(r);
}
static void slotlog_flush() {
    if (g_sl_recs.empty()) return;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\slotlog.txt", g_outdir);
    if (FILE *f = fopen(path, "a")) {
        for (auto &r : g_sl_recs) {
            fprintf(f, "T%d ret=%08X args=", r.turn, r.ret);
            for (int i = 0; i < 8; i++) fprintf(f, "%s%X", i ? "," : "", r.args[i]);
            fprintf(f, " vals=");
            for (int i = 0; i < g_sl.noff; i++) fprintf(f, "%s%08X", i ? "," : "", r.vals[i]);
            fprintf(f, " ebx=%08X esi=%08X edi=%08X ebp=%08X\n", r.regs[0], r.regs[1], r.regs[2], r.regs[3]);
        }
        fclose(f);
    }
    g_sl_recs.clear();
}
static void install_slotlog(int prologue) {
    g_sl_recs.reserve(1 << 20);
    uint8_t *mem = (uint8_t *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    uint8_t *t = mem;
    auto u32 = [&](uint32_t v) { memcpy(t, &v, 4); t += 4; };
    uint32_t top = (uint32_t)(uintptr_t)(g_sl_stack + sizeof(g_sl_stack) - 16);
    *t++ = 0x89; *t++ = 0x25; u32((uint32_t)(uintptr_t)&g_sl_saved_esp); /* mov [saved], esp */
    *t++ = 0xBC; u32(top);                                                /* mov esp, top */
    *t++ = 0x60; *t++ = 0x9C;                                             /* pushad; pushfd */
    *t++ = 0x89; *t++ = 0x25; u32((uint32_t)(uintptr_t)&g_sl_regs);      /* mov [g_sl_regs], esp */
    *t++ = 0xFF; *t++ = 0x35; u32((uint32_t)(uintptr_t)&g_sl_saved_esp);  /* push [saved] */
    *t++ = 0xE8; u32((uint32_t)(uintptr_t)slotlog_cb - ((uint32_t)(uintptr_t)t + 4));
    *t++ = 0x83; *t++ = 0xC4; *t++ = 0x04;                                /* add esp, 4 */
    *t++ = 0x9D; *t++ = 0x61;                                             /* popfd; popad */
    *t++ = 0x8B; *t++ = 0x25; u32((uint32_t)(uintptr_t)&g_sl_saved_esp);  /* mov esp, [saved] */
    memcpy(t, (void *)g_sl.func, prologue); t += prologue;
    *t++ = 0xE9; u32((g_sl.func + prologue) - ((uint32_t)(uintptr_t)t + 4));
    write_rel(g_sl.func, 0xE9, mem, prologue);
}

/* poison pump: also poison the stack after the message pump at the start of enemy_veh
 * (do_all_non_input, 0x57928D). A window message dispatched there (timing-dependent) leaves its
 * frames in the area enemy_move's frame occupies next; enemy_move reads its local [ebp-0x48]
 * uninitialised (0x574DD7), so without this the oracle itself is not reproducible. Off by default. */
ORIG(f_none, o_do_all_non_input, 0x5FCB20);
static int __cdecl hook_enemy_veh_pump() {
    int r = o_do_all_non_input();
    poison_stack();
    return r;
}

#ifdef ORACLE_WITH_ENGINE
static void hook_enemy_veh_pump_void() { hook_enemy_veh_pump(); }
#endif

/* wants_to_attack slot poisoning: wants_to_attack (0x55BC80) takes per-faction maxima starting
 * from the never-initialised dword array [ebp-0x134..-0x118] (known uninitialised read #4,
 * docs/RECONSTRUCTION.md). Before every call that starts an evaluation (a single call, or the
 * first (a, b) call of an (a, b) / (b, a) pair) the oracle fills exactly these 8 dwords
 * ([entry ESP - 0x138, entry ESP - 0x118)) with the poison value and then enters the function at
 * the original stack depth. The second call of a pair (same ESP, no call in between) still sees
 * the first call's reduced slots, as in the original. Without this the slots hold whatever
 * earlier frames left at that depth, including UI/Win32 frames (move animation, popups, Wine
 * internals) that a headless build cannot reproduce. Only these dwords are written, so no other
 * dead stack changes. */
static const uint32_t kWantsToAttackFirstSites[] = {
    0x52B0E1, 0x52B21A,           /* wants_prop (pairs) */
    0x53BED6,                     /* diplomacy_check */
    0x54648E, 0x54685A,           /* propose_attack (pairs) */
    0x549DF1,                     /* threaten (pair) */
    0x54C352,                     /* call_off_vendetta */
    0x552566, 0x5527E8, 0x553325, /* communicate (two singles, one pair) */
    0x55EB3F,                     /* encounter (pair) */
    0x562D0A,                     /* enemy_strategy (pair) */
    0x5BCC70,                     /* tech_val (pair) */
};
extern "C" uint32_t oracle_wta_fill[8];
uint32_t oracle_wta_fill[8];
extern "C" void __cdecl oracle_wta_prepare() {
    uint8_t *b = (uint8_t *)oracle_wta_fill;
    for (size_t i = 0; i < sizeof(oracle_wta_fill); i++) {
        if (g_poison_rand) {
            g_poison_rand = g_poison_rand * 1103515245u + 12345u;
            b[i] = (uint8_t)(g_poison_rand >> 16);
        } else {
            b[i] = (uint8_t)g_poison;
        }
    }
}
/* Called instead of wants_to_attack at the sites above; ESP at entry equals the original entry ESP. */
extern "C" __attribute__((naked, used)) void oracle_wta_poison_thunk() {
    __asm__("call _oracle_wta_prepare\n"
            "xor %ecx, %ecx\n"
            "1:\n mov _oracle_wta_fill(,%ecx,4), %eax\n mov %eax, -0x138(%esp,%ecx,4)\n"
            "inc %ecx\n cmp $8, %ecx\n jb 1b\n"
            "mov $0x55BC80, %eax\n jmp *%eax\n");
}

/* trace 3 <turn>: dump state before every enemy_veh call on that turn */
static int g_trace_turn = -1, g_trace_seq = 0;
ORIG(f_int, o_enemy_veh, 0x579240);
static int __cdecl hook_enemy_veh(int veh) {
    g_progress++;
    if (g_trace >= 3 && CurrentTurn == g_trace_turn) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\step_%03d_9_c%05d_veh%d.state", g_outdir, CurrentTurn, g_trace_seq, veh);
        write_state(path);
        snprintf(path, sizeof(path), "%s\\step_%03d_9_c%05d_veh%d.rng", g_outdir, CurrentTurn, g_trace_seq++, veh);
        if (FILE *r = fopen(path, "w")) {
            fprintf(r, "crt %u\ngame %u\nclock %u\nturn %d\n", crt_state(), GameRandomState, logic_clock_state(), CurrentTurn);
            fclose(r);
        }
    }
    trace_orders("enemy_veh", veh);
    static int st_seq = 0, st_seq_turn = -1;
    if (st_seq_turn != CurrentTurn) st_seq_turn = CurrentTurn, st_seq = 0;
    int seq = st_seq++;
    if (steptrace_want(veh)) {
        poison_stack();
        int r = steptrace_enemy_veh(veh, seq);
        trace_orders("enemy_veh_done", veh);
        return r;
    }
    poison_stack();
    int r = o_enemy_veh(veh);
    trace_orders("enemy_veh_done", veh);
    return r;
}

static int __cdecl hook_enemy_turn(int f) {
    FCOMPARE_HOOK("enemy", f);
    trace_orders("enemy_turn", f);
    trace_point("b_enemy", f);
    poison_stack();
    return o_enemy_turn(f);
}

static int __cdecl hook_human_turn() {
    logf_("Console_human_turn called (unexpected) cur=%d status=%02X%02X\n", CurrentPlayerFaction, FactionStatus[1],
          FactionStatus[0]);
    return o_Console_human_turn();
}

/* control_turn calls Console_human_turn after a round in which no human moved (observer pause:
 * "TURN COMPLETE"). For unattended runs the observer continues immediately. */
static int __cdecl hook_observer_pause() { return 0; }

static int __cdecl hook_turn_upkeep() {
    g_progress++;
    char name[64];
    apply_observer_prefs();
    snprintf(name, sizeof(name), "turn_%03d", CurrentTurn);
    snapshot(name);
    FCOMPARE_HOOK("turn", 0);
    if (g_turn_limit >= 0 && CurrentTurn >= g_turn_limit) {
        FCOMPARE_FINISH();
        slotlog_flush();
        logf_("turn limit reached, exiting\n");
        if (g_log) fclose(g_log);
        ExitProcess(0);
    }
    slotlog_flush();
    trace_orders("turn_upkeep", 0);
    if (CurrentTurn == g_watch_turn && g_watch_addr) watch_arm();
    poison_stack();
    int r = o_turn_upkeep();
    trace_orders("turn_upkeep_done", 0);
    return r;
}

/* ---- hang diagnosis ----
 * A watchdog thread logs where the main thread waits when the game makes no progress (no turn,
 * faction or vehicle step) for `hangdump <seconds>` (default 60): EIP, the modal window stack and
 * the return addresses into terranx.exe found on the stack (dwords preceded by a call
 * instruction). Logging only: the main thread is suspended for the dump and resumed. */
static int g_hang_secs = 60;
static DWORD g_main_thread_id;
static bool is_return_address(uint32_t v) {
    if (v < 0x401005 || v >= kImageCodeHi) return false;
    const uint8_t *p = (const uint8_t *)(uintptr_t)v;
    if (p[-5] == 0xE8) return true;                                   /* call rel32 */
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) return true;         /* call r32 / [r32] */
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;         /* call [r32+d8] */
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;         /* call [r32+d32] / [abs] */
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10) return true;         /* call [sib+d32] */
    return false;
}
static void hang_dump() {
    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, g_main_thread_id);
    if (!th) return;
    SuspendThread(th);
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(th, &ctx);
    logf_("HANG turn=%d eip=%08X esp=%08X modal_depth=%d modal_top=%08X vtbl=%08X stack:", CurrentTurn,
          (uint32_t)ctx.Eip, (uint32_t)ctx.Esp, G(int32_t, 0x9B7AE4),
          G(uint32_t, 0x9B7AE0), G(uint32_t, 0x9B7AE0) ? *(uint32_t *)(uintptr_t)G(uint32_t, 0x9B7AE0) : 0);
    uint32_t sp = (uint32_t)ctx.Esp;
    MEMORY_BASIC_INFORMATION mbi;
    uint32_t end = sp + 0x40000;
    if (VirtualQuery((void *)(uintptr_t)sp, &mbi, sizeof(mbi)))
        end = (uint32_t)(uintptr_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
    int n = 0;
    for (uint32_t a = sp & ~3u; a + 4 <= end && n < 48; a += 4) {
        uint32_t v = *(uint32_t *)(uintptr_t)a;
        if (is_return_address(v)) {
            logf_(" %08X", v);
            n++;
        }
    }
    logf_("\n");
    ResumeThread(th);
    CloseHandle(th);
}
static DWORD WINAPI hang_watchdog(LPVOID) {
    LONG last = g_progress;
    int idle = 0;
    for (;;) {
        Sleep(1000);
        LONG now = g_progress;
        if (now != last) {
            last = now;
            idle = 0;
            continue;
        }
        if (++idle % g_hang_secs == 0) hang_dump();
    }
    return 0;
}
static void hang_watchdog_start() {
    static bool started = false;
    if (started || g_hang_secs <= 0) return;
    started = true;
    g_main_thread_id = GetCurrentThreadId();
    CloseHandle(CreateThread(NULL, 0, hang_watchdog, NULL, 0, NULL));
}

/* ---- newgame ---- */
static const char *kDefaultFactions[7] = {"gaians", "hive", "univ", "morgan", "spartans", "believe", "peace"};
static const int kWorldSize[5][2] = {{24, 48}, {32, 64}, {40, 80}, {44, 90}, {64, 128}};

static int cmd_newgame(char *args) {
    int size = 2, ocean = 1, erosion = 1, orbit = 1, clouds = 1, natives = 1, diff = 3;
    int width = 0, height = 0;
    uint32_t rules = 0;
    int rules_set = 0;
    int random_leaders = 0; /* bit 0: random personalities (GameState 0x800000), bit 1: random agendas (0x1000000) */
    char factions[7][24];
    for (int i = 0; i < 7; i++) snprintf(factions[i], 24, "%s", kDefaultFactions[i]);
    for (char *tok = strtok(args, " \t"); tok; tok = strtok(NULL, " \t")) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = tok, *v = eq + 1;
        if (!strcmp(k, "size")) size = atoi(v);
        else if (!strcmp(k, "width")) width = atoi(v);
        else if (!strcmp(k, "height")) height = atoi(v);
        else if (!strcmp(k, "ocean")) ocean = atoi(v);
        else if (!strcmp(k, "erosion")) erosion = atoi(v);
        else if (!strcmp(k, "orbit")) orbit = atoi(v);
        else if (!strcmp(k, "clouds")) clouds = atoi(v);
        else if (!strcmp(k, "natives")) natives = atoi(v);
        else if (!strcmp(k, "diff")) diff = atoi(v);
        else if (!strcmp(k, "random")) random_leaders = atoi(v);
        else if (!strcmp(k, "rules")) {
            rules = strtoul(v, NULL, 0);
            rules_set = 1;
        } else if (!strcmp(k, "factions")) {
            std::string list(v);
            size_t pos = 0;
            for (int i = 0; i < 7 && pos <= list.size(); i++) {
                size_t comma = list.find(',', pos);
                snprintf(factions[i], 24, "%s", list.substr(pos, comma - pos).c_str());
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
        }
    }
    if (!width) {
        width = kWorldSize[size][0];
        height = kWorldSize[size][1];
    }
    /* size_of_planet */
    MapGenerate = 1;
    MapSizePlanet = size;
    MapAreaX = 2 * width;
    MapAreaY = height;
    o_map_init();
    o_Path_init(Paths);
    /* map_menu (random/custom planet) */
    MapOceanCoverage = ocean;
    MapLandCoverage = 2 - ocean;
    MapErosiveForces = erosion;
    MapPlanetaryOrbit = orbit;
    MapCloudCover = clouds;
    MapNativeLifeForms = natives;
    /* config_game */
    DiffLevel = diff;
    GameRules = (GameRules & 0xFFFF0000) | (rules_set ? rules : (uint32_t)o_default_rules());
    GameState &= ~0x1800000u;
    if (random_leaders & 1) GameState |= 0x800000u;
    if (random_leaders & 2) GameState |= 0x1000000u;
    for (int i = 1; i < 8; i++) {
        uint8_t *m = MFactions + i * MFACTION_SIZE;
        memset(m + 4, 0, 24);
        snprintf((char *)m + 4, 24, "%s", factions[i - 1]);
        o_read_faction(m, 0);
    }
    FactionStatus[0] = 0; /* no human players */
    CurrentPlayerFaction = 1;
    GameTimeControl = 0;
    VehCount = 0;
    logf_("newgame size=%d %dx%d ocean=%d erosion=%d orbit=%d clouds=%d natives=%d diff=%d rules=%08X crt=%u game=%u\n",
          size, MapAreaX, MapAreaY, ocean, erosion, orbit, clouds, natives, diff, GameRules, crt_state(),
          GameRandomState);
    o_world_build();
    o_setup_game(0);
    if (ExpansionEnabled) o_alien_start();
    FactionStatus[0] = 0;
    CurrentPlayerFaction = g_viewer; /* observer views as native life by default: no popups */
    apply_observer_prefs();
    logf_("newgame done mapseed=%d turn=%d crt=%u game=%u\n", MapRandomSeed, CurrentTurn, crt_state(),
          GameRandomState);
    return 0;
}

/* ---- script ---- */
static int __cdecl oracle_top_menu() {
    FILE *f = fopen("oracle.cmd", "r");
    if (!f) {
        logf_("no oracle.cmd\n");
        ExitProcess(2);
    }
    char line[16384];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *cmd = line + strspn(line, " \t");
        if (!*cmd || *cmd == '#') continue;
        char *args = cmd + strcspn(cmd, " \t");
        if (*args) *args++ = 0;
        args += strspn(args, " \t");
        logf_("> %s %s\n", cmd, args);
        if (!strcmp(cmd, "outdir")) {
            snprintf(g_outdir, sizeof(g_outdir), "%s", args);
            CreateDirectoryA(g_outdir, NULL);
            if (g_log) {
                fclose(g_log);
                g_log = NULL;
            }
        } else if (!strcmp(cmd, "seed")) {
            uint32_t a = 0, b = 0;
            sscanf(args, "%u %u", &a, &b);
            o_game_srand((int)a);
            o_game_reseed((int)b);
            logic_clock_reset();
        } else if (!strcmp(cmd, "newgame")) {
            cmd_newgame(args);
        } else if (!strcmp(cmd, "load")) {
            int r = o_load_daemon(args, 0);
            logf_("load %s -> %d\n", args, r);
        } else if (!strcmp(cmd, "save")) {
            uint32_t crt = crt_state(), game = GameRandomState;
            int r = o_save_daemon(args);
            crt_set_state(crt);
            GameRandomState = game;
            logf_("save %s -> %d\n", args, r);
        } else if (!strcmp(cmd, "dumprules")) {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s\\%s", g_outdir, args);
            FILE *df = fopen(path, "w");
            if (df) {
                rules_dump(df);
                fclose(df);
            }
            logf_("dumprules %s -> %d\n", args, df != NULL);
        } else if (!strcmp(cmd, "snapshot")) {
            snapshot(args);
        } else if (!strcmp(cmd, "turns")) {
            g_turn_limit = CurrentTurn + atoi(args);
            hang_watchdog_start();
            fclose(f);
            return 0; /* continue into desktop_init + control_turn */
        } else if (!strcmp(cmd, "viewer")) {
            g_viewer = atoi(args);
            CurrentPlayerFaction = g_viewer;
        } else if (!strcmp(cmd, "poison")) {
            if (!strncmp(args, "rand", 4))
                g_poison_rand = (uint32_t)strtoul(args + 4, NULL, 0) | 1;
            else
                g_poison = (int)strtol(args, NULL, 0);
        } else if (!strcmp(cmd, "logcalls")) {
            g_logcalls_from_turn = atoi(args);
            install_call_hooks();
        } else if (!strcmp(cmd, "watch")) {
            sscanf(args, "%d %x", &g_watch_turn, &g_watch_addr);
        } else if (!strcmp(cmd, "poisonpump")) {
            /* now always installed at startup; kept for old scripts */
        } else if (!strcmp(cmd, "slotlog")) {
            char *tok = strtok(args, " \t");
            g_sl.func = strtoul(tok, NULL, 16);
            int prologue = atoi(strtok(NULL, " \t"));
            while ((tok = strtok(NULL, " \t")) && g_sl.noff < 8) g_sl.off[g_sl.noff++] = (uint32_t)strtol(tok, NULL, 16);
            install_slotlog(prologue);
        } else if (!strcmp(cmd, "steptrace")) {
            sscanf(args, "%d %d %d", &g_st_turn, &g_st_veh, &g_st_max);
        } else if (!strcmp(cmd, "dumpmem")) {
            /* dumpmem <hexaddr> <hexsize> <file>: raw bytes of the original's memory, now */
            uint32_t addr = 0, size = 0;
            char name[MAX_PATH] = "";
            sscanf(args, "%x %x %259s", &addr, &size, name);
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s\\%s", g_outdir, name);
            FILE *df = fopen(path, "wb");
            if (df) {
                fwrite((const void *)(uintptr_t)addr, 1, size, df);
                fclose(df);
            }
            logf_("dumpmem %08X %X %s -> %d\n", addr, size, name, df != NULL);
        } else if (!strcmp(cmd, "hangdump")) {
            g_hang_secs = atoi(args);
        } else if (!strcmp(cmd, "trace")) {
            sscanf(args, "%d %d", &g_trace, &g_trace_turn);
#ifdef ORACLE_WITH_ENGINE
        } else if (!strcmp(cmd, "fcompare")) {
#ifdef ORACLE_WITH_ENGINE
            fcompare_cmd(args);
#else
            logf_("fcompare: needs hybrid.dll\n");
#endif
        } else if (!strcmp(cmd, "patch")) {
            cmd_patch(args);
#endif
        } else if (!strcmp(cmd, "quit")) {
            break;
        } else {
            logf_("unknown command %s\n", cmd);
        }
    }
    fclose(f);
    if (g_log) fclose(g_log);
    ExitProcess(0);
    return 1;
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *e) {
    logf_("CRASH code=%08lX addr=%p turn=%d stack:", e->ExceptionRecord->ExceptionCode,
          e->ExceptionRecord->ExceptionAddress, CurrentTurn);
    {
        uint32_t sp = (uint32_t)e->ContextRecord->Esp, end = sp + 0x40000;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void *)(uintptr_t)sp, &mbi, sizeof(mbi)))
            end = (uint32_t)(uintptr_t)mbi.BaseAddress + (uint32_t)mbi.RegionSize;
        int n = 0;
        for (uint32_t a = sp & ~3u; a + 4 <= end && n < 32; a += 4) {
            uint32_t v = *(uint32_t *)(uintptr_t)a;
            if (is_return_address(v)) logf_(" %08X", v), n++;
        }
        logf_("\n");
    }
    ExitProcess(4);
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        write_call(0x52AC4A, (void *)oracle_top_menu);  /* control_game -> top_menu */
        write_call(0x52768A, (void *)hook_turn_upkeep); /* control_turn -> turn_upkeep */
        write_call(0x528214, (void *)hook_faction_upkeep);
        write_call(0x528289, (void *)hook_enemy_turn);
        write_call(0x579703, (void *)hook_enemy_veh); /* enemy_turn -> enemy_veh */
        /* Poison again after enemy_veh's message pump (do_all_non_input), so the uninitialised
         * local read by enemy_move ([ebp-0x48], 0x574DD7) never sees UI leftovers. */
        write_call(0x57928D, (void *)hook_enemy_veh_pump);
        /* Poison wants_to_attack's uninitialised slot array before each evaluation. */
        for (uint32_t site : kWantsToAttackFirstSites) write_call(site, (void *)oracle_wta_poison_thunk);
        write_call(0x52826E, (void *)hook_human_turn);
        write_call(0x5282C9, (void *)hook_observer_pause);
        for (uint32_t site : ui_sites_game_random) write_call(site, (void *)ui_game_random);
        install_popup_hook();
        install_council_hook();
        install_end_of_game_hooks();
        /* No cosmetic UI timers (cursor blink, status redraw): their WM_TIMER callbacks run at
         * wall-clock-dependent points inside engine message pumps and leave different stack
         * contents behind, which the original's AI code can read uninitialised. */
        {
            const uint8_t ret = 0xC3;
            patch_bytes(0x50F3D0, &ret, 1); /* start_timers */
        }
        g_real_timeGetTime = *(f_timeGetTime *)IAT_timeGetTime;
        {
            void *hook = (void *)hook_timeGetTime;
            patch_bytes(IAT_timeGetTime, (const uint8_t *)&hook, 4);
        }
#ifdef ORACLE_WITH_ENGINE
        g_harness_turn_upkeep = hook_turn_upkeep;
        g_harness_faction_upkeep = hook_faction_upkeep;
        g_harness_enemy_turn = hook_enemy_turn;
        g_harness_human_turn = hook_human_turn;
        g_harness_observer_pause = hook_observer_pause;
        g_harness_top_menu = oracle_top_menu;
        g_harness_enemy_veh = hook_enemy_veh;
        g_harness_enemy_veh_pump = hook_enemy_veh_pump_void;
        g_harness_wants_to_attack_fresh = (int(__cdecl *)(int, int, int))(void *)oracle_wta_poison_thunk;
        g_harness_council_modal = council_modal_engine;
        engine_set_deterministic(true);
        patches_init();
        /* oracle.patch: patch spec applied before the game boots, so reconstructed boot code
         * (rules parsing, preferences) runs too. */
        if (FILE *pf = fopen("oracle.patch", "r")) {
            char spec[4096] = "";
            size_t n = fread(spec, 1, sizeof(spec) - 1, pf);
            spec[n] = 0;
            fclose(pf);
            for (char *c = spec; *c; c++)
                if (*c == '\r' || *c == '\n') *c = ' ';
            logf_("early patch: %s\n", spec);
            cmd_patch(spec);
        }
#endif
        SetUnhandledExceptionFilter(crash_filter);
    }
    return TRUE;
}
