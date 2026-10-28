// The scenario / map editor (console module 0x4D9970..0x4E1FA0, MapWin_editor 0x46B510),
// reconstructed from the asm: the editor switched on and off (Console_editor_on: ctrl+K, the
// Scenario menu), the map painting (MapWin_editor: the paint chosen by Console_editor_paints /
// _terrain / _minor or the editor's interface pane (editpane.cpp), the brush by
// Console_editor_set_brush; a ctrl+click or a drag on the map paints, the right button removes),
// the map commands (flat / round, polar caps, climate, rockiness, fungus, randomize, clear,
// generate, the natural landmarks placed / removed), the unit commands (Console_editor_veh: a unit
// of the chosen faction and morale at the cursor; Console_editor_kill_veh; kill the stack;
// Console_editor_edit_veh: the flags, MORALE, DATA, PLAYER, HOME), energy reserves, the date, the
// protection lock, the map save / load and undo; the Scenario menu's faction commands (technology,
// view / switch sides, difficulty, eliminate, reload a faction file, reset the factions or the
// technology, personality and strategy; the diplomacy editor: editor_diplo.cpp) and the scenario's
// rules, parameters and victory conditions.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/diplo_act.h"
#include "engine/setup.h"
#include "engine/social.h"
#include "engine/path.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/tech.h"
#include "engine/save.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/map.h"
#include "engine/world.h"
#include "engine/zoc.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/infowin.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/topmenu.h"
#include "ui/win.h"

GLOBAL(int32_t, EditorPaint, 0x945AE0);  // DAT_00945AE0: the paint (0 altitude .. 0x1D)
GLOBAL(int32_t, EditorBrush, 0x945AE4);  // DAT_00945AE4: the brush (0 one tile .. 6)
GLOBAL(int32_t, EditorVehFaction, 0x688350);  // DAT_00688350: the faction of the units created
GLOBAL(int32_t, EditorVehMorale, 0x688354);  // DAT_00688354: their morale
GLOBAL(int32_t, EditorAllUnits, 0x90DB8C);  // DAT_0090DB8C: every unit type listed
GLOBAL(int32_t, EditorLastUnit, 0x90DB90);  // DAT_0090DB90
// The paints 6..0x19 (0x691888, 0x20 bytes each): the tile bit set, the bits cleared with it.
GLOBAL(int32_t[20][8], EditorPaintBits, 0x691888);
// The terrain features' list (Terraform 0x691878.., 0x20 bytes each, 20 entries).

namespace {


int32_t cur_x() { return MapWinCursorX[MapWinCursorIndex]; }
int32_t cur_y() { return MapWinCursorY[MapWinCursorIndex]; }
uint8_t* tile_raw(int32_t x, int32_t y) { return reinterpret_cast<uint8_t*>(map_tile(x, y)); }
uint32_t& tile_bits(int32_t x, int32_t y) { return *reinterpret_cast<uint32_t*>(tile_raw(x, y) + 8); }
uint32_t& tile_bits2(int32_t x, int32_t y) { return *reinterpret_cast<uint32_t*>(tile_raw(x, y) + 0xC); }
bool land(int32_t x, int32_t y) { return (tile_raw(x, y)[0] & 0xE0) >= 0x60; }
void redraw_all() {
    for_each_mapwin([](int32_t, uint8_t* w) { pfncRedrawMap(w, 1); });
}

struct Pop {
    BasePop* p = popup_new();
    ~Pop() { popup_delete(p); }
};

// The paint / brush choice: the info pane's next display (InfoWin +0x90 = 7: the editor's) and
// the editor's interface pane (MainInterface_draw_edit_pane, editpane.cpp).
void edit_pane() {
    std::fprintf(stderr, "editor paint %d brush %d\n", EditorPaint, EditorBrush);
    *reinterpret_cast<int32_t*>(mem::host(0x7AD330)) = 7;
    InfoWin_on_redraw(&InfoWin);
    MainInterface_draw_edit_pane(&MainInterfaceWin);
}

}  // namespace

// ---- Console_editor_on (0x4D9970) ----------------------------------------------------------------

void THISCALL Console_editor_on(uint8_t* self) {
    if ((MultiplayerActive || PbemActive) &&
        !(GameState & (STATE_GAME_DONE | STATE_SCENARIO_CHEATED_FLAG | STATE_SCENARIO_EDITOR)) &&
        !(GameState & STATE_DEBUG_MODE)) {
        NetMsg_pop(&NetMsg, "EDITORNET", 5000, 0, nullptr);
        return;
    }
    if (!(GameState & (STATE_SCENARIO_CHEATED_FLAG | STATE_SCENARIO_EDITOR)) && !X_pop("SCENEDITOR", 0)) return;
    GameState = (GameState ^ 0x40) | 0x20;
    if (GameRules & RULES_IRONMAN) auto_save();
    if (!(GameState & STATE_SCENARIO_EDITOR)) {
        NetMsg_pop(&NetMsg, "EDITOROFF", 5000, 0, nullptr);
        GameState &= ~STATE_OMNISCIENT_VIEW;
    } else {
        NetMsg_pop(&NetMsg, "EDITORON", 5000, 0, nullptr);
        GameState |= STATE_OMNISCIENT_VIEW;
    }
    // EditorPanelWin (the info pane's 0x3EA button) shown with the omniscient view.
    ui_editor_panel_show((GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_SCENARIO_EDITOR) &&
                         InfcList[InfcUsed] == 0);
    pfncMainMenuUpdateVisible(nullptr, console_win(self).veh);
    mi_set_mode((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_OMNISCIENT_VIEW) ? 0x3EA : 0x3E9);
    set_dirty();
    draw_map(1);
    std::fprintf(stderr, "scenario editor %s (GameState %08X)\n", (GameState & STATE_SCENARIO_EDITOR) ? "on" : "off",
                 GameState);
    Console_update_data(self, 0);
}

// ---- the paints ----------------------------------------------------------------------------------

// Console_editor_paints (0x4E0A50): the paint, its button of the pane's group 2 pressed (the
// terrain features' buttons skip the features without a tile bit).
void GAMEAPI Console_editor_paints(int32_t paint) {
    EditorPaint = paint;
    edit_pane();
    int32_t id = paint + 0x3F3;
    if (paint > 5 && paint < 0x1A) {
        int32_t k = paint - 6;
        for (int32_t j = 19; j >= 0; j--)
            if (!Terraform[j].bit && j <= k) k--;
        id = k + 0x3F9;
    }
    editpane_group_set(2, id);
}

void GAMEAPI Console_editor_set_brush() {
    PopupDialogState = 6;
    int32_t r = X_pop_9(ScriptFileName, "BRUSH", EditorBrush, nullptr, 0x40, 0);
    PopupDialogState = 0;
    if (r >= 0) EditorBrush = r;
    edit_pane();
    if (ScreenWidth != 800) editpane_group_set(3, EditorBrush + 0x40C);
}

void GAMEAPI Console_editor_minor() {
    int32_t r = X_pop_6("EDITORMINOR", 0x40, 0);
    if (r >= 0) Console_editor_paints(r + 0x1A);
}

// The terrain features of Terraform (0x691878, 0x20 bytes: name, name of the sea version, the
// tile bit +0x10): those with a bit, "name/sea name" when they differ; paint 6 + the index.
void GAMEAPI Console_editor_terrain() {
    Pop pop;
    PopupDialogState = 6;
    BasePop_start(pop.p, ScriptFileName, "EDITORTERRAIN", -1, nullptr, 0, nullptr);
    for (int32_t i = 0; i < 20; i++) {
        if (!Terraform[i].bit) continue;
        StrBuffer[0] = 0;
        const char* a = Terraform[i].name;
        const char* b = Terraform[i].name_sea;
        std::strcat(StrBuffer, a);
        if (std::strcmp(a, b)) {
            std::strcat(StrBuffer, "/");
            std::strcat(StrBuffer, b);
        }
        BasePop_item(pop.p, StrBuffer, i);
    }
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    if (r >= 0) Console_editor_paints(r + 6);
}

// ---- MapWin_editor (0x46B510) --------------------------------------------------------------------

// Paints tile (x, y) and the brush's tiles around it (TableOffset: the brush's range, the cross
// every other offset): `right` removes. With the one-tile brush and no change on the tile, the
// next ring is tried (up to 8). A drag (+0x1DD8C) paints each tile once.
void THISCALL MapWin_editor(uint8_t* win, int32_t x, int32_t y, int32_t right) {
    if (map_win(win).edit_paint) {
        if (x == map_win(win).edit_x && y == map_win(win).edit_y) return;
        map_win(win).edit_x = x;
        map_win(win).edit_y = y;
    }
    const bool drag = map_win(win).edit_paint != 0;
    int32_t step = EditorBrush == 1 ? 2 : 1;
    int32_t brush = EditorBrush > 1 ? EditorBrush - 1 : EditorBrush;
    int32_t end = TableRange[brush], start = 0;
    int32_t change = 0;  // bVar12
    bool single = false, ring = false;
    int32_t climate_change = 1;  // local_20
    for (int32_t r = 0; !single && r <= 8; r++) {
        if (r) {
            if (EditorBrush || drag) break;
            start = TableRange[r - 1];
            end = TableRange[r];
            step = 1;
            ring = true;
        }
        for (int32_t c = start; c < end && !single;) {
            int32_t tx = TableOffsetX[c] + x, ty = TableOffsetY[c] + y;
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            if (ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX) {
                switch (EditorPaint) {
                case 0:  // elevation
                    if (!drag) {
                        if (!right) world_raise_alt(tx, ty);
                        else world_lower_alt(tx, ty);
                        change = 1;
                    } else if (!right) {
                        if (!land(tx, ty)) world_alt_set(tx, ty, 3, 1);
                        change = 1;
                    } else {
                        if (land(tx, ty)) world_alt_set(tx, ty, 2, 1);
                        change = 1;
                    }
                    break;
                case 1: {  // rockiness
                    int32_t rk = tile_raw(tx, ty)[5] >> 6;
                    if (!right) {
                        rocky_set(tx, ty, rk + 1 < 3 ? rk + 1 : 2);
                        if (rk < 2) change = 1;
                    } else {
                        rocky_set(tx, ty, std::max(0, std::min(2, rk - 1)));
                        if (rk) change = 1;
                    }
                    break;
                }
                case 2:  // a river source (the cursor's tile)
                    climate_change = right ? (tile_bits(x, y) & 0x100) : (~tile_bits(x, y) >> 8 & 1);
                    bit_set(x, y, 0x100, !right);
                    single = true;
                    break;
                case 3: {  // a special resource, cycling its kinds (the cursor's tile)
                    if (!right) {
                        uint32_t b = tile_bits(x, y);
                        if (!(b & 0x400)) {
                            bit_set(x, y, 0x20000400, 1);
                            bit_set(x, y, 0x10000000, 0);
                        } else if (b & 0x10000) {
                            bit_set(x, y, 0x10000, 0);
                            bit_set(x, y, 0x20000, 1);
                        } else if (b & 0x20000) {
                            bit_set(x, y, 0x20000, 0);
                        } else {
                            bit_set(x, y, 0x10000, 1);
                        }
                    } else {
                        bit_set(x, y, 0x20000400, 0);
                    }
                    single = true;
                    break;
                }
                case 4: {  // a unity pod
                    uint32_t b = tile_bits(tx, ty);
                    if (!right) {
                        if ((b & 0x100000) || !(b & 0x10000000)) change = 1;
                        bit_set(tx, ty, 0x10000000, 1);
                        bit_set(tx, ty, 0x20100400, 0);
                    } else {
                        if ((b & 0x10000000) || !(b & 0x100000)) change = 1;
                        bit_set(tx, ty, 0x10000000, 0);
                        bit_set(tx, ty, 0x100000, 1);
                    }
                    break;
                }
                case 5:  // cover / uncover: the viewer's knowledge of the tile
                    if (!right) {
                        if (!(Factions[CurrentPlayerFaction].player_flags & 0x200) &&
                            !(tile_raw(tx, ty)[4] & (1 << CurrentPlayerFaction)))
                            change = 1;
                        spot_loc(tx, ty, CurrentPlayerFaction);
                        synch_bit(tx, ty, CurrentPlayerFaction);
                    } else {
                        if ((Factions[CurrentPlayerFaction].player_flags & 0x200) ||
                            (tile_raw(tx, ty)[4] & (1 << CurrentPlayerFaction)))
                            change = 1;
                        tile_raw(tx, ty)[4] &= (uint8_t)~(1 << CurrentPlayerFaction);
                        int32_t b = base_at(tx, ty);
                        if (b >= 0) Bases[b].visibility &= (uint8_t)~(1 << CurrentPlayerFaction);
                    }
                    break;
                case 0x1A:
                case 0x1B:
                case 0x1C:
                case 0x1D: {  // the minor features (bit2)
                    static const uint32_t kBit[4] = {4, 0x40, 8, 0x400};
                    uint32_t bit = kBit[EditorPaint - 0x1A];
                    uint32_t b2 = tile_bits2(tx, ty);
                    if (EditorPaint == 0x1A) {
                        if (!right) {
                            change = ((b2 & 0x80000004) != 4 || !land(tx, ty)) ? 1 : 0;
                            bit2_set(tx, ty, 4, 1);
                            bit2_set(tx, ty, 0x80000000, 0);
                        } else {
                            change = ((b2 & 0x80000004) != 4 || !land(tx, ty)) ? 0 : 1;
                            bit2_set(tx, ty, 4, 0);
                        }
                    } else if (!right) {
                        change = (b2 & (0x80000000 | bit)) != bit;
                        bit2_set(tx, ty, bit, 1);
                        bit2_set(tx, ty, 0x80000000, 0);
                    } else {
                        change = (b2 & (0x80000000 | bit)) == bit;
                        bit2_set(tx, ty, bit, 0);
                    }
                    break;
                }
                default: {  // 6..0x19: a terrain enhancement of the table (same land / sea as the cursor's)
                    if (EditorPaint < 6 || EditorPaint > 0x19) break;
                    uint32_t bit = (uint32_t)EditorPaintBits[EditorPaint - 6][0];
                    if (land(tx, ty) != land(x, y)) break;
                    if (!right) {
                        if (!(tile_bits(tx, ty) & bit)) change = ((bit & 0xC) != 0) + 1;
                        if (bit & 8) bit |= 4;
                        bit_set(tx, ty, (uint32_t)EditorPaintBits[EditorPaint - 6][1], 0);
                        bit_set(tx, ty, bit, 1);
                    } else {
                        if (tile_bits(tx, ty) & bit) change = 1;
                        bit_set(tx, ty, bit, 0);
                    }
                    break;
                }
                }
                for (int32_t f = 1; f < 8; f++) synch_bit(tx, ty, f);
            }
            c += c == 0 ? 1 : step;
        }
        if (change) break;
    }
    if (drag && !(map_win(win).edit_paint & 4)) {
        MapWin_draw_radius(win, x, y, brush, 2);
        return;
    }
    if (EditorPaint == 0 || EditorPaint == 2) {
        if (climate_change) {
            world_climate();
            redraw_all();
        }
    } else if (EditorPaint == 0x12 && change) {
        world_climate();
        redraw_all();
    } else if (ring || EditorBrush || change > 1) {
        redraw_all();
    } else if (change) {
        for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, 0, 2); });
    }
    std::fprintf(stderr, "editor paint %d at %d,%d%s: change %d\n", EditorPaint, x, y, right ? " (remove)" : "",
                 change);
}

// ---- the map commands ----------------------------------------------------------------------------

void GAMEAPI Console_editor_flat() {
    auto_undo();
    MapToggleFlat ^= 1;
    draw_map(1);
    X_pop((MapToggleFlat & 1) ? "MAPFLAT" : "MAPROUND", 0);
}

void GAMEAPI Console_editor_polar() {
    auto_undo();
    world_polar_caps();
    world_climate();
    draw_map(1);
}

void GAMEAPI Console_editor_climate() {
    auto_undo();
    if (custom_planet(0, 0) == 0) {
        world_climate();
        draw_map(1);
    }
}

void GAMEAPI Console_editor_rockiness() {
    auto_undo();
    if (X_pop_6("ROCKOSITY", 0x40, 0) != 0) {
        world_rocky();
        draw_map(1);
    }
}

// Fungus: FUNGOSITY (0 none, else the native life level + 1); FUNGMOTIZE 0 keeps the fungus.
void THISCALL Console_editor_fungus(uint8_t*) {
    auto_undo();
    int32_t level = X_pop_6("FUNGOSITY", 0x40, 0);
    if (level < 0) return;
    bool clear = true;
    if (level > 0) {
        int32_t keep = X_pop_6("FUNGMOTIZE", 0x40, 0);
        if (keep < 0) return;
        clear = keep != 0;
    }
    if (clear) {
        for (int32_t i = 0; i < MapAreaTiles; i++) {
            uint32_t* b = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8);
            *b &= 0xFFFFFFDF;
            for (int32_t f = 1; f < 8; f++) b[1 + f] = b[0];
        }
    }
    int32_t keep_life = MapNativeLifeForms;
    if (level > 0) {
        MapNativeLifeForms = level - 1;
        world_fungus();
    }
    MapNativeLifeForms = keep_life;
    draw_map(1);
}

namespace {
// The tile bits masked on every tile, the factions' copies (+0x10..) refreshed.
void mask_tiles(uint32_t keep) {
    for (int32_t i = 0; i < MapAreaTiles; i++) {
        uint32_t* b = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8);
        *b &= keep;
        for (int32_t f = 1; f < 8; f++) b[1 + f] = b[0];
    }
}
void wipe_units() {
    GameState |= 4;
    VehCount = 0;
    BaseCount = 0;
    MapWinInVehMode = 0;
    *reinterpret_cast<int32_t*>(mem::host(0x93928C)) = -1;
}
}  // namespace

void GAMEAPI Console_editor_clear() {
    auto_undo();
    wipe_units();
    map_wipe();
    size_of_planet(2);
    map_shutdown();
    map_init();
    Path_init(&Paths);
    map_wipe();
    draw_map(1);
}

void GAMEAPI Console_editor_clear_vehicles() {
    auto_undo();
    wipe_units();
    for (int32_t i = 0; i < MapAreaTiles; i++) {
        uint32_t* b = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8);
        *b &= 0xFFFFFFFC;
        for (int32_t f = 1; f < 8; f++) b[1 + f] = b[0];
    }
    reset_territory();
    draw_map(1);
}

void GAMEAPI Console_editor_clear_terrain() {
    auto_undo();
    mask_tiles(0x420347A3);
    draw_map(1);
}

void GAMEAPI Console_editor_randomize() {
    auto_undo();
    MapRandomSeed = game_random(0, 0x7FFF) + 1;
    mask_tiles(0xCFEFFBFF);
    draw_map(1);
}

void GAMEAPI Console_editor_clear_random() {
    auto_undo();
    if (MapRandomSeed) {
        MapRandomSeed = 0;
        for (int32_t i = 0; i < MapAreaTiles; i++) {
            uint32_t* b = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8);
            if ((*b & 0x400) && !(*b & 0x20000000)) {
                *b &= 0xDFFFFBFF;
                for (int32_t f = 1; f < 8; f++) b[1 + f] = b[0];
            }
        }
        draw_map(1);
        return;
    }
    MapRandomSeed = game_random(0, 0x7FFF) + 1;
    draw_map(1);
}

void GAMEAPI Console_editor_fast() {
    auto_undo();
    wipe_units();
    for (int32_t i = 0; i < MapAreaTiles; i++)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8) &= 0xFFFFFFFC;
    uint32_t omni = GameState & STATE_OMNISCIENT_VIEW;
    GameState &= ~STATE_OMNISCIENT_VIEW;
    world_build();
    if (omni) GameState |= STATE_OMNISCIENT_VIEW;
    draw_map(1);
}

void GAMEAPI Console_editor_generate() {
    auto_undo();
    VehCount = 0;
    BaseCount = 0;
    MapWinInVehMode = 0;
    *reinterpret_cast<int32_t*>(mem::host(0x93928C)) = -1;
    for (int32_t i = 0; i < MapAreaTiles; i++)
        *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8) &= 0xFFFFFFFC;
    *reinterpret_cast<int32_t*>(mem::host(0x9392A8)) = 0;
    GameState |= 4;
    map_win(MapWins[0]).zoom = -0xE;
    pfncRedrawMap(MapWins[0], 1);
    uint32_t omni = GameState & STATE_OMNISCIENT_VIEW;
    GameState |= STATE_OMNISCIENT_VIEW;
    world_build();
    draw_map(1);
    if (!omni) {
        GameState &= ~STATE_OMNISCIENT_VIEW;
        draw_map(1);
    }
}

void GAMEAPI Console_editor_lock2() {
    if (GameState & STATE_EDITOR_ONLY_MODE) {
        GameState &= ~STATE_EDITOR_ONLY_MODE;
        NetMsg_pop(&NetMsg, "PROTECTOFF", 5000, 0, nullptr);
    } else {
        GameState |= STATE_EDITOR_ONLY_MODE;
        NetMsg_pop(&NetMsg, "PROTECTON", 5000, 0, nullptr);
        if (MapWinInVehMode) Console_set_view(&MainConsole, 1);
    }
    MainInterface_draw_edit_pane(&MainInterfaceWin);
}

void GAMEAPI Console_editor_undo() {
    load_undo(1);
    draw_map(1);
}
void GAMEAPI Console_editor_redo() {
    load_undo(0);
    draw_map(1);
}

void GAMEAPI Console_editor_save() { save_map(); }

void GAMEAPI Console_editor_load() {
    if (load_map() == 0) {
        GameState |= 4;
        *reinterpret_cast<int32_t*>(mem::host(0x93928C)) = -1;
        MapWinInVehMode = 0;
        VehCount = 0;
        BaseCount = 0;
        draw_map(1);
    }
}

// ---- the factions' values --------------------------------------------------------------------------

// Console_editor_who (0x4D9AD0): EDITORWHO, the factions from `from` (their nouns), the active one
// selected; the chosen faction or -1.
int32_t THISCALL Console_editor_who(uint8_t*, int32_t faction_id, int32_t from) {
    Pop pop;
    PopupDialogState = 6;
    BasePop_start(pop.p, ScriptFileName, "EDITORWHO", faction_id, nullptr, 0, nullptr);
    for (int32_t f = from; f < 8 && from < 8; f++) {
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        BasePop_item(pop.p, MFactions[f].noun_faction, f);
    }
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    return r;
}

void GAMEAPI Console_editor_energy() {
    auto_undo();
    int32_t f = Console_editor_who(&MainConsole, CurrentPlayerFaction, 1);
    if (f < 0) return;
    parse_says(0, MFactions[f].noun_faction, -1, -1);
    for (int32_t i = 1; i < 8; i++) {
        GenderDefault = MFactions[i].noun_gender;
        PluralDefault = 0;
        parse_says(i, MFactions[i].noun_faction, -1, -1);
        parse_num(i, Factions[i].energy_credits);
    }
    if (X_pop_ask_number("EDITORENERGY", Factions[f].energy_credits, 0) == 0) {
        Factions[f].energy_credits = ParseNumTable[0];
        BaseWin_on_redraw(&BaseWin);
        Console_update_data(&MainConsole, 0);
    }
}

void GAMEAPI Console_editor_date() {
    auto_undo();
    if (X_pop_ask_number("EDITORYEAR", game_year(CurrentTurn), 0) == 0) {
        CurrentTurn = ParseNumTable[0] - StartingMissionYear;
        if (CurrentTurn < 1) CurrentTurn = 1;
        ui_invalidate();
        BaseWin_on_redraw(&BaseWin);
        Console_update_data(&MainConsole, 0);
    }
}

// ---- the units ------------------------------------------------------------------------------------

void GAMEAPI Console_editor_kill_vehicles() {
    auto_undo();
    stack_kill(veh_at(cur_x(), cur_y()));
}

// Console_editor_kill_veh (0x4DE960): the unit (-1: the selected one in the unit mode, else one
// picked from the cursor's stack: PICKKILL) deleted after DELETEME.
void THISCALL Console_editor_kill_veh(uint8_t*, int32_t veh_id) {
    auto_undo();
    if (veh_id < 0) {
        if (MapWinInVehMode) {
            veh_id = *reinterpret_cast<int32_t*>(mem::host(0x93928C));
        } else {
            int32_t v = veh_at(cur_x(), cur_y());
            if (v < 0) return;
            veh_id = veh_pick(v, "PICKKILL", 1);
            if (veh_id < 0) return;
        }
    }
    parse_says(0, Units[Vehs[veh_id].unit_id].name, -1, -1);
    if (X_pop("DELETEME", 0)) {
        std::fprintf(stderr, "editor: unit %d deleted\n", veh_id);
        kill(veh_id);
    }
}

// Console_editor_veh (0x4DED00): EDITORUNIT, the unit types (the prototypes the faction can build
// unless All); its buttons: All, Player (EDITORWHO), Morale; OK creates the unit at the cursor
// (unit 0x13: the tile's monolith / pod bits cleared first).
void GAMEAPI Console_editor_veh() {
    auto_undo();
    if (EditorVehFaction < 0) EditorVehFaction = CurrentPlayerFaction;
    for (;;) {
        Pop pop;
        PopupDialogState = 6;
        BasePop_start(pop.p, ScriptFileName, "EDITORUNIT", EditorLastUnit, nullptr, 0x40, nullptr);
        for (int32_t u = 0; u < 512; u++) {
            if (!(Units[u].unit_flags & 1)) continue;
            if (!EditorAllUnits && !veh_avail(u, EditorVehFaction, -1) && u >= 64) continue;
            BasePop_item(pop.p, Units[u].name, u);
        }
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        PopupDialogState = 0;
        if (r < 0) return;
        EditorLastUnit = r;
        int32_t button = BasePop_button_pressed(pop.p);
        if (button == 1) {
            EditorAllUnits = !EditorAllUnits;
            continue;
        }
        if (button == 2) {
            int32_t f = Console_editor_who(&MainConsole, EditorVehFaction, 0);
            if (f >= 0) EditorVehFaction = f;
            continue;
        }
        if (button == 3) {
            Pop m;
            // (the original restarts EDITORUNIT's popup with the morale names; EDITMORALE's caption here)
            BasePop_start(m.p, ScriptFileName, "EDITMORALE", EditorVehMorale, nullptr, 0, nullptr);
            for (int32_t i = 0; i < 7; i++) BasePop_item(m.p, Strings_get(&StringTable, Morale[i].name), i);
            int32_t mr = BasePop_exec_3(m.p, 0, 0);
            if (mr >= 0) EditorVehMorale = mr;
            continue;
        }
        const int32_t x = cur_x(), y = cur_y();
        if (r == 0x13) {
            EditorVehFaction = 0;
            bit_set(x, y, 0x20, 1);
            bit_set(x, y, 0x28805C, 0);
            for (int32_t f = 1; f < 8; f++) synch_bit(x, y, f);
            draw_tile(x, y, 1);
        }
        int32_t v = veh_init(r, EditorVehFaction, x, y);
        if (v >= 0) {
            Vehs[v].morale = (int8_t)EditorVehMorale;
            draw_tile_fixup(x, y, Units[Vehs[v].unit_id].unit_flags & 2, 2);
            if (Vehs[v].faction_id == (uint32_t)CurrentPlayerFaction) {
                *reinterpret_cast<int32_t*>(mem::host(0x93928C)) = v;
                if (MapWinInVehMode) Console_set_move(&MainConsole, 1);
            }
            FactionStatus[1] |= (uint8_t)(1 << EditorVehFaction);
            std::fprintf(stderr, "editor: unit %d (%s) of faction %d at %d,%d\n", v, Units[r].name, EditorVehFaction, x,
                         y);
        }
        return;
    }
}

// The omniscient view on / off (the y key, the Scenario menu's 0x601): the editor's panel button
// (EditorPanelWin) shown with it, the map, the world map, the info and status panes and the
// caption redrawn.
void editor_toggle_omniscient() {
    GameState ^= STATE_OMNISCIENT_VIEW;
    ui_editor_panel_show((GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_SCENARIO_EDITOR));
    draw_map(1);
    GraphicWin_redraw(&WorldWindow);
    desktop_update();
    InfoWin_change(&InfoWindow, 0);
    StatusWin_redraw(&InfoWin);
    MapWin_main_caption(MapWins[0]);
    std::fprintf(stderr, "editor: omniscient view %s\n", (GameState & STATE_OMNISCIENT_VIEW) ? "on" : "off");
}

// ---- the keys (Console_on_key_click's editor part) -----------------------------------------------

bool editor_key(uint8_t* self, int32_t key, int32_t ch) {
    if (!(GameState & STATE_SCENARIO_EDITOR)) return false;
    int32_t vk = key & 0x7FFFF;
    // The characters' switch (tolower (0x6466EF)'s lower case): 'y' the omniscient view. (F10 stays
    // the hall of fame.)
    if (ch == 'y' || ch == 'Y') {
        set_dirty();
        editor_toggle_omniscient();
        return true;
    }
    switch (vk) {
    case 0x10070: Console_editor_veh(); return true;
    case 0x10071: tech_advance(CurrentPlayerFaction); return true;
    case 0x10072: Console_editor_view(); return true;
    case 0x10073: Console_editor_energy(); return true;
    case 0x10074: Console_editor_date(); return true;
    case 0x10075: Console_editor_eliminate(); return true;
    case 0x10076: show_replay(); return true;
    case 0x10077: Console_show_movies(self); return true;
    case 0x10078: Console_editor_diplomacy(); return true;
    case 0x10079: Console_editor_rules(self); return true;
    case 0x1007A: Console_editor_scen_rules(); return true;
    case 0x1007B: Console_editor_scen_param(); return true;
    case 0x20071: Console_editor_tech(); return true;
    case 0x30070: Console_editor_edit_veh(self, -1); return true;
    case 0x20074: Console_editor_save(); return true;
    case 0x20075: Console_editor_kill_vehicles(); return true;
    case 0x20076: save_game(1); return true;
    case 0x20077: Console_editor_reset_tech(self); return true;
    case 0x20078: Console_editor_personality(); return true;
    case 0x30071: editor_all_techs(); return true;
    case 0x30072: Console_editor_diff(); return true;
    case 0x30074: Console_editor_load(); return true;
    case 0x30075: Console_editor_reload(); return true;
    case 0x30076: Console_load_proc(self, 1, 0); return true;
    case 0x30077: Console_editor_reset_faction(); return true;
    case 0x30078: Console_editor_strategy(); return true;
    case 0x30079: Console_editor_lock2(); return true;
    case 0x3007B: Console_editor_scen_victory(); return true;
    case 0x40008: Console_editor_undo(); return true;
    default: break;
    }
    if (key & 0x30000) return false;
    switch (vk) {
    case '0': Console_editor_fast(); return true;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5': Console_editor_paints(vk - '1'); return true;
    case '6': Console_editor_terrain(); return true;
    case '7': Console_editor_set_brush(); return true;
    case '8': Console_editor_climate(); return true;
    case '9': Console_editor_generate(); return true;
    default: return false;
    }
}

// Console_editor_edit_veh (0x4DDA50): the unit (-1: the selected one, else picked: PICKEDIT).
// EDITVEH: the unit's flags as check boxes, then its buttons: MORALE (EDITMORALE), DATA (EDITVEH2 /
// EDITVEH2A: damage, moves spent, the year its lurking ends and, for a chassis of range > 1, the
// turns aloft), PLAYER (EDITVEHWHO), HOME (EDITVEHHOME).
void THISCALL Console_editor_edit_veh(uint8_t*, int32_t veh_id) {
    auto_undo();
    if (veh_id < 0) {
        if (MapWinInVehMode) {
            veh_id = MapWinVehA;
        } else {
            int32_t v = veh_at(cur_x(), cur_y());
            if (v < 0) return;
            veh_id = veh_pick(v, "PICKEDIT", 1);
            if (veh_id < 0) return;
        }
    }
    VEH& v = Vehs[veh_id];
    // The check boxes from the unit's flags (0x20 objective, 0x80, 0x100, 0x200, 0x40, 0x400) and
    // its state bit 0x100.
    static const uint16_t kFlag[6] = {0x20, 0x80, 0x100, 0x200, 0x40, 0x400};
    uint32_t c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 6; i++) c = (v.flags & kFlag[i]) ? (c | (1u << i)) : (c & ~(1u << i));
    c = (v.state & 0x100) ? (c | 0x40) : (c & ~0x40u);
    DialogChoices = (int32_t)c;
    StrBuffer[0] = 0;
    std::strcat(StrBuffer, Units[v.unit_id].name);
    std::strcat(StrBuffer, " ");
    std::strcat(StrBuffer, "(");
    std::snprintf(StrBuffer + std::strlen(StrBuffer), 16, "%d", veh_id);
    std::strcat(StrBuffer, ")");
    parse_says(0, StrBuffer, -1, -1);
    if (X_pop_6("EDITVEH", 0x41, 0) < 0) return;
    c = (uint32_t)DialogChoices;
    v.flags &= 0xF81F;
    v.state &= ~0x100u;
    for (int32_t i = 0; i < 6; i++)
        if (c & (1u << i)) v.flags |= kFlag[i];
    if (c & 0x40) v.state |= 0x100;
    if (v.flags & 0x400) owner_set(v.x, v.y, -1);
    Console_update_data(&MainConsole, 0);
    draw_tile(v.x, v.y, 1);
    std::fprintf(stderr, "editor: unit %d flags %04X state %08X (button %d)\n", veh_id, v.flags, v.state, DialogToggle);
    if (DialogToggle == 1) {  // MORALE: the levels (native life's names for psi units), the shown morale
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "EDITMORALE", -1, nullptr, 0x40);
        for (int32_t i = 0; i < 7; i++) {
            bool life = v.unit_id < 0x40 && (Weapon[Units[v.unit_id].weapon_id].offense_value < 0 || v.unit_id == 0xF);
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, Strings_get(&StringTable, life ? Morale[i].name_lifecycle : Morale[i].name));
            BasePop_item(pop.p, StrBuffer, i);
            if (i == morale_veh(veh_id, 1, 0)) BasePop_set_active(pop.p, i);
        }
        int32_t bonus = morale_veh(veh_id, 1, 0) - v.morale;
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        v.morale = (uint8_t)std::max(0, std::min(6, r - bonus));
        // Native life: its lifecycle bits (flags 0x18) moved until morale_alien reaches the level.
        if (v.faction_id == 0) {
            if (r != 0)
                while (r < morale_alien(veh_id, 0) && ((v.flags >> 3) & 3))
                    v.flags = (uint16_t)((v.flags & ~0x18) | ((((v.flags >> 3) & 3) - 1) << 3));
            if (r <= 6)
                while (r > morale_alien(veh_id, 0) && ((v.flags >> 3) & 3) < 3)
                    v.flags = (uint16_t)((v.flags & ~0x18) | ((((v.flags >> 3) & 3) + 1) << 3));
        }
        std::fprintf(stderr, "editor: unit %d morale %d flags %04X\n", veh_id, v.morale, v.flags);
        Console_update_data(&MainConsole, 0);
    } else if (DialogToggle == 2) {  // DATA: the numbers as the edit group's fields ($STR0..3)
        auto say = [](int32_t slot, int32_t n) {
            char num[16];
            std::snprintf(num, sizeof(num), "%d", n);
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, num);
            parse_says(slot, StrBuffer, -1, -1);
        };
        say(0, v.damage_taken);
        say(1, v.moves_spent);
        say(2, v.year_end_lurking);
        bool air = Chassis[Units[v.unit_id].chassis_id].range > 1;
        if (air) say(3, v.movement_turns);
        Pop pop;
        Popup_start(pop.p, ScriptFileName, air ? "EDITVEH2A" : "EDITVEH2", -1, nullptr, 0x44);
        if (BasePop_exec_3(pop.p, 0, 0) < 0) return;
        v.damage_taken = (uint8_t)std::atoi(ParseStrBuffer[0]);
        v.moves_spent = (uint8_t)std::atoi(ParseStrBuffer[1]);
        v.year_end_lurking = (uint8_t)std::atoi(ParseStrBuffer[2]);
        if (Chassis[Units[v.unit_id].chassis_id].range > 1) v.movement_turns = (uint8_t)std::atoi(ParseStrBuffer[3]);
        std::fprintf(stderr, "editor: unit %d data damage %d moves %d lurking %d turns %d\n", veh_id, v.damage_taken,
                     v.moves_spent, v.year_end_lurking, v.movement_turns);
        Console_update_data(&MainConsole, 0);
        draw_tile(v.x, v.y, 1);
    } else if (DialogToggle == 3) {  // PLAYER
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "EDITVEHWHO", -1, nullptr, 0x42);
        for (int32_t f = 0; f < 8; f++) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, MFactions[f].noun_faction);
            BasePop_item(pop.p, StrBuffer, f);
        }
        BasePop_set_active(pop.p, v.faction_id);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        v.faction_id = (uint8_t)r;
        std::fprintf(stderr, "editor: unit %d faction %d\n", veh_id, r);
        draw_tile(v.x, v.y, 1);
        Console_update_data(&MainConsole, 0);
    } else if (DialogToggle == 4) {  // HOME: none (9999) or a base of the unit's faction
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "EDITVEHHOME", -1, nullptr, 0x42);
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, label_get(25));
        BasePop_item(pop.p, StrBuffer, 9999);
        for (int32_t b = 0; b < BaseCount; b++)
            if ((uint8_t)Bases[b].faction_id == v.faction_id) BasePop_item(pop.p, Bases[b].name, b);
        BasePop_set_active(pop.p, v.home_base_id < 0 ? 9999 : v.home_base_id);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        v.home_base_id = (int16_t)(r == 9999 ? -1 : r);
        std::fprintf(stderr, "editor: unit %d home %d\n", veh_id, v.home_base_id);
        draw_tile(v.x, v.y, 1);
        Console_update_data(&MainConsole, 0);
    }
}

// ---- the factions (Scenario menu) -----------------------------------------------------------------

namespace {

uint8_t* fac_raw(int32_t f) { return reinterpret_cast<uint8_t*>(&Factions[f]); }
int32_t& fac_i(int32_t f, uint32_t off) { return *reinterpret_cast<int32_t*>(fac_raw(f) + off); }
uint32_t& fac_u(int32_t f, uint32_t off) { return *reinterpret_cast<uint32_t*>(fac_raw(f) + off); }
// Faction fields by their offsets: player_flags (+0), the AI's priorities (+0x3DC fight, +0x3E0
// growth, +0x3E4 tech, +0x3E8 wealth, +0x3EC power), the extended flags (+0x2090).
constexpr uint32_t kFlags = 0, kAiFight = 0x3DC, kAiGrowth = 0x3E0, kAiTech = 0x3E4, kAiWealth = 0x3E8,
                   kAiPower = 0x3EC, kFlagsExt = 0x2090;

// The faction list popup of the editor's commands (EDITORWHO, the factions 1..7 by noun).
int32_t who_popup(const char* label, int32_t active) {
    Pop pop;
    PopupDialogState = 6;
    Popup_start(pop.p, ScriptFileName, label, -1, nullptr, 0x40);
    for (int32_t f = 1; f < 8; f++) {
        PluralDefault = MFactions[f].is_noun_plural;
        GenderDefault = MFactions[f].noun_gender;
        BasePop_item(pop.p, MFactions[f].noun_faction, f);
    }
    if (active >= 0) BasePop_set_active(pop.p, active);
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    return r;
}

// $TITLE0 $NAME1 of the $FACTION2 (AIEDITOR / AIEDITOR2).
void say_leader(int32_t f) {
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(0, MFactions[f].title_leader, -1, -1);
    GenderDefault = MFactions[f].is_leader_female;
    PluralDefault = 0;
    parse_says(1, MFactions[f].name_leader, -1, -1);
    GenderDefault = MFactions[f].noun_gender;
    PluralDefault = MFactions[f].is_noun_plural;
    parse_says(2, MFactions[f].noun_faction, -1, -1);
}

void set_bit(uint32_t& c, uint32_t bit, bool on) { c = on ? (c | bit) : (c & ~bit); }

}  // namespace

// Console_editor_personality (0x4DBD20): AIEDITOR, the faction's priorities and attitudes.
void GAMEAPI Console_editor_personality() {
    int32_t f = Console_editor_who(&MainConsole, CurrentPlayerFaction, 1);
    if (f < 0) return;
    auto_undo();
    say_leader(f);
    uint32_t c = (uint32_t)DialogChoices;
    set_bit(c, 1, fac_i(f, kAiGrowth) != 0);
    set_bit(c, 2, fac_i(f, kAiTech) != 0);
    set_bit(c, 4, fac_i(f, kAiWealth) != 0);
    set_bit(c, 8, fac_i(f, kAiPower) != 0);
    set_bit(c, 0x10, fac_i(f, kAiFight) > 0);
    set_bit(c, 0x20, fac_i(f, kAiFight) < 0);
    uint32_t fl = fac_u(f, kFlags);
    set_bit(c, 0x40, fl & 0x800000);
    set_bit(c, 0x80, fl & 0x400000);
    set_bit(c, 0x100, fl & 0x1000000);
    set_bit(c, 0x200, fl & 0x2000000);
    set_bit(c, 0x400, fac_u(f, kFlagsExt) & 0x20);
    DialogChoices = (int32_t)c;
    if (X_pop_6("AIEDITOR", 0x41, 0) < 0) return;
    fac_u(f, kFlags) &= 0xFC3FFFFF;
    c = (uint32_t)DialogChoices;
    fac_u(f, kFlagsExt) &= ~0x20u;
    fac_i(f, kAiGrowth) = (c & 1) != 0;
    fac_i(f, kAiTech) = (c & 2) != 0;
    fac_i(f, kAiWealth) = (c & 4) != 0;
    fac_i(f, kAiPower) = (c & 8) != 0;
    fac_i(f, kAiFight) = (c & 0x10) && !(c & 0x20) ? 1 : (!(c & 0x10) && (c & 0x20) ? -1 : 0);
    if (c & 0x40) fac_u(f, kFlags) |= 0x800000;
    if (c & 0x80) fac_u(f, kFlags) |= 0x400000;
    if (c & 0x100) fac_u(f, kFlags) |= 0x1000000;
    if (c & 0x200) fac_u(f, kFlags) |= 0x2000000;
    if (c & 0x400) fac_u(f, kFlagsExt) |= 0x20;
    std::fprintf(stderr, "editor: faction %d personality %d %d %d %d fight %d flags %08X ext %08X\n", f,
                 fac_i(f, kAiGrowth), fac_i(f, kAiTech), fac_i(f, kAiWealth), fac_i(f, kAiPower), fac_i(f, kAiFight),
                 fac_u(f, kFlags), fac_u(f, kFlagsExt));
}

// Console_editor_strategy (0x4DBFB0): AIEDITOR2, the faction's strategy flags.
void GAMEAPI Console_editor_strategy() {
    int32_t f = Console_editor_who(&MainConsole, CurrentPlayerFaction, 1);
    if (f < 0) return;
    auto_undo();
    say_leader(f);
    static const uint32_t kMain[7] = {0x4000000, 0x8000000, 0x10000000, 0x80000000, 0x40000000, 0x20000000, 0x200000};
    static const uint32_t kExt[6] = {1, 2, 4, 8, 0x10, 0x40};
    uint32_t c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 7; i++) set_bit(c, 1u << i, fac_u(f, kFlags) & kMain[i]);
    for (int32_t i = 0; i < 6; i++) set_bit(c, 0x80u << i, fac_u(f, kFlagsExt) & kExt[i]);
    DialogChoices = (int32_t)c;
    if (X_pop_6("AIEDITOR2", 0x41, 0) < 0) return;
    c = (uint32_t)DialogChoices;
    fac_u(f, kFlags) &= 0x3DFFFFF;
    fac_u(f, kFlagsExt) &= 0xFFFFFFA0;
    for (int32_t i = 0; i < 7; i++)
        if (c & (1u << i)) fac_u(f, kFlags) |= kMain[i];
    for (int32_t i = 0; i < 6; i++)
        if (c & (0x80u << i)) fac_u(f, kFlagsExt) |= kExt[i];
    std::fprintf(stderr, "editor: faction %d strategy flags %08X ext %08X\n", f, fac_u(f, kFlags), fac_u(f, kFlagsExt));
}

// Console_editor_reset_faction (0x4DBB40): RESETALLFAC: every faction's social engineering (the
// allocation 0 / 5, the choices and the pending ones), atrocities, integrity and tectonic counts
// cleared; the bases' governor (flag 0x80000000) and queue count (+0x4C), the units' orders and
// automation bits.
void GAMEAPI Console_editor_reset_faction() {
    if (!X_pop("RESETALLFAC", 0)) return;
    for (int32_t f = 1; f < 8; f++) {
        fac_i(f, 0x328) = 5;
        fac_i(f, 0x324) = 0;
        compute_faction_modifiers(f);
        for (int32_t i = 0; i < 4; i++) {
            fac_i(f, 0x228 + 4 * i + 0x10) = 0;
            fac_i(f, 0x228 + 4 * i) = 0;
        }
        social_upkeep(f);
        fac_i(f, 0x1D8) = 0;
        fac_i(f, 0x1D4) = 0;
        TectonicDetonationCount[f] = 0;
        fac_i(f, 0x124) = 0;
        for (int32_t b = 0; b < BaseCount; b++) {
            uint8_t* p = reinterpret_cast<uint8_t*>(&Bases[b]);
            if (p[4] != f) continue;
            *reinterpret_cast<uint32_t*>(p + 0x38) &= 0x7FFFFFFF;
            *reinterpret_cast<int32_t*>(p + 0x4C) = 0;
        }
        for (int32_t v = 0; v < VehCount; v++) {
            if (Vehs[v].faction_id != f) continue;
            Vehs[v].order = 0;
            Vehs[v].state &= 0xFCFFBDFF;
        }
    }
    std::fprintf(stderr, "editor: factions reset\n");
}

// Console_editor_reset_tech (0x4DBC40): RESETALLTECH: no faction has any technology, the
// prototypes (units 64..) retired and their units killed; the bases' production reset.
void THISCALL Console_editor_reset_tech(uint8_t* self) {
    if (!X_pop("RESETALLTECH", 0)) return;
    for (int32_t f = 1; f < 8; f++) fac_i(f, 0x35C) = 0;
    std::memset(TechOwners, 0, sizeof(TechOwners));
    for (int32_t u = 0; u < 512; u++) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&Units[u]);
        p[0x2C] = 0;
        if (u >= 64) p[0x30] &= 0xFE;
    }
    // (the loop counter is the console's selected unit, +0x23BDC: left at -1)
    int32_t& sel = console_win(self).veh;
    for (sel = VehCount - 1; sel >= 0; sel--)
        if (Vehs[sel].unit_id > 0x3F) veh_kill(sel);
    for (int32_t b = 0; b < BaseCount; b++) {
        int32_t* p = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Bases[b]) + 0x4C);
        p[0] = 0;
        if (p[1] > 0x3F) p[0] = -69;
    }
    draw_map(1);
    std::fprintf(stderr, "editor: technology reset (%d units)\n", VehCount);
}

// Console_editor_diff (0x4DD6F0): EDITORDIFF, the difficulty of every faction.
void GAMEAPI Console_editor_diff() {
    Pop pop;
    auto_undo();
    Popup_start(pop.p, ScriptFileName, "EDITORDIFF", -1, nullptr, 0x40);
    for (int32_t i = 0; i < 6; i++) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, Difficulty[i]);
        BasePop_item(pop.p, StrBuffer, i);
    }
    BasePop_set_active(pop.p, DiffLevel);
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    if (r < 0) return;
    DiffLevel = r;
    for (int32_t f = 0; f < 8; f++) Factions[f].diff_level = r;
    std::fprintf(stderr, "editor: difficulty %d\n", r);
}

// Console_editor_reload (0x4E0290): EDITORWHO, then RELOADFAC (the faction file, its default the
// current one): the faction's file and search key set and read again (read_faction2).
void GAMEAPI Console_editor_reload() {
    int32_t f = who_popup("EDITORWHO", -1);
    if (f < 0) return;
    if (X_pop_ask("RELOADFAC", 0x50, MFactions[f].filename, 0, 0)) return;
    std::strcpy(MFactions[f].filename, ParseStrBuffer[0]);
    std::strcpy(MFactions[f].search_key, ParseStrBuffer[0]);
    read_faction2(f);
    compute_faction_modifiers(f);
    std::fprintf(stderr, "editor: faction %d reloaded from %s\n", f, MFactions[f].filename);
}

// Console_editor_eliminate (0x4E05E0): EDITORWHO: the faction out of the game, its bases killed.
void GAMEAPI Console_editor_eliminate() {
    auto_undo();
    int32_t f = who_popup("EDITORWHO", -1);
    if (f < 0) return;
    FactionStatus[0] &= (uint8_t)~(1 << f);
    FactionStatus[1] &= (uint8_t)~(1 << f);
    for (int32_t b = BaseCount - 1; b >= 0; b--)
        if (Bases[b].faction_id == f) base_kill(b);
    draw_map(1);
    eliminate_player(f, -2);
    std::fprintf(stderr, "editor: faction %d eliminated\n", f);
}

// Console_editor_scen_rules (0x4DC520): SCENRULES' check boxes from GameRules / GameMoreRules.
void GAMEAPI Console_editor_scen_rules() {
    auto_undo();
    static const uint32_t kRule[15] = {0x200000, 0x400000, 0x800000, 0x4000000, 0x2000000, 0x80000000, 0x40000, 0x20000,
                                       0x10000,  0,        0,        0x1000000, 0x100000,  0,          0};
    static const uint32_t kMore[15] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 0, 4, 8};
    uint32_t c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 15; i++)
        set_bit(c, 1u << i, kRule[i] ? ((uint32_t)GameRules & kRule[i]) : ((uint32_t)GameMoreRules & kMore[i]));
    DialogChoices = (int32_t)c;
    if (X_pop_6("SCENRULES", 0x41, 0) < 0) return;
    GameRules = (int32_t)((uint32_t)GameRules & 0x7808FFFF);
    GameMoreRules = (int32_t)((uint32_t)GameMoreRules & 0xFFFFFFF0);
    c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 15; i++) {
        if (!(c & (1u << i))) continue;
        if (kRule[i]) GameRules = (int32_t)((uint32_t)GameRules | kRule[i]);
        else GameMoreRules |= (int32_t)kMore[i];
    }
    std::fprintf(stderr, "editor: rules %08X more %08X\n", (uint32_t)GameRules, (uint32_t)GameMoreRules);
}

// Console_editor_scen_victory (0x4DC860): SCENVICTORY's check boxes (GameRules / GameState bits);
// with "base facilities as objectives" (GameState 0x4000000) FACOBJ, the facility.
void GAMEAPI Console_editor_scen_victory() {
    Pop pop;
    auto_undo();
    // (word, bit): 0 GameRules, 1 GameState
    static const struct {
        int32_t w;
        uint32_t bit;
    } kBit[13] = {{0, 0x8000000},  {0, 0x10000000}, {1, 0x40000}, {0, 0x40000000}, {0, 0x20000000},
                  {1, 0x2000000},  {1, 0x4000000},  {1, 0x8000},  {1, 0x80000000}, {1, 0x20000000},
                  {1, 0x40000000}, {0, 0x80000},    {1, 0x80000}};
    auto word = [](int32_t w) -> uint32_t& {
        return *reinterpret_cast<uint32_t*>(w ? reinterpret_cast<void*>(&GameState)
                                              : reinterpret_cast<void*>(&GameRules));
    };
    uint32_t c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 13; i++) set_bit(c, 1u << i, word(kBit[i].w) & kBit[i].bit);
    DialogChoices = (int32_t)c;
    if (X_pop_6("SCENVICTORY", 0x41, 0) < 0) return;
    word(0) &= 0x87F7FFFF;
    word(1) &= 0x19F37FFF;
    c = (uint32_t)DialogChoices;
    for (int32_t i = 0; i < 13; i++)
        if (c & (1u << i)) word(kBit[i].w) |= kBit[i].bit;
    if (GameState & STATE_SCN_VICT_BASE_FACIL_COUNT_OBJ) {
        Popup_start(pop.p, ScriptFileName, "FACOBJ", -1, nullptr, 0);
        for (int32_t i = 1; i < 0x41; i++) BasePop_item(pop.p, Facility[i].name, i);
        BasePop_set_active(pop.p, ScnVictFacilityObj);
        ScnVictFacilityObj = BasePop_exec_3(pop.p, 0, 0);
    }
    std::fprintf(stderr, "editor: victory rules %08X state %08X facility %d\n", (uint32_t)GameRules,
                 (uint32_t)GameState, ScnVictFacilityObj);
}

// Console_editor_scen_param (0x4DCCC0): SCENPARAM's fields ($STR0..6: the objectives for victory and
// sudden death, the points, the years, the sunspots' turns).
void GAMEAPI Console_editor_scen_param() {
    Pop pop;
    auto_undo();
    int32_t* const kVal[7] = {&ObjectiveReqVictory,    &ObjectivesSuddenDeathVictory, &ObjectiveAchievePts,
                              &VictoryAchieveBonusPts, &StartingMissionYear,          &EndingMissionYear,
                              &SunspotDuration};
    for (int32_t i = 0; i < 7; i++) {
        char num[16];
        std::snprintf(num, sizeof(num), "%d", *kVal[i]);
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, num);
        parse_says(i, StrBuffer, -1, -1);
    }
    Popup_start(pop.p, ScriptFileName, "SCENPARAM", -1, nullptr, 0x44);
    if (BasePop_exec_3(pop.p, 0, 0) < 0) return;
    for (int32_t i = 0; i < 7; i++) *kVal[i] = std::atoi(ParseStrBuffer[i]);
    std::fprintf(stderr, "editor: parameters %d %d %d %d %d %d %d\n", ObjectiveReqVictory, ObjectivesSuddenDeathVictory,
                 ObjectiveAchievePts, VictoryAchieveBonusPts, StartingMissionYear, EndingMissionYear, SunspotDuration);
}

// Console_editor_tech (0x4DFAD0): EDITORTECH, the technologies of the faction (0x688358: the
// viewer at first) sorted, each marked known / available / not; a choice gives or takes it; the
// Player button picks the faction (EDITORWHO). Cancel ends.
GLOBAL(int32_t, EditorTechFaction, 0x688358);
GLOBAL(int32_t, EditorTechLast, 0x68835C);

void GAMEAPI Console_editor_tech() {
    auto_undo();
    if (EditorTechFaction < 0) EditorTechFaction = CurrentPlayerFaction;
    for (;;) {
        Pop pop;
        const int32_t f = EditorTechFaction;
        PopupDialogState = 6;
        PluralDefault = MFactions[f].is_noun_plural;
        GenderDefault = MFactions[f].noun_gender;
        parse_says(0, MFactions[f].noun_faction, -1, -1);
        Popup_start(pop.p, ScriptFileName, "EDITORTECH", -1, nullptr, 0x42);
        BasePop_set_cancel_text(pop.p, label_get(0x438 / 4));
        BasePop_set_ok_text(pop.p, label_get(0xFC0 / 4));
        for (int32_t t = 0; t < 89; t++) {
            if (Tech[t].preq_tech1 < -1 || Tech[t].preq_tech2 < -1) continue;
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, t, 0);
            std::strcat(StrBuffer, "|");
            int32_t lab = has_tech(t, f) ? 0x3D8 : (tech_avail(t, f) ? 0x3DC : 0x3E0);
            std::strcat(StrBuffer, label_get(lab / 4));
            BasePop_item(pop.p, StrBuffer, t);
        }
        BasePop_sort_items(pop.p);
        if (EditorTechLast >= 0) BasePop_set_active(pop.p, EditorTechLast);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        PopupDialogState = 0;
        if (r < 0) {
            see_map_check();
            return;
        }
        if (BasePop_button_pressed(pop.p) == 1) {
            int32_t nf = -1;
            {
                Pop who;
                PopupDialogState = 6;
                Popup_start(who.p, ScriptFileName, "EDITORWHO", -1, nullptr, 0x40);
                for (int32_t i = 1; i < 8; i++) {
                    PluralDefault = MFactions[i].is_noun_plural;
                    GenderDefault = MFactions[i].noun_gender;
                    BasePop_item(who.p, MFactions[i].noun_faction, i);
                }
                if (f >= 0) BasePop_set_active(who.p, f);
                nf = BasePop_exec_3(who.p, 0, 0);
                PopupDialogState = 0;
            }
            if (nf >= 1) EditorTechFaction = nf;
            continue;
        }
        if (has_tech(r, f)) {
            int32_t off = 0, mask = 0;
            bitmask(r, &off, &mask);
            TechOwners[r] &= (uint8_t)~(1 << f);
        } else {
            tech_achieved(f, r, 0, 0);
        }
        EditorTechLast = r;
        fac_i(f, 0x35C) = 0;
        for (int32_t t = 0; t < 89; t++)
            if (has_tech(t, f)) fac_i(f, 0x35C) += 2;
        std::fprintf(stderr, "editor: faction %d tech %d %s (ranking %d)\n", f, r, has_tech(r, f) ? "given" : "taken",
                     fac_i(f, 0x35C));
        GraphicWin_redraw(&BaseWin);
    }
}

// Console_on_key_click's ctrl+shift+F2: every technology the viewer lacks given (the bits set, the
// ranking counted); with none missing, all taken away.
void editor_all_techs() {
    const int32_t f = CurrentPlayerFaction;
    int32_t n = 0;
    for (int32_t t = 0; t < 88; t++) {
        if (has_tech(t, f) || Tech[t].preq_tech1 < -1 || Tech[t].preq_tech2 < -1) continue;
        n++;
        int32_t off = 0, mask = 0;
        bitmask(t, &off, &mask);
        TechOwners[t] |= (uint8_t)(1 << f);
        fac_i(f, 0x35C) += 2;
    }
    if (!n) {
        fac_i(f, 0x35C) = 1;
        for (int32_t t = 0; t < 0x59; t++) TechOwners[t] &= (uint8_t)~(1 << f);
    }
    std::fprintf(stderr, "editor: all techs %s (%d)\n", n ? "given" : "taken", n);
}

// Console_editor_view (0x4DF4F0): EDITORWHO with the living factions ("formal (noun)") and ALL
// (99): the omniscient view (EDITORCONTROLALL: every faction human for scenario building), or a
// faction's view (EDITORCONTROL: take its control).
void GAMEAPI Console_editor_view() {
    Pop pop;
    PopupDialogState = 6;
    Popup_start(pop.p, ScriptFileName, "EDITORWHO", -1, nullptr, 0x40);
    for (int32_t f = 1; f < 8; f++) {
        if (!(FactionStatus[1] & (1 << f))) continue;
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, MFactions[f].formal_name_faction);
        std::strcat(StrBuffer, "(");
        PluralDefault = MFactions[f].is_noun_plural;
        GenderDefault = MFactions[f].noun_gender;
        std::strcat(StrBuffer, MFactions[f].noun_faction);
        std::strcat(StrBuffer, ")");
        BasePop_item(pop.p, StrBuffer, f);
        if (f == CurrentPlayerFaction) BasePop_set_active(pop.p, f);
    }
    BasePop_item(pop.p, GameLanguage == 1 ? "TOUS" : (GameLanguage == 2 ? "ALLE" : "ALL"), 99);
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    if (r < 0) return;
    if (r == 99) {
        PopupDialogState = 6;
        if (X_pop("EDITORCONTROLALL", 0)) FactionStatus[0] = FactionStatus[1] & 0xFE;
        GameState |= STATE_OMNISCIENT_VIEW;
        PopupDialogState = 0;
        draw_map(1);
        desktop_update();
        std::fprintf(stderr, "editor: view all (humans %02X)\n", FactionStatus[0]);
        return;
    }
    GameState &= ~STATE_OMNISCIENT_VIEW;
    desktop_update();
    ui_editor_panel_show((GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_SCENARIO_EDITOR));
    if (!(FactionStatus[0] & (1 << r))) {
        PluralDefault = MFactions[r].is_noun_plural;
        GenderDefault = MFactions[r].noun_gender;
        parse_says(0, MFactions[r].noun_faction, -1, -1);
        PopupDialogState = 6;
        FactionStatus[0] = X_pop("EDITORCONTROL", 0) == 0 ? (uint8_t)(1 << r) : 0;
        PopupDialogState = 0;
    }
    CurrentPlayerFaction = r;
    if (base_find_faction(cur_x(), cur_y(), r) >= 0)
        Console_focus(&MainConsole, cur_x(), cur_y(), CurrentPlayerFaction);
    Console_set_view(&MainConsole, 1);
    draw_map(1);
    std::fprintf(stderr, "editor: view faction %d (humans %02X)\n", r, FactionStatus[0]);
}

// Console_editor_natural (0x4E14D0): NATPLACE: a natural landmark generated at the cursor
// (world_crater ..); NATUNPLACE: the landmark's tiles around the cursor cleared (its bit, the tile
// codes, its other bits; its name removed), the fresh water sea's whole region.
void GAMEAPI Console_editor_natural(int32_t place) {
    Pop pop;
    auto_undo();
    Popup_start(pop.p, ScriptFileName, place ? "NATPLACE" : "NATUNPLACE", -1, nullptr, 0x40);
    for (int32_t i = 0; i < 16; i++) BasePop_item(pop.p, Natural[i].name, i);
    const int32_t r = BasePop_exec_3(pop.p, 0, 0);
    if (r < 0) return;
    const int32_t x = cur_x(), y = cur_y();
    auto unname = [r](int32_t tx, int32_t ty) {
        int32_t l = find_landmark(tx, ty, 1);
        if (l >= 0 && !std::strcmp(Landmarks[l].name, Natural[r].name)) kill_landmark(Landmarks[l].x, Landmarks[l].y);
    };
    if (place) {
        switch (r) {
        case 0: world_crater(x, y); break;
        case 1: world_volcano(x, y, 0); break;
        case 2: world_monsoon(x, y); break;
        case 3: world_diamond(x, y); break;
        case 4: world_sargasso(x, y); break;
        case 5: world_ruin(x, y); break;
        case 6: world_dune(x, y); break;
        case 7: world_fresh(x, y); break;
        case 8: world_mesa(x, y); break;
        case 9: world_canyon(x, y); break;
        case 10: world_geothermal(x, y); break;
        case 11: world_ridge(x, y); break;
        case 12: world_borehole(x, y); break;
        case 13: world_temple(x, y); break;
        case 14: world_unity(x, y); break;
        case 15: world_fossil(x, y); break;
        default: break;
        }
    } else if (r == 7) {
        // The fresh water sea: the cursor's sea region.
        uint8_t* t = tile_raw(x, y);
        if (!(t[0xC] & 0x80) || (t[0] & 0xE0) >= 0x60) return;
        const uint8_t region = t[3];
        for (int32_t ty = 0; ty < MapAreaY; ty++)
            for (int32_t tx = ty & 1; tx < MapAreaX; tx += 2) {
                if (tile_raw(tx, ty)[3] != region) continue;
                bit2_set(tx, ty, 0x80, 0);
                unname(tx, ty);
            }
    } else {
        int32_t cls;
        switch (r) {
        case 0: cls = 3; break;
        case 1:
        case 0xC:
        case 0xD:
        case 0xF: cls = 1; break;
        case 2: cls = 5; break;
        case 4: cls = 4; break;
        case 3:
        case 5:
        case 6:
        case 8:
        case 9:
        case 10:
        case 0xB:
        case 0xE: cls = 2; break;
        default: cls = 0; break;
        }
        const int32_t n = TableRange[cls];
        const uint32_t bit = 1u << r;
        auto wrap = [](int32_t tx) {
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            return tx;
        };
        auto inside = [](int32_t tx, int32_t ty) { return ty >= 0 && ty < MapAreaY && tx >= 0 && tx < MapAreaX; };
        // The landmark's centre: the first tile with its bit and no tile code.
        int32_t i = 0;
        for (; i < n; i++) {
            int32_t cx = wrap(x + TableOffsetX[i]), cy = TableOffsetY[i] + y;
            if (!inside(cx, cy)) continue;
            uint32_t b = tile_bits2(cx, cy);
            if ((b & bit) && !(b & 0xFF000000)) {
                for (int32_t k = 0; k < n; k++) {
                    int32_t tx = wrap(cx + TableOffsetX[k]), ty = TableOffsetY[k] + cy;
                    if (!inside(tx, ty) || !(tile_bits2(tx, ty) & bit)) continue;
                    bit2_set(tx, ty, bit, 0);
                    code_set(tx, ty, 0);
                    if (r == 4) bit_set(tx, ty, 0x20, 0);
                    else if (r == 5) bit_set(tx, ty, 0x102020, 0);
                    else if (r == 0xC) bit_set(tx, ty, 0x1100000, 0);
                    unname(tx, ty);
                }
                break;
            }
        }
        if (i >= n) return;
    }
    world_climate();
    draw_map(1);
    std::fprintf(stderr, "editor: natural %d %s at %d,%d\n", r, place ? "placed" : "removed", x, y);
}

// Console_show_movies (0x515F70): SHOWMOVIES, the secret projects sorted; a choice plays its movie
// (amovie_project), until Cancel.
void THISCALL Console_show_movies(uint8_t*) {
    for (;;) {
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "SHOWMOVIES", -1, nullptr, 0x42);
        for (int32_t i = 0; i < 64; i++) {
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, Facility[70 + i].name);
            BasePop_item(pop.p, StrBuffer, i);
        }
        BasePop_sort_items(pop.p);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        std::fprintf(stderr, "editor: project movie %d\n", r);
        amovie_project(r);
    }
}
