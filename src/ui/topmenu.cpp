// The opening screens: the desktop, the menu column (SetupWin), the main menu (top_menu) and its
// sub-menus, the map setup (map_menu, size_of_planet, custom_planet) and the credits (see
// topmenu.h; config_game and the faction picker are in gamesetup.cpp).
//
// Look: the desktop is the opening picture (xopeninga.pcx at 1024x768, xopeningb.pcx at 800x600)
// shown through its own palette (under GDI the window's DIB colour table: palette layer 1, win.h).
// The menu column stacks the items of openitem.flc (354 x 75, frame 5 is the item at rest; frames
// 0-5 slide it in and 6-11 out) at the bottom right: the animation's indices
// are those of the desktop's colour table, 9 is transparent and 0 shows the redtint table's colour
// of the desktop pixel under it (Win +0xA0 = RedTintTable). Texts: Arial 18 bold, centred, upper
// case, the part from "(" dropped, colour 0xCD (0xCA highlighted; the caption 0x42). Items of the
// custom map show their planet picture (S<ocean>L<life>C<clouds>.pcx at the right edge, the moons
// MOON1..3.PCX at the top left) on the desktop when highlighted; the pictures stay until the
// desktop is redrawn (change_opening at the next main menu).
// The column slides in and out (frames 0-5 and 6-11 of openitem.flc, at least 10 ms each; not in
// unattended runs); its sound (fx\menu out.wav) and the opening menu's music (fx\opening menu.wav)
// play through sound.cpp.
#include "ui/topmenu.h"
#include "ui/sound.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/clock.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/net_misc.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/world.h"
#include "engine/zoc.h"
#include "engine/parse.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/colortables.h"
#include "ui/console.h"
#include "ui/flic.h"
#include "ui/screenwin.h"
#include "ui/win.h"

void (*g_ui_before_world_build)() = nullptr;

namespace {

// ---- the desktop (TopMenuWin) ----

struct DesktopWin : ScreenWin {
    void redraw() override {}  // the canvas holds the picture (change_opening) and what was drawn on it
};
DesktopWin g_desk;
Palette g_desk_pal;  // its colour table: the opening picture's palette in 10..245
bool g_desk_up = false;

// A picture of the opening screens with its own palette, copied over the game palette's 10..245.
void palette_from(Palette* out, const Buffer& b) {
    std::memcpy(out, &GamePalette, sizeof(Palette));
    for (int32_t i = 10; i < 246; i++) std::memcpy(out->rgb[i], b.colors[i], 3);
}

const char* opening_pcx() {
    if (ExpansionEnabled) return ScreenWidth == 800 ? "xopeningb.pcx" : "xopeninga.pcx";
    return ScreenWidth == 800 ? "openingb.pcx" : "openinga.pcx";
}

// ---- SetupWin ----

constexpr int32_t kCaptionGap = 0x14;  // between the caption's frame and the first item

struct SetupPics {  // the pictures of the highlighted item (custom_planet)
    Buffer pic[3];
    int32_t x[3] = {}, y[3] = {};
    bool on = false;
};

struct SetupWin : ScreenWin {
    std::vector<std::string> items;  // upper-cased, as SetupWin_draw_item leaves them
    std::string caption;  // local_1b4
    int32_t count = 0;  // +0xFCC: items (with the caption while drawn)
    int32_t sel = 0;  // +0xFD0
    bool done = false;
    const Flc* flc = nullptr;
    Font font;  // Arial 18 bold
    SetupPics* pics = nullptr;
    Spots spots;
    int32_t anim = 5;  // the frame of openitem.flc drawn (5: at rest)

    // Top of item i's frame (the caption is frame 0 when present).
    int32_t frame_top(int32_t i) const {
        int32_t k = caption.empty() ? i : i + 1;
        return k * flc->height + (caption.empty() ? 0 : kCaptionGap);
    }
    void frame(int32_t top) {
        // Frame 5 of openitem.flc (0-4 sliding in, 6-11 out): 9 transparent, 0 the redtint of the
        // desktop under it.
        const std::vector<uint8_t>& px = flc->frames[std::min<size_t>((size_t)anim, flc->frames.size() - 1)];
        for (int32_t y = 0; y < flc->height; y++) {
            int32_t cy = top + y;
            if (cy < 0 || cy >= canvas.height) continue;
            uint8_t* d = canvas.data + (size_t)cy * canvas.stride;
            for (int32_t x = 0; x < flc->width && x < canvas.width; x++) {
                uint8_t v = px[(size_t)y * flc->width + x];
                if (v == 9) continue;
                if (v == 0) {
                    int32_t dx = this->x + x, dy = this->y + cy;
                    uint8_t under = (uint8_t)Buffer_get_pixel(&g_desk.canvas, dx, dy);
                    v = (ExpansionEnabled ? RedTintTable : BlueTintTable)[under];
                }
                d[x] = v;
            }
        }
    }
    // SetupWin_draw_item (0x4AE4D0).
    void draw_item(int32_t i, bool hi) {
        if (i < 0 || i >= (int32_t)items.size()) return;
        int32_t top = frame_top(i);
        frame(top);
        UiRect r{0xF + 0xC, top + 0x1F + 2, 0xF + 0x11D + 4, top + 0x39 - 2};
        Buffer_set_font(&canvas, &font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(&canvas, hi ? 0xCA : 0xCD, -1, 1, 1);
        texts.push_back(items[i]);
        Buffer_write_cent_l_7(&canvas, items[i].c_str(), &r, (int32_t)items[i].size());
        if (hi && pics && pics->on && (ScreenWidth == 800 || ScreenWidth == 1024)) {
            int32_t k = std::max(0, std::min(2, i));
            Buffer& p = pics->pic[k];
            if (p.data) {
                bool keyed = Buffer_get_pixel(&p, 0, 0) == 9;
                for (int32_t y = 0; y < p.height; y++) {
                    int32_t dy = pics->y[k] + y;
                    if (dy < 0 || dy >= g_desk.canvas.height) continue;
                    for (int32_t x = 0; x < p.width; x++) {
                        int32_t dx = pics->x[k] + x;
                        uint8_t v = p.data[(size_t)y * p.stride + x];
                        if (dx < 0 || dx >= g_desk.canvas.width || (keyed && v == 9)) continue;
                        g_desk.canvas.data[(size_t)dy * g_desk.canvas.stride + dx] = v;
                    }
                }
            }
        }
    }
    void redraw() override {
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 9);
        if (!caption.empty()) {
            frame(0);
            UiRect r{0xF + 4, 0x1F + 2, 0x12C - 4, 0x1F + 0x1A - 2};
            Buffer_set_font(&canvas, &font, nullptr, nullptr, nullptr);
            Buffer_set_text_color(&canvas, 0x42, -1, 1, 1);
            texts.push_back(caption);
            Buffer_write_cent_l_7(&canvas, caption.c_str(), &r, (int32_t)caption.size());
        }
        // The highlighted item's picture goes on the desktop first: the tint shows it.
        if (sel >= 0) draw_item(sel, true);
        for (int32_t i = 0; i < (int32_t)items.size(); i++) draw_item(i, i == sel);
    }
    int32_t item_at(int32_t x, int32_t y) {
        int32_t index = -1, type = 0;
        spot_check(&spots, x, y, &index, &type);
        return index;
    }
    void on_key(int32_t key) override {  // SetupWin_on_key_click (0x4AE3E0)
        int32_t n = (int32_t)items.size();
        switch (key & 0xFFFF) {
        case VKEY_RETURN:
            done = true;
            sw_close(this);
            break;
        case VKEY_ESCAPE:
            sel = -1;
            done = true;
            sw_close(this);
            break;
        case VKEY_UP: sel = sel == 0 ? n - 1 : sel - 1; break;
        case VKEY_DOWN: sel = sel == n - 1 ? 0 : sel + 1; break;
        default: break;
        }
    }
    void on_move(int32_t x, int32_t y) override {  // SetupWin_on_mouse_move (0x4AE380)
        int32_t i = item_at(x, y);
        if (i >= 0 && i != sel) {
            sel = i;
            redraw();
            ui_invalidate();
        }
    }
    void on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) override {
        if (parent != 0 || right) return;  // SetupWin_on_left_click (0x4AE340)
        int32_t i = item_at(x, y);
        if (i < 0) return;
        sel = i;
        done = true;
        sw_close(this);
    }
    void log_state() override {
        std::fprintf(stderr, "screenstate menu caption=\"%s\" selected=%d items=%d\n", caption.c_str(), sel,
                     (int32_t)items.size());
    }
};

SetupWin* g_menu = nullptr;  // the open menu column (MENUSTATE)
SetupPics* g_next_pics = nullptr;  // pictures for the next menu (custom_planet)

bool prefix_i(const char* s, const char* p) {
    for (; *p; s++, p++)
        if (std::tolower((uint8_t)*s) != *p) return false;
    return true;
}

std::string menu_text(const char* s) {
    // SetupWin_draw_item: upper case, "|" as a space, the text from "(" dropped with the
    // character before it.
    std::string t = s ? s : "";
    for (char& c : t) c = (char)std::toupper((uint8_t)c);
    size_t bar = t.find('|');
    if (bar != std::string::npos) t[bar] = ' ';
    size_t par = t.find('(');
    if (par != std::string::npos) t = t.substr(0, par ? par - 1 : 0);
    return t;
}

// SetupWin_do_menu_rightside (0x4ADB70).
int32_t run_menu(std::vector<std::string> items, std::string caption, int32_t active) {
    static const Flc* flc = nullptr;
    static Flc flc_raw;
    if (!flc) {
        if (flc_load("openitem.flc", &flc_raw, nullptr)) flc = &flc_raw;
        if (!flc) return -1;
    }
    SetupWin w;
    w.name = "setupwin";
    w.flc = flc;
    w.layer = 1;
    w.pics = g_next_pics;
    g_next_pics = nullptr;
    Font_ctor(&w.font);
    Font_init(&w.font, "Arial", 0x12, 1);
    for (std::string& s : items) w.items.push_back(menu_text(s.c_str()));
    w.caption = caption;
    int32_t n = (int32_t)w.items.size() + (caption.empty() ? 0 : 1);
    int32_t h = flc->height * n + (caption.empty() ? 0 : kCaptionGap);
    sw_init(&w, ScreenWidth - flc->width, ScreenHeight - h, flc->width, h, false);
    w.layer = 1;
    spot_init(&w.spots, n + 2);
    for (int32_t i = 0; i < (int32_t)w.items.size(); i++)
        spot_add(&w.spots, i, 0, 0xF, w.frame_top(i), 0x12C - 0xF, flc->height);
    w.sel = active >= 0 && active < (int32_t)w.items.size() ? active : 0;
    g_menu = &w;
    std::fprintf(stderr, "menu %s: %d items, selected %d\n", caption.c_str(), (int32_t)w.items.size(), w.sel);
    // The column's sound (a Wave of its own: fx\menu out.wav) as it slides in.
    static uint8_t menu_out[0x6C];
    Wave_load(menu_out, "fx\\menu out.wav");
    if (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) Wave_play_2(menu_out);
    // The slide (SetupWin_do_menu_rightside): frames 0..5 of every item's frame, at least 10 ms
    // each, then the texts; on leaving, frames 6..11 (not for unattended runs).
    auto slide = [&](int32_t from, int32_t to) {
        if (g_ui_unattended) return;
        sw_show(&w);
        for (int32_t f = from; f <= to; f++) {
            uint32_t t0 = platform::ticks_ms();
            w.anim = f;
            UiRect all{0, 0, w.canvas.width, w.canvas.height};
            Buffer_fill_3(&w.canvas, &all, 9);
            for (int32_t k = 0; k < n; k++) w.frame(k * flc->height + (k && !w.caption.empty() ? kCaptionGap : 0));
            ui_invalidate();
            ui_present();
            while (platform::ticks_ms() - t0 < 10) platform::delay_ms(1);
        }
        w.anim = 5;
    };
    slide(0, 5);
    sw_run(&w);
    if (w.done && (int32_t)flc->frames.size() >= 12) slide(6, 11);
    g_menu = nullptr;
    // No more input (a scripted run that ended): cancelled.
    if (!w.done) w.sel = -1;
    std::fprintf(stderr, "menu %s -> %d\n", caption.c_str(), w.sel);
    return w.sel;
}

}  // namespace

// ---- desktop ----

int32_t GAMEAPI change_opening(const char* pcx) {
    Buffer b;
    Buffer_ctor(&b);
    if (Buffer_load_pcx(&b, pcx, nullptr, 10, 0xEC)) return 7;
    if (!g_desk.canvas.data || g_desk.canvas.width != ScreenWidth || g_desk.canvas.height != ScreenHeight) {
        g_desk.name = "desktop";
        Buffer_ctor(&g_desk.canvas);
        Buffer_init(&g_desk.canvas, ScreenWidth, ScreenHeight, 8, 0);
    }
    // GraphicWin_fill(0), then the picture (Buffer_copy_3).
    std::memset(g_desk.canvas.data, 0, (size_t)g_desk.canvas.stride * g_desk.canvas.height);
    for (int32_t y = 0; y < std::min(b.height, g_desk.canvas.height); y++)
        std::memcpy(g_desk.canvas.data + (size_t)y * g_desk.canvas.stride, b.data + (size_t)y * b.stride,
                    (size_t)std::min(b.width, g_desk.canvas.width));
    palette_from(&g_desk_pal, b);
    ui_set_layer_palette(1, &g_desk_pal);
    g_desk.layer = 1;
    g_desk.x = g_desk.y = 0;
    ui_set_desktop(true);
    sw_show(&g_desk);
    g_desk_up = true;
    return 0;
}

void GAMEAPI close_opening() {
    if (!g_desk_up) return;
    sw_hide(&g_desk);
    ui_set_desktop(false);
    g_desk_up = false;
}

// ---- SetupWin entry points ----

int32_t setupwin_do_menu_2(BasePop* pop) {
    std::vector<std::string> items;
    for (int32_t i = 0; i < BasePop_item_count(pop); i++) items.push_back(BasePop_item_text(pop, i));
    const char* cap = BasePop_caption(pop);
    int32_t pos = run_menu(items, cap ? cap : "", BasePop_active(pop));
    // Dialog_pos_to_id: the item's id.
    return pos < 0 ? -1 : BasePop_item_id(pop, pos);
}

int32_t setupwin_do_menu(const char* label, int32_t active) {
    // The section read directly (X_text_open): "#caption" lines, items up to the second blank.
    if (text_open(ScriptFileName, label)) return -1;
    std::vector<std::string> items;
    std::string caption;
    bool blank = false;
    for (;;) {
        char* line = text_get();
        if (!line) break;
        if (*line == '#') {
            if (prefix_i(line + 1, "caption")) {
                std::string c = line + 8;
                size_t b = c.find_first_not_of(" \t");
                caption = b == std::string::npos ? "" : c.substr(b);
                while (!caption.empty() && std::isspace((uint8_t)caption.back())) caption.pop_back();
            }
            continue;
        }
        if (!*line) {
            if (blank) break;
            blank = true;
            continue;
        }
        blank = true;
        items.push_back(line);
    }
    text_close();
    return run_menu(items, caption, active);
}

void topmenu_log_state() {
    if (!g_menu) {
        std::fprintf(stderr, "menustate none\n");
        return;
    }
    std::fprintf(stderr, "menustate caption=\"%s\" selected=%d", g_menu->caption.c_str(), g_menu->sel);
    for (const std::string& s : g_menu->items) std::fprintf(stderr, " [%s]", s.c_str());
    std::fprintf(stderr, "\n");
}

// ---- clear_all_player_messages (net_misc 0x51E3C0): the combat counts and the players' message
// lists of the previous game ----
static void clear_all_player_messages() {
    for (int32_t i = 0; i < 8; i++) {
        FactionCombatLoss[i] = 0;
        FactionCombatWin[i] = 0;
    }
    clear_hotseat_chat();
}

// ---- top_menu (0x58E360) ----

int32_t GAMEAPI top_menu(int32_t hotseat) {
    BasePop* pop = popup_new();
    if (!hotseat) {
        PbemActive = 0;
        clear_all_player_messages();
    }
    prefs_load(0);
    prefs_use();
    // The opening menu's music (fx\opening menu.wav looping on 0x945780, music preference 0x800).
    uint8_t* music = mem::host(0x945780);
    if (!hotseat && (GamePreferences & PREF_AV_VOLUME_MUSIC_TOGGLE)) {
        Wave_load_3(music, "fx\\opening menu.wav", 2);
        Wave_play_2(music);
    }
    // Leaving the menu fades it out over 3 seconds.
    auto music_end = [&]() {
        if ((hotseat || (GamePreferences & PREF_AV_VOLUME_MUSIC_TOGGLE)) && Wave_is_playing(music)) {
            Sound_fade_2(music, 3000);
            Wave_unload(music);
        }
    };
    if (AlphaIniPrefs.preferences & 0x20000000) GameDialogFlags &= ~4;
    else GameDialogFlags |= 4;
    NetUpkeepState = 0;
    int32_t r = 0;
    do {
        change_opening(opening_pcx());
        prefs_load(0);
        prefs_use();
        Popup_start(pop, ScriptFileName, hotseat ? "HOTSEAT" : "TOPMENU", -1, nullptr, 0);
        if (!hotseat) BasePop_set_active(pop, DefaultPrefs.top_menu);
        r = setupwin_do_menu_2(pop);
        if (r < 0 || r == 6) {
            popup_delete(pop);
            return 1;
        }
        if (!hotseat) {
            DefaultPrefs.top_menu = r;
            prefs_save(0);
        } else if (r > 0) {
            r++;
        }
        bool leave = false;
        switch (r) {
        case 0:
        case 1:
            r = map_menu(r == 1);
            leave = r == 0;
            break;
        case 2: {
            int32_t s = 0;
            if (!hotseat) {
                s = setupwin_do_menu("SCENARIOMENU", 0);
                if (s < 0) break;
                if (s == 1) {  // CREATE SCENARIO
                    size_of_planet(0);
                    MapOceanCoverage = MapLandCoverage = MapErosiveForces = 1;
                    MapPlanetaryOrbit = MapCloudCover = MapNativeLifeForms = 1;
                    map_wipe();
                    config_game(3);
                    VehCount = 0;
                    setup_game(1);
                    GameState |= 0x80000E0;
                    ControlTurnMove = CurrentPlayerFaction;
                    music_end();
                    popup_delete(pop);
                    close_opening();
                    return ControlTurnA;
                }
                if (s == 2) {  // LOAD MAP FILE
                    if (!load_map()) {
                        config_game(3);
                        VehCount = 0;
                        setup_game(1);
                        GameState |= 0x80000E0;
                        ControlTurnMove = CurrentPlayerFaction;
                        leave = true;
                    }
                    break;
                }
            }
            // PLAY SCENARIO (0) / EDIT SCENARIO (3): a scenario file.
            int32_t pbem = PbemActive;
            int32_t loaded = load_game(1, 0);
            if (pbem == 0 || (PbemActive = 1, MultiplayerActive != 0)) PbemActive = 0;
            if (loaded) break;
            if (GameAltPathChanged) {
                stop_timers();
                labels_shutdown();
                Strings_shutdown(&StringTable);
                if (!game_init(1, 0)) GameNotStarted = 0;
            }
            if (s == 0) {
                if (config_game(2)) {
                    read_rules(1);
                    break;
                }
                ControlTurnMove = CurrentPlayerFaction;
                // The scenario's introduction (INTRO of the scenario's script).
                const MFaction& m = MFactions[CurrentPlayerFaction];
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(1, m.title_leader, -1, -1);
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(2, m.name_leader, -1, -1);
                PluralDefault = m.is_noun_plural;
                GenderDefault = m.noun_gender;
                parse_says(3, m.noun_faction, -1, -1);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, label_get(0));
                crt_strcat(StrBuffer, " ");
                say_year(StrBuffer);
                parse_says(4, StrBuffer, -1, -1);
                parse_says(0, StrBuffer, -1, -1);
                parse_num(0, ObjectiveReqVictory);
                parse_num(1, ObjectivesSuddenDeathVictory);
                parse_num(2, num_objectives(CurrentPlayerFaction, GameRules & RULES_VICTORY_COOPERATIVE));
                parse_num(3, EndingMissionYear);
                X_pop_2("SCENARIO", "INTRO", 0);
                Console_set_move(&MainConsole, 1);
            } else {
                ControlTurnMove = CurrentPlayerFaction;
                GameState |= 0x80000E0;
            }
            leave = true;
            break;
        }
        case 3: {  // LOAD GAME
            SaveFileMenu = hotseat;
            int32_t lr = load_game(0, 0);
            SaveFileMenu = 0;
            if (lr == 0) {
                ControlTurnMove = CurrentPlayerFaction;
                if (GameAltPathChanged) {
                    stop_timers();
                    labels_shutdown();
                    Strings_shutdown(&StringTable);
                    if (!game_init(1, 0)) GameNotStarted = 0;
                }
                leave = true;
            }
            break;
        }
        case 4: leave = multiplayer_init(0) == 0; break;
        case 5: show_credits(); break;
        default: leave = true; break;
        }
        if (leave) break;
    } while (!ControlTurnA);
    music_end();
    popup_delete(pop);
    // The game's windows cover the desktop from here.
    close_opening();
    return ControlTurnA;
}

// ---- map_menu (0x58D6B0) ----

int32_t GAMEAPI map_menu(int32_t quick) {
    BasePop* pop = popup_new();
    for (;;) {
        Popup_start(pop, ScriptFileName, "MAPMENU", -1, nullptr, 0);
        MapGenerate = 0;
        BasePop_set_active(pop, DefaultPrefs.map_type);
        int32_t r = DefaultPrefs.map_type;
        if (!quick) r = setupwin_do_menu_2(pop);
        if (r < 0) {
            popup_delete(pop);
            return 1;
        }
        DefaultPrefs.map_type = r;
        prefs_save(0);
        if ((uint32_t)r < 5) {
            bool again = false;
            for (bool dispatch = true; dispatch;) {
                dispatch = false;
                if (r == 0 || r == 1) {
                    if (size_of_planet(quick)) {
                        again = true;
                        break;
                    }
                    if (MapGenerate) {
                        if (r == 1) {
                            if (custom_planet(1, quick)) dispatch = true;  // cancelled: the size again
                        } else {
                            if (!MultiplayerActive) {
                                int32_t v = (game_rand() % 7 + 1) / 2;
                                v = std::max(0, std::min(2, v));
                                MapOceanCoverage = 2 - v;
                            } else {
                                MapOceanCoverage = 0;
                            }
                            MapLandCoverage = 2 - MapOceanCoverage;
                            int32_t e = game_rand();
                            MapPlanetaryOrbit = 1;
                            MapCloudCover = 1;
                            MapNativeLifeForms = 1;
                            MapErosiveForces = (e % 6 + 5) / 5;
                        }
                    }
                } else {
                    int32_t lr;
                    if (r == 2) lr = load_map_daemon("maps\\xplanet.mp");
                    else if (r == 3) lr = load_map_daemon("maps\\xplanetx.mp");
                    else {
                        lr = load_map();
                        if (lr) quick = 0;
                    }
                    if (lr) {
                        again = true;
                        break;
                    }
                    world_climate();
                }
            }
            if (again) continue;
        }
        if (config_game((uint32_t)quick) == 0) {
            popup_delete(pop);
            return 0;
        }
    }
}

// ---- size_of_planet (0x58CED0) ----

int32_t GAMEAPI size_of_planet(int32_t mode) {
    BasePop* pop = popup_new();
    Popup_start(pop, ScriptFileName, "WORLDSIZE", -1, nullptr, mode != 2 ? 0x40 : 0);
    MapGenerate = 1;
    int32_t w = 40, h = 80;
    int32_t widths[32] = {}, heights[32] = {};
    auto finish = [&](int32_t ww, int32_t hh) {
        MapAreaY = hh;
        MapAreaX = ww * 2;
        prefs_save(0);
        if (mode < 2) {
            map_init();
            Path_init(&Paths);
        }
        GameDrawState |= 8;
        popup_delete(pop);
        return 0;
    };
    if (text_open(AlphaxFileName, "WORLDSIZE")) {
        MapSizePlanet = 2;
        return finish(w, h);
    }
    text_get();
    int32_t n = std::min(text_item_number(), 32);
    for (int32_t i = 0; i < n; i++) {
        text_get();
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, text_item());
        BasePop_item(pop, StrBuffer, i);
        widths[i] = text_item_number();
        heights[i] = text_item_number();
    }
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0x1104 / 4));
    BasePop_item(pop, StrBuffer, 99);
    BasePop_set_active(pop, AlphaIniPrefs.custom_world[0]);
    if (mode == 1) {
        MapSizePlanet = AlphaIniPrefs.custom_world[0];
    } else {
        MapSizePlanet = mode == 2 ? BasePop_exec_3(pop, 0, 0) : setupwin_do_menu_2(pop);
        if (MapSizePlanet < 0) {
            popup_delete(pop);
            return 1;
        }
        if (BasePop_button_pressed(pop)) MapSizePlanet = game_rand() % 5;  // Random
        else if (MapSizePlanet != 99) AlphaIniPrefs.custom_world[0] = MapSizePlanet;
    }
    if (MapSizePlanet != 99) {
        int32_t s = std::max(0, std::min(MapSizePlanet, 31));
        return finish(widths[s], heights[s]);
    }
    // Custom size (CUSTOMMAP): the width and height, 16..256, beyond 128 after VERYLARGEMAP.
    Popup_start(pop, ScriptFileName, "CUSTOMMAP", -1, nullptr, 0x44);
    int32_t cx = prefs_get_int("Custom Map X", 0x28, 0), cy = prefs_get_int("Custom Map Y", 0x50, 0);
    char num[16];
    std::snprintf(num, sizeof(num), "%d", cx);
    BasePop_edit(pop, label_get(0x1108 / 4), num, 8);
    std::snprintf(num, sizeof(num), "%d", cy);
    BasePop_edit(pop, label_get(0x110C / 4), num, 8);
    if (BasePop_exec_3(pop, 0, 0) < 0) {
        popup_delete(pop);
        return 1;
    }
    int32_t x = std::max(0x10, std::min(0x100, std::atoi(ParseStrBuffer[0])));
    int32_t y = std::max(0x10, std::min(0x100, std::atoi(ParseStrBuffer[1])));
    if ((x > 0x80 || y > 0x80) && X_pop("VERYLARGEMAP", 0)) {
        popup_delete(pop);
        return 1;
    }
    prefs_put_int("Custom Map X", (uint32_t)x, 0);
    prefs_put_int("Custom Map Y", (uint32_t)y, 0);
    return finish(x, y);
}

// ---- custom_planet (0x58C2A0) ----

int32_t GAMEAPI custom_planet(int32_t setup_win, int32_t no_dialogs) {
    BasePop* pop = popup_new();
    static const int32_t kOrder[4] = {1, 3, 6, 5};  // ocean, erosion, native life, clouds
    int32_t* params = &MapSizePlanet;
    for (int32_t i = 1; i <= 6; i++) params[i] = 0;
    for (int32_t idx : kOrder) {
        int32_t* v = &params[idx];
        *v = AlphaIniPrefs.custom_world[idx];
        if (no_dialogs) continue;
        std::string label = "WORLD";
        SetupPics pics;
        auto clamp13 = [](int32_t v) { return (char)('0' + std::max(1, std::min(3, v + 1))); };
        auto planets = [&](int32_t which) {
            // S<ocean>L<life>C<clouds>.PCX, the item's choice in place `which` (1, 3, 5).
            for (int32_t i = 0; i < 3; i++) {
                char name[16] = "S1L1C1.PCX";
                name[1] = clamp13(MapOceanCoverage);
                name[3] = clamp13(MapNativeLifeForms);
                name[5] = clamp13(MapCloudCover);
                name[which] = (char)('1' + i);
                Buffer_ctor(&pics.pic[i]);
                Buffer_load_pcx(&pics.pic[i], name, nullptr, 10, 0xEC);
                if (ScreenWidth == 800) {
                    pics.x[i] = 0x1C1;
                    pics.y[i] = -0xA8;
                } else {
                    pics.x[i] = ScreenWidth - pics.pic[i].width;
                    pics.y[i] = 0;
                }
            }
            pics.on = true;
        };
        if (setup_win) {
            switch (idx) {
            case 1:
                planets(1);
                label += "LAND";
                break;
            case 3:
                for (int32_t i = 0; i < 3; i++) {
                    char name[20];
                    std::snprintf(name, sizeof(name), ScreenWidth == 800 ? "moon%d_800.pcx" : "MOON%d.PCX", i + 1);
                    Buffer_ctor(&pics.pic[i]);
                    Buffer_load_pcx(&pics.pic[i], name, nullptr, 10, 0xEC);
                }
                pics.on = true;
                label += "TIDES";
                break;
            case 4: label += "ORBIT"; break;
            case 5:
                planets(5);
                label += "CLOUDS";
                break;
            case 6:
                planets(3);
                label += "LIFE";
                break;
            default: break;
            }
        } else {
            static const char* kSuffix[7] = {"", "LAND", "", "TIDES", "ORBIT", "CLOUDS", "LIFE"};
            label += kSuffix[idx];
        }
        Popup_start(pop, ScriptFileName, label.c_str(), -1, nullptr, 0x40);
        BasePop_set_active(pop, AlphaIniPrefs.custom_world[idx]);
        int32_t r;
        if (setup_win) {
            g_next_pics = &pics;
            r = setupwin_do_menu_2(pop);
        } else {
            r = BasePop_exec_3(pop, 0, 0);
        }
        *v = r;
        if (r < 0) {
            popup_delete(pop);
            return 1;
        }
        if (!BasePop_button_pressed(pop)) AlphaIniPrefs.custom_world[idx] = r;
        else *v = -1;  // Random: world_build draws it
    }
    MapLandCoverage = 2 - MapOceanCoverage;
    prefs_save(0);
    popup_delete(pop);
    return 0;
}

// ---- multiplayer_init (0x58DB30): not reconstructed ----
// The original sets up a DirectPlay session (MULTIPLAYER SETUP: the service, the session, the
// players, the time controls) and starts the game through config_game with MultiplayerActive set.
// Here the menu shows the setup's first popup and goes back: there is no network layer.
int32_t GAMEAPI multiplayer_init(int32_t) {
    std::fprintf(stderr, "multiplayer_init: not reconstructed (no network play)\n");
    BasePop* pop = popup_new();
    BasePop_start(pop, nullptr, "SMACXMULTI", -1, nullptr, 0x40, nullptr);
    BasePop_string(pop, "^^MULTIPLAYER SETUP");
    BasePop_string(pop, "^");
    BasePop_string(pop, "Multiplayer games (Internet, LAN, hotseat and play-by-email) are not available.");
    BasePop_set_loc(pop, 0x1000, 0x1000);
    BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return 1;
}

// ---- show_credits (0x428DF0, CreditsWin 0x428780..0x428DD0) ----
// credits2.pcx (credits.pcx at 800x600) over the screen through its own palette (layer 2) and, in
// a 400 x 400 window at (30, 200) (100 at 800x600), the CREDITS section of credits.txt scrolling up
// (the timer moves it one pixel every 30 ms), in "Alpha Centauri" 14 bold, faded over the top and
// bottom 14 lines. A key or a click closes it (the original also closes when the text is through).

namespace {
struct CreditsWin : ScreenWin {
    Buffer back;
    Font font;
    std::vector<std::string> lines;
    int32_t line_h = 16;
    uint32_t start = 0;
    bool animate = true;
    int32_t text_color = 0, dim[7] = {};
    void redraw() override {
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        std::memcpy(canvas.data, back.data, (size_t)back.stride * back.height);
        int32_t ox = 0x1E, oy = ScreenWidth == 800 ? 100 : 200, w = 400, hh = 400;
        uint32_t t = animate ? (platform::ticks_ms() - start) / 30 : 0;
        int32_t scroll = (int32_t)t - hh;  // the text enters at the window's bottom
        Buffer_set_font(&canvas, &font, nullptr, nullptr, nullptr);
        for (int32_t i = 0; i < (int32_t)lines.size(); i++) {
            int32_t y = i * line_h - scroll;
            if (y < -line_h || y >= hh) continue;
            // The fade of the window's edges (Buffer_change_color_3 over 2-pixel bands).
            int32_t edge = std::min(y, hh - line_h - y);
            int32_t c = edge < 14 ? dim[std::max(0, std::min(6, edge / 2))] : text_color;
            Buffer_set_text_color(&canvas, c, -1, 1, 1);
            UiRect r{ox, oy + y, ox + w, oy + y + line_h};
            UiRect clip{ox, oy, ox + w, oy + hh};
            Buffer_set_clip(&canvas, &clip);
            Buffer_write_cent_l_7(&canvas, lines[i].c_str(), &r, (int32_t)lines[i].size());
            Buffer_set_clip(&canvas, &all);
        }
        texts.assign(lines.begin(), lines.begin() + std::min<size_t>(lines.size(), 3));
    }
    void on_key(int32_t) override { sw_close(this); }
    void on_click(int32_t, int32_t, int32_t, int32_t, int32_t, bool, bool) override { sw_close(this); }
};
Palette g_credits_pal;
}  // namespace

void GAMEAPI show_credits() {
    CreditsWin w;
    w.name = "credits";
    Buffer_ctor(&w.back);
    if (Buffer_load_pcx(&w.back, ScreenWidth == 800 ? "credits.pcx" : "credits2.pcx", nullptr, 10, 0xEC)) return;
    palette_from(&g_credits_pal, w.back);
    ui_set_layer_palette(2, &g_credits_pal);
    sw_init(&w, 0, 0, w.back.width, w.back.height, false);
    w.layer = 2;
    Font_ctor(&w.font);
    Font_init(&w.font, "Alpha Centauri", 0xE, 1);
    w.line_h = w.font.height > 0 ? w.font.height : 16;
    if (!text_open("CREDITS", "CREDITS")) {
        for (char* l = text_get(); l && *l != '#'; l = text_get()) w.lines.push_back(l);
        text_close();
    }
    // The text's colour and its fade in the picture's palette (white, then darker to the edges).
    w.text_color = Palette_get_nearest_palette_index(&g_credits_pal, 0xE6, 0xE6, 0xE6, 1);
    for (int32_t i = 0; i < 7; i++) {
        int32_t v = 0x20 + i * 0x1C;
        w.dim[i] = Palette_get_nearest_palette_index(&g_credits_pal, v, v, v, 1);
    }
    w.start = platform::ticks_ms();
    w.animate = !engine_is_deterministic();
    sw_run(&w);
}
