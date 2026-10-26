// slide_show / slide_hide (ui 0x45D440 / 0x45D970): a full-screen window slides up into view
// between the interface's arms (and down out of view), with fx\main menu.wav; the console part
// (flag 2: the centre pane slid down) is mi_slide (battlewin.cpp).
//
// From the asm: the sound (MainInterface +0x25098, fx\main menu.wav at volume 0x23) when the
// interface sounds preference (0x400) is on, faded after the show; the animation with the
// "animate interface" preference (0x8000000) outside multiplayer: 5 frames 20 ms apart
// (DAT_00691AF8; slide_hide: 3 during the computer players' turns), the window's picture rising
// from the bottom by a fifth of its height per frame (slide_hide: sinking, its side strips of
// 0x17 pixels cleared) with the four arms of console_x2_A.pcx (0x2C9, 0x2D9, 0x2E2, 0x2EB: 15,
// 8, 8, 15 x 0x211) along its sides, the outer ones not above y 0xEE (0x5E at 800); at the end
// slide_show draws the arms into the window's canvas.
// Unattended runs (--hidden with scripted keys) skip the frames (the sound and the arms stay).
#include <cstring>
#include <vector>

#include "engine/globals.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/gfx.h"
#include "ui/slide.h"
#include "ui/sound.h"
#include "ui/win.h"

namespace {

Sprite g_arm[4];
bool g_loaded = false;
uint8_t* main_menu_wave() { return mem::host(0x7D38B8); }

void load() {
    if (g_loaded) return;
    g_loaded = true;
    Buffer b;
    Buffer_ctor(&b);
    bool small = ScreenWidth == 800;
    if (!Buffer_load_pcx(&b, small ? "console_x_A.pcx" : "console_x2_A.pcx", nullptr, 10, 0xEC)) {
        static const int32_t kX[2][4] = {{0x2C9, 0x2D9, 0x2E2, 0x2EB}, {0x2AB, 699, 0x2C4, 0x2CD}};
        static const int32_t kW[4] = {0xF, 8, 8, 0xF};
        for (int32_t i = 0; i < 4; i++) pfncSpriteFromCanvasRectTrans(&g_arm[i], &b, 9, kX[small][i], 1, kW[i], 0x211, 0);
    }
    Wave_init(main_menu_wave(), "fx\\main menu.wav", 4);
    Wave_set_volume(main_menu_wave(), 0x23);
}

bool animate() {
    return (GamePreferences & 0x8000000) && !MultiplayerActive && !g_ui_exit_when_script_done;
}

int32_t outer_top() { return ScreenWidth != 800 ? 0xEE : 0x5E; }

// The arms' x positions in the window (0: its left edge) for a window of width w.
void arm_x(int32_t w, int32_t* x) {
    x[0] = 0;
    x[1] = g_arm[0].full_width;
    x[2] = (w - g_arm[3].full_width - g_arm[2].full_width) - 0x20;
    x[3] = (w - g_arm[3].full_width) - 0x20;
}

// One frame: the screen as it was, the window's picture at (sx, y), the arms beside it.
void frame(const std::vector<uint8_t>& screen, Buffer* scr, const Buffer* win, int32_t sx, int32_t y) {
    std::memcpy(scr->data, screen.data(), screen.size());
    int32_t w = win->width;
    for (int32_t j = 0; j < win->height; j++) {
        int32_t dy = y + j;
        if (dy < 0 || dy >= scr->height) continue;
        const uint8_t* s = win->data + (size_t)j * win->stride;
        uint8_t* d = scr->data + (size_t)dy * scr->stride;
        for (int32_t i = 0; i < w; i++) {
            int32_t dx = sx + i;
            if (dx >= 0 && dx < scr->width && s[i] != 9) d[dx] = s[i];
        }
    }
    int32_t ax[4];
    arm_x(w, ax);
    UiRect all{0, 0, scr->width, scr->height};
    Buffer_set_clip(scr, &all);
    BlitDestScale = BlitSourceScale = 1;
    for (int32_t k = 0; k < 4; k++) {
        int32_t ay = (k == 0 || k == 3) ? std::max(y, outer_top()) : y;
        if (g_arm[k].width) pfncSpriteStretchCopyToCanvas(&g_arm[k], scr, 9, sx + ax[k], ay);
    }
    platform::video_present(scr->data, scr->stride, scr->width, scr->height,
                            reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
}

void wait_frame(uint32_t start) {
    while ((int32_t)(platform::ticks_ms() - start) < *reinterpret_cast<int32_t*>(mem::host(0x691AF8))) platform::delay_ms(1);
}

}  // namespace

void slide_show_window(Buffer* win, int32_t sx, int32_t sy, std::vector<uint8_t>* back, bool draw_arms) {
    load();
    if (GamePreferences & 0x400) Wave_play_2(main_menu_wave());
    Buffer* scr = ui_screen_canvas();
    if (animate() && scr && scr->data && win->data) {
        ui_present();
        std::vector<uint8_t> screen(scr->data, scr->data + (size_t)scr->stride * scr->height);
        int32_t frames = 5, h = win->height;
        int32_t step = h / frames;
        if (sy < sy + h - step * frames) step++;
        int32_t y = sy + h - step;
        for (int32_t f = 0; f < frames; f++) {
            uint32_t t = platform::ticks_ms();
            frame(screen, scr, win, sx, y);
            y -= step;
            if (y <= sy) y = sy;
            wait_frame(t);
        }
        std::memcpy(scr->data, screen.data(), screen.size());
    }
    if (GamePreferences & 0x400) Sound_fade(main_menu_wave());
    if (!draw_arms) return;
    // The arms in the window's canvas (and its background copy).
    int32_t ax[4];
    arm_x(win->width, ax);
    UiRect all{0, 0, win->width, win->height};
    Buffer_set_clip(win, &all);
    BlitDestScale = BlitSourceScale = 1;
    int32_t outer = std::max(0, outer_top() - sy);
    for (int32_t k = 0; k < 4; k++) {
        if (!g_arm[k].width) continue;
        pfncSpriteStretchCopyToCanvas(&g_arm[k], win, 9, ax[k], (k == 0 || k == 3) ? outer : 0);
    }
    if (back && back->size() == (size_t)win->stride * win->height) std::memcpy(back->data(), win->data, back->size());
    ui_invalidate();
}

void slide_hide_window(const Buffer* win, int32_t sx, int32_t sy) {
    load();
    Buffer* scr = ui_screen_canvas();
    if (!animate() || !scr || !scr->data || !win->data) return;
    if (GamePreferences & 0x400) Wave_play_2(main_menu_wave());
    // The window's picture without its side strips over the screen without the window.
    Buffer pic;
    Buffer_ctor(&pic);
    Buffer_init(&pic, win->width, win->height, 8, 0);
    for (int32_t j = 0; j < win->height; j++) std::memcpy(pic.data + (size_t)j * pic.stride, win->data + (size_t)j * win->stride, (size_t)win->width);
    UiRect l{0, 0, 0x17, pic.height}, r{pic.width - 0x17, 0, pic.width, pic.height};
    Buffer_fill_3(&pic, &l, 9);
    Buffer_fill_3(&pic, &r, 9);
    ui_present();
    std::vector<uint8_t> screen(scr->data, scr->data + (size_t)scr->stride * scr->height);
    int32_t frames = (MultiplayerActive || ControlUpkeepA || ControlTurnC) ? 3 : 5;
    int32_t step = (scr->height - sy) / frames + 1;
    int32_t y = sy;
    for (int32_t f = 0; f < frames; f++) {
        uint32_t t = platform::ticks_ms();
        y += step;
        frame(screen, scr, &pic, sx, y);
        wait_frame(t);
    }
    std::memcpy(scr->data, screen.data(), screen.size());
    ui_invalidate();
}
