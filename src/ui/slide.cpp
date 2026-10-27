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
#include "ui/basewin.h"
#include "ui/popup.h"
#include <algorithm>

#include <cstdio>
#include "ui/sound.h"
#include "ui/win.h"

namespace {

Sprite g_arm[4];
Sprite g_rail[4];  // 0x7CFF14 top cap, 0x7CFF40 middle, 0x7CFF6C bottom cap, 0x7CFF98 below
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
        for (int32_t i = 0; i < 4; i++)
            pfncSpriteFromCanvasRectTrans(&g_arm[i], &b, 9, kX[small][i], 1, kW[i], 0x211, 0);
        // MainInterface_init's scroll rail (+0x216F4 .. +0x21778): the top cap, the middle (tiled),
        // the bottom cap and the part below it (tiled), 0x20 wide, at x 1 of the same art.
        static const int32_t kRailY[4] = {0x117, 0x132, 0x155, 0x16F}, kRailH[4] = {0x1A, 0x22, 0x19, 0x22};
        for (int32_t i = 0; i < 4; i++)
            pfncSpriteFromCanvasRectTrans(&g_rail[i], &b, 9, 1, kRailY[i], 0x20, kRailH[i], 0);
    }
    Wave_init(main_menu_wave(), "fx\\main menu.wav", 4);
    Wave_set_volume(main_menu_wave(), 0x23);
}

bool animate() {
    return (GamePreferences & PREF_AV_SLIDING_WINDOWS) && !MultiplayerActive && !g_ui_unattended;
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
    while ((int32_t)(platform::ticks_ms() - start) < *reinterpret_cast<int32_t*>(mem::host(0x691AF8)))
        platform::delay_ms(1);
}

}  // namespace

void slide_show_window(Buffer* win, int32_t sx, int32_t sy, std::vector<uint8_t>* back, bool draw_arms) {
    load();
    std::fprintf(stderr, "slide show %dx%d at %d,%d\n", win->width, win->height, sx, sy);
    if (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) Wave_play_2(main_menu_wave());
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
    if (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) Sound_fade(main_menu_wave());
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
    std::fprintf(stderr, "slide hide %dx%d at %d,%d\n", win->width, win->height, sx, sy);
    Buffer* scr = ui_screen_canvas();
    if (!animate() || !scr || !scr->data || !win->data) return;
    if (GamePreferences & PREF_AV_VOLUME_SFX_TOGGLE) Wave_play_2(main_menu_wave());
    // The window's picture without its side strips over the screen without the window.
    Buffer pic;
    Buffer_ctor(&pic);
    Buffer_init(&pic, win->width, win->height, 8, 0);
    for (int32_t j = 0; j < win->height; j++)
        std::memcpy(pic.data + (size_t)j * pic.stride, win->data + (size_t)j * win->stride, (size_t)win->width);
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

// ---- scroll_show / scroll_hide (0x45DEA0 / 0x45E540) ----

namespace {
// The rail's length for a strip of height h (esi of scroll_show): the caps and as many middle
// pieces as fit with 0x2C to spare.
int32_t rail_length(int32_t h) {
    int32_t a = g_rail[0].full_height, b = g_rail[1].full_height, c = g_rail[2].full_height;
    if (b <= 0) return 0;
    return a + c - ((c + a + 0x2C - h) / b) * b;  // (C + A + 0x2C - H) / B, truncated
}
// The strip scroll_show builds (screen T..B; S its top): the rail at its left (from y 3), the
// "below" art under it, the outer right arm at its right edge; colour 9 elsewhere.
void build_strip(Buffer* strip, int32_t h) {
    const Sprite& arm = g_arm[3];
    int32_t w = arm.full_width + 0x20;
    Buffer_ctor(strip);
    Buffer_init(strip, w, h, 8, 0);
    Buffer_fill_2(strip, 9);
    int32_t len = rail_length(h);
    BlitDestScale = BlitSourceScale = 1;
    if (g_rail[1].width) Buffer_tile_2(strip, &g_rail[1], 0, 0, 0, 3, g_rail[1].full_width, len);
    if (g_rail[3].width) Buffer_tile_2(strip, &g_rail[3], 0, len + 3, 0, len, g_rail[3].full_width, h - len);
    if (g_rail[0].width) pfncSpriteStretchCopyToCanvas(&g_rail[0], strip, g_rail[0].key, 0, 3);
    if (g_rail[2].width)
        pfncSpriteStretchCopyToCanvas(&g_rail[2], strip, g_rail[2].key, 0, len - g_rail[2].full_height + 3);
    if (arm.width) pfncSpriteStretchCopyToCanvas(const_cast<Sprite*>(&arm), strip, arm.key, w - arm.full_width, 0);
}
void blit_key9(Buffer* dst, const Buffer* src, int32_t x, int32_t y, const UiRect& clip) {
    for (int32_t j = 0; j < src->height; j++) {
        int32_t dy = y + j;
        if (dy < clip.top || dy >= clip.bottom || dy < 0 || dy >= dst->height) continue;
        for (int32_t i = 0; i < src->width; i++) {
            int32_t dx = x + i;
            uint8_t v = src->data[(size_t)j * src->stride + i];
            if (v != 9 && dx >= clip.left && dx < clip.right && dx >= 0 && dx < dst->width)
                dst->data[(size_t)dy * dst->stride + dx] = v;
        }
    }
}
void present(Buffer* scr) {
    platform::video_present(scr->data, scr->stride, scr->width, scr->height,
                            reinterpret_cast<const platform::Rgb*>(GamePalette.rgb));
}
// The arm's frames run 10 ms apart (DAT_00691AF8 / 2).
void wait_half(uint32_t start) {
    while ((int32_t)(platform::ticks_ms() - start) < *reinterpret_cast<int32_t*>(mem::host(0x691AF8)) / 2)
        platform::delay_ms(1);
}
}  // namespace

int32_t scroll_bar_height(int32_t h) {
    load();
    return rail_length(h - 0xD) - 10;
}

void scroll_show_window(Buffer* win, int32_t sx, int32_t sy) {
    load();
    const Sprite& arm = g_arm[3];
    const int32_t start = ScreenWidth != 800 ? 0xEE : 0x5E;
    const int32_t T = sy, R = sx + win->width, B = sy + win->height;
    const int32_t top0 = std::max(start, T), S = std::min(T + 0xD, top0);
    const int32_t armx = R - arm.full_width - 0x20, W = arm.full_width + 0x20;
    std::fprintf(stderr, "scroll show %dx%d at %d,%d: rail %d\n", win->width, win->height, sx, sy, rail_length(B - S));
    Buffer strip;
    build_strip(&strip, B - S);
    Buffer* scr = ui_screen_canvas();
    if (animate() && scr && scr->data && strip.data) {
        ui_present();
        std::vector<uint8_t> screen(scr->data, scr->data + (size_t)scr->stride * scr->height);
        auto restore = [&]() { std::memcpy(scr->data, screen.data(), screen.size()); };
        // The arm rises from its rest (y 0xEE, 0x5E at 800) to the strip's top in 6 frames (3 in
        // multiplayer), behind the main interface (visible above its top + 0x39).
        const int32_t n = MultiplayerActive ? 3 : 6;
        const int32_t step = (top0 - S) / n;
        *reinterpret_cast<int32_t*>(mem::host(0x7D3924)) = step;
        if (step > 1 && (GameMorePreferences & MPREF_AV_SLIDING_SCROLLBARS)) {
            UiRect clip{armx, 0, R, mi_rect(kMiPos).top + 0x39};
            int32_t y = top0 - step;
            for (int32_t f = 0; f < n; f++) {
                uint32_t t = platform::ticks_ms();
                restore();
                Buffer a;
                Buffer_ctor(&a);
                Buffer_init(&a, arm.full_width, arm.full_height, 8, 0);
                Buffer_fill_2(&a, 9);
                BlitDestScale = BlitSourceScale = 1;
                pfncSpriteStretchCopyToCanvas(const_cast<Sprite*>(&arm), &a, arm.key, 0, 0);
                blit_key9(scr, &a, armx, std::max(y, S), clip);
                present(scr);
                y -= step;
                wait_half(t);
            }
        }
        // The rail slides in from the left of its place, 4 pixels a frame (8 in multiplayer), 8
        // frames (4), clipped to its place.
        if (GamePreferences & PREF_AV_SLIDING_WINDOWS) {
            const int32_t d = MultiplayerActive ? 8 : 4, frames = MultiplayerActive ? 4 : 8;
            UiRect clip{armx, S, R, B};
            int32_t x = d + armx - 0x20;
            for (int32_t f = 0; f < frames; f++) {
                uint32_t t = platform::ticks_ms();
                restore();
                blit_key9(scr, &strip, x, S, clip);
                present(scr);
                x += d;
                wait_half(t);
            }
        }
        restore();
    }
    // The strip into the window at its right edge (the scroll bar goes on the rail).
    UiRect all{0, 0, win->width, win->height};
    if (strip.data) blit_key9(win, &strip, win->width - W, S - T, all);
}

void scroll_rail_draw(Buffer* win, int32_t sy) {
    load();
    const Sprite& arm = g_arm[3];
    const int32_t start = ScreenWidth != 800 ? 0xEE : 0x5E;
    const int32_t T = sy, B = sy + win->height, S = std::min(T + 0xD, std::max(start, T));
    Buffer strip;
    build_strip(&strip, B - S);
    UiRect all{0, 0, win->width, win->height};
    if (strip.data) blit_key9(win, &strip, win->width - (arm.full_width + 0x20), S - T, all);
}

void scroll_hide_window(Buffer* win, int32_t sx, int32_t sy) {
    load();
    const Sprite& arm = g_arm[3];
    const int32_t start = ScreenWidth != 800 ? 0xEE : 0x5E;
    const int32_t T = sy, R = sx + win->width, B = sy + win->height;
    const int32_t top0 = std::max(start, T), S = std::min(T + 0xD, top0);
    const int32_t armx = R - arm.full_width - 0x20, W = arm.full_width + 0x20;
    std::fprintf(stderr, "scroll hide %dx%d at %d,%d\n", win->width, win->height, sx, sy);
    Buffer* scr = ui_screen_canvas();
    if (animate() && scr && scr->data && win->data && (GameMorePreferences & MPREF_AV_SLIDING_SCROLLBARS)) {
        // The window's right strip slides left out of view, then the arm sinks to its rest.
        Buffer strip;
        Buffer_ctor(&strip);
        Buffer_init(&strip, W, win->height, 8, 0);
        for (int32_t j = 0; j < win->height; j++)
            std::memcpy(strip.data + (size_t)j * strip.stride, win->data + (size_t)j * win->stride + (win->width - W),
                        (size_t)W);
        ui_present();
        std::vector<uint8_t> screen(scr->data, scr->data + (size_t)scr->stride * scr->height);
        const int32_t d = MultiplayerActive ? 8 : 4;
        UiRect clip{armx, T, R, B};
        int32_t x = armx - d;
        for (int32_t f = 0; f < 8; f++) {
            uint32_t t = platform::ticks_ms();
            std::memcpy(scr->data, screen.data(), screen.size());
            blit_key9(scr, &strip, x, T, clip);
            present(scr);
            x -= d;
            wait_half(t);
        }
        std::memcpy(scr->data, screen.data(), screen.size());
        ui_invalidate();
    }
    // The strip's place in the window cleared, the arm at its rest.
    UiRect r{win->width - W, 0, win->width, win->height};
    Buffer_fill_3(win, &r, 9);
    (void)S;
    if (arm.width) {
        BlitDestScale = BlitSourceScale = 1;
        Buffer_set_clip(win, &r);
        pfncSpriteStretchCopyToCanvas(const_cast<Sprite*>(&arm), win, arm.key, win->width - arm.full_width - 0x20,
                                      top0 - T);
        UiRect all{0, 0, win->width, win->height};
        Buffer_set_clip(win, &all);
    }
}
