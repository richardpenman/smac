// The monuments window (MonuWin 0x7FA... ; ui module 0x472230..0x477E00, monument 0x476A50) and
// the hall of fame (FameWin; hall_of_fame 0x44AE50, ui 0x448400..0x44B100).
//
// monument(is_new): shown when the viewer (not an alien faction, single player) has at least one
// of the fifteen monuments; a new monument (is_new) only without the "no monuments" preference
// (GameMorePreferences 0x1000000), outside scenarios being edited and during the viewer's turn.
// The window (1024 x 768; the original's 800 x 600 layout centred) shows the monuments in the
// order of their years at the original's slot rectangles (monicons.pcx icons), the latest one
// large in the centre, and the leaders the faction eliminated; a click on one shows its text
// (monument.txt: FOUNDCOLONY1ST.., the parse slots as MonuWin_on_redraw sets them) and its picture
// (monbigN.pcx, the faction's "3" picture for unification and eliminations) and records it as
// the faction's latest monument (Monuments[f].latest_event_id, game state, as the original). A key
// or the button closes the text, then the window.
// Approximated: the background (the original plays the frames of monument.flc up to the number of
// monuments: FLC decoding is not reconstructed), the decorations and the eliminated leaders'
// portraits (written as text).
//
// hall_of_fame(add): reads hallfame.jhh (7 factions x 5 entries of 0x8C bytes, 0x9A3840),
// shows the viewer's faction's top five, and writes the table back. smacx reads and writes the
// file in --outdir (the original: the game directory), so tests never change the game data.
// Adding the finished game's entry (add != 0, end of game: FUN_0044A660) is not reconstructed.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
#include "ui/win.h"

GLOBAL(int32_t, MonumentCount, 0x7FD634);       // dword_7FD634: monuments of the viewer
GLOBAL(int32_t, MonumentIsNew, 0x7FA9A4);       // DAT_007FA9A4
GLOBAL(uint8_t[0x1324], HallOfFame, 0x9A3840);  // 7 x 5 entries of 0x8C bytes

namespace {

constexpr int32_t kClose = 999;

// Monuments[f] fields (0x4F4 bytes): events of 0x38 bytes (year, param_a, param_b, the faction
// that was first, achieved, first), latest event (+0x498), names (+0x4AC base, +0x4C4 facility,
// +0x4DC enemy base).
int32_t& ev(int32_t f, int32_t idx, int32_t field) {
    return reinterpret_cast<int32_t*>(&Monuments[f])[idx * 14 + field];
}
int32_t& latest(int32_t f) { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Monuments[f]) + 0x498); }
const char* mon_str(int32_t f, uint32_t off) { return reinterpret_cast<const char*>(&Monuments[f]) + off; }

struct MonuWinState : ScreenWin {
    int32_t faction = 0;     // +0xA50
    int32_t zoom = 0;        // +0xA4C (0x293 * 4): the text of a monument is shown
    int32_t selected = 0;    // +0xA54
    Sprite small[15], big[15];
    Font *f_text = nullptr, *f_bold = nullptr;
    BaseButton *close = nullptr, *back = nullptr;
    std::string text;        // the monument's text (test aid)

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
    void set_text(int32_t idx);
};

MonuWinState g_mw;
constexpr int32_t kOx = 0x70, kOy = 0x54;  // the 800 x 600 layout centred in 1024 x 768

// The slot rectangles of the monuments in the order of their years (MonuWin_init, +0x3164).
const UiRect kSlots[15] = {{0x12D, 0x1A9, 0x159, 0x1D3}, {0x1C7, 0x1A9, 499, 0x1D3},   {0x12D, 0x175, 0x159, 0x19F},
                           {0x1C7, 0x175, 499, 0x19F},   {0xA3, 0x1A9, 0xCF, 0x1D3},   {0x24E, 0x1A9, 0x27A, 0x1D3},
                           {0xB7, 0x175, 0xE3, 0x19F},   {0x23A, 0x175, 0x266, 0x19F}, {0, 0x1A9, 0x2C, 0x1D3},
                           {0x2EE, 0x1A9, 0x31A, 0x1D3}, {0x25, 0x175, 0x51, 0x19F},   {0x2CC, 0x175, 0x2F8, 0x19F},
                           {0x45, 0x141, 0x71, 0x16B},   {0x2AA, 0x141, 0x2D6, 0x16B}, {0, 0, 0, 0}};
// The eliminated factions' slots (+0x366C).
const UiRect kKills[6] = {{0xFE, 0x1FF, 0x138, 0x231},  {0x1E0, 0x1FF, 0x21A, 0x231}, {0xC0, 0x1FF, 0xFA, 0x231},
                          {0x21E, 0x1FF, 600, 0x231},   {0x81, 0x1FF, 0xBB, 0x231},   {0x25D, 0x1FF, 0x297, 0x231}};
const UiRect kCentre{0x13F, 0xCA, 0x1DF, 299};
const UiRect kTextRect{0x90, 0x2F, 0x293, 0x5B};

UiRect off(UiRect r) { return {r.left + kOx, r.top + kOy, r.right + kOx, r.bottom + kOy}; }

// MonuWin_on_redraw's parse slots and text section per monument (first / not first).
void says(int32_t n, const char* s, int32_t gender, int32_t plural) { parse_says(n, s, gender, plural); }
void title(int32_t n, int32_t f, int32_t g) {
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    says(n, MFactions[f].title_leader, g, -1);
}
void name(int32_t n, int32_t f, int32_t g) {
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    says(n, MFactions[f].name_leader, g, -1);
}
void adj(int32_t n, int32_t f) { says(n, MFactions[f].adj_name_faction, -1, -1); }
void noun(int32_t n, int32_t f, int32_t viewer) {
    PluralDefault = MFactions[f].is_noun_plural;
    GenderDefault = MFactions[f].noun_gender;
    says(n, MFactions[f].noun_faction, MFactions[viewer].noun_gender, MFactions[viewer].is_noun_plural);
}

const char* monument_section(int32_t F, int32_t idx) {
    int32_t G = MFactions[F].is_leader_female;
    int32_t first = ev(F, idx, 5) != 0;  // +0x14
    int32_t other = ev(F, idx, 3);       // +0x0C: the faction that was first
    int32_t pb = ev(F, idx, 2), pa = ev(F, idx, 1);
    parse_num(0, ev(F, idx, 0));
    auto other_year = [&] { parse_num(1, ev(other, idx, 0)); };
    switch (idx) {
    case 0:
        title(0, F, G);
        name(1, F, G);
        says(2, mon_str(F, 0x4AC), -1, -1);
        if (first) return "FOUNDCOLONY1ST";
        adj(3, F);
        title(4, other, G);
        name(5, other, G);
        noun(6, other, F);
        other_year();
        return "FOUNDCOLONY2ND";
    case 1:
        adj(0, F);
        says(1, tech_name_2(pb, 0), -1, -1);
        if (first) return "TECHDISCOVER1ST";
        noun(2, other, F);
        other_year();
        return "TECHDISCOVER2ND";
    case 2:
        adj(0, F);
        if (!first) {
            noun(1, other, F);
            other_year();
            if (pb == -1) return "SECRETS2NDA";
            adj(2, F);
            parse_num(2, pb);
            if (pa == -1) return "SECRETS2NDB";
            parse_num(3, pa);
            return "SECRETS2NDC";
        }
        if (pb == -1) return "SECRETS1STA";
        parse_num(1, pb);
        if (pa == -1) return "SECRETS1STB";
        parse_num(2, pa);
        return "SECRETS1STC";
    case 3:
        if (!first) {
            adj(0, F);
            title(1, other, G);
            name(2, other, G);
            noun(3, other, F);
            other_year();
            return "PROTO2ND";
        }
        title(0, F, G);
        name(1, F, G);
        adj(2, F);
        return "PROTO1ST";
    case 4:
        adj(0, F);
        if (!first) {
            title(1, other, G);
            name(2, other, G);
            adj(3, other);
            other_year();
            return "FACILITY2ND";
        }
        title(1, F, G);
        name(2, F, G);
        says(3, mon_str(F, 0x4C4), -1, -1);
        return "FACILITY1ST";
    case 5:
        if (!first) {
            adj(0, F);
            says(1, Facility[pb].name, -1, -1);
            title(2, other, G);
            name(3, other, G);
            noun(4, other, F);
            other_year();
            return "PROJECT2ND";
        }
        title(0, F, G);
        name(1, F, G);
        says(2, Facility[pb].name, -1, -1);
        return "PROJECT1ST";
    case 6:
        title(0, F, G);
        name(1, F, G);
        if (!first) {
            adj(2, F);
            title(3, other, G);
            name(4, other, G);
            noun(5, other, F);
            other_year();
            return "BATTLEWIN2ND";
        }
        title(2, pb, G);
        name(3, pb, G);
        noun(4, pb, F);
        return "BATTLEWIN1ST";
    case 7:
        title(0, F, G);
        name(1, F, G);
        if (!first) {
            says(2, mon_str(F, 0x4DC), -1, -1);
            adj(3, F);
            title(4, other, G);
            name(5, other, G);
            noun(6, other, F);
            other_year();
            return "BASECAP2ND";
        }
        noun(2, F, F);
        says(3, mon_str(F, 0x4DC), -1, -1);
        return "BASECAP1ST";
    case 8:
    case 9:
    case 10:
        if (!first) {
            adj(0, F);
            title(1, other, G);
            name(2, other, G);
            noun(3, other, F);
            other_year();
            return idx == 8 ? "NAVY2ND" : (idx == 9 ? "AIR2ND" : "NATIVE2ND");
        }
        title(0, F, G);
        name(1, F, G);
        if (idx == 10) {
            adj(2, F);
            return "NATIVE1ST";
        }
        noun(2, F, F);
        return idx == 8 ? "NAVY1ST" : "AIR1ST";
    case 11:
        if (!first) {
            title(0, F, G);
            name(1, F, G);
            adj(2, F);
            noun(3, other, F);
            title(4, other, G);
            name(5, other, G);
            other_year();
            return "SPACE2ND";
        }
        noun(0, F, F);
        title(1, F, G);
        name(2, F, G);
        return "SPACE1ST";
    case 12:
        title(0, F, G);
        name(1, F, G);
        if (!first) {
            adj(2, F);
            title(3, other, G);
            name(4, other, G);
            other_year();
            return "PRESERVE2ND";
        }
        noun(2, F, F);
        return "PRESERVE1ST";
    case 13:
        if (pb == -1) {
            title(0, F, G);
            name(1, F, G);
            adj(2, F);
            return "WINUNIFY";
        }
        title(0, pb, G);
        name(1, pb, G);
        noun(2, pb, F);
        noun(3, pb, F);
        return "WINCO_OPUNIFY";
    case 14:
        if (pb == -1) {
            title(0, F, G);
            name(1, F, G);
            noun(2, F, F);
            return "WINTRANSCEND";
        }
        title(0, pb, G);
        name(1, pb, G);
        noun(2, pb, F);
        noun(3, pb, F);
        return "WINCO_OPASCENT";
    default:  // 15..20: an eliminated faction
        noun(0, F, F);
        noun(1, pb, F);
        title(2, pb, G);
        name(3, pb, G);
        return "KILLFACTION";
    }
}

// StringList_load + Buffer_write_strings: the section's lines expanded (parse_string), flowed.
void MonuWinState::set_text(int32_t idx) {
    const char* label = monument_section(faction, idx);
    text.clear();
    if (text_open("monument", label)) return;
    for (;;) {
        char* line = text_get();
        if (!line || !*line || *line == '#') break;
        char out[1024];
        parse_string(line, out);
        if (!text.empty()) text += " ";
        text += out;
    }
    text_close();
}

void MonuWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    spot_init(&spots[0], 0x100);
    // Background: the overview's (the frames of monument.flc) is a tiled panel here; the text's
    // is monzoom1.pcx (up to four monuments) or monzoom2.pcx.
    Buffer* tile = box_def(9)->center;
    Buffer_tile(c, tile, 0, 0, 0, 0, c->width, c->height);
    if (zoom) {
        Buffer bg;
        Buffer_ctor(&bg);
        if (Buffer_load_pcx(&bg, MonumentCount > 4 ? "monzoom2.pcx" : "monzoom1.pcx", nullptr, 10, 0xEC) == 0)
            Buffer_copy_4(&bg, c, 0, 0, 0, 0, std::min(bg.width, c->width), std::min(bg.height, c->height));
    }
    if (close) close->shown = !zoom;
    if (back) back->shown = zoom != 0;
    if (!zoom) {
        // The monuments in the order of their years (equal years: their index order).
        int32_t order[15];
        for (int32_t i = 0; i < 15; i++) order[i] = ev(faction, i, 4) ? i : -1;
        for (int32_t i = 0; i < 14; i++)
            for (int32_t j = i + 1; j < 15; j++)
                if (order[j] != -1 && (order[i] == -1 || ev(faction, order[j], 0) < ev(faction, order[i], 0)))
                    std::swap(order[i], order[j]);
        for (int32_t k = 0; k < 15; k++) {
            int32_t i = order[k];
            if (i < 0) continue;
            UiRect r = off(kSlots[k]);
            sprite_copy1(&small[i], c, 9, r.left, r.top, 1, 1);
            spot_add(&spots[0], i, 0, r.left, r.top, r.right - r.left, r.bottom - r.top);
            texts.push_back("monument " + std::to_string(i) + " year " + std::to_string(ev(faction, i, 0)));
        }
        int32_t cur = latest(faction);
        selected = cur;
        UiRect cr = off(kCentre);
        spot_add(&spots[0], 0x45, 0, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top);
        if (cur >= 0 && cur < 15) sprite_copy1(&big[cur], c, 9, cr.left, cr.top, 1, 1);
        // The leaders the faction eliminated (events 15..20).
        for (int32_t k = 0; k < 6; k++) {
            if (!ev(faction, 15 + k, 4)) continue;
            int32_t victim = ev(faction, 15 + k, 2);
            UiRect r = off(kKills[k]);
            Buffer_box_sprite(c, &r, box_def(1));
            spot_add(&spots[0], 15 + k, 0, r.left, r.top, r.right - r.left, r.bottom - r.top);
            sw_font(c, f_text, 0xFF);
            if (victim >= 0 && victim < 8) sw_text_cent(this, c, MFactions[victim].noun_faction, r);
        }
        return;
    }
    // A monument's text and picture.
    set_text(selected);
    UiRect tr = off(kTextRect);
    Buffer_box_sprite(c, &tr, box_def(1));
    sw_font(c, f_text, 0xFF);
    UiRect tt{tr.left + 6, tr.top + 3, tr.right - 6, tr.bottom - 3};
    Buffer_set_clip(c, &tt);
    texts.push_back(text);
    Buffer_wrap_2(c, text.c_str(), &tt);
    Buffer_set_clip(c, &all);
    Buffer pic;
    Buffer_ctor(&pic);
    std::string file;
    int32_t sx = 0, sy = 0;
    if (selected == 0xD) {
        file = std::string(MFactions[faction].filename) + "3.pcx";
        sx = 0x155;
        sy = 0xB;
    } else if (selected < 0xF) {
        file = "monbig" + std::to_string(selected) + ".pcx";
    } else {
        file = std::string(MFactions[ev(faction, selected, 2) & 7].filename) + "3.pcx";
        sx = 0x155;
        sy = 0x174;
    }
    if (Buffer_load_pcx(&pic, file.c_str(), nullptr, 10, 0xEC) == 0)
        Buffer_copy_4(&pic, c, sx, sy, 0x65 + kOx, 0x76 + kOy, 600, 0x168);
}

void MonuWinState::on_button(int32_t id) {
    if (id != kClose) return;
    if (zoom) {
        zoom = 0;
        return;
    }
    sw_close(this);
}

void MonuWinState::on_key(int32_t) { on_button(kClose); }

// MonuWin_on_left_click: the text of the monument clicked (the centre: the latest one), recorded
// as the faction's latest monument.
void MonuWinState::on_click(int32_t, int32_t index, int32_t, int32_t, int32_t, bool, bool) {
    if ((NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))) || NetSessionState[0x21] || NetInputPending) return;
    if (zoom) {
        zoom = 0;
        return;
    }
    if (index == -1) return;
    if (index == 0x45) index = selected;
    latest(CurrentPlayerFaction) = index;
    selected = index;
    zoom = 1;
}

void MonuWinState::log_state() {
    std::fprintf(stderr, "monustate f%d count %d latest %d zoom %d\n", faction, MonumentCount, latest(faction), zoom);
}

void load_icons() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, "monicons.pcx", nullptr, 10, 0xEC) != 0) return;
    for (int32_t i = 0, x = 2, y = 0x226; i < 15; i++) {
        pfncSpriteFromCanvasRectTrans(&g_mw.small[i], &art, 9, x, y, 0x2C, 0x2A, 0);
        if (i == 7) {
            x = 2;
            y += 0x2B;
        } else {
            x += 0x2D;
        }
    }
    for (int32_t i = 0, x = 1, y = 0x89; i < 15; i++) {
        pfncSpriteFromCanvasRectTrans(&g_mw.big[i], &art, 9, x, y, 0xA0, 0x61, 0);
        if ((i + 1) % 5 == 0) {
            x = 1;
            y += 0x62;
        } else {
            x += 0xA1;
        }
    }
}

// ---- hall of fame ----

struct FameWinState : ScreenWin {
    int32_t faction = 1;
    Font *f_text = nullptr, *f_bold = nullptr;
    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t) override { sw_close(this); }
    void log_state() override;
};

FameWinState g_fw;

uint8_t* fame_entry(int32_t f, int32_t slot) { return &HallOfFame[((f - 1) * 5 + slot) * 0x8C]; }

void FameWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    {
        Buffer bg;  // hof.pcx (FUN_00448400)
        Buffer_ctor(&bg);
        if (Buffer_load_pcx(&bg, "hof.pcx", nullptr, 10, 0xEC) == 0)
            Buffer_copy_4(&bg, c, 0, 0, 0, 0, std::min(bg.width, c->width), std::min(bg.height, c->height));
        else
            Buffer_box_sprite(c, &all, box_def(9));
    }
    UiRect in{0x10, 0x30, c->width - 0x10, c->height - 0x40};
    Buffer_box_sprite(c, &in, box_def(0));
    UiRect tr{in.left + 3, in.top + 3, in.right - 3, in.top + 0x1C};
    Buffer_box_sprite(c, &tr, box_def(1));
    sw_font(c, f_bold, 0x99);
    sw_text_cent(this, c, sw_upper(sw_label(0x109C)) + ": " + sw_upper(MFactions[faction].noun_faction), tr);
    int32_t y = tr.bottom + 10, lh = f_text->height + 2;
    for (int32_t slot = 0; slot < 5; slot++) {
        uint8_t* e = fame_entry(faction, slot);
        if (!(e[0] & 1)) continue;
        auto i32 = [&](int32_t o) { return *reinterpret_cast<int32_t*>(e + o); };
        std::string who = std::string(reinterpret_cast<char*>(e + 0x22)) + " " + reinterpret_cast<char*>(e + 0x3A) + " " +
                          sw_label(0x2D0) + " " + reinterpret_cast<char*>(e + 0x52);
        std::string l = std::to_string(slot + 1) + ". " + sw_label(0xB68) + " " + std::to_string(i32(4)) + "  " + who;
        sw_font(c, f_text, 0xFF);
        sw_text(this, c, l, in.left + 12, y);
        y += lh;
        std::string s = sw_label(0x1090) + ": " + std::to_string(i32(0x10)) + "   " + sw_label(0x6C0) + ": " +
                        std::to_string(i32(8)) + "   " + sw_label(0x10A0) + ": " + std::to_string(e[0x20]);
        if (e[0] & 2) s += "   " + sw_label(0x1044);
        sw_font(c, f_text, 0x99);
        sw_text(this, c, s, in.left + 30, y);
        y += lh + 6;
    }
    for (BaseButton* b : buttons)
        if (b->id >= 0x8AE && b->id < 0x8AE + 8) bw_button_set(b, b->id - 0x8AE == faction);
}

void FameWinState::on_button(int32_t id) {
    if (id == 0x7B8) sw_close(this);
    else if (id > 0x8AE && id < 0x8AE + 8) faction = id - 0x8AE;
}

void FameWinState::log_state() {
    int32_t used = 0;
    for (int32_t f = 1; f < 8; f++)
        for (int32_t s = 0; s < 5; s++) used += fame_entry(f, s)[0] & 1;
    std::fprintf(stderr, "famestate faction %d entries %d\n", faction, used);
}

std::string fame_path() { return g_ui_outdir + "/hallfame.jhh"; }

}  // namespace

void GAMEAPI monument(int32_t is_new) {
    if ((MFactions[CurrentPlayerFaction].rule_flags & 0x80) || MultiplayerActive) return;
    if (is_new && ((GameMorePreferences & 0x1000000) || (GameState & 0x10) || ((GameState & 1) && (GameState & 8))))
        return;
    MonumentCount = 0;
    for (int32_t i = 0; i < 15; i++)
        if (ev(CurrentPlayerFaction, i, 4)) MonumentCount++;
    if (!MonumentCount) return;
    // Observer games (no human player) never stop in the window, as the oracle runs them.
    if (!FactionStatus[0]) return;
    MonuWinState* s = &g_mw;
    s->name = "monuments";
    s->faction = CurrentPlayerFaction;
    s->zoom = 0;
    MonumentIsNew = is_new != 0;
    sw_init(s, (ScreenWidth - 0x400) / 2, ScreenHeight - 0x300, 0x400, 0x300, false);
    load_icons();
    s->f_text = find_font(14, 0);
    s->f_bold = find_font(14, 1);
    sw_clear_buttons(s);
    s->close = sw_button(s, sw_upper(sw_label(0xA64)).c_str(), kClose, 0x1C0, 0x2CE, 0xC0, 0x18, 0, s->f_bold);
    s->back = sw_button(s, sw_upper(sw_label(0xB60)).c_str(), kClose, 0x1C0, 0x2CE, 0xC0, 0x18, 0, s->f_bold);
    for (BaseButton* b : s->buttons) {
        b->text_color[0] = 0x99;
        b->text_color[1] = b->text_color[2] = 0xFF;
    }
    sw_run(s);
    sw_clear_buttons(s);
    s->close = s->back = nullptr;
    do_all_draws();
}

void GAMEAPI hall_of_fame(int32_t add) {
    std::memset(HallOfFame, 0, sizeof(HallOfFame));
    if (FILE* f = std::fopen(fame_path().c_str(), "rb")) {
        if (std::fread(HallOfFame, 1, sizeof(HallOfFame), f) != sizeof(HallOfFame))
            std::memset(HallOfFame, 0, sizeof(HallOfFame));
        std::fclose(f);
    }
    if (add) console_not_reconstructed_quiet("hall_of_fame: the new entry (FUN_0044A660)");
    FameWinState* s = &g_fw;
    s->name = "halloffame";
    s->faction = CurrentPlayerFaction >= 1 && CurrentPlayerFaction < 8 ? CurrentPlayerFaction : 1;
    sw_init(s, (ScreenWidth - 0x320) / 2, (ScreenHeight - 0x258) / 2, 0x320, 0x258, false);
    s->f_text = find_font(14, 0);
    s->f_bold = find_font(16, 1);
    sw_clear_buttons(s);
    for (int32_t f = 1; f < 8; f++)
        sw_button(s, MFactions[f].noun_faction, 0x8AE + f, 0x10 + (f - 1) * 0x6A, 8, 0x66, 0x1C, 0, s->f_text);
    sw_button(s, sw_upper(sw_label(0xA64)).c_str(), 0x7B8, 0x320 / 2 - 0x50, 0x258 - 0x28, 0xA0, 0x1C, 0, s->f_bold);
    for (BaseButton* b : s->buttons) {
        b->text_color[0] = b->id == 0x7B8 ? 0x99 : 0;
        b->text_color[2] = 0xFF;
    }
    sw_run(s);
    sw_clear_buttons(s);
    if (FILE* f = std::fopen(fame_path().c_str(), "wb")) {
        std::fwrite(HallOfFame, 1, sizeof(HallOfFame), f);
        std::fclose(f);
    }
}
