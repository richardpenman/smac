// The menu bar (AlphaMenu, ui 0x402F40..0x403B70, on the gfx Menu / PullDown classes) and its
// pull-downs, built as MainMenu_menu_init (0x45FB20) builds them: the sections of menu.txt
// (GAME, HQ, NETWORK, MAP, ACTION, TERRAFORM, SCENARIO, EDITOR, HELP; the first line is the
// title, each following line an item "text|shortcut", '&' marking the accelerator) with the
// command ids and separators of the original (table below, read from the asm) and the items it
// hides at init.
//
// Approximated: the look (bar and pull-downs drawn with the interface's box tile and Arial 14,
// not by Menu_on_redraw / PullDown_on_redraw; the pull-downs opened from the map's right-click menu
// are the reconstructed PullDown of pulldown.cpp) and the item visibility of
// pfncMainMenuUpdateVisible (only the init's hidden items and the menus of other modes: Network in
// multiplayer, Scenario and Edit Map in the scenario editor). A chosen item runs its command id
// through Console_menu_proc (console_menu.cpp), as Menu_menu_event does.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/colortables.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/popup.h"
#include "ui/pulldown.h"
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
    bool separator = false, hidden = false, checked = false;
    char accel = 0;
};
struct Menu {
    std::string section, title;
    int32_t index = 0;
    char accel = 0;
    std::vector<Item> items;
    int32_t x = 0, w = 0;  // on the bar
};
std::vector<Menu> g_menus;
bool g_built = false;
bool g_open = false;   // the bar is shown
int32_t g_cur = -1;    // open pull-down (-1: the bar only)
int32_t g_sel = -1;    // selected item of the pull-down
constexpr int32_t kBarH = 24, kItemH = 19;

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
    if (m.section == "SCENARIO" || m.section == "EDITOR") return (GameState & 0x40) != 0;
    return true;
}

int32_t color(int32_t r, int32_t g, int32_t b) { return Palette_get_nearest_palette_index(&GamePalette, r, g, b, 1); }

void layout(Buffer* s) {
    Font* f = find_font(14, 1);
    int32_t x = 8;
    for (Menu& m : g_menus) {
        if (!menu_shown(m)) {
            m.w = 0;
            continue;
        }
        m.x = x;
        m.w = Font_width(f, m.title.c_str()) + 16;
        x += m.w;
    }
    (void)s;
}

UiRect pulldown_rect(const Menu& m) {
    Font* f = find_font(14, 1);
    int32_t w = 0, h = 4;
    for (const Item& it : m.items) {
        if (it.hidden) continue;
        if (it.separator) {
            h += 6;
            continue;
        }
        int32_t iw = Font_width(f, it.text.c_str()) + Font_width(f, it.shortcut.c_str()) + 48;
        if (iw > w) w = iw;
        h += kItemH;
    }
    return {m.x, kBarH, m.x + w, kBarH + h + 4};
}

// The visible item at y in the pull-down, -1 none.
int32_t item_at(const Menu& m, int32_t y) {
    int32_t iy = kBarH + 4;
    for (size_t i = 0; i < m.items.size(); i++) {
        const Item& it = m.items[i];
        if (it.hidden) continue;
        int32_t h = it.separator ? 6 : kItemH;
        if (y >= iy && y < iy + h) return it.separator ? -1 : (int32_t)i;
        iy += h;
    }
    return -1;
}

void close_menus() {
    g_open = false;
    g_cur = -1;
    g_sel = -1;
    ui_invalidate();
}

void choose(const Item& it) {
    close_menus();
    std::fprintf(stderr, "menu item %03X %s\n", it.id, it.text.c_str());
    Console_menu_proc(&MainConsole, it.id);  // Menu_menu_event -> Console_menu_daemon
}

void open_menu(int32_t i) {
    g_open = true;
    g_cur = i;
    g_sel = -1;
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
    if (y < kBarH) {
        if (type == UiInput::LDOWN) {
            for (size_t i = 0; i < g_menus.size(); i++) {
                const Menu& m = g_menus[i];
                if (m.w && x >= m.x && x < m.x + m.w) {
                    open_menu((int32_t)i);
                    return true;
                }
            }
            close_menus();
        }
        return true;
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
    BoxDef* b = box_def(9);
    int32_t text = color(200, 220, 200), hi = color(228, 104, 24), dim = color(116, 156, 56);
    UiRect bar = {0, 0, s->width, kBarH};
    if (b && b->center) Buffer_tile(s, b->center, 0, 0, 0, 0, s->width, kBarH);
    else Buffer_fill_3(s, &bar, 0);
    Buffer_box_2(s, &bar, dim, 0);
    Font* f = find_font(14, 1);
    Buffer_set_font(s, f, nullptr, nullptr, nullptr);
    for (size_t i = 0; i < g_menus.size(); i++) {
        const Menu& m = g_menus[i];
        if (!m.w) continue;
        Buffer_set_text_color(s, (int32_t)i == g_cur ? hi : text, -1, 1, 1);
        Buffer_write_l_3(s, m.title.c_str(), m.x + 8, 4, (int32_t)m.title.size());
    }
    if (g_cur < 0) return;
    const Menu& m = g_menus[g_cur];
    UiRect r = pulldown_rect(m);
    if (b && b->center) Buffer_tile(s, b->center, r.left, r.top, r.left, r.top, r.right - r.left, r.bottom - r.top);
    else Buffer_fill_3(s, &r, 0);
    Buffer_box_2(s, &r, dim, 0);
    int32_t y = kBarH + 4;
    for (size_t i = 0; i < m.items.size(); i++) {
        const Item& it = m.items[i];
        if (it.hidden) continue;
        if (it.separator) {
            Buffer_hline_2(s, r.left + 4, r.right - 4, y + 2, dim);
            y += 6;
            continue;
        }
        if ((int32_t)i == g_sel) {
            UiRect h = {r.left + 2, y, r.right - 2, y + kItemH};
            Buffer_fill_3(s, &h, color(40, 60, 40));
        }
        Buffer_set_text_color(s, (int32_t)i == g_sel ? hi : text, -1, 1, 1);
        Buffer_write_l_3(s, it.text.c_str(), r.left + 10, y + 1, (int32_t)it.text.size());
        if (it.checked) {  // the check mark of Menu_check_menu_item
            int32_t cx = r.left + 3, cy = y + kItemH / 2;
            Buffer_line_2(s, cx, cy, cx + 2, cy + 3, (int32_t)i == g_sel ? hi : text);
            Buffer_line_2(s, cx + 2, cy + 3, cx + 6, cy - 4, (int32_t)i == g_sel ? hi : text);
        }
        int32_t sw = Font_width(f, it.shortcut.c_str());
        Buffer_write_l_3(s, it.shortcut.c_str(), r.right - 10 - sw, y + 1, (int32_t)it.shortcut.size());
        y += kItemH;
    }
}
