// Partial redraws of a map window (MapWin_draw_radius and the engine's draw_tile family; ui
// module 0x468C20..0x46B190) and the view's optional overlays (MapWin_gen_overlays,
// MapWin_draw_legend, MapWin_draw_wars). See mapwin.h.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/console.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"
#include "ui/win.h"

namespace {

Buffer* canvas_of(uint8_t* win) {
    MapWinNative* n = ui_mapwin_native(win);
    return n ? n->canvas : nullptr;
}
UiRect* client_rect(uint8_t* win) { return &graphic_win(win + ui_mapwin_native(win)->vbase).client; }
int32_t wrap_tile_x(int32_t x) {
    if (MapToggleFlat & 1) return x;
    if (x < 0) return x + MapAreaX;
    if (MapAreaX <= x) return x - MapAreaX;
    return x;
}
// Whether (x, y) (on the map) is in the window's tile range (the column brought next to the
// origin across the map's wrap).
bool in_view_range(uint8_t* win, int32_t x, int32_t y) {
    const int32_t oy = map_win(win).origin_y, ox = map_win(win).origin_x;
    if (y < oy || y >= map_win(win).rows_below + map_win(win).rows_above + oy) return false;
    const int32_t right = map_win(win).cols_even + map_win(win).cols_odd + ox;
    int32_t vx = x;
    if (!(MapToggleFlat & 1)) {
        if (x < ox) vx = MapAreaX + x;
        if (right <= vx) vx -= MapAreaX;
    }
    return ox <= vx && vx < right;
}
// IntersectRect: false (and an empty result) when the rectangles do not overlap.
bool intersect(UiRect* out, const UiRect& a, const UiRect& b) {
    UiRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
             std::min(a.bottom, b.bottom)};
    if (r.right <= r.left || r.bottom <= r.top) {
        *out = UiRect{0, 0, 0, 0};
        return false;
    }
    *out = r;
    return true;
}
int32_t line_height(const Font* f) { return f->field_0 < 0 ? f->height : f->field_0 + f->ascent_above; }
// The rows of tiles a partial redraw walks: from (y - half) to (y + half + 1), the row dy
// `half - |dy|` tiles wide on either side above the centre and `half - (dy & 1)` below (+ extra).
template <typename F>
void for_radius_tiles(int32_t x, int32_t y, int32_t half, int32_t extra, F&& fn) {
    for (int32_t dy = -half; dy <= half + 1; dy++) {
        int32_t w = dy > 0 ? (dy & 1) : -dy;
        int32_t hw = half - w + extra;
        if (hw < 0) continue;
        int32_t ty = y + dy;
        for (int32_t i = 0, tx = x - hw; i <= hw; i++, tx += 2) fn(wrap_tile_x(tx), ty);
    }
}

}  // namespace

int32_t THISCALL MapWin_compute_clip(uint8_t* win, int32_t x, int32_t y, int32_t radius, UiRect* out) {
    UiRect r{100000, 100000, 0, 0};
    int32_t px, py;
    int32_t lx = wrap_tile_x(x - radius * 2);
    for (int32_t i = 0, ty = y - radius * 2; i < radius * 2 + 1; i++, ty += 2)
        if (on_map(lx, ty)) {
            pfncTileToPoint(win, lx, ty, &px, &py);
            if (px < r.left) r.left = px;
        }
    int32_t rx = wrap_tile_x(x + 2 + radius * 2);
    for (int32_t i = 0, ty = y - radius * 2; i < radius * 2 + 1; i++, ty += 2)
        if (on_map(rx, ty)) {
            pfncTileToPoint(win, rx, ty, &px, &py);
            if (r.right < px) r.right = px;
        }
    int32_t ty = y - radius * 2;
    if (ty < 0 || MapAreaY <= ty || x < 0 || MapAreaX <= x) {
        r.top = 0;
    } else {
        for (int32_t i = 0, tx = x - radius * 2; i < radius * 2 + 1; i++, tx += 2) {
            pfncTileToPoint(win, tx, ty, &px, &py);
            if (py < r.top) r.top = py;
        }
    }
    ty = y + 2 + radius * 2;
    if (ty < 0 || MapAreaY <= ty || x < 0 || MapAreaX <= x) {
        r.bottom = -graphic_win(win + ui_mapwin_native(win)->vbase).height;
    } else {
        for (int32_t i = 0, tx = x - radius * 2; i < radius * 2 + 1; i++, tx += 2) {
            pfncTileToPoint(win, tx, ty, &px, &py);
            if (r.bottom < py) r.bottom = py;
        }
    }
    r.top -= map_win(win).half_h;
    const UiRect& client = *client_rect(win);
    if (intersect(out, r, client) && radius == 0) {
        auto div4 = [](int32_t v) { return (v + ((v >> 31) & 3)) >> 2; };
        int32_t qh = div4(r.bottom - r.top), qw = div4(r.right - r.left);
        r.left -= qw;
        r.right += qw;
        r.top -= qh;
        r.bottom += qh;
    }
    intersect(out, r, client);
    return (out->right - out->left > 0 && out->bottom - out->top > 0) ? 1 : 0;
}

void THISCALL MapWin_draw_vehicles_2(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t faction_id) {
    Buffer* canvas = canvas_of(win);
    int32_t half = radius * 2 + 2;
    int32_t tmp, top_row = 32000;
    pfncPtToTile(win, canvas->clip.left, canvas->clip.top, &tmp, &top_row);
    for (int32_t cy = y - half; top_row < cy && half < 10; cy -= 2) half += 2;
    half += 2;
    if (half < 0) return;
    for_radius_tiles(x, y, half, radius == 0 ? 2 : 0, [&](int32_t tx, int32_t ty) {
        if (!on_map(tx, ty) || !in_view_range(win, tx, ty)) return;
        int32_t px, py;
        pfncTileToPoint(win, tx, ty, &px, &py);
        MapWin_draw_vehicles(win, tx, ty, px, py - map_win(win).half_h, faction_id);
    });
}

void THISCALL MapWin_gen_radius(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t faction_id,
                                int32_t no_terrain) {
    Buffer* canvas = canvas_of(win);
    UiRect clip;
    if (!MapWin_compute_clip(win, x, y, radius, &clip)) return;
    Buffer_set_clip(canvas, &clip);
    const int32_t zoom = map_win(win).zoom;
    // The two tiles three rows up (their raised terrain can reach down into the tile).
    auto above = [&](auto&& fn) {
        for (int32_t dx : {-1, 1}) {
            int32_t tx = wrap_tile_x(x + dx), ty = y - 3;
            if (on_map(tx, ty)) fn(tx, ty);
        }
    };
    if (radius == 0 && !no_terrain) {
        if (!(map_win(win).options & 0x10000000)) Buffer_fill_3(canvas, &clip, 0x41);  // GraphicWin_fill_2
        above([&](int32_t tx, int32_t ty) {
            int32_t px, py;
            pfncTileToPoint(win, tx, ty, &px, &py);
            MapWin_gen_terrain_poly(canvas, win, zoom, faction_id, -1, map_win(win).options, tx, ty, px, py);
        });
    }
    // Widened upwards until it reaches the row at the clip's top corners.
    const int32_t half0 = radius * 2 + 2;
    int32_t half = half0, tmp, row_l = 32000, row_r = 32000;
    pfncPtToTile(win, clip.left, clip.top, &tmp, &row_l);
    pfncPtToTile(win, clip.right, clip.top, &tmp, &row_r);
    int32_t top_row = std::min(row_l, row_r);
    for (int32_t cy = y - half; top_row < cy; cy -= 2) half += 2;
    half += 2;
    if (!no_terrain) {
        for_radius_tiles(x, y, half, radius == 0 ? 2 : 0, [&](int32_t tx, int32_t ty) {
            if (!on_map(tx, ty) || !in_view_range(win, tx, ty)) return;
            int32_t px, py;
            pfncTileToPoint(win, tx, ty, &px, &py);
            MapWin_gen_terrain_poly(canvas, win, zoom, faction_id, -1, map_win(win).options, tx, ty, px,
                                    py);  // mapwin_unk1
        });
    }
    MapWin_draw_vehicles_2(win, x, y, radius, faction_id);
    MapWin_draw_base_names(win);
    if (map_win(win).options & 0x2000) {
        if (radius == 0) above([&](int32_t tx, int32_t ty) { MapWin_gen_overlays(win, tx, ty); });
        int32_t h = half0, row = 32000;
        pfncPtToTile(win, clip.left, clip.top, &tmp, &row);
        for (int32_t cy = y - h; row < cy; cy -= 2) h += 2;
        for_radius_tiles(x, y, h + 2, 0, [&](int32_t tx, int32_t ty) {
            if (on_map(tx, ty) && mapwin_tile_in_view(win, tx, ty, 0)) MapWin_gen_overlays(win, tx, ty);
        });
    }
    if (map_win(win).options & 0x20000) MapWin_draw_legend(win);
    Buffer_set_clip(canvas, client_rect(win));
}

void THISCALL MapWin_draw_radius(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t mode) {
    if (!canvas_of(win) || !canvas_of(win)->data) return;
    // A full redraw is pending (ui_invalidate_map): it draws these tiles too.
    if (win == MapWins[0] && ui_map_dirty()) return;
    MapWin_gen_radius(win, x, y, radius, (GameState & STATE_OMNISCIENT_VIEW) ? -1 : CurrentPlayerFaction, mode < 0);
    // (The cursor's texture, which the original draws here again when the cursor tile is in the
    // view, is drawn over the view at every presentation: win.cpp draw_map_cursor.)
    UiRect r;
    if (mode != 0 && MapWin_compute_clip(win, x, y, radius, &r)) {
        // GraphicWin_soft_update_3 (and do_all_draws for mode >= 2): presented with the next
        // frame.
        ui_invalidate();
    }
}

void THISCALL MapWin_draw_tile(uint8_t* win, int32_t x, int32_t y, int32_t mode) {
    if (y < map_win(win).origin_y || y >= map_win(win).rows_below + map_win(win).rows_above + map_win(win).origin_y)
        return;
    const int32_t ox = map_win(win).origin_x, right = map_win(win).cols_even + map_win(win).cols_odd + ox;
    int32_t vx = x;
    if (!(MapToggleFlat & 1)) {
        if (x < ox) vx = MapAreaX + x;
        if (right <= vx) vx -= MapAreaX;
    }
    if (ox <= vx && vx < right) MapWin_draw_radius(win, x, y, 0, mode);
}

void THISCALL MapWin_gen_tile(uint8_t* win, int32_t x, int32_t y, int32_t faction_id, int32_t units) {
    Buffer* canvas = canvas_of(win);
    int32_t px, py;
    pfncTileToPoint(win, x, y, &px, &py);
    MapWin_gen_terrain_poly(canvas, win, map_win(win).zoom, faction_id, -1, map_win(win).options, x, y, px, py);
    if (on_map(x, y) && units) {
        MapWin_draw_vehicles(win, x, y, px, py - map_win(win).half_h, faction_id);
        MapWin_gen_overlays(win, x, y);
    }
}

// ---- overlays ------------------------------------------------------------------------------------

void THISCALL MapWin_gen_overlays(uint8_t* win, int32_t x, int32_t y) {
    if (!(map_win(win).options & 0x2000)) return;
    Buffer* canvas = canvas_of(win);
    int32_t px, py;
    pfncTileToPoint(win, x, y, &px, &py);
    for (int32_t f = 1; f < 8; f++) {
        const bool omni = (GameState & STATE_OMNISCIENT_VIEW) != 0;
        if (!omni && f != CurrentPlayerFaction) continue;
        Goal* goals = Factions[f].goals;
        for (int32_t i = 0; i < 75; i++) {
            Goal& g = goals[i];
            if (x != g.x || y != g.y || g.type < 0 || g.priority == 0) continue;
            if (g.priority < 0) {
                // A negative priority yields to another goal of the same type there (which the
                // original removes for good).
                for (int32_t j = 0; j < 75; j++)
                    if (j != i && goals[j].x == x && goals[j].y == y && goals[j].type == g.type) {
                        g.type = -1;
                        break;
                    }
                if (g.type < 0) continue;
            }
            UiRect r{px, py, px + map_win(win).tile_w, py + map_win(win).tile_h};
            StrBuffer[0] = 0;
            if (omni) {
                GenderDefault = MFactions[f].noun_gender;
                PluralDefault = MFactions[f].is_noun_plural;
                std::strcat(StrBuffer, MFactions[f].noun_faction);
                StrBuffer[1] = 0;  // its initial
            }
            int32_t color = 0xFF;
            switch (g.type) {
            case 0: std::strcat(StrBuffer, "a"), color = 0xF9; break;
            case 2: std::strcat(StrBuffer, "d"), color = 0xFB; break;
            case 3: std::strcat(StrBuffer, "s"), color = 0xFD; break;
            case 6: std::strcat(StrBuffer, "n"), color = 0xFC; break;
            case 8: std::strcat(StrBuffer, "c"), color = 0xFE; break;
            case 9: std::strcat(StrBuffer, "f"), color = 0xFA; break;
            case 0x29: std::strcat(StrBuffer, "^"), color = 0xFF; break;
            default: break;
            }
            int32_t p = g.priority < 0 ? -g.priority : g.priority;
            std::snprintf(StrBuffer + std::strlen(StrBuffer), 32, "%d", p);
            Buffer_set_text_color(canvas, color, 0, 1, 1);
            Buffer_set_font(canvas, mapwin_font(win, 1), nullptr, nullptr, nullptr);
            Buffer_write_cent_l_7(canvas, StrBuffer, &r, (int32_t)std::strlen(StrBuffer));
            break;
        }
    }
}

namespace {
// The keypad digits around the cursor tile (0x66EF50 dx, 0x66EF74 dy, 0x66EF98 digit).
constexpr int32_t kLegendDx[8] = {1, 2, 1, 0, -1, -2, -1, 0};
constexpr int32_t kLegendDy[8] = {-1, 0, 1, 2, 1, 0, -1, -2};
constexpr int32_t kLegendDigit[8] = {9, 6, 3, 2, 1, 4, 7, 8};
}  // namespace

void THISCALL MapWin_draw_legend(uint8_t* win) {
    Buffer* canvas = canvas_of(win);
    Font font;
    Font_ctor(&font);
    Font_init(&font, "Arial", map_win(win).tile_h * 2 / 3, 1);
    for (int32_t i = 0; i < 8; i++) {
        int32_t tx = wrap_tile_x(kLegendDx[i] + MapWinCursorX[MapWinCursorIndex]);
        int32_t ty = kLegendDy[i] + MapWinCursorY[MapWinCursorIndex];
        if (!on_map(tx, ty)) continue;
        int32_t px, py;
        pfncTileToPoint(win, tx, ty, &px, &py);
        std::snprintf(StrBuffer, sizeof(StrBuffer), "%d", kLegendDigit[i]);
        Buffer_set_text_color(canvas, 0xFF, 0, 1, 1);
        // Buffer_write_cent_l_3: in the tile's box with the font in slot 0.
        Font* saved = canvas->fonts[0];
        Font* s1 = canvas->fonts[1];
        Font* s2 = canvas->fonts[2];
        if (font.handle) {
            canvas->fonts[0] = &font;
            canvas->fonts[1] = canvas->fonts[2] = nullptr;
        }
        UiRect r{px, py, px + map_win(win).tile_w, py + map_win(win).tile_h};
        Buffer_write_cent_l_7(canvas, StrBuffer, &r, (int32_t)std::strlen(StrBuffer));
        canvas->fonts[0] = saved;
        canvas->fonts[1] = s1;
        canvas->fonts[2] = s2;
    }
    if (font.handle) platform::font_close(font.handle);  // Font_dtor
}

void THISCALL MapWin_draw_wars(uint8_t* win) {
    Buffer* canvas = canvas_of(win);
    Font* font = mapwin_font(win, 1);
    const int32_t cell = Font_width(font, "W");
    int32_t y = line_height(font);
    Buffer_set_font(canvas, font, nullptr, nullptr, nullptr);
    int32_t left = cell;
    for (int32_t f = 1; f < 8; f++) {
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, MFactions[f].noun_faction);
        std::strcat(StrBuffer, ": ");
        Buffer_set_text_color(canvas, FactionColorFill[f], 0, 1, 1);
        int32_t end = Buffer_write_l_3(canvas, StrBuffer, cell, y, (int32_t)std::strlen(StrBuffer));
        if (left <= end) left = end;
        y += line_height(font);
    }
    // The table of the diplomatic states: W vendetta, P pact, T treaty, t truce, - no contact (a
    // contact without any of these keeps the previous cell's letter).
    y = line_height(font);
    char c = 0;
    for (int32_t f = 1; f < 8; f++) {
        int32_t x = left;
        for (int32_t g = 1; g < 8; g++) {
            Buffer_set_text_color(canvas, FactionColorFill[g], 0, 1, 1);
            uint32_t s = Factions[f].diplo_status[g];
            if (!(s & 8)) c = '-';
            else if (s & 0x10) c = 'W';
            else if (s & 1) c = 'P';
            else if (s & 2) c = 'T';
            else if (s & 4) c = 't';
            StrBuffer[0] = c;
            StrBuffer[1] = 0;
            Buffer_write_l_3(canvas, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
            x += cell;
        }
        y += line_height(font);
    }
}
