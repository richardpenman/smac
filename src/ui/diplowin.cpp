// The diplomacy dialog's interface mode (DiploPop, ui 0x43F0F0..0x440EF0) and the human side of
// the diplomacy dialog (diplomacy module): commlink_attempt (0x558C60), commlink_attempter
// (0x5589E0, the single player path), diplomacy_menu (0x54C560), proposal_menu (0x54DCF0),
// make_a_proposal (0x54F420) and battle_plans (0x54B1C0), reconstructed from the disassembly;
// communicate (0x54FFD0) is in diplo_comm.cpp.
//
// DiploPop: while a commlink is open (DiploPop_start .. DiploPop_stop) the main interface is in
// the diplomacy mode: its left pane shows the bioscan animation (dip_<a|f|m>_<mood>.flc at
// 0x1A, 0x5A of the interface; mood = DiploFriction * 3 / 20, 0..2) and, in the pane's right
// part (DiploPop_init's rectangle +0x20E4), the leader's animation (<faction>.flc, faction.flc
// when missing), the leader's name in capitals, "Attitude:" (Repute[integrity_blemishes]) and
// "Mood:" (Mood[get_mood(friction)]) as DiploPop_draw_portrait writes them. The dialog's texts
// and menus are the script popups (X_pops_11, BasePop menus, flag 0x100000) drawn into
// DiploPop's own window (700 x 0x1C2 above the console, slid up when the dialog starts:
// BasePop_set_def_external_window). The globe buttons: the world map or the detail map
// (detailmap.cpp).
// FLC files are decoded here (COLOR_256, BRUN, COPY, BLACK, SS2/DELTA_FLC, LC) and their colours
// mapped to the game palette (the original's player loads their palette into the reserved range).
// DiploPop_draw_info: the centre pane's two faction boxes (draw_info below); DiploPop's buttons
// (DATALINKS: the faction's datalink entry, PROFILE: report_intel, HQINFO: DiploPop_draw_hq's pane
// in place of the world map until a globe button is clicked), clickable while the dialog's popups
// are open (DiploPop_set_modal: the interface belongs to the modal set). The base trade menus'
// PlanWin previews (proposal_menu). Not reconstructed: the multiplayer paths (channels, diplo
// lock messages, PBEM diplo).
#include "ui/diplowin.h"
#include "ui/flic.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/compat.h"
#include "engine/council.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/turn.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/help.h"
#include "ui/colortables.h"
#include "ui/popup.h"
#include "ui/planwin.h"
#include "ui/sprites.h"
#include "ui/font_text.h"
#include "ui/win.h"
#include "ui/slide.h"
#include "ui/reports.h"
#include "ui/statuswin.h"
#include "ui/console.h"

void battle_routine(int32_t target);

namespace {

constexpr int32_t kPopWait = 0x5398E0;

// ---- DiploPop state ----
int32_t g_faction = -1;  // +0xA94: the faction on the commlink
std::string g_bioscan, g_portrait;
uint32_t g_start_ms = 0;
bool g_animate = false;

// DiploPop's own window (DiploPop_init: GraphicWin 700 x 0x1C2, 600 less the console at 800;
// Win_move to the screen's centre + 0x10 above the console; box 9 inset 0x17 / 0x37): shown and
// slid up by DiploPop_on_status(1) (slide_show flag 2), the dialog's popups drawn into it
// (BasePop_set_def_external_window).
struct DiploWindow {
    bool shown = false;
    Buffer canvas;
    int32_t x = 0, y = 0;
    uint32_t seq = 0;  // ui_window_seq() when shown
    DiploWindow() { Buffer_ctor(&canvas); }
} g_win;

int32_t console_h() { return ScreenWidth >= 1024 ? 213 : 213 * ScreenHeight / 768; }

void window_geometry(int32_t* w, int32_t* h, int32_t* x, int32_t* y) {
    *w = 700;
    *h = ScreenWidth == 800 ? 600 - console_h() : 0x1C2;
    *x = ((ScreenWidth - *w) + 0x20) / 2;
    *y = ScreenHeight - console_h() - *h;
}

// DiploPop_on_status(1): the window drawn and slid up, the console's lid (flag 2).
void window_show() {
    if (g_win.shown) return;
    int32_t w, h;
    window_geometry(&w, &h, &g_win.x, &g_win.y);
    Buffer_init(&g_win.canvas, w, h, 8, 0);
    UiRect all{0, 0, w, h};
    Buffer_set_clip(&g_win.canvas, &all);
    Buffer_fill_3(&g_win.canvas, &all, 9);
    UiRect frame{0x17, 0, w - 0x37, h};
    Buffer_box_sprite(&g_win.canvas, &frame, box_def(9));
    BasePop_set_def_external_window(true, g_win.x, g_win.y, w, h);
    multiwin_hide();  // MainInterface_on_status(0)
    slide_show_window(&g_win.canvas, g_win.x, g_win.y);
    mi_slide(true);
    g_win.shown = true;
    g_win.seq = ui_window_seq();
    std::fprintf(stderr, "diplopop window shown at %d,%d %dx%d\n", g_win.x, g_win.y, w, h);
}

// DiploPop_on_status(0): slid down out of view.
void window_hide() {
    if (!g_win.shown) return;
    g_win.shown = false;
    ui_invalidate();
    slide_hide_window(&g_win.canvas, g_win.x, g_win.y);
    mi_slide(false);
    std::fprintf(stderr, "diplopop window hidden\n");
}

void say_leader(int32_t n, int32_t f) {
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(n, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, m.name_leader, -1, -1);
}
int32_t pop(const char* label, int32_t fw) { return X_pops_11(label, 0x100000, FactionPortraits[fw], kPopWait); }
uint32_t& ds(int32_t a, int32_t b) { return Factions[a].diplo_status[b]; }

}  // namespace

namespace {
int32_t tech_count(int32_t f) {
    int32_t n = 0;
    for (int32_t t = 0; t < 89; t++) n += has_tech(t, f) ? 1 : 0;
    return n;
}
}  // namespace

void diplowin_log_state() {
    int32_t v = CurrentPlayerFaction;
    int32_t techs = 0;
    for (int32_t t = 0; t < 89; t++) techs += has_tech(t, v) ? 1 : 0;
    std::fprintf(stderr, "diplostate f%d credits %d techs %d bases %d governor %d\n", v, Factions[v].energy_credits,
                 techs, Factions[v].base_count, GovernorFaction);
    for (int32_t i = 1; i < 8; i++) {
        if (i == v || !(FactionStatus[1] & (1 << i))) continue;
        const Faction& F = Factions[v];
        std::fprintf(
            stderr,
            "diplo %d status %08X agenda %08X friction %d patience %d loan %d/%d credits %d bases %d techs %d\n", i,
            F.diplo_status[i], F.diplo_agenda[i], Factions[i].diplo_friction[v], Factions[i].diplo_patience[v],
            F.loan_balance[i], F.loan_payment[i], Factions[i].energy_credits, Factions[i].base_count, tech_count(i));
    }
}

void diplowin_set_animate(bool on) { g_animate = on; }
int32_t diplowin_faction() { return g_faction; }

uint32_t diplowin_window_seq() { return g_win.shown ? g_win.seq : 0xFFFFFFFFu; }

void diplowin_draw_window(Buffer* screen) {
    if (!g_win.shown) return;
    const Buffer& c = g_win.canvas;
    for (int32_t j = 0; j < c.height; j++) {
        int32_t y = g_win.y + j;
        if (y < 0 || y >= screen->height) continue;
        const uint8_t* src = c.data + (size_t)j * c.stride;
        uint8_t* dst = screen->data + (size_t)y * screen->stride;
        for (int32_t i = 0; i < c.width; i++) {
            int32_t x = g_win.x + i;
            if (x >= 0 && x < screen->width && src[i] != 9) dst[x] = src[i];
        }
    }
    ui_layer_mark(g_win.x, g_win.y, &c, 0, 9);
}

namespace {
// DiploPop_draw_info (0x43FA90): the centre pane (MainInfc + 0xDC0, inset 7, 0x17 above its bottom
// for the buttons) split in two boxes, the faction on the commlink then the viewer's: the faction
// logo (BaseSprites[f][29]), its formal name in capitals in its colour, then in colour 0xA0 the
// diplomatic status (the other faction's), the intelligence ("spying" or not), the energy reserves
// (the viewer's or a faction spied on: credits less the hurry costs paid this turn) and the power
// (Might by ranking).
void draw_info(Buffer* s, int32_t ox, int32_t oy) {
    UiRect c = mi_rect(kMiPaneCenter2);
    int32_t l = c.left + 7 + ox, t = c.top + 7 + oy, r = c.right - 7 + ox, b = c.bottom - 7 - 0x17 + oy;
    int32_t half = ((r - l) + 3) / 2 - 3 + l;
    bool w800 = ScreenWidth == 800;
    Font* fa = find_font(w800 ? 12 : 14, 1);  // +0xA6C
    Font* fb = find_font(w800 ? 10 : 12, 0);  // +0xA44
    auto lh = [](Font* f) { return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0; };
    for (int32_t i = 0; i < 2; i++) {
        UiRect box{l, t, half, b};
        Buffer_box_sprite(s, &box, box_def(0));
        int32_t bl = l + 3, bt = t + 3, br = half - 3, bb = b - 3;
        int32_t f = i == 0 ? g_faction : CurrentPlayerFaction;
        Sprite* logo = &BaseSprites[f & 7][29];
        if (logo->width) {
            BlitDestScale = BlitSourceScale = 1;
            if (w800) {
                BlitDestScale = 0x41;
                Sprite_draw_mono_2(logo, s, logo->key, ((br - logo->full_width) - bl) / 2 + bl,
                                   ((bb - logo->full_height) - bt) / 2 + bt, 0);
                BlitDestScale = 1;
            } else {
                pfncSpriteStretchCopyToCanvas(logo, s, logo->key, bl, lh(fa) + 3 + bt);
            }
        }
        Buffer_set_text_color(s, *reinterpret_cast<int32_t*>(mem::host(0x68FA30 + (uint32_t)f * 4)), -1, 1, 1);
        Buffer_set_font(s, fa, nullptr, nullptr, nullptr);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, MFactions[f].formal_name_faction);
        win32::CharUpperA(StrBuffer);
        UiRect nr{bl, bt + 2, br, lh(fa) + bt};
        int32_t y = Buffer_wrap_cent_6(s, StrBuffer, &nr);
        if (!w800) bl += 2 + logo->full_width;
        Buffer_set_text_color(s, 0xA0, -1, 1, 1);
        br -= 3;
        int32_t x = bl + 3;
        y += 3;
        Buffer_set_font(s, fb, nullptr, nullptr, nullptr);
        auto wrap = [&](const char* text) {
            UiRect rr{x, y, br, y};
            y = Buffer_wrap_2(s, text, &rr);
        };
        if (i == 0) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, label_get(0xE98 / 4));
            crt_strcat(StrBuffer, ": ");
            uint32_t st = Factions[CurrentPlayerFaction].diplo_status[f];
            uint32_t lab = (st & 1) ? 0xA14 : (st & 2) ? 0xA10 : (st & 4) ? 0xA0C : (st & 0x10) ? 0xA1C : 100;
            crt_strcat(StrBuffer, label_get(lab / 4));
            win32::CharUpperA(StrBuffer);
            wrap(StrBuffer);
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, label_get(0xE9C / 4));
        crt_strcat(StrBuffer, ": ");
        crt_strcat(StrBuffer, label_get((spying(f) ? 0x614 : 0xEA0) / 4));
        wrap(StrBuffer);
        if (f == CurrentPlayerFaction || spying(f)) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, label_get(0x90 / 4));
            crt_strcat(StrBuffer, ": ");
            std::string n = std::to_string(Factions[f].energy_credits - Factions[f].hurry_cost_total);
            crt_strcat(StrBuffer, n.c_str());
            Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
            y += lh(fb);
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, label_get(0x37C / 4));
        crt_strcat(StrBuffer, ": ");
        int32_t px = Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
        StrBuffer[0] = 0;
        int32_t m = 7 - Factions[f].ranking;
        m = m < 0 ? 0 : (m > 6 ? 6 : m);
        say_fac_special(StrBuffer, Strings_get(&StringTable, Might[m].adj_start), f);
        win32::CharUpperA(StrBuffer);
        Buffer_write_l_3(s, StrBuffer, px, y, (int32_t)std::strlen(StrBuffer));
        int32_t d = (half - l) + 3;
        l += d;
        half += d;
    }
}
}  // namespace

bool diplowin_draw_console(Buffer* s, int32_t ox, int32_t oy) {
    if (g_faction < 0) return false;
    int32_t fw = g_faction;
    uint32_t t = g_animate ? platform::ticks_ms() - g_start_ms : 0;
    // The bioscan (left part of the left pane).
    if (const Flc* b = flc_get(g_bioscan)) flc_draw(s, b, ox + 0x1A, oy + 0x5A, (int32_t)(t / (uint32_t)b->speed));
    // The portrait rectangle: the left pane's right part, as wide as the animation (at least 40).
    const Flc* pf = flc_get(g_portrait);
    int32_t fw_w = pf ? pf->width : 0;
    if (fw_w < 0x28) fw_w = 0x28;
    UiRect r = {ox + 0xF5 - 4 - fw_w - 2, oy + 0x38 + 4, ox + 0xF5 - 4, oy + 0xFC - 4};
    if (BoxDef* box = box_def(0)) Buffer_box_sprite(s, &r, box);
    if (pf) flc_draw(s, pf, r.left + 1, r.top + (r.bottom - r.top) / 4 + 1, (int32_t)(t / 2000));
    // DiploPop_draw_portrait: name (capitals), attitude, mood.
    int32_t x = r.left + 1 + 4, y = r.top + 1 + 4;
    char text[256];
    std::snprintf(text, sizeof(text), "%s", MFactions[fw].name_leader);
    win32::CharUpperA(text);
    Font* bold = find_font(14, 1);
    Font* plain = find_font(12, 0);
    Buffer_set_font(s, bold, nullptr, nullptr, nullptr);
    Buffer_set_text_color(s, 0xA0, -1, 1, 1);
    Buffer_write_l_3(s, text, x, y, (int32_t)std::strlen(text));
    y += bold ? bold->ascent_above + 2 : 16;
    Buffer_set_font(s, plain, nullptr, nullptr, nullptr);
    int32_t rep = Factions[fw].integrity_blemishes;
    rep = rep < 0 ? 0 : (rep > 7 ? 7 : rep);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0xE90 / 4));
    crt_strcat(StrBuffer, " ");
    say_fac_special(StrBuffer, Strings_get(&StringTable, Repute[rep]), fw);
    Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    y += plain ? plain->ascent_above + 2 : 14;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0xE94 / 4));
    crt_strcat(StrBuffer, " ");
    int32_t mood = get_mood(Factions[fw].diplo_friction[CurrentPlayerFaction]);
    say_fac_special(StrBuffer, Strings_get(&StringTable, Mood[mood]), fw);
    Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    draw_info(s, ox, oy);
    return true;
}

// ---- DiploPop's interface buttons and HQ pane ----

namespace {
// DiploPop_init's buttons (BaseButton, parent MainInfc): DATALINKS (id 0, +0x2158), PROFILE (id 1,
// +0x2CA4) under the centre pane's two boxes, HQINFO (id 2, +0x37F0) in the right pane left of the
// globe buttons; boxes 27..29 (0x78D960 / 988 / 9B0) by state, text colour 0x41, font +0xA6C.
BaseButton g_btn[3];
bool g_btn_ready = false;
int32_t g_hq = 0;  // +0x4208: HQINFO's state (BaseButton_set): the HQ pane shown

UiRect btn_rect(int32_t id) {
    UiRect c = mi_rect(kMiPaneCenter2);
    int32_t l = c.left + 7, r = c.right - 7, b = c.bottom - 7;
    int32_t half = ((r - l) + 3) / 2 - 3 + l, d = (half - l) + 3;
    if (id == 0) return {l, b - 0x14, half, b};
    if (id == 1) return {l + d, b - 0x14, half + d, b};
    const UiRect R = mi_rect(kMiPaneRight), g1 = mi_rect(0xA8C), g2 = mi_rect(0xA9C);
    return {(R.right - g2.right) + R.left, g1.top, g1.left - 3, g1.bottom};
}

void buttons_init() {
    static const uint32_t kLabel[3] = {0xEA8, 0x774, 0xEAC};
    Font* fa = find_font(ScreenWidth == 800 ? 12 : 14, 1);
    for (int32_t i = 0; i < 3; i++) {
        BaseButton* b = &g_btn[i];
        UiRect r = btn_rect(i);
        if (g_btn_ready && b->r.left == r.left && b->r.top == r.top && b->r.right == r.right && b->r.bottom == r.bottom)
            continue;
        int32_t state = b->set_state;
        std::string text = label_get(kLabel[i] / 4);
        if (i == 1) win32::CharUpperA(&text[0]);  // StrBuffer upper-cased (PROFILE)
        bw_button_ctor(b);
        bw_button_init(b, text.c_str(), i, r.left, r.top, r.right - r.left, r.bottom - r.top, 1);
        for (int32_t k = 0; k < 3; k++) {
            b->box[k] = box_def(27 + k);
            b->text_color[k] = 0x41;
        }
        b->font = fa;
        b->shown = true;
        b->set_state = state;
    }
    g_btn_ready = true;
}

// DiploPop_draw_hq (0x440080): with HQINFO set, the right pane (+0x2114: the pane inset 4 down to
// 6 above the button) shows the faction's headquarters: a box, the HQ base's name in capitals
// (NO HEADQUARTERS without one) and the faction's picture 30 (DiploPop +0x20C0) centred below.
void draw_hq(Buffer* mi) {
    if (!g_hq) return;  // MainInterface_restore_back: the world map pane as usual
    const UiRect R = mi_rect(kMiPaneRight);
    UiRect box{R.left + 4, R.top + 4, R.right - 4, btn_rect(2).top - 6 - 4};
    Buffer_box_sprite(mi, &box, box_def(0));
    UiRect t{box.left + 1, box.top + 1, box.right - 1, box.top + 0x15};
    int32_t i = 0;
    for (; i < BaseCount; i++)
        if (Bases[i].faction_id == g_faction && has_fac(FAC_HEADQUARTERS, i, 0)) break;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, i == BaseCount ? label_get(0xEA4 / 4) : Bases[i].name);
    win32::CharUpperA(StrBuffer);
    Buffer_set_font(mi, find_font(ScreenWidth == 800 ? 12 : 14, 1), nullptr, nullptr, nullptr);
    Buffer_set_text_color(mi, 0xA0, -1, 1, 1);
    Buffer_write_cent_l_7(mi, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    Sprite* pic = &BaseSprites[g_faction & 7][30];
    if (pic->width) {
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(pic, mi, pic->key, ((t.right - pic->full_width) - t.left) / 2 + t.left,
                                      t.bottom + 1);
    }
}

void draw_buttons(Buffer* mi) {
    buttons_init();
    for (BaseButton& b : g_btn) {
        bw_button_draw(&b);
        const Buffer& c = b.canvas;
        for (int32_t y = 0; y < c.height; y++) {
            int32_t dy = b.r.top + y;
            if (dy < 0 || dy >= mi->height) continue;
            for (int32_t x = 0; x < c.width; x++) {
                int32_t dx = b.r.left + x;
                uint8_t v = c.data[(size_t)y * c.stride + x];
                if (dx >= 0 && dx < mi->width && v != 9) mi->data[(size_t)dy * mi->stride + dx] = v;
            }
        }
    }
}

// DiploPop_on_iface_button_clicked (0x440550).
void button_clicked(int32_t id) {
    std::fprintf(stderr, "diplopop button %d\n", id);
    if (id == 0) {
        Datalink_exec(&DatalinkWin, 0xF, g_faction);  // (DAT_00737CD0 set around it)
    } else if (id == 1) {
        report_intel(g_faction);
    } else if (id == 2) {
        // The world map pane hidden (WorldWin, MapWins[1]), no globe button pressed, HQINFO set.
        diplowin_set_hq(1);
    }
    ui_invalidate();
}
}  // namespace

void diplowin_draw_iface(Buffer* mi) {
    if (g_faction < 0) return;
    draw_hq(mi);
    draw_buttons(mi);  // child windows: over the date box
}

void diplowin_set_hq(int32_t on) {
    g_btn[2].set_state = on;
    g_hq = on;
}

bool diplowin_hq_shown() { return g_faction >= 0 && g_hq; }

// DiploPop_on_iface_group_clicked (0x440620): a globe button (0x3EE the world map, 0x3EF the
// detail map) shows the map pane again and clears HQINFO.
void diplowin_globe_clicked(int32_t id) {
    diplowin_set_hq(0);
    // 6: WorldWin shown, MapWins[1] hidden; 7: the reverse (the detail map).
    if (id == 0x3EF) WorldWin_set_detail_map(&WorldWin);
    else WorldWin_set_world_map(&WorldWin);
    std::fprintf(stderr, "diplopop globe %d: hq 0\n", id);
    ui_invalidate();
}

bool diplowin_iface_mouse(int32_t type, int32_t x, int32_t y) {
    if (g_faction < 0 || !g_btn_ready) return false;
    const UiRect& pos = mi_rect(kMiPos);
    x -= pos.left;
    y -= pos.top;
    static int32_t pressed = -1;
    for (int32_t i = 0; i < 3; i++) {
        const UiRect& r = g_btn[i].r;
        bool in = x >= r.left && x < r.right && y >= r.top && y < r.bottom;
        if (type == UiInput::LDOWN && in) {
            pressed = i;
            g_btn[i].pressed = 1;
            ui_invalidate();
            return true;
        }
        if (type == UiInput::LUP && pressed == i) {
            g_btn[i].pressed = 0;
            pressed = -1;
            if (in) button_clicked(i);
            ui_invalidate();
            return true;
        }
    }
    if (type == UiInput::LUP) {
        for (int32_t i = 0; i < 2; i++) {
            const UiRect& r = mi_rect(i ? 0xA9C : 0xA8C);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
                diplowin_globe_clicked(i ? 0x3EF : 0x3EE);
                return true;
            }
        }
    }
    return false;
}

// ---- DiploPop methods ----

void THISCALL DiploPop_draw_info(uint8_t*) { ui_invalidate(); }
void THISCALL GraphicWin_soft_update_3(uint8_t*, uint8_t*) { ui_invalidate(); }

// DiploPop_start (0x43F2B0): diplomacy_check for the viewer, then (for a new faction) the
// animations: dip_<alien a | female f | male m>_<DiploFriction * 3 / 20, 0..2>.flc and
// <faction file>.flc (faction.flc when missing).
void THISCALL DiploPop_start(uint8_t*, int32_t faction_id_with) {
    diplomacy_check(CurrentPlayerFaction, faction_id_with, 0);
    // SubInterface_set_iface_mode -> DiploPop_on_status(1) when the mode starts: the main
    // interface's WorldWin hidden, no globe button pressed, HQINFO set (the HQ pane shown).
    if (g_faction < 0) {
        diplowin_set_hq(1);
        window_show();
    }
    if (g_faction != faction_id_with) {
        g_faction = faction_id_with;
        const char* kind = (ExpansionEnabled && (MFactions[faction_id_with].rule_flags & 0x80))
                               ? "a_"
                               : (MFactions[faction_id_with].is_leader_female ? "f_" : "m_");
        int32_t mood = DiploFriction * 3 / 20;
        mood = mood < 0 ? 0 : (mood > 2 ? 2 : mood);
        g_bioscan = std::string("dip_") + kind + std::to_string(mood) + ".flc";
        g_portrait = std::string(MFactions[faction_id_with].filename) + ".flc";
        if (!flc_get(g_portrait)) g_portrait = "faction.flc";
        g_start_ms = platform::ticks_ms();
    }
    ui_invalidate();
}

// DiploPop_stop (0x43F5C0): the interface mode released.
void THISCALL DiploPop_stop(uint8_t*) {
    window_hide();
    g_faction = -1;
    ui_invalidate();
}

// get_he_she (0x58FCE0): labels 209/210 by the leader's gender.
char* GAMEAPI get_he_she(int32_t faction_id, int32_t mode) {
    crt_strcpy(HisHerBuffer, label_get((MFactions[faction_id].is_leader_female != 0) + 209));
    if (mode == 1) {
        char first[2] = {HisHerBuffer[0], 0};
        win32::CharUpperA(first);
        HisHerBuffer[0] = first[0];
    } else if (mode) {
        win32::CharUpperA(HisHerBuffer);
    }
    return HisHerBuffer;
}

// commlink_attempter (0x5589E0): faction_id_with | 0x10 (not met / eradicated) shows the message
// and refuses; in a network game a player who ended the turn (one player at a time off: ALREADYDONE)
// and a channel already being opened (DIPLOALREADY: continue, or close it and call again) refuse,
// as do the turn's end and the input lock. The debug dialog (Ctrl/Shift in debug mode, the
// DiploWin with any faction) is not reconstructed.
namespace {
void say_title_name(int32_t f) {
    GenderDefault = MFactions[f].is_leader_female;
    PluralDefault = 0;
    parse_says(0, MFactions[f].title_leader, -1, -1);
    GenderDefault = MFactions[f].is_leader_female;
    PluralDefault = 0;
    parse_says(1, MFactions[f].name_leader, -1, -1);
}
}  // namespace

int32_t GAMEAPI commlink_attempter(int32_t, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (!(faction_id_with & 0x10)) {
        if (MultiplayerActive && !(GameMoreRules & 0x10) && (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31)))) {
            NetMsg_pop(&NetMsg, "ALREADYDONE", 5000, 0, nullptr);
            return 1;
        }
        if (NetCommlinkActive) {
            int32_t partner = *reinterpret_cast<int32_t*>(mem::host(0x73B790));
            say_title_name(partner);
            int32_t r = X_pop("DIPLOALREADY", 0);
            if (r == 0) return 1;
            message_data(0x1505, partner, 0, 0, 0, 0);
            ui_diplo_closed(partner);  // GraphicWin_close of the DiploWin
            if (r > 1) return 1;
        }
        if (MultiplayerActive && (ControlTurnC || NetLocked)) return 1;
        return 0;
    }
    int32_t f = faction_id_with & ~0x10;
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.name_leader, -1, -1);
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(2, m.noun_faction, -1, -1);
    NetMsg_pop(&NetMsg, (FactionStatus[1] & (1 << f)) ? "NOTMETYET" : "ERADICATED", 5000, 0, nullptr);
    return 1;
}

// commlink_attempt (0x558C60): the viewer calls faction_id_with (commlink panel, F12 keys).
void GAMEAPI commlink_attempt(int32_t fw) {
    int32_t f = CurrentPlayerFaction;
    if (f == fw || !(FactionStatus[1] & (1 << fw))) return;
    if (SunspotDuration > 0) {
        NetMsg_pop(&NetMsg, "SUNSPOTSNO", 5000, 0, nullptr);
        return;
    }
    bool alien_f = MFactions[f].rule_flags & 0x80, alien_w = MFactions[fw].rule_flags & 0x80;
    if (alien_f && alien_w) {
        GenderDefault = MFactions[f].is_leader_female;
        PluralDefault = 0;
        parse_says(1, MFactions[f].title_leader, -1, -1);
        GenderDefault = MFactions[f].is_leader_female;
        PluralDefault = 0;
        parse_says(3, MFactions[f].name_leader, -1, -1);
        X_pops_11("ALIENHATRED2", 0x100000, FactionPortraits[fw], kPopWait);
        return;
    }
    if (alien_f && !has_tech(TECH_PrPsych, fw) && !has_tech(TECH_Psych, f)) {
        NetMsg_pop(&NetMsg, "WENEEDHUMANPSYCH", -5000, 0, "Alopdir.pcx");
        return;
    }
    if (alien_w && !has_tech(TECH_PrPsych, f) && !has_tech(TECH_Psych, fw)) {
        NetMsg_pop(&NetMsg, "WENEEDALIENPSYCH", -5000, 0, "talent_sm.pcx");
        return;
    }
    if (commlink_attempter(f, fw)) return;
    if (MultiplayerActive) {
        if (FactionStatus[0] & (1 << fw)) {
            // Another human: the call (0x1501, COMMDIPLO on their side); the DiploWin opens with
            // their answer (0x1502, NetDaemon_process_message).
            NetCommlinkActive = 1;
            *reinterpret_cast<int32_t*>(mem::host(0x73B790)) = fw;
            message_data(0x1501, fw, f, fw, 0, 0);
            say_title_name(fw);
            parse_num(0, fw);
            ui_net_note("net: calling faction " + std::to_string(fw));
            NetMsg_pop(&NetMsg, "DIPLOCOMM", 5000, 0, nullptr);
            return;
        }
        log_say_values("Commlink open channel", f, fw, 0);
        if (NetDaemon_open_channel(&NetState, f, fw)) {
            say_title_name(fw);
            NetMsg_pop(&NetMsg, "NOCHANNEL", 5000, 0, nullptr);
            return;
        }
    }
    if (PbemActive && (FactionStatus[0] & (1 << fw))) {
        diplo(f, fw);
        return;
    }
    communicate(f, fw, 1);
    if (MultiplayerActive) {
        log_say_values("Sending TREATY_ON", f, fw, 0);
        message_data(0x2441, 0, f, fw, (int32_t)0x88000008, 0);
        NetDaemon_await_diplo(&NetState, 0x441);
        return;
    }
    treaty_on(f, fw, 0x88000008);
}

// diplomacy_menu (0x54C560): the DIPLO menu (DIPLOMENU lines) of the human's topics.
int32_t GAMEAPI diplomacy_menu(int32_t f, int32_t fw) {
    DiploSecondFaction = fw;
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(0, MFactions[f].name_leader, -1, -1);
    parse_says(1, get_pact_hood(f, fw), -1, -1);
    diplomacy_caption(f, fw);
    ui_diplo_menu_start("DIPLO", 0x100000);
    if (X_text_open(ScriptFileName, "DIPLOMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    const bool shown[9] = {
        true,
        DiploMenuFlagA != 0,
        DiploMenuFlagB == 0,
        DiploMenuFlagB != 0,
        !MultiplayerActive && DiploCommonEnemy > 0,
        !MultiplayerActive && DiploPeaceFaction > 0,
        can_call_council(f, 0) && !(MFactions[f].rule_flags & 0x80) && !(MFactions[fw].rule_flags & 0x80),
        (ds(fw, f) & 1) != 0,
        (ds(f, fw) & 7) != 0,
    };
    const int32_t value[9] = {0, 1, 2, 2, 5, 6, 7, 3, 4};
    for (int32_t i = 0; i < 9; i++) {
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
    }
    text_close();
    int32_t r = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    return r;
}

namespace {
// The faction list of PROPOSEATTACK / PROPOSECOMMLINK (ATTACKSTRING's line for each faction).
void faction_items(int32_t f, int32_t fw, bool commlink) {
    const char* line = text_get();
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || i == f || i == fw) continue;
        if (commlink) {
            if (ds(f, i) & 8) continue;
        } else if (!(ds(f, i) & 8) && !(FactionStatus[0] & (1 << i))) {
            continue;
        }
        const MFaction& m = MFactions[i];
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        StrBuffer[0] = 0;
        parse_string(line ? line : TextBufferGetPtr, StrBuffer);
        ui_diplo_menu_item(StrBuffer, i);
    }
}
bool has_hq(int32_t base_id) {
    int32_t off, mask;
    bitmask(1, &off, &mask);
    return (Bases[base_id].facilities_built[off] & (uint8_t)mask) != 0;
}
// PROPOSEBASE / COUNTERBASE: the bases (without headquarters) to swap; -1 when none was chosen.
// PlanWin previews them (FUN_0048BC60, its background FUN_0048B3F0): zoomed on the first base,
// its single-point "route" (FUN_0048AE90(b, b, 0)); the list's selection callback is the
// original's battle_routine (its routes from attack_from for DiploBattlePlayer / Rival, as left
// by the last battle plans: proposal_menu sets it, FUN_0054DC90 the base preview is not used).
int32_t base_menu(const char* label, int32_t owner, int32_t viewer, int32_t fw) {
    planwin_open();
    planwin_redraw_back();
    ui_diplo_menu_start(label, 0x100040);
    int32_t n = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != owner) continue;
        if (viewer >= 0 && owner != viewer && !(Bases[i].visibility & (1 << viewer))) continue;
        if (has_hq(i)) continue;
        StrBuffer[0] = 0;
        say_base(StrBuffer, i);
        ui_diplo_menu_item(StrBuffer, i);
        if (!n) {
            planwin_clear_lines();
            planwin_zoom_tile(Bases[i].x, Bases[i].y, 0);
            planwin_add_base_line(i, i, 0);
            planwin_draw();
        }
        n++;
    }
    int32_t r = -1;
    if (n) {
        ui_diplo_menu_set_callback(battle_routine);
        r = ui_diplo_menu_exec(fw);
    }
    ui_diplo_menu_close();
    planwin_close();
    return r;
}
}  // namespace

// proposal_menu (0x54DCF0): PROPOSAL (the human's proposal), its sub-menus (a faction to attack or
// a commlink to share, a base to ask for) and COUNTER (what is offered in exchange). Sets
// DiploCurrentProposal / DiploCounterProposal (and the base and faction choices); returns the
// proposal (0: none).
int32_t GAMEAPI proposal_menu(int32_t f, int32_t fw) {
    DiploBidBaseSwapID = -1;
    DiploAskBaseSwapID = -1;
    DiploSecondFaction = fw;
    DiploCounterProposal = 0;
    DiploCurrentProposal = 0;
    DiploProposalFaction = 0;
    int32_t swap_bases = 0, known = 0, unknown = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != fw) continue;
        if (fw != f && !(Bases[i].visibility & (1 << f))) continue;
        if (!has_hq(i)) swap_bases++;
    }
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || i == f || i == fw) continue;
        uint32_t c = ds(f, i) & 8;
        if (c) known++;
        if (!c) {
            int32_t p = SecretProjects[FAC_EMPATH_GUILD - SP_ID_First];
            if (p < 0 || (uint8_t)Bases[p].faction_id != f) unknown++;
        }
    }
    PluralDefault = 0;
    GenderDefault = MFactions[fw].is_leader_female;
    parse_says(0, MFactions[fw].title_leader, -1, -1);
    parse_says(1, get_pact_hood(f, fw), -1, -1);
    parse_num(0, Factions[f].loan_balance[fw]);
    diplomacy_caption(f, fw);
    ui_diplo_menu_start("PROPOSAL", 0x100000);
    if (X_text_open(ScriptFileName, "PROPOSALMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    uint32_t s = ds(f, fw);
    const bool shown[12] = {
        true,
        true,
        (s & 2) && !(s & 1),
        !(s & 2),
        !(GameRules & RULES_SCN_NO_TECH_TRADING),
        false,
        unknown != 0,
        true,
        Factions[f].loan_balance[fw] && Factions[f].energy_credits >= Factions[f].loan_balance[fw],
        false,
        known != 0,
        Factions[fw].base_count > 1 && swap_bases && !MultiplayerActive,
    };
    const int32_t value[12] = {0, 1, 2, 3, 4, 0, 13, 6, 11, 0, 8, 9};
    for (int32_t i = 0; i < 12; i++) {
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
    }
    text_get();
    StrBuffer[0] = 0;
    parse_string(TextBufferGetPtr, StrBuffer);
    DiploCurrentProposal = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (DiploCurrentProposal == 8) {
        // PROPOSEATTACK: the faction to attack.
        ui_diplo_menu_start("PROPOSEATTACK", 0x100040);
        if (X_text_open(ScriptFileName, "ATTACKSTRING")) {
            ui_diplo_menu_close();
            goto none;
        }
        faction_items(f, fw, false);
        DiploProposalFaction = ui_diplo_menu_exec(fw);
        ui_diplo_menu_close();
        if (DiploProposalFaction < 0) goto none;
        // BUG(original): the loop's counter (8) is used as the faction: diplo_status[8] of
        // faction_id_with is its diplo_agenda[0].
        COMPAT_ORIGINAL(diplo_agenda_loop_counter);
        if (Factions[fw].diplo_agenda[0] & 0x10) {
            int32_t p = DiploProposalFaction;
            PluralDefault = 0;
            GenderDefault = MFactions[p].is_leader_female;
            parse_says(0, MFactions[p].title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = MFactions[DiploProposalFaction].is_leader_female;
            parse_says(1, MFactions[DiploProposalFaction].name_leader, -1, -1);
            pop("REJWARALREADY", fw);
            goto none;
        }
    }
    if (DiploCurrentProposal == 13) {
        // PROPOSECOMMLINK: a commlink to share.
        ui_diplo_menu_start("PROPOSECOMMLINK", 0x100040);
        if (X_text_open(ScriptFileName, "ATTACKSTRING")) {
            ui_diplo_menu_close();
            goto none;
        }
        faction_items(f, fw, true);
        int32_t p = ui_diplo_menu_exec(fw);
        ui_diplo_menu_close();
        DiploProposalFaction = p;
        if (p < 0) goto none;
        if (!(ds(fw, p) & 8)) {
            int32_t sp = SecretProjects[FAC_EMPATH_GUILD - SP_ID_First];
            if (sp < 0 || (uint8_t)Bases[sp].faction_id != fw) {
                const MFaction& m = MFactions[p];
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(0, m.title_leader, -1, -1);
                PluralDefault = 0;
                GenderDefault = MFactions[DiploProposalFaction].is_leader_female;
                parse_says(1, MFactions[DiploProposalFaction].name_leader, -1, -1);
                const MFaction& m2 = MFactions[DiploProposalFaction];
                PluralDefault = m2.is_noun_plural;
                GenderDefault = m2.noun_gender;
                parse_says(2, m2.noun_faction, -1, -1);
                DiploCurrentProposal = 0;
                X_pops("NOCOMMLINK", FactionPortraits[fw], kPopWait);
            }
        }
    }
    if (DiploCurrentProposal == 9) {
        DiploAskBaseSwapID = base_menu("PROPOSEBASE", fw, f, fw);
        if (DiploAskBaseSwapID < 0) {
            DiploAskBaseSwapID = -1;
            DiploCurrentProposal = 0;
        }
    }
    {
        int32_t p = DiploCurrentProposal;
        if (p == 0 || p == 7 || p == 10 || p == 11 || p == 5 || p == 12) {
            DiploCounterProposal = 1;
            return p;
        }
    }
    // COUNTER: what faction_id offers.
    PluralDefault = 0;
    GenderDefault = MFactions[fw].is_leader_female;
    parse_says(0, MFactions[fw].title_leader, -1, -1);
    GenderDefault = MFactions[fw].is_leader_female;
    PluralDefault = 0;
    parse_says(1, MFactions[fw].name_leader, -1, -1);
    parse_says(2, get_pact_hood(f, fw), -1, -1);
    diplomacy_caption(f, fw);
    {
        char label[64];
        std::snprintf(label, sizeof(label), "COUNTER%d", DiploCurrentProposal == 1 ? 1 : 0);
        ui_diplo_menu_start(label, 0x100000);
    }
    if (X_text_open(ScriptFileName, "COUNTERMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    {
        int32_t p = DiploCurrentProposal;
        const bool shown[10] = {
            true,
            p == 6,
            p != 1,
            p != 1,
            p != 1 && !(s & 1),
            p != 1 && (s & 1),
            !(GameRules & RULES_SCN_NO_TECH_TRADING),
            p == 1,
            p != 6,
            Factions[f].base_count > 1 && !MultiplayerActive && (p == 9 || p == 1),
        };
        const int32_t value[10] = {0, 6, 1, 2, 3, 3, 4, 7, 5, 8};
        for (int32_t i = 0; i < 10; i++) {
            text_get();
            StrBuffer[0] = 0;
            parse_string(TextBufferGetPtr, StrBuffer);
            if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
        }
    }
    DiploCounterProposal = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (DiploCounterProposal == 8) {
        DiploBidBaseSwapID = base_menu("COUNTERBASE", f, -1, fw);
        if (DiploBidBaseSwapID < 0) DiploCurrentProposal = 0;
    }
    if (DiploCounterProposal == 0) DiploCurrentProposal = 0;
    return DiploCurrentProposal;
none:
    DiploCurrentProposal = 0;
    DiploCounterProposal = 1;
    return 0;
}

// make_a_proposal (0x54F420): the proposal chosen (proposal_menu, unless `direct`: the council
// topic) carried out by the engine's propose_* / trade functions.
void GAMEAPI make_a_proposal(int32_t f, int32_t fw, int32_t direct) {
    DiploSecondFaction = fw;
    DiploMenuFlagA = 0;
    if (!direct && !proposal_menu(f, fw)) return;
    DiploMenuFlagB = 1;
    bool threat = DiploCounterProposal == 3;
    int32_t p = DiploCurrentProposal;
    switch (p) {
    case 2:
        if (!propose_pact(f, fw) && threat) threaten(f, fw);
        return;
    case 3:
        if (!propose_treaty(f, fw) && threat) threaten(f, fw);
        return;
    case 7:
        DiploSecondFaction = fw;
        if ((!(ds(f, fw) & 1) && DiploFriction > 0) || DiploFriction > 4) {
            parse_says(0, MFactions[fw].adj_name_faction, -1, -1);
            pop("REJMAPS", fw);
        } else {
            pop("TRADEMAPS", fw);
            Factions[fw].diplo_patience[f] += 2;
            net_maps(f, fw, 1);
            DiploMapMsg = 1;
        }
        return;
    case 10: return;
    default: break;
    }
    if (p == 8 && (ds(fw, f) & 1) && (ds(fw, f) & 0x2000000)) {
        propose_attack(f, fw);
        return;
    }
    if (threat) {
        threaten(f, fw);
        return;
    }
    switch (p) {
    case 8: propose_attack(f, fw); return;
    case 9: base_swap(f, fw); return;
    case 4:
    case 5:
    case 13: DiploMenuFlagA = tech_trade(f, fw, 0, DiploCounterProposal, p); return;
    case 6: DiploMenuFlagA = energy_trade(f, fw); return;
    case 1: make_gift(f, fw); return;
    case 11: {
        // The loan repaid.
        const MFaction& m = MFactions[f];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        int32_t bal = Factions[f].loan_balance[fw];
        net_energy(f, -bal, fw, bal, 1);
        net_loan(f, fw, 0, 0);
        pop("REPAIDLOAN", fw);
        break;
    }
    default: break;
    }
    if (DiploCurrentProposal != 12) return;
    // COUNCILMATTER: a vote asked of faction_id_with.
    ui_diplo_menu_start("COUNCILMATTER", 0x100040);
    for (int32_t i = 0; i < 11; i++) {
        if (!has_tech(Proposal[i].preq_tech, f) &&
            !((GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_DEBUG_MODE)))
            continue;
        if (i == 1 && !(GameRules & RULES_VICTORY_DIPLOMATIC)) continue;
        bool ok = true;
        switch (i) {
        case 1: ok = !(GameState & STATE_GAME_DONE) && !ProposalPassCount[1] && eligible(f); break;
        case 2:
        case 5: ok = !ProposalPassCount[i]; break;
        case 3: ok = ProposalPassCount[3] <= ProposalPassCount[4]; break;
        case 4: ok = ProposalPassCount[4] != ProposalPassCount[3]; break;
        case 6: ok = ProposalPassCount[5] != 0; break;
        case 8: ok = ProposalPassCount[8] <= ProposalPassCount[9]; break;
        case 9: ok = ProposalPassCount[9] < ProposalPassCount[8]; break;
        default: break;
        }
        if (!ok) continue;
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[i].name));
        crt_strcat(StrBuffer, "|");
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[i].description));
        ui_diplo_menu_item(StrBuffer, i);
    }
    int32_t prop = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (prop < 0) return;
    if (prop != 0) {
        int32_t r = pop("COUNCILVOTE", fw);
        int32_t vote = -1 - r;
        if (vote) buy_council_vote(f, fw, prop, vote);
        return;
    }
    // COUNCILVOTEGOV: the candidate for governor.
    ui_diplo_menu_start("COUNCILVOTEGOV", 0x100000);
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || !eligible(i)) continue;
        const MFaction& m = MFactions[i];
        StrBuffer[0] = 0;
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        crt_strcat(StrBuffer, m.title_leader);
        crt_strcat(StrBuffer, " ");
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        crt_strcat(StrBuffer, m.name_leader);
        ui_diplo_menu_item(StrBuffer, i);
        if (i == f) ui_diplo_menu_set_active(i);
    }
    int32_t cand = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (cand >= 0) buy_council_vote(f, fw, 0, cand);
}

// battle_plans (0x54B1C0): the BATTLEPLANS dialog: maps traded first (not between pact
// partners), each side's suggested target (suggest_plan), then a common target (BATTLEPLAN menu)
// that faction_id_with accepts (base_id_attack_target; its current target dropped when in
// another region) or not (BATTLENO).
// battle_routine (0x54B140, the BATTLEPLAN list's selection callback): PlanWin shows the routes
// from each side's attacking base to the target.
void battle_routine(int32_t target) {
    int32_t a = attack_from(target, DiploBattlePlayer);
    int32_t b = attack_from(target, DiploBattleRival);
    planwin_clear_lines();
    planwin_add_base_line(a, target, FactionColorFill[DiploBattlePlayer]);
    planwin_add_base_line(b, target, FactionColorFill[DiploBattleRival]);
    planwin_draw();
}

void GAMEAPI battle_plans(int32_t f, int32_t fw) {
    DiploSecondFaction = fw;
    DiploBattlePlayer = f;
    DiploBattleRival = fw;
    int32_t tf = suggest_plan(f, fw);
    int32_t tw = suggest_plan(fw, f);
    int32_t af = attack_from(tf, f);
    int32_t aw = attack_from(tw, fw);
    if (!(ds(f, fw) & 1)) {
        if ((ds(fw, f) & 0x8020) || DiploCapturedBases || DiploFriction >= 0x12 || DiploHostility) {
            pop("NEVERBATTLEPLANS", fw);
            return;
        }
        if (!DiploMapsTraded && pop("MAPBATTLEPLANS", fw) == 0) return;
        net_maps(f, fw, 1);
    }
    if (tf < 0 || tw < 0) {
        pop("NOBATTLEPLANS", fw);
        return;
    }
    // PlanWin (FUN_0048BC60) between the two targets and their attacking bases: each side's route
    // in its colour; it stays shown until battle_plans returns (FUN_0048BC50).
    struct PlanGuard {
        ~PlanGuard() { planwin_close(); }
    } plan_guard;
    planwin_open();
    planwin_center_between(Bases[tf].x, Bases[tf].y, Bases[tw].x, Bases[tw].y, Bases[af < 0 ? tf : af].x,
                           Bases[af < 0 ? tf : af].y, Bases[aw < 0 ? tw : aw].x, Bases[aw < 0 ? tw : aw].y);
    planwin_redraw_back();
    planwin_add_base_line(af, tf, FactionColorFill[f]);
    planwin_add_base_line(aw, tw, FactionColorFill[fw]);
    planwin_draw();
    Faction& W = Factions[fw];
    W.base_id_attack_target = tw;
    parse_says(0, Bases[tw].name, -1, -1);
    parse_says(1, Bases[tf].name, -1, -1);
    if (!pop("BATTLEPLANS", fw)) return;
    {
        const MFaction& m = MFactions[f];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
    }
    ui_diplo_menu_start("BATTLEPLAN", 0x100042);
    int32_t n = 0;
    for (int32_t b = 0; b < BaseCount; b++) {
        int32_t owner = (uint8_t)Bases[b].faction_id;
        if (owner == f || owner == fw) continue;
        if (!(ds(f, owner) & 0x10) || !(ds(fw, owner) & 0x10)) continue;
        uint8_t vis = Bases[b].visibility;
        if (!(vis & (1 << f)) && !(vis & (1 << fw))) continue;
        ui_diplo_menu_item(Bases[b].name, b);
        if (!n) battle_routine(b);  // the first target's routes
        n++;
    }
    ui_diplo_menu_set_callback(battle_routine);  // Popup +0x3280: the routes of the selection
    if (!n) {
        ui_diplo_menu_close();
        return;
    }
    int32_t target = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (target < 0) return;
    BASE& t = Bases[target];
    parse_says(0, t.name, -1, -1);
    if ((uint8_t)t.faction_id != fw && !(t.visibility & (1 << fw))) spot_loc(t.x, t.y, fw);
    int32_t reg = map_tile(t.x, t.y)->region;
    int32_t reg2 = map_tile(Bases[tw].x, Bases[tw].y)->region;
    bool go = (reg < 64 && W.region_total_bases[reg]) || !W.region_total_bases[reg2];
    for (int32_t r = 0; r < 64 && !go; r++) {
        int32_t m = r & 0x3F;
        if (m != 0x3F && m && W.region_total_bases[r] && W.region_base_plan[r] == 7) go = true;
    }
    if (!go) {
        pop("BATTLENO", fw);
        return;
    }
    base_find_faction(Bases[tw].x, Bases[tw].y, fw);
    int32_t d1 = BaseFindDist;
    base_find_faction(t.x, t.y, fw);
    if (BaseFindDist >= d1 * 2) {
        pop("BATTLENO", fw);
        return;
    }
    W.base_id_attack_target = target;
    pop("BATTLEYES", fw);
    if (W.target_x < 0) return;
    if (map_tile(W.target_x, W.target_y)->region == map_tile(t.x, t.y)->region) return;
    for (int32_t v = 0; v < VehCount; v++) {
        if (Vehs[v].faction_id == (uint32_t)fw && (Vehs[v].state & 0x40000)) Vehs[v].state &= 0xFFF9FFFF;
    }
    W.target_y = -1;
    W.target_x = -1;
    W.unk_72 = 0;
    W.unk_71 = 0;
}
