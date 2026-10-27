// The info pane (InfoWin, ui 0x4562F0..0x459500) and the message list of its M mode (MessageWin,
// ui 0x4713A0..0x472190). See infowin.h.
//
// Reconstructed from the asm: InfoWin_init, InfoWin_state_valid, InfoWin_setup_text (every
// display: the year, research and allocation; commerce; the diplomacy partner; the faction's
// standing; the power graph; the score bars; the records; the editor's brush),
// InfoWin_draw_text (the typing: the text up to the progress, its last character in the second
// colour; the blink of the finished text under the mouse), InfoWin_UNK2 (redraw),
// InfoWin_set_state, InfoWin_timer_change / timer_proc / timer_daemon, InfoWin_reset,
// InfoWin_on_left_click (finish the typing, then the next display), InfoWin_right_menu (the
// displays and help); MainInterface_on_redraw's modes 1000 / 0x3E9 (the M / I buttons of
// ButtonGroup 0), MessageWin_add_message / add_base / clear / on_selected (go to the base or
// tile, delete), MainInterface_set_message.
// The original's quirks are kept: draw_text(-1) starts the second column's lines at the first
// column's line count (its text stays from the typing); the ranking names of the power graph are
// cut to nothing (their width is compared with an absolute x; the right-aligned writes use pane
// relative x) so the graph shows without them.
// The message list: ListBox_draw_item's rows with MessageWin's callbacks (see msg_draw).
// Approximated: the message list's scroll bar is not drawn; the character drawn for a
// progress exactly at a line's start (the original writes the byte below its stack buffer, the
// high byte of a saved register).
// Not reconstructed: control_turn's faction boxes in the pane during the computer players' turns
// (the pane keeps its text; the timer does not run then).
#include "ui/infowin.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/diplomacy.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/zoc.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/font_text.h"
#include "ui/help.h"
#include "ui/popup.h"
#include "ui/pulldown.h"
#include "ui/sprites.h"
#include "ui/statuswin.h"
#include "ui/win.h"

GLOBAL(int32_t, EditorPaintType, 0x945AE0);  // the scenario editor's brush (InfoWin state 7)
GLOBAL(int32_t, EditorBrushSize, 0x945AE4);  // its size (1: "the whole continent")
GLOBAL(int32_t, MessageCount, 0x9393D0);  // DAT_009393D0: messages in the list

namespace {

constexpr uint32_t kMiMode = 0x1E8FC;  // MainInterface: pane mode (ButtonGroup 0)

uint8_t* self_() { return &InfoWindow; }
int32_t& fld(uint32_t off) { return *reinterpret_cast<int32_t*>(&InfoWindow + off); }
UiRect& prect(int32_t i) { return *reinterpret_cast<UiRect*>(&InfoWindow + (uint32_t)i * 0x10); }  // 0: all
char* line(int32_t pane, int32_t i) { return reinterpret_cast<char*>(&InfoWindow + 0xB8 + pane * 0x3C0 + i * 0x50); }
int32_t& lx(int32_t pane, int32_t i) { return fld(0x838 + (pane * 12 + i) * 4); }
int32_t& ly(int32_t pane, int32_t i) { return fld(0x898 + (pane * 12 + i) * 4); }
int32_t& lcol(int32_t pane, int32_t i) { return fld(0x8F8 + (pane * 12 + i) * 4); }
int32_t& lcol2(int32_t pane, int32_t i) { return fld(0x958 + (pane * 12 + i) * 4); }
int32_t& nlines(int32_t pane) { return fld(0x9B8 + pane * 4); }
int32_t& nchars(int32_t pane) { return fld(0x9C0 + pane * 4); }
int32_t& prog(int32_t pane) { return fld(0x9C8 + pane * 4); }
int32_t& cur() { return fld(0x80); }
int32_t& state(int32_t pane) { return fld(0x88 + pane * 4); }
Font* font() { return reinterpret_cast<Font*>(&InfoWindow + 0x58); }
int32_t mem_i(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
Buffer* canvas() { return basewin_mi_canvas(); }

// The signed `& 0x80000007` remainder of the original's state arithmetic.
int32_t mod8(int32_t v) {
    uint32_t u = (uint32_t)v & 0x80000007u;
    if ((int32_t)u < 0) u = ((u - 1) | 0xFFFFFFF8u) + 1;
    return (int32_t)u;
}

int32_t font_lh(const Font* f) { return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0; }

// What InfoWin drew in the main interface's canvas (its rectangle +0), kept between the
// console's redraws (GraphicWin_update_2 puts it on the screen in the original).
std::vector<uint8_t> g_saved;
bool g_inited = false;

void save_pixels() {
    Buffer* c = canvas();
    const UiRect& r = prect(0);
    if (!c->data || r.right <= r.left) return;
    int32_t w = r.right - r.left, h = r.bottom - r.top;
    g_saved.resize((size_t)w * h);
    for (int32_t y = 0; y < h; y++)
        std::memcpy(&g_saved[(size_t)y * w], c->data + (size_t)(r.top + y) * c->stride + r.left, (size_t)w);
    ui_invalidate();
}

void restore_pixels(Buffer* c) {
    const UiRect& r = prect(0);
    int32_t w = r.right - r.left, h = r.bottom - r.top;
    if (g_saved.size() != (size_t)w * h || !c->data) return;
    for (int32_t y = 0; y < h; y++)
        std::memcpy(c->data + (size_t)(r.top + y) * c->stride + r.left, &g_saved[(size_t)y * w], (size_t)w);
}

void cat(char* s, const char* t) { crt_strcat(s, t); }
void cat_num(char* s, int32_t v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%d", v);
    crt_strcat(s, b);
}
void set_line(int32_t pane, int32_t i, const char* s) { crt_strcpy(line(pane, i), s); }

// say_special (0x58F610): text in the faction's leader gender, singular.
void say_special(char* out, const char* text, int32_t faction_id) {
    char markup[256], parsed[256];
    markup[0] = 0;
    int32_t g = MFactions[faction_id].is_leader_female;
    crt_strcat(markup, "$<");
    if (g == 0) crt_strcat(markup, "M");
    else if (g == 1) crt_strcat(markup, "F");
    else if (g == 2) crt_strcat(markup, "N");
    crt_strcat(markup, "1");
    crt_strcat(markup, ":");
    crt_strcat(markup, text);
    crt_strcat(markup, ">");
    parse_string(markup, parsed);
    crt_strcat(out, parsed);
}

// "title name" of a faction's leader (the leader's gender, singular).
void cat_leader(char* s, int32_t f) {
    GenderDefault = MFactions[f].is_leader_female;
    PluralDefault = 0;
    cat(s, MFactions[f].title_leader);
    cat(s, " ");
    GenderDefault = MFactions[f].is_leader_female;
    PluralDefault = 0;
    cat(s, MFactions[f].name_leader);
}

// The living faction with the largest value (ties: the later one); the viewer when none is above 0.
template <class F>
int32_t best_faction(F value) {
    int32_t best = 0, who = CurrentPlayerFaction;
    for (int32_t f = 1; f < 8; f++) {
        if (!(FactionStatus[1] & (1 << f))) continue;
        int32_t v = value(f);
        if (best <= v) {
            best = v;
            who = f;
        }
    }
    return who;
}

// ---- MessageWin -------------------------------------------------------------------------------

struct Message {
    std::string text;
    uint32_t id;
};
std::deque<Message> g_msgs;
Font g_msg_font;
bool g_msg_font_ok = false;
int32_t g_msg_first = 0;  // first row shown

UiRect msg_rect() { return mi_rect(0xD1C); }

// MessageWin_init's ListBox (Dialog_init_6 on MainInfc +0xD1C): rows a fifth of the box high
// (+0x48) less the items' spacing (6, the Dialog default), 3 pixels above and below (+0x38 /
// +0x3C), no left margin, the text after a column a quarter of the box's height (+0x34) and a
// pixel.
constexpr int32_t kMsgSpacing = 6, kMsgMargin = 3;
int32_t msg_pitch() { return std::max(1, (msg_rect().bottom - msg_rect().top) / 5); }

int32_t msg_rows() {  // ListBox_calculate_dimensions' rows
    UiRect r = msg_rect();
    return std::max(1, (r.bottom - r.top - 2 * kMsgMargin + kMsgSpacing) / msg_pitch());
}

void msg_scroll_end() {  // ListBox_set_selected_pos(-1, 1)
    g_msg_first = std::max(0, (int32_t)g_msgs.size() - msg_rows());
    ui_invalidate();
}

// ListBox_on_redraw with MessageWin's callbacks: the box (MessageWin_listbox_fill: fill 9 and box
// 18), each row's icon (MessageWin_listbox_draw: 4 pixels in, centred on the row) and its text
// (ListBox_draw_item: Buffer_write_l_4 at the row's top in the list font, the colours of
// MessageWin_listbox_write).
void msg_draw(Buffer* c) {
    UiRect r = msg_rect();
    Buffer_fill_3(c, &r, 9);
    Buffer_box_sprite(c, &r, box_def(18));
    if (!g_msg_font_ok) return;
    const int32_t h = r.bottom - r.top, pitch = msg_pitch(), row_h = pitch - kMsgSpacing, col = h / 4;
    Sprite* icon = message_icon_sprite();
    Buffer_set_font(c, &g_msg_font, nullptr, nullptr, nullptr);
    for (int32_t i = g_msg_first, k = 0; i < (int32_t)g_msgs.size() && k < msg_rows(); i++, k++) {
        const Message& m = g_msgs[(size_t)i];
        int32_t top = r.top + kMsgMargin + k * pitch;
        UiRect item{r.left, top, r.right, top + row_h};
        Buffer_set_clip(c, &item);
        if (icon->width) {
            BlitDestScale = BlitSourceScale = 1;
            pfncSpriteStretchCopyToCanvas(icon, c, icon->key & 0xFF, r.left + 4, top + (row_h - icon->full_height) / 2);
        }
        int32_t shadow = mem_i(0x68A5B0);
        if ((m.id & 0x10000000) && (m.id & 0xEFFFFFFF)) shadow = -1;
        Buffer_set_text_color(c, mem_i(0x68A5A4), shadow, 1, 1);
        UiRect text{r.left + col + 1, top, r.right, top + row_h};
        Buffer_set_clip(c, &text);
        Buffer_write_l_3(c, m.text.c_str(), text.left, top, (int32_t)m.text.size());
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
}

// ---- the mode buttons (MainInterface_init: 1000 "M", 0x3E9 "I", 0x3EA the editor's, hidden) ----

BaseButton g_btn[3];  // 1000, 0x3E9, 0x3EA
Font g_btn_font;
bool g_editor_button = false;  // EditorPanelWin shown (before the buttons exist too)

}  // namespace

int32_t mi_mode() { return *reinterpret_cast<int32_t*>(&MainInterfaceWin + kMiMode); }

// ==== InfoWin ===================================================================================

// InfoWin_init (0x4592A0): states unset (-1), the second pane current, the font (Arial 12, 10 at
// 800), the two pane rectangles in the main interface's info pane (+0xDA0; one at 800) and the
// typing timer (10, every 50 ms).
void THISCALL InfoWin_init(uint8_t*) {
    fld(0x88) = -1;
    fld(0x8C) = -1;
    fld(0x90) = -1;
    fld(0xB4) = -1;
    fld(0xAC) = 0;
    fld(0x94) = 0;
    fld(0xA8) = 0;
    fld(0x84) = 1;
    fld(0x80) = 1;
    Font_init(font(), "Arial", ScreenWidth == 800 ? 10 : 12, 0);
    font()->field_0 = 4;
    const UiRect& c = mi_rect(kMiPaneCenter);
    int32_t l = c.left + 7, t = c.top + 7, r = c.right - 0x27, b = c.bottom - 7;
    if (ScreenWidth == 800) {
        prect(1) = {l, t, r, b};
    } else {
        r = ((r - l) - 3) / 2 + l;
        prect(1) = {l, t, r, b};
        int32_t d = (r - l) + 3;
        l += d;
        r += d;
    }
    prect(2) = {l, t, r, b};
    UiRect& u = prect(0);  // UnionRect
    u = {std::min(prect(1).left, prect(2).left), std::min(prect(1).top, prect(2).top),
         std::max(prect(1).right, prect(2).right), std::max(prect(1).bottom, prect(2).bottom)};
}

// InfoWin_state_valid (0x458DC0).
int32_t GAMEAPI InfoWin_state_valid(int32_t s) {
    if (s == 0) return 1;
    if (!CurrentTurn) return 0;
    const Faction& me = Factions[CurrentPlayerFaction];
    switch (s) {
    case 0:
    case 0xB: return 1;
    case 1: return CurrentTurn > 0x31 && (CurrentTurn > 99 || me.diff_level > 1);
    case 2: return DiploWinState != 0;
    case 3:
        if (DiploWinState) return 1;
        if (CurrentTurn > 0x31 || me.diff_level > 1)
            for (int32_t f = 1; f < 8; f++)
                if (f != CurrentPlayerFaction && (me.diplo_status[f] & 8)) return 1;
        return 0;
    case 4:
    case 5:
    case 6: return CurrentTurn > 0x13 || (GameState & STATE_IS_SCENARIO);
    case 7: return (GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_SCENARIO_EDITOR);
    default: return 0;
    }
}

// InfoWin_setup_text (0x456300): the current pane's box and its lines for its state.
void THISCALL InfoWin_setup_text(uint8_t*) {
    Buffer* c = canvas();
    int32_t viewer = CurrentPlayerFaction;
    int32_t lh = font_lh(font());
    for (int32_t p = 0; p < 2; p++) {
        nlines(p) = 0;
        for (int32_t i = 0; i < 12; i++) {
            line(p, i)[0] = 0;
            lx(p, i) = prect(p + 1).left + 5;
            ly(p, i) = prect(p + 1).top + 5 + i * lh;
            lcol(p, i) = 0;
            lcol2(p, i) = 0;
        }
    }
    int32_t pane = cur();
    UiRect r = prect(pane + 1);
    Buffer_box_sprite(c, &r, box_def(18));
    r = {r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
    int32_t n = 0;
    char s[0x50 * 2];
    Faction& me = Factions[viewer];
    switch (mod8(state(pane))) {
    case 0: {
        s[0] = 0;
        cat(s, label_get(0));
        cat(s, ": {");
        say_year(s);
        cat(s, "}");
        set_line(pane, 0, s);
        energy_compute(viewer, 0);
        if (me.tech_research_id >= 0 && ((GameState & STATE_OMNISCIENT_VIEW) || !(GameRules & RULES_BLIND_RESEARCH))) {
            s[0] = 0;
            cat(s, label_get(0x6A));
            cat(s, ": {");
            cat(s, Tech[me.tech_research_id].name);
            cat(s, "}");
            set_line(pane, 1, s);
        }
        s[0] = 0;
        cat(s, Energy[0].abbrev);
        cat(s, ": {");
        cat_num(s, (10 - me.SE_alloc_labs - me.SE_alloc_psych) * 10);
        cat(s, "%  (");
        if (FactionCurrentNetIncome >= 0) cat(s, "+");
        cat_num(s, FactionCurrentNetIncome);
        cat(s, "/");
        cat(s, label_get(0x30));
        cat(s, ")}");
        set_line(pane, 2, s);
        s[0] = 0;
        cat(s, Energy[1].abbrev);
        cat(s, ": {");
        cat_num(s, me.SE_alloc_psych * 10);
        cat(s, "%}");
        set_line(pane, 3, s);
        s[0] = 0;
        cat(s, Energy[2].abbrev);
        cat(s, ": {");
        cat_num(s, me.SE_alloc_labs * 10);
        cat(s, "%");
        if (FactionCurrentBreakthroughTurns < 9000) {
            cat(s, "  (");
            cat_num(s, FactionCurrentBreakthroughTurns);
            cat(s, " ");
            cat(s, label_get(0x31));
            cat(s, ")");
        }
        cat(s, "}");
        set_line(pane, 4, s);
        n = 5;
        break;
    }
    case 1: {
        energy_compute(viewer, 0);
        auto money = [&](int32_t i, const char* lbl, int32_t v) {
            s[0] = 0;
            cat(s, lbl);
            cat(s, ": {");
            cat_num(s, v);
            cat(s, label_get(0x130));
            cat(s, "}");
            set_line(pane, i, s);
        };
        s[0] = 0;
        cat(s, label_get(300));
        cat(s, ": {");
        cat_num(s, me.tech_commerce_bonus + 1);
        cat(s, "}");
        set_line(pane, 0, s);
        money(1, label_get(0x12D), me.turn_commerce_income);
        money(2, label_get(0x12E), FactionCurrentGrossIncome);
        int32_t i = 3;
        if (GameLanguage == 0) money(i++, "Total Costs", FactionCurrentNetIncome - FactionCurrentGrossIncome);
        money(i, label_get(0x3BF), FactionCurrentNetIncome);
        n = i + 1;
        break;
    }
    case 2: {
        int32_t f = DiploFrictionFactionID;
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        set_line(pane, 0, MFactions[f].formal_name_faction);
        s[0] = 0;
        cat_leader(s, f);
        set_line(pane, 1, s);
        s[0] = 0;
        say_special(s, Strings_get(&StringTable, Mood[get_mood(DiploFriction)]), f);
        set_line(pane, 2, s);
        s[0] = 0;
        uint32_t st = me.diplo_status[f];
        int32_t lbl = (st & 1) ? 0xD3 : (st & 2) ? 0xD4 : (st & 4) ? 0xD5 : (st & 0x10) ? 0xD6 : -1;
        if (lbl >= 0) cat(s, label_get(lbl));
        set_line(pane, 3, s);
        n = 4;
        break;
    }
    case 3: {
        GenderDefault = MFactions[viewer].noun_gender;
        PluralDefault = MFactions[viewer].is_noun_plural;
        set_line(pane, 0, MFactions[viewer].formal_name_faction);
        s[0] = 0;
        cat_leader(s, viewer);
        set_line(pane, 1, s);
        s[0] = 0;
        cat(s, label_get(0xDE));
        cat(s, ": {");
        say_special(s, Strings_get(&StringTable, Repute[std::max(0, std::min(7, me.integrity_blemishes))]), viewer);
        cat(s, "}");
        set_line(pane, 2, s);
        s[0] = 0;
        cat(s, label_get(0xDF));
        cat(s, ": {");
        say_special(s, Strings_get(&StringTable, Might[7 - std::max(1, std::min(7, me.ranking))].adj_start), viewer);
        cat(s, "}");
        set_line(pane, 3, s);
        s[0] = 0;
        cat(s, label_get(0x3C0));
        cat(s, ": {");
        cat_num(s, council_votes(viewer));
        cat(s, "}");
        set_line(pane, 4, s);
        n = 5;
        break;
    }
    case 4:
    case 5: {
        bool bars = mod8(state(pane)) == 5;
        Buffer_set_clip(c, &r);
        s[0] = 0;
        if ((ObjectiveReqVictory < 9000 || ObjectivesSuddenDeathVictory < 9000) && !ObjectiveAchievePts) {
            cat(s, label_get(899));
            for (char* q = s; *q; q++) *q = (char)crt_toupper((uint8_t)*q);
        } else {
            cat(s, label_get(0x1B8));
        }
        set_line(pane, 0, s);
        int32_t h = (r.bottom - r.top) - lh - 4;
        rankings(0);
        int32_t top = 1;
        for (int32_t f = 1; f < 8; f++) top = std::max(top, FactionRankingsUnk[f]);
        if (!bars) {
            // The power graph: each faction's score per turn (FactionTurnMight), a turn per pixel or
            // fewer, in its colour (the Spartans' frame colour).
            int32_t step = 1;
            for (int32_t t = CurrentTurn; t > r.right - r.left; t = CurrentTurn / step) step++;
            int32_t scale = 1;
            for (int32_t v = h; v < top; v += h) scale++;
            int32_t x = r.left;
            for (int32_t f = 1; f < 8; f++) {
                int32_t px = r.left, py = r.bottom - 1;
                x = r.left;
                for (int32_t t = 1; t <= CurrentTurn; t += step) {
                    int32_t y = (r.bottom - FactionTurnMight[t][f] / scale) - 1;
                    if (t > 1) {
                        int32_t col = crt_strcmp(MFactions[f].search_key, "SPARTANS") ? FactionColorFill[f]
                                                                                      : FactionColorFrame[f];
                        Buffer_line_2(c, px, py, x, y, col);
                    }
                    px = x;
                    py = y;
                    x++;
                }
            }
            // The ranking names (right-aligned by draw_text), cut until they fit beside the graph.
            for (int32_t k = 7, i = 1; k >= 1; k--, i++) {
                int32_t f = FactionRankings[k];
                s[0] = 0;
                GenderDefault = MFactions[f].noun_gender;
                PluralDefault = MFactions[f].is_noun_plural;
                cat(s, MFactions[f].noun_faction);
                Buffer_set_font(c, font(), nullptr, nullptr, nullptr);
                while (crt_strlen(s) && (r.right - r.left) - Buffer_text_width(c, s) - 2 < x) s[crt_strlen(s) - 1] = 0;
                set_line(pane, i, s);
            }
            n = 8;
        } else {
            // The score bars of the seven factions, the faction's initial in each.
            int32_t base = r.bottom - 2;
            int32_t w = (r.right - r.left) / 7;
            UiRect b;
            b.left = ((r.right - w * 7) - r.left) / 2 + r.left;
            b.right = b.left + w;
            for (int32_t i = 0; i < 7; i++) {
                int32_t f = i + 1;
                int32_t bh = FactionRankingsUnk[f] * h / top;
                if (bh < 1) bh = 1;
                b.top = base - bh;
                b.bottom = base;
                Buffer_box_2(c, &b, FactionColorFrame[f], FactionColorFrame[f]);
                UiRect in{b.left + 1, b.top + 1, b.right - 1, b.bottom - 1};
                Buffer_fill_3(c, &in, FactionColorFill[f]);
                GenderDefault = MFactions[f].noun_gender;
                PluralDefault = MFactions[f].is_noun_plural;
                char ch[2] = {MFactions[f].noun_faction[0], 0};
                Buffer_set_font(c, font(), nullptr, nullptr, nullptr);
                Buffer_set_text_color(c, FactionColorText[f], FactionColorShadow[f], 1, 1);
                Buffer_write_cent_l_7(c, ch, &b, (int32_t)crt_strlen(ch));
                b.left += w;
                b.right += w;
            }
            n = 1;
        }
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        break;
    }
    case 6: {
        auto rec = [&](int32_t i, int32_t lbl, int32_t f) {
            s[0] = 0;
            cat(s, label_get(lbl));
            cat(s, ": {");
            cat_leader(s, f);
            cat(s, "}");
            set_line(pane, i, s);
        };
        rec(0, 0x107, best_faction([](int32_t f) { return Factions[f].tech_ranking; }));
        rec(1, 0x108, best_faction([](int32_t f) { return Factions[f].mil_strength_1; }));
        rec(2, 0x109, best_faction([](int32_t f) { return Factions[f].pop_total; }));
        rec(3, 0x13C, best_faction([](int32_t f) { return Factions[f].energy_credits; }));
        rec(4, 0x25F, best_faction([](int32_t f) {
                int32_t sum = 0;
                for (int32_t k = 1; k < 0x40; k++)
                    if ((k & 0x3F) != 0x3F && (k & 0x3F) != 0) sum += (uint16_t)Factions[f].region_territory_tiles[k];
                return sum;
            }));
        n = 5;
        if (RankingFactionIDUnk1 > 0) {
            rec(5, 0x10A, FactionRankings[7]);
            n = 6;
        }
        break;
    }
    case 7: {
        set_line(pane, 0, label_get(0x1AB));
        s[0] = 0;
        cat(s, label_get(0x1AA));
        cat(s, ": {");
        cat(s, MapFilePath);
        cat(s, "}");
        set_line(pane, 1, s);
        s[0] = 0;
        cat(s, label_get(0x1A9));
        cat(s, ": ");
        const char* brush;
        switch (EditorPaintType) {
        case 0: brush = label_get(0x3AF); break;
        case 1: brush = label_get(0x3B0); break;
        case 2: brush = label_get(0x3B1); break;
        case 3: brush = label_get(0x3B2); break;
        case 4: brush = label_get(0x3B3); break;
        case 5: brush = label_get(0x3B4); break;
        case 0x1A: brush = Strings_get(&StringTable, *reinterpret_cast<char**>(mem::host(0x94ADF4))); break;
        case 0x1B: brush = Strings_get(&StringTable, *reinterpret_cast<char**>(mem::host(0x94AE14))); break;
        case 0x1C: brush = Strings_get(&StringTable, *reinterpret_cast<char**>(mem::host(0x94ADFC))); break;
        case 0x1D: brush = Strings_get(&StringTable, *reinterpret_cast<char**>(mem::host(0x94AE34))); break;
        default: brush = Strings_get(&StringTable, Terraform[EditorPaintType - 6].name); break;
        }
        cat(s, brush);
        set_line(pane, 2, s);
        s[0] = 0;
        cat(s, label_get(0x1A1));
        cat(s, ": ");
        if (EditorBrushSize == 1) cat(s, label_get(0x1A2));
        else cat_num(s, EditorBrushSize ? EditorBrushSize - 1 : 0);
        set_line(pane, 3, s);
        s[0] = 0;
        cat(s, label_get(0x35F));
        cat(s, ": ");
        cat_num(s, MapAreaX / 2);
        cat(s, "x");
        cat_num(s, MapAreaY);
        cat(s, " ");
        cat(s, label_get((MapToggleFlat & 1) ? 0x360 : 0x361));
        set_line(pane, 4, s);
        s[0] = 0;
        cat(s, label_get(0x21));
        cat(s, ": {");
        cat(s, label_get(MapCloudCover + 0x14));
        cat(s, "}");
        set_line(pane, 5, s);
        n = 6;
        break;
    }
    default: break;
    }
    nlines(pane) = n;
    nchars(pane) = 0;
    for (int32_t i = 0; i < n; i++) nchars(pane) += (int32_t)crt_strlen(line(pane, i));
}

// InfoWin_draw_text (0x458420): the lines of `pane` (-1: both) typed up to the progress.
void THISCALL InfoWin_draw_text(uint8_t*, int32_t pane) {
    Buffer* c = canvas();
    if (!c->data) return;
    Buffer_set_font(c, font(), nullptr, nullptr, nullptr);
    int32_t first = pane == -1 ? 0 : pane, end = pane == -1 ? 2 : pane + 1;
    COMPAT_ORIGINAL(infowin_counter_per_pane);
    int32_t i = 0;  // BUG(original): not reset per pane (draw_text(-1)).
    for (int32_t p = first; p < end; p++) {
        UiRect pr = prect(p + 1);
        Buffer_set_clip(c, &pr);
        UiRect r{pr.left + 3, pr.top, pr.right - 3, pr.bottom};
        int32_t left = prog(p);
        int32_t x = 0;
        for (; i < nlines(p); i++) {
            if (left < 0) break;
            const char* text = line(p, i);
            int32_t len = (int32_t)crt_strlen(text);
            bool right = state(p) == 4 && i > 0;
            if (len) {
                if (right) {
                    // (&RankingFactionIDUnk1)[-i]: FactionRankings[8 - i].
                    int32_t f = *reinterpret_cast<int32_t*>(mem::host(0x9A650C - (uint32_t)i * 4));
                    Buffer_set_text_color(c, FactionColorFill[f], FactionColorFrame[f], 1, 1);
                } else if (lcol(p, i)) {
                    Buffer_set_text_color(c, fld(0x94) ? lcol2(p, i) : lcol(p, i), -1, 1, 1);
                } else if (fld(0x94)) {
                    Buffer_set_text_color(c, mem_i(0x8C6BBC), mem_i(0x8C6BC0), 1, 1);
                    Buffer_set_text_color2(c, mem_i(0x8C6BCC), mem_i(0x8C6BD0), 1, 1);
                } else {
                    Buffer_set_text_color(c, mem_i(0x8C6BB4), mem_i(0x8C6BB8), 1, 1);
                    Buffer_set_text_color2(c, mem_i(0x8C6BC4), mem_i(0x8C6BC8), 1, 1);
                }
                if (left > len) {
                    if (right) {
                        // Buffer_write_right_l_4 at x 2 (pane relative; outside the clip).
                        int32_t w = Buffer_text_width_2(c, text, len);
                        Buffer_write_multi_font_raw_l(c, text, 2 + ((r.right - r.left) - 4 - w), ly(p, i), len);
                    } else {
                        Buffer_write_l_3(c, text, lx(p, i), ly(p, i), len);
                    }
                } else {
                    char buf[0x50 + 1];
                    if (left != 0) {
                        crt_strcpy(buf, text);
                        buf[left - 1] = 0;
                        int32_t sx = right ? (r.right - r.left) - Buffer_text_width(c, text) - 2 : lx(p, i);
                        x = Buffer_write_l_3(c, buf, sx, ly(p, i), (int32_t)crt_strlen(buf));
                        // The last character typed, in the second colour.
                        crt_strcpy(buf, text);
                        buf[left] = 0;
                        if (!right) {
                            if (lcol(p, i)) {
                                Buffer_set_text_color(c, lcol2(p, i), -1, 1, 1);
                            } else {
                                Buffer_set_text_color(c, mem_i(0x8C6BBC), mem_i(0x8C6BB8), 1, 1);
                                Buffer_set_text_color2(c, mem_i(0x8C6BCC), mem_i(0x8C6BD0), 1, 1);
                            }
                        }
                        const char* last = buf + left - 1;
                        Buffer_write_l_3(c, last, x, ly(p, i), (int32_t)crt_strlen(last));
                    }
                }
            }
            left -= len;
        }
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    save_pixels();  // GraphicWin_update_2 (MainInterface, the pane or both)
}

// InfoWin_UNK2 (0x4588B0): the current pane again from the start.
void THISCALL InfoWin_redraw(uint8_t* self) {
    Buffer_box_sprite(canvas(), &prect(cur() + 1), box_def(18));
    prog(0) = 0;
    prog(1) = 0;
    InfoWin_setup_text(self);
    InfoWin_draw_text(self, cur());
}

// InfoWin_set_state (0x458F10): the current pane shows `state` (the next valid one); its typing
// starts again (a2 decides an instant text, which the inlined redraw then resets).
void THISCALL InfoWin_set_state(uint8_t* self, int32_t s, int32_t a2) {
    if (!g_inited) return;
    int32_t p = cur();
    if (s < 0) s = 0;
    state(p) = s;
    while (!InfoWin_state_valid(state(p))) state(p) = mod8(state(p) + 1);
    fld(0x94) = 0;
    prog(0) = a2 ? 0 : nchars(0) + 1;
    prog(1) = a2 ? 0 : nchars(1) + 1;
    fld(0xA8) = 0;
    InfoWin_redraw(self);
}

// InfoWin_timer_change (0x458FF0): the other pane (the only one at 800) shows the display after
// the one of the previous pane (after InfoWindowTechReset's when set).
void THISCALL InfoWin_timer_change(uint8_t* self, int32_t) {
    if (InfcList[InfcUsed] != 0) return;
    int32_t old = cur();
    fld(0x94) = 0;
    bool first = true;
    cur() = ScreenWidth == 800 ? 0 : (old - 1) & 1;
    do {
        if (fld(0x90) < 0) state(cur()) = mod8(state(first ? old : cur()) + 1);
        else state(cur()) = mod8(fld(0x90) + 1);
        fld(0x90) = -1;
        first = false;
    } while (!InfoWin_state_valid(state(cur())));
    prog(0) = prog(1) = 0;
    fld(0xA8) = 0;
    InfoWin_redraw(self);
}

// InfoWin_timer_proc (0x459150): one more character; 50 ticks after the end the next display,
// unless the mouse is over the pane (then the text blinks: the second colours every 10 ticks).
void THISCALL InfoWin_timer_proc(uint8_t* self, int32_t a1) {
    if (fld(0xAC) || InfcList[InfcUsed] != 0) return;
    if (mi_mode() == 0x3EA) return;  // pfncWinIsVisible(the editor's first button)
    int32_t p = cur();
    prog(p)++;
    if (prog(p) > nchars(p) + 0x32) {
        int32_t mx = -1, my = -1;
        ui_mouse_pos(&mx, &my);
        const UiRect& pos = mi_rect(kMiPos);
        mx -= pos.left;
        my -= pos.top;
        const UiRect& u = prect(0);
        bool over = mx >= u.left && mx < u.right && my >= u.top && my < u.bottom;
        if (!over && !fld(0xA8) && !fld(0x94)) {
            InfoWin_timer_change(self, a1);
            return;
        }
        prog(p) = nchars(p) + 0x28;
        fld(0x94) = !fld(0x94);
        InfoWin_draw_text(self, -1);
        return;
    }
    if (prog(p) <= nchars(p) + 1) InfoWin_draw_text(self, p);
}

// InfoWin_timer_daemon (0x459260).
void GAMEAPI InfoWin_timer_daemon(int32_t a1) {
    if (!GameNotStarted) InfoWin_timer_proc(&InfoWindow, a1);
}

// InfoWin_reset (0x459280): after a hold (+0xA8), the timer's step at once.
void THISCALL InfoWin_reset(uint8_t* self) {
    if (!g_inited) return;
    if (fld(0xA8)) {
        fld(0xA8) = 0;
        InfoWin_timer_proc(self, 1);
    }
}

// InfoWin_change (0x458900): nothing in the original (a 3-byte stub).
void THISCALL InfoWin_change(uint8_t*, int32_t) { ui_invalidate(); }

// InfoWin_on_left_click (0x458930): the typing finished at once; when finished, the next display.
void THISCALL InfoWin_on_left_click(uint8_t* self, int32_t, int32_t) {
    BaseWinMultiplayerRefresh = 0;
    fld(0xA8) = 0;
    StatusWin_reset(&InfoWin);
    for (int32_t p = 0; p < 2; p++) {
        if (prog(p) < nchars(p) + 1) {
            prog(p) = nchars(p) + 1;
            InfoWin_draw_text(self, p);
            fld(0x90) = 0;
            return;
        }
    }
    InfoWin_timer_change(self, 1);
    prog(0) = nchars(0) + 1;
    prog(1) = nchars(1) + 1;
    InfoWin_draw_text(self, -1);
}

// InfoWin_right_menu (0x4589C0): the valid displays and Help.
void THISCALL InfoWin_right_menu(uint8_t* self, int32_t x, int32_t y) {
    PullDown pd;
    const UiRect& pos = mi_rect(kMiPos);
    x += pos.left;  // Win_client_to_screen_2
    y += pos.top;
    PullDown_init(&pd);
    static const uint32_t kLabel[8] = {0x92C, 0x930, 0x934, 0x938, 0x948, 0x94C, 0x950, 0x954};
    for (int32_t s = 0; s < 8; s++)
        if (InfoWin_state_valid(s)) PullDown_add_item(&pd, s, label_get((int32_t)(kLabel[s] / 4)));
    PullDown_add_separator(&pd, 99);
    PullDown_add_item(&pd, 0x14, label_get(0x8FC / 4));
    PullDown_show(&pd, x, y);
    if (GameState & 0x800) return;
    int32_t sel = PullDown_get_selected(&pd);
    if (sel >= 0x40) return;
    int32_t id = pulldown_item_id(&pd, sel);
    if (id >= 0 && id < 0x11 && InfoWin_state_valid(id)) {
        int32_t p = cur();
        state(p) = id;
        while (!InfoWin_state_valid(state(p))) state(p) = mod8(state(p) + 1);
        fld(0x94) = 0;
        fld(0xA8) = 0;
        InfoWin_redraw(self);
        return;
    }
    if (id == 0x14) help_any();
}

void THISCALL InfoWin_on_right_click(uint8_t* self, int32_t x, int32_t y) { InfoWin_right_menu(self, x, y); }

// ==== MessageWin ================================================================================

// MessageWin_add_message (0x471530): a message at the end of the list (not again when it repeats
// the last one, for id 0); at most 100.
void THISCALL MessageWin_add_message(uint8_t*, const char* text, uint32_t id) {
    if (MessageCount && !g_msgs.empty() && id == 0 && g_msgs.back().text == text) return;
    MessageCount++;
    g_msgs.push_back({text, id | 0x10000000});
    while (MessageCount >= 0x65) {
        MessageCount--;
        g_msgs.pop_front();
    }
    msg_scroll_end();
}

// MessageWin_add_base (0x4713B0): a base's message (the list shows; hotseat / PBEM: the owner's
// list when another player's).
void THISCALL MessageWin_add_base(uint8_t*, uint32_t base_id, const char* text) {
    if (PbemActive && Bases[base_id].faction_id != CurrentPlayerFaction) return;  // StringStruct_add: not reconstructed
    MessageCount++;
    g_msgs.push_back({text, base_id | 0x40000000});
    msg_scroll_end();
    mi_set_mode(1000);
}

// MessageWin_clear (0x4717A0): the list emptied, the info display back.
void THISCALL MessageWin_clear(uint8_t*) {
    MessageCount = 0;
    g_msgs.clear();
    g_msg_first = 0;
    mi_set_mode(0x3E9);
}

// MessageWin_on_selected (0x4718C0): the menu of a message: go there (the base's window, or the
// tile when seen), delete it, delete it and go there.
void THISCALL MessageWin_on_selected(uint8_t*, uint32_t id) {
    static bool busy = false;  // DAT_007F9F54
    if (busy) return;
    PullDown pd;
    int32_t mx = 0, my = 0;
    ui_mouse_pos(&mx, &my);
    PullDown_init(&pd);
    PullDown_add_item(&pd, 1, label_get(0x1130 / 4));
    PullDown_add_item(&pd, 2, label_get(0x1134 / 4));
    PullDown_add_item(&pd, 3, label_get(0x1138 / 4));
    PullDown_show(&pd, mx, my);
    int32_t sel = PullDown_get_selected(&pd);
    uint32_t choice = sel < 0x40 ? (uint32_t)pulldown_item_id(&pd, sel) : 0xFFFFFFFFu;
    bool del = (choice & 2) != 0;
    if (choice & 1) {
        busy = true;
        if (!(id & 0x40000000)) {
            if (!(id & 0x20000000)) {
                uint32_t v = id & 0x7FFFFFFF;
                int32_t y = (int32_t)(v / (uint32_t)MapAreaX), x = (int32_t)(v % (uint32_t)MapAreaX);
                if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX &&
                    ((Factions[CurrentPlayerFaction].player_flags & 0x200) ||
                     (MapTiles[MapHalfX * y + (x >> 1)].visibility & (1 << CurrentPlayerFaction)))) {
                    pfncMoveMap(&MainConsole, x, y, 1);
                    if (!MapWinInVehMode) Console_set_cursor(&MainConsole, x, y);
                }
            }
        } else {
            int32_t b = (int32_t)(id & 0xBFFFFFFF);
            if (b >= 0 && Bases[b].faction_id == CurrentPlayerFaction) {
                pfncMoveMap(&MainConsole, Bases[b].x, Bases[b].y, 1);
                if (!MapWinInVehMode) Console_set_cursor(&MainConsole, Bases[b].x, Bases[b].y);
                BaseWin_zoom(&BaseWin, b, 0);
            }
        }
    }
    if (del) {
        MessageCount--;
        for (size_t i = 0; i < g_msgs.size(); i++)
            if (g_msgs[i].id == id) {
                g_msgs.erase(g_msgs.begin() + (long)i);
                break;
            }
        msg_scroll_end();
        if (g_msgs.empty()) mi_set_mode(0x3E9);
    }
    busy = false;
}

// MainInterface_set_message (0x45C0F0): the message line (StringBox_add, 5 s) and the list.
void THISCALL MainInterface_set_message(uint8_t*, const char* text) {
    ui_set_message(text);
    MessageWin_add_message(&MessageWin, text, 0);
}

// ==== the main interface's mode (ButtonGroup 0) =================================================

void mi_set_mode(int32_t mode) {
    int32_t& m = *reinterpret_cast<int32_t*>(&MainInterfaceWin + kMiMode);
    if (m == mode) return;
    m = mode;
    for (int32_t i = 0; i < 3; i++) bw_button_set(&g_btn[i], g_btn[i].id == mode);
    if (!g_inited) return;
    // MainInterface_on_redraw of the mode: the info pane's boxes and InfoWin_reset (0x3E9), the
    // message list (1000).
    if (mode == 0x3E9) {
        Buffer* c = canvas();
        Buffer_box_sprite(c, &prect(1), box_def(18));
        Buffer_box_sprite(c, &prect(2), box_def(18));
        save_pixels();
        InfoWin_reset(self_());
    }
    ui_invalidate();
}

void infowin_setup() {
    if (g_inited) return;
    Buffer* c = canvas();
    if (!c->data) return;
    g_inited = true;
    *reinterpret_cast<int32_t*>(&MainInterfaceWin + kMiMode) = 0x3E9;
    Font_ctor(font());  // InfoWin_ctor
    fld(0x80) = 0;  // (the native Font_ctor clears the whole Font; the original leaves +0x28 = +0x80)
    InfoWin_init(self_());
    // MessageWin_init: its font.
    Font_ctor(&g_msg_font);
    g_msg_font_ok = Font_init(&g_msg_font, "Arial", 12, 0) == 0 || g_msg_font.handle;
    // The mode buttons (1000 "M", 0x3E9 "I"; 0x3EA hidden), the initial of their labels.
    Font_ctor(&g_btn_font);
    Font_init(&g_btn_font, "Arial", 14, 1);  // MainInterface +0x2166C
    static const int32_t kId[3] = {1000, 0x3E9, 0x3EA};
    static const uint32_t kRect[3] = {0xA2C, 0xA3C, 0xA4C};
    static const int32_t kLabel[3] = {0xAC0 / 4, 0xABC / 4, 0xDBC / 4};
    for (int32_t i = 0; i < 3; i++) {
        BaseButton& b = g_btn[i];
        bw_button_ctor(&b);
        char name[2] = {label_get(kLabel[i])[0], 0};
        const UiRect& r = mi_rect(kRect[i]);
        bw_button_init(&b, name, kId[i], r.left, r.top, r.right - r.left, r.bottom - r.top, 1);
        b.font = &g_btn_font;
        b.shown = i < 2 || g_editor_button;
        bw_button_set(&b, kId[i] == 0x3E9);
    }
    // MainInterface_on_redraw (mode 0x3E9): the empty panes, the timer started.
    Buffer_box_sprite(c, &prect(1), box_def(18));
    Buffer_box_sprite(c, &prect(2), box_def(18));
    save_pixels();
}

void infowin_editor_button(bool show) {
    g_editor_button = show;
    g_btn[2].shown = show;
    ui_invalidate();
}

void infowin_compose(Buffer* mi) {
    infowin_setup();
    if (!g_inited) return;
    if (mi_mode() == 1000) msg_draw(mi);
    else if (mi_mode() == 0x3E9) restore_pixels(mi);
    else if (mi_mode() == 0x3EA) editpane_compose(mi);
    for (BaseButton& b : g_btn) {
        if (!b.shown) continue;
        bw_button_draw(&b);
        for (int32_t y = 0; y < b.canvas.height; y++) {
            int32_t dy = b.r.top + y;
            if (dy < 0 || dy >= mi->height) continue;
            const uint8_t* s = b.canvas.data + (size_t)y * b.canvas.stride;
            uint8_t* d = mi->data + (size_t)dy * mi->stride;
            for (int32_t x = 0; x < b.canvas.width; x++) {
                int32_t dx = b.r.left + x;
                if (dx >= 0 && dx < mi->width && s[x] != 9) d[dx] = s[x];
            }
        }
    }
}

bool infowin_mouse(int32_t type, int32_t x, int32_t y) {
    if (!g_inited) return false;
    for (BaseButton& b : g_btn) {
        if (!b.shown || x < b.r.left || x >= b.r.right || y < b.r.top || y >= b.r.bottom) continue;
        if (type == UiInput::LDOWN) b.pressed = 1;
        if (type == UiInput::LUP) {
            b.pressed = 0;
            // MainInterface_on_iface_group_clicked (group 0): the mode's redraw.
            if (InfcList[InfcUsed] == 0) mi_set_mode(b.id);
        }
        ui_invalidate();
        return true;
    }
    for (BaseButton& b : g_btn) b.pressed = 0;
    if (editpane_mouse(type, x, y)) return true;
    if (mi_mode() == 1000) {
        UiRect r = msg_rect();
        if (x < r.left || x >= r.right || y < r.top || y >= r.bottom) return false;
        if (type == UiInput::LUP && g_msg_font_ok) {
            // ListBox: the row under the mouse selected (MainInterface_on_iface_selected).
            int32_t row = (y - r.top - kMsgMargin) / msg_pitch() + g_msg_first;
            if (row >= 0 && row < (int32_t)g_msgs.size()) MessageWin_on_selected(&MessageWin, g_msgs[(size_t)row].id);
        }
        return true;
    }
    const UiRect& u = prect(0);
    if (mi_mode() != 0x3E9 || x < u.left || x >= u.right || y < u.top || y >= u.bottom) return false;
    if (type == UiInput::LUP) InfoWin_on_left_click(self_(), x, y);
    else if (type == UiInput::RUP) InfoWin_on_right_click(self_(), x, y);
    return true;
}

void infowin_tick() {
    if (!g_inited || Win_is_visible(&BaseWin) || mi_mode() != 0x3E9) return;
    InfoWin_timer_daemon(0);
}

void infowin_log_state() {
    std::fprintf(stderr, "infostate mode %d pane %d", mi_mode(), g_inited ? cur() : -1);
    if (!g_inited) {
        std::fprintf(stderr, "\n");
        return;
    }
    for (int32_t p = 0; p < 2; p++) std::fprintf(stderr, " state%d %d progress %d/%d", p, state(p), prog(p), nchars(p));
    const int32_t* st = reinterpret_cast<const int32_t*>(&InfoWin);
    std::fprintf(stderr, " messages %d status numbers %d veh %d base %d hover %d/%d\n", (int32_t)g_msgs.size(),
                 st[0x15A8 / 4], st[0x15BC / 4], st[0x15C0 / 4], st[0x15C4 / 4], st[0x15C8 / 4]);
    for (int32_t p = 0; p < 2; p++)
        for (int32_t i = 0; i < nlines(p); i++) std::fprintf(stderr, "info %d.%d: %s\n", p, i, line(p, i));
    for (const Message& m : g_msgs) std::fprintf(stderr, "message %08X: %s\n", m.id, m.text.c_str());
}
