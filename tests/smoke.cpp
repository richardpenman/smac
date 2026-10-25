// Portability smoke test: runs without game data (CI on every platform). Checks the target and
// compiler properties the faithful engine relies on, and the host-dependent native pieces
// (file system shim, MSVC CRT emulation, random generators).
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>

#include "engine/crt.h"
#include "engine/memmap.h"
#include "engine/random.h"
#include "platform/win32.h"

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static_assert(std::numeric_limits<char>::is_signed, "char must be signed (MSVC)");
static_assert(sizeof(int32_t) == 4 && sizeof(int16_t) == 2, "fixed-width integers");

// Opaque to the optimizer, so the checks below run on the target instead of being folded.
template <class T> T opaque(T v) {
    volatile T x = v;
    return x;
}

static void check_target() {
    uint32_t one = opaque<uint32_t>(1);
    uint8_t first;
    std::memcpy(&first, &one, 1);
    CHECK(first == 1);  // little-endian
    CHECK((uintptr_t)mem::g_image % 4096 == 0);
    // Integer semantics of the original (MSVC x86): wrapping, truncating division, arithmetic shift.
    int32_t max = opaque<int32_t>(INT32_MAX);
    CHECK(max + 1 == INT32_MIN);
    int32_t m7 = opaque<int32_t>(-7);
    CHECK(m7 / 2 == -3 && m7 % 2 == -1 && (m7 >> 1) == -4);
    CHECK((char)opaque<int32_t>(0xFF) == -1);
    // Floating point as base.cpp's share formula (x87 at 53-bit precision = IEEE double; no
    // fused multiply-add): bit pattern from the original's operation order.
    double chance = 1.0;
    const int32_t shares[] = {7, 18, 42};  // a fused multiply-add gives a different result
    for (int32_t s : shares) chance = (1.0 - (double)opaque<int32_t>(s) * 0.01) * chance;
    uint64_t bits;
    std::memcpy(&bits, &chance, sizeof(bits));
    CHECK(bits == 0x3FDC4EC636B09637ull);
    CHECK((int32_t)(chance * 100.0) == 44);
}

static void check_random() {
    crt_srand(opaque<uint32_t>(1));
    const int32_t msvc[] = {41, 18467, 6334, 26500, 19169};  // MSVC rand() after srand(1)
    for (int32_t v : msvc) CHECK(game_rand() == v);
    game_reseed(opaque<uint32_t>(12345));
    CHECK(game_random(0, 100) == 11 && game_random(100, 0) == 47);
    CHECK(game_random_state() == 0x043C7AD3u);
}

static void check_crt() {
    CHECK(crt_atoi("  -42x") == -42);
    CHECK(crt_stricmp("TerraNX", "terranx") == 0 && crt_stricmp("a", "B") < 0);
    CHECK(crt_toupper('q') == 'Q' && !crt_isupper(0xC0));
}

// File system shim: '\\' separators, case-insensitive lookup, MSVC text mode, .ini access.
static void check_files() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all("smoke_tmp", ec);
    CHECK(crt_mkdir("smoke_tmp") == 0);
    CHECK(crt_mkdir("smoke_tmp\\SubDir") == 0);
    CHECK(crt_mkdir("smoke_tmp\\SubDir") == -1);  // exists
    CrtFile* f = crt_fopen("smoke_tmp\\SubDir\\File.TXT", "wt");
    CHECK(f != nullptr);
    if (f) {
        CHECK(crt_fwrite("a\nb", 1, 3, f) == 3);
        crt_fclose(f);
    }
    uint32_t size = 0;
    uint8_t* raw = win32::read_file("SMOKE_TMP/subdir/file.txt", &size);
    CHECK(raw && size == 4 && std::memcmp(raw, "a\r\nb", 4) == 0);
    if (raw) win32::free_file(raw);
    f = crt_fopen("smoke_tmp/SUBDIR/FILE.txt", "rt");
    CHECK(f != nullptr);
    if (f) {
        char line[16] = {};
        CHECK(crt_fgets(line, sizeof(line), f) && std::strcmp(line, "a\n") == 0);
        crt_fclose(f);
    }
    CHECK(win32::file_exists("Smoke_Tmp\\subdir\\FILE.TXT"));
    CHECK(!win32::file_exists("smoke_tmp\\nothing"));
    CHECK(crt_rename("smoke_tmp\\subdir\\file.txt", "smoke_tmp\\SubDir\\Renamed.txt") == 0);
    CHECK(crt_remove("smoke_tmp\\subdir\\renamed.TXT") == 0);
    CHECK(crt_remove("smoke_tmp\\subdir") == -1);  // directories are not removed
    CHECK(win32::WritePrivateProfileStringA("Alpha Centauri", "Key", "Value", "smoke_tmp\\Test.ini"));
    char buf[32];
    CHECK(win32::GetPrivateProfileStringA("ALPHA CENTAURI", "key", "x", buf, sizeof(buf), "smoke_tmp/test.INI") == 5 &&
          std::strcmp(buf, "Value") == 0);
    CHECK(win32::GetPrivateProfileStringA("Alpha Centauri", "Missing", "Def  ", buf, sizeof(buf), "smoke_tmp/test.ini") == 3 &&
          std::strcmp(buf, "Def") == 0);
    char cwd[4096];
    CHECK(win32::GetCurrentDirectoryA(sizeof(cwd), cwd) > 0);
    fs::remove_all("smoke_tmp", ec);
}

int main() {
    check_target();
    check_random();
    check_crt();
    check_files();
    if (g_failures) {
        std::fprintf(stderr, "smacx_smoke: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("smacx_smoke: all checks passed\n");
    return 0;
}
