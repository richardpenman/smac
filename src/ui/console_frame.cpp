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
// Approximated (not the original's drawing code): the contents of the panes (StatusWin_draw_active
// / StatusWin_draw_status, InfoWin_setup_text, WorldWin_on_redraw, MainInterface_set_date) are
// written with the original's fonts, colours and positions as measured on the original's screen,
// not by those functions; the turn.flc animation, the button highlight states and the slide
// animation of the arms are not drawn.
#include "ui/console_frame.h"
#include "ui/reports.h"

#include <cstdio>
#include <cstring>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/map.h"
#include "engine/strings.h"
#include "engine/veh.h"
#include "engine/zoc.h"
#include "ui/basewin.h"
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

// The unit pane (StatusWin_draw_active): picture, name, statistics, morale, faction, moves,
// orders.
void draw_unit_pane(Buffer* s, int32_t v) {
    const VEH& u = Vehs[v];
    int32_t green = color(116, 156, 56);
    UiRect clip = {g_ox + 8, g_oy + 0x3C, g_ox + 0x86, g_oy + 0xF8};
    Buffer_set_clip(s, &clip);
    veh_draw(s, v, g_ox + 12, g_oy + 0x3E, 0, 0x1000, 0);
    Buffer_set_clip(s, nullptr);
    char line[256];
    std::snprintf(line, sizeof(line), "%s", Units[u.unit_id].name);
    UiRect r = {8, 0x7C, 0x86, 0x8C};
    write_center(s, line, r, 12, 0, green);
    line[0] = 0;
    say_stats_3(line, u.unit_id);
    r.top += 14;
    r.bottom += 14;
    write_center(s, line, r, 12, 0, green);
    // Morale, home base (or "Independent"), moves left, orders.
    int32_t y = 0xA3;
    int32_t m = u.morale;  // the unit's morale level (the pane's word)
    if (m < 0) m = 0;
    if (m > 6) m = 6;
    std::snprintf(line, sizeof(line), "%s", Morale[m].name);
    write(s, line, 14, y, 12, 0, green);
    if (u.home_base_id >= 0) std::snprintf(line, sizeof(line), "%s", Bases[u.home_base_id].name);
    else std::snprintf(line, sizeof(line), "%s", label_get(194));
    write(s, line, 14, y + 14, 12, 0, green);
    int32_t left = speed(v, 0) - u.moves_spent;
    if (left < 0) left = 0;
    std::snprintf(line, sizeof(line), "%s: %d", label_get(7), left / Rules.move_rate_roads);
    write(s, line, 14, y + 28, 12, 0, green);
    line[0] = 0;
    say_orders(line, v);
    if (!line[0]) std::snprintf(line, sizeof(line), "%s", Order[0].order);
    y += 14;
    write(s, line, 14, y + 28, 12, 0, green);
}

// The tile pane (StatusWin_draw_status): elevation, coordinates (unexplored tiles: the word).
void draw_tile_pane(Buffer* s, int32_t x, int32_t y) {
    int32_t green = color(116, 156, 56);
    char line[128];
    if (x < 0 || y < 0 || x >= MapAreaX || y >= MapAreaY) return;
    MAP* sq = map_tile(x, y);
    bool seen = (GameState & 0x80) || (sq->visibility & (1 << (CurrentPlayerFaction & 31)));
    if (!seen) {
        UiRect r = {0x8C, 0x60, 0xF1, 0x70};
        write_center(s, label_get(192), r, 12, 0, green);  // "Unexplored"
    } else {
        std::snprintf(line, sizeof(line), "%s: %dm", label_get(11), pfncGetElevation(x, y));
        write(s, line, 0x95, 0x75, 12, 0, green);
    }
    std::snprintf(line, sizeof(line), "(%d , %d)", x, y);
    write(s, line, 0xBA, 0xA6, 12, 0, green);
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

// The stack row (the units of the cursor's tile, or of the selected unit).
void draw_stack(Buffer* s, int32_t v) {
    if (v < 0) return;
    while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
    // The row starts right of the unit pane (+0xCEC: the unit pane's right + 3).
    UiRect clip = {g_ox + 0x8E, g_oy + kStack.top + 4, g_ox + kStack.right - 4, g_oy + kStack.bottom - 4};
    for (int n = 0; v >= 0 && n < 9; v = Vehs[v].next_veh_id_stack, n++) {
        Buffer_set_clip(s, &clip);
        veh_draw(s, v, g_ox + 0xC8 + n * 64, g_oy + kStack.top + 0x6, -6, 0x1000, 0);
        Buffer_set_clip(s, nullptr);
    }
}

// The world map (WorldWin): one dot per tile (land / sea, territory), the view's outline.
void draw_world(Buffer* s) {
    UiRect r = {g_ox + kRight.left + 6, g_oy + kRight.top + 30, g_ox + kRight.right - 6, g_oy + kRight.bottom - 46};
    if (MapAreaX <= 0 || MapAreaY <= 0) return;
    int32_t w = r.right - r.left, h = r.bottom - r.top;
    int32_t sea = color(24, 48, 96), land = color(80, 64, 40);
    for (int32_t py = 0; py < h; py++) {
        int32_t y = py * MapAreaY / h;
        for (int32_t px = 0; px < w; px++) {
            int32_t x = px * MapAreaX / w;
            if ((x ^ y) & 1) x = (x + 1) % MapAreaX;
            MAP* sq = map_tile(x, y);
            if (!(GameState & 0x80) && !(sq->visibility & (1 << (CurrentPlayerFaction & 31)))) continue;
            int32_t c = (sq->climate & 0xE0) < 0x60 ? sea : land;
            int32_t owner = sq->owner;
            if (owner > 0 && owner < 8 && ((x + y) & 2)) c = FactionColorFill[owner];
            s->data[(size_t)(r.top + py) * s->stride + r.left + px] = (uint8_t)c;
        }
    }
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
    g_ox = (s->width - 1024) / 2;
    g_oy = s->height - 0x101;
    // Below the main view the screen has nothing else: black.
    int32_t view_h = console_view_height(s->height);
    for (int32_t y = view_h; y < s->height; y++) std::memset(s->data + (size_t)y * s->stride, 0, (size_t)s->width);
    for (int32_t y = 0; y < g_art.frame.height; y++) {
        int32_t sy = g_oy + y;
        if (sy < 0 || sy >= s->height) continue;
        const uint8_t* src = g_art.frame.data + (size_t)y * g_art.frame.stride;
        uint8_t* dst = s->data + (size_t)sy * s->stride + g_ox;
        for (int32_t x = 0; x < g_art.frame.width && g_ox + x < s->width; x++)
            if (src[x] != kKey) dst[x] = src[x];
    }
    restore_back(s, kLeft);
    restore_back(s, kRight);
    restore_back(s, kInfo);
    restore_back(s, kStack);
    // The panes' inner frames (StatusWin, InfoWin, the date and turn spots), measured on the
    // original's screen and drawn with the box sprite of the InfoWin frames (box 18).
    static const UiRect kBoxes[] = {{8, 61, 136, 250},  {140, 61, 242, 188}, {140, 192, 765, 250},
                                    {266, 73, 496, 170}, {497, 73, 725, 170}, {783, 212, 901, 248},
                                    {783, 61, 1018, 84}};
    for (const UiRect& r : kBoxes) {
        UiRect a = {g_ox + r.left, g_oy + r.top, g_ox + r.right, g_oy + r.bottom};
        if (BoxDef* b = box_def(18)) Buffer_box_sprite(s, &a, b);
    }
    draw_button(s, kMenuBtn, label_get(624));
    draw_button(s, kCommBtn, label_get(527));
    // MainInterface_redraw_complete: "TURN COMPLETE" while the turn waits for its end.
    {
        char up[64];
        std::snprintf(up, sizeof(up), "%s", label_get(26));
        for (char* p = up; *p; p++) *p = (char)crt_toupper((uint8_t)*p);
        UiRect spot = {0x30F, 0x3D, 0x3F8, 0x54};
        write_center(s, up, spot, 14, 1, 0x65);
    }
    char line[160];
    char year[64];
    year[0] = 0;
    say_year(year);
    std::snprintf(line, sizeof(line), "%s %s", label_get(0), year);
    write(s, line, 0x312, 0xDA, 12, 0, color(60, 148, 124));
    std::snprintf(line, sizeof(line), "%s: %d", label_get(36), Factions[CurrentPlayerFaction].energy_credits);
    write(s, line, 0x312, 0xE8, 12, 0, color(60, 148, 124));
    // The diplomacy mode (DiploPop) replaces the status panes while a commlink is open.
    if (diplowin_draw_console(s, g_ox, g_oy)) {
        draw_info_pane(s);
        draw_world(s);
        return;
    }
    int32_t v = mw(&MainConsole, kConVeh);
    int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
    if (MapWinInVehMode && v >= 0 && v < VehCount) {
        draw_unit_pane(s, v);
        draw_tile_pane(s, Vehs[v].x, Vehs[v].y);
        draw_stack(s, v);
    } else {
        draw_tile_pane(s, cx, cy);
        int32_t t = (cx >= 0 && cy >= 0 && cx < MapAreaX && cy < MapAreaY) ? veh_at(cx, cy) : -1;
        if (t >= 0 && (Vehs[t].faction_id == (uint32_t)CurrentPlayerFaction || (GameState & 0x80))) {
            draw_unit_pane(s, t);
            draw_stack(s, t);
        }
    }
    draw_info_pane(s);
    draw_world(s);
}

void THISCALL StatusWin_set_title(uint8_t*, const char*) { ui_invalidate(); }
