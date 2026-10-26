// Native frame of smacx's full-screen modal windows (not an original class): the social
// engineering window (SocialWin), the reports (ReportWin / ReportIf), the monuments (MonuWin) and
// the hall of fame (FameWin). In the original each is a GraphicWin over the map with, for most of
// them, a SubInterface in the main interface (the console at the bottom of the screen), run by
// Win_set_modal until Win_release_modal. Here a ScreenWin holds the window's canvas and position,
// optionally a copy of the main interface's canvas (console2_A.pcx art, the pane rectangles of
// MainInfc as basewin.cpp sets them), push buttons (BaseButton / FlatButton, basewin_internal.h)
// and clickable spots on either canvas, and runs the modal loop on smacx's input (ui_wait_input).
// The screens draw with the original's art (box sprites, PCX files), fonts (find_font) and text
// colours; their layout follows the original's rectangles where noted, their pixels are not
// compared with the original (each file's header says what is approximated).
//
// The windows are composed over the screen by ui_present (screens_compose, below the base window,
// the menus and the popups). A base window opened from a screen (BaseWin_zoom from the base
// operations report) gets the input while it is open, as the original's modal stack does.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/basewin_internal.h"
#include "ui/gfx.h"
#include "ui/win.h"

struct ScreenWin {
    const char* name = "";
    bool open = false;
    int32_t x = 0, y = 0;              // window position on the screen
    Buffer canvas;                     // the window's canvas (+0x444 of the GraphicWin)
    bool iface = false;                // uses the main interface (SubInterface)
    Buffer mi;                         // the main interface's canvas while the window is open
    std::vector<BaseButton*> buttons;  // parent 0: the window, 1: the main interface
    Spots spots[2];                    // clickable spots of the window / the main interface
    BaseButton* pressed = nullptr;
    // What the last redraw wrote (test aid: SCREENSTATE): one entry per text drawn by sw_text.
    std::vector<std::string> texts;
    // The palette layer of the window's canvas (win.h ui_layer_mark; 0: the game palette).
    uint8_t layer = 0;

    virtual ~ScreenWin() {}
    virtual void redraw() = 0;
    virtual void on_button(int32_t /*id*/) {}
    // A key (virtual key with the modifier bits as ui_wait_input delivers them).
    virtual void on_key(int32_t /*key*/) {}
    // A click on a spot (index, type) or elsewhere (index -1) of parent 0 / 1 at (x, y) of that canvas.
    virtual void on_click(int32_t /*parent*/, int32_t /*index*/, int32_t /*type*/, int32_t /*x*/, int32_t /*y*/,
                          bool /*right*/, bool /*double_click*/) {}
    // Logged by SCREENSTATE after the texts (state the tests check).
    virtual void log_state() {}
    // The mouse moved to (x, y) of the window's canvas (screen coordinates minus the position).
    virtual void on_move(int32_t /*x*/, int32_t /*y*/) {}
};

// Creates the canvases: the window (w x h at x, y; filled with the transparent colour 9) and, with
// iface, the main interface's canvas (console art and pane backgrounds as BaseWin's).
void sw_init(ScreenWin* s, int32_t x, int32_t y, int32_t w, int32_t h, bool iface);
// A push button of parent 0 / 1 (FlatButton look: interface.pcx bars), its label in `font`.
BaseButton* sw_button(ScreenWin* s, const char* text, int32_t id, int32_t x, int32_t y, int32_t w, int32_t h,
                      int32_t parent, Font* font);
void sw_clear_buttons(ScreenWin* s);
// Runs the modal loop until the window closes (s->open false) or the input ends; redraws first.
void sw_run(ScreenWin* s);
void sw_close(ScreenWin* s);
// Shows / hides the window without running it (a window that stays up under other modal windows,
// as the desktop of the opening screens).
void sw_show(ScreenWin* s);
void sw_hide(ScreenWin* s);
// Text helpers recording what they write (s->texts).
int32_t sw_text(ScreenWin* s, Buffer* c, const std::string& text, int32_t x, int32_t y);
int32_t sw_text_cent(ScreenWin* s, Buffer* c, const std::string& text, const UiRect& r);
int32_t sw_text_right(ScreenWin* s, Buffer* c, const std::string& text, const UiRect& r);
void sw_font(Buffer* c, Font* font, int32_t color, int32_t shadow = -1);
// The main interface's pane background (MainInterface_restore_back) on the screen's own copy.
void sw_restore_iface(ScreenWin* s);
// Label TextLabels[off / 4] (Strings_get), as is / upper-cased (CharUpperA).
std::string sw_label(uint32_t off);
std::string sw_upper(std::string s);

// ---- composition and test aids (win.cpp) ----
void screens_compose(Buffer* screen);
// The open screen (top of the stack), null when none.
ScreenWin* screen_top();
// SCREENSTATE: the open screen's name, texts and state on stderr ("screenstate ..." lines).
void screens_log_state();
// FACTIONSTATE: the viewer's social choice, allocation, credits and upheaval paid.
void screens_log_faction();
