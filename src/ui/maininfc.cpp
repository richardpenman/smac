// The main interface's panes drawn by the original's code: the status panes of StatusWin (InfoWin
// 0x8C5568: StatusWin_draw_status 0x4B6570, StatusWin_draw_active 0x4B4CF0, StatusWin_redraw,
// StatusWin_set_loc, StatusWin_reset), the date box (MainInterface_set_date 0x45BE80), "TURN
// COMPLETE" (MainInterface_redraw_complete 0x45C3A0) and the world map (WorldWin_on_redraw
// 0x4C3EC0 with MapWin_gen_bases_only 0x469B40 and WorldWin_draw_box 0x4C4630), with the pane
// rectangles of MainInterface_init (0x459680). They draw into the main interface's canvas
// (basewin.cpp's MainInfc canvas), composed by console_frame.cpp.
//
// This file also replaces the headless "battle" area (src/headless/ui_headless_battle.cpp):
// BattleWin_clear / pulse_timer / stop_timer and SubInterface_set_iface_mode do nothing as there
// (the battle display, BattleWin_battle_report, is not reconstructed), tut_check2 is the same.
//
// Not reconstructed: the panes' clickable spots (StatusWin_on_left_click / on_right_click), the
// stack row's scroll arrows (buttons 0x3F0..0x3F2, shown when the tile has more units than fit),
// the world map's StretchBlt when the map is larger than the pane (nearest pixel here; the
// original's GDI stretch mode is BLACKONWHITE).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/design.h"
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
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/font_text.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/statuswin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

// ---- the battle area's other functions (as src/headless/ui_headless_battle.cpp) ----
void THISCALL SubInterface_set_iface_mode(uint8_t*) {}
void THISCALL BattleWin_clear(uint8_t*) {}
void THISCALL BattleWin_pulse_timer(uint8_t*) {}
void THISCALL BattleWin_stop_timer(uint8_t*) {}
int32_t GAMEAPI tut_check2(int32_t bit) {
    if (MultiplayerActive) return 0;
    if (!(GamePreferences & 0x20)) return 0;
    if (bit & TutorialHintsShown) return 0;
    if (MFactions[CurrentPlayerFaction].rule_flags & 0x80) return 0;
    TutorialHintsShown |= bit;
    return 1;
}

GLOBAL(int32_t, DestLinesVeh, 0x6874C4);    // the unit whose go-to path draw_my_dest shows (-1)
GLOBAL(int32_t, VehStackSortOff, 0x7D3934);  // the stack row is not sorted

namespace {

int32_t& sw(uint32_t off) { return *reinterpret_cast<int32_t*>(&InfoWin + off); }
Buffer* canvas() { return basewin_mi_canvas(); }
int32_t tc(uint32_t addr) { return text_color(addr); }
UiRect rect(uint32_t off) { return mi_rect(off); }
int32_t zoom_or(int32_t base, int32_t mul) { return ZoomNum < 1 ? base : ZoomNum * mul / ZoomDenom; }

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

int32_t fac_int(int32_t f, uint32_t off) { return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Factions[f]) + off); }

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
    std::string s = std::string(lbl(depth < 0 ? 0x30 : 0x2C)) + ": " + num(std::abs(pfncGetElevation(x, y))) + lbl(0x34);
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
        static const struct { uint32_t bit; int32_t natural; } kMarks[] = {
            {2, 1}, {1, 0}, {4, 2}, {8, 3}, {0x40, 6}, {0x100, 8}, {0x200, 9}, {0x800, 11}, {0x2000, 13}};
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
    UiRect r = rect(0xCDC);
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
        if ((GameState & 0x40) && (GameState & 0x1000)) s += "(" + num(veh_id) + ")";
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
        say_stats_3(stats + 1, v.unit_id);
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
            if (vf == CurrentPlayerFaction || (GameState & 0x80)) y = wrap3(c, m, x0, y, w);
            else y = wrap_cent4(c, m, x0, y, w);
        }
        if (vf == CurrentPlayerFaction || (GameState & 0x80)) {
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
                s = num(sp * ((range + (left == 0)) - v.movement_turns) - v.moves_spent) ;
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
        if ((vf == CurrentPlayerFaction || (GameState & 0x80) || pact(vf)) && (v.state & 0x80)) {
            Buffer_set_text_color(c, 0xFB, -1, 1, 1);
            write(c, lbl(0x1048), x0, y);
            y += lh;
        }
        Buffer_set_text_color(c, tc(0x8C6B94), tc(0x8C6B98), 1, 1);
        if (vf == CurrentPlayerFaction || (GameState & 0x80) || pact(vf)) {
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
        if (spots) {}  // Spot_add(veh, 10, pane): the pane's spot (clicks: not reconstructed)
        DestLinesVeh = -1;
        if ((vf == CurrentPlayerFaction || (GameState & 0x80)) && v.order == 0x18 && !(GameMorePreferences & 0x2000000)) {
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
        if ((GameState & 0x40) && (GameState & 0x1000)) s += " (" + num(b) + ")";
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
        if (bf == CurrentPlayerFaction || (GameState & 0x80) || pact(bf) || spying(bf)) {
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
    }
    ui_invalidate();
}

// StatusWin_draw_status (0x4B6570): the tile pane (the terrain picture; the terrain words or the
// resource numbers; the coordinates), the unit pane (StatusWin_draw_active) and the stack row (the
// tile's base and units with their statistics or terraforming turns).
void THISCALL StatusWin_draw_status(uint8_t* self, int32_t atk, int32_t def, int32_t a3, int32_t a4, int32_t a5) {
    sw(0x15DC) = 0;
    if (atk >= 0 && def >= 0) return;  // BattleWin_battle_report: not reconstructed
    if (InfcList[InfcUsed] != 0) return;
    if (!ui_screen_canvas() || !ui_screen_canvas()->data) return;  // (no screen yet)
    maininfc_geometry();
    Buffer* c = canvas();
    if (!c->data) return;
    status_fonts();
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
            if (Vehs[shown].faction_id != CurrentPlayerFaction && !(GameState & 0x80)) {
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
                    if ((bf != CurrentPlayerFaction && !(GameState & 0x80) &&
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
    if (sel_base >= 0 && !(GameState & 0x80) && Bases[sel_base].faction_id != CurrentPlayerFaction &&
        !(Bases[sel_base].visibility & (1 << (CurrentPlayerFaction & 31)))) {
        sel_base = -1;
        sw(0x15C0) = -1;
    }
    // The tile pane.
    UiRect r = rect(0xCCC);
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
        if (!(GameState & 0x80) && !(Factions[CurrentPlayerFaction].player_flags & 0x200) &&
            !(sq->visibility & (1 << (CurrentPlayerFaction & 31)))) {
            std::string s = lbl(0x300);
            Buffer_write_cent_l_7(c, s.c_str(), &r, (int32_t)s.size());
        } else {
            ZoomDenom = 100;
            ZoomNum = r.right - r.left - 4;
            MapWin_gen_terrain_poly(c, nullptr, 0, (GameState & 0x80) ? -1 : CurrentPlayerFaction, 0, 0x621, x, y,
                                    r.left + 2, r.top + 1);
            ZoomNum = -1;
            ZoomDenom = -1;
            py = (r.right - r.left) / 2 + 3 + r.top;
            if (!sw(0x15A8)) tile_words(c, x, y, &r, &py, lh);
            else tile_numbers(c, x, y, r.left, &py, lh);
        }
        if ((GameState & 0x40) && (GameState & 0x1000)) {
            Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
            write(c, "(" + num(sq->region) + ")", r.left + 2, r.bottom - lh);
        }
        if (((GameState & 0x40) && (GameState & 0x1000)) || py < r.bottom - lh) {
            std::string s = "(" + num(x) + " , " + num(y) + ")";
            Buffer_set_text_color(c, tc(0x8C6BA4), tc(0x8C6BA8), 1, 1);
            write(c, s, r.right - 2 - Buffer_text_width_2(c, s.c_str(), (int32_t)s.size()), r.bottom - lh);  // Buffer_write_right_l_5
        }
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    sw(0x15C8) = -1;
    sw(0x15C4) = -1;
    sw(0x15CC) = shown;
    sw(0x15D0) = sel_base;
    StatusWin_draw_active(self, shown, sel_base, 1);
    // The stack row.
    r = rect(0xCEC);
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
    int32_t rh = (rect(0xCEC).bottom - rect(0xCEC).top) - 0xC;
    int32_t b = base_at(x, y);
    int32_t count = 0;
    if (b >= 0) {
        if (!(GameState & 0x80) && Bases[b].faction_id != CurrentPlayerFaction &&
            !((1 << (CurrentPlayerFaction & 31)) & Bases[b].visibility))
            b = -1;
        else
            count = 1;
    }
    if (shown >= 0) {
        int32_t v = shown;
        while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
        for (; v >= 0; v = Vehs[v].next_veh_id_stack) count++;
    }
    int32_t fit = (r.right - r.left) / (cw + 0x10);
    // (more than fit: the scroll arrows' layout; not reconstructed, the row is clipped)
    int32_t cx = r.left, cy = r.top;
    UiRect cr{cx - 2, cy - 2, cx + cell + 2, cy + rh + 2};
    Buffer_set_clip(c, &cr);
    int32_t n = 0;
    if (b >= 0) {
        n = 1;
        base_draw(c, b, cx + 0xB, cy - 1, -8, 0x40);
        base_draw(c, b, cx, cy - 1, 0, 0x1800);
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
                (u.faction_id != CurrentPlayerFaction && !(GameState & 0x80) && !pact(u.faction_id))) {
                char st[128] = "";
                say_stats_3(st, u.unit_id);
                s = st;
                if (u.state & 0x80) s += "#";
                if ((GameState & 0x40) && (GameState & 0x1000)) s += " (" + num(v) + ")";
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
    UiRect d = rect(0xD0C), g = rect(0xA9C);
    UiRect u{std::min(d.left, g.left) - 3, std::min(d.top, g.top) - 3, std::max(d.right, g.right) + 3,
             std::max(d.bottom, g.bottom) + 3};
    Buffer_box_sprite(c, &u, box_def(21));
    Buffer_box_sprite(c, &d, box_def(18));
    int32_t x = d.left + 3, y = rect(0xD0C).top + 3;
    char year[64] = "";
    say_year(year);
    write(c, std::string(label_get(0)) + " " + year, x, y);
    int32_t lhh = Buffer_text_line_height(c);
    const Faction& fa = Factions[CurrentPlayerFaction];
    write(c, std::string(label_get(0x24)) + ": " + num(fa.energy_credits - fa.hurry_cost_total), x, y + lhh);
}

// MainInterface_redraw_complete (0x45C3A0): "TURN COMPLETE" in spot 5.
void maininfc_redraw_complete() {
    if (InfcList[InfcUsed] != 0) return;
    Buffer* c = canvas();
    UiRect r = rect(0xA7C);
    uint32_t label = 0x68;
    if (MultiplayerActive && (GameState & 2) && !(GameMoreRules & 0x10) && NetTurnDoneMask != 0xFF &&
        (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))))
        label = 0x3FC;
    char text[256];
    std::snprintf(text, sizeof(text), "%s", lbl(label));
    for (char* p = text; *p; p++) *p = (char)crt_toupper((uint8_t)*p);
    // (turn.flc: the original plays the turn animation frame under the text, frame 0 while the
    // turn goes on; the FLC is not decoded: the spot keeps the console art)
    Buffer_set_font(c, find_font(14, 1), nullptr, nullptr, nullptr);  // MainInterface + 0x2166C
    Buffer_set_text_color(c, 0x65, -1, 1, 1);
    // (the turn's state chooses turn.flc's frame only; the text is always written)
    Buffer_write_cent_l_7(c, text, &r, (int32_t)std::strlen(text));
}

// ---- WorldWin ----

namespace {
Buffer g_world_map;    // the world map MapWin's canvas (WorldWin + 0x152C, + 0x1970)
Buffer g_world_terr;   // + 0xFA4: the terrain image
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
    UiRect pane = rect(0xCFC);
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

// WorldWin_on_redraw (0x4C3EC0) into the main interface's canvas at the pane +0xCFC.
void worldwin_draw() {
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
        mw(map, kMwIndex) = 1;
        mw(map, kMwActive) = 1;
    }
    mw(map, kMwPixelX) = 0;
    mw(map, kMwPixelY) = 0;
    mw(map, kMwCenterX) = MapAreaX / 2;
    mw(map, kMwZoom) = -0xE;
    mw(map, kMwCenterY) = MapAreaY / 2;
    mw(map, kMwOptions) = (int32_t)0x80000000;
    pfncRedrawMap(map, 0);
    Buffer_copy_4(&g_world_map, &g_world_terr, 0, 0, 0, 0, w, h);
    // MapWin_gen_bases_only: the bases over the terrain image.
    mw(map, kMwOptions) = (int32_t)0x80000080;
    int32_t rows = mw(map, kMwRowsAbove) + 1 + mw(map, kMwRowsBelow);
    for (int32_t r = 0; r < rows; r++) {
        int32_t ty = mw(map, kMwOriginY) + r;
        int32_t tx = mw(map, kMwOriginX) + (r & 1);
        int32_t cols = (r & 1) ? mw(map, kMwColsOdd) : mw(map, kMwColsEven);
        for (int32_t k = 0; k < cols; k++, tx += 2) {
            int32_t wx = tx;
            if (!(MapToggleFlat & 1)) wx = (wx % MapAreaX + MapAreaX) % MapAreaX;
            if (ty < 0 || ty >= MapAreaY || wx < 0 || wx >= MapAreaX) continue;
            MAP* sq = map_tile(wx, ty);
            if (!(sq->items & 1) || (sq->val2 & 0xF) >= 8) continue;
            int32_t px, py;
            pfncTileToPoint(map, wx, ty, &px, &py);
            MapWin_draw_bases(map, wx, ty, px, py - mw(map, kMwHalfH), (GameState & 0x80) ? -1 : CurrentPlayerFaction);
        }
    }
    Buffer_copy_4(&g_world_map, &g_world_final, 0, 0, 0, 0, w, h);
    // Into the pane: centred when it fits, else stretched.
    Buffer* c = canvas();
    UiRect pane = rect(0xCFC);
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
    if (!main || !mw(main, kMwColsOdd) || mw(main, kMwPixelX) || mw(main, kMwPixelY)) return;
    int32_t x1, y1, x2, y2;
    if (worldwin_tile_to_pixel(mw(main, kMwOriginX), mw(main, kMwOriginY), &x1, &y1)) return;
    if (worldwin_tile_to_pixel(mw(main, kMwColsEven) + mw(main, kMwOriginX) + mw(main, kMwColsOdd),
                               mw(main, kMwRowsBelow) + mw(main, kMwRowsAbove) + mw(main, kMwOriginY), &x2, &y2))
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
    auto set = [](uint32_t off, int32_t l, int32_t t, int32_t r, int32_t b) { mi_rect(off) = {l, t, r, b}; };
    const UiRect q = rect(kMiPaneQueue), right = rect(kMiPaneRight), center = rect(kMiPaneCenter),
                 bottom = rect(kMiPaneBottom);
    // StatusWin's panes: the unit (+0xCDC: 7/12 of the left pane), the tile (+0xCCC), the stack (+0xCEC).
    int32_t ur = (q.right - q.left) * 7 / 12;
    set(0xCDC, q.left + 4, q.top + 4, ur - 4, q.bottom - 4);
    int32_t ucr = ur - 4;
    set(0xCEC, ucr + 3, bottom.top + 4, bottom.right - 4, bottom.bottom - 4);
    set(0xCCC, ucr + 4, q.top + 4, q.right - 4, (bottom.top + 4) - 3);
    // Spot 5 ("TURN COMPLETE"), the world map (+0xCFC), the date box (+0xD0C) and the globe buttons.
    set(0xA7C, right.left + 4, right.top + 5, right.left + 0xED, right.top + 0x1C);
    int32_t wtop = right.top + 0x1C + 4, wleft = right.left + 1, wright = right.right - 1;
    set(0xCFC, wleft, wtop, wright, (wright - wleft) / 2 + wtop);
    int32_t gl = right.right - 0x3C, gt = right.bottom - 0x26, gr = right.right - 6, gb = right.bottom - 6;
    set(0xA9C, gl, gt, gr, gb);
    int32_t gw = (gl - gr) - 2;
    set(0xA8C, gl + gw, gt, gr + gw, gb);
    set(0xD0C, right.left + 6, right.bottom - 0x26, mi_rect(0xA8C).left - 3, right.bottom - 6);
    // The info pane's buttons (+0xA2C 1000, +0xA3C 0x3E9, +0xA4C 0x3EA) and text rectangle (+0xD1C).
    int32_t b1 = center.right - 0x1D, b2 = center.right - 9;
    set(0xA2C, b1, center.top + 9, b2, center.top + 0x1D);
    set(0xA3C, b1, center.bottom - 0x1D, b2, center.bottom - 9);
    set(0xA4C, b1, center.top + 0x1D + 5, b2, center.bottom - 0x1D - 5);
    set(0xD1C, center.left + 7, center.top + 7, b1 - 10, center.bottom - 7);
}
