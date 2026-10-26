// Native parts of the base window shared by basewin*.cpp (not original functions).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/base.h"
#include "ui/basewin.h"
#include "ui/popup.h"
#include "engine/log.h"
#include "engine/net.h"

// NetDaemon_lock_base / NetDaemon_unlock_base (0x530D50 / 0x530E50, methods of NetState): their
// single player paths (log, then 0 / nothing); the multiplayer paths are the engine's net module's.
inline int32_t bw_lock_base(int32_t base_id, int32_t a2 = 0) {
    log_say_2("LockBase", ControlUpkeepA, ControlTurnC, CurrentBaseID);
    if (!MultiplayerActive) return 0;
    return NetDaemon_lock_base(base_id, a2, -1, -1);
}
inline void bw_unlock_base(int32_t base_id) {
    log_say_2("UnlockBase", NetSessionState[0x1F], base_id, 0);  // 0x93E93C
    if (MultiplayerActive) NetDaemon_unlock_base(base_id);
}

// Spot list (0x0C bytes: entries, capacity, count; entries 0x18 bytes: rect, index, type).
struct Spot {
    UiRect r;
    int32_t index;  // +0x14 (Spot_add's first argument)
    int32_t type;   // +0x10
};
struct Spots {
    std::vector<Spot> list;
    int32_t capacity = 0;
};
void spot_init(Spots* s, int32_t capacity);
int32_t spot_add(Spots* s, int32_t index, int32_t type, int32_t x, int32_t y, int32_t w, int32_t h);
int32_t spot_check(Spots* s, int32_t x, int32_t y, int32_t* index, int32_t* type);
void spot_kill_type(Spots* s, int32_t type);

// BaseButton / FlatButton (0xB4C bytes): a child window with its own canvas.
struct BaseButton {
    bool inited = false, shown = false;
    int32_t id = 0;            // +0xA78
    UiRect r{};                // position in the parent's canvas
    int32_t parent = 0;        // 0 the base window, 1 the main interface
    std::string name;          // +0xA7C
    bool has_name = false;
    int32_t set_state = 0;     // +0xA18 (BaseButton_set)
    int32_t hover = 0;         // +0xA74
    int32_t pressed = 0;       // +0xA9C
    int32_t forced = -1;       // +0xAB8
    int32_t text_color[3] = {-1, -1, -1};  // +0xABC per state
    BoxDef* box[3] = {};       // +0xAC8
    Sprite* sprite[3] = {};    // +0xAD4: a picture centred
    Sprite* left[3] = {};      // +0xAE0
    Sprite* right[3] = {};     // +0xAEC
    Sprite* top[3] = {};       // +0xAF8
    Sprite* bottom[3] = {};    // +0xB04
    Sprite* tl[3] = {};        // +0xB10
    Sprite* tr[3] = {};        // +0xB1C
    Sprite* bl[3] = {};        // +0xB28
    Sprite* br[3] = {};        // +0xB34
    Font* font = nullptr;
    int32_t color = 0, shadow = -1;
    Buffer canvas;
};

struct BaseWinNative {
    bool ready = false, mi_ready = false, visible = false;
    int32_t x = 0, y = 0;            // window position on the screen
    Buffer canvas;                   // +0x444
    std::vector<uint8_t> back;       // the canvas after BaseWin_init (frame and boxes)
    Buffer mi_canvas;                // MainInfc + 0x444
    std::vector<uint8_t> mi_back;    // console art
    Buffer mask;                     // +0x40ECC
    Spots spots_iface, spots_win;    // +0x40EB4, +0x40EC0
    BaseButton buttons[22];          // +0xD7C0
    BaseButton staple2;              // +0xBE80
    BaseButton* pressed_button = nullptr;
    int32_t down_parent = 0;
};
extern BaseWinNative g_bw;

// Multiplayer input state (0x93E8C8: input of another player pending; 0x93E8F0: factions that
// finished their upkeep).
GLOBAL(int32_t, NetInputPending, 0x93E8C8);
GLOBAL(int32_t, NetUpkeepDoneMask, 0x93E8F0);

// helpers (basewin.cpp)
void sprite_copy1(Sprite* s, Buffer* dest, int32_t key, int32_t x, int32_t y, int32_t ds, int32_t ss);
bool in_box(int32_t x, int32_t y, const UiRect& r);
const char* label_upper(uint32_t off);
void bw_set_base();
bool bw_own_base();
void bw_redraw();
// The common tail of BaseWin_exit / BaseWin_base_killed / BaseWin_garrison_click: closes the
// window and recentres the view on the selected unit when it is not well inside the view.
void bw_close_window(uint8_t* self);
void bw_button_set(BaseButton* b, int32_t on);
void bw_button_draw(BaseButton* b);
// FlatButton_ctor's look / BaseButton_init (also used by the screens of screenwin.h).
void bw_button_ctor(BaseButton* b);
void bw_button_init(BaseButton* b, const char* text, int32_t id, int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t parent);
// MainInterface_init's parts the base window and the screens need (console art, pane rectangles)
// and the console art as loaded.
void basewin_mi_init();
const std::vector<uint8_t>& basewin_mi_back();
Buffer* basewin_mi_canvas();
BaseButton* bw_button(int32_t id);
void mi_restore_back(int32_t clip);
void iface_set_mode(int32_t mode);
void iface_release_mode(int32_t mode);
void console_center();
bool mapwin_tile_in_view(uint8_t* win, int32_t x, int32_t y, int32_t margin);
// textcolor_init (0x4BA220) into the memory image (basewin_draw.cpp); the boot function itself
// stays in the headless boot area.
void textcolor_load();
Sprite* bw_sprite_at(uint32_t off);  // the base window's sprite at +off (ArtSprite)
// The production picker (prodwin.cpp, FUN_00494700): see there.
int32_t prodwin_pick(int32_t faction, int32_t slot, int32_t* mode);
void prodwin_rebuild();
void prodwin_hide();
bool prodwin_shown();
void prodwin_compose(Buffer* screen);

// The scroll bars of the facilities pane (window) and of the units supported pane (main
// interface): basewin_draw.cpp, clicked in basewin.cpp.
#include "ui/scroll.h"
extern ScrollBar g_fac_bar, g_sup_bar;
extern bool g_fac_bar_shown, g_sup_bar_shown;
extern int32_t g_fac_bar_page;
