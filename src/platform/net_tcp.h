// smacx's network transport: the replacement of the original's DirectPlay layer (Net_*, netlib
// 0x62D6A0..0x633000) for online games between smacx copies (docs/MULTIPLAYER.md, phase 3).
//
// One TCP stream per client to the host (star topology, as DirectPlay's client/server sessions).
// Every message travels as a length-prefixed frame (FrameHeader, little-endian, then the payload).
// Player ids (pids) are given by the host: the host is kHostPid, clients 2, 3, ... in the order they
// join (never reused). A frame's `to` is a pid or 0 (everyone but the sender); the host delivers
// frames meant for it and relays the others (route()). The transport's own frames (kinds other than
// kData) announce players: a joining client sends kHello with its name, the host answers kWelcome
// (the client's pid, then every player's pid and name), and tells everyone kJoined / kLeft.
// All I/O is non-blocking: pump() (called from the game's wait loops) accepts, reads and writes.
//
// The framing (FrameHeader, encode_frame, FrameReader) and the routing (route) are pure functions,
// unit-tested in tests/unit/test_net_tcp.cpp; Transport adds the sockets (POSIX and Winsock).
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace nettcp {

constexpr int32_t kHostPid = 1;
constexpr uint32_t kMaxPayload = 0x100000;  // a frame larger than this is a protocol error
constexpr uint16_t kDefaultPort = 28100;

enum FrameKind : uint32_t {
    kData = 0,     // a game message (NetDaemon's), from -> to
    kHello = 1,    // client -> host: the player's name (payload)
    kWelcome = 2,  // host -> client: to = the client's pid; payload = the player list (encode_players)
    kJoined = 3,   // host -> all: from = the new player's pid, payload = its name
    kLeft = 4,     // host -> all: from = the player who left
};

struct FrameHeader {
    uint32_t size;  // payload bytes
    uint32_t kind;  // FrameKind
    int32_t from;   // sender pid
    int32_t to;     // receiver pid, 0: everyone but the sender
};
constexpr uint32_t kHeaderSize = 16;

struct Frame {
    FrameHeader head{};
    std::vector<uint8_t> payload;
};

// Appends one frame (header + payload) to out.
void encode_frame(std::vector<uint8_t>& out, uint32_t kind, int32_t from, int32_t to, const void* data, uint32_t size);

// Splits a byte stream into frames: feed() the bytes as they arrive, then pop() whole frames.
class FrameReader {
public:
    void feed(const void* data, size_t size);
    // A whole frame, if one is buffered. False when none is (or after an error).
    bool pop(Frame& out);
    // An oversized frame was announced: the stream cannot be trusted any more.
    bool error() const { return error_; }
    size_t buffered() const { return buf_.size() - pos_; }

private:
    std::vector<uint8_t> buf_;
    size_t pos_ = 0;
    bool error_ = false;
};

// The host's routing of a frame from `from` to `to`, among the players `pids` (the host's own pid
// included): the pids it is delivered to. to == 0: everyone but the sender; a known pid: that one;
// an unknown pid: nobody.
std::vector<int32_t> route(int32_t from, int32_t to, const std::vector<int32_t>& pids);

struct PlayerInfo {
    int32_t pid;
    std::string name;
};
std::vector<uint8_t> encode_players(const std::vector<PlayerInfo>& players);
bool decode_players(const uint8_t* data, size_t size, std::vector<PlayerInfo>& out);

// A received message or player event.
struct Event {
    uint32_t kind;  // kData, kJoined, kLeft
    int32_t from;
    int32_t to;
    std::vector<uint8_t> data;
};

class Transport {
public:
    Transport();
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    // Starts a session as its host, listening on port (0: any free port, see port()).
    bool host(uint16_t port, const std::string& name, std::string* error);
    // Joins the host at address:port; waits up to timeout_ms for the welcome (pid given). With
    // timeout_ms 0 it returns at once and the caller pumps until welcomed() (or lost()).
    bool join(const std::string& address, uint16_t port, const std::string& name, uint32_t timeout_ms,
              std::string* error);
    void close();

    bool active() const { return mode_ != kNone; }
    bool is_host() const { return mode_ == kHostMode; }
    bool welcomed() const { return self_ != 0; }
    bool lost() const { return lost_; }  // a client whose connection to the host ended
    int32_t self_pid() const { return self_; }
    uint16_t port() const { return port_; }
    const std::vector<PlayerInfo>& players() const { return players_; }

    // Accepts connections, reads and writes what the sockets allow. Never blocks.
    void pump();
    // Queues a message to `to` (0: everyone else; self_pid(): to itself). False if not active.
    bool send(int32_t to, const void* data, uint32_t size);
    // The next received message or player event, if any (pump() first).
    bool receive(Event& out);
    // Pumps until every queued byte is written or timeout_ms passes.
    void flush(uint32_t timeout_ms);

private:
    enum Mode { kNone, kHostMode, kClientMode };
    struct Peer;
    void deliver(const Frame& f, Peer* origin);
    void drop(size_t index);
    void queue(Peer& p, uint32_t kind, int32_t from, int32_t to, const void* data, uint32_t size);
    bool read_peer(Peer& p);
    bool write_peer(Peer& p);

    Mode mode_ = kNone;
    intptr_t listen_ = -1;
    std::vector<Peer*> peers_;  // host: the clients; client: the host
    std::vector<PlayerInfo> players_;
    std::deque<Event> inbox_;
    int32_t self_ = 0;
    int32_t next_pid_ = kHostPid + 1;
    uint16_t port_ = 0;
    bool lost_ = false;
};

}  // namespace nettcp
