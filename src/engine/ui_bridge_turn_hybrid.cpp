// Hybrid-build implementations of the turn module's operations on original UI objects and the
// registry (see ui_bridge.h). Native builds use src/headless/ui_headless_turn.cpp.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

#include <windows.h>

#include "engine/crt.h"

bool ui_directplay_registered() {
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\DirectPlay\\Applications\\Sid Meier's Alpha Centauri",
                      0, 0, &key) != 0)
        return false;
    bool done = false;
    BYTE value[12];
    DWORD type, size = 10;
    if (RegQueryValueExA(key, "Complete", nullptr, &type, value, &size) == 0 &&
        crt_stricmp(reinterpret_cast<char*>(value), "Yes") == 0)
        done = true;
    RegCloseKey(key);
    return done;
}

void ui_main_mapwin_set_options(uint32_t bits) { *reinterpret_cast<uint32_t*>(MapWins[0] + 0x1DD70) |= bits; }

// Hotseat/PBEM screens and the human council menu are not shown in hybrid runs (observer games).
int32_t ui_hotseat_switch(int32_t) { return 0; }
void ui_hotseat_show(int32_t) {}
void ui_council_menu_open(int32_t) {}
void ui_council_menu_add(const char*, int32_t) {}
int32_t ui_council_menu_exec() { return -1; }
void ui_council_menu_close() {}
#endif
