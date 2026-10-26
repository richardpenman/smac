// Sound effects and voices (ui module): the FX object Sounds (FX_init 0x445CD0, FX_play 0x446A00,
// FX_play_loc 0x446960, FX_stop, FX_fade, FX_engine_pitch), the CPU voices (wave_it 0x4455F0,
// popup_wave_callback 0x4456B0), ambience (0x446B40) and ambience_veh (0x446D10), the interface
// callbacks (ok_callback, passover_callback, input_callback), the Wave objects (0x4C66E0..) and
// the sound groups of the sound system (0x90D978, the wrapper of soundx.dll), release_sound.
//
// Reconstructed from the disassembly: FX_init's table of files, flags and groups (slot = offset /
// 0x6C; the slots FX_init does not load stay silent, as in the original: FX_play(6), (0x0F) ...
// play nothing), the preference bits (GamePreferences 0x400 sound effects, 0x800 music and unit
// ambience), FX_play's psi-attack pitch steps and music ducking, wave_it's "one voice at a time",
// popup_wave_callback's substitutions (wave 0x19 -> 0x25 for a secret project, 0x2B one time in
// three, 0x10 adds FX 0x38), ambience's one current ambient effect and ambience_veh's choice of
// a unit's sound, the group volumes and switches of the audio preferences.
// soundx.dll itself (DirectSound) is replaced by the platform mixer (platform/ui_platform.h):
// approximated are its volume curve (linear gain = volume / 127 x group / 127 x master / 127), the
// pitch (cents: 2^(pitch / 1200)) and the pan (-64..63).
#include "ui/sound.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/map.h"
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/prefs.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/mp3.h"
#include "ui/win.h"

namespace {

bool g_log = false;

struct WaveState {
    std::string file;       // +0x4C
    uint32_t flags = 0;     // +0x54: 1 static, 2 created, 4 load on play, 8 3D, 0x10 fire and forget
    bool loop = false;      // Wave_init flag 2 (+0x48(1))
    int32_t group = 16;     // +0x68 (16: none)
    int32_t volume = 127;   // +4
    int32_t pan = 0;        // +8
    int32_t pitch = 0;      // +0x58
    int32_t fade_ms = 1000; // +0x38
    std::shared_ptr<const platform::Pcm> pcm;  // the soundx handle (+0x3C)
    int32_t voice = 0;
    uint32_t start = 0, length = 0;  // +0x64, +0x60
};
std::map<const uint8_t*, WaveState> g_waves;

struct Group {
    bool on = true;        // +0x24
    int32_t volume = 127;  // +0x28
};
Group g_groups[16];
int32_t g_master = 127;
bool g_master_on = true;

// The original's statics.
int32_t g_ambience = -1;       // 0x683BF0
int32_t g_ambience_veh = -1;   // 0x683BF4
int32_t g_engine_slot = -1;    // Sounds +0x28EC
int32_t g_voice_id = 0;        // 0x74DAA4
int32_t g_ok_pitch = 0;        // 0x74DAA8

WaveState& W(const uint8_t* wave) { return g_waves[wave]; }
uint8_t* fx(uint8_t* sounds, int32_t id) { return sounds + id * 0x6C; }
uint8_t* cpu_voice(int32_t id) { return mem::host(0x74C5F0) + id * 0x6C; }

platform::VoiceParams params(const WaveState& w) {
    platform::VoiceParams p;
    float g = (float)w.volume / 127.0f;
    if (w.group < 16) g *= (float)g_groups[w.group].volume / 127.0f;
    g *= g_master_on ? (float)g_master / 127.0f : 0.0f;
    p.volume = g;
    p.pan = (float)w.pan / 64.0f;
    p.pitch = std::pow(2.0f, (float)w.pitch / 1200.0f);
    p.loop = w.loop;
    return p;
}

bool playing(const uint8_t* wave) {
    const WaveState& w = W(wave);
    if (platform::audio_playing(w.voice)) return true;
    return false;
}

// +0x8C: loads the file (creates the soundx buffer).
void load(WaveState& w) {
    if (!w.pcm && !w.file.empty()) w.pcm = sound_load_file(w.file.c_str());
    if (w.pcm) w.length = w.pcm->ms();
}

// +0x14: releases the buffer (DirectSound stops a released buffer).
void release(WaveState& w) {
    platform::audio_stop(w.voice);
    w.voice = 0;
    w.pcm.reset();
}

void fade(WaveState& w, int32_t ms) {
    platform::audio_stop(w.voice, ms);
}

// Finds a game file (fx\NAME.wav) case-insensitively in the game directory.
bool file_ok(const std::string& name) { return win32::file_exists(name.c_str()); }

// FX_init's loader (FUN_00445BB0): fx\NAME (.wav added without an extension), else NAME.
void fx_wave(uint8_t* wave, const char* name, uint32_t flags) {
    if (!(flags & 0x81)) flags |= 4;
    std::string path = std::string("fx\\") + name;
    if (!std::strchr(path.c_str(), '.')) path += ".wav";
    if (!file_ok(path)) {
        path = name;
        if (!std::strchr(name, '.')) path += ".wav";
    }
    Wave_init(wave, path.c_str(), flags);
    if (!(flags & 0x84)) load(W(wave));
}

uint32_t read_le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

std::shared_ptr<const platform::Pcm> decode_wav(const uint8_t* d, uint32_t n) {
    if (n < 12 || std::memcmp(d, "RIFF", 4) || std::memcmp(d + 8, "WAVE", 4)) return nullptr;
    auto pcm = std::make_shared<platform::Pcm>();
    int32_t bits = 16, format = 0;
    for (uint32_t i = 12; i + 8 <= n;) {
        uint32_t len = read_le32(d + i + 4);
        const uint8_t* body = d + i + 8;
        if (i + 8 + len > n) len = n - i - 8;
        if (!std::memcmp(d + i, "fmt ", 4) && len >= 16) {
            format = body[0] | body[1] << 8;
            pcm->channels = body[2] | body[3] << 8;
            pcm->rate = (int32_t)read_le32(body + 4);
            bits = body[14] | body[15] << 8;
        } else if (!std::memcmp(d + i, "data", 4)) {
            if (format != 1 || (bits != 8 && bits != 16) || pcm->channels < 1) return nullptr;
            if (bits == 16) {
                pcm->samples.resize(len / 2);
                for (uint32_t k = 0; k < len / 2; k++) pcm->samples[k] = (int16_t)(body[2 * k] | body[2 * k + 1] << 8);
            } else {
                pcm->samples.resize(len);
                for (uint32_t k = 0; k < len; k++) pcm->samples[k] = (int16_t)((body[k] - 128) << 8);
            }
            return pcm;
        }
        i += 8 + len + (len & 1);
    }
    return nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Setup and logging

void sound_startup(bool enabled) {
    const char* e = std::getenv("SMACX_SOUNDLOG");
    g_log = e && *e && *e != '0';
    platform::audio_init(enabled);
}

void sound_shutdown() {
    music_release();
    for (auto& w : g_waves) release(w.second);
    platform::audio_shutdown();
}

bool sound_log_on() { return g_log; }

void sound_log(const char* fmt, ...) {
    if (!g_log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "sound: T%d %s\n", CurrentTurn, buf);
}

std::shared_ptr<const platform::Pcm> sound_load_file(const char* name) {
    static std::map<std::string, std::shared_ptr<const platform::Pcm>> cache;
    if (!platform::audio_enabled() || !name || !*name) return nullptr;
    char path[1024];
    platform::resolve_path(name, path, sizeof(path));
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    uint32_t size = 0;
    uint8_t* data = win32::read_file(name, &size);
    std::shared_ptr<const platform::Pcm> pcm;
    if (data) {
        pcm = decode_wav(data, size);
        if (!pcm) pcm = mp3_decode(data, size);
        win32::free_file(data);
    }
    if (!pcm) std::fprintf(stderr, "sound: cannot play %s\n", name);
    cache[path] = pcm;
    return pcm;
}

// ---------------------------------------------------------------------------------------------
// Wave objects

void THISCALL Wave_init(uint8_t* wave, const char* file, uint32_t flags) {
    WaveState& w = W(wave);
    uint32_t deferred = flags & 4;
    if (deferred && (flags & 0x10)) return;
    if (deferred && (flags & 0x80)) return;
    if (!file || !win32::file_exists(file)) return;
    w.file = file;
    w.flags = 0;
    if (deferred) w.flags |= 2;
    if (flags & 1) w.flags |= 1;
    if ((flags & 0x10) && !deferred) w.flags |= 4;
    if (flags & 2) w.loop = true;
    if (flags & 0x40) w.flags |= 8;
    if (flags & 0x80) w.flags |= 0x10;
    if (flags & 0x100) w.flags |= 0x20;
}

int32_t THISCALL Wave_load_3(uint8_t* wave, const char* file, uint32_t flags) {
    Wave_init(wave, file, flags);
    if (flags & 4) return 0;
    load(W(wave));
    return 0;
}

int32_t THISCALL Wave_load_2(uint8_t* wave) {
    WaveState& w = W(wave);
    if (w.file.empty()) return 8;
    load(w);
    return w.pcm ? 0 : 1;
}

int32_t THISCALL Wave_load(uint8_t* wave, const char* file) {
    WaveState& w = W(wave);
    sound_log("Wave_load %s", file ? file : "");
    w.file = file ? file : "";
    w.pcm.reset();
    load(w);
    return w.pcm ? 0 : 1;
}

int32_t THISCALL Wave_unload(uint8_t* wave) {
    release(W(wave));
    return 0;
}

int32_t THISCALL Wave_play_2(uint8_t* wave) {
    WaveState& w = W(wave);
    if (g_log) {
        size_t p = w.file.find_last_of("\\/");
        sound_log("Wave_play_2 %s", w.file.empty() ? "-" : w.file.c_str() + (p == std::string::npos ? 0 : p + 1));
    }
    if (w.group < 16 && !g_groups[w.group].on) return 0x14;
    if (!w.pcm) {
        if (!(w.flags & 0x10)) return 0x14;
        load(w);
        if (!w.pcm) return 0x14;
    }
    platform::audio_stop(w.voice);
    w.voice = platform::audio_play(w.pcm, params(w));
    w.start = platform::ticks_ms();
    return 0;
}

int32_t THISCALL Wave_is_playing(uint8_t* wave) { return playing(wave) ? 1 : 0; }

void Wave_stop(uint8_t* wave) {
    WaveState& w = W(wave);
    platform::audio_stop(w.voice);
    w.voice = 0;
}

void Sound_fade(uint8_t* wave) {
    WaveState& w = W(wave);
    fade(w, w.fade_ms);
}

void THISCALL Sound_fade_2(uint8_t* wave, int32_t ms) {
    WaveState& w = W(wave);
    w.fade_ms = ms;
    fade(w, ms);
}

void THISCALL Wave_set_volume(uint8_t* wave, uint32_t volume) {
    WaveState& w = W(wave);
    w.volume = (int32_t)(volume & 0x7F);
    platform::audio_set(w.voice, params(w));
}

int32_t THISCALL Wave_set_pitch(uint8_t* wave, int32_t pitch) {
    WaveState& w = W(wave);
    w.pitch = std::max(-1200, std::min(1200, pitch));
    platform::audio_set(w.voice, params(w));
    return 0;
}

void THISCALL Wave_set_pan(uint8_t* wave, int32_t pan) {
    WaveState& w = W(wave);
    w.pan = std::max(-64, std::min(63, pan));
    platform::audio_set(w.voice, params(w));
}

int32_t THISCALL SoundSys_add_to_group(uint8_t*, uint32_t group, uint8_t* wave) {
    if (group >= 16 || !wave) return 10;
    W(wave).group = (int32_t)group;
    return 0;
}

void sound_set_master_volume(int32_t volume) {
    g_master = std::max(0, std::min(127, volume));
    for (auto& w : g_waves) platform::audio_set(w.second.voice, params(w.second));
}

void sound_set_group_volume(int32_t group, int32_t volume) {
    if (group < 0 || group >= 16 || volume < 0 || volume > 127) return;
    g_groups[group].volume = volume;
    for (auto& w : g_waves)
        if (w.second.group == group) platform::audio_set(w.second.voice, params(w.second));
}

int32_t sound_group_volume(int32_t group) { return group >= 0 && group < 16 ? g_groups[group].volume : 127; }
int32_t sound_master_volume() { return g_master; }

void sound_apply_volume_prefs() {
    sound_set_master_volume(prefs_get_2("Main Volume", 0x7F, 0));
    sound_set_group_volume(0, prefs_get_2("Voice Volume", 0x55, 0));
    sound_set_group_volume(1, prefs_get_2("SFX Volume", 0x5F, 0));
    if (music_loaded()) music_set_volume(prefs_get_2("Music Volume", 0x3F, 0));
}

void sound_enable_group(int32_t group, bool on) {
    if (group < 0 || group >= 16 || g_groups[group].on == on) return;
    for (auto& w : g_waves) {
        if (w.second.group != group) continue;
        if (on) {
            if (!(w.second.flags & 0x10)) load(w.second);
        } else {
            release(w.second);
        }
    }
    g_groups[group].on = on;
}

void sound_enable_master(bool on) {
    g_master_on = on;
    for (auto& w : g_waves) platform::audio_set(w.second.voice, params(w.second));
}

// ---------------------------------------------------------------------------------------------
// FX (the Sounds object)

void THISCALL FX_init(uint8_t* sounds) {
    sound_log("FX_init");
    g_engine_slot = -1;
    uint8_t* sys = SoundSystem();
    Wave_load_3(WaveOk(), "fx\\ok.wav", 0);
    Wave_set_volume(WaveOk(), 0x3C);
    SoundSys_add_to_group(sys, 1, WaveOk());
    // FX_init's table: file, flags, group, slot.
    struct Entry {
        int32_t slot;
        const char* file;
        uint32_t flags;
        int32_t group;
    };
    static const Entry kFx[] = {
        {0, "psi attack.wav", 0, 1},       {1, "energy1.wav", 0xC0, 1},         {2, "projectile1.wav", 0xC0, 1},
        {3, "projectile2.wav", 0xC0, 1},   {56, "mindworms", 0xC0, 1},          {59, "planet buster lead.wav", 0xC0, 1},
        {60, "planet buster.wav", 0xC0, 1}, {57, "cpu turn complete", 0x80, 0}, {58, "cpu new data", 0x80, 0},
        {61, "psi blow", 0xC0, 1},         {62, "boom", 0xC0, 1},               {64, "infantry", 0xC0, 1},
        {65, "military land", 0xC0, 1},    {66, "non military land", 0xC0, 1},  {67, "grav ship", 0xC0, 1},
        {68, "hover", 0xC0, 1},            {69, "military ship", 0xC0, 1},      {70, "ship", 0xC0, 1},
        {71, "jet", 0xC0, 1},              {72, "engine2", 0xC0, 1},            {75, "missile", 0xC0, 1},
        {4, "wpn laser2.wav", 0xC0, 1},    {73, "wpn particle.wav", 0xC0, 1},   {74, "wpn gatlin laser.wav", 0xC0, 1},
        {76, "wpn chaos gun.wav", 0xC0, 1}, {77, "wpn fusion laser.wav", 0xC0, 1}, {78, "wpn tachyon bolt.wav", 0xC0, 1},
        {79, "wpn plasma shard.wav", 0xC0, 1}, {80, "wpn quantum laser.wav", 0xC0, 1},
        {81, "wpn graviton gun.wav", 0xC0, 1}, {85, "wpn Resonance Laser.wav", 0xC0, 1},
        {86, "wpn res bolt.wav", 0xC0, 1},  {87, "wpn string disruptor.wav", 0xC0, 1},
        {90, "wpn spore launcher.wav", 0xC0, 1}, {88, "wpn singularity laser.wav", 0xC0, 1},
        {89, "wpn missile launcher.wav", 0xC0, 1}, {82, "wpn conventional payload.wav", 0xC0, 1},
        {83, "wpn tetonic payload.wav", 0xC0, 1}, {84, "wpn fungal payload.wav", 0xC0, 1},
        {91, "alien infantry.wav", 0xC0, 1}, {92, "alien non military.wav", 0xC0, 1}, {93, "alien ogre.wav", 0xC0, 1},
        {94, "spore launcher.wav", 0xC0, 1}, {95, "sea lurker.wav", 0xC0, 1}, {96, "isle of the deep.wav", 0xC0, 1},
    };
    for (const Entry& e : kFx) {
        fx_wave(fx(sounds, e.slot), e.file, e.flags);
        // The laser's pitch (+0x98(0x44C)) is set between its load and its group.
        if (e.slot == 4) Wave_set_pitch(fx(sounds, 4), 0x44C);
        SoundSys_add_to_group(sys, (uint32_t)e.group, fx(sounds, e.slot));
    }
    // The CPU voices (wave_it, popups' #wave): 0x74C5F0 [45], loaded when played; 19 and 31 have
    // no file.
    static const char* const kCpu[45] = {
        "pod recovered", "data pod recovered", "invalid command", "invalid base site", "invalid terraform",
        "improved already", "lacks ability", "combat units only", "nocando", "insufficient energy",
        "nn already linked", "PT insufficient exp", "system error", "combat report", "artifact lost",
        "faction gone", "indigenous lifeforms", "improvement destroyed", "enemy pt gone", nullptr,
        "terraform complete", "mission accomplished", "pt compromised", "mission aborted", "resource shortfall",
        "prod complete", "max population", "new orders", "drone riots", "drone riots over", "golden age begun",
        nullptr, "request confirmation", "time up", "units engaged", "are you certain1", "project initiated",
        "project complete", "enemy project initiated", "unit lost", "nutrient resources", "energy resources",
        "mineral resources", "please don't go", "review your options"};
    for (int32_t i = 0; i < 45; i++) {
        if (!kCpu[i]) continue;
        std::string f = std::string("fx\\CPU ") + kCpu[i] + ".wav";
        Wave_init(cpu_voice(i), f.c_str(), 0x80);
    }
    for (int32_t i = 0; i < 45; i++) SoundSys_add_to_group(sys, 0, cpu_voice(i));
    Wave_load_3(WaveAlphaMenu(), "fx\\amenu2.wav", 0x11);
    SoundSys_add_to_group(sys, 1, WaveAlphaMenu());
    Wave_set_pan(WaveAlphaMenu(), -0x20);
    Wave_set_volume(WaveAlphaMenu(), 100);
    Wave_load_3(WaveScoot(), "fx\\scoot.wav", 0x11);
    SoundSys_add_to_group(sys, 1, WaveScoot());
    Wave_load_3(WavePassover(), "fx\\passover.wav", 0x11);
    Wave_set_volume(WavePassover(), 0x3C);
    SoundSys_add_to_group(sys, 1, WavePassover());
    Wave_load_3(WaveMenuUp(), "fx\\menu up.wav", 0x10);
    SoundSys_add_to_group(sys, 1, WaveMenuUp());
    Wave_set_pitch(WaveMenuUp(), 200);
    Wave_set_volume(WaveMenuUp(), 0x2D);
    Wave_load_3(WaveMenuDown(), "fx\\menu down.wav", 0x10);
    SoundSys_add_to_group(sys, 1, WaveMenuDown());
    Wave_set_volume(WaveMenuDown(), 100);
    Wave_set_pitch(WaveMenuDown(), 300);
}

namespace {
// FX_play's body after the log: the preference, ducking and psi attack pitch, then the play.
void fx_play(uint8_t* sounds, int32_t id, bool loc) {
    if (!(GamePreferences & 0x400) || id < 0 || id > 96) return;
    uint8_t* wave = fx(sounds, id);
    WaveState& w = W(wave);
    if (id == 0 || id == 1 || id == 2) {
        music_call(0x148);
        if (loc) Wave_set_volume(wave, 0x3C);
        if (id == 0) Wave_set_pitch(wave, playing(wave) ? w.pitch + 100 : 0);
    }
    if (w.flags & 0x10) load(w);
    Wave_play_2(wave);
}
}  // namespace

void THISCALL FX_play(uint8_t* sounds, int32_t id) {
    sound_log("FX_play %d", id);
    fx_play(sounds, id, false);
}

void THISCALL FX_play_loc(uint8_t* sounds, int32_t id, int32_t x, int32_t y) {
    sound_log("FX_play_loc %d (%d,%d)", id, x, y);
    fx_play(sounds, id, true);
}

void THISCALL FX_stop(uint8_t* sounds, int32_t id) {
    sound_log("FX_stop %d", id);
    Wave_stop(fx(sounds, id));
}

void THISCALL FX_fade(uint8_t* sounds, int32_t id) {
    sound_log("FX_fade %d", id);
    WaveState& w = W(fx(sounds, id));
    fade(w, w.fade_ms);
}

void THISCALL FX_engine_pitch(uint8_t* sounds, int32_t pitch) {
    sound_log("FX_engine_pitch %d", pitch);
    if (g_engine_slot >= 0) Wave_set_pitch(fx(sounds, g_engine_slot), pitch);
}

// planet_busting: waits while the planet buster's lead-in (slot 59) plays (do_task loop).
void ui_battle_wait_sound() {
    uint8_t* wave = fx(&Sounds, 59);
    uint32_t until = platform::ticks_ms() + 10000;
    while (playing(wave) && (int32_t)(until - platform::ticks_ms()) > 0) platform::delay_ms(10);
}

void GAMEAPI release_sound() {
    sound_log("release_sound");
    sound_shutdown();
}

// ---------------------------------------------------------------------------------------------
// Voices

void GAMEAPI wave_it(int32_t id) {
    sound_log("wave_it %d", id);
    if (!(GamePreferences & 0x400)) return;
    if (playing(&FacilityVoice) || playing(cpu_voice(g_voice_id))) return;
    if (id < 0 || id >= 45) return;
    g_voice_id = id;
    Wave_load_2(cpu_voice(id));
    Wave_play_2(cpu_voice(id));
}

void sound_popup_wave(int32_t wave, int32_t volume) {
    sound_log("popup_wave %d", wave);
    if (!(GamePreferences & 0x400) || wave < 0 || !volume || wave >= 0x2D) return;
    if (wave == 0x19 && CurrentBasePtr && cur_base().queue_items[0] < -0x46) wave = 0x25;
    if (wave == 0x2B && platform::ticks_ms() % 3 != 1) return;
    if (!playing(cpu_voice(g_voice_id)) && !playing(&FacilityVoice)) {
        g_voice_id = wave;
        Wave_load_2(cpu_voice(wave));
        Wave_play_2(cpu_voice(wave));
    }
    if (wave == 0x19) music_call(0x138);
    if (wave == 0x10) FX_play(&Sounds, 0x38);
}

// ---------------------------------------------------------------------------------------------
// Interface sounds

void GAMEAPI ok_callback() {
    Wave_set_pitch(WaveOk(), g_ok_pitch);
    Wave_play_2(WaveOk());
    g_ok_pitch += g_ok_pitch < 0 ? 200 : -300;
    if (g_ok_pitch < -0x4AF || g_ok_pitch > 0x4AF) g_ok_pitch = 0;
}

void GAMEAPI passover_callback() { Wave_play_2(WavePassover()); }

void GAMEAPI input_callback() { music_call(0x13C); }

void sound_menu_up() { Wave_play_2(WaveMenuUp()); }
void sound_menu_down() { Wave_play_2(WaveMenuDown()); }
void sound_alpha_menu() { Wave_play_2(WaveAlphaMenu()); }

// ---------------------------------------------------------------------------------------------
// Ambience

int32_t GAMEAPI ambience(int32_t id) {
    sound_log("ambience %d", id);
    if (!(GamePreferences & 0x800)) return id;
    if (g_ambience == id) return g_ambience;
    if (id >= 200 && id < 0xD6) {
        static const int16_t kMethod[] = {0x13C, 0x130, 0x140, 0x144, 0x148, 0x14C, 0x150,
                                          0x134, 0x13C, 0x154, 0x158, 0x15C, 0x160, 0x164};
        music_call(kMethod[id - 200]);
        return id;
    }
    g_engine_slot = -1;
    if (g_ambience >= 0) {
        WaveState& w = W(fx(&Sounds, g_ambience));
        fade(w, w.fade_ms);
    }
    g_ambience = id;
    if (id >= 0) {
        if (playing(fx(&Sounds, id))) Wave_stop(fx(&Sounds, id));
        FX_play(&Sounds, id);
    }
    return g_ambience;
}

// The selected unit's sound: native life, artillery, engines by triad and speed, the alien
// factions' units (MFaction rule flag 0x80), terraformers; played when not already playing.
void GAMEAPI ambience_veh(int32_t veh_id) {
    sound_log("ambience_veh %d", veh_id);
    if (veh_id < 0) return;
    if (g_ambience_veh >= 0 && g_ambience_veh != veh_id && g_ambience_veh < 0x61) {
        WaveState& w = W(fx(&Sounds, g_ambience_veh));
        fade(w, w.fade_ms);
    }
    g_ambience_veh = veh_id;
    if (!(GamePreferences & 0x800)) return;
    const VEH& v = Vehs[veh_id];
    if (v.order != 0) return;
    int32_t unit_id = v.unit_id;
    const UNIT& u = Units[unit_id];
    bool alien = (MFactions[v.faction_id].rule_flags & 0x80) != 0;
    auto try_play = [&](int32_t slot) {
        if (!playing(fx(&Sounds, slot))) FX_play(&Sounds, slot);
    };
    if (u.plan == 8) {  // terraformers
        if (!alien) {
            FX_play(&Sounds, 0x42);
            return;
        }
        try_play(0x5C);
        return;
    }
    int32_t chassis = u.chassis_id;
    if (chassis == 0) {  // infantry
        if (Weapon[u.weapon_id].offense_value == 0) {
            if (!playing(fx(&Sounds, 0x42))) FX_play(&Sounds, 0x42);
            return;
        }
        if (!alien) {
            try_play(0x40);
            return;
        }
        if (unit_id == 0x10 || unit_id == 0x11 || unit_id == 0x12) {
            try_play(0x5D);
            return;
        }
        if (unit_id != 8) {
            if (unit_id != 0xF) {
                try_play(0x5B);
                return;
            }
            try_play(0x5E);
            return;
        }
    } else {
        if (chassis == 1) {
            try_play(0x48);
            return;
        }
        if (chassis == 2) {
            try_play(0x31);
            return;
        }
        if (unit_id == 0xE) {
            try_play(0x5F);
            return;
        }
        if (unit_id == 9) {
            try_play(0x60);
            return;
        }
        static const int8_t kByChassis[] = {0, 0, 0, 0x45, 0x46, 0x47, 0x44, 0x43, 0x4B};
        if (chassis >= 3 && chassis <= 8) {
            try_play(kByChassis[chassis]);
            return;
        }
        if (unit_id != 8) {
            int32_t triad = Chassis[chassis].triad;
            if (triad == 1) {
                try_play(0xC);
                return;
            }
            if (triad == 2) {
                try_play(0x12);
                return;
            }
            if (u.plan == 0xC) {
                try_play(0x14);
                return;
            }
            int32_t off = Weapon[u.weapon_id].offense_value;
            if (off != 0) {
                if (off <= (int32_t)Chassis[chassis].speed) {
                    try_play(0x1A);
                    return;
                }
                try_play(7);
                return;
            }
            try_play(0x2E);
            return;
        }
    }
    try_play(0x38);  // mind worms (unit 8)
}

// ---------------------------------------------------------------------------------------------
// boom (0x504AA0): the combat animation's sounds. The animation itself (explosion sprites, the
// attacker's shot) is not reconstructed; the sounds follow boom's choices as if the battle were
// on screen (the original plays them only when it draws the tiles): a missile's launch (chassis 8,
// FX 0x52), the hit (0x3D for a big explosion: flag 0x100 for native life and missiles' targets,
// else 0x3E, or 0x5A when the defender is a spore launcher), then the attacker's weapon (energy
// 1 for hand weapons, slots 4, 0x49..0x59 by weapon) or the psi attack (0).
int32_t GAMEAPI boom(int32_t x, int32_t y, int32_t flags) {
    sound_log("boom %d %d %d", x, y, flags);
    int32_t att = VehDrawAttackID, def = VehDrawDefendID;
    auto unit_of = [](int32_t veh) -> const UNIT& { return Units[Vehs[veh].unit_id]; };
    int32_t first = att, second = def;    // local_20, local_18
    int32_t shooter = att, target = def;  // iVar10, iVar12
    bool psi = false;
    if (!(flags & 0x70)) {
        if (att >= 0 && unit_of(att).chassis_id == 8 && (flags & 1)) {
            FX_play_loc(&Sounds, 0x52, Vehs[att].x, Vehs[att].y);
            return 0;
        }
        if (flags & 1) {
            if ((att >= 0 && unit_of(att).weapon_id == 0xF) || (def >= 0 && unit_of(def).weapon_id == 0xF)) psi = true;
            if (def >= 0 && Vehs[def].unit_id == 1) FX_play(&Sounds, 1);
        }
        if (!psi && !(flags & 8)) {
            second = att;
            first = def;
            target = att;
            shooter = def;
        }
        if ((flags & 2) && target >= 0) {
            int32_t u = Vehs[target].unit_id;
            if (u == 8 || u == 9 || u == 10 || u == 0xE || u == 0xF || u == 0x13) flags |= 0x100;
        }
    }
    // The explosion's setup: for every map window showing the tile (window 0, and the others that
    // are active), the explosion's sound (not for the shot, flag 1); nothing more when no window
    // shows it.
    bool shown = false;
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* win = MapWins[i];
        if (!win || (i != 0 && !mw(win, kMwActive))) continue;
        int32_t oy = mw(win, kMwOriginY), ox = mw(win, kMwOriginX);
        if (y < oy || y >= mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) + oy) continue;
        int32_t tx = x;
        if (!(MapToggleFlat & 1)) {
            if (x < ox) tx = MapAreaX + x;
            if (mw(win, kMwColsEven) + mw(win, kMwColsOdd) + ox <= tx) tx -= MapAreaX;
        }
        if (tx < ox || tx >= mw(win, kMwColsEven) + mw(win, kMwColsOdd) + ox) continue;
        shown = true;
        if ((flags & 0x70) || (flags & 1)) continue;  // the explosion (local_24 0, not the shot)
        if (!(flags & 0x100)) {
            if (second >= 0 && Vehs[second].unit_id == 0xF) FX_play_loc(&Sounds, 0x5A, Vehs[second].x, Vehs[second].y);
            else if (first < 0 || unit_of(first).chassis_id != 8) FX_play_loc(&Sounds, 0x3E, x, y);
        } else {
            FX_play_loc(&Sounds, 0x3D, x, y);
        }
    }
    if ((flags & 0x60) || !shown || !(flags & 1)) return 0;
    if (psi) {
        FX_play(&Sounds, 0);
        return 0;
    }
    if (second >= 0 && unit_of(second).weapon_id == 0) FX_play(&Sounds, 1);
    if (shooter >= 0) {
        static const int8_t kWeapon[15] = {-1, 4, 0x49, 0x4A, 0x59, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x58, 0x55, 0x56, 0x57};
        int32_t w = unit_of(shooter).weapon_id;
        if (w >= 1 && w <= 14) FX_play_loc(&Sounds, kWeapon[w], Vehs[shooter].x, Vehs[shooter].y);
        if (first >= 0 && Vehs[first].unit_id == 0xF) FX_play_loc(&Sounds, 0x5A, Vehs[shooter].x, Vehs[shooter].y);
    }
    return 0;
}

// veh_scoot (0x55A150): the unit's sliding animation starts with fx\scoot.wav (0x749C18). The
// slide itself is not drawn; its game-state effect is the vehicle's facing, which the original
// always sets (vehdraw_turn_start, or directly with flags 0x4000).
void GAMEAPI veh_scoot(int32_t veh_id, int32_t, int32_t, int32_t dir, int32_t) {
    if (dir > 7) dir %= 8;
    Wave_play_2(WaveScoot());
    Vehs[veh_id].rotate_angle = (int8_t)dir;
}
