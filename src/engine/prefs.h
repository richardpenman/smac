// Preferences ("Alpha Centauri.Ini", section [Alpha Centauri]) (original module prefs,
// 0x59D980-0x59E980). Values are read into TextBufferGetPtr and parsed with the text_item_*
// functions; bit sets are stored as binary strings ("1" and "0", at least 8 digits).
#pragma once
#include "engine/globals.h"

GLOBAL_NATIVE(const char*, PrefsIniPath, 0x6900CC);  // ".\\Alpha Centauri.Ini"
GLOBAL_NATIVE(const char*, PrefsSection, 0x6900D0);  // "Alpha Centauri"

// Reads string key (default def) into TextBufferGetPtr; with use_default (or an old "Prefs Format")
// only the default is used.
ADDR(0x59D980) void GAMEAPI prefs_get(const char* key, const char* def, int32_t use_default);
// Default "Preferences" bits (another set when "Laptop" is set).
ADDR(0x59DA20) uint32_t GAMEAPI default_prefs();
// Default "More Preferences" bits.
ADDR(0x59DAA0) uint32_t GAMEAPI default_prefs2();
// Default "Announce" (warning) bits.
ADDR(0x59DB20) uint32_t GAMEAPI default_warn();
// Default game rules bits.
ADDR(0x59DB30) uint32_t GAMEAPI default_rules();
// Reads number key (default def) and returns it; the text stays in TextBufferGetPtr.
ADDR(0x59DB40) int32_t GAMEAPI prefs_get_int(const char* key, int32_t def, int32_t use_default);
// Expansion: faction file names from keys "Faction 1".."Faction 7".
ADDR(0x59DBD0) void GAMEAPI prefs_fac_load();
// Loads all preferences (DefaultPrefs, AlphaIniPrefs); use_defaults ignores the file's bit sets.
ADDR(0x59DCF0) void GAMEAPI prefs_load(int32_t use_defaults);
ADDR(0x59E510) void GAMEAPI prefs_put(const char* key, const char* value);
// Writes a number (decimal, or binary digits when binary != 0).
ADDR(0x59E530) void GAMEAPI prefs_put_int(const char* key, uint32_t value, int32_t binary);
// Saves all preferences (and the expansion faction choices when save_factions != 0).
ADDR(0x59E5D0) void GAMEAPI prefs_save(int32_t save_factions);
// Copies the loaded preferences into the game state (GamePreferences etc.).
ADDR(0x59E950) void GAMEAPI prefs_use();

// Sets GameLanguage (original pop module).
ADDR(0x627100) void GAMEAPI set_language(int32_t language);

// Native builds: static data of the module (ini path and section pointers).
void prefs_static_init(const char* ini_path);
