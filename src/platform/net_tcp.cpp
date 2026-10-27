#include "platform/net_tcp.h"

#include <chrono>
#include <cstring>

namespace nettcp {

namespace {
void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back((uint8_t)(v >> (8 * i)));
}
uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
}  // namespace

void encode_frame(std::vector<uint8_t>& out, uint32_t kind, int32_t from, int32_t to, const void* data,
                  uint32_t size) {
    put32(out, size);
    put32(out, kind);
    put32(out, (uint32_t)from);
    put32(out, (uint32_t)to);
    const uint8_t* p = static_cast<const uint8_t*>(data);
    if (size) out.insert(out.end(), p, p + size);
}

void FrameReader::feed(const void* data, size_t size) {
    if (error_ || !size) return;
    if (pos_ && pos_ == buf_.size()) {
        buf_.clear();
        pos_ = 0;
    } else if (pos_ > 0x10000) {
        buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t)pos_);
        pos_ = 0;
    }
    const uint8_t* p = static_cast<const uint8_t*>(data);
    buf_.insert(buf_.end(), p, p + size);
}

bool FrameReader::pop(Frame& out) {
    if (error_ || buffered() < kHeaderSize) return false;
    const uint8_t* p = buf_.data() + pos_;
    uint32_t size = get32(p);
    if (size > kMaxPayload) {
        error_ = true;
        return false;
    }
    if (buffered() < kHeaderSize + size) return false;
    out.head.size = size;
    out.head.kind = get32(p + 4);
    out.head.from = (int32_t)get32(p + 8);
    out.head.to = (int32_t)get32(p + 12);
    out.payload.assign(p + kHeaderSize, p + kHeaderSize + size);
    pos_ += kHeaderSize + size;
    return true;
}

std::vector<int32_t> route(int32_t from, int32_t to, const std::vector<int32_t>& pids) {
    std::vector<int32_t> out;
    for (int32_t pid : pids) {
        if (to == 0 ? pid != from : pid == to) out.push_back(pid);
    }
    return out;
}

std::vector<uint8_t> encode_players(const std::vector<PlayerInfo>& players) {
    std::vector<uint8_t> out;
    put32(out, (uint32_t)players.size());
    for (const PlayerInfo& p : players) {
        put32(out, (uint32_t)p.pid);
        put32(out, (uint32_t)p.name.size());
        out.insert(out.end(), p.name.begin(), p.name.end());
    }
    return out;
}

bool decode_players(const uint8_t* data, size_t size, std::vector<PlayerInfo>& out) {
    out.clear();
    if (size < 4) return false;
    uint32_t n = get32(data);
    size_t pos = 4;
    for (uint32_t i = 0; i < n; i++) {
        if (size - pos < 8) return false;
        PlayerInfo p;
        p.pid = (int32_t)get32(data + pos);
        uint32_t len = get32(data + pos + 4);
        pos += 8;
        if (size - pos < len) return false;
        p.name.assign(reinterpret_cast<const char*>(data + pos), len);
        pos += len;
        out.push_back(p);
    }
    return pos == size;
}

}  // namespace nettcp

// ---- the sockets (native builds; the hybrid DLL runs inside the original, with DirectPlay) ----

#ifndef SMACX_HYBRID
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
static bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
static bool in_progress() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close ::close
static bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static bool in_progress() { return errno == EINPROGRESS || errno == EINTR; }
#endif

namespace nettcp {

namespace {
bool sockets_ready() {
#ifdef _WIN32
    static bool started = false;
    if (!started) {
        WSADATA wsa;
        started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }
    return started;
#else
    return true;
#endif
}
void set_nonblocking(sock_t s) {
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}
uint32_t now_ms() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
void nap() {
#ifdef _WIN32
    Sleep(5);
#else
    poll(nullptr, 0, 5);
#endif
}
}  // namespace

struct Transport::Peer {
    sock_t sock = SOCK_INVALID;
    int32_t pid = 0;  // 0 until its hello (host side)
    std::string name;
    FrameReader reader;
    std::vector<uint8_t> out;
    size_t out_pos = 0;
    bool dead = false;
};

Transport::Transport() = default;
Transport::~Transport() { close(); }

bool Transport::host(uint16_t port, const std::string& name, std::string* error) {
    close();
    if (!sockets_ready()) {
        if (error) *error = "sockets unavailable";
        return false;
    }
    sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCK_INVALID) {
        if (error) *error = "socket() failed";
        return false;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(s, 8) != 0) {
        sock_close(s);
        if (error) *error = "cannot listen on port " + std::to_string(port);
        return false;
    }
    socklen_t len = sizeof(a);
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    port_ = ntohs(a.sin_port);
    set_nonblocking(s);
    listen_ = (intptr_t)s;
    mode_ = kHostMode;
    self_ = kHostPid;
    next_pid_ = kHostPid + 1;
    players_.push_back({kHostPid, name});
    return true;
}

bool Transport::join(const std::string& address, uint16_t port, const std::string& name, uint32_t timeout_ms,
                     std::string* error) {
    close();
    if (!sockets_ready()) {
        if (error) *error = "sockets unavailable";
        return false;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(address.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (error) *error = "unknown host " + address;
        return false;
    }
    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == SOCK_INVALID) {
        freeaddrinfo(res);
        if (error) *error = "socket() failed";
        return false;
    }
    set_nonblocking(s);
    int r = connect(s, res->ai_addr, (socklen_t)res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && !in_progress()) {
        sock_close(s);
        if (error) *error = "cannot connect to " + address;
        return false;
    }
    Peer* p = new Peer;
    p->sock = s;
    p->pid = kHostPid;
    peers_.push_back(p);
    mode_ = kClientMode;
    port_ = port;
    queue(*p, kHello, 0, kHostPid, name.data(), (uint32_t)name.size());
    if (!timeout_ms) return true;  // the caller pumps until welcomed()
    uint32_t start = now_ms();
    while (!self_ && !lost_ && now_ms() - start < timeout_ms) {
        pump();
        if (!self_) nap();
    }
    if (!self_) {
        close();
        if (error) *error = "no answer from " + address;
        return false;
    }
    return true;
}

void Transport::close() {
    for (Peer* p : peers_) {
        if (p->sock != SOCK_INVALID) sock_close(p->sock);
        delete p;
    }
    peers_.clear();
    if (listen_ != -1) sock_close((sock_t)listen_);
    listen_ = -1;
    players_.clear();
    inbox_.clear();
    mode_ = kNone;
    self_ = 0;
    lost_ = false;
}

void Transport::queue(Peer& p, uint32_t kind, int32_t from, int32_t to, const void* data, uint32_t size) {
    encode_frame(p.out, kind, from, to, data, size);
}

bool Transport::send(int32_t to, const void* data, uint32_t size) {
    if (mode_ == kNone || size > kMaxPayload) return false;
    if (to == self_) {  // to itself: straight into the inbox (DirectPlay's local delivery)
        const uint8_t* b = static_cast<const uint8_t*>(data);
        inbox_.push_back({kData, self_, to, std::vector<uint8_t>(b, b + size)});
        return true;
    }
    if (mode_ == kClientMode) {
        if (!peers_.empty()) queue(*peers_[0], kData, self_, to, data, size);
    } else {
        Frame f;
        f.head = {size, kData, self_, to};
        const uint8_t* b = static_cast<const uint8_t*>(data);
        f.payload.assign(b, b + size);
        deliver(f, nullptr);
    }
    pump();
    return true;
}

// Host: the frame to its receivers (route): itself into the inbox, the clients through their streams.
void Transport::deliver(const Frame& f, Peer* origin) {
    (void)origin;
    std::vector<int32_t> pids;
    for (const PlayerInfo& p : players_) pids.push_back(p.pid);
    for (int32_t pid : route(f.head.from, f.head.to, pids)) {
        if (pid == self_) {
            inbox_.push_back({kData, f.head.from, f.head.to, f.payload});
            continue;
        }
        for (Peer* p : peers_) {
            if (p->pid == pid && !p->dead) {
                queue(*p, kData, f.head.from, f.head.to, f.payload.data(), f.head.size);
            }
        }
    }
}

bool Transport::read_peer(Peer& p) {
    uint8_t buf[16384];
    for (;;) {
        int n = (int)recv(p.sock, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n > 0) {
            p.reader.feed(buf, (size_t)n);
            continue;
        }
        if (n == 0) return false;  // closed
        return would_block();
    }
}

bool Transport::write_peer(Peer& p) {
    while (p.out_pos < p.out.size()) {
        int n = (int)::send(p.sock, reinterpret_cast<const char*>(p.out.data() + p.out_pos),
                            (int)(p.out.size() - p.out_pos), 0
#ifdef MSG_NOSIGNAL
                            | MSG_NOSIGNAL
#endif
        );
        if (n > 0) {
            p.out_pos += (size_t)n;
            continue;
        }
        if (n < 0 && would_block()) return true;
        if (n < 0 && in_progress()) return true;  // a client's connect still under way
        return false;
    }
    p.out.clear();
    p.out_pos = 0;
    return true;
}

void Transport::drop(size_t index) {
    Peer* p = peers_[index];
    peers_.erase(peers_.begin() + (std::ptrdiff_t)index);
    if (p->sock != SOCK_INVALID) sock_close(p->sock);
    int32_t pid = p->pid;
    delete p;
    if (mode_ == kClientMode) {
        lost_ = true;
        return;
    }
    if (!pid) return;
    for (size_t i = 0; i < players_.size(); i++) {
        if (players_[i].pid == pid) players_.erase(players_.begin() + (std::ptrdiff_t)i);
    }
    inbox_.push_back({kLeft, pid, 0, {}});
    for (Peer* q : peers_) {
        if (q->pid) queue(*q, kLeft, pid, 0, nullptr, 0);
    }
}

void Transport::pump() {
    if (mode_ == kNone) return;
    if (mode_ == kHostMode) {
        for (;;) {
            sock_t c = accept((sock_t)listen_, nullptr, nullptr);
            if (c == SOCK_INVALID) break;
            set_nonblocking(c);
            Peer* p = new Peer;
            p->sock = c;
            peers_.push_back(p);
        }
    }
    for (size_t i = 0; i < peers_.size(); i++) {
        Peer& p = *peers_[i];
        if (!write_peer(p) || !read_peer(p)) p.dead = true;
        Frame f;
        while (!p.dead && p.reader.pop(f)) {
            if (mode_ == kHostMode) {
                if (f.head.kind == kHello && !p.pid) {
                    p.pid = next_pid_++;
                    p.name.assign(f.payload.begin(), f.payload.end());
                    players_.push_back({p.pid, p.name});
                    std::vector<uint8_t> list = encode_players(players_);
                    queue(p, kWelcome, kHostPid, p.pid, list.data(), (uint32_t)list.size());
                    for (Peer* q : peers_) {
                        if (q != &p && q->pid) queue(*q, kJoined, p.pid, 0, p.name.data(), (uint32_t)p.name.size());
                    }
                    inbox_.push_back({kJoined, p.pid, 0, f.payload});
                } else if (f.head.kind == kData && p.pid) {
                    f.head.from = p.pid;  // a client cannot speak for another
                    deliver(f, &p);
                }
            } else {
                if (f.head.kind == kWelcome) {
                    self_ = f.head.to;
                    decode_players(f.payload.data(), f.payload.size(), players_);
                } else if (f.head.kind == kJoined) {
                    players_.push_back({f.head.from, std::string(f.payload.begin(), f.payload.end())});
                    inbox_.push_back({kJoined, f.head.from, 0, f.payload});
                } else if (f.head.kind == kLeft) {
                    for (size_t k = 0; k < players_.size(); k++) {
                        if (players_[k].pid == f.head.from) players_.erase(players_.begin() + (std::ptrdiff_t)k);
                    }
                    inbox_.push_back({kLeft, f.head.from, 0, {}});
                } else if (f.head.kind == kData) {
                    inbox_.push_back({kData, f.head.from, f.head.to, std::move(f.payload)});
                }
            }
        }
        if (p.reader.error()) p.dead = true;
        if (!p.dead) write_peer(p);
    }
    for (size_t i = peers_.size(); i-- > 0;) {
        if (peers_[i]->dead) drop(i);
    }
}

bool Transport::receive(Event& out) {
    if (inbox_.empty()) return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

void Transport::flush(uint32_t timeout_ms) {
    uint32_t start = now_ms();
    for (;;) {
        pump();
        bool pending = false;
        for (Peer* p : peers_) pending |= p->out_pos < p->out.size();
        if (!pending || now_ms() - start >= timeout_ms) return;
        nap();
    }
}

}  // namespace nettcp
#endif  // SMACX_HYBRID
