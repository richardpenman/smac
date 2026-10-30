// The per-user directory of a copy from before the rename to OpenSMAC (v0.2.x, called smacx):
// moved to OpenSMAC's on the first start, so that a player keeps the ini, the saved games, the
// remembered game folder, the data image and the Hall of Fame. The work is here, without SDL, so
// that the unit tests can check it; gamedata::user_dir (src/app/gamedata.cpp) calls it.
#pragma once
#include <string>

namespace userdir {

// The directory's name below the platform's per-user location (SDL_GetPrefPath's app): Windows
// %APPDATA%\OpenSMAC, macOS ~/Library/Application Support/OpenSMAC, else (Linux, the XDG
// convention) ~/.local/share/opensmac.
const char* app_name();
// The names before the rename: the directory and the remembered-folder file.
constexpr char kLegacyAppName[] = "smacx";
constexpr char kLegacyConfig[] = "smacx.cfg";
constexpr char kConfig[] = "opensmac.cfg";

// The legacy directory beside udir (a path ending with a separator, as SDL_GetPrefPath gives it):
// its sibling named kLegacyAppName, also ending with a separator. Empty when udir has no parent.
std::string legacy_dir(const std::string& udir);

enum class Migrated { kNothing, kMoved, kCopied, kFailed };

// Moves old_dir to new_dir when old_dir is a directory and new_dir is missing or empty
// (SDL_GetPrefPath creates it): a rename, else (another file system) a copy that leaves old_dir
// in place. A failed copy is removed again, so the next start tries anew. *error: why it failed.
Migrated migrate_dir(const std::string& old_dir, const std::string& new_dir, std::string* error);

// The files in udir (ending with a separator) named before the rename: smacx.cfg becomes
// opensmac.cfg unless that exists; the stale smacxnet.txt (the network popups' texts, now
// opensmacnet.txt) is removed.
void migrate_files(const std::string& udir);

}  // namespace userdir
