// BattleWin (ui 0x4219B0..0x422EE0): the battle report in the console during a combat (interface
// mode 10): the two factions' emblems and names in the left pane, the attacker's and the
// defender's columns in the centre pane (slid down over the stack row: slide_show(0, 2), the
// iface_down picture), with the units, their statistics, weapon / armour strength, morale and
// the combat modifiers, strength and hit points; and SubInterface_set_iface_mode /
// release_iface_mode (0x45D310 / 0x45D380) for it.
//
// From the asm: BattleWin_init's rectangles, BattleWin_clear, BattleWin_on_status (the console
// slid down / up), BattleWin_battle_report, BattleWin_pulse_timer (the report ends 5 s later;
// unattended runs end it at once) and BattleWin_stop_timer; slide_show / slide_hide's console
// part (flag 2: the iface_down / iface_up picture at +0xD3C, MiSlideState).
// Not reconstructed: native life's pictures in the emblem boxes (the sprites at 0x76DC30.. are
// not loaded: none drawn), the show / hide of the menu, commlink and globe buttons around the report
// (they stay as they are), the arms' slide of slide_show's window part. SMACX_BATTLE_SHOT=FILE: a
// screenshot after each report (test aid).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/globals.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console_frame.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/sprites.h"
#include "ui/statuswin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

namespace {

int32_t& bf(uint32_t off) { return *reinterpret_cast<int32_t*>(&BattleWin + off); }
UiRect& br(uint32_t off) { return *reinterpret_cast<UiRect*>(&BattleWin + off); }
Buffer* canvas() { return basewin_mi_canvas(); }
int32_t mem_i(uint32_t a) { return *reinterpret_cast<int32_t*>(mem::host(a)); }

bool g_init = false;
uint32_t g_deadline = 0;           // Time_pulse(BattleWin_timer_callback_daemon_2, 5000)
std::vector<uint8_t> g_snapshot;   // the main interface's canvas with the report
Sprite g_slide[2];                 // iface_up / iface_down (MainInterface +0x22F30 / +0x22F5C)
bool g_slide_loaded = false;

// BattleWin_init (0x4219E0): the rectangles from the left pane (+0xD80) and the slid centre pane
// (+0xDC0): +0x30 / +0x40 the two columns, +0x50 / +0x60 the two emblems.
void battlewin_init() {
    if (g_init) return;
    g_init = true;
    maininfc_geometry();
    const UiRect L = mi_rect(kMiPaneQueue), C = mi_rect(kMiPaneCenter2);
    br(0x70) = L;
    br(0x90) = C;
    int32_t l = C.left + 4, t = C.top + 4, b = C.bottom - 4;
    int32_t m = ((C.right - l) - 1) / 2 - 3 + l;
    br(0x30) = {l, t, m, b};
    int32_t d = (m - l) + 3;
    br(0x40) = {l + d, t, m + d, b};
    l = L.left + 4;
    t = L.top + 4;
    m = ((L.right - l) - 1) / 2 - 3 + l;
    bf(4) = 10;
    br(0x50) = {l, t, m, L.bottom - 4};
    br(0x60) = {m + 3, t, L.right - 4, L.bottom - 4};
    // MainInterface_init's +0xD3C: where slide_show / slide_hide put the console's piece.
    const UiRect& pos = mi_rect(kMiPos);
    int32_t h = pos.bottom - pos.top;
    mi_rect(0xD3C) = {0xF4, h - 0x60, 0x30B, h};
}

void load_slide() {
    if (g_slide_loaded) return;
    g_slide_loaded = true;
    const char* files[2] = {ScreenWidth == 800 ? "iface_up_A.pcx" : "iface_up2_A.pcx",
                            ScreenWidth == 800 ? "iface_down_A.pcx" : "iface_down2_A.pcx"};
    for (int32_t i = 0; i < 2; i++) {
        Buffer b;
        Buffer_ctor(&b);
        if (Buffer_load_pcx(&b, files[i], nullptr, 10, 0xEC)) continue;
        pfncSpriteFromCanvasRectTrans(&g_slide[i], &b, 9, 0, 0, b.width, b.height, 0);
    }
}

void snapshot() {
    Buffer* c = canvas();
    if (c->data) g_snapshot.assign(c->data, c->data + (size_t)c->stride * c->height);
    ui_invalidate();
}

// The sprite of a faction's emblem (BaseSprites' 27th of the faction PCX), or native life's.
Sprite* emblem(int32_t faction, int32_t unit_id) {
    if (faction) return &BaseSprites[faction & 7][26];
    switch (unit_id) {
    case 9: return ArtSprite(0x76DC30);
    case 10: return ArtSprite(0x78A4A0);
    case 0xE: return ArtSprite(0x779570);
    case 0xF: return ArtSprite(0x7AC000);
    case 0x13: return ArtSprite(0x787E70);
    default: return ArtSprite(0x779200);
    }
}

void cat(std::string& s, int32_t v) { s += std::to_string(v); }

}  // namespace

// slide_show / slide_hide's console part (flag 2): the console with the centre pane slid down
// over the stack row (iface_down) or back (iface_up), at +0xD3C.
void mi_slide(bool down) {
    load_slide();
    MiSlideState = down ? 1 : 0;
    ui_invalidate();
}

void mi_slide_draw(Buffer* mi) {
    if (!MiSlideState) return;
    battlewin_init();
    load_slide();
    Sprite* s = &g_slide[1];
    if (!s->width) return;
    const UiRect& r = mi_rect(0xD3C);
    BlitDestScale = BlitSourceScale = 1;
    pfncSpriteStretchCopyToCanvas(s, mi, s->key, r.left, r.top);
}

bool battlewin_shown() { return InfcList[InfcUsed] == 10 && !g_snapshot.empty(); }

void battlewin_compose(Buffer* mi) {
    if (g_snapshot.size() == (size_t)mi->stride * mi->height) std::memcpy(mi->data, g_snapshot.data(), g_snapshot.size());
}

void battlewin_tick() {
    if (g_deadline && (int32_t)(platform::ticks_ms() - g_deadline) >= 0) {
        g_deadline = 0;
        SubInterface_release_iface_mode(&BattleWin);  // BattleWin_timer_callback_daemon_2
    }
}

// BattleWin_on_status (0x422CC0): 1 the console slid down for the report, 0 back up.
void THISCALL BattleWin_on_status(uint8_t*, int32_t on) {
    if (on) {
        mi_slide(true);
        maininfc_set_date();
    } else {
        mi_slide(false);
        g_snapshot.clear();
    }
    ui_invalidate();
}

// SubInterface_set_iface_mode (0x45D310): the window's mode on top of the interface stack (mode
// 10, the battle, only over mode 0), its on_status(1).
void THISCALL SubInterface_set_iface_mode(uint8_t* win) {
    battlewin_init();
    int32_t mode = *reinterpret_cast<int32_t*>(win + 4), cur = InfcList[InfcUsed];
    if (cur == mode || (mode == 10 && cur != 0)) return;
    if (win == &BattleWin) {
        // The console as it is (before the mode changes), then the battle's state.
        Buffer* c = canvas();
        if (c->data && ui_screen_canvas() && ui_screen_canvas()->data) {
            console_frame_draw(ui_screen_canvas());
            snapshot();
        }
    }
    InfcUsed++;
    InfcList[InfcUsed] = mode;
    if (win == &BattleWin) BattleWin_on_status(win, 1);
}

// SubInterface_release_iface_mode (0x45D380).
void THISCALL SubInterface_release_iface_mode(uint8_t* win) {
    int32_t mode = *reinterpret_cast<int32_t*>(win + 4);
    if (!InfcUsed) return;
    if (InfcList[InfcUsed] == mode) {
        InfcUsed--;
        if (win == &BattleWin && InfcList[InfcUsed] != mode) BattleWin_on_status(win, 0);
        return;
    }
    iface_release_mode(mode);
}

// BattleWin_clear (0x422C50): the four boxes empty.
void THISCALL BattleWin_clear(uint8_t*) {
    battlewin_init();
    Buffer* c = canvas();
    if (!c->data || g_snapshot.empty()) return;
    std::memcpy(c->data, g_snapshot.data(), g_snapshot.size());
    mi_slide_draw(c);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    mi_restore_back(0);
    for (uint32_t off : {0x30u, 0x40u, 0x50u, 0x60u}) Buffer_box_sprite(c, &br(off), box_def(0));
    maininfc_redraw_complete();
    snapshot();
}

void THISCALL BattleWin_pulse_timer(uint8_t*) {
    if (g_ui_exit_when_script_done) {  // unattended: the report ends at once
        g_deadline = 0;
        SubInterface_release_iface_mode(&BattleWin);
        return;
    }
    g_deadline = platform::ticks_ms() + 5000;
}

void THISCALL BattleWin_stop_timer(uint8_t*) { g_deadline = 0; }

// BattleWin_battle_report (0x421B50): the report of veh_atk against veh_def with their strengths
// (8.8 fixed point; -1: not known yet); flags 2: an artillery duel.
void THISCALL BattleWin_battle_report(uint8_t*, int32_t atk, int32_t def, int32_t off, int32_t dstr, uint32_t flags) {
    if (InfcList[InfcUsed] != 10) return;
    battlewin_init();
    Buffer* c = canvas();
    if (!c->data) return;
    bool big = ScreenWidth != 800;
    for (uint32_t o : {0x30u, 0x40u, 0x50u, 0x60u}) Buffer_box_sprite(c, &br(o), box_def(0));
    maininfc_redraw_complete();
    int32_t picw = big ? 0x4B : 0x32;
    int32_t size = big ? 12 : 10;
    Font fn, fi, fb;
    Font_ctor(&fn);
    Font_ctor(&fi);
    Font_ctor(&fb);
    Font_init(&fn, "Arial", size, 0);
    Font_init(&fi, "Arial", size, 2);
    Font_init(&fb, "Arial", size, 1);
    const UNIT& ua = Units[Vehs[atk].unit_id];
    const UNIT& ud = Units[Vehs[def].unit_id];
    bool probes = ua.plan == 0xB && ud.plan == 0xB;
    bool psi = off >= 0 && dstr >= 0 &&
               (Weapon[ua.weapon_id].offense_value < 0 || Armor[ud.armor_id].defense_value < 0 || Vehs[atk].faction_id == 0);
    int32_t fa = Vehs[atk].faction_id, fd = Vehs[def].faction_id;
    // The emblems and the factions' names.
    Buffer_set_font(c, find_font(0xE, 1), nullptr, nullptr, nullptr);
    for (int32_t side = 0; side < 2; side++) {
        int32_t f = side ? fd : fa, unit = side ? Vehs[def].unit_id : Vehs[atk].unit_id;
        const UiRect& r = br(side ? 0x60 : 0x50);
        Sprite* s = emblem(f, unit);
        int32_t x = ((r.right - r.left) - s->full_width) / 2 + r.left;
        int32_t y = (r.top - r.left) + x;
        BlitDestScale = BlitSourceScale = 1;
        if (s->width) sprite_copy1(s, c, s->key, x, y, 1, 1);
        UiRect t{r.left + 3, s->full_height + 3 + y, r.right - 3, r.bottom - 3};
        Buffer_set_text_color(c, FactionColorText[f], FactionColorShadow[f], 1, 1);
        Buffer_wrap_cent_6(c, MFactions[f].formal_name_faction, &t);
    }
    // The two columns.
    for (int32_t side = 0; side < 2; side++) {
        int32_t v = side ? def : atk;
        bool dside = side == 1;
        const UiRect& rr = br(side ? 0x40 : 0x30);
        UiRect inner{rr.left + 3, rr.top + 3, rr.right - 3, rr.bottom - 3};
        int32_t w = inner.right - inner.left, x0 = inner.left, top = inner.top;
        Buffer_set_clip(c, &inner);
        const UNIT& u = Units[Vehs[v].unit_id];
        auto hit_points = [&]() {
            int32_t hp = u.plan == 0xC ? 1 : std::max(1, std::min(100, (int32_t)u.reactor_id)) * 10;
            return std::max(0, std::min(9999, hp - (int32_t)Vehs[v].damage_taken));
        };
        int32_t half = w / 2;
        if (big) {
            veh_draw(c, v, x0 + (half - picw) / 2, top, 0, 0, 0);
            if (hit_points() == 0) {
                Buffer_set_font(c, &fi, nullptr, nullptr, nullptr);
                Buffer_set_text_color(c, 0x6A, 0, 1, 1);
                const char* t = label_get((u.plan == 0xC ? 0x4A0 : Vehs[v].faction_id == 0 ? 0x49C : 0x498) / 4);
                Buffer_write_cent_l_5(c, t, x0, picw / 4 + top, half, (int32_t)crt_strlen(t));
            }
        }
        int32_t y = (big ? picw : 4) + top;
        std::string s;
        int32_t vf = Vehs[v].faction_id;
        if (vf && vf != CurrentPlayerFaction && GameLanguage == 0) s += std::string(MFactions[vf].adj_name_faction) + " ";
        s += u.name;
        if (vf && vf != CurrentPlayerFaction && GameLanguage != 0) {
            PluralDefault = MFactions[vf].is_noun_plural;
            GenderDefault = MFactions[vf].noun_gender;
            s += std::string(" (") + MFactions[vf].noun_faction + ")";
        }
        if (GameState & STATE_OMNISCIENT_VIEW) s += "(" + std::to_string(v) + ")";
        Buffer_set_font(c, find_font(big ? 0x10 : 0xC, 1), nullptr, nullptr, nullptr);
        int32_t cf = dside ? fd : fa;
        Buffer_set_text_color(c, FactionColorText[cf], FactionColorShadow[cf], 1, 1);
        y = Buffer_wrap_cent_4(c, s.c_str(), x0, y, big ? half : w);
        Buffer_set_font(c, &fn, &fi, &fb, nullptr);
        Buffer_set_text_color(c, mem_i(0x68A5A4), -1, 1, 1);
        // ({} values: the second colour as the canvas last had it, the info pane's)
        Buffer_set_text_color2(c, mem_i(0x8C6BC4), mem_i(0x8C6BC8), 1, 1);
        int32_t sx = x0;
        if (big) {
            sx = x0 + w / 2;
            y = top + 5;
        }
        auto wrap = [&](const std::string& t) {
            UiRect r{sx, y, sx + half, 0x7FFF};
            y = t.empty() ? y : Buffer_wrap_2(c, t.c_str(), &r);
        };
        char st[256] = "";
        say_stats_short(st, Vehs[v].unit_id);
        wrap(st);
        bool armor = dside && (!(flags & 2) || Chassis[u.chassis_id].triad == 1);
        if (!probes) {
            s = psi ? label_get(0x310 / 4)
                    : Strings_get(&StringTable, armor ? Armor[u.armor_id].name : Weapon[u.weapon_id].name);
            if (off >= 0 && dstr >= 0) {
                s += ": ";
                int32_t triad = Chassis[Units[Vehs[def].unit_id].chassis_id].triad;
                if (armor) cat(s, psi ? Rules.psi_combat_ratio_def[triad] : armor_proto(Vehs[v].unit_id, -1, 0));
                else cat(s, psi ? Rules.psi_combat_ratio_atk[triad] : offense_proto(Vehs[v].unit_id, -1, 0));
                wrap(s);
            }
        }
        char mb[256];
        std::memcpy(mb, StrBuffer, sizeof(mb));
        StrBuffer[0] = 0;
        say_morale_2(v, dside ? fa : fd);  // morale against the opponent's faction
        s = StrBuffer;
        std::memcpy(StrBuffer, mb, sizeof(mb));
        s += ": ";
        int32_t pct = ((VehBasicBattleMorale[dside] - 2) * 100) / 8;
        if (pct >= 0) s += "+";
        cat(s, pct);
        s += "%  ";
        wrap(s);
        for (int32_t i = 0; i < VehBattleModCount[dside] && i < 4; i++) {
            s = std::string(VehBattleDisplay[dside][i]) + " ";
            if (VehBattleModifier[dside][i] >= 0) s += "+";
            cat(s, VehBattleModifier[dside][i]);
            s += "%";
            wrap(s);
        }
        int32_t str = dside ? dstr : off;
        s = std::string(label_get(0x520 / 4)) + ": {";
        cat(s, str >> 8);
        s += ".";
        int32_t frac = (int32_t)((uint32_t)(str & 0xFF) * 100) >> 8;
        if (frac < 10) s += "0";
        cat(s, frac);
        s += "}";
        wrap(s);
        s = std::string(label_get(0x1E4 / 4)) + ": {";
        cat(s, hit_points());
        s += "}";
        wrap(s);
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    BlitDestScale = BlitSourceScale = 1;
    snapshot();
    do_non_input();
    g_deadline = g_ui_exit_when_script_done ? 0 : platform::ticks_ms() + 5000;  // Time_pulse
    std::fprintf(stderr, "battle report: %d (%s) vs %d (%s) strength %d / %d\n", atk, ua.name, def, ud.name, off, dstr);
    if (const char* shot = std::getenv("SMACX_BATTLE_SHOT")) ui_screenshot(g_ui_outdir + "/" + shot);  // test aid
}

// ---- the headless "move" area (src/headless/ui_headless_move.cpp) ----
// say_morale_2 (0x4B43C0): say_morale into StrBuffer.
void GAMEAPI say_morale_2(int32_t veh_id, int32_t faction_id) { statuswin_say_morale(StrBuffer, veh_id, faction_id); }
// The ctrl key as the engine asks (as headless: not held).
bool ui_ctrl_pressed() { return false; }
