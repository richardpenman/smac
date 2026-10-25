// Native implementations of the MSVC runtime functions declared in crt.h.
#include "engine/crt.h"

#ifdef SMACX_HYBRID

// MSVC FILE: int _cnt at +4... _flag at +0xC; _IOEOF = 0x10.
bool crt_feof(CrtFile* file) { return (*((const uint8_t*)file + 0xC) & 0x10) != 0; }

#else

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "platform/win32.h"

// Read streams hold the whole file contents; write streams ("w"/"a" modes) write through a host
// FILE. Text-mode streams ("t" or no "b" in the mode) translate CR LF to LF and end at the first
// Ctrl-Z when reading, and write LF as CR LF, like the MSVC runtime.
struct CrtFile {
    std::vector<uint8_t> data;
    size_t pos = 0;
    bool text = true;
    bool eof = false;   // _IOEOF
    FILE* out = nullptr;  // write stream
};

void* GAMEAPI crt_malloc(uint32_t size) { return std::malloc(size ? size : 1); }
void* GAMEAPI crt_realloc(void* ptr, uint32_t size) { return std::realloc(ptr, size ? size : 1); }
void* GAMEAPI crt_new(uint32_t size) { return std::malloc(size ? size : 1); }
void GAMEAPI crt_exit(int32_t code) { std::exit(code); }

int32_t GAMEAPI crt_mkdir(const char* path) {
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    return platform::host_mkdir(p) ? 0 : -1;
}

CrtFile* GAMEAPI crt_fopen(const char* path, const char* mode) {
    if (!mode || std::strchr(mode, '+')) return nullptr;  // update modes are not needed
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    if (mode[0] == 'w' || mode[0] == 'a') {
        FILE* out = std::fopen(p, mode[0] == 'w' ? "wb" : "ab");
        if (!out) return nullptr;
        CrtFile* cf = new CrtFile;
        cf->text = std::strchr(mode, 'b') == nullptr;
        cf->out = out;
        return cf;
    }
    if (mode[0] != 'r') return nullptr;
    bool is_dir = false;
    if (!platform::host_stat(p, &is_dir, nullptr) || is_dir) return nullptr;
    FILE* f = std::fopen(p, "rb");
    if (!f) return nullptr;
    CrtFile* cf = new CrtFile;
    cf->text = std::strchr(mode, 'b') == nullptr;
    uint8_t chunk[8192];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) cf->data.insert(cf->data.end(), chunk, chunk + n);
    std::fclose(f);
    return cf;
}

int32_t GAMEAPI crt_fclose(CrtFile* file) {
    if (!file) return -1;
    int32_t r = 0;
    if (file->out && std::fclose(file->out) != 0) r = -1;
    delete file;
    return r;
}

// Next character of the stream, or -1 at end of file (sets the EOF flag).
static int crt_getc(CrtFile* f) {
    if (f->pos >= f->data.size()) {
        f->eof = true;
        return -1;
    }
    uint8_t c = f->data[f->pos];
    if (f->text) {
        if (c == 0x1A) {  // Ctrl-Z ends a text stream
            f->eof = true;
            return -1;
        }
        if (c == '\r' && f->pos + 1 < f->data.size() && f->data[f->pos + 1] == '\n') {
            f->pos += 2;
            return '\n';
        }
    }
    f->pos++;
    return c;
}

char* GAMEAPI crt_fgets(char* buf, int32_t size, CrtFile* file) {
    if (size <= 0) return nullptr;
    int32_t n = 0;
    while (n < size - 1) {
        int c = crt_getc(file);
        if (c < 0) {
            if (n == 0) return nullptr;
            break;
        }
        buf[n++] = (char)c;
        if (c == '\n') break;
    }
    buf[n] = 0;
    return buf;
}

void GAMEAPI crt_rewind(CrtFile* file) {
    file->pos = 0;
    file->eof = false;
}

int32_t GAMEAPI crt_fseek(CrtFile* file, int32_t offset, int32_t origin) {
    int64_t base = origin == 0 ? 0 : origin == 1 ? (int64_t)file->pos : (int64_t)file->data.size();
    int64_t pos = base + offset;
    if (pos < 0) return -1;
    file->pos = (size_t)pos;
    file->eof = false;
    return 0;
}

bool crt_feof(CrtFile* file) { return file->eof; }

int32_t GAMEAPI crt_fgetc(CrtFile* file) {
    if (file->out) return -1;
    return crt_getc(file);
}

uint32_t GAMEAPI crt_fread(void* buf, uint32_t size, uint32_t count, CrtFile* file) {
    uint64_t total = (uint64_t)size * count;
    if (!total || file->out) return 0;
    uint8_t* dst = (uint8_t*)buf;
    uint64_t n = 0;
    if (!file->text) {
        size_t avail = file->pos < file->data.size() ? file->data.size() - file->pos : 0;
        n = total < avail ? total : avail;
        if (n) std::memcpy(dst, file->data.data() + file->pos, (size_t)n);
        file->pos += (size_t)n;
        if (n < total) file->eof = true;
    } else {
        for (int c; n < total && (c = crt_getc(file)) >= 0;) dst[n++] = (uint8_t)c;
    }
    return (uint32_t)(n / size);
}

int32_t GAMEAPI crt_fputc(int32_t c, CrtFile* file) {
    if (!file->out) return -1;
    if (file->text && (uint8_t)c == '\n' && std::fputc('\r', file->out) < 0) return -1;
    return std::fputc((uint8_t)c, file->out) < 0 ? -1 : (uint8_t)c;
}

uint32_t GAMEAPI crt_fwrite(const void* buf, uint32_t size, uint32_t count, CrtFile* file) {
    uint64_t total = (uint64_t)size * count;
    if (!total || !file->out) return 0;
    const uint8_t* src = (const uint8_t*)buf;
    uint64_t n = 0;
    if (!file->text) {
        n = std::fwrite(src, 1, (size_t)total, file->out);
    } else {
        while (n < total && crt_fputc(src[n], file) >= 0) n++;
    }
    return (uint32_t)(n / size);
}

int32_t GAMEAPI crt_remove(const char* path) {
    char p[1024];
    platform::resolve_path(path, p, sizeof(p));
    bool is_dir = false;
    if (!platform::host_stat(p, &is_dir, nullptr) || is_dir) return -1;
    return std::remove(p) == 0 ? 0 : -1;
}

int32_t GAMEAPI crt_rename(const char* from, const char* to) {
    char a[1024], b[1024];
    platform::resolve_path(from, a, sizeof(a));
    platform::resolve_path(to, b, sizeof(b));
    // MoveFileA fails if the target exists
    if (!platform::host_stat(a, nullptr, nullptr) || platform::host_stat(b, nullptr, nullptr)) return -1;
    return std::rename(a, b) == 0 ? 0 : -1;
}

#endif
