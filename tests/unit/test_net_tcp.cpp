// smacx's network transport (platform/net_tcp): the frame codec, the host's routing, the player
// list codec, and a host with two clients on localhost.
#include <chrono>
#include <thread>

#include "platform/net_tcp.h"
#include "unit.h"

using namespace nettcp;

TEST_CASE("net_tcp: frames survive any split of the stream") {
    std::vector<uint8_t> stream;
    const char a[] = "hello";
    std::vector<uint8_t> big(5000);
    for (size_t i = 0; i < big.size(); i++) big[i] = (uint8_t)(i * 7);
    encode_frame(stream, kData, 2, 0, a, 5);
    encode_frame(stream, kData, 1, 3, big.data(), (uint32_t)big.size());
    encode_frame(stream, kLeft, 4, 0, nullptr, 0);
    CHECK(stream.size() == 3 * kHeaderSize + 5 + 5000);
    for (size_t chunk : {1u, 3u, 16u, 17u, 1000u, 100000u}) {
        FrameReader r;
        std::vector<Frame> got;
        for (size_t pos = 0; pos < stream.size(); pos += chunk) {
            r.feed(stream.data() + pos, std::min(chunk, stream.size() - pos));
            Frame f;
            while (r.pop(f)) got.push_back(f);
        }
        REQUIRE(got.size() == 3);
        CHECK(got[0].head.kind == kData);
        CHECK(got[0].head.from == 2);
        CHECK(got[0].head.to == 0);
        CHECK(std::string(got[0].payload.begin(), got[0].payload.end()) == "hello");
        CHECK(got[1].head.to == 3);
        CHECK(got[1].payload == big);
        CHECK(got[2].head.kind == kLeft);
        CHECK(got[2].payload.empty());
        CHECK(r.buffered() == 0);
        CHECK_FALSE(r.error());
    }
}

TEST_CASE("net_tcp: an oversized frame is an error") {
    std::vector<uint8_t> stream;
    encode_frame(stream, kData, 1, 2, nullptr, 0);
    stream[0] = 0xFF;
    stream[3] = 0x7F;
    FrameReader r;
    r.feed(stream.data(), stream.size());
    Frame f;
    CHECK_FALSE(r.pop(f));
    CHECK(r.error());
}

TEST_CASE("net_tcp: routing") {
    std::vector<int32_t> pids = {1, 2, 3, 5};
    CHECK(route(2, 0, pids) == std::vector<int32_t>{1, 3, 5});  // to everyone else
    CHECK(route(1, 0, pids) == std::vector<int32_t>{2, 3, 5});
    CHECK(route(2, 3, pids) == std::vector<int32_t>{3});  // relayed by the host
    CHECK(route(2, 1, pids) == std::vector<int32_t>{1});  // to the host
    CHECK(route(2, 4, pids).empty());                       // unknown (or gone) player
    CHECK(route(2, 2, pids) == std::vector<int32_t>{2});  // to itself
}

TEST_CASE("net_tcp: the player list") {
    std::vector<PlayerInfo> in = {{1, "Host"}, {2, ""}, {7, "Lal"}};
    std::vector<uint8_t> b = encode_players(in);
    std::vector<PlayerInfo> out;
    REQUIRE(decode_players(b.data(), b.size(), out));
    REQUIRE(out.size() == 3);
    CHECK(out[2].pid == 7);
    CHECK(out[2].name == "Lal");
    CHECK_FALSE(decode_players(b.data(), b.size() - 1, out));
}

namespace {
bool pump_until(std::vector<Transport*> ts, bool (*done)(std::vector<Transport*>&), int ms = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        for (Transport* t : ts) t->pump();
        if (done(ts)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
std::vector<Event> drain(Transport& t) {
    std::vector<Event> out;
    Event e;
    while (t.receive(e)) out.push_back(e);
    return out;
}
}  // namespace

TEST_CASE("net_tcp: a host and two clients on localhost") {
    Transport host, a, b;
    std::string err;
    REQUIRE(host.host(0, "Host", &err));
    REQUIRE(host.port() != 0);
    REQUIRE(a.join("127.0.0.1", host.port(), "Alice", 0, &err));
    REQUIRE(pump_until({&host, &a}, [](std::vector<Transport*>& t) { return t[1]->welcomed(); }));
    REQUIRE(b.join("127.0.0.1", host.port(), "Bob", 0, &err));
    REQUIRE(pump_until({&host, &a, &b}, [](std::vector<Transport*>& t) {
        return t[2]->welcomed() && t[1]->players().size() == 3;
    }));
    CHECK(host.self_pid() == kHostPid);
    CHECK(a.self_pid() == 2);
    CHECK(b.self_pid() == 3);
    CHECK(b.players()[1].name == "Alice");
    std::vector<Event> he = drain(host);
    REQUIRE(he.size() == 2);
    CHECK(he[0].kind == kJoined);
    CHECK(he[1].from == 3);
    std::vector<Event> ae = drain(a);
    REQUIRE(ae.size() == 1);
    CHECK(ae[0].kind == kJoined);
    CHECK(ae[0].from == 3);
    drain(b);

    // A broadcast from a client reaches the host and the other client, not itself.
    a.send(0, "bcast", 5);
    // A client to another client: relayed by the host.
    a.send(3, "direct", 6);
    // A client to itself: local.
    a.send(2, "self", 4);
    // The host to one client.
    host.send(2, "fromhost", 8);
    std::vector<Event> hs, as, bs;
    pump_until({&host, &a, &b}, [](std::vector<Transport*>&) { return false; }, 300);
    hs = drain(host);
    as = drain(a);
    bs = drain(b);
    REQUIRE(hs.size() == 1);
    CHECK(hs[0].from == 2);
    CHECK(std::string(hs[0].data.begin(), hs[0].data.end()) == "bcast");
    REQUIRE(bs.size() == 2);
    CHECK(std::string(bs[0].data.begin(), bs[0].data.end()) == "bcast");
    CHECK(bs[1].from == 2);
    CHECK(bs[1].to == 3);
    CHECK(std::string(bs[1].data.begin(), bs[1].data.end()) == "direct");
    REQUIRE(as.size() == 2);
    CHECK(std::string(as[0].data.begin(), as[0].data.end()) == "self");
    CHECK(as[1].from == kHostPid);

    // A client leaves: the others are told.
    b.close();
    REQUIRE(pump_until({&host, &a}, [](std::vector<Transport*>& t) { return t[1]->players().size() == 2; }));
    hs = drain(host);
    REQUIRE(hs.size() == 1);
    CHECK(hs[0].kind == kLeft);
    CHECK(hs[0].from == 3);
    as = drain(a);
    REQUIRE(as.size() == 1);
    CHECK(as[0].kind == kLeft);
    // The host leaves: the client has lost the session.
    host.close();
    CHECK(pump_until({&a}, [](std::vector<Transport*>& t) { return t[0]->lost(); }));
}
