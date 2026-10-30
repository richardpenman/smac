// The network transport of native builds: the original's Net_* entry points (DirectPlay, netlib
// 0x62D6A0..0x633000) over OpenSMAC's TCP transport (platform/net_tcp.h), for online games between
// OpenSMAC copies. Only what the NetDaemon layer uses is kept: Net_send / Net_get with the original's
// player ids (the transport's: the host 1, the clients 2, 3, ...), the player names, the session's
// player events (DirectPlay's system messages, Net_handle_sys_msg) and Net_close. The DirectPlay
// service enumeration, lobbies, voice, groups and packet splitting are not needed (the transport
// carries messages of any size).
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "engine/net.h"
#include "engine/ui_bridge.h"
#include "platform/net_tcp.h"

namespace {
nettcp::Transport g_net;
std::vector<uint8_t> g_message;  // Net_get's buffer (0x9BC4BC in the original): one at a time
std::deque<nettcp::Event> g_data, g_sys;
std::string g_name;
int32_t& fld(uint8_t* net, uint32_t off) { return *reinterpret_cast<int32_t*>(net + off); }

void copy_error(const std::string& e, char* out, uint32_t size) {
    if (!out || !size) return;
    std::snprintf(out, size, "%s", e.c_str());
}
}  // namespace

bool net_link_active() { return g_net.active(); }

void net_link_pump() {
    if (!g_net.active()) return;
    g_net.pump();
    nettcp::Event e;
    while (g_net.receive(e)) (e.kind == nettcp::kData ? g_data : g_sys).push_back(std::move(e));
    if (g_net.lost() && (g_sys.empty() || g_sys.back().kind != 99)) {
        nettcp::Event lost;
        lost.kind = 99;  // the session lost (DirectPlay's DPSYS_SESSIONLOST)
        lost.from = nettcp::kHostPid;
        lost.to = 0;
        g_sys.push_back(lost);
        g_net.close();
    }
}

int32_t net_link_host(uint8_t* net, uint16_t port, const char* name, char* error, uint32_t error_size) {
    std::string err;
    g_data.clear();
    g_sys.clear();
    if (!g_net.host(port, name, &err)) {
        copy_error(err, error, error_size);
        return 1;
    }
    g_name = name;
    fld(net, 0x760) = g_net.self_pid();
    fld(net, 0x764) = nettcp::kHostPid;
    std::fprintf(stderr, "net: hosting on port %u as pid %d\n", g_net.port(), g_net.self_pid());
    // The host's own player created (DirectPlay's DPSYS_CREATEPLAYERORGROUP for the local player).
    nettcp::Event self;
    self.kind = nettcp::kJoined;
    self.from = g_net.self_pid();
    self.to = 0;
    self.data.assign(g_name.begin(), g_name.end());
    g_sys.push_back(self);
    return 0;
}

int32_t net_link_join(uint8_t* net, const char* address, uint16_t port, const char* name, char* error,
                      uint32_t error_size) {
    std::string err;
    g_data.clear();
    g_sys.clear();
    if (!g_net.join(address, port, name, 10000, &err)) {
        copy_error(err, error, error_size);
        return 1;
    }
    g_name = name;
    fld(net, 0x760) = g_net.self_pid();
    fld(net, 0x764) = nettcp::kHostPid;
    std::fprintf(stderr, "net: joined %s:%u as pid %d\n", address, port, g_net.self_pid());
    // The session's players, the joining one last (DirectPlay's player list sent to a new player:
    // NetDaemon_on_player_added for each, in the host's order).
    for (const nettcp::PlayerInfo& p : g_net.players()) {
        nettcp::Event e;
        e.kind = nettcp::kJoined;
        e.from = p.pid;
        e.to = 0;
        e.data.assign(p.name.begin(), p.name.end());
        g_sys.push_back(e);
    }
    return 0;
}

int32_t THISCALL Net_send(uint8_t* net, const void* msg, int32_t len, int32_t to, int32_t) {
    (void)net;
    if (!g_net.active() || !msg || len <= 0) return 1;
    if (to == 0) {
        // A message to everyone reaches its sender too (DirectPlay's local delivery: the session's
        // flags 9 of Net_init make Net_send queue it locally first).
        nettcp::Event e;
        e.kind = nettcp::kData;
        e.from = g_net.self_pid();
        e.to = 0;
        const uint8_t* b = static_cast<const uint8_t*>(msg);
        e.data.assign(b, b + len);
        g_data.push_back(std::move(e));
    }
    g_net.send(to, msg, (uint32_t)len);
    net_link_pump();
    return 0;
}

uint8_t* THISCALL Net_get(uint8_t*, int32_t* from, int32_t* size) {
    if (!from) return nullptr;
    net_link_pump();
    if (g_data.empty()) return nullptr;
    nettcp::Event e = std::move(g_data.front());
    g_data.pop_front();
    // The buffer is reused: a message handler that waits for other messages loses its own (as
    // the original's single receive buffer).
    g_message.assign(e.data.begin(), e.data.end());
    if (g_message.size() < 0x40) g_message.resize(0x40, 0);  // headers read past short messages
    *from = e.from;
    if (size) *size = (int32_t)e.data.size();
    return g_message.data();
}

const char* THISCALL Net_get_player_name(uint8_t*, int32_t pid) {
    static std::string name;
    for (const nettcp::PlayerInfo& p : g_net.players()) {
        if (p.pid == pid) {
            name = p.name;
            return name.c_str();
        }
    }
    return nullptr;
}

void THISCALL Net_close(uint8_t* net) {
    if (g_net.active()) g_net.flush(500);
    g_net.close();
    g_data.clear();
    g_sys.clear();
    fld(net, 0x760) = 0;
    fld(net, 0x764) = 0;
}

void THISCALL Net_handle_sys_msg(uint8_t* net) {
    net_link_pump();
    while (!g_sys.empty()) {
        nettcp::Event e = std::move(g_sys.front());
        g_sys.pop_front();
        if (e.kind == nettcp::kJoined) {
            std::string name(e.data.begin(), e.data.end());
            NetDaemon_on_player_added(net, name.c_str(), e.from);
        } else if (e.kind == nettcp::kLeft) {
            NetDaemon_on_player_deleted(net, 0, e.from);
        } else if (e.kind == 99) {
            NetDaemon_on_player_deleted(net, 0, fld(net, 0x764));
        }
    }
}

