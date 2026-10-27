// smacx's start-up as a distributed game: the per-user directory, finding the installed game
// (remembered folder, standard install locations, then asking the player) and the data image
// built from the player's terranx.exe (src/tools/gen_data_image.py's format; engine/memmap.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gamedata {

// md5 of the supported terranx.exe (v2.0, the GOG Planetary Pack's).
extern const char kExeMd5[];

// The md5 of a file as 32 lower-case hex digits ("" when it cannot be read).
std::string md5_file(const std::string& path);

// The initial contents of terranx.exe's writable data region as gen_data_image.py writes them
// (records {u32 addr, u32 len, bytes}). False when exe is not a PE image.
bool build_data_image(const std::vector<uint8_t>& exe, std::vector<uint8_t>* image);

// Whether dir holds a usable game: terranx.exe v2.0 and the data files the game opens. On false,
// *why says what is wrong (missing files or another version of terranx.exe).
bool check_game_dir(const std::string& dir, std::string* why);

// The per-user directory for settings, saves and caches (SDL_GetPrefPath; ends with a separator).
// Empty when it cannot be created.
std::string user_dir();

// The game folder: the remembered one (user_dir/smacx.cfg), the SMACX_GAMEDIR environment
// variable, the standard install locations, else the player is asked (message box, the
// platform's folder picker, a typed path on a terminal). The choice is remembered. Empty when
// none was found or the player gave up (the reason is on stderr and was shown).
std::string find_game_dir(const std::string& udir);

// The data image for the game in gamedir, built on first use and cached in udir. Returns its
// path ("" on failure, reported on stderr).
std::string data_image(const std::string& gamedir, const std::string& udir);

// The default ini next to the executable (the release ships it), else the built-in copy, written
// to path when path does not exist yet, with "Sliding windows" off (smacx's default for players).
// An existing ini has them turned off once (recorded in udir's smacx.cfg).
void seed_ini(const std::string& path, const std::string& udir);

// Shows an error to the player (message box, and stderr).
void show_error(const std::string& text);

}  // namespace gamedata
