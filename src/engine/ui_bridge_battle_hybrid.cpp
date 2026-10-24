// Hybrid-build implementations of the battle module's operations on original UI objects (see
// ui_bridge.h). Native builds use src/headless/ui_headless_battle.cpp.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

namespace {
GLOBAL(uint8_t, BattleSoundObject, 0x74B5DC);  // sound object (vtable first)
typedef int32_t(__thiscall* SoundBusyFn)(uint8_t*);
typedef void(__cdecl* DoTaskFn)();
}  // namespace

void ui_battle_wait_sound() {
    uint8_t* obj = &BattleSoundObject;
    auto busy = [obj]() {
        uint8_t** vtable = *reinterpret_cast<uint8_t***>(obj);
        return reinterpret_cast<SoundBusyFn>(vtable[0x5C / 4])(obj);
    };
    while (busy()) reinterpret_cast<DoTaskFn>(0x5FC7F0)();
}
#endif
