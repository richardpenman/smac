// Sound effects, voices and the Wave objects (ui 0x4455F0..0x447360 FX / wave_it / ambience,
// 0x4C5000..0x4C7260 Wave and the sound groups of soundx.dll's wrapper): sound.cpp. Music
// (load_music and the ambient music classes): music.cpp. Movies (amovie_project_name, the WVE
// player): movie.cpp.
//
// The original's Wave objects (0x6C bytes: vtable, volume +4, pan +8, fade time +0x38, soundx
// handle +0x3C, file name +0x4C, flags +0x54, pitch +0x58, length +0x60, start time +0x64, sound
// group +0x68) live in the memory image (Sounds 0x749CF8: 97 FX slots; the CPU voices
// 0x74C5F0 [45]; FacilityVoice 0x945ED0 ...). Here their state is kept in native side structures
// keyed by the object's address; the image is never written (the fcompare ignore list names those
// objects, and no game state depends on them).
//
// SMACX_SOUNDLOG=1 logs every FX / voice / music call on stderr ("sound: T<turn> FX_play 15"), before any
// test of the preferences or of the device, so the log is the same with or without sound.
#pragma once
#include <cstdint>
#include <memory>

#include "engine/memmap.h"
#include "platform/ui_platform.h"

// smacx: opens the audio device unless disabled (--nosound); the log flag.
void sound_startup(bool enabled);
void sound_shutdown();
bool sound_log_on();
void sound_log(const char* fmt, ...);

// Decoded sound files (WAV, MP3), cached by resolved path; null when missing or undecodable.
std::shared_ptr<const platform::Pcm> sound_load_file(const char* name);

// ---- Wave (0x4C66E0..0x4C7260) ----
// Wave_init (+0x88): file name and flags (1 static buffer, 2 loop, 4 deferred, 0x10 load on play,
// 0x40 3D, 0x80 fire and forget: loaded at each play, timed by its length).
ADDR_NATIVE(0x4C69B0) void THISCALL Wave_init(uint8_t* wave, const char* file, uint32_t flags);
// Wave_load_3: Wave_init, then loads unless flags & 4.
ADDR_NATIVE(0x4C6DB0) int32_t THISCALL Wave_load_3(uint8_t* wave, const char* file, uint32_t flags);
ADDR_NATIVE(0x4C6CE0) int32_t THISCALL Wave_load_2(uint8_t* wave);
ADDR_NATIVE(0x4C7130) void THISCALL Wave_set_volume(uint8_t* wave, uint32_t volume);
ADDR_NATIVE(0x4C6EE0) int32_t THISCALL Wave_set_pitch(uint8_t* wave, int32_t pitch);
ADDR_NATIVE(0x4C66B0) void THISCALL Wave_set_pan(uint8_t* wave, int32_t pan);
// Sound group of a wave (FUN_004C5240 on the sound system 0x90D978): 0 voices, 1 effects.
ADDR_NATIVE(0x4C5240) int32_t THISCALL SoundSys_add_to_group(uint8_t* sys, uint32_t group, uint8_t* wave);
// Stops a wave (vtable +0x20).
void Wave_stop(uint8_t* wave);
// Sound_fade (0x4C65E0): fades the wave out over its fade time (+0x38, 1000 ms).
void Sound_fade(uint8_t* wave);
// The sound system's master volume (FUN_004C5150), group volumes (FUN_004C5320), group on / off
// (FUN_004C53A0 / FUN_004C5400: the group's waves are reloaded / released) and master on / off
// (FUN_004C51C0 / FUN_004C51D0).
void sound_set_master_volume(int32_t volume);
void sound_set_group_volume(int32_t group, int32_t volume);
void sound_enable_group(int32_t group, bool on);
void sound_enable_master(bool on);
int32_t sound_group_volume(int32_t group);
int32_t sound_master_volume();
// PrefWin_init's sliders: Main Volume (127), Voice Volume (0x55), SFX Volume (0x5F) and Music
// Volume (0x3F) of the preferences applied to the master, group 0, group 1 and the music.
void sound_apply_volume_prefs();

// ---- interface sounds (FX_init's callbacks) ----
// BaseButton_on_left_click's ok_callback (fx\ok.wav, pitch stepping), FlatButton's passover_callback
// (fx\passover.wav), the window procedure's input_callback (music: a user event).
ADDR_NATIVE(0x445650) void GAMEAPI ok_callback();
ADDR_NATIVE(0x4456A0) void GAMEAPI passover_callback();
ADDR_NATIVE(0x4457C0) void GAMEAPI input_callback();
// BasePop's sound (#wave N, #vol V of the popup's script section): popup_wave_callback with the
// popup's +0x104 / +0x108 fields.
void sound_popup_wave(int32_t wave, int32_t volume);
// Sliding windows (Popup_show / slide_show: fx\menu up.wav at 0x749C88; BaseWin_on_status: fx\menu
// down.wav at 0x74D960; AlphaMenu_show / hide: fx\amenu2.wav at 0x6A7090).
void sound_menu_up();
void sound_menu_down();
void sound_alpha_menu();

// The Wave objects of the image used by the UI.
inline uint8_t* WaveOk() { return mem::host(0x74D8F0); }
inline uint8_t* WaveMenuUp() { return mem::host(0x749C88); }
inline uint8_t* WaveMenuDown() { return mem::host(0x74D960); }
inline uint8_t* WaveAlphaMenu() { return mem::host(0x6A7090); }
inline uint8_t* WaveScoot() { return mem::host(0x749C18); }
inline uint8_t* WavePassover() { return mem::host(0x74D9D0); }
inline uint8_t* SoundSystem() { return mem::host(0x90D978); }

// ---- music (music.cpp) ----
// load_music (0x4457E0): the faction's ambient music set (fx\?set?.amb) and its music class.
ADDR_NATIVE(0x4457E0) void GAMEAPI load_music(int32_t faction_id);
// The music object (0x74DAA0) or null; its methods by the original's vtable offsets.
struct MusicObj;
MusicObj* music_object();
void music_call(int32_t vtable_offset);
void music_release();
void music_set_volume(int32_t volume);
bool music_loaded();
// Test aid (smacx --keys MUSICRENDER:FILE:SECONDS): renders the music sequencer's next seconds into a
// WAV file instead of the device and logs its level.
bool music_render(const char* path, int32_t seconds);

// ---- movies (movie.cpp) ----
// FUN_004BEA50: plays movies\NAME.wve full screen; Enter, Esc, space or a click end it.
// Returns false when the file could not be played.
bool movie_play(const char* path);
