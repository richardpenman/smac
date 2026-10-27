// boom (battle 0x504AA0): the combat animation on the map windows that show the tile, with its
// sounds. Replaces the headless "boom" area (src/headless/ui_headless_boom.cpp).
//
// From the asm: the attacker's shot (flag 1: XI.PCX's row of the second unit's weapon, a psi
// attack row 8) or the explosion (XF.PCX: kind 3 big for native life, missiles' targets and flag
// 0x100's units, 2 for flag 0x80, else 1 when the second unit's reactor is above fission), 16
// frames scaled to the window's zoom, blended through the explosion tables (shots: the weapon's
// group, explosions 0 or 6), 25 ms apart (50 ms for flags 0x12; none with shift held, or with the
// quick battle preference 8 unless flags 0x12); the tile under the frames is restored from what
// it was, from frame 8 on for a destroyed unit (flag 2) without that unit (VehMovingID hidden
// while the tile is drawn again). The planet buster (flags 0x20 setup: buster.flc and the
// background before; 0x40: the background after the blast; 0x11 / 0x12: the two halves of the
// animation, 0x12 ending on the new background) is scaled by its size (PlanetBusterBoomSize) and
// tinted with the attacker's faction table. The end waits 50 ms (clock_wait, not with shift or
// quick battles). The sounds: a missile's launch (0x52), the explosion (0x3D big, 0x3E, 0x5A a
// spore launcher's), the weapon of the second unit (energy 1, 4, 0x49..0x59 by weapon; psi 0).
// Approximated: the buster's frames use the sprite blitter's scaling.
// Unattended runs (--hidden with scripted keys) skip the frames (their sounds and the game-state
// calls stay); SMACX_ANIM=1 draws them without waiting (SMACX_ANIM_SHOT=FILE: the screen after
// the middle frame, a test aid).
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "engine/battle.h"
#include "engine/compat.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/rules.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/flic.h"
#include "ui/gfx.h"
#include "ui/mapwin.h"
#include "ui/sound.h"
#include "ui/sprites.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

GLOBAL(int32_t, BoomActive, 0x915620);  // DAT_00915620: set while boom draws

namespace {

int32_t& mwi(uint8_t* win, uint32_t off) { return *reinterpret_cast<int32_t*>(win + off); }
// The unit of a vehicle; the original also reads Vehs[-1] here (no unit on the target tile): it
// lies in the Reactor table (rules_orig_byte), as in veh.cpp's veh_unit_raw.
int32_t veh_unit(int32_t veh) {
    if ((uint32_t)veh < 2049) return Vehs[veh].unit_id;
    COMPAT_ORIGINAL(out_of_range_reads);
    int32_t u = rules_orig_i16(0x952828 + (uint32_t)veh * 0x34 + offsetof(VEH, unit_id));
    return u;
}
const UNIT& unit_of(int32_t veh) { return Units[veh_unit(veh)]; }
// Its x (i 0) or y (1), Vehs[-1]'s from the Reactor table as above.
int32_t veh_pos(int32_t veh, int32_t i) {
    if ((uint32_t)veh < 2049) return i ? Vehs[veh].y : Vehs[veh].x;
    COMPAT_ORIGINAL(out_of_range_reads);
    return rules_orig_i16(0x952828 + (uint32_t)veh * 0x34 + (i ? 2 : 0));
}
const uint8_t* xtable(int32_t group) { return mem::host(0x6F1B7Cu + (uint32_t)group * 0x1000); }
Sprite* shot_sprite(int32_t row, int32_t frame) { return ArtSprite(0x7A9B30u + (uint32_t)(row * 16 + frame) * 0x2C); }
Sprite* boom_sprite(int32_t kind, int32_t frame) { return ArtSprite(0x7AB460u + (uint32_t)(kind * 16 + frame) * 0x2C); }

// The weapon's XI row and table group (0x66EB40, 0x66EBA8).
const int32_t kShotRow[26] = {0, 4, 0, 0, 4, 1, 5, 5, 3, 6, 2, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const int32_t kShotTable[26] = {3, 1, 0, 0, 2, 4, 5, 6, 5, 2, 7, 5, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// The buster's table group by faction, its scale (size 1..4).
const int32_t kFactionTable[8] = {0, 5, 2, 10, 9, 11, 0, 2};
const int32_t kBusterNum[5] = {2, 3, 4, 5, 5};
const int32_t kBusterDen[5] = {2, 2, 2, 2, 2};

// The saved backgrounds per window (DAT_00912428 / DAT_0090F7E0, 0x588 bytes each in the
// original): kept between the buster's calls.
struct WinSave {
    Buffer bg1, bg2;
    bool init = false;
} g_save[8];
Flc g_buster;
bool g_buster_ok = false;
int32_t g_buster_frame = 0;

int32_t zoom_scale(uint8_t* win, int32_t v) {
    return ZoomNum < 1 ? ((mwi(win, kMwZoom) + 0x10) * v) / 16 : (v * ZoomNum) / ZoomDenom;
}

void save_rect(Buffer* to, Buffer* from, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!to->data || to->width != w || to->height != h) {
        Buffer_ctor(to);
        Buffer_init(to, std::max(1, w), std::max(1, h), 8, 0);
    }
    UiRect all{0, 0, to->width, to->height};
    Buffer_set_clip(to, &all);
    Buffer_copy_4(from, to, x, y, 0, 0, w, h);
}

bool animate() { return !g_ui_unattended || std::getenv("SMACX_ANIM"); }

}  // namespace

int32_t GAMEAPI boom(int32_t x, int32_t y, int32_t flags) {
    sound_log("boom %d %d %d", x, y, flags);
    int32_t first = VehDrawAttackID, second = VehDrawDefendID;  // [ebp-0x1C], [ebp-0x14]
    int32_t anim = flags & 0x70;  // local_24
    int32_t psi_or_num = 0, row_or_den = 0, shot_table = 0;  // [ebp-0x10], [ebp-0xC], local_38
    if (!anim) {
        if (unit_of(first).chassis_id == 8 && (flags & 1)) {
            FX_play_loc(&Sounds, 0x52, veh_pos(first, 0), veh_pos(first, 1));
            return 0;
        }
        if (flags & 1) {
            if (unit_of(first).weapon_id == 0xF || unit_of(second).weapon_id == 0xF) psi_or_num = 1;
            if (veh_unit(second) == 1) FX_play(&Sounds, 1);
        }
        if (!psi_or_num && !(flags & 8)) {
            first = VehDrawDefendID;
            second = VehDrawAttackID;
        }
        if ((flags & 2) && second >= 0) {
            int32_t u = veh_unit(second);
            if (u == 8 || u == 9 || u == 10 || u == 0xE || u == 0xF || u == 0x13) flags |= 0x100;
        }
        if (flags & 1) {
            shot_table = 0;
            if (!psi_or_num) {
                if (first == -1) {
                    row_or_den = 0;
                } else {
                    int32_t w = unit_of(second).weapon_id;
                    row_or_den = kShotRow[(w >= 0 && w < 26) ? w : 0];
                    shot_table = kShotTable[(w >= 0 && w < 26) ? w : 0];
                }
            } else {
                row_or_den = 8;
                shot_table = 7;
            }
        }
    } else {
        if ((flags & 0x20) && animate()) {
            g_buster_ok = flc_load("buster.flc", &g_buster, nullptr) && !g_buster.frames.empty();
            g_buster_frame = 0;
            if (g_buster_ok) anim = 1;
        }
        int32_t size = std::max(0, std::min(4, PlanetBusterBoomSize));
        psi_or_num = kBusterNum[size];
        row_or_den = kBusterDen[size];
    }
    bool drawing = animate();
    if (drawing && ui_map_dirty()) ui_present();
    bool shown = false;
    UiRect rect[8];
    bool on[8] = {};
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* win = MapWins[i];
        if (!win || (i != 0 && !mwi(win, kMwActive))) continue;
        int32_t oy = mwi(win, kMwOriginY), ox = mwi(win, kMwOriginX);
        if (y < oy || y >= mwi(win, kMwRowsBelow) + mwi(win, kMwRowsAbove) + oy) continue;
        int32_t tx = x;
        if (!(MapToggleFlat & 1)) {
            if (x < ox) tx = MapAreaX + x;
            if (mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) + ox <= tx) tx -= MapAreaX;
        }
        if (tx < ox || tx >= mwi(win, kMwColsEven) + mwi(win, kMwColsOdd) + ox) continue;
        shown = true;
        on[i] = true;
        int32_t px = 0, py = 0;
        pfncTileToPoint(win, x, y, &px, &py);
        px += mwi(win, kMwTileW) / 2;
        py += mwi(win, kMwTileH);
        UiRect& r = rect[i];
        if (!anim) {
            Sprite* ref = (flags & 1) ? shot_sprite(0, 0) : boom_sprite(0, 0);
            int32_t sw = ref->full_width, sh = ref->full_height;
            px -= zoom_scale(win, sw / 2);
            py -= zoom_scale(win, sh);
            r = {px, py, px + zoom_scale(win, sw), py + zoom_scale(win, sh)};
            if (!(flags & 1)) {
                if (!(flags & 0x100)) {
                    if (veh_unit(second) == 0xF) FX_play_loc(&Sounds, 0x5A, veh_pos(second, 0), veh_pos(second, 1));
                    else if (unit_of(first).chassis_id != 8) FX_play_loc(&Sounds, 0x3E, x, y);
                } else {
                    FX_play_loc(&Sounds, 0x3D, x, y);
                }
            }
        } else {
            int32_t w = psi_or_num * 300 / row_or_den;
            int32_t ws = zoom_scale(win, w);
            px -= ws / 2;
            int32_t h = psi_or_num * 0xF8 / row_or_den;
            int32_t lift = psi_or_num * 0x3C / row_or_den;
            py = zoom_scale(win, lift) + (py - zoom_scale(win, h));
            r = {px, py, px + ws, py + zoom_scale(win, h)};
            if (!(flags & 0x60)) continue;
        }
        MapWinNative* nat = ui_mapwin_native(win);
        if (!drawing || !nat || !nat->canvas || !nat->canvas->data) continue;
        Buffer* canvas = nat->canvas;
        WinSave& sv = g_save[i];
        int32_t w = r.right - r.left, h = r.bottom - r.top;
        // The background before (bg1); a destroyed unit's tile without it (bg2); the buster's
        // background after the blast (0x40: bg2, the screen keeps bg1 until the animation ends).
        if (!(flags & 2) || (flags & 0x20)) {
            if (!(flags & 0x40)) {
                save_rect(&sv.bg1, canvas, r.left, r.top, w, h);
                continue;
            }
        } else if (!(flags & 0x40)) {
            save_rect(&sv.bg1, canvas, r.left, r.top, w, h);
            int32_t keep = VehMovingID;
            VehMovingID = (flags & 8) ? VehDrawDefendID : VehDrawAttackID;
            MapWin_draw_tile(win, x, y, 0);
            VehMovingID = keep;
        }
        save_rect(&sv.bg2, canvas, r.left, r.top, w, h);
        Buffer_copy_4(&sv.bg1, canvas, 0, 0, r.left, r.top, w, h);
    }
    if ((flags & 0x60) || !shown) return 0;
    bool shot = (flags & 1) != 0;
    BoomActive = 1;
    if (shot) {
        if (!psi_or_num) {
            if (unit_of(second).weapon_id == 0) FX_play(&Sounds, 1);
            static const int8_t kWeapon[15] = {-1,   4,    0x49, 0x4A, 0x59, 0x4C, 0x4D, 0x4E,
                                               0x4F, 0x50, 0x51, 0x58, 0x55, 0x56, 0x57};
            int32_t w = unit_of(second).weapon_id;
            if (w >= 1 && w <= 14) FX_play_loc(&Sounds, kWeapon[w], veh_pos(second, 0), veh_pos(second, 1));
            if (veh_unit(first) == 0xF) FX_play_loc(&Sounds, 0x5A, veh_pos(second, 0), veh_pos(second, 1));
        } else {
            FX_play(&Sounds, 0);
        }
    }
    if (drawing) {
        int32_t frames = anim == 0 ? 16 : (g_buster_ok ? (int32_t)g_buster.frames.size() / 2 : 0);
        uint32_t last = platform::ticks_ms();
        bool nowait = g_ui_unattended;
        const char* shot_file = std::getenv("SMACX_ANIM_SHOT");
        for (int32_t f = 0; f < frames; f++) {
            if (MultiplayerActive && shot && (f & 1)) continue;
            for (int32_t i = 0; i < 8; i++) {
                if (!on[i]) continue;
                uint8_t* win = MapWins[i];
                MapWinNative* nat = ui_mapwin_native(win);
                if (!nat || !nat->canvas || !nat->canvas->data) continue;
                if (ZoomNum < 1) {
                    BlitDestScale = mwi(win, kMwZoom) + 0x10;
                    BlitSourceScale = 0x10;
                } else {
                    BlitDestScale = ZoomNum;
                    BlitSourceScale = ZoomDenom;
                }
                if (!anim) {
                    Sprite* s;
                    int32_t table;
                    if (!shot) {
                        int32_t kind = (flags & 0x100)  ? 3
                                       : (flags & 0x80) ? 2
                                                        : (Units[veh_unit(second)].reactor_id > 1 ? 1 : 0);
                        s = boom_sprite(kind, f);
                        table = kind == 3 ? 6 : 0;
                    } else {
                        s = shot_sprite(row_or_den, f);
                        table = shot_table;
                    }
                    Sprite_draw_multi_table_dest_2(s, nat->canvas, s->key, rect[i].left, rect[i].top, xtable(table));
                } else {
                    // FUN_0062A070: the buster's next frame, scaled by its size.
                    Buffer fb;
                    Buffer_ctor(&fb);
                    Buffer_init(&fb, g_buster.width, g_buster.height, 8, 0);
                    const std::vector<uint8_t>& px =
                        g_buster.frames[(size_t)(g_buster_frame % (int32_t)g_buster.frames.size())];
                    for (int32_t yy = 0; yy < g_buster.height; yy++)
                        std::copy(px.begin() + (size_t)yy * g_buster.width,
                                  px.begin() + (size_t)(yy + 1) * g_buster.width, fb.data + (size_t)yy * fb.stride);
                    g_buster_frame++;
                    int32_t fac = first >= 0 ? Vehs[first].faction_id : 0;
                    Buffer_draw_multi_table_dest_2(&fb, nat->canvas, 0xFF, rect[i].left, rect[i].top,
                                                   (mwi(win, kMwZoom) + 0x10) * psi_or_num, row_or_den << 4,
                                                   xtable(kFactionTable[fac & 7]));
                }
                ui_present();  // GraphicWin_update_2
                if (shot_file && f == frames / 2 && i == 0) ui_screenshot(g_ui_outdir + "/" + shot_file);
                if (MultiplayerActive) break;
            }
            bool fast = (ui_key_state(VKEY_SHIFT) & 0x8000) ||
                        ((GamePreferences & PREF_ADV_FAST_BATTLE_RESOLUTION) && !(flags & 0x12)) || MultiplayerActive;
            int32_t delay = fast ? 0 : ((flags & 0x12) ? 0x32 : 0x19);
            if (!nowait) {
                uint32_t now;
                do {
                    now = platform::ticks_ms();
                    do_non_input();
                } while ((int32_t)(now - last) < delay);
                last = now;
            }
            for (int32_t i = 0; i < 8; i++) {
                if (!on[i]) continue;
                MapWinNative* nat = ui_mapwin_native(MapWins[i]);
                if (!nat || !nat->canvas || !nat->canvas->data) continue;
                WinSave& sv = g_save[i];
                bool second_bg = anim ? !shot : ((flags & 2) && f > 7);
                Buffer* from = second_bg ? &sv.bg2 : &sv.bg1;
                if (from->data)
                    Buffer_copy_4(from, nat->canvas, 0, 0, rect[i].left, rect[i].top, from->width, from->height);
                if (MultiplayerActive) break;
            }
        }
        BlitDestScale = BlitSourceScale = 1;
        if (flags != 0x11) ui_present();
        std::fprintf(stderr, "boom anim: flags %X frames %d at %d,%d\n", flags, frames, rect[0].left, rect[0].top);
    }
    if (anim && (flags & 2)) g_buster_ok = false;  // the buster's animation closed
    if (!(ui_key_state(VKEY_SHIFT) & 0x8000) && !(GamePreferences & PREF_ADV_FAST_BATTLE_RESOLUTION) &&
        !MultiplayerActive)
        clock_wait(0x32);
    BoomActive = 0;
    return 1;
}
