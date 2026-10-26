// The tutorial windows (TutWin, ui 0x4BA6B0..0x4BEA20) and the unit hints of the human turn
// (Console_give_hints 0x511BB0).
//
// Reconstructed from the asm: Console_give_hints (every hint and its conditions: the new unit,
// colonist, terraformer, damaged unit, mind worms, fast / big / artillery units, ships, aircraft,
// a base to defend, an empty base, the artifact, the ogre, spore launchers and sea lurks, the
// bonus resources, fungus, where to build a base, the terraforming hints, supply pods, enemy bases,
// units and territory; with their calls (site_at, enemy_move, at_goal, can_terraform,
// good_sensor ...), tut_check / tut_check2 bits, TutHintsDone (0x9A64BC) and GameState 0x400);
// TutWin_tut_map (the tile's centre on the screen), TutWin_tut_win (the TUTOR popup with the
// unit's / tile's / base's picture, placed in the corner away from the tile, with its pointer
// lines to it; Close, or Continue / End Tour for the tour's windows; closing it with its button
// records its TutHintsDone bits), TutWin_draw_arrow, TutWin_reset, TutWin_on_button_clicked,
// TutWin_do_base and TutWin_base_rect (the base window's hints).
// Approximated: the pictures (drawn into a 100 x 75 picture instead of the popup's sprite box
// callbacks; the terrain at the window's zoom); keys other than Enter / Esc do not close the
// window (TutWin_on_key_click's forwarding of the key to the console).
// TutWin_tour (0x4BC6E0, the tour of kind +0x53C0 = TutorialArrowShown): kind 0 (the first turn):
// the view on the cursor, game_random(0, 8) and the best neighbour in that order for the
// DIRECTIONS text (TOUR1, TOUR2), the nearest ocean and fungus (TOUROCEAN, TOURFUNGUS), the
// interface (TOURMENU, TOURCOMMLINK, TOURUNITSINFO, TOURTERRAININFO, TOURSTACKINFO,
// TOURMESSAGEINFO, TOURBUTTONMESSAGE, TOURBUTTONINFO, TOURWORLDINFO, TOURDATEINFO) and TOUROVER;
// kind 3 (the first terraformer): TERRAFORMER and TOURMAP.
// Not reconstructed: kind 3's terraforming lessons after TOURMAP (TOURRAIN .. TOURFORMDONE: the
// scoring of the tiles around the cursor), the tours of the base window (kind 1, tour1), the
// social engineering (5), the drone riots (6, tour6) and the design workshop (7, tour7): they end
// at once.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/design.h"
#include "engine/goals.h"
#include "engine/goody.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/random.h"
#include "engine/text.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/sprites.h"
#include "ui/tutwin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

GLOBAL(int32_t, TutHintsDone, 0x9A64BC);     // _DAT_009A64BC: hints closed with their button
GLOBAL(int32_t, TutTourContinues, 0x8CC244); // DAT_008CC244: the window is part of a tour

namespace {

int32_t& tf(uint32_t off) { return *reinterpret_cast<int32_t*>(&TutWin + off); }
// TutWin fields (+0x537C..): the terrain tile shown, the target point, the pointer lines, the
// state.
constexpr uint32_t kTileX = 0x537C, kTileY = 0x5380, kPtX = 0x5384, kPtY = 0x5388, kLineX = 0x538C,
                   kLineY = 0x5390, kPt2X = 0x5394, kPt2Y = 0x5398, kLine2X = 0x539C, kLine2Y = 0x53A0,
                   kShown = 0x53A4, kDone = 0x53A8, kVeh = 0x53AC, kResult = 0x53B4, kKind = 0x53B8,
                   kTour = 0x53BC, kKeyPending = 0x53C4, kKeyCh = 0x53C8, kKeyVk = 0x53CC, kBits = 0x53D0,
                   kParent = 0x53D4;

int32_t& mwi(uint8_t* win, uint32_t off) { return *reinterpret_cast<int32_t*>(win + off); }

void clear_state() {
    tf(kShown) = 0;
    tf(kTileY) = -1;
    tf(kTileX) = -1;
    tf(kLine2X) = -1;
    tf(kDone) = 0;
    tf(kVeh) = -1;
    tf(kKind) = 0;
    tf(kKeyPending) = 0;
    TutTourContinues = 0;
}

bool in_view(uint8_t* win, int32_t x, int32_t y, int32_t margin) {
    int32_t oy = mwi(win, kMwOriginY), ox = mwi(win, kMwOriginX);
    if (y < oy + margin || y >= mwi(win, kMwRowsBelow) + mwi(win, kMwRowsAbove) - margin + oy) return false;
    int32_t tx = x;
    if (!(MapToggleFlat & 1)) {
        if (x < ox + margin) tx = MapAreaX + x;
        if (mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - margin + ox <= tx) tx -= MapAreaX;
    }
    return tx >= ox + margin && tx < mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - margin + ox;
}

// The picture of the window: a given sprite, else the unit (TutWin_veh_draw), the base
// (TutWin_base_draw) or the tile (TutWin_terrain_draw) drawn into a picture.
void set_picture(BasePop* pop, const Sprite* sprite) {
    if (sprite) {
        BasePop_set_picture_sprite(pop, sprite);
        return;
    }
    int32_t veh = tf(kVeh);
    if (veh < 0 && veh >= -1 && tf(kTileX) < 0) return;
    Buffer b;
    Buffer_ctor(&b);
    Buffer_init(&b, 100, 0x4B, 8, 0);
    Buffer_fill_2(&b, 9);
    if (veh >= 0) veh_draw(&b, veh, 0, 0, 0, 0, 0);
    else if (veh < -1) base_draw(&b, -2 - veh, 0, 0, 0, 0);
    else MapWin_gen_terrain_poly(&b, nullptr, 0, CurrentPlayerFaction, -1, 0x621, tf(kTileX), tf(kTileY), 0, 0);
    Sprite s;
    if (!pfncSpriteFromCanvasRectTrans(&s, &b, 9, 0, 0, b.width, b.height, 0) || s.width) {
        s.full_width = b.width;
        s.full_height = b.height;
        BasePop_set_picture_sprite(pop, &s);
    }
}

}  // namespace

// TutWin_reset (0x4BDDE0): the window closed; 1 when one was up.
int32_t THISCALL TutWin_reset(uint8_t*) {
    int32_t was = tf(kParent) != 0;
    tf(kParent) = 0;
    clear_state();
    if (was) ui_invalidate();
    return was;
}

// TutWin_on_button_clicked (0x4BDCC0): the button's answer (0 OK / Continue, n the script's
// #button n, -2 End Tour); a #button records the window's hint bits (and, for a window of a tour
// to come, e.g. LANDFALL's and BASECONTROL's TOUR, starts it: TutorialTourPending); End Tour ends
// the tour (+0x53B4).
void tutwin_button(int32_t button) {
    int32_t tour = TutTourContinues;
    tf(kParent) = 0;
    clear_state();
    tf(kTour) = button < 0 ? button + 1 : button;
    if (button < -1) tf(kResult) = 0;
    if (button < 1) return;
    if (tf(kResult) == 0 && tour) TutorialTourPending = 1;
    tf(kResult) = 0;
    if (tf(kBits)) TutHintsDone |= tf(kBits);
}

// TutWin_tut_win (0x4BDFE0): the window `label` of tutor.txt pointing at the screen point (px, py)
// (and at (px2, py2) when px2 >= 0); flags: bits 16.. the hint's TutHintsDone bits, bits 8..15 a
// tour's window, bits 0..7 the kind (0 a hint with Close, 1 a hint whose key goes on to the
// console, 2.. a tour's window with Continue / End Tour; 6 not placed).
int32_t THISCALL TutWin_tut_win(uint8_t*, uint8_t* parent, const char* label, int32_t px, int32_t py,
                                const Sprite* sprite, uint32_t flags, int32_t px2, int32_t py2) {
    tf(kKeyPending) = 0;
    tf(kBits) = 0;
    if (flags & 0xFFFF0000u) {
        tf(kBits) = (int32_t)(flags & 0xFFFF0000u);
        flags &= 0xFFFF;
    }
    uint32_t kind = flags & 0xFF;
    uint32_t pop_flags = kind == 0 ? 0x4000 : kind == 1 ? 0 : 0x40;
    tf(kPtX) = px;
    tf(kPtY) = py;
    int32_t sw = ScreenWidth, sh = ScreenHeight;
    popups_tutorial();
    if (tf(kParent)) {
        tf(kParent) = 0;
        clear_state();
        ui_invalidate();
    } else {
        tf(0x53D4) = 0;  // TutWin_UNK1
        clear_state();
    }
    tf(kParent) = (int32_t)(parent ? 1 : 0);
    if (flags & 0xFF00) {
        TutTourContinues = 1;
        flags = kind;
    }
    BasePop* pop = popup_new();
    auto start = [&]() {
        Popup_start(pop, "tutor", label, -1, nullptr, pop_flags);
        if ((int32_t)flags < 2) {
            BasePop_set_ok_text(pop, label_get(0x36C / 4));
        } else {
            BasePop_set_ok_text(pop, label_get(0x3B0 / 4));
            BasePop_set_cancel_text(pop, label_get(0x3AC / 4));
        }
        set_picture(pop, sprite);
    };
    start();
    BasePop_exec_3(pop, 1, 0);
    int32_t pw = 0, ph = 0;
    BasePop_size(pop, &pw, &ph);
    // The corner away from the point (the eighths of the screen by kind), the line's start at the
    // window's edge.
    int32_t limit_y = sh * 5 / 8, limit_x = sw / 2, eighth = sw / 8;
    if ((int32_t)flags < 2) limit_x += eighth;
    if (flags == 2 || flags == 5) limit_x += eighth;
    if (((flags == 3 || flags == 4) && (limit_x -= eighth, flags == 4)) || flags == 5) limit_y -= sh / 8;
    int32_t wx, wy;
    if (limit_y < py) {
        wy = tf(kPtY) / 8;
        tf(kLineY) = wy + ph;
    } else {
        wy = (sh - ph) - 8;
        tf(kLineY) = wy;
    }
    if (limit_x < px) {
        wx = (sw - pw) - 5;
        tf(kLineX) = (sw - pw / 2) - 5;
    } else {
        wx = 5;
        tf(kLineX) = pw / 2 + 5;
    }
    if (px2 >= 0) {
        tf(kPt2X) = px2;
        tf(kPt2Y) = py2;
        tf(kLine2X) = tf(kLineX);
        tf(kLine2Y) = tf(kLineY);
    }
    tf(kKind) = (int32_t)flags;
    tf(kShown) = 1;  // the pointer lines (Win_window_line_raw; drawn by tutwin_draw_lines)
    ui_invalidate();
    start();
    if (flags != 6) BasePop_set_loc(pop, wx, wy);
    int32_t answer = BasePop_exec_3(pop, 0, 0);
    int32_t button = BasePop_button_pressed(pop);
    // The OK button (1) or Cancel (-2) (TutWin_on_button_clicked).
    bool cancelled = (pop_flags & 0x40) && answer == -1;
    std::fprintf(stderr, "tutwin %s: kind %u answer %d%s\n", label, flags, answer, cancelled ? " (cancelled)" : "");
    popup_delete(pop);
    popups_normal();
    tutwin_button(cancelled ? -2 : button);  // 0 OK / Continue, n the #button n, -2 End Tour
    tf(kDone) = 1;
    if (flags == 1) {
        int32_t key_vk = tf(kKeyVk), pending = tf(kKeyPending), key_ch = tf(kKeyCh);
        if (tf(kTour)) TutWin_tour(&TutWin);
        if (pending) Console_on_key_click(&MainConsole, key_ch, key_vk);
    }
    ui_invalidate();
    return tf(kResult);
}

// TutWin_tut_map (0x4BE650, tut_map: its answer): the window `label` pointing at tile (x, y) (veh_id: the unit's
// picture; no picture: the tile's), and at (x2, y2) when it is in view.
int32_t tut_map(uint8_t* self, const char* label, int32_t x, int32_t y, int32_t veh_id, const Sprite* sprite,
                uint32_t flags, int32_t x2, int32_t y2) {
    if (veh_id < 0) {
        if (!sprite) {
            tf(kTileX) = x;
            tf(kTileY) = y;
        }
    } else {
        tf(kVeh) = veh_id;
    }
    uint8_t* win = MapWins[0];
    MapWin_focus(win, x, y);
    int32_t px = 0, py = 0;
    pfncTileToPoint(win, x, y, &px, &py);
    px += mwi(win, kMwHalfW);
    py += mwi(win, kMwHalfH);
    int32_t px2 = -1, py2 = -1;
    if (x2 >= 0 && in_view(win, x2, y2, 0)) {
        pfncTileToPoint(win, x2, y2, &px2, &py2);
        px2 += mwi(win, kMwHalfW);
        py2 += mwi(win, kMwHalfH);
    }
    return TutWin_tut_win(self, win, label, px, py, sprite, flags, px2, py2);
}

void THISCALL TutWin_tut_map(uint8_t* self, const char* label, int32_t x, int32_t y, int32_t veh_id, uint8_t* image,
                             int32_t flags, int32_t x2, int32_t y2) {
    const Sprite* s = nullptr;
    if (image) s = ArtSprite((uint32_t)(image - mem::host(mem::kImageBase)) + mem::kImageBase);
    tut_map(self, label, x, y, veh_id, s, (uint32_t)flags, x2, y2);
}

// (The headless "action" area's other function.)
void THISCALL ReportIf_on_redraw(uint8_t*) { ui_invalidate(); }

// TutWin_draw_arrow (0x4BDEA0): the pointer lines while the window is up (colour 0x68A5A0, two
// pixels wide).
void tutwin_draw_lines(Buffer* screen) {
    if (!tf(kShown) || !screen || !screen->data) return;
    int32_t color = *mem::host(0x68A5A0);
    auto line = [&](int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
        for (int32_t d = 0; d < 2; d++) {
            Buffer_line_2(screen, x1 + d, y1, x2 + d, y2, color);
            Buffer_line_2(screen, x1, y1 + d, x2, y2 + d, color);
        }
    };
    UiRect all{0, 0, screen->width, screen->height};
    Buffer_set_clip(screen, &all);
    line(tf(kLineX), tf(kLineY), tf(kPtX), tf(kPtY));
    if (tf(kLine2X) >= 0) line(tf(kLine2X), tf(kLine2Y), tf(kPt2X), tf(kPt2Y));
}

void THISCALL TutWin_draw_arrow(uint8_t*) { ui_invalidate(); }

// TutWin_do_base (0x4BA870): the window `label` pointing at the centre of the base window's
// rectangle.
void THISCALL TutWin_do_base(uint8_t* self, const UiRect* r, const char* label, uint32_t flags) {
    int32_t x = (r->right - r->left) / 2 + r->left, y = (r->bottom - r->top) / 2 + r->top;
    basewin_client_to_screen(&x, &y);
    TutWin_tut_win(self, MapWins[0], label, x, y, nullptr, flags, -1, -1);
}

// TutWin_base_rect (0x4BC5F0): the centre of the base window's rectangle on the screen.
void THISCALL TutWin_base_rect(const UiRect* r, int32_t* x, int32_t* y) {
    *x = (r->right - r->left) / 2 + r->left;
    *y = (r->bottom - r->top) / 2 + r->top;
    basewin_client_to_screen(x, y);
}

// ---- TutWin_tour (0x4BC6E0) ---------------------------------------------------------------------

namespace {

// The centre of the main interface's rectangle +off on the screen.
void mi_point(uint32_t off, int32_t* x, int32_t* y) {
    const UiRect& r = mi_rect(off);
    const UiRect& pos = mi_rect(kMiPos);
    *x = (r.right - r.left) / 2 + r.left + pos.left;
    *y = (r.bottom - r.top) / 2 + r.top + pos.top;
}

}  // namespace

void THISCALL TutWin_tour(uint8_t* self) {
    if (tf(kResult)) return;
    int32_t kind = TutorialArrowShown;
    tf(kResult) = 1;
    uint8_t* win = MapWins[0];
    int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
    auto end = [&]() {
        tf(kResult) = 0;
        if (tf(kParent)) ui_invalidate();
        tf(kParent) = 0;
        clear_state();
        ui_invalidate();
    };
    if (kind == 1 || kind == 5 || kind == 6 || kind == 7) {
        console_not_reconstructed_quiet("TutWin_tour (base window / social engineering / design workshop tours)");
        end();
        return;
    }
    pfncMoveMap(win, cx, cy, 1);
    // The best of the 8 neighbours in view, starting at a random direction.
    int32_t best = 0, best_dir = 0;
    int32_t start = game_random(0, 8);
    for (int32_t k = 0; k < 8; k++) {
        int32_t d = (k + start) & 7;
        int32_t tx = NearOffsetX[d] + cx, ty = NearOffsetY[d] + cy;
        if (!(MapToggleFlat & 1)) {
            if (tx < 0) tx += MapAreaX;
            else if (tx >= MapAreaX) tx -= MapAreaX;
        }
        if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX) continue;
        MAP* sq = map_tile(tx, ty);
        uint8_t alt = sq->climate & 0xE0;
        if (!(alt < 0x61 || mwi(win, kMwOriginY) + 2 <= (int32_t)((ty - (uint32_t)(sq->climate >> 5)) + 3))) continue;
        if (!in_view(win, tx, ty, 2) || alt <= 0x5F) continue;
        int32_t score = ((sq->items & 0x20) && alt > 0x3F) ? 4 : 8;
        if (NearOffsetX[d] == 0 || NearOffsetY[d] == 0) score++;
        if (best <= score) {
            best = score;
            best_dir = d;
        }
    }
    int32_t veh = MapWinVehA;
    if (kind == 0) {
        std::string label = "TOUR1";
        if (!MapWinInVehMode || veh < 0) label += "B";
        else if (Vehs[veh].unit_id == 2) parse_says(0, Units[2].name, -1, -1);
        else {
            parse_says(0, Units[Vehs[veh].unit_id].name, -1, -1);
            label += "A";
        }
        if (!tut_map(self, label.c_str(), cx, cy, veh, nullptr, 2, -1, -1)) return end();
        mw(win, kMwOptions) |= 0x20000;
        ui_invalidate_map();
        static const int32_t kDirNumber[9] = {9, 6, 3, 2, 1, 4, 7, 8, 5};
        parse_num(0, kDirNumber[best_dir]);
        if (text_open("tutor", "DIRECTIONS") == 0) {
            for (int32_t i = 0; i <= best_dir; i++) text_get();
            text_close();
            parse_says(0, TextBufferGetPtr, -1, -1);
        }
        if (!tut_map(self, "TOUR2", cx, cy, veh, nullptr, 2, -1, -1)) {
            mw(win, kMwOptions) &= ~0x20000;
            ui_invalidate_map();
            return end();
        }
    } else if (kind == 3 && veh >= 0 && Vehs[veh].unit_id == 1) {
        if (!tut_map(self, "TERRAFORMER", cx, cy, veh, nullptr, 2, -1, -1)) return end();
    }
    mw(win, kMwOptions) &= ~0x20000;
    ui_invalidate_map();
    // The tiles around the cursor (rings 1..20): the best ocean tile, a fungus tile.
    int32_t ocean_x = -1, ocean_y = -1, ocean_score = -1, fungus_x = -1, fungus_y = -1;
    for (int32_t pass = 0; pass < 2; pass++) {
        for (int32_t i = 1; i < 0x15; i++) {
            int32_t tx = TableOffsetX[i] + cx, ty = TableOffsetY[i] + cy;
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX) continue;
            MAP* sq = map_tile(tx, ty);
            uint8_t alt = sq->climate & 0xE0;
            if (!(alt < 0x61 || mwi(win, kMwOriginY) + 2 <= (int32_t)((ty - (uint32_t)(sq->climate >> 5)) + 3))) continue;
            if (!in_view(win, tx, ty, 2)) continue;
            if (!((Factions[CurrentPlayerFaction].player_flags & 0x200) || (sq->visibility & (1 << (CurrentPlayerFaction & 31))) ||
                  (GameState & 0x80)))
                continue;
            if (alt < 0x60) {
                int32_t score = ((sq->items & 0x20) && alt >= 0x40) ? 1 : 2;
                if (ocean_score < score) {
                    ocean_x = tx;
                    ocean_y = ty;
                    ocean_score = score;
                }
            } else if ((sq->items & 0x20) && alt >= 0x40) {
                if (fungus_y < 0 || fungus_y >= MapAreaY || fungus_x < 0 || fungus_x >= MapAreaX || (sq->val3 & 0xC0) < 0x80) {
                    fungus_y = ty;
                    fungus_x = tx;
                }
            }
        }
    }
    if (kind != 0) {
        if (tut_map(self, "TOURMAP", cx, cy, -1, nullptr, 2, -1, -1))
            console_not_reconstructed_quiet("TutWin_tour (the terraforming lessons after TOURMAP)");
        return end();
    }
    if (ocean_x >= 0 && !tut_map(self, "TOUROCEAN", ocean_x, ocean_y, -1, nullptr, 2, -1, -1)) return end();
    if (fungus_x >= 0 && !tut_map(self, "TOURFUNGUS", fungus_x, fungus_y, -1, nullptr, 2, -1, -1)) return end();
    // The interface.
    static const struct {
        const char* label;
        uint32_t rect;
    } kSteps[] = {{"TOURMENU", 0xA5C},          {"TOURCOMMLINK", 0xA6C},   {"TOURUNITSINFO", 0xCDC},
                  {"TOURTERRAININFO", 0xCCC},   {"TOURSTACKINFO", 0xCEC},  {"TOURMESSAGEINFO", 0xD1C},
                  {"TOURBUTTONMESSAGE", 0xA2C}, {"TOURBUTTONINFO", 0xA3C}, {"TOURWORLDINFO", 0xCFC},
                  {"TOURDATEINFO", 0xD0C}};
    for (const auto& st : kSteps) {
        int32_t x = 0, y = 0;
        mi_point(st.rect, &x, &y);
        if (!TutWin_tut_win(self, win, st.label, x, y, nullptr, 2, -1, -1)) return end();
    }
    if (MapWinInVehMode && CurrentTurn < 2) {
        tf(kParent) = 0;
        clear_state();
        tf(kResult) = 0;
        tut_map(self, "TOUROVER", cx, cy, veh, nullptr, 0, -1, -1);
        return;
    }
    end();
}

// ---- Console_give_hints (0x511BB0) ----------------------------------------------------------------

void THISCALL Console_give_hints(uint8_t* self) {
    int32_t v = mw(self, kConVeh);
    int32_t F = mw(self, kConFaction);
    if (Vehs[v].state & 0x4200) return;
    if (MultiplayerActive) return;
    if (Win_is_visible(&BaseWin) || Win_is_visible(&SocialWin)) return;
    auto U = [&](int32_t veh) -> const UNIT& { return Units[Vehs[veh].unit_id]; };
    auto triad = [&](int32_t veh) { return (int32_t)Chassis[U(veh).chassis_id].triad; };
    uint8_t* win = MapWins[0];
    int32_t x = Vehs[v].x, y = Vehs[v].y;
    auto tile = [&](int32_t tx, int32_t ty) { return map_tile(tx, ty); };
    auto wrapx = [&](int32_t tx) {
        if (!(MapToggleFlat & 1)) {
            if (tx < 0) tx += MapAreaX;
            else if (tx >= MapAreaX) tx -= MapAreaX;
        }
        return tx;
    };
    auto on_map = [&](int32_t tx, int32_t ty) { return ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX; };
    auto near = [&](int32_t i, int32_t* tx, int32_t* ty) {
        *tx = wrapx(NearOffsetX[i] + x);
        *ty = NearOffsetY[i] + y;
        return on_map(*tx, *ty);
    };
    auto say_home = [&]() {
        StrBuffer[0] = 0;
        say_base(StrBuffer, Vehs[v].home_base_id);
        parse_says(0, StrBuffer, -1, -1);
    };
    auto say_leader = [&](int32_t f) {
        PluralDefault = 0;
        GenderDefault = MFactions[f].is_leader_female;
        parse_says(1, MFactions[f].title_leader, -1, -1);
        PluralDefault = 0;
        GenderDefault = MFactions[f].is_leader_female;
        parse_says(2, MFactions[f].name_leader, -1, -1);
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        parse_says(3, MFactions[f].noun_faction, -1, -1);
    };
    // A tile well inside the view (2 tiles from its edges; the rolling / rocky tiles' height).
    auto well_in_view = [&](int32_t tx, int32_t ty) {
        int32_t oy = mwi(win, kMwOriginY);
        uint8_t c = tile(tx, ty)->climate;
        if (!((c & 0xE0) < 0x61 || oy + 2 <= (int32_t)((ty - (uint32_t)(c >> 5)) + 3))) return false;
        if (!(oy + 2 <= ty && ty < mwi(win, kMwRowsBelow) + mwi(win, kMwRowsAbove) - 2 + oy)) return false;
        int32_t ox = mwi(win, kMwOriginX), t = tx;
        if (!(MapToggleFlat & 1)) {
            if (tx < ox + 2) t = MapAreaX + tx;
            if (mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - 2 + ox <= t) t -= MapAreaX;
        }
        return ox + 2 <= t && t < mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - 2 + ox;
    };
    uint32_t sea = 0;  // uVar8: the unit is a ship
    if (Vehs[v].state & 0x2000) {
        // A unit just produced.
        if (U(v).plan < 4 && Vehs[v].home_base_id >= 0 && tut_check(8) && CurrentTurn < 0x19) {
            say_home();
            parse_says(1, U(v).name, -1, -1);
            tut_map(&TutWin, "NEWUNIT", x, y, v, nullptr, 0, -1, -1);
            return;
        }
        if (U(v).plan < 4 && tut_check(0x10) && CurrentTurn > 0x27) {
            tut_map(&TutWin, "NEWUNITS", x, y, v, nullptr, 0, -1, -1);
            return;
        }
        if (U(v).plan == 9 && Factions[F].units_active[Vehs[v].unit_id] == 1 && tut_check(0x10000) && triad(v) != 1) {
            say_home();
            parse_says(1, U(v).name, -1, -1);
            TutorialArrowShown = 2;
            tut_map(&TutWin, "NEWTERRAFORMER", x, y, v, nullptr, 0x100, -1, -1);
            GameState |= 0x400;
            return;
        }
        if (U(v).plan == 8 && triad(v) == 0 && !(TutHintsDone & 0x1000000)) {
            // The best site in view in the faction's territory on the same continent.
            int32_t bx = -1, by = -1, best = 0;
            Console_focus(&MainConsole, x, y, CurrentPlayerFaction);
            for (int32_t i = 49; i < 289; i++) {
                int32_t tx = wrapx(TableOffsetX[i] + x), ty = TableOffsetY[i] + y;
                if (!on_map(tx, ty) || !well_in_view(tx, ty)) continue;
                MAP* sq = tile(tx, ty);
                if (sq->region != tile(x, y)->region) continue;
                if (whose_territory(F, tx, ty, nullptr, 0) != F) continue;
                if (!((Factions[F].player_flags & 0x200) || (sq->visibility & (1 << (F & 31))))) continue;
                if ((sq->items & 0x20) && (sq->climate & 0xE0) >= 0x40) continue;
                if ((sq->val3 & 0xC0) >= 0x41) continue;
                int32_t site = (sq->items & 0x1000) ? 0 : site_at(tx, ty, 0);
                if (best <= site) {
                    bx = tx;
                    by = ty;
                    best = site;
                }
            }
            say_home();
            if (MFactions[CurrentPlayerFaction].rule_flags & 0x100) return;
            tut_map(&TutWin, "NEWCOLONIST", x, y, v, nullptr, 0x1000000, bx, by);
            return;
        }
    }
    if (TutorialWinActive) return;
    sea = triad(v) == 1;
    if (Vehs[v].damage_taken && tut_check(0x200000)) {
        tut_map(&TutWin, "DAMAGED", x, y, v, nullptr, 0, -1, -1);
        return;
    }
    if (triad(v) == 0 && (tile(x, y)->climate & 0xE0) > 0x5F) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t tx, ty;
            if (!near(i, &tx, &ty)) continue;
            int32_t w = veh_at(tx, ty);
            if (w < 0 || Vehs[w].faction_id != 0) continue;
            if (Vehs[w].unit_id == 8 && tut_check(0x4000000)) {
                tut_map(&TutWin, "MINDWORMS", tx, ty, w, nullptr, 0, -1, -1);
                return;
            }
        }
        if (triad(v) == 0 && Chassis[U(v).chassis_id].speed == 2 && Weapon[U(v).weapon_id].offense_value != 0 &&
            tut_check(0x2000000)) {
            StrBuffer[0] = 0;
            say_stats_3(StrBuffer, Vehs[v].unit_id);
            parse_says(1, StrBuffer, -1, -1);
            parse_says(0, U(v).name, -1, -1);
            tut_map(&TutWin, "FASTUNIT", x, y, v, nullptr, 0, -1, -1);
            return;
        }
        if (triad(v) == 0 && Chassis[U(v).chassis_id].speed == 1 && Weapon[U(v).weapon_id].offense_value > 1 &&
            !can_arty(Vehs[v].unit_id, 1) && tut_check(0x40000000)) {
            StrBuffer[0] = 0;
            say_stats_3(StrBuffer, Vehs[v].unit_id);
            parse_says(1, StrBuffer, -1, -1);
            parse_says(0, U(v).name, -1, -1);
            tut_map(&TutWin, "BIGUNIT", x, y, v, nullptr, 0, -1, -1);
            return;
        }
        if (triad(v) == 0 && can_arty(Vehs[v].unit_id, 1) && tut_check((int32_t)0x80000000)) {
            StrBuffer[0] = 0;
            parse_says(0, U(v).name, -1, -1);
            tut_map(&TutWin, "ARTYUNIT", x, y, v, nullptr, 0, -1, -1);
            return;
        }
        if (triad(v) == 1) {
            if (!(TutHintsShown & 0x20)) {
                tut_map(&TutWin, "NEWSHIP", x, y, v, nullptr, 0, -1, -1);
                TutHintsShown |= 0x20;
                return;
            }
            TutHintsShown |= 0x20;
        }
        if (triad(v) == 2) {
            if (!(TutHintsShown & 0x40)) {
                tut_map(&TutWin, "NEWAIR", x, y, v, nullptr, 0, -1, -1);
                TutHintsShown |= 0x40;
                return;
            }
            TutHintsShown |= 0x40;
        }
        int32_t b = base_at(x, y);
        if (b >= 0 && (uint32_t)Bases[b].faction_id == (uint32_t)F &&
            (Bases[b].pop_size > 2 || Factions[F].total_combat_units > 3 ||
             Factions[F].units_active[Vehs[v].unit_id] > 1) &&
            enemy_move(-1 - v) && !(TutHintsDone & 0x800000)) {
            StrBuffer[0] = 0;
            say_stats_3(StrBuffer, Vehs[v].unit_id);
            parse_says(2, StrBuffer, -1, -1);
            parse_says(0, Bases[b].name, -1, -1);
            parse_says(1, U(v).name, -1, -1);
            const char* label = "DEFENDIT";
            if (U(v).plan == 2 && Armor[U(v).armor_id].defense_value > Weapon[U(v).weapon_id].offense_value &&
                Weapon[U(v).weapon_id].offense_value == 1)
                label = "DEFENDIT2";
            tut_map(&TutWin, label, x, y, v, nullptr, 0x800000, -1, -1);
            return;
        }
        if (CurrentTurn > 0x13 && !(TutHintsDone & 0x4000000) && Weapon[U(v).weapon_id].offense_value != 0) {
            for (int32_t i = 0; i < BaseCount; i++) {
                BASE& bs = Bases[i];
                if ((uint32_t)bs.faction_id != (uint32_t)F || veh_at(bs.x, bs.y) >= 0 || bs.pop_size == 1) continue;
                int32_t by = bs.y, bx = bs.x;
                bool ok = true;
                if (on_map(bx, by)) {
                    uint8_t c = tile(bx, by)->climate;
                    if (!((c & 0xE0) < 0x61 || mwi(win, kMwOriginY) + 2 <= (int32_t)((by - (uint32_t)(c >> 5)) + 3))) ok = false;
                }
                if (!ok) continue;
                if (!(mwi(win, kMwOriginY) + 2 <= by && by < mwi(win, kMwRowsBelow) + mwi(win, kMwRowsAbove) - 2 + mwi(win, kMwOriginY)))
                    continue;
                int32_t t = bx, ox = mwi(win, kMwOriginX);
                if (!(MapToggleFlat & 1)) {
                    if (bx < ox + 2) t += MapAreaX;
                    if (mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - 2 + ox <= t) t -= MapAreaX;
                }
                if (!(ox + 2 <= t && t < mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) - 2 + ox)) continue;
                if (tut_check2(0x100) || CurrentTurn % 0x14 == 0) {
                    parse_says(0, bs.name, -1, -1);
                    tut_map(&TutWin, "EMPTYBASE", bs.x, bs.y, -1, nullptr, 0x4000000, -1, -1);
                    break;
                }
            }
        }
    }
    {
        MAP* sq = tile(x, y);
        if ((sq->items & 1) && (sq->val2 & 0xF) < 8) return;  // in a base
    }
    if (U(v).plan == 0xC && tut_check(0x1000000)) {
        tut_map(&TutWin, "ARTIFACT", x, y, v, nullptr, 0, -1, -1);
        return;
    }
    if (tut_check2(0x20000)) {
        int32_t u = Vehs[v].unit_id;
        if (u == 0x10 || u == 0x11 || u == 0x12) {
            tut_map(&TutWin, "GETOGRE", x, y, v, nullptr, 0, -1, -1);
            return;
        }
    }
    for (int32_t i = 0; i < 8; i++) {
        int32_t tx, ty;
        if (!near(i, &tx, &ty)) continue;
        int32_t w = veh_at(tx, ty);
        if (w < 0 || Vehs[w].faction_id != 0) continue;
        if (Vehs[w].unit_id == 0xF && tut_check2(0x80000)) {
            tut_map(&TutWin, "SPORELAUNCHER", tx, ty, w, nullptr, 0, -1, -1);
            return;
        }
        if (Vehs[w].unit_id == 0xE && tut_check2(0x40000)) {
            tut_map(&TutWin, "SEALURK", tx, ty, w, nullptr, 0, -1, -1);
            return;
        }
    }
    for (int32_t i = 0; i < 8; i++) {
        int32_t tx, ty;
        if (!near(i, &tx, &ty)) continue;
        MAP* sq = tile(tx, ty);
        if (!(((sq->items & 3) == 0 || (sq->val2 & 0xF) > 7) && (sq->items & 0x81E02850) == 0 && !goody_at(tx, ty)))
            continue;
        int32_t bonus = bonus_at(tx, ty);
        const char* label = nullptr;
        int32_t bit = 0;
        if (bonus == 1) label = "BONUSNUTRIENT", bit = 0x20000;
        else if (bonus == 2) label = "BONUSMINERALS", bit = 0x40000;
        else if (bonus == 3) label = "BONUSENERGY", bit = 0x80000;
        if (!label || !tut_check(bit)) continue;
        int32_t b2 = bonus_at(tx, ty);
        Sprite* pic = ArtSprite(0x75B230u + (uint32_t)(((int32_t)sea - 2) + b2 * 2) * 0x58);
        tut_map(&TutWin, label, tx, ty, -1, pic, 0, -1, -1);
        return;
    }
    {
        MAP* sq = tile(x, y);
        if ((sq->items & 0x20) && (sq->climate & 0xE0) > 0x3F) {
            if ((sq->climate & 0xE0) < 0x60) {
                if (tut_check(0x8000)) {
                    tut_map(&TutWin, "FUNGUSSEA", x, y, -1, nullptr, 0, -1, -1);
                    return;
                }
            } else if (tut_check(0x100)) {
                tut_map(&TutWin, "FUNGUSAMUNGUS", x, y, -1, nullptr, 0, -1, -1);
                return;
            }
        }
    }
    if (GameState & 0x400) return;
    if (U(v).plan == 8 && !(TutHintsDone & 0x10000)) {
        // Build here: no better site within 2 tiles in the faction's (or no one's) territory.
        int32_t site = site_at(x, y, 0);
        if (site <= 3 && site < (int32_t)Factions[F].region_unk_6[tile(x, y)->region]) return;
        for (int32_t i = 0; i < 21; i++) {
            int32_t tx = wrapx(TableOffsetX[i] + x), ty = TableOffsetY[i] + y;
            if (!on_map(tx, ty)) continue;
            int32_t owner = whose_territory(F, tx, ty, nullptr, 0);
            if (owner >= 0 && owner != F) continue;
            if (site < site_at(tx, ty, 0)) return;
        }
        tut_map(&TutWin, "BUILDHERE", x, y, v, nullptr, 0x10000, -1, -1);
        return;
    }
    const char* label = nullptr;
    const Sprite* pic = nullptr;
    uint32_t bits = 0;
    if (U(v).plan == 9 && sea == 0) {
        int32_t b = base_find_2(x, y, F);
        if (b < 0) return;
        if (BaseFindDist <= 2) {
            if (at_goal(F, 0x29, x, y) && !(TutHintsDone & 0x200000)) {
                parse_says(0, cur_base().name, -1, -1);
                if (!(tile(x, y)->items & 0x40)) {
                    GameState |= 0x400;
                    return;
                }
                tut_map(&TutWin, "TERRARAISE", x, y, -1, nullptr, 0x200000, -1, -1);
                GameState |= 0x400;
                return;
            }
            int32_t t = can_terraform(F, x, y, 0, b);
            bool other = false;
            if (t == 4) {
                if (!(TutHintsDone & 0x100000) && !(tile(x, y)->items & 0x8000)) {
                    GameState |= 0x400;
                    parse_says(0, Bases[b].name, -1, -1);
                    tut_map(&TutWin, "TERRAFARM", x, y, -1, ArtSprite(0x79204C), 0x100000, -1, -1);
                    return;
                }
                other = true;
            } else if (t == 6) {
                if (!(TutHintsDone & 0x40000)) label = "TERRAMINE", pic = ArtSprite(0x776A50), bits = 0x40000;
                else other = true;
            } else if (t == 7) {
                if (!(TutHintsDone & 0x20000)) label = "TERRASOLAR", pic = ArtSprite(0x7991F8), bits = 0x20000;
                else other = true;
            } else if (t == 8 && !(TutHintsDone & 0x400000)) {
                label = "TERRAFOREST", bits = 0x400000;
            } else {
                other = true;
            }
            if (other) {
                if ((at_goal(F, 0x79, x, y) || good_sensor(CurrentPlayerFaction, x, y)) && !(TutHintsDone & 0x2000000)) {
                    label = "TERRASENSOR", bits = 0x2000000;
                } else {
                    uint32_t items = tile(x, y)->items;
                    if (!((items & 4) || (TutHintsDone & 0x80000) || (!(items & 0x10) && !at_goal(F, 9, x, y)))) {
                        label = "TERRAROAD", bits = 0x80000;
                    }
                }
            }
            if (label) {
                tut_map(&TutWin, label, x, y, -1, pic, bits, -1, -1);
                GameState |= 0x400;
                return;
            }
        }
    }
    // Supply pods, enemy bases, units and territory around the unit.
    if (!(TutHintsShown & 0x2000)) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t tx, ty;
            if (!near(i, &tx, &ty)) continue;
            if (!goody_at(tx, ty)) continue;
            if (((tile(tx, ty)->climate & 0xE0) < 0x60) != (sea != 0)) continue;
            if (!tut_check(0x2000)) return;
            Sprite* p = ArtSprite(0x77AFF8u + (uint32_t)(sea + ((ty * 5 + tx * 7) % 3) * 2) * 0x2C);
            tut_map(&TutWin, "EXPLORATION", tx, ty, -1, p, 0, -1, -1);
            return;
        }
    }
    if (!(TutHintsShown & 0x8000000)) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t tx, ty;
            if (!near(i, &tx, &ty)) continue;
            int32_t b = base_at(tx, ty);
            if (b < 0 || (uint32_t)Bases[b].faction_id == (uint32_t)F) continue;
            if (!tut_check(0x8000000)) return;
            parse_says(0, Bases[b].name, -1, -1);
            say_leader(Bases[b].faction_id);
            tut_map(&TutWin, "ENEMYBASE", tx, ty, -1, nullptr, 0, -1, -1);
            return;
        }
    }
    if (!(TutHintsShown & 0x10000000)) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t tx, ty;
            if (!near(i, &tx, &ty)) continue;
            MAP* sq = tile(tx, ty);
            if (!((sq->items & 1) == 0 || (sq->val2 & 0xF) > 7)) continue;
            int32_t w = veh_at(tx, ty);
            if (w < 0 || (uint32_t)Vehs[w].faction_id == (uint32_t)F || Vehs[w].faction_id == 0) continue;
            if (!tut_check(0x10000000)) return;
            say_leader(Vehs[w].faction_id);
            tut_map(&TutWin, "ENEMYVEH", tx, ty, w, nullptr, 0, -1, -1);
            return;
        }
    }
    if (TutHintsShown & 0x20000000) return;
    for (int32_t i = 0; i < 8; i++) {
        int32_t tx, ty;
        if (!near(i, &tx, &ty)) continue;
        MAP* sq = tile(tx, ty);
        if (!((sq->items & 3) == 0 || (sq->val2 & 0xF) > 7)) continue;
        int32_t owner = whose_territory(F, tx, ty, nullptr, 0);
        if (owner == F || owner < 0 || whose_territory(F, x, y, nullptr, 0) == owner) continue;
        if (!tut_check(0x20000000)) return;
        say_leader(owner);
        tut_map(&TutWin, "ENEMYTERR", tx, ty, -1, nullptr, 0, -1, -1);
        return;
    }
}
