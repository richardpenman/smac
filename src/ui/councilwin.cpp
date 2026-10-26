// The Planetary Council window (CouncilWin, ui 0x424410..0x428550) for a human viewer: the window
// operations of engine/council.h (ui_councilwin_*); the session logic (votes, tally, keys) is the
// engine's (council.cpp: CouncilWin_session / CouncilWin_vote / CouncilWin_tally / CouncilWin_key).
//
// As the original (FUN_00427230's window parts, FUN_00424870 the button handler): the window is
// 0x32C wide, as tall as the space above the main interface allows (at most 0x18B), centred at
// x = (width - 0x30C) / 2; the title, seven faction columns (the rectangles of the original's
// layout: portrait, leader, council votes, the vote cast) and the result line; the buttons are in
// the main interface's info pane: YEA (id 1, vote -1), NAY or VETO for the governor (id 2, -2) and
// ABSTAIN (id 3, -3) for proposals, VOTE (id 4: the GOVVOTE menu of candidates, "abstain" -3) for
// elections, OK (id 5: Enter) once decided. Enter / Esc go to CouncilWin_key (before the human
// votes it fills in the computer players' votes; after the result it closes the window and a
// passed proposal is applied). A viewer that is not human (observer mode) does what the
// unattended window does (src/headless/ui_headless_council.cpp): no window is shown.
// Approximated: the panels' art (the portraits are the faction art's sprite 26; the original's
// panel frames and fonts are box sprites and find_font), the vote animations, the multiplayer
// timer and messages.
#include <cstdio>
#include <string>

#include "engine/council.h"
#include "engine/crt.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/win.h"

namespace {

constexpr int32_t kW = 0x32C;
// The faction columns (the original's inner panel rectangles, fields +0xC04..).
const UiRect kCols[7] = {{0x20, 0x1D, 0x82, 0x137},   {0x87, 0x1D, 0xE9, 0x137},   {0xEE, 0x1D, 0x150, 0x137},
                         {0x155, 0x1D, 0x1B7, 0x137}, {0x1BC, 0x1D, 0x21E, 0x137}, {0x223, 0x1D, 0x285, 0x137},
                         {0x28A, 0x1D, 0x2EC, 0x137}};
const UiRect kTitle = {0x1E, 7, 0x2EE, 0x1B};
const UiRect kResult = {0xF5, 0x163, 0x21A, 0x17A};

bool is_human(int32_t f) { return (FactionStatus[0] & (1 << f)) != 0; }
bool is_alive(int32_t f) { return (FactionStatus[1] & (1 << f)) != 0; }
CouncilSession& session(uint8_t* win) { return *reinterpret_cast<CouncilSession*>(win + kCouncilWinSession); }

struct CouncilWinState : ScreenWin {
    uint8_t* win = nullptr;
    Font *f_title = nullptr, *f_text = nullptr, *f_bold = nullptr;
    Sprite portraits[8];
    bool loaded[8] = {};

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void log_state() override;
    void buttons();
};
CouncilWinState g_cw;

std::string vote_text(const CouncilSession& s, int32_t v) {
    switch (v) {
    case 0: return "";
    case -1: return sw_label(0x1EF * 4);
    case -2: return sw_label(0x1F0 * 4);
    case -3: return sw_label(0x1F1 * 4);
    case -5: return sw_label(0x1F6 * 4);
    default: break;
    }
    if (v > 0 && v < 8) return MFactions[v].name_leader;
    (void)s;
    return "";
}

void load_portrait(CouncilWinState* w, int32_t f) {
    if (w->loaded[f]) return;
    w->loaded[f] = true;
    Buffer b;
    Buffer_ctor(&b);
    std::string file = std::string(MFactions[f].filename) + ".pcx";
    if (Buffer_load_pcx(&b, file.c_str(), nullptr, 10, 0xEC) && Buffer_load_pcx(&b, "faction.pcx", nullptr, 10, 0xEC))
        return;
    pfncSpriteFromCanvasRectTrans(&w->portraits[f], &b, 9, 0x58, 0x1C9, 0x60, 0x73, 0);
}

void CouncilWinState::redraw() {
    Buffer* c = &canvas;
    const CouncilSession& s = session(win);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    Buffer_box_sprite(c, &all, box_def(9));
    UiRect t = kTitle;
    Buffer_box_sprite(c, &t, box_def(1));
    std::string title = sw_label(0x1ED * 4) + ": ";
    title += s.proposal == 0 ? sw_label(0x1EE * 4) : std::string(Proposal[s.proposal].name);
    sw_font(c, f_title, 0xFF);
    sw_text_cent(this, c, sw_upper(title), t);
    for (int32_t f = 1; f < 8; f++) {
        UiRect r = kCols[f - 1];
        if (r.bottom > c->height - 4) r.bottom = c->height - 4;
        Buffer_box_sprite(c, &r, box_def(0));
        if (!is_alive(f)) {
            sw_font(c, f_bold, 0xFF);
            sw_text_cent(this, c, sw_label(0x1F6 * 4), {r.left, r.top + 4, r.right, r.top + 20});
            continue;
        }
        load_portrait(this, f);
        if (portraits[f].width) sprite_copy1(&portraits[f], c, 9, r.left + 1, r.top + 1, 1, 1);
        int32_t y = r.top + 0x76;
        sw_font(c, f_bold, 0xFF);
        sw_text_cent(this, c, MFactions[f].name_leader, {r.left, y, r.right, y + 16});
        y += 18;
        sw_font(c, f_text, 0xFF);
        bool alien = MFactions[f].rule_flags & 0x80;
        sw_text_cent(this, c, alien ? sw_label(0x1F5 * 4) : std::to_string(s.council_votes[f]),
                     {r.left, y, r.right, y + 16});
        y += 18;
        if (f == GovernorFaction) {
            sw_text_cent(this, c, sw_label(0x1EE * 4), {r.left, y, r.right, y + 16});
        }
        y += 18;
        sw_font(c, f_bold, 0xFF);
        sw_text_cent(this, c, vote_text(s, s.votes[f]), {r.left, y, r.right, y + 16});
    }
    if (s.result) {
        std::string res;
        if (s.proposal == 0) {
            res = s.result == 1 && s.candidate > 0 && s.candidate < 8
                      ? sw_label(0x1EE * 4) + ": " + MFactions[s.candidate].name_leader
                      : sw_label(0x1F8 * 4);
        } else {
            res = sw_label((s.result == 1 ? 0x1F7 : s.result == -2 ? 0x1F9 : 0x1F8) * 4);
        }
        UiRect r = kResult;
        if (r.bottom > c->height) {
            r.top = c->height - 0x1C;
            r.bottom = c->height - 4;
        }
        Buffer_box_sprite(c, &r, box_def(1));
        sw_font(c, f_title, 0xFF);
        sw_text_cent(this, c, res, r);
    }
}

// The buttons on the main interface (the info pane's bottom row, as the original's layout).
void CouncilWinState::buttons() {
    sw_clear_buttons(this);
    const CouncilSession& s = session(win);
    const UiRect frame = {0x106 + 4, 0x42 + 4, 0x2FB - 4, 0xAC - 4};
    int32_t y0 = frame.bottom - 0x17, y1 = frame.bottom - 3;
    int32_t x0 = frame.left + 3, x_end = frame.right - 3;
    if (s.result) {
        sw_button(this, sw_upper(sw_label(0x65 * 4)).c_str(), 5, x0, y0, x_end - x0, y1 - y0, 1, f_bold);
        return;
    }
    if (s.proposal == 0) {
        sw_button(this, sw_upper(sw_label(0x1FB * 4)).c_str(), 4, x0, y0, x_end - x0, y1 - y0, 1, f_bold);
        return;
    }
    int32_t w = ((x_end - x0) + 3) / 3 - 3;
    const int32_t labels[3] = {0x1EF, GovernorFaction == CurrentPlayerFaction ? 0x1F4 : 0x1F0, 0x1F1};
    for (int32_t b = 0; b < 3; b++)
        sw_button(this, sw_upper(sw_label(labels[b] * 4)).c_str(), b + 1, x0 + b * (w + 3), y0, w, y1 - y0, 1, f_bold);
}

// FUN_00424870 (the window's button handler).
void CouncilWinState::on_button(int32_t id) {
    CouncilSession& s = session(win);
    if (id >= 1 && id <= 3) {
        if (!s.result) CouncilWin_vote(win, CurrentPlayerFaction, -id);
    } else if (id == 4) {
        if (s.result) return;
        // GOVVOTE: abstain, then the eligible candidates.
        BasePop* pop = popup_new();
        Popup_start(pop, ScriptFileName, "GOVVOTE", -1, nullptr, 0x200040);
        BasePop_set_picture(pop, "council_sm.pcx");
        BasePop_item(pop, label_get(0x7C4 / 4), 0);
        for (int32_t i = 1; i < 8; i++) {
            if ((MFactions[i].rule_flags & 0x80) || !is_alive(i) || !eligible(i)) continue;
            GenderDefault = MFactions[i].is_leader_female;
            PluralDefault = 0;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, MFactions[i].name_leader);
            BasePop_item(pop, StrBuffer, i);
        }
        int32_t r = BasePop_exec_3(pop, 0, 0x5398E0);
        popup_delete(pop);
        if (r < 0) return;
        if (r == 0) r = -3;
        CouncilWin_vote(win, CurrentPlayerFaction, r);
    } else if (id == 5) {
        CouncilWin_key(win, 0, 0x0D);
    }
    if (open) buttons();
}

void CouncilWinState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (vk == 0x0D || vk == 0x1B) {
        CouncilWin_key(win, 0, vk);
        if (open) buttons();
    }
}

void CouncilWinState::log_state() {
    const CouncilSession& s = session(win);
    std::fprintf(stderr, "councilstate proposal %d candidate %d result %d governor %d votes", s.proposal, s.candidate,
                 s.result, GovernorFaction);
    for (int32_t i = 1; i < 8; i++) std::fprintf(stderr, " %d", s.votes[i]);
    std::fprintf(stderr, " power");
    for (int32_t i = 1; i < 8; i++) std::fprintf(stderr, " %d", s.council_votes[i]);
    std::fprintf(stderr, "\n");
}

bool viewer_plays() { return is_human(CurrentPlayerFaction) && CurrentPlayerFaction > 0; }

}  // namespace

void ui_councilwin_open(uint8_t* win) { g_cw.win = win; }

void ui_councilwin_build(uint8_t* win) {
    g_cw.win = win;
    if (!viewer_plays()) return;
    CouncilWinState* s = &g_cw;
    s->name = "council";
    basewin_mi_init();
    int32_t avail = ScreenHeight - MiConsoleHeight;
    int32_t h = avail > 0x18B ? 0x18B : avail;
    sw_init(s, (ScreenWidth - 0x30C) / 2, avail - h, kW, h, true);
    s->slide = true;  // FUN_00426FA0 (the window's on_status): slide_show / slide_hide
    s->f_title = find_font(14, 1);
    s->f_text = find_font(12, 0);
    s->f_bold = find_font(12, 1);
    sw_restore_iface(s);
    s->buttons();
}

int32_t ui_councilwin_show(uint8_t*) { return 0; }

void ui_councilwin_modal(uint8_t* win) {
    if (!viewer_plays()) {
        // Unattended (observer): the vote the window holds for the viewer, then Enter.
        const CouncilSession& s = session(win);
        int32_t viewer = CurrentPlayerFaction;
        if (!s.result) CouncilWin_vote(win, viewer, s.votes[viewer]);
        CouncilWin_key(win, 0, 0x0D);
        return;
    }
    std::fprintf(stderr, "council window proposal %d\n", session(win).proposal);
    sw_run(&g_cw);
    sw_clear_buttons(&g_cw);
}

void ui_councilwin_close(uint8_t*, int32_t) { ui_invalidate(); }
void ui_councilwin_redraw(uint8_t*) { ui_invalidate(); }
void ui_councilwin_panel_draw(uint8_t*, int32_t) { ui_invalidate(); }
void ui_councilwin_button_draw(uint8_t*) { ui_invalidate(); }
void ui_councilwin_release(uint8_t*) {
    if (g_cw.open) sw_close(&g_cw);
}
void ui_councilwin_close_fonts(uint8_t*) { ui_invalidate(); }
