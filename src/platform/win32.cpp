// Win32 API functions used by engine code (see win32.h).
#include "platform/win32.h"

#include <cstdio>
#include <cstring>

#ifdef SMACX_HYBRID
#include <windows.h>

namespace win32 {
uint32_t GetPrivateProfileStringA(const char* section, const char* key, const char* def, char* buf,
                                  uint32_t size, const char* file) {
    return ::GetPrivateProfileStringA(section, key, def, buf, size, file);
}
int32_t WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file) {
    return ::WritePrivateProfileStringA(section, key, value, file);
}
uint32_t GetCurrentDirectoryA(uint32_t size, char* buf) { return ::GetCurrentDirectoryA(size, buf); }
int32_t SetCurrentDirectoryA(const char* path) { return ::SetCurrentDirectoryA(path); }
uint32_t GetDriveTypeA(const char* root) { return ::GetDriveTypeA(root); }
bool file_exists(const char* path) {
    WIN32_FIND_DATAA data;
    HANDLE h = ::FindFirstFileA(path, &data);
    ::FindClose(h);  // the original also calls FindClose on INVALID_HANDLE_VALUE
    return h != INVALID_HANDLE_VALUE;
}
int32_t MessageBoxA(const char* text, const char* caption, uint32_t type) {
    return ::MessageBoxA(nullptr, text, caption, type);
}
uint8_t* read_file(const char* path, uint32_t* size) {
    HANDLE h = ::CreateFileA(path, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    DWORD n = ::GetFileSize(h, nullptr), got = 0;
    uint8_t* data = (uint8_t*)::HeapAlloc(::GetProcessHeap(), 0, n ? n : 1);
    if (data && !::ReadFile(h, data, n, &got, nullptr)) got = 0;
    ::CloseHandle(h);
    if (data && got != n) {
        ::HeapFree(::GetProcessHeap(), 0, data);
        return nullptr;
    }
    *size = n;
    return data;
}
void free_file(uint8_t* data) { ::HeapFree(::GetProcessHeap(), 0, data); }
void CharUpperA(char* str) { ::CharUpperA(str); }
}  // namespace win32

namespace platform {
const char* resolve_path(const char* path, char* out, uint32_t out_size) {
    std::snprintf(out, out_size, "%s", path);
    return out;
}
}  // namespace platform

#else  // native

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return false;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) e--;
    return s.substr(b, e - b);
}

// Splits an ini file into lines (CR/LF stripped).
bool read_lines(const char* file, std::vector<std::string>& lines) {
    char path[1024];
    platform::resolve_path(file, path, sizeof(path));
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::string data;
    char chunk[4096];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) data.append(chunk, n);
    std::fclose(f);
    size_t pos = 0;
    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        std::string line = data.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return true;
}

bool is_section(const std::string& line, std::string* name) {
    std::string t = trim(line);
    if (t.size() < 2 || t[0] != '[') return false;
    size_t e = t.find(']');
    if (e == std::string::npos) e = t.size();
    if (name) *name = trim(t.substr(1, e - 1));
    return true;
}

bool is_key(const std::string& line, const char* key, std::string* value) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    if (!ieq(trim(line.substr(0, eq)), key)) return false;
    if (value) *value = trim(line.substr(eq + 1));
    return true;
}

}  // namespace

namespace platform {
const char* resolve_path(const char* path, char* out, uint32_t out_size) {
    std::string p(path);
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string result;
    size_t pos = 0;
    if (!p.empty() && p[0] == '/') {
        result = "/";
        pos = 1;
    }
    bool ok = true;
    while (pos <= p.size()) {
        size_t e = p.find('/', pos);
        std::string comp = p.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!comp.empty()) {
            std::string dir = result.empty() ? "." : result;
            std::string cand = (result.empty() || result.back() == '/' ? result : result + "/") + comp;
            struct stat st;
            if (ok && comp != "." && comp != ".." && stat(cand.c_str(), &st) != 0) {
                // look for a case-insensitive match in dir
                bool found = false;
                if (DIR* d = opendir(dir.c_str())) {
                    while (struct dirent* de = readdir(d)) {
                        if (ieq(de->d_name, comp)) {
                            cand = (result.empty() || result.back() == '/' ? result : result + "/") + de->d_name;
                            found = true;
                            break;
                        }
                    }
                    closedir(d);
                }
                if (!found) ok = false;
            }
            result = cand;
        }
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    std::snprintf(out, out_size, "%s", result.c_str());
    return out;
}
}  // namespace platform

namespace win32 {

uint32_t GetPrivateProfileStringA(const char* section, const char* key, const char* def, char* buf,
                                  uint32_t size, const char* file) {
    if (!buf || !size) return 0;
    std::vector<std::string> lines;
    std::string value;
    bool found = false;
    if (section && key && read_lines(file, lines)) {
        bool in = false;
        for (const std::string& line : lines) {
            std::string name;
            if (is_section(line, &name)) {
                in = ieq(name, section);
                continue;
            }
            if (in && is_key(line, key, &value)) {
                found = true;
                break;
            }
        }
    }
    if (found) {
        // Windows strips one pair of matching quotes around the value
        if (value.size() >= 2 && (value[0] == '"' || value[0] == '\'') && value.back() == value[0])
            value = value.substr(1, value.size() - 2);
    } else {
        value = def ? def : "";
        while (!value.empty() && value.back() == ' ') value.pop_back();  // trailing blanks of the default
    }
    uint32_t n = (uint32_t)value.size();
    if (n > size - 1) n = size - 1;
    std::memcpy(buf, value.data(), n);
    buf[n] = 0;
    return n;
}

int32_t WritePrivateProfileStringA(const char* section, const char* key, const char* value, const char* file) {
    if (!section) return 0;
    std::vector<std::string> lines;
    read_lines(file, lines);
    // locate the section
    size_t start = lines.size(), end = lines.size();
    for (size_t i = 0; i < lines.size(); i++) {
        std::string name;
        if (is_section(lines[i], &name)) {
            if (start != lines.size()) {
                end = i;
                break;
            }
            if (ieq(name, section)) start = i;
        }
    }
    if (start == lines.size()) {
        if (!key || !value) return 1;
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back(std::string("[") + section + "]");
        lines.push_back(std::string(key) + "=" + value);
    } else if (!key) {
        lines.erase(lines.begin() + start, lines.begin() + end);
    } else {
        size_t at = end, last = start;
        for (size_t i = start + 1; i < end; i++) {
            if (is_key(lines[i], key, nullptr)) {
                at = i;
                break;
            }
            if (!trim(lines[i]).empty()) last = i;
        }
        if (at != end) {
            if (value)
                lines[at] = std::string(key) + "=" + value;
            else
                lines.erase(lines.begin() + at);
        } else if (value) {
            lines.insert(lines.begin() + last + 1, std::string(key) + "=" + value);
        }
    }
    char path[1024];
    platform::resolve_path(file, path, sizeof(path));
    FILE* f = std::fopen(path, "wb");
    if (!f) return 0;
    for (const std::string& l : lines) std::fprintf(f, "%s\r\n", l.c_str());
    std::fclose(f);
    return 1;
}

uint32_t GetCurrentDirectoryA(uint32_t size, char* buf) {
    char tmp[4096];
    if (!getcwd(tmp, sizeof(tmp))) return 0;
    uint32_t n = (uint32_t)std::strlen(tmp);
    if (n + 1 > size) return n + 1;
    std::memcpy(buf, tmp, n + 1);
    return n;
}

int32_t SetCurrentDirectoryA(const char* path) {
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    return chdir(p) == 0;
}

uint32_t GetDriveTypeA(const char*) { return 1; /* DRIVE_NO_ROOT_DIR */ }

bool file_exists(const char* path) {
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    struct stat st;
    return stat(p, &st) == 0;
}

uint8_t* read_file(const char* path, uint32_t* size) {
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    struct stat st;
    if (stat(p, &st) != 0 || S_ISDIR(st.st_mode)) return nullptr;
    FILE* f = std::fopen(p, "rb");
    if (!f) return nullptr;
    uint8_t* data = (uint8_t*)std::malloc(st.st_size ? (size_t)st.st_size : 1);
    size_t got = std::fread(data, 1, (size_t)st.st_size, f);
    std::fclose(f);
    if (got != (size_t)st.st_size) {
        std::free(data);
        return nullptr;
    }
    *size = (uint32_t)st.st_size;
    return data;
}

void free_file(uint8_t* data) { std::free(data); }

int32_t MessageBoxA(const char* text, const char* caption, uint32_t) {
    std::fprintf(stderr, "%s: %s\n", caption ? caption : "", text ? text : "");
    return 1;
}

void CharUpperA(char* str) {
    for (unsigned char* p = (unsigned char*)str; *p; p++) {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 32;
        else if (c == 0x9A || c == 0x9C || c == 0x9E) c -= 0x10;
        else if (c == 0xFF) c = 0x9F;
        *p = c;
    }
}

}  // namespace win32

#endif
