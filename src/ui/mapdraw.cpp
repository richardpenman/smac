// Units, bases and base names on the map (see mapwin.h).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"

GLOBAL(int32_t, VehMovingID, 0x689F30); // unit being animated (-1: none)
GLOBAL(int32_t, BaseShownDefense, 0x9A6528);  // base whose faction's bases show perimeter defence

namespace {

int32_t trunc_div16(int32_t v) { return (v + ((v >> 31) & 0xF)) >> 4; }
int32_t zoom_scale(int32_t zoom, int32_t base) {
    if (ZoomNum < 1) return trunc_div16((zoom + 0x10) * base);
    return ZoomNum * base / ZoomDenom;
}
int32_t wrap_x(int32_t x) {
    if ((MapToggleFlat & 1) == 0) {
        if (x < 0) x += MapAreaX;
        else if (x >= MapAreaX) x -= MapAreaX;
    }
    return x;
}
MAP* tile(int32_t x, int32_t y) { return &MapTiles[MapHalfX * y + (x >> 1)]; }
bool omniscient(int32_t faction_id) { return faction_id >= 0 && faction_id < 8 && (Factions[faction_id].player_flags & 0x200); }
bool tile_known(int32_t faction_id, MAP* sq) {
    return (GameState & 0x80) || omniscient(faction_id) || (faction_id >= 0 && (sq->visibility & (1 << faction_id)));
}
// Faction of the units on a base tile (items bit 1 and the occupant nibble), or -1.
int32_t tile_occupant(MAP* sq) {
    if (!(sq->items & 2)) return -1;
    int32_t f = sq->val2 & 0xF;
    return f > 7 ? -1 : f;
}
bool is_spartans(int32_t faction_id) { return !std::strcmp(MFactions[faction_id].search_key, "SPARTANS"); }

// Map label fonts (FUN_005592D0): a three-entry cache of Arial fonts keyed by size and style;
// size = zoom unit (50 at normal zoom) * num / den (10 is bumped to 11).
struct LabelFonts {
    Font fonts[3];
    int32_t sizes[3] = {0, 0, 0};
    int32_t styles[3] = {-1, -1, -1};
    int32_t order[3] = {0, 1, 2};  // least recently used first
};
LabelFonts g_label_fonts;

Font* label_font(int32_t zoom, int32_t num, int32_t den, int32_t style) {
    LabelFonts& c = g_label_fonts;
    int32_t size = zoom_scale(zoom, 0x32) * num / den;
    if (size == 10) size = 0xB;
    int32_t i = 0;
    while (c.sizes[i] != size || c.styles[i] != style) {
        if (++i > 2) {
            int32_t victim = c.order[0];
            c.order[0] = c.order[1];
            c.order[1] = c.order[2];
            c.order[2] = victim;
            c.sizes[victim] = size;
            c.styles[victim] = style;
            Font_init(&c.fonts[victim], "Arial", size, style);
            return &c.fonts[victim];
        }
    }
    for (int32_t k = 0; k < 3; k++) {
        if (c.order[k] == i) {
            for (int32_t m = k; m < 2; m++) c.order[m] = c.order[m + 1];
            c.order[2] = i;
            break;
        }
    }
    return &c.fonts[i];
}

}  // namespace

void GAMEAPI base_draw(Buffer* dest, int32_t base_id, int32_t px, int32_t py, int32_t zoom, int32_t flags) {
    BASE& base = Bases[base_id];
    int32_t f = (uint8_t)base.faction_id;
    if ((zoom < -8 || (flags & 0x100)) && !(flags & 8) && !(flags & 0x800)) {
        // Small zoom: a box in the faction colour.
        int32_t h = zoom_scale(zoom, 0x32), w = zoom_scale(zoom, 100);
        UiRect r;
        if (!(flags & 0x200)) {
            int32_t d = trunc_div16(zoom * 4 + 0x40);
            if (ZoomNum >= 1) d = ZoomNum * 4 / ZoomDenom;
            r = {d + px, d + py, w + px - d, h + py - d};
        } else {
            r = {px - 2, py - 2, w + px + 2, h + py + 2};
        }
        int32_t adj = ((r.right - r.bottom) - r.left + r.top) / 2;
        r.left += adj;
        r.right -= adj;
        int32_t d = (flags & 0x200) ? 2 : (ZoomNum < 1 ? trunc_div16(zoom * 4 + 0x40) : ZoomNum * 4 / ZoomDenom);
        int32_t e = ZoomNum < 1 ? trunc_div16((zoom * 3 + 0x30) * 2) : ZoomNum * 6 / ZoomDenom;
        r.top += e - d;
        r.bottom += e - d;
        int32_t frame = is_spartans(f) ? 0x2C : 0;
        Buffer_box_2(dest, &r, frame, frame);
        r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        Buffer_fill_3(dest, &r, FactionColorFill[f]);
        return;
    }
    if (ZoomNum < 1) {
        BlitDestScale = zoom + 0x10;
        BlitSourceScale = 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    int32_t pop = base.pop_size;
    int32_t size = 3;
    if (pop < 4) {
        size = 0;
    } else if (pop < std::max(Rules.pop_limit_wo_hab_complex + 1, 7)) {
        size = 1;
    } else if (pop < std::max(Rules.pop_limit_wo_hab_dome + 1, 10)) {
        size = 2;
    }
    if (base_has_fac(base_id, 1) && ++size > 3) size = 3;  // headquarters
    int32_t def = 0;
    if (base_has_fac(base_id, 4) || (BaseShownDefense >= 0 && (uint8_t)Bases[BaseShownDefense].faction_id == f)) def = 1;
    if (base_has_fac(base_id, 5)) def = 2;  // tachyon field
    MAP* sq = tile(base.x, base.y);
    bool water = (sq->climate & 0xE0) < 0x60;
    if (!(flags & 0x800)) {
        Sprite* s = &BaseSprites[f][((water ? 1 : 0) * 4 + size) * 3 + def];
        Sprite_draw_dest(s, dest, s->key, px, py, ShadowTable);
    }
    Font* font = label_font(zoom, 1, 3, 1);
    if (flags & 0x40) {
        BlitDestScale = 1;
        BlitSourceScale = 1;
        return;
    }
    char text[80], width_text[80];
    std::snprintf(text, sizeof(text), "%d", (int32_t)base.pop_size);
    std::snprintf(width_text, sizeof(width_text), "%s", base.pop_size > 9 ? "88" : "8");
    UiRect r = {px, py, Font_width(font, width_text) + 5 + px, font->ascent_above + 4 + py};
    int32_t occupant = tile_occupant(sq);
    bool plain = false;  // undefended base seen by a friend: black frame, coloured number
    if (occupant < 0) {
        int32_t viewer = CurrentPlayerFaction;
        if ((Factions[f].diplo_status[viewer] & 1) || spying(f) || (GameState & 0x80) ||
            (Factions[viewer].player_flags & 0x200) || f == viewer) {
            plain = true;
        } else {
            for (int32_t k = 0; k < 8 && !plain; k++) {
                int32_t nx = wrap_x(NearOffsetX[k] + base.x), ny = NearOffsetY[k] + base.y;
                if (ny < 0 || ny >= MapAreaY || nx < 0 || nx >= MapAreaX) continue;
                if (tile_occupant(tile(nx, ny)) == viewer) plain = true;
            }
        }
    }
    if (plain) {
        Buffer_box_2(dest, &r, 0, 0);
        r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        Buffer_set_font(dest, font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(dest, FactionColorText[f], -1, 1, 1);
    } else {
        int32_t frame = is_spartans(f) ? FactionColorFrame[f] : 0;
        Buffer_box_2(dest, &r, frame, frame);
        r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        Buffer_fill_3(dest, &r, FactionColorFill[f]);
        Buffer_set_font(dest, font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(dest, is_spartans(f) ? 0x2C : 0, -1, 1, 1);
    }
    Buffer_write_l_3(dest, text, r.left + 2, r.top + 1, (int32_t)std::strlen(text));
    // Not drawn yet: the drone riot / golden age icon right of the box (sprites not loaded).
    BlitDestScale = 1;
    BlitSourceScale = 1;
}

void GAMEAPI veh_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t zoom, int32_t mode, int32_t) {
    // Approximation: the original renders the unit's voxel model (.cvr) at the tile centre, with
    // the faction flag (and stack count) at its left. Here: the flag and a figure in the
    // faction's colours.
    VEH& v = Vehs[veh_id];
    int32_t f = v.faction_id;
    int32_t q = std::max(1, zoom_scale(zoom, 0x32) / 4);
    int32_t cx = px + q * 4, cy = py + q * 2;  // tile centre (py is lifted by half a tile)
    int32_t fw = std::max(3, q * 3 / 4), fh = std::max(4, q);
    UiRect pole = {cx - q * 2, cy - q * 2 - fh, cx - q * 2 + 1, cy - q};
    Buffer_fill_3(dest, &pole, 0);
    UiRect flag = {pole.right, pole.top, pole.right + fw, pole.top + fh};
    Buffer_box_2(dest, &flag, 0, 0);
    UiRect inner = {flag.left + 1, flag.top + 1, flag.right - 1, flag.bottom - 1};
    Buffer_fill_3(dest, &inner, FactionColorFill[f]);
    // Health bar along the pole (green, red when damaged).
    int32_t hp = 10 - std::min<int32_t>(v.damage_taken, 10);
    UiRect bar = {pole.left - 2, pole.bottom - (fh + q) * hp / 10, pole.left - 1, pole.bottom};
    Buffer_fill_3(dest, &bar, hp > 5 ? 0xFA : 0xF9);
    UiRect body = {cx - q, cy - q, cx + q, cy + q / 2};
    Buffer_box_2(dest, &body, 0, 0);
    UiRect fill = {body.left + 1, body.top + 1, body.right - 1, body.bottom - 1};
    Buffer_fill_3(dest, &fill, FactionColorFill[f]);
    (void)mode;
}

void THISCALL MapWin_draw_bases(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id) {
    uint32_t opts = (uint32_t)mw(win, kMwOptions);
    if (!(opts & 0x200080)) return;
    if (!dword_93A940 && !ControlTurnMove && !(GameRules & 0x80)) return;
    int32_t b = base_at(x, y);
    if (b < 0) return;
    if (!(GameState & 0x80)) {
        MAP* sq = tile(x, y);
        if (!omniscient(faction_id) && !(faction_id >= 0 && (sq->visibility & (1 << faction_id)))) return;
        if ((uint8_t)Bases[b].faction_id != (uint32_t)CurrentPlayerFaction &&
            !(Bases[b].visibility & (1 << CurrentPlayerFaction)))
            return;
    }
    if ((opts & 0x200000) && b != BaseUpkeepDrawID) return;
    base_draw(ui_mapwin_native(win)->canvas, b, px, py, mw(win, kMwZoom), (opts >> 0x12) & 8);
}

void THISCALL MapWin_draw_vehicles(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id) {
    uint32_t opts = (uint32_t)mw(win, kMwOptions);
    if (!dword_93A940 && !ControlTurnMove && !(GameRules & 0x80)) return;
    if ((opts & 0x1000000) || !(opts & 0x802000C0) || (opts & 0x1000)) return;
    MAP* sq = tile(x, y);
    if ((!(opts & 0x80000000u) || (opts & 0x80)) && tile_known(faction_id, sq))
        MapWin_draw_bases(win, x, y, px, py, faction_id);
    if (opts & 0x80000000u) return;
    // Units being animated (VehMovingID, combat) and the blinking selected unit of a human
    // player are not handled yet: the top unit of the stack is drawn.
    if (!tile_known(faction_id, sq)) return;
    if (!(opts & 0x200040)) return;
    int32_t v = veh_at(x, y);
    if ((opts & 0x200000) != 0) {
        while (v >= 0 && Vehs[v].home_base_id != BaseUpkeepDrawID) v = Vehs[v].next_veh_id_stack;
    }
    if (v < 0) return;
    if (!(GameState & 0x80)) {
        VEH& u = Vehs[v];
        if ((u.faction_id != CurrentPlayerFaction || (u.flags & 0x440) == 0x440) &&
            !(u.visibility & (1 << CurrentPlayerFaction)))
            return;
    }
    if ((sq->items & 1) && (sq->val2 & 0xF) < 8) return;  // units inside bases are not shown
    veh_draw(ui_mapwin_native(win)->canvas, v, px, py, mw(win, kMwZoom), 3, 1);
}

void THISCALL MapWin_draw_base_names(uint8_t* win) {
    if (!dword_93A940 && !ControlTurnMove && !(GameRules & 0x80)) return;
    MapWinNative* n = ui_mapwin_native(win);
    Buffer* canvas = n->canvas;
    UiRect saved = canvas->clip;
    Buffer_set_clip(canvas, reinterpret_cast<UiRect*>(win + n->vbase + kGwClientRect));
    if (!((mw(win, kMwOptions) & 0x800) && (GameMorePreferences & 0x2000)) || mw(win, kMwZoom) < -10) {
        Buffer_set_clip(canvas, &saved);
        return;
    }
    Font* font = reinterpret_cast<Font*>(win + kMwFont0);
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& b = Bases[i];
        if (!(GameState & 0x80) && (uint8_t)b.faction_id != (uint32_t)CurrentPlayerFaction &&
            !(b.visibility & (1 << CurrentPlayerFaction)))
            continue;
        int32_t by = b.y, bx = b.x;
        int32_t oy = mw(win, kMwOriginY), ox = mw(win, kMwOriginX);
        if (by < oy || by >= mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) + oy) continue;
        int32_t cols = mw(win, kMwColsEven) + mw(win, kMwColsOdd);
        int32_t x1 = bx;
        if ((MapToggleFlat & 1) == 0) {
            if (bx < ox) x1 = bx + MapAreaX;
            if (cols + ox <= x1) x1 -= MapAreaX;
        }
        if (x1 < ox || x1 >= cols + ox) continue;
        int32_t px, py;
        pfncTileToPoint(win, bx, by, &px, &py);
        Buffer_set_font(canvas, font, nullptr, nullptr, nullptr);
        char name[256];
        std::snprintf(name, sizeof(name), "%s", b.name);
        if ((b.state_flags & 0x10) && (uint8_t)b.faction_id == (uint32_t)CurrentPlayerFaction)
            std::strncat(name, " (*)", sizeof(name) - std::strlen(name) - 1);
        Buffer_set_text_color(canvas, FactionColorText[(uint8_t)b.faction_id], FactionColorShadow[(uint8_t)b.faction_id], 1, 1);
        Buffer_write_cent_l_8(canvas, name, px + mw(win, kMwHalfW), (mw(win, kMwQ) * 7) / 2 + py,
                              (int32_t)std::strlen(name));
        // Not drawn yet: the objective / production line below the name.
    }
    Buffer_set_clip(canvas, &saved);
}
