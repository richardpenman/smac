// The diplomacy menus built on Popup objects on the original functions' stacks
// (engine/ui_bridge.h, ui_diplo_menu_start ...), shown only to a human player. Hybrid builds:
// started by the original Popup_start on a heap Popup, not shown; headless: nothing. exec returns
// what the oracle's popups return (BasePop_exec_3's no-display path: 0).
#include "engine/ui_bridge.h"

#ifdef SMACX_HYBRID
namespace {
typedef int32_t(__thiscall* PopupStartFn)(uint8_t*, const char*, const char*, int32_t, int32_t, int32_t, int32_t);
const PopupStartFn popup_start = reinterpret_cast<PopupStartFn>(0x406380);
// The script file's name, as the game keeps it for Popup_start.
const char* const kScriptFile = reinterpret_cast<const char*>(0x9B8AA8);
uint8_t* diplo_menu = nullptr;
}  // namespace
void ui_diplo_menu_start(const char* label, int32_t flags) {
    if (!diplo_menu) diplo_menu = BasePopAlloc();
    popup_start(diplo_menu, kScriptFile, label, -1, 0, flags, 0);
}
void ui_diplo_menu_close() {
    if (diplo_menu) BasePop_delete(diplo_menu);
    diplo_menu = nullptr;
}
#else
void ui_diplo_menu_start(const char*, int32_t) {}
void ui_diplo_menu_close() {}
#endif
void ui_diplo_menu_item(const char*, int32_t) {}
int32_t ui_diplo_menu_exec(int32_t) { return 0; }
