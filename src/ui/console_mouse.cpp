// The main view's mouse code (MapWin's message handlers and the go-to drag timer of the timers
// module), the mouse cursors of CURSOR.PCX and the routing of mouse input on the main screen.
//
// Original behaviour (asm of MapWin_on_left_down 0x46ED30, go_timer 0x50F170, MapWin_click
// 0x46D5D0): a left click on the map centres the view on the tile and activates the player's
// unit there (or opens the base); a mode started by an order (Console_cursor_on_2: go to, road
// to, patrol, artillery, drop, bombing) gives that order on the clicked tile; holding the button
// (1 s, 1/3 s in unit mode) turns the cursor into the go-to arrow and the release tile becomes the
// selected unit's destination (with ctrl in view mode: a base's rally point). Right clicks show
// the tile (shift+ctrl), the unit menu (MapWin_right_menu, when the preference asks for it) or
// centre the view.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/action.h"
#include "engine/compat.h"
#include "engine/game.h"
#include "engine/base.h"
#include "engine/crt.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/save.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "platform/ui_platform.h"
#include "ui/infowin.h"
#include "ui/statuswin.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/font_text.h"
#include "ui/basewin.h"
#include "engine/strings.h"
#include "ui/console_frame.h"
#include "ui/popup.h"
#include "ui/sprites.h"
#include "ui/win.h"

namespace {

GLOBAL(int32_t, MapMouseEnabled, 0x90D938);  // set by WorldWin_init: the map windows take clicks
GLOBAL(int32_t, MapMouseModal, 0x915620);  // go_timer does nothing while set
GLOBAL(int32_t, MouseMoveBusy, 0x7F67C4);  // MapWin_on_mouse_move re-entry guard
GLOBAL(int32_t, WorldBuildBrush, 0x945AE0);  // scenario editor brush (0, 2, 0x12: climate changes)

// MapWin fields used by the mouse code (byte offsets of the MapWin object).
constexpr uint32_t kMwRightCount = 0x1DD7C;
constexpr uint32_t kMwGroupDrag = 0x1DD84;  // the drag moves the stack (Console_group)
constexpr uint32_t kMwIgnoreClick = 0x1DD88;  // the next click is ignored (set by go_reset)
constexpr uint32_t kMwEditPaint = 0x1DD8C;  // scenario editor painting (1 left, 2 right)
constexpr uint32_t kMwDragX = 0x1DE0C;  // tile under the drag (-1: none)
constexpr uint32_t kMwDragY = 0x1DE10;
constexpr uint32_t kMwDragBase = 0x1DE24;  // base whose rally point is dragged
constexpr uint32_t kMwCursor = 0x1EF4C;  // cursor sprite address (0: arrow)
constexpr uint32_t kMwCursorId = 0x1EF50;  // standard cursor id (0x7F00: IDC_ARROW)
constexpr uint32_t kCursorGoTo = 0x93AA70 + 1 * 0x2C;  // 0x93AA9C
constexpr uint32_t kCursorNo = 0x93AA70 + 3 * 0x2C;  // 0x93AB4C
constexpr uint32_t kCursorBase = 0x93AA70 + 9 * 0x2C;  // 0x93ABFC

// ---- cursors (load_cursors 0x454FC0: CURSOR.PCX, 12 cursors of 32 x 32 at x = 1 + 33 i; the
// hot spot is marked by colour 5 in the row above and the column left of each cell) ----
struct Cursor {
    Sprite sprite;
    int32_t hot_x = 0, hot_y = 0;
};
Cursor g_cursors[12];
bool g_cursors_loaded = false;
int32_t g_cursor = -1;  // shown cursor (-1: the system arrow)
int32_t g_mouse_x = -1, g_mouse_y = -1;

void load_cursors() {
    if (g_cursors_loaded) return;
    g_cursors_loaded = true;
    Buffer canvas;
    Buffer_ctor(&canvas);
    if (Buffer_load_pcx(&canvas, "CURSOR.PCX", nullptr, 10, 0xEC)) return;
    int32_t x0 = 1;
    for (Cursor& c : g_cursors) {
        for (int32_t i = 0; i < 0x20; i++) {
            if (Buffer_get_pixel(&canvas, x0 + i, 0) == 5) c.hot_x = i;
        }
        for (int32_t i = 0; i < 0x20; i++) {
            if (Buffer_get_pixel(&canvas, x0 - 1, i + 1) == 5) c.hot_y = i;
        }
        Buffer_change_color(&canvas, 0x107, 0x109);
        Buffer_change_color(&canvas, 0x106, 0x109);
        pfncSpriteFromCanvasRectTrans(&c.sprite, &canvas, 0x109, x0, 1, 0x20, 0x20, 0);
        x0 += 0x21;
    }
}

// ---- timers (pfncStartTimer / pfncStopTimer on the console's timer object) ----
uint32_t g_go_timer_due = 0;  // 0: stopped

// ---- mouse capture of the main screen ----
enum Target { T_NONE, T_MAP, T_FRAME };
Target g_left_target = T_NONE, g_right_target = T_NONE;

uint8_t* main_view() { return MapWins[0]; }

bool key_held(int32_t vk) { return (ui_key_state(vk) & 0x8000) != 0; }

}  // namespace

// ---- cursors -----------------------------------------------------------------------------------------

void ui_set_map_cursor(int32_t index) {
    load_cursors();
    g_cursor = index;
    ui_invalidate();
}

void ui_mouse_pos(int32_t* x, int32_t* y) {
    *x = g_mouse_x;
    *y = g_mouse_y;
}

void console_draw_cursor(Buffer* screen) {
    if (g_cursor < 0 || g_cursor >= 12 || g_mouse_x < 0) return;
    Cursor& c = g_cursors[g_cursor];
    UiRect clip = {0, 0, screen->width, screen->height};
    Buffer_set_clip(screen, &clip);
    BlitDestScale = BlitSourceScale = 1;
    Sprite_draw_dest(&c.sprite, screen, 0x109, g_mouse_x - c.hot_x, g_mouse_y - c.hot_y, nullptr);
    Buffer_set_clip(screen, nullptr);
}

// ---- go-to drag ----------------------------------------------------------------------------------------

void GAMEAPI go_reset() {
    // Win_hide(DesktopWin) and the drag timer stop.
    ui_desktop_show(false);
    g_go_timer_due = 0;
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* w = MapWins[i];
        if (!w || (i && !mw(w, kMwActive)) || i == MapWinMouseIndex || !mw(w, kMwTracking)) continue;
        mw(w, kMwIgnoreClick) = 1;
        mw(w, kMwTracking) = 0;
        mw(w, kMwEditPaint) = 0;
        mw(w, kMwGroupDrag) = 0;
        mw(w, kMwDragX) = -1;
        StatusWin_set_title(&InfoWin, nullptr);
    }
    uint8_t* m = main_view();
    if (mw(m, kMwCursor) || mw(m, kMwCursorId) != 0x7F00) {
        ui_set_map_cursor(-1);
        mw(m, kMwCursorId) = 0x7F00;
        mw(m, kMwCursor) = 0;
    }
    if (MapWinMouseIndex >= 0) {
        uint8_t* w = MapWins[MapWinMouseIndex];
        uint32_t cur = (uint32_t)mw(w, kMwCursor);
        if ((cur == kCursorGoTo || cur == kCursorBase) && (mw(w, kMwCursor) || mw(w, kMwCursorId) != 0x7F00)) {
            ui_set_map_cursor(-1);
            mw(w, kMwCursorId) = 0x7F00;
            mw(w, kMwCursor) = 0;
        }
        mw(w, kMwIgnoreClick) = 1;
        mw(w, kMwTracking) = 0;
        mw(w, kMwEditPaint) = 0;
        mw(w, kMwGroupDrag) = 0;
        mw(w, kMwDragX) = -1;
        StatusWin_set_title(&InfoWin, nullptr);
        MapWinMouseIndex = -1;
    }
    ui_invalidate();
}

void GAMEAPI go_timer() {
    g_go_timer_due = 0;
    if (MapWinMouseIndex < 0 || MapMouseModal) return;
    uint8_t* w = MapWins[MapWinMouseIndex];
    int32_t mx, my;
    ui_mouse_pos(&mx, &my);
    uint8_t* self = &MainConsole;
    // ctrl in view mode (no group): a base's rally point (cursor 9); else the go-to arrow.
    int32_t cursor = (key_held(VKEY_CONTROL) && !MapWinInVehMode && !mw(self, kCon23D1C)) ? 9 : 1;
    if (mw(w, kMwOptions) & 0x40000) {
        cursor = 9;
    } else if (cursor == 1 && !MapWinInVehMode && !mw(self, kCon23D1C)) {
        // View mode: the dragged unit is the selected one at the cursor, else an exploring unit of
        // the player there.
        int32_t v = mw(self, kConVeh);
        int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
        bool keep = v >= 0 && Vehs[v].x == cx && Vehs[v].y == cy && veh_selectable(v) && !Vehs[v].order &&
                    !(Vehs[v].state & 0x4200);
        if (!keep) {
            v = veh_find(cx, cy, CurrentPlayerFaction, -1);
            if (v < 0 || !veh_selectable(v) || Vehs[v].order || (Vehs[v].state & 0x200) || !(Vehs[v].state & 0x4000)) {
                go_reset();
                return;
            }
            mw(self, kConVeh) = v;
        }
    }
    uint32_t sprite = 0x93AA70u + (uint32_t)cursor * 0x2C;
    if ((uint32_t)mw(w, kMwCursor) != sprite) {
        ui_set_map_cursor(cursor);
        mw(w, kMwCursor) = (int32_t)sprite;
    }
    mw(self, kConMode) = cursor;
    Buffer_fill_2(ui_desktop_canvas(), 9);  // DesktopWin: GraphicWin_fill(9), Win_show
    ui_desktop_show(true);
    mw(w, kMwTracking) = 1;
    mw(w, kMwGroupDrag) = (!MapWinInVehMode && mw(self, kCon23D1C)) ? 1 : 0;
    MapWin_on_mouse_move(w, mx, my);
}

// Fires the drag timer when it is due (called by the pump).
// blink_timer (0x50EA40, every 150 ms; start_timers): while the human gives orders, the cursor
// (view mode) or the selected unit (unit mode) blinks: kConCursorShown toggles and draw_cursor
// redraws. Not reconstructed: its scrolling of the view when the mouse is at the screen's edge,
// PlanWin_blink.
void blink_timer() {
    uint8_t* self = &MainConsole;
    if (GameNotStarted || MapMouseModal || Win_is_visible(&BaseWin)) return;
    if ((!mw(self, kConTurnActive) && (!MultiplayerActive || !(GameState & 2))) || ControlTurnA) {
        mw(self, kConCursorOn) = 0;
        return;
    }
    if (mw(self, kConWaiting) || (MultiplayerActive && (GameState & 2))) {
        mw(self, kConCursorShown) = !mw(self, kConCursorShown);
        draw_cursor();
    }
}

void console_ui_tick() {
    if (g_go_timer_due && (int32_t)(platform::ticks_ms() - g_go_timer_due) >= 0) go_timer();
    static uint32_t last_blink = 0;
    uint32_t now = platform::ticks_ms();
    if (now - last_blink >= 150) {
        last_blink = now;
        blink_timer();
    }
    // InfoWin_init's timer (10, every 50 ms): the info pane's typing.
    static uint32_t last_info = 0;
    if (now - last_info >= 50) {
        last_info = now;
        infowin_tick();
    }
    battlewin_tick();
}

// ---- MapWin handlers -------------------------------------------------------------------------------

void THISCALL MapWin_on_left_down(uint8_t* win, int32_t px, int32_t py) {
    if (!MapMouseEnabled) return;
    go_reset();
    mw(win, kMwRightCount) = 0;
    mw(win, kMwTracking) = 0;
    mw(win, kMwEditPaint) = 0;
    mw(win, kMwDragX) = -1;
    mw(win, kMwIgnoreClick) = 0;
    mw(win, kMwGroupDrag) = 0;
    if (ControlTurnC || mw(win, kMwIndex)) return;
    int32_t mode = mw(&MainConsole, kConMode);
    if (mode) {
        if (mode != 8) return;
        // Patrol: the waypoints follow the mouse.
        mw(win, kMwTracking) = 1;
        MapWin_on_mouse_move(win, px, py);
        return;
    }
    bool shift = key_held(VKEY_SHIFT), ctrl = key_held(VKEY_CONTROL);
    if (((shift && ctrl) || ((ui_key_state(0x91) & 1) && !ctrl)) && (GameState & STATE_SCENARIO_EDITOR)) {
        // Scenario editor painting.
        auto_undo();
        mw(win, kMwEditPaint) = 1;
        mw(win, 0x1DD94) = -1;
        mw(win, 0x1DD90) = -1;
        MapWin_on_mouse_move(win, px, py);
        return;
    }
    if (ctrl || Console_edit_lock(&MainConsole)) return;
    int32_t x = -1, y = -1;
    if (!MapWinInVehMode || (mw(win, kMwOptions) & 0x40000)) {
        if (pfncPtToTile(win, px, py, &x, &y) || y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    }
    if (mw(win, kMwOptions) & 0x40000) {
        mw(win, kMwDragBase) = base_find_faction(x, y, CurrentPlayerFaction);
        if (mw(win, kMwDragBase) < 0) return;
    }
    MapWinMouseIndex = mw(win, kMwIndex);
    if (key_held(VKEY_SHIFT) && !(GameState & STATE_SCENARIO_EDITOR)) {
        go_timer();
        return;
    }
    g_go_timer_due = platform::ticks_ms() + (MapWinInVehMode ? 333 : 1000);
}

void THISCALL MapWin_on_left_click(uint8_t* win, int32_t px, int32_t py) {
    if (MapMouseEnabled) MapWin_click(win, px, py, 0);
}

void THISCALL MapWin_on_right_click(uint8_t* win, int32_t px, int32_t py) {
    if (MapMouseEnabled) MapWin_click(win, px, py, 1);
}

void THISCALL MapWin_on_right_down(uint8_t* win, int32_t px, int32_t py) {
    if (!MapMouseEnabled) return;
    if (mw(win, kMwTracking)) StatusWin_set_title(&InfoWin, nullptr);
    mw(win, kMwRightCount) = 0;
    mw(win, kMwTracking) = 0;
    mw(win, kMwEditPaint) = 0;
    mw(win, kMwGroupDrag) = 0;
    mw(win, kMwDragX) = -1;
    mw(win, kMwIgnoreClick) = 0;
    if (mw(&MainConsole, kConMode)) return;
    bool shift = key_held(VKEY_SHIFT), ctrl = key_held(VKEY_CONTROL);
    if (((shift && ctrl) || ((ui_key_state(0x91) & 1) && !ctrl)) && (GameState & STATE_SCENARIO_EDITOR)) {
        mw(win, kMwEditPaint) = 2;
        mw(win, 0x1DD94) = -1;
        mw(win, 0x1DD90) = -1;
        MapWin_on_mouse_move(win, px, py);
    }
}

void THISCALL MapWin_on_mouse_move(uint8_t* win, int32_t px, int32_t py) {
    if (MouseMoveBusy || ControlUpkeepA || ControlTurnC || !MapMouseEnabled || mw(win, kMwIndex)) return;
    int32_t x, y;
    if (pfncPtToTile(win, px, py, &x, &y) || y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    if (mw(win, kMwEditPaint)) {
        MapWin_editor(win, x, y, mw(win, kMwEditPaint) == 2);
        return;
    }
    uint8_t* self = &MainConsole;
    if (!Console_selecting(self, x, y)) {
        if (mw(win, kMwCursor) || mw(win, kMwCursorId) != 0x7F00) {
            ui_set_map_cursor(-1);
            mw(win, kMwCursorId) = 0x7F00;
            mw(win, kMwCursor) = 0;
        }
        return;
    }
    if (!mw(win, kMwTracking)) return;
    MouseMoveBusy = 1;
    do_all_non_input();
    MouseMoveBusy = 0;
    if (mw(win, kMwDragX) == x && mw(win, kMwDragY) == y) return;
    // The path from the unit (each leg of a patrol / waypoint route) or the base to the tile on
    // DesktopWin (GraphicWin_fill(9), MapWin_dest_line solid), with "Go to <tile>: Turns = n".
    Buffer* d = ui_desktop_canvas();
    Buffer_fill_2(d, 9);
    mw(win, kMwDragX) = x;
    mw(win, kMwDragY) = y;
    // Path segment table of the drag (+0x1DDE4: x[i], +0x1DDF8: y[i]; leg i from i to i + 1).
    auto seg_x = [&](int32_t i) -> int32_t& { return mw(win, 0x1DDE4 + 4 * (uint32_t)i); };
    auto seg_y = [&](int32_t i) -> int32_t& { return mw(win, 0x1DDF8 + 4 * (uint32_t)i); };
    int32_t v = mw(self, kConVeh);
    if (v < 0) {
        // A base's rally point. BUG(original): the leg's end is (y, stale) — the tile's y is stored
        // as the end's x and the end's y is not written.
        COMPAT_ORIGINAL(rally_leg_end);
        mw(win, 0x1DE14) = 10;
        int32_t b = mw(win, kMwDragBase);
        seg_x(0) = Bases[b].x;
        seg_y(0) = Bases[b].y;
        seg_x(1) = y;
        mw(win, 0x1DE18) = 2;
        mw(win, 0x1DE1C) = 0;
        mw(win, 0x1DE20) = 1;
        MapWin_dest_line(win, seg_x(0), seg_y(0), seg_x(1), seg_y(1), mw(win, 0x1DE14), CurrentPlayerFaction,
                         mw(win, 0x1DE18), mw(win, 0x1DE1C), 3, 5, 1);
    } else {
        // The waypoint under the mouse (patrol: the next one; go to: the destination).
        VEH& u = Vehs[v];
        mw(win, 0x1DE14) = u.unit_id;
        bool patrol = mw(self, kConMode) == 8;
        mw(win, 0x1DE1C) = patrol;
        if (patrol || (mw(self, kConMode) == 1 && (u.state & 0x2000000))) {
            u.waypoint_x[u.waypoint_count] = (int16_t)x;
            u.waypoint_y[u.waypoint_count] = (int16_t)y;
        } else if (mw(self, kConMode) == 1) {
            u.waypoint_x[0] = (int16_t)x;
            u.waypoint_y[0] = (int16_t)y;
            u.waypoint_count = 0;
        }
        mw(win, 0x1DE18) = Chassis[Units[u.unit_id].chassis_id].triad;
        for (int32_t i = 0; i < u.waypoint_count + 1 + (patrol && u.waypoint_count ? 1 : 0); i++) {
            int32_t n = u.waypoint_count + 2, j = (i + 1) % n;
            int32_t fx = i ? u.waypoint_x[i - 1] : u.x, fy = i ? u.waypoint_y[i - 1] : u.y;
            int32_t tx = j ? u.waypoint_x[j - 1] : u.x, ty = j ? u.waypoint_y[j - 1] : u.y;
            seg_x(i) = fx;
            seg_y(i) = fy;
            seg_x(i + 1) = tx;
            seg_y(i + 1) = ty;
            mw(win, 0x1DE20) = i + 1;
            MapWin_dest_line(win, fx, fy, tx, ty, mw(win, 0x1DE14), CurrentPlayerFaction, mw(win, 0x1DE18),
                             mw(win, 0x1DE1C), 3, 5, 1);
        }
    }
    // The text beside the mouse: "<Go to|Patrol|Group> <tile>: Turns = n[ r/m]".
    char text[512], num[16];
    int32_t label = mw(self, kConMode) == 8 ? 0x668 : (mw(win, kMwGroupDrag) ? 0x6C4 : 0x66C);
    crt_strcpy(text, label_get(label / 4));
    crt_strcat(text, " ");
    say_loc(text, x, y, 0, 2, 0);
    crt_strcat(text, reinterpret_cast<const char*>(mem::host(0x682E94)));
    crt_strcat(text, label_get(0x670 / 4));
    crt_strcat(text, reinterpret_cast<const char*>(mem::host(0x6863A0)));
    int32_t mr = Rules.move_rate_roads;
    std::snprintf(num, sizeof(num), "%d", DestLineCost / mr);
    crt_strcat(text, num);
    if (DestLineCost % mr) {
        std::snprintf(num, sizeof(num), " %d", DestLineCost % mr);
        crt_strcat(text, num);
        crt_strcat(text, reinterpret_cast<const char*>(mem::host(0x6863A4)));
        std::snprintf(num, sizeof(num), "%d", mr);
        crt_strcat(text, num);
    }
    Buffer_set_text_color(d, 0x9F, 0x41, 1, 1);
    Buffer_set_font(d, find_font(12, 1), nullptr, nullptr, nullptr);
    uint8_t* mv = main_view();
    int32_t width = mw(mv, ui_mapwin_native(mv)->vbase + kGwWidth);
    int32_t tw = Buffer_text_width(d, text);
    Buffer_write_l_3(d, text, px - tw * px / (width ? width : 1), py + mw(&MainConsole, kMwTileH),
                     (int32_t)std::strlen(text));
    ui_desktop_show(true);
    std::fprintf(stderr, "goto drag: %s\n", text);
    ui_set_drag_target(x, y);
}

void THISCALL MapWin_click(uint8_t* win, int32_t px, int32_t py, int32_t right) {
    if (mw(win, kMwIgnoreClick)) {
        mw(win, kMwIgnoreClick) = 0;
        return;
    }
    AlphaMenu_hide_main();
    InfoWin_reset(&InfoWindow);
    StatusWin_reset(&InfoWin);
    TutWin_reset(&TutWin);
    mw(&MainConsole, kConRedraw) = 0;  // MapWinField23C00
    BaseWinMultiplayerRefresh = 0;
    uint32_t cursor = (uint32_t)mw(win, kMwCursor);
    bool goto_cursor = cursor == kCursorGoTo;  // [ebp-0x28]
    bool base_cursor = cursor == kCursorBase;  // [ebp-0x1c]
    int32_t drag_base = mw(win, kMwDragBase);  // [ebp-0x2c]
    int32_t group = mw(win, kMwGroupDrag);  // [ebp-0x30]
    go_reset();
    ui_set_drag_target(-1, -1);
    int32_t ctrl = key_held(VKEY_CONTROL);
    if ((GameState & STATE_SCENARIO_EDITOR) && (ui_key_state(0x91) & 1) && !key_held(VKEY_SHIFT)) ctrl = !ctrl;
    bool no_cursor = (uint32_t)mw(win, kMwCursor) == kCursorNo;
    int32_t mode = Console_selected(&MainConsole);  // [ebp-0x24]
    if (mode && no_cursor) return;  // out of range
    if ((right || ControlTurnC) && (goto_cursor || base_cursor || mode)) return;
    int32_t x, y;
    if (pfncPtToTile(win, px, py, &x, &y)) return;
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    uint8_t* self = &MainConsole;
    if (mw(win, kMwEditPaint)) {
        MapWin_editor(win, x, y, mw(win, kMwEditPaint) & 2);
        mw(win, kMwEditPaint) = 0;
        if (!WorldBuildBrush || WorldBuildBrush == 2 || WorldBuildBrush == 0x12) {
            world_climate();
            for_each_mapwin([](int32_t, uint8_t* w) { pfncRedrawMap(w, 1); });
        } else {
            draw_map(1);
        }
        return;
    }
    if (base_cursor && drag_base >= 0 && (uint8_t)Bases[drag_base].faction_id == CurrentPlayerFaction) {
        // Dragging a base's rally point: asked for which triad.
        set_base(drag_base);
        if (NetDaemon_lock_base(&NetState, CurrentBaseID, 0, -1, -1)) return;
        int32_t target = base_find_filtered(x, y, CurrentPlayerFaction, -1, -2, -1);
        parse_says(0, Bases[drag_base].name, -1, -1);
        parse_says(1, Bases[target].name, -1, -1);
        BasePop* pop = popup_new();
        BasePop_start(pop, ScriptFileName, "DESTTRIAD", -1, nullptr, 0x40, nullptr);
        for (int32_t i = 0; i < 3; i++) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, TriadName[i]);
            crt_strcat(StrBuffer, " ");
            crt_strcat(StrBuffer, "(");
            say_base(StrBuffer, (&Bases[drag_base].autoforward_land_base_id)[i]);
            crt_strcat(StrBuffer, ")");
            BasePop_item(pop, StrBuffer, i);
        }
        int32_t triad = BasePop_exec_3(pop, 0, 0);
        popup_delete(pop);
        if (triad >= 0) {
            int32_t r_from = map_tile(Bases[drag_base].x, Bases[drag_base].y)->region;
            int32_t r_to = map_tile(Bases[target].x, Bases[target].y)->region;
            bool ok = true;
            int32_t dest = target;
            if (drag_base == target) {
                dest = -1;
            } else if (triad == TRIAD_SEA) {
                ok = port_to_port(drag_base, target) != 0;
            } else if (triad == TRIAD_LAND) {
                ok = r_to == r_from;
            }
            if (ok) {
                (&Bases[drag_base].autoforward_land_base_id)[triad] = (int16_t)dest;
            } else {
                parse_say(2, TriadName[triad], -1, -1);
                NetMsg_pop(&NetMsg, "DESTNO", 5000, 0, nullptr);
            }
        }
        NetDaemon_unlock_base(&NetState, CurrentBaseID);
        return;
    }
    if (goto_cursor) {
        int32_t v = mw(self, kConVeh);
        if (!MapWinInVehMode || v < 0) {
            if (mw(self, kCon23D1C)) {
                // A group without a leader: a stand-in leads it to the tile.
                Console_set_cursor(self, x, y);
                int32_t fake = veh_fake(0, CurrentPlayerFaction);
                VEH& f = Vehs[fake];
                f.x = (int16_t)x;
                f.y = (int16_t)y;
                f.order = ORDER_MOVE_TO;
                f.waypoint_x[0] = (int16_t)x;
                f.waypoint_y[0] = (int16_t)y;
                f.state = 0;
                Console_group(self, fake);
                return;
            }
            if (v < 0) return;
        }
        if (net_lock_veh(&v, group ? 4 : 0)) return;
        VEH& u = Vehs[v];
        u.order = ORDER_MOVE_TO;
        if (u.state & 0x2000000) {
            // Patrol under way: the tile becomes the next waypoint.
            u.state |= 0x2000200;
            u.waypoint_x[u.waypoint_count] = (int16_t)x;
            u.waypoint_y[u.waypoint_count] = (int16_t)y;
            u.state &= 0xFFFEFFFF;
        } else {
            u.waypoint_x[0] = (int16_t)x;
            u.waypoint_y[0] = (int16_t)y;
            u.state &= 0xFDFEFFFF;
            u.waypoint_count = 0;
            u.patrol_current_point = 0;
        }
        if (group) Console_group(self, v);
        ui_synch_veh(v);
        net_unlock_veh();
        if (!MapWinInVehMode) Console_set_move(self, 1);
        return;
    }
    if (mode) {
        int32_t v = mw(self, kConVeh);
        if (v < 0) return;
        switch (mode) {
        case 2: Console_set_road_to(self, v, x, y, ORDER_ROAD_TO); break;
        case 3: Console_set_road_to(self, v, x, y, ORDER_MAGTUBE_TO); break;
        case 4: action_airdrop(v, x, y, 3); break;  // NetDaemon_veh_drop without multiplayer
        case 8: action_patrol(v, x, y); break;
        case 10: action_arty(v, x, y); break;
        case 11: Console_set_bombing_run(self, v, x, y); break;
        default: break;
        }
        mw(self, kConWaiting) = 0;
        return;
    }
    if ((GameState & STATE_SCENARIO_EDITOR) && ctrl && !ControlTurnC) {
        Console_set_view(self, 1);
        Console_set_cursor(self, x, y);
        auto_undo();
        MapWin_editor(win, x, y, right);
        return;
    }
    if (right) {
        if (key_held(VKEY_SHIFT)) {
            if (ctrl) {
                // shift+ctrl: the tile's description.
                StatusWin_set_loc(&InfoWin, x, y);
                StatusWin_redraw(&InfoWin);
                InfoWin_set_state(&InfoWindow, -1, 0);
                return;
            }
            if (!(GameMorePreferences & MPREF_ADV_RIGHT_CLICK_POPS_UP_MENU) && !ControlTurnC) {
                MapWin_right_menu(win, x, y, px, py);
                return;
            }
            Console_set_view(self, right);
            Console_set_cursor(self, x, y);
            if (win == main_view()) mw(win, kMwScrolling) = 1;
            pfncMoveMap(win, x, y, 1);
            mw(win, kMwScrolling) = 0;
            return;
        }
        if (((GameMorePreferences & MPREF_ADV_RIGHT_CLICK_POPS_UP_MENU) || mw(win, kMwIndex) == 1) && !ControlTurnC) {
            MapWin_right_menu(win, x, y, px, py);
            return;
        }
    }
    if (!mw(win, kMwIndex)) {
        if (!MapWinInVehMode || right || ControlTurnC || Console_edit_lock(self)) {
            Console_set_view(self, right);
            Console_set_cursor(self, x, y);
            if (right && mw(win, kMwRightCount) > 1) return;
        }
    } else if (right && mw(win, kMwRightCount) > 1) {
        return;
    }
    int32_t base_id = base_at(x, y);
    if (mw(win, kMwIndex) != 1 && base_id >= 0 && !right && !ControlTurnC) {
        // A known base opens (own bases, spied on, pact partners'), the view centred on it.
        BASE& b = Bases[base_id];
        int32_t owner = (uint8_t)b.faction_id;
        bool known = owner == CurrentPlayerFaction || (b.visibility & (1 << (CurrentPlayerFaction & 31))) ||
                     (GameState & STATE_OMNISCIENT_VIEW);
        if (known && ((GameState & STATE_OMNISCIENT_VIEW) || owner == CurrentPlayerFaction || spying(owner) ||
                      (Factions[CurrentPlayerFaction].diplo_status[owner] & 1))) {
            pfncMoveMap(win, x, y, 0);
            if (mw(win, kMwIndex) && (mw(win, kMwOptions) & 0x40000000)) pfncMoveMap(main_view(), x, y, 0);
            BaseWin_zoom(&BaseWin, base_id, 0);
        }
        return;
    }
    if (win == main_view()) mw(win, kMwScrolling) = 1;
    pfncMoveMap(win, x, y, 1);
    mw(win, kMwScrolling) = 0;
    int32_t v = veh_at(x, y);
    if (mw(win, kMwIndex) != 1 && v >= 0 && Vehs[v].faction_id == (uint32_t)CurrentPlayerFaction && !right &&
        !ControlTurnC && !Console_edit_lock(self)) {
        Console_activate(self, x, y, 0);
    }
}

int32_t THISCALL Console_set_cursor(uint8_t* self, int32_t x, int32_t y) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return 1;
    mw(self, kConCursorShown) = 0;
    if (mw(self, kConRedraw)) {
        mw(self, kConRedraw) = 0;
        pfncRedrawMap(main_view(), 1);
    }
    MapWin_draw_cursor(main_view());
    Console_cursor_next(self, x, y);
    StatusWin_redraw(&InfoWin);
    mw(self, kConCursorShown) = 1;
    mw(self, kConCursorOn) = 1;
    MapWin_draw_cursor(main_view());
    return 0;
}

// ---- routing of mouse input on the main screen (the window manager's part: Win_on_mouse...) ----

void console_ui_mouse(int32_t type, int32_t x, int32_t y) {
    load_cursors();
    MapMouseEnabled = 1;
    g_mouse_x = x;
    g_mouse_y = y;
    if (g_cursor >= 0) ui_invalidate();
    // The menu bar and its pull-downs take the mouse first, then the interface frame.
    if (console_menu_mouse(type, x, y)) return;
    uint8_t* view = main_view();
    bool over_frame = console_frame_hit(x, y);
    switch (type) {
    case UiInput::LDOWN:
        g_left_target = over_frame ? T_FRAME : T_MAP;
        if (g_left_target == T_FRAME) console_frame_mouse(type, x, y);
        else MapWin_on_left_down(view, x, y);
        break;
    case UiInput::LUP: {
        Target t = g_left_target;
        g_left_target = T_NONE;
        if (t == T_FRAME) console_frame_mouse(type, x, y);
        else if (t == T_MAP && !over_frame) MapWin_on_left_click(view, x, y);
        else if (t == T_MAP) go_reset();
        break;
    }
    case UiInput::RDOWN:
        g_right_target = over_frame ? T_FRAME : T_MAP;
        if (g_right_target == T_FRAME) console_frame_mouse(type, x, y);
        else MapWin_on_right_down(view, x, y);
        break;
    case UiInput::RUP: {
        Target t = g_right_target;
        g_right_target = T_NONE;
        if (t == T_FRAME) console_frame_mouse(type, x, y);
        else if (t == T_MAP && !over_frame) MapWin_on_right_click(view, x, y);
        break;
    }
    case UiInput::MOVE:
        if (over_frame && g_left_target != T_MAP) console_frame_mouse(type, x, y);
        else MapWin_on_mouse_move(view, x, y);
        break;
    default: break;
    }
}

// Scripted drag (test aid "DRAG:X1xY1:X2xY2"): press, the go-to timer as if held, move, release.
void console_ui_drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const std::string& shot) {
    console_ui_mouse(UiInput::LDOWN, x1, y1);
    if (g_go_timer_due) go_timer();
    console_ui_mouse(UiInput::MOVE, x2, y2);
    if (!shot.empty()) ui_screenshot(shot);
    console_ui_mouse(UiInput::LUP, x2, y2);
}
