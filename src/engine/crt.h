// MSVC C runtime functions used by engine code (original module crt, 0x644EF2-0x669000).
//
// Functions whose state lives outside the memory image (heap blocks, FILE objects, the file
// system) are ADDR_NATIVE: hybrid builds call the original CRT, so heap blocks and FILE objects
// can be shared with original code. Native builds implement them with MSVC semantics:
// crt_fopen resolves game file names case-insensitively, and text-mode streams translate CR LF
// to LF and stop at Ctrl-Z when reading (and write LF as CR LF), as the MSVC runtime does.
//
// Pure string/character functions are reimplemented inline (no ADDR): they behave as the
// original's "C" locale code paths.
#pragma once
#include <cstdint>

#include "engine/memmap.h"

// Opaque CRT stream (MSVC FILE in hybrid builds).
struct CrtFile;

ADDR_NATIVE(0x6470A6) void* GAMEAPI crt_malloc(uint32_t size);
ADDR_NATIVE(0x647132) void* GAMEAPI crt_realloc(void* ptr, uint32_t size);
// operator new.
ADDR_NATIVE(0x64558A) void* GAMEAPI crt_new(uint32_t size);
// exit() (0x644DFF): terminates the process.
ADDR_NATIVE(0x644DFF) void GAMEAPI crt_exit(int32_t code);
// _mkdir: 0 on success, -1 on failure.
ADDR_NATIVE(0x646829) int32_t GAMEAPI crt_mkdir(const char* path);

ADDR_NATIVE(0x645646) CrtFile* GAMEAPI crt_fopen(const char* path, const char* mode);
ADDR_NATIVE(0x645598) int32_t GAMEAPI crt_fclose(CrtFile* file);
ADDR_NATIVE(0x64726A) char* GAMEAPI crt_fgets(char* buf, int32_t size, CrtFile* file);
ADDR_NATIVE(0x6472CC) void GAMEAPI crt_rewind(CrtFile* file);
ADDR_NATIVE(0x647330) int32_t GAMEAPI crt_fseek(CrtFile* file, int32_t offset, int32_t origin);
// fread/fwrite: number of complete items transferred.
ADDR_NATIVE(0x646178) uint32_t GAMEAPI crt_fread(void* buf, uint32_t size, uint32_t count, CrtFile* file);
ADDR_NATIVE(0x64603F) uint32_t GAMEAPI crt_fwrite(const void* buf, uint32_t size, uint32_t count, CrtFile* file);
// fgetc/fputc: the byte (0-255), or -1 (EOF) at end of file / on error.
ADDR_NATIVE(0x646855) int32_t GAMEAPI crt_fgetc(CrtFile* file);
ADDR_NATIVE(0x646886) int32_t GAMEAPI crt_fputc(int32_t c, CrtFile* file);
// remove (DeleteFileA) and rename (MoveFileA: fails if the target exists): 0 on success, -1 on error.
ADDR_NATIVE(0x646950) int32_t GAMEAPI crt_remove(const char* path);
ADDR_NATIVE(0x6469F7) int32_t GAMEAPI crt_rename(const char* from, const char* to);
// feof(): the original tests the stream's _IOEOF flag inline (FILE._flag & 0x10).
bool crt_feof(CrtFile* file);

// ---- string and character functions ("C" locale) ----
// isupper/isdigit/isxdigit/toupper as the CRT ctype table (0x6A61A0) for ASCII; other bytes are
// not classified.
inline int32_t crt_isupper(int32_t c) { return c >= 'A' && c <= 'Z'; }
inline int32_t crt_isdigit(int32_t c) { return c >= '0' && c <= '9'; }
inline int32_t crt_isxdigit(int32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
inline int32_t crt_isspace(int32_t c) { return c == ' ' || (c >= 9 && c <= 13); }
inline int32_t crt_toupper(int32_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

inline uint32_t crt_strlen(const char* s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
inline char* crt_strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}
inline char* crt_strcat(char* dst, const char* src) {
    crt_strcpy(dst + crt_strlen(dst), src);
    return dst;
}
// strncpy: copies at most n characters and pads with NULs; no terminator if src is longer.
inline char* crt_strncpy(char* dst, const char* src, uint32_t n) {
    uint32_t i = 0;
    for (; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
inline void* crt_memcpy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d > s && d < s + n) {
        while (n--) d[n] = s[n];  // memmove semantics, as the original's memcpy
    } else {
        for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    }
    return dst;
}
inline const void* crt_memchr(const void* p, int32_t c, uint32_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (uint32_t i = 0; i < n; i++)
        if (b[i] == (uint8_t)c) return b + i;
    return nullptr;
}
inline int32_t crt_strcmp(const char* a, const char* b) {
    const uint8_t *x = (const uint8_t*)a, *y = (const uint8_t*)b;
    while (*x && *x == *y) x++, y++;
    return *x < *y ? -1 : *x > *y ? 1 : 0;
}
inline int32_t crt_strncmp(const char* a, const char* b, uint32_t n) {
    const uint8_t *x = (const uint8_t*)a, *y = (const uint8_t*)b;
    for (; n; n--, x++, y++) {
        if (*x != *y) return *x < *y ? -1 : 1;
        if (!*x) break;
    }
    return 0;
}
// _stricmp (0x64FD20): ASCII case folding to lower case; returns -1, 0 or 1.
inline int32_t crt_stricmp(const char* a, const char* b) {
    const uint8_t *x = (const uint8_t*)a, *y = (const uint8_t*)b;
    for (;; x++, y++) {
        uint8_t c1 = *x, c2 = *y;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 < c2 ? -1 : 1;
        if (!c1) return 0;
    }
}
// _strnicmp (0x64FDF0).
inline int32_t crt_strnicmp(const char* a, const char* b, uint32_t n) {
    const uint8_t *x = (const uint8_t*)a, *y = (const uint8_t*)b;
    for (; n; n--, x++, y++) {
        uint8_t c1 = *x, c2 = *y;
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        if (c1 != c2) return c1 < c2 ? -1 : 1;
        if (!c1) break;
    }
    return 0;
}
inline char* crt_strchr(const char* s, int32_t c) {
    for (;; s++) {
        if (*s == (char)c) return (char*)s;
        if (!*s) return nullptr;
    }
}
inline char* crt_strrchr(const char* s, int32_t c) {
    const char* r = nullptr;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return (char*)r;
    }
}
inline char* crt_strstr(const char* s, const char* sub) {
    if (!*sub) return (char*)s;
    for (; *s; s++) {
        const char *a = s, *b = sub;
        while (*a && *b && *a == *b) a++, b++;
        if (!*b) return (char*)s;
    }
    return nullptr;
}
// atoi (0x645DA7): skips white space, optional sign, decimal digits; overflow wraps.
inline int32_t crt_atoi(const char* s) {
    const uint8_t* p = (const uint8_t*)s;
    while (crt_isspace(*p)) p++;
    uint8_t sign = *p;
    if (sign == '-' || sign == '+') p++;
    uint32_t total = 0;
    while (crt_isdigit(*p)) total = total * 10 + (uint32_t)(*p++ - '0');
    return sign == '-' ? (int32_t)(0u - total) : (int32_t)total;
}
// _itoa (0x64FC88): negative values get a '-' only in radix 10.
inline char* crt_itoa(int32_t value, char* buf, int32_t radix) {
    char tmp[40];
    uint32_t v = (uint32_t)value;
    char* out = buf;
    if (radix == 10 && value < 0) {
        *out++ = '-';
        v = 0u - v;
    }
    int n = 0;
    do {
        uint32_t d = v % (uint32_t)radix;
        tmp[n++] = (char)(d > 9 ? d - 10 + 'a' : d + '0');
        v /= (uint32_t)radix;
    } while (v);
    while (n) *out++ = tmp[--n];
    *out = 0;
    return buf;
}
