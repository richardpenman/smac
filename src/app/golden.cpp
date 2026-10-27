// Replay of golden records (see golden.h).
#include "golden.h"

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

#include "engine/state.h"
#include "engine/base.h"
#include "engine/clock.h"
#include "engine/globals.h"
#include "engine/memmap.h"
#include "engine/random.h"
#include "oracle/fcompare_table.h"
#include "state_hash.h"
#define GOLDEN_INCLUDES
#include "golden_funcs.h"
#undef GOLDEN_INCLUDES

namespace fs = std::filesystem;

namespace {

// ---- calling a reconstructed function with recorded arguments ----
struct Result {
    uint32_t value = 0;
    uint32_t mask = 0;  // the bits of the return type (the original's EAX beyond them is undefined)
    bool ok = true;     // false: a pointer outside the memory image (no original address)
};
template <class T>
T arg_as(int64_t v) {
    if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(static_cast<uintptr_t>(v));
    else return static_cast<T>(v);
}
template <class R, class... A, size_t... I>
Result invoke(R (*fn)(A...), const int64_t* a, std::index_sequence<I...>) {
    Result r;
    if constexpr (std::is_void_v<R>) {
        fn(arg_as<A>(a[I])...);
    } else if constexpr (std::is_pointer_v<R>) {
        auto p = reinterpret_cast<const uint8_t*>(fn(arg_as<A>(a[I])...));
        r.mask = 0xFFFFFFFFu;
        if (uint32_t addr = mem::address_of(p)) r.value = addr;  // the image or a game-state record
        else r.ok = p == nullptr;
    } else {
        R v = fn(arg_as<A>(a[I])...);
        r.value = static_cast<uint32_t>(static_cast<int64_t>(v));
        r.mask = sizeof(R) >= 4 ? 0xFFFFFFFFu : (1u << (8 * sizeof(R))) - 1;
    }
    return r;
}
template <class R, class... A>
Result invoke(R (*fn)(A...), const int64_t* a) {
    static_assert(sizeof...(A) <= 8, "at most 8 arguments");
    return invoke(fn, a, std::index_sequence_for<A...>{});
}
struct Func {
    const char* name;
    Result (*call)(const int64_t*);
};
const Func kFuncs[] = {
#define GOLDEN_FUNC(name) {#name, [](const int64_t* a) { return invoke(&name, a); }},
#include "golden_funcs.h"
#undef GOLDEN_FUNC
};

// ---- records ----
struct Record {
    const Func* func;
    const FcFunc* entry;  // fcompare_table.h: argument kinds, FC_SETBASE / FC_COMPUTE
    std::string args, ret, outs;
    uint32_t crt, game;
};
struct Turn {
    uint64_t hash = 0;
    std::vector<Record> records;
    bool replayed = false;
};
std::map<int, Turn> g_turns;
std::string g_scenario;
struct Stats {
    long calls = 0, bad = 0;
    int logged = 0;
};
std::map<std::string, Stats> g_stats;
long g_bad_states = 0;

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> r;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            r.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    r.push_back(cur);
    return r;
}

std::string unescape(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            r += static_cast<char>(std::strtoul(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            r += s[i];
        }
    }
    return r;
}

// fcompare's FA_BUF contents (fcompare.cpp compare_one).
void fill_buf(int32_t* b, uint32_t seed, const FcArg& a) {
    uint32_t span = static_cast<uint32_t>(a.hi - a.lo) + 1;
    for (uint32_t k = 0; k < 16; k++) {
        uint32_t h = (seed * 16 + k + 1) * 2654435761u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        b[k] = a.lo + static_cast<int32_t>(span ? h % span : h);
    }
    b[16] = 0;
}
uint32_t buf_hash(const int32_t* b) {
    uint32_t h = 2166136261u;
    for (int k = 0; k < 17; k++)
        for (int j = 0; j < 4; j++) h = (h ^ ((static_cast<uint32_t>(b[k]) >> (8 * j)) & 0xFF)) * 16777619u;
    return h;
}

void glog(FILE* log, const char* fmt, ...) {
    if (!log) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(log, fmt, ap);
    va_end(ap);
    std::fflush(log);
}

// Replays one record; returns "" or what differs.
std::string replay(const Record& r) {
    const FcFunc& f = *r.entry;
    int nargs = 0;
    while (nargs < 8 && f.args[nargs].kind != FA_NONE) nargs++;
    std::vector<std::string> tok = r.args == "-" ? std::vector<std::string>{} : split(r.args, ',');
    if (static_cast<int>(tok.size()) != nargs) return "argument count differs from fcompare_table.h";
    int64_t a[8] = {};
    int32_t outs[8];
    char strs[8][256];
    int32_t bufs[8][17];
    for (int i = 0; i < nargs; i++) {
        FcArgKind k = f.args[i].kind;
        const std::string& t = tok[i];
        if (t == "&") {
            outs[i] = 0x7777;
            a[i] = reinterpret_cast<intptr_t>(&outs[i]);
        } else if (t == "&s") {
            std::memset(strs[i], 0, sizeof(strs[i]));  // as fcompare: defined bytes after the string
            std::strcpy(strs[i], "x");
            a[i] = reinterpret_cast<intptr_t>(strs[i]);
        } else if (t == "null") {
            a[i] = 0;
        } else if (t[0] == 'b' && (k == FA_BUF || k == FA_BUF_OPT)) {
            fill_buf(bufs[i], static_cast<uint32_t>(std::strtoul(t.c_str() + 1, nullptr, 10)), f.args[i]);
            a[i] = reinterpret_cast<intptr_t>(bufs[i]);
        } else {
            a[i] = std::strtoll(t.c_str(), nullptr, 10);
        }
    }
    if (f.flags & FC_SETBASE) {  // fcompare: set_base(a[0]) (and base_compute(1)); the rest shift down
        set_base(static_cast<int32_t>(a[0]));
        if (f.flags & FC_COMPUTE) base_compute(1);
        for (int i = 0; i < 7; i++) a[i] = a[i + 1];
        a[7] = 0;
    }
    Result res = r.func->call(a);
    char b[160];
    if (!res.ok) return "returns a pointer outside the memory image";
    if (r.ret != "-") {
        uint32_t want = static_cast<uint32_t>(std::strtoll(r.ret.c_str(), nullptr, 10));
        if ((want ^ res.value) & res.mask) {
            std::snprintf(b, sizeof(b), "return original=%d reconstruction=%d", static_cast<int32_t>(want & res.mask),
                          static_cast<int32_t>(res.value & res.mask));
            return b;
        }
    }
    std::vector<std::string> want_outs = r.outs == "-" ? std::vector<std::string>{} : split(r.outs, ',');
    size_t o = 0;
    for (int i = 0; i < nargs; i++) {
        const std::string& t = tok[i];
        bool is_out = t == "&" || t == "&s" || (t[0] == 'b' && (f.args[i].kind == FA_BUF || f.args[i].kind == FA_BUF_OPT));
        if (!is_out) continue;
        if (o >= want_outs.size()) return "fewer outputs recorded than out-arguments";
        const std::string& w = want_outs[o++];
        if (t == "&" && std::to_string(outs[i]) != w) {
            std::snprintf(b, sizeof(b), "out arg %d original=%s reconstruction=%d", i, w.c_str(), outs[i]);
            return b;
        }
        if (t == "&s" && unescape(w.substr(1)) != strs[i])
            return "string arg " + std::to_string(i) + " original=\"" + unescape(w.substr(1)) + "\" reconstruction=\"" +
                   strs[i] + "\"";
        if (t[0] == 'b') {
            std::snprintf(b, sizeof(b), "h%08X", buf_hash(bufs[i]));
            if (w != b) return "buffer arg " + std::to_string(i) + " differs";
        }
    }
    if (crt_rand_state() != r.crt || game_random_state() != r.game) {
        std::snprintf(b, sizeof(b), "random states after the call: original crt=%u game=%u, reconstruction crt=%u game=%u",
                      r.crt, r.game, crt_rand_state(), game_random_state());
        return b;
    }
    return "";
}

}  // namespace

long golden_load(const std::string& dir, const std::string& scenario, std::string* err) {
    g_scenario = scenario;
    g_turns.clear();
    std::ifstream states(fs::path(dir) / "states.tsv");
    if (!states) {
        *err = dir + "/states.tsv not found";
        return -1;
    }
    std::string line;
    std::getline(states, line);
    while (std::getline(states, line)) {
        auto p = split(line, '\t');
        if (p.size() == 3 && p[0] == scenario) g_turns[std::atoi(p[1].c_str())].hash = std::strtoull(p[2].c_str(), nullptr, 16);
    }
    long n = 0;
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".tsv" && e.path().filename() != "states.tsv") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& path : files) {
        std::string name = path.stem().string();
        const Func* func = nullptr;
        for (const Func& f : kFuncs)
            if (name == f.name) func = &f;
        const FcFunc* entry = nullptr;
        for (const FcFunc& f : kFcFuncs)
            if (name == f.name && !(f.flags & (FC_MUTATES | FC_PATHS))) {
                entry = &f;
                break;
            }
        if (!func || !entry) {
            *err = path.string() + ": " + name + " is not a function golden records can replay";
            return -1;
        }
        std::ifstream in(path);
        std::getline(in, line);
        while (std::getline(in, line)) {
            auto p = split(line, '\t');
            if (p.size() != 7) {
                *err = path.string() + ": bad row " + line.substr(0, 60);
                return -1;
            }
            if (p[0] != scenario) continue;
            auto t = g_turns.find(std::atoi(p[1].c_str()));
            if (t == g_turns.end()) {
                *err = path.string() + ": turn " + p[1] + " has no state in states.tsv";
                return -1;
            }
            t->second.records.push_back({func, entry, p[2], p[3], p[4], static_cast<uint32_t>(std::stoul(p[5])),
                                         static_cast<uint32_t>(std::stoul(p[6]))});
            n++;
        }
    }
    return n;
}

void golden_hook(FILE* log) {
    auto t = g_turns.find(CurrentTurn);
    if (t == g_turns.end() || t->second.replayed) return;
    Turn& turn = t->second;
    turn.replayed = true;
    uint64_t hash = state_hash();
    if (hash != turn.hash) {
        g_bad_states++;
        glog(log, "golden: turn %d: state %016llX, recorded on %016llX: the game is not where the records were made\n",
             CurrentTurn, static_cast<unsigned long long>(hash), static_cast<unsigned long long>(turn.hash));
        for (const Record& r : turn.records) {
            g_stats[r.func->name].calls++;
            g_stats[r.func->name].bad++;
        }
        return;
    }
    // The calls change no game memory except set_base's (FC_SETBASE); the generators and the logic
    // clock are reset before each call, as fcompare does.
    std::vector<uint8_t> image = state_capture();  // the memory image and the game-state records
    size_t map_size = static_cast<size_t>(MapAreaTiles) * sizeof(MAP);
    std::vector<uint8_t> map(reinterpret_cast<uint8_t*>(MapTiles), reinterpret_cast<uint8_t*>(MapTiles) + map_size);
    uint32_t crt = crt_rand_state(), game = game_random_state(), clock = engine_logic_clock_state();
    long bad0 = 0;
    for (auto& s : g_stats) bad0 += s.second.bad;
    const Func* prev = nullptr;
    auto check_memory = [&](const Func* f) {
        if (!f) return;
        if (!state_equals(image) || std::memcmp(map.data(), MapTiles, map_size)) {
            g_stats[f->name].bad++;
            glog(log, "golden: turn %d: %s changed game memory (restored)\n", CurrentTurn, f->name);
            state_restore(image);
            std::memcpy(MapTiles, map.data(), map_size);
        }
    };
    for (const Record& r : turn.records) {
        if (r.func != prev) check_memory(prev);
        prev = r.func;
        crt_set_rand_state(crt);
        game_reseed(game);
        engine_set_logic_clock_state(clock);
        std::string why = replay(r);
        if (r.entry->flags & FC_SETBASE) {
            state_restore(image);
            std::memcpy(MapTiles, map.data(), map_size);
        }
        Stats& st = g_stats[r.func->name];
        st.calls++;
        if (why.empty()) continue;
        st.bad++;
        if (st.logged++ < 10)
            glog(log, "golden MISMATCH T%d %s(%s): %s\n", CurrentTurn, r.func->name, r.args.c_str(), why.c_str());
    }
    check_memory(prev);
    crt_set_rand_state(crt);
    game_reseed(game);
    engine_set_logic_clock_state(clock);
    long bad = -bad0;
    for (auto& s : g_stats) bad += s.second.bad;
    glog(log, "golden: turn %d: %zu calls, %ld mismatches\n", CurrentTurn, turn.records.size(), bad);
}

int golden_finish(FILE* log) {
    long calls = 0, bad = 0;
    std::string missed;
    for (auto& t : g_turns)
        if (!t.second.replayed) missed += " " + std::to_string(t.first);
    for (auto& s : g_stats) {
        calls += s.second.calls;
        bad += s.second.bad;
        if (s.second.bad)
            std::printf("golden: %s: %ld of %ld calls differ\n", s.first.c_str(), s.second.bad, s.second.calls);
    }
    bool ok = bad == 0 && missed.empty() && calls > 0 && !g_bad_states;
    char line[256];
    std::snprintf(line, sizeof(line), "golden %s: %s: %zu functions, %ld calls, %ld mismatches%s%s\n",
                  ok ? "PASS" : "FAIL", g_scenario.c_str(), g_stats.size(), calls, bad,
                  missed.empty() ? "" : ", turns never reached:", missed.c_str());
    std::fputs(line, stdout);
    glog(log, "%s", line);
    return ok ? 0 : 1;
}
