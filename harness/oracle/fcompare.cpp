/* fcompare: side-by-side comparison of original and reconstructed functions (see fcompare.h). */
#include "fcompare.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/clock.h"
#include "engine/globals.h"
#include "engine/hybrid_patch.h"
#include "engine/memmap.h"
#include "fcompare_table.h"
#include "state_hash.h"

namespace {

constexpr uint32_t kImgLo = 0x682000, kImgHi = 0x9CA000;
constexpr int kLogPerFunc = 20;

struct FcState {
    const FcFunc* f;
    const HybridPatch* p;
    uint64_t calls = 0, bad = 0;
    int logged = 0;
};

bool g_on = false;
int g_from = 0, g_to = -1;
bool g_hook_turn = true, g_hook_faction = false, g_hook_enemy = false;
int g_limit_pure = 2000, g_limit_mut = 100;
int g_every = 1; /* every=N: only the hooks of turns from, from + N, ... */
bool g_golden = false; /* golden: record the original's results (golden.tsv) */
std::vector<FcState> g_funcs;
FILE* g_log;
FILE* g_golden_file;
uint64_t g_total_calls, g_total_bad;

/* deterministic private generator (does not touch the game's generators) */
uint32_t g_rng;
uint32_t rnd() { return g_rng = g_rng * 1664525u + 1013904223u; }

void flog(const char* fmt, ...) {
    if (!g_log) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\fcompare.log", oracle_outdir());
        g_log = fopen(p, "a");
        if (!g_log) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* ---- golden records (harness/README.md "Golden records") ----
 * golden.tsv: "STATE <turn> <state hash>" at each recorded hook, then one line per call of a
 * function that changes no game memory (no FC_MUTATES, no FC_PATHS):
 * <name> <turn> <args> <return> <outputs> <crt state> <game state> (tab-separated), the original's
 * results. src/tools/golden.py imports the file into tests/golden/; smacx_headless --golden
 * replays it. */
bool golden_eligible(const FcFunc& f) { return !(f.flags & (FC_MUTATES | FC_PATHS)); }
void gput(const char* fmt, ...) {
    if (!g_golden_file) {
        char p[MAX_PATH];
        snprintf(p, sizeof(p), "%s\\golden.tsv", oracle_outdir());
        g_golden_file = fopen(p, "w");
        if (!g_golden_file) return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_golden_file, fmt, ap);
    va_end(ap);
}
/* A string output: bytes outside 0x21..0x7E, '%' and ',' as %XX. */
std::string golden_escape(const char* s) {
    std::string r;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c <= 0x20 || c >= 0x7F || c == '%' || c == ',') {
            char b[4];
            snprintf(b, sizeof(b), "%%%02X", c);
            r += b;
        } else {
            r += (char)c;
        }
    }
    return r;
}
/* FNV-1a 32 of a FA_BUF buffer after the call. */
uint32_t golden_buf_hash(const int32_t* b) {
    uint32_t h = 2166136261u;
    for (int k = 0; k < 17; k++)
        for (int j = 0; j < 4; j++) h = (h ^ (((uint32_t)b[k] >> (8 * j)) & 0xFF)) * 16777619u;
    return h;
}

/* ---- state snapshots ---- */
struct Snap {
    std::vector<uint8_t> img, map;
    uint32_t crt = 0, game = 0;
};
size_t map_bytes() { return (size_t)(MapAreaX / 2) * MapAreaY * sizeof(MAP); }
void take(Snap& s) {
    s.img.assign((uint8_t*)kImgLo, (uint8_t*)kImgHi);
    s.map.assign((uint8_t*)MapTiles, (uint8_t*)MapTiles + map_bytes());
    s.crt = oracle_crt_state();
    s.game = *(uint32_t*)0x9BB568;
}
void put(const Snap& s) {
    memcpy((void*)kImgLo, s.img.data(), s.img.size());
    if (s.map.size() == map_bytes()) memcpy(MapTiles, s.map.data(), s.map.size());
    oracle_crt_set_state(s.crt);
    *(uint32_t*)0x9BB568 = s.game;
}
/* Name of an image address (main game tables), for mismatch reports. */
std::string addr_name(uint32_t a) {
    struct T {
        const char* n;
        uint32_t base, size, count;
    } tabs[] = {{"Vehs", 0x952828, 0x34, 2049},
                {"Bases", 0x97D040, 0x134, 512},
                {"Factions", 0x96C9E0, 0x20CC, 8},
                {"MFactions", 0x946A50, 0x59C, 8},
                {"Units", 0x9AB868, 0x34, 512}};
    char b[96];
    for (auto& t : tabs)
        if (a >= t.base && a < t.base + t.size * t.count) {
            snprintf(b, sizeof(b), "%s[%u]+0x%X", t.n, (a - t.base) / t.size, (a - t.base) % t.size);
            return b;
        }
    snprintf(b, sizeof(b), "%08X", a);
    return b;
}
/* First difference between two snapshots ("" if equal). */
std::string snap_diff(const Snap& a, const Snap& b) {
    char buf[256];
    size_t n = a.img.size() < b.img.size() ? a.img.size() : b.img.size();
    auto ignored = [](size_t i) {
        uint32_t addr = kImgLo + (uint32_t)i;
        for (auto& r : kFcIgnore)
            if (addr >= r.addr && addr < r.addr + r.size) return true;
        return false;
    };
    for (size_t i = 0; i < n; i++)
        if (a.img[i] != b.img[i] && !ignored(i)) {
            int cnt = 0;
            for (size_t j = i; j < n; j++) cnt += a.img[j] != b.img[j] && !ignored(j);
            snprintf(buf, sizeof(buf), "memory %s orig=%02X recon=%02X (%d bytes differ)",
                     addr_name(kImgLo + (uint32_t)i).c_str(), a.img[i], b.img[i], cnt);
            return buf;
        }
    if (a.map != b.map) {
        size_t m = a.map.size() < b.map.size() ? a.map.size() : b.map.size();
        for (size_t i = 0; i < m; i++)
            if (a.map[i] != b.map[i]) {
                snprintf(buf, sizeof(buf), "MapTiles[%u]+%u orig=%02X recon=%02X", (unsigned)(i / sizeof(MAP)),
                         (unsigned)(i % sizeof(MAP)), a.map[i], b.map[i]);
                return buf;
            }
        return "MapTiles size";
    }
    if (a.crt != b.crt) return "crt rand state " + std::to_string(a.crt) + " vs " + std::to_string(b.crt);
    if (a.game != b.game) return "game rand state " + std::to_string(a.game) + " vs " + std::to_string(b.game);
    return "";
}

__attribute__((noinline)) void poison() {
    volatile uint8_t buf[0x8000];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = 0;
    __asm__ volatile("" ::"r"(buf) : "memory");
}

/* THISCALL call with ECX = self; restores ESP itself (works for any callee argument count).
 * slot30: written to [callee entry esp - 0x30] just before the call (FC_PATHS). */
extern "C" uint32_t __cdecl fc_call_this(uint32_t fn, uint32_t self, const uint32_t* args, uint32_t slot30);
__asm__(
    ".text\n.globl _fc_call_this\n_fc_call_this:\n"
    "push %ebp\n mov %esp, %ebp\n push %esi\n push %edi\n push %ebx\n mov %esp, %esi\n"
    "mov 16(%ebp), %edi\n"
    "push 28(%edi)\n push 24(%edi)\n push 20(%edi)\n push 16(%edi)\n"
    "push 12(%edi)\n push 8(%edi)\n push 4(%edi)\n push (%edi)\n"
    "mov 20(%ebp), %eax\n mov %eax, -0x34(%esp)\n"
    "mov 12(%ebp), %ecx\n mov 8(%ebp), %eax\n call *%eax\n"
    "mov %esi, %esp\n pop %ebx\n pop %edi\n pop %esi\n pop %ebp\n ret\n");

/* FC_PATHS: the Paths object's heap tables (map costs, queue). */
struct PathTabs {
    std::vector<uint8_t> map, xs, ys;
};
void paths_take(PathTabs& t) {
    uint32_t* p = (uint32_t*)0x945B00;
    size_t n = (size_t)MapAreaTiles;
    t.map.assign((uint8_t*)p[0], (uint8_t*)p[0] + n * 4);
    t.xs.assign((uint8_t*)p[1], (uint8_t*)p[1] + n * 2);
    t.ys.assign((uint8_t*)p[2], (uint8_t*)p[2] + n * 2);
}
void paths_put(const PathTabs& t) {
    uint32_t* p = (uint32_t*)0x945B00;
    memcpy((void*)p[0], t.map.data(), t.map.size());
    memcpy((void*)p[1], t.xs.data(), t.xs.size());
    memcpy((void*)p[2], t.ys.data(), t.ys.size());
}
uint32_t g_paths_calls;

/* ---- argument domains ---- */
typedef uint32_t(__cdecl* Fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

struct Domain {
    std::vector<uint32_t> v; /* values (for tiles: x, then y in v2) */
    std::vector<uint32_t> v2;
    bool tile = false;
};
void build_domain(const FcArg& a, Domain& d) {
    auto range = [&](int lo, int hi) {
        for (int i = lo; i <= hi; i++) d.v.push_back((uint32_t)i);
    };
    switch (a.kind) {
    case FA_FACTION: range(0, 7); break;
    case FA_FACTION1: range(1, 7); break;
    case FA_FACTION_OPT: range(-1, 7); break;
    case FA_VEH: range(0, VehCount - 1); break;
    case FA_VEH_OPT: range(-1, VehCount - 1); break;
    case FA_BASE: range(0, BaseCount - 1); break;
    case FA_BASE_OPT: range(-1, BaseCount - 1); break;
    case FA_BASE_SIGNED: range(-BaseCount, BaseCount - 1); break;
    case FA_UNIT:
        for (int u = 0; u < 512; u++)
            if (u < 64 || (Units[u].unit_flags & 1)) d.v.push_back((uint32_t)u);
        break;
    case FA_TECH: range(-1, 88); break;
    case FA_X:
        d.tile = true;
        for (int y = 0; y < MapAreaY; y++)
            for (int x = y & 1; x < MapAreaX; x += 2) {
                d.v.push_back((uint32_t)x);
                d.v2.push_back((uint32_t)y);
            }
        break;
    case FA_BOOL: range(0, 1); break;
    case FA_RANGE: range(a.lo, a.hi); break;
    case FA_CONST: d.v.push_back((uint32_t)a.lo); break;
    case FA_INT:
        for (int i = 0; i < 64; i++) d.v.push_back(rnd());
        d.v.push_back(0);
        d.v.push_back(0xFFFFFFFF);
        break;
    case FA_BIT:
        for (int i = a.lo; i <= a.hi; i++) d.v.push_back(1u << i);
        break;
    case FA_OUT:
    case FA_STR:
        d.v.push_back(0); /* replaced by a buffer pointer at call time */
        break;
    case FA_BUF:
    case FA_BUF_OPT: /* seeds; replaced by a buffer pointer at call time (FA_BUF_OPT: -1 = null) */
        if (a.kind == FA_BUF_OPT) d.v.push_back(0xFFFFFFFF);
        range(0, a.lo == a.hi ? 0 : 63);
        break;
    default: break;
    }
}

struct Call {
    uint32_t a[8];
};

/* Runs one comparison; returns false on mismatch (and logs it). */
bool compare_one(FcState& st, const Call& c, int nargs, const char* where) {
    const FcFunc& f = *st.f;
    bool mut = f.flags & FC_MUTATES;
    bool setbase = f.flags & FC_SETBASE;
    bool snap = mut || setbase; /* setup writes memory: restore after each side */
    int32_t outs[2][8];
    char strs[2][8][256];
    int32_t bufs[2][8][17];
    uint32_t ret[2];
    Snap s0, after[2];
    uint32_t crt0 = oracle_crt_state(), game0 = *(uint32_t*)0x9BB568;
    uint32_t clock0 = engine_logic_clock_state(); /* logic-clock sites (timeGetTime) see the same value */
    if (snap) take(s0);
    bool paths = f.flags & FC_PATHS;
    PathTabs pt0, pt[2];
    uint32_t slot30 = (g_paths_calls++ & 1) ? 0x5C105Au : 0;
    if (paths) paths_take(pt0);
    for (int side = 0; side < 2; side++) {
        uint32_t a[8];
        for (int i = 0; i < 8; i++) {
            a[i] = c.a[i];
            if (i < nargs && f.args[i].kind == FA_OUT) {
                outs[side][i] = 0x7777;
                a[i] = (uint32_t)(uintptr_t)&outs[side][i];
            } else if (i < nargs && f.args[i].kind == FA_STR) {
                memset(strs[side][i], 0, sizeof(strs[side][i])); /* defined bytes after the string */
                strcpy(strs[side][i], "x");
                a[i] = (uint32_t)(uintptr_t)strs[side][i];
            } else if (i < nargs && (f.args[i].kind == FA_BUF || f.args[i].kind == FA_BUF_OPT)) {
                uint32_t span = (uint32_t)(f.args[i].hi - f.args[i].lo) + 1;
                for (uint32_t k = 0; k < 16; k++) {
                    uint32_t h = (c.a[i] * 16 + k + 1) * 2654435761u;
                    h ^= h >> 15;
                    h *= 2246822519u;
                    h ^= h >> 13;
                    bufs[side][i][k] = f.args[i].lo + (int32_t)(span ? h % span : h);
                }
                bufs[side][i][16] = 0;
                a[i] = c.a[i] == 0xFFFFFFFF ? 0 : (uint32_t)(uintptr_t)bufs[side][i];
            }
        }
        Fn8 fn;
        int prev = -1;
        if (side == 0) {
            prev = oracle_patch_set(f.name, 0);
            fn = (Fn8)(uintptr_t)st.p->addr;
        } else {
            fn = (Fn8)st.p->func;
        }
        if (setbase) {
            /* set_base(a[0]) (and the original base_compute(1)); the other arguments shift down */
            *(int32_t*)0x689370 = (int32_t)a[0];
            *(uint32_t*)0x90EA30 = 0x97D040u + a[0] * 0x134u;
            if (f.flags & FC_COMPUTE) {
                int cprev = oracle_patch_set("base_compute", 0);
                ((void(__cdecl*)(int32_t))0x4EC3B0)(1);
                if (cprev > 0) oracle_patch_set("base_compute", 1);
            }
            for (int i = 0; i < 7; i++) a[i] = a[i + 1];
            a[7] = 0;
        }
        if (paths) paths_put(pt0);
        poison();
        if (paths) {
            ret[side] = fc_call_this((uint32_t)(uintptr_t)fn, 0x945B00u, a, slot30);
            paths_take(pt[side]);
            paths_put(pt0);
        } else {
            ret[side] = fn(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        }
        if (side == 0 && prev > 0) oracle_patch_set(f.name, 1);
        if (snap) {
            take(after[side]);
            put(s0);
        } else {
            after[side].crt = oracle_crt_state();
            after[side].game = *(uint32_t*)0x9BB568;
            oracle_crt_set_state(crt0);
            *(uint32_t*)0x9BB568 = game0;
        }
        engine_set_logic_clock_state(clock0);
    }
    if (g_golden && golden_eligible(f)) {
        std::string args, outv;
        char b[64];
        for (int i = 0; i < nargs; i++) {
            if (i) args += ",";
            FcArgKind k = f.args[i].kind;
            if (k == FA_OUT || k == FA_STR) {
                args += k == FA_OUT ? "&" : "&s";
            } else if (k == FA_BUF || k == FA_BUF_OPT) {
                snprintf(b, sizeof(b), "b%u", c.a[i]);
                args += c.a[i] == 0xFFFFFFFF ? "null" : b;
            } else {
                args += std::to_string((int32_t)c.a[i]);
            }
            if (k == FA_OUT || k == FA_STR || ((k == FA_BUF || k == FA_BUF_OPT) && c.a[i] != 0xFFFFFFFF)) {
                if (!outv.empty()) outv += ",";
                if (k == FA_OUT) {
                    outv += std::to_string(outs[0][i]);
                } else if (k == FA_STR) {
                    outv += "s" + golden_escape(strs[0][i]);
                } else {
                    snprintf(b, sizeof(b), "h%08X", golden_buf_hash(bufs[0][i]));
                    outv += b;
                }
            }
        }
        if (f.flags & FC_VOID) snprintf(b, sizeof(b), "-");
        else snprintf(b, sizeof(b), "%d", (int32_t)ret[0]);
        gput("%s\t%d\t%s\t%s\t%s\t%u\t%u\n", f.name, CurrentTurn, args.empty() ? "-" : args.c_str(), b,
             outv.empty() ? "-" : outv.c_str(), after[0].crt, after[0].game);
    }
    std::string why;
    if (!(f.flags & FC_VOID) && ret[0] != ret[1]) {
        char b[96];
        snprintf(b, sizeof(b), "return orig=%d recon=%d", (int32_t)ret[0], (int32_t)ret[1]);
        why = b;
    }
    for (int i = 0; i < nargs && why.empty(); i++) {
        if (f.args[i].kind == FA_OUT && outs[0][i] != outs[1][i]) {
            char b[96];
            snprintf(b, sizeof(b), "out arg %d orig=%d recon=%d", i, outs[0][i], outs[1][i]);
            why = b;
        }
        if ((f.args[i].kind == FA_BUF || f.args[i].kind == FA_BUF_OPT) && c.a[i] != 0xFFFFFFFF &&
            memcmp(bufs[0][i], bufs[1][i], sizeof(bufs[0][i]))) {
            int k = 0;
            while (bufs[0][i][k] == bufs[1][i][k]) k++;
            char b[96];
            snprintf(b, sizeof(b), "buf arg %d[%d] orig=%d recon=%d", i, k, bufs[0][i][k], bufs[1][i][k]);
            why = b;
        }
        if (f.args[i].kind == FA_STR && strcmp(strs[0][i], strs[1][i])) {
            why = std::string("string arg orig=\"") + strs[0][i] + "\" recon=\"" + strs[1][i] + "\"";
        }
    }
    if (why.empty() && mut) why = snap_diff(after[0], after[1]);
    if (why.empty() && paths) {
        const char* names[3] = {"Paths.mapTable", "Paths.xTable", "Paths.yTable"};
        const std::vector<uint8_t>* t0[3] = {&pt[0].map, &pt[0].xs, &pt[0].ys};
        const std::vector<uint8_t>* t1[3] = {&pt[1].map, &pt[1].xs, &pt[1].ys};
        for (int k = 0; k < 3 && why.empty(); k++)
            if (*t0[k] != *t1[k]) {
                size_t i = 0;
                while (i < t0[k]->size() && (*t0[k])[i] == (*t1[k])[i]) i++;
                char b[96];
                snprintf(b, sizeof(b), "%s byte %u orig=%02X recon=%02X", names[k], (unsigned)i, (*t0[k])[i],
                         (*t1[k])[i]);
                why = b;
            }
    }
    if (why.empty() && !mut && (after[0].crt != after[1].crt || after[0].game != after[1].game))
        why = "random number draws differ";
    st.calls++;
    g_total_calls++;
    if (why.empty()) return true;
    st.bad++;
    g_total_bad++;
    if (st.logged++ < kLogPerFunc) {
        std::string args;
        for (int i = 0; i < nargs; i++) {
            if (i) args += ",";
            if (f.args[i].kind == FA_OUT || f.args[i].kind == FA_STR) args += "&";
            else if (f.args[i].kind == FA_BUF || f.args[i].kind == FA_BUF_OPT)
                args += c.a[i] == 0xFFFFFFFF ? std::string("null") : "&buf" + std::to_string(c.a[i]);
            else args += std::to_string((int32_t)c.a[i]);
        }
        flog("MISMATCH T%d %s: %s(%s): %s\n", CurrentTurn, where, f.name, args.c_str(), why.c_str());
    }
    return false;
}

void run_func(FcState& st, const char* where) {
    const FcFunc& f = *st.f;
    int nargs = 0;
    while (nargs < 8 && f.args[nargs].kind != FA_NONE) nargs++;
    /* domains: a tile occupies two argument slots */
    std::vector<Domain> doms;
    std::vector<int> slot; /* first argument index of each domain */
    for (int i = 0; i < nargs; i++) {
        if (f.args[i].kind == FA_Y) continue;
        Domain d;
        build_domain(f.args[i], d);
        if (d.v.empty()) return; /* e.g. no vehicles yet */
        doms.push_back(std::move(d));
        slot.push_back(i);
    }
    double total = 1;
    for (auto& d : doms) total *= (double)d.v.size();
    int limit = (f.flags & FC_MUTATES) ? g_limit_mut : g_limit_pure;
    auto fill = [&](Call& c, const std::vector<size_t>& idx) {
        memset(&c, 0, sizeof(c));
        for (size_t k = 0; k < doms.size(); k++) {
            c.a[slot[k]] = doms[k].v[idx[k]];
            if (doms[k].tile) c.a[slot[k] + 1] = doms[k].v2[idx[k]];
        }
    };
    std::vector<size_t> idx(doms.size(), 0);
    Call c;
    Snap pure0;
    bool pure = !(f.flags & FC_MUTATES);
    if (pure) take(pure0);
    if (total <= limit) {
        for (;;) {
            fill(c, idx);
            compare_one(st, c, nargs, where);
            size_t k = 0;
            for (; k < doms.size(); k++) {
                if (++idx[k] < doms[k].v.size()) break;
                idx[k] = 0;
            }
            if (k == doms.size()) break;
        }
    } else {
        for (int n = 0; n < limit; n++) {
            for (size_t k = 0; k < doms.size(); k++) idx[k] = rnd() % doms[k].v.size();
            fill(c, idx);
            compare_one(st, c, nargs, where);
        }
    }
    if (pure) {
        Snap now;
        take(now);
        std::string d = snap_diff(pure0, now);
        if (!d.empty()) {
            st.bad++;
            g_total_bad++;
            flog("MISMATCH T%d %s: %s is declared pure but changed memory: %s (orig/recon unknown; restored)\n",
                 CurrentTurn, where, f.name, d.c_str());
            put(pure0);
        }
    }
}

void write_summary() {
    char p[MAX_PATH];
    snprintf(p, sizeof(p), "%s\\fcompare_summary.txt", oracle_outdir());
    FILE* f = fopen(p, "w");
    if (!f) return;
    for (auto& st : g_funcs)
        fprintf(f, "FUNC %-24s calls=%llu mismatches=%llu\n", st.f->name, (unsigned long long)st.calls,
                (unsigned long long)st.bad);
    fprintf(f, "TOTAL functions=%d calls=%llu mismatches=%llu\n", (int)g_funcs.size(),
            (unsigned long long)g_total_calls, (unsigned long long)g_total_bad);
    fclose(f);
}

const HybridPatch* find_patch(const char* name) {
    for (const HybridPatch* p = kHybridPatches; p->func; p++)
        if (!strcmp(p->name, name)) return p;
    return nullptr;
}

void select_func(const char* name, bool on) {
    for (const FcFunc& f : kFcFuncs) {
        if (name && strcmp(f.name, name)) continue;
        bool have = false;
        for (size_t i = 0; i < g_funcs.size(); i++)
            if (g_funcs[i].f == &f) {
                have = true;
                if (!on) g_funcs.erase(g_funcs.begin() + i);
                break;
            }
        if (on && !have) {
            const HybridPatch* p = find_patch(f.name);
            if (!p) {
                if (name) oracle_logf("fcompare: %s is not reconstructed\n", f.name);
                continue;
            }
            FcState st;
            st.f = &f;
            st.p = p;
            g_funcs.push_back(st);
        }
        if (name) return;
    }
    if (name) oracle_logf("fcompare: no argument table entry for %s (harness/oracle/fcompare_table.h)\n", name);
}

}  // namespace

void fcompare_cmd(const char* args) {
    std::string s(args);
    std::vector<std::string> tok;
    for (char* t = strtok(&s[0], " \t"); t; t = strtok(NULL, " \t")) tok.push_back(t);
    if (tok.size() < 3) {
        oracle_logf("fcompare: usage: fcompare <from> <to> [hooks=..] [limit=p,m] <spec...>\n");
        return;
    }
    g_from = atoi(tok[0].c_str());
    g_to = atoi(tok[1].c_str());
    for (size_t i = 2; i < tok.size(); i++) {
        const std::string& t = tok[i];
        if (!t.compare(0, 6, "hooks=")) {
            g_hook_turn = t.find("turn") != std::string::npos;
            g_hook_faction = t.find("faction") != std::string::npos;
            g_hook_enemy = t.find("enemy") != std::string::npos;
        } else if (!t.compare(0, 6, "every=")) {
            g_every = atoi(t.c_str() + 6) > 0 ? atoi(t.c_str() + 6) : 1;
        } else if (t == "golden") {
            g_golden = true;
        } else if (!t.compare(0, 6, "limit=")) {
            sscanf(t.c_str() + 6, "%d,%d", &g_limit_pure, &g_limit_mut);
        } else if (t == "all") {
            select_func(nullptr, true);
        } else if (t == "none") {
            g_funcs.clear();
        } else {
            bool on = t[0] != '-';
            select_func((t[0] == '+' || t[0] == '-') ? t.c_str() + 1 : t.c_str(), on);
        }
    }
    if (g_golden) g_hook_turn = true, g_hook_faction = g_hook_enemy = false; /* replayed at turn_upkeep */
    g_on = true;
    oracle_logf("fcompare: %d functions, turns %d-%d every %d, hooks%s%s%s, limits %d/%d%s\n", (int)g_funcs.size(),
                g_from, g_to, g_every, g_hook_turn ? " turn" : "", g_hook_faction ? " faction" : "",
                g_hook_enemy ? " enemy" : "", g_limit_pure, g_limit_mut, g_golden ? ", golden records" : "");
}

void fcompare_hook(const char* where, int arg) {
    if (!g_on || CurrentTurn < g_from || CurrentTurn > g_to || (CurrentTurn - g_from) % g_every) return;
    if ((where[0] == 't' && !g_hook_turn) || (where[0] == 'f' && !g_hook_faction) || (where[0] == 'e' && !g_hook_enemy))
        return;
    g_rng = 0x9E3779B9u ^ (uint32_t)(CurrentTurn * 977 + arg * 31 + where[0]);
    uint64_t c0 = g_total_calls, b0 = g_total_bad;
    char w[32];
    snprintf(w, sizeof(w), "%s%d", where, arg);
    if (g_golden) gput("STATE\t%d\t%016llX\n", CurrentTurn, (unsigned long long)state_hash());
    for (auto& st : g_funcs) run_func(st, w);
    if (g_golden_file) fflush(g_golden_file);
    flog("T%d %s: %llu calls, %llu mismatches\n", CurrentTurn, w, (unsigned long long)(g_total_calls - c0),
         (unsigned long long)(g_total_bad - b0));
    oracle_logf("fcompare T%d %s: %llu calls, %llu mismatches\n", CurrentTurn, w,
                (unsigned long long)(g_total_calls - c0), (unsigned long long)(g_total_bad - b0));
    write_summary();
}

void fcompare_finish() {
    if (!g_on) return;
    write_summary();
    if (g_golden_file) fclose(g_golden_file), g_golden_file = nullptr;
    oracle_logf("fcompare total: %llu calls, %llu mismatches\n", (unsigned long long)g_total_calls,
                (unsigned long long)g_total_bad);
}
