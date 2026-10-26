// veh_scoot (0x55A150, filed in the diplomacy module): the unit's move on the map windows: it turns
// to its direction (vehdraw_turn_start / vehdraw_turn) and slides to the next tile
// (vehdraw_scoot_start / vehdraw_scoot), with fx\scoot.wav. Replaces the headless "scoot" area
// (src/headless/ui_headless_scoot.cpp).
//
// From the asm: dir above 7 (taken mod 8) skips the final drawing; flags 0x4000 only set the
// facing (no animation); flags 1 turn without sliding. The engine has lifted the unit
// (veh_lift) so the tiles drawn first show the map without it. Per window showing either tile:
// the two tile points (minus the half tile height), a background rectangle covering both, saved
// and restored around each frame. Turning: 25 / 16 / 8 frames a whole turn (UnitAnimMode smooth /
// normal / fast), each half the slide delay; sliding: 9 / 6 / 3 frames (multiplayer: the own
// unit's 4, no delay) of 16 ms (10 in multiplayer; sea units 2/3, air units 1/3), the unit drawn
// at the source point plus (frame + 1) steps of the way. At the end the unit is drawn on the
// next tile (VehScootX / Y there) and the input flushed.
// Approximated: the background rectangle (MapWin_compute_clip is not reconstructed: the tiles'
// rectangles with a quarter tile around and the unit's height above); the redraw before the
// animation is a full one (draw_tile's partial redraws are not reconstructed).
// Unattended runs (--hidden with scripted keys) skip the frames; SMACX_ANIM=1 draws them without
// waiting (SMACX_SCOOT_SHOT=FILE: the screen at the half way of a slide, a test aid). The facing (the
// only game state) is set in every case.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/sound.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

namespace {

int32_t& mwi(uint8_t* win, uint32_t off) { return *reinterpret_cast<int32_t*>(win + off); }

bool tile_in_window(uint8_t* win, int32_t x, int32_t y) {
    int32_t oy = mwi(win, kMwOriginY), ox = mwi(win, kMwOriginX);
    if (y < oy || y >= mwi(win, kMwRowsBelow) + mwi(win, kMwRowsAbove) + oy) return false;
    int32_t tx = x;
    if (!(MapToggleFlat & 1)) {
        if (x < ox) tx = MapAreaX + x;
        if (mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) + ox <= tx) tx -= MapAreaX;
    }
    return tx >= ox && tx < mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) + ox;
}

bool animate() { return !g_ui_exit_when_script_done || std::getenv("SMACX_ANIM"); }

void wait_ms(uint32_t* last, int32_t ms) {
    if (g_ui_exit_when_script_done) return;
    uint32_t now;
    do {
        do_non_input();
        now = platform::ticks_ms();
    } while ((int32_t)(now - *last) < ms);
    *last = now;
}

struct WinMove {
    bool on = false;
    int32_t sx = 0, sy = 0, dx = 0, dy = 0;  // tile points (minus the half tile height)
    UiRect r{};                               // the saved background
    Buffer bg;
};

}  // namespace

void GAMEAPI veh_scoot(int32_t veh_id, int32_t x, int32_t y, int32_t dir, int32_t flags) {
    VehScootY = y;
    VehScootX = x;
    VehScootID = veh_id;
    bool no_final = false;
    if (dir > 7) {
        dir %= 8;
        no_final = true;
    }
    Wave_play_2(WaveScoot());
    int32_t x2 = NearOffsetX[dir] + x, y2 = NearOffsetY[dir] + y;
    if (!(MapToggleFlat & 1)) {
        if (x2 < 0) x2 += MapAreaX;
        else if (x2 >= MapAreaX) x2 -= MapAreaX;
    }
    bool drawing = animate();
    WinMove mv[8];
    if (!(flags & 0x4000)) {
        int32_t delay = MultiplayerActive ? 10 : 0x10;
        int32_t triad = Chassis[Units[Vehs[veh_id].unit_id].chassis_id].triad;
        if (triad == 1) delay = delay * 2 / 3;
        if (triad == 2) delay /= 3;
        int32_t frames = UnitAnimMode < 0 ? 9 : (UnitAnimMode ? 3 : 6);
        if (MultiplayerActive) {
            if (Vehs[veh_id].faction_id == CurrentPlayerFaction) {
                delay = 0;
                frames = 4;
            }
            Vehs[veh_id].rotate_angle = (int8_t)dir;
        }
        if (drawing) {
            draw_tile(x, y, 0);  // the tiles without the lifted unit
            draw_tile(x2, y2, 0);
            if (ui_map_dirty()) ui_present();
            for (int32_t i = 0; i < 8; i++) {
                uint8_t* win = MapWins[i];
                if (!win || (i != 0 && !mwi(win, kMwActive))) continue;
                if (!tile_in_window(win, x, y) && !tile_in_window(win, x2, y2)) continue;
                MapWinNative* nat = ui_mapwin_native(win);
                if (!nat || !nat->canvas || !nat->canvas->data) continue;
                WinMove& m = mv[i];
                m.on = true;
                pfncTileToPoint(win, x, y, &m.sx, &m.sy);
                pfncTileToPoint(win, x2, y2, &m.dx, &m.dy);
                m.sy -= mwi(win, kMwHalfH);
                m.dy -= mwi(win, kMwHalfH);
                int32_t tw = mwi(win, kMwTileW), th = mwi(win, kMwTileH), hh = mwi(win, kMwHalfH);
                UiRect r{std::min(m.sx, m.dx), std::min(m.sy, m.dy), std::max(m.sx, m.dx) + tw, std::max(m.sy, m.dy) + th + hh};
                int32_t qw = (r.right - r.left) / 4, qh = (r.bottom - r.top) / 4;
                r = {r.left - qw, r.top - qh - th, r.right + qw, r.bottom + qh};
                m.r = r;
                Buffer_ctor(&m.bg);
                Buffer_init(&m.bg, r.right - r.left, r.bottom - r.top, 8, 0);
                Buffer_copy_4(nat->canvas, &m.bg, r.left, r.top, 0, 0, r.right - r.left, r.bottom - r.top);
            }
        }
        int32_t per_turn = UnitAnimMode < 0 ? 0x19 : (UnitAnimMode ? 8 : 0x10);
        int32_t turn = 0;
        if (drawing) turn = vehdraw_turn_start(veh_id, dir, per_turn, 2);
        else Vehs[veh_id].rotate_angle = (int8_t)dir;  // (vehdraw_turn_start's facing)
        uint32_t last = platform::ticks_ms();
        auto frame = [&](int32_t mode, int32_t f, bool slide, float t) {
            for (int32_t i = 0; i < 8; i++) {
                WinMove& m = mv[i];
                if (!m.on) continue;
                uint8_t* win = MapWins[i];
                Buffer* c = ui_mapwin_native(win)->canvas;
                int32_t px = m.sx, py = m.sy;
                if (slide) {
                    px = (int32_t)((float)m.sx + (float)(m.dx - m.sx) * t);
                    py = (int32_t)((float)m.sy + (float)(m.dy - m.sy) * t);
                }
                veh_draw(c, veh_id, px, py, mwi(win, kMwZoom), mode, f);
                ui_present();  // GraphicWin_update_2
                Buffer_copy_4(&m.bg, c, 0, 0, m.r.left, m.r.top, m.bg.width, m.bg.height);
            }
        };
        if (drawing)
            for (int32_t f = 0; f < turn; f++) {
                frame(0x12, f, false, 0.0f);
                wait_ms(&last, delay / 2);
            }
        if (!(flags & 1)) {
            if (drawing) {
                vehdraw_scoot_start(veh_id, dir, frames, 2);
                for (int32_t f = 0; f < frames; f++) {
                    frame(0x22, f, true, (float)(f + 1) * (1.0f / (float)frames));
                    const char* shot = std::getenv("SMACX_SCOOT_SHOT");
                    if (shot && *shot && f == frames / 2) {  // test aid: the screen at the half way
                        for (int32_t i = 0; i < 8; i++)
                            if (mv[i].on) veh_draw(ui_mapwin_native(MapWins[i])->canvas, veh_id,
                                                   (mv[i].sx + mv[i].dx) / 2, (mv[i].sy + mv[i].dy) / 2,
                                                   mwi(MapWins[i], kMwZoom), 0x22, f);
                        ui_screenshot(g_ui_outdir + "/" + shot);
                        for (int32_t i = 0; i < 8; i++)
                            if (mv[i].on)
                                Buffer_copy_4(&mv[i].bg, ui_mapwin_native(MapWins[i])->canvas, 0, 0, mv[i].r.left,
                                              mv[i].r.top, mv[i].bg.width, mv[i].bg.height);
                    }
                    wait_ms(&last, delay);
                }
            }
        } else {
            no_final = true;  // (the original's path skips to the end)
        }
        if (drawing && std::getenv("SMACX_ANIM")) std::fprintf(stderr, "scoot anim: veh %d dir %d turn %d slide %d\n", veh_id, dir, turn, (flags & 1) ? 0 : frames);
    } else {
        if (drawing) {
            draw_tile(x, y, 0);
            draw_tile(x2, y2, 0);
        }
        Vehs[veh_id].rotate_angle = (int8_t)dir;
    }
    if (!no_final) {
        VehScootX = x2;
        VehScootY = y2;
        if (drawing)
            for (int32_t i = 0; i < 8; i++) {
                if (!mv[i].on) continue;
                uint8_t* win = MapWins[i];
                veh_draw(ui_mapwin_native(win)->canvas, veh_id, mv[i].dx, mv[i].dy, mwi(win, kMwZoom), 2, 0);
                ui_present();
            }
    }
    VehScootX = -1;
    VehScootY = -1;
    flush_input();
}
