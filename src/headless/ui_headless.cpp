// Headless implementations of the UI-layer functions declared in engine/ui_bridge.h (the areas
// that no native UI replaces yet; see ui_headless_mapview.cpp for how areas are replaced).
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "engine/prefs.h"

// ---- map windows: nothing is drawn ----
void GAMEAPI mapwin_terrain_fixup() {}
void THISCALL WorldWin_clear_terrain(uint8_t*) {}

// ---- Console dialogs ----
// No human players headless: the upgrade dialog is never reached; cancelled.
int32_t THISCALL Console_upgrade_human(uint8_t*, int32_t) { return 0; }

// ---- boot: no windows, sprites, fonts or timers ----
void GAMEAPI textcolor_init() {}
void GAMEAPI load_fixed_sprites() {}
void GAMEAPI load_sprites() {}
// Only the engine-visible part: canvas size and the unit animation preference.
void GAMEAPI config_popups() {
    CanvasWidth = ScreenWidth;
    CanvasHeight = ScreenHeight;
    UnitAnimMode = prefs_get_2("FastUnitAnim", 0, 0);
    prefs_put_2("FastUnitAnim", (uint32_t)UnitAnimMode, 0);
    if (!UnitAnimMode && prefs_get_2("SmoothUnitAnim", 0, 0)) UnitAnimMode = -1;
    prefs_put_2("SmoothUnitAnim", UnitAnimMode < 0, 0);
}
// The window block (0x22480 bytes, system_init) is zeroed, so the view fields saved by game_io
// are deterministic (all zero).
uint8_t* THISCALL MapWin_ctor(uint8_t* self, int32_t) {
    std::memset(self, 0, 0x22480);
    return self;
}
int32_t THISCALL MainInterface_init(uint8_t*, int32_t) { return 0; }
int32_t THISCALL BattleWin_init(uint8_t*) { return 0; }
int32_t THISCALL Console_init(uint8_t*, int32_t) { return 0; }
void THISCALL Console_close(uint8_t*) {}
int32_t THISCALL MultiDebug_init(uint8_t*, uint8_t*, uint8_t*, uint8_t*) { return 0; }
void THISCALL MultiDebug_close(uint8_t*) {}
int32_t THISCALL MessageWin_init(uint8_t*) { return 0; }
int32_t THISCALL Datalink_init(uint8_t*) { return 0; }
void GAMEAPI bung_momma() {}
void GAMEAPI release_sound() {}
void THISCALL DesignWin_close(uint8_t*) {}
void THISCALL BaseWin_close(uint8_t*) {}
void GAMEAPI mapwin_system_shutdown() {}
void THISCALL StatusWin_close(uint8_t*) {}
void THISCALL GraphicWin_close(uint8_t*) {}
void THISCALL Win_hide(uint8_t*) {}
int32_t GAMEAPI desktop_init(int32_t) { return 0; }
void GAMEAPI start_timers() {}
void GAMEAPI stop_timers() {}
uint8_t* ui_console_vbase() { return &MainConsole; }
// Headless UI objects are plain heap blocks (see headless_basepop_alloc, MapWin_ctor).
void ui_delete_object(uint8_t* obj, bool) { std::free(obj); }

// ---- popups ----
// Popup objects: zeroed blocks large enough for the fields the engine reads.
uint8_t* GAMEAPI headless_basepop_alloc() { return (uint8_t*)std::calloc(1, 0x3200); }
// Not displayed; returns 0 with choice 0, like the oracle's forced no-display path.
int32_t THISCALL BasePop_start(uint8_t*, const char*, const char*, int32_t, int32_t, int32_t, int32_t) { return 0; }
int32_t GAMEAPI BasePop_set_def_ok_text(const char*) { return 0; }
int32_t GAMEAPI BasePop_set_def_cancel_text(const char*) { return 0; }
int32_t BasePop_choice(uint8_t* pop) { return *reinterpret_cast<int32_t*>(pop + 0x3100); }

// ---- interface buttons, faction art ----
void THISCALL BaseButton_set_bubble_text(uint8_t*, const char*) {}

// ---- vehicles, spotting: no sound, messages or windows ----
void THISCALL FX_play(uint8_t*, int32_t) {}
void THISCALL FX_fade(uint8_t*, int32_t) {}
void THISCALL InfoWin_on_redraw(uint8_t*) {}
void THISCALL GraphicWin_redraw(uint8_t*) {}
// The base window is never visible headless: nothing to recompute.
void THISCALL BaseWin_check_base(uint8_t*, int32_t) {}
void THISCALL BaseWin_check_loc(uint8_t*, int32_t, int32_t, int32_t) {}
// ---- save module: map windows, editor panel, PBEM message lists ----
void THISCALL MapWin_close(uint8_t*) {}
void ui_editor_panel_show(bool) {}
// List entries live outside the memory image (they are heap objects in the original); the
// count (+0x10) and position (+0x14) fields of the image objects are kept as the original does.
static std::map<const uint8_t*, std::vector<std::pair<int32_t, std::string>>>& headless_stringlists() {
    static std::map<const uint8_t*, std::vector<std::pair<int32_t, std::string>>> lists;
    return lists;
}
void ui_stringlist_clear(uint8_t* list) {
    headless_stringlists().erase(list);
    *reinterpret_cast<int32_t*>(list + 0x10) = 0;
    *reinterpret_cast<int32_t*>(list + 0x14) = 0;
    *reinterpret_cast<int32_t*>(list + 0x18) = 0;
}
const char* ui_stringlist_entry(uint8_t* list, int32_t index, int32_t* id) {
    auto& entries = headless_stringlists()[list];
    if (index >= 0 && index < (int32_t)entries.size()) *reinterpret_cast<int32_t*>(list + 0x14) = index;
    if (entries.empty()) {
        *id = 0;
        return nullptr;
    }
    const auto& e = entries[(size_t)*reinterpret_cast<int32_t*>(list + 0x14) % entries.size()];
    *id = e.first;
    return e.second.c_str();
}
void ui_stringlist_add(uint8_t* list, int32_t id, char* text) {
    auto& entries = headless_stringlists()[list];
    entries.emplace_back(id, text);
    *reinterpret_cast<int32_t*>(list + 0x10) = (int32_t)entries.size();
    *reinterpret_cast<int32_t*>(list + 0x14) = (int32_t)entries.size() - 1;
}
// ---- file dialogs: nothing is shown; every dialog is cancelled (an unattended observer neither
// saves nor loads through the menus) ----
void THISCALL FileDialog_ctor(uint8_t* self) { std::memset(self, 0, kFileDialogSize); }
void THISCALL FileDialog_dtor(uint8_t*) {}
char* THISCALL FileDialog_save(uint8_t*) { return nullptr; }
char* THISCALL FileDialog_open(uint8_t*) { return nullptr; }
int32_t THISCALL FileDialog_add_filter(uint8_t*, const char*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_ext(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_name(uint8_t*, const char*) { return 0; }
void THISCALL FileBox_ctor(uint8_t* self) { std::memset(self, 0, kFileBoxSize); }
void THISCALL FileBox_init(uint8_t*) {}
void THISCALL FileBox_close(uint8_t*) {}
char* THISCALL FileBox_save(uint8_t*) { return nullptr; }
char* THISCALL FileBox_load(uint8_t*) { return nullptr; }
int32_t THISCALL FileList_set_path(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileList_add_ext(uint8_t*, const char*) { return 0; }
void THISCALL FileList_close(uint8_t*) {}
void ui_filebox_destroy(uint8_t*) {}
// No window is ever visible.
int32_t THISCALL Win_is_visible(uint8_t*) { return 0; }
void GAMEAPI do_all_draws() {}
void THISCALL Lock_clear(uint8_t*) {}
void THISCALL AlphaNet_close(uint8_t*) {}
#endif
