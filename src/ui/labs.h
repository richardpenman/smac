// The technology tree of the labs report and of the datalinks' technology entries
// (ReportWin_draw_labs 0x498B30, ReportWin_labs_line 0x49B450, ReportWin_on_button_passover
// 0x49B220, the tree part of ReportWin_on_button_clicked 0x4986D0; labs.cpp). One function draws
// both: the report's (its research box, the progress bar and the breakthrough line above the tree)
// and the datalinks' (InfcList's top 8: the tree in the entry's text rectangle, the consequences
// as links).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/gfx.h"

struct ScreenWin;

// The technology at the tree's centre (ReportWin + 0xFAB8, 0x885F30).
int32_t& labs_tree_tech();
// ReportWin_draw_labs into canvas c of window s (its buttons 0x379.. are created on s, parent 0):
// datalink false: the report's layout in its inner rectangle; true: the datalinks' layout in
// `area` (the union of the entry's text rectangles). Lines of what was drawn go to `log`.
void labs_draw(ScreenWin* s, Buffer* c, bool datalink, int32_t faction, const UiRect& area,
               std::vector<std::string>* log);
// Removes the tree's buttons from s (an entry that is not a technology).
void labs_clear(ScreenWin* s);
// ReportWin_on_button_clicked for the tree's buttons (0x379 / 0x37A the prerequisites, 0x37B..
// 0x37E the technologies it leads to): the centre's new technology; false for other ids.
bool labs_button(int32_t id);
// ReportWin_on_button_passover: the mouse at (x, y) of the canvas; the button under it and the
// one left are drawn highlighted / plain (with their lines to the centre).
void labs_mouse(ScreenWin* s, Buffer* c, int32_t x, int32_t y);
// The interface arrows' sprites (load_box_sprites: 0x75AFD0 [3], interface.pcx).
void labs_load_arrows(Buffer* interface_pcx);
