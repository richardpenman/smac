/* fcompare: side-by-side comparison of original and reconstructed functions on the live game
 * state (hybrid.dll only). See harness/README.md ("Function comparison").
 *
 * At the selected hooks, every selected function is called with generated arguments twice: once
 * the original (its patch temporarily removed), once the reconstruction. Between and after the
 * calls the memory image (0x682000-0x9CA000), the map tiles and both random generators are
 * restored, so the game itself continues unaffected. Return values, out-parameters and (for
 * functions marked as mutating) the resulting memory are compared. */
#pragma once
#include <cstdint>

/* ---- provided by oracle.cpp ---- */
void oracle_logf(const char *fmt, ...);
const char *oracle_outdir();
/* Enables/disables the jump patch of a reconstructed function; returns the previous state, or
 * -1 if the name is not a reconstructed function. */
int oracle_patch_set(const char *name, int on);
uint32_t oracle_crt_state();
void oracle_crt_set_state(uint32_t v);

/* ---- provided by fcompare.cpp ---- */
/* fcompare <from> <to> [hooks=turn,faction,enemy] [limit=<pure>,<mutating>] <spec...> */
void fcompare_cmd(const char *args);
/* Called by the oracle hooks: where = "turn" (before turn_upkeep), "faction" (before
 * faction_upkeep), "enemy" (before enemy_turn); arg = faction id or 0. */
void fcompare_hook(const char *where, int arg);
/* Writes the final summary (called before the process exits). */
void fcompare_finish();

/* ---- argument table (fcompare_table.h) ---- */
enum FcArgKind : uint8_t {
    FA_NONE = 0,
    FA_FACTION,      /* 0..7 */
    FA_FACTION1,     /* 1..7 (no natives: avoids viewer popups for faction 0) */
    FA_FACTION_OPT,  /* -1..7 */
    FA_VEH,          /* 0..VehCount-1 */
    FA_VEH_OPT,      /* -1..VehCount-1 */
    FA_BASE,         /* 0..BaseCount-1 */
    FA_BASE_OPT,     /* -1..BaseCount-1 */
    FA_UNIT,         /* basic units 0..63 and active faction prototypes */
    FA_TECH,         /* -1..88 */
    FA_X,            /* map tile: FA_X must be directly followed by FA_Y (one valid (x, y) pair) */
    FA_Y,
    FA_BOOL,         /* 0, 1 */
    FA_RANGE,        /* lo..hi inclusive */
    FA_CONST,        /* lo */
    FA_INT,          /* random 32-bit values */
    FA_BIT,          /* single bits 1 << lo .. 1 << hi */
    FA_OUT,          /* pointer to an int32 out-parameter (initialised to 0x7777; compared) */
    FA_STR,          /* pointer to a 256-byte char buffer (initialised to "x"; compared) */
    FA_BUF,          /* pointer to an int32[16] buffer (+ a 0 terminator) filled with values lo..hi
                        derived from a seed (64 seeds; one if lo == hi); compared after the call */
    FA_BUF_OPT,      /* like FA_BUF, plus a null pointer */
    FA_BASE_SIGNED,  /* -BaseCount..BaseCount-1 (base_build: -1 - id selects base id) */
};
struct FcArg {
    FcArgKind kind;
    int32_t lo, hi;
};
enum FcFlags : uint32_t {
    FC_VOID = 1,      /* no return value */
    FC_MUTATES = 2,   /* changes game memory: compare the memory image and map after the call */
    FC_SETBASE = 4,   /* the first argument (FA_BASE) is made the current base (set_base) before the
                         call and not passed; the remaining arguments shift down */
    FC_COMPUTE = 8,   /* with FC_SETBASE: also run the original base_compute(1) before the call */
    FC_PATHS = 16,    /* THISCALL method of the Paths object (0x945B00): called with ECX = &Paths;
                         its heap tables are saved/restored around each side and compared; the
                         dead stack dword at [entry esp - 0x30] (Path_find's uninitialised local)
                         is set to 0 / non-zero on alternate calls */
};
struct FcFunc {
    const char *name;  /* as in the ADDR declaration (and the patch table) */
    uint32_t flags;
    FcArg args[8];  /* up to 8 arguments */
};
