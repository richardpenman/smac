// The base window's production picker (the "ProdWin" member of BaseWin at +0xA1C, ui
// 0x492420..0x494700): a grid of pictures of what the base can build, opened by
// BaseWin_production (basewin_prod.cpp) for the current production (slot 0) or a queue slot.
//
// Reconstructed from the asm: the list (FUN_00492FA0: the prototypes in proto_sort order with
// their build time and stats, then the facilities with their time and effect; the selection on
// the slot's item), the window (FUN_00494700: the base window's size, the grid of 5 columns of
// (width - 0x51) / 5 by 108 pixels, 3 rows (4 above 800 pixels), the buttons HELP / WORKSHOP /
// OK / [RESET] / CANCEL or, for a queue slot, HELP / OK / REPLACE / INSERT / DELETE / CANCEL in a
// frame below), a cell (FUN_004927C0: frame box 0 / 2 when selected, the name in the item's
// mandate colour, the picture (a facility's in the mandate colour, a unit by veh_draw with its 3D
// model turned 30 degrees), the build time and the stats at the bottom, "Prototype +n%"), the
// keys (FUN_00494280: arrows / keypad by cell, Home, End, PgUp / PgDn by page; Enter OK, Esc
// Cancel), the mouse (a click selects the cell under the point (FUN_004941C0: the point is not
// offset by the grid's margin, as the original), a double click chooses it, a right click opens
// the menu: set production / add to queue / obsolete / cancel all obsolete / help
// (FUN_00493EA0)), the buttons (FUN_004945D0: the mode returned to BaseWin_production) and the
// dimmed base window under it (BaseWin_production's window: the base window's canvas with the
// tile at +0xBDD0 over it).
// The scroll bar (FUN_00494700: Scroll_init at (width - 0x14, 0x15), 0x18 x 0x13A, scroll_normal,
// when the list has more cells than shown; its position top / cols, range 0 .. count / cols -
// rows + 2, page rows - 1; scroll.cpp): its buttons move a row, the track a page. The unit spin
// timer (FUN_004946D0) is never started by the original.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/proto.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "ui/basewin_internal.h"
#include "ui/scroll.h"
#include "ui/font_text.h"
#include "ui/mapwin.h"
#include "ui/pulldown.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
#include "ui/vehdraw.h"
#include "ui/voxel.h"
#include "ui/win.h"

Sprite* basewin_facility_picture(int32_t fac);  // basewin_draw.cpp

namespace {

struct ProdItem {        // the three StringStruct lists of the window (+0xA788, +0xA7B8, +0xA7E8)
    int32_t id = 0;      // unit id or facility id
    uint32_t flags = 0;  // mandate (0..3) | 0x10 facility | 0x20 unit | 0x80 prototype cost
    std::string name, turns, stats;
};

struct ProdWin : ScreenWin {
    std::vector<ProdItem> items;
    int32_t sel = -1;      // +0xA774
    int32_t mode = 0;      // +0xA778: the button's action (FUN_004945D0)
    int32_t top = 0;       // +0xA77C: first shown cell
    int32_t cur = 0;       // the lists' current entry (StringStruct_current_entry)
    int32_t faction = 0;   // +0xA76C
    int32_t slot = 0;      // +0xB68: the queue slot (0: production)
    int32_t spin = 0;      // +0xB64
    int32_t cols = 5, rows = 3, cw = 0, ch = 0x6C, visible = 0;  // +0xA810.. +0xA82C
    int32_t nbuttons = 0, bwidth = 100;  // +0xB6C, +0xB70
    Font f_name, f_big, f_small, f_button;  // +0xA9C, +0xAC4, +0xAEC, +0xB14
    VoxelScene scene;                       // +0x926C
    bool scene_ready = false, shown = false;
    Buffer dim;                             // BaseWin_production's window under the picker
    void redraw() override;
};

ProdWin g_pw;

int32_t line_h(const Font* f) { return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0; }

// FUN_00493AF0: the mandate whose colour a unit is drawn in.
uint32_t unit_mandate(int32_t u) {
    if (u >= 0x40 && !(Units[u].unit_flags & 4)) return 1;
    switch (Units[u].plan) {
    case 3: case 7: case 8: return 0;
    case 9: case 10: return 2;
    case 0xB: return 1;
    default: return 3;
    }
}

// FUN_00493B70.
uint32_t facility_mandate(int32_t fac) {
    if (fac > 0x45) return 1;
    switch (fac) {
    case 3: case 0x1F: case 0x20: return 0;
    case 4: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x44: return 3;
    case 8: case 0xF: case 0x10: case 0x11: return 1;
    default: return 2;
    }
}

std::string num(int32_t v) { return std::to_string(v); }

// FUN_00492FA0: the list of what the base can build.
void build_list(ProdWin* w) {
    BASE& b = Bases[CurrentBaseID];
    w->items.clear();
    w->cur = 0;
    w->sel = -1;
    w->top = 0;
    int32_t current = b.queue_items[w->slot];
    int32_t last = b.production_id_last;
    int32_t surplus = b.mineral_surplus;
    proto_sort(w->faction);
    for (int32_t i = 0; i < 128; i++) {
        int32_t u = ProtoSortList[i];
        if (!veh_avail(u, w->faction, CurrentBaseID)) continue;
        ProdItem it;
        it.id = u;
        it.name = Units[u].name;
        if (GameState & STATE_OMNISCIENT_VIEW) {
            it.name += "(" + num(Factions[w->faction].units_active[u]) + "/" + num(Factions[w->faction].units_queue[u]) + ")";
        }
        int32_t proto = 0;
        int32_t base_cost = cost_factor((uint8_t)b.faction_id, 1, -1) * Units[u].cost;
        int32_t cost = cost_factor((uint8_t)b.faction_id, 1, -1) * veh_cost(u, CurrentBaseID, &proto);
        if (surplus < 1) {
            it.turns = "-" + std::string(label_get(0x10C / 4)) + "-";
        } else {
            if (w->slot == 0) {
                int32_t m = b.minerals_accumulated_2;
                if (base_making(u, CurrentBaseID) != base_making(last, CurrentBaseID) &&
                    Factions[w->faction].diff_level >= 2 && Rules.retool_exemption < m)
                    m = (m - Rules.retool_exemption) * (100 - Rules.retool_penalty_prod_change) / 100 + Rules.retool_exemption;
                base_cost -= m;
                cost -= m;
            }
            int32_t t = (surplus - 1 + cost) / surplus;
            t = std::min(std::max(t, 0), 999999);
            it.turns = num(t);
            if (base_cost != cost && Units[u].plan != 8) {
                int32_t t2 = (surplus - 1 + base_cost) / surplus;
                t2 = std::min(std::max(t2, 0), 999999);
                it.turns += "(" + num(t2) + ")";
            }
            it.turns += " ";
            it.turns += label_get(0xC4 / 4);
        }
        char stats[256] = "";
        say_stats_short(stats, u);
        it.stats = stats;
        it.flags = unit_mandate(u) | (proto ? 0x80u : 0u) | 0x20u;
        if (u == current) w->sel = (int32_t)w->items.size();
        w->items.push_back(it);
    }
    for (int32_t fac = 1; fac <= 0x85; fac++) {
        if (!facility_avail(fac, (uint8_t)b.faction_id, CurrentBaseID, w->slot)) continue;
        ProdItem it;
        it.id = fac;
        if (fac == 1) it.name = "(" + std::string(label_get(0x230 / 4)) + ") ";
        it.name += Strings_get(&StringTable, Facility[fac].name);
        int32_t cost = cost_factor((uint8_t)b.faction_id, 1, -1) * Facility[fac].cost;
        if (w->slot == 0) {
            if (base_making(-fac, CurrentBaseID) == base_making(last, CurrentBaseID) || Factions[w->faction].diff_level < 2)
                cost -= b.minerals_accumulated;
            else
                cost -= b.minerals_accumulated / 2;
        }
        if (fac == 0x45) {
            int32_t e = std::min(std::max((surplus + 1) / 2, 0), 9999);
            it.turns = num(e) + " " + label_get(0x90 / 4) + " " + label_get(0x264 / 4);
        } else if (surplus > 0) {
            it.turns = num((surplus - 1 + cost) / surplus) + " " + label_get(0xC4 / 4);
        } else {
            it.turns = "-" + std::string(label_get(0x10C / 4)) + "-";
        }
        const char* effect = Strings_get(&StringTable, Facility[fac].effect);
        if (effect && *effect) it.stats = effect;
        it.flags = facility_mandate(fac) | 0x10u;
        if (-current == fac) w->sel = (int32_t)w->items.size();
        w->items.push_back(it);
    }
    std::fprintf(stderr, "prodwin items:");
    for (const ProdItem& it : w->items) std::fprintf(stderr, " %d", (it.flags & 0x20) ? it.id : -it.id);
    std::fprintf(stderr, "\n");
    if (w->items.empty()) return;
    int32_t width = g_bw.canvas.width;
    w->cols = 5;
    w->rows = (ScreenWidth > 800) + 3;
    w->cw = (width - 0x51) / 5;
    w->ch = 0x6C;
    w->cols = (width - 0x51) / w->cw;
    w->visible = w->rows * w->cols;
    Font_init(&w->f_name, "Arial", 0xC, 1);
    Font_init(&w->f_big, "Arial", std::max(w->cw / 0xF, 0x18), 1);
    Font_init(&w->f_small, "Arial", std::max(w->cw / 0xF, 10), 0);
    Font_init(&w->f_button, "Arial", 0xE, 1);
}

// The lists' current entry for `index` (StringStruct's walk: out of range keeps the last one).
const ProdItem* entry(ProdWin* w, int32_t index) {
    if (index <= (int32_t)w->items.size() - 1) {
        if (index < 0) {
            if (-index <= (int32_t)w->items.size()) w->cur = index + (int32_t)w->items.size();
        } else {
            w->cur = index;
        }
    }
    if (w->items.empty()) return nullptr;
    return &w->items[std::min(std::max(w->cur, 0), (int32_t)w->items.size() - 1)];
}

// FUN_004927C0: cell `i`.
void draw_cell(ProdWin* w, int32_t i) {
    if (i < w->top || i >= w->top + w->visible) return;
    Buffer* c = &w->canvas;
    int32_t k = i - w->top;
    int32_t row = k / w->cols, col = k % w->cols;
    UiRect r{col * w->cw + 0x1B, row * w->ch + 4, w->cw + col * w->cw + 0x18, row * w->ch + w->ch + 1};
    Buffer_set_clip(c, &r);
    Buffer_box_sprite(c, &r, box_def(w->sel == i ? 2 : 0));
    if (i >= (int32_t)w->items.size()) {
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        return;
    }
    r.left++, r.right--, r.top++, r.bottom--;
    const ProdItem* it = entry(w, i);
    int32_t id = (it->flags & 0x20) ? it->id : -it->id;
    int32_t color = mandate_color((int32_t)(it->flags & 0xF));
    Buffer_set_text_color(c, color, -1, 1, 1);
    Buffer_set_font(c, &w->f_name, nullptr, nullptr, nullptr);
    UiRect t{r.left, r.top + 5, r.right, r.top + line_h(&w->f_name) * 3 + 5};
    Buffer_wrap_cent_6(c, it->name.c_str(), &t);
    w->f_small.field_0 = 3;
    Buffer_set_text_color(c, 0xA1, -1, 1, 1);
    Buffer_set_font(c, &w->f_small, nullptr, nullptr, nullptr);
    t = {r.left + 2, r.top, r.right - 2, r.bottom};
    int32_t h = Buffer_wrap_height(c, it->stats.c_str(), t.right - t.left);
    t.top = t.bottom - h;
    Buffer_wrap_cent_6(c, it->stats.c_str(), &t);
    t.bottom = t.top;
    t.top -= line_h(&w->f_small);
    if (!it->turns.empty()) Buffer_write_cent_l_7(c, it->turns.c_str(), &t, (int32_t)it->turns.size());
    if (id < 0) {
        int32_t s = w->ch * 3 / 7, fac = -id;
        Sprite* sp = basewin_facility_picture(fac);
        int32_t src = fac < 0x46 ? basewin_facility_picture(0)->full_width : basewin_facility_picture(0x46)->full_width;
        int32_t od = BlitDestScale, os = BlitSourceScale;
        BlitDestScale = s;
        BlitSourceScale = src;
        Sprite_draw_mono_2(sp, c, sp->key & 0xFF, (r.right - r.left) / 2 - s / 2 + r.left,
                           (r.bottom - r.top) / 2 - s / 2 + r.top, color);
        BlitDestScale = od;
        BlitSourceScale = os;
    } else {
        int32_t s = w->ch * 2 / 3, hh = s * 0x4B / 100;
        int32_t fake = veh_fake(id, (uint8_t)Bases[CurrentBaseID].faction_id);
        int32_t angle = w->sel == i ? w->spin : 0;
        ZoomNum = s;
        ZoomDenom = 100;
        if (veh_draw(c, fake, (r.right - r.left) / 2 - s / 2 + r.left, (r.bottom - r.top) / 2 - hh / 2 + r.top, 1, 0xCC, 0)) {
            if (!w->scene_ready) {
                Scene_ctor(&w->scene);
                Scene_init(&w->scene);
                w->scene_ready = true;
            }
            // FUN_00494700's camera: angles (0, 0, 0.785), the light and the shadow straight down.
            float angles[3] = {0.0f, 0.0f, 0.785f}, mat[9];
            mat3_from_euler(angles, mat);
            mat[6] = -mat[6];
            mat[7] = -mat[7];
            mat[8] = -mat[8];
            const float pos[3] = {(float)((double)mat[6] * -10000.0f), (float)((double)mat[7] * -10000.0f),
                                  (float)((double)mat[8] * -10000.0f)};
            Scene_set_camera(&w->scene, pos, mat);
            const float down[3] = {0.0f, 0.0f, 1.0f};
            std::memcpy(w->scene.v_shadow, down, 12);
            std::memcpy(w->scene.v_light, down, 12);
            w->scene.count = 0;
            vehdraw_construct_vehicle(fake, &w->scene);
            Scene_set_scale(&w->scene, (float)((double)s * 0.0007));
            Scene_set_rotation(&w->scene, (float)((double)(angle + 0x1E) * 0.017444445), 0.0f, 0.0f);
            Scene_render(&w->scene, c, (r.right - r.left) / 2 + r.left, (r.bottom - r.top) / 2 + r.top, 0);
        }
        ZoomNum = -1;
        ZoomDenom = -1;
        int32_t proto = 0;
        veh_cost(id, CurrentBaseID, &proto);
        if (proto) {
            Buffer_set_font(c, &w->f_small, nullptr, nullptr, nullptr);
            Buffer_set_text_color(c, 0xFA, -1, 1, 1);
            std::string p = std::string(label_get(0x2E4 / 4)) + " +" + num(prototype_factor(id)) + "%";
            Buffer_write_cent_l_7(c, p.c_str(), &r, (int32_t)p.size());
        }
    }
    if (w->sel == i) Buffer_box_2(c, &r, 0xFF, 0xFF);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    ui_invalidate();
}

ScrollBar scroll_bar(ProdWin* w);

void ProdWin::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    // BaseWin_production's window: the base window's canvas from x 0x17, the tile over it.
    Buffer_fill_3(c, &all, 9);
    int32_t dw = c->width - 0x2E, dh = c->height;
    Buffer_copy_4(&g_bw.canvas, c, 0x17, 0, 0x17, 0, dw, dh);
    UiRect dr{0x17, 0, 0x17 + dw, dh};
    Buffer_set_clip(c, &dr);
    Buffer_tile_2(c, bw_sprite_at(0xBDD0), 0, 0, 0x17 + 3, 3, dw - 6, dh - 6);
    Buffer_set_clip(c, &all);
    // The picker's frame: box 0 with a 10 x 10 crop of that tile as its centre.
    static Buffer centre;
    if (!centre.data) {
        Buffer_ctor(&centre);
        Buffer_init(&centre, 10, 10, 8, 0);
        Sprite* tile = bw_sprite_at(0xBDD0);
        sprite_copy1(tile, &centre, tile->key & 0xFF, 0, 0, 1, 1);
    }
    BoxDef box = *box_def(0);
    box.center = &centre;
    UiRect fr{0x17, 0, c->width - 0x37, c->height};
    Buffer_box_sprite(c, &fr, &box);
    // The buttons' frame (box 3).
    if (nbuttons) {
        int32_t x0 = ((c->width - (bwidth + 3) * nbuttons) - 0x20) / 2 + (nbuttons - 1) * (bwidth + 3);
        int32_t y = ch * rows + 8, left = x0 - (nbuttons - 1) * (bwidth + 3);
        UiRect br{left - 3, y - 3, x0 + bwidth + 3, ch * rows + 0x1C + 3};
        Buffer_box_sprite(c, &br, box_def(3));
    }
    for (int32_t i = top; i < visible + top; i++) draw_cell(this, i);
    if (visible < (int32_t)items.size()) scroll_draw(c, scroll_bar(this));
    ui_invalidate();
}

// FUN_004945D0: a button.
void on_button(ProdWin* w, int32_t id, bool* done) {
    switch (id) {
    case 0: if (w->sel == -1) return; w->mode = 1; break;
    case 1: w->mode = 2; break;
    case 2: if (w->sel == -1) return; w->mode = 7; break;
    case 7: w->mode = 7; break;
    case 3: w->mode = 3; break;
    case 4: w->mode = 4; break;
    case 5: if (w->sel == -1) return; w->mode = 5; break;
    case 6: w->mode = 6; break;
    case -2: w->mode = 0; w->sel = -1; break;
    case -1: w->mode = 0; break;
    default: return;
    }
    *done = true;  // Win_release_modal
    if (w->slot != 0 && (id == -1 || id == -2)) w->shown = false;  // hide
}

// The scroll bar of FUN_00494700.
ScrollBar scroll_bar(ProdWin* w) {
    ScrollBar s;
    s.r = {w->canvas.width - 0x14, 0x15, w->canvas.width - 0x14 + 0x18, 0x15 + 0x13A};
    s.max = std::max(0, (int32_t)w->items.size() / w->cols - w->rows + 2);
    s.pos = std::min(w->top / w->cols, s.max);
    return s;
}

// FUN_00494280: the keys (the scroll bar's position is top / cols, its page rows - 1, its range
// 0 .. count / cols - rows + 2).
void on_key(ProdWin* w, int32_t vk) {
    int32_t old_top = w->top, old_sel = w->sel, n = (int32_t)w->items.size(), cols = w->cols;
    auto set_pos = [&](int32_t pos) {
        int32_t max_pos = std::max(0, n / cols - w->rows + 2);
        return std::min(std::max(pos, 0), max_pos);
    };
    int32_t pos = w->top / cols;
    switch (vk) {
    case 0x21:  // PgUp
    case 0x22:  // PgDn
        if (n <= w->visible) return;
        pos = set_pos(vk == 0x21 ? pos - (w->rows - 1) : pos + (w->rows - 1));
        w->top = cols * pos;
        break;
    case 0x23:  // End
        w->sel = n - 1;
        if (w->visible < n) w->top = (w->sel / cols + 1) * cols - w->visible;
        break;
    case 0x24:  // Home
        w->top = 0;
        w->sel = 0;
        break;
    case 0x25: case 0x64:    // Left
    case 0x27: case 0x66: {  // Right
        int32_t lo = (old_sel / cols) * cols, hi = std::min(cols - 1 + lo, n - 1);
        int32_t s = old_sel + ((vk == 0x25 || vk == 0x64) ? -1 : 1);
        w->sel = (lo <= s && lo <= hi) ? std::min(s, hi) : lo;
        break;
    }
    case 0x26: case 0x68: {  // Up
        if (old_sel < cols) return;
        int32_t s = old_sel;
        do s -= cols; while (w->visible + old_top <= s);
        w->sel = s;
        if (s < old_top) w->top = old_top - cols;
        break;
    }
    case 0x28: case 0x62: {  // Down
        if (n - cols - 1 < old_sel) {
            if (n <= w->visible) return;
            w->top = set_pos(pos + 1) * cols;
        } else {
            int32_t s = old_sel;
            do s += cols; while (s < old_top);
            w->sel = s;
            if (w->visible + old_top <= s) w->top = cols + old_top;
        }
        break;
    }
    default: return;
    }
    if (old_top != w->top) {
        w->spin = 0;
        w->redraw();
    } else if (old_sel != w->sel) {
        w->spin = 0;
        draw_cell(w, old_sel);
        draw_cell(w, w->sel);
    }
}

// FUN_00493EA0: the menu of a right click.
void right_menu(ProdWin* w, int32_t x, int32_t y, bool* done) {
    PullDown pd;
    const ProdItem* it = entry(w, w->sel);
    int32_t unit = it && (it->flags & 0x20) ? it->id : -1;
    PullDown_init(&pd);
    if (w->slot == 0) PullDown_add_item(&pd, 5, label_get(0x1148 / 4));
    PullDown_add_item(&pd, 4, label_get(0x1144 / 4));
    if (unit >= 0 && (unit > 0x3F || (Weapon[Units[unit].weapon_id].offense_value >= 0 && unit != 0xF)))
        PullDown_add_item(&pd, 1, label_get(0xE40 / 4));
    if (!MultiplayerActive) PullDown_add_item(&pd, 3, label_get(0x1140 / 4));
    PullDown_add_item(&pd, 2, label_get(0x8FC / 4));
    PullDown_show(&pd, x, y);
    int32_t s = PullDown_get_selected(&pd);
    int32_t id = s < 0x40 ? pulldown_item_id(&pd, s) : -1;
    uint8_t f = (uint8_t)Bases[CurrentBaseID].faction_id;
    switch (id) {
    case 1:
        Units[unit].obsolete_factions |= (uint8_t)(1 << (f & 31));
        synch_proto(unit);
        build_list(w);
        w->redraw();
        break;
    case 2: on_button(w, 0, done); break;
    case 3:
        for (int32_t u = 0; u < 512; u++)
            if (Units[u].unit_flags & 1) Units[u].obsolete_factions &= (uint8_t)~(1 << (f & 31));
        build_list(w);
        w->redraw();
        break;
    case 4: on_button(w, 5, done); break;
    case 5: on_button(w, -1, done); break;
    default: break;
    }
}

void select_at(ProdWin* w, int32_t x, int32_t y) {  // FUN_004941C0
    int32_t old = w->sel;
    w->sel = w->top + (y / w->ch) * w->cols + x / w->cw;
    if (old != w->sel) {
        w->spin = 0;
        w->redraw();
    }
}

BaseButton* button_under(ProdWin* w, int32_t x, int32_t y) {
    for (BaseButton* b : w->buttons)
        if (b->shown && in_box(x, y, b->r)) return b;
    return nullptr;
}

}  // namespace

// FUN_00494700: runs the picker for queue slot `slot` of the current base (faction: the base's
// owner). Returns the item (unit id, -facility id), -9999 cancelled, 0 a mode without a selection;
// *mode: the button's mode (0 OK, 1 help, 2 workshop, 3 new design, 4 delete / 6 reset, 5 insert,
// 7 replace). The window stays shown between calls for a queue slot until OK or Cancel.
int32_t prodwin_pick(int32_t faction, int32_t slot, int32_t* mode) {
    ProdWin* w = &g_pw;
    w->name = "production";
    bool rebuild = !w->shown, rebuttons = rebuild || (w->slot != 0) != (slot != 0);
    w->slot = slot;
    if (rebuild) {
        Font_ctor(&w->f_name);
        Font_ctor(&w->f_big);
        Font_ctor(&w->f_small);
        Font_ctor(&w->f_button);
        w->faction = faction;
        w->spin = 0;
        w->mode = 0;
        build_list(w);
        sw_init(w, g_bw.x, g_bw.y, g_bw.canvas.width, g_bw.canvas.height, false);
        w->bwidth = 100;
        w->top = 0;
        if (w->sel >= w->visible)
            while (w->top + w->visible <= w->sel) w->top += w->cols;
    }
    if (rebuttons) {
        sw_clear_buttons(w);
        BASE& b = Bases[CurrentBaseID];
        bool ai = !((1u << (faction & 31)) & (uint8_t)FactionStatus[0]) ||
                  ((GameState & STATE_SCENARIO_EDITOR) && (b.governor_flags & 0x80000000) && (b.governor_flags & 1));
        w->nbuttons = slot ? 6 : (ai ? 5 : 4);
        int32_t x = ((w->canvas.width - (w->bwidth + 3) * w->nbuttons) - 0x20) / 2 + (w->nbuttons - 1) * (w->bwidth + 3);
        int32_t y = w->ch * w->rows + 8;
        auto add = [&](uint32_t label, int32_t id, bool upper) {
            std::string t = label_get(label / 4);
            if (upper) t = sw_upper(t);
            BaseButton* bt = sw_button(w, t.c_str(), id, x, y, w->bwidth, 0x14, 0, &w->f_button);
            bt->color = 0x41;
            bt->shadow = -1;
            x -= 3 + w->bwidth;
        };
        add(0x198, -2, GameLanguage != 2);
        if (!slot) {
            if (w->nbuttons == 5) add(0x4F4, 6, true);
            add(0x194, -1, true);
            add(0x9B4, 1, true);
        } else {
            add(0x728, 4, true);
            add(0x72C, 5, true);
            add(0xFE8, 7, true);
            add(0x194, -1, true);
        }
        add(0x19C, 0, true);
    }
    w->shown = true;
    w->open = true;
    w->redraw();
    std::fprintf(stderr, "prodwin shown: slot %d, %d items, selected %d\n", slot, (int32_t)w->items.size(), w->sel);
    for (BaseButton* b : w->buttons)
        std::fprintf(stderr, "prodwin button %d %s at %d,%d\n", b->id, b->name.c_str(), w->x + (b->r.left + b->r.right) / 2,
                     w->y + (b->r.top + b->r.bottom) / 2);
    // The modal loop (Win_set_modal).
    bool done = false;
    BaseButton* pressed = nullptr;
    while (!done && !ControlTurnA) {
        UiInput in;
        if (!ui_wait_input(&in)) {
            on_button(w, -2, &done);
            break;
        }
        int32_t lx = in.x - w->x, ly = in.y - w->y;
        if (in.type == UiInput::KEY) {
            int32_t vk = in.key & 0xFFFF;
            if (vk == VKEY_RETURN) on_button(w, -1, &done);
            else if (vk == VKEY_ESCAPE) on_button(w, -2, &done);
            else on_key(w, vk);
            continue;
        }
        if (in.type == UiInput::MOVE) continue;
        BaseButton* b = button_under(w, lx, ly);
        if (in.type == UiInput::LDOWN) {
            if (b) {
                b->pressed = 1;
                pressed = b;
                ui_invalidate();
            } else if (in.double_click) {
                select_at(w, lx, ly);  // the double click (vtable +0x70)
                on_button(w, w->slot ? 5 : -1, &done);
            }
            continue;
        }
        if (in.type == UiInput::LUP) {
            if (pressed) {
                pressed->pressed = 0;
                BaseButton* p = pressed;
                pressed = nullptr;
                ui_invalidate();
                if (p == b) on_button(w, p->id, &done);
                continue;
            }
            if (!b && w->visible < (int32_t)w->items.size()) {
                ScrollBar sb = scroll_bar(w);
                if (int32_t d = scroll_click(sb, lx, ly, w->rows - 1)) {  // Scroll: a row / a page
                    int32_t pos = std::max(0, std::min(sb.pos + d, sb.max));
                    w->top = pos * w->cols;
                    w->redraw();
                    continue;
                }
                if (lx >= sb.r.left && lx < sb.r.right && ly >= sb.r.top && ly < sb.r.bottom) continue;
            }
            if (!b && lx >= 0 && ly >= 0 && lx < w->canvas.width && ly < w->canvas.height) select_at(w, lx, ly);
            continue;
        }
        if (in.type == UiInput::RUP && !b && lx >= 0 && ly >= 0 && lx < w->canvas.width && ly < w->canvas.height) {
            select_at(w, lx, ly);
            right_menu(w, in.x, in.y, &done);
        }
    }
    if (!slot) w->shown = false;
    w->open = w->shown;
    ui_invalidate();
    *mode = w->mode;
    int32_t item = 0;
    if (w->sel == -1) {
        item = w->mode ? 0 : -9999;
    } else if (const ProdItem* it = entry(w, w->sel)) {
        item = (it->flags & 0x20) ? it->id : -it->id;
    }
    std::fprintf(stderr, "prodwin answer: mode %d selected %d item %d\n", w->mode, w->sel, item);
    return item;
}

// The list again (BaseWin_production after the design workshop: FUN_00492FA0).
void prodwin_rebuild() {
    if (!g_pw.shown && !g_pw.open) return;
    build_list(&g_pw);
    g_pw.redraw();
}

void prodwin_hide() {
    g_pw.shown = false;
    g_pw.open = false;
    ui_invalidate();
}

bool prodwin_shown() { return g_pw.shown; }

void prodwin_compose(Buffer* screen) {
    ProdWin* w = &g_pw;
    if (!w->shown || !w->canvas.data) return;
    for (int32_t y = 0; y < w->canvas.height; y++) {
        int32_t sy = w->y + y;
        if (sy < 0 || sy >= screen->height) continue;
        const uint8_t* s = w->canvas.data + (size_t)y * w->canvas.stride;
        uint8_t* d = screen->data + (size_t)sy * screen->stride;
        for (int32_t x = 0; x < w->canvas.width; x++) {
            int32_t sx = w->x + x;
            if (sx >= 0 && sx < screen->width && s[x] != 9) d[sx] = s[x];
        }
    }
    for (BaseButton* b : w->buttons) {
        if (!b->shown) continue;
        bw_button_draw(b);
        const Buffer& c = b->canvas;
        for (int32_t y = 0; y < c.height; y++) {
            int32_t sy = w->y + b->r.top + y;
            if (sy < 0 || sy >= screen->height) continue;
            const uint8_t* s = c.data + (size_t)y * c.stride;
            uint8_t* d = screen->data + (size_t)sy * screen->stride;
            for (int32_t x = 0; x < c.width; x++) {
                int32_t sx = w->x + b->r.left + x;
                if (sx >= 0 && sx < screen->width && s[x] != 9) d[sx] = s[x];
            }
        }
    }
}
