// The commlink panel (MultiWin, ui module 0x477E70..0x47A760): F12 and the console's COMMLINK
// button (MainInterface_on_iface_button_clicked 0x3EC: MultiWin_show / MultiWin_hide). It lists
// the factions the viewer has a commlink with (MultiWin_draw: commlink bit 8 of diplo_status, the
// scenario editor, the Empath Guild (project 4), or a human player auto_contact reaches) with
// their leader and the treaty (MultiWin_draw_player: PACT / TREATY / TRUCE / VENDETTA), the
// viewer's council votes, and the Council button.
//
// As the original: a click on a faction opens the commlink (commlink_attempt); an eliminated
// faction gives ELIMINATED; a right click opens the faction's menu (MultiWin_on_right_click:
// Contact, Faction Profile (report_intel), Renounce Pact / Demand Withdrawal when there is a
// treaty, Summon Planetary Council (call_council)); the Council button calls call_council.
// Approximated: the original panel stays on the screen beside the map while the game goes on
// (toggled by F12); here it is a modal panel closed by F12, Esc or Enter. Its sprites (the panel
// art of MultiWin_init) are box sprites here; the multiplayer parts (time controls, chat flags,
// the chat button) are not reconstructed.
#include <cstdio>
#include <string>
#include <vector>

#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/screenwin.h"
#include "ui/win.h"

namespace {

constexpr int32_t kCouncil = 1;  // MultiWin_on_button_clicked: 0 chat, 1 council

struct MultiWinState : ScreenWin {
    Font *f_text = nullptr, *f_bold = nullptr;
    std::vector<int32_t> rows;  // the factions listed
    int32_t menu_faction = -1;  // the faction of the open right-click menu
    std::vector<std::pair<int32_t, std::string>> menu;  // PullDown items (id, text)
    UiRect menu_rect{};
    std::string last_action;

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
    void act(int32_t item, int32_t f);
};

MultiWinState g_mp;
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

void MultiWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    spot_init(&spots[0], 0x100);
    Buffer_box_sprite(c, &all, box_def(9));
    rows.clear();
    for (int32_t f = 1; f < 8; f++)
        if (listed(f)) rows.push_back(f);
    int32_t y = 6;
    if (rows.empty()) {
        sw_font(c, f_bold, 0xFF);
        sw_text_cent(this, c, sw_label(0x9D0), {0, y, kW, y + kRowH});  // NO CONTACTS
    }
    for (size_t i = 0; i < rows.size(); i++) {
        int32_t f = rows[i];
        UiRect r{4, y, kW - 4, y + kRowH};
        Buffer_box_sprite(c, &r, box_def(1));
        spot_add(&spots[0], f, 1, r.left, r.top, r.right - r.left, r.bottom - r.top);
        Buffer_set_font(c, f_text, nullptr, nullptr, nullptr);
        Buffer_set_text_color(c, text_color(0x68FA30 + (uint32_t)f * 4), text_color(0x68FA50 + (uint32_t)f * 4), 1, 1);
        UiRect tr{r.left + 4, r.top, r.right - 4, r.bottom};
        sw_text(this, c, std::to_string(i + 1) + ". " + MFactions[f].name_leader, tr.left, tr.top + 1);
        std::string t = treaty(f);
        if (!t.empty()) sw_text_right(this, c, t, tr);
        y += kRowH + 2;
    }
    // The viewer's votes (aliens: none shown) below the Council button.
    y = c->height - 0x16;
    if (!(MFactions[CurrentPlayerFaction].rule_flags & 0x80)) {
        sw_font(c, f_bold, 0xFF);
        sw_text_cent(this, c, sw_label(0x80C) + ":  " + std::to_string(council_votes(CurrentPlayerFaction)),
                     {0, y, kW, y + kRowH});
    }
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

void MultiWinState::on_button(int32_t id) {
    if (id == kCouncil) {
        last_action = "council";
        std::fprintf(stderr, "multiwin council\n");
        call_council(CurrentPlayerFaction);
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
    if (type != 1 || index < 0) return;
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
    std::fprintf(stderr, " votes %d menu %d\n", council_votes(CurrentPlayerFaction), menu_faction);
}

}  // namespace

// MultiWin_show (0x479970) with the modal run of smacx (see above).
void THISCALL MultiWin_show(uint8_t*) {
    MultiWinState* s = &g_mp;
    if (s->open) return;
    s->name = "commlink";
    s->menu_faction = -1;
    basewin_mi_init();
    int32_t h = 7 * (kRowH + 2) + 0x30;
    sw_init(s, ScreenWidth - kW, ScreenHeight - MiConsoleHeight - h - 0x2A, kW, h, false);
    s->f_text = find_font(10, 0);
    s->f_bold = find_font(12, 1);
    sw_clear_buttons(s);
    BaseButton* b = sw_button(s, sw_label(0x7E8).c_str(), kCouncil, 0x0A, h - 0x2C, kW - 0x14, 0x12, 0, s->f_bold);
    b->text_color[0] = 0x99;
    b->text_color[1] = b->text_color[2] = 0xFF;
    sw_run(s);
    sw_clear_buttons(s);
}

// MultiWin_hide (0x479C00) as MainInterface_on_status(0) calls it when another interface mode
// starts (the diplomacy dialog): the commlink window closes.
void multiwin_hide() {
    if (g_mp.open) {
        sw_close(&g_mp);
        sw_hide(&g_mp);
    }
}
