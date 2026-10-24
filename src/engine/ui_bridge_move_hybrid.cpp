// Hybrid builds: UI-bridge helpers of the path/move/goals modules that are not original functions.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

#include <windows.h>

int32_t ui_supply_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_artifact_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_disembark_menu(int32_t) { return 0; }
bool ui_ctrl_pressed() {
    typedef short(__stdcall * GetKey)(int);
    static GetKey fn = nullptr;
    if (!fn) fn = reinterpret_cast<GetKey>(GetProcAddress(GetModuleHandleA("user32.dll"), "GetAsyncKeyState"));
    return ((uint16_t)fn(0x11) >> 8) != 0;
}
#endif
