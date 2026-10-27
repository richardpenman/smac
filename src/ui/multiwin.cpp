// The commlink panel (MultiWin, ui module 0x477E70..0x47A760): F12 and the console's COMMLINK
// button (MainInterface_on_iface_button_clicked 0x3EC: MultiWin_show / MultiWin_hide). It lists
// the factions the viewer has a commlink with (MultiWin_draw: commlink bit 8 of diplo_status, the
// scenario editor, the Empath Guild (project 4), or a human player auto_contact reaches) with
// their leader and the treaty (MultiWin_draw_player: PACT / TREATY / TRUCE / VENDETTA), the
// viewer's council votes, and the Council button.
//
// As the original: the panel's drawing (MultiWin_draw / MultiWin_draw_player, single-player path:
// seven rows of multi.pcx's row art, the listed factions numbered "N. leader" in their text
// colours with the treaty right-aligned, the row under the mouse highlighted
// (MultiWin_on_mouse_move / on_mouse_leave), NO CONTACTS on the first row when none is listed,
// the votes row and the bottom art; the art cut from multi.pcx as MultiWin_init cuts it, the
// Council button with multi.pcx's button pictures); a click on a faction opens the commlink
// (commlink_attempt); an eliminated faction gives ELIMINATED; a right click opens the faction's
// menu (MultiWin_on_right_click: Contact, Faction Profile (report_intel), Renounce Pact / Demand
// Withdrawal when there is a treaty, Summon Planetary Council (call_council)); the buttons call
// MultiWin_on_button_clicked (0 chat, 1 Council).
// Single player: a modal panel closed by F12, Esc or Enter (the original's stays on the screen
// beside the map); a network game: modeless beside the map as the original's (F12 / the console's
// button toggle it, Enter hides it; the main loop and the network go on).
// In a network game (MultiWin_draw's network path): the top art, the time row (MultiWin_draw_time:
// the turn's time left, the upkeep's count, the moving player's name, PAUSED, "No Time Control"; a
// click: Console_time_controls), the bonus pills' row ("^" each; a click: Console_use_time_bonus),
// each player's chat flag (a click cycles refused / left out of the chat / none) and state
// (MultiWin_draw_player: PAUSED, (*), (+), (-), the upkeep, (?) / (@) the locks, the treaty's short
// name), the chat button (MultiWin_init: id 0, Console_chat) under the Council button. Closed, the
// panel keeps its time and pills rows on the screen (multiwin_net_tick: turn_timer's countdown),
// clickable (multiwin_collapsed_click).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/memmap.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/font_text.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/screenwin.h"
#include "ui/win.h"

namespace {

constexpr int32_t kCouncil = 1;  // MultiWin_on_button_clicked: 0 chat, 1 council

struct MultiWinState : ScreenWin {
    Font *f_text = nullptr, *f_bold = nullptr, *f_bold12 = nullptr;
    int32_t hover = -1;  // +0x1168: the faction row under the mouse
    std::vector<int32_t> rows;  // the factions listed
    int32_t menu_faction = -1;  // the faction of the open right-click menu
    std::vector<std::pair<int32_t, std::string>> menu;  // PullDown items (id, text)
    UiRect menu_rect{};
    std::string last_action;
    bool full = false;  // +0xA24: the panel opened (F12); in a network game the time rows stay shown
    std::string time_line;
    int32_t time_color = 0xE4;

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
    void on_move(int32_t x, int32_t y) override;
    void act(int32_t item, int32_t f);
};

MultiWinState g_mp;
bool g_collapsed = false;  // the closed panel of a network game shown (multiwin_net_tick)
bool g_modeless = false;   // a network game's open panel, modeless beside the map
constexpr int32_t kW = 0x8A, kRowH = 0x11;  // MultiWin_draw: 0x8A wide, seven rows

bool listed(int32_t f) {
    int32_t v = CurrentPlayerFaction;
    if (f == v || !(FactionStatus[1] & (1 << f))) return false;
    return (GameState & STATE_OMNISCIENT_VIEW) || (Factions[v].diplo_status[f] & 8) ||
           (SecretProjects[FAC_EMPATH_GUILD - SP_ID_First] >= 0 &&
            Bases[SecretProjects[FAC_EMPATH_GUILD - SP_ID_First]].faction_id == v) ||
           ((FactionStatus[0] & (1 << f)) && auto_contact());
}

std::string treaty(int32_t f) {
    uint32_t d = Factions[CurrentPlayerFaction].diplo_status[f];
    if (d & 1) return sw_label(0x34C);
    if (d & 2) return sw_label(0x350);
    if (d & 4) return sw_label(0x354);
    if (d & 0x10) return (MFactions[f].rule_flags & 0x100) ? sw_upper(sw_label(0xA1C)) : sw_label(0x358);
    return "";
}

// MultiWin_init's art (multi.pcx, cut with the transparent index 0x696D14).
struct MultiArt {
    bool loaded = false;
    Sprite top, time, row, row_hi, bottom;  // +0x10E4, +0x1110, +0x113C, +0x116C, +0x1198
    Sprite button[3];  // +0xFDC: the buttons' three states
    Sprite flag[3];    // +0x1060: a player's chat flag (0 none, 1 refused, 2 left out of the chat)
};
MultiArt g_art;

void load_art() {
    if (g_art.loaded) return;
    g_art.loaded = true;
    Buffer art;
    if (Buffer_load_pcx(&art, "multi.pcx", nullptr, 10, 0xEC) != 0) return;
    int32_t key = *mem::host(0x696D14);
    pfncSpriteFromCanvasRectTrans(&g_art.top, &art, key, 1, 1, 0x8A, 6, 0);
    pfncSpriteFromCanvasRectTrans(&g_art.time, &art, key, 1, 8, 0x8A, 0x1C, 0);
    pfncSpriteFromCanvasRectTrans(&g_art.row, &art, key, 1, 0x25, 0x8A, 0x12, 0);
    pfncSpriteFromCanvasRectTrans(&g_art.bottom, &art, key, 1, 0x38, 0x8A, 0x18, 0);
    pfncSpriteFromCanvasRectTrans(&g_art.row_hi, &art, key, 1, 0x51, 0x8A, 0x12, 0);
    for (int32_t i = 0, y = 1; i < 3; i++, y += 0x13)
        pfncSpriteFromCanvasRectTrans(&g_art.button[i], &art, key, 0x8C, y, 0x79, 0x12, 0);
    for (int32_t i = 0, y = 1; i < 3; i++, y += 0xF)
        pfncSpriteFromCanvasRectTrans(&g_art.flag[i], &art, key, 0x106, (i != 1 ? 0 : 0x1E) + y, 0xE, 0xE, 0);
}

int32_t art_h(const Sprite& sp, int32_t def) { return sp.full_height ? sp.full_height : def; }
uint8_t& netb(uint32_t off) { return *(&NetState + off); }
int32_t& net32(uint32_t off) { return *reinterpret_cast<int32_t*>(&NetState + off); }
int32_t& g32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }

// MultiWin_draw_time (0x478BE0): the time row's text and colour; with tick (turn_timer, every
// 500 ms) the turn's time counts down (not while paused or in a network wait) and, with time
// controls, runs out: GameState 0x800 (the turn ends) and the alarm (wave 0x21); the last fifteen
// seconds TIMEWARNING and a tick each second.
int32_t g_prev_sec = 0;  // 0x7FFF78
int32_t g_blink = 0;     // 0x7FFF7C
std::string time_text(bool tick, int32_t* color) {
    const int32_t paused = NetSessionState[8];  // NetState +0x1B50: the players pausing
    bool waiting = false, had_time = false;
    *color = 0xE4;
    if (tick) {
        g_blink++;
        if (!NetBusyCount && !paused && !g32(0x93F668)) {
            int32_t& left = ControlWaitLoop ? g32(0x6FF6BC) : NetTurnTimeLeft;
            had_time = left > 0;
            left = left - 500 < 0 ? 0 : left - 500;
        } else {
            waiting = true;
            if (g32(0x93F668) > 0) g32(0x93F668)--;
        }
    }
    int32_t left = NetTurnTimeLeft < 0 ? 0 : NetTurnTimeLeft;
    if (ControlWaitLoop) left = g32(0x6FF6BC) < 0 ? 0 : g32(0x6FF6BC);
    int32_t sec = (left + 999) / 1000, min = sec / 60;
    sec %= 60;
    char mmss[32];
    std::snprintf(mmss, sizeof(mmss), "%d:%s%d", min, sec < 10 ? "0" : "", sec);
    std::string t;
    if (ControlWaitLoop) {  // the council's vote (its time-out, 0x2601, is the council's)
        t = sw_upper(sw_label(0x7E8)) + ": " + mmss;
        g_prev_sec = sec;
        return t;
    }
    if (ControlTurnC) {
        g_prev_sec = sec;
        return sw_upper(sw_label(0x410)) + " " + std::to_string(NetUpkeepCount);
    }
    if ((GameMoreRules & 0x10) && (CurrentFaction != CurrentPlayerFaction || NetPlayerTurnDone)) {
        g_prev_sec = sec;
        if (!(FactionStatus[0] & (1 << (CurrentFaction & 31)))) return "";
        const char* name = Net_get_player_name(&NetState, AlphaNet_who_2_pid(&NetState, CurrentFaction));
        return name ? name : "";
    }
    const uint32_t me = 1u << (CurrentPlayerFaction & 31);
    if (tick && NetSessionState[5] && !paused && GameTimeControl) {
        if (left == 0 && !(NetTurnDoneMask & me) && g_prev_sec != 0) {
            GameState |= 0x800;
            ui_net_note("net: turn time out");
            if (had_time) wave_it(0x21);
        }
        if (min == 0 && sec != 0 && sec < 16 && sec != g_prev_sec && !ControlTurnC && !(GameState & 0x800) &&
            !(NetTurnDoneMask & me)) {
            if (sec == 15) NetMsg_pop(&NetMsg, "TIMEWARNING", -5000, 0, nullptr);
            FX_play(&Sounds, 0x23);
        }
    }
    t = sw_label(0x40C) + ": ";
    if (paused) t = sw_label(0x988) + ": " + sw_label(0x444);
    else if (GameTimeControl == 0) t = sw_label(0xA04);
    else t += mmss;
    if (waiting && (g_blink & 1)) *color = 0;
    g_prev_sec = sec;
    return t;
}

void put(Buffer* c, Sprite* sp, int32_t y) { pfncSpriteStretchCopyToCanvas(sp, c, sp->key, 0, y); }

// MultiWin_draw_player (0x478630), single-player path: the row's spot (type 1, the faction), "N.
// leader" in the faction's colours from 7 + 3 pixels in, the treaty right-aligned 10 pixels from
// the right edge (PACT / TREATY / TRUCE / VENDETTA; the aliens' vendetta upper case).
// In a network game the row starts with the player's chat flag (a spot of type 0: the click
// toggles it), and the text on the right is the player's state: PAUSED, "(*)" moving, "(+)" / "(-)"
// to move after / before the current player, the upkeep's count, "(?)" / "(@)" the server's view of
// the locks, then the treaty's short name.
std::string net_state(int32_t f) {
    const uint32_t bit = 1u << (f & 31), me = 1u << (CurrentPlayerFaction & 31);
    const uint32_t mask = (uint32_t)(ControlTurnC ? NetSessionState[15] : NetSessionState[14]);
    const bool human = FactionStatus[0] & bit, omni = GameState & STATE_OMNISCIENT_VIEW;
    std::string t;
    if (!(GameMoreRules & 0x10) && (NetSessionState[8] & bit)) return sw_label(0x444);
    bool turn_part;  // LAB_00478A6A
    if (!(bit & mask)) {
        if (ControlTurnC) return "*" + sw_label(0x42C) + " " + std::to_string(NetUpkeepCount);
        turn_part = true;
    } else {
        if (ControlTurnC) return sw_label(0x42C) + " " + std::to_string(NetUpkeepCount);
        turn_part = !omni && !(mask & me);
    }
    if (turn_part) {
        if (GameMoreRules & 0x10) {
            if (CurrentFaction == f) t = "(*)";
            else if (human) t = CurrentFaction < f ? "(+)" : "(-)";
        } else if (human && ((mask & me) || omni)) {
            if (!(bit & mask)) {
                t = "(*)";
            } else if (NetSessionState[0]) {
                if (PlayerLock_active(&NetState + 0x14A0 + (uint32_t)f * 0x1C)) t = "(?)";
                else if (NetSessionState[19]) t = "(@)";
            }
        }
    }
    uint32_t d = Factions[CurrentPlayerFaction].diplo_status[f];
    if (d & 1) t += sw_label(0xA14);
    else if (d & 2) t += sw_label(0xA10);
    else if (d & 4) t += sw_label(0xA0C);
    else if (d & 0x10) t += sw_label(0xA1C);
    return t;
}

void draw_player(MultiWinState* s, Buffer* c, int32_t f, int32_t n, const UiRect& r) {
    Buffer_set_font(c, s->f_text, nullptr, nullptr, nullptr);
    const int32_t rh = r.bottom - r.top;
    UiRect tr{r.left + 7 + 3, r.top, r.right - 10, r.bottom};
    if (MultiplayerActive) tr.left += rh;
    if (MultiplayerActive && (FactionStatus[0] & (1 << f)))
        spot_add(&s->spots[0], f, 0, r.left + 7, r.top, rh, rh);
    spot_add(&s->spots[0], f, 1, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top);
    if (MultiplayerActive && (FactionStatus[0] & (1 << f))) {
        // The chat flag: 1 refused (0x7FFF74), 2 left out of the chat (0x93E960), else 0.
        uint32_t bit = 1u << (f & 31);
        int32_t st = ((uint32_t)g32(0x7FFF74) & bit) ? 1 : (((uint32_t)NetSessionState[40] & bit) ? 2 : 0);
        Sprite* sp = &g_art.flag[st];
        int32_t fh = art_h(*sp, 0xE);
        pfncSpriteStretchCopyToCanvas(sp, c, sp->key, r.left + 7, (rh - fh) / 2 + r.top);
    }
    Buffer_set_text_color(c, text_color(0x68FA30 + (uint32_t)f * 4), text_color(0x68FA50 + (uint32_t)f * 4), 1, 1);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    std::string t = std::to_string(n) + ". " + MFactions[f].name_leader;
    s->texts.push_back(t);
    Buffer_write_l_4(c, t.c_str(), &tr, (int32_t)t.size());
    t = MultiplayerActive ? net_state(f) : treaty(f);
    if (!t.empty()) sw_text_right(s, c, t, tr);
}

// MultiWin_draw (0x479330), single-player path.
void MultiWinState::redraw() {
    Buffer* c = &canvas;
    load_art();
    spot_kill_type(&spots[0], 2);
    spot_kill_type(&spots[0], 0);
    spot_kill_type(&spots[0], 1);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);  // GraphicWin_fill(9)
    const int32_t row_h = g_art.row.full_height ? g_art.row.full_height : kRowH;
    const int32_t bottom_h = art_h(g_art.bottom, 0x18), time_h = art_h(g_art.time, 0x1C);
    // Network game: closed, only the top, time and bonus rows show, at the window's bottom.
    int32_t y = MultiplayerActive && !full ? row_h * 7 + bottom_h * 2 : 0;
    if (MultiplayerActive) {
        put(c, &g_art.top, y);
        y += art_h(g_art.top, 6);
        // The time row (spot 0 of type 2: Console_time_controls) and the bonus pills' row (spot 3
        // of type 2: Console_use_time_bonus): "^" per pill, at most three.
        put(c, &g_art.time, y);
        UiRect tr{0, y, c->width, y + time_h};
        spot_add(&spots[0], 0, 2, tr.left, tr.top, tr.right - tr.left, tr.bottom - tr.top);
        Buffer_set_font(c, f_bold, nullptr, nullptr, nullptr);
        Buffer_set_text_color(c, time_color, -1, 1, 1);
        texts.push_back(time_line);
        Buffer_write_cent_l_7(c, time_line.c_str(), &tr, (int32_t)time_line.size());
        y += time_h;
        put(c, &g_art.row, y);
        UiRect pr{0, y, c->width, y + 0xE};
        spot_add(&spots[0], 3, 2, pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top);
        int32_t pills = *reinterpret_cast<int32_t*>(mem::host(0x96EA34 + (uint32_t)CurrentPlayerFaction * 0x20CC));
        if (pills > 0) {
            std::string p = " " + std::string((size_t)std::min(pills, 3), '^');
            Buffer_set_clip(c, &pr);
            Buffer_write_cent_l_7(c, p.c_str(), &pr, (int32_t)p.size());
            UiRect all2{0, 0, c->width, c->height};
            Buffer_set_clip(c, &all2);
        }
        y += 0xE;
        if (!full) return;
    }
    rows.clear();
    for (int32_t f = 1; f < 8; f++) {
        if (!listed(f)) continue;
        rows.push_back(f);
        put(c, (hover > 0 && hover == f) ? &g_art.row_hi : &g_art.row, y);
        draw_player(this, c, f, (int32_t)rows.size(), UiRect{0, y, c->width, y + row_h});
        y += row_h;
    }
    for (int32_t i = (int32_t)rows.size(); i < 7; i++) {
        put(c, &g_art.row, y);
        y += row_h;
    }
    // The votes row (the button's height above it), the viewer's council votes (aliens: none).
    Buffer_set_font(c, f_text, nullptr, nullptr, nullptr);
    y += g_art.button[0].full_height - row_h;
    put(c, &g_art.row, y);
    std::string v = sw_label(0x80C) + ":  " + std::to_string(council_votes(CurrentPlayerFaction));
    Buffer_set_text_color(c, 0xFF, -1, 1, 1);
    if (!(MFactions[CurrentPlayerFaction].rule_flags & 0x80)) {
        texts.push_back(v);
        Buffer_write_cent_l_5(c, v.c_str(), 0, y + 10, 0x8A, (int32_t)v.size());
        y += row_h;
    }
    if (MultiplayerActive) {  // the chat button's row
        put(c, &g_art.bottom, y);
        y += bottom_h;
    }
    put(c, &g_art.bottom, y);
    if (rows.empty()) {
        // NO CONTACTS on the first row (find_font(12, bold)).
        std::string t = sw_label(0x9D0);
        texts.push_back(t);
        Buffer_set_text_color(c, text_color(0x68A5A4), text_color(0x68A5B0), 1, 1);
        Buffer_set_font(c, f_bold12, nullptr, nullptr, nullptr);
        UiRect r{0, 0, c->width, row_h};
        Buffer_write_cent_l_7(c, t.c_str(), &r, (int32_t)t.size());
    }
    // (The right-click menu: smacx's PullDown stand-in, drawn over the panel.)
    if (menu_faction >= 0) {
        Buffer_box_sprite(c, &menu_rect, box_def(9));
        int32_t my = menu_rect.top + 4;
        for (auto& it : menu) {
            sw_font(c, f_text, 0xFF);
            sw_text(this, c, it.second, menu_rect.left + 6, my);
            spot_add(&spots[0], it.first, 2, menu_rect.left, my, menu_rect.right - menu_rect.left, kRowH);
            my += kRowH;
        }
    }
}

void MultiWinState::act(int32_t item, int32_t f) {
    if (!(FactionStatus[1] & (1 << f))) {
        // Eliminated: ELIMINATED (NetMsg).
        PluralDefault = 0;
        GenderDefault = MFactions[f].is_leader_female;
        parse_says(0, MFactions[f].title_leader, -1, -1);
        parse_says(1, MFactions[f].name_leader, -1, -1);
        PluralDefault = MFactions[f].is_noun_plural;
        GenderDefault = MFactions[f].noun_gender;
        parse_says(2, MFactions[f].noun_faction, -1, -1);
        NetMsg_pop(&NetMsg, "ELIMINATED", 5000, 0, nullptr);
        return;
    }
    last_action = "action " + std::to_string(item) + " faction " + std::to_string(f);
    std::fprintf(stderr, "multiwin %s\n", last_action.c_str());
    switch (item) {
    case 1: commlink_attempt(f); break;
    case 2: report_intel(f); break;
    case 3: call_council(CurrentPlayerFaction); break;
    case 8: renounce_pact(CurrentPlayerFaction, f); break;
    case 9: demands_withdrawal(CurrentPlayerFaction, f); break;
    default: break;
    }
}

void MultiWinState::on_button(int32_t id) { MultiWin_on_button_clicked(&MultiWinObj, id); }

// MultiWin_on_mouse_move (0x477E70): the row under the mouse highlighted (MultiWin_on_mouse_leave
// when the mouse leaves the spots).
void MultiWinState::on_move(int32_t x, int32_t y) {
    int32_t index = -1, type = 0;
    bool inside = x >= 0 && y >= 0 && x < canvas.width && y < canvas.height;
    if (!inside || spot_check(&spots[0], x, y, &index, &type) < 0) {
        if (hover != -1) {
            hover = -1;
            texts.clear();
            redraw();
            ui_invalidate();
        }
    } else if (type == 1 && hover != index) {
        hover = index;
        texts.clear();
        redraw();
        ui_invalidate();
    }
}

void MultiWinState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (menu_faction >= 0 && vk == 0x1B) {
        menu_faction = -1;
        return;
    }
    if (vk == 0x7B || vk == 0x1B || vk == 0xD) sw_close(this);
}

void MultiWinState::on_click(int32_t, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool) {
    if (menu_faction >= 0) {
        int32_t f = menu_faction;
        menu_faction = -1;
        if (type == 2 && index >= 0 && !right) act(index, f);
        return;
    }
    if (index < 0) return;
    if (!right && MultiplayerActive && (type == 0 || type == 2)) {
        // MultiWin_on_left_click, network spots (the spot's index checked as a faction first, as
        // the original: ELIMINATED).
        if (!(FactionStatus[1] & (1 << index))) {
            act(1, index);
            return;
        }
        if (type == 2) {
            if (index == 0) Console_time_controls(&MainConsole);
            else if (index == 3) Console_use_time_bonus(&MainConsole);
        } else {
            // The chat flag: refused -> left out of the chat -> none -> refused.
            uint32_t bit = 1u << (index & 31);
            int32_t& refused = g32(0x7FFF74);
            if ((uint32_t)refused & bit) {
                refused &= (int32_t)~bit;
                NetSessionState[40] |= (int32_t)bit;
            } else if ((uint32_t)NetSessionState[40] & bit) {
                NetSessionState[40] &= (int32_t)~bit;
            } else {
                refused |= (int32_t)bit;
            }
        }
        texts.clear();
        redraw();
        ui_invalidate();
        return;
    }
    if (type != 1) return;
    if (!right) {
        // MultiWin_on_left_click: the commlink.
        act(1, index);
        return;
    }
    // MultiWin_on_right_click: the faction's menu.
    int32_t f = index;
    menu.clear();
    menu.push_back({1, sw_label(0x9F0) + " " + MFactions[f].title_leader + " " + MFactions[f].name_leader});
    menu.push_back({2, sw_label(0x9F4)});
    uint32_t d = Factions[CurrentPlayerFaction].diplo_status[f];
    if (d & 7) menu.push_back((d & 1) ? std::make_pair(8, sw_label(0xE20)) : std::make_pair(9, sw_label(0xE24)));
    menu.push_back({3, sw_label(0x9F8)});
    menu_faction = f;
    int32_t h = (int32_t)menu.size() * kRowH + 8;
    menu_rect = {std::max(0, std::min(x, kW - 0xE0)), y, 0, 0};
    menu_rect.right = std::min(menu_rect.left + 0xE0, canvas.width);
    menu_rect.bottom = std::min(menu_rect.top + h, canvas.height);
    menu_rect.top = menu_rect.bottom - h;
}

void MultiWinState::log_state() {
    std::fprintf(stderr, "multistate rows");
    for (int32_t f : rows) std::fprintf(stderr, " %d", f);
    std::fprintf(stderr, " votes %d menu %d hover %d\n", council_votes(CurrentPlayerFaction), menu_faction, hover);
}

}  // namespace

// MultiWin_show (0x479970) with the modal run of smacx (see above).
void THISCALL MultiWin_show(uint8_t*) {
    MultiWinState* s = &g_mp;
    if (g_modeless) {  // (MainInterface_on_iface_button_clicked 0x3EC: shown, F12 hides it)
        multiwin_hide();
        return;
    }
    if (s->open) return;
    s->name = "commlink";
    s->menu_faction = -1;
    s->hover = -1;
    basewin_mi_init();
    load_art();
    // MultiWin_init: seven rows, the bottom art and the top art's height; the Council button
    // (label 0x7E8, id 1; not for the aliens of the expansion) centred on the bottom art's and the
    // button's heights, a row above; text colours 0x65 / 0x41 / 0x41.
    const int32_t row_h = g_art.row.full_height ? g_art.row.full_height : kRowH;
    const int32_t bh = g_art.button[0].full_height ? g_art.button[0].full_height : 0x12;
    const int32_t bw = g_art.button[0].full_width ? g_art.button[0].full_width : 0x79;
    int32_t h = row_h * 7 + (g_art.bottom.full_height ? g_art.bottom.full_height : 0x18) +
                (g_art.top.full_height ? g_art.top.full_height : 6);
    if (MultiplayerActive) h += art_h(g_art.bottom, 0x18) + art_h(g_art.time, 0x1C) + 0xE;
    s->full = true;
    // MultiWin_init's Win_move (the canvas' height is stored negated at +0x4C8).
    int32_t x = ScreenWidth == 800 ? 800 - kW : (ScreenWidth + 0x400) / 2 - kW;
    int32_t y = ScreenWidth == 800 ? 0x166 - h : ScreenHeight - 0xFF - h;
    if (g_collapsed) sw_hide(s);
    sw_init(s, x, y, kW, h, false);
    s->f_text = find_font(10, 0);
    s->f_bold = find_font(14, 1);
    s->f_bold12 = find_font(12, 1);
    sw_clear_buttons(s);
    if (!ExpansionEnabled || !(MFactions[CurrentPlayerFaction].rule_flags & 0x80)) {
        int32_t by = h - ((g_art.bottom.full_height ? g_art.bottom.full_height : 0x18) + bh) / 2 - row_h;
        BaseButton* b = sw_button(s, sw_label(0x7E8).c_str(), kCouncil, 0x0A, by, bw, bh, 0, s->f_bold);
        for (int32_t st = 0; st < 3; st++) {
            b->box[st] = nullptr;
            b->left[st] = b->right[st] = b->top[st] = b->bottom[st] = nullptr;
            b->tl[st] = b->tr[st] = b->bl[st] = b->br[st] = nullptr;
            b->sprite[st] = &g_art.button[st];
        }
        b->text_color[0] = 0x65;
        b->text_color[1] = b->text_color[2] = 0x41;
    }
    if (MultiplayerActive) {
        // MultiWin_init: the chat button (label 0x440, id 0) at the council button's place, the
        // council button a bottom art higher.
        int32_t by = h - (art_h(g_art.bottom, 0x18) + bh) / 2;
        BaseButton* b = sw_button(s, sw_label(0x440).c_str(), 0, 0x0A, by, bw, bh, 0, s->f_bold);
        for (int32_t st = 0; st < 3; st++) {
            b->box[st] = nullptr;
            b->left[st] = b->right[st] = b->top[st] = b->bottom[st] = nullptr;
            b->tl[st] = b->tr[st] = b->bl[st] = b->br[st] = nullptr;
            b->sprite[st] = &g_art.button[st];
        }
        b->text_color[0] = 0x65;
        b->text_color[1] = b->text_color[2] = 0x41;
        for (BaseButton* o : s->buttons)
            if (o != b) {
                o->r.top -= art_h(g_art.bottom, 0x18);
                o->r.bottom -= art_h(g_art.bottom, 0x18);
            }
    }
    s->time_line = time_text(false, &s->time_color);
    if (MultiplayerActive) {
        // A network game: the panel stays beside the map, modeless, while the main loop runs
        // (multiwin_modeless_mouse takes the mouse over it; F12 / Enter / MultiWin_hide close it).
        s->open = true;
        g_modeless = true;
        MultiWinVisible = 1;
        sw_show(s);
        std::fprintf(stderr, "multiwin shown (modeless)\n");
        return;
    }
    sw_run(s);
    sw_clear_buttons(s);
    s->full = false;
    g_collapsed = false;
    multiwin_net_tick();  // (a network game: the time rows shown again)
}

// turn_timer (0x50EF10): every 500 ms of a network game MultiWin_draw(1): the time counted down
// (MultiWin_draw_time). The original's panel stays on the screen in a network game, closed to its
// time and bonus rows (MultiWin_init's Win_show(0)); here that closed panel is shown over the
// screen while the game runs, its rows clickable from the main screen (multiwin_collapsed_click).
void multiwin_net_tick() {
    MultiWinState* s = &g_mp;
    netmsg_tick();
    bool in_game = MultiplayerActive && *reinterpret_cast<int32_t*>(&NetState + 0x1B40) == 2;
    if (!in_game) {
        if (g_collapsed) {
            sw_hide(s);
            g_collapsed = false;
        }
        return;
    }
    static uint32_t last = 0;
    uint32_t now = platform::ticks_ms();
    bool tick = false;
    if (now - last >= 500) {
        last = now;
        tick = !g32(0x915620);
    }
    if (tick) s->time_line = time_text(true, &s->time_color);
    if (s->open) {
        if (tick) {
            s->texts.clear();
            s->redraw();
            ui_invalidate();
        }
        return;
    }
    if (!g_collapsed) {
        load_art();
        s->name = "commlink";
        s->full = false;
        s->f_text = find_font(10, 0);
        s->f_bold = find_font(14, 1);
        s->f_bold12 = find_font(12, 1);
        const int32_t row_h = art_h(g_art.row, kRowH), bottom_h = art_h(g_art.bottom, 0x18);
        int32_t h = row_h * 7 + bottom_h * 2 + art_h(g_art.top, 6) + art_h(g_art.time, 0x1C) + 0xE;
        int32_t x = ScreenWidth == 800 ? 800 - kW : (ScreenWidth + 0x400) / 2 - kW;
        int32_t y = ScreenWidth == 800 ? 0x166 - h : ScreenHeight - 0xFF - h;
        sw_init(s, x, y, kW, h, false);
        sw_clear_buttons(s);
        s->time_line = time_text(false, &s->time_color);
        s->redraw();
        sw_show(s);
        g_collapsed = true;
        tick = true;
        for (const Spot& sp : s->spots[0].list)
            if (sp.type == 2)
                std::fprintf(stderr, "multiwin closed panel row %d at %d,%d %dx%d\n", sp.index, s->x + sp.r.left,
                             s->y + sp.r.top, sp.r.right - sp.r.left, sp.r.bottom - sp.r.top);
    }
    if (tick) {
        s->texts.clear();
        s->redraw();
        ui_invalidate();
    }
}

// The open panel of a network game (modeless): the main screen's mouse over it goes to the panel
// (its buttons, its spots, the row under the mouse). True when taken.
bool multiwin_modeless_mouse(int32_t type, int32_t x, int32_t y) {
    MultiWinState* s = &g_mp;
    if (!g_modeless || !s->open) return false;
    int32_t lx = x - s->x, ly = y - s->y;
    bool inside = lx >= 0 && ly >= 0 && lx < s->canvas.width && ly < s->canvas.height;
    if (type == UiInput::MOVE) {
        s->on_move(lx, ly);
        return inside;
    }
    if (!inside) return false;
    if (type == UiInput::LDOWN) {
        for (BaseButton* b : s->buttons) {
            if (b->shown && lx >= b->r.left && lx < b->r.right && ly >= b->r.top && ly < b->r.bottom) {
                s->on_button(b->id);
                return true;
            }
        }
    }
    if (type == UiInput::LDOWN || type == UiInput::RDOWN) {
        int32_t index = -1, kind = -1;
        spot_check(&s->spots[0], lx, ly, &index, &kind);
        s->on_click(0, index, kind, lx, ly, type == UiInput::RDOWN, false);
        if (g_modeless && s->open) {
            s->texts.clear();
            s->redraw();
            ui_invalidate();
        }
    }
    return true;
}

// The closed panel's rows clicked (MultiWin_on_left_click on the panel the original keeps on the
// screen): the time row, Console_time_controls; the bonus pills' row, Console_use_time_bonus.
bool multiwin_collapsed_click(int32_t x, int32_t y) {
    MultiWinState* s = &g_mp;
    if (!g_collapsed || s->open) return false;
    int32_t index = -1, type = -1;
    if (spot_check(&s->spots[0], x - s->x, y - s->y, &index, &type) < 0 || type != 2) return false;
    if (index == 0) {
        std::fprintf(stderr, "multiwin time row clicked\n");
        Console_time_controls(&MainConsole);
    } else if (index == 3) {
        std::fprintf(stderr, "multiwin bonus row clicked\n");
        Console_use_time_bonus(&MainConsole);
    } else {
        return false;
    }
    return true;
}

// MultiWin_on_button_clicked (0x477F00): 0 the chat (Console_chat(0)), 1 the Council
// (call_council of the viewer); other ids (the console's 0x802 command and its key send 2): nothing.
void THISCALL MultiWin_on_button_clicked(uint8_t*, int32_t id) {
    if (id == 0) {
        Console_chat(&MainConsole, 0);
    } else if (id == 1) {
        std::fprintf(stderr, "multiwin council\n");
        g_mp.last_action = "council";
        call_council(CurrentPlayerFaction);
    }
}

// MultiWin_hide (0x479C00) as MainInterface_on_status(0) calls it when another interface mode
// starts (the diplomacy dialog): the commlink window closes.
void multiwin_hide() {
    if (g_modeless) {
        MultiWinState* s = &g_mp;
        g_modeless = false;
        MultiWinVisible = 0;
        s->open = false;
        s->menu_faction = -1;
        sw_clear_buttons(s);
        sw_hide(s);
        s->full = false;
        g_collapsed = false;
        std::fprintf(stderr, "multiwin hidden\n");
        multiwin_net_tick();  // (the time rows shown again)
        return;
    }
    if (g_mp.open) {
        sw_close(&g_mp);
        sw_hide(&g_mp);
    }
}
