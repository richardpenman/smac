// Base window (basewin.h): the window object, its art and layout (BaseWin_init), opening and
// closing (BaseWin_zoom, BaseWin_exit, next / previous base), the buttons (BaseButton /
// FlatButton), the clickable spots, input (mouse, the console's base window keys) and the
// composition of its canvases on the screen. The panes are drawn by basewin_draw.cpp, the commands
// that change the base are in basewin_cmd.cpp.
#include "ui/tutwin.h"
#include "ui/slide.h"
#include "ui/basewin.h"
#include "ui/sound.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/goody.h"
#include "engine/net.h"
#include "engine/map.h"
#include "engine/action.h"
#include "engine/diplomacy.h"
#include "engine/random.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/sprites.h"
#include "ui/win.h"

BaseWinNative g_bw;

// ==== helpers ====================================================================================

void sprite_copy1(Sprite* s, Buffer* dest, int32_t key, int32_t x, int32_t y, int32_t ds, int32_t ss) {
    if (!s || !s->width) return;
    int32_t od = BlitDestScale, os = BlitSourceScale;
    BlitDestScale = ds;
    BlitSourceScale = ss;
    pfncSpriteStretchCopyToCanvas(s, dest, key & 0xFF, x, y);
    BlitDestScale = od;
    BlitSourceScale = os;
}

bool in_box(int32_t x, int32_t y, const UiRect& r) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

// The window's own copy of a label (Strings_get of TextLabels[off / 4]) into StrBuffer, upper-cased
// as CharUpperA does in the C locale.
const char* label_upper(uint32_t off) {
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get((int32_t)(off / 4)));
    for (char* p = StrBuffer; *p; p++) *p = (char)std::toupper((uint8_t)*p);
    return StrBuffer;
}

// ---- Spot (gfx2 0x5FA8A0..): clickable rectangles with an index and a type ----
void spot_init(Spots* s, int32_t capacity) {
    s->list.clear();
    s->capacity = capacity;
}
int32_t spot_add(Spots* s, int32_t index, int32_t type, int32_t x, int32_t y, int32_t w, int32_t h) {
    if ((int32_t)s->list.size() >= s->capacity) return -1;
    s->list.push_back({{x, y, x + w, y + h}, index, type});
    return (int32_t)s->list.size() - 1;
}
int32_t spot_check(Spots* s, int32_t x, int32_t y, int32_t* index, int32_t* type) {
    for (int32_t i = (int32_t)s->list.size() - 1; i >= 0; i--) {
        const Spot& p = s->list[(size_t)i];
        if (p.r.left <= x && x < p.r.right && p.r.top <= y && y < p.r.bottom) {
            if (index) *index = p.index;
            if (type) *type = p.type;
            return i;
        }
    }
    return -1;
}
void spot_kill_type(Spots* s, int32_t type) {
    for (int32_t i = (int32_t)s->list.size() - 1; i >= 0; i--)
        if (s->list[(size_t)i].type == type) s->list.erase(s->list.begin() + i);
}

// ---- the iface mode stack (SubInterface_set_iface_mode 0x45D310 / release 0x45D380) ----
// The main interface's sub-interfaces (InfcList / InfcUsed): the base window is mode 3. The other
// interfaces' status calls are not reconstructed (only the base window's).
void iface_set_mode(int32_t mode) {
    int32_t cur = InfcList[InfcUsed];
    if (cur == mode || (mode == 10 && cur != 0)) return;
    InfcUsed++;
    InfcList[InfcUsed] = mode;
}
void iface_release_mode(int32_t mode) {
    if (!InfcUsed) return;
    if (InfcList[InfcUsed] == mode) {
        InfcUsed--;
        return;
    }
    for (int32_t i = InfcUsed - 1; i > 0; i--) {
        if (InfcList[i] != mode) continue;
        for (int32_t j = i; j < InfcUsed; j++) InfcList[j] = InfcList[j + 1];
        InfcUsed--;
    }
}

// Console_center (0x515400): the main view back on the cursor.
void console_center() {
    pfncMoveMap(MapWins[0], MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], 1);
}

// FUN_004216F0 (MapWin): whether tile (x, y) is in the view at least `margin` tiles from its edges
// (land tiles also by their height above the top edge).
bool mapwin_tile_in_view(uint8_t* win, int32_t x, int32_t y, int32_t margin) {
    if (margin && y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
        uint8_t climate = map_tile(x, y)->climate;
        if ((climate & 0xE0) > 0x60 && (int32_t)(y - (climate >> 5) + 3) < mw(win, kMwOriginY) + margin) return false;
    }
    if (y < mw(win, kMwOriginY) + margin) return false;
    if (y >= mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) - margin + mw(win, kMwOriginY)) return false;
    if (!(MapToggleFlat & 1)) {
        if (x < mw(win, kMwOriginX) + margin) x += MapAreaX;
        if (x >= mw(win, kMwColsEven) + mw(win, kMwColsOdd) - margin + mw(win, kMwOriginX)) x -= MapAreaX;
    }
    if (x < mw(win, kMwOriginX) + margin) return false;
    return x < mw(win, kMwColsEven) + mw(win, kMwColsOdd) - margin + mw(win, kMwOriginX);
}

// The base window's "set base" prologue of most functions: makes the shown base current and
// recomputes it; in multiplayer, a base whose turn data changed is relocked.
void bw_set_base() {
    int32_t worked = Bases[bwf(kBwBase)].worked_tiles;
    set_base(bwf(kBwBase));
    base_compute(0);
    if (MultiplayerActive && !ControlTurnC && worked != Bases[bwf(kBwBase)].worked_tiles &&
        !bw_lock_base(CurrentBaseID))
        bw_unlock_base(CurrentBaseID);
}

bool bw_own_base() { return cur_base().faction_id == CurrentPlayerFaction || (GameState & 0x80); }

void bw_invalidate() { ui_invalidate(); }

// The window's vtable +0xF8 (GraphicWin redraw: on_redraw into the canvas).
void bw_redraw() {
    if (g_bw.visible) BaseWin_on_redraw(&BaseWin);
    ui_invalidate();
}

// ==== buttons (BaseButton 0x606F30.., FlatButton 0x607CF0..) ===================================

BaseButton* bw_button(int32_t id) { return id >= 0 && id < 22 ? &g_bw.buttons[id] : nullptr; }

namespace {

// FlatButton_ctor's default look: the interface.pcx bars of rows 6..8 (load_interface 0x77A5C8 +
// row * 0x84: left cap, middle, right cap).
Sprite* bar(int32_t row, int32_t part) { return ArtSprite(0x77A5C8 + (uint32_t)row * 0x84 + (uint32_t)part * 0x2C); }

void button_ctor(BaseButton* b) {
    *b = BaseButton();
    for (int32_t s = 0; s < 3; s++) {
        b->tl[s] = bar(6 + s, 0);
        b->top[s] = bar(6 + s, 1);
        b->tr[s] = bar(6 + s, 2);
    }
}

// BaseButton_init (0x607210): the label, the id, the rectangle in the parent, the default text
// colour (black, no shadow) and the font.
void button_init(BaseButton* b, const char* text, int32_t id, int32_t x, int32_t y, int32_t w, int32_t h, int32_t parent) {
    b->name = text ? text : "";
    b->has_name = text != nullptr;
    b->id = id;
    b->r = {x, y, x + w, y + h};
    b->parent = parent;
    b->inited = true;
    b->color = 0;
    b->shadow = -1;
    Buffer_ctor(&b->canvas);
    Buffer_init(&b->canvas, w, h, 8, 0);
}

}  // namespace

void bw_button_ctor(BaseButton* b) { button_ctor(b); }
void bw_button_init(BaseButton* b, const char* text, int32_t id, int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t parent) {
    button_init(b, text, id, x, y, w, h, parent);
}

// BaseButton_set (0x607C80): the toggled state (pressed look).
void bw_button_set(BaseButton* b, int32_t on) {
    if (b->set_state == on) return;
    b->set_state = on;
}

// FlatButton_on_redraw (0x607E50) into the button's canvas.
void bw_button_draw(BaseButton* b) {
    Buffer* c = &b->canvas;
    if (!c->data) return;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);  // GraphicWin_fill(DAT_00696D14)
    int32_t st = b->forced;
    if (st < 0 || st > 2) st = (b->pressed || b->set_state) ? 2 : (b->hover ? 1 : 0);
    UiRect r = all;
    int32_t w = c->width, h = c->height;
    if (b->sprite[st]) {
        Sprite* s = b->sprite[st];
        sprite_copy1(s, c, s->key, (w - s->full_width) / 2, (h - s->full_height) / 2, 1, 1);
    } else if (b->box[st]) {
        Buffer_box_sprite(c, &r, b->box[st]);
    } else if (b->tl[st]) {
        Sprite *tl = b->tl[st], *tr = b->tr[st], *bl = b->bl[st], *br = b->br[st];
        sprite_copy1(tl, c, tl->key, 0, 0, 1, 1);
        if (tr) sprite_copy1(tr, c, tr->key, w - tr->full_width, 0, 1, 1);
        if (bl) sprite_copy1(bl, c, bl->key, 0, h - bl->full_height, 1, 1);
        if (br) sprite_copy1(br, c, br->key, w - br->full_width, h - br->full_height, 1, 1);
        if (b->left[st] && bl)
            Buffer_tile_2(c, b->left[st], 0, 0, 0, tl->full_height, b->left[st]->full_width,
                          h - bl->full_height - tl->full_height);
        if (b->right[st] && tr && br)
            Buffer_tile_2(c, b->right[st], 0, 0, w - b->right[st]->full_width, tr->full_height,
                          b->right[st]->full_width, h - br->full_height - tr->full_height);
        if (b->top[st] && tr)
            Buffer_tile_2(c, b->top[st], 0, 0, tl->full_width, 0, w - tr->full_width - tl->full_width,
                          b->top[st]->full_height);
        if (b->bottom[st] && bl && br)
            Buffer_tile_2(c, b->bottom[st], 0, 0, bl->full_width, h - b->bottom[st]->full_height,
                          w - br->full_width - bl->full_width, b->bottom[st]->full_height);
    }
    if (!b->has_name) return;
    if (b->text_color[st] >= 0) b->color = b->text_color[st];
    Buffer_set_font(c, b->font, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, b->color, b->shadow, 1, 1);
    const char* text = b->name.c_str();
    int32_t tw = r.right - r.left - 6;
    int32_t wh = Buffer_wrap_height(c, text, tw);
    int32_t y = ((r.bottom - r.top) - (wh - Buffer_text_line_height(c) + Buffer_text_height(c))) / 2 + r.top;
    // Wrapped and centred when it fits: more than one line, or one line within the width.
    if (y >= 0 && (buffer_text(c).lines > 1 || Buffer_text_width(c, text) <= tw)) {
        Buffer_wrap_cent_4(c, text, r.left + 3, y, tw);
        return;
    }
    // Too wide: left aligned, cut, then "...".
    UiRect t{r.left + 3, r.top, r.right - 3, r.bottom};
    t.right -= Buffer_text_width(c, "...");
    Buffer_set_clip(c, &t);
    Buffer_write_l_4(c, text, &t, (int32_t)std::strlen(text));
    Buffer_set_clip(c, &all);
    int32_t d = t.right - t.left;
    t.left += d;
    t.right += d;
    Buffer_write_l_4(c, "...", &t, 3);
}

// ==== the main interface canvas (MainInfc) =======================================================

namespace {

// MainInterface_init (0x459680), the parts the base window needs: the window rectangle, the pane
// rectangles and the console art (console2_A.pcx at 1024, console_A.pcx at 800 pixels).
void mi_init() {
    if (g_bw.mi_ready) return;
    g_bw.mi_ready = true;
    int32_t sw = ScreenWidth, sh = ScreenHeight;
    mif(kMiHeight) = 0xD5;
    mif(kMiPaneQueue) = 4;
    if (sw == 800) {
        mif(kMiPos) = 0;
        mif(kMiPos + 4) = 0x157;
        mif(kMiPos + 8) = 800;
        mif(kMiPos + 12) = 600;
        static const int32_t r800[] = {0x38, 0xF5, 0xFC, 0x22B, 0x39, 0x31C, 0xFD, 0x105, 0x42, 0x21C, 0xAC};
        int32_t* f = &mif(kMiPaneQueue + 4);
        for (int32_t i = 0; i < 11; i++) f[i] = r800[i];
        mi_rect(kMiPaneBottom) = {0xF4, 0xBC, 0x220, 0xFC};
    } else {
        // 1024 (the original's values; the window spans the screen width at its bottom).
        mi_rect(kMiPos) = {0, sh - 0x101, sw, sh};
        mi_rect(kMiPaneQueue) = {4, 0x38, 0xF5, 0xFC};
        mi_rect(kMiPaneRight) = {0x30B, 0x38, 0x3FC, 0xFC};
        mi_rect(kMiPaneCenter) = {0x106, 0x42, 0x2FB, 0xAC};
        mi_rect(kMiPaneBottom) = {0xF4, 0xBC, 0x300, 0xFC};
    }
    mi_rect(kMiPaneCenter2) = mi_rect(kMiPaneCenter);
    mif(kMiPaneCenter2 + 12) += 0x3E;
    mi_rect(kMiPaneBottom2) = mi_rect(kMiPaneBottom);
    mif(kMiPaneBottom2 + 4) += 0x3E;
    const UiRect& pos = mi_rect(kMiPos);
    int32_t w = pos.right - pos.left, h = pos.bottom - pos.top;
    Buffer_ctor(&g_bw.mi_canvas);
    Buffer_init(&g_bw.mi_canvas, w, h, 8, 0);
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, sw == 800 ? "console_A.pcx" : "console2_A.pcx", nullptr, 10, 0xEC) == 0)
        Buffer_copy_4(&art, &g_bw.mi_canvas, 0, 0, 0, 0, std::min(w, art.width), std::min(h, art.height));
    g_bw.mi_back.assign(g_bw.mi_canvas.data, g_bw.mi_canvas.data + (size_t)g_bw.mi_canvas.stride * h);
}

}  // namespace

void basewin_mi_init() { mi_init(); }
const std::vector<uint8_t>& basewin_mi_back() { return g_bw.mi_back; }

// MainInterface_restore_back (0x45C290): the panes' background (the centre of box 9 tiled).
void mi_restore_back(int32_t clip) {
    Buffer* c = &g_bw.mi_canvas;
    if (clip) {
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
    }
    Buffer* tile = box_def(9)->center;
    auto fill = [&](const UiRect& p, int32_t dx) {
        int32_t x = p.left + dx, y = p.top + 1;
        Buffer_tile(c, tile, x, y, x, y, p.right - 1 - x, p.bottom - 1 - y);
    };
    fill(mi_rect(kMiPaneQueue), 1);
    fill(mi_rect(MiSlideState ? kMiPaneBottom2 : kMiPaneBottom), 0);
    fill(mi_rect(MiSlideState ? kMiPaneCenter2 : kMiPaneCenter), 1);
    fill(mi_rect(kMiPaneRight), 1);
}

// ==== BaseWin_init (0x41E8D0) ====================================================================

namespace {

Sprite* bw_sprite(uint32_t off) { return ArtSprite(0x6A7628 + off); }

void extract(uint32_t off, Buffer* canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    pfncSpriteFromCanvasRectTrans(bw_sprite(off), canvas, 9, x, y, w, h, 0);
}

}  // namespace

void THISCALL BaseWin_init(uint8_t* self) {
    popup_art_init();  // interface art, box sprites, fonts (load_interface, config_popups)
    // The window's sound (fx\menu2.wav at 0x6EEE68, effects group, volume 0x41, pitch 300).
    Wave_load_3(mem::host(0x6EEE68), "fx\\menu2.wav", 0x10);
    SoundSys_add_to_group(SoundSystem(), 1, mem::host(0x6EEE68));
    Wave_set_volume(mem::host(0x6EEE68), 0x41);
    Wave_set_pitch(mem::host(0x6EEE68), 300);
    textcolor_load();
    mi_init();
    bwf(0x40B00) = -1;
    bwf(0xA18) = 3;  // the SubInterface's mode
    bool w1024 = ScreenWidth != 800;
    int32_t height;
    auto set = [&](uint32_t off, int32_t l, int32_t t, int32_t r, int32_t b) { bw_rect(off) = {l, t, r, b}; };
    set(kBwRectMap, 0xA6, 0x21, 0x230, 0x101);
    set(kBwRectCommerce2, 0x1E, 0xAA, 0xA0, 0x125);
    set(kBwRectCommerce, 0x1E, 0x95, 0xA0, 0xA9);
    set(kBwRectCommercePane, 0x1B, 0x92, 0xA3, 0x128);
    set(kBwRectFacList, 0x236, 0x39, 0x2B8, 0xFE);
    set(kBwRectFacTitle, 0x236, 0x24, 0x2B8, 0x38);
    set(kBwRectFacPane, 0x233, 0x21, 699, 0x101);
    set(kBwRectNutrients, 0x1E, 0x39, 0xA0, 0x8C);
    set(kBwRectNutTitle, 0x1E, 0x24, 0xA0, 0x38);
    set(kBwRectNutPane, 0x1B, 0x21, 0xA3, 0x8F);
    set(kBwRectExpenses, 0xA6, 0x104, 699, 0x15D);
    bw_rect(kBwRectAlloc) = bw_rect(kBwRectExpenses);
    if (!w1024) {
        set(kBwRectRename, 0xBF, 0x162, 0x171, 0x176);
        set(kBwRectOk, 0x175, 0x162, 0x227, 0x176);
        height = ScreenHeight - MiConsoleHeight;
        if (height > 0x18E) height = 0x18E;
    } else {
        // The energy allocation pane below the resource rows (as high as they are); the panes on
        // the left grow by half of it.
        int32_t bottom = bwf(kBwRectAlloc + 12), top = bwf(kBwRectAlloc + 4);
        int32_t d = bottom - top + 3;
        bwf(kBwRectAlloc + 4) = top + d;
        bwf(kBwRectAlloc + 12) = bottom + d;
        int32_t h1 = (bottom - top) / 2, h2 = (bwf(kBwRectAlloc + 12) - bwf(kBwRectAlloc + 4)) / 2;
        bwf(kBwRectCommerce2 + 4) += h2;
        bwf(kBwRectCommerce2 + 12) += h2;
        bwf(kBwRectCommercePane + 4) += h1;
        bwf(kBwRectCommercePane + 12) += h1;
        bwf(kBwRectCommerce + 4) += h1;
        bwf(kBwRectCommerce + 12) += h1;
        bwf(kBwRectCommerce2 + 12) += h1;
        bwf(kBwRectCommercePane + 12) += h1;
        bwf(kBwRectNutrients + 12) += h1;
        bwf(kBwRectNutPane + 12) += h1;
        int32_t ah = bwf(kBwRectAlloc + 12) - bwf(kBwRectAlloc + 4);
        set(kBwRectOk, 0x17D, ah + 0x168, 0x26B, ah + 0x17C);
        set(kBwRectRename, 0x83, ah + 0x168, 0x171, ah + 0x17C);
        height = ah + 0x191;
    }
    set(kBwRectMisc, bwf(kBwRectCommercePane), bwf(kBwRectCommercePane + 12) + 3, bwf(kBwRectCommercePane + 8),
        bwf(kBwRectOk + 4) - 6);
    bwf(kBwQueueSel) = 0;
    bwf(0x40B30) = 0;
    // Rectangles in the main interface: its panes (MainInfc + 0xD80 queue, + 0xD90 right, + 0xDA0
    // centre, + 0xDB0 bottom) inset by 4 and divided.
    const UiRect qp = mi_rect(kMiPaneQueue), rp = mi_rect(kMiPaneRight), cp = mi_rect(kMiPaneCenter),
                 bp = mi_rect(kMiPaneBottom);
    set(kBwRectGarrisonMI, bp.left, bp.top + 4, bp.right - 4, bp.bottom - 4);
    set(kBwRectQueueMI, qp.left + 4, qp.top + 4, (qp.right - qp.left) * 7 / 12 - 4, qp.bottom - 4);
    {
        int32_t qb = bwf(kBwRectQueueMI + 12), qr = bwf(kBwRectQueueMI + 8) + 3, pl = bwf(kBwRectGarrisonMI) - 7;
        set(kBwRectHurryMI, qr, qb - 0x17, pl, qb - 3);
        set(kBwRectChangeMI, qr, qb - 0x2D, pl, qb - 0x19);
        int32_t cx = bwf(kBwRectQueueMI + 8);
        set(kBwRectQueueBoxMI, cx + 4, qp.top + 4, qp.right - 4, bwf(kBwRectChangeMI + 4) - 7);
        set(kBwRectQueueListMI, cx + 7, qp.top + 7, qp.right - 7, qp.top + 7 + 0x14);
        set(kBwRectQueueList2MI, cx + 7, qp.top + 7 + 0x14 + 2, qp.right - 7, bwf(kBwRectChangeMI + 4) - 10);
        bw_rect(kBwRectQueueBtnMI) = bw_rect(kBwRectQueueListMI);
    }
    set(kBwRectPopMI, cp.left + 4, cp.bottom - 0x3A, cp.right - 0x3E, cp.bottom - 4);
    set(kBwRectNameMI, cp.left + 4, cp.top + 4, cp.right - 4, bwf(kBwRectPopMI + 4) - 4 + 1);
    set(kBwRectStapleMI, bwf(kBwRectPopMI + 8) + 7, bwf(kBwRectPopMI + 4) + 3, cp.right - 7,
        bwf(kBwRectPopMI + 12) - 3);
    set(kBwRectProdPaneMI, rp.left + 4, rp.top + 4, rp.right - 4, rp.bottom - 4);
    set(kBwRectProdMI, rp.left + 7, rp.top + 7, rp.right - 7, rp.top + 7 + 0x14);
    set(kBwRectProd2MI, rp.left + 7, rp.top + 7 + 0x14 + 2, rp.right - 7, rp.bottom - 7);
    set(kBwRectEditor, bwf(kBwRectFacPane) + 3, bwf(kBwRectFacPane + 12) - 0x17, bwf(kBwRectFacPane + 8) - 3,
        bwf(kBwRectFacPane + 12) - 3);
    // Fonts.
    Font* font = reinterpret_cast<Font*>(&BaseWin + kBwFont);
    Font_ctor(font);
    Font_init(font, "Arial", 0xE, 1);
    // Base names drawn inverted.
    if (!text_open("BASENAME", "INVERTLIST")) {
        text_get();
        BaseInvertCount = text_item_number();
        for (int32_t i = 0; i < BaseInvertCount && i < 8; i++) {
            text_get();
            BaseInvertNames[i][0] = 0;
            crt_strcat(BaseInvertNames[i], TextBufferGetPtr);
        }
        text_close();
    }
    // The tile diamond (sprite 0x7ACBB8) for BaseWin_farm_get.
    Buffer_ctor(&g_bw.mask);
    Buffer_init(&g_bw.mask, 100, 0x32, 0, 0);
    Sprite* diamond = ArtSprite(0x7ACBB8);
    sprite_copy1(diamond, &g_bw.mask, diamond->key, 0, 0, 1, 1);
    bwf(0x40B1C) = bwf(0x40B20) = bwf(0x40B24) = -1;
    bwf(kBwDragState) = -1;
    bwf(0x40B58) = 0xDF;
    bwf(kBwPane) = 0;
    bwf(0x40AF4) = 0;
    bwf(0x40AF8) = 0;
    bwf(0x40B40) = 0;
    spot_init(&g_bw.spots_iface, 200);
    spot_init(&g_bw.spots_win, 200);
    // The window (GraphicWin_init): centred above the console.
    g_bw.x = (ScreenWidth - 0x2D6) / 2;
    g_bw.y = ScreenHeight - MiConsoleHeight - height;
    Buffer_ctor(&g_bw.canvas);
    Buffer_init(&g_bw.canvas, 0x2F6, height, 8, 0);
    bw_rect(kBwClient) = {0, 0, 0x2F6, height};
    Buffer* c = &g_bw.canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    UiRect r{23, 0, 0x2F6 - 0x37, height + 3};
    Buffer_box_sprite(c, &r, box_def(9));
    r = {0x1B, 4, 699, 0x1E};
    Buffer_box_sprite(c, &r, box_def(12));
    {
        const UiRect &a = bw_rect(kBwRectOk), &b = bw_rect(kBwRectRename);
        r = {std::min(a.left, b.left) - 3, std::min(a.top, b.top) - 3, std::max(a.right, b.right) + 3,
             std::max(a.bottom, b.bottom) + 3};
        Buffer_box_sprite(c, &r, box_def(3));
    }
    g_bw.back.assign(c->data, c->data + (size_t)c->stride * c->height);
    // Art: ter1wreck.pcx (the alien factions' nerve staple button), interface.pcx buttons and icons.
    Buffer art;
    Buffer_ctor(&art);
    Buffer_load_pcx(&art, "ter1wreck.pcx", nullptr, 10, 0xEC);
    for (int32_t i = 0; i < 3; i++) extract(0xBDFC + i * 0x2C, &art, 0x300, 1 + 0x31 * i, 0x30, 0x30);
    Buffer_load_pcx(&art, "interface.pcx", nullptr, 10, 0xEC);
    for (int32_t i = 0; i < 3; i++) extract(0xC9CC + i * 0x2C, &art, 0xF7, 1 + 0x15 * i, 0x14, 0x14);
    for (int32_t i = 0; i < 3; i++) extract(0xCA50 + i * 0x2C, &art, 0x10C, 1 + 0x15 * i, 0x15, 0x14);
    for (int32_t i = 0; i < 3; i++) extract(0xCAD4 + i * 0x2C, &art, 0x122, 0x40 + 0x24 * i, 0x14, 0x23);
    for (int32_t i = 0; i < 3; i++) extract(0xCB58 + i * 0x2C, &art, 0x137, 0x40 + 0x24 * i, 0x14, 0x23);
    for (int32_t i = 0; i < 3; i++) extract(0xCBDC + i * 0x2C, &art, 0x14C, 1 + 0x31 * i, 0x30, 0x30);
    extract(0xBC70, &art, 0x18D, 1, 0x14, 0x14);
    extract(0xBC9C, &art, 0x1A2, 1, 0x14, 0x14);
    extract(0xBD20, &art, 0x18D, 0x16, 0x14, 0x14);
    extract(0xBD4C, &art, 0x1A2, 0x16, 0x14, 0x14);
    extract(0xBCC8, &art, 0x18D, 0x2B, 0x14, 0x14);
    extract(0xBCF4, &art, 0x1A2, 0x2B, 0x14, 0x14);
    extract(0xBD78, &art, 0x18D, 0x40, 0x14, 0x14);
    extract(0xBDA4, &art, 0x1A2, 0x40, 0x14, 0x14);
    extract(0xBDD0, &art, 0x1B7, 0x40, 0x14, 0x14);
    // Buttons.
    for (BaseButton& b : g_bw.buttons) button_ctor(&b);
    button_ctor(&g_bw.staple2);
    auto btn = [&](int32_t id, const char* text, uint32_t rect, int32_t parent) {
        const UiRect& rr = bw_rect(rect);
        button_init(&g_bw.buttons[id], text, id, rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top, parent);
    };
    auto sprites = [&](BaseButton* b, uint32_t off) {
        for (int32_t s = 0; s < 3; s++) b->sprite[s] = bw_sprite(off + (uint32_t)s * 0x2C);
    };
    auto gov = [&](BaseButton* b, int32_t row, uint32_t colors) {
        for (int32_t s = 0; s < 3; s++) {
            b->tl[s] = bar(row + s, 0);
            b->top[s] = bar(row + s, 1);
            b->tr[s] = bar(row + s, 2);
            b->text_color[s] = text_color(colors + (uint32_t)s * 4);
        }
    };
    btn(0x14, label_upper(0x77C), kBwRectQueueBtnMI, 1);
    {
        Sprite* s = bw_sprite(0xCAD4);
        const UiRect& n = bw_rect(kBwRectNameMI);
        set(kBwRectPrevMI, n.left + 3, n.top + 3, n.left + 3 + s->full_width, n.top + 3 + s->full_height);
        btn(2, nullptr, kBwRectPrevMI, 1);
        sprites(&g_bw.buttons[2], 0xCAD4);
        set(kBwRectNextMI, n.right - 3 - s->full_width, n.top + 3, n.right - 3, n.top + 3 + s->full_height);
        btn(3, nullptr, kBwRectNextMI, 1);
        sprites(&g_bw.buttons[3], 0xCB58);
    }
    btn(0, label_upper(0x194), kBwRectOk, 0);
    btn(1, label_upper(0x1F4), kBwRectRename, 0);
    btn(0xC, label_upper(0x814), kBwRectChangeMI, 1);
    btn(0xD, label_upper(0xAC), kBwRectHurryMI, 1);
    set(0x40DCC, 0xA9, 0xEA, 0x129, 0xFE);
    btn(10, label_upper(0x4FC), 0x40DCC, 0);
    set(0x40DBC, 299, 0xEA, 0x1AB, 0xFE);
    btn(9, label_upper(0xD4), 0x40DBC, 0);
    set(0x40DDC, 0x1AD, 0xEA, 0x22D, 0xFE);
    btn(0xB, label_upper(0x4F8), 0x40DDC, 0);
    set(0x40D6C, 0x1E, 7, 0x8E, 0x1B);
    btn(4, label_upper(0x824), 0x40D6C, 0);
    gov(&g_bw.buttons[4], 3, 0x8C6C54);
    set(0x40D7C, 0x90, 7, 0x100, 0x1B);
    btn(5, label_upper(0x828), 0x40D7C, 0);
    gov(&g_bw.buttons[5], 3, 0x8C6C54);
    {
        Sprite* s = bw_sprite(0xC9CC);
        set(0x40E4C, 0x102, 7, 0x102 + s->full_width, 7 + s->full_height);
        btn(0x12, nullptr, 0x40E4C, 0);
        sprites(&g_bw.buttons[0x12], 0xC9CC);
        for (int32_t st = 0; st < 3; st++) g_bw.buttons[0x12].text_color[st] = text_color(0x8C6C60 + (uint32_t)st * 4);
    }
    set(0x40DAC, 0x118, 7, 0x1BE, 0x1B);
    btn(8, label_upper(0x724), 0x40DAC, 0);
    gov(&g_bw.buttons[8], 0, 0x8C6C60);
    {
        Sprite* s = bw_sprite(0xC9CC);
        set(0x40E5C, 0x1C0, 7, 0x1C0 + s->full_width, 7 + s->full_height);
        btn(0x13, nullptr, 0x40E5C, 0);
        sprites(&g_bw.buttons[0x13], 0xC9CC);
        for (int32_t st = 0; st < 3; st++) g_bw.buttons[0x13].text_color[st] = text_color(0x8C6C60 + (uint32_t)st * 4);
    }
    set(0x40D8C, 0x1D6, 7, 0x246, 0x1B);
    btn(6, label_upper(0x82C), 0x40D8C, 0);
    gov(&g_bw.buttons[6], 3, 0x8C6C54);
    set(0x40D9C, 0x248, 7, 0x2B8, 0x1B);
    btn(7, label_upper(0x830), 0x40D9C, 0);
    gov(&g_bw.buttons[7], 3, 0x8C6C54);
    btn(0x10, nullptr, kBwRectStapleMI, 1);
    sprites(&g_bw.buttons[0x10], 0xCBDC);
    {
        const UiRect& rr = bw_rect(kBwRectStapleMI);
        button_init(&g_bw.staple2, nullptr, 0x10, rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top, 1);
        sprites(&g_bw.staple2, 0xBDFC);
    }
    btn(0x15, label_upper(0xD38), kBwRectEditor, 0);
    if (!w1024) {
        Sprite* s = bw_sprite(0xCA50);
        set(0x40E3C, 0xA9, 0x107, 0xA9 + s->full_width, 0x107 + s->full_height);
        btn(0x11, nullptr, 0x40E3C, 0);
        sprites(&g_bw.buttons[0x11], 0xCA50);
    }
    for (BaseButton& b : g_bw.buttons) b.font = font;
    g_bw.staple2.font = font;
    g_bw.ready = true;
}

// ==== window ======================================================================================

// BaseWin_on_status (0x41DAD0, the SubInterface's status): shows or hides the buttons and the
// window, sliding it in / out (slide.cpp; the world map windows are not reconstructed).
void bw_status(int32_t on) {
    for (int32_t i = 0; i < 22; i++) g_bw.buttons[i].shown = false;
    g_bw.staple2.shown = false;
    uint8_t* menu2 = mem::host(0x6EEE68);  // BaseWin_init's fx\menu2.wav
    if (!on) {
        ambience(0xCE);
        Wave_play_2(WaveMenuDown());
        g_bw.visible = false;
        ui_invalidate_map();
        slide_hide_window(&g_bw.canvas, g_bw.x, g_bw.y);  // slide_hide(win, 0)
        return;
    }
    for (int32_t i = 0; i < 22; i++) {
        if (i == 0x10 && (MFactions[(uint8_t)Bases[bwf(kBwBase)].faction_id].rule_flags & 0x80)) {
            g_bw.staple2.shown = true;
            continue;
        }
        if (i == 0x15 && !(GameState & 0x40)) continue;
        if (g_bw.buttons[i].inited) g_bw.buttons[i].shown = true;
    }
    bwf(kBwDragState) = -1;
    if ((GamePreferences & 0x400) && !Wave_is_playing(menu2)) Wave_play_2(menu2);
    ambience(0xCD);
    g_bw.visible = true;
    ui_invalidate();
    slide_show_window(&g_bw.canvas, g_bw.x, g_bw.y, &g_bw.back);  // slide_show(win, 0): the arms
    if (GamePreferences & 0x400) Sound_fade(menu2);
}

// BaseWin_show (0x41E890) / BaseWin_hide (0x41E8B0) through the iface mode stack.
void bw_show() {
    if (!g_bw.visible) {
        iface_set_mode(3);
        bw_status(1);
    }
}
void bw_hide() {
    if (g_bw.visible) {
        iface_release_mode(3);
        bw_status(0);
    }
}

void basewin_client_to_screen(int32_t* x, int32_t* y) {
    *x += g_bw.x;
    *y += g_bw.y;
}

int32_t THISCALL Win_is_visible(uint8_t* win) { return win == &BaseWin && g_bw.visible ? 1 : 0; }

void THISCALL GraphicWin_redraw(uint8_t* win) {
    if (win == &BaseWin && g_bw.visible) BaseWin_on_redraw(win);
    ui_invalidate();
}

void THISCALL BaseWin_close(uint8_t*) {}

// BaseWin_zoom (0x41A920): shows base_id (wait: until the window is closed).
void THISCALL BaseWin_zoom(uint8_t* self, int32_t base_id, int32_t wait) {
    if (!g_bw.ready) BaseWin_init(self);
    bwf(0x40B00) = -1;
    TutWin_reset(&TutWin);
    bwf(kBwBase) = base_id;
    ComputeBaseID = -1;
    bw_set_base();
    cur_base().state_flags &= ~0x10;
    bwf(kBwOpen) = 1;
    bwf(kBwB10) = 0;
    bwf(kBwB14) = 0;
    bwf(kBwB18) = 1;
    bwf(kBwProdAtOpen) = cur_base().queue_items[0];
    FX_play(&Sounds, 0xF);
    ambience(0x1E);
    if (!Win_is_visible(self)) bw_show();
    BaseWin_on_redraw(self);  // vtable + 0xF8 (when already visible), then GraphicWin_redraw
    ui_invalidate();
    bwf(0x1E670) = 0;  // in_ECX[0x799C]
    // Tutorial hints (TutWin_tut_win / TutWin_do_base, tutwin.cpp): BASECONTROL, DRONERIOTS,
    // LOWNUTRIENT, SUPPORT, LOWMINERALS(2), INEFFICIENCY(HQ).
    BASE& b = cur_base();
    if ((uint8_t)b.faction_id == CurrentPlayerFaction && Factions[CurrentPlayerFaction].base_count == 1 &&
        tut_check(2)) {
        TutorialArrowShown = 1;
        parse_says(0, b.name, -1, -1);
        int32_t x = 0, y = 0;
        TutWin_base_rect(&bw_rect(0x40DAC), &x, &y);
        TutWin_tut_win(&TutWin, MapWins[0], "BASECONTROL", x, y, nullptr, 0x100, -1, -1);
    } else if ((b.state_flags & 2) && tut_check2(0x10) && (b.governor_flags & 0x80000040) != 0x80000040) {
        TutorialArrowShown = 6;
        parse_says(0, b.name, -1, -1);
        int32_t x = 0, y = 0;
        TutWin_base_rect(&bw_rect(0x40DAC), &x, &y);
        TutWin_tut_win(&TutWin, MapWins[0], "DRONERIOTS", x, y, nullptr, 0x100, -1, -1);
    } else {
        const char* label = nullptr;
        uint32_t rect = 0;
        if (b.nutrient_surplus < 0 && tut_check2(0x20) && b.specialist_total &&
            (b.governor_flags & 0x80000040) != 0x80000040) {
            parse_says(0, b.name, -1, -1);
            label = "LOWNUTRIENT";
            rect = 0x40B8C;
        } else if (b.mineral_surplus < (b.pop_size + 1) / 2 && BaseForcesMaintCost > 0 &&
                   (b.pop_size > 3 || b.mineral_surplus < 1) && tut_check2(0x200)) {
            parse_says(0, b.name, -1, -1);
            label = "SUPPORT";
            rect = 0x40CDC;
        } else if (b.mineral_surplus < 2 && BaseForcesMaintCost < b.pop_size / 2 && b.pop_size > 2 &&
                   tut_check2(0x40)) {
            parse_says(0, b.name, -1, -1);
            label = (map_tile(b.x, b.y)->climate & 0xE0) > 0x5F ? "LOWMINERALS" : "LOWMINERALS2";
            rect = 0x40CDC;
        } else if (b.energy_inefficiency > b.energy_surplus && !(b.governor_flags & 0x80000000)) {
            bool hq = false;
            for (int32_t i = 0; i < BaseCount; i++) {
                if (Bases[i].faction_id == b.faction_id && base_fac(i, 1)) {
                    hq = true;
                    break;
                }
            }
            if (!hq && tut_check2(0x400)) {
                parse_says(0, b.name, -1, -1);
                BaseWin_draw_expenses(self, 2);
                label = "INEFFICIENCYHQ";
                rect = 0x40CBC;
            } else if (tut_check2(0x80)) {
                parse_says(0, b.name, -1, -1);
                BaseWin_draw_expenses(self, 2);
                label = "INEFFICIENCY";
                rect = 0x40CBC;
            }
        }
        if (label) TutWin_do_base(&TutWin, &bw_rect(rect), label, 0);
    }
    if (wait) {
        while (bwf(kBwOpen) && Win_is_visible(self) && !ControlTurnA) wait_task();
    }
}

// BaseWin_soft_zoom (0x41AE40): makes base_id the window's base; opens it when it is visible.
void THISCALL BaseWin_soft_zoom(uint8_t* self, int32_t base_id) {
    if (Win_is_visible(self)) {
        BaseWin_zoom(self, base_id, 0);
        return;
    }
    bwf(kBwBase) = base_id;
    ComputeBaseID = -1;
    bw_set_base();
    bwf(kBwProdAtOpen) = Bases[base_id].queue_items[0];
}

// The common tail of BaseWin_exit and BaseWin_base_killed: closes the window and recentres the
// view on the selected unit when it is not well inside the view.
void bw_close_window(uint8_t* self) {
    int32_t was = Win_is_visible(self);
    bwf(kBwOpen) = 0;
    // Time_stop (the window's animation timer).
    if (Win_is_visible(self)) bw_hide();
    do_all_draws();
    if (!was || !MapWinInVehMode || MapWinVehA < 0) return;
    int32_t y = Vehs[MapWinVehA].y, x = Vehs[MapWinVehA].x;
    uint8_t* win = MapWins[0];
    if (!(y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX || (map_tile(x, y)->climate & 0xE0) < 0x61 ||
          mw(win, kMwOriginY) + 2 <= (int32_t)(y - (map_tile(x, y)->climate >> 5) + 3))) {
        console_center();
        return;
    }
    if (mw(win, kMwOriginY) + 2 <= y && y < mw(win, kMwRowsAbove) + mw(win, kMwRowsBelow) - 2 + mw(win, kMwOriginY)) {
        if (!(MapToggleFlat & 1)) {
            if (x < mw(win, kMwOriginX) + 2) x += MapAreaX;
            if (mw(win, kMwColsOdd) + mw(win, kMwColsEven) - 2 + mw(win, kMwOriginX) <= x) x -= MapAreaX;
        }
        if (mw(win, kMwOriginX) + 2 <= x && x < mw(win, kMwColsOdd) + mw(win, kMwColsEven) - 2 + mw(win, kMwOriginX))
            return;
    }
    console_center();
}

void THISCALL BaseWin_exit(uint8_t* self) { bw_close_window(self); }

void THISCALL BaseWin_base_killed(uint8_t* self, int32_t base_id) {
    if (!Win_is_visible(self)) return;
    if (base_id < bwf(kBwBase)) {
        bwf(kBwBase)--;
        return;
    }
    if (bwf(kBwBase) == base_id) bw_close_window(self);
}

// BaseWin_focus (0x415DD0): the shown base becomes the current base again.
void THISCALL BaseWin_focus(uint8_t*) { bw_set_base(); }

void THISCALL BaseWin_recompute(uint8_t* self, int32_t) {
    if (!Win_is_visible(self)) return;
    set_base(bwf(kBwBase));
    base_compute(1);
    bw_redraw();
}

void THISCALL BaseWin_check_base(uint8_t* self, int32_t base_id) {
    if (Win_is_visible(self) && base_id == bwf(kBwBase)) {
        base_compute(1);
        bw_redraw();
    }
}

void THISCALL BaseWin_check_loc(uint8_t* self, int32_t x, int32_t y, int32_t dist) {
    if (!Win_is_visible(self)) return;
    const BASE& b = Bases[bwf(kBwBase)];
    int32_t dx = std::abs(x - b.x);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    int32_t dy = std::abs(y - b.y);
    dx = std::abs(dx);
    dy = std::abs(dy);
    int32_t big = std::max(dx, dy), small = std::min(dx, dy);
    if (big - ((((dx + dy) >> 1) - small + 1) >> 1) <= dist) {
        base_compute(1);
        bw_redraw();
    }
}

void THISCALL BaseWin_check_veh(uint8_t* self, int32_t veh_id) {
    if (!Win_is_visible(self)) return;
    int32_t base_id = bwf(kBwBase);
    if ((Vehs[veh_id].x == Bases[base_id].x && Vehs[veh_id].y == Bases[base_id].y) ||
        Vehs[veh_id].home_base_id == base_id) {
        base_compute(1);
        BaseWin_on_redraw(self);
        ui_invalidate();
    }
}

// BaseWin_prev (0x41A010) / BaseWin_next (0x41A160): the faction's base before / after the shown
// one in alphabetical order (wrapping around).
static void bw_step(uint8_t* self, bool next) {
    set_base(bwf(kBwBase));
    if (cur_base().faction_id != CurrentPlayerFaction && !(GameState & 0x80)) return;
    if (ControlTurnC) {
        X_pop("ARROWUPKEEP", 0);
        return;
    }
    int32_t shown = bwf(kBwBase);
    int32_t found = -1, wrap = shown;
    int32_t found_cmp = -1, wrap_cmp = shown;  // base whose name is compared
    for (int32_t i = 0; i < BaseCount; i++) {
        if (i == bwf(kBwBase)) continue;
        if (Bases[i].faction_id != Bases[bwf(kBwBase)].faction_id) continue;
        const char* name = Bases[i].name;
        int32_t c = crt_stricmp(name, Bases[bwf(kBwBase)].name);
        if (next ? c > 0 : c < 0) {
            if (found < 0 || (next ? crt_stricmp(name, Bases[found_cmp].name) < 0
                                   : crt_stricmp(name, Bases[found_cmp].name) > 0)) {
                found = i;
                found_cmp = i;
            }
        }
        // The wrap-around candidate: the first (next) / last (prev) name overall.
        if (next ? crt_stricmp(name, Bases[wrap_cmp].name) < 0 : crt_stricmp(name, Bases[wrap_cmp].name) > 0) {
            wrap = i;
            wrap_cmp = i;
        }
    }
    int32_t target = found >= 0 ? found : wrap;
    bwf(kBwBase) = target;
    BaseWin_zoom(self, target, 0);
}
void THISCALL BaseWin_prev(uint8_t* self) { bw_step(self, false); }
void THISCALL BaseWin_next(uint8_t* self) { bw_step(self, true); }

// BaseWin_rename (0x41A2B0): RENAME (X_pop_ask, 24 characters); renaming a base taken from another
// faction annoys it (and may end a pact).
void THISCALL BaseWin_rename(uint8_t* self) {
    bw_set_base();
    if (!bw_own_base()) return;
    if (bw_lock_base(CurrentBaseID)) return;
    parse_says(0, cur_base().name, -1, -1);
    if (X_pop_ask("RENAME", 0x18, cur_base().name, 0, 0)) {
        bw_unlock_base(CurrentBaseID);
        return;
    }
    crt_strcpy(cur_base().name, ParseStrBuffer[0]);
    BASE& b = cur_base();
    if ((uint8_t)b.faction_id == CurrentPlayerFaction) {
        int32_t former = (uint8_t)b.faction_id_former;
        if (former != CurrentPlayerFaction && !((1 << former) & FactionStatus[0]) && !MultiplayerActive) {
            cause_friction(former, CurrentPlayerFaction, 3);
            uint32_t d = Factions[former].diplo_status[CurrentPlayerFaction];
            if ((d & 7) && (d & 0x20) && game_random(0, 2) == 0) set_treaty(former, CurrentPlayerFaction, 0x400, 1);
        }
    }
    bw_unlock_base(CurrentBaseID);
    BaseWin_draw_info(self, 1);
    draw_radius(cur_base().x, cur_base().y, 3, 2);
}

// BaseWin_on_redraw (0x41E790).
void THISCALL BaseWin_on_redraw(uint8_t* self) {
    if (!g_bw.ready || InfcList[InfcUsed] != 3) return;
    // The window's canvas starts from its frame (the panes redraw their own boxes).
    std::copy(g_bw.back.begin(), g_bw.back.end(), g_bw.canvas.data);
    mi_restore_back(1);
    BaseWin_update_gov_buttons(self);
    bw_button_set(&g_bw.buttons[10], bwf(kBwPane) == 0);
    bw_button_set(&g_bw.buttons[11], bwf(kBwPane) == 1);
    bw_button_set(&g_bw.buttons[9], bwf(kBwPane) == 2);
    BaseWin_draw_production(self, 0);
    BaseWin_draw_expenses(self, 0);
    BaseWin_draw_queue(self, 0);
    BaseWin_draw_info(self, 0);
    BaseWin_draw_misc(self, 0);
    pfncDrawCityMap(self, 0);
    BaseWin_draw_pop(self, 0);
    BaseWin_draw_facilities(self, 0);
    BaseWin_draw_commerce(self, 0);
    BaseWin_draw_nutrients(self, 0);
    BaseWin_draw_supported(self, 0);
    BaseWin_draw_garrison(self, 0);
    ui_invalidate();
}

// ==== keys ========================================================================================

int32_t basewin_console_key(uint8_t* console, int32_t ch, int32_t vk, int32_t key, int32_t net_busy) {
    uint8_t* self = &BaseWin;
    // The upkeep phase of a multiplayer turn: production keys only for a faction not yet done.
    bool prod_ok = !net_busy || (ControlUpkeepA && !(NetUpkeepDoneMask & (1 << (CurrentPlayerFaction & 31))));
    switch (vk) {
    case 0xD:
    case 0x1B:
        BaseWin_exit(self);
        return 1;
    case 0x21: case 0x25: case 0x64: case 0x69:  // PgUp, left, keypad 4 / 9
        BaseWin_prev(self);
        return 1;
    case 0x22: case 0x27: case 0x63: case 0x66:  // PgDn, right, keypad 3 / 6
        BaseWin_next(self);
        return 1;
    default:
        break;
    }
    if (key >= '0' && key <= '9') {
        if (prod_ok) BaseWin_production(self, key - '0', 0);
        return 1;
    }
    switch (key) {
    case ',': case '<':
        BaseWin_prev(self);
        return 1;
    case '.': case '>': case 'n':
        BaseWin_next(self);
        return 1;
    case 'c': case 'p':
        if (prod_ok) BaseWin_production(self, 0, 0);
        return 1;
    case 'h':
        if (!net_busy) BaseWin_hurry(self);
        return 1;
    case 'r':
        BaseWin_rename(self);
        return 1;
    case 'e':
        if (GameState & 0x40) console_not_reconstructed("BaseWin_base_editor");
        return 1;
    case '+': case '=':  // scenario editor: population
        if (GameState & 0x40) {
            BaseWin_focus(self);
            if (cur_base().pop_size < 99) cur_base().pop_size++;
            BaseWin_recompute(self, 0);
        }
        break;
    case '-': case '_':
        if (GameState & 0x40) {
            BaseWin_focus(self);
            if (cur_base().pop_size > 1) cur_base().pop_size--;
            BaseWin_recompute(self, 0);
        }
        break;
    default:
        break;
    }
    // The rest of the console's keys with the base window's flag ([ebp-0x28]): end of turn and
    // time bonus; the character commands, editor keys and movement are skipped.
    if (vk == 0x2000D) {
        Console_end_my_turn(console);
        return 1;
    }
    if (vk == 0x20020) {
        Console_use_time_bonus(console);
        return 1;
    }
    if (key == '*' && !Console_edit_lock(console)) {
        Console_zoom_to_upkeep_base(console);
        return 1;
    }
    console_not_reconstructed_quiet("Console_on_key_click: function keys with the base window open");
    return 1;
}

// ==== composition and mouse ======================================================================

Buffer* basewin_canvas() { return &g_bw.canvas; }
Buffer* basewin_mi_canvas() { return &g_bw.mi_canvas; }

static void blit_keyed(Buffer* screen, Buffer* src, int32_t x0, int32_t y0) {
    if (!src->data) return;
    for (int32_t y = 0; y < src->height; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= screen->height) continue;
        const uint8_t* s = src->data + (size_t)y * src->stride;
        uint8_t* d = screen->data + (size_t)sy * screen->stride;
        for (int32_t x = 0; x < src->width; x++) {
            int32_t sx = x0 + x;
            if (sx < 0 || sx >= screen->width || s[x] == 9) continue;
            d[sx] = s[x];
        }
    }
}

static void compose_buttons(Buffer* screen, int32_t parent, int32_t ox, int32_t oy) {
    auto one = [&](BaseButton& b) {
        if (!b.shown || b.parent != parent) return;
        bw_button_draw(&b);
        blit_keyed(screen, &b.canvas, ox + b.r.left, oy + b.r.top);
    };
    for (BaseButton& b : g_bw.buttons) one(b);
    one(g_bw.staple2);
}

Sprite* bw_sprite_at(uint32_t off) { return ArtSprite(0x6A7628 + off); }

void basewin_compose(Buffer* screen) {
    if (!g_bw.visible) return;
    blit_keyed(screen, &g_bw.canvas, g_bw.x, g_bw.y);
    compose_buttons(screen, 0, g_bw.x, g_bw.y);
    const UiRect& mp = mi_rect(kMiPos);
    blit_keyed(screen, &g_bw.mi_canvas, mp.left, mp.top);
    compose_buttons(screen, 1, mp.left, mp.top);
    prodwin_compose(screen);  // the production picker over the base window
}

void basewin_log_state() {
    if (!Win_is_visible(&BaseWin)) {
        std::fprintf(stderr, "basestate closed\n");
        return;
    }
    const BASE& b = Bases[bwf(kBwBase)];
    std::fprintf(stderr, "basestate base %d %s f%d pop %d tiles %08X spec %d minerals %d/%d credits %d queue",
                 bwf(kBwBase), b.name, b.faction_id, b.pop_size, b.worked_tiles, b.specialist_total,
                 b.minerals_accumulated, b.minerals_accumulated_2, Factions[b.faction_id].energy_credits);
    for (int32_t i = 0; i <= b.queue_size && i < 10; i++) std::fprintf(stderr, " %d", b.queue_items[i]);
    std::fprintf(stderr, " state %08X gov %08X\n", b.state_flags, b.governor_flags);
}

bool basewin_dump(const char* path) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite("SMXBASE1", 1, 8, f);
    std::fwrite(GamePalette.rgb, 1, 768, f);
    for (Buffer* c : {&g_bw.canvas, &g_bw.mi_canvas}) {
        int32_t wh[2] = {c->data ? c->width : 0, c->data ? c->height : 0};
        std::fwrite(wh, 4, 2, f);
        for (int32_t y = 0; y < wh[1]; y++) std::fwrite(c->data + (size_t)y * c->stride, 1, (size_t)wh[0], f);
    }
    std::fclose(f);
    std::fprintf(stderr, "basedump %s (base %d)\n", path, bwf(kBwBase));
    return true;
}

// A button under (x, y) of a parent canvas.
static BaseButton* button_at(int32_t parent, int32_t x, int32_t y) {
    auto hit = [&](BaseButton& b) { return b.shown && b.parent == parent && in_box(x, y, b.r); };
    if (hit(g_bw.staple2)) return &g_bw.staple2;
    for (BaseButton& b : g_bw.buttons)
        if (hit(b)) return &b;
    return nullptr;
}

bool basewin_mouse(int32_t type, int32_t x, int32_t y, bool double_click, bool right) {
    if (!g_bw.visible) return false;
    uint8_t* self = &BaseWin;
    const UiRect& mp = mi_rect(kMiPos);
    // Which window: the main interface lies over the base window (its pixels of colour 9 are
    // holes, as the window regions of the original).
    int32_t parent = -1, lx = 0, ly = 0;
    auto opaque = [](Buffer* c, int32_t px, int32_t py) {
        return px >= 0 && py >= 0 && px < c->width && py < c->height && c->data[(size_t)py * c->stride + px] != 9;
    };
    if (in_box(x, y, mp) && (opaque(&g_bw.mi_canvas, x - mp.left, y - mp.top) || button_at(1, x - mp.left, y - mp.top))) {
        parent = 1;
        lx = x - mp.left;
        ly = y - mp.top;
    } else if (x >= g_bw.x && y >= g_bw.y && x < g_bw.x + g_bw.canvas.width && y < g_bw.y + g_bw.canvas.height) {
        parent = 0;
        lx = x - g_bw.x;
        ly = y - g_bw.y;
    }
    if (parent < 0) return true;  // the map around the window: no action
    BaseButton* b = button_at(parent, lx, ly);
    if (b) {
        // BaseButton_on_left_down / on_left_up: pressed look, the click on release inside.
        if (type == UiInput::LDOWN) {
            b->pressed = 1;
            g_bw.pressed_button = b;
        } else if (type == UiInput::LUP && g_bw.pressed_button == b) {
            b->pressed = 0;
            g_bw.pressed_button = nullptr;
            ui_invalidate();
            BaseWin_on_button_clicked(self, b->id);
        }
        ui_invalidate();
        return true;
    }
    if (g_bw.pressed_button && type == UiInput::LUP) {
        g_bw.pressed_button->pressed = 0;
        g_bw.pressed_button = nullptr;
        ui_invalidate();
    }
    if (type == UiInput::LDOWN) {
        // BaseWin_on_left_down / on_iface_left_down: the spot under the press (drag source).
        uint32_t base = parent ? 0x40130 + 0xA14 - 0x40130 + 0x40130 : 0;
        (void)base;
        if (bwf(kBwDragState) > 0) {
        }
        bwf(kBwDragState) = -1;
        bwf(kBwDragType) = -1;
        bwf(kBwDragIndex) = -1;
        bwf(kBwDownX) = lx;
        bwf(kBwDownY) = ly;
        g_bw.down_parent = parent;
        if (!MultiplayerActive || !NetInputPending) {
            int32_t index, t;
            if (spot_check(parent ? &g_bw.spots_iface : &g_bw.spots_win, lx, ly, &index, &t) >= 0) {
                bwf(kBwDragType) = t;
                bwf(kBwDragIndex) = index;
                bwf(kBwDragState) = 0;
            }
        }
        return true;
    }
    if (type == UiInput::LUP) {
        // A click: BaseWin_on_left_click / on_iface_left_click (right: the right button's click).
        bool moved = std::abs(lx - bwf(kBwDownX)) > 2 || std::abs(ly - bwf(kBwDownY)) > 2 || g_bw.down_parent != parent;
        if (moved && bwf(kBwDragState) >= 0) {
            // A drag (worker dragged to another tile): the release's click with the drag kept.
            bwf(kBwDragState) = 1;
        }
        if (parent == 0) BaseWin_click(self, lx, ly, right ? 1 : 0, 0);
        else BaseWin_iface_click(self, lx, ly, right ? 1 : 0, 0);
        bwf(kBwDragState) = -1;
        bwf(kBwDragType) = -1;
        bwf(kBwDragIndex) = -1;
        (void)double_click;
        return true;
    }
    return true;
}
