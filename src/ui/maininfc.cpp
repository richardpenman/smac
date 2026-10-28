// The main interface's panes drawn by the original's code: the status panes of StatusWin (InfoWin
// 0x8C5568: StatusWin_draw_status 0x4B6570, StatusWin_draw_active 0x4B4CF0, StatusWin_redraw,
// StatusWin_set_loc, StatusWin_reset), the date box (MainInterface_set_date 0x45BE80), "TURN
// COMPLETE" (MainInterface_redraw_complete 0x45C3A0, with turn.flc's frames) and the world map (WorldWin_on_redraw
// 0x4C3EC0 with MapWin_gen_bases_only 0x469B40 and WorldWin_draw_box 0x4C4630), with the pane
// rectangles of MainInterface_init (0x459680). They draw into the main interface's canvas
// (basewin.cpp's MainInfc canvas), composed by console_frame.cpp.
//
// This file also replaces the headless "battle" area (src/headless/ui_headless_battle.cpp):
// BattleWin_clear / pulse_timer / stop_timer and SubInterface_set_iface_mode do nothing as there
// (the battle display, BattleWin_battle_report, is not reconstructed), tut_check2 is the same.
//
// The panes' clicks (StatusWin_on_left_click / on_left_double_click / on_right_click, the hover of
// MainInterface_on_iface_mouse_move), the stack row's scroll arrows (buttons 0x3F0 / 0x3F2 of
// MainInterface_on_iface_button_clicked), StatusWin_arrow_left / right, the world map's clicks
// (WorldWin_click, WorldWin_right_menu) and spot 5 (TURN COMPLETE: Console_end_my_turn) are at the
// end of the file. Not reconstructed: the world map's StretchBlt when the map is larger than the pane (nearest pixel here; the
// original's GDI stretch mode is BLACKONWHITE).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/action.h"
#include "engine/diplo_act.h"
#include "engine/tech.h"
#include "engine/util.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/zoc.h"
#include "platform/ui_platform.h"
#include "ui/pulldown.h"
#include "ui/help.h"
#include "ui/infowin.h"
#include "ui/mapwin.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/flic.h"
#include "ui/font_text.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/statuswin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

// ---- the battle area's other functions (as src/headless/ui_headless_battle.cpp) ----
// (SubInterface_set_iface_mode, BattleWin_clear / pulse_timer / stop_timer: battlewin.cpp)
int32_t GAMEAPI tut_check2(int32_t bit) {
    if (MultiplayerActive) return 0;
    if (!(GamePreferences & PREF_BSC_TUTORIAL_MSGS)) return 0;
    if (bit & TutorialHintsShown) return 0;
    if (MFactions[CurrentPlayerFaction].rule_flags & 0x80) return 0;
    TutorialHintsShown |= bit;
    return 1;
}

GLOBAL(int32_t, DestLinesVeh, 0x6874C4);  // the unit whose go-to path draw_my_dest shows (-1)
GLOBAL(int32_t, VehStackSortOff, 0x7D3934);  // the stack row is not sorted

namespace {

int32_t& sw(uint32_t off) { return *reinterpret_cast<int32_t*>(&InfoWin + off); }
Buffer* canvas() { return basewin_mi_canvas(); }
int32_t tc(uint32_t addr) { return text_color(addr); }
int32_t zoom_or(int32_t base, int32_t mul) { return ZoomNum < 1 ? base : ZoomNum * mul / ZoomDenom; }

Spots g_spots;  // StatusWin's spots (+0x15E0): the panes and the stack row's units
void stack_arrows_show(bool on);

// StatusWin's fonts (+0x1530 normal, +0x1558 italic, +0x1580 bold; StatusWin_redraw).
Font g_font[3];
int32_t g_font_size = -1;  // +0x15AC

void status_fonts() {
    int32_t size = ScreenWidth != 800 ? 12 : 10;
    if (size != g_font_size) {
        g_font_size = size;
        static const int32_t kStyle[3] = {0, 2, 1};
        for (int32_t i = 0; i < 3; i++) {
            Font_ctor(&g_font[i]);
            Font_init(&g_font[i], "Arial", size, kStyle[i]);
        }
    }
    if (ScreenWidth != 800)
        for (Font& f : g_font) f.field_0 = 4;
}

int32_t line_h(const Font* f) { return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0; }

std::string num(int32_t v) { return std::to_string(v); }

void write(Buffer* c, const std::string& s, int32_t x, int32_t y) {
    if (!s.empty()) Buffer_write_l_3(c, s.c_str(), x, y, (int32_t)s.size());
}
int32_t write_x(Buffer* c, const std::string& s, int32_t x, int32_t y) {
    return s.empty() ? x : Buffer_write_l_3(c, s.c_str(), x, y, (int32_t)s.size());
}

// Buffer_wrap_3 (0x5DD730): word-wrapped from (x, y) in width w; the y below.
int32_t wrap3(Buffer* c, const std::string& s, int32_t x, int32_t y, int32_t w) {
    if (s.empty() || !w) return y;
    UiRect r{x, y, x + w, 0x7FFF};
    return Buffer_wrap_2(c, s.c_str(), &r);
}
int32_t wrap_cent4(Buffer* c, const std::string& s, int32_t x, int32_t y, int32_t w) {
    return Buffer_wrap_cent_4(c, s.c_str(), x, y, w);
}
// Buffer_wrap_cent (0x5DB9E0): wrap_cent_4 with `font` as the only font.
int32_t wrap_cent_font(Buffer* c, Font* font, const std::string& s, int32_t x, int32_t y, int32_t w) {
    Font* keep[3] = {c->fonts[0], c->fonts[1], c->fonts[2]};
    c->fonts[0] = font;
    c->fonts[1] = c->fonts[2] = nullptr;
    int32_t r = wrap_cent4(c, s, x, y, w);
    c->fonts[0] = keep[0];
    c->fonts[1] = keep[1];
    c->fonts[2] = keep[2];
    return r;
}

const char* lbl(uint32_t off) { return label_get((int32_t)(off / 4)); }

int32_t fac_int(int32_t f, uint32_t off) {
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Factions[f]) + off);
}

// say_morale (0x4B3FD0): the morale word, and its modifiers as '+' marks in parentheses (" (-)":
// the home base's drone riots, "(d)": the unit is damaged... as the original writes them).
void say_morale(char* out, int32_t veh_id, int32_t a3) {
    static int32_t& extra = *reinterpret_cast<int32_t*>(mem::host(0x8C6B54));
    const VEH& v = Vehs[veh_id];
    int32_t m = morale_veh(veh_id, 0, a3);
    int32_t f = v.faction_id;
    int32_t u = v.unit_id;
    bool psi = u < 0x40 && (Weapon[Units[u].weapon_id].offense_value < 0 || u == 0xF);
    crt_strcat(out, Strings_get(&StringTable, psi ? Morale[m].name_lifecycle : Morale[m].name));
    if ((uint8_t)Units[u].plan > 7) return;
    int32_t riot = 0;
    if (v.home_base_id >= 0 && (Bases[v.home_base_id].state_flags & 2) && m > 0 &&
        !(MFactions[f].rule_flags & 0x8000)) {
        crt_strcat(out, " (-)");
        riot = 1;
    }
    extra = 0;
    int32_t b = base_at(v.x, v.y);
    auto has = [&](int32_t fac) {
        int32_t off, mask;
        bitmask(fac, &off, &mask);
        return (Bases[b].facilities_built[off] & (uint8_t)mask) != 0;
    };
    auto se_bonus = [&]() {
        int32_t se = fac_int(f, 0x288);
        if (se < -4) se = -4;
        if (se > 4) return;
        if (se < -1) se++;
        if (se < 0) {
            int32_t k = m + extra;
            while (k <= 5 && se < 0) {
                extra++;
                k++;
                se++;
            }
        }
    };
    if (b >= 0 && m < 6) {
        if (!has(2)) {
            if (has(0x23) && psi) {
                extra++;
                se_bonus();
            }
        } else {
            extra++;
            se_bonus();
            if (has(0x23) && psi) extra++;
        }
    }
    if (fac_int(f, 0x25C) == 2 || fac_int(f, 0x25C) == 3) extra++;
    if (m == 0) {
        if (!extra) extra = 1;
    }
    if (extra) {
        crt_strcat(out, " (");
        for (int32_t i = 0; i < extra; i++) crt_strcat(out, "+");
        crt_strcat(out, ")");
    }
    extra -= riot;
    if (v.state & 0x80) crt_strcat(out, "(d)");
}

bool pact(int32_t f) { return (Factions[CurrentPlayerFaction].diplo_status[f] & 1) != 0; }

// The tile's terrain words (StatusWin_draw_status's first mode).
void tile_words(Buffer* c, int32_t x, int32_t y, UiRect* r, int32_t* py, int32_t lh) {
    MAP* sq = map_tile(x, y);
    int32_t left = r->left;
    int32_t depth = (sq->climate >> 5) - 3;
    int32_t widest = 0;
    Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
    if (depth < 0) {
        std::string s;
        if (depth == -3) s = lbl(0x568);
        else if (depth == -2) s = lbl(0x304);
        else if (depth == -1) s = lbl(0x564);
        int32_t e = write_x(c, s, left, *py);
        if (e >= 0) widest = e;
        *py += lh;
    }
    std::string s =
        std::string(lbl(depth < 0 ? 0x30 : 0x2C)) + ": " + num(std::abs(pfncGetElevation(x, y))) + lbl(0x34);
    int32_t last = write_x(c, s, left, *py);
    *py += lh;
    int32_t height = (int32_t)sq->contour - MapSeaLevel;
    int32_t threshold = *reinterpret_cast<int32_t*>(mem::host(0x68FB58)) + ClimateFutureChange;
    if ((depth < 0 && ClimateFutureChange < 0 && threshold <= height) ||
        (depth >= 0 && ClimateFutureChange > 0 && height < threshold)) {
        Buffer_set_text_color(c, 0xF9, -1, 1, 1);
        write(c, lbl(0x7FC), left, *py);
        *py += lh;
    }
    Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
    std::string line;
    if (depth >= 0) line = std::string(lbl(0x164 + (sq->val3 >> 6) * 4)) + " & ";
    if (depth < 0) {
        line.clear();
        uint32_t lm = sq->landmarks;
        if (ExpansionEnabled && (lm & 0x80008000) == 0x8000) line += Strings_get(&StringTable, Natural[15].name_short);
        if ((lm & 0x80) && (sq->climate & 0xE0) < 0x60) line += Strings_get(&StringTable, Natural[7].name_short);
        if ((lm & 0x80000400) == 0x400) line += Strings_get(&StringTable, Natural[10].name_short);
        if ((sq->items & 0x20) && (sq->climate & 0xE0) > 0x3F) {
            if (!line.empty()) line += ", ";
            line += lbl(0x398);
        }
        if (!line.empty()) {
            write(c, line, left, *py);
            *py += lh;
        }
    } else {
        line += lbl(0x50 + ((sq->climate & 0x18) >> 3) * 4);
        write(c, line, left, *py);
        *py += lh;
        line.clear();
        uint32_t lm = sq->landmarks;
        static const struct {
            uint32_t bit;
            int32_t natural;
        } kMarks[] = {{2, 1}, {1, 0}, {4, 2}, {8, 3}, {0x40, 6}, {0x100, 8}, {0x200, 9}, {0x800, 11}, {0x2000, 13}};
        for (const auto& m : kMarks) {
            if ((lm & (0x80000000u | m.bit)) != m.bit) continue;
            if (m.bit == 4 && (sq->climate & 0xE0) < 0x60) continue;
            line += Strings_get(&StringTable, Natural[m.natural].name_short);
        }
        if (ExpansionEnabled && (lm & 0x80004000) == 0x4000) line += Strings_get(&StringTable, Natural[14].name_short);
        if (!line.empty()) {
            write(c, line, left, *py);
            *py += lh;
        }
        line.clear();
        if (sq->items & 0x80) line = lbl(0x3A8);
        if (!(sq->items & 0x20) || (sq->climate & 0xE0) < 0x40) {
            if (sq->items & 0x200000) {
                if (!line.empty()) line += ", ";
                line += lbl(0x48C);
            }
        } else {
            if (!line.empty()) line += ", ";
            line += lbl(0x21C);
        }
        if (!line.empty()) {
            write(c, line, left, *py);
            *py += lh;
        }
    }
    (void)widest;
    (void)last;
    int32_t bonus = bonus_at(x, y);
    if (bonus > 0) {
        write(c, std::string(lbl(0x16C + bonus * 4)) + " " + lbl(0x17C), left, *py);
        *py += lh;
    }
    line.clear();
    uint32_t items = sq->items;
    for (int32_t i = 0; i < 0x13; i++) {
        uint32_t bit = Terraform[i].bit;
        if (!bit || !(items & bit) || i == 4 || i == 10 || i == 11) continue;
        if (i == 5 && (items & 8)) continue;
        if (i == 0 && (items & 0x80000)) continue;
        if (!line.empty()) line += ", ";
        line += Strings_get(&StringTable, (sq->climate & 0xE0) < 0x60 ? Terraform[i].name_sea : Terraform[i].name);
    }
    if (items & 0x2000) {
        if (!line.empty()) line += ", ";
        line += lbl(0x238);
    }
    if (!line.empty()) {
        UiRect wr = *r;
        wr.top = *py;
        *py = Buffer_wrap_2(c, line.c_str(), &wr);
    }
}

// The tile's resource numbers (StatusWin_draw_status's second mode, +0x15A8).
void tile_numbers(Buffer* c, int32_t x, int32_t y, int32_t left, int32_t* py, int32_t lh) {
    MAP* sq = map_tile(x, y);
    int32_t f = CurrentPlayerFaction;
    Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
    int32_t food = crop_yield(f, -1, x, y, 0);
    write(c, std::string(Strings_get(&StringTable, ResName[0].name_plural)) + ":  " + num(food), left, *py - 4);
    *py += lh - 4;
    if ((sq->val3 & 0xC0) < 0x80 && (!(sq->items & 0x20) || (sq->climate & 0xE0) < 0x40) && !(sq->items & 0xA000) &&
        (food < 2 || has_tech(Rules.tech_preq_allow_3_nutrients_sq, f) || bonus_at(x, y) == 1 ||
         (sq->items & 0x400000)))
        write(c, std::string("  ") + lbl(0xFC4) + ":  " + num(food + 1), left, *py);
    *py += lh;
    int32_t prod = mine_yield(f, -1, x, y, 0);
    write(c, std::string(Strings_get(&StringTable, ResName[1].name_plural)) + ":  " + num(prod), left, *py);
    *py += lh;
    uint32_t it = sq->items;
    if (!(it & 0x1002000) && !(it & 0x10) && (!(it & 0x20) || (sq->climate & 0xE0) < 0x40) &&
        prod < mine_yield(f, -1, x, y, 1))
        write(c, std::string("  ") + lbl(0xFC8) + ":  " + num(mine_yield(f, -1, x, y, 1)), left, *py);
    *py += lh;
    write(c, std::string(Strings_get(&StringTable, ResName[2].name_plural)) + ":  " + num(energy_yield(f, -1, x, y, 0)),
          left, *py);
    *py += lh;
    if (!(it & 0x1002000) && !(it & 0x40) && (!(it & 0x20) || (sq->climate & 0xE0) < 0x40))
        write(c, std::string("  ") + lbl(0xFCC) + ":  " + num(energy_yield(f, -1, x, y, 1)), left, *py);
    *py += lh;
}

}  // namespace

// StatusWin_set_loc (0x4B9F90).
void THISCALL StatusWin_set_loc(uint8_t* self, int32_t x, int32_t y) {
    *reinterpret_cast<int32_t*>(self + 0x15B4) = x;
    *reinterpret_cast<int32_t*>(self + 0x15B8) = y;
    *reinterpret_cast<int32_t*>(self + 0x15C0) = -1;
    *reinterpret_cast<int32_t*>(self + 0x15BC) = -1;
    ui_invalidate();
}

void THISCALL StatusWin_reset(uint8_t* self) {
    if (*reinterpret_cast<int32_t*>(self + 0x15D4)) {
        *reinterpret_cast<int32_t*>(self + 0x15D4) = 0;
        iface_release_mode(InfcList[InfcUsed]);  // SubInterface_release_iface_mode
    }
}

// StatusWin_draw_active (0x4B4CF0): the unit pane: the unit (picture, name, owner, statistics and
// abilities, morale, home base, moves, fuel, cargo, damage, the sentry / pact marks, orders and
// the terraforming turns) or the base (picture, name, owner, size, defences, production).
void THISCALL StatusWin_draw_active(uint8_t* self, int32_t veh_id, int32_t base_id, int32_t spots) {
    Buffer* c = canvas();
    if (!c->data || !ui_screen_canvas()) return;
    status_fonts();
    int32_t lh = line_h(&g_font[0]);
    Buffer_set_font(c, &g_font[0], &g_font[2], &g_font[1], nullptr);
    UiRect r = main_interface().unit_pane;
    Buffer_box_sprite(c, &r, box_def(21));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    Buffer_box_sprite(c, &r, box_def(18));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    int32_t x0 = r.left, top = r.top, w = r.right - r.left;
    int32_t shown_base = base_id;
    if (base_id < 0 && veh_id >= 0) {
        shown_base = -1;
        Buffer_set_clip(c, &r);
        int32_t pw = zoom_or(0x4B, 100);
        veh_draw(c, veh_id, (r.right - pw - r.left) / 2 + x0, top, -4, 0x48, 0);
        veh_draw(c, veh_id, x0, top, 0, 0x1800, 0);
        Buffer_set_font(c, &g_font[0], nullptr, nullptr, nullptr);
        VEH& v = Vehs[veh_id];
        int32_t y = zoom_or(0x38, 0x4B) + 8 + top;
        std::string s = Units[v.unit_id].name;
        if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) s += "(" + num(veh_id) + ")";
        Buffer_set_text_color(c, tc(0x8C6B8C), tc(0x8C6B90), 1, 1);
        y = wrap_cent_font(c, &g_font[2], s, x0, y, w);
        Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
        int32_t vf = v.faction_id;
        if (vf && vf != CurrentPlayerFaction) {
            if (!GameLanguage) {
                s = MFactions[vf].adj_name_faction;
            } else {
                GenderDefault = MFactions[vf].noun_gender;
                PluralDefault = MFactions[vf].is_noun_plural;
                s = std::string("(") + MFactions[vf].noun_faction + ")";
            }
            y = wrap_cent4(c, s, x0, y, w);
        }
        char stats[256] = "(";
        say_stats_short(stats + 1, v.unit_id);
        s = stats;
        if (v.state & 0x80) s += "#";
        for (int32_t a = 0; a < 29; a++) {
            if (!has_abil(v.unit_id, 1u << a)) continue;
            const char* n = Strings_get(&StringTable, Ability[a].abbreviation);
            if (n && *n) s += std::string(", ") + n;
        }
        s += ")";
        y = wrap_cent4(c, s, x0, y, w);
        int32_t plan = Units[v.unit_id].plan;
        if ((vf && (uint8_t)plan < 8) || (plan == 0xB && morale_veh(veh_id, 1, 0) > 1)) {
            char m[128] = "";
            say_morale(m, veh_id, 0);
            if (vf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) y = wrap3(c, m, x0, y, w);
            else y = wrap_cent4(c, m, x0, y, w);
        }
        if (vf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) {
            s = v.home_base_id < 0 ? lbl(0x308) : Bases[v.home_base_id].name;
            y = wrap3(c, s, x0, y, w);
            int32_t sp = speed(veh_id, 0);
            if (vf == CurrentPlayerFaction) {
                int32_t left = sp - v.moves_spent;
                if (left < 0 || (left < 1000 && sp == v.moves_spent)) {
                    Buffer_set_text_color(c, 0xF9, -1, 1, 1);
                    y = wrap3(c, lbl(0x9E4), x0, y, w);
                } else {
                    left = std::min(std::max(left, 0), 999);
                    int32_t mr = Rules.move_rate_roads;
                    s = std::string(lbl(0x1C)) + ": " + num(left / mr);
                    if (left % mr) s += " " + num(left % mr) + "/" + num(mr);
                    y = wrap3(c, s, x0, y, w);
                }
            }
            int32_t range = Chassis[Units[v.unit_id].chassis_id].range;
            if (range) {
                int32_t e = write_x(c, std::string(lbl(0x30C)) + ": ", x0, y);
                int32_t left = std::min(std::max(sp - v.moves_spent, 0), 999);
                int32_t mr = Rules.move_rate_roads;
                s = num(sp * ((range + (left == 0)) - v.movement_turns) - v.moves_spent);
                s = num((sp * (range + (left == 0 ? 1 : 0) - v.movement_turns) - v.moves_spent) / mr) + "/" +
                    num(sp * range / mr);
                Buffer_set_text_color(c, 0xFA, -1, 1, 1);
                if (v.movement_turns && range >> 1 <= v.movement_turns) Buffer_set_text_color(c, 0xFB, -1, 1, 1);
                if (range == 1 && left <= sp / 2) Buffer_set_text_color(c, 0xFB, -1, 1, 1);
                if (action_home(veh_id, -1)) Buffer_set_text_color(c, 0xF9, -1, 1, 1);
                write(c, s, e, y);
                y += lh;
            }
            Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
            if (veh_cargo(veh_id)) {
                s = std::string(lbl(0x360)) + ": " + num(veh_cargo(veh_id) - stack_veh(veh_id, 0)) + "/" +
                    num(veh_cargo(veh_id));
                write(c, s, x0, y);
                y += lh;
            }
        }
        if (v.damage_taken) {
            int32_t e = write_x(c, std::string(lbl(0x628)) + ": ", x0, y);
            auto hp = [&]() {
                if (Units[v.unit_id].plan == 0xC) return 1;
                int32_t rv = Units[v.unit_id].reactor_id;
                return std::min(std::max(rv, 1), 100) * 10;
            };
            Buffer_set_text_color(c, 0xFA, -1, 1, 1);
            if ((uint32_t)hp() / 3 <= v.damage_taken) Buffer_set_text_color(c, 0xFB, -1, 1, 1);
            if ((uint32_t)(hp() * 2) / 3 <= v.damage_taken) Buffer_set_text_color(c, 0xF9, -1, 1, 1);
            int32_t left = std::min(std::max(hp() - v.damage_taken, 0), 9999);
            write(c, num((hp() - left) * 100 / hp()) + "%", e, y);
            Buffer_set_text_color(c, g_iface_color(1), g_iface_color(4), 1, 1);
            y += lh;
        }
        if ((vf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) || pact(vf)) && (v.state & 0x80)) {
            Buffer_set_text_color(c, 0xFB, -1, 1, 1);
            write(c, lbl(0x1048), x0, y);
            y += lh;
        }
        Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
        if (vf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) || pact(vf)) {
            char o[256] = "";
            say_orders(o, veh_id);
            int32_t yy = wrap3(c, o, x0, y, w);
            int32_t order = (int8_t)v.order;
            if (order > 3 && order < 0x18) {
                int32_t t = action_terraform(veh_id, order - 4, 0);
                Buffer_set_text_color(c, 0xFB, -1, 1, 1);
                write(c, std::string("[") + lbl(0x71C) + ": " + num(t + 1) + "]", x0, yy);
            }
        }
        if (spots) spot_add(&g_spots, veh_id, 10, r.left, r.top, r.right - r.left, r.bottom - r.top);
        DestLinesVeh = -1;
        if ((vf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) && v.order == 0x18 &&
            !(GameMorePreferences & MPREF_MAP_HIDE_ACTIVE_VEH_GOTO_PATH)) {
            // draw_my_dest + Win_show(DesktopWin): the go-to order's path on the overlay.
            Buffer_fill_2(ui_desktop_canvas(), 9);
            MapWin_dest_line(MapWins[0], v.x, v.y, v.waypoint_x[0], v.waypoint_y[0], v.unit_id, vf,
                             Chassis[Units[v.unit_id].chassis_id].triad, 0, 3, 5, 1);
            DestLinesVeh = veh_id;
            ui_desktop_show(true);
        } else {
            ui_desktop_show(false);
        }
    } else {
        DestLinesVeh = -1;
        ui_desktop_show(false);
    }
    Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    if (shown_base >= 0 && !(veh_id >= 0 && base_id < 0)) {
        int32_t b = shown_base;
        BASE& bs = Bases[b];
        int32_t pw = zoom_or(0x4B, 100);
        base_draw(c, b, (r.right - pw - r.left) / 2 + x0, top, -4, 0x40);
        base_draw(c, b, (r.right - pw - r.left) / 2 + x0, top, 0, 0x1800);
        Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
        Buffer_set_font(c, &g_font[0], nullptr, nullptr, nullptr);
        int32_t y = zoom_or(0x38, 0x4B) + 8 + top;
        std::string s = bs.name;
        if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) s += " (" + num(b) + ")";
        y = wrap_cent_font(c, &g_font[2], s, x0, y, w);
        int32_t bf = (uint8_t)bs.faction_id;
        if (bf != CurrentPlayerFaction) {
            if (!GameLanguage) {
                s = MFactions[bf].adj_name_faction;
            } else {
                GenderDefault = MFactions[bf].noun_gender;
                PluralDefault = MFactions[bf].is_noun_plural;
                s = std::string("(") + MFactions[bf].noun_faction + ")";
            }
            y = wrap_cent4(c, s, x0, y, w);
            int32_t off, mask;
            bitmask(1, &off, &mask);
            bool hq = (bs.facilities_built[off] & (uint8_t)mask) != 0;
            const char* size;
            if (hq) size = lbl(0x5C4);
            else if (bs.pop_size < 2) size = lbl(0x2F0);
            else if (Rules.pop_limit_wo_hab_complex < bs.pop_size) size = lbl(0x570);
            else size = lbl(0x2F4);
            y = wrap_cent4(c, size, x0, y, w);
            s.clear();
            bitmask(5, &off, &mask);
            if (bs.facilities_built[off] & (uint8_t)mask) s = Strings_get(&StringTable, Facility[5].name);
            else {
                bitmask(4, &off, &mask);
                if (bs.facilities_built[off] & (uint8_t)mask) s = Strings_get(&StringTable, Facility[4].name);
            }
            if (!s.empty()) y = wrap_cent4(c, s, x0, y, w);
            if (bs.state_flags & 2) {
                Buffer_set_text_color(c, 0xF9, -1, 1, 1);
                y = wrap_cent4(c, lbl(0xB04), x0, y, w);
            }
            if (bs.nerve_staple_turns_left) {
                Buffer_set_text_color(c, 0xFB, -1, 1, 1);
                y = wrap_cent4(c, std::string(lbl(0xB08)) + ": " + num(bs.nerve_staple_turns_left), x0, y, w);
            }
        }
        Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
        if (bf == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) || pact(bf) || spying(bf)) {
            int32_t item = bs.queue_items[0], cost;
            s = std::string(lbl(0x798)) + ": ";
            if (item < 0) {
                s += Strings_get(&StringTable, Facility[-item].name);
                cost = Facility[-item].cost;
            } else {
                s += Units[item].name;
                cost = Units[item].cost;
            }
            if (item != -0x45)
                s += " (" + num(bs.minerals_accumulated) + "/" + num(cost_factor(bf, 1, -1) * cost) + ")";
            wrap_cent4(c, s, x0, y, w);
        }
        if (spots) spot_add(&g_spots, b, 1, r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    ui_invalidate();
}

// StatusWin_draw_status (0x4B6570): the tile pane (the terrain picture; the terrain words or the
// resource numbers; the coordinates), the unit pane (StatusWin_draw_active) and the stack row (the
// tile's base and units with their statistics or terraforming turns).
void THISCALL StatusWin_draw_status(uint8_t* self, int32_t atk, int32_t def, int32_t a3, int32_t a4, int32_t a5) {
    sw(0x15DC) = 0;
    if (atk >= 0 && def >= 0) {
        BattleWin_battle_report(&BattleWin, atk, def, a3, a4, (uint32_t)a5);
        return;
    }
    if (InfcList[InfcUsed] != 0) return;
    if (!ui_screen_canvas() || !ui_screen_canvas()->data) return;  // (no screen yet)
    maininfc_geometry();
    Buffer* c = canvas();
    if (!c->data) return;
    status_fonts();
    if (!g_spots.capacity) spot_init(&g_spots, 0x40);
    for (int32_t t : {0, 1, 2, 3, 10, 11}) spot_kill_type(&g_spots, t);
    int32_t lh = line_h(&g_font[0]);
    Buffer_set_font(c, &g_font[0], &g_font[2], &g_font[1], nullptr);
    int32_t shown = MapWinVehA, x, y;
    int32_t base_id = -1;
    if (!MapWinInVehMode || MapWinVehA < 0 || !CurrentTurn || sw(0x15B4) != Vehs[MapWinVehA].x ||
        sw(0x15B8) != Vehs[MapWinVehA].y) {
        x = sw(0x15B4);
        y = sw(0x15B8);
        shown = (!CurrentTurn || x < 0 || y < 0 || x >= MapAreaX || y >= MapAreaY) ? -1 : veh_at(x, y);
        base_id = (x < 0 || y < 0 || x >= MapAreaX || y >= MapAreaY) ? -1 : base_at(x, y);
        if (shown >= 0) {
            int32_t last = shown;
            while (Vehs[last].next_veh_id_stack >= 0) last = Vehs[last].next_veh_id_stack;
            if (Vehs[shown].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) {
                MAP* sq = map_tile(x, y);
                bool seen = (Factions[CurrentPlayerFaction].player_flags & 0x200) ||
                            (sq->visibility & (1 << (CurrentPlayerFaction & 31)));
                if (!seen) {
                    shown = -1;
                } else if (base_id < 0) {
                    const VEH& lv = Vehs[last];
                    if ((lv.faction_id != CurrentPlayerFaction || (lv.flags & 0x440) == 0x440) &&
                        !(lv.visibility & (1 << (CurrentPlayerFaction & 31))))
                        shown = -1;
                } else {
                    int32_t bf = (uint8_t)Bases[base_id].faction_id;
                    bool near = pact(bf) || spying(bf);
                    if (!near) {
                        for (int32_t k = 0; k < 8 && !near; k++) {
                            int32_t nx = Bases[base_id].x + NearOffsetX[k], ny = Bases[base_id].y + NearOffsetY[k];
                            if (!(MapToggleFlat & 1)) nx = (nx + MapAreaX) % MapAreaX;
                            if (ny < 0 || ny >= MapAreaY || nx < 0 || nx >= MapAreaX) continue;
                            MAP* t = map_tile(nx, ny);
                            int32_t owner = (t->items & 2) && (t->val2 & 0xF) < 8 ? (t->val2 & 0xF) : -1;
                            if (owner == CurrentPlayerFaction) near = true;
                        }
                    }
                    if ((bf != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) &&
                         !(Bases[base_id].visibility & (1 << (CurrentPlayerFaction & 31)))) ||
                        !near)
                        shown = -1;
                }
            }
        }
    } else {
        x = Vehs[MapWinVehA].x;
        y = Vehs[MapWinVehA].y;
    }
    int32_t sel_base = -1;
    if (sw(0x15BC) < 0) {
        int32_t b = sw(0x15C0);
        if (b >= 0 && Bases[b].x == x && Bases[b].y == y) sel_base = b;
    } else if (Vehs[sw(0x15BC)].x == x && Vehs[sw(0x15BC)].y == y) {
        shown = sw(0x15BC);
    }
    sw(0x15DC) = shown >= 0;
    if (sel_base >= 0 && !(GameState & STATE_OMNISCIENT_VIEW) && Bases[sel_base].faction_id != CurrentPlayerFaction &&
        !(Bases[sel_base].visibility & (1 << (CurrentPlayerFaction & 31)))) {
        sel_base = -1;
        sw(0x15C0) = -1;
    }
    // The tile pane.
    UiRect r = main_interface().tile_pane;
    Buffer_box_sprite(c, &r, box_def(21));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    Buffer_box_sprite(c, &r, box_def(18));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    Buffer_set_clip(c, &r);
    Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
    bool on_map = x >= 0 && y >= 0 && x < MapAreaX && y < MapAreaY;
    int32_t py = r.top;
    if (on_map) {
        MAP* sq = map_tile(x, y);
        if (!(GameState & STATE_OMNISCIENT_VIEW) && !(Factions[CurrentPlayerFaction].player_flags & 0x200) &&
            !(sq->visibility & (1 << (CurrentPlayerFaction & 31)))) {
            std::string s = lbl(0x300);
            Buffer_write_cent_l_7(c, s.c_str(), &r, (int32_t)s.size());
        } else {
            ZoomDenom = 100;
            ZoomNum = r.right - r.left - 4;
            MapWin_gen_terrain_poly(c, nullptr, 0, (GameState & STATE_OMNISCIENT_VIEW) ? -1 : CurrentPlayerFaction, 0,
                                    0x621, x, y, r.left + 2, r.top + 1);
            ZoomNum = -1;
            ZoomDenom = -1;
            py = (r.right - r.left) / 2 + 3 + r.top;
            if (!sw(0x15A8)) tile_words(c, x, y, &r, &py, lh);
            else tile_numbers(c, x, y, r.left, &py, lh);
        }
        if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) {
            Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
            write(c, "(" + num(sq->region) + ")", r.left + 2, r.bottom - lh);
        }
        if (((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) || py < r.bottom - lh) {
            std::string s = "(" + num(x) + " , " + num(y) + ")";
            Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
            write(c, s, r.right - 2 - Buffer_text_width_2(c, s.c_str(), (int32_t)s.size()),
                  r.bottom - lh);  // Buffer_write_right_l_5
        }
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    {
        const UiRect t = main_interface().tile_pane;
        spot_add(&g_spots, 0, 3, t.left, t.top, t.right - t.left, t.bottom - t.top);
    }
    sw(0x15C8) = -1;
    sw(0x15C4) = -1;
    sw(0x15CC) = shown;
    sw(0x15D0) = sel_base;
    StatusWin_draw_active(self, shown, sel_base, 1);
    // The stack row.
    r = main_interface().stack_pane;
    Buffer_box_sprite(c, &r, box_def(21));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    Buffer_box_sprite(c, &r, box_def(18));
    r.left += 3, r.top += 3, r.right -= 3, r.bottom -= 3;
    if (!CurrentTurn || !on_map) {
        ui_invalidate();
        return;
    }
    int32_t cw = zoom_or(0x32, 100);
    int32_t cell = cw + 0xB;
    int32_t rh = (main_interface().stack_pane.bottom - main_interface().stack_pane.top) - 0xC;
    int32_t b = base_at(x, y);
    int32_t count = 0;
    if (b >= 0) {
        if (!(GameState & STATE_OMNISCIENT_VIEW) && Bases[b].faction_id != CurrentPlayerFaction &&
            !((1 << (CurrentPlayerFaction & 31)) & Bases[b].visibility))
            b = -1;
        else count = 1;
    }
    if (shown >= 0) {
        int32_t v = shown;
        while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
        for (; v >= 0; v = Vehs[v].next_veh_id_stack) count++;
    }
    int32_t fit = (r.right - r.left) / (cw + 0x10);
    // More than fit: the row between the scroll arrows (buttons 0x3F0 / 0x3F2 shown; 0x3F1 is the
    // base window's).
    if (fit < count) {
        r.left = main_interface().arrow_left.right + 1;
        r.right = main_interface().arrow_right.left - 1;
        cell = (r.right - r.left) / fit - 5;
        stack_arrows_show(true);
    } else {
        stack_arrows_show(false);
    }
    int32_t cx = r.left, cy = r.top;
    UiRect cr{cx - 2, cy - 2, cx + cell + 2, cy + rh + 2};
    Buffer_set_clip(c, &cr);
    int32_t n = 0;
    if (b >= 0) {
        n = 1;
        base_draw(c, b, cx + 0xB, cy - 1, -8, 0x40);
        base_draw(c, b, cx, cy - 1, 0, 0x1800);
        spot_add(&g_spots, b, 0xB, cx, cy, cell + 5, rh);
        if (b == sel_base) {
            UiRect hb{cx - 2, cy - 2, cx + cell + 2, cy + rh + 2};
            Buffer_box_2(c, &hb, 0xAE, 0xAE);
        }
        cx += cell + 5;
    }
    if (shown >= 0) {
        if (!VehStackSortOff) stack_sort_2(shown);
        int32_t v = shown;
        while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
        for (; v >= 0 && n < fit; n++, v = Vehs[v].next_veh_id_stack) {
            UiRect clip{cx - 2, cy - 2, cx + cell + 2, cy + rh + 2};
            Buffer_set_clip(c, &clip);
            veh_draw(c, v, cx + 0xB, cy - 1, -8, 0x400, 0);
            veh_draw(c, v, cx, cy - 1, 0, 0x1800, 0);
            Buffer_set_font(c, &g_font[0], nullptr, nullptr, nullptr);
            const VEH& u = Vehs[v];
            int32_t order = (int8_t)u.order;
            std::string s;
            int32_t color = tc(0x8C6BAC), shadow = tc(0x8C6BB0);
            if (order < 4 || order > 0x17 ||
                (u.faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) && !pact(u.faction_id))) {
                char st[128] = "";
                say_stats_short(st, u.unit_id);
                s = st;
                if (u.state & 0x80) s += "#";
                if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) s += " (" + num(v) + ")";
            } else {
                int32_t t = action_terraform(v, order - 4, 0);
                s = num(t + 1) + " " + lbl(t == 1 ? 0xC0 : 0xC4);
                color = 0xFB;
                shadow = 3;
            }
            Buffer_set_text_color(c, color, shadow, 1, 1);
            Buffer_write_cent_l_5(c, s.c_str(), cx, g_font[0].internal_leading - lh + 1 + (cy + rh), cell,
                                  (int32_t)s.size());
            if (v == shown && sw(0x15C0) < 0) {
                UiRect hb{cx - 2, cy - 2, cx + cell + 2, cy + rh + 2};
                Buffer_box_2(c, &hb, 0xAE, 0xAE);
            }
            spot_add(&g_spots, v, 0, cx, cy, cell + 5, rh);
            cx += cell + 5;
        }
    }
    (void)count;
    Buffer_set_clip(c, &all);
    ui_invalidate();
}

void THISCALL StatusWin_draw(uint8_t* win, int32_t atk, int32_t def, int32_t offense, int32_t defense, int32_t flag) {
    StatusWin_draw_status(win, atk, def, offense, defense, flag);
}

void THISCALL StatusWin_redraw(uint8_t* self) {
    status_fonts();
    StatusWin_draw_status(self, -1, -1, -1, -1, 0);
}

// MainInterface_set_date (0x45BE80): the date and the credits box.
void maininfc_set_date() {
    int32_t m = InfcList[InfcUsed];
    if (m != 0 && m != 10) return;
    Buffer* c = canvas();
    Font* f = find_font(12, 0);  // MainInterface + 0x21694 (Arial 12)
    Buffer_set_font(c, f, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, g_iface_color(1), g_iface_color(4), 1, 1);
    UiRect d = main_interface().date, g = main_interface().globe1;
    UiRect u{std::min(d.left, g.left) - 3, std::min(d.top, g.top) - 3, std::max(d.right, g.right) + 3,
             std::max(d.bottom, g.bottom) + 3};
    Buffer_box_sprite(c, &u, box_def(21));
    Buffer_box_sprite(c, &d, box_def(18));
    int32_t x = d.left + 3, y = main_interface().date.top + 3;
    char year[64] = "";
    say_year(year);
    write(c, std::string(label_get(0)) + " " + year, x, y);
    int32_t lhh = Buffer_text_line_height(c);
    const Faction& fa = Factions[CurrentPlayerFaction];
    write(c, std::string(label_get(0x24)) + ": " + num(fa.energy_credits - fa.hurry_cost_total), x, y + lhh);
}

// MainInterface_redraw_complete (0x45C3A0): "TURN COMPLETE" in spot 5.
namespace {
int32_t g_turn_frame = 0;  // turn.flc's frame (the Flic at MainInterface + 0x24550: +0xA10)

// MainInterface_redraw_complete's test: the turn's end awaited (GameState 2 with the console
// waiting, or a network game's players done), not another player's turn of a one-at-a-time game.
bool turn_complete_shown() {
    if (!(GameState & 2)) return false;
    if (MultiplayerActive && (GameMoreRules & 0x10) && CurrentFaction != CurrentPlayerFaction) return false;
    if (!console_win(&MainConsole).waiting &&
        (!MultiplayerActive || NetTurnDoneMask == 0xFF || NetTurnDoneMask == 0))
        return false;
    return true;
}
}  // namespace

void maininfc_redraw_complete() {
    if (InfcList[InfcUsed] != 0) return;
    Buffer* c = canvas();
    UiRect r = main_interface().turn_complete;
    // turn.flc (MainInterface_init: FUN_00629E50 at the spot's top left): its first frame while the
    // turn goes on (FUN_0062A280: rewound); the turn complete, its frames in turn (FUN_0062A070,
    // the next frame at each blink2_timer tick: maininfc_blink2) -- the orange frame's animation.
    // (Its pixels are the game palette's indices as they are: the main interface's canvas is
    // synchronised to the game palette, FUN_00629E50's Buffer_sync_to_palette.)
    static Flc flc;
    static int32_t loaded = 0;  // 1 loaded, -1 missing
    if (!loaded) loaded = flc_load("turn.flc", &flc, nullptr) && !flc.frames.empty() ? 1 : -1;
    if (loaded > 0) {
        if (!turn_complete_shown()) g_turn_frame = 0;
        flc_draw(c, &flc, r.left, r.top, g_turn_frame);
    }
    uint32_t label = 0x68;
    if (MultiplayerActive && (GameState & 2) && !(GameMoreRules & 0x10) && NetTurnDoneMask != 0xFF &&
        (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))))
        label = 0x3FC;
    char text[256];
    std::snprintf(text, sizeof(text), "%s", lbl(label));
    for (char* p = text; *p; p++) *p = (char)crt_toupper((uint8_t)*p);
    static Font font;  // MainInterface + 0x2166C (MainInterface_init: Font_init("Arial", 14, 1))
    static bool font_ok = (Font_init(&font, "Arial", 14, 1), true);
    (void)font_ok;
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, 0x65, -1, 1, 1);
    Buffer_write_cent_l_7(c, text, &r, (int32_t)std::strlen(text));
}

// blink2_timer (0x50EE30, every 100 ms; start_timers): no base window, social engineering, boom,
// ControlTurnA or game not started; the TURN COMPLETE box redrawn, its animation a frame on.
void maininfc_blink2() {
    const int32_t boom = *reinterpret_cast<int32_t*>(mem::host(0x915620));  // (DAT_00915620: boom draws)
    if (Win_is_visible(&BaseWin) || Win_is_visible(&SocialWin) || boom || ControlTurnA || GameNotStarted) return;
    if (InfcList[InfcUsed] != 0) return;
    if (!turn_complete_shown()) return;
    g_turn_frame++;
    ui_invalidate();
}

// ---- WorldWin ----

namespace {
Buffer g_world_map;  // the world map MapWin's canvas (WorldWin + 0x152C, + 0x1970)
Buffer g_world_terr;  // + 0xFA4: the terrain image
Buffer g_world_final;  // + 0xA1C: with the bases
int32_t g_world_ox = 0, g_world_oy = 0;  // +0x239C4 / +0x239C8: the image's offset in the pane
}  // namespace

// WorldWin_tile_to_pixel (0x4C44E0): pane coordinates of a tile; 1 when off the map.
int32_t worldwin_tile_to_pixel(int32_t x, int32_t y, int32_t* px, int32_t* py) {
    if (!(MapToggleFlat & 1)) {
        if (x < 0) x += MapAreaX;
        else if (x >= MapAreaX) x -= MapAreaX;
    }
    int32_t maxy = MapAreaY - 1;
    if (y < 0 || maxy < 0) y = 0;
    else if (maxy < y) y = maxy;
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return 1;
    UiRect pane = main_interface().world_map;
    int32_t w = pane.right - pane.left, h = pane.bottom - pane.top;
    if (!g_world_ox && !g_world_oy) {
        *px = w * x / MapAreaX;
        *py = h * y / MapAreaY;
        return 0;
    }
    *px = g_world_map.width * x / MapAreaX + g_world_ox;
    *py = g_world_oy + g_world_map.height * y / MapAreaY;
    return 0;
}

void worldwin_draw() { worldwin_draw_to(canvas()); }

// WorldWin_on_redraw (0x4C3EC0) into a main interface's canvas at the pane +0xCFC.
void worldwin_draw_to(Buffer* c) {
    uint8_t* map = &WorldWin + 0x152C;
    int32_t q4 = (((ZoomNum < 1 ? 6 : ZoomNum * 0x32 / ZoomDenom) + 1) >> 2) * 4;
    int32_t w = (MapAreaX / 2) * q4 * 2, h = (MapAreaY / 2 - 2) * q4;
    if (w <= 0 || h <= 0) return;
    if (!ui_mapwin_native(map) || g_world_map.width != w || g_world_map.height != h) {
        Buffer_ctor(&g_world_map);
        Buffer_init(&g_world_map, w, h, 8, 0);
        Buffer_ctor(&g_world_terr);
        Buffer_init(&g_world_terr, w, h, 8, 0);
        Buffer_ctor(&g_world_final);
        Buffer_init(&g_world_final, w, h, 8, 0);
        ui_mapwin_attach(map, kMapWinVBase, &g_world_map);
        map_win(map).index = 1;
        map_win(map).active = 1;
    }
    map_win(map).pixel_x = 0;
    map_win(map).pixel_y = 0;
    map_win(map).center_x = MapAreaX / 2;
    map_win(map).zoom = -0xE;
    map_win(map).center_y = MapAreaY / 2;
    map_win(map).options = (int32_t)0x80000000;
    pfncRedrawMap(map, 0);
    Buffer_copy_4(&g_world_map, &g_world_terr, 0, 0, 0, 0, w, h);
    // MapWin_gen_bases_only: the bases over the terrain image.
    map_win(map).options = (int32_t)0x80000080;
    int32_t rows = map_win(map).rows_above + 1 + map_win(map).rows_below;
    for (int32_t r = 0; r < rows; r++) {
        int32_t ty = map_win(map).origin_y + r;
        int32_t tx = map_win(map).origin_x + (r & 1);
        int32_t cols = (r & 1) ? map_win(map).cols_odd : map_win(map).cols_even;
        for (int32_t k = 0; k < cols; k++, tx += 2) {
            int32_t wx = tx;
            if (!(MapToggleFlat & 1)) wx = (wx % MapAreaX + MapAreaX) % MapAreaX;
            if (ty < 0 || ty >= MapAreaY || wx < 0 || wx >= MapAreaX) continue;
            MAP* sq = map_tile(wx, ty);
            if (!(sq->items & 1) || (sq->val2 & 0xF) >= 8) continue;
            int32_t px, py;
            pfncTileToPoint(map, wx, ty, &px, &py);
            MapWin_draw_bases(map, wx, ty, px, py - map_win(map).half_h,
                              (GameState & STATE_OMNISCIENT_VIEW) ? -1 : CurrentPlayerFaction);
        }
    }
    Buffer_copy_4(&g_world_map, &g_world_final, 0, 0, 0, 0, w, h);
    // Into the pane: centred when it fits, else stretched.
    UiRect pane = main_interface().world_map;
    int32_t pw = pane.right - pane.left, ph = pane.bottom - pane.top;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &pane, 0);  // the window's background (GraphicWin_fill(0) of the image)
    if (w <= pw && h <= ph) {
        g_world_ox = (pw - w) / 2;
        g_world_oy = (ph - h) / 2;
        Buffer_copy_4(&g_world_final, c, 0, 0, pane.left + g_world_ox, pane.top + g_world_oy, w, h);
    } else {
        g_world_ox = g_world_oy = 0;
        for (int32_t yy = 0; yy < ph; yy++) {
            const uint8_t* s = g_world_final.data + (size_t)(yy * h / ph) * g_world_final.stride;
            uint8_t* d = c->data + (size_t)(pane.top + yy) * c->stride + pane.left;
            for (int32_t xx = 0; xx < pw; xx++) d[xx] = s[xx * w / pw];
        }
    }
    // WorldWin_draw_box: the main view's tiles.
    uint8_t* main = MapWins[0];
    if (!main || !map_win(main).cols_odd || map_win(main).pixel_x || map_win(main).pixel_y) return;
    int32_t x1, y1, x2, y2;
    if (worldwin_tile_to_pixel(map_win(main).origin_x, map_win(main).origin_y, &x1, &y1)) return;
    if (worldwin_tile_to_pixel(map_win(main).cols_even + map_win(main).origin_x + map_win(main).cols_odd,
                               map_win(main).rows_below + map_win(main).rows_above + map_win(main).origin_y, &x2, &y2))
        return;
    int32_t ox = pane.left, oy = pane.top;
    int32_t bottom = y2 - 1;
    UiRect clip = pane;
    Buffer_set_clip(c, &clip);
    if (x2 < x1) {  // the view wraps around the map's edge
        int32_t right = (g_world_ox ? g_world_map.width - 1 + g_world_ox : pw - 1);
        Buffer_hline_2(c, ox + g_world_ox, ox + x2 - 1, oy + y1, 0xFF);
        Buffer_hline_2(c, ox + g_world_ox, ox + x2 - 2, oy + bottom, 0xFF);
        Buffer_vline_2(c, ox + x2 - 1, oy + y1 + 1, oy + bottom, 0xFF);
        Buffer_hline_2(c, ox + x1 + 1, ox + right, oy + y1, 0xFF);
        Buffer_hline_2(c, ox + x1, ox + right, oy + bottom, 0xFF);
        Buffer_vline_2(c, ox + x1, oy + y1, oy + bottom - 1, 0xFF);
    } else {
        UiRect b{ox + x1, oy + y1, ox + x2, oy + bottom};
        Buffer_box_2(c, &b, 0xFF, 0xFF);
    }
    Buffer_set_clip(c, &all);
}

// The pane rectangles of MainInterface_init (0x459680) that basewin.cpp's part leaves out.
void maininfc_geometry() {
    popup_art_init();
    textcolor_load();
    basewin_mi_init();
    auto set = [](UiRect& rect, int32_t l, int32_t t, int32_t r, int32_t b) { rect = {l, t, r, b}; };
    const UiRect q = main_interface().pane_queue, right = main_interface().pane_right,
                 center = main_interface().pane_center, bottom = main_interface().pane_bottom;
    // StatusWin's panes: the unit (+0xCDC: 7/12 of the left pane), the tile (+0xCCC), the stack (+0xCEC).
    int32_t ur = (q.right - q.left) * 7 / 12;
    set(main_interface().unit_pane, q.left + 4, q.top + 4, ur - 4, q.bottom - 4);
    int32_t ucr = ur - 4;
    set(main_interface().stack_pane, ucr + 3, bottom.top + 4, bottom.right - 4, bottom.bottom - 4);
    set(main_interface().tile_pane, ucr + 4, q.top + 4, q.right - 4, (bottom.top + 4) - 3);
    // Spot 5 ("TURN COMPLETE"), the world map (+0xCFC), the date box (+0xD0C) and the globe buttons.
    set(main_interface().turn_complete, right.left + 4, right.top + 5, right.left + 0xED, right.top + 0x1C);
    int32_t wtop = right.top + 0x1C + 4, wleft = right.left + 1, wright = right.right - 1;
    set(main_interface().world_map, wleft, wtop, wright, (wright - wleft) / 2 + wtop);
    int32_t gl = right.right - 0x3C, gt = right.bottom - 0x26, gr = right.right - 6, gb = right.bottom - 6;
    set(main_interface().globe1, gl, gt, gr, gb);
    int32_t gw = (gl - gr) - 2;
    set(main_interface().globe2, gl + gw, gt, gr + gw, gb);
    set(main_interface().date, right.left + 6, right.bottom - 0x26, main_interface().globe2.left - 3, right.bottom - 6);
    // The info pane's buttons (+0xA2C 1000, +0xA3C 0x3E9, +0xA4C 0x3EA) and text rectangle (+0xD1C).
    int32_t b1 = center.right - 0x1D, b2 = center.right - 9;
    set(main_interface().info_buttons[0], b1, center.top + 9, b2, center.top + 0x1D);
    set(main_interface().info_buttons[1], b1, center.bottom - 0x1D, b2, center.bottom - 9);
    set(main_interface().info_buttons[2], b1, center.top + 0x1D + 5, b2, center.bottom - 0x1D - 5);
    set(main_interface().text, center.left + 7, center.top + 7, b1 - 10, center.bottom - 7);
}

// ==== the panes' mouse input ====================================================================

namespace {

// The stack row's scroll arrows (MainInterface_init 0x45A80E..0x45A8D3 and 0x45AE42..: buttons
// 0x3F0 / 0x3F2 with the arrows of interface.pcx (0x18D + 13 k, 0x99 / 0xC4; 12 x 42, three
// states), 4 pixels inside the stack pane, vertically centred; 0x3F1, the left arrow's art too, at
// the bottom pane's left (+0xABC) at the same height. The main screen's stack row shows 0x3F0 and
// 0x3F2, the base window's garrison row 0x3F1 and 0x3F2 (BaseWin_draw_garrison).
struct Arrow {
    Sprite spr[3];
    bool shown = false, pressed = false;
    uint32_t rect_off = 0;
    int32_t id = 0;
} g_arrow[3];  // 0x3F0, 0x3F2, 0x3F1
bool g_arrows_loaded = false;

void load_arrows() {
    if (g_arrows_loaded) return;
    g_arrows_loaded = true;
    Buffer ifc;
    Buffer_ctor(&ifc);
    if (Buffer_load_pcx(&ifc, "interface.pcx", nullptr, 10, 0xEC)) return;
    for (int32_t j = 0; j < 2; j++)
        for (int32_t k = 0; k < 3; k++)
            pfncSpriteFromCanvasRectTrans(&g_arrow[j].spr[k], &ifc, 9, 0x18D + 0xD * k, 0x99 + 0x2B * j, 0xC, 0x2A, 0);
    for (int32_t k = 0; k < 3; k++)
        pfncSpriteFromCanvasRectTrans(&g_arrow[2].spr[k], &ifc, 9, 0x18D + 0xD * k, 0x99, 0xC, 0x2A, 0);
    const UiRect s = main_interface().stack_pane;
    int32_t w = g_arrow[0].spr[0].width, h = g_arrow[0].spr[0].height;
    int32_t top = s.top + (s.bottom - h - s.top) / 2;
    main_interface().arrow_left = {s.left + 4, top, s.left + 4 + w, top + h};
    main_interface().arrow_right = {s.right - w - 4, top, s.right - 4, top + h};
    g_arrow[0].rect_off = offsetof(MainInterface, arrow_left);
    g_arrow[0].id = 0x3F0;
    g_arrow[1].rect_off = offsetof(MainInterface, arrow_right);
    g_arrow[1].id = 0x3F2;
    const int32_t bl = main_interface().pane_bottom.left;
    main_interface().rect_abc = {bl, top, bl + w, top + h};
    g_arrow[2].rect_off = offsetof(MainInterface, rect_abc);
    g_arrow[2].id = 0x3F1;
}

bool in_rect(int32_t x, int32_t y, const UiRect& r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// pfncDrawTileInfo (0x4B8890): the fonts, StatusWin_draw_status, the panes to the screen.
void draw_tile_info() {
    StatusWin_redraw(&InfoWin);
    ui_invalidate();
}

// MainInterface_on_iface_button_clicked's 0x3F0 / 0x3F1 (the top unit to the bottom of the stack)
// and 0x3F2 (the bottom unit to the top).
void arrow_clicked(int32_t id) {
    int32_t v = veh_at(sw(0x15B4), sw(0x15B8));
    if (v < 0) return;
    if (id == 0x3F2) {
        while (Vehs[v].next_veh_id_stack >= 0) v = Vehs[v].next_veh_id_stack;
        veh_promote(v);
    } else {
        veh_demote(v);
    }
    VehStackSortOff = 1;
    draw_tile_info();
    VehStackSortOff = 0;
}

// The StatusWin's selection after a menu choice or an arrow key: draw_status of the panes.
void reselect() { draw_tile_info(); }

int32_t g_dbl = 0;  // +0x15D8: a double click

}  // namespace

namespace {
void stack_arrows_show(bool on) {
    load_arrows();
    g_arrow[0].shown = g_arrow[1].shown = on;
}
}  // namespace

// StatusWin_on_left_click (0x4B8E40): a unit of the stack or the unit pane activates it; a base
// (the stack row's or the pane's) opens its window when it may be seen; the tile pane toggles the
// terrain words / resource numbers.
void THISCALL StatusWin_on_left_click(uint8_t* self, int32_t x, int32_t y) {
    if (g_dbl) return;
    InfoWin_reset(&InfoWindow);
    StatusWin_reset(self);
    BaseWinMultiplayerRefresh = 0;
    int32_t index = 0, type = 0;
    if (spot_check(&g_spots, x, y, &index, &type) < 0) return;
    switch (type) {
    case 0:
    case 10:
        if (veh_selectable(index)) {
            MapWinVehA = index;
            if (!net_lock_veh(&index, 0)) {
                veh_wake(index);
                ui_synch_veh(index);
                net_await_synch();
                net_unlock_veh();
            }
            Console_set_move(&MainConsole, 1);
        }
        break;
    case 1:
    case 0xB: {
        int32_t f = Bases[index].faction_id;
        if ((GameState & STATE_OMNISCIENT_VIEW) || f == CurrentPlayerFaction || spying(f) ||
            (Factions[CurrentPlayerFaction].diplo_status[f] & 1))
            BaseWin_zoom(&BaseWin, index, 0);
        break;
    }
    case 3:
        sw(0x15A8) = ~sw(0x15A8);
        draw_tile_info();
        break;
    default: break;
    }
}

// StatusWin_on_left_double_click (0x4B8990): the click with +0x15D8 set (nothing).
void THISCALL StatusWin_on_left_double_click(uint8_t* self, int32_t x, int32_t y) {
    g_dbl = 1;
    StatusWin_on_left_click(self, x, y);
    g_dbl = 0;
}

// StatusWin_on_right_click (0x4B9010): the tile pane's menu (terrain words, resource numbers, the
// datalinks on terrain); a unit's menu (activate it, show it; the editor's unit edit); a base's
// menu (show it, its window, production, hurry, the governor's modes); Help.
void THISCALL StatusWin_on_right_click(uint8_t* self, int32_t x, int32_t y, int32_t) {
    const UiRect& pos = main_interface().pos;
    int32_t index = 0, type = -1;
    bool hit = spot_check(&g_spots, x, y, &index, &type) >= 0;
    if (hit && type == 3) {
        // PopMenu: the three items by their position.
        PullDown pm;
        PullDown_init(&pm);
        PullDown_add_item(&pm, 0, label_get(0xFEC / 4));
        PullDown_add_item(&pm, 1, label_get(0xFF0 / 4));
        PullDown_add_item(&pm, 2, label_get(0xFF4 / 4));
        int32_t mx = 0, my = 0;
        ui_mouse_pos(&mx, &my);
        PullDown_show(&pm, mx, my);
        int32_t sel = PullDown_get_selected(&pm);
        int32_t r = sel >= 0 && sel < 0x40 ? pulldown_item_id(&pm, sel) : -1;
        if (r == 0) sw(0x15A8) = 0;
        else if (r == 1) sw(0x15A8) = -1;
        else if (r == 2) Datalink_exec(&DatalinkWin, 9, 0);
        if (r == 0 || r == 1) draw_tile_info();
        return;
    }
    PullDown pd;
    PullDown_init(&pd);
    int32_t veh = -1, base = -1;
    if (hit && type != 2) {
        if (type == 0 || type == 10) {
            veh = type == 10 ? MapWinVehA : index;
            PullDown_add_item(&pd, 6, label_get(0x958 / 4));
            PullDown_add_item(&pd, 7, label_get(0x112C / 4));
            if (GameState & STATE_SCENARIO_EDITOR) PullDown_add_item(&pd, 0x5A, label_get(0xDFC / 4));
            PullDown_add_separator(&pd, 99);
        } else {
            base = index;
            int32_t owner = Bases[base].faction_id;
            if ((GameState & STATE_OMNISCIENT_VIEW) || owner == CurrentPlayerFaction || spying(owner) ||
                (Factions[CurrentPlayerFaction].diplo_status[owner] & 1))
                PullDown_add_item(&pd, 5, label_get(0x894 / 4));
            if (owner == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) {
                static char str[6][256];
                auto ind = [&](int32_t k, uint32_t off) {
                    std::snprintf(str[k], sizeof(str[k]), " %s", label_get((int32_t)(off / 4)));
                    return str[k];
                };
                PullDown_add_item(&pd, 0x33, ind(0, 0x898));
                int32_t item = Bases[base].queue_items[0];
                const char* prod = item < 0 ? Strings_get(&StringTable, Facility[-item].name) : Units[item].name;
                std::snprintf(str[1], sizeof(str[1]), " %s %s", label_get(0x89C / 4), prod);
                PullDown_add_item(&pd, 0x34, str[1]);
                PullDown_add_item(&pd, 0x35, ind(2, 0x8A0));
                PullDown_add_item(&pd, 0x36, ind(3, 0x8A4));
                PullDown_add_item(&pd, 0x37, ind(4, 0x8A8));
                PullDown_add_item(&pd, 0x38, ind(5, 0x8AC));
                PullDown_add_separator(&pd, 99);
            }
        }
    }
    PullDown_add_item(&pd, 0x14, label_get(0x8FC / 4));
    PullDown_show(&pd, x + pos.left, y + pos.top);
    int32_t sel = PullDown_get_selected(&pd);
    int32_t id = sel >= 0 && sel < 0x40 ? pulldown_item_id(&pd, sel) : -1;
    bool locked = MultiplayerActive && (ControlTurnC || NetLocked || NetInputPending ||
                                        ((1u << CurrentPlayerFaction) & (uint32_t)NetTurnDoneMask));
    switch (id) {
    case 5: BaseWin_zoom(&BaseWin, base, 0); break;
    case 6:
        if (veh_selectable(veh)) {
            MapWinVehA = veh;
            if (!net_lock_veh(&veh, 0)) {
                veh_wake(veh);
                ui_synch_veh(veh);
                net_await_synch();
                net_unlock_veh();
            }
            Console_set_move(&MainConsole, 1);
        }
        break;
    case 7:
        sw(0x15BC) = veh;
        reselect();
        break;
    case 0x14: help_any(); break;
    case 0x33:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base);
            BaseWin_production(&BaseWin, 0, 0);
        }
        break;
    case 0x34:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base);
            BaseWin_hurry(&BaseWin);
        }
        break;
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base);
            BaseWin_gov_on(&BaseWin, 0x1000000u << (id - 0x35));
        }
        break;
    case 0x5A: Console_editor_edit_veh(&MainConsole, veh); break;
    default: break;
    }
    if (id != 5 && id != 6 && (sw(0x15C4) >= 0 || sw(0x15C8) >= 0)) {
        sw(0x15C8) = -1;
        sw(0x15C4) = -1;
        ui_invalidate();
    }
}

// StatusWin_arrow_left (0x4B89C0, ctrl+shift+left): the unit above in the stack, else the tile's
// base.
void THISCALL StatusWin_arrow_left(uint8_t*) {
    int32_t v = sw(0x15BC);
    if (v == -1) return;
    if (Vehs[v].prev_veh_id_stack >= 0) {
        sw(0x15BC) = Vehs[v].prev_veh_id_stack;
        reselect();
        return;
    }
    int32_t b = base_at(sw(0x15B4), sw(0x15B8));
    if (b >= 0) {
        sw(0x15C0) = b;
        sw(0x15BC) = -1;
        reselect();
    }
}

// StatusWin_arrow_right (0x4B8BD0, ctrl+shift+right): the unit below, or from the base the top unit.
void THISCALL StatusWin_arrow_right(uint8_t*) {
    if (sw(0x15C0) < 0) {
        int32_t v = sw(0x15BC);
        if (v >= 0 && Vehs[v].next_veh_id_stack >= 0) {
            sw(0x15BC) = Vehs[v].next_veh_id_stack;
            reselect();
        }
        return;
    }
    int32_t v = veh_at(sw(0x15B4), sw(0x15B8));
    if (sw(0x15DC) && v >= 0) {
        sw(0x15C0) = -1;
        sw(0x15BC) = v;
        reselect();
    }
}

// WorldWin_pixel_to_tile (0x4C43F0): the tile under a point of the world map pane; 1 when none.
int32_t worldwin_pixel_to_tile(int32_t px, int32_t py, int32_t* tx, int32_t* ty) {
    UiRect pane = main_interface().world_map;
    int32_t w, h;
    if (!g_world_ox && !g_world_oy) {
        w = pane.right - pane.left;
        h = pane.bottom - pane.top;
    } else {
        if (px < g_world_ox || py < g_world_oy) return 1;
        w = g_world_map.width;
        h = g_world_map.height;
        if (px >= w + g_world_ox || py >= g_world_oy + h) return 1;
        px -= g_world_ox;
        py -= g_world_oy;
    }
    if (w <= 0 || h <= 0) return 1;
    *tx = MapAreaX * px / w;
    int32_t y = MapAreaY * py / h;
    y = (*tx & 1) ? (y | 1) : (y & ~1);
    *ty = y;
    return (y >= 0 && y < MapAreaY && *tx >= 0 && *tx < MapAreaX) ? 0 : 1;
}

// WorldWin_right_menu (0x4C3AB0).
static void worldwin_right_menu(int32_t px, int32_t py, int32_t sx, int32_t sy) {
    int32_t tx = 0, ty = 0;
    bool ok = worldwin_pixel_to_tile(px, py, &tx, &ty) == 0;
    PullDown pd;
    PullDown_init(&pd);
    PullDown_add_item(&pd, 4, label_get(0xC00 / 4));
    PullDown_add_separator(&pd, 99);
    if (ok) {
        PullDown_add_item(&pd, 1, label_get(0x910 / 4));
        PullDown_add_item(&pd, 2, label_get(0x914 / 4));
    }
    PullDown_add_separator(&pd, 99);
    PullDown_add_item(&pd, 0x14, label_get(0x8FC / 4));
    PullDown_show(&pd, sx, sy);
    if (GameState & 0x800) return;
    int32_t sel = PullDown_get_selected(&pd);
    int32_t id = sel >= 0 && sel < 0x40 ? pulldown_item_id(&pd, sel) : -1;
    switch (id) {
    case 1: pfncMoveMap(&MainConsole, tx, ty, 1); break;
    case 2: ui_invalidate(); break;  // WorldWin's redraw
    case 4: worldwin_show_detail_at(map_win(MapWins[0]).center_x, map_win(MapWins[0]).center_y); break;
    case 0x14: help_any(); break;
    default: break;
    }
}

// WorldWin_click (0x4C3D40): the view centred on the tile clicked (shift: the detail map, MapWins[1]
// at the main view's centre, shown in the pane); the right button's menu.
static void worldwin_click(int32_t px, int32_t py, int32_t sx, int32_t sy, bool right) {
    BaseWinMultiplayerRefresh = 0;
    const UiRect pane = main_interface().world_map;
    if (right) {
        worldwin_right_menu(px - pane.left, py - pane.top, sx, sy);
        return;
    }
    int32_t tx = 0, ty = 0;
    if (worldwin_pixel_to_tile(px - pane.left, py - pane.top, &tx, &ty)) return;
    if (!(ui_key_state(VKEY_SHIFT) & 0x8000)) pfncMoveMap(&MainConsole, tx, ty, 1);
    else worldwin_show_detail_at(map_win(MapWins[0]).center_x, map_win(MapWins[0]).center_y);
}

// The stack arrows in the base window's copy of the main interface (BaseWin_draw_garrison shows
// MainInfc's buttons 0x3F1 / 0x3F2 when the units present do not fit), and the one at (x, y).
void stack_arrows_load() { load_arrows(); }
void stack_arrows_draw(Buffer* mi) {
    load_arrows();
    for (Arrow& a : g_arrow) {
        if (a.id == 0x3F0 || !a.spr[0].width) continue;
        const UiRect& r = mi_rect(a.rect_off);
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(&a.spr[0], mi, 9, r.left, r.top);
    }
}
int32_t stack_arrow_at(int32_t x, int32_t y) {
    load_arrows();
    for (Arrow& a : g_arrow)
        if (a.id != 0x3F0 && in_rect(x, y, mi_rect(a.rect_off))) return a.id;
    return 0;
}

void statuswin_compose(Buffer* mi) {
    load_arrows();
    for (Arrow& a : g_arrow) {
        if (!a.shown || !a.spr[0].width) continue;
        const UiRect& r = mi_rect(a.rect_off);
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(&a.spr[a.pressed ? 2 : 0], mi, 9, r.left, r.top);
    }
}

// MainInterface_on_iface_left_click / right_click / left_down / mouse_move / button_clicked for the
// status panes, the stack arrows, the world map and spot 5 (main interface coordinates).
bool statuswin_mouse(int32_t type, int32_t x, int32_t y, int32_t sx, int32_t sy) {
    load_arrows();
    for (Arrow& a : g_arrow) {
        if (!a.shown || !in_rect(x, y, mi_rect(a.rect_off))) continue;
        if (type == UiInput::LDOWN) a.pressed = true;
        if (type == UiInput::LUP && a.pressed) {
            a.pressed = false;
            arrow_clicked(a.id);
        }
        ui_invalidate();
        return true;
    }
    for (Arrow& a : g_arrow) a.pressed = false;
    if (in_rect(x, y, main_interface().turn_complete)) {  // spot 5
        if (type == UiInput::LDOWN) Console_end_my_turn(&MainConsole);
        return true;
    }
    bool panes = in_rect(x, y, main_interface().unit_pane) || in_rect(x, y, main_interface().stack_pane) ||
                 in_rect(x, y, main_interface().tile_pane);
    if (type == UiInput::MOVE) {
        // MainInterface_on_iface_mouse_move: the stack row's unit / base under the mouse in the
        // unit pane.
        int32_t index = 0, t = -1;
        if (panes && spot_check(&g_spots, x, y, &index, &t) >= 0 && (t == 0 || t == 0xB)) {
            if (t == 0xB && index != sw(0x15C8)) {
                sw(0x15C8) = index;
                sw(0x15C4) = -1;
                ui_invalidate();
            } else if (t == 0 && index != sw(0x15C4)) {
                sw(0x15C8) = -1;
                sw(0x15C4) = index;
                ui_invalidate();
            }
        } else if (sw(0x15C4) >= 0 || sw(0x15C8) >= 0) {  // StatusWin_reset_active
            sw(0x15C8) = -1;
            sw(0x15C4) = -1;
            ui_invalidate();
        }
        return panes;
    }
    if (in_rect(x, y, main_interface().world_map)) {
        if (type == UiInput::LUP) worldwin_click(x, y, sx, sy, false);
        else if (type == UiInput::RUP) worldwin_click(x, y, sx, sy, true);
        return true;
    }
    if (!panes) return false;
    if (type == UiInput::LUP) StatusWin_on_left_click(&InfoWin, x, y);
    else if (type == UiInput::RUP) StatusWin_on_right_click(&InfoWin, x, y, in_rect(x, y, main_interface().stack_pane));
    return true;
}

// The unit pane of the stack row's unit / base under the mouse after the panes' redraw.
void statuswin_hover() {
    int32_t v = sw(0x15C4), b = sw(0x15C8);
    if (v < 0 && b < 0) return;
    if ((v >= 0 && v >= VehCount) || (b >= 0 && b >= BaseCount)) return;
    StatusWin_draw_active(&InfoWin, v, v >= 0 ? -1 : b, 0);
    sw(0x15C4) = v;
    sw(0x15C8) = b;
}

void statuswin_say_morale(char* out, int32_t veh_id, int32_t faction_id) { say_morale(out, veh_id, faction_id); }

// The tile of the world map pane under the screen point (sx, sy): 0 and the tile, 1 outside it
// (pfncProcZoomKey's Win_get_mouse_pos on WorldWin and WorldWin_pixel_to_tile).
int32_t worldwin_screen_tile(int32_t sx, int32_t sy, int32_t* tx, int32_t* ty) {
    const UiRect& pos = main_interface().pos;
    const UiRect& pane = main_interface().world_map;
    int32_t px = sx - pos.left - pane.left, py = sy - pos.top - pane.top;
    if (px < 0 || py < 0 || px >= pane.right - pane.left || py >= pane.bottom - pane.top) return 1;
    return worldwin_pixel_to_tile(px, py, tx, ty);
}
