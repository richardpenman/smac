// The menu bar (AlphaMenu, ui 0x402F40..0x403B70, on the gfx Menu / PullDown classes) and its
// pull-downs, built as MainMenu_menu_init (0x45FB20) builds them: the sections of menu.txt
// (GAME, HQ, NETWORK, MAP, ACTION, TERRAFORM, SCENARIO, EDITOR, HELP; the first line is the
// title, each following line an item "text|shortcut", '&' marking the accelerator) with the
// command ids and separators of the original (table below, read from the asm) and the items it
// hides at init.
//
// The look from the asm (no screenshot of the original's menu could be taken: its XTEST Alt does not
// open it): AlphaMenu's column of buttons above the console (AlphaMenu_init / compute / on_redraw /
// adjust, menu2.pcx), the open menu's pull-down drawn by the reconstructed PullDown
// (PullDown_on_redraw with AlphaMenu's fill functions, pulldown.cpp) at the column's right; the
// item visibility of
// pfncMainMenuUpdateVisible (only the init's hidden items and the menus of other modes: Network in
// multiplayer, Scenario and Edit Map in the scenario editor). A chosen item runs its command id
// through Console_menu_proc (console_menu.cpp), as Menu_menu_event does.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/action.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/rules.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/colortables.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/popup.h"
#include "ui/pulldown.h"
#include "ui/sound.h"
#include "ui/win.h"

namespace {

struct Entry {
    const char* section;  // text_open of a section
    int32_t kind;         // 0 section, 1 title (menu index), 2 item (menu index, id), 3 separator,
                          // 4 the terraforming orders (Order[4..23] with Terraform's shortcuts)
    int32_t menu, id;
};
const Entry kMenuInit[] = {
{"GAME", 0, 0, 0},
{nullptr, 1, 0x0, 0},
{nullptr, 2, 0x0, 0x51},
{nullptr, 2, 0x0, 0x52},
{nullptr, 3, 0x0, 0},
{nullptr, 2, 0x0, 0x53},
{nullptr, 2, 0x0, 0x54},
{nullptr, 2, 0x0, 0x55},
{nullptr, 2, 0x0, 0x56},
{nullptr, 2, 0x0, 0x57},
{nullptr, 2, 0x0, 0x58},
{nullptr, 3, 0x0, 0},
{nullptr, 2, 0x0, 0x59},
{nullptr, 2, 0x0, 0x5a},
{nullptr, 3, 0x0, 0},
{nullptr, 2, 0x0, 0x5b},
{nullptr, 2, 0x0, 0x5c},
{nullptr, 2, 0x0, 0x5d},
{"HQ", 0, 0, 0},
{nullptr, 1, 0x1, 0},
{nullptr, 2, 0x1, 0x105},
{nullptr, 2, 0x1, 0x106},
{nullptr, 2, 0x1, 0x107},
{nullptr, 3, 0x1, 0},
{nullptr, 2, 0x1, 0x103},
{nullptr, 3, 0x1, 0},
{nullptr, 2, 0x1, 0x108},
{nullptr, 2, 0x1, 0x109},
{nullptr, 2, 0x1, 0x10a},
{nullptr, 3, 0x1, 0},
{nullptr, 2, 0x1, 0x10b},
{nullptr, 2, 0x1, 0x10c},
{nullptr, 2, 0x1, 0x10d},
{nullptr, 2, 0x1, 0x10e},
{nullptr, 3, 0x1, 0},
{nullptr, 2, 0x1, 0x10f},
{nullptr, 2, 0x1, 0x110},
{nullptr, 2, 0x1, 0x111},
{nullptr, 2, 0x1, 0x113},
{nullptr, 3, 0x1, 0},
{nullptr, 2, 0x1, 0x114},
{nullptr, 2, 0x1, 0x115},
{"NETWORK", 0, 0, 0},
{nullptr, 1, 0x8, 0},
{nullptr, 2, 0x8, 0x800},
{nullptr, 2, 0x8, 0x801},
{nullptr, 2, 0x8, 0x802},
{nullptr, 2, 0x8, 0x803},
{nullptr, 2, 0x8, 0x804},
{nullptr, 3, 0x8, 0},
{nullptr, 2, 0x8, 0x805},
{nullptr, 3, 0x8, 0},
{nullptr, 2, 0x8, 0x806},
{nullptr, 2, 0x8, 0x807},
{nullptr, 2, 0x8, 0x808},
{"MAP", 0, 0, 0},
{nullptr, 1, 0x2, 0},
{nullptr, 2, 0x2, 0x200},
{nullptr, 2, 0x2, 0x201},
{nullptr, 3, 0x2, 0},
{nullptr, 2, 0x2, 0x203},
{nullptr, 2, 0x2, 0x204},
{nullptr, 2, 0x2, 0x205},
{nullptr, 2, 0x2, 0x206},
{nullptr, 2, 0x2, 0x207},
{nullptr, 2, 0x2, 0x208},
{nullptr, 3, 0x2, 0},
{nullptr, 2, 0x2, 0x209},
{nullptr, 2, 0x2, 0x20a},
{nullptr, 2, 0x2, 0x20b},
{nullptr, 2, 0x2, 0x20c},
{nullptr, 2, 0x2, 0x215},
{nullptr, 3, 0x2, 0},
{nullptr, 2, 0x2, 0x20f},
{nullptr, 2, 0x2, 0x210},
{nullptr, 2, 0x2, 0x211},
{nullptr, 2, 0x2, 0x212},
{nullptr, 2, 0x2, 0x213},
{nullptr, 3, 0x2, 0},
{nullptr, 2, 0x2, 0x216},
{nullptr, 2, 0x2, 0x217},
{nullptr, 2, 0x2, 0x214},
{"ACTION", 0, 0, 0},
{nullptr, 1, 0x3, 0},
{nullptr, 2, 0x3, 0x300},
{nullptr, 2, 0x3, 0x301},
{nullptr, 2, 0x3, 0x302},
{nullptr, 3, 0x3, 0},
{nullptr, 2, 0x3, 0x305},
{nullptr, 2, 0x3, 0x309},
{nullptr, 2, 0x3, 0x30a},
{nullptr, 2, 0x3, 0x310},
{nullptr, 2, 0x3, 0x308},
{nullptr, 2, 0x3, 0x311},
{nullptr, 2, 0x3, 0x312},
{nullptr, 3, 0x3, 0},
{nullptr, 2, 0x3, 0x314},
{nullptr, 2, 0x3, 0x31b},
{nullptr, 2, 0x3, 0x324},
{nullptr, 2, 0x3, 0x306},
{nullptr, 2, 0x3, 0x304},
{nullptr, 3, 0x3, 0},
{nullptr, 2, 0x3, 0x318},
{nullptr, 2, 0x3, 0x319},
{nullptr, 2, 0x3, 0x31a},
{nullptr, 2, 0x3, 0x317},
{nullptr, 2, 0x3, 0x316},
{nullptr, 3, 0x3, 0},
{nullptr, 2, 0x3, 0x313},
{nullptr, 2, 0x3, 0x31c},
{nullptr, 2, 0x3, 0x31d},
{nullptr, 2, 0x3, 0x31e},
{nullptr, 2, 0x3, 0x31f},
{nullptr, 2, 0x3, 0x320},
{nullptr, 3, 0x3, 0},
{nullptr, 2, 0x3, 0x321},
{nullptr, 2, 0x3, 0x307},
{nullptr, 2, 0x3, 0x322},
{nullptr, 2, 0x3, 0x315},
{nullptr, 2, 0x3, 0x323},
{"TERRAFORM", 0, 0, 0},
{nullptr, 1, 0x9, 0},
{nullptr, 4, 0x9, 0x900},
{nullptr, 3, 0x9, 0},
{nullptr, 2, 0x9, 0x9f8},
{nullptr, 2, 0x9, 0x9f9},
{nullptr, 3, 0x9, 0},
{nullptr, 2, 0x9, 0x9f0},
{nullptr, 2, 0x9, 0x9f1},
{nullptr, 3, 0x9, 0},
{nullptr, 2, 0x9, 0x9f3},
{nullptr, 2, 0x9, 0x9f4},
{nullptr, 2, 0x9, 0x9f5},
{nullptr, 2, 0x9, 0x9f6},
{nullptr, 2, 0x9, 0x9f7},
{nullptr, 2, 0x9, 0x9f2},
{"SCENARIO", 0, 0, 0},
{nullptr, 1, 0x6, 0},
{nullptr, 2, 0x6, 0x600},
{nullptr, 2, 0x6, 0x601},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x602},
{nullptr, 2, 0x6, 0x603},
{nullptr, 2, 0x6, 0x604},
{nullptr, 2, 0x6, 0x605},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x606},
{nullptr, 2, 0x6, 0x607},
{nullptr, 2, 0x6, 0x608},
{nullptr, 2, 0x6, 0x609},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x60a},
{nullptr, 2, 0x6, 0x60b},
{nullptr, 2, 0x6, 0x60c},
{nullptr, 2, 0x6, 0x60d},
{nullptr, 2, 0x6, 0x60e},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x60f},
{nullptr, 2, 0x6, 0x610},
{nullptr, 2, 0x6, 0x611},
{nullptr, 2, 0x6, 0x612},
{nullptr, 2, 0x6, 0x613},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x614},
{nullptr, 2, 0x6, 0x615},
{nullptr, 2, 0x6, 0x616},
{nullptr, 2, 0x6, 0x617},
{nullptr, 3, 0x6, 0},
{nullptr, 2, 0x6, 0x618},
{nullptr, 2, 0x6, 0x619},
{nullptr, 2, 0x6, 0x61a},
{nullptr, 2, 0x6, 0x61b},
{"EDITOR", 0, 0, 0},
{nullptr, 1, 0xa, 0},
{nullptr, 2, 0xa, 0xa00},
{nullptr, 2, 0xa, 0xa01},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa10},
{nullptr, 2, 0xa, 0xa11},
{nullptr, 2, 0xa, 0xa12},
{nullptr, 2, 0xa, 0xa13},
{nullptr, 2, 0xa, 0xa14},
{nullptr, 2, 0xa, 0xa15},
{nullptr, 2, 0xa, 0xa16},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa17},
{nullptr, 2, 0xa, 0xa18},
{nullptr, 2, 0xa, 0xa19},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa20},
{nullptr, 2, 0xa, 0xa21},
{nullptr, 2, 0xa, 0xa22},
{nullptr, 2, 0xa, 0xa24},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa48},
{nullptr, 2, 0xa, 0xa30},
{nullptr, 2, 0xa, 0xa31},
{nullptr, 2, 0xa, 0xa32},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa36},
{nullptr, 2, 0xa, 0xa37},
{nullptr, 2, 0xa, 0xa40},
{nullptr, 2, 0xa, 0xa41},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa50},
{nullptr, 2, 0xa, 0xa51},
{nullptr, 2, 0xa, 0xa52},
{nullptr, 2, 0xa, 0xa53},
{nullptr, 3, 0xa, 0},
{nullptr, 2, 0xa, 0xa54},
{"HELP", 0, 0, 0},
{nullptr, 1, 0x7, 0},
{nullptr, 2, 0x7, 0x701},
{nullptr, 2, 0x7, 0x702},
{nullptr, 2, 0x7, 0x703},
{nullptr, 3, 0x7, 0},
{nullptr, 2, 0x7, 0x704},
{nullptr, 2, 0x7, 0x70b},
{nullptr, 2, 0x7, 0x70c},
{nullptr, 3, 0x7, 0},
{nullptr, 2, 0x7, 0x70d},
{nullptr, 2, 0x7, 0x70e},
{nullptr, 3, 0x7, 0},
{nullptr, 2, 0x7, 0x70a},
{nullptr, 2, 0x7, 0x705},
{nullptr, 2, 0x7, 0x706},
{nullptr, 2, 0x7, 0x707},
{nullptr, 2, 0x7, 0x708},
{nullptr, 2, 0x7, 0x709},
{nullptr, 3, 0x7, 0},
{nullptr, 2, 0x7, 0x720},
{nullptr, 2, 0x7, 0x721},
{nullptr, 2, 0x7, 0x722},
{nullptr, 2, 0x7, 0x723},
{nullptr, 2, 0x7, 0x724},
{nullptr, 2, 0x7, 0x725},
{nullptr, 3, 0x7, 0},
{nullptr, 2, 0x7, 0x750},
{nullptr, 2, 0x7, 0x751}
};
// Items MainMenu_menu_init hides (Menu_hide_menu_item) or disables at the end.
const int32_t kHidden[] = {0x106, 0x107, 0x301, 0x309, 0x30A, 0x310, 0x306, 0x304, 0x5A};

struct Item {
    std::string text, shortcut, raw;  // raw: the menu.txt line ('&', "|shortcut")
    int32_t id = 0, key = 0;
    bool separator = false, hidden = false, checked = false, disabled = false;
    char accel = 0;
};
struct Menu {
    std::string section, title;
    int32_t index = 0;
    char accel = 0;
    std::vector<Item> items;
    int32_t x = 0, w = 0;  // AlphaMenu's spot: its button (x, y, w, h) on the screen (w 0: not shown)
    int32_t y = 0, h = 0;
};
std::vector<Menu> g_menus;
bool g_built = false;
bool g_open = false;   // the bar is shown
int32_t g_cur = -1;    // open pull-down (-1: the bar only)
int32_t g_sel = -1;    // selected item of the pull-down

// "Ctrl+Shift+F1" -> virtual key with modifier bits; 0 when it is no key.
int32_t parse_key(std::string s) {
    int32_t mods = 0;
    for (;;) {
        if (s.rfind("Ctrl+", 0) == 0) mods |= 0x20000, s = s.substr(5);
        else if (s.rfind("Shift+", 0) == 0) mods |= 0x10000, s = s.substr(6);
        else if (s.rfind("Alt+", 0) == 0) mods |= 0x40000, s = s.substr(4);
        else break;
    }
    if (s.size() >= 2 && s[0] == 'F' && s[1] >= '0' && s[1] <= '9') return (VKEY_F1 + std::atoi(s.c_str() + 1) - 1) | mods;
    if (s == "ENTER") return VKEY_RETURN | mods;
    if (s == "SPACE" || s == "Space") return VKEY_SPACE | mods;
    if (s == "BKSP" || s == "BACKSPACE" || s == "Backspace") return VKEY_BACK | mods;
    if (s == "SCROLL LOCK") return 0x91 | mods;
    if (s.size() != 1) return 0;
    char c = s[0];
    if (c >= 'A' && c <= 'Z') return c | mods;
    if (c >= '0' && c <= '9') return c | mods;
    static const char kPlain[] = ";=,-./`[\\]'";
    static const char kShift[] = ":+<_>?~{|}\"";
    static const int32_t kVk[] = {0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE};
    if (const char* p = std::strchr(kPlain, c)) return kVk[p - kPlain] | mods;
    if (const char* p = std::strchr(kShift, c)) return kVk[p - kShift] | mods | 0x10000;
    if (c == '*') return '8' | mods | 0x10000;
    return 0;
}

std::string strip_amp(const std::string& s, char* accel) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&' && i + 1 < s.size() && s[i + 1] != ' ') {  // "& " stays (PullDown_add_item)
            if (accel) *accel = (char)std::toupper((uint8_t)s[i + 1]);
            continue;
        }
        out += s[i];
    }
    return out;
}

void build() {
    if (g_built) return;
    g_built = true;
    Menu* cur = nullptr;
    for (const Entry& e : kMenuInit) {
        switch (e.kind) {
        case 0:
            g_menus.emplace_back();
            cur = &g_menus.back();
            cur->section = e.section;
            if (text_open("menu", e.section)) cur = nullptr;
            break;
        case 1:
            if (cur) {
                cur->index = e.menu;
                cur->title = strip_amp(text_get(), &cur->accel);
            }
            break;
        case 2:
            if (cur) {
                Item it;
                std::string line = text_get();
                it.raw = line;
                size_t bar = line.find('|');
                it.text = strip_amp(line.substr(0, bar), &it.accel);
                if (bar != std::string::npos) it.shortcut = line.substr(bar + 1);
                it.id = e.id;
                it.key = parse_key(it.shortcut);
                for (int32_t h : kHidden) it.hidden |= h == e.id;
                cur->items.push_back(it);
            }
            break;
        case 4:
            if (cur) {
                for (int32_t i = 0; i < 20; i++) {
                    Item it;
                    it.text = strip_amp(Order[i + 4].order, &it.accel);
                    it.shortcut = Terraform[i].shortcuts ? Terraform[i].shortcuts : "";
                    it.raw = std::string(Order[i + 4].order) + (it.shortcut.empty() ? "" : "|" + it.shortcut);
                    it.id = e.id + i;
                    it.key = parse_key(it.shortcut);
                    cur->items.push_back(it);
                    if (i == 4 || i == 11 || i == 14) {
                        Item sep;
                        sep.separator = true;
                        cur->items.push_back(sep);
                    }
                }
            }
            break;
        case 3:
            if (cur) {
                Item it;
                it.separator = true;
                cur->items.push_back(it);
            }
            break;
        default: break;
        }
    }
}

bool menu_shown(const Menu& m) {
    if (m.section == "NETWORK") return MultiplayerActive != 0;
    if (m.section == "SCENARIO" || m.section == "EDITOR") return (GameState & STATE_SCENARIO_EDITOR) != 0;
    return true;
}

int32_t color(int32_t r, int32_t g, int32_t b) { return Palette_get_nearest_palette_index(&GamePalette, r, g, b, 1); }

// ---- AlphaMenu (0x402F40..0x403B70): the menus as a column of buttons above the console's MENU
// button (AlphaMenu_init: menu2.pcx / menu.pcx at 800: the top 0x8B x 6, the buttons 0x8B x 0x12 in
// three states at y 8, 0x1B, 0x2E; GraphicWin 0x51 x 0x8F, AlphaMenu_adjust: as tall as the top and
// a button per menu, at the left of the 1024-wide layout, its bottom on the console's top).

struct AlphaArt {
    Sprite top, button[3];  // +0x10EC; +0x1118 normal, +0x1144 under the mouse, +0x1170 open
    bool loaded = false;
} g_art;
// The menus in AlphaMenu's order (MainMenu_menu_init's pfncMainMenuAddBaseMenu calls: GAME, HQ,
// NETWORK, MAP, ACTION, TERRAFORM, SCENARIO, EDITOR, HELP; HELP right-aligned: from the bottom).
const int32_t kAddOrder[9] = {0, 1, 8, 2, 3, 9, 6, 10, 7};
int32_t g_hover = -1;  // DAT_00696E3C: the button under the mouse

void load_art() {
    if (g_art.loaded) return;
    g_art.loaded = true;
    bool small = ScreenWidth == 800;
    Buffer b;
    Buffer_ctor(&b);
    if (Buffer_load_pcx(&b, small ? "menu.pcx" : "menu2.pcx", nullptr, 10, 0xEC) != 0) return;
    int32_t w = small ? 0x8A : 0x8B;
    pfncSpriteFromCanvasRectTrans(&g_art.top, &b, 9, 1, 1, w, 6, 0);
    for (int32_t k = 0; k < 3; k++) pfncSpriteFromCanvasRectTrans(&g_art.button[k], &b, 9, 1, 8 + 0x13 * k, w, 0x12, 0);
}

int32_t column_x() { return ScreenWidth == 800 ? 0 : (ScreenWidth - 0x400) / 2; }
int32_t column_h() { return g_art.top.full_height + g_art.button[0].full_height * (int32_t)g_menus.size(); }
// AlphaMenu_adjust: the column's top (its bottom on the main interface's top).
int32_t column_y() {
    const UiRect& pos = mi_rect(kMiPos);
    return (pos.top - pos.bottom) - column_h() + ScreenHeight;
}

// AlphaMenu_compute: the shown menus' buttons from the top (right-aligned ones from the bottom).
void layout(Buffer* s) {
    load_art();
    int32_t step = g_art.button[0].full_height, n = (int32_t)g_menus.size();
    int32_t top = g_art.top.full_height, bottom = (n - 1) * step + top;
    int32_t x0 = column_x(), y0 = column_y();
    for (int32_t k = 0; k < 9 && k < n; k++) {
        Menu* m = nullptr;
        for (Menu& mm : g_menus)
            if (mm.index == kAddOrder[k]) m = &mm;
        if (!m) continue;
        if (!menu_shown(*m)) {
            m->w = 0;
            continue;
        }
        m->x = x0;
        m->w = g_art.button[0].full_width;
        m->h = step;
        if (m->section == "HELP") {
            m->y = y0 + bottom;
            bottom -= step;
        } else {
            m->y = y0 + top;
            top += step;
        }
    }
    (void)s;
}

// The open menu's pull-down (AlphaMenu_on_adjust_pulldown_pos: at the column's right, level with
// its button), drawn as the reconstructed PullDown (pulldown.cpp).
PullDown g_pd;
void build_pulldown(const Menu& m) {
    PullDown_init(&g_pd, true);
    for (const Item& it : m.items) {
        if (it.separator) PullDown_add_separator(&g_pd, 99);
        else PullDown_add_item(&g_pd, it.id, it.raw.c_str());
        if (it.hidden) PullDown_hide_item(&g_pd, it.separator ? 99 : it.id);
        if (it.disabled && !it.separator) PullDown_disable_item(&g_pd, it.id);
        if (it.checked) PullDown_check_item(&g_pd, it.id);
    }
    pulldown_place(&g_pd, m.x + m.w, m.y);
}
UiRect pulldown_rect(const Menu& m) {
    build_pulldown(m);
    return {g_pd.x, g_pd.y, g_pd.x + g_pd.w, g_pd.y + g_pd.h};
}

// The visible item at screen y in the pull-down, -1 none.
int32_t item_at(const Menu& m, int32_t y) {
    build_pulldown(m);
    return pulldown_row_at(&g_pd, y - g_pd.y);
}

void close_menus() {
    g_open = false;
    g_cur = -1;
    g_sel = -1;
    ui_invalidate();
}

void choose(const Item& it) {
    if (it.disabled) return;  // (Menu_menu_event: a disabled item does nothing)
    close_menus();
    std::fprintf(stderr, "menu item %03X %s\n", it.id, it.text.c_str());
    Console_menu_proc(&MainConsole, it.id);  // Menu_menu_event -> Console_menu_daemon
}

// AlphaMenu_show (0x403680): the column shown with fx\amenu2.wav (the interface sounds) and, with
// the "animate interface" preference outside multiplayer, slid in: its picture drawn six times in
// its rectangle, from y = bottom - top piece's height up by a fifth of its height per frame
// (DAT_00691AF8 ms apart). Unattended runs skip the frames.
void alphamenu_show() {
    if (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) Wave_play_2(mem::host(0x6A7090));
    std::fprintf(
        stderr, "alphamenu show (sound %d, slide %d)\n", (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) ? 1 : 0,
        (GamePreferences & PREF_AV_SLIDING_WINDOWS) && !MultiplayerActive && !g_ui_exit_when_script_done ? 1 : 0);
    Buffer* scr = ui_screen_canvas();
    if (!(GamePreferences & PREF_AV_SLIDING_WINDOWS) || MultiplayerActive || g_ui_exit_when_script_done || !scr ||
        !scr->data)
        return;
    load_art();
    // The column's picture: drawn alone into a screen-sized canvas of colour 9.
    Buffer pic;
    Buffer_ctor(&pic);
    Buffer_init(&pic, scr->width, scr->height, 8, 0);
    UiRect all{0, 0, pic.width, pic.height};
    Buffer_set_clip(&pic, &all);
    Buffer_fill_3(&pic, &all, 9);
    int32_t keep = g_cur;
    g_cur = -1;
    console_menu_draw(&pic);
    g_cur = keep;
    ui_present();
    std::vector<uint8_t> screen(scr->data, scr->data + (size_t)scr->stride * scr->height);
    const int32_t x0 = column_x(), top = column_y(), h = column_h(), bottom = top + h;
    const int32_t w = g_art.top.full_width;
    const int32_t step = h / 5;
    int32_t y = bottom - g_art.top.full_height;
    std::fprintf(stderr, "alphamenu slide %dx%d at %d,%d\n", w, h, x0, top);
    for (int32_t f = 0; f < 6; f++) {
        uint32_t t = platform::ticks_ms();
        std::memcpy(scr->data, screen.data(), screen.size());
        for (int32_t j = top; j < bottom && j < scr->height; j++) {
            int32_t sj = j - y + top;  // the picture's row shown at screen row j
            if (j < 0 || sj < top || sj >= bottom) continue;
            const uint8_t* src = pic.data + (size_t)sj * pic.stride;
            uint8_t* dst = scr->data + (size_t)j * scr->stride;
            for (int32_t i = x0; i < x0 + w && i < scr->width; i++)
                if (src[i] != 9) dst[i] = src[i];
        }
        platform::video_present(scr->data, scr->stride, scr->width, scr->height,
                                reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
        y -= step;
        while ((int32_t)(platform::ticks_ms() - t) < *reinterpret_cast<int32_t*>(mem::host(0x691AF8))) platform::delay_ms(1);
    }
    std::memcpy(scr->data, screen.data(), screen.size());
}

void open_menu(int32_t i) {
    if (!g_open) {
        g_open = true;
        alphamenu_show();
    }
    g_open = true;
    g_cur = i;
    g_sel = -1;
    layout(nullptr);
    if (i >= 0) {
        const Menu& m = g_menus[i];
        std::fprintf(stderr, "menu column at %d,%d (%dx%d): %s at %d,%d\n", column_x(), column_y(),
                     g_art.top.full_width, column_h(), m.title.c_str(), m.x, m.y);
    }
    if (i >= 0) {
        for (size_t k = 0; k < g_menus[i].items.size(); k++) {
            if (!g_menus[i].items[k].hidden && !g_menus[i].items[k].separator) {
                g_sel = (int32_t)k;
                break;
            }
        }
    }
    ui_invalidate();
}

int32_t step_menu(int32_t i, int32_t d) {
    int32_t n = (int32_t)g_menus.size();
    for (int32_t k = 0; k < n; k++) {
        i = ((i + d) % n + n) % n;
        if (menu_shown(g_menus[i])) return i;
    }
    return i;
}

}  // namespace

void AlphaMenu_hide_main() {
    if (g_open) close_menus();
}

bool console_menu_key(int32_t key) {
    build();
    int32_t vk = key & 0xFFFF;
    if (Win_is_visible(&BaseWin) && !g_open) return false;
    if (!g_open) {
        if (vk == VKEY_MENU || (key & 0x40000)) {
            // Alt opens the bar; Alt+letter the menu with that accelerator.
            if (vk == VKEY_MENU) {
                open_menu(-1);
                return true;
            }
            for (size_t i = 0; i < g_menus.size(); i++) {
                if (menu_shown(g_menus[i]) && g_menus[i].accel == vk) {
                    open_menu((int32_t)i);
                    return true;
                }
            }
        }
        return false;
    }
    if (vk == VKEY_ESCAPE || vk == VKEY_MENU) {
        close_menus();
        return true;
    }
    if (g_cur < 0) {
        int32_t first = step_menu(-1, 1);
        if (vk == VKEY_RETURN || vk == VKEY_DOWN) open_menu(first);
        else if (vk == VKEY_RIGHT) open_menu(first);
        else {
            for (size_t i = 0; i < g_menus.size(); i++)
                if (menu_shown(g_menus[i]) && g_menus[i].accel == vk) open_menu((int32_t)i);
        }
        return true;
    }
    Menu& m = g_menus[g_cur];
    int32_t n = (int32_t)m.items.size();
    auto move = [&](int32_t d) {
        for (int32_t k = 0; k < n; k++) {
            g_sel = ((g_sel + d) % n + n) % n;
            if (!m.items[g_sel].hidden && !m.items[g_sel].separator) break;
        }
        ui_invalidate();
    };
    switch (vk) {
    case VKEY_DOWN: move(1); break;
    case VKEY_UP: move(-1); break;
    case VKEY_LEFT: open_menu(step_menu(g_cur, -1)); break;
    case VKEY_RIGHT: open_menu(step_menu(g_cur, 1)); break;
    case VKEY_RETURN:
        if (g_sel >= 0) choose(m.items[g_sel]);
        break;
    default:
        for (const Item& it : m.items) {
            if (!it.hidden && !it.separator && it.accel == vk) {
                choose(it);
                break;
            }
        }
        break;
    }
    return true;
}

bool console_menu_mouse(int32_t type, int32_t x, int32_t y) {
    build();
    if (!g_open) return false;
    if (type == UiInput::MOVE) {
        int32_t hover = -1;  // AlphaMenu_on_mouse_move: the button under the mouse
        for (size_t i = 0; i < g_menus.size(); i++) {
            const Menu& m = g_menus[i];
            if (m.w && x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) hover = (int32_t)i;
        }
        if (hover != g_hover) {
            g_hover = hover;
            ui_invalidate();
        }
        if (g_cur >= 0) {
            UiRect r = pulldown_rect(g_menus[g_cur]);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
                int32_t i = item_at(g_menus[g_cur], y);
                if (i != g_sel) {
                    g_sel = i;
                    ui_invalidate();
                }
            }
        }
        return true;
    }
    if (type != UiInput::LDOWN && type != UiInput::LUP) return true;
    for (size_t i = 0; i < g_menus.size(); i++) {  // the column's buttons (AlphaMenu's spots)
        const Menu& m = g_menus[i];
        if (m.w && x >= m.x && x < m.x + m.w && y >= m.y && y < m.y + m.h) {
            if (type == UiInput::LDOWN) open_menu((int32_t)i);
            return true;
        }
    }
    if (g_cur >= 0) {
        UiRect r = pulldown_rect(g_menus[g_cur]);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            if (type == UiInput::LUP) {
                int32_t i = item_at(g_menus[g_cur], y);
                if (i >= 0) choose(g_menus[g_cur].items[i]);
            }
            return true;
        }
    }
    if (type == UiInput::LDOWN) close_menus();
    return true;
}

void console_menu_check(int32_t index, int32_t id, bool on) {
    build();
    for (Menu& m : g_menus)
        if (m.index == index)
            for (Item& it : m.items)
                if (it.id == id && !it.separator) it.checked = on;
    ui_invalidate();
}

void console_menu_popup(int32_t index, int32_t x, int32_t y) {
    build();
    for (const Menu& m : g_menus) {
        if (m.index != index || m.title.empty()) continue;
        // Menu_id_to_index + PullDown_show(x, y, 0) of the menu's pull-down; the chosen item goes to
        // Menu_menu_event (the console's menu procedure).
        PullDown pd;
        PullDown_init(&pd, true);
        for (const Item& it : m.items) {
            if (it.separator) PullDown_add_separator(&pd, 99);
            else PullDown_add_item(&pd, it.id, it.raw.c_str());
            if (it.hidden) PullDown_hide_item(&pd, it.separator ? 99 : it.id);
            if (it.disabled && !it.separator) PullDown_disable_item(&pd, it.id);
            if (it.checked) PullDown_check_item(&pd, it.id);
        }
        int32_t sel = PullDown_show(&pd, x, y);
        if (sel >= 0 && sel < (int32_t)m.items.size() && PullDown_get_selected(&pd) >= 0) choose(m.items[sel]);
        return;
    }
}

void console_menu_draw(Buffer* s) {
    if (!g_open) return;
    build();
    layout(s);
    // AlphaMenu_on_redraw: the top, a button per menu (open: state 2, under the mouse: 1), the
    // shown menus' titles in capitals 8 pixels inside their buttons, in the text colours of
    // text.pcx (0x8C6BD4.. : open, normal; disabled).
    const int32_t x0 = column_x(), y0 = column_y();
    BlitDestScale = BlitSourceScale = 1;
    if (g_art.top.width) pfncSpriteStretchCopyToCanvas(&g_art.top, s, g_art.top.key, x0, y0);
    int32_t y = y0 + g_art.top.full_height;
    std::vector<const Menu*> slots(g_menus.size(), nullptr);
    for (size_t i = 0; i < g_menus.size(); i++) {
        const Menu& m = g_menus[i];
        if (m.w) slots[(m.y - y0 - g_art.top.full_height) / std::max(1, m.h)] = &m;
    }
    for (size_t k = 0; k < g_menus.size(); k++) {
        const Menu* m = slots[k];
        int32_t idx = m ? (int32_t)(m - g_menus.data()) : -1;
        int32_t st = (idx >= 0 && idx == g_cur) ? 2 : (idx >= 0 && idx == g_hover ? 1 : 0);
        Sprite* b = &g_art.button[st];
        if (b->width) pfncSpriteStretchCopyToCanvas(b, s, b->key, x0, y);
        y += g_art.button[0].full_height;
    }
    Font* f = find_font(ScreenWidth == 800 ? 12 : 16, 1);  // DAT_00945764
    Buffer_set_font(s, f, nullptr, nullptr, nullptr);
    for (size_t i = 0; i < g_menus.size(); i++) {
        const Menu& m = g_menus[i];
        if (!m.w) continue;
        bool open = (int32_t)i == g_cur;
        int32_t c = text_color(open ? 0x8C6BD4 : 0x8C6BDC), sh = text_color(open ? 0x8C6BD8 : 0x8C6BE0);
        Buffer_set_text_color(s, c, sh, 1, 1);
        std::string t = m.title;
        for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
        UiRect r{m.x + 8, m.y, m.x + m.w - 8, m.y + m.h};
        Buffer_write_l_4(s, t.c_str(), &r, (int32_t)t.size());
    }
    if (g_cur < 0) return;
    build_pulldown(g_menus[g_cur]);
    g_pd.selected = g_sel;
    pulldown_render(&g_pd, s);
}

// ---- pfncMainMenuUpdateVisible (0x460DD0) ----

namespace {
Item* menu_item(int32_t index, int32_t id) {
    build();
    for (Menu& m : g_menus)
        if (m.index == index)
            for (Item& it : m.items)
                if (!it.separator && it.id == id) return &it;
    return nullptr;
}
void show_item(int32_t index, int32_t id) {  // Menu_show_menu_item
    if (Item* it = menu_item(index, id)) it->hidden = false;
}
void hide_item(int32_t index, int32_t id) {  // Menu_hide_menu_item
    if (Item* it = menu_item(index, id)) it->hidden = true;
}
void enable_item(int32_t index, int32_t id) {  // Menu_enable_menu_item
    if (Item* it = menu_item(index, id)) it->disabled = false;
}
void disable_item(int32_t index, int32_t id) {  // Menu_disable_menu_item
    if (Item* it = menu_item(index, id)) it->disabled = true;
}
void rename_item(int32_t index, int32_t id, const std::string& raw) {  // pfncMainMenuRenameMenuItem
    if (Item* it = menu_item(index, id)) {
        it->raw = raw;
        size_t bar = raw.find('|');
        it->text = strip_amp(raw.substr(0, bar), &it->accel);
        it->shortcut = bar != std::string::npos ? raw.substr(bar + 1) : "";
    }
}
void show_if(int32_t index, int32_t id, bool on) { on ? show_item(index, id) : hide_item(index, id); }
void enable_if(int32_t index, int32_t id, bool on) { on ? enable_item(index, id) : disable_item(index, id); }
}  // namespace

// The menus' items for the game's state and the unit veh_id (-1 none): the items of the
// "detailed menus" preference (GameMorePreferences 0x200000), the save / iron man items, the
// scenario objectives, the economic victory's items, the view mode's, the unit's orders (Action:
// enabled with a unit; its abilities, its chassis and weapon, its tile: base, territory), the
// terraforming orders (renamed with their turns for a terraformer, shown when available), the
// editor's items (enabled in the scenario editor). (Multiplayer's items as the single player's.)
void THISCALL pfncMainMenuUpdateVisible(uint8_t*, int32_t veh) {
    build();
    bool detail = GameMorePreferences & MPREF_ADV_DETAIL_MAIN_MENUS;
    show_if(0, detail ? 0x52 : 0x51, true);
    hide_item(0, detail ? 0x51 : 0x52);
    if (!MultiplayerActive && !PbemActive && !(GameRules & RULES_IRONMAN)) {
        show_item(0, 0x5C);
        show_item(0, 0x5A);
    } else {
        if (!(!MultiplayerActive && !PbemActive)) hide_item(0, 0x5C);
        show_if(0, 0x5A, GameState & STATE_DEBUG_MODE);
    }
    static const int32_t kDetail2[] = {0x205, 0x206, 0x207, 0x208, 0x20C, 0x215, 0x211, 0x212};
    static const int32_t kDetail3[] = {0x31B, 0x324, 0x306, 0x304, 0x319, 0x31A, 0x317, 799, 800, 0x307, 0x322};
    static const int32_t kDetail9[] = {0x9F8, 0x9F9, 0x9F6, 0x9F5, 0x9F4, 0x9F3, 0x9F1, 0x9F0, 5, 6, 7};
    if (detail) {
        for (int32_t id : kDetail2) show_item(2, id);
        for (int32_t id : kDetail3) show_item(3, id);
        for (int32_t id : kDetail9) show_item(9, id);
    }
    show_if(1, 0x115,
            (GameState & STATE_IS_SCENARIO) || ObjectiveReqVictory < 1000 || ObjectivesSuddenDeathVictory < 1000);
    show_item(8, 0x807);  // (multiplayer: 0x808 during the others' turns)
    hide_item(8, 0x808);
    show_if(1, 0x105,
            (GameState & (STATE_SCENARIO_EDITOR | STATE_OMNISCIENT_VIEW)) ||
                !(GameMoreRules & MRULES_NO_SOCIAL_ENGINEERING));
    show_item(1, (GameRules & RULES_BLIND_RESEARCH) ? 0x106 : 0x107);
    hide_item(1, (GameRules & RULES_BLIND_RESEARCH) ? 0x107 : 0x106);
    bool econ =
        (has_tech(Rules.tech_preq_economic_victory, CurrentPlayerFaction) && (GameRules & RULES_VICTORY_ECONOMIC)) ||
        ((GameState & STATE_DEBUG_MODE) && (GameState & STATE_SCENARIO_EDITOR));
    show_if(1, 0x114, econ);
    show_if(1, 9, econ);
    show_item(2, MapWinInVehMode ? 0x201 : 0x200);
    hide_item(2, MapWinInVehMode ? 0x200 : 0x201);
    if (!MapWinInVehMode || veh < 0) {
        for (int32_t id = 0x300; id < 0x325; id++) enable_if(3, id, id == 0x313);
    } else {
        for (int32_t id = 0x300; id < 0x325; id++) enable_item(3, id);
        const VEH& v = Vehs[veh];
        int32_t u = v.unit_id;
        const UNIT& un = Units[u];
        int32_t triad = Chassis[un.chassis_id].triad;
        int32_t offense = Weapon[un.weapon_id].offense_value;
        MAP* sq = map_tile(v.x, v.y);
        show_if(3, 0x309, has_abil(u, 0x10));
        int32_t ter = whose_territory(CurrentPlayerFaction, v.x, v.y, nullptr, 0);
        uint32_t ds = ter >= 0 ? Factions[CurrentPlayerFaction].diplo_status[ter] : 0;
        bool probe_out = (ter < 0 || ter == CurrentPlayerFaction || !(ds & 8) || (ds & 0x10)) &&
                         (u > 0x3F || ((int8_t)Weapon[un.weapon_id].offense_value >= 0 && u != 0xF));
        show_if(3, 799, !probe_out);
        show_if(3, 0x30A, Chassis[un.chassis_id].missile && (un.weapon_id == 0x18 || un.weapon_id == 0x19));
        int32_t b = base_at(v.x, v.y);
        show_if(3, 0x310, b >= 0 && (Bases[b].facilities_built[0x21 >> 3] & (1 << (0x21 & 7))));
        show_if(3, 0x31D, veh_cargo(veh) || has_abil(u, 0x80));
        bool air_combat = triad == 2 && offense != 0;
        show_if(3, 0x306, air_combat);
        show_if(3, 0x304, air_combat);
        show_if(3, 0x305, can_arty(u, 1));
        show_if(3, 0x308, un.plan == 10);
        enable_if(3, 0x308, un.plan == 10);
        bool base_tile = (sq->items & 1) && (sq->val2 & 0xF) <= 7;
        int32_t owner = base_tile ? (sq->val2 & 0xF) : -1;
        if (un.plan == 8) {
            enable_item(3, 0x300);
            enable_item(3, 0x301);
            show_item(3, 2);
            show_if(3, 0x301, base_tile);
            show_if(3, 0x300, !base_tile);
            hide_item(3, 0x302);
        } else {
            disable_item(3, 0x300);
            disable_item(3, 0x301);
            hide_item(3, 0x300);
            hide_item(3, 0x301);
            bool attack_base = offense != 0 && owner == (int32_t)v.faction_id && triad != 2;
            show_if(3, 0x302, attack_base);
            enable_if(3, 0x302, attack_base);
            hide_item(3, 2);
        }
        show_if(3, 800, offense != 0);
        show_if(3, 0x311, offense != 0);
        int32_t sea = (triad == 1 || (triad == 2 && (sq->climate & 0xE0) < 0x60)) ? 1 : 0;
        if (un.plan == 9) {
            for (int32_t t = 0; t < 0x13; t++) {
                bool blocked = (t == 1 && !(sq->items & 0x8000)) || (t == 6 && !(sq->items & 4));
                const char* name = sea ? Order[4 + t].order_sea : Order[4 + t].order;
                std::string sc = Terraform[t].shortcuts ? Terraform[t].shortcuts : "";
                if (blocked || !terrain_avail(t, sea, CurrentPlayerFaction)) {
                    rename_item(9, 0x900 + t, std::string(Order[4 + t].order) + "|" + sc);
                    disable_item(9, 0x900 + t);
                    hide_item(9, 0x900 + t);
                } else {
                    int32_t turns = action_terraform(veh, t, 0);
                    std::string l = std::string(name) + " (" + std::to_string(turns + 1) + " " +
                                    label_get((turns == 1 ? 0xC0 : 0xC4) / 4) + ")|" + sc;
                    rename_item(9, 0x900 + t, l);
                    show_item(9, 0x900 + t);
                    enable_item(9, 0x900 + t);
                }
            }
            show_if(9, 0x9F8, triad != 1);
            show_if(9, 0x9F9, triad != 1);
            if (triad != 1) {
                enable_item(9, 0x9F8);
                enable_item(9, 0x9F9);
            }
            enable_item(9, 0x9F7);
            enable_item(9, 0x9F2);
            enable_if(9, 0x9F6, has_tech((&Terraform[10].preq_tech)[sea], CurrentPlayerFaction));
            bool t5 = has_tech((&Terraform[5].preq_tech)[sea], CurrentPlayerFaction);
            enable_if(9, 0x9F0, t5);
            enable_if(9, 0x9F3, t5);
            bool t6 = has_tech((&Terraform[6].preq_tech)[sea], CurrentPlayerFaction);
            show_if(9, 0x9F1, t6);
            show_if(9, 0x9F4, t6);
            if (t6) {
                enable_item(9, 0x9F1);
                enable_item(9, 0x9F4);
            }
            bool t9 = has_tech((&Terraform[9].preq_tech)[sea], CurrentPlayerFaction);
            show_if(9, 0x9F5, t9);
            if (t9) enable_item(9, 0x9F5);
        } else {
            for (int32_t t = 0; t < 0x13; t++) {
                const char* name = sea ? Order[4 + t].order_sea : Order[4 + t].order;
                std::string sc = Terraform[t].shortcuts ? Terraform[t].shortcuts : "";
                rename_item(9, 0x900 + t, std::string(name) + "|" + sc);
                if (!terrain_avail(t, sea, CurrentPlayerFaction)) {
                    hide_item(9, 0x900 + t);
                } else {
                    show_item(9, 0x900 + t);
                    disable_item(9, 0x900 + t);
                }
            }
            for (int32_t id : {0x9F8, 0x9F9, 0x9F0, 0x9F1, 0x9F6, 0x9F3, 0x9F4, 0x9F5, 0x9F7, 0x9F2}) disable_item(9, id);
        }
        show_if(3, 0x317, !base_tile);
        show_if(3, 0x316, base_tile);
    }
    for (int32_t id = 0x601; id < 0x61C; id++) enable_if(6, id, GameState & STATE_SCENARIO_EDITOR);
    for (int32_t id = 0xA00; id < 0xA55; id++) enable_if(10, id, GameState & STATE_SCENARIO_EDITOR);
    if (!detail) {
        for (int32_t id : kDetail2) hide_item(2, id);
        for (int32_t id : {0x31B, 0x324, 0x306, 0x304, 0x319, 0x31A, 0x317, 800, 799, 0x307, 0x322}) hide_item(3, id);
        for (int32_t id : kDetail9) hide_item(9, id);
    }
    ui_invalidate();
}
