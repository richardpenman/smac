// Music (ui module): load_music (0x4457E0) picks the faction's ambient music set and its music class;
// ambience (sound.cpp) sends the game's events (200..0xD5) to the class's methods; input_callback
// sends every key and click (vtable +0x13C). The classes (vtables 0x66B888 aset1, 0x66B9F0
// bset1, 0x66BB58 sset1, 0x66BCC0 uset2, 0x66BE28 mset1, 0x66BF90 gset1; methods 0x447340..
// 0x448280) decide which "groups" of the set play, when, and at which pitch: reconstructed from
// the disassembly with their statics (0x683BF8.., 0x74DAAC..).
//
// The groups themselves are played by soundx.dll's ambient engine on the .amb files (fx\?set?.amb:
// prgm programs, kmap key maps of fx\*.wav samples with their smpl unity notes and loops, glbl
// groups, then a standard MIDI file whose tracks are the groups' patterns). soundx.dll is not
// reconstructed; this file's sequencer is an approximation: a group (glbl entry: type, id, first and
// last track, ..., next group) plays one track of its range chosen at random, again and again for
// type 4 (until stopped or +0xB0 turns its looping off), once for type 1 (then its next group, if
// any); a track's notes play their program's sample (key zone, unity note, sustain loop, release),
// with the track's volume and pan controllers, the group's pitch (+0x114, cents) and the music
// volume (+0x40).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/prefs.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/sound.h"

namespace {

// ---- the .amb set ----
struct Zone {
    int32_t lo = 0, hi = 127;
    std::shared_ptr<const platform::Pcm> pcm;
    int32_t unity = 60;
    int64_t loop_start = -1, loop_end = -1;
};
struct Program {
    std::vector<Zone> zones;
    int32_t release_ms = 250;
};
struct Event {
    uint32_t tick;
    uint8_t status, d1, d2;
};
struct Track {
    std::vector<Event> events;
    uint32_t length = 0;
};
struct GroupDef {
    int32_t type = 4, first = 0, last = 0, count = 0, next = -1;
};
struct AmbSet {
    std::map<int32_t, Program> programs;
    std::vector<Track> tracks;  // MIDI tracks (0: the tempo track)
    std::map<int32_t, GroupDef> groups;
    int32_t division = 120;
    double us_per_beat = 500000;
};

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The unity note and loop of a sample (its WAV smpl chunk).
void read_smpl(const char* file, Zone* z) {
    uint32_t n = 0;
    uint8_t* d = win32::read_file(file, &n);
    if (!d) return;
    for (uint32_t i = 12; i + 8 <= n;) {
        uint32_t len = le32(d + i + 4);
        if (!std::memcmp(d + i, "smpl", 4) && len >= 36 && i + 8 + len <= n) {
            z->unity = (int32_t)le32(d + i + 8 + 12);
            if (le32(d + i + 8 + 28) >= 1 && len >= 60) {
                z->loop_start = le32(d + i + 8 + 36 + 8);
                z->loop_end = le32(d + i + 8 + 36 + 12);
            }
            break;
        }
        i += 8 + len + (len & 1);
    }
    win32::free_file(d);
}

bool load_amb(const char* path, AmbSet* set) {
    uint32_t n = 0;
    uint8_t* d = win32::read_file(path, &n);
    if (!d) return false;
    struct PrgmRef {
        int32_t number;
        std::string file, name;
    };
    std::vector<PrgmRef> prgms;
    std::map<std::string, Program> kmaps;
    uint32_t i = 0;
    while (i + 8 <= n && std::memcmp(d + i, "MThd", 4)) {
        uint32_t len = le32(d + i + 4);
        const uint8_t* b = d + i + 8;
        const uint8_t* e = b + len;
        if (i + 8 + len > n) break;
        if (!std::memcmp(d + i, "prgm", 4) && len >= 28) {
            PrgmRef r;
            r.number = (int32_t)le32(b);
            const char* s = (const char*)b + 28;
            r.file = std::string(s, strnlen(s, (size_t)(e - (const uint8_t*)s)));
            s += r.file.size() + 1;
            if ((const uint8_t*)s < e) r.name = std::string(s, strnlen(s, (size_t)(e - (const uint8_t*)s)));
            prgms.push_back(r);
        } else if (!std::memcmp(d + i, "kmap", 4) && len >= 16) {
            const char* s = (const char*)b + 12;
            std::string name(s, strnlen(s, (size_t)(e - (const uint8_t*)s)));
            const uint8_t* p = b + 12 + name.size() + 1;
            Program prog;
            if (p + 8 <= e) {
                uint32_t count = le32(p);
                p += 8;  // count, then a constant 12
                for (uint32_t k = 0; k < count && p + 12 <= e; k++) {
                    Zone z;
                    z.hi = (int32_t)le32(p);
                    z.lo = (int32_t)le32(p + 4);
                    p += 12;
                    std::string file((const char*)p, strnlen((const char*)p, (size_t)(e - p)));
                    p += file.size() + 1;
                    z.pcm = sound_load_file(file.c_str());
                    read_smpl(file.c_str(), &z);
                    prog.zones.push_back(z);
                }
                if (p + 4 <= e) prog.release_ms = (int32_t)le32(p);
            }
            kmaps[lower(name)] = prog;
        } else if (!std::memcmp(d + i, "glbl", 4) && len >= 16) {
            // 0x0C, the entry size (32 or 36 bytes), the count, 2; then the entries.
            uint32_t size = le32(b + 4), count = le32(b + 8);
            const uint8_t* p = b + 16;
            for (uint32_t k = 0; k < count && size >= 32 && p + size <= e; k++, p += size) {
                GroupDef g;
                g.type = (int32_t)le32(p);
                int32_t id = (int32_t)le32(p + 4);
                g.first = (int32_t)le32(p + 8);
                g.last = (int32_t)le32(p + 12);
                g.count = (int32_t)le32(p + 20);
                g.next = size >= 36 ? (int32_t)le32(p + 32) : -1;
                set->groups[id] = g;
            }
        }
        i += 8 + len;
    }
    for (const PrgmRef& r : prgms) {
        std::string f = lower(r.file);
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".wav") == 0) {
            Program prog;
            Zone z;
            z.pcm = sound_load_file(r.file.c_str());
            read_smpl(r.file.c_str(), &z);
            prog.zones.push_back(z);
            set->programs[r.number] = prog;
            continue;
        }
        std::string key = lower(r.name);
        if (!kmaps.count(key) && f.rfind("fx\\", 0) == 0) key = f.substr(3);
        if (kmaps.count(key)) set->programs[r.number] = kmaps[key];
    }
    // The MIDI file.
    if (i + 14 <= n) {
        set->division = d[i + 12] << 8 | d[i + 13];
        uint32_t ntracks = (uint32_t)(d[i + 10] << 8 | d[i + 11]);
        i += 8 + be32(d + i + 4);
        for (uint32_t t = 0; t < ntracks && i + 8 <= n; t++) {
            uint32_t len = be32(d + i + 4);
            const uint8_t* p = d + i + 8;
            const uint8_t* e = p + std::min(len, n - i - 8);
            Track tr;
            uint32_t tick = 0;
            uint8_t running = 0;
            auto vlq = [&]() {
                uint32_t v = 0;
                while (p < e) {
                    uint8_t c = *p++;
                    v = v << 7 | (c & 0x7F);
                    if (!(c & 0x80)) break;
                }
                return v;
            };
            while (p < e) {
                tick += vlq();
                if (p >= e) break;
                uint8_t c = *p;
                if (c == 0xFF) {
                    uint8_t type = p[1];
                    p += 2;
                    uint32_t l = vlq();
                    if (type == 0x51 && l == 3 && p + 3 <= e && t == 0) set->us_per_beat = p[0] << 16 | p[1] << 8 | p[2];
                    p += l;
                    continue;
                }
                if (c == 0xF0 || c == 0xF7) {
                    p++;
                    p += vlq();
                    continue;
                }
                if (c & 0x80) {
                    running = c;
                    p++;
                }
                uint8_t st = running & 0xF0;
                uint8_t d1 = p < e ? *p++ : 0, d2 = 0;
                if (st != 0xC0 && st != 0xD0) d2 = p < e ? *p++ : 0;
                tr.events.push_back({tick, running, d1, d2});
            }
            tr.length = tick;
            set->tracks.push_back(std::move(tr));
            i += 8 + len;
        }
    }
    win32::free_file(d);
    return !set->tracks.empty();
}

// ---- the sequencer (state shared with the audio thread: audio_lock) ----
struct Note {
    const Zone* zone;
    double pos, step;
    float gain, pan;
    bool held = true;
    float fade = 1.0f, fade_step = 0.0f;
    int32_t release_ms = 250;
    int32_t player, key;
};
struct Player {
    int32_t id, group, track;
    double tick = 0;
    size_t next = 0;
    int32_t program = 0;
    float volume = 1.0f, pan = 0.0f;
    bool loop = false;
    int32_t repeats = 0;
};

class Sequencer {
public:
    std::unique_ptr<AmbSet> set;
    std::vector<Player> players;
    std::vector<Note> notes;
    std::map<int32_t, int32_t> pitch;  // group -> cents
    std::map<int32_t, bool> looping;    // group -> +0xB0 flag
    float volume = 63.0f / 127.0f;
    bool running = false;
    int32_t stream = 0, next_id = 1, rate = 44100;
    uint32_t rnd = 0x1234567;

    int32_t random(int32_t n) {
        rnd ^= rnd << 13;
        rnd ^= rnd >> 17;
        rnd ^= rnd << 5;
        return n > 0 ? (int32_t)(rnd % (uint32_t)n) : 0;
    }

    // Starts group id (lock held).
    void play(int32_t id) {
        auto it = set->groups.find(id);
        if (it == set->groups.end()) return;
        const GroupDef& g = it->second;
        int32_t lo = std::max(1, g.first), hi = std::min((int32_t)set->tracks.size() - 1, g.last);
        if (hi < lo) return;
        stop(id);
        Player p;
        p.id = next_id++;
        p.group = id;
        p.track = lo + random(hi - lo + 1);
        p.loop = g.type == 4 && (!looping.count(id) || looping[id]);
        p.repeats = g.type == 1 ? std::max(0, g.count - 1) : 0;
        players.push_back(p);
    }
    void stop(int32_t id) {
        for (auto& p : players)
            if (p.group == id) release_notes(p.id);
        players.erase(std::remove_if(players.begin(), players.end(), [&](const Player& p) { return p.group == id; }),
                      players.end());
    }
    bool playing(int32_t id) const {
        for (auto& p : players)
            if (p.group == id) return true;
        return false;
    }
    void release_notes(int32_t player, int32_t key = -1) {
        for (Note& n : notes)
            if (n.player == player && n.held && (key < 0 || n.key == key)) {
                n.held = false;
                n.fade_step = 1000.0f / ((float)std::max(10, n.release_ms) * (float)rate);
            }
    }

    void note_on(Player& p, int32_t key, int32_t vel, int32_t rate) {
        auto it = set->programs.find(p.program);
        if (it == set->programs.end()) return;
        const Program& prog = it->second;
        for (const Zone& z : prog.zones) {
            if (key < z.lo || key > z.hi || !z.pcm || z.pcm->samples.empty()) continue;
            Note n;
            n.zone = &z;
            n.pos = 0;
            int32_t cents = pitch.count(p.group) ? pitch[p.group] : 0;
            n.step = (double)z.pcm->rate / rate * std::pow(2.0, (key - z.unity) / 12.0 + cents / 1200.0);
            n.gain = (float)vel / 127.0f * p.volume;
            n.pan = p.pan;
            n.player = p.id;
            n.key = key;
            n.fade_step = 0;
            n.release_ms = prog.release_ms;
            notes.push_back(n);
            return;
        }
    }

    void advance(Player& p, double ticks, int32_t rate) {
        const Track& t = set->tracks[(size_t)p.track];
        double end = p.tick + ticks;
        for (;;) {
            while (p.next < t.events.size() && t.events[p.next].tick <= end) {
                const Event& e = t.events[p.next++];
                uint8_t st = e.status & 0xF0;
                if (st == 0x90 && e.d2) note_on(p, e.d1, e.d2, rate);
                else if (st == 0x80 || (st == 0x90 && !e.d2)) release_notes(p.id, e.d1);
                else if (st == 0xC0) p.program = e.d1;
                else if (st == 0xB0 && e.d1 == 7) p.volume = e.d2 / 127.0f;
                else if (st == 0xB0 && e.d1 == 10) p.pan = (e.d2 - 64) / 64.0f;
            }
            if (end < (double)t.length || p.next < t.events.size()) break;
            // The track ended.
            release_notes(p.id);
            end -= std::max<double>(1.0, t.length);
            if (p.loop || p.repeats > 0) {
                if (!p.loop) p.repeats--;
                auto it = set->groups.find(p.group);
                if (it != set->groups.end()) {
                    int32_t lo = std::max(1, it->second.first), hi = std::min((int32_t)set->tracks.size() - 1, it->second.last);
                    if (hi >= lo) p.track = lo + random(hi - lo + 1);
                }
                p.next = 0;
                p.program = 0;
                continue;
            }
            p.track = -1;  // finished
            break;
        }
        p.tick = end;
    }

    bool mix(float* out, int32_t frames, int32_t rate) {
        if (!set || !running) return true;
        this->rate = rate;
        const int32_t kBlock = 64;
        for (int32_t f0 = 0; f0 < frames; f0 += kBlock) {
            int32_t nf = std::min(kBlock, frames - f0);
            double ticks = set->division * 1e6 / set->us_per_beat * nf / rate;
            std::vector<int32_t> chain;
            for (Player& p : players) {
                advance(p, ticks, rate);
                if (p.track < 0) {
                    auto it = set->groups.find(p.group);
                    if (it != set->groups.end() && it->second.type == 1 && it->second.next >= 0) chain.push_back(it->second.next);
                }
            }
            players.erase(std::remove_if(players.begin(), players.end(), [](const Player& p) { return p.track < 0; }),
                          players.end());
            for (int32_t g : chain) play(g);
            for (Note& n : notes) {
                const platform::Pcm& s = *n.zone->pcm;
                int64_t len = (int64_t)s.samples.size() / s.channels;
                bool loops = n.held && n.zone->loop_end > n.zone->loop_start && n.zone->loop_start >= 0 &&
                             n.zone->loop_end <= len;
                float gl = n.gain * volume * (n.pan > 0 ? 1 - n.pan : 1), gr = n.gain * volume * (n.pan < 0 ? 1 + n.pan : 1);
                for (int32_t i = 0; i < nf; i++) {
                    if (loops && n.pos >= (double)n.zone->loop_end)
                        n.pos -= (double)(n.zone->loop_end - n.zone->loop_start);
                    int64_t k = (int64_t)n.pos;
                    if (k + 1 >= len) {
                        n.fade = 0;
                        break;
                    }
                    float t = (float)(n.pos - (double)k);
                    float l, r;
                    if (s.channels == 1) {
                        l = r = (s.samples[(size_t)k] * (1 - t) + s.samples[(size_t)k + 1] * t) / 32768.0f;
                    } else {
                        l = (s.samples[(size_t)k * 2] * (1 - t) + s.samples[(size_t)k * 2 + 2] * t) / 32768.0f;
                        r = (s.samples[(size_t)k * 2 + 1] * (1 - t) + s.samples[(size_t)k * 2 + 3] * t) / 32768.0f;
                    }
                    out[2 * (f0 + i)] += l * gl * n.fade;
                    out[2 * (f0 + i) + 1] += r * gr * n.fade;
                    n.pos += n.step;
                    if (!n.held) {
                        n.fade -= n.fade_step;
                        if (n.fade <= 0) break;
                    }
                }
            }
            notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Note& n) { return n.fade <= 0; }), notes.end());
        }
        return true;
    }
};

bool seq_callback(void* user, float* out, int32_t frames, int32_t rate) {
    return static_cast<Sequencer*>(user)->mix(out, frames, rate);
}

// ---- the music classes ----
struct Lock {
    Lock() { platform::audio_lock(); }
    ~Lock() { platform::audio_unlock(); }
};

// The original's statics of the classes (initial values from the image).
struct Statics {
    int32_t c0074daac = 0, c0074dab0 = 0, c00683bf8 = 5, c00683bfc = 1, c0074d8ec = 0;
    bool c0074c5e8 = false;
    int32_t c00683c00 = 2, c00683c04 = 3, c00683c08 = 10, c00683c0c = 5;
    int32_t c0074dab4 = 0, c0074dab8 = 0, c0074dabc = 0, c0074dac0 = 0, c0074dac4 = 0;
    int32_t c00683c14 = 17, c0074dac8 = 0, c0074dacc = 0, c0074dad0 = 0;
    int32_t c00683c18 = 3, c0074dad8 = 0, c0074dadc = 0;
    bool c0074dad4 = false, c00683c1c = true;
    int32_t c00683c20 = 24, c00683c24 = 7, c00683c28 = 1;
    int32_t c00683c30 = 6, c00683c34 = 1, c00683c38 = 3, c00683c3c = -600, c00683c40 = 200, c00683c44 = 90;
    int32_t c00683c48 = 18, c00683c4c = 21, c00683c50 = 24, c0074dae8 = 0;
    bool c0074daf0 = false;
} S;

}  // namespace

struct MusicObj {
    Sequencer seq;
    int32_t f58 = 0, f68 = 0, f70 = 0, f74 = 0, f78 = 0, f7c = 0;
    bool f6c = false, f6d = false;
    virtual ~MusicObj() {
        if (seq.stream) platform::audio_stream_close(seq.stream);
    }
    // The engine (soundx's ambient object through the base class 0x4C7xxx).
    bool load(const char* file) {  // +0x10
        auto set = std::make_unique<AmbSet>();
        if (!load_amb(file, set.get())) return false;
        Lock l;
        seq.set = std::move(set);
        return true;
    }
    void base_play() {  // +0x1C
        if (!seq.set) return;
        if (!seq.stream) seq.stream = platform::audio_stream_open(seq_callback, &seq);
        Lock l;
        seq.running = true;
    }
    void play(int32_t g) {  // +0x10C
        Lock l;
        if (seq.set) seq.play(g);
    }
    void stop(int32_t g) {  // +0x110
        Lock l;
        if (seq.set) seq.stop(g);
    }
    void pitch(int32_t g, int32_t cents) {  // +0x114
        Lock l;
        seq.pitch[g] = cents;
    }
    bool playing(int32_t g) {  // +0x118
        Lock l;
        return seq.set && seq.playing(g);
    }
    void set_loop(int32_t g, bool on) {  // +0xB0
        Lock l;
        seq.looping[g] = on;
        for (Player& p : seq.players)
            if (p.group == g) p.loop = on && seq.set->groups[g].type == 4;
    }
    void set_volume(int32_t v) {  // +0x40
        Lock l;
        seq.volume = (float)std::max(0, std::min(127, v)) / 127.0f;
    }
    // The class's methods (vtable +0x12C..+0x164).
    virtual void start() = 0;
    virtual void m130() {}
    virtual void m134() {}
    virtual void m138() {}
    virtual void m13c() {}
    virtual void m140() {}
    virtual void m144() {}
    virtual void m148() {}
    virtual void m14c() { f6c = true; }   // 0x447270
    virtual void m150() { f6c = false; }  // 0x447280
    virtual void m154() {}
    virtual void m158() {}
    virtual void m15c() { f6d = true; }   // 0x447290
    virtual void m160() { f6d = false; }  // 0x4472A0
    virtual void m164() {}
};

namespace {

// aset1 (0x66B888): the default set.
struct MusicA : MusicObj {
    void start() override {  // 0x447DB0
        base_play();
        play(3);
    }
    void m130() override { f58++; }  // 0x448280
    void m13c() override {           // 0x447DD0
        S.c00683c30++;
        if (S.c00683c38 == 1) {
            if (S.c00683c30 % 7 != 0) {
                phase_end();
                return;
            }
            for (int32_t g : {0xE, 0xF, 0x10, 0xD}) pitch(g, S.c00683c3c);
            S.c00683c3c += S.c00683c40;
            play(0xE);
            play(2);
            play(1);
            if (S.c00683c3c >= 300 || S.c00683c3c < -900) S.c00683c40 = -S.c00683c40;
            if (S.c00683c30 > 10) play(0xF);
            if (S.c00683c30 > 20) play(0x10);
            if (S.c00683c30 > 25) play(0xD);
        }
        if (S.c00683c38 == 2) {
            play(0x1B);
            S.c00683c3c += S.c00683c40;
            if (S.c00683c3c >= 100 || S.c00683c3c < -600) S.c00683c40 = -S.c00683c40;
            S.c00683c44 += 10;
            S.c0074daf0 = !S.c0074daf0;
            play(S.c0074daf0 ? S.c00683c4c : S.c00683c48);
            if (S.c00683c50 != 0x19) play(S.c00683c50);
            if (++S.c00683c48 > 20) S.c00683c48 = 18;
            if (++S.c00683c4c > 23) S.c00683c4c = 21;
            if (++S.c00683c50 > 26) S.c00683c50 = 24;
        }
        if (S.c00683c38 == 3) {
            S.c0074dae8 += S.c00683c40;
            if (S.c0074dae8 >= 100 || S.c0074dae8 < -600) S.c00683c40 = -S.c00683c40;
            for (int32_t g : {8, 3, 8, 6, 5, 2, 4}) pitch(g, S.c0074dae8);
            if (S.c00683c30 == 5 || S.c00683c30 == 15) play(5);
            if (S.c00683c34 > 5) S.c00683c34 = 1;
            else S.c00683c34++;
            int32_t g = (int32_t)(platform::ticks_ms() % (uint32_t)S.c00683c34);
            if (g == 3 || (g == 8 && S.c0074dae8 >= 0)) g++;
            if (!playing(g)) play(g);
        }
        phase_end();
    }
    void phase_end() {
        if (S.c00683c30 < 40) return;
        if (S.c00683c38 == 1) {
            stop(0xD);
            stop(0xF);
            stop(0x10);
            S.c00683c38 = 2;
            set_loop(0xE, false);
            S.c00683c30 = 0;
            return;
        }
        if (S.c00683c38 == 2) {
            for (int32_t g = 0x12; g <= 0x1B; g++) stop(g);
            set_loop(0xE, true);
            play(3);
            S.c00683c38 = 3;
            S.c00683c30 = 0;
            return;
        }
        if (S.c00683c38 == 3) {
            for (int32_t g = 1; g <= 8; g++) stop(g);
            S.c00683c38 = 1;
        }
        S.c00683c30 = 0;
    }
};

// bset1 (0x66B9F0): the Believers' set (+0x70 = 1, +0x74 = 0, +0x78 = 0x14, +0x7C = 0x0F).
struct MusicB : MusicObj {
    MusicB() {
        f70 = 1;
        f74 = 0;
        f78 = 0x14;
        f7c = 0xF;
    }
    void start() override {  // 0x447920
        play(0x1A);
        base_play();
    }
    void m13c() override {  // 0x447940
        f74 += 500;
        if (f74 > 500) f74 = -1000;
        S.c00683c18++;
        S.c0074dad8++;
        S.c0074dadc++;
        if (S.c0074dad4) {
            if (playing(4)) return;
            S.c0074dad4 = false;
        }
        if (f6d) {
            play(f78++);
            if (f78 > 0x16) f78 = 0x14;
            if (S.c00683c18 > 2) {
                play(f7c);
                S.c00683c18 = 0;
            }
            f7c += 2;
            if (f7c == 0x15 || f7c == 0x16) f7c = 0x10;
        }
        if (S.c00683c18 > 2) {
            int32_t g = 0x19 - (S.c00683c1c ? 1 : 0);
            pitch(g, f74);
            if (f70 > 1) {
                pitch(0xB, f74);
                play(0xB);
            }
            play(g);
            S.c00683c1c = !S.c00683c1c;
        }
        if (S.c0074dad8 > 4) {
            pitch(0xD, f74);
            play(0xD);
            S.c0074dad8 = 0;
        }
    }
    void m148() override {  // 0x447AF0
        if (S.c0074dad4) return;
        S.c0074dad4 = true;
        pitch(0xF, f74);
        play(0xF);
        play(0x1B);
    }
    void m154() override {  // 0x447B30
        f70++;
        pitch(0xD, f74);
        play(0xD);
    }
    void m160() override {  // 0x447AC0
        f6d = false;
        stop(0x14);
        stop(0x15);
        stop(0x16);
    }
    void m164() override {  // 0x447B60
        stop(0x1B);
        if (!playing(100)) play(100);
    }
};

// sset1 (0x66BB58): the Spartans' set.
struct MusicS : MusicObj {
    void start() override {  // 0x4476C0
        base_play();
        play(0x11);
    }
    void m130() override { f58++; }    // 0x4476E0
    void m138() override { play(0xF); }  // 0x447840
    void m13c() override {             // 0x4476F0
        if (S.c00683c14 > 0x12) S.c00683c14 = 0x11;
        bool due = S.c0074dacc > 1;
        S.c0074dacc++;
        if (due) {
            pitch(S.c00683c14, S.c0074dac8);
            play(S.c00683c14);
            S.c0074dacc = 0;
        }
        due = S.c0074dad0 > 7;
        S.c0074dad0++;
        if (due) {
            pitch(0x10, S.c0074dac8);
            play(0x10);
            S.c0074dad0 = 0;
        }
        S.c0074dac8 += 100;
        if (S.c0074dac8 > 400) S.c0074dac8 = -300;
        S.c00683c14++;
    }
    void m148() override {  // 0x4477B0
        if (playing(100)) return;
        if (f70 == 0) f70 = 100;
        if (playing(f70)) return;
        if (++f70 > 0x67) f70 = 0x65;
        pitch(f70, f74);
        play(f70);
        f74 += 100;
        if (f74 > 300) f74 = -300;
    }
    void m164() override {  // 0x447880
        stop(0x65);
        stop(0x66);
        stop(0x67);
        if (!playing(100)) play(100);
    }
};

// uset2 (0x66BCC0): the University's set.
struct MusicU : MusicObj {
    void start() override {  // 0x447510
        base_play();
        play(0);
    }
    void m13c() override {  // 0x447570
        if (f6c) return;
        S.c0074dac4++;
        S.c0074dab4++;
        S.c0074dab8++;
        S.c0074dac0++;
        if (S.c0074dac4 == 0xF) {
            S.c00683c0c = (S.c00683c0c == 4) + 4;
            play(S.c00683c0c);
            S.c0074dac4 = 0;
        }
        if (S.c0074dab4 == 9) {
            play(0);
            S.c0074dab4 = 0;
        }
        if (S.c0074dab8 == 6) {
            S.c00683c00 = (S.c00683c00 != 2) + 1;
            pitch(S.c00683c00, S.c0074dabc);
            S.c0074dabc += 100;
            if (S.c00683c00 > 600) S.c00683c00 = -600;  // (sic: the original tests the group)
            play(S.c00683c00);
            S.c0074dab8 = 0;
        }
        if (S.c0074dac0 == S.c00683c08) {
            play(S.c00683c04);
            S.c0074dac0 = 0;
        }
    }
    void m14c() override {  // 0x447530
        if (f6c) return;
        play(6);
        f6c = true;
    }
    void m150() override {  // 0x447550
        stop(6);
        f6c = false;
    }
    void m15c() override {}
    void m160() override {}
};

// mset1 (0x66BE28): the Hive's and Morgan's set.
struct MusicM : MusicObj {
    void start() override {  // 0x447340
        play(1);
        base_play();
    }
    void m130() override {  // 0x447470
        if (!playing(100)) play(0x69);
    }
    void m138() override {  // 0x4474B0
        if (playing(100)) return;
        pitch(4, f70);
        play(4);
    }
    void m13c() override {  // 0x447360
        if (!S.c0074c5e8) {
            S.c0074c5e8 = true;
            S.c0074d8ec = (int32_t)platform::ticks_ms();
        }
        if (playing(100)) return;
        uint32_t now = platform::ticks_ms();
        S.c0074daac++;
        if (now - (uint32_t)S.c0074d8ec > 4999 && S.c0074daac > 4) {
            S.c0074daac = 0;
            pitch(S.c00683bf8, S.c0074dab0);
            play(S.c00683bf8++);
            if (S.c00683bf8 > 7) S.c00683bf8 = 5;
            S.c0074dab0 += 0x96;
            if (S.c0074dab0 > 600) S.c0074dab0 = -600;
            S.c0074d8ec = (int32_t)now;
            f70 += 100;
            if (f70 > 500) f70 = -700;
            pitch(S.c00683bfc, f70);
        }
    }
    void m154() override {  // 0x4474E0
        stop(4);
        stop(0x69);
        play(100);
    }
    void m158() override {  // 0x447490
        if (!playing(100)) play(4);
    }
};

// gset1 (0x66BF90): the Gaians' and Peacekeepers' set.
struct MusicG : MusicObj {
    void start() override {  // 0x447BC0
        base_play();
        f68 = 0x18;
        play(0x18);
    }
    void m134() override {  // 0x447C70
        play(S.c00683c24);
        if (++S.c00683c24 > 9) S.c00683c24 = 7;
    }
    void m13c() override {  // 0x447C00
        pitch(f68, f70);
        if (S.c00683c20 == 0x18) {
            f70 += 300;
            if (f70 > 600) f70 = -600;
        } else {
            f70 += 700;
            if (f70 > 0x4B0) f70 = -700;
        }
    }
    void m14c() override {  // 0x447BE0
        if (!f6c) f6c = true;
    }
    void m154() override {  // 0x447CD0
        S.c00683c28 = 1;
        stop(f68);
        f68 = 0x19;
        f70 = 0;
        play(0x19);
    }
    void m15c() override {}
    void m160() override {}
};

MusicObj* g_music = nullptr;  // 0x74DAA0

}  // namespace

MusicObj* music_object() { return g_music; }
bool music_loaded() { return g_music != nullptr; }

void music_release() {
    delete g_music;  // +8(1)
    g_music = nullptr;
}

bool music_render(const char* path, int32_t seconds) {
    MusicObj* m = g_music;
    if (!m || !m->seq.set) return false;
    const int32_t rate = 22050;
    std::vector<float> buf((size_t)rate * 2 * (size_t)seconds, 0.0f);
    platform::audio_lock();
    bool was = m->seq.running;
    m->seq.running = true;
    for (int32_t f = 0; f < rate * seconds; f += 1024)
        m->seq.mix(buf.data() + (size_t)f * 2, std::min(1024, rate * seconds - f), rate);
    m->seq.running = was;
    platform::audio_unlock();
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    uint32_t data = (uint32_t)buf.size() * 2;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + data);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(rate);
    u32(rate * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(data);
    double sum = 0;
    float peak = 0;
    for (float v : buf) {
        float c = std::max(-1.0f, std::min(1.0f, v));
        int16_t s16 = (int16_t)std::lrint(c * 32767.0f);
        std::fwrite(&s16, 2, 1, f);
        sum += (double)c * c;
        peak = std::max(peak, std::fabs(c));
    }
    std::fclose(f);
    std::fprintf(stderr, "musicrender %s: %d s, rms %.4f, peak %.3f\n", path, seconds, std::sqrt(sum / buf.size()), peak);
    return true;
}

void music_set_volume(int32_t volume) {
    if (g_music) g_music->set_volume(volume);
}

void music_call(int32_t off) {
    MusicObj* m = g_music;
    if (!m) return;
    switch (off) {
    case 0x12C: m->start(); break;
    case 0x130: m->m130(); break;
    case 0x134: m->m134(); break;
    case 0x138: m->m138(); break;
    case 0x13C: m->m13c(); break;
    case 0x140: m->m140(); break;
    case 0x144: m->m144(); break;
    case 0x148: m->m148(); break;
    case 0x14C: m->m14c(); break;
    case 0x150: m->m150(); break;
    case 0x154: m->m154(); break;
    case 0x158: m->m158(); break;
    case 0x15C: m->m15c(); break;
    case 0x160: m->m160(); break;
    case 0x164: m->m164(); break;
    default: break;
    }
}

void GAMEAPI load_music(int32_t faction_id) {
    music_release();
    const char* key = MFactions[faction_id].search_key;
    const char* set;
    MusicObj* m;
    if (!crt_stricmp(key, "gaians")) set = "gset1.amb", m = new MusicG();
    else if (!crt_stricmp(key, "hive")) set = "mset1.amb", m = new MusicM();
    else if (!crt_stricmp(key, "univ")) set = "uset2.amb", m = new MusicU();
    else if (!crt_stricmp(key, "morgan")) set = "mset1.amb", m = new MusicM();
    else if (!crt_stricmp(key, "spartans")) set = "sset1.amb", m = new MusicS();
    else if (!crt_stricmp(key, "believers")) set = "bset1.amb", m = new MusicB();
    else if (!crt_stricmp(key, "peace")) set = "gset1.amb", m = new MusicG();
    else set = "aset1.amb", m = new MusicA();
    sound_log("load_music %d %s %s", faction_id, key, set);
    g_music = m;
    if ((GamePreferences & 0x800) && platform::audio_enabled()) {
        std::string path = std::string("fx\\") + set;
        if (m->load(path.c_str())) {
            m->set_volume(127);
            m->start();
        }
    }
}

// desktop_init (0x58EE60): the game's windows are set up natively by smacx; its music part.
// PrefWin_init (called by desktop_init) then sets the volumes of the audio page's sliders from the
// preferences (PrefWin_on_scrolling: master, voices, effects, music).
int32_t GAMEAPI desktop_init(int32_t) {
    load_music(CurrentPlayerFaction);
    sound_apply_volume_prefs();
    return 0;
}
