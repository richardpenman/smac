// The save / load file dialogs of save_game and load_game (smacx's replacement of the headless
// "filebox" area: FileDialog, FileBox, FileList; engine/ui_bridge.h).
//
// Simplified (not the original's FileBox 0x407350..0x407BE0 with its FileList widget, nor the
// Windows common dialog): saving asks for the file name in a popup with an edit field (the
// default is the name save_game proposes, as FileList_set_path gives it: "<dir>\<leader> <...>,
// <year>"), the extension is added; loading lists the files of the directory with the dialog's
// extension (newest first) in a popup. The returned path keeps the original's naming
// ("saves\<name>.SAV"), which the engine's file layer resolves.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/ui_bridge.h"
#include "platform/win32.h"
#include "ui/popup.h"

namespace {

// Case-insensitive suffix test (the original's file names are case-insensitive).
bool has_ext(const std::string& s, const std::string& ext) {
    if (s.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); i++)
        if (std::tolower((uint8_t)s[s.size() - ext.size() + i]) != std::tolower((uint8_t)ext[i])) return false;
    return true;
}

std::string g_path;  // FileList_set_path: initial "<dir>\<name>"
std::string g_ext;   // FileList_add_ext: ".SAV"
char g_result[256];

std::string dir_of(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}
std::string name_of(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    std::string n = s == std::string::npos ? p : p.substr(s + 1);
    if (n.find('*') != std::string::npos) n.clear();  // a pattern ("saves\*.SAV")
    return n;
}

}  // namespace

void THISCALL FileDialog_ctor(uint8_t* self) { std::memset(self, 0, kFileDialogSize); }
void THISCALL FileDialog_dtor(uint8_t*) {}
// The Windows dialog ("WindowsFileBox" preference) is not available: the in-game box is used.
char* THISCALL FileDialog_save(uint8_t* self) { return FileBox_save(self); }
char* THISCALL FileDialog_open(uint8_t* self) { return FileBox_load(self); }
int32_t THISCALL FileDialog_add_filter(uint8_t*, const char*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_ext(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_name(uint8_t*, const char*) { return 0; }
void THISCALL FileBox_ctor(uint8_t* self) { std::memset(self, 0, kFileBoxSize); }
void THISCALL FileBox_init(uint8_t*) {}
void THISCALL FileBox_close(uint8_t*) {}
int32_t THISCALL FileList_set_path(uint8_t*, const char* path) {
    g_path = path ? path : "";
    return 0;
}
int32_t THISCALL FileList_add_ext(uint8_t*, const char* ext) {
    g_ext = ext ? ext : "";
    return 0;
}
void THISCALL FileList_close(uint8_t*) {}
void ui_filebox_destroy(uint8_t*) {}

char* THISCALL FileBox_save(uint8_t*) {
    BasePop* pop = popup_new();
    std::string name = name_of(g_path);
    // An edit group (flags 4) with Cancel (0x40): "Save game as" and the name field.
    BasePop_start(pop, nullptr, "SAVEGAMEAS", -1, name.c_str(), 0x44, nullptr);
    BasePop_string(pop, "^Save Game");
    BasePop_item(pop, "Name:", 0);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    std::string typed = BasePop_edit_text(pop, 0) ? BasePop_edit_text(pop, 0) : "";
    popup_delete(pop);
    if (r < 0 || typed.empty()) return nullptr;
    std::string out = dir_of(g_path) + typed;
    if (!has_ext(out, g_ext)) out += g_ext;
    std::snprintf(g_result, sizeof(g_result), "%s", out.c_str());
    return g_result;
}

char* THISCALL FileBox_load(uint8_t*) {
    std::string dir = dir_of(g_path);
    char host[1024];
    platform::resolve_path(dir.empty() ? "." : dir.c_str(), host, sizeof(host));
    struct File {
        std::string name;
        std::filesystem::file_time_type time;
    };
    std::vector<File> files;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(host, ec)) {
        std::string n = e.path().filename().string();
        if (n.size() <= g_ext.size() || !has_ext(n, g_ext)) continue;
        files.push_back({n, e.last_write_time(ec)});
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.time > b.time; });
    if (files.empty()) return nullptr;
    BasePop* pop = popup_new();
    BasePop_start(pop, nullptr, "SMACXFILELIST", -1, nullptr, 0x42, nullptr);  // list box, Cancel
    BasePop_string(pop, "^Load Game");
    for (size_t i = 0; i < files.size() && i < 200; i++) BasePop_item(pop, files[i].name.c_str(), (int32_t)i);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0 || r >= (int32_t)files.size()) return nullptr;
    std::snprintf(g_result, sizeof(g_result), "%s%s", dir.c_str(), files[r].name.c_str());
    return g_result;
}
