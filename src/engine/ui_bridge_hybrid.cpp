// Hybrid-build implementations of the engine-side loops over the original UI's map windows.
// Native builds use src/headless/ui_headless.cpp.
#ifdef SMACX_HYBRID
#include "engine/ui_bridge.h"

// Field of the original MapWin object: non-zero while the window is in use.
static int32_t mapwin_active(uint8_t* win) { return *reinterpret_cast<int32_t*>(win + 0x1DD74); }

void ui_mapwins_clear_terrain() {
    for (int i = 0; i < 8; i++)
        if (mapwin_active(MapWins[i])) MapWin_clear_terrain(MapWins[i]);
}

void ui_mapwins_clear_alt(int32_t x, int32_t y) {
    for (int i = 0; i < 8; i++) {
        if (mapwin_active(MapWins[i])) {
            *reinterpret_cast<int32_t*>(MapWins[i] + 8) = 0x98967F;
            MapWin_clear_alt(MapWins[i], x, y, 1);
        }
    }
}
#endif

// ---- boot helpers ----
#ifdef SMACX_HYBRID
uint8_t* ui_console_vbase() {
    uint8_t* obj = &MainConsole;
    return obj + *reinterpret_cast<int32_t*>(*reinterpret_cast<uint8_t**>(obj) + 4);
}

void ui_delete_object(uint8_t* obj, bool with_vbase) {
    if (with_vbase) obj += *reinterpret_cast<int32_t*>(*reinterpret_cast<uint8_t**>(obj) + 4);
    typedef void(__thiscall * Dtor)(uint8_t*, int32_t);
    (*reinterpret_cast<Dtor*>(*reinterpret_cast<uint8_t**>(obj)))(obj, 1);
}

// BasePop field: index of the selected choice.
int32_t BasePop_choice(uint8_t* pop) { return *reinterpret_cast<int32_t*>(pop + 0x3100); }
#endif

// ---- save module helpers (see ui_bridge.h) ----
#ifdef SMACX_HYBRID
void ui_editor_panel_show(bool show) {
    uint8_t* win = &EditorPanelWin;
    void** vtable = *reinterpret_cast<void***>(win);
    if (show) {
        reinterpret_cast<void(__thiscall*)(uint8_t*, int32_t)>(vtable[1])(win, 0);
    } else {
        reinterpret_cast<void(__thiscall*)(uint8_t*)>(vtable[2])(win);
    }
}

// StringStruct layout: +0 vtable, +8 head node, +0xC cursor node, +0x10 count, +0x14 position,
// +0x18 flags, +0x1C source text, +0x20/+0x24 add() parameters. Nodes: +4 id, +8 data object
// (+4 text), +0xC next, +0x10 previous. Objects are deleted through their virtual base's
// scalar-deleting destructor.
namespace {
uint8_t*& ss_ptr(uint8_t* p, uint32_t off) { return *reinterpret_cast<uint8_t**>(p + off); }
int32_t& ss_int(uint8_t* p, uint32_t off) { return *reinterpret_cast<int32_t*>(p + off); }
void ss_delete(uint8_t* obj) {
    uint8_t* base = obj + *reinterpret_cast<int32_t*>(*reinterpret_cast<uint8_t**>(obj) + 4);
    typedef void(__thiscall * Dtor)(uint8_t*, int32_t);
    (**reinterpret_cast<Dtor**>(base))(base, 1);
}
}  // namespace

void ui_stringlist_clear(uint8_t* list) {
    if (ss_ptr(list, 8)) {
        for (int32_t n = 0; n < ss_int(list, 0x10); n++) {
            uint8_t* head = ss_ptr(list, 8);
            ss_ptr(list, 0xC) = ss_ptr(head, 0xC);
            uint8_t* data = ss_ptr(head, 8);
            typedef void(__thiscall * Release)(uint8_t*, uint8_t*);
            (*reinterpret_cast<Release*>(*reinterpret_cast<uint8_t**>(list) + 4))(list, data);
            if (data) ss_delete(data);
            ss_ptr(ss_ptr(list, 8), 8) = nullptr;
            if (ss_ptr(list, 8)) ss_delete(ss_ptr(list, 8));
            ss_ptr(list, 8) = ss_ptr(list, 0xC);
        }
        ss_ptr(list, 8) = nullptr;
        ss_int(list, 0x14) = 0;
        ss_int(list, 0x10) = 0;
    }
    ss_int(list, 0x14) = 0;
    ss_int(list, 0x18) = 0;
}

const char* ui_stringlist_entry(uint8_t* list, int32_t index, int32_t* id) {
    int32_t count = ss_int(list, 0x10);
    if (index <= count - 1) {
        ss_ptr(list, 0xC) = ss_ptr(list, 8);
        int32_t pos;
        if (index < 0) {
            // Never reached from game_data (index >= 0); kept as the original's seek.
            int32_t back = index < 0 ? -index : index;
            if (back > count) goto done;
            for (int32_t k = back; k > 0; k--) ss_ptr(list, 0xC) = ss_ptr(ss_ptr(list, 0xC), 0x10);
            pos = count + index;
        } else {
            for (int32_t k = index; k > 0; k--) ss_ptr(list, 0xC) = ss_ptr(ss_ptr(list, 0xC), 0xC);
            pos = index;
        }
        ss_int(list, 0x14) = pos;
    }
done:
    if (!ss_ptr(list, 8)) {
        *id = 0;
        return nullptr;
    }
    *id = ss_int(ss_ptr(list, 0xC), 4);
    return *reinterpret_cast<const char**>(ss_ptr(ss_ptr(list, 0xC), 8) + 4);
}

void ui_stringlist_add(uint8_t* list, int32_t id, char* text) {
    ss_int(list, 0x20) = 0;
    ss_int(list, 0x24) = 0;
    ss_ptr(list, 0x1C) = reinterpret_cast<uint8_t*>(text);
    reinterpret_cast<int32_t(__thiscall*)(uint8_t*, int32_t)>(0x401100)(list, id);
}
#endif

// ---- crash_landing (see ui_bridge.h): not displayed ----
#ifdef SMACX_HYBRID
#include "engine/crt.h"
void ui_crash_landing_drop(int32_t, int32_t, bool) {}
void ui_crash_landing_close() {}
void ui_first_base_name(const char* default_name) { crt_strcpy(ParseStrBuffer[0], default_name); }
#endif
