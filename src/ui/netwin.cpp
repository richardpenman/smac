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
// The window (NetWin_on_redraw and its draw_* parts) has the original's 800x600 layout from
// NetWin_exec's rectangles and art: the session's settings (the host's pick lists: NetWin_pick_*),
// seven player rows (ready mark, password lock, name, faction, difficulty in the player's colour;
// a click on the own row: NetWin_pick_faction / _diff / _pass), LAUNCH GAME / MAKE READY and
// CANCEL, the chat (StringBox and EditBox: Enter sends the line), the custom game options' check
// boxes (the host's clicks toggle them). The type of game (NetWin_pick_map: a random map, a map, a
// scenario, a saved game) and the players seated again in a loaded game (NetWin_pick_faction,
// NetWin_pick_pass). Approximated: the PopMenu lists (popup lists); NetWin_pick_player
// (CUSTOMNAMES) is not reconstructed; NetWin_pick_num_players has no spot (as the original).
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
#include "engine/save.h"
#include "engine/game.h"
#include "engine/score.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
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
std::vector<uint8_t> g_blob_backup;  // NetWin +0x7730: the host's own faction data (a new game's)
int32_t g_start = 0;  // NetWin +0x7614: > 0 start, -1 cancelled
std::vector<std::string> g_chat;
std::vector<int32_t> g_chat_idx;  // each line's player (its colour)
bool g_visible = false;
bool g_trace = std::getenv("SMACX_NETTRACE") != nullptr;

const char* kTexts =
    "#TCPHOST\r\n#xs 440\r\n#caption Host Internet Game (TCP/IP)\r\n#editbox\r\n#itemlist\r\n"
    "Your name:\r\nPort:\r\n\r\n"
    "#TCPJOIN\r\n#xs 440\r\n#caption Join Internet Game (TCP/IP)\r\n#editbox\r\n#itemlist\r\n"
    "Your name:\r\nHost address:\r\nPort:\r\n\r\n"
    "#TCPFAILED\r\n#xs 440\r\n#caption Internet (TCP/IP)\r\n^^Could not connect:\r\n^^$TEXT0\r\n\r\n"
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
const char* ui_user_dir() { return g_ui_outdir.c_str(); }
bool ui_escape_pressed() { return platform::key_down(0x1B); }

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
    g_chat_idx.push_back(idx);
    ui_net_note(std::string("net: chat ") + g_chat.back());
    ui_netwin_redraw();
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

// The faction's text colour: the pixel (4, 0x2F3) of its PCX (faction.pcx when missing).
static int32_t faction_pcx_colour(const char* filename) {
    Buffer b;
    Buffer_ctor(&b);
    std::string file = std::string(filename) + ".pcx";
    if (Buffer_load_pcx(&b, file.c_str(), nullptr, 10, 0xEC)) Buffer_load_pcx(&b, "faction.pcx", nullptr, 10, 0xEC);
    return Buffer_get_pixel(&b, 4, 0x2F3);
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
        e[8] = (uint8_t)faction_pcx_colour(files[i].c_str());
    }
    text_close();
    g_blob_backup = g_blob;
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

namespace {
int32_t& cfg_pass(int32_t idx) { return *reinterpret_cast<int32_t*>(cfg(idx) + 0x178); }
int32_t blob_pass(int32_t faction) { return *reinterpret_cast<int32_t*>(&g_blob[(size_t)faction * 400 + 4]); }
bool loaded_game() { return NetSetupType == 2 || NetSetupType == 3; }

// A player's settings to the host (0x2F04): the own index's 0x17C bytes.
void send_settings(const uint8_t* p, int32_t idx) {
    std::vector<uint8_t> m(0x18C, 0);
    *reinterpret_cast<uint16_t*>(&m[0]) = 0x2F04;
    *reinterpret_cast<int32_t*>(&m[4]) = idx;
    std::memcpy(&m[0x10], p, 0x17C);
    Net_send(&NetState, m.data(), 0x18C, host_pid(), 1);
}
}  // namespace

// NetWin_pick_pass (0x47C970). A new game (NetSetupType 0 / 1, a click on the own row's password
// spot, a1 0): ENTERPASSWORDNEW and VERIFYPASSWORD (DIFFERENTPASSWORDS until they agree; blank:
// none), the checksum (checksum_password) sent to the host in the player's settings. A saved game
// or a scenario (a faction chosen, or the host's 0x0F0E, a1 1): a faction with a password asks
// ENTERPASSWORDFACTION; a wrong one: BADPASSWORD (0); else the checksum kept in the player's
// settings for the host's check (process_message 0x2F04). Without a password: PASSWORDOK (a1 0).
// 0: cancelled / refused.
int32_t NetWin_pick_pass(int32_t a1) {
    int32_t idx = AlphaNet_pid_2_idx(&NetState, own_pid());
    bool loaded = loaded_game();
    if (!loaded && a1) return 1;
    int32_t faction = (int8_t)cfg(idx)[3];
    if (faction == -1) {
        cfg_pass(idx) = 0;
        return 1;
    }
    if (loaded && !g_blob.empty() && (blob_pass(faction) == 0 || blob_pass(faction) == cfg_pass(idx))) {
        if (!a1) X_pop_2("SCRIPT", "PASSWORDOK", 0);
        return 1;
    }
    for (;;) {
        int32_t sum = 0;
        if (loaded) parse_says(0, reinterpret_cast<const char*>(&g_blob[(size_t)faction * 400 + 0x62]), -1, -1);
        if (X_pop_ask(loaded ? "ENTERPASSWORDFACTION" : "ENTERPASSWORDNEW", 0x50, "", 0, 0x800040) == -1) return 0;
        if (ParseStrBuffer[0][0]) {
            sum = checksum_password(ParseStrBuffer[0]);
            if (!loaded) {
                X_pop_ask("VERIFYPASSWORD", 0x50, "", 0, 0x800000);
                int32_t again = ParseStrBuffer[0][0] ? checksum_password(ParseStrBuffer[0]) : 0;
                if (sum != again) {
                    X_pop("DIFFERENTPASSWORDS", 0);
                    continue;
                }
            }
        }
        if (loaded) {
            if (blob_pass(faction) != sum) {
                ui_net_note("net: bad password");
                X_pop("BADPASSWORD", 0);
                return 0;
            }
            cfg_pass(idx) = sum;
            return 1;
        }
        cfg_pass(idx) = sum;
        uint8_t p[0x17C];
        std::memcpy(p, cfg(idx), 0x17C);
        send_settings(p, idx);
        char note[64];
        std::snprintf(note, sizeof(note), "net: password set (%s)", sum ? "on" : "none");
        ui_net_note(note);
        return 1;
    }
}

void ui_netwin_pick_pass(int32_t a1) { NetWin_pick_pass(a1); }

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
    // The AlphaNet slots take the players' settings: a new game (NetSetupType 0 / 1), player i plays
    // faction i; a saved game or a scenario, the faction chosen (random: 0).
    if (!loaded_game()) {
        for (int32_t i = 0; i < 8; i++) {
            uint8_t* slot = &NetState + 0x790 + i * 0x19C;
            std::memcpy(slot, cfg(i), 0x176);
            slot[0] = (uint8_t)i;
        }
    } else {
        for (int32_t i = 0; i < 8; i++) {
            uint8_t* slot = &NetState + 0x790 + i * 0x19C;
            std::memcpy(slot, cfg(i), 0x176);
            slot[0] = cfg(i)[3] == 0xFF ? 0 : cfg(i)[3];
        }
    }
    if (!loaded_game()) {
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
    } else if (own_pid() != host_pid()) {
        // A player's copy: the factions' files from the host's faction data, their art.
        for (int32_t f = 1; f < 8; f++) {
            crt_strcpy(MFactions[f].filename, reinterpret_cast<const char*>(&g_blob[(size_t)f * 400 + 10]));
            load_faction_art(f);
        }
    }
    load_flags();
}

namespace {
struct NetWinState : ScreenWin {
    Font* big = nullptr;    // find_font(14, bold)
    Font* small = nullptr;  // find_font(12, bold): the rules
    std::string edit;       // the chat's EditBox (+0x10BD)
    BaseButton* ready = nullptr;
    void redraw() override;
    void on_key(int32_t key) override;
    void on_button(int32_t id) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
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

// The type of game (NetWin_draw_session_info): the file's name (none: a random map) and its kind.
std::string game_type_text() {
    const char* name = reinterpret_cast<const char*>(mem::host(0x90E79C));
    std::string t = name[0] ? std::string(name) : lab(0x107C);
    if (NetSetupType >= 1 && NetSetupType <= 3) {
        static const uint32_t kKind[4] = {0, 0x10, 0x1018, 0x1014};
        t += " (" + lab(kKind[NetSetupType]) + ")";
    }
    return t;
}

std::vector<std::pair<int32_t, std::string>> session_lines() {
    const uint8_t* v = config_value();
    std::vector<std::pair<int32_t, std::string>> l;
    l.push_back({0, lab(0x1078) + ": " + game_type_text()});
    int32_t d = std::max(0, std::min(5, (int32_t)(int8_t)v[2]));
    l.push_back({2, lab(0xFA4) + ": " + std::string((Difficulty[d] ? Difficulty[d] : ""))});
    l.push_back({3, lab(0x100C) + ": " + std::string(TimeControl[v[3] % 6].name ? TimeControl[v[3] % 6].name : "")});
    // A file's map: its size, the rest MAP DEFINED (and no pick lists).
    const bool file = mem::host(0x90E79C)[0] != 0;
    std::string defined = lab(0x1000);
    l.push_back({file ? -1 : 4, lab(0xFAC) + ": " +
                                    (file ? std::to_string(v[4]) + " x " + std::to_string(v[5]) : size_name((int8_t)v[6]))});
    l.push_back({file ? -1 : 5, lab(0xFB0) + ": " + (file ? defined : value_name((int8_t)v[7], 0x3D8))});
    l.push_back({file ? -1 : 6, lab(0xFB4) + ": " + (file ? defined : value_name((int8_t)v[8], 0x3DB))});
    l.push_back({file ? -1 : 7, lab(0xFB8) + ": " + (file ? defined : value_name((int8_t)v[9], 0x3E4))});
    l.push_back({file ? -1 : 8, lab(0xFBC) + ": " + (file ? defined : value_name((int8_t)v[10], 0x3E1))});
    return l;
}

// ---- the window's layout (NetWin_exec's rectangles, window coordinates) ----
struct R {
    int32_t l, t, r, b;
    UiRect u() const { return UiRect{l, t, r, b}; }
};
const R kTitle{3, 3, 797, 0x1D};                     // +0xA14
const R kInfo{7, 0x3A, 0x134, 0x180};                // +0xA24: the session's settings
const R kInfoHead{7, 0x24, 0x134, 0x38};             // +0xA34
const R kInfoFrame{4, 0x21, 0x137, 0x183};           // +0xA44 (the union, 3 pixels out)
const R kPlayersHead{0x13D, 0x24, 0x319, 0x38};      // +0xA54
const R kPlayersFrame{0x13A, 0x21, 0x31C, 0xEB};     // +0xA64
const int32_t kRowTop = 0x3A, kRowStep = 0x16, kRowH = 0x14;
const int32_t kNameL = 0x161, kNameR = 0x213;        // +0xA74: the players' names
const int32_t kFacL = 0x215, kFacR = 0x2A1;          // +0xAE4: their factions
const int32_t kDiffL = 0x2A3, kDiffR = 0x319;        // +0xB54: their difficulties
const int32_t kLockL = 0x14F, kLockR = 0x15F;        // +0xBC4: the password's lock
const int32_t kReadyL = 0x13D, kReadyR = 0x14D;      // +0xC54: the ready mark
const R kReadyButton{0x13D, 0xD4, 0x229, 0xE8};      // +0xC34 (button 10)
const R kCancelButton{0x22C, 0xD4, 0x318, 0xE8};     // +0xC44 (button -2)
const R kChat{0x13D, 0xF1, 0x319, 0x162};            // +0xCC4: the StringBox
const R kEdit{0x13D, 0x164, 0x319, 0x180};           // +0xCD4: the EditBox
const R kChatFrame{0x13A, 0xEE, 0x31C, 0x183};       // +0xCE4
const R kRulesHead{7, 0x189, 0x319, 0x19D};          // +0xCF4
const R kRulesFrame{4, 0x186, 0x31C, 0x254};         // +0xD04
const R kRules1{0x1B, 0x19F, 399, 0x251};            // +0xD14
const R kRules2{0x1A5, 0x19F, 0x319, 0x251};         // +0xD24
R row(int32_t i, int32_t l, int32_t r) { return R{l, kRowTop + i * kRowStep, r, kRowTop + i * kRowStep + kRowH}; }

// interface.pcx's small sprites (load_sprites): the drop-down arrow (0x788038, 7x13 at 534, 537),
// the open and closed locks (0x7AC040 / 0x7AC06C, 15x19 at 247 / 263, 71).
Sprite g_arrow, g_unlocked, g_locked;
void load_art() {
    static bool done = false;
    if (done) return;
    done = true;
    Buffer b;
    Buffer_ctor(&b);
    if (Buffer_load_pcx(&b, "interface.pcx", nullptr, 10, 0xEC)) return;
    Buffer_change_color(&b, 0x107, 0x109);
    Buffer_change_color(&b, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(&g_arrow, &b, 0x109, 0x216, 0x219, 7, 0xD, 0);
    pfncSpriteFromCanvasRectTrans(&g_unlocked, &b, 0x109, 0xF7, 0x47, 0xF, 0x13, 0);
    pfncSpriteFromCanvasRectTrans(&g_locked, &b, 0x109, 0x107, 0x47, 0xF, 0x13, 0);
}
void sprite(Buffer* c, Sprite* s, int32_t x, int32_t y) {
    if (s && s->width) pfncSpriteStretchCopyToCanvas(s, c, s->key, x, y);
}
// The drop-down arrow at a pick list's right end, centred in the row.
void arrow(Buffer* c, const R& r) { sprite(c, &g_arrow, r.r - 10, ((r.b - g_arrow.full_height) - r.t) / 2 + r.t); }
// Buffer_write_l_4: left-aligned in the row, centred vertically.
void text_left(NetWinState* w, Buffer* c, const std::string& t, const R& r);
void box(Buffer* c, const R& r, int32_t n) {
    UiRect u = r.u();
    Buffer_box_sprite(c, &u, box_def(n));
}
// A header: box 6 with its label upper-cased, centred (find_font(14, bold), colour 0xE1).
void header(NetWinState* w, const R& r, const std::string& text) {
    box(&w->canvas, r, 6);
    sw_font(&w->canvas, w->big, 0xE1);
    sw_text_cent(w, &w->canvas, sw_upper(text), r.u());
}

void text_left(NetWinState* w, Buffer* c, const std::string& t, const R& r) {
    w->texts.push_back(t);
    if (t.empty() || !c->fonts[0]) return;
    int32_t y = ((r.b - c->fonts[0]->ascent_above) - r.t) / 2 + r.t;
    Buffer_write_multi_font_raw_l(c, t.c_str(), r.l, y, (int32_t)t.size());
}

// NetWin_get_player_color (0x47B380): the colour of the player's faction (a faction another row
// took first: the first faction no row took); random: 0xFF.
int32_t player_color(int32_t idx) {
    if (g_blob.empty()) return 0;
    int32_t f = (int8_t)cfg(idx)[3];
    if (f == -1) return 0xFF;
    int32_t i = 1;
    while (i < idx && (int8_t)cfg(i)[3] != f) i++;
    if (i != idx) {
        f = 0;
        for (; f < g_blob[1]; f++) {
            int32_t k = 1;
            while (k < players() + 1 && (int8_t)cfg(k)[3] != f) k++;
            if (k == players() + 1) break;
        }
    }
    return g_blob[(size_t)f * 400 + 8];
}

// NetWin_draw_session_info (0x480900): the session's settings ("{label: } value", the host's pick
// lists marked with the arrow, spots of type 0), the passwords no player gave yet (a saved game).
void draw_session_info(NetWinState* w) {
    Buffer* c = &w->canvas;
    spot_kill_type(&w->spots[0], 0);
    box(c, kInfoFrame, 3);
    const char* session = *reinterpret_cast<const char* const*>(&NetState + 0x704);
    header(w, kInfoHead, session ? session : "");
    box(c, kInfo, 0);
    sw_font(c, w->big, 0xDF);
    Buffer_set_text_color2(c, 0xE2, -1, 1, 1);
    const bool host = NetSessionState[0] != 0;
    const int32_t x = kInfo.l + 4, wide = kInfo.r - kInfo.l - 8;
    int32_t y = kInfo.t + 4;
    const int32_t h = c->fonts[0] ? Buffer_text_line_height(c) : 16;
    auto line = [&](int32_t spot, const std::string& label, const std::string& value, bool pick, int32_t gap) {
        std::string t = "{" + label + ": } " + value;
        w->texts.push_back(label + ": " + value);
        int32_t right = Buffer_write_multi_font_raw_l(c, t.c_str(), x, y, (int32_t)t.size());
        if (pick && host) {
            sprite(c, &g_arrow, right + 3, y);
            spot_add(&w->spots[0], spot, 0, x, y, std::min(right - x + 0x1E, wide), h);
        }
        y += h * gap;
    };
    const uint8_t* v = config_value();
    int32_t d = std::max(0, std::min(5, (int32_t)(int8_t)v[2]));
    line(2, lab(0xFA4), Difficulty[d] ? Difficulty[d] : "", true, 1);
    line(3, lab(0x100C), TimeControl[v[3] % 6].name ? TimeControl[v[3] % 6].name : "", true, 2);
    line(0, lab(0x1078), game_type_text(), true, 1);
    const bool file = mem::host(0x90E79C)[0] != 0;
    std::string defined = lab(0x1000);
    line(4, lab(0xFAC), file ? std::to_string(v[4]) + " X " + std::to_string(v[5]) : size_name((int8_t)v[6]), !file, 1);
    line(5, lab(0xFB0), file ? defined : value_name((int8_t)v[7], 0x3D8), !file, 1);
    line(6, lab(0xFB4), file ? defined : value_name((int8_t)v[8], 0x3DB), !file, 1);
    line(7, lab(0xFB8), file ? defined : value_name((int8_t)v[9], 0x3E4), !file, 1);
    line(8, lab(0xFBC), file ? defined : value_name((int8_t)v[10], 0x3E1), !file, 2);
    if (loaded_game() && !g_blob.empty()) {
        int32_t missing = 0;
        for (int32_t f = 0; f < 8; f++) {
            int32_t pass = blob_pass(f);
            if (!pass) continue;
            int32_t i = 1;
            while (i <= players() && cfg_pass(i) != pass) i++;
            if (i > players()) missing++;
        }
        line(-1, lab(0x113C), std::to_string(missing), false, 1);
    }
}

// NetWin_draw_player_names / _factions / _diffs (0x480370, 0x47FED0, 0x480180): seven rows, the
// session's players then the computer's (a scenario: as many as its factions); the ready mark (box
// 48 / 50), the password's lock (the own row's spot: NetWin_pick_pass), the name ("Computer"; with
// the leader's), the faction (with the row's own noun when it differs), the difficulty, in the
// player's colour; the own row's (and for the host the computer's rows') pick lists marked.
void draw_players(NetWinState* w) {
    Buffer* c = &w->canvas;
    spot_kill_type(&w->spots[0], 2);
    spot_kill_type(&w->spots[0], 3);
    spot_kill_type(&w->spots[0], 4);
    spot_kill_type(&w->spots[0], 5);
    const int32_t own = AlphaNet_pid_2_idx(&NetState, own_pid());
    const bool host = own_pid() == host_pid();
    box(c, kPlayersFrame, 3);
    header(w, kPlayersHead, lab(0x414));
    for (int32_t i = 0; i < 7; i++) {
        const int32_t idx = i + 1;
        const uint8_t* p = cfg(idx);
        if (NetSetupType == 3 && !g_blob.empty() && i >= g_blob[0]) break;
        R name = row(i, kNameL, kNameR), lock = row(i, kLockL, kLockR), mark = row(i, kReadyL, kReadyR);
        R fac = row(i, kFacL, kFacR), diff = row(i, kDiffL, kDiffR);
        box(c, name, 0);
        box(c, lock, 0);
        box(c, mark, p[1] ? 50 : 48);
        box(c, fac, 0);
        box(c, diff, 0);
        const bool computer = p[0] == 0xFF;
        if (NetSetupType == 3 && computer) continue;
        const int32_t f = (int8_t)p[3];
        bool locked;
        if (loaded_game()) locked = f != -1 && !g_blob.empty() && blob_pass(f) && blob_pass(f) != cfg_pass(idx);
        else locked = cfg_pass(idx) != 0;
        sprite(c, locked ? &g_locked : &g_unlocked, lock.l + 1, lock.t);
        if (!loaded_game() && idx == own) arrow(c, name);
        sw_font(c, w->big, player_color(idx));
        std::string t = computer ? std::string("Computer") : std::string(reinterpret_cast<const char*>(&NetState + 0x76C + idx * 0x19C));
        if (f != -1) {
            t += ":  ";
            t += reinterpret_cast<const char*>(p + 5);
            int32_t same = 0;
            for (int32_t k = 1; k < idx; k++) {
                if (!crt_strcmp(reinterpret_cast<const char*>(cfg(k) + 5), reinterpret_cast<const char*>(p + 5))) same++;
            }
            if (same) t += std::to_string(same);
        }
        UiRect clip{name.l + 4, name.t, name.r - 4, name.b};
        Buffer_set_clip(c, &clip);
        text_left(w, c, t, R{name.l + 4, name.t, name.r - 4, name.b});
        Buffer_set_clip(c, nullptr);
        spot_add(&w->spots[0], i, 2, mark.l, mark.t, mark.r - mark.l, mark.b - mark.t);
        if (!loaded_game()) spot_add(&w->spots[0], i, 2, name.l, name.t, name.r - name.l, name.b - name.t);
        spot_add(&w->spots[0], i, 5, lock.l, lock.t, lock.r - lock.l, lock.b - lock.t);
        // The faction.
        const bool mine = idx == own || (host && computer);
        if (mine) arrow(c, fac);
        sw_font(c, w->big, player_color(idx));
        std::string ft = faction_name(p[3]);
        const char* noun = reinterpret_cast<const char*>(p + 0x45);
        if (crt_strcmp(ft.c_str(), noun)) ft += std::string(":  ") + noun;
        UiRect fclip{fac.l + 4, fac.t, fac.r - 11, fac.b};
        Buffer_set_clip(c, &fclip);
        text_left(w, c, ft, R{fac.l + 4, fac.t, fac.r - 11, fac.b});
        Buffer_set_clip(c, nullptr);
        spot_add(&w->spots[0], i, 3, fac.l, fac.t, fac.r - fac.l, fac.b - fac.t);
        // The difficulty.
        if (mine) arrow(c, diff);
        int32_t dl = std::max(0, std::min(5, (int32_t)(int8_t)p[2]));
        UiRect dclip{diff.l + 4, diff.t, diff.r - 4, diff.b};
        Buffer_set_clip(c, &dclip);
        text_left(w, c, Difficulty[dl] ? Difficulty[dl] : "", R{diff.l + 4, diff.t, diff.r - 4, diff.b});
        Buffer_set_clip(c, nullptr);
        spot_add(&w->spots[0], i, 4, diff.l, diff.t, diff.r - diff.l, diff.b - diff.t);
    }
    // A player's button: MAKE READY / MAKE NOT READY.
    if (!NetSessionState[0] && w->ready && own > 0) w->ready->name = lab(cfg(own)[1] ? 0x1054 : 0x1050);
}

// NetWin_draw_custom_rules (0x47F960): the check boxes (interface.pcx's marks; the host's spots of
// type 1), Simultaneous Moves first, the RULES lines in two columns (eight, then the rest; Iron Man,
// line 15, not offered); a rule on in colour 0xDE, off 0xE4.
void draw_rules(NetWinState* w) {
    Buffer* c = &w->canvas;
    spot_kill_type(&w->spots[0], 1);
    box(c, kRulesFrame, 3);
    box(c, kRulesHead, 0);
    sw_font(c, w->big, 0xE1);
    sw_text_cent(w, c, sw_upper(lab(0xF9C)), kRulesHead.u());
    box(c, kRules1, 0);
    box(c, kRules2, 0);
    const bool host = NetSessionState[0] != 0;
    int32_t x = kRules1.l - 0x14, y = kRules1.t, right = (kRules1.r - kRules1.l) - 6 + x;
    auto item = [&](int32_t spot, bool on, const std::string& text) {
        sprite(c, popup_check_sprite(on ? 2 : 0), x, y);
        if (host) spot_add(&w->spots[0], spot, 1, x, y, right - x, 0x12);
        sw_font(c, w->small, on ? 0xDE : 0xE4);
        w->texts.push_back(std::string(on ? "[X] " : "[ ] ") + text);
        Buffer_write_multi_font_raw_l(c, text.c_str(), x + 0x19, y, (int32_t)text.size());
    };
    item(100, !(config_mrules() & 0x10), lab(0x10A4) + ".");
    y += 0x14;
    load_rule_lines();
    for (int32_t i = 0; i < (int32_t)g_rule_lines.size() && i < 18; i++) {
        if (i == 15) continue;
        item(i, rule_on(i), g_rule_lines[(size_t)i]);
        if (i == 7) {
            x += 0x18A;
            right += 0x18A;
            y -= 0xA0;
        } else {
            y += 0x14;
        }
    }
}

// The chat: the StringBox's last lines (each in its writer's colour: NetWin_string_callback) and
// the EditBox's line.
void draw_chat(NetWinState* w) {
    Buffer* c = &w->canvas;
    box(c, kChatFrame, 3);
    box(c, kEdit, 0);
    box(c, kChat, 0);
    if (!c->fonts[0]) return;
    sw_font(c, w->big, 0xDF);
    const int32_t h = Buffer_text_line_height(c);
    const int32_t rows = std::max(1, (kChat.b - kChat.t - 6) / std::max(1, h));
    size_t from = g_chat.size() > (size_t)rows ? g_chat.size() - (size_t)rows : 0;
    int32_t y = kChat.t + 3;
    UiRect clip{kChat.l + 3, kChat.t + 3, kChat.r - 3, kChat.b - 3};
    Buffer_set_clip(c, &clip);
    for (size_t i = from; i < g_chat.size(); i++, y += h) {
        sw_font(c, w->big, player_color(g_chat_idx[i]));
        sw_text(w, c, g_chat[i], kChat.l + 3, y);
    }
    UiRect eclip{kEdit.l + 3, kEdit.t, kEdit.r - 3, kEdit.b};
    Buffer_set_clip(c, &eclip);
    sw_font(c, w->big, 0x41);
    std::string e = w->edit + "_";
    Buffer_write_multi_font_raw_l(c, e.c_str(), kEdit.l + 4, (kEdit.b - h - kEdit.t) / 2 + kEdit.t, (int32_t)e.size());
    Buffer_set_clip(c, nullptr);
}

// NetWin_on_redraw (0x481AD0): the background (box 9), the title, the rules, the chat's frames,
// the players, the session's settings.
void NetWinState::redraw() {
    load_art();
    if (!big) big = find_font(0xE, 1);
    if (!small) small = find_font(0xC, 1);
    UiRect all{0, 0, canvas.width, canvas.height};
    Buffer_fill_3(&canvas, &all, 0);
    Buffer_box_sprite(&canvas, &all, box_def(9));
    texts.clear();
    box(&canvas, kTitle, 3);
    R inner{kTitle.l + 3, kTitle.t + 3, kTitle.r - 3, kTitle.b - 3};
    box(&canvas, inner, 6);
    sw_font(&canvas, big, 0xE1);
    sw_text_cent(this, &canvas, sw_upper(lab(0x3F0)), kTitle.u());
    draw_rules(this);
    draw_chat(this);
    draw_players(this);
    draw_session_info(this);
}

// SCREENSTATE: the spots' rectangles (the tests click them).
void NetWinState::log_state() {
    std::fprintf(stderr, "screenstate netwin at %d,%d\n", x, y);
    for (const Spot& sp : spots[0].list)
        std::fprintf(stderr, "screenstate netwin spot %d %d at %d,%d %dx%d\n", sp.type, sp.index, x + sp.r.left,
                     y + sp.r.top, sp.r.right - sp.r.left, sp.r.bottom - sp.r.top);
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
        if (idx > 0) send_settings(p, idx);
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

// NetWin_pick_faction (0x47C530): the PopMenu of the factions (here a popup list: Random, then each
// faction available; a saved game's or a scenario's without those other players took); the choice
// (its password asked first: NetWin_pick_pass(1)) sent to the host.
void NetWin_pick_faction(int32_t idx) {
    if (g_blob.empty() || idx <= 0) return;
    BasePop* pop = popup_new();
    if (!Popup_start(pop, ScriptFileName, "FACTION", -1, nullptr, 0)) {
        BasePop_item(pop, label_get(0xFE4 / 4), 0);
        for (int32_t f = 0; f < g_blob[1]; f++) {
            if (!g_blob[(size_t)f * 400 + 9]) continue;
            if (loaded_game()) {
                int32_t i = 1;
                while (i <= players() && (int8_t)cfg(i)[3] != f) i++;
                if (i <= players()) continue;
            }
            BasePop_item(pop, faction_name(f).c_str(), f + 1);
        }
        int32_t r = BasePop_exec_3(pop, 0, 0x483240);
        if (r >= 0) {
            uint8_t& f = cfg(idx)[3];
            uint8_t old = f;
            f = (uint8_t)(r - 1);
            int32_t ok = NetWin_pick_pass(1);
            f = old;
            if (ok) {
                uint8_t p[0x17C];
                std::memcpy(p, cfg(idx), 0x17C);
                p[3] = (uint8_t)(r - 1);
                send_settings(p, idx);
            }
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

// NetWin_pick_map (0x47D300): the type of game (the host). The PopMenu (here a popup list): a new
// game with a random map, LOAD MAP (load_map: a new game on that map, NetGameFileKind 1), LOAD
// SCENARIO and LOAD GAME (load_game: 3 / 2; a scenario for fewer players than there are:
// TOOMANYFORSCENARIO). A file loaded: the factions' passwords and the map's size kept for the
// window, the game's name; the session's difficulty, rules, alive factions (NetConfigValue[0])
// from the game; sent (0x2F02). A saved game or a scenario: the faction data become the game's
// factions (8 entries, the alive ones available with their names, colour and password; the
// players' rows given their names, the faction random), sent to everyone (send_faction_data);
// back to a new game: the host's own faction data again. Then the players' settings (0x4F04),
// NetSetupType's block (0x2F03) and, after load_game, 0x2F0E.
void NetWin_pick_map() {
    int32_t old_kind = NetGameFileKind;
    bool loaded = false;
    BasePop* pop = popup_new();
    int32_t r = -1;
    if (!Popup_start(pop, "smacxnet", "NETPICK", -1, nullptr, 0)) {
        BasePop_item(pop, lab(0x107C).c_str(), 0);
        BasePop_item(pop, lab(0xFD8).c_str(), 1);
        BasePop_item(pop, lab(0xFDC).c_str(), 2);
        BasePop_item(pop, lab(0xFE0).c_str(), 3);
        r = BasePop_exec_3(pop, 0, 0x483240);
    }
    popup_delete(pop);
    char note[96];
    std::snprintf(note, sizeof(note), "net: type of game chosen %d", r);
    ui_net_note(note);
    if (r < 0) return;
    uint8_t* v = config_value();
    if (r == 0) {
        g_blob = g_blob_backup;
        NetGameFileKind = 0;
        v[2] = mem::host(0x94B350)[0];
        mem::host(0x90E79C)[0] = 0;
        v[0] = 0xFF;
        config_rules() = *reinterpret_cast<int32_t*>(mem::host(0x94B470));
    } else {
        SaveFileMenu = 1;
        if (r == 1) {
            if (load_map()) return;  // (SaveFileMenu stays set, as in the original)
        } else {
            if (load_game(r == 2, 0)) return;  // (SaveFileMenu stays set, as in the original)
            int32_t alive = bit_count(FactionStatus[1] & 0xFFFE);
            if (r == 2 && alive < players()) {
                parse_num(0, alive);
                X_pop("TOOMANYFORSCENARIO", 0);
                return;
            }
            loaded = true;
        }
        SaveFileMenu = 0;
        if (NetGameFileKind) {
            for (int32_t f = 0; f < 8 && !g_blob.empty(); f++)
                *reinterpret_cast<int32_t*>(&g_blob[(size_t)f * 400 + 4]) = Factions[f].unk_102;
            v[4] = (uint8_t)(MapAreaX / 2);
            v[5] = (uint8_t)MapAreaY;
            char* name = reinterpret_cast<char*>(mem::host(0x90E79C));
            const char* slash = crt_strrchr(NetGameFileName, '\\');
            name[0] = 0;
            crt_strcat(name, slash ? slash + 1 : NetGameFileName);
        }
        if (GameMoreRules & 0x10) config_mrules() |= 0x10;
        else config_mrules() &= ~0x10;
        v[2] = (uint8_t)DiffLevel;
        v[0] = FactionStatus[1];
        config_rules() = GameRules;
    }
    send_session();
    if (NetGameFileKind == 0 || NetGameFileKind == 1) {
        if (old_kind != 0 && old_kind != 1) {
            g_blob = g_blob_backup;
            send_faction_data(-1);
        }
        for (int32_t i = 1; i < 8; i++) {
            cfg(i)[3] = 0xFF;
            crt_strcpy(reinterpret_cast<char*>(cfg(i) + 5), lab(0xFE4).c_str());
            crt_strcpy(reinterpret_cast<char*>(cfg(i) + 0x45), lab(0xFE4).c_str());
        }
    } else {
        int32_t size = 8 * 399 + 0x194;
        if ((int32_t)g_blob.size() < size) g_blob.resize((size_t)size, 0);
        g_blob[1] = 8;
        g_blob[0] = (uint8_t)bit_count(FactionStatus[1] & 0xFFFE);
        g_blob[9] = 0;
        int32_t row = 1;
        for (int32_t f = 1; f < 8; f++) {
            uint8_t* e = &g_blob[(size_t)f * 400];
            if (!(v[0] & (1 << f))) {
                e[9] = 0;
                continue;
            }
            const MFaction& m = MFactions[f];
            e[9] = 1;
            *reinterpret_cast<int32_t*>(e + 4) = Factions[f].unk_102;
            crt_strcpy(reinterpret_cast<char*>(e + 10), m.filename);
            crt_strcpy(reinterpret_cast<char*>(e + 0x22), m.name_leader);
            crt_strcpy(reinterpret_cast<char*>(e + 0x22), m.name_leader);
            crt_strcpy(reinterpret_cast<char*>(e + 0x3A), m.formal_name_faction);
            crt_strcpy(reinterpret_cast<char*>(e + 0x62), m.noun_faction);
            crt_strcpy(reinterpret_cast<char*>(e + 0x7A), m.adj_name_faction);
            crt_strcpy(reinterpret_cast<char*>(e + 0xFA), m.title_leader);
            crt_strcpy(reinterpret_cast<char*>(e + 0x112), m.adj_leader);
            e[0x192] = (uint8_t)m.is_leader_female;
            e[8] = (uint8_t)faction_pcx_colour(m.filename);
            // The row-th player's row takes the row-th alive faction's names (seven at most).
            uint8_t* p = cfg(row++);
            *reinterpret_cast<int32_t*>(p + 0x178) = 0;
            p[3] = 0xFF;
            crt_strcpy(reinterpret_cast<char*>(p + 5), m.name_leader);
            crt_strcpy(reinterpret_cast<char*>(p + 0x1D), m.formal_name_faction);
            crt_strcpy(reinterpret_cast<char*>(p + 0x45), lab(0xFE4).c_str());
            crt_strcpy(reinterpret_cast<char*>(p + 0x5D), m.adj_name_faction);
            crt_strcpy(reinterpret_cast<char*>(p + 0xDD), m.title_leader);
            crt_strcpy(reinterpret_cast<char*>(p + 0xF5), m.adj_leader);
            p[0x175] = (uint8_t)m.is_leader_female;
        }
        send_faction_data(-1);
    }
    // (The original first fills a copy of the table with empty rows, then copies the real one over it.)
    send_all_player_packets(-1);
    NetSetupType = NetGameFileKind;
    send_session2_packet(0, 0);
    if (loaded) {
        uint8_t m[0x20] = {};
        *reinterpret_cast<uint16_t*>(m) = 0x2F0E;
        Net_send(&NetState, m, 0x20, host_pid(), 1);
    }
    std::snprintf(note, sizeof(note), "net: type of game %d (%s)", NetSetupType,
                  reinterpret_cast<const char*>(mem::host(0x90E79C)));
    ui_net_note(note);
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

// NetWin_pick_diff (0x47CF40): the PopMenu of the difficulties (here a popup list); the row's
// settings with it sent to the host (0x2F04; even when cancelled, as the original).
void NetWin_pick_diff(int32_t idx) {
    BasePop* pop = popup_new();
    int32_t r = -1;
    if (!Popup_start(pop, "smacxnet", "NETPICK", -1, nullptr, 0)) {
        for (int32_t d = 0; d < 6; d++) BasePop_item(pop, Difficulty[d] ? Difficulty[d] : "", d);
        BasePop_set_active(pop, cfg(idx)[2]);
        r = BasePop_exec_3(pop, 0, 0x483240);
    }
    popup_delete(pop);
    uint8_t p[0x17C];
    std::memcpy(p, cfg(idx), 0x17C);
    if (r >= 0) p[2] = (uint8_t)r;
    if (idx > 0) send_settings(p, idx);
}

// NetWin_on_left_click (0x47B760): the host's settings (type 0: the pick lists; 1: the rules,
// sent as 0x2F02), a row's faction (3) and difficulty (4): the own row, or for the host a
// computer's row; the own row's password (5, a new game). (Type 2, the own row's names:
// NetWin_pick_player's CUSTOMNAMES, is not reconstructed.)
void NetWinState::on_click(int32_t, int32_t index, int32_t type, int32_t, int32_t, bool right, bool) {
    if (right || index < 0) return;
    const bool host = NetSessionState[0] != 0;
    const int32_t own = AlphaNet_pid_2_idx(&NetState, own_pid());
    const int32_t idx = index + 1;
    const bool row_mine = (type >= 2 && type <= 5) &&
                          (idx == own || (cfg(idx)[0] == 0xFF && own_pid() == host_pid()));
    if (type == 0 && host) {
        if (index == 0) NetWin_pick_map();
        else if (index >= 1 && index <= 8) NetWin_pick(index);
    } else if (type == 1 && host) {
        if (index == 100) config_mrules() ^= 0x10;
        else if (index >= 16 && index < 18) config_state() ^= (int32_t)kRuleBit[index];
        else if (index >= 0 && index < 15) config_rules() ^= (int32_t)kRuleBit[index];
        char note[64];
        std::snprintf(note, sizeof(note), "net: rule %d toggled", index);
        ui_net_note(note);
        send_session();
    } else if (type == 2 && idx == own) {
        ui_net_note("net: custom names (not reconstructed)");
    } else if (type == 3 && row_mine) {
        NetWin_pick_faction(idx);
    } else if (type == 4 && row_mine) {
        NetWin_pick_diff(idx);
    } else if (type == 5 && !loaded_game() && idx == own) {
        NetWin_pick_pass(0);
    }
    texts.clear();
    redraw();
    ui_invalidate();
}

// NetWin_on_key_down (0x483310): Enter sends the EditBox's line to everyone (message_chat 0x1E00:
// the sender's own copy shows it), Esc cancels the window, the other keys edit the line.
void NetWinState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (vk == 0x1B) {
        NetWin_on_button_clicked(-2);
        return;
    }
    if (vk == 0x0D) {
        message_chat(0x1E00, 0, edit.c_str());
        edit.clear();
    } else if (vk == 8) {
        if (!edit.empty()) edit.pop_back();
    } else {
        bool shift = key & 0x10000;
        char ch = 0;
        if (vk >= 'A' && vk <= 'Z') ch = (char)(shift ? vk : vk - 'A' + 'a');
        else if (vk >= '0' && vk <= '9') ch = (char)vk;
        else if (vk == 0x20) ch = ' ';
        if (!ch || edit.size() >= 0xFF) return;
        edit += ch;
    }
    texts.clear();
    redraw();
    ui_invalidate();
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
    g_chat_idx.clear();
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
    sw_init(&w, (ScreenWidth - 800) / 2, (ScreenHeight - 600) / 2, 800, 600, false);
    // The buttons: LAUNCH GAME (the host) / MAKE READY (a player), CANCEL (font 14 bold, colour 0x41).
    w.big = find_font(0xE, 1);
    w.small = find_font(0xC, 1);
    w.ready = sw_button(&w, lab(NetSessionState[0] ? 0x1058 : 0x1050).c_str(), 10, kReadyButton.l, kReadyButton.t,
                        kReadyButton.r - kReadyButton.l, kReadyButton.b - kReadyButton.t, 0, w.big);
    BaseButton* cancel = sw_button(&w, sw_upper(lab(0x198)).c_str(), -2, kCancelButton.l, kCancelButton.t,
                                   kCancelButton.r - kCancelButton.l, kCancelButton.b - kCancelButton.t, 0, w.big);
    for (BaseButton* b : {w.ready, cancel})
        for (int32_t& c : b->text_color) c = 0x41;
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
            if (NetSetupType == 3) {
                // A scenario: its files sent to the players (NetDaemon_send_files), its setup.
                GameState |= 0x10;
                if (NetDaemon_send_files(&NetState)) {
                    ui_net_note("net: scenario cancelled");
                    NetMsg_close(&NetMsg);  // (pop_close)
                    log_set_state(0);
                    net_session_clear();
                    MultiplayerActive = 0;
                    return 1;
                }
                if (NetSessionState[0]) scenario_setup();
            } else {
                GameState &= ~0x10;
            }
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
                net_send_original_bugs();  // smacx: the players play with the host's setting
                NetDaemon_send_rules(&NetState);
                NetDaemon_send_game(&NetState);
            }
            if (NetSetupType == 3) game_reload(1, 0);  // (inline in the original: the windows, texts and art again)
            Lock_clear(&NetState + 0x14A0);
            char line[160];
            std::snprintf(line, sizeof(line),
                          "net: game started: faction %d, humans %02x, map %dx%d, rules %08X, time control %d, difficulty %d",
                          CurrentPlayerFaction, FactionStatus[0], MapAreaX, MapAreaY, (uint32_t)GameRules, GameTimeControl,
                          DiffLevel);
            ui_net_note(line);
            if (!ControlTurnMove) planetfall(CurrentPlayerFaction);
            if (NetSetupType == 3) {
                const MFaction& m = MFactions[CurrentPlayerFaction];
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(1, m.title_leader, -1, -1);
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(2, m.name_leader, -1, -1);
                GenderDefault = m.noun_gender;
                PluralDefault = m.is_noun_plural;
                parse_says(3, m.noun_faction, -1, -1);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, label_get(0));
                crt_strcat(StrBuffer, " ");
                say_year(StrBuffer);
                parse_says(4, StrBuffer, -1, -1);
                parse_num(0, ObjectiveReqVictory);
                parse_num(1, ObjectivesSuddenDeathVictory);
                parse_num(2, num_objectives(CurrentPlayerFaction, GameRules & 0x1000));
                parse_num(3, EndingMissionYear);
                X_pop_2("SCENARIO", "INTRO", 0);
            }
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
