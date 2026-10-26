// The scenario / map editor (console module 0x4D9970..0x4E1FA0, MapWin_editor 0x46B510),
// reconstructed from the asm: the editor switched on and off (Console_editor_on: ctrl+K, the
// Scenario menu), the map painting (MapWin_editor: the paint chosen by Console_editor_paints /
// _terrain / _minor, the brush by Console_editor_set_brush; a ctrl+click or a drag on the map
// paints, the right button removes), the map commands (flat / round, polar caps, climate,
// rockiness, fungus, randomize, clear, generate), the unit commands (Console_editor_veh: a unit
// of the chosen faction and morale at the cursor; Console_editor_kill_veh; kill the stack),
// energy reserves, the date, the protection lock, the map save / load and undo.
//
// Console_editor_edit_veh: the unit's flags (EDITVEH's check boxes), its morale, faction and home
// base (MORALE, PLAYER, HOME; DATA, the damage / moves / lurking numbers, and native life's
// morale adjustment are not reconstructed).
// Not reconstructed: Console_editor_natural, _view, _tech, _diff, _eliminate,
// _reload, _reset_*, _diplomacy, _personality, _strategy, _scen_rules / _param / _victory,
// BaseWin_base_editor, the editor's interface pane (MainInterface_draw_edit_pane: the paint and
// brush buttons; the choices are logged).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/base.h"
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
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/topmenu.h"
#include "ui/win.h"

GLOBAL(int32_t, EditorPaint, 0x945AE0);      // DAT_00945AE0: the paint (0 altitude .. 0x1D)
GLOBAL(int32_t, EditorBrush, 0x945AE4);      // DAT_00945AE4: the brush (0 one tile .. 6)
GLOBAL(int32_t, EditorVehFaction, 0x688350); // DAT_00688350: the faction of the units created
GLOBAL(int32_t, EditorVehMorale, 0x688354);  // DAT_00688354: their morale
GLOBAL(int32_t, EditorAllUnits, 0x90DB8C);   // DAT_0090DB8C: every unit type listed
GLOBAL(int32_t, EditorLastUnit, 0x90DB90);   // DAT_0090DB90
// The paints 6..0x19 (0x691888, 0x20 bytes each): the tile bit set, the bits cleared with it.
GLOBAL(int32_t[20][8], EditorPaintBits, 0x691888);
// The terrain features' list (Terraform 0x691878.., 0x20 bytes each, 20 entries).

namespace {

constexpr uint32_t kMwEditPaint = 0x1DD8C;  // a drag paints (1 left, 2 right; 4: no redraw)
constexpr uint32_t kMwEditX = 0x1DD90, kMwEditY = 0x1DD94;

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

// The edit pane of the info pane (InfoWin mode 7, MainInterface_draw_edit_pane): logged.
void edit_pane() {
    std::fprintf(stderr, "editor paint %d brush %d\n", EditorPaint, EditorBrush);
    ui_invalidate();
}

}  // namespace

// ---- Console_editor_on (0x4D9970) ----------------------------------------------------------------

void THISCALL Console_editor_on(uint8_t* self) {
    if ((MultiplayerActive || PbemActive) && !(GameState & 0x61) && !(GameState & 0x1000)) {
        NetMsg_pop(&NetMsg, "EDITORNET", 5000, 0, nullptr);
        return;
    }
    if (!(GameState & 0x60) && !X_pop("SCENEDITOR", 0)) return;
    GameState = (GameState ^ 0x40) | 0x20;
    if (GameRules & 0x400) auto_save();
    if (!(GameState & 0x40)) {
        NetMsg_pop(&NetMsg, "EDITOROFF", 5000, 0, nullptr);
        GameState &= ~0x80;
    } else {
        NetMsg_pop(&NetMsg, "EDITORON", 5000, 0, nullptr);
        GameState |= 0x80;
    }
    pfncMainMenuUpdateVisible(nullptr, mw(self, 0x23BDC));
    set_dirty();
    draw_map(1);
    std::fprintf(stderr, "scenario editor %s (GameState %08X)\n", (GameState & 0x40) ? "on" : "off", GameState);
    Console_update_data(self, 0);
}

// ---- the paints ----------------------------------------------------------------------------------

void GAMEAPI Console_editor_paints(int32_t paint) {
    EditorPaint = paint;
    edit_pane();
}

void GAMEAPI Console_editor_set_brush() {
    PopupDialogState = 6;
    int32_t r = X_pop_9(ScriptFileName, "BRUSH", EditorBrush, nullptr, 0x40, 0);
    PopupDialogState = 0;
    if (r >= 0) EditorBrush = r;
    edit_pane();
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
    if (mw(win, kMwEditPaint)) {
        if (x == mw(win, kMwEditX) && y == mw(win, kMwEditY)) return;
        mw(win, kMwEditX) = x;
        mw(win, kMwEditY) = y;
    }
    const bool drag = mw(win, kMwEditPaint) != 0;
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
                case 0x1A: case 0x1B: case 0x1C: case 0x1D: {  // the minor features (bit2)
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
    if (drag && !(mw(win, kMwEditPaint) & 4)) {
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
    std::fprintf(stderr, "editor paint %d at %d,%d%s: change %d\n", EditorPaint, x, y, right ? " (remove)" : "", change);
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
    for (int32_t i = 0; i < MapAreaTiles; i++) *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8) &= 0xFFFFFFFC;
    uint32_t omni = GameState & 0x80;
    GameState &= ~0x80;
    world_build();
    if (omni) GameState |= 0x80;
    draw_map(1);
}

void GAMEAPI Console_editor_generate() {
    auto_undo();
    VehCount = 0;
    BaseCount = 0;
    MapWinInVehMode = 0;
    *reinterpret_cast<int32_t*>(mem::host(0x93928C)) = -1;
    for (int32_t i = 0; i < MapAreaTiles; i++) *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&MapTiles[i]) + 8) &= 0xFFFFFFFC;
    *reinterpret_cast<int32_t*>(mem::host(0x9392A8)) = 0;
    GameState |= 4;
    mw(MapWins[0], kMwZoom) = -0xE;
    pfncRedrawMap(MapWins[0], 1);
    uint32_t omni = GameState & 0x80;
    GameState |= 0x80;
    world_build();
    draw_map(1);
    if (!omni) {
        GameState &= ~0x80;
        draw_map(1);
    }
}

void GAMEAPI Console_editor_lock2() {
    if (GameState & 0x8000000) {
        GameState &= ~0x8000000;
        NetMsg_pop(&NetMsg, "PROTECTOFF", 5000, 0, nullptr);
    } else {
        GameState |= 0x8000000;
        NetMsg_pop(&NetMsg, "PROTECTON", 5000, 0, nullptr);
        if (MapWinInVehMode) Console_set_view(&MainConsole, 1);
    }
    edit_pane();
}

void GAMEAPI Console_editor_undo() { load_undo(1); draw_map(1); }
void GAMEAPI Console_editor_redo() { load_undo(0); draw_map(1); }

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
// (ui_headless_social.cpp's other functions, as the area is replaced: no registry.)
void GAMEAPI read_tamper() {}
void GAMEAPI set_tamper() {}

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
            std::fprintf(stderr, "editor: unit %d (%s) of faction %d at %d,%d\n", v, Units[r].name, EditorVehFaction, x, y);
        }
        return;
    }
}

// ---- the keys (Console_on_key_click's editor part) -----------------------------------------------

bool editor_key(uint8_t* self, int32_t key) {
    if (!(GameState & 0x40)) return false;
    int32_t vk = key & 0x7FFFF;
    switch (vk) {
    case 0x79:  // F10: the omniscient view
        GameState ^= 0x80;
        set_dirty();
        draw_map(1);
        return true;
    case 0x10070: Console_editor_veh(); return true;
    case 0x10071: tech_advance(CurrentPlayerFaction); return true;
    case 0x10073: Console_editor_energy(); return true;
    case 0x10074: Console_editor_date(); return true;
    case 0x10076: show_replay(); return true;
    case 0x10079: Console_editor_rules(self); return true;
    case 0x30070: Console_editor_edit_veh(self, -1); return true;
    case 0x20074: Console_editor_save(); return true;
    case 0x20075: Console_editor_kill_vehicles(); return true;
    case 0x20076: save_game(1); return true;
    case 0x30074: Console_editor_load(); return true;
    case 0x30076: Console_load_proc(self, 1, 0); return true;
    case 0x30079: Console_editor_lock2(); return true;
    case 0x40008: Console_editor_undo(); return true;
    default: break;
    }
    if (key & 0x30000) return false;
    switch (vk) {
    case '0': Console_editor_fast(); return true;
    case '1': case '2': case '3': case '4': case '5': Console_editor_paints(vk - '1'); return true;
    case '6': Console_editor_terrain(); return true;
    case '7': Console_editor_set_brush(); return true;
    case '8': Console_editor_climate(); return true;
    case '9': Console_editor_generate(); return true;
    default: return false;
    }
}

// Console_editor_edit_veh (0x4DDA50): the unit (-1: the selected one, else picked: PICKEDIT).
void THISCALL Console_editor_edit_veh(uint8_t*, int32_t veh_id) {
    auto_undo();
    if (veh_id < 0) {
        if (MapWinInVehMode) {
            veh_id = *reinterpret_cast<int32_t*>(mem::host(0x93928C));
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
    std::strcat(StrBuffer, " (");
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
    if (DialogToggle == 1) {  // MORALE: the morale shown is morale_veh's, stored less its bonuses
        Pop pop;
        int32_t shown = morale_veh(veh_id, 0, 0);
        BasePop_start(pop.p, ScriptFileName, "EDITMORALE", shown, nullptr, 0, nullptr);
        for (int32_t i = 0; i < 7; i++) BasePop_item(pop.p, Strings_get(&StringTable, Morale[i].name), i);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        int32_t m = r - (shown - v.morale);
        v.morale = (uint8_t)std::max(0, std::min(6, m));
        Console_update_data(&MainConsole, 0);
    } else if (DialogToggle == 3) {  // PLAYER
        Pop pop;
        BasePop_start(pop.p, ScriptFileName, "EDITVEHWHO", v.faction_id, nullptr, 0, nullptr);
        for (int32_t f = 0; f < 8; f++) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            BasePop_item(pop.p, MFactions[f].noun_faction, f);
        }
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        v.faction_id = (uint8_t)r;
        draw_tile(v.x, v.y, 1);
        Console_update_data(&MainConsole, 0);
    } else if (DialogToggle == 4) {  // HOME: none (9999) or a base of the unit's faction
        Pop pop;
        BasePop_start(pop.p, ScriptFileName, "EDITVEHHOME", v.home_base_id, nullptr, 0, nullptr);
        BasePop_item(pop.p, label_get(251), 9999);
        for (int32_t b = 0; b < BaseCount; b++)
            if ((uint8_t)Bases[b].faction_id == v.faction_id) BasePop_item(pop.p, Bases[b].name, b);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r < 0) return;
        v.home_base_id = (int16_t)(r == 9999 ? -1 : r);
        draw_tile(v.x, v.y, 1);
        Console_update_data(&MainConsole, 0);
    } else if (DialogToggle == 2) {
        console_not_reconstructed("Console_editor_edit_veh: DATA (EDITVEH2)");
    }
}
