// Units on the map (see vehdraw.h).
#include "ui/vehdraw.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/battle.h"
#include "engine/globals.h"
#include "engine/stack.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/colortables.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"

int8_t VehSpriteOffsets[19][8][8];

namespace {

Buffer g_canvas;  // poLoadingCanvas while the unit art is read

int32_t trunc_div16(int32_t v) { return (v + ((v >> 31) & 0xF)) >> 4; }
// v * (zoom + 16) / 16 (or the fixed zoom factor).
int32_t zscale(int32_t zoom, int32_t v) {
    if (ZoomNum < 1) return trunc_div16((zoom + 0x10) * v);
    return ZoomNum * v / ZoomDenom;
}

void cell(Sprite* s, int32_t x, int32_t y, int32_t w, int32_t h) {
    Buffer_change_color(&g_canvas, 0x107, 0x109);
    Buffer_change_color(&g_canvas, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(s, &g_canvas, 0x109, x, y, w, h, 0);
}

// Sprite_copy (0x5EAE4F) and Sprite_change_color (0x5EAD9F).
void sprite_copy(const Sprite* from, Sprite* to) { *to = *from; }
void sprite_change_color(Sprite* s, int32_t from, int32_t to) {
    for (uint8_t& p : s->pixels)
        if (p == (uint8_t)from) p = (uint8_t)to;
}

bool is_spartans(int32_t faction_id) { return !std::strcmp(MFactions[faction_id].search_key, "SPARTANS"); }

GLOBAL(int32_t, MapWinField23BF8, 0x9392A8);
GLOBAL(int32_t, MapWinField23C00, 0x9392B0);

}  // namespace

void GAMEAPI load_vehicles() {
    // XI.PCX / XF.PCX (explosion and battle animation frames, 0x7A9B30..) are not loaded: the
    // animations are not reconstructed.
    // Faction 0 (native life) takes its colours from PALETTE.PCX.
    Buffer_load_pcx(&g_canvas, "PALETTE.PCX", nullptr, 10, 0xEC);
    FactionColorFill[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x17);
    FactionColorFrame[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x1F);
    FactionColorText[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x27);
    FactionColorShadow[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x2F);
    Buffer_load_pcx(&g_canvas, "UNITS.PCX", nullptr, 10, 0xEC);
    // Special sprites 9..18, one row each (2 + 0x4D * (index - 9)); rows with variants: morale
    // levels (7) or kinds (3). Marker pixels left of / above each cell give the flag position
    // (colour 5) and a second point (colour 2).
    int32_t last_count = 0;
    for (int32_t idx = 9; idx < 19; idx++) {
        int32_t y = 2 + 0x4D * (idx - 9);
        int32_t count = 1;
        if (y == 0xE9 || y == 0x4F || y == 0x136 || y == 0x183) count = 7;
        else if (y == 0x1D0) count = 3;
        last_count = count;
        for (int32_t k = 0, x = 2; k < count; k++, x += 0x66) {
            int32_t cx = x, cy = y;
            if (idx == 18) {
                cx = 0x2CC;
                cy = 0xE9;
            }
            cell(ArtSprite(kSprUnits + (idx * 8 + k) * 0x2C), cx, cy, 100, 0x4B);
            int8_t(&o)[8][8] = VehSpriteOffsets[idx];
            o[0][k] = o[1][k] = o[2][k] = o[3][k] = 0;
            o[4][k] = o[5][k] = o[6][k] = o[7][k] = (int8_t)0x9D;
            for (int32_t i = 0; i < 100; i++)
                for (int32_t r = cy - 2; r <= cy - 1; r++) {
                    int32_t p = Buffer_get_pixel(&g_canvas, cx + i, r);
                    if (p == 5) o[0][k] = (int8_t)i;
                    if (p == 2) o[2][k] = (int8_t)i;
                }
            for (int32_t i = 0; i < 0x4B; i++)
                for (int32_t c = cx - 2; c <= cx - 1; c++) {
                    int32_t p = Buffer_get_pixel(&g_canvas, c, cy + i);
                    if (p == 5) o[1][k] = (int8_t)i;
                    if (p == 2) o[3][k] = (int8_t)i;
                }
        }
    }
    // Chassis 0..8: the flag markers right of the model column (x 0x394..0x3F9). The vertical
    // position is stored in the variant slot of the last special row's count (BUG(original):
    // a stale loop counter; veh_draw reads slot 0, which stays 0).
    for (int32_t c = 0; c < 9; c++) {
        int32_t y0 = 2 + 0x4D * c;
        for (int32_t i = 0; i < 100; i++)
            for (int32_t r = y0 - 1; r <= y0; r++)
                if (Buffer_get_pixel(&g_canvas, 0x396 + i, r) == 5) VehSpriteOffsets[c][0][0] = (int8_t)i;
        for (int32_t i = 0; i < 0x4B; i++)
            for (int32_t x = 0x394; x <= 0x395; x++)
                if (Buffer_get_pixel(&g_canvas, x, y0 + i + 1) == 5) VehSpriteOffsets[c][1][last_count] = (int8_t)i;
    }
    load_flags();
}

void GAMEAPI load_flags() {
    Sprite flags[14];
    Buffer_load_pcx(&g_canvas, "UNITS.PCX", nullptr, 10, 0xEC);
    for (int32_t i = 0, x = 1; i < 7; i++, x += 0x18) {
        cell(&flags[i * 2], x, 0x2BC, 0x17, 0x1E);
        cell(&flags[i * 2 + 1], x, 0x2DB, 0x17, 0x1E);
    }
    for (int32_t f = 0; f < 8; f++)
        for (int32_t j = 0; j < 14; j++) {
            Sprite* s = ArtSprite(kSprFlags + (f * 14 + j) * 0x2C);
            sprite_copy(&flags[j], s);
            sprite_change_color(s, 0x104 & 0xFF, FactionColorFill[f]);
            sprite_change_color(s, 0x103 & 0xFF, FactionColorFrame[f]);
        }
    // Factions with their own flag (MFactions + 0x508 bit 0x80) use FLAGS.PCX.
    Buffer_load_pcx(&g_canvas, "FLAGS.PCX", nullptr, 10, 0xEC);
    for (int32_t i = 0, x = 1; i < 7; i++, x += 0x18) {
        cell(&flags[i * 2], x, 1, 0x17, 0x1E);
        cell(&flags[i * 2 + 1], x, 0x20, 0x17, 0x1E);
    }
    for (int32_t f = 0; f < 8; f++) {
        if (!(reinterpret_cast<const uint8_t*>(&MFactions[f])[0x508] & 0x80)) continue;
        for (int32_t j = 0; j < 14; j++) {
            Sprite* s = ArtSprite(kSprFlags + (f * 14 + j) * 0x2C);
            sprite_copy(&flags[j], s);
            sprite_change_color(s, 0x104 & 0xFF, FactionColorFill[f]);
            sprite_change_color(s, 0x103 & 0xFF, FactionColorFrame[f]);
        }
    }
}

int32_t GAMEAPI pick_top_veh(int32_t veh_id) {
    int32_t best = -99;
    if (veh_id < 0) return veh_id;
    int32_t v = veh_id;
    while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
    for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
        if (VehMovingID != -1 && v == VehMovingID) continue;
        if (v == MapWinVehA && Vehs[v].faction_id == CurrentPlayerFaction && !(Vehs[v].state & 0x4200) &&
            MapWinInVehMode && MapWinField23BE4)
            return v;
        int32_t value = Chassis[Units[Vehs[v].unit_id].chassis_id].triad +
                        (Vehs[v].faction_id != CurrentPlayerFaction ? 0 : 4);
        if (best < value) {
            best = value;
            veh_id = v;
        }
    }
    return veh_id;
}

void GAMEAPI vehdraw_draw(Buffer*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {
    // The 3D unit models (voxel models through the gfx2 software renderer) are not
    // reconstructed yet.
}

int32_t GAMEAPI veh_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t zoom, int32_t mode, int32_t a7) {
    const uint32_t m = (uint32_t)mode;
    if (m & 1) {
        veh_id = pick_top_veh(veh_id);
        if (VehMovingID != -1 && VehMovingID == veh_id) return 0;
    }
    VEH& veh = Vehs[veh_id];
    const int32_t f = veh.faction_id;
    const UNIT& unit = Units[veh.unit_id];
    if ((zoom < -8 || (m & 0x100)) && !(m & 8) && !(m & 0x800)) {
        // Small zoom: a box in the faction colour.
        if (f != CurrentPlayerFaction && unit.plan == 0xB && !(GameState & 0x80)) return 0;
        if (m & 0x400) return 0;
        int32_t right = zscale(zoom, 100) + px, bottom = zscale(zoom, 0x32) + py;
        int32_t left = px, top = py;
        if (!(m & 0x200)) {
            int32_t d = ZoomNum < 1 ? trunc_div16(zoom * 8 + 0x80) : ZoomNum * 8 / ZoomDenom;
            left += d;
            right -= d;
            top += d;
            bottom -= d;
        } else {
            left -= 2;
            right += 2;
            top -= 2;
            bottom += 2;
        }
        int32_t q = (bottom - top + (((bottom - top) >> 31) & 3)) >> 2;
        top += q;
        bottom += q;
        int32_t adj = ((right - left) - bottom + top) / 2;
        UiRect r{left + adj, top, right - adj, bottom};
        int32_t frame = is_spartans(f) ? 0x2C : 0;
        Buffer_box_2(dest, &r, frame, frame);
        r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        Buffer_fill_3(dest, &r, FactionColorFill[f]);
        return 0;
    }
    if (ZoomNum < 1) {
        BlitDestScale = zoom + 0x10;
        BlitSourceScale = 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    const int32_t x0 = px, y0 = py;
    // Sprite index: the chassis, or 9 + the weapon's / the unit's icon.
    int32_t idx = unit.chassis_id;
    if (Weapon[unit.weapon_id].icon >= 0 && idx == 0) idx = Weapon[unit.weapon_id].icon + 9;
    if (unit.icon_offset >= 0) idx = unit.icon_offset + 9;
    int32_t morale = f == 0 ? morale_alien(veh_id, CurrentPlayerFaction) : morale_veh(veh_id, 0, 0);
    int32_t variant = 0;
    if (idx == 0xC || idx == 10 || idx == 0xD || idx == 0xE) variant = morale;
    else if (idx == 0xF) variant = veh.unit_id == 0x11 ? 1 : (veh.unit_id == 0x12 ? 2 : 0);
    if (!(m & 0x1000)) {
        px += zscale(zoom, (uint8_t)VehSpriteOffsets[idx][0][variant]);
        py += zscale(zoom, (uint8_t)VehSpriteOffsets[idx][1][variant]);
    }
    const uint32_t flag_only = m & 0x800;
    if (!flag_only) {
        if (idx > 8) {
            Sprite* s = ArtSprite(kSprUnits + (variant + idx * 8) * 0x2C);
            Sprite_draw_dest(s, dest, s->key & 0xFF, x0, y0, ShadowTable);
        }
        BlitDestScale = 1;
        BlitSourceScale = 1;
        if (!(m & 0x80) && idx < 9) {
            // (mode 0x10 / 0x20: the turning and moving animations, vehdraw_turn / _scoot)
            vehdraw_draw(dest, veh_id, x0, y0, mode, zoom, a7);
        }
    }
    if (ZoomNum < 1) {
        BlitSourceScale = 0x10;
        BlitDestScale = zoom + 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    bool skip_flag = (((m & 0x40) || (m & 0x400)) && !flag_only) ||
                     (f != CurrentPlayerFaction && unit.plan == 0xB && !(GameState & 0x80));
    if (!skip_flag) {
        // Flag: [faction][morale][no moves left]; behind it a silhouette for stacks.
        int32_t moved = 0;
        if (MultiplayerActive || f == CurrentPlayerFaction) {
            int32_t sp = speed(veh_id, 0), left = sp - veh.moves_spent;
            if (left < 0 || (left < 1000 && sp == veh.moves_spent)) moved = 1;
        }
        Sprite* flag = ArtSprite(kSprFlags + (moved + (f * 7 + morale) * 2) * 0x2C);
        const int32_t six = ZoomNum < 1 ? trunc_div16((zoom * 3 + 0x30) * 2) : ZoomNum * 6 / ZoomDenom;
        if ((m & 2) && stack_check(veh_id, 1, -1, -1, -1) > 1) {
            if ((uint8_t)VehSpriteOffsets[idx][0][variant] > 0x32) px -= six;
            Sprite_draw_mono_2(flag, dest, flag->key & 0xFF, px, py, FactionColorFrame[f]);
            px += six;
        }
        pfncSpriteStretchCopyToCanvas(flag, dest, flag->key & 0xFF, px, py);
        // Health bar along the flag pole.
        UiRect bar;
        bar.left = six + px;
        bar.top = six + py;
        bar.right = bar.left + (ZoomNum < 1 ? trunc_div16(zoom * 2 + 0x20) : ZoomNum * 2 / ZoomDenom);
        bar.bottom = bar.top + zscale(zoom, 0x15);
        Buffer_fill_3(dest, &bar, 0);
        auto max_hp = [&]() -> uint32_t {
            if (unit.plan == 0xC) return 1;
            uint32_t r = unit.reactor_id;
            if (r == 0) r = 1;
            else if (r > 100) r = 100;
            return r * 10;
        };
        uint32_t dmg = veh.damage_taken;
        int32_t hp = (int32_t)(max_hp() - dmg);
        if (hp >= 0 && (hp > 9999 || max_hp() != dmg)) {
            if (hp > 9999) hp = 9999;
            int32_t color = 0xF9;
            bar.bottom = ((bar.bottom - bar.top) * hp) / (int32_t)max_hp() + bar.top;
            if (dmg < (max_hp() * 2) / 3) color = 0xFB;
            if (dmg < max_hp() / 3 || veh.damage_taken == 0) color = 0xFA;
            Buffer_fill_3(dest, &bar, color);
        }
        // Order letter in the flag.
        Font* font = ui_label_font(zoom, 1, 4, 1);
        Buffer_set_font(dest, font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(dest, is_spartans(f) ? 0x2C : 0, -1, 1, 1);
        int32_t order = (int8_t)veh.order;
        if (order < 0) order = 0;
        else if (order > 0x1D) order = 0x1D;
        UiRect box;
        box.left = (ZoomNum < 1 ? trunc_div16((zoom * 5 + 0x50) * 2) : ZoomNum * 10 / ZoomDenom) + px;
        box.top = (ZoomNum < 1 ? trunc_div16(zoom * 5 + 0x50) : ZoomNum * 5 / ZoomDenom) + py;
        box.right = box.left + zscale(zoom, 0xB);
        box.bottom = box.top + zscale(zoom, 0xE);
        char text[4] = {0, 0, 0, 0};
        auto set = [&](const char* sv) { text[0] = sv ? sv[0] : 0; };
        set(Order[order].letter);
        if (veh.state & 0x4000) set("/");
        if ((veh.state & 0x1000200) == 0x1000200) set("P");
        if (veh.state & 0x200) {
            if (veh.order_auto_type == 0xB) set("*");
            if ((veh.state & 0x200) && veh.order_auto_type == 0xA) set("B");
        }
        text[2] = 0;
        text[1] = 0;
        auto brackets = [&]() {
            if (text[0] == '[' || text[0] == ']') text[1] = text[0];
        };
        if (f == CurrentPlayerFaction && (FactionStatus[0] & (1 << f))) {
            brackets();
        } else if ((GameState & 0x80) || !(FactionStatus[0] & (1 << CurrentPlayerFaction))) {
            if ((GameState & 0x40) && (GameState & 0x1000)) text[0] = (char)veh.status_icon;
            if (text[0] != '-' && (veh.state & 0x40000))
                Buffer_set_text_color(dest, (veh.state & 0x20000) ? 0xF8 : 0xFB, -1, 1, 1);
            brackets();
        } else {
            text[0] = '-';
        }
        if (f == CurrentPlayerFaction) {
            if (veh.state & 0x200) {
                Buffer_set_text_color(dest, FactionColorFrame[f], -1, 1, 1);
                if (text[0] == '-') text[0] = 'a';
                if ((GameState & 0x40) && (GameState & 0x1000)) text[0] = (char)veh.status_icon;
            }
            if (veh.state & 0x8000000) Buffer_set_text_color(dest, (CurrentPlayerFaction != 3 ? 5 : 0) - 6, -1, 1, 1);
        }
        Buffer_write_cent_l_7(dest, text, &box, (int32_t)std::strlen(text));
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
    if (flag_only) return 0;
    return idx < 9;
}

void THISCALL MapWin_draw_vehicles(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id) {
    int32_t mode = 2;
    if (!dword_93A940 && !ControlTurnMove && !(GameRules & 0x80)) return;
    uint32_t opts = (uint32_t)mw(win, kMwOptions);
    if ((opts & 0x1000000) || !(opts & 0x802000C0) || (opts & 0x1000)) return;
    MAP* sq = &MapTiles[MapHalfX * y + (x >> 1)];
    auto known = [&]() {
        return (GameState & 0x80) || (Factions[faction_id].player_flags & 0x200) || (sq->visibility & (1 << faction_id));
    };
    if ((!(opts & 0x80000000u) || (opts & 0x80)) && known()) MapWin_draw_bases(win, x, y, px, py, faction_id);
    if (mw(win, kMwOptions) & 0x80000000u) return;
    int32_t v;
    if (VehMovingID != -1 && Vehs[VehMovingID].x == x && Vehs[VehMovingID].y == y) {
        mode = 3;
        v = VehMovingID;
    } else if (VehDrawAttackID >= 0 && x == Vehs[VehDrawAttackID].x && y == Vehs[VehDrawAttackID].y) {
        v = VehDrawAttackID;
        if (Units[Vehs[VehDrawAttackID].unit_id].chassis_id == 8) {
            v = Vehs[VehDrawAttackID].next_veh_id_stack;
            if (v < 0) return;
        }
    } else if (VehDrawDefendID >= 0 && x == Vehs[VehDrawDefendID].x && y == Vehs[VehDrawDefendID].y) {
        v = VehDrawDefendID;
    } else {
        if (!known()) return;
        mode = 3;
        int32_t sel = MapWinVehA;
        if (x == Vehs[sel].x && y == Vehs[sel].y && (FactionStatus[0] & (1 << Vehs[sel].faction_id)) &&
            veh_selectable(sel) && !(Vehs[sel].state & 0x4200)) {
            v = sel;
            if (MapWinInVehMode && MapWinField23BF8 && MapWinField23BE4 && Vehs[sel].order == 0 && !MapWinField23C00 &&
                (!MultiplayerActive || !ControlTurnC)) {
                // The selected unit blinks: hidden while the mouse is near it, else drawn
                // with mode 0x403. (Mouse position: not reconstructed, taken as far away.)
                if (GameMorePreferences & 0x200) return;
                mode = 0x403;
            }
        } else {
            if (!(mw(win, kMwOptions) & 0x200040)) return;
            v = veh_at(x, y);
        }
        if (mw(win, kMwOptions) & 0x200000) {
            v = veh_at(x, y);
            if (v < 0) return;
            while (Vehs[v].home_base_id != BaseUpkeepDrawID) {
                v = Vehs[v].next_veh_id_stack;
                if (v < 0) return;
            }
        }
        if (v < 0) return;
        if (!(GameState & 0x80) &&
            ((Vehs[v].faction_id != CurrentPlayerFaction || (Vehs[v].flags & 0x440) == 0x440) &&
             !(Vehs[v].visibility & (1 << CurrentPlayerFaction))))
            return;
        if ((sq->items & 1) && (sq->val2 & 0xF) < 8) {
            // Units in bases are hidden, except the selected one while it is ready.
            if (!MapWinInVehMode || v != MapWinVehA || !veh_ready(v) || (Vehs[v].state & 0x4200) || !MapWinField23BE4)
                return;
        }
    }
    veh_draw(ui_mapwin_native(win)->canvas, v, px, py, mw(win, kMwZoom), mode, 1);
}
