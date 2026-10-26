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
// The overview's background is monument.flc's frame of the number of monuments (as MonuWin_init
// leaves it); MonuWin_init's decorations: the eliminated leaders' portraits (<faction>3.pcx) in
// their slots with the pillars for their number (monicons.pcx), the viewer's emblem above the
// centre, the victory picture or an eliminated leader in the centre.
//
// hall_of_fame(add): reads hallfame.jhh (7 factions x 5 entries of 0x8C bytes, 0x9A3840),
// shows the viewer's faction's top five, and writes the table back. smacx reads and writes the
// file in --outdir (the original: the game directory), so tests never change the game data.
// The finished game's entry (add != 0, end of game: FUN_0044A660) is added (fame_add) with its world
// map picture hof_<faction>_<slot>.pcx (also in --outdir).
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
#include "engine/zoc.h"
#include "ui/flic.h"
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
    Sprite deco[6];          // +0x3244 .. +0x3320: the eliminations' pillars (left / right by count)
    Sprite portrait[8];      // +0x3380 + f * 0x2C: the leaders' small portraits (<faction>3.pcx)
    Sprite portrait_big[8];  // +0x34B4 + f * 0x2C: their large portraits (the centre)
    Sprite unify;            // +0x3614: the viewer's victory picture (the centre, monument 13)
    Sprite crest;            // +0x3640: the viewer's emblem above the centre (+0x36CC)
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
    // Background: the overview's is monument.flc advanced by one frame per monument (MonuWin_init:
    // the first frame, then MonumentCount more, 13 for 14); the text's is monzoom1.pcx (up to
    // four monuments) or monzoom2.pcx.
    const Flc* flc = flc_get("monument.flc");
    if (flc) {
        Buffer_fill_3(c, &all, 0);
        flc_draw(c, flc, (c->width - flc->width) / 2, (c->height - flc->height) / 2,
                 MonumentCount != 14 ? MonumentCount : 13);
    } else {
        Buffer_tile(c, box_def(9)->center, 0, 0, 0, 0, c->width, c->height);
    }
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
        // The centre: the latest monument's large icon; the victory's picture (13); an eliminated
        // faction's leader (15..20).
        Sprite* centre = nullptr;
        if (cur == 0xD) centre = &unify;
        else if (cur > 0xE && cur < 21) centre = &portrait_big[ev(faction, cur, 2) & 7];
        else if (cur >= 0 && cur < 15) centre = &big[cur];
        if (centre) sprite_copy1(centre, c, centre->key, cr.left, cr.top, 1, 1);
        // The leaders the faction eliminated (events 15..20): the pillars by their count (the left
        // piece of (n - 1) / 2, the right one of (n - 2) / 2), their portraits in the slots.
        static const UiRect kDecoRect[6] = {{0xFD, 500, 0x14B, 0x233}, {0x1DC, 500, 0x225, 0x233},
                                            {0xBE, 500, 0x14B, 0x233}, {0x1DC, 500, 0x262, 0x233},
                                            {0x7F, 500, 0x14B, 0x233}, {0x1DC, 500, 0x2B1, 0x233}};
        int32_t kills = 0;
        for (int32_t k = 0; k < 6; k++) kills += ev(faction, 15 + k, 4) ? 1 : 0;
        if (kills) {
            int32_t l = (kills - 1) / 2;
            UiRect r = off(kDecoRect[l * 2]);
            sprite_copy1(&deco[l * 2], c, deco[l * 2].key, r.left, r.top, 1, 1);
            if (kills > 1) {
                int32_t rr = (kills - 2) / 2;
                UiRect r2 = off(kDecoRect[rr * 2 + 1]);
                sprite_copy1(&deco[rr * 2 + 1], c, deco[rr * 2 + 1].key, r2.left, r2.top, 1, 1);
            }
            for (int32_t k = 0, slot = 0; k < 6 && slot < kills; k++) {
                if (!ev(faction, 15 + k, 4)) continue;
                int32_t victim = ev(faction, 15 + k, 2) & 7;
                UiRect r = off(kKills[slot++]);
                sprite_copy1(&portrait[victim], c, portrait[victim].key, r.left, r.top, 1, 1);
                spot_add(&spots[0], 15 + k, 0, r.left, r.top, r.right - r.left, r.bottom - r.top);
                texts.push_back("eliminated " + std::to_string(victim) + " slot " + std::to_string(slot - 1));
            }
        }
        // The viewer's emblem (+0x36CC: higher with all fourteen monuments).
        UiRect er = MonumentCount == 14 ? UiRect{0x148, 6, 0x1D6, 0x6B} : UiRect{0x16C, 0x6F, 0x1B3, 0xAF};
        er = off(er);
        sprite_copy1(&crest, c, crest.key, er.left, er.top, 1, 1);
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
    // The eliminations' pillars: left and right pieces for 1..2, 3..4, 5..6 eliminated factions.
    static const int16_t kDeco[6][4] = {{0x116, 0x41, 0x4E, 0x3F}, {0x165, 0x41, 0x49, 0x3F}, {1, 0x41, 0x8D, 0x3F},
                                        {0x8F, 0x41, 0x86, 0x3F},  {1, 1, 0xCC, 0x3F},         {0xCE, 1, 0xD5, 0x3F}};
    for (int32_t i = 0; i < 6; i++)
        pfncSpriteFromCanvasRectTrans(&g_mw.deco[i], &art, 9, kDeco[i][0], kDeco[i][1], kDeco[i][2], kDeco[i][3], 0);
}

// MonuWin_init's pictures of each faction's <file>3.pcx: the small (0xB2, 0xA, 0x3A x 0x32) and the
// large (0xA4, 0x6D, 0xA0 x 0x61) portraits; for the viewer also the victory picture (0xA4, 0xB,
// 0xA0 x 0x61) and the emblem (0xA, 0xB, 0x8E x 0x65 with all 14 monuments, else 0xA, 0x71,
// 0x47 x 0x40).
void load_portraits(int32_t viewer) {
    for (int32_t f = 1; f < 8; f++) {
        Buffer art;
        Buffer_ctor(&art);
        std::string file = std::string(MFactions[f].filename) + "3.pcx";
        if (Buffer_load_pcx(&art, file.c_str(), nullptr, 10, 0xEC) != 0) continue;
        if (f == viewer) {
            pfncSpriteFromCanvasRectTrans(&g_mw.unify, &art, 9, 0xA4, 0xB, 0xA0, 0x61, 0);
            if (MonumentCount == 14) pfncSpriteFromCanvasRectTrans(&g_mw.crest, &art, 9, 0xA, 0xB, 0x8E, 0x65, 0);
            else pfncSpriteFromCanvasRectTrans(&g_mw.crest, &art, 9, 0xA, 0x71, 0x47, 0x40, 0);
        }
        pfncSpriteFromCanvasRectTrans(&g_mw.portrait_big[f], &art, 9, 0xA4, 0x6D, 0xA0, 0x61, 0);
        pfncSpriteFromCanvasRectTrans(&g_mw.portrait[f], &art, 9, 0xB2, 0xA, 0x3A, 0x32, 0);
    }
}

// ---- hall of fame ----

struct FameWinState : ScreenWin {
    int32_t faction = 1;
    int32_t entry = -1;  // +0xA918: the entry analysed (FUN_00448400: -1; the new one after fame_add)
    Font *f_text = nullptr, *f_bold = nullptr;
    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t) override { sw_close(this); }
    void on_click(int32_t, int32_t index, int32_t type, int32_t, int32_t, bool, bool) override {
        if (type == 1 && index > 0 && index < 8) faction = index;  // ButtonGroup 0x15 of the factions
        else if (type == 0 && index >= 0 && index < 5) entry = index;  // an entry of TOP 5 SCORES
    }
    void log_state() override;
};

FameWinState g_fw;

uint8_t* fame_entry(int32_t f, int32_t slot) { return &HallOfFame[((f - 1) * 5 + slot) * 0x8C]; }

// FUN_00448400's layout: the 800 x 600 panel at (0x77, 0x50) of the 1024 x 768 window and its
// rectangles (+0xB42C ..), relative to the panel.
constexpr int32_t kFx = 0x77, kFy = 0x50;
UiRect fr(int32_t l, int32_t t, int32_t r, int32_t b) { return {l + kFx, t + kFy, r + kFx, b + kFy}; }

void FameWinState::redraw() {
    // FUN_00448D90: frame.pcx over the window, hof.pcx (the panel) at its origin; the six score
    // categories (hoficons.pcx buttons 0x8AE..: their pressed picture for the analysed entry's
    // "best of" marks), the factions' emblems (buttons 1..7 of ButtonGroup 0x15: the faction's
    // PCX at 0x2BD, 0x48 / pressed 0x2BD, 1), the analysed entry's world map picture, its
    // faction's logo, SCORE ANALYSIS (its marks, how the game was won, its score lines), the
    // banner of its first mark, the column titles and TOP 5 SCORES.
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    spot_init(&spots[0], 0x100);
    Buffer bg;
    Buffer_ctor(&bg);
    if (Buffer_load_pcx(&bg, "frame.pcx", nullptr, 10, 0xEC) == 0)
        Buffer_copy_4(&bg, c, 0, 0, 0, 0, std::min(bg.width, c->width), std::min(bg.height, c->height));
    if (Buffer_load_pcx(&bg, "hof.pcx", nullptr, 10, 0xEC) == 0)
        Buffer_copy_4(&bg, c, 0, 0, kFx, kFy, std::min(bg.width, 800), std::min(bg.height, 600));
    // ReportIf_init's fonts (ReportWin 0x885F38 + 0x1E180 ..): 0x8A40C8 (14), 0x8A40D4 (16),
    // 0x8A40EC (22).
    Font* f12b = find_font(14, 0);
    Font* f14b = find_font(16, 0);
    Font* f18i = find_font(22, 0);
    const int32_t lh = f12b->field_0 < 0 ? f12b->height : f12b->ascent_above + f12b->field_0;  // +0xA44
    auto cent5 = [&](const std::string& t, int32_t x, int32_t y, int32_t w) {
        int32_t tw = Buffer_text_width_2(c, t.c_str(), (int32_t)t.size());
        sw_text(this, c, t, x + (w - tw) / 2, y);
    };
    auto right4 = [&](const std::string& t, int32_t x, int32_t y, int32_t w) {
        int32_t tw = Buffer_text_width_2(c, t.c_str(), (int32_t)t.size());
        sw_text(this, c, t, x + w - tw, y);
    };
    // The analysed entry: +0xA918 within the faction's five (-1: the flat index before them).
    const int32_t flat = (faction - 1) * 5 + entry;
    const uint8_t* e = flat >= 0 ? &HallOfFame[flat * 0x8C] : nullptr;
    const bool valid = e && (e[0] & 1);
    // The category buttons (6 of 0x4F in +0xB46C: 0x4C, 0x27 .. 0x2D4, 0x77).
    static Sprite icons[6][2];
    static bool icons_loaded = false;
    if (!icons_loaded && Buffer_load_pcx(&bg, "hoficons.pcx", nullptr, 10, 0xEC) == 0) {
        icons_loaded = true;
        for (int32_t k = 0; k < 6; k++)
            for (int32_t r = 0; r < 2; r++)
                pfncSpriteFromCanvasRectTrans(&icons[k][r], &bg, 9, 1 + k * 0x50, 1 + r * 0x50, 0x4F, 0x4F, 0);
    }
    {
        UiRect r = fr(0x4C, 0x27, 0x2D4, 0x77);
        int32_t w = (r.right - r.left) / 6;
        int32_t x0 = (w - 0x4F) / 2 + r.left, y0 = ((r.bottom - r.top) - 0x4F) / 2 + r.top;
        for (int32_t k = 0; k < 6; k++) {
            bool pressed = valid && (e[1] & (1 << k));  // forced state 2, else -1
            Sprite* sp = &icons[k][pressed ? 0 : 1];
            if (sp->width) sprite_copy1(sp, c, 9, x0 + k * w, y0, 1, 1);
        }
    }
    // The faction buttons (7 of 0x46 in +0xB45C: 0x15, 0x7B .. 0x303, 0xED).
    {
        UiRect r = fr(0x15, 0x7B, 0x303, 0xED);
        int32_t w = (r.right - r.left) / 7;
        int32_t x0 = (w - 0x46) / 2 + r.left, y0 = ((r.bottom - r.top) - 0x46) / 2 + r.top;
        for (int32_t f = 1; f < 8; f++) {
            int32_t x = (f - 1) * w + x0;
            Sprite* sp = &BaseSprites[f][f == faction ? 32 : 33];
            if (sp->width) sprite_copy1(sp, c, sp->key, x, y0, 1, 1);
            spot_add(&spots[0], f, 1, x, y0, 0x46, 0x46);
        }
    }
    if (valid) {
        // WORLD MAP (+0xB43C: 0x12F, 0x10D .. 0x21D, 0x183): hof_<f>_<entry + 1>.pcx.
        Buffer pic;
        Buffer_ctor(&pic);
        std::string file = g_ui_outdir + "/hof_" + std::to_string(faction) + "_" + std::to_string(entry + 1) + ".pcx";
        if (Buffer_load_pcx(&pic, file.c_str(), nullptr, 0, 0xEC) == 0) {
            UiRect m = fr(0x12F, 0x10D, 0x21D, 0x183);
            Buffer_copy_4(&pic, c, 0, 0, m.left, m.top, m.right - m.left, m.bottom - m.top);
            texts.push_back("world map " + file.substr(file.rfind('/') + 1));
        }
        // The faction's logo (sprite 30) in +0xB47C (0x128, 0x187 ..), 9 and 10 in.
        Sprite* logo = &BaseSprites[faction][30];
        UiRect lr = fr(0x128, 0x187, 0x224, 0x248);
        if (logo->width) sprite_copy1(logo, c, logo->key, lr.left + 9, lr.top + 10, 1, 1);
        // SCORE ANALYSIS (+0xB44C: 0x228, 0x10D .. 0x2FA, 0x21F), 3 in.
        UiRect a = fr(0x228, 0x10D, 0x2FA, 0x21F);
        Buffer_set_clip(c, &a);
        const int32_t x = a.left + 3, w = (a.right - a.left) - 6;
        int32_t y = a.top + 3;
        sw_font(c, f12b, 0xE6);
        auto wrapped = [&](std::string t, bool upper) {
            if (upper) t = sw_upper(t);
            texts.push_back(t);
            Buffer_wrap_cent_4(c, t.c_str(), x, y, w);
            y += Buffer_wrap_height(c, t.c_str(), w);
        };
        for (int32_t k = 0; k < 6; k++)
            if (e[1] & (1 << k)) wrapped(sw_label(0xF38 + 4 * (uint32_t)k), false);
        const uint8_t v = e[0];
        if (v & 4) wrapped(Facility[102].name, true);
        else if (v & 8) wrapped(sw_label(0x830) + " " + sw_label(0xE10), true);
        else if (v & 0x10) wrapped(sw_label(0xE88), true);
        else if (v & 0x20) wrapped(sw_label(0xE70), true);
        else if (v & 0x40) wrapped(sw_label(0x1014) + " " + sw_label(0xE10), true);
        auto i32 = [&](int32_t o) { return *reinterpret_cast<const int32_t*>(e + o); };
        auto row = [&](const std::string& l, const std::string& val) {
            sw_text(this, c, l, x, y);
            right4(val, x, y, w);
            y += lh;
        };
        row(sw_upper(sw_label(0x51C)), std::to_string(i32(0x14)));
        row(sw_upper(sw_label(0x130)), std::to_string(i32(0x1C)));
        row(sw_label(0xE4), std::to_string(i32(0xC)));
        row(sw_label(0xAB4), std::to_string(i32(0x18)));
        row(sw_label(0x10A0), sw_upper(Difficulty[e[0x20] < 6 ? e[0x20] : 0]));
        if (e[0x21]) {
            if (e[0x21] == 2) row(sw_upper(sw_label(0x908)), "+25%");
            else row(sw_upper(sw_label(0x904)), "-25%");
        }
        if (v & 2) row(sw_upper(sw_label(0x1044)), "+100%");
        row(sw_upper(sw_label(0x6C0)), std::to_string(i32(8)));
        row(sw_upper(sw_label(0x1090)), std::to_string(i32(0x10)) + "%");
        Buffer_set_clip(c, &all);
        // The banner of the first mark: the label on a box of colour 0x41 at the window's top.
        sw_font(c, f18i, 0xF4);
        for (int32_t k = 0; k < 6; k++) {
            if (!(e[1] & (1 << k))) continue;
            std::string t = sw_label(0xF38 + 4 * (uint32_t)k);
            int32_t tw = Buffer_text_width_2(c, t.c_str(), (int32_t)t.size());
            UiRect box{kFx - tw / 2 + 0x186, kFy + 0xF, kFx - tw / 2 + 0x186 + tw + 0x14, kFy + 0xF + 0x15};
            Buffer_fill_3(c, &box, 0x41);
            cent5(t, kFx, kFy + 0x10, 800);
            break;
        }
    }
    // The column titles.
    sw_font(c, f14b, 0xF4);
    cent5(sw_label(0x109C), kFx + 0x1C, kFy + 0xF7, 0x109);
    cent5(sw_upper(sw_label(0x4C8)), kFx + 0x12E, kFy + 0xF7, 0xF0);
    cent5(sw_label(0xF10), kFx + 0x227, kFy + 0xF7, 0xD4);
    // TOP 5 SCORES (+0xB42C: 0x1E, 0x10E .. 0x123, 0x147, one per 0x3F): "title name", the
    // faction, "rating%  <label> year" centred; the analysed one in colour 0x9B; each a spot.
    UiRect er = fr(0x1E, 0x10E, 0x123, 0x147);
    for (int32_t slot = 0; slot < 5; slot++) {
        const uint8_t* q = fame_entry(faction, slot);
        if (q[0] & 1) {
            int32_t x = er.left, y = er.top + 3, w = er.right - er.left;
            sw_font(c, f12b, slot == entry ? 0x9B : 0xE6);
            cent5(std::string(reinterpret_cast<const char*>(q + 0x22)) + " " + reinterpret_cast<const char*>(q + 0x3A), x, y, w);
            y += lh;
            cent5(reinterpret_cast<const char*>(q + 0x52), x, y, w);
            y += lh;
            cent5(std::to_string(*reinterpret_cast<const int32_t*>(q + 0x10)) + "%" + "  " + sw_label(0xB68) +
                      std::to_string(*reinterpret_cast<const int32_t*>(q + 4)),
                  x, y, w);
            spot_add(&spots[0], slot, 0, er.left, er.top, er.right - er.left, er.bottom - er.top);
        }
        int32_t step = (er.bottom - er.top) + 6;
        er.top += step;
        er.bottom += step;
    }
}

void FameWinState::on_button(int32_t id) {
    if (id == 0x7B8) sw_close(this);
}

void FameWinState::log_state() {
    int32_t used = 0;
    for (int32_t f = 1; f < 8; f++)
        for (int32_t s = 0; s < 5; s++) used += fame_entry(f, s)[0] & 1;
    std::fprintf(stderr, "famestate faction %d entries %d\n", faction, used);
}

std::string fame_path() { return g_ui_outdir + "/hallfame.jhh"; }
// The world map picture of an entry: hof_<faction>_<slot + 1>.pcx (the original's game directory;
// here --outdir, as hallfame.jhh).
std::string hof_pcx(int32_t f, int32_t n) {
    return g_ui_outdir + "/hof_" + std::to_string(f) + "_" + std::to_string(n) + ".pcx";
}
int32_t& fi(int32_t f, uint32_t off) { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Factions[f]) + off); }
int32_t& ei(uint8_t* e, uint32_t off) { return *reinterpret_cast<int32_t*>(e + off); }
void strncpy_pad(char* d, const char* s, size_t n) {  // _strncpy: zeros after the terminator
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
}

// FUN_0044AC10: entry `slot` (< 4) of faction f moves down one place, its world map picture too.
void fame_move_down(int32_t f, int32_t slot) {
    if (slot >= 4) return;
    Buffer pic;
    Buffer_ctor(&pic);
    if (Buffer_load_pcx(&pic, hof_pcx(f, slot + 1).c_str(), nullptr, 0, 0xEC) == 0)
        Buffer_write_pcx(&pic, hof_pcx(f, slot + 2).c_str());
    uint8_t *src = fame_entry(f, slot), *dst = fame_entry(f, slot + 1);
    dst[0] = src[0];
    dst[1] = src[1];
    std::memcpy(dst + 4, src + 4, 0x1C);  // year, score .. (+0x04 .. +0x1F)
    dst[0x20] = src[0x20];
    dst[0x21] = src[0x21];
    for (uint32_t o : {0x22u, 0x3Au, 0x52u}) strncpy_pad(reinterpret_cast<char*>(dst + o), reinterpret_cast<char*>(src + o), 0x18);
    std::memcpy(dst + 0x6C, src + 0x6C, 0x20);
}

// FUN_0044A660 (hall_of_fame(1), the end of the game): the viewer's entry where its rating ranks
// among the faction's five (not in a scenario: GameState 0x20), the lower ones moved down; its
// fields (year, the score and rating of compute_score / get_rating, the faction's totals, its
// leader's title, name and faction noun, the difficulty, how the game was won), the "best of"
// marks it takes from the other entries (bits 1 rating, 2 +0x1C, 4 +0xC, 8 +0x18, 0x10 the
// earliest transcendence, 0x20 the earliest conquest; all of 1..8 without other entries), and the
// world map pane's picture written as hof_<f>_<slot + 1>.pcx.
void fame_add() {
    if (GameState & STATE_SCENARIO_CHEATED_FLAG) return;
    const int32_t f = CurrentPlayerFaction;
    if (f < 1 || f > 7) return;
    compute_score(f, nullptr, nullptr, 0);
    const int32_t score = fi(f, 0x209C);
    const int32_t rating = get_rating(f, score);
    int32_t slot = 0x11A;
    for (int32_t i = 4; i >= 0; i--) {
        uint8_t* e = fame_entry(f, i);
        if (ei(e, 0x10) <= rating || !(e[0] & 1)) slot = i;
    }
    if (slot > 4) return;
    for (int32_t k = 3; k >= slot; k--) fame_move_down(f, k);
    uint8_t* n = fame_entry(f, slot);
    ei(n, 0x10) = rating;
    ei(n, 8) = score;
    n[1] = 0;
    ei(n, 0xC) = fi(f, 0x220);
    n[0] = 1;
    ei(n, 4) = game_year(CurrentTurn);
    ei(n, 0x1C) = fi(f, 0x20B4) + fi(f, 0x20B0);
    ei(n, 0x14) = fi(f, 0x20B8);
    ei(n, 0x18) = fi(f, 0x20A8);
    n[0x21] = fi(f, 0x20BC) == 0 ? 0 : (MapNativeLifeForms == 0 ? 1 : 2);
    const uint8_t* mf = reinterpret_cast<const uint8_t*>(&MFactions[f]);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    strncpy_pad(reinterpret_cast<char*>(n + 0x22), reinterpret_cast<const char*>(mf + 0x4C), 0x18);
    strncpy_pad(reinterpret_cast<char*>(n + 0x3A), reinterpret_cast<const char*>(mf + 0x34), 0x18);
    GenderDefault = MFactions[f].noun_gender;
    PluralDefault = MFactions[f].is_noun_plural;
    strncpy_pad(reinterpret_cast<char*>(n + 0x52), MFactions[f].noun_faction, 0x18);
    n[0x20] = (uint8_t)fi(f, 8);
    if (GameRules & RULES_IRONMAN) n[0] |= 2;
    if (GameState & STATE_VICTORY_CONQUER) n[0] |= 8;
    else if (GameState & STATE_VICTORY_ECONOMIC) n[0] |= 0x10;
    else if (GameState & STATE_VICTORY_DIPLOMATIC) n[0] |= 0x20;
    else if (fi(f, 0x20C0)) n[0] |= 4;
    else if (GameState & STATE_IS_SCENARIO) n[0] |= 0x40;
    auto others = [&](auto&& fn) {
        for (int32_t g = 1; g < 8; g++)
            for (int32_t i = 0; i < 5; i++)
                if (g != f || i != slot) fn(fame_entry(g, i));
    };
    int32_t used = 0;
    others([&](uint8_t* o) {
        if (!(o[0] & 1)) return;
        used++;
        static const struct { uint8_t bit; uint32_t off; } kBest[4] = {{1, 0x10}, {2, 0x1C}, {4, 0xC}, {8, 0x18}};
        for (const auto& b : kBest)
            if ((o[1] & b.bit) && ei(o, b.off) <= ei(n, b.off)) {
                n[1] |= b.bit;
                o[1] &= (uint8_t)~b.bit;
            }
    });
    if (!used) n[1] = 0xF;
    for (const auto& w : {std::pair<uint8_t, uint8_t>{8, 0x10}, std::pair<uint8_t, uint8_t>{4, 0x20}}) {
        int32_t count = 0;
        others([&](uint8_t* o) {
            if (!(o[0] & 1) || !(o[0] & w.first)) return;
            count++;
            if ((o[1] & w.second) && (n[0] & w.first) && ei(n, 4) <= ei(o, 4)) {
                n[1] |= w.second;
                o[1] &= (uint8_t)~w.second;
            }
        });
        if (!count && (n[0] & w.first)) n[1] |= w.second;
    }
    // The world map pane (WorldWin's canvas) as the entry's picture.
    Buffer* mi = basewin_mi_canvas();
    UiRect pane = mi_rect(0xCFC);
    Buffer pic;
    Buffer_ctor(&pic);
    int32_t w = pane.right - pane.left, h = pane.bottom - pane.top;
    if (mi && mi->data && w > 0 && h > 0) {
        Buffer_init(&pic, w, h, 8, 0);
        Buffer_copy_4(mi, &pic, pane.left, pane.top, 0, 0, w, h);
    }
    int32_t r = Buffer_write_pcx(&pic, hof_pcx(f, slot + 1).c_str());
    g_fw.entry = slot;
    std::fprintf(stderr, "hall of fame: faction %d slot %d score %d rating %d flags %02X best %02X picture %d\n", f, slot,
                 score, rating, n[0], n[1], r);
}

}  // namespace

void GAMEAPI monument(int32_t is_new) {
    if ((MFactions[CurrentPlayerFaction].rule_flags & 0x80) || MultiplayerActive) return;
    if (is_new && ((GameMorePreferences & MPREF_AV_MONUMENTS_DISABLED) || (GameState & STATE_IS_SCENARIO) ||
                   ((GameState & STATE_GAME_DONE) && (GameState & STATE_FINAL_SCORE_DONE))))
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
    load_portraits(CurrentPlayerFaction);
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
    g_fw.entry = -1;
    if (add) fame_add();
    FameWinState* s = &g_fw;
    s->name = "halloffame";
    s->faction = CurrentPlayerFaction >= 1 && CurrentPlayerFaction < 8 ? CurrentPlayerFaction : 1;
    sw_init(s, (ScreenWidth - 0x400) / 2, ScreenHeight - 0x300, 0x400, 0x300, false);
    s->f_text = find_font(12, 0);
    s->f_bold = find_font(14, 1);
    sw_clear_buttons(s);
    // CLOSE (0x7B8: 0x22A, 0x22C .. 0x2F8, 0x242 of the panel; boxes 47 / 46 / 45).
    BaseButton* close = sw_button(s, sw_label(0xA64).c_str(), 0x7B8, kFx + 0x22A, kFy + 0x22C, 0x2F8 - 0x22A,
                                  0x242 - 0x22C, 0, s->f_bold);
    close->box[0] = box_def(47);
    close->box[1] = box_def(46);
    close->box[2] = box_def(45);
    for (Sprite** sp : {close->left, close->right, close->top, close->bottom, close->tl, close->tr, close->bl, close->br})
        sp[0] = sp[1] = sp[2] = nullptr;
    sw_run(s);
    sw_clear_buttons(s);
    if (FILE* f = std::fopen(fame_path().c_str(), "wb")) {
        std::fwrite(HallOfFame, 1, sizeof(HallOfFame), f);
        std::fclose(f);
    }
}
