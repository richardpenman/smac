// The per-user directory of a copy from before the rename to OpenSMAC (platform/userdir.h): moved
// to the new one with its files, smacx.cfg renamed, and nothing touched when there is nothing to do.
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/userdir.h"
#include "unit.h"

namespace fs = std::filesystem;

namespace {
// A fresh directory below the system's temporary one (removed again by the destructor).
struct TempRoot {
    fs::path path;
    explicit TempRoot(const char* name) : path(fs::temp_directory_path() / (std::string("opensmac_unit_") + name)) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    // A directory path as SDL_GetPrefPath gives it: ending with a separator.
    std::string dir(const char* name) const { return (path / name).string() + (char)fs::path::preferred_separator; }
};

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("userdir: names") {
    std::string name = userdir::app_name();
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(name == "OpenSMAC");
#else
    CHECK(name == "opensmac");
#endif
    TempRoot root("userdir_names");
    CHECK(userdir::legacy_dir(root.dir("opensmac")) == root.dir("smacx"));
}

TEST_CASE("userdir: an old directory moves to the new one, smacx.cfg renamed") {
    TempRoot root("userdir_move");
    std::string old_dir = root.dir("smacx"), new_dir = root.dir("opensmac");
    write(fs::path(old_dir) / "smacx.cfg", "gamedir=/games/smac\n");
    write(fs::path(old_dir) / "Alpha Centauri.Ini", "[Alpha Centauri]\n");
    write(fs::path(old_dir) / "saves" / "game.sav", "save");
    write(fs::path(old_dir) / "smacxnet.txt", "#TCPHOST\n");
    fs::create_directories(new_dir);  // SDL_GetPrefPath creates the new directory, empty
    std::string error;
    CHECK(userdir::migrate_dir(old_dir, new_dir, &error) == userdir::Migrated::kMoved);
    CHECK(!fs::exists(old_dir));
    CHECK(read(fs::path(new_dir) / "saves" / "game.sav") == "save");
    CHECK(read(fs::path(new_dir) / "Alpha Centauri.Ini") == "[Alpha Centauri]\n");
    userdir::migrate_files(new_dir);
    CHECK(read(fs::path(new_dir) / "opensmac.cfg") == "gamedir=/games/smac\n");
    CHECK(!fs::exists(fs::path(new_dir) / "smacx.cfg"));
    CHECK(!fs::exists(fs::path(new_dir) / "smacxnet.txt"));
    // The next start: nothing left to move.
    CHECK(userdir::migrate_dir(old_dir, new_dir, &error) == userdir::Migrated::kNothing);
}

TEST_CASE("userdir: a new directory in use is kept") {
    TempRoot root("userdir_keep");
    std::string old_dir = root.dir("smacx"), new_dir = root.dir("opensmac");
    write(fs::path(old_dir) / "smacx.cfg", "gamedir=/old\n");
    write(fs::path(new_dir) / "opensmac.cfg", "gamedir=/new\n");
    std::string error;
    CHECK(userdir::migrate_dir(old_dir, new_dir, &error) == userdir::Migrated::kNothing);
    CHECK(fs::exists(fs::path(old_dir) / "smacx.cfg"));
    // A smacx.cfg beside opensmac.cfg does not replace it.
    write(fs::path(new_dir) / "smacx.cfg", "gamedir=/old\n");
    userdir::migrate_files(new_dir);
    CHECK(read(fs::path(new_dir) / "opensmac.cfg") == "gamedir=/new\n");
}

TEST_CASE("userdir: no old directory") {
    TempRoot root("userdir_none");
    std::string old_dir = root.dir("smacx"), new_dir = root.dir("opensmac");
    fs::create_directories(new_dir);
    std::string error;
    CHECK(userdir::migrate_dir(old_dir, new_dir, &error) == userdir::Migrated::kNothing);
    CHECK(fs::is_directory(new_dir));
    CHECK(!fs::exists(old_dir));
    userdir::migrate_files(new_dir);
    CHECK(fs::is_empty(new_dir));
}
