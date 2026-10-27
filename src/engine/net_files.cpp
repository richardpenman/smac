// The scenario's files sent to the players (docs/MULTIPLAYER.md, phase 3): NetDaemon_send_files
// (0x52ED40), called by multiplayer_init for a scenario (NetSetupType 3) on every machine, and its
// messages in NetDaemon_process_message (0x0F05..0x0F0C, 0x0F0F).
//
// The host sends the scenario's directory (its GameAltPath; 0x0F05) and waits for each player's
// 0x0F0F; each player makes it its alternate directory. The host then lists the directory's files
// (FindFirstFile "*.*"): each one's header (0x0F06: index, checksum_file, size, blocks, name) goes
// to everyone, then 0x0F07 (2). A player whose copy of a file is missing or differs asks for it
// (0x0F08: the host marks the player in the file's entry) and makes an empty file; after the
// headers it answers 0x0F0A (files to receive) or 0x0F0C (none), which every machine notes in the
// player's slot (+0x28: 1 receiving, 2 done; the host's own: 2). Once every player answered, the
// host sends each asked file to the players who asked (a DirectPlay group) in blocks of 2048 bytes
// (0x0F0B: size, index, block number, the data from +0x20) with MPSENDFILE shown; a player shows
// MPRECEIVEFILE at a file's first block and writes the file at its last; after its last asked file
// it answers 0x0F0C. When every player is done the host sends 0x0F07 (3), which ends the players'
// wait. Esc cancels (the scenario is not started: 1).
//
// smacx: a player's files go below its per-user directory (the game folder is read-only), at the
// scenario's relative path, which becomes its alternate directory: a player receives the files it
// has not received before even when its own game folder holds the scenario. No current directory
// change (the original's SetCurrentDirectory to the scenario's directory: the paths are built
// instead). The modeless MPSENDFILE / MPRECEIVEFILE popups are the NetMsg box (NetMsg_pop).
// The original's quirks are kept: a player answers the host through NetDaemon_send_message with
// the host's pid as a faction (AlphaNet_who_2_pid(host pid): the player of faction 1, or everyone
// when no player has it); a file whose size is a multiple of 2048 counts one block more than the
// host sends (the player never writes it: Esc).
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/crt.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/save.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"

#ifndef SMACX_HYBRID
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
constexpr int32_t kEntry = 0x11C;  // a file's header (0x0F06)
std::vector<uint8_t> g_table;      // 0x93E970: the files' headers (256 at most)
int32_t g_phase = 0;               // 0x93F79C: 1 the directory known, 2 the headers sent, 3 done
uint32_t g_ready = 0;              // 0x93F7A0: the host: the players who set their directory
int32_t g_last = -1;               // 0x93F7A4: a player: the last file it asked for
int32_t g_needed = 0;              // 0x93F7AC: a player: the files still to receive
std::vector<uint8_t> g_buf;        // 0x93F7B0: the file being received
char g_dir[256];                   // 0x93AC88: the scenario's directory (relative)

int32_t& fld(uint8_t* net, int32_t off) { return *reinterpret_cast<int32_t*>(net + off); }
uint8_t& slot_state(uint8_t* net, int32_t idx) { return net[0x794 + idx * 0x19C]; }
int32_t own_idx(uint8_t* net) { return AlphaNet_pid_2_idx(net, fld(net, 0x760)); }
bool host(uint8_t* net) { return fld(net, 0x1B30) != 0; }
int32_t& cancelled() { return NetSessionState[43]; }  // 0x93E96C
uint8_t* entry(int32_t i) { return &g_table[(size_t)i * kEntry]; }

std::string host_path(const std::string& p) {
    char out[1024];
    platform::resolve_path(p.c_str(), out, sizeof(out));
    return out;
}
// A player's copy of the scenario's directory (smacx: below the per-user directory).
std::string local_dir() {
    std::string d = std::string(ui_user_dir()) + "/" + g_dir;
    for (char& c : d)
        if (c == '\\') c = '/';
    return d;
}
void pump(uint8_t* net) {
    wait_task();
    NetDaemon_receive(net);
    Net_handle_sys_msg(net);
}
void progress(const char* label, const char* name, int32_t count) {
    parse_says(0, name, -1, -1);
    parse_num(0, count);
    NetMsg_pop(&NetMsg, label, 0, 1, nullptr);
}
}  // namespace

int32_t THISCALL NetDaemon_send_files(uint8_t* net) {
    cancelled() = 0;
    int32_t idx = own_idx(net);
    g_table.assign((size_t)kEntry * 256, 0);
    uint32_t others = 0;
    if (host(net)) {
        for (int32_t i = 1; i <= fld(net, 0x768); i++) {
            if (i != idx) others |= 1u << i;
            slot_state(net, i) = i == idx ? 2 : 0;
        }
    }
    int32_t files = 0;
    if (!host(net)) {
        while (g_phase != 1 && !ControlTurnA) pump(net);
        uint8_t m[0x20] = {};
        *reinterpret_cast<uint16_t*>(m) = 0xF0F;
        *reinterpret_cast<int32_t*>(m + 4) = idx;
        Net_send(net, m, 0x20, fld(net, 0x764), 1);
        std::string dir = local_dir();
        std::error_code ec;
        fs::create_directories(dir, ec);
        GameAltPath[0] = 0;
        crt_strncpy(GameAltPath, dir.c_str(), 0xFF);
        filefind_set_alternate(GameAltPath);
        ui_net_note_text((std::string("net: scenario directory ") + g_dir).c_str());
        while (g_phase != 3 && !cancelled() && !ControlTurnA) {
            if (ui_escape_pressed()) {
                cancelled() = 1;
                NetMsg_close(&NetMsg);
                break;
            }
            pump(net);
        }
    } else {
        // The directory relative to the game's (an absolute one: the game directory's part cut).
        const char* d = GameAltPath;
        if (d[0] && d[1] == ':') d += crt_strlen(FilefindPaths.exe_dir);
        g_dir[0] = 0;
        crt_strncpy(g_dir, d, 0xFF);
        std::vector<uint8_t> m(kEntry, 0);
        *reinterpret_cast<uint16_t*>(&m[0]) = 0xF05;
        crt_strncpy(reinterpret_cast<char*>(&m[0x18]), g_dir, 0x103);
        NetDaemon_send_message(net, m.data(), kEntry, 0);
        g_ready = 0;
        while (others && g_ready != others && !ControlTurnA) pump(net);
        // The headers, each to everyone.
        std::string dir = host_path(GameAltPath);
        std::vector<fs::directory_entry> list;
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec)) list.push_back(*it);
        }
        for (const auto& f : list) {
            if (files >= 256) break;
            uint8_t* e = entry(files);
            uint32_t size = (uint32_t)f.file_size(ec);
            std::string name = f.path().filename().string();
            *reinterpret_cast<uint16_t*>(e) = 0xF06;
            *reinterpret_cast<int32_t*>(e + 4) = idx;
            *reinterpret_cast<int32_t*>(e + 8) = 0;
            *reinterpret_cast<int32_t*>(e + 0xC) = (int32_t)((size + 0x800) >> 11);
            e[0x10] = (uint8_t)files;
            *reinterpret_cast<uint32_t*>(e + 0x14) = size;
            e[0x11] = (uint8_t)checksum_file(name.c_str());
            e[0x18] = 0;
            crt_strncpy(reinterpret_cast<char*>(e + 0x18), name.c_str(), 0x103);
            NetDaemon_send_message(net, e, kEntry, 0);
            files++;
        }
        uint8_t done[0x20] = {};
        *reinterpret_cast<uint16_t*>(done) = 0xF07;
        *reinterpret_cast<int32_t*>(done + 8) = 2;
        NetDaemon_send_message(net, done, 0x20, 0);
        // Every player answered (its slot's state set).
        for (;;) {
            pump(net);
            int32_t i = 1;
            while (i <= fld(net, 0x768) && slot_state(net, i)) i++;
            if (i > fld(net, 0x768) || ControlTurnA) break;
        }
        int32_t asked = 0;
        for (int32_t i = 0; i < files; i++) {
            if (*reinterpret_cast<int32_t*>(entry(i) + 8)) asked++;
        }
        const int32_t total = asked;
        std::vector<uint8_t> block(0x820, 0);
        *reinterpret_cast<uint16_t*>(&block[0]) = 0xF0B;
        for (int32_t i = 0; i < files && !cancelled(); i++) {
            if (ui_escape_pressed()) {
                cancelled() = 1;
                break;
            }
            uint8_t* e = entry(i);
            uint32_t mask = *reinterpret_cast<uint32_t*>(e + 8);
            if (!mask) continue;
            const char* name = reinterpret_cast<const char*>(e + 0x18);
            progress("MPSENDFILE", name, asked--);
            *reinterpret_cast<int32_t*>(&block[8]) = i;
            uint32_t size = 0;
            uint8_t* data = win32::read_file(host_path(std::string(GameAltPath) + name).c_str(), &size);
            if (!data) continue;
            *reinterpret_cast<int32_t*>(&block[4]) = (int32_t)size;
            auto send_group = [&](int32_t len) {
                for (int32_t p = 1; p < 8; p++) {
                    if (mask & (1u << p)) Net_send(net, block.data(), len, fld(net, 0x78C + p * 0x19C), 1);
                }
            };
            uint32_t pos = 0;
            int32_t n = 0;
            while (size - pos > 0x7FF) {
                wait_task();
                std::memcpy(&block[0x20], data + pos, 0x800);
                pos += 0x800;
                *reinterpret_cast<int32_t*>(&block[0xC]) = n++;
                send_group(0x820);
            }
            *reinterpret_cast<int32_t*>(&block[0xC]) = n;
            if (pos < size) {
                std::memcpy(&block[0x20], data + pos, size - pos);
                send_group((int32_t)(size - pos) + 0x20);
            }
            win32::free_file(data);
        }
        // Every player done.
        while (!cancelled() && !ControlTurnA) {
            if (ui_escape_pressed()) break;
            pump(net);
            int32_t i = 1;
            while (i <= fld(net, 0x768) && slot_state(net, i) > 1) i++;
            if (i > fld(net, 0x768)) break;
        }
        *reinterpret_cast<int32_t*>(done + 8) = 3;
        NetDaemon_send_message(net, done, 0x20, 0);
        char note[96];
        std::snprintf(note, sizeof(note), "net: scenario files sent (%d files, %d asked)", files, total);
        ui_net_note_text(note);
    }
    NetMsg_close(&NetMsg);
    g_table.clear();
    g_buf.clear();
    g_phase = 0;
    return cancelled();
}

// process_message's file transfer messages (0x0F05..0x0F0C, 0x0F0F).
void net_files_message(uint8_t* net, uint8_t* msg, int32_t size, int32_t from) {
    int32_t* m = reinterpret_cast<int32_t*>(msg);
    uint16_t type = *reinterpret_cast<uint16_t*>(msg);
    switch (type) {
    case 0xF05:
        if (host(net)) break;
        g_phase = 1;
        g_last = -1;
        g_needed = 0;
        if (g_table.empty()) g_table.assign((size_t)kEntry * 256, 0);
        g_dir[0] = 0;
        crt_strncpy(g_dir, reinterpret_cast<const char*>(msg + 0x18), 0xFF);
        break;
    case 0xF06: {
        if (host(net) || g_table.empty()) break;
        const char* name = reinterpret_cast<const char*>(msg + 0x18);
        if ((uint8_t)checksum_file(name) == msg[0x11]) break;
        int32_t i = msg[0x10];
        g_needed++;
        g_last = i;
        m[1] = own_idx(net);
        *reinterpret_cast<uint16_t*>(msg) = 0xF08;
        NetDaemon_send_message(net, msg, 0x14, from);
        uint8_t* e = entry(i);
        e[0x18] = 0;
        crt_strncpy(reinterpret_cast<char*>(e + 0x18), name, 0x103);
        *reinterpret_cast<int32_t*>(e + 0x14) = m[5];
        *reinterpret_cast<int32_t*>(e + 0xC) = m[3];
        std::ofstream(local_dir() + name, std::ios::binary | std::ios::trunc);  // (Filemap_create, empty)
        ui_net_note_text((std::string("net: scenario file asked ") + name).c_str());
        break;
    }
    case 0xF07: {
        if (host(net)) break;
        uint8_t reply[0x20];
        std::memcpy(reply, msg, 0x20);
        if (g_last < 0) {
            *reinterpret_cast<uint16_t*>(reply) = 0xF0C;
            g_table.clear();
        } else {
            *reinterpret_cast<uint16_t*>(reply) = 0xF0A;
        }
        reinterpret_cast<int32_t*>(reply)[1] = own_idx(net);
        NetDaemon_send_message(net, reply, size < 0x20 ? 0x20 : size, from);
        g_phase = m[2];
        break;
    }
    case 0xF08:
        if (host(net) && !g_table.empty()) *reinterpret_cast<uint32_t*>(entry(msg[0x10]) + 8) |= 1u << (m[1] & 31);
        break;
    case 0xF0A: slot_state(net, m[1]) = 1; break;
    case 0xF0C: slot_state(net, m[1]) = 2; break;
    case 0xF0B: {
        if (host(net) || g_table.empty()) break;
        int32_t i = m[2], n = m[3];
        uint8_t* e = entry(i);
        if (n == 0) {
            g_buf.assign((size_t)*reinterpret_cast<uint32_t*>(e + 0x14), 0);
            *reinterpret_cast<int32_t*>(e + 0xC) = (m[1] + 0x800) / 0x800;
        }
        size_t at = (size_t)n * 0x800, len = (size_t)(size - 0x20);
        if (at + len <= g_buf.size()) std::memcpy(&g_buf[at], msg + 0x20, len);
        const char* name = reinterpret_cast<const char*>(e + 0x18);
        if (n == 0) {
            PopupDialogState = 6;
            progress("MPRECEIVEFILE", name, g_needed);
            g_needed--;
            PopupDialogState = 0;
        }
        if (*reinterpret_cast<int32_t*>(e + 0xC) == n + 1) {
            std::ofstream out(local_dir() + name, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(g_buf.data()), (std::streamsize)g_buf.size());
            g_buf.clear();
            ui_net_note_text((std::string("net: scenario file received ") + name).c_str());
        }
        if (g_last == i && *reinterpret_cast<int32_t*>(e + 0xC) == n + 1) {
            uint8_t reply[0x20];
            std::memcpy(reply, msg, 0x20);
            *reinterpret_cast<uint16_t*>(reply) = 0xF0C;
            reinterpret_cast<int32_t*>(reply)[1] = own_idx(net);
            NetDaemon_send_message(net, reply, 0x20, from);
            g_table.clear();
            NetMsg_close(&NetMsg);
        }
        break;
    }
    case 0xF0F:
        if (host(net)) g_ready |= 1u << (m[1] & 31);
        break;
    }
}
#else
void net_files_message(uint8_t*, uint8_t*, int32_t, int32_t) {}
#endif
