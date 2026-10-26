// The scenario editor's interface pane (MainInterface mode 0x3EA, reconstructed from the asm):
// the third mode button of the info pane (MainInterface +0x2498, id 0x3EA, "EditorPanelWin"),
// shown only while the editor's omniscient view is on (Console_editor_on, the F10 key and the
// Scenario menu's 0x601, Console_editor_view, load_daemon: its vtable slots 1 / 2), and the pane
// it selects in place of the info text: MainInterface_init's paint buttons (25 sprite buttons of
// interface.pcx, ids 0x3F3.., ButtonGroup 2, in columns of five) and brush buttons (six, ids
// 0x40C.., ButtonGroup 3, columns of three; not at 800), their help bubbles, the boxes
// MainInterface_on_redraw draws around them, and MainInterface_draw_edit_pane's text (the map
// file, the paint, the brush, the map size, the cloud cover, the protection lock). A click on a
// button: MainInterface_on_iface_group_clicked (Console_editor_paints / the brush).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/gfx.h"
#include "ui/infowin.h"
#include "ui/popup.h"
#include "ui/win.h"

GLOBAL(int32_t, EditorPaint2, 0x945AE0);  // the paint (editor.cpp's EditorPaint)
GLOBAL(int32_t, EditorBrush2, 0x945AE4);  // the brush

namespace {

constexpr int32_t kKey = 9;
constexpr uint32_t kPaintRects = 0xADC;  // 25 rectangles, then the six brushes' (0xC6C)
constexpr uint32_t kPaintBox = 0xD4C, kBrushBox = 0xD5C, kTextBox = 0xD6C;

bool g_loaded = false;
Sprite g_paint[25][3];  // MainInfc +0x21E00 (interface.pcx, 0x2B x 0x12)
Sprite g_brush[6][3];   // MainInfc +0x22AE4 (0x18 x 0x18)
BaseButton g_btn[31];   // 0x3F3.. (paints), 0x40C.. (brushes)
int32_t g_group[2] = {0x3F3, 0x40C};  // ButtonGroup_set of groups 2 and 3
bool g_shown = false;   // EditorPanelWin (the 0x3EA mode button) shown

int32_t mem_i(uint32_t a) { return *reinterpret_cast<int32_t*>(mem::host(a)); }

// MainInterface_init's sprites of interface.pcx.
void load() {
    if (g_loaded) return;
    g_loaded = true;
    Buffer ifc;
    Buffer_ctor(&ifc);
    if (Buffer_load_pcx(&ifc, "interface.pcx", nullptr, 10, 0xEC)) return;
    int32_t x = 0x15E, y = 0xF5;
    for (int32_t i = 0; i < 25; i++) {
        if (i) {
            if (i % 8 == 0) {
                x = 0x15E;
                y += 0x39;
            } else {
                x += 0x2C;
            }
        }
        for (int32_t k = 0; k < 3; k++) pfncSpriteFromCanvasRectTrans(&g_paint[i][k], &ifc, kKey, x, y + 0x13 * k, 0x2B, 0x12, 0);
    }
    for (int32_t i = 0; i < 6; i++)
        for (int32_t k = 0; k < 3; k++)
            pfncSpriteFromCanvasRectTrans(&g_brush[i][k], &ifc, kKey, 0x1D1 + 0x19 * i, 0x1B6 + 0x19 * k, 0x18, 0x18, 0);
}

UiRect& rect(uint32_t off) { return mi_rect(off); }

void union_rect(UiRect& u, const UiRect& r) {
    if (u.left >= u.right || u.top >= u.bottom) {
        u = r;
        return;
    }
    u.left = std::min(u.left, r.left);
    u.top = std::min(u.top, r.top);
    u.right = std::max(u.right, r.right);
    u.bottom = std::max(u.bottom, r.bottom);
}

// The terrain feature of paint button i (6..20): the (i - 6)th of Terraform with a tile bit.
int32_t terraform_of(int32_t k) {
    for (int32_t j = 0; j < 20; j++)
        if (!Terraform[j].bit && j <= k) k++;
    return k;
}

// MainInterface_init's layout of the buttons (the pane rectangle +0xDA0) and their bubbles.
void layout() {
    load();
    const UiRect c = rect(kMiPaneCenter);
    // The paints: columns of five, centred vertically.
    {
        int32_t w = g_paint[0][0].full_width, h = g_paint[0][0].full_height;
        int32_t l = c.left + 7, t = c.top + 7;
        t = t + ((c.bottom - h * 5 - t) - 0xB) / 2;
        int32_t r = w + l, b = h + t, prev = b;
        for (int32_t i = 0; i < 25; i++) {
            if (i) {
                int32_t d;
                if (i % 5 == 0) {
                    d = (t - prev) * 4 - 4;
                    int32_t dx = (r - l) + 1;
                    l += dx;
                    r += dx;
                } else {
                    d = (prev - t) + 1;
                }
                t += d;
                b = prev + d;
                prev = b;
            }
            rect(kPaintRects + 16 * i) = {l, t, r, b};
        }
    }
    // The brushes: right of the paints, columns of three.
    {
        int32_t w = g_brush[0][0].full_width, h = g_brush[0][0].full_height;
        int32_t l = rect(kPaintRects + 16 * 24).right + 9, t = c.top + 7;
        t = t + (((c.bottom - 7) - (h * 3 + 2)) - t) / 2;
        int32_t r = w + l, b = h + t, prev = b;
        for (int32_t i = 0; i < 6; i++) {
            if (i) {
                int32_t d;
                if (i % 3 == 0) {
                    d = (t - prev) * 2 - 2;
                    int32_t dx = (r - l) + 1;
                    l += dx;
                    r += dx;
                } else {
                    d = (prev - t) + 1;
                }
                t += d;
                b = prev + d;
                prev = b;
            }
            rect(kPaintRects + 16 * (25 + i)) = {l, t, r, b};
        }
    }
    UiRect& pb = rect(kPaintBox);
    pb = {};
    for (int32_t i = 0; i < 25; i++) {
        const UiRect& r = rect(kPaintRects + 16 * i);
        union_rect(pb, r);
        BaseButton& bt = g_btn[i];
        bw_button_ctor(&bt);
        bw_button_init(&bt, nullptr, 0x3F3 + i, r.left, r.top, r.right - r.left, r.bottom - r.top, 1);
        bt.has_name = false;
        for (int32_t k = 0; k < 3; k++) bt.sprite[k] = &g_paint[i][k];
        if (i < 6) bt.bubble = label_get(0xEBC / 4 + i);
        else if (i <= 20) bt.bubble = Terraform[terraform_of(i - 6)].name;
        else {
            static const int32_t kNat[4] = {2, 6, 3, 10};
            bt.bubble = Natural[kNat[i - 21]].name_short;
        }
    }
    pb = {pb.left - 3, pb.top - 3, pb.right + 3, pb.bottom + 3};
    UiRect& bb = rect(kBrushBox);
    bb = {};
    if (ScreenWidth != 800) {
        for (int32_t i = 0; i < 6; i++) {
            const UiRect& r = rect(kPaintRects + 16 * (25 + i));
            union_rect(bb, r);
            BaseButton& bt = g_btn[25 + i];
            bw_button_ctor(&bt);
            bw_button_init(&bt, nullptr, 0x40C + i, r.left, r.top, r.right - r.left, r.bottom - r.top, 1);
            bt.has_name = false;
            for (int32_t k = 0; k < 3; k++) bt.sprite[k] = &g_brush[i][k];
            bt.bubble = label_get(0xF64 / 4 + i);
        }
        bb = {bb.left - 3, bb.top - 3, bb.right + 3, bb.bottom + 3};
    }
    UiRect& tb = rect(kTextBox);
    tb = {bb.right + 3, bb.top, rect(0xA3C).left - 3, bb.bottom};
}

bool g_laid = false;
void ensure_layout() {
    if (g_laid || !rect(kMiPaneCenter).right) return;
    g_laid = true;
    layout();
}

int32_t button_count() { return ScreenWidth == 800 ? 25 : 31; }

void draw_text(Buffer* c) {
    if (ScreenWidth < 0x400 || mi_mode() != 0x3EA) return;
    UiRect r = rect(kTextBox);
    Buffer_set_text_color(c, mem_i(0x8C6BB4), mem_i(0x8C6BB8), 1, 1);
    // The "{...}" parts: the original's second text colour is the info pane's (InfoWin_draw_text's
    // Buffer_set_text_color2, the global text state it leaves).
    Buffer_set_text_color2(c, mem_i(0x8C6BC4), mem_i(0x8C6BC8), 1, 1);
    Font* f1 = find_font(10, 1);
    Font* f2 = find_font(10, 2);
    Font* f0 = find_font(10, 0);
    Buffer_set_font(c, f0, f2, f1, nullptr);
    Buffer_box_sprite(c, &r, box_def(0));
    r = {r.left + 4, r.top + 4, r.right - 4, r.bottom - 4};
    const int32_t x = r.left;
    int32_t y = r.top;
    std::string s;
    auto line = [&](const std::string& t) {
        Buffer_write_l_3(c, t.c_str(), x, y, (int32_t)t.size());
        y += Buffer_text_line_height(c);
    };
    line(std::string(label_get(0x6A8 / 4)) + ": " + "{" + MapFilePath + "}");
    const char* paint;
    const int32_t p = EditorPaint2;
    switch (p) {
    case 0: case 1: case 2: case 3: case 4: case 5: paint = label_get(0xEBC / 4 + p); break;
    case 0x1A: paint = Natural[2].name_short; break;
    case 0x1B: paint = Natural[6].name_short; break;
    case 0x1C: paint = Natural[3].name_short; break;
    case 0x1D: paint = Natural[10].name_short; break;
    default: paint = (p >= 6 && p < 26) ? Terraform[p - 6].name : ""; break;
    }
    line(std::string(label_get(0x6A4 / 4)) + ": " + paint);
    char num[32];
    s = std::string(label_get(0x684 / 4)) + ": ";
    if (EditorBrush2 == 1) {
        s += label_get(0x688 / 4);
    } else {
        std::snprintf(num, sizeof(num), "%d", EditorBrush2 == 0 ? 0 : EditorBrush2 - 1);
        s += num;
    }
    line(s);
    std::snprintf(num, sizeof(num), "%dx%d", MapAreaX / 2, MapAreaY);
    line(std::string(label_get(0xD7C / 4)) + ": " + num + " " + label_get((MapToggleFlat & 1) ? 0xD80 / 4 : 0xD84 / 4));
    line(std::string(label_get(0x84 / 4)) + ": " + "{" + label_get(0x50 / 4 + MapCloudCover) + "}");
    int32_t lock = (ui_key_state(0x91) & 1) ? 0xF50 : (Console_edit_lock(&MainConsole) ? 0x1020 : 0xF54);
    line(std::string("{") + label_get(lock / 4) + "}");
}

}  // namespace

// EditorPanelWin's slots 1 (show) and 2 (hide): the 0x3EA mode button.
void ui_editor_panel_show(bool show) {
    g_shown = show;
    infowin_editor_button(show);
}

// MainInterface_draw_edit_pane (0x45B550): the text, drawn at each console redraw (editpane_compose).
void THISCALL MainInterface_draw_edit_pane(uint8_t*) { ui_invalidate(); }

void editpane_group_set(int32_t group, int32_t id) {
    g_group[group == 3] = id;
    ui_invalidate();
}

// MainInterface_on_redraw (mode 0x3EA): the boxes, the buttons shown, the text.
void editpane_compose(Buffer* mi) {
    if (mi_mode() != 0x3EA) return;
    ensure_layout();
    if (!g_laid) return;
    Buffer_box_sprite(mi, &rect(kPaintBox), box_def(3));
    if (ScreenWidth != 800) Buffer_box_sprite(mi, &rect(kBrushBox), box_def(3));
    for (int32_t i = 0; i < button_count(); i++) {
        BaseButton& b = g_btn[i];
        bw_button_set(&b, b.id == g_group[i >= 25]);
        bw_button_draw(&b);
        for (int32_t y = 0; y < b.canvas.height; y++) {
            int32_t dy = b.r.top + y;
            if (dy < 0 || dy >= mi->height) continue;
            const uint8_t* s = b.canvas.data + (size_t)y * b.canvas.stride;
            uint8_t* d = mi->data + (size_t)dy * mi->stride;
            for (int32_t x = 0; x < b.canvas.width; x++) {
                int32_t dx = b.r.left + x;
                if (dx >= 0 && dx < mi->width && s[x] != kKey) d[dx] = s[x];
            }
        }
    }
    draw_text(mi);
}

// MainInterface_on_iface_group_clicked (0x45CCB0), groups 2 and 3; a right click: the bubble.
bool editpane_mouse(int32_t type, int32_t x, int32_t y) {
    if (mi_mode() != 0x3EA || !g_laid) return false;
    for (int32_t i = 0; i < button_count(); i++) {
        BaseButton& b = g_btn[i];
        if (x < b.r.left || x >= b.r.right || y < b.r.top || y >= b.r.bottom) continue;
        if (type == UiInput::RUP) {
            const UiRect& pos = mi_rect(kMiPos);
            ui_bubble_set(b.bubble.c_str(), pos.left + x, pos.top + y);
            return true;
        }
        if (type != UiInput::LUP) return true;
        if (InfcList[InfcUsed] != 0) return true;
        const int32_t id = b.id;
        if (i < 25) {
            if (id - 1000 > 0x10) Console_editor_paints(terraform_of(id - 0x3F9) + 6);
            else Console_editor_paints(id - 0x3F3);
        } else {
            EditorBrush2 = id - 0x40C;
            g_group[1] = id;
            std::fprintf(stderr, "editor paint %d brush %d\n", EditorPaint2, EditorBrush2);
            MainInterface_draw_edit_pane(&MainInterfaceWin);
        }
        return true;
    }
    return false;
}

void editpane_log_state() {
    std::fprintf(stderr, "editpane shown %d mode %d paint %d (button %d) brush %d (button %d)\n", g_shown ? 1 : 0, mi_mode(),
                 EditorPaint2, g_group[0], EditorBrush2, g_group[1]);
    if (g_laid)
        for (int32_t i = 0; i < button_count(); i++)
            std::fprintf(stderr, "editpane button %X at %d,%d %dx%d: %s\n", g_btn[i].id, g_btn[i].r.left, g_btn[i].r.top,
                         g_btn[i].r.right - g_btn[i].r.left, g_btn[i].r.bottom - g_btn[i].r.top, g_btn[i].bubble.c_str());
}
