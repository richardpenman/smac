// The serialisation of the game-state records (engine/state_io.h): each record's field list must
// produce exactly the original's byte layout, which the packed layout structs still have. A
// field left out, listed twice or out of order changes the bytes and fails here.
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "engine/state_io.h"
#include "unit.h"

namespace {

std::vector<uint8_t> random_bytes(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t x = seed * 2654435761u + 1;
    for (auto& b : v) {
        x = x * 1664525u + 1013904223u;
        b = static_cast<uint8_t>(x >> 24);
    }
    return v;
}

// Writing a record gives its memory bytes; reading bytes gives a record with those memory bytes.
template <class T>
void check_record(const char* name, size_t original_size) {
    INFO(name);
    CHECK(sizeof(T) == original_size);
    for (uint32_t seed = 1; seed <= 3; seed++) {
        auto record = std::make_unique<T>();
        std::vector<uint8_t> bytes = random_bytes(sizeof(T), seed);
        std::memcpy(record.get(), bytes.data(), sizeof(T));
        Writer w;
        w(*record);
        REQUIRE(w.size() == sizeof(T));
        CHECK(std::memcmp(w.data(), bytes.data(), sizeof(T)) == 0);

        std::vector<uint8_t> other = random_bytes(sizeof(T), seed + 100);
        Reader r(other.data(), other.size());
        r(*record);
        CHECK(r.ok());
        CHECK(r.pos() == sizeof(T));
        CHECK(std::memcmp(record.get(), other.data(), sizeof(T)) == 0);
    }
}

}  // namespace

TEST_CASE("serialize: every record writes and reads the original's byte layout") {
    check_record<GamePrefs>("GamePrefs", 0x398);
    check_record<Faction>("Faction", 0x20CC);
    check_record<MFaction>("MFaction", 0x59C);
    check_record<MapHeader>("MapHeader", 0xAA4);
    check_record<MAP>("MAP", 0x2C);
    check_record<Continent>("Continent", 0x1C);
    check_record<UNIT>("UNIT", 0x34);
    check_record<VEH>("VEH", 0x34);
    check_record<BASE>("BASE", 0x134);
    check_record<ReplayEvent>("ReplayEvent", 8);
    check_record<Monument>("Monument", 0x4F4);
    check_record<ScenarioRules>("ScenarioRules", 0x98);
    check_record<PbemState>("PbemState", 0x4C);
}

TEST_CASE("serialize: integers are little-endian in their own width") {
    Writer w;
    int16_t a = -2;
    uint32_t b = 0x11223344;
    int8_t c = -1;
    w(a);
    w(b);
    w(c);
    const uint8_t expect[] = {0xFE, 0xFF, 0x44, 0x33, 0x22, 0x11, 0xFF};
    REQUIRE(w.size() == sizeof(expect));
    CHECK(std::memcmp(w.data(), expect, sizeof(expect)) == 0);

    int16_t a2 = 0;
    uint32_t b2 = 0;
    int8_t c2 = 0;
    Reader r(expect, sizeof(expect));
    r(a2);
    r(b2);
    r(c2);
    CHECK(a2 == -2);
    CHECK(b2 == 0x11223344);
    CHECK(c2 == -1);
}

TEST_CASE("serialize: a short read leaves the rest of the record unchanged") {
    VEH v{};
    v.prev_veh_id_stack = 1234;
    v.next_veh_id_stack = 77;
    std::vector<uint8_t> bytes(0x30, 0);  // the record without its last two fields
    CHECK_FALSE(read_records(&v, 1, bytes.data(), bytes.size()));
    CHECK(v.x == 0);
    CHECK(v.next_veh_id_stack == 77);
    CHECK(v.prev_veh_id_stack == 1234);
}

TEST_CASE("serialize: the snapshot regions have the sizes of harness/state_regions.h") {
    // state_regions_write aborts when a fixed region serialises to another size; the variable
    // regions follow their counts.
    SaveGlobal<int32_t> vc{VehCount}, bc{BaseCount}, rs{ReplayEventSize};
    VehCount = 3;
    BaseCount = 2;
    ReplayEventSize = 16;
    std::vector<std::pair<uint32_t, uint32_t>> seen;
    state_regions_write([&](uint32_t addr, const uint8_t*, uint32_t size) { seen.emplace_back(addr, size); });
    auto size_of = [&](uint32_t addr) {
        for (auto& s : seen)
            if (s.first == addr) return s.second;
        return 0xFFFFFFFFu;
    };
    CHECK(size_of(0x9A6490) == 0x398);  // GamePrefs
    CHECK(size_of(0x96C9E0) == 0x10660);  // Factions
    CHECK(size_of(0x949870) == 0xAA4);  // MapHeader
    CHECK(size_of(0x952828) == 3 * 0x34);  // Vehs
    CHECK(size_of(0x97D040) == 2 * 0x134);  // Bases
    CHECK(size_of(0x950324) == 16);  // ReplayEvents
    CHECK(size_of(0x93A9AC) == 0x4C);  // PbemState
}
