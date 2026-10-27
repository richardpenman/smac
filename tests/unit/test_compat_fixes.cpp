// The fixed paths of the original's bugs (engine/compat.h, docs/COMPAT.md). Each test sets the
// state a bug needs in the memory image, then checks the fix (the flag off) and, where running
// the original's path is harmless, the original's behaviour (the flag on). Bugs inside the large
// AI and turn functions, and the UI's (src/ui, not linked here), are covered by the harness with
// the original bugs and by the fixed-game runs (gate step "fixes").
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/compat.h"
#include "engine/compat_reads.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/game_state.h"
#include "engine/heap.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/save.h"
#include "engine/score.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/veh.h"
#include "unit.h"

namespace {
// The game plays with the fixes; a test switches single flags and gets every flag back after.
struct CompatScope {
    SaveGlobal<Compat> keep{g_compat};
    CompatScope() { g_compat = Compat{}; }
};

std::string temp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / (std::string("smacx_unit_") + name)).string();
}
}  // namespace

// ---- bugs at several sites: out_of_range_reads (the guarded readers), arm_val_unit_ids ----

TEST_CASE("compat out_of_range_reads: no vehicle, no base and no technology read as none") {
    CompatScope scope;
    SaveGlobal<VEH[2049]> vehs(Vehs);
    Vehs[3].unit_id = 17;
    Vehs[3].faction_id = 4;
    Vehs[3].x = 10;
    Vehs[3].y = 12;
    // In the table: the same in both settings.
    for (bool original : {false, true}) {
        g_compat.out_of_range_reads = original;
        CHECK(compat_veh_unit(3) == 17);
        CHECK(compat_veh_faction(3, 0xFF) == 4);
        CHECK(compat_veh_pos(3, 0, -1) == 10);
        CHECK(compat_veh_pos(3, 1, -1) == 12);
    }
    // Outside: the caller's none.
    g_compat.out_of_range_reads = false;
    CHECK(compat_veh_unit(-1) == -1);
    CHECK(compat_veh_unit(-1, -7) == -7);
    CHECK(compat_veh_faction(-1, 0xFF) == 0xFF);
    CHECK(compat_veh_faction(-1, 0) == 0);
    CHECK(compat_veh_pos(-1, 0, 0) == 0);
    CHECK(compat_base_faction(-1, 0xFF) == 0xFF);
    CHECK(compat_base_faction(512, 0xFF) == 0xFF);
    CHECK(compat_tech_owners(-1) == 0);
    CHECK(compat_tech_owners(97 + 70) == 0);
}

TEST_CASE("compat out_of_range_reads: Bases[-1] is the end of Factions[7] with the original's bug") {
    CompatScope scope;
    SaveGlobal<Faction[8]> factions(Factions);
    // Bases[-1].faction_id lies in the last 0x134 bytes of Factions[7].
    uint8_t* bases = reinterpret_cast<uint8_t*>(&game().factions[8]);  // Bases' address in the original
    bases[-(int32_t)sizeof(BASE) + offsetof(BASE, faction_id)] = 3;
    g_compat.out_of_range_reads = true;
    CHECK(compat_base_faction(-1, 0xFF) == 3);
    g_compat.out_of_range_reads = false;
    CHECK(compat_base_faction(-1, 0xFF) == 0xFF);
}

TEST_CASE("compat out_of_range_reads: veh_cost without a base has no Brood Pit") {
    CompatScope scope;
    SaveGlobal<Faction[8]> factions(Factions);
    SaveGlobal<UNIT[512]> units(Units);
    Units[15].cost = 8;  // the Spore Launcher, a native unit
    Units[15].plan = 0;
    // Bases[-1]'s Brood Pit (facility 0x23) bit, in the end of Factions[7].
    uint8_t* bases = reinterpret_cast<uint8_t*>(&game().factions[8]);  // Bases' address in the original
    bases[-(int32_t)sizeof(BASE) + offsetof(BASE, facilities_built) + 0x23 / 8] |= 1 << (0x23 & 7);
    CHECK(veh_cost(15, -1, nullptr) == 8);
    g_compat.out_of_range_reads = true;
    CHECK(veh_cost(15, -1, nullptr) == 6);  // three quarters, as with a Brood Pit
}

TEST_CASE("compat out_of_range_reads: battle_fight without a unit is no battle") {
    CompatScope scope;
    CHECK(battle_fight(-1, 0, 0, 0, nullptr) == 0);
}

TEST_CASE("compat arm_val_unit_ids: armour ids past the table give 0 with the fix") {
    CompatScope scope;
    SaveGlobal<int32_t> expansion(ExpansionEnabled);
    ExpansionEnabled = 1;
    CHECK(arm_val(20, -1) == 0);  // a unit id taken for an armour id
    CHECK(arm_val(200, -1) == 0);
}

// ---- tech, strings, parse, heap, map ----

TEST_CASE("compat last_tech_never_counts: technology 88 can be owned") {
    CompatScope scope;
    SaveGlobal<uint8_t[89]> owners(TechOwners);
    TechOwners[88] = 1 << 2;
    CHECK(has_tech(88, 2) == 1);
    CHECK(has_tech(88, 3) == 0);
    g_compat.last_tech_never_counts = true;
    CHECK(has_tech(88, 2) == 0);
}

TEST_CASE("compat purge_leading_noop: the leading spaces are removed") {
    CompatScope scope;
    char text[32] = "   Gaia's Landing";
    purge_leading(text);
    CHECK(std::string(text) == "Gaia's Landing");
    char untouched[32] = "Gaia";
    purge_leading(untouched);
    CHECK(std::string(untouched) == "Gaia");
    g_compat.purge_leading_noop = true;
    char kept[32] = "  Gaia";
    purge_leading(kept);
    CHECK(std::string(kept) == "  Gaia");
}

TEST_CASE("compat parse_marker_no_digit: a marker without a slot digit is copied, no hang") {
    CompatScope scope;
    char out[128] = {};
    CHECK(parse_string("cost $X now", out) == 0);
    CHECK(std::string(out) == "cost $X now");
}

TEST_CASE("compat heap_realloc_moves: a heap grows without moving its strings") {
    CompatScope scope;
    Heap heap;
    Heap_ctor(&heap);
    REQUIRE(Heap_init(&heap, 16) == 0);
    char* first = static_cast<char*>(Heap_get(&heap, 8));
    std::strcpy(first, "Planet");
    uint8_t* base = heap.base;
    for (int32_t i = 0; i < 64; i++) REQUIRE(Heap_get(&heap, 0x40) != nullptr);  // grows past 16 bytes
    CHECK(heap.base == base);
    CHECK(std::string(first) == "Planet");
    CHECK(heap.base_size >= 16 + 64 * 0x40);
    Heap_squeeze(&heap, 0);
    CHECK(heap.base == base);
    CHECK(heap.free_size == 0);
    Heap_free(&heap);
}

TEST_CASE("compat clear_dirty_sets: clear_dirty clears the tiles' redraw flag") {
    CompatScope scope;
    MapDims dims(4, 4, true);
    SaveGlobal<MAP*> tiles(game().map.tiles);
    std::vector<MAP> map(8);
    game().map.tiles = map.data();
    for (MAP& sq : map) sq.landmarks = 0x400000 | 2;
    clear_dirty();
    for (const MAP& sq : map) CHECK(sq.landmarks == 2);
    g_compat.clear_dirty_sets = true;
    clear_dirty();
    for (const MAP& sq : map) CHECK(sq.landmarks == (0x400000u | 2));
}

// ---- score ----

TEST_CASE("compat ascent_queue_sign: a base building the Ascent is transcending") {
    CompatScope scope;
    SaveGlobal<int32_t[64]> projects(SecretProjects);
    SaveGlobal<BASE[512]> bases(Bases);
    SaveGlobal<int32_t> count(BaseCount);
    SecretProjects[FAC_VOICE_OF_PLANET - 70] = 0;
    BaseCount = 1;
    Bases[0].faction_id = 2;
    Bases[0].queue_items[0] = -FAC_ASCENT_TO_TRANSCENDENCE;
    CHECK(transcending(2) == 1);
    CHECK(transcending(3) == 0);
    g_compat.ascent_queue_sign = true;
    CHECK(transcending(2) == 0);
}

TEST_CASE("compat replay_events_overflow: no event is recorded past the table") {
    CompatScope scope;
    SaveGlobal<int32_t> size(ReplayEventSize);
    SaveGlobal<ReplayEvent[1024]> events(ReplayEvents);
    ReplayEventSize = 0;
    replay_base(1, 5, 6, 2);
    CHECK(ReplayEventSize == 8);
    ReplayEventSize = (int32_t)sizeof(ReplayEvents);
    replay_base(1, 5, 6, 2);
    CHECK(ReplayEventSize == (int32_t)sizeof(ReplayEvents));
}

// ---- base ----

TEST_CASE("compat residue.base_list_frame: base_rank of a negative position is no base") {
    CompatScope scope;
    CHECK(base_rank(1, -1) == -1);
}

// ---- design, save ----

TEST_CASE("compat read_name_unterminated: a header of 256 characters or more is cut at 255") {
    CompatScope scope;
    std::string path = temp_file("header.bin");
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f);
        std::string text(300, 'A');
        std::fwrite(text.data(), 1, text.size(), f);
        std::fputc(0, f);
        std::fputc(0x1A, f);
        std::fclose(f);
    }
    char header[256 + 16];
    std::memset(header, 0x55, sizeof(header));
    CrtFile* file = crt_fopen(path.c_str(), "rb");
    REQUIRE(file);
    header_check(header, file);
    crt_fclose(file);
    CHECK(header[255] == 0);
    CHECK(std::strlen(header) == 255);
    std::remove(path.c_str());
}

TEST_CASE("compat encrypt_odd_size: an odd-sized block encrypts without reading past its end") {
    CompatScope scope;
    std::string path = temp_file("encrypt.bin");
    // The byte after the block is not 0: the original would encrypt it as the padding byte.
    const uint8_t data[4] = {0x12, 0x34, 0x56, 0x9A};
    CrtFile* out = crt_fopen(path.c_str(), "wb");
    REQUIRE(out);
    CHECK(encrypt_write(data, 3, 1, out) == 1);
    crt_fclose(out);
    uint8_t raw[4] = {};
    FILE* f = std::fopen(path.c_str(), "rb");
    REQUIRE(f);
    CHECK(std::fread(raw, 1, 4, f) == 4);
    std::fclose(f);
    CHECK(raw[2] == 0);  // the padding byte of the last pair (stored first)
    uint8_t back[4] = {};
    CrtFile* in = crt_fopen(path.c_str(), "rb");
    REQUIRE(in);
    CHECK(encrypt_read(back, 3, 1, in) == 1);
    crt_fclose(in);
    CHECK(std::memcmp(back, data, 3) == 0);
    std::remove(path.c_str());
}

// ---- the setting ----

TEST_CASE("compat: the save mark of the setting") {
    CHECK(kSmacxSaveMark == 0x42584D53u);  // "SMXB"
    uint32_t mark;
    std::memcpy(&mark, "SMXB", 4);
    CHECK(mark == kSmacxSaveMark);
}
