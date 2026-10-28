// The main interface frame (MainInterface) with the status, info and world panes. See
// console_frame.h.
//
// From the original (MainInterface_init 0x459680, MainInterface_on_redraw 0x45BC50,
// MainInterface_restore_back 0x45C290, MainInterface_redraw_complete 0x45C3A0): the frame is
// console2_A.pcx (1024 wide; console_A.pcx at 800) loaded with the +10 pixel offset, drawn with
// colour 9 (raw 255) transparent at the bottom of the screen (x = (width - 1024) / 2, y = height
// - 257); its four panes are filled with the centre tile of box sprite 9; the menu and commlink
// buttons use the bars of console_x2_a.pcx; "TURN COMPLETE" is written centred in spot 5 in
// colour 0x65 over turn.flc's frame. The pane rectangles below are the init's (fields +0xD80.., 1024 layout).
//
// The status panes, the world map, the date box and "TURN COMPLETE" are drawn by the original's
// code (maininfc.cpp) into the main interface's canvas, the info pane and its M / I buttons by
// infowin.cpp. Not drawn: the button highlight states of the menu / commlink buttons.
#include "ui/console_frame.h"
#include "ui/reports.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/map.h"
#include "engine/strings.h"
#include "engine/veh.h"
#include "engine/zoc.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/colortables.h"
#include "ui/console.h"
#include "ui/diplowin.h"
#include "ui/planwin.h"
#include "ui/infowin.h"
#include "platform/ui_platform.h"
#include "ui/popup.h"
#include "ui/sprites.h"
#include "ui/statuswin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

namespace {

constexpr int32_t kKey = 9;

// Pane rectangles of the 1024 layout (MainInterface +0xD80, +0xD90, +0xDA0, +0xDB0), relative
// to the frame.
const UiRect kLeft = {4, 0x38, 0xF5, 0xFC};  // status: unit and tile
const UiRect kRight = {0x30B, 0x38, 0x3FC, 0xFC};  // world map, date
const UiRect kInfo = {0x106, 0x42, 0x2FB, 0xAC};  // info (two columns)
const UiRect kStack = {0xF4, 0xBC, 0x300, 0xFC};  // the units of the tile
const UiRect kMenuBtn = {0xB, 0x14, 0x75, 0x22};  // +0xA5C
const UiRect kCommBtn = {0x38C, 0x16, 0x3F6, 0x24};  // +0xA6C

struct Art {
    bool loaded = false, ok = false;
    Buffer frame;  // console2_A.pcx
    Sprite bar[3];  // button bars (console_x2_a.pcx, 106 x 14 at (1, 0xD9 + 15 i))
    Sprite globe[2][3];  // the world map buttons 0x3EE / 0x3EF (interface.pcx, MainInfc +0x21C74 / +0x21CF8)
} g_art;

int32_t g_ox = 0, g_oy = 0;  // frame origin on the screen

void load_art() {
    if (g_art.loaded) return;
    g_art.loaded = true;
    Buffer_ctor(&g_art.frame);
    const char* file = ScreenWidth == 800 ? "console_A.pcx" : "console2_A.pcx";
    if (Buffer_load_pcx(&g_art.frame, file, nullptr, 10, 0xEC)) return;
    Buffer x;
    Buffer_ctor(&x);
    if (!Buffer_load_pcx(&x, ScreenWidth == 800 ? "console_x_A.pcx" : "console_x2_A.pcx", nullptr, 10, 0xEC)) {
        int32_t y = 0xD9, y2 = 0xE7;
        for (Sprite& s : g_art.bar) {
            int32_t h = y2 - y;
            pfncSpriteFromCanvasRectTrans(&s, &x, kKey, 1, y, 0x6A, h, 0);
            y += h + 1;
            y2 += h + 1;
        }
    }
    Buffer ifc;
    Buffer_ctor(&ifc);
    if (!Buffer_load_pcx(&ifc, "interface.pcx", nullptr, 10, 0xEC)) {
        for (int32_t k = 0, y = 0x8F; k < 3; k++, y += 0x21) {
            pfncSpriteFromCanvasRectTrans(&g_art.globe[0][k], &ifc, kKey, 0x214, y, 0x36, 0x20, 0);
            pfncSpriteFromCanvasRectTrans(&g_art.globe[1][k], &ifc, kKey, 0x24B, y, 0x36, 0x20, 0);
        }
    }
    g_art.ok = true;
}

int32_t color(int32_t r, int32_t g, int32_t b) {
    static int32_t cache[4][2] = {};
    (void)cache;
    return Palette_get_nearest_palette_index(&GamePalette, r, g, b, 1);
}

void write(Buffer* s, const char* text, int32_t x, int32_t y, int32_t size, int32_t style, int32_t col) {
    Font* f = find_font(size, style);
    Buffer_set_font(s, f, nullptr, nullptr, nullptr);
    Buffer_set_text_color(s, col, -1, 1, 1);
    Buffer_write_l_3(s, text, g_ox + x, g_oy + y, (int32_t)std::strlen(text));
}

void write_center(Buffer* s, const char* text, UiRect r, int32_t size, int32_t style, int32_t col) {
    Font* f = find_font(size, style);
    Buffer_set_font(s, f, nullptr, nullptr, nullptr);
    Buffer_set_text_color(s, col, -1, 1, 1);
    UiRect a = {g_ox + r.left, g_oy + r.top, g_ox + r.right, g_oy + r.bottom};
    Buffer_write_cent_l_7(s, text, &a, (int32_t)std::strlen(text));
}

// MainInterface_restore_back: the pane's inside tiled with box 9's centre.
void restore_back(Buffer* s, const UiRect& r) {
    BoxDef* b = box_def(9);
    if (!b || !b->center) return;
    int32_t x = g_ox + r.left + 1, y = g_oy + r.top + 1;
    Buffer_tile(s, b->center, r.left + 1, r.top + 1, x, y, r.right - 1 - (r.left + 1), r.bottom - 1 - (r.top + 1));
}

void draw_button(Buffer* s, const UiRect& r, const char* label) {
    if (g_art.bar[0].width) {
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(&g_art.bar[0], s, kKey, g_ox + r.left, g_oy + r.top);
    }
    char up[64];
    std::snprintf(up, sizeof(up), "%s", label);
    for (char* p = up; *p; p++) *p = (char)crt_toupper((uint8_t)*p);
    write_center(s, up, r, 14, 1, color(228, 104, 24));
}

}  // namespace

int32_t console_view_height(int32_t h) { return h - 168; }

bool console_frame_hit(int32_t x, int32_t y) {
    load_art();
    if (!g_art.ok) return false;
    int32_t fx = x - g_ox, fy = y - g_oy;
    if (fx < 0 || fy < 0 || fx >= g_art.frame.width || fy >= g_art.frame.height) return false;
    if (Win_is_visible(&BaseWin)) return false;
    auto in = [&](const UiRect& r) { return fx >= r.left && fx < r.right && fy >= r.top && fy < r.bottom; };
    if (in(kLeft) || in(kRight) || in(kInfo) || in(kStack)) return true;
    return g_art.frame.data[(size_t)fy * g_art.frame.stride + fx] != kKey;
}

void console_frame_mouse(int32_t type, int32_t x, int32_t y) {
    int32_t fx = x - g_ox, fy = y - g_oy;
    auto in = [&](const UiRect& r) { return fx >= r.left && fx < r.right && fy >= r.top && fy < r.bottom; };
    const UiRect& pos = main_interface().pos;
    if (infowin_mouse(type, x - pos.left, y - pos.top)) return;
    // MainInterface_on_iface_group_clicked: the globe buttons (1000 + 6 the world map, else the
    // detail map).
    if (type == UiInput::LUP && InfcList[InfcUsed] == 0) {
        for (int32_t i = 0; i < 2; i++) {
            const UiRect& r = (i ? main_interface().globe1 : main_interface().globe2);
            int32_t mx = x - pos.left, my = y - pos.top;
            if (mx >= r.left && mx < r.right && my >= r.top && my < r.bottom) {
                if (i) WorldWin_set_detail_map(&WorldWin);
                else WorldWin_set_world_map(&WorldWin);
                return;
            }
        }
    }
    if (statuswin_mouse(type, x - pos.left, y - pos.top, x, y)) return;
    if (type != UiInput::LUP) return;
    // MainInterface_on_iface_button_clicked: 0x3EB the menu, 0x3EC commlink (Alt / the menu
    // bar's HQ menu is the keyboard way to them).
    if (in(kMenuBtn)) console_menu_toggle();
    else if (in(kCommBtn)) MultiWin_show(nullptr);
}

void console_frame_draw(Buffer* s) {
    load_art();
    if (!g_art.ok || Win_is_visible(&BaseWin)) return;
    maininfc_geometry();
    // Below the main view the screen has nothing else: black.
    int32_t view_h = console_view_height(s->height);
    for (int32_t y = view_h; y < s->height; y++) std::memset(s->data + (size_t)y * s->stride, 0, (size_t)s->width);
    // MainInterface_on_redraw (mode 0): the console art, the panes' background and their contents
    // drawn into the main interface's canvas.
    Buffer* mi = basewin_mi_canvas();
    const std::vector<uint8_t>& back = basewin_mi_back();
    if (mi->data && back.size() == (size_t)mi->stride * mi->height) std::memcpy(mi->data, back.data(), back.size());
    mi_slide_draw(mi);  // the console slid down (the battle report)
    mi_restore_back(1);
    g_ox = 0;
    g_oy = 0;
    // The info pane's frame and text (InfoWin; approximated, see the file's header).
    {
        UiRect c = main_interface().pane_center;
        UiRect r{c.left + 4, c.top + 4, c.right - 4, c.bottom - 4};
        Buffer_box_sprite(mi, &r, box_def(3));  // MainInterface_on_redraw's box (button 0x3E9: info)
    }
    if (battlewin_shown()) {
        battlewin_compose(mi);  // the battle report (interface mode 10)
    } else if (!diplowin_draw_console(mi, 0, 0)) {
        int32_t hv = *reinterpret_cast<int32_t*>(&InfoWin + 0x15C4),
                hb = *reinterpret_cast<int32_t*>(&InfoWin + 0x15C8);
        StatusWin_redraw(&InfoWin);
        *reinterpret_cast<int32_t*>(&InfoWin + 0x15C4) = hv;
        *reinterpret_cast<int32_t*>(&InfoWin + 0x15C8) = hb;
        statuswin_hover();
    }
    if (!battlewin_shown()) statuswin_compose(mi);  // the stack row's scroll arrows
    // (In the diplomacy mode DiploPop's panes replace the info pane: InfoWin is hidden.)
    if (!battlewin_shown() && diplowin_faction() < 0) infowin_compose(mi);  // the info pane or the message list
    if (!planwin_shown() && !diplowin_hq_shown())
        worldpane_draw(mi);  // WorldWin / MapWins[1] (hidden under DiploPop's HQ pane)
    maininfc_set_date();
    // The globe buttons (0x3EE world map, 0x3EF detail map: the one shown pressed; neither under
    // DiploPop's HQ pane, ButtonGroup_set(-1)): their sprite centred.
    int32_t pressed = diplowin_hq_shown() ? -1 : (worldwin_detail_shown() ? 1 : 0);
    for (int32_t i = 0; i < 2; i++) {
        const UiRect& r = (i ? main_interface().globe1 : main_interface().globe2);
        Sprite* sp = &g_art.globe[i][i == pressed ? 2 : 0];
        if (!sp->width) continue;
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(sp, mi, kKey, r.left + ((r.right - r.left) - sp->full_width) / 2,
                                      r.top + ((r.bottom - r.top) - sp->full_height) / 2);
    }
    maininfc_redraw_complete();
    diplowin_draw_iface(mi);
    // PlanWin (the go-to list's, the diplomacy's base and battle previews): its own window over the
    // world map pane (above DiploPop's HQ pane).
    if (planwin_shown()) planwin_compose(mi);
    // The window onto the screen (colour 9: the screen behind).
    const UiRect& pos = main_interface().pos;
    g_ox = pos.left;
    g_oy = pos.top;
    for (int32_t y = 0; y < mi->height; y++) {
        int32_t sy = pos.top + y;
        if (sy < 0 || sy >= s->height) continue;
        const uint8_t* src = mi->data + (size_t)y * mi->stride;
        uint8_t* dst = s->data + (size_t)sy * s->stride + pos.left;
        for (int32_t x = 0; x < mi->width && pos.left + x < s->width; x++)
            if (src[x] != kKey) dst[x] = src[x];
    }
    // MainInterface_on_status(0) hides the interface's buttons while another mode (the diplomacy
    // dialog's) is on.
    if (InfcList[InfcUsed] == 0 && diplowin_faction() < 0) {
        draw_button(s, kMenuBtn, label_get(624));
        draw_button(s, kCommBtn, label_get(527));
    }
}

void THISCALL StatusWin_set_title(uint8_t*, const char*) { ui_invalidate(); }
