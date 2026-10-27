// smacx's start-up as a distributed game (gamedata.h).
#include "app/gamedata.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <shlobj.h>
#define SMACX_ISATTY(fd) _isatty(fd)
#else
#include <sys/wait.h>
#include <unistd.h>
#define SMACX_ISATTY(fd) isatty(fd)
#endif

#include "default_ini.h"  // generated: kDefaultIni, the bundled default ini (harness/oracle/runtime)
#include "platform/win32.h"

namespace fs = std::filesystem;

namespace gamedata {

const char kExeMd5[] = "cc659f53cb6fa0eaaddccd0a54176647";

namespace {

// ---- md5 (RFC 1321) ----
struct Md5 {
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint8_t buf[64];
    uint64_t len = 0;

    static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
    void block(const uint8_t* p) {
        static const uint32_t K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
        static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
        uint32_t m[16];
        for (int i = 0; i < 16; i++)
            m[i] = p[i * 4] | (p[i * 4 + 1] << 8) | (p[i * 4 + 2] << 16) | ((uint32_t)p[i * 4 + 3] << 24);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) f = (b & c) | (~b & d), g = i;
            else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
            else f = c ^ (b | ~d), g = (7 * i) % 16;
            uint32_t t = d;
            d = c;
            c = b;
            b = b + rol(a + f + K[i] + m[g], R[i]);
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    }
    void update(const uint8_t* p, size_t n) {
        while (n) {
            size_t used = len % 64, take = std::min(n, 64 - used);
            std::memcpy(buf + used, p, take);
            len += take, p += take, n -= take;
            if (len % 64 == 0) block(buf);
        }
    }
    std::string hex() {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        pad = 0;
        while (len % 64 != 56) update(&pad, 1);
        uint8_t l[8];
        for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (8 * i));
        update(l, 8);
        char out[33];
        for (int i = 0; i < 16; i++) std::snprintf(out + i * 2, 3, "%02x", (h[i / 4] >> (8 * (i % 4))) & 0xFF);
        return out;
    }
};

bool read_file(const std::string& path, std::vector<uint8_t>* data) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    data->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

bool write_file(const std::string& path, const uint8_t* data, size_t size) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write((const char*)data, (std::streamsize)size);
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// The entry of dir named name in any case ("" when there is none).
std::string find_ci(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    if (fs::exists(dir / name, ec)) return (dir / name).string();
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (ieq(it->path().filename().string(), name)) return it->path().string();
    return "";
}

std::vector<fs::path> subdirs(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (is_dir(it->path())) out.push_back(it->path());
    return out;
}

bool has_exe(const fs::path& dir) {
    std::error_code ec;
    for (const char* n : {"terranx.exe", "TERRANX.EXE", "Terranx.exe"})
        if (fs::exists(dir / n, ec)) return true;
    return false;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// ---- standard install locations ----
// A folder that may hold the game itself or contain it one level down.
void add_root(std::vector<fs::path>* roots, const fs::path& p) {
    if (!p.empty() && is_dir(p)) roots->push_back(p);
}

// The folders inside a Wine prefix (or a Wineskin wrapper's Resources) where installers put games.
void add_prefix(std::vector<fs::path>* roots, const fs::path& prefix) {
    fs::path c = prefix / "drive_c";
    if (!is_dir(c)) return;
    for (const char* sub :
         {"GOG Games", "Program Files (x86)/GOG Galaxy/Games", "Program Files (x86)", "Program Files", "Games"})
        add_root(roots, c / sub);
}

void add_prefixes_in(std::vector<fs::path>* roots, const fs::path& dir) {
    for (const fs::path& p : subdirs(dir)) {
        add_prefix(roots, p);
        add_prefix(roots, p / "pfx");  // Steam Proton (compatdata/<id>/pfx), Heroic
    }
}

void add_steam(std::vector<fs::path>* roots, const fs::path& steam) {
    add_root(roots, steam / "steamapps" / "common");
    add_prefixes_in(roots, steam / "steamapps" / "compatdata");
}

#ifdef _WIN32
std::string reg_string(HKEY root, const char* key, const char* value) {
    char buf[1024];
    DWORD size = sizeof(buf), type = 0;
    HKEY h;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &h) != ERROR_SUCCESS) return "";
    LONG r = RegQueryValueExA(h, value, nullptr, &type, (BYTE*)buf, &size);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || size == 0) return "";
    buf[std::min<DWORD>(size, sizeof(buf) - 1)] = 0;
    return buf;
}

// GOG's installers write HKLM\SOFTWARE\[WOW6432Node\]GOG.com\Games\<id> with the folder in "path".
void add_gog_registry(std::vector<fs::path>* out) {
    for (const char* key : {"SOFTWARE\\WOW6432Node\\GOG.com\\Games", "SOFTWARE\\GOG.com\\Games"}) {
        HKEY h;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &h) != ERROR_SUCCESS) continue;
        char name[256];
        for (DWORD i = 0;; i++) {
            DWORD n = sizeof(name);
            if (RegEnumKeyExA(h, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            std::string path = reg_string(HKEY_LOCAL_MACHINE, (std::string(key) + "\\" + name).c_str(), "path");
            if (!path.empty()) out->push_back(path);
        }
        RegCloseKey(h);
    }
}
#endif

// Folders that may be the game (checked directly) and roots whose sub-folders may be.
void standard_locations(std::vector<fs::path>* direct, std::vector<fs::path>* roots) {
#ifdef _WIN32
    add_gog_registry(direct);
    std::string steam = reg_string(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (!steam.empty()) add_steam(roots, steam);
    DWORD drives = GetLogicalDrives();
    for (int d = 0; d < 26; d++) {
        if (!(drives & (1u << d))) continue;
        char root[4] = {(char)('A' + d), ':', '\\', 0};
        if (GetDriveTypeA(root) != DRIVE_FIXED) continue;
        add_root(roots, fs::path(root) / "GOG Games");
        add_root(roots, fs::path(root) / "Games");
    }
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"}) {
        std::string pf = env(var);
        if (pf.empty()) continue;
        add_root(roots, fs::path(pf) / "GOG Galaxy" / "Games");
        add_root(roots, fs::path(pf) / "GOG.com");
        add_steam(roots, fs::path(pf) / "Steam");
        add_root(roots, pf);
    }
#else
    fs::path home = env("HOME");
#ifdef __APPLE__
    // The GOG Mac app is a Wineskin wrapper: the Windows install is inside the bundle.
    for (const fs::path& apps : {home / "Applications", fs::path("/Applications"), home / "Applications" / "GOG Games",
                                 fs::path("/Applications/GOG Games")}) {
        for (const fs::path& app : subdirs(apps)) {
            std::string n = app.filename().string();
            for (char& c : n) c = (char)std::tolower((unsigned char)c);
            if (n.find("alpha") == std::string::npos && n.find("centauri") == std::string::npos &&
                n.find("smac") == std::string::npos)
                continue;
            for (const char* res :
                 {"Contents/Resources", "Contents/SharedSupport/prefix", "Contents/SharedSupport/wineprefix"})
                add_prefix(roots, app / res);
            add_root(roots, app);
        }
    }
    add_prefixes_in(roots, home / "Library/Application Support/CrossOver/Bottles");
    add_steam(roots, home / "Library/Application Support/Steam");
#endif
    add_root(roots, home / "GOG Games");
    add_root(roots, home / "Games");
    add_root(roots, home / "Games" / "Heroic");  // Heroic's default install folder
    add_prefix(roots, env("WINEPREFIX"));
    add_prefix(roots, home / ".wine");
    add_prefixes_in(roots, home / "Games");  // Lutris (~/Games/<game>)
    add_prefixes_in(roots, home / "Games" / "Heroic" / "Prefixes");  // Heroic
    add_prefixes_in(roots, home / "Games" / "Heroic" / "Prefixes" / "default");
    add_prefixes_in(roots, home / ".local/share/bottles/bottles");  // Bottles
    add_prefixes_in(roots, home / ".var/app/com.usebottles.bottles/data/bottles/bottles");
    add_steam(roots, home / ".steam" / "steam");
    add_steam(roots, home / ".local/share/Steam");
    add_steam(roots, home / ".var/app/com.valvesoftware.Steam/.local/share/Steam");
#endif
}

// ---- remembered folder (user_dir/smacx.cfg: key=value lines) ----
std::string config_path(const std::string& udir) { return udir + "smacx.cfg"; }

std::string config_get(const std::string& udir, const std::string& key) {
    std::ifstream f(config_path(udir));
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, key.size() + 1, key + "=") == 0) return line.substr(key.size() + 1);
    }
    return "";
}

void config_set(const std::string& udir, const std::string& key, const std::string& value) {
    std::vector<std::string> lines;
    {
        std::ifstream f(config_path(udir));
        std::string line;
        while (std::getline(f, line))
            if (line.compare(0, key.size() + 1, key + "=") != 0) lines.push_back(line);
    }
    lines.push_back(key + "=" + value);
    std::string text;
    for (const std::string& l : lines) text += l + "\n";
    write_file(config_path(udir), (const uint8_t*)text.data(), text.size());
}

// ---- asking the player ----
const char kTitle[] = "Sid Meier's Alpha Centauri";

// 1: the first button, 0: the second (or closed), -1: no message box (no display).
int ask_box(const std::string& text, const char* yes, const char* no) {
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, no},
    };
    SDL_MessageBoxData data = {SDL_MESSAGEBOX_INFORMATION, nullptr, kTitle, text.c_str(), 2, buttons, nullptr};
    int id = 0;
    if (SDL_ShowMessageBox(&data, &id) != 0) return -1;
    return id == 1 ? 1 : 0;
}

enum class Pick { picked, cancelled, unavailable };

#ifndef _WIN32
// Runs a picker command; its first output line is the folder. Exit status 1: cancelled.
Pick run_picker(const std::string& cmd, std::string* out) {
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) return Pick::unavailable;
    char buf[4096];
    std::string text;
    while (std::fgets(buf, sizeof(buf), p)) text += buf;
    int status = pclose(p);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (code == 0 && !text.empty()) {
        *out = text;
        return Pick::picked;
    }
    return code == 1 ? Pick::cancelled : Pick::unavailable;
}
#endif

// The platform's folder picker.
Pick native_pick(std::string* out) {
    const char prompt[] = "Choose the Alpha Centauri folder (the one with terranx.exe)";
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    BROWSEINFOA bi = {};
    bi.lpszTitle = prompt;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderA(&bi);
    if (!id) return Pick::cancelled;
    char path[MAX_PATH];
    bool ok = SHGetPathFromIDListA(id, path);
    CoTaskMemFree(id);
    if (!ok) return Pick::cancelled;
    *out = path;
    return Pick::picked;
#elif defined(__APPLE__)
    Pick r =
        run_picker(std::string("osascript -e 'POSIX path of (choose folder with prompt \"") + prompt + "\")'", out);
    return r;
#else
    if (env("DISPLAY").empty() && env("WAYLAND_DISPLAY").empty()) return Pick::unavailable;
    if (std::system("command -v zenity >/dev/null 2>&1") == 0)
        return run_picker(std::string("zenity --file-selection --directory --title=\"") + prompt + "\"", out);
    if (std::system("command -v kdialog >/dev/null 2>&1") == 0)
        return run_picker(std::string("kdialog --title \"") + prompt + "\" --getexistingdirectory \"$HOME\"", out);
    return Pick::unavailable;
#endif
}

// A path typed on the terminal ("" at the end of input or an empty line).
bool typed_path(std::string* out) {
    if (!SMACX_ISATTY(0)) return false;
    std::fprintf(stderr, "Enter the folder that contains terranx.exe (empty to quit): ");
    std::string line;
    if (!std::getline(std::cin, line)) return false;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    *out = line;
    return !line.empty();
}

std::string ask_game_dir(const std::string& reason) {
    std::string answer = env("SMACX_GAMEDIR");  // the answer given in advance (tests, scripts)
    if (!answer.empty()) {
        std::string why;
        if (check_game_dir(answer, &why)) return answer;
        show_error("SMACX_GAMEDIR: " + why);
        return "";
    }
    std::string text = reason;
    for (;;) {
        int box = ask_box(text +
                              "\n\nsmacx plays the game with the files of an installed copy of Sid Meier's "
                              "Alpha Centauri (v2.0 with Alien Crossfire, e.g. GOG's Planetary Pack). "
                              "Choose the folder that contains terranx.exe.",
                          "Choose folder...", "Quit");
        if (box == 0) return "";
        std::string dir;
        Pick pick = Pick::unavailable;
        if (box == 1) pick = native_pick(&dir);
        if (pick == Pick::cancelled) continue;
        if (pick == Pick::unavailable) {
            std::fprintf(stderr, "%s\n", text.c_str());
            if (!typed_path(&dir)) {
                std::string msg = text +
                                  "\n\nStart smacx with --gamedir FOLDER (or set SMACX_GAMEDIR) to give the folder "
                                  "that contains terranx.exe.";
                if (box == 1) ask_box(msg, "OK", "Quit");
                std::fprintf(stderr,
                             "smacx: no game folder: start smacx with --gamedir FOLDER (or set SMACX_GAMEDIR)\n");
                return "";
            }
        }
        std::string why;
        if (check_game_dir(dir, &why)) return dir;
        text = why;
    }
}

}  // namespace

std::string md5_file(const std::string& path) {
    std::vector<uint8_t> data;
    if (!read_file(path, &data)) return "";
    Md5 m;
    m.update(data.data(), data.size());
    return m.hex();
}

// gen_data_image.py, line for line: the sections of the region [BASE, END), their raw data without
// trailing zeros, split into runs of non-zero data separated by 64 or more zero bytes.
bool build_data_image(const std::vector<uint8_t>& d, std::vector<uint8_t>* image) {
    const uint32_t BASE = 0x682000, END = 0x9CA000;
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= d.size() ? d[o] | (d[o + 1] << 8) : 0; };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? u16(o) | (u16(o + 2) << 16) : 0; };
    image->clear();
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z') return false;
    size_t pe = u32(0x3C);
    if (pe + 24 > d.size() || std::memcmp(&d[pe], "PE\0\0", 4) != 0) return false;
    uint32_t nsec = u16(pe + 6), optsz = u16(pe + 20), image_base = u32(pe + 24 + 28);
    size_t sec = pe + 24 + optsz;
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) image->push_back((uint8_t)(v >> (8 * i)));
    };
    for (uint32_t s = 0; s < nsec; s++) {
        size_t h = sec + s * 40;
        if (h + 40 > d.size()) return false;
        uint32_t vsize = u32(h + 8), va = u32(h + 12), rsize = u32(h + 16), rptr = u32(h + 20);
        uint32_t lo = image_base + va;
        if (lo < BASE || lo >= END) continue;
        size_t begin = std::min<size_t>(rptr, d.size());
        size_t end = std::min<size_t>(begin + std::min(rsize, vsize), d.size());
        while (end > begin && d[end - 1] == 0) end--;  // rstrip(b"\0")
        const uint8_t* raw = d.data() + begin;
        size_t n = end - begin;
        size_t i0 = 0;
        while (i0 < n) {
            while (i0 < n && raw[i0] == 0) i0++;
            size_t j = i0, zeros = 0;
            while (j < n) {
                zeros = raw[j] == 0 ? zeros + 1 : 0;
                if (zeros >= 64) break;
                j++;
            }
            size_t stop = zeros ? j - zeros + 1 : j;
            if (stop > i0) {
                put32(lo + (uint32_t)i0);
                put32((uint32_t)(stop - i0));
                image->insert(image->end(), raw + i0, raw + stop);
            }
            i0 = j + 1;
        }
    }
    return true;
}

bool check_game_dir(const std::string& dir, std::string* why) {
    if (dir.empty() || !is_dir(dir)) {
        *why = "\"" + dir + "\" is not a folder.";
        return false;
    }
    std::string exe = find_ci(dir, "terranx.exe");
    if (exe.empty()) {
        *why = "\"" + dir + "\" does not contain terranx.exe (Alpha Centauri with Alien Crossfire).";
        return false;
    }
    std::string missing;
    for (const char* f :
         {"alpha.txt", "alphax.txt", "labels.txt", "script.txt", "texture.pcx", "palette.pcx", "xi.pcx"})
        if (find_ci(dir, f).empty()) missing += std::string(missing.empty() ? "" : ", ") + f;
    if (!missing.empty()) {
        *why = "\"" + dir + "\" has terranx.exe but not the game's data files (" + missing +
               "). Choose the folder of a complete installation.";
        return false;
    }
    std::string md5 = md5_file(exe);
    if (md5 != kExeMd5) {
        *why = "\"" + exe + "\" is not the supported version (md5 " + (md5.empty() ? "unreadable" : md5) +
               "). smacx needs terranx.exe v2.0 (md5 " + kExeMd5 +
               "), the version of GOG's Planetary Pack; update the game to v2.0 (Alien Crossfire 2.0 patch).";
        return false;
    }
    return true;
}

std::string user_dir() {
    char* p = SDL_GetPrefPath("", "smacx");
    if (!p) {
        std::fprintf(stderr, "smacx: no per-user directory: %s\n", SDL_GetError());
        return "";
    }
    std::string dir = p;
    SDL_free(p);
    return dir;
}

std::string find_game_dir(const std::string& udir) {
    std::string why, first_problem;
    std::string remembered = config_get(udir, "gamedir");
    if (!remembered.empty()) {
        if (check_game_dir(remembered, &why)) return remembered;
        std::fprintf(stderr, "smacx: the remembered game folder is no longer usable: %s\n", why.c_str());
        first_problem = why;
    }
    std::vector<fs::path> direct, roots;
    standard_locations(&direct, &roots);
    for (const fs::path& r : roots) {
        if (has_exe(r)) direct.push_back(r);
        for (const fs::path& c : subdirs(r))
            if (has_exe(c)) direct.push_back(c);
    }
    for (const fs::path& d : direct) {
        if (check_game_dir(d.string(), &why)) {
            std::fprintf(stderr, "smacx: game found in %s\n", d.string().c_str());
            config_set(udir, "gamedir", d.string());
            return d.string();
        }
        std::fprintf(stderr, "smacx: skipping %s: %s\n", d.string().c_str(), why.c_str());
        if (first_problem.empty()) first_problem = why;
    }
    std::string dir = ask_game_dir(
        first_problem.empty() ? "Alpha Centauri was not found in the usual install locations." : first_problem);
    if (dir.empty()) return "";
    dir = fs::absolute(dir).lexically_normal().string();
    config_set(udir, "gamedir", dir);
    return dir;
}

std::string data_image(const std::string& gamedir, const std::string& udir) {
    std::string path = udir + "data_image.bin";
    std::error_code ec;
    if (fs::file_size(path, ec) > 0 && !ec) return path;
    std::vector<uint8_t> exe, image;
    std::string exe_path = find_ci(gamedir, "terranx.exe");
    if (!read_file(exe_path, &exe) || !build_data_image(exe, &image) || image.empty()) {
        show_error("Cannot read the game's data from " + exe_path + ".");
        return "";
    }
    if (!write_file(path, image.data(), image.size())) {
        show_error("Cannot write " + path + ".");
        return "";
    }
    std::fprintf(stderr, "smacx: data image built from %s (%zu bytes, %s)\n", exe_path.c_str(), image.size(),
                 path.c_str());
    return path;
}

// A deliberate difference from the original: a player's new settings have "Sliding windows"
// (PREF_AV_SLIDING_WINDOWS, 0x8000000) off; Preferences can turn it on. The original's default
// preferences (prefs_load: default_prefs 0xBBE1DD96, the tutorial bit 0x20 below difficulty 2)
// are written with it cleared, under the current preferences format (12), so that prefs_load reads
// them; every other key keeps its default. An ini that already has preferences only loses the bit.
static void default_sliding_windows_off(const std::string& path) {
    char buf[256] = "";
    win32::GetPrivateProfileStringA("Alpha Centauri", "Prefs Format", "0", buf, sizeof(buf), path.c_str());
    uint32_t prefs = 0xBBE1DD96u | 0x20u;
    bool current = std::atoi(buf) == 12;
    if (current) {
        win32::GetPrivateProfileStringA("Alpha Centauri", "Preferences", "", buf, sizeof(buf), path.c_str());
        if (buf[0]) {
            prefs = 0;
            for (const char* c = buf; *c == '0' || *c == '1'; c++) prefs = prefs * 2 + (uint32_t)(*c - '0');
        }
    }
    prefs &= ~0x8000000u;
    std::string bits;
    bool started = false;
    for (int32_t bit = 31; bit >= 0; bit--) {  // prefs_save's str_binary
        if (prefs & (1u << bit)) {
            started = true;
            bits += '1';
        } else if (started || bit < 8) {
            bits += '0';
        }
    }
    if (!current) win32::WritePrivateProfileStringA("Alpha Centauri", "Prefs Format", "12", path.c_str());
    win32::WritePrivateProfileStringA("Alpha Centauri", "Preferences", bits.c_str(), path.c_str());
}

// smacx's own key: "Original Bugs" (0: the original's bugs fixed, the default; 1: played as in the
// original, engine/compat.h), written with its default when missing so that players find it.
static void original_bugs_key(const std::string& path) {
    char buf[16] = "";
    win32::GetPrivateProfileStringA("Alpha Centauri", "Original Bugs", "", buf, sizeof(buf), path.c_str());
    if (!buf[0]) win32::WritePrivateProfileStringA("Alpha Centauri", "Original Bugs", "0", path.c_str());
}

void seed_ini(const std::string& path, const std::string& udir) {
    std::error_code ec;
    // An ini from before this default loses "Sliding windows" once (smacx.cfg records it), so a
    // player who turns them on again keeps them.
    const char* kMigrated = "sliding_windows_off";
    if (fs::exists(path, ec)) {
        if (config_get(udir, kMigrated).empty()) {
            default_sliding_windows_off(path);
            config_set(udir, kMigrated, "1");
            std::fprintf(stderr, "smacx: \"Sliding windows\" turned off in %s (once)\n", path.c_str());
        }
        original_bugs_key(path);
        return;
    }
    bool copied = false;
    if (char* base = SDL_GetBasePath()) {
        std::string bundled = std::string(base) + "Alpha Centauri.Ini";
        SDL_free(base);
        copied = fs::copy_file(bundled, path, ec);
    }
    if (!copied) write_file(path, kDefaultIni, sizeof(kDefaultIni));
    default_sliding_windows_off(path);
    original_bugs_key(path);
    config_set(udir, kMigrated, "1");
}

void show_error(const std::string& text) {
    std::fprintf(stderr, "smacx: %s\n", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, text.c_str(), nullptr);
}

}  // namespace gamedata
