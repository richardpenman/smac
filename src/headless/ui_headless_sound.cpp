// Headless implementations of the sound functions (engine/ui_bridge.h): the FX object, the voices,
// the Wave objects, ambience, release_sound and the planet buster's wait, and desktop_init (whose
// only native effect in OpenSMAC is load_music). OpenSMAC replaces this area (src/ui/sound.cpp, music.cpp).
#include "engine/ui_bridge.h"

// No sound: every call does nothing; no wave is ever playing.
void THISCALL FX_init(uint8_t*) {}
void THISCALL FX_play(uint8_t*, int32_t) {}
void THISCALL FX_play_loc(uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL FX_fade(uint8_t*, int32_t) {}
void THISCALL FX_stop(uint8_t*, int32_t) {}
void THISCALL FX_engine_pitch(uint8_t*, int32_t) {}
void GAMEAPI wave_it(int32_t) {}
int32_t GAMEAPI ambience(int32_t) { return 0; }
int32_t THISCALL Wave_unload(uint8_t*) { return 0; }
int32_t THISCALL Wave_load(uint8_t*, const char*) { return 0; }
int32_t THISCALL Wave_play_2(uint8_t*) { return 0; }
int32_t THISCALL Wave_is_playing(uint8_t*) { return 0; }
void THISCALL Sound_fade_2(uint8_t*, int32_t) {}
void GAMEAPI release_sound() {}
void ui_battle_wait_sound() {}
// The game desktop's windows (none headless).
int32_t GAMEAPI desktop_init(int32_t) { return 0; }
