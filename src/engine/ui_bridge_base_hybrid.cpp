// Hybrid-build implementations of the base module's inline operations on original UI objects
// (see ui_bridge.h). Native builds use src/headless/ui_headless_base.cpp.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

#include <windows.h>

namespace {
uint8_t*& ptr_at(uint8_t* p, uint32_t off) { return *reinterpret_cast<uint8_t**>(p + off); }
int32_t& int_at(uint8_t* p, uint32_t off) { return *reinterpret_cast<int32_t*>(p + off); }
// Scalar-deleting destructor through the object's virtual base.
void delete_vbase(uint8_t* obj) {
    uint8_t* base = obj + *reinterpret_cast<int32_t*>(*reinterpret_cast<uint8_t**>(obj) + 4);
    typedef void(__thiscall * Dtor)(uint8_t*, int32_t);
    (**reinterpret_cast<Dtor**>(base))(base, 1);
}
}  // namespace

// The ListBox at 0x7F685C keeps its StringStruct at a virtual-base offset; node layout as in
// ui_bridge_hybrid.cpp (+4 id, +8 data, +0xC next, +0x10 previous).
void ui_messages_base_killed(int32_t base_id) {
    uint8_t* box = reinterpret_cast<uint8_t*>(0x7F685C);
    uint8_t* list = box + 0xBC + int_at(ptr_at(box, 0), 8);  // 0x7F6918 + vbase offset
    int32_t key = base_id | 0x40000000;
    if (ptr_at(list, 8)) {
        for (int32_t i = 0; i < int_at(list, 0x10); i++) {
            uint8_t* node = ptr_at(list, 0xC);
            int32_t id = int_at(node, 4);
            if (id == key) {
                ptr_at(ptr_at(node, 0xC), 0x10) = ptr_at(node, 0x10);
                node = ptr_at(list, 0xC);
                ptr_at(ptr_at(node, 0x10), 0xC) = ptr_at(node, 0xC);
                uint8_t* cur = ptr_at(list, 0xC);
                if (cur == ptr_at(list, 8)) ptr_at(list, 8) = ptr_at(cur, 0xC);
                ptr_at(list, 0xC) = ptr_at(cur, 0xC);
                uint8_t* data = ptr_at(cur, 8);
                typedef void(__thiscall * Release)(uint8_t*, uint8_t*);
                (*reinterpret_cast<Release*>(ptr_at(list, 0) + 4))(list, data);
                if (data) delete_vbase(data);
                ptr_at(cur, 8) = nullptr;
                if (cur) delete_vbase(cur);
                int_at(list, 0x10)--;
                i--;
            } else {
                if (id > key) int_at(node, 4) = id - 1;
                ptr_at(list, 0xC) = ptr_at(ptr_at(list, 0xC), 0xC);
            }
        }
        if (int_at(list, 0x10) == 0) ptr_at(list, 8) = nullptr;
        int_at(list, 0x14) = int_at(list, 0x10) - 1;
    }
    reinterpret_cast<void(__thiscall*)(uint8_t*)>(0x60C9B0)(box);  // ListBox_update_changes
}

bool ui_shift_pressed() {
    typedef short(__stdcall * GetKey)(int);
    static GetKey fn = nullptr;
    if (!fn) {
        // user32 is loaded by the game; the import address of GetAsyncKeyState is not needed.
        HMODULE user32 = GetModuleHandleA("user32.dll");
        fn = reinterpret_cast<GetKey>(GetProcAddress(user32, "GetAsyncKeyState"));
    }
    return ((uint16_t)fn(0x10) >> 8) != 0;
}
// base_production's popup object lives on the original function's stack; here it is a heap
// Popup (BasePopAlloc) driven through the same calls (its texts land in TextBuffer, engine-visible
// state; the picture lookup also sets the file search state).
namespace {
typedef int32_t(__thiscall* PopupStartFn)(uint8_t*, const char*, const char*, int32_t, int32_t, int32_t, int32_t);
typedef int32_t(__thiscall* PopInitFn)(uint8_t*, int32_t, int32_t);
typedef void(__thiscall* PopWidthFn)(uint8_t*, int32_t);
typedef int32_t(__thiscall* PopStringFn)(uint8_t*, const char*);
typedef int32_t(__thiscall* PopExecFn)(uint8_t*, int32_t, int32_t);
const PopupStartFn popup_start = reinterpret_cast<PopupStartFn>(0x406380);
const PopInitFn basepop_init = reinterpret_cast<PopInitFn>(0x6015B0);
const PopWidthFn basepop_set_width = reinterpret_cast<PopWidthFn>(0x601B20);
const PopStringFn basepop_string = reinterpret_cast<PopStringFn>(0x601980);
const PopExecFn basepop_exec_3 = reinterpret_cast<PopExecFn>(0x602600);
typedef int32_t(__cdecl* PcxDimsFn)(const char*, int32_t*, int32_t*);
typedef int32_t(__thiscall* SpriteInitFn)(uint8_t*, const char*, int32_t, int32_t);
const PcxDimsFn buffer_get_pcx_dimensions = reinterpret_cast<PcxDimsFn>(0x5E2480);
const SpriteInitFn sprite_init = reinterpret_cast<SpriteInitFn>(0x5E3890);
uint8_t* production_pop = nullptr;
}  // namespace
void ui_production_popup_open(int32_t width) {
    if (production_pop) BasePop_delete(production_pop);
    production_pop = BasePopAlloc();
    basepop_init(production_pop, 0, 0);
    basepop_set_width(production_pop, width);
}
void ui_production_popup_blurb(bool is_project) {
    uint8_t* pop = production_pop;
    popup_start(pop, "BLURBS", StrBuffer, -1, 0, 0x80, 0);
    if (!is_project || ScreenWidth != 800) basepop_string(pop, "^");
    basepop_string(pop, "^------");
    basepop_string(pop, "^");
    if (is_project) popup_start(pop, reinterpret_cast<const char*>(0x9B8AA8), "PRODUCE2", -1, 0, 0x80, 0);
}
void ui_production_popup_exec() {
    uint8_t* pop = production_pop;
    int32_t w, h;
    if (!buffer_get_pcx_dimensions("secproj_sm.pcx", &w, &h)) {
        uint8_t* sprite = pop + 0x2118;  // the Popup's picture Sprite member
        if (!sprite_init(sprite, "secproj_sm.pcx", w, h)) *reinterpret_cast<uint8_t**>(pop + 0x2144) = sprite;
    }
    basepop_exec_3(pop, 0, 0);
}
#endif
