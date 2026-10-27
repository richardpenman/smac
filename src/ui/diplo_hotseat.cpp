// diplo (0x444D90 -> FUN_00443F10): the diplomacy between two human players, the DiploWin (ui
// module 0x440F80..0x445080; interface mode 11). commlink_attempt calls it in a hotseat / PBEM
// game when the faction called is human, and control_turn's handover when another human left
// offers for the new player (DiploStateB bit 1).
//
// From the asm: the two sides' offers (at most 8 each: type and value; 0 a technology, a
// commlink frequency (0x59 + faction) or the maps (9999), 1 energy credits, 2 a pact, 3 a treaty,
// 4 a truce, 5 a vendetta against a faction), each side's state (0 nothing, 1 agreed, 2 rejected,
// 3 done) and the buttons (FUN_004415C0: 2 AGREE, 3 REJECT, 4 CLOSE, 5 a technology (DIPLOTECH:
// ours not offered yet, "*" those the other already has, the commlinks of the factions we have a
// commlink with and they not, the maps), 6 energy (DIPLOENERGY, at most our credits), 7 every
// technology they lack, 8 a vendetta (DIPLOVENDETTA: the factions either side knows and the
// humans; DIPLONOVENDETTA when none), 9 truce, 10 treaty, 11 pact, 12 end the pact (DONTHAVEPACT /
// REALLYENDPACT: net_pact_ends)); an offer added or removed clears both sides' states
// (FUN_00441490 / FUN_004413F0; a click on one of our offers removes it: FUN_00442510); both
// sides agreed: the deal is done (FUN_00442740: net_tech, net_energy (at most the giver's
// credits), net_treaty_on, and for a vendetta net_treaty_on(vendetta) + net_double_cross); the
// chat (FUN_00442590: Enter sends the edit line "LEADER: text", an empty line or Esc closes).
// In a hotseat / PBEM game the state is kept per pair of factions (DiploStateC[me][them] and
// the mirror [them][me] with the sides swapped), the other player is called at their turn when
// we changed something and have offers or a state (DiploStateB[them][me] bit 1), and the chat's
// last three lines are kept in DiploMessageLists (FUN_00441040: appended at the end, as the
// original, also the lines shown again; FUN_00441100: the last three shown at the opening).
// The layout: the window (0x340 wide over the main interface, the rectangles of FUN_00443F10),
// the relations line, the two leaders' boxes with their portraits, states and offers, TRADE and
// DIPLOMACY with their buttons, AGREE / REJECT / CLOSE; the chat in the main interface's lower
// centre pane (StringBox, EditBox).
// Approximated: the boxes' art (interface.pcx box sprites as the original's), the StringBox and
// EditBox (a list of the last lines and a text line), the panes' FUN_00443E50 background; the
// multiplayer messages (message_data, message_chat) are the online phase's (no-ops here).
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
#include "ui/win.h"

namespace {

struct DiploWin : ScreenWin {
    int32_t me = 0, them = 0;  // +0xAB4, +0xAB8
    int32_t count[2] = {};  // +0xA1C: the offers per side (0 ours, 1 theirs)
    int32_t state[2] = {};  // +0xA24: 0 nothing, 1 agreed, 2 rejected, 3 done
    int32_t type[2][8] = {}, value[2][8] = {};  // +0xA2C, +0xA6C
    std::vector<std::pair<int32_t, std::string>> chat;  // the StringBox (faction, line)
    std::string edit;  // the EditBox
    Font *f14b = nullptr, *f14 = nullptr, *f12 = nullptr;
    int32_t h = 0;
    UiRect r_frame{}, r_status{}, r_name[2]{}, r_offers[2]{}, r_title[2]{}, r_group[2]{}, r_button[13]{};

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
    void layout();
};

DiploWin g_dw;
bool g_changed = false;  // DAT_007492D0: something changed while the window was open

int32_t faction_of(int32_t side) { return side == 0 ? g_dw.me : g_dw.them; }
std::string sl(uint32_t off) { return label_get(off / 4); }
uint32_t dstatus(int32_t f, int32_t fw) { return Factions[f].diplo_status[fw]; }

void refresh() {  // vtable +0xF8: the window redrawn
    g_dw.texts.clear();
    g_dw.redraw();
    ui_invalidate();
}

void log_offer(const char* what, int32_t side, int32_t t, int32_t v) {
    std::fprintf(stderr, "diplo %s: side %d type %d value %d\n", what, side, t, v);
}

// FUN_004413F0: offer `idx` of `side` removed.
void remove_offer(int32_t side, int32_t idx) {
    if (side == 0) message_data(0x1507, g_dw.them, idx, 0, 0, 0);
    if (idx >= g_dw.count[side]) return;
    log_offer("remove", side, g_dw.type[side][idx], g_dw.value[side][idx]);
    for (int32_t i = idx; i < g_dw.count[side] - 1; i++) {
        g_dw.type[side][i] = g_dw.type[side][i + 1];
        g_dw.value[side][i] = g_dw.value[side][i + 1];
    }
    g_dw.count[side]--;
    g_changed = true;
    g_dw.state[0] = g_dw.state[1] = 0;
    refresh();
}

// FUN_00441490: an offer added to `side`: a treaty replaces the side's treaty, energy replaces
// the energy (energy 0 or less removes it), a technology or a vendetta already offered changes
// nothing; at most 8.
void add_offer(int32_t side, int32_t t, int32_t v) {
    g_changed = true;
    int32_t i = 0, n = g_dw.count[side];
    for (; i < g_dw.count[side]; i++) {
        int32_t o = g_dw.type[side][i];
        if ((t == 4 || t == 2 || t == 3) && (o == 4 || o == 2 || o == 3)) goto set;
        if (t == 1) {
            if (o == 1) {
                if (v < 1) {
                    remove_offer(side, i);
                    return;
                }
                goto set;
            }
        } else if (t == 0) {
            if (o == 0 && g_dw.value[side][i] == v) return;
        } else if (t == 5 && o == 5 && g_dw.value[side][i] == v) {
            return;
        }
        n = g_dw.count[side];
    }
    i = n;
    if (i >= 8) return;
    g_dw.count[side] = i + 1;
set:
    g_dw.type[side][i] = t;
    g_dw.value[side][i] = v;
    g_dw.state[0] = g_dw.state[1] = 0;
    log_offer("offer", side, t, v);
    if (side == 0) message_data(0x1506, g_dw.them, t, v, 0, 0);
    refresh();
}

// FUN_00442740: both sides agreed, the deal is done.
void execute() {
    if (!((g_dw.me <= g_dw.them || PbemActive) && NetCommlinkActive < 2)) return;
    NetCommlinkActive = 2;
    if (diplo_lock(200)) return;
    for (int32_t side = 0; side < 2; side++) {
        int32_t giver = side == 0 ? g_dw.me : g_dw.them, taker = side == 0 ? g_dw.them : g_dw.me;
        for (int32_t i = 0; i < g_dw.count[side]; i++) {
            int32_t v = g_dw.value[side][i];
            switch (g_dw.type[side][i]) {
            case 0:
                if (v > 0x58 || !has_tech(v, taker)) net_tech(taker, v, giver, 1);
                break;
            case 1: {
                int32_t credits = Factions[giver].energy_credits;
                if (v < 0 || credits < 0) v = 0;
                else if (credits < v) v = credits;
                net_energy(giver, -v, taker, v, 1);
                break;
            }
            case 2: net_treaty_on(giver, taker, 1, 1); break;
            case 3: net_treaty_on(giver, taker, 2, 1); break;
            case 4: net_treaty_on(giver, taker, 4, 1); break;
            case 5:
                if ((FactionStatus[0] & (1 << v)) && !(dstatus(giver, v) & 0x10))
                    message_data(0x150F, v, giver, taker, 0, 0);
                net_treaty_on(giver, v, 8, 0);
                net_double_cross(giver, v, taker, 1);
                break;
            default: break;
            }
        }
    }
    diplo_unlock();
    NetCommlinkActive = 1;
    g_dw.state[1] = 3;
    g_dw.state[0] = 3;
    g_dw.count[1] = 0;
    g_dw.count[0] = 0;
    std::fprintf(stderr, "diplo: deal done between %d and %d\n", g_dw.me, g_dw.them);
    message_data(0x150D, g_dw.them, 0, 0, 0, 0);
    refresh();
}

bool offered(int32_t t, int32_t v) {
    for (int32_t i = 0; i < g_dw.count[0]; i++)
        if (g_dw.type[0][i] == t && g_dw.value[0][i] == v) return true;
    return false;
}

// The factions whose commlink frequency we can give (we have it, they not).
bool commlink_to_give(int32_t f) {
    return f != g_dw.me && f != g_dw.them && (FactionStatus[1] & (1 << f)) && (dstatus(g_dw.me, f) & 8) &&
           !(dstatus(g_dw.them, f) & 8);
}

void say_faction_slots(int32_t f) {
    const MFaction& m = MFactions[f];
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(0, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.name_leader, -1, -1);
    PluralDefault = m.is_noun_plural;
    GenderDefault = m.noun_gender;
    parse_says(2, m.noun_faction, -1, -1);
}

void random_offers() {  // (single player only: the window's test offers, from game_rand)
    if (!(game_rand() & 1)) {
        add_offer(1, 0, game_rand() % 0x59);
        if (game_rand() % 3 != 0) add_offer(1, 0, game_rand() % 0x59);
    } else {
        add_offer(1, 1, ((game_rand() % 0x32) * 5 + 5) * 5);
    }
    switch (game_rand() & 3) {
    case 0: add_offer(1, 4, 0); [[fallthrough]];
    case 1: add_offer(1, 3, 0); [[fallthrough]];
    case 2: add_offer(1, 2, 0); break;
    default: break;
    }
}

// FUN_004415C0: the window's commands.
void command(int32_t id) {
    switch (id) {
    case 0:
        g_dw.count[0] = 0;
        message_data(0x1508, g_dw.them, 0, 0, 0, 0);
        refresh();
        break;
    case 1: Console_chat(&MainConsole, 0); break;
    case 2:
        g_changed = true;
        g_dw.state[0] = 1;
        refresh();
        message_data(0x1509, g_dw.them, 1, 0, 0, 0);
        std::fprintf(stderr, "diplo: faction %d agrees\n", g_dw.me);
        if (g_dw.state[0] == 1 && g_dw.state[1] == 1) {
            message_data(0x150A, g_dw.them, 0, 0, 0, 0);
            if (PbemActive) execute();
        }
        break;
    case 3: {
        g_changed = true;
        g_dw.state[0] = 2;
        refresh();
        message_data(0x1509, g_dw.them, 2, 0, 0, 0);
        std::fprintf(stderr, "diplo: faction %d rejects\n", g_dw.me);
        if (MultiplayerActive || PbemActive) break;
        g_dw.count[1] = 0;
        random_offers();
        int32_t r = game_rand() % 3;
        if (r == 0) g_dw.state[1] = 1;
        else if (r == 1) g_dw.type[0][0] = 2;  // (in_ECX[0x28B] = 2, as the original)
        refresh();
        break;
    }
    case 5: {
        BasePop* pop = popup_new();
        Popup_start(pop, ScriptFileName, "DIPLOTECH", -1, nullptr, 0x40);
        int32_t n = 0;
        for (int32_t t = 0; t < 0x59; t++) {
            if (!has_tech(t, g_dw.me) || offered(0, t)) continue;
            bool has = has_tech(t, g_dw.them);
            StrBuffer[0] = 0;
            if (has) crt_strcat(StrBuffer, "*");
            say_tech_2(StrBuffer, t, 1);
            if (GameLanguage == 0 && has) crt_strcat(StrBuffer, " (already has)");
            BasePop_item(pop, StrBuffer, t);
            n++;
        }
        for (int32_t f = 1; f < 8; f++) {
            if (!commlink_to_give(f) || offered(0, f + 0x59)) continue;
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, f + 0x59, 1);
            BasePop_item(pop, StrBuffer, f + 0x59);
            n++;
        }
        if (!offered(0, 9999)) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, sl(0x4C8).c_str());
            BasePop_item(pop, StrBuffer, 9999);
            n++;
        }
        if (n) {
            int32_t r = BasePop_exec_3(pop, 0, 0);
            if (r >= 0) add_offer(0, 0, r);
        }
        popup_delete(pop);
        break;
    }
    case 6: {
        int32_t v = 0;
        for (int32_t i = 0; i < g_dw.count[0]; i++)
            if (g_dw.type[0][i] == 1) v = g_dw.value[0][i];
        if (X_pop_ask_number("DIPLOENERGY", v, 0)) break;
        int32_t credits = Factions[g_dw.me].energy_credits;
        if (credits < ParseNumTable[0]) ParseNumTable[0] = credits;
        add_offer(0, 1, ParseNumTable[0]);
        break;
    }
    case 7:
        for (int32_t t = 0; t < 0x59; t++)
            if (has_tech(t, g_dw.me) && !offered(0, t) && !has_tech(t, g_dw.them)) add_offer(0, 0, t);
        for (int32_t f = 1; f < 8; f++)
            if (commlink_to_give(f) && !offered(0, f + 0x59)) add_offer(0, 0, f + 0x59);
        add_offer(0, 0, 9999);
        break;
    case 8: {
        BasePop* pop = popup_new();
        Popup_start(pop, ScriptFileName, "DIPLOVENDETTA", -1, nullptr, 0x40);
        int32_t n = 0;
        for (int32_t f = 1; f < 8; f++) {
            if (f == g_dw.me || f == g_dw.them) continue;
            if (!(dstatus(g_dw.me, f) & 8) && !(dstatus(g_dw.them, f) & 8) && !(FactionStatus[0] & (1 << f)))
                continue;
            n++;
            StrBuffer[0] = 0;
            say_faction_slots(f);
            parse_it(ScriptFileName, "ATTACKSTRING");
            BasePop_item(pop, StrBuffer, f);
        }
        if (n < 1) {
            NetMsg_pop(&NetMsg, "DIPLONOVENDETTA", 5000, 0, nullptr);
        } else {
            int32_t r = BasePop_exec_3(pop, 0, 0);
            if (r >= 0) add_offer(0, 5, r);
        }
        popup_delete(pop);
        break;
    }
    case 9: add_offer(0, 4, 0); break;
    case 10: add_offer(0, 3, 0); break;
    case 11: add_offer(0, 2, 0); break;
    case 12: {
        say_faction_slots(g_dw.them);
        if (!(dstatus(g_dw.me, g_dw.them) & 1)) {
            X_pop("DONTHAVEPACT", 0);
        } else if (X_pop("REALLYENDPACT", 0)) {
            if (!diplo_lock(0x32)) {
                net_pact_ends(g_dw.me, g_dw.them, 1);
                diplo_unlock();
            }
            g_changed = true;
        }
        break;
    }
    case -2:
    case 4: sw_close(&g_dw); break;
    default: break;
    }
}

std::string offer_text(int32_t side, int32_t i, bool* has) {
    std::string t;
    int32_t v = g_dw.value[side][i];
    *has = false;
    switch (g_dw.type[side][i]) {
    case 0:
        StrBuffer[0] = 0;
        if (side == 1 && v < 0x59 && has_tech(v, g_dw.me)) {
            *has = true;
            crt_strcat(StrBuffer, "(");
            say_tech_2(StrBuffer, v, 1);
            crt_strcat(StrBuffer, ")");
        } else {
            say_tech_2(StrBuffer, v, 1);
        }
        return StrBuffer;
    case 1: return std::to_string(v) + " " + sl(0x90);
    case 2: return sl(0x34C);
    case 3: return sl(0x350);
    case 4: return sl(0x354);
    case 5: {
        const MFaction& m = MFactions[v];
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        return sl(0x980) + " " + m.noun_faction;
    }
    default: return t;
    }
}

void DiploWin::layout() {
    // FUN_00443F10's rectangles (window coordinates).
    r_frame = {0x17, 0, 0x340 - 0x17 - 0x20, h};
    const int32_t L = r_frame.left, R = r_frame.right;
    r_status = {L + 7, 7, R - 7, 7 + 0x14};
    int32_t mid = ((r_status.right - r_status.left) + 2) / 2 - 2 + r_status.left;
    r_name[0] = {r_status.left, r_status.top + 0x17, mid, r_status.bottom + 0x17};
    r_name[1] = {mid + 2, r_status.top + 0x17, r_status.right, r_status.bottom + 0x17};
    // Buttons: AGREE / REJECT / CLOSE (2..4) below, a seventh of the width in from each side.
    int32_t w7 = (R - L) / 7, bx = L + w7, bw = ((R - w7) - bx + 2) / 3 - 2;
    for (int32_t i = 0; i < 3; i++) r_button[2 + i] = {bx + i * (bw + 2), h - 0x2E, bx + i * (bw + 2) + bw, h - 0x1A};
    // TRADE's buttons 5..8 and DIPLOMACY's 9..12: two rows of two in each group.
    int32_t gx = L + 7, gw = ((R - gx) - 0xB) / 4 - 2;
    int32_t col[4] = {gx, gx + gw + 2, gx + 2 * (gw + 2) + 6, gx + 3 * (gw + 2) + 6};
    for (int32_t k = 0; k < 2; k++) {
        r_button[5 + k] = {col[k], h - 0x62, col[k] + gw, h - 0x4E};
        r_button[7 + k] = {col[k], h - 0x4C, col[k] + gw, h - 0x38};
        r_button[9 + k] = {col[2 + k], h - 0x62, col[2 + k] + gw, h - 0x4E};
        r_button[11 + k] = {col[2 + k], h - 0x4C, col[2 + k] + gw, h - 0x38};
    }
    r_title[0] = {col[0], h - 0x78, col[1] + gw, h - 100};
    r_title[1] = {col[2], h - 0x78, col[3] + gw, h - 100};
    for (int32_t k = 0; k < 2; k++)
        r_group[k] = {r_title[k].left - 3, r_title[k].top - 3, r_title[k].right + 3, h - 0x38 + 3};
    int32_t ox = L + 7, ow = ((R - ox) - 5) / 2 - 2;
    r_offers[0] = {ox, r_name[0].bottom + 3, ox + ow, r_group[0].top - 7};
    r_offers[1] = {ox + ow + 2, r_name[0].bottom + 3, ox + 2 * ow + 2, r_group[0].top - 7};
}

void DiploWin::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    Buffer_box_sprite(c, &r_frame, box_def(9));
    spot_kill_type(&spots[0], 0);
    spot_kill_type(&spots[0], 1);
    // FUN_004436C0: the groups, TRADE, DIPLOMACY, the relations line.
    for (int32_t k = 0; k < 2; k++) {
        Buffer_box_sprite(c, &r_group[k], box_def(3));
        Buffer_box_sprite(c, &r_title[k], box_def(6));
        sw_font(c, f14b, 0x99);
        UiRect t{r_title[k].left + 3, r_title[k].top + 3, r_title[k].right - 3, r_title[k].bottom - 3};
        std::string s = k == 0 ? "TRADE" : "DIPLOMACY";
        texts.push_back(s);
        Buffer_write_cent_l_7(c, s.c_str(), &t, (int32_t)s.size());
    }
    Buffer_box_sprite(c, &r_status, box_def(6));
    {
        uint32_t d = dstatus(me, them);
        std::string rel;
        int32_t color = 0xFA;
        if (d & 1) rel = sl(0x34C);
        else if (d & 2) rel = sl(0x350);
        else if (d & 4) rel = sl(0x354);
        else if (d & 0x10) rel = sl(0x358), color = 0xF9;
        else rel = sl(0x784), color = 0xFB;
        std::string s = sl(0x748) + "  (" + sl(0x788) + ": {" + rel + "})";
        sw_font(c, f14b, 0x99);
        Buffer_set_text_color2(c, color, -1, 1, 1);
        texts.push_back(s);
        UiRect t{r_status.left + 3, r_status.top + 3, r_status.right - 3, r_status.bottom - 3};
        Buffer_write_cent_l_7(c, s.c_str(), &t, (int32_t)s.size());
    }
    // FUN_00442A10 / FUN_00442DC0: each leader's name, portrait and state.
    static const uint32_t kState[4] = {0x73C, 0x740, 0x744, 0x780};
    for (int32_t side = 0; side < 2; side++) {
        int32_t f = faction_of(side);
        if (f < 1) continue;
        const MFaction& m = MFactions[f];
        std::string name = std::string(m.title_leader) + " " + m.name_leader;
        win32::CharUpperA(&name[0]);
        Buffer_box_sprite(c, &r_name[side], box_def(6));
        sw_font(c, f14b, text_color(0x68FA30 + (uint32_t)f * 4), text_color(0x68FA50 + (uint32_t)f * 4));
        UiRect t{r_name[side].left + 3, r_name[side].top + 3, r_name[side].right - 3, r_name[side].bottom - 3};
        texts.push_back(name);
        Buffer_write_cent_l_7(c, name.c_str(), &t, (int32_t)name.size());
        const UiRect& o = r_offers[side];
        Buffer_box_sprite(c, &o, box_def(0));
        Sprite* p = &BaseSprites[f][31];
        int32_t px = side == 0 ? o.left + 1 : o.right - 1 - p->full_width;
        pfncSpriteStretchCopyToCanvas(p, c, p->key, px, o.top + 1);
        std::string st = state[side] >= 0 && state[side] < 4 ? sl(kState[state[side]]) : std::string();
        win32::CharUpperA(&st[0]);
        sw_font(c, f14b, state[side] == 2 ? text_color(0x68FA30) : 0xFF);
        UiRect sr{px + 4, o.top + 1 + p->full_height - 0x18, px + p->full_width - 4, o.top + 1 + p->full_height - 4};
        texts.push_back(st);
        Buffer_write_cent_l_7(c, st.c_str(), &sr, (int32_t)st.size());
        // FUN_00443170: the offers, centred beside the portrait (a spot each: index, side).
        int32_t lx = side == 0 ? o.left + 1 + p->full_width + 3 : o.left + 3;
        int32_t rx = side == 0 ? o.right - 3 : px - 3;
        int32_t lh = f14->height ? f14->height : 16;
        sw_font(c, f14, text_color(0x68FA30 + (uint32_t)f * 4), text_color(0x68FA50 + (uint32_t)f * 4));
        std::string head = sl(count[side] == 0 ? 0x734 : 0x738);
        texts.push_back(head);
        Buffer_write_cent_l_5(c, head.c_str(), lx, o.top + 3, rx - lx, (int32_t)head.size());
        int32_t y = o.top + 3 + lh;
        for (int32_t i = 0; i < count[side]; i++) {
            bool has = false;
            std::string s = offer_text(side, i, &has);
            if (has) Buffer_set_text_color(c, text_color(0x68A5C4), 0, 1, 1);
            else
                Buffer_set_text_color(c, text_color(0x68FA30 + (uint32_t)f * 4),
                                      text_color(0x68FA50 + (uint32_t)f * 4), 1, 1);
            texts.push_back(s);
            Buffer_write_cent_l_5(c, s.c_str(), lx, y, rx - lx, (int32_t)s.size());
            spot_add(&spots[0], i, side, lx, y, rx - lx, lh);
            y += lh;
        }
    }
    // The chat (StringBox, EditBox) in the main interface's lower centre pane.
    if (mi.data) {
        sw_restore_iface(this);
        Buffer_box_sprite(&mi, &mi_rect(kMiPaneQueue), box_def(3));
        Buffer_box_sprite(&mi, &mi_rect(kMiPaneRight), box_def(3));
        const UiRect& p = mi_rect(kMiPaneCenter2);
        UiRect box{p.left + 7, p.top + 7, p.right - 7, p.bottom - 0x1B - 3};
        int32_t lh = f12->height ? f12->height : 14;
        int32_t rows = std::max(1, (box.bottom - box.top) / lh);
        int32_t first = std::max(0, (int32_t)chat.size() - rows);
        int32_t y = box.top;
        for (size_t i = (size_t)first; i < chat.size(); i++, y += lh) {
            int32_t f = chat[i].first;
            sw_font(&mi, f12, f > 0 && f < 8 ? text_color(0x68FA30 + (uint32_t)f * 4) : 0xFF, -1);
            UiRect lr{box.left, y, box.right, y + lh};
            Buffer_write_l_4(&mi, chat[i].second.c_str(), &lr, (int32_t)chat[i].second.size());
        }
        UiRect e{p.left + 4, p.bottom - 0x18, p.right - 4, p.bottom - 4};
        Buffer_box_sprite(&mi, &e, box_def(6));
        sw_font(&mi, f12, 0xFF, -1);
        std::string line = edit + "_";
        UiRect er{e.left + 4, e.top + 3, e.right - 4, e.bottom - 3};
        Buffer_write_l_4(&mi, line.c_str(), &er, (int32_t)line.size());
    }
}

void DiploWin::on_button(int32_t id) { command(id); }

// FUN_00442590 / FUN_00442560: Esc closes; Enter sends the line (empty: closes); other keys edit.
void DiploWin::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (vk == 0x1B) {
        command(4);
        return;
    }
    if (vk == 0xD) {
        if (edit.empty()) {
            command(4);
            return;
        }
        message_chat(0x1E00, them, edit.c_str());
        std::string line = MFactions[CurrentPlayerFaction].name_leader;
        win32::CharUpperA(&line[0]);
        line += ": ";
        line += " ";
        line += edit;
        chat.push_back({CurrentPlayerFaction, line});
        std::fprintf(stderr, "diplo chat: %s\n", line.c_str());
        g_changed = true;
        edit.clear();
        return;
    }
    if (vk == 8) {
        if (!edit.empty()) edit.pop_back();
        return;
    }
    bool shift = key & 0x10000;
    char ch = 0;
    if (vk >= 'A' && vk <= 'Z') ch = (char)(shift ? vk : vk - 'A' + 'a');
    else if (vk >= '0' && vk <= '9') ch = (char)vk;
    else if (vk == 0x20) ch = ' ';
    if (ch && edit.size() < 100) edit += ch;  // (EditBox +0x2B68: 100 characters)
}

// FUN_00442510: a click on one of our offers removes it.
void DiploWin::on_click(int32_t parent, int32_t index, int32_t type, int32_t, int32_t, bool right, bool) {
    if (parent != 0 || right || index < 0 || type != 0) return;
    remove_offer(0, index);
    refresh();
}

void DiploWin::log_state() {
    std::fprintf(stderr, "diplostate win %d-%d state %d %d offers", me, them, state[0], state[1]);
    for (int32_t s = 0; s < 2; s++) {
        std::fprintf(stderr, " [");
        for (int32_t i = 0; i < count[s]; i++) std::fprintf(stderr, "%s%d:%d", i ? " " : "", type[s][i], value[s][i]);
        std::fprintf(stderr, "]");
    }
    std::fprintf(stderr, " chat %d\n", (int32_t)chat.size());
}

uint8_t* pair_list(int32_t a, int32_t b) {
    if (b < a) std::swap(a, b);
    return DiploMessageLists[a][b];
}

}  // namespace

void GAMEAPI diplo(int32_t f, int32_t fw) {
    DiploWin* s = &g_dw;
    if (s->open) return;
    std::fprintf(stderr, "diplo %d %d\n", f, fw);
    s->name = "diplo";
    s->me = f;
    s->them = fw;
    s->h = 0x254 - MiConsoleHeight;
    basewin_mi_init();
    sw_init(s, (ScreenWidth - 800) / 2, (ScreenHeight - MiConsoleHeight) - s->h, 0x340, s->h, true);
    s->layout();
    std::fprintf(stderr, "diplo window at %d,%d (%dx%d)\n", s->x, s->y, 0x340, s->h);
    s->f14b = find_font(14, 1);
    s->f14 = find_font(14, 0);
    s->f12 = find_font(12, 0);
    sw_clear_buttons(s);
    for (int32_t i = 2; i < 13; i++) {
        const UiRect& r = s->r_button[i];
        BaseButton* b = sw_button(s, sl(0xDC4 + (uint32_t)i * 4).c_str(), i, r.left, r.top, r.right - r.left,
                                  r.bottom - r.top, 0, s->f14b);
        b->text_color[0] = b->text_color[1] = b->text_color[2] = 0x41;
        b->box[0] = box_def(27);
        b->box[1] = box_def(28);
        b->box[2] = box_def(29);
    }
    s->chat.clear();
    s->edit.clear();
    if (!PbemActive) {
        s->count[0] = s->count[1] = 0;
        s->state[0] = s->state[1] = 0;
    } else {
        // FUN_00441100: the pair's last three chat lines; the pair's offers and states.
        uint8_t* list = pair_list(f, fw);
        int32_t n = ui_stringlist_count(list);
        for (int32_t i = n - 3; i < n; i++) {
            if (i < 0) continue;
            int32_t id = 0;
            const char* t = ui_stringlist_entry(list, i, &id);
            s->chat.push_back({id, t ? t : ""});
        }
        const int32_t* d = DiploStateC[f][fw];
        for (int32_t side = 0; side < 2; side++) {
            s->count[side] = d[side];
            s->state[side] = d[2 + side];
            for (int32_t j = 0; j < 8; j++) {
                s->type[side][j] = d[4 + side * 8 + j];
                s->value[side][j] = d[20 + side * 8 + j];
            }
        }
    }
    NetCommlinkActive = 1;
    g_changed = false;
    if (bit_count(FactionStatus[0]) == 2) *reinterpret_cast<int32_t*>(mem::host(0x9A64E4)) *= 2;
    if (!NetGameStarted) NetGameStarted = 1;
    if (!MultiplayerActive && !PbemActive) random_offers();
    iface_set_mode(11);  // SubInterface_set_iface_mode (mode 0xB)
    sw_run(s);
    if (PbemActive) {
        // FUN_00441040: the box's last three lines appended to the pair's list.
        int32_t n = (int32_t)s->chat.size();
        for (int32_t i = n - 3; i < n; i++) {
            if (i < 0) continue;
            std::vector<char> t(s->chat[(size_t)i].second.begin(), s->chat[(size_t)i].second.end());
            t.push_back(0);
            ui_stringlist_add(pair_list(f, fw), s->chat[(size_t)i].first, t.data());
        }
        int32_t* a = DiploStateC[f][fw];
        int32_t* b = DiploStateC[fw][f];
        for (int32_t side = 0; side < 2; side++) {
            a[side] = s->count[side];
            a[2 + side] = s->state[side];
            b[side] = s->count[1 - side];
            b[2 + side] = s->state[1 - side];
            for (int32_t j = 0; j < 8; j++) {
                a[4 + side * 8 + j] = s->type[side][j];
                a[20 + side * 8 + j] = s->value[side][j];
                b[4 + side * 8 + j] = s->type[1 - side][j];
                b[20 + side * 8 + j] = s->value[1 - side][j];
            }
        }
        if (g_changed && (s->state[0] != 0 || s->count[0] > 0)) DiploStateB[fw][f] |= 1;
    }
    if (NetCommlinkActive) {
        NetCommlinkActive = 0;
        message_data(0x1505, fw, 0, 0, 0, 0);
    }
    iface_release_mode(11);
    sw_clear_buttons(s);
    sw_hide(s);
}

