// Hybrid-build implementations of the council window's operations (engine/council.h): the window
// parts of FUN_00427230, FUN_00424720, FUN_00424410 and FUN_00425BA0 on the original CouncilWin.
// Native builds use src/headless/ui_headless_council.cpp.
#ifdef SMACX_HYBRID
#include "engine/council.h"

#include <windows.h>

#include "engine/crt.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"

namespace {
typedef void(__thiscall* Method0)(uint8_t*);
typedef void(__thiscall* Method1)(uint8_t*, int32_t);
typedef int32_t(__thiscall* ModalFn)(uint8_t*, int32_t, int32_t, uint8_t*);
typedef int32_t(__thiscall* FontInitFn)(uint8_t*, const char*, int32_t, int32_t);
typedef int32_t(__thiscall* GraphicWinInitFn)(uint8_t*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint8_t*,
                                              int32_t, int32_t);
typedef int32_t(__thiscall* ButtonInitFn)(uint8_t*, const char*, int32_t, int32_t, int32_t, int32_t, int32_t, uint8_t*,
                                          int32_t);
typedef void(__thiscall* SetFontFn)(uint8_t*, uint8_t*, int32_t, int32_t, int32_t);
typedef int32_t(__thiscall* IsVisibleFn)(uint8_t*);

const Method0 buffer_ctor = reinterpret_cast<Method0>(0x5D7210);
const Method0 buffer_dtor = reinterpret_cast<Method0>(0x5D7410);
const Method0 font_close = reinterpret_cast<Method0>(0x619230);
const FontInitFn font_init = reinterpret_cast<FontInitFn>(0x618F40);
const Method1 spot_init = reinterpret_cast<Method1>(0x5FA8A0);
const GraphicWinInitFn graphicwin_init = reinterpret_cast<GraphicWinInitFn>(0x5D4EF0);
const ButtonInitFn basebutton_init = reinterpret_cast<ButtonInitFn>(0x607210);
const SetFontFn buffer_set_font = reinterpret_cast<SetFontFn>(0x5DAC70);
const IsVisibleFn win_is_visible = reinterpret_cast<IsVisibleFn>(0x5F7E90);  // pfncWinIsVisible
const Method0 multiwin_hide = reinterpret_cast<Method0>(0x479C00);
const Method0 multiwin_show = reinterpret_cast<Method0>(0x479970);
const Method0 alphamenu_hide = reinterpret_cast<Method0>(0x403830);
const Method0 alphamenu_show = reinterpret_cast<Method0>(0x403680);
const Method0 release_iface_mode = reinterpret_cast<Method0>(0x45D380);  // SubInterface_release_iface_mode
uint8_t* const kMultiWin = reinterpret_cast<uint8_t*>(0x7FD648);
uint8_t* const kAlphaMenu = reinterpret_cast<uint8_t*>(0x9380DC);
const int32_t kPopWait = 0x5398E0;  // pop_wait

// FUN_00427230's Buffer local (constructed at entry, destroyed at exit; otherwise unused).
alignas(16) uint8_t g_buffer[0x600];

// CouncilWin layout: int fields; panels (6, +0xD94, stride 0xB4C); fonts; buttons.
int32_t& field(uint8_t* win, int32_t index) { return *reinterpret_cast<int32_t*>(win + index * 4); }
uint8_t* panel(uint8_t* win, int32_t i) { return win + 0xD94 + i * 0xB4C; }
void* vmethod(uint8_t* obj, uint32_t offset) { return (*reinterpret_cast<void***>(obj))[offset / 4]; }
const uint32_t kFonts[5] = {0xAB4, 0xADC, 0xB04, 0xB2C, 0xB54};
uint8_t* button5(uint8_t* win) { return win + 0x4610; }

void button(uint8_t* win, uint32_t offset, int32_t label, int32_t id, int32_t rect) {
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(label));
    CharUpperA(StrBuffer);
    int32_t* r = &field(win, rect);
    basebutton_init(win + offset, StrBuffer, id, r[0], r[1], r[2] - r[0], r[3] - r[1], &MainInterfaceWin, 0);
}
}  // namespace

void ui_councilwin_open(uint8_t* win) {
    buffer_ctor(g_buffer);
    for (int32_t i = 0; i < 6; i++) reinterpret_cast<Method0>(vmethod(panel(win, i), 0x168))(panel(win, i));
    for (uint32_t f : kFonts) font_close(win + f);
    spot_init(win + 0xA48, 8);
    *reinterpret_cast<uint8_t**>(0x7AE80C) = win + 0xA14;
    field(win, 0x286) = 7;
    static const int32_t kLayout[][2] = {
        {0x2F2, 7},     {0x2F3, 0x2EE}, {0x2F4, 0x1B},  {0x2F1, 0x1E},  {0x2FD, 0x1E},  {0x2FE, 0x35},
        {0x2FF, 0x80},  {0x300, 0x14F}, {0x305, 0x82},  {0x306, 0x2D},  {0x307, 0xE4},  {0x308, 0x147},
        {0x30D, 0xE6},  {0x30E, 0x25},  {0x30F, 0x148}, {0x310, 0x13F}, {0x315, 0x14A}, {0x316, 0x1D},
        {0x317, 0x1C2}, {0x318, 0x160}, {0x31E, 0x25},  {0x31D, 0x1C4}, {0x31F, 0x226}, {0x320, 0x13F},
        {0x325, 0x228}, {0x326, 0x2D},  {0x327, 0x28A}, {0x328, 0x147}, {0x32D, 0x28C}, {0x32E, 0x35},
        {0x32F, 0x2EE}, {0x330, 0x14F}, {0x301, 0x20},  {0x302, 0x1D},  {0x303, 0x82},  {0x304, 0x137},
        {0x309, 0x87},  {0x30A, 0x1D},  {0x30B, 0xE9},  {0x30C, 0x137}, {0x311, 0xEE},  {0x312, 0x1D},
        {0x313, 0x150}, {0x314, 0x137}, {0x319, 0x155}, {0x31A, 0x1D},  {0x31B, 0x1B7}, {0x31C, 0x137},
        {0x321, 0x1BC}, {0x322, 0x1D},  {0x323, 0x21E}, {0x324, 0x137}, {0x329, 0x223}, {0x32A, 0x1D},
        {0x32B, 0x285}, {0x32C, 0x137}, {0x331, 0x28A}, {0x332, 0x1D},  {0x333, 0x2EC}, {0x334, 0x137},
        {0x2E9, 0xF5},  {0x2EA, 0x163}, {0x2EB, 0x21A}, {0x2EC, 0x17A},
    };
    for (const auto& e : kLayout) field(win, e[0]) = e[1];
    // (The original first stores a screen-size dependent layout of the fields below; every value
    // is overwritten here.) Frame: the main interface's rectangle, inset by 4; the three vote
    // buttons share its bottom row, the close button spans it.
    const int32_t* frame = reinterpret_cast<const int32_t*>(0x7AF5C0);
    field(win, 0x2ED) = frame[0] + 4;
    field(win, 0x2EE) = frame[1] + 4;
    field(win, 0x2EF) = frame[2] - 4;
    int32_t bottom = frame[3] - 4;
    int32_t y1 = bottom - 3, y0 = bottom - 0x17;
    int32_t x0 = field(win, 0x2ED) + 3;
    field(win, 0x2F0) = bottom - 0x1A;
    field(win, 0x341) = x0;
    field(win, 0x342) = y0;
    field(win, 0x343) = field(win, 0x2EF) - 3;
    field(win, 0x344) = y1;
    int32_t x1 = ((field(win, 0x2EF) - 3 - x0) + 3) / 3 - 3 + x0;
    for (int32_t b = 0; b < 3; b++) {
        field(win, 0x335 + b * 4) = x0;
        field(win, 0x336 + b * 4) = y0;
        field(win, 0x337 + b * 4) = x1;
        field(win, 0x338 + b * 4) = y1;
        int32_t step = x1 - x0 + 3;
        x0 += step;
        x1 += step;
    }
    const char* face = *reinterpret_cast<const char* const*>(0x691B30);  // "Arial Narrow"
    font_init(win + 0xB7C, face, 0x14, 1);
    font_init(win + 0xADC, face, 0xE, 1);
    font_init(win + 0xAB4, face, 0xE, 1);
    font_init(win + 0xB04, face, 0xC, 1);
    field(win, 0x2C1) = 5;
    font_init(win + 0xB2C, face, 0xC, 1);
    font_init(win + 0xB54, face, 0xE, 1);
}

void ui_councilwin_build(uint8_t* win) {
    // Faction portrait images (8 sets of 0x65C bytes from 0x78EDC4).
    for (int32_t k = 0; k < 8; k++) {
        uint32_t images = 0x78EDC4 + k * 0x65C;
        field(win, 0x345 + 2 * k) = images - 0x2C;
        field(win, 0x346 + 2 * k) = images;
        field(win, 0x355 + 2 * k) = images + 0x2C;
        field(win, 0x356 + 2 * k) = images + 0x58;
    }
    int32_t avail = *reinterpret_cast<int32_t*>(0x9B7B20) - *reinterpret_cast<int32_t*>(0x7AF59C);
    int32_t height = avail > 0x18B ? 0x18B : avail;
    uint8_t* parent = &MainConsole;
    parent += *reinterpret_cast<int32_t*>(*reinterpret_cast<uint8_t**>(parent) + 4);
    graphicwin_init(win, (ScreenWidth - 0x30C) / 2, avail - height, 0x32C, height, 0, 0x1000000, parent, 0, 0);
    GraphicWin_fill(win, 9);
    button(win, 0x18E0, 0x1EF, 1, 0x335);
    if (field(win, 0x287) == 0) {
        button(win, 0x3AC4, 0x1FB, 4, 0x341);
    } else {
        button(win, 0x242C, GovernorFaction == CurrentPlayerFaction ? 0x1F4 : 0x1F0, 2, 0x339);
        button(win, 0x2F78, 0x1F1, 3, 0x33D);
        button(win, 0x18E0, 0x1EF, 1, 0x335);
    }
    button(win, 0x4610, 0x65, 5, 0x341);
    reinterpret_cast<Method0>(vmethod(button5(win), 8))(button5(win));
    for (int32_t i = 0; i < 6; i++) {
        uint8_t* p = panel(win, i);
        reinterpret_cast<Method0>(vmethod(p, 8))(p);
        buffer_set_font(p + 0x444, win + 0xAB4, 0, 0, 0);
        reinterpret_cast<Method0>(vmethod(p, 0xF8))(p);
    }
}

int32_t ui_councilwin_show(uint8_t* win) {
    int32_t hidden = 0;
    if (win_is_visible(kMultiWin)) {
        hidden |= 1;
        multiwin_hide(kMultiWin);
    }
    if (win_is_visible(kAlphaMenu)) {
        hidden |= 2;
        alphamenu_hide(kAlphaMenu);
    }
    reinterpret_cast<Method1>(vmethod(win, 4))(win, 0);
    return hidden;
}

void ui_councilwin_modal(uint8_t* win) {
    if (g_harness_council_modal) {
        g_harness_council_modal(win);
        return;
    }
    reinterpret_cast<ModalFn>(vmethod(win, 0xE4))(win, 0, kPopWait, &MainInterfaceWin);
}

void ui_councilwin_close(uint8_t* win, int32_t hidden) {
    release_iface_mode(win + 0xA14);
    if (hidden & 2) alphamenu_show(kAlphaMenu);
    if (hidden & 1) multiwin_show(kMultiWin);
    buffer_dtor(g_buffer);
}

void ui_councilwin_redraw(uint8_t* win) { reinterpret_cast<Method0>(vmethod(win, 0xF8))(win); }

void ui_councilwin_panel_draw(uint8_t* win, int32_t i) {
    reinterpret_cast<Method0>(vmethod(panel(win, i), 8))(panel(win, i));
}

void ui_councilwin_button_draw(uint8_t* win) { reinterpret_cast<Method1>(vmethod(button5(win), 4))(button5(win), 0); }

void ui_councilwin_release(uint8_t* win) { reinterpret_cast<Method0>(vmethod(win, 0xE8))(win); }

void ui_councilwin_close_fonts(uint8_t* win) {
    for (int32_t i = 0; i < 6; i++) reinterpret_cast<Method0>(vmethod(panel(win, i), 0x168))(panel(win, i));
    for (uint32_t f : kFonts) font_close(win + f);
    do_all_draws();
}
#endif
