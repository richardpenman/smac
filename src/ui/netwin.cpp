// The online game's set up (docs/MULTIPLAYER.md, phase 3): multiplayer_init (0x58DB30),
// AlphaNet_setup (0x4E31E0: the service list, host or join), the MULTIPLAYER SETUP window (NetWin,
// ui module 0x47B380..0x483840: the players, their factions, ready / start) and the network game's
// UI hooks of the engine's NetDaemon (engine/net.h: the chat lines, MultiWin_draw, ...).
//
// SELECTSERVICE (jackal.txt) lists smacx's one network service, "Internet (TCP/IP)", and the
// original's hotseat/email item (label 0x1128, id 9999: PbemActive). The original's DirectPlay
// services, sessions list (NETCONNECT_SESSIONS) and the service provider's own address dialog are
// replaced by smacx's TCPHOST / TCPJOIN popups (smacxnet.txt, written next to the per-user files):
// the player's name, the host's address and the port (prefs "Net Name", "Net Address",
// "Net Port"). Then NETCONNECT_JOIN_OR_CREATE (Host / Join) as the original.
//
// NetWin keeps the original's data and protocol: the players' settings (0x90DB98, 0x17C bytes per
// player index: +1 ready, +2 difficulty, +3 the faction chosen (-1: random), +5.. the leader's names,
// +0x178 the password's checksum), the session's (NetConfigValue 0x90E8E0, NetSetupType's block
// 0x90E778), the faction data (NetWin_setup_faction_data: the host's alphax.txt FACTIONS /
// NEWFACTIONS / CUSTOMFACTIONS, 400 bytes per faction, sent as 0x0F0D), the packets of
// send_all_player_packets / send_session_packet, the ready button (NetWin_on_button_clicked 10: a
// client's 0x2F04, the host's start when every player is ready, NOTALLREADY otherwise) and
// NetWin_prepare_game (the random factions, Random_reseed(1000), the factions read from their
// files, the leaders' names).
// The window lists the players (faction, difficulty, ready), the host's session settings
// (NetWin_draw_session_info's lines; a click: the NetWin_pick_* lists, sent as 0x2F02) and the
// rules' check boxes (NetWin_draw_custom_rules; a click toggles them), the chat lines; F (or a
// click on one's row) chooses the own faction, C types a chat line, Enter is READY / START, Esc
// cancels. Approximated: the original's 800x600 art and exact layout (a plain box here), the
// PopMenu lists (popup lists), the number of players, the type of game (a random map only: no
// scenario or saved game, NetSetupType 0), the passwords and the chat EditBox.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/setup.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/world.h"
#include "engine/zoc.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/techwin.h"
#include "ui/topmenu.h"
#include "ui/win.h"

namespace {
int32_t& fld(int32_t off) { return *reinterpret_cast<int32_t*>(&NetState + off); }
uint8_t* cfg(int32_t idx) { return mem::host(0x90DB98 + (uint32_t)idx * 0x17C); }
uint8_t* config_value() { return mem::host(0x90E8E0); }  // NetConfigValue (6 dwords)
int32_t& config_rules() { return *reinterpret_cast<int32_t*>(mem::host(0x90E8EC)); }
int32_t& config_state() { return *reinterpret_cast<int32_t*>(mem::host(0x90E8F0)); }
int32_t& config_mrules() { return *reinterpret_cast<int32_t*>(mem::host(0x90E8F4)); }
int32_t players() { return fld(0x768); }
int32_t own_pid() { return fld(0x760); }
int32_t host_pid() { return fld(0x764); }

// The faction data (NetWin +0x772C): [0] 7, [1] the number of factions, then 400 bytes per faction
// from +0x10 (+4 a password, +8 the colour, +9 available, +10 the file name, +0x22.. the names).
std::vector<uint8_t> g_blob;
int32_t g_start = 0;  // NetWin +0x7614: > 0 start, -1 cancelled
std::vector<std::string> g_chat;
bool g_visible = false;
bool g_trace = std::getenv("SMACX_NETTRACE") != nullptr;

const char* kTexts =
    "#TCPHOST\r\n#xs 440\r\n#caption Host Internet Game (TCP/IP)\r\n#editbox\r\n#itemlist\r\n"
    "Your name:\r\nPort:\r\n\r\n"
    "#TCPJOIN\r\n#xs 440\r\n#caption Join Internet Game (TCP/IP)\r\n#editbox\r\n#itemlist\r\n"
    "Your name:\r\nHost address:\r\nPort:\r\n\r\n"
    "#TCPFAILED\r\n#xs 440\r\n#caption Internet (TCP/IP)\r\n^^Could not connect:\r\n^^$TEXT0\r\n\r\n"
    "#NETCHAT\r\n#xs 440\r\n#caption Chat\r\n#editbox\r\n#itemlist\r\nMessage:\r\n\r\n"
    "#NETPICK\r\n#xs 300\r\n\r\n#END\r\n";

void write_texts() {
    static bool done = false;
    if (done) return;
    done = true;
    std::string path = g_ui_outdir + "/smacxnet.txt";
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fputs(kTexts, f);
        std::fclose(f);
    }
    platform::set_builtin_file("smacxnet.txt", path.c_str());
}
}  // namespace

// ---- the engine's hooks ----

bool net_trace_enabled() { return g_trace; }

void ui_net_turn(const char* stage) {
    char line[160];
    std::snprintf(line, sizeof(line), "net: turn %d %s (faction %d)", CurrentTurn, stage, CurrentPlayerFaction);
    ui_net_note(line);
}

bool ui_netwin_visible() { return g_visible; }
void ui_net_note_text(const char* text) { ui_net_note(text); }
void ui_netwin_cancel() { g_start = -1; }
const uint8_t* ui_netwin_faction_blob() { return g_blob.empty() ? nullptr : g_blob.data(); }

void ui_netwin_chat(int32_t idx, const char* text) {
    const char* name = reinterpret_cast<const char*>(&NetState + 0x76C + idx * 0x19C);
    g_chat.push_back(std::string(name) + ": " + text);
    ui_net_note(std::string("net: chat ") + g_chat.back());
    ui_invalidate();
}

void ui_main_chat(const char* text, int32_t faction_id) {
    (void)faction_id;
    ui_net_note(std::string("net: chat ") + text);
    ui_set_message(text);
}


void ui_multiwin_draw() { ui_invalidate(); }
void ui_net_close_windows() {}
void ui_console_set_ai(int32_t faction_id) { Console_set_ai(&MainConsole, faction_id); }
void ui_console_set_view() { Console_set_view(&MainConsole, 0); }
void ui_net_end_of_turn() {
    if (Win_is_visible(&BaseWin)) BaseWin_exit(&BaseWin);
    go_reset();
}
void ui_net_center(int32_t x, int32_t y) {
    pfncMoveMap(MapWins[0], x, y, 1);
    Console_set_cursor(&MainConsole, x, y);
}
void ui_net_clear_messages() { MessageWin_clear(&MessageWin); }

void GAMEAPI do_all_tasks() {
    if (MultiplayerActive) Net_handle_sys_msg(&NetState);
    do_all_non_input();
}

// ---- NetWin: the faction data, the players ----

void GAMEAPI NetWin_config_players() {
    NetSetupType = 0;
    *reinterpret_cast<int32_t*>(mem::host(0x90E79C)) = 0;
    uint8_t* v = config_value();
    v[0] = 0xFF;
    v[1] = mem::host(0x93D4B0)[0];
    v[2] = 2;
    v[4] = 0;
    v[5] = 0;
    v[6] = 2;
    v[7] = 2;
    v[8] = 2;
    v[9] = 2;
    v[10] = 2;
    v[11] = 0xFF;
    for (int32_t i = 1; i < 8; i++) {
        uint8_t* p = cfg(i);
        *reinterpret_cast<int32_t*>(p + 0x178) = 0;
        p[0] = 0xFF;
        p[1] = 0;
        p[2] = v[2];
        p[3] = 0xFF;
        p[4] = 2;
        const MFaction& m = MFactions[i];
        crt_strcpy(reinterpret_cast<char*>(p + 5), m.name_leader);
        crt_strcpy(reinterpret_cast<char*>(p + 0x1D), m.formal_name_faction);
        crt_strcpy(reinterpret_cast<char*>(p + 0x45), label_get(0xFE4 / 4));
        crt_strcpy(reinterpret_cast<char*>(p + 0x5D), m.adj_name_faction);
        crt_strcpy(reinterpret_cast<char*>(p + 0xDD), m.title_leader);
        crt_strcpy(reinterpret_cast<char*>(p + 0xF5), m.adj_leader);
        p[0x175] = (uint8_t)m.is_leader_female;
    }
}

// NetWin_setup_faction_data (0x481FD0): the host (msg null) reads the factions' files; a player
// receives the host's (0x0F0D).
void ui_netwin_faction_data(const uint8_t* msg) {
    if (msg) {
        if (own_pid() == host_pid()) return;
        int32_t n = std::max<int32_t>(8, msg[0x11]);
        int32_t size = n * 399 + 0x194;
        g_blob.assign(msg + 0x10, msg + 0x10 + size);
        ui_invalidate();
        return;
    }
    std::vector<std::string> files;
    auto section = [&](const char* name, int32_t max) {
        if (text_open("alphax", name)) return;
        for (int32_t i = 0; i < max; i++) {
            text_get();
            const char* item = text_item();
            if (!item || !*item) break;
            files.push_back(item);
        }
    };
    section("FACTIONS", 7);
    section("NEWFACTIONS", 7);
    section("CUSTOMFACTIONS", 10);
    text_close();
    int32_t n = (int32_t)files.size();
    int32_t size = std::max(n, 8) * 399 + 0x194;
    g_blob.assign((size_t)size, 0);
    g_blob[0] = 7;
    g_blob[1] = (uint8_t)n;
    for (int32_t i = 0; i < n; i++) {
        uint8_t* e = &g_blob[(size_t)i * 400];
        e[9] = 1;
        crt_strncpy(reinterpret_cast<char*>(e + 10), files[i].c_str(), 0x18);
        MFaction mf;
        std::memset(&mf, 0, sizeof(mf));
        crt_strcpy(mf.filename, files[i].c_str());
        crt_strcpy(mf.search_key, files[i].c_str());
        read_faction(&mf, 0);
        crt_strcpy(reinterpret_cast<char*>(e + 0x22), mf.name_leader);
        crt_strcpy(reinterpret_cast<char*>(e + 0x3A), mf.formal_name_faction);
        crt_strcpy(reinterpret_cast<char*>(e + 0x62), mf.noun_faction);
        crt_strcpy(reinterpret_cast<char*>(e + 0x7A), mf.adj_name_faction);
        crt_strcpy(reinterpret_cast<char*>(e + 0xFA), mf.title_leader);
        crt_strcpy(reinterpret_cast<char*>(e + 0x112), mf.adj_leader);
        e[0x192] = (uint8_t)mf.is_leader_female;
    }
    text_close();
}

// NetWin_fill_player (0x481DA0): a player's names from the faction chosen (-1: random).
void ui_netwin_fill_player(uint8_t* p, int32_t faction) {
    if (faction == -1) {
        crt_strcpy(reinterpret_cast<char*>(p + 0x45), label_get(0xFE4 / 4));
        return;
    }
    if (g_blob.empty()) return;
    const uint8_t* e = &g_blob[(size_t)faction * 400];
    crt_strcpy(reinterpret_cast<char*>(p + 5), reinterpret_cast<const char*>(e + 0x22));
    crt_strcpy(reinterpret_cast<char*>(p + 0x1D), reinterpret_cast<const char*>(e + 0x3A));
    crt_strcpy(reinterpret_cast<char*>(p + 0x45), reinterpret_cast<const char*>(e + 0x62));
    crt_strcpy(reinterpret_cast<char*>(p + 0x5D), reinterpret_cast<const char*>(e + 0x7A));
    crt_strcpy(reinterpret_cast<char*>(p + 0xDD), reinterpret_cast<const char*>(e + 0xFA));
    crt_strcpy(reinterpret_cast<char*>(p + 0xF5), reinterpret_cast<const char*>(e + 0x112));
    p[0x175] = e[0x192];
}

void ui_netwin_pick_pass(int32_t) { X_pop("PASSWORDSNOTVALIDATED", 0); }

namespace {
// The Random object of NetWin_prepare_game (0x625730..0x6257A4).
struct Random {
    uint32_t seed = 0;
    void reseed(uint32_t s) { seed = s; }
    int32_t get(int32_t lo, int32_t hi) {
        if (lo > hi) std::swap(lo, hi);
        seed = seed * 0x19660D + 0x3C6EF35F;
        return (int32_t)(((uint32_t)(hi - lo) * (seed & 0xFFFF)) >> 16) + lo;
    }
};
}  // namespace

// NetWin_prepare_game (0x482D90): the random factions, the slots, the factions read.
static void NetWin_prepare_game() {
    Random rnd;
    rnd.reseed(1000);
    int32_t count = g_blob.empty() ? 0 : g_blob[1];
    uint32_t taken = 0;
    for (int32_t i = 0; i < count; i++) {
        if (!g_blob[(size_t)i * 400 + 9]) taken |= 1u << i;
    }
    for (int32_t i = 1; i < 8; i++) {
        if (cfg(i)[3] != 0xFF) taken |= 1u << (cfg(i)[3] & 31);
    }
    int32_t free_count = 0;
    for (int32_t i = 0; i < count; i++) {
        if (g_blob[(size_t)i * 400 + 9] && !(taken & (1u << i))) free_count++;
    }
    uint32_t randomized = 0;
    for (int32_t i = 1; i < 8; i++) {
        uint8_t& f = cfg(i)[3];
        if (f != 0xFF) continue;
        randomized |= 1u << i;
        int32_t pick;
        if (!free_count) {
            pick = rnd.get(0, count);
        } else {
            do {
                pick = rnd.get(0, count);
            } while (taken & (1u << (pick & 31)));
            free_count--;
        }
        f = (uint8_t)pick;
        taken |= 1u << (pick & 31);
    }
    // The AlphaNet slots take the players' settings; player i plays faction i (NetSetupType 0 / 1).
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* slot = &NetState + 0x790 + i * 0x19C;
        std::memcpy(slot, cfg(i), 0x176);
        slot[0] = (uint8_t)i;
    }
    // The same leader name twice: numbered.
    for (int32_t i = 2; i < 8; i++) {
        char* name = reinterpret_cast<char*>(cfg(i) + 0x1D);
        int32_t same = 0;
        for (int32_t k = 1; k < i; k++) {
            if (!crt_strcmp(name, reinterpret_cast<char*>(cfg(k) + 0x1D))) same++;
        }
        if (same) {
            char num[16];
            std::snprintf(num, sizeof(num), "%d", same);
            crt_strcat(name, num);
        }
    }
    for (int32_t f = 1; f < 8; f++) {
        const char* file = reinterpret_cast<const char*>(&g_blob[(size_t)cfg(f)[3] * 400 + 10]);
        crt_strcpy(MFactions[f].filename, file);
        crt_strcpy(MFactions[f].search_key, file);
        read_faction2(f);
        compute_faction_modifiers(f);
    }
    for (int32_t i = 1; i < 8; i++) {
        uint8_t* slot = &NetState + 0x76C + i * 0x19C;
        int32_t f = (int8_t)slot[0x24];
        *reinterpret_cast<int32_t*>(mem::host(0x96C9E8 + (uint32_t)f * 0x20CC)) = (int8_t)slot[0x26];  // difficulty
        if (randomized & (1u << i)) continue;
        uint8_t* p = cfg(i);
        MFaction& m = MFactions[f];
        crt_strcpy(m.name_leader, reinterpret_cast<char*>(p + 5));
        crt_strcpy(m.formal_name_faction, reinterpret_cast<char*>(p + 0x1D));
        crt_strcpy(m.noun_faction, reinterpret_cast<char*>(p + 0x45));
        crt_strcpy(m.adj_name_faction, reinterpret_cast<char*>(p + 0x5D));
        crt_strcpy(m.title_leader, reinterpret_cast<char*>(p + 0xDD));
        crt_strcpy(m.adj_leader, reinterpret_cast<char*>(p + 0xF5));
        m.is_leader_female = p[0x175];
    }
    load_flags();
}

namespace {
struct NetWinState : ScreenWin {
    Font font;
    Font* small = nullptr;
    void redraw() override;
    void on_key(int32_t key) override;
    void on_button(int32_t id) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
};

std::string faction_name(int32_t f) {
    if (f == 0xFF || g_blob.empty()) return label_get(0xFE4 / 4);  // random
    return reinterpret_cast<const char*>(&g_blob[(size_t)f * 400 + 0x62]);
}
std::string lab(uint32_t off) { return label_get(off / 4); }

// The game rules' check boxes (NetWin_draw_custom_rules: SCRIPT's RULES lines; index 15, Iron Man,
// cannot be changed in a network game): the bit of NetConfigRules (0..14) / NetConfigState (16, 17);
// line 11 (No Unity Survey) shows the bit inverted.
constexpr uint32_t kRuleBit[18] = {0x800, 2, 8, 4, 0x1000, 1, 0x10, 0x20, 0x4000, 0x200, 0x40, 0x100, 0x2000,
                                   0x8000, 0x80, 0, 0x800000, 0x1000000};
bool rule_on(int32_t i) {
    if (i == 15) return false;
    if (i >= 16) return (config_state() & (int32_t)kRuleBit[i]) != 0;
    bool on = (config_rules() & (int32_t)kRuleBit[i]) != 0;
    return i == 11 ? !on : on;
}
std::vector<std::string> g_rule_lines;
void load_rule_lines() {
    if (!g_rule_lines.empty()) return;
    if (X_text_open(ScriptFileName, "RULES")) return;
    for (int32_t i = 0; i < 18; i++) {
        char* line = text_get();
        if (!line) break;
        if (line[0] == '#') {
            i--;
            continue;
        }
        g_rule_lines.push_back(line);
    }
    text_close();
}

// NetWin_draw_session_info's lines: (spot index, label, value).
std::string size_name(int8_t v) {
    if (v == -1) return lab(0xFE4);
    std::string r;
    if (!text_open("alphax", "WORLDSIZE")) {
        text_get();
        for (int32_t i = 0; i <= v; i++) text_get();
        r = text_item();
    }
    return r;
}
std::string value_name(int8_t v, uint32_t base) { return v == -1 ? lab(0xFE4) : lab((uint32_t)(v + (int32_t)base) * 4); }

std::vector<std::pair<int32_t, std::string>> session_lines() {
    const uint8_t* v = config_value();
    std::vector<std::pair<int32_t, std::string>> l;
    l.push_back({0, lab(0x1078) + ": " + lab(0x107C)});  // the map: a random map (NetSetupType 0)
    int32_t d = std::max(0, std::min(5, (int32_t)(int8_t)v[2]));
    l.push_back({2, lab(0xFA4) + ": " + std::string((Difficulty[d] ? Difficulty[d] : ""))});
    l.push_back({3, lab(0x100C) + ": " + std::string(TimeControl[v[3] % 6].name ? TimeControl[v[3] % 6].name : "")});
    l.push_back({4, lab(0xFAC) + ": " + size_name((int8_t)v[6])});
    l.push_back({5, lab(0xFB0) + ": " + value_name((int8_t)v[7], 0x3D8)});
    l.push_back({6, lab(0xFB4) + ": " + value_name((int8_t)v[8], 0x3DB)});
    l.push_back({7, lab(0xFB8) + ": " + value_name((int8_t)v[9], 0x3E4)});
    l.push_back({8, lab(0xFBC) + ": " + value_name((int8_t)v[10], 0x3E1)});
    return l;
}

// The MULTIPLAYER SETUP window: the players (name, faction, difficulty, ready), the host's session
// settings (spots of type 0: the host's pick lists) and the rules (spots of type 1: check boxes),
// the chat lines, the keys' help.
void NetWinState::redraw() {
    UiRect all{0, 0, canvas.width, canvas.height};
    Buffer_fill_3(&canvas, &all, 0);
    Buffer_box_sprite(&canvas, &all, box_def(9));
    texts.clear();
    spot_kill_type(&spots[0], 0);
    spot_kill_type(&spots[0], 1);
    spot_kill_type(&spots[0], 3);
    const bool host = own_pid() == host_pid();
    sw_font(&canvas, &font, 0xFF);
    sw_text(this, &canvas, "MULTIPLAYER SETUP", 20, 12);
    sw_text(this, &canvas, host ? "(host)" : "(player)", 300, 12);
    for (int32_t i = 1; i <= players(); i++) {
        uint8_t* p = cfg(i);
        const char* name = reinterpret_cast<const char*>(&NetState + 0x76C + i * 0x19C);
        int32_t d = std::max(0, std::min(5, (int32_t)(int8_t)p[2]));
        char line[256];
        std::snprintf(line, sizeof(line), "%d. %s - %s - %s%s", i, name, faction_name(p[3]).c_str(),
                      (Difficulty[d] ? Difficulty[d] : ""), p[1] ? " - READY" : "");
        sw_text(this, &canvas, line, 20, 40 + i * 22);
        spot_add(&spots[0], i - 1, 3, 20, 40 + i * 22, 360, 20);
    }
    sw_font(&canvas, small ? small : &font, 0xFF);
    int32_t y = 40;
    for (auto& l : session_lines()) {
        sw_text(this, &canvas, l.second, 420, y);
        if (NetSessionState[0]) spot_add(&spots[0], l.first, 0, 420, y, 360, 18);
        y += 20;
    }
    y += 6;
    load_rule_lines();
    for (int32_t i = 0; i < (int32_t)g_rule_lines.size(); i++, y += 16) {
        std::string t = std::string(rule_on(i) ? "[X] " : "[ ] ") + g_rule_lines[(size_t)i];
        sw_text(this, &canvas, t, 420, y);
        if (NetSessionState[0] && i != 15) spot_add(&spots[0], i, 1, 420, y, 360, 16);
    }
    std::string sim = std::string((config_mrules() & 0x10) ? "[ ] " : "[X] ") + lab(0x10A4);
    sw_text(this, &canvas, sim, 420, y);
    if (NetSessionState[0]) spot_add(&spots[0], 100, 1, 420, y, 360, 16);
    sw_font(&canvas, &font, 0xFF);
    y = 240;
    size_t from = g_chat.size() > 8 ? g_chat.size() - 8 : 0;
    for (size_t i = from; i < g_chat.size(); i++, y += 20) sw_text(this, &canvas, g_chat[i], 20, y);
    sw_text(this, &canvas, host ? "Enter: START   F: faction   C: chat   Esc: cancel"
                                : "Enter: READY   F: faction   C: chat   Esc: cancel",
            20, canvas.height - 30);
}

void send_own_settings(uint8_t* p, int32_t idx) {
    std::vector<uint8_t> m(0x18C, 0);
    *reinterpret_cast<uint16_t*>(&m[0]) = 0x2F04;
    *reinterpret_cast<int32_t*>(&m[4]) = idx;
    std::memcpy(&m[0x10], p, 0x17C);
    Net_send(&NetState, m.data(), 0x18C, host_pid(), 1);
}

// NetWin_on_button_clicked (0x47B450): 10 ready (a player) / start (the host), -2 cancel.
void NetWin_on_button_clicked(int32_t id) {
    if (id == -2) {
        g_start = -1;
        return;
    }
    if (id != 10) return;
    int32_t idx = AlphaNet_pid_2_idx(&NetState, own_pid());
    if (!NetSessionState[0]) {
        uint8_t p[0x17C];
        std::memcpy(p, cfg(idx), 0x17C);
        p[1] = p[1] == 0;
        if (idx > 0) send_own_settings(p, idx);
        return;
    }
    int32_t n = players() + 1;
    cfg(idx)[1] = 1;
    int32_t i = 1;
    while (i < n && cfg(i)[1]) i++;
    cfg(idx)[1] = 0;
    if (i != n && X_pop_2("SCRIPT", "NOTALLREADY", 0) != 0) return;
    for (int32_t k = 1; k < n; k++) cfg(k)[1] = 1;
    send_all_player_packets(-1);
}

// NetWin_pick_faction (0x47C530): a popup of the factions; the choice sent to the host.
void NetWin_pick_faction() {
    if (g_blob.empty()) return;
    int32_t idx = AlphaNet_pid_2_idx(&NetState, own_pid());
    if (idx <= 0) return;
    BasePop* pop = popup_new();
    if (!Popup_start(pop, ScriptFileName, "FACTION", -1, nullptr, 0)) {
        BasePop_item(pop, label_get(0xFE4 / 4), 0xFF);
        for (int32_t f = 0; f < g_blob[1]; f++) {
            if (g_blob[(size_t)f * 400 + 9]) BasePop_item(pop, faction_name(f).c_str(), f);
        }
        int32_t r = BasePop_exec_3(pop, 0, 0);
        if (r >= 0) {
            uint8_t p[0x17C];
            std::memcpy(p, cfg(idx), 0x17C);
            p[3] = (uint8_t)r;
            ui_netwin_fill_player(p, r == 0xFF ? -1 : r);
            send_own_settings(p, idx);
        }
    }
    popup_delete(pop);
}

// The host's session packet (NetWin_on_left_click / the pick lists: 0x2F02 with NetConfigValue to
// the host, who sends it to everyone).
void send_session() {
    uint8_t m[0x28] = {};
    *reinterpret_cast<uint16_t*>(m) = 0x2F02;
    *reinterpret_cast<int32_t*>(m + 4) = AlphaNet_pid_2_idx(&NetState, own_pid());
    std::memcpy(m + 0x10, config_value(), 0x18);
    Net_send(&NetState, m, 0x28, host_pid(), 1);
}

// NetWin_pick_num_players / _global_diff / _time / _planet_size / _ocean / _erosion / _native /
// _clouds (0x47E010..0x47F5F0): the PopMenu of the values (here a popup list); the choice in
// NetConfigValue, sent.
void NetWin_pick(int32_t index) {
    uint8_t* v = config_value();
    BasePop* pop = popup_new();
    int32_t r = -1;
    auto run = [&]() { r = BasePop_exec_3(pop, 0, 0x483240); };
    if (index == 3) {
        time_controls_dialog(pop);
        BasePop_set_active(pop, v[3]);
        run();
        if (r >= 0) v[3] = (uint8_t)r;
    } else if (!Popup_start(pop, "smacxnet", "NETPICK", -1, nullptr, 0)) {
        int32_t first = 0;
        if (index == 1) {
            for (int32_t n = 2; n < 8; n++) BasePop_item(pop, std::to_string(n).c_str(), n);
            first = v[1];
        } else if (index == 2) {
            for (int32_t d = 0; d < 6; d++)
                BasePop_item(pop, (Difficulty[d] ? Difficulty[d] : ""), d);
            first = v[2];
        } else {
            BasePop_item(pop, lab(0xFE4).c_str(), 0);
            int32_t count = 3;
            uint32_t base = index == 5 ? 0x3D8 : index == 6 ? 0x3DB : index == 7 ? 0x3E4 : 0x3E1;
            if (index == 4) count = 5;
            for (int32_t i = 0; i < count; i++)
                BasePop_item(pop, (index == 4 ? size_name((int8_t)i) : lab((uint32_t)(i + (int32_t)base) * 4)).c_str(), i + 1);
            const uint32_t off[9] = {0, 0, 0, 0, 6, 7, 8, 9, 10};
            first = (int8_t)v[off[index]] + 1;
        }
        BasePop_set_active(pop, first);
        run();
        if (r >= 0) {
            if (index == 1) v[1] = (uint8_t)std::max(2, std::min(7, r));
            else if (index == 2) v[2] = (uint8_t)r;
            else {
                const uint32_t off[9] = {0, 0, 0, 0, 6, 7, 8, 9, 10};
                v[off[index]] = (uint8_t)(r - 1);
            }
        }
    }
    popup_delete(pop);
    char note[64];
    std::snprintf(note, sizeof(note), "net: setting %d chosen %d", index, r);
    ui_net_note(note);
    send_session();
}

void NetWinState::on_click(int32_t, int32_t index, int32_t type, int32_t, int32_t, bool right, bool) {
    if (right || index < 0 || !NetSessionState[0]) {
        if (!right && type == 3 && index + 1 == AlphaNet_pid_2_idx(&NetState, own_pid())) NetWin_pick_faction();
        return;
    }
    if (type == 0) {
        if (index >= 1 && index <= 8) NetWin_pick(index);  // (0: the map: a random map only)
    } else if (type == 1) {
        // NetWin_on_left_click: a rule toggled, the session packet sent.
        if (index == 100) config_mrules() ^= 0x10;
        else if (index >= 16 && index < 18) config_state() ^= (int32_t)kRuleBit[index];
        else if (index >= 0 && index < 15) config_rules() ^= (int32_t)kRuleBit[index];
        char note[64];
        std::snprintf(note, sizeof(note), "net: rule %d toggled", index);
        ui_net_note(note);
        send_session();
    } else if (type == 3 && index + 1 == AlphaNet_pid_2_idx(&NetState, own_pid())) {
        NetWin_pick_faction();
    }
    texts.clear();
    redraw();
    ui_invalidate();
}

void NetWin_chat() {
    write_texts();
    BasePop* pop = popup_new();
    ParseStrBuffer[0][0] = 0;
    if (!Popup_start(pop, "smacxnet", "NETCHAT", -1, nullptr, 0) && BasePop_exec_3(pop, 0, 0) >= 0 &&
        ParseStrBuffer[0][0]) {
        // The setup window's chat (NetWin_on_key_down): to each other player.
        for (int32_t i = 1; i <= players(); i++) {
            int32_t pid = fld(0x78C + i * 0x19C);
            if (pid != own_pid()) message_chat(0x1E00, pid, ParseStrBuffer[0]);
        }
        ui_netwin_chat(AlphaNet_pid_2_idx(&NetState, own_pid()), ParseStrBuffer[0]);
    }
    popup_delete(pop);
}

void NetWinState::on_key(int32_t key) {
    int32_t k = key & 0xFFFF;
    if (k == 0x1B) NetWin_on_button_clicked(-2);
    else if (k == 0x0D) NetWin_on_button_clicked(10);
    else if (k == 'F' || k == 'f') NetWin_pick_faction();
    else if (k == 'C' || k == 'c') NetWin_chat();
}

void NetWinState::on_button(int32_t id) { NetWin_on_button_clicked(id); }

NetWinState* g_win = nullptr;
}  // namespace

// NetWin_on_redraw: the window drawn again (a player's or the session's packet received).
void ui_netwin_redraw() {
    if (g_win) {
        g_win->texts.clear();
        g_win->redraw();
    }
    ui_invalidate();
}

// The idle hook while the setup window is up (NetWin_modal_callback_daemon 0x483240): the
// messages, then the start once every player is ready.
static bool netwin_tick() {
    if (!g_win) return false;
    if (NetDaemon_receive(&NetState)) {
        while (NetDaemon_receive(&NetState)) {}
        ui_invalidate();
    }
    Net_handle_sys_msg(&NetState);
    if (!g_start) {
        bool all = players() > 0;
        for (int32_t i = 1; i <= players(); i++) {
            if (!cfg(i)[1]) all = false;
        }
        if (all) g_start = 1;
    }
    if (!g_start) return false;
    sw_close(g_win);
    return true;
}

// NetWin_exec (0x482490).
static int32_t NetWin_exec() {
    if (ControlTurnA) return 0;
    ui_netwin_faction_data(nullptr);
    if (NetSessionState[0]) send_all_player_packets(-1);
    NetSessionState[4] = 1;  // the phase: setup
    g_start = 0;
    g_chat.clear();
    if (NetSessionState[0]) {
        config_rules() = *reinterpret_cast<int32_t*>(mem::host(0x94B470));
        config_value()[2] = mem::host(0x94B350)[0];
        config_mrules() |= 0x10;
        mem::host(0x90DD16)[0] = config_value()[2];
        config_state() = *reinterpret_cast<int32_t*>(mem::host(0x94B474));
        send_session_packet(0, 0);
    }
    NetWinState w;
    w.name = "netwin";
    Font_ctor(&w.font);
    Font_init(&w.font, "Arial", 0xE, 1);
    sw_init(&w, (ScreenWidth - 800) / 2, (ScreenHeight - 600) / 2, 800, 600, false);
    g_win = &w;
    g_visible = true;
    ui_set_idle_hook(netwin_tick);
    ui_net_note("net: setup window open");
    sw_run(&w);
    ui_set_idle_hook(nullptr);
    g_visible = false;
    g_win = nullptr;
    if (g_start > 0) NetWin_prepare_game();
    NetSessionState[4] = 2;  // the phase: the game
    char line[80];
    std::snprintf(line, sizeof(line), "net: setup window closed (%s, %d players)", g_start > 0 ? "start" : "cancel",
                  players());
    ui_net_note(line);
    return ControlTurnA ? 0 : g_start;
}

// ---- AlphaNet_setup (0x4E31E0): the service, host or join ----

namespace {
// AlphaNet_pick_service (0x4E26E0): SELECTSERVICE; 0 a network service chosen, 1 cancelled.
int32_t pick_service() {
    PbemActive = 0;
    BasePop* pop = popup_new();
    int32_t r = 1;
    if (BasePop_start(pop, "jackal", "SELECTSERVICE", -1, nullptr, 0x42, nullptr) == 0) {
        // (The original lists its DirectPlay services first; smacx lists the hotseat/email item
        // first so that it stays the default choice, as with no network service installed.)
        BasePop_item(pop, label_get(0x1128 / 4), 9999);
        BasePop_item(pop, "Internet (TCP/IP)", 0);
        int32_t a = BasePop_exec_3(pop, 0, 0);
        if (a == 9999) {
            PbemActive = 1;
            r = 0;
        } else if (a == 0) {
            r = 0;
        }
    }
    popup_delete(pop);
    return r;
}

void pref(const char* key, const char* def, char* out, size_t size) {
    prefs_get(key, def, 0);
    std::snprintf(out, size, "%s", TextBufferGetPtr);
}

int32_t ask(const char* label, int32_t fields) {
    write_texts();
    BasePop* pop = popup_new();
    int32_t r = -1;
    if (!Popup_start(pop, "smacxnet", label, -1, nullptr, 0)) r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    (void)fields;
    return r;
}

void failed(const char* error) {
    write_texts();
    parse_says(0, error, -1, -1);
    BasePop* pop = popup_new();
    if (!Popup_start(pop, "smacxnet", "TCPFAILED", -1, nullptr, 0)) BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
}

// AlphaNet_create (0x4E2A30) / AlphaNet_join (0x4E2E50): non-zero when the session is set up.
int32_t create(uint8_t* net) {
    char name[64], port[16];
    pref("Net Name", "Player", name, sizeof(name));
    pref("Net Port", "28100", port, sizeof(port));
    for (;;) {
        parse_says(0, name, -1, -1);
        parse_says(1, port, -1, -1);
        if (ask("TCPHOST", 2) < 0) return 0;
        if (ParseStrBuffer[0][0]) break;
        X_pop_2("jackal", "NETCONNECT_NEED_PLAYER", 0);
    }
    prefs_put("Net Name", ParseStrBuffer[0]);
    prefs_put("Net Port", ParseStrBuffer[1]);
    char error[200];
    if (net_link_host(net, (uint16_t)std::atoi(ParseStrBuffer[1]), ParseStrBuffer[0], error, sizeof(error))) {
        failed(error);
        return 0;
    }
    return 1;
}

int32_t join(uint8_t* net) {
    char name[64], address[128], port[16];
    pref("Net Name", "Player", name, sizeof(name));
    pref("Net Address", "127.0.0.1", address, sizeof(address));
    pref("Net Port", "28100", port, sizeof(port));
    for (;;) {
        parse_says(0, name, -1, -1);
        parse_says(1, address, -1, -1);
        parse_says(2, port, -1, -1);
        if (ask("TCPJOIN", 3) < 0) return 0;
        if (ParseStrBuffer[0][0]) break;
        X_pop_2("jackal", "NETCONNECT_NEED_PLAYER", 0);
    }
    prefs_put("Net Name", ParseStrBuffer[0]);
    prefs_put("Net Address", ParseStrBuffer[1]);
    prefs_put("Net Port", ParseStrBuffer[2]);
    char error[200];
    if (net_link_join(net, ParseStrBuffer[1], (uint16_t)std::atoi(ParseStrBuffer[2]), ParseStrBuffer[0], error,
                      sizeof(error))) {
        failed(error);
        return 0;
    }
    return 1;
}
}  // namespace

int32_t THISCALL AlphaNet_setup(uint8_t* net, int32_t lobby) {
    for (int32_t i = 0; i < 8; i++) *reinterpret_cast<int32_t*>(net + 0x78C + i * 0x67 * 4) = 0;
    *reinterpret_cast<int32_t*>(net + 0x768) = 0;
    Net_close(net);
    PbemActive = 0;
    if (lobby) return 1;  // (no lobby launch in smacx)
    if (pick_service()) return 1;
    if (PbemActive) {
        Net_close(net);
        return 0;
    }
    BasePop* pop = popup_new();
    int32_t r = -1;
    if (!BasePop_start(pop, "jackal", "NETCONNECT_JOIN_OR_CREATE", -1, nullptr, 0x50, nullptr))
        r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0) return 1;
    if (!(r == 1 ? join(net) : create(net))) return 1;
    *reinterpret_cast<int32_t*>(net + 0x48) = 0xDC;
    *reinterpret_cast<int32_t*>(net + 0x4C) = 0x100DC;
    return 0;
}

// ---- multiplayer_init (0x58DB30) ----

int32_t GAMEAPI multiplayer_init(int32_t lobby) {
    int32_t result = 1;
    MultiplayerActive = 1;
    log_set_state(0);
    NetWin_config_players();
    bool ok = true;
    if (!lobby) {
        if (NetDaemon_init(&NetState, 0)) ok = false;
    }
    if (ok && PbemActive) {
        MultiplayerActive = 0;
        int32_t r = top_menu(1);
        if (!r) return 0;
        PbemActive = 0;
        return r;
    }
    if (ok) {
        NetSessionState[6] = 0;
        int32_t start = 0;
        if (!ControlTurnA) {
            // The players' events (joins) before the window: the host's own slot first.
            Net_handle_sys_msg(&NetState);
            start = NetWin_exec();
        }
        if (start > 0) {
            uint8_t* v = config_value();
            DiffLevel = (int8_t)v[2];
            GameRules = config_rules();
            GameState |= config_state() & 0x1800000;
            if (config_mrules() & 0x10) GameMoreRules |= 0x10;
            else GameMoreRules &= ~0x10;
            if (NetSessionState[0]) {
                FactionStatus[0] = 0;
                for (int32_t i = 1; i <= players(); i++) FactionStatus[0] |= (uint8_t)(1 << ((&NetState)[0x790 + i * 0x19C] & 31));
                if (NetSetupType == 0) {
                    int32_t sizes_x[32], sizes_y[32], n = 0;
                    if (!text_open("alphax", "WORLDSIZE")) {
                        text_get();
                        n = std::min(text_item_number(), 32);
                        for (int32_t i = 0; i < n; i++) {
                            text_get();
                            text_item();
                            sizes_x[i] = text_item_number();
                            sizes_y[i] = text_item_number();
                        }
                        int8_t size = (int8_t)v[6];
                        MapSizePlanet = size == -1 ? game_rand() % 5 : size;
                        MapAreaY = sizes_y[MapSizePlanet];
                        MapAreaX = sizes_x[MapSizePlanet] << 1;
                    } else {
                        MapSizePlanet = 2;
                        MapAreaX = 0x28;
                        MapAreaY = 0x50;
                    }
                    map_init();
                    Path_init(&Paths);
                    GameDrawState |= 8;
                    MapOceanCoverage = 0;  // (MultiplayerActive)
                    MapLandCoverage = 2 - MapOceanCoverage;
                    game_rand();
                    MapOceanCoverage = (int8_t)v[7];
                    MapErosiveForces = (int8_t)v[8];
                    MapCloudCover = (int8_t)v[10];
                    MapNativeLifeForms = (int8_t)v[9];
                    MapPlanetaryOrbit = 1;
                    world_build();
                }
                if (NetSetupType != 3 && NetSetupType != 2) {
                    VehCount = 0;
                    setup_game(0);
                }
            }
            for (int32_t i = 1; i <= players(); i++) {
                int32_t f = (int8_t)(&NetState)[0x790 + i * 0x19C];
                *reinterpret_cast<int32_t*>(mem::host(0x96EA68 + (uint32_t)f * 0x20CC)) =
                    *reinterpret_cast<int32_t*>(cfg(i) + 0x178);  // the password
            }
            GameState |= config_state() & 0x1800000;
            if (config_mrules() & 0x10) GameMoreRules |= 0x10;
            else GameMoreRules &= ~0x10;
            GameTimeControl = v[3];
            CurrentPlayerFaction = AlphaNet_pid_2_who(&NetState, own_pid());
            GameState &= ~0x10;  // (NetSetupType 3: the scenario files, not reconstructed)
            log_reset();
            if (!NetSessionState[0]) {
                std::memcpy(AltNatural, AltNaturalLinear, sizeof(AltNatural));
                while (NetSessionState[6] < 8 && !ControlTurnA) {
                    NetDaemon_receive(&NetState);
                    do_all_tasks();
                }
            } else {
                FactionStatus[0] = 0;
                for (int32_t i = 1; i <= players(); i++) FactionStatus[0] |= (uint8_t)(1 << ((&NetState)[0x790 + i * 0x19C] & 31));
                NetDaemon_send_rules(&NetState);
                NetDaemon_send_game(&NetState);
            }
            Lock_clear(&NetState + 0x14A0);
            char line[160];
            std::snprintf(line, sizeof(line),
                          "net: game started: faction %d, humans %02x, map %dx%d, rules %08X, time control %d, difficulty %d",
                          CurrentPlayerFaction, FactionStatus[0], MapAreaX, MapAreaY, (uint32_t)GameRules, GameTimeControl,
                          DiffLevel);
            ui_net_note(line);
            if (!ControlTurnMove) planetfall(CurrentPlayerFaction);
            result = ControlTurnA;
            if (!ControlTurnA) return 0;
        } else {
            net_session_clear();
        }
    }
    log_set_state(0);
    net_session_clear();
    MultiplayerActive = 0;
    return result;
}
