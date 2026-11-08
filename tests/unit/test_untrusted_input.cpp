// Data from outside the game: a peer's synch blocks in a network game (engine/net_daemon.cpp,
// NetDaemon_synch) and the PBEM message lists of a saved game (engine/save.cpp,
// save_io_stringlist). The original indexes and sizes with what the message or the file says; a
// malformed or malicious one must not reach memory outside the tables (whatever the setting).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/memmap.h"
#include "engine/net.h"
#include "engine/save.h"
#include "engine/ui_bridge.h"
#include "unit.h"

namespace {
// Restores the compat setting after a test that plays with the original's bugs.
struct CompatScope {
    SaveGlobal<Compat> keep{g_compat};
    CompatScope() { g_compat = Compat{}; }
};

// A received synch message (0x2101 relayed as 0x1101): the header (+0x10 the block type, +0x12
// the chunk index, args 5..7 the ids) and one chunk of data at +0x24.
std::vector<int32_t> synch_message(int32_t type, int32_t id, int32_t a3, int16_t chunk, const void* data, size_t size) {
    std::vector<int32_t> m((0x24 + 0x800) / 4, 0);
    uint8_t* b = reinterpret_cast<uint8_t*>(m.data());
    *reinterpret_cast<uint16_t*>(b) = 0x1101;
    m[1] = 1;  // the sender's faction
    *reinterpret_cast<uint16_t*>(b + 0x10) = (uint16_t)type;
    *reinterpret_cast<int16_t*>(b + 0x12) = chunk;
    m[5] = id;
    m[6] = a3;
    std::memcpy(b + 0x24, data, size);
    return m;
}

void receive(std::vector<int32_t>& m) {
    NetDaemon_synch(&NetState, *reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(m.data()) + 0x10), m[5], m[6],
                    m[7], m.data(), 0, 0x2101);
}

int32_t& pending() { return *reinterpret_cast<int32_t*>(&NetState + 0x1BBC); }

std::string temp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("opensmac_unit_") + name)).string();
}
}  // namespace

TEST_CASE("untrusted: a synch block for a unit outside the table is dropped") {
    for (bool original : {false, true}) {
        CAPTURE(original);
        CompatScope scope;
        compat_set_original_bugs(original);
        SaveGlobal<int32_t> mp{MultiplayerActive};
        SaveGlobal<VEH[2049]> vehs{Vehs};
        SaveGlobal<int32_t> pend{pending()};
        MultiplayerActive = 1;
        // Vehs[2048] is the scratch slot past the 2048 units: a write there stays in the table, so
        // the test sees it without corrupting memory. The block's position matches the slot's (a
        // changed position is a resynch, not a copy).
        VEH& scratch = Vehs[MaxVehNum];
        std::memset(&scratch, 0, sizeof(scratch));
        scratch.x = 3;
        scratch.y = 5;
        VEH sent = scratch;
        sent.faction_id = 6;
        std::vector<int32_t> m = synch_message(0x11, MaxVehNum, 0, 0, &sent, 0x30);
        pending() = 1 << 1;
        receive(m);
        CHECK(Vehs[MaxVehNum].faction_id == 0);
        CHECK(pending() == 0);  // the sender's block is done: its caller stops waiting
        // A valid unit's block is still copied.
        Vehs[7] = scratch;
        m = synch_message(0x11, 7, 0, 0, &sent, 0x30);
        receive(m);
        CHECK(Vehs[7].faction_id == 6);
    }
}

TEST_CASE("untrusted: a synch block's faction, base, unit range and section are checked") {
    CompatScope scope;
    SaveGlobal<int32_t> mp{MultiplayerActive};
    SaveGlobal<int32_t> pend{pending()};
    MultiplayerActive = 1;
    const uint8_t zeros[0x48] = {};
    struct Case {
        int32_t type, id, a3;
    };
    for (Case c : {Case{6, 8, 0}, Case{6, -1, 0}, Case{0x16, 1, 8}, Case{0x13, MaxBaseNum, 0}, Case{0x23, -5, 0},
                   Case{0xF, 500, 0x40}, Case{0xF, 0, -1}, Case{0x10, 512, 0}, Case{0x30, 16, 0}}) {
        CAPTURE(c.type);
        CAPTURE(c.id);
        CAPTURE(c.a3);
        std::vector<int32_t> m = synch_message(c.type, c.id, c.a3, 0, zeros, sizeof(zeros));
        pending() = 1 << 1;
        receive(m);  // (under ASan the unchecked write is reported)
        CHECK(pending() == 0);
    }
}

TEST_CASE("untrusted: a synch chunk index outside the block is dropped") {
    CompatScope scope;
    compat_set_original_bugs(true);
    SaveGlobal<int32_t> mp{MultiplayerActive};
    SaveGlobal<int32_t> count{VehCount};
    SaveGlobal<VEH[2049]> vehs{Vehs};
    SaveGlobal<int32_t> pend{pending()};
    MultiplayerActive = 1;
    VehCount = 1;  // the units block (0x12) is one chunk of 0x34 bytes
    uint8_t* block = mem::host(0x952828);
    std::vector<uint8_t> chunk(0x800, 0xAB);
    for (int16_t index : {int16_t(3), int16_t(-2)}) {
        CAPTURE(index);
        // Chunk 3 would land 0x1800 bytes into the vehicle table (chunk -2 before it).
        std::vector<uint8_t> before(block + 0x1800, block + 0x2000);
        std::vector<int32_t> m = synch_message(0x12, 0, 0, index, chunk.data(), chunk.size());
        pending() = 1 << 1;
        receive(m);  // (chunk -2 is outside the table: ASan reports the original's write)
        CHECK(std::memcmp(before.data(), block + 0x1800, before.size()) == 0);
        CHECK(pending() == (1 << 1));  // not the block's last chunk: still waiting for it
    }
}

TEST_CASE("untrusted: a saved message list's text longer than the buffer fails the load") {
    for (bool original : {false, true}) {
        CAPTURE(original);
        CompatScope scope;
        compat_set_original_bugs(original);
        SaveGlobal<char[256]> buf{StrBuffer};
        SaveGlobal<char[10][256]> after{ParseStrBuffer};  // the memory image after StrBuffer
        std::string path = temp_file("stringlist.bin");
        auto write_list = [&](int32_t len, const std::string& text) {
            FILE* f = std::fopen(path.c_str(), "wb");
            REQUIRE(f);
            int32_t head[3] = {1, 42, len};  // one entry: id 42, len bytes of text
            std::fwrite(head, 4, 3, f);
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        };
        uint8_t* list = DiploTextLists[1];
        auto read_list = [&]() {
            CrtFile* in = crt_fopen(path.c_str(), "rb");
            REQUIRE(in);
            int32_t r = save_io_stringlist(list, 0, in);
            crt_fclose(in);
            return r;
        };
        // 0x4000 bytes: past StrBuffer, the original overwrites ParseStrBuffer and beyond.
        std::memset(ParseStrBuffer, 0, sizeof(ParseStrBuffer));
        write_list(0x4000, std::string(0x4000, 'X'));
        CHECK(read_list() == 1);
        CHECK(ParseStrBuffer[0][0] == 0);
        write_list(-1, "");
        CHECK(read_list() == 1);
        // The longest text the buffer holds still loads.
        write_list(255, std::string(255, 'Y'));
        CHECK(read_list() == 0);
        CHECK(ui_stringlist_count(list) == 1);
        int32_t id = 0;
        CHECK(std::string(ui_stringlist_entry(list, 0, &id)) == std::string(255, 'Y'));
        CHECK(id == 42);
        ui_stringlist_clear(list);
        std::remove(path.c_str());
    }
}
