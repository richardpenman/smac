// The main interface frame (MainInterface) with the status, info and world panes. See
// console_frame.h.
//
// From the original (MainInterface_init 0x459680, MainInterface_on_redraw 0x45BC50,
// MainInterface_restore_back 0x45C290, MainInterface_redraw_complete 0x45C3A0): the frame is
// console2_A.pcx (1024 wide; console_A.pcx at 800) loaded with the +10 pixel offset, drawn with
// colour 9 (raw 255) transparent at the bottom of the screen (x = (width - 1024) / 2, y = height
// - 257); its four panes are filled with the centre tile of box sprite 9; the menu and commlink
// buttons use the bars of console_x2_a.pcx; "TURN COMPLETE" is written centred in spot 5 in
// colour 0x65. The pane rectangles below are the init's (fields +0xD80.., 1024 layout).
//
// The status panes, the world map, the date box and "TURN COMPLETE" are drawn by the original's
// code (maininfc.cpp) into the main interface's canvas. Approximated: the info pane's text
// (InfoWin_setup_text: the year and the allocation, at measured positions). Not drawn: turn.flc,
// the info pane's buttons (M / I), the globe buttons, the button highlight states and the slide
// animation of the arms.
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
const UiRect kLeft = {4, 0x38, 0xF5, 0xFC};      // status: unit and tile
const UiRect kRight = {0x30B, 0x38, 0x3FC, 0xFC};  // world map, date
const UiRect kInfo = {0x106, 0x42, 0x2FB, 0xAC};   // info (two columns)
const UiRect kStack = {0xF4, 0xBC, 0x300, 0xFC};   // the units of the tile
const UiRect kMenuBtn = {0xB, 0x14, 0x75, 0x22};   // +0xA5C
const UiRect kCommBtn = {0x38C, 0x16, 0x3F6, 0x24};  // +0xA6C

struct Art {
    bool loaded = false, ok = false;
    Buffer frame;      // console2_A.pcx
    Sprite bar[3];     // button bars (console_x2_a.pcx, 106 x 14 at (1, 0xD9 + 15 i))
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

// The info pane (InfoWin_setup_text, first state): the year, the faction's allocation.
void draw_info_pane(Buffer* s) {
    int32_t green = color(116, 156, 56), light = color(184, 192, 96);
    char line[160];
    char year[64];
    year[0] = 0;
    say_year(year);
    std::snprintf(line, sizeof(line), "%s: %s", label_get(0), year);
    write(s, line, 0x112, 0x4C, 12, 0, light);
    const Faction& f = Factions[CurrentPlayerFaction];
    std::snprintf(line, sizeof(line), "%s: %d%%", label_get(169), (10 - f.SE_alloc_psych - f.SE_alloc_labs) * 10);
    write(s, line, 0x112, 0x68, 12, 0, green);
    std::snprintf(line, sizeof(line), "%s: %d%%", label_get(168), f.SE_alloc_psych * 10);
    write(s, line, 0x112, 0x76, 12, 0, green);
    std::snprintf(line, sizeof(line), "%s: %d%%", label_get(170), f.SE_alloc_labs * 10);
    write(s, line, 0x112, 0x84, 12, 0, green);
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
    if (type != UiInput::LUP) return;
    // MainInterface_on_iface_button_clicked: 0x3EB the menu, 0x3EC commlink (Alt / the menu
    // bar's HQ menu is the keyboard way to them).
    if (in(kMenuBtn)) console_menu_key(0x40000 | VKEY_MENU);
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
    mi_restore_back(1);
    g_ox = 0;
    g_oy = 0;
    // The info pane's frame and text (InfoWin; approximated, see the file's header).
    {
        UiRect c = mi_rect(kMiPaneCenter);
        UiRect r{c.left + 4, c.top + 4, c.right - 4, c.bottom - 4};
        Buffer_box_sprite(mi, &r, box_def(3));  // MainInterface_on_redraw's box (button 0x3E9: info)
    }
    if (!diplowin_draw_console(mi, 0, 0)) {
        StatusWin_redraw(&InfoWin);
        draw_info_pane(mi);
    } else {
        draw_info_pane(mi);
    }
    worldwin_draw();
    maininfc_set_date();
    // The globe buttons (0x3EE pressed by ButtonGroup_set(0x3EE, 1), 0x3EF): their sprite centred.
    for (int32_t i = 0; i < 2; i++) {
        const UiRect& r = mi_rect(i ? 0xA9C : 0xA8C);
        Sprite* sp = &g_art.globe[i][i ? 0 : 2];
        if (!sp->width) continue;
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(sp, mi, kKey, r.left + ((r.right - r.left) - sp->full_width) / 2,
                                      r.top + ((r.bottom - r.top) - sp->full_height) / 2);
    }
    maininfc_redraw_complete();
    // The window onto the screen (colour 9: the screen behind).
    const UiRect& pos = mi_rect(kMiPos);
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
    draw_button(s, kMenuBtn, label_get(624));
    draw_button(s, kCommBtn, label_get(527));
}

void THISCALL StatusWin_set_title(uint8_t*, const char*) { ui_invalidate(); }
