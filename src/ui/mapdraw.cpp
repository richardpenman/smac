// Units, bases and base names on the map (see mapwin.h).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/score.h"
#include "ui/colortables.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"
#include "ui/texture.h"

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

}  // namespace

Font* ui_label_font(int32_t zoom, int32_t num, int32_t den, int32_t style) {
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

namespace {
Font* label_font(int32_t zoom, int32_t num, int32_t den, int32_t style) { return ui_label_font(zoom, num, den, style); }

}  // namespace

namespace {
// load_icons' riot icons (colours 6 and 7 made transparent).
Sprite* riot_icon(bool alien) {
    static Sprite icons[2];
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        const char* files[2] = {"ICONS.PCX", "TER1WRECK.PCX"};
        static const int32_t at[2][4] = {{0x2E, 0x12F, 0x21, 0x1E}, {0x300, 0x94, 0x1C, 0x2A}};
        for (int32_t k = 0; k < 2; k++) {
            Buffer art;
            Buffer_ctor(&art);
            if (Buffer_load_pcx(&art, files[k], nullptr, 10, 0xEC)) continue;
            Buffer_change_color(&art, 0x107, 0x109);
            Buffer_change_color(&art, 0x106, 0x109);
            pfncSpriteFromCanvasRectTrans(&icons[k], &art, 0x109, at[k][0], at[k][1], at[k][2], at[k][3], 0);
        }
    }
    return &icons[alien ? 1 : 0];
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
        // The art of the faction that founded the base (BASE +5, faction_id_former; base_draw's
        // index (water + former * 2) * 4 + size, * 3 + defence), not of its owner.
        int32_t former = (uint8_t)base.faction_id_former & 7;
        Sprite* s = &BaseSprites[former][((water ? 1 : 0) * 4 + size) * 3 + def];
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
    // Drone riots (state 2): the riot icon right of the box (load_icons: 0x75AD88 of ICONS.PCX,
    // aliens 0x791F98 of TER1WRECK.PCX, 6 pixels higher), at the base's scale.
    if (base.state_flags & 2) {
        bool alien = MFactions[f].rule_flags & 0x80;
        Sprite* icon = riot_icon(alien);
        int32_t ix = px + 2 + (r.right - r.left + 2), iy = py;
        if (alien) {
            iy -= 6;
            ix += 2;
        }
        if (icon->width) pfncSpriteStretchCopyToCanvas(icon, dest, icon->key & 0xFF, ix, iy);
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
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

void THISCALL MapWin_draw_base_names(uint8_t* win) {
    if (!dword_93A940 && !ControlTurnMove && !(GameRules & 0x80)) return;
    MapWinNative* n = ui_mapwin_native(win);
    Buffer* canvas = n->canvas;
    UiRect saved = canvas->clip;
    Buffer_set_clip(canvas, reinterpret_cast<UiRect*>(win + n->vbase + kGwClientRect));
    MapWin_draw_base_dest(win, 0);
    if (!(mw(win, kMwOptions) & 0x800) || !(GameMorePreferences & 0x2000)) {
        Buffer_set_clip(canvas, &saved);
        return;
    }
    if (mw(win, kMwZoom) < -10) {
        Buffer_set_clip(canvas, &saved);
        return;
    }
    Font* font = reinterpret_cast<Font*>(win + kMwFont0);
    const int32_t viewer = CurrentPlayerFaction;
    auto in_view = [&](int32_t bx, int32_t by) {
        int32_t oy = mw(win, kMwOriginY), ox = mw(win, kMwOriginX);
        if (by < oy || by >= mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) + oy) return false;
        int32_t cols = mw(win, kMwColsEven) + mw(win, kMwColsOdd), x1 = bx;
        if ((MapToggleFlat & 1) == 0) {
            if (bx < ox) x1 = bx + MapAreaX;
            if (cols + ox <= x1) x1 -= MapAreaX;
        }
        return x1 >= ox && x1 < cols + ox;
    };
    // The second line below the name: a line of the font lower.
    auto second_line_y = [&](int32_t py) {
        int32_t dy = font->field_0 < 0 ? font->height : font->ascent_above + font->field_0;
        return (mw(win, kMwQ) * 7) / 2 + dy + py;
    };
    auto str = [](uint32_t addr) { return reinterpret_cast<const char*>(mem::host(addr)); };
    for (int32_t i = 0; i < BaseCount; i++) {
        BASE& b = Bases[i];
        uint8_t f = (uint8_t)b.faction_id;
        if (!(GameState & 0x80) && f != (uint32_t)viewer && !(b.visibility & (1 << viewer))) continue;
        if (!in_view(b.x, b.y)) continue;
        int32_t px, py;
        pfncTileToPoint(win, b.x, b.y, &px, &py);
        Buffer_set_font(canvas, font, nullptr, nullptr, nullptr);
        char line[256];
        std::snprintf(line, sizeof(line), "%s", b.name);
        if ((b.state_flags & 0x10) && f == (uint32_t)viewer) std::strncat(line, str(0x686358), sizeof(line) - std::strlen(line) - 1);
        Buffer_set_text_color(canvas, FactionColorText[f], FactionColorShadow[f], 1, 1);
        if ((GameState & 0x1000) && ((GameState & 0x80) || !(FactionStatus[0] & (1 << viewer)))) {
            const uint8_t* fac = reinterpret_cast<const uint8_t*>(&Factions[f]);
            int32_t tx = *reinterpret_cast<const int32_t*>(fac + 0x3F0), ty = *reinterpret_cast<const int32_t*>(fac + 0x3F4);
            if (tx >= 0 && b.x == tx && b.y == ty) Buffer_set_text_color(canvas, 0xFF, 0, 1, 1);
        }
        int32_t cx = px + mw(win, kMwHalfW);
        Buffer_write_cent_l_8(canvas, line, cx, (mw(win, kMwQ) * 7) / 2 + py, (int32_t)std::strlen(line));
        bool objective = is_objective(i) && !(GameRules & 0x20000000);
        auto objective_line = [&]() {
            if (!objective) return;
            std::snprintf(line, sizeof(line), "%s", label_get(0x382));
            for (char* c = line; *c; c++) *c = (char)std::toupper((unsigned char)*c);  // CharUpperA
            Buffer_write_cent_l_8(canvas, line, cx, second_line_y(py), (int32_t)std::strlen(line));
        };
        auto production = [&]() -> const char* {
            int32_t id = b.queue_items[0];
            return id < 0 ? Facility[-id].name : Units[id].name;
        };
        if ((GameState & 0x1000) && (GameState & 0x80) && mw(win, kMwZoom) >= -3) {
            uint8_t region = MapTiles[MapHalfX * b.y + (b.x >> 1)].region;
            uint8_t plan = Factions[f].region_base_plan[region];
            std::snprintf(line, sizeof(line), "%s%s%s%s%s", str(0x682E9C), PlansShortName[plan], str(0x682E94), production(),
                          str(0x682E98));
            Buffer_write_cent_l_8(canvas, line, cx, second_line_y(py), (int32_t)std::strlen(line));
        } else if (!(GameMorePreferences & 0x1000) && !(mw(win, kMwOptions) & 0x8000000)) {
            objective_line();
        } else if (f == (uint32_t)viewer || spying(f)) {
            std::snprintf(line, sizeof(line), "%s", str(0x682E9C));
            if (!objective) {
                uint32_t g = (uint32_t)b.governor_flags;
                if ((g & 0x80000000u) && (g & 1)) {
                    int32_t label = (g & 0x1000000) ? 650 : (g & 0x2000000) ? 651 : (g & 0x4000000) ? 652
                                    : (g & 0x8000000) ? 653 : 649;
                    std::strncat(line, label_get(label), sizeof(line) - std::strlen(line) - 1);
                    std::strncat(line, str(0x682E94), sizeof(line) - std::strlen(line) - 1);
                }
            } else {
                std::strncat(line, label_get(0x382), sizeof(line) - std::strlen(line) - 1);
                std::strncat(line, str(0x682E94), sizeof(line) - std::strlen(line) - 1);
                for (char* c = line; *c; c++) *c = (char)std::toupper((unsigned char)*c);
            }
            std::strncat(line, production(), sizeof(line) - std::strlen(line) - 1);
            std::strncat(line, str(0x682E98), sizeof(line) - std::strlen(line) - 1);
            Buffer_write_cent_l_8(canvas, line, cx, second_line_y(py), (int32_t)std::strlen(line));
        } else {
            objective_line();
        }
    }
    // Landmark names (italic font), white with a black shadow offset by the zoom.
    int32_t off = mw(win, kMwZoom) < 1 ? 1 : (ZoomNum < 1 ? trunc_div16(mw(win, kMwZoom) + 0x10) : ZoomNum / ZoomDenom);
    Buffer_set_text_color(canvas, 0xFF, 0, off, off);
    Buffer_set_font(canvas, reinterpret_cast<Font*>(win + kMwFont2), nullptr, nullptr, nullptr);
    for (int32_t i = 0; i < MapLandmarkCount; i++) {
        const uint8_t* lmk = mem::host(0x949894 + i * 0x28);
        int32_t lx = *reinterpret_cast<const int32_t*>(lmk), ly = *reinterpret_cast<const int32_t*>(lmk + 4);
        const char* name = reinterpret_cast<const char*>(lmk + 8);
        MAP* sq = &MapTiles[MapHalfX * ly + (lx >> 1)];
        if (!(GameState & 0x80) && !(Factions[viewer].player_flags & 0x200) && !(sq->visibility & (1 << viewer))) continue;
        if (!in_view(lx, ly)) continue;
        if ((sq->items & 1) && (sq->val2 & 0xF) <= 7) continue;  // a base is named instead
        int32_t px, py;
        pfncTileToPoint(win, lx, ly, &px, &py);
        Buffer_write_cent_l_8(canvas, name, mw(win, kMwHalfW) + px, (mw(win, kMwQ) * 7) / 2 + py, (int32_t)std::strlen(name));
    }
    Buffer_set_clip(canvas, &saved);
}
