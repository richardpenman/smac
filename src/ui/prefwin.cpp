// The preferences dialog (PrefWin, ui 0x48D840..0x492410; PrefWin_display from Console_set_preferences
// ctrl+P, Console_set_adv_preferences ctrl+O, Console_set_base_preferences ctrl+W,
// Console_set_auto_preferences ctrl+A, Console_set_audiovisual ctrl+I, Console_set_map_display
// ctrl+M) and the Game menu's Resign (Console_retire_game 0x514B30) and the scenario editor's
// custom rules (Console_editor_rules 0x4DC230).
//
// PrefWin: the six pages (BASICPREFS, ADVANCEDPREFS, BASEWARNINGS, AUTOMATIONPREFS,
// AUDIOVISUALPREFS, MAPPREFS of the script: their items are the check boxes, with the original's
// check box ids), the page buttons (labels 750..754, 1077), Gamma Correction (label 1078), Restore
// Defaults (1079), OK and Cancel. PrefWin_display copies GamePreferences / GameMorePreferences /
// GameWarnings; PrefWin_get_preferences sets the boxes from them and PrefWin_set_preferences
// (on OK) builds the three words from the boxes alone (bits no box stands for are dropped, as the
// original), then PrefWin_destroy: AlphaIniPrefs, prefs_use, prefs_save(0) (Alpha Centauri.Ini).
// Keys: Enter OK, Esc Cancel, Up / Down the page.
// The audio page (PrefWin_init): its first three items (music, sound effects, voiceovers) are not
// check boxes but the volume buttons Main / Voice / SFX / Music On|Off (ids 0x55, 0x57, 0x56,
// 0x58, labels 1080..1087; PrefWin_change_volume_button_state / _text) with their sliders (0x50
// master, 0x51 voices, 0x52 effects, 0x53 music, 0..127; PrefWin_on_scrolling sets the volumes at
// once); the button states make GamePreferences 0x800 / 0x400 and GameMorePreferences 0x4000 on OK,
// and PrefWin_destroy writes Main / Voice / Music / SFX Volume to the preferences.
// Approximated: the layout (measured on the original's screen at 1024x768, not PrefWin_init's
// rectangles; the volume rows below the page's check boxes). The gamma correction dialog:
// GammaWin below.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/globals.h"
#include "engine/prefs.h"
#include "engine/save.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/rules.h"
#include "engine/ui_bridge.h"
#include "engine/log.h"
#include "platform/ui_platform.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/sound.h"
#include "ui/win.h"

namespace {

int32_t mem_i32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }

// PrefWin_set_preferences / PrefWin_get_preferences: check box id -> word (P GamePreferences,
// M GameMorePreferences, W GameWarnings), bit, inverted (checked when the bit is clear).
struct Bit {
    int32_t id;
    char word;
    uint32_t bit;
    bool inverted;
};
const Bit kBits[] = {
    {0x7, 'P', 0x20, 0},        {0x8, 'P', 0x4000, 0},     {0x9, 'M', 0x100000, 0},    {0xa, 'P', 0x1000, 0},
    {0xb, 'P', 0x2, 0},         {0xc, 'P', 0x1, 0},        {0xd, 'P', 0x4, 1},         {0xe, 'P', 0x8000, 1},
    {0xf, 'P', 0x20000000, 1},  {0x10, 'M', 0x40000, 0},   {0x11, 'M', 0x8000, 0},     {0x12, 'M', 0x10, 0},
    {0x13, 'P', 0x8, 0},        {0x1c, 'M', 0x40, 0},      {0x14, 'M', 0x80, 0},       {0x15, 'P', 0x80000, 0},
    {0x17, 'M', 0x4, 0},        {0x18, 'M', 0x200000, 0},  {0x19, 'M', 0x400, 0},      {0x1a, 'M', 0x100, 0},
    {0x1d, 'P', 0x10000, 0},    {0x1e, 'P', 0x1000000, 0}, {0x1f, 'P', 0x20000, 0},    {0x20, 'P', 0x2000000, 0},
    {0x2a, 'M', 0x400000, 1},   {0x21, 'M', 0x8, 0},       {0x22, 'M', 0x20, 0},       {0x23, 'P', 0x100000, 0},
    {0x24, 'P', 0x200000, 0},   {0x25, 'P', 0x400000, 0},  {0x26, 'P', 0x800000, 0},   {0x27, 'P', 0x40000000, 0},
    {0x28, 'P', 0x80000000, 0}, {0x29, 'M', 0x800, 0},     {0x3d, 'P', 0x800, 0},      {0x3e, 'P', 0x400, 0},
    {0x3f, 'M', 0x4000, 0},     {0x40, 'M', 0x20000, 0},   {0x41, 'P', 0x10000000, 0}, {0x42, 'P', 0x40000, 1},
    {0x47, 'M', 0x1000000, 1},  {0x43, 'P', 0x80, 0},      {0x44, 'P', 0x8000000, 0},  {0x45, 'M', 0x80000, 0},
    {0x16, 'M', 0x200, 0},      {0x2b, 'W', 0x1, 0},       {0x2c, 'W', 0x2, 0},        {0x2d, 'W', 0x400, 0},
    {0x2e, 'W', 0x4, 0},        {0x2f, 'W', 0x2000, 0},    {0x30, 'W', 0x8, 0},        {0x31, 'W', 0x10, 0},
    {0x32, 'W', 0x20, 0},       {0x33, 'W', 0x40, 0},      {0x34, 'W', 0x80, 0},       {0x35, 'W', 0x100, 0},
    {0x36, 'W', 0x200, 0},      {0x37, 'W', 0x800, 0},     {0x38, 'W', 0x1000, 0},     {0x39, 'W', 0x4000, 0},
    {0x3a, 'W', 0x8000, 0},     {0x3b, 'W', 0x10000, 0},   {0x3c, 'W', 0x20000, 0},    {0x48, 'M', 0x1, 0},
    {0x49, 'P', 0x100, 0},      {0x4a, 'P', 0x200, 0},     {0x4b, 'M', 0x2000, 0},     {0x4c, 'M', 0x1000, 0},
    {0x4d, 'M', 0x10000, 0},    {0x4e, 'M', 0x800000, 0},  {0x4f, 'M', 0x2000000, 1}};

// PrefWin_init: the pages' script sections and the ids of their items in order.
struct Page {
    const char* section;
    int32_t label;
    std::vector<int32_t> ids;
};
const Page kPages[6] = {
    {"BASICPREFS", 750, {0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe}},
    {"ADVANCEDPREFS", 751, {0xf, 0x10, 0x11, 0x12, 0x13, 0x1c, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1a}},
    {"BASEWARNINGS",
     752,
     {0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c}},
    {"AUTOMATIONPREFS", 753, {0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x2a, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29}},
    {"AUDIOVISUALPREFS", 754, {0x40, 0x41, 0x42, 0x47, 0x43, 0x44, 0x45, 0x16}},
    {"MAPPREFS", 1077, {0x48, 0x49, 0x4e, 0x4a, 0x4b, 0x4c, 0x4d, 0x4f}},
};

// The gamma correction dialog (FUN_005C8E60 with FUN_005C9420 / 5C9520 / 5C95E0, PrefWin button
// 6): a full-screen window of colour 0xF8, centred: the ramp of the game colours 10..245 (one
// pixel each, 0xEC x 0x20), below it the value ("%f"), below that the scroll bar (range 1..400,
// the gamma in hundredths: no sprites, buttons of colour 7, a two-pixel bevel 7 / 0) and OK /
// CANCEL (BasePop's texts, colour 0xF8, black text). While it is open the window's colours 10..245
// are the grey ramp corrected by the scroll's gamma (FUN_005C9520: pow(i / 235, 1 / gamma)); OK
// keeps the gamma (prefs "Gamma Correction", the game palette reloaded with it), CANCEL the old
// one. Approximated: the scroll bar's thumb is not dragged (its buttons and track are clicked;
// Left / Right step it).
struct GammaWin : ScreenWin {
    int32_t pos = 100, old = 100;
    bool ok = false;
    UiRect ramp{}, text{}, bar{}, okr{}, cancelr{};
    Palette pal{};
    Font font{};

    void layout() {
        int32_t w = 0xEC, h = 0x20, x0 = (ScreenWidth - w) / 2, y0 = (ScreenHeight - h) / 2;
        ramp = {x0, y0, x0 + w, y0 + h};
        text = {x0, y0 + h + 3, x0 + w, y0 + 2 * h + 3};
        bar = {x0, text.bottom + 3 + 4, x0 + w, text.bottom + 3 + h - 4};
        int32_t by = bar.bottom + 3 + 4;
        okr = {x0, by, (w + 3) / 2 - 3 + x0, by + 0x14};
        cancelr = {okr.right + 3, by, x0 + w, by + 0x14};
    }
    void set_palette() {  // FUN_005C9520
        pal = GamePalette;
        double g = pos * 0.01;
        for (int32_t i = 0; i < 0xEC; i++) {
            double v = std::pow(i * 0.00425531914893617, 1.0 / g);
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            uint8_t c = (uint8_t)(int32_t)(v * 235.99);
            pal.rgb[10 + i][0] = pal.rgb[10 + i][1] = pal.rgb[10 + i][2] = c;
        }
        ui_set_layer_palette(3, &pal);
    }
    void bevel_box(const UiRect& r, int32_t fill) {
        UiRect rr = r;
        Buffer_fill_3(&canvas, &rr, fill);
        for (int32_t i = 0; i < 2; i++) {
            Buffer_box_2(&canvas, &rr, 7, 0);
            rr = {rr.left + 1, rr.top + 1, rr.right - 1, rr.bottom - 1};
        }
    }
    void redraw() override {
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 0xF8);
        for (int32_t i = 10; i < 0xF6; i++)
            Buffer_line_2(&canvas, ramp.left + i - 10, ramp.top, ramp.left + i - 10, ramp.bottom, i);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%f", pos * 0.01);
        sw_font(&canvas, &font, 0);
        Buffer_write_cent_l_7(&canvas, buf, &text, (int32_t)std::strlen(buf));
        texts.push_back(buf);
        // The scroll bar: the track, the buttons at both ends, the thumb (squares of its height).
        int32_t h = bar.bottom - bar.top;
        UiRect track = bar;
        Buffer_fill_3(&canvas, &track, 0xF8);
        bevel_box({bar.left, bar.top, bar.left + h, bar.bottom}, 7);
        bevel_box({bar.right - h, bar.top, bar.right, bar.bottom}, 7);
        int32_t tx = bar.left + h + 1 + ((pos - 1) * ((bar.right - bar.left) - 3 * h - 1)) / 399;
        bevel_box({tx, bar.top, tx + h, bar.bottom}, 7);
        for (const UiRect* r : {&okr, &cancelr}) {
            bevel_box(*r, 0xF8);
            std::string t = r == &okr ? "OK" : sw_upper(label_get(102));
            sw_font(&canvas, &font, 0);
            Buffer_write_cent_l_7(&canvas, t.c_str(), r, (int32_t)t.size());
        }
        spot_init(&spots[0], 8);
        spot_add(&spots[0], 1, 0, bar.left, bar.top, h, h);
        spot_add(&spots[0], 2, 0, bar.right - h, bar.top, h, h);
        spot_add(&spots[0], 3, 0, bar.left + h, bar.top, tx - bar.left - h, h);
        spot_add(&spots[0], 4, 0, tx + h, bar.top, bar.right - h - tx - h, h);
        spot_add(&spots[0], 5, 0, okr.left, okr.top, okr.right - okr.left, okr.bottom - okr.top);
        spot_add(&spots[0], 6, 0, cancelr.left, cancelr.top, cancelr.right - cancelr.left,
                 cancelr.bottom - cancelr.top);
    }
    void step(int32_t d) {
        pos = std::max(1, std::min(400, pos + d));
        set_palette();
    }
    void on_click(int32_t, int32_t index, int32_t, int32_t, int32_t, bool right, bool) override {
        if (right) return;
        switch (index) {
        case 1: step(-1); break;
        case 2: step(1); break;
        case 3: step(-10); break;
        case 4: step(10); break;
        case 5:
            ok = true;
            sw_close(this);
            break;
        case 6: sw_close(this); break;
        default: break;
        }
    }
    void on_key(int32_t key) override {
        int32_t vk = key & 0xFFFF;
        if (vk == 0x25) step(-1);
        else if (vk == 0x27) step(1);
        else if (vk == 0xD) {
            ok = true;
            sw_close(this);
        } else if (vk == 0x1B) {
            sw_close(this);
        }
    }
    void log_state() override { std::fprintf(stderr, "gammastate pos %d old %d\n", pos, old); }
};

// PrefWin button 6: the dialog, then the gamma kept (DAT_006846A8, "Gamma Correction").
void gamma_dialog() {
    GammaWin w;
    w.name = "gamma";
    w.old = w.pos = std::max(1, std::min(400, (int32_t)prefs_get_int("Gamma Correction", 100, 0)));
    Font_ctor(&w.font);
    Font_init(&w.font, "Arial", 0xE, 1);
    w.layout();
    sw_init(&w, 0, 0, ScreenWidth, ScreenHeight, false);
    w.layer = 3;
    w.set_palette();
    sw_run(&w);
    ui_set_layer_palette(3, nullptr);
    int32_t g = w.ok ? w.pos : w.old;
    std::fprintf(stderr, "gamma: %d%s\n", g, w.ok ? " (OK)" : " (cancelled)");
    if (w.ok) {
        prefs_put_int("Gamma Correction", (uint32_t)g, 0);
        load_palette();
    }
    ui_invalidate();
}

struct PrefWin : ScreenWin {
    int32_t page = 0;  // +0xA14
    uint32_t words[3] = {};  // P, M, W
    bool state[0x60] = {};  // the check boxes by id
    bool apply = false;  // +0xA5C
    std::vector<std::string> items[6];
    std::vector<std::string> notes[6];  // text lines of a page (MAPPREFS)
    Font font, small;
    UiRect panel{};
    int32_t rows_y = 0;
    // The audio page: master / sfx / voice / music switches (+0x15820, +0x1636C, +0x16EB8, +0x17A04)
    // and the four sliders' positions.
    int32_t vol_on[4] = {1, 1, 1, 1};  // by button 0x55, 0x56, 0x57, 0x58
    int32_t volume[4] = {};  // by slider 0x50..0x53
    BaseButton* vol_button[4] = {};
    UiRect slider[4] = {};
    int32_t dragging = -1;

    uint32_t& word(char w) { return words[w == 'P' ? 0 : (w == 'M' ? 1 : 2)]; }
    void get_preferences() {
        for (const Bit& b : kBits) state[b.id] = ((word(b.word) & b.bit) != 0) != b.inverted;
        vol_on[3] = state[0x3d];  // music: 0x58
        vol_on[1] = state[0x3e];  // effects: 0x56
        vol_on[2] = state[0x3f];  // voices: 0x57
        button_texts();
    }
    void set_preferences() {
        state[0x3d] = vol_on[3] != 0;
        state[0x3e] = vol_on[1] != 0;
        state[0x3f] = vol_on[2] != 0;
        words[0] = words[1] = words[2] = 0;
        for (const Bit& b : kBits)
            if (state[b.id] != b.inverted) word(b.word) |= b.bit;
    }
    // PrefWin_change_volume_button_text: "<name> Off" / "<name> On" (labels 1080..1087).
    void button_texts() {
        static const int32_t kOff[4] = {1080, 1084, 1082, 1086};
        for (int32_t i = 0; i < 4; i++)
            if (vol_button[i]) {
                vol_button[i]->name = label_get(kOff[i] + (vol_on[i] ? 1 : 0));
                vol_button[i]->has_name = true;
            }
    }
    // PrefWin_change_volume_button_state (0x4919F0).
    void volume_button(int32_t id) {
        int32_t i = id - 0x55;
        if (i == 0) {
            if (!vol_on[0]) {
                vol_on[0] = vol_on[3] = vol_on[2] = vol_on[1] = 1;
                sound_enable_master(true);
            } else {
                vol_on[0] = vol_on[3] = vol_on[2] = vol_on[1] = 0;
                sound_enable_master(false);
                GamePreferences &= ~(PREF_AV_VOLUME_SFX_TOGGLE | PREF_AV_VOLUME_MUSIC_TOGGLE);
                GameMorePreferences &= ~MPREF_AV_VOLUME_VOICE_TOGGLE;
            }
        } else if (i == 1 || i == 2) {  // 0x56 effects (group 1), 0x57 voices (group 0)
            int32_t group = i == 1 ? 1 : 0;
            if (!vol_on[i]) {
                vol_on[i] = 1;
                vol_on[0] = 1;
                sound_enable_group(group, true);
            } else {
                sound_enable_group(group, false);
                vol_on[i] = 0;
            }
        } else if (i == 3) {
            if (vol_on[3]) {
                music_release();
                vol_on[3] = 0;
            } else {
                vol_on[3] = 1;
                vol_on[0] = 1;
                GamePreferences |= PREF_AV_VOLUME_MUSIC_TOGGLE;
                load_music(CurrentPlayerFaction);
            }
        }
        button_texts();
    }
    // PrefWin_on_scrolling (0x491380).
    void scrolled(int32_t i, int32_t v) {
        volume[i] = std::max(0, std::min(0x7F, v));
        if (i == 0) sound_set_master_volume(volume[i]);
        else if (i == 1) sound_set_group_volume(0, volume[i]);
        else if (i == 2) sound_set_group_volume(1, volume[i]);
        else music_set_volume(volume[i]);
    }
    void load() {
        for (int32_t p = 0; p < 6; p++) {
            if (text_open(ScriptFileName, kPages[p].section)) continue;
            bool list = false;
            for (char* l = text_get(); l; l = text_get()) {
                if (*l == '#') {
                    if (!list && (l[1] == 'i' || l[1] == 'I')) list = true;  // #itemlist
                    else if (list) break;
                    continue;
                }
                if (!*l) {
                    if (list && !items[p].empty()) break;
                    continue;
                }
                if (list) items[p].push_back(std::string(" ") + l);
                else notes[p].push_back(l);
            }
            text_close();
        }
        // The audio page's first three items are the volume buttons (PrefWin_init skips them).
        if (items[4].size() >= 3) items[4].erase(items[4].begin(), items[4].begin() + 3);
    }
    void layout() {
        Font_ctor(&font);
        Font_init(&font, "Arial", 0xE, 1);
        int32_t w = 0x2B9, h = 0x182;
        int32_t x = (ScreenWidth - w) / 2 + 26 + 4, y = ScreenHeight == 768 ? 150 : (ScreenHeight - h) / 2;
        sw_init(this, x, y, w, h, false);
        panel = UiRect{0xE6, 5, w - 10, h - 5};
        int32_t bx = 10, bw = 0xD6, bh = 0x13;
        for (int32_t p = 0; p < 6; p++)
            sw_button(this, label_get(kPages[p].label), p, bx, 10 + p * 22, bw, bh, 0, &font);
        sw_button(this, label_get(1078), 6, bx, 10 + 7 * 22, bw, bh, 0, &font);
        sw_button(this, label_get(1079), 0x5A, bx, h - 0x48, bw, bh, 0, &font);
        sw_button(this, sw_upper(label_get(101)).c_str(), 0x5C, bx, h - 0x31, bw, bh, 0, &font);
        sw_button(this, label_get(102), 0x5B, bx, h - 0x1A, bw, bh, 0, &font);
        // The audio page's rows (order of PrefWin_init: master, voices, effects, music).
        static const int32_t kRow[4] = {0x55, 0x57, 0x56, 0x58};
        int32_t vy = h - 4 * 0x16 - 14;
        for (int32_t r = 0; r < 4; r++) {
            int32_t i = kRow[r] - 0x55;
            vol_button[i] = sw_button(this, "", kRow[r], panel.left + 8, vy + r * 0x16, 0x6E, bh, 0, &font);
            int32_t slider_id = r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 2 : 3;  // 0x50 + slider_id
            slider[slider_id] = UiRect{panel.left + 0x80, vy + r * 0x16 + 2, panel.right - 12, vy + r * 0x16 + bh - 2};
        }
        volume[0] = prefs_get_int("Main Volume", 0x7F, 0);
        volume[1] = prefs_get_int("Voice Volume", 0x55, 0);
        volume[2] = prefs_get_int("SFX Volume", 0x5F, 0);
        volume[3] = music_loaded() ? prefs_get_int("Music Volume", 0x3F, 0) : 0;
        vol_on[0] = 1;
    }
    void redraw() override {
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 9);
        Buffer_box_sprite(&canvas, &all, box_def(9));
        UiRect in{5, 5, 0xE4, canvas.height - 5};
        Buffer_box_sprite(&canvas, &in, box_def(0));
        Buffer_box_sprite(&canvas, &panel, box_def(0));
        for (BaseButton* b : buttons) b->set_state = b->id == page ? 1 : 0;
        for (BaseButton* b : vol_button)
            if (b) b->shown = page == 4;
        spot_init(&spots[0], 0x20);
        int32_t y = panel.top + 10;
        sw_font(&canvas, &font, mem_i32(0x8C6C74), mem_i32(0x8C6C78));
        for (const std::string& n : notes[page]) {
            sw_text(this, &canvas, n, panel.left + 6, y);
            y += 24;
        }
        const std::vector<int32_t>& ids = kPages[page].ids;
        for (size_t i = 0; i < items[page].size() && i < ids.size(); i++) {
            int32_t id = ids[i];
            popup_draw_check(&canvas, panel.left + 4, y - 2, state[id] ? 2 : 0);
            sw_font(&canvas, &font, mem_i32(0x8C6C74), mem_i32(0x8C6C78));
            sw_text(this, &canvas, items[page][i], panel.left + 0x18, y);
            spot_add(&spots[0], id, 0, panel.left + 2, y - 4, panel.right - panel.left - 4, 24);
            y += 24;
        }
        if (page == 4) {
            for (int32_t i = 0; i < 4; i++) {  // the sliders (Scroll_init_horz): track and thumb
                const UiRect& r = slider[i];
                Buffer_box_sprite(&canvas, &r, box_def(0));
                int32_t span = r.right - r.left - 10;
                int32_t tx = r.left + 1 + span * volume[i] / 0x7F;
                UiRect thumb{tx, r.top + 1, tx + 8, r.bottom - 1};
                Buffer_fill_3(&canvas, &thumb, mem_i32(0x8C6C74));
                spot_add(&spots[0], 0x50 + i, 1, r.left, r.top - 2, r.right - r.left, r.bottom - r.top + 4);
            }
        }
    }
    int32_t slider_value(int32_t i, int32_t x) const {
        const UiRect& r = slider[i];
        int32_t span = std::max(1, r.right - r.left - 10);
        return (x - r.left - 5) * 0x7F / span;
    }
    void show_page(int32_t p) { page = (p + 6) % 6; }
    void close(bool ok) {
        // PrefWin_destroy.
        if (ok) {
            set_preferences();
            AlphaIniPrefs.preferences = (int32_t)words[0];
            AlphaIniPrefs.more_preferences = (int32_t)words[1];
            AlphaIniPrefs.announce = (int32_t)words[2];
            prefs_use();
            prefs_save(0);
            prefs_put_int("Main Volume", (uint32_t)volume[0], 0);
            prefs_put_int("Voice Volume", (uint32_t)volume[1], 0);
            prefs_put_int("Music Volume", (uint32_t)volume[3], 0);
            prefs_put_int("SFX Volume", (uint32_t)volume[2], 0);
            std::fprintf(stderr, "prefs: preferences %08X more %08X warnings %08X saved\n", words[0], words[1],
                         words[2]);
        }
        draw_map(1);
        GameDrawState |= 4;
        sw_close(this);
    }
    void on_button(int32_t id) override {  // PrefWin_on_button_clicked (0x491400)
        if (id >= 0 && id < 6) {
            show_page(id);
        } else if (id == 6) {
            gamma_dialog();
        } else if (id >= 0x55 && id <= 0x58) {
            volume_button(id);
        } else if (id == 0x5A) {  // Restore Defaults
            words[0] = default_prefs();
            words[1] = default_prefs2();
            words[2] = default_warn();
            get_preferences();
        } else if (id == 0x5C) {
            close(true);
        } else if (id == 0x5B) {
            close(false);
        }
    }
    void on_key(int32_t key) override {  // PrefWin_on_key_click (0x48FCD0)
        switch (key & 0xFFFF) {
        case VKEY_RETURN: close(true); break;
        case VKEY_ESCAPE: close(false); break;
        case VKEY_UP: show_page(page < 1 ? 5 : page - 1); break;
        case VKEY_DOWN: show_page(page < 5 ? page + 1 : 0); break;
        default: break;
        }
    }
    void on_click(int32_t parent, int32_t index, int32_t, int32_t x, int32_t, bool right, bool) override {
        if (parent != 0 || right || index < 0 || index >= 0x60) return;
        if (index >= 0x50 && index <= 0x53) {
            scrolled(index - 0x50, slider_value(index - 0x50, x));
            return;
        }
        state[index] = !state[index];  // CheckBox_flip_state_id
    }
    void log_state() override {
        std::fprintf(stderr, "screenstate prefwin page=%d", page);
        for (int32_t id : kPages[page].ids) std::fprintf(stderr, " %x:%d", id, state[id] ? 1 : 0);
        if (page == 4)
            std::fprintf(stderr, " buttons %d%d%d%d volumes %d %d %d %d", vol_on[0], vol_on[1], vol_on[2], vol_on[3],
                         volume[0], volume[1], volume[2], volume[3]);
        std::fprintf(stderr, "\n");
    }
};

}  // namespace

// PrefWin_display (0x48FA00): the dialog on page `page`.
void prefwin_display(int32_t page) {
    PrefWin w;
    w.name = "prefwin";
    w.slide = true;  // PrefWin_display / PrefWin_destroy: slide_show / slide_hide
    w.load();
    w.layout();
    w.words[0] = (uint32_t)GamePreferences;
    w.words[1] = (uint32_t)GameMorePreferences;
    w.words[2] = (uint32_t)GameWarnings;
    w.get_preferences();
    if (page >= 0) w.show_page(page);
    sw_run(&w);
    sw_clear_buttons(&w);
    if (AlphaIniPrefs.preferences & 0x20000000) GameDialogFlags &= ~4;
    else GameDialogFlags |= 4;
}

// ---- Console_retire_game (0x514B30): Resign ----
void THISCALL Console_retire_game(uint8_t*) {
    if (GameRules & RULES_IRONMAN) auto_save();
    const char* label = (GameRules & RULES_IRONMAN) ? "REALLYRETIRE2" : "REALLYRETIRE";
    if ((MultiplayerActive || PbemActive)) {
        int32_t humans = 0;
        for (int32_t f = 1; f < 8; f++)
            if (FactionStatus[0] & (1 << f)) humans++;
        if (humans > 1) label = "REALLYWIMPOUT";
    }
    if (!X_pop(label, 0)) return;
    if (PbemActive) {
        FactionStatus[0] &= (uint8_t)~(1 << CurrentPlayerFaction);
        save_game(0);
    }
    bool net = false;
    if (MultiplayerActive) {
        // NetDaemon_hang_up and the session's state (no network play here).
        MultiplayerActive = 0;
        net = true;
    }
    if (GameRules & RULES_IRONMAN) kill_auto_save();
    report_score(1);
    if (!(GameState & STATE_IS_SCENARIO)) {
        quayle(CurrentPlayerFaction);
        hall_of_fame(1);
    }
    if (!net) show_replay();
    ControlTurnB = !net;
    ControlTurnA = 1;
    log_say_values("Retire --------------------------------", 0, 0, 0);
}

// ---- Console_editor_rules (0x4DC230): the scenario's rules (RULES' check boxes) ----
void THISCALL Console_editor_rules(uint8_t*) {
    static const uint32_t kRuleBits[16] = {0x800,  2,     8,    4,     0x1000, 1,      0x10, 0x20,
                                           0x4000, 0x200, 0x40, 0x100, 0x2000, 0x8000, 0x80, 0x400};
    auto_undo();
    for (;;) {
        GameState = (int32_t)((uint32_t)GameState & 0xFE7FFFFFu);
        uint32_t g = (uint32_t)GameRules, c = 0;
        for (int32_t i = 0; i < 16; i++)
            if (i == 11 ? !(g & kRuleBits[i]) : (g & kRuleBits[i]) != 0) c |= 1u << i;
        DialogChoices = (int32_t)c;
        PopupDialogState = 6;
        int32_t r = X_pop_6("RULES", 0x41, 0);
        PopupDialogState = 0;
        if (r < 0) break;
        GameRules = (int32_t)((uint32_t)GameRules & 0xFFFF0000u);
        GameState = (int32_t)((uint32_t)GameState & 0xFE7FFFFFu);
        if (DialogToggle == 0) {
            uint32_t d = (uint32_t)DialogChoices;
            for (int32_t i = 0; i < 16; i++)
                if (i == 11 ? !(d & (1u << i)) : (d & (1u << i)) != 0) GameRules |= (int32_t)kRuleBits[i];
            if (d & 0x10000) GameState |= STATE_RAND_FAC_LEADER_PERSONALITIES;
            if (d & 0x20000) GameState |= STATE_RAND_FAC_LEADER_SOCIAL_AGENDA;
            break;
        }
        GameRules |= (int32_t)default_rules();  // Standard Rules
    }
    prefs_save(0);
}
