// Social engineering window (SocialWin 0x8A6270; ui module 0x4AE9E0..0x4B3C80, unnamed in the
// decompilation: FUN_004b2740 is SocialWin_exec, FUN_004b26d0 SocialWin_close). Opened by
// social_select ('e' on the main view, the menu, tech_achieved's new models): the four social
// categories with a button per available model, ten effect buttons that pick, per category, the
// model with the largest value of that effect, the effects of the tentative choice, the energy
// allocation (economy / psych / labs, +/- buttons and locks, in the main interface) and OK /
// Reset / Cancel.
//
// The game state changes are the original's, in its order of calls (asm-faithful):
//   * a model click (FUN_004aebf0): the tentative choice's effects (social_calc), and the net
//     income and breakthrough time it would give (energy_compute with the choice written into the
//     faction's pending effects, which are then recomputed from the pending choice);
//   * an effect button (FUN_004af110): per category the available model with the largest value
//     of the effect (> 0; model 0 when none);
//   * the allocation buttons (FUN_004b2470, FUN_004b1ff0): +/- 1 on one share, the others
//     rebalanced to a total of 10 in the original's order skipping locked shares; written to the
//     faction (SE_alloc_psych / SE_alloc_labs) at once and applied with social_set;
//   * Reset (FUN_004aece0, also the start): the pending choice and the allocation of the opening;
//   * OK / Enter (FUN_004aef10): the upheaval cost (social_upheaval minus what was paid this
//     turn): UPHEAVALTOOMUCH when the credits do not cover it, else the UPHEAVAL question; the
//     cost is paid (net_energy) and the choice becomes the pending choice (synch_alloc,
//     synch_soc); Cancel / Esc: Reset then OK (nothing to pay).
// social_select then closes the window and applies the pending choice (social_set).
//
// Drawing (approximated, not compared with the original's pixels): the window (833 x 354 above
// the console) and the main interface panes at the original's rectangles, with its box sprites,
// fonts and colours; the models' effects under their buttons are written as text ("++POLICE")
// where the original draws icons of socwin.pcx; the leader's portrait and the hover bubbles are
// not drawn. The tutorial: the SOCWIN hint and the tour's display (SocialWin_exec with a
// negative faction, tutwin.cpp); the slide (screenwin.cpp).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/social.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
#include "ui/tutwin.h"
#include "ui/console.h"
#include "ui/mapwin.h"

namespace {

// Button ids (BaseButton_init's id): 0..15 the models (category * 4 + model), 0x10 Cancel,
// 0x11..0x1A the effects (TALENT has none), 0x1B Reset, 0x1C OK, 0x1D..0x22 the allocation's
// -/+ (economy, labs, psych).
constexpr int32_t kCancel = 0x10, kReset = 0x1B, kOk = 0x1C;

struct SocialWinState : ScreenWin {
    int32_t faction = -1;  // +0xCF4
    int32_t choices[4]{};  // +0xCF8: the tentative choice
    int32_t effects[11]{};  // +0xD08: its effects
    int32_t effects_prev[11]{};  // +0xD34: the effects before the last change
    int32_t net_income = 0;  // +0xD60
    int32_t breakthrough = 0;  // +0xD64
    int32_t alloc_open[3]{};  // +0x2C38: economy, psych, labs when the window opened
    int32_t alloc[3]{};  // +0x2C44: the shares the buttons step
    int32_t alloc_work[3]{};  // +0x2C50: rebalanced shares
    int32_t limit_hit = 0;  // +0x2C5C
    int32_t locks[3]{};  // the three CheckButtons' states (+0xA14 of each)
    int32_t effect_focus = -1;  // +0x2C24 (an effect button is applying its choice)
    int32_t resetting = 0;  // +0x2C28
    int32_t effect_selected = -1;  // the effect button group's selection (ButtonGroup 2)
    BaseButton* model_btn[16]{};
    BaseButton* effect_btn[10]{};
    Font *font_title = nullptr, *font_button = nullptr, *font_text = nullptr, *font_small = nullptr;
    Buffer tiles[6];  // +0x3EF0: allocation bar tiles of socwin.pcx (14 x 14)
    Sprite checks[2];  // +0x3D90: lock check box, off / on
    Sprite plusminus[6];  // +0x3DE8: the -/+ buttons' states

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;

    // The original's methods.
    void select_model(int32_t cat, int32_t model);  // FUN_004aebf0
    void reset();  // FUN_004aece0
    void accept();  // FUN_004aef10
    void effect_choice(int32_t effect);  // FUN_004af110
    void group_clicked(int32_t id);  // FUN_004b1ce0
    void alloc_clicked(int32_t id);  // FUN_004b2470
    void rebalance(int32_t index, int32_t value);  // FUN_004b1ff0
    void refresh_buttons();  // ButtonGroup_set of the models
};

SocialWinState g_sw;

Faction& fac(int32_t f) { return Factions[f]; }
int32_t* pending_choice(int32_t f) { return &fac(f).SE_Politics_pending; }
int32_t* pending_effects(int32_t f) { return &fac(f).SE_economy_pending; }

// Effect index of effect button i (0..9): TALENT (3) has no button.
int32_t effect_of_button(int32_t i) { return i < 3 ? i : i + 1; }

void SocialWinState::refresh_buttons() {
    for (int32_t i = 0; i < 16; i++)
        if (model_btn[i]) bw_button_set(model_btn[i], choices[i / 4] == i % 4);
    for (int32_t i = 0; i < 10; i++)
        if (effect_btn[i]) bw_button_set(effect_btn[i], effect_selected == i);
}

void SocialWinState::select_model(int32_t cat, int32_t model) {
    choices[cat] = model;
    for (int32_t i = 0; i < 11; i++) effects_prev[i] = effects[i];
    social_calc(choices, effects, faction, 0, 0);
    social_calc(choices, pending_effects(faction), faction, 0, 0);
    energy_compute(faction, 1);
    breakthrough = FactionCurrentBreakthroughTurns;
    net_income = FactionCurrentNetIncome;
    social_calc(pending_choice(faction), pending_effects(faction), faction, 0, 0);
}

void SocialWinState::reset() {
    social_ai(faction, -1, -1, -1, -1, nullptr);
    resetting = 1;
    for (int32_t i = 0; i < 4; i++) choices[i] = pending_choice(faction)[i];
    effect_selected = -1;
    refresh_buttons();
    resetting = 0;
    for (int32_t i = 0; i < 11; i++) effects[i] = effects_prev[i] = pending_effects(faction)[i];
    Faction& f = fac(faction);
    bool changed = f.SE_alloc_psych != alloc_open[1] || f.SE_alloc_labs != alloc_open[2];
    f.SE_alloc_psych = alloc_open[1];
    f.SE_alloc_labs = alloc_open[2];
    for (int32_t i = 0; i < 3; i++) alloc_work[i] = alloc_open[i];
    if (changed) {
        social_set(faction);
        social_calc(choices, effects, faction, 0, 0);
        social_calc(choices, pending_effects(faction), faction, 0, 0);
    }
    energy_compute(faction, 1);
    breakthrough = FactionCurrentBreakthroughTurns;
    net_income = FactionCurrentNetIncome;
    social_calc(pending_choice(faction), pending_effects(faction), faction, 0, 0);
}

void SocialWinState::accept() {
    Faction& f = fac(faction);
    int32_t cost = social_upheaval(faction, choices) - f.SE_upheaval_cost_paid;
    if (!(GameState & STATE_SCENARIO_EDITOR) || !(GameState & STATE_OMNISCIENT_VIEW)) {
        if (cost > 0) {
            parse_num(0, cost);
            parse_num(1, f.energy_credits);
            if (f.energy_credits < cost) {
                popp(ScriptFileName, "UPHEAVALTOOMUCH", 0, "genwarning_sm.pcx", 0);
                return;
            }
            int32_t yes = (MFactions[faction].rule_flags & 0x80)
                              ? popp(ScriptFileName, "UPHEAVAL", 0, "Alopdir.pcx", 0)
                              : popp(ScriptFileName, "UPHEAVAL", 0, "talent_sm.pcx", 0);
            if (!yes) return;
        }
    } else {
        cost = 0;  // scenario editor
    }
    if (f.energy_credits <= cost) cost = f.energy_credits;
    f.SE_upheaval_cost_paid += cost;
    for (int32_t i = 0; i < 4; i++) pending_choice(faction)[i] = choices[i];
    synch_alloc(faction);
    synch_soc(faction);
    net_energy(faction, -cost, 0, 0, 0);
    social_calc(pending_choice(faction), pending_effects(faction), faction, 0, 0);
    sw_close(this);  // vtable + 0xE8: the modal loop ends
}

void SocialWinState::effect_choice(int32_t effect) {
    effect_focus = effect;
    for (int32_t cat = 0; cat < 4; cat++) {
        int32_t best = 0, best_value = 0;
        for (int32_t m = 0; m < 4; m++) {
            int32_t v = SocialField[cat].soc_effect[m].values[effect];
            if (society_avail(cat, m, faction) && best_value < v) {
                best = m;
                best_value = v;
            }
        }
        choices[cat] = best;  // ButtonGroup_set(best + cat * 4)
        select_model(cat, best);
    }
    refresh_buttons();
    effect_focus = -1;
}

void SocialWinState::group_clicked(int32_t id) {
    if (id > 0x10 && id < 0x1B) {
        effect_selected = id - 0x11;
        effect_choice(effect_of_button(id - 0x11));
        return;
    }
    if (id >= 0 && id < 0x10) {
        select_model(id / 4, id % 4);
        if (effect_focus == -1 && resetting == 0) effect_selected = -1;  // ButtonGroup_set(-1)
        refresh_buttons();
    }
}

void SocialWinState::rebalance(int32_t index, int32_t value) {
    int32_t limit = energy_limit(faction);
    alloc_work[index] = value;
    auto sum = [&] { return alloc_work[0] + alloc_work[1] + alloc_work[2]; };
    while (sum() < 10) {
        for (int32_t k = 1; k <= 3; k++) {
            int32_t j = (index + k) % 3;
            if (j == index || (!locks[j] && alloc_work[j] < limit)) {
                alloc_work[j]++;
                break;
            }
        }
    }
    while (sum() > 10) {
        for (int32_t k = 1; k <= 3; k++) {
            int32_t j = (index + k) % 3;
            if (j == index || (!locks[j] && alloc_work[j] != 0)) {
                alloc_work[j]--;
                break;
            }
        }
    }
}

void SocialWinState::alloc_clicked(int32_t id) {
    int32_t index, delta;
    switch (id) {
    case 0x1D:
        index = 0;
        delta = -1;
        break;
    case 0x1E:
        index = 0;
        delta = 1;
        break;
    case 0x1F:
        index = 2;
        delta = -1;
        break;
    case 0x20:
        index = 2;
        delta = 1;
        break;
    case 0x21:
        index = 1;
        delta = -1;
        break;
    case 0x22:
        index = 1;
        delta = 1;
        break;
    default: return;
    }
    alloc[index] += delta;
    if (alloc[index] < 0) alloc[index] = 0;
    if (energy_limit(faction) < alloc[index]) {
        limit_hit = 1;
        alloc[index] = energy_limit(faction);
    }
    rebalance(index, alloc[index]);
    for (int32_t i = 0; i < 3; i++) alloc[i] = alloc_work[i];
    Faction& f = fac(faction);
    f.SE_alloc_psych = alloc[1];
    f.SE_alloc_labs = alloc[2];
    social_set(faction);
    social_calc(choices, effects, faction, 0, 0);
    social_calc(choices, pending_effects(faction), faction, 0, 0);
    energy_compute(faction, 1);
    breakthrough = FactionCurrentBreakthroughTurns;
    net_income = FactionCurrentNetIncome;
    social_calc(pending_choice(faction), pending_effects(faction), faction, 0, 0);
}

void SocialWinState::on_button(int32_t id) {
    if (id >= 0 && id < 0x10) return group_clicked(id);
    if (id > 0x10 && id < 0x1B) return group_clicked(id);
    if (id >= 0x1D && id <= 0x22) return alloc_clicked(id);
    // FUN_004b1e00
    switch (id) {
    case kReset: reset(); break;
    case -2:
    case kCancel:
        reset();
        accept();
        break;
    case -1:
    case kOk: accept(); break;
    default: break;
    }
}

// FUN_004b1e80: Enter accepts, Esc resets and accepts.
void SocialWinState::on_key(int32_t key) {
    if (key == 0xD) {
        accept();
    } else if (key == 0x1B) {
        reset();
        accept();
    }
}

// The lock check boxes of the allocation (CheckButtons in the main interface): a click toggles.
void SocialWinState::on_click(int32_t parent, int32_t index, int32_t type, int32_t, int32_t, bool, bool) {
    if (parent == 1 && type == 1 && index >= 0 && index < 3) locks[index] ^= 1;
}

std::string signed_num(int32_t v) { return (v > 0 ? "+" : "") + std::to_string(v); }

// The models' effects as SOCIO writes them ("++POLICE, --EFFIC").
std::string model_effects(int32_t cat, int32_t m) {
    std::string s;
    for (int32_t e = 0; e < 11; e++) {
        int32_t v = SocialField[cat].soc_effect[m].values[e];
        if (!v) continue;
        if (!s.empty()) s += " ";
        s += std::string(std::min(std::abs(v), 5), v > 0 ? '+' : '-') + std::string(SocialParam[e].set2).substr(0, 3);
    }
    return s;
}

// The rectangles of SocialWin_exec (window coordinates unless noted).
UiRect cat_rect(int32_t cat) {
    static const int32_t top[4] = {4, 0x4D, 0x96, 0xDF}, bottom[4] = {0x4A, 0x93, 0xDC, 0x125};
    return {0x1A, top[cat], 0x26E, bottom[cat]};
}
UiRect model_rect(int32_t cat, int32_t m) {
    static const int32_t left[4] = {0x1D, 0xB1, 0x145, 0x1D9}, right[4] = {0xAF, 0x143, 0x1D7, 0x26B};
    static const int32_t top[4] = {0x1D, 0x66, 0xAF, 0xF8};
    return {left[m], top[cat], right[m], top[cat] + 0x14};
}

// The allocation columns (main interface coordinates): economy, psych, labs.
UiRect alloc_rect(int32_t i) {
    const UiRect& q = mi_rect(kMiPaneQueue);
    const UiRect& b = mi_rect(kMiPaneBottom);
    int32_t left = q.left + 7, top = b.top + 7, bottom = b.bottom - 7;
    int32_t r0 = ((b.right - left) - 4) / 3 - 3 + left;  // ((7af5d8 - left) - 4) / 3 - 3 + left
    int32_t step = r0 - left + 3;
    UiRect econ{left, top, r0, bottom};
    UiRect psych{left + step, top, r0 + step, bottom};
    UiRect labs{r0 + step + 3, top, b.right - 7, bottom};
    return i == 0 ? econ : (i == 1 ? psych : labs);
}
UiRect inset(UiRect r, int32_t d) { return {r.left + d, r.top + d, r.right - d, r.bottom - d}; }

void SocialWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    UiRect frame{0x17, 0, 0x30A, 0x162};
    Buffer_box_sprite(c, &frame, box_def(9));
    // The categories (FUN_004af310): frame, title bar, the models' effects under their buttons.
    for (int32_t cat = 0; cat < 4; cat++) {
        UiRect r = cat_rect(cat);
        Buffer_box_sprite(c, &r, box_def(3));
        UiRect t{r.left + 3, r.top + 3, r.right - 3, r.top + 0x17};
        Buffer_box_sprite(c, &t, box_def(6));
        sw_font(c, font_title, 0x9F);
        sw_text_cent(this, c, sw_upper(SocialField[cat].field_name), t);
        for (int32_t m = 0; m < 4; m++) {
            UiRect mr = model_rect(cat, m);
            UiRect below{mr.left, mr.top + 0x16, mr.right, mr.bottom + 0x16};
            if (!society_avail(cat, m, faction)) {
                Buffer_box_sprite(c, &mr, box_def(6));
                Buffer_box_sprite(c, &below, box_def(6));
                continue;
            }
            Buffer_box_sprite(c, &below, box_def(6));
            sw_font(c, font_small, choices[cat] == m ? 0xFF : 0xA0);
            Buffer_set_clip(c, &below);
            sw_text_cent(this, c, model_effects(cat, m), below);
            Buffer_set_clip(c, &all);
        }
    }
    // Effect buttons' frame and the faction power box.
    UiRect eb{0x271, 4, 0x306, 0xFA};
    Buffer_box_sprite(c, &eb, box_def(3));
    UiRect power{0x271, 0xFD, 0x306, 0x12D};
    Buffer_box_sprite(c, &power, box_def(3));
    UiRect pt{power.left + 3, power.top + 3, power.right - 3, power.top + 0x17};
    Buffer_box_sprite(c, &pt, box_def(6));
    sw_font(c, font_small, 0x9F);
    sw_text_cent(this, c, sw_label(0xB94), pt);  // FACTION POWER
    std::string bonus;
    for (int32_t e = 0; e < 11; e++) {
        int32_t v = (&fac(faction).SE_economy_base)[e];
        if (v) bonus += (bonus.empty() ? "" : " ") + signed_num(v) + " " + SocialParam[e].set2;
    }
    UiRect pb{power.left + 4, pt.bottom + 1, power.right - 4, power.bottom - 3};
    sw_font(c, font_small, 0xA0);
    if (!bonus.empty()) {
        texts.push_back(bonus);
        Buffer_set_clip(c, &pb);
        Buffer_wrap_cent_4(c, bonus.c_str(), pb.left, pb.top + 1, pb.right - pb.left);
        Buffer_set_clip(c, &all);
    }
    refresh_buttons();

    // ---- the main interface ----
    sw_restore_iface(this);
    Buffer* m = &mi;
    UiRect mall{0, 0, m->width, m->height};
    Buffer_set_clip(m, &mall);
    // Left pane top (FUN_004af8c0): the faction and its leader.
    const UiRect& q = mi_rect(kMiPaneQueue);
    UiRect lead{q.left + 4, q.top + 4, q.right - 4, alloc_rect(0).top - 6};
    Buffer_box_sprite(m, &lead, box_def(3));
    sw_font(m, font_title, 0x9F);
    UiRect l1{lead.left, lead.top + 6, lead.right, lead.top + 26};
    sw_text_cent(this, m, sw_upper(MFactions[faction].noun_faction), l1);
    UiRect l2{lead.left, l1.bottom, lead.right, l1.bottom + 20};
    sw_font(m, font_text, 0xA0);
    sw_text_cent(this, m, std::string(MFactions[faction].title_leader) + " " + MFactions[faction].name_leader, l2);
    UiRect l3{lead.left, l2.bottom, lead.right, l2.bottom + 20};
    sw_text_cent(this, m, std::to_string(fac(faction).energy_credits) + " " + sw_label(0x57C), l3);
    // Centre pane (FUN_004afb60): net income, breakthroughs, the allocation's inefficiency, upheaval.
    UiRect sp = inset(mi_rect(kMiPaneCenter), 4);
    UiRect st{sp.left, sp.top, sp.right, sp.top + 0x14};
    Buffer_box_sprite(m, &st, box_def(6));
    sw_font(m, font_title, 0x9C);
    sw_text_cent(this, m, sw_upper(sw_label(0xAEC)), st);
    UiRect sb{sp.left, st.bottom + 2, sp.right, sp.bottom};
    Buffer_box_sprite(m, &sb, box_def(0));
    sw_font(m, font_text, 0xA0);
    int32_t lh = font_text->height, y = sb.top + 6, x = sb.left + 6;
    sw_text(this, m, sw_label(0xEFC) + ": " + std::to_string(FactionCurrentNetIncome), x, y);
    y += lh;
    sw_text(this, m, sw_label(600) + ": " + std::to_string(FactionCurrentBreakthroughTurns) + " " + sw_label(0xC4), x,
            y);
    y += lh;
    {
        // The allocation's inefficiency (EFFIC below 4 with a share above 50%).
        Faction& f = fac(faction);
        int32_t over = f.SE_alloc_psych - 10 + f.SE_alloc_labs * 2;
        if (over < 0) over = (5 - f.SE_alloc_labs) * 2 - f.SE_alloc_psych;
        over /= 2;
        int32_t eff = std::max(-4, std::min(4, effects[1]));
        int32_t pen = (eff - 4 < 0) ? 4 - eff : 0;
        if (effects[1] > 4) pen = 0;
        pen *= 2;
        if (over != 0 && pen != 0) {
            std::string s;
            int32_t labs = f.SE_alloc_labs, econ = 10 - f.SE_alloc_psych - labs;
            if (econ < labs) {
                s = std::to_string(-std::max(0, std::min(0x28, pen * over))) + "% " + sw_label(0xF0);
                if (10 - f.SE_alloc_psych != labs)
                    s += ", " + std::to_string(-std::max(0, std::min(0x50, pen * over * 2))) + "% " + sw_label(0x2A4);
            } else {
                if (labs != 0)
                    s = std::to_string(-std::max(0, std::min(0x50, pen * over * 2))) + "% " + sw_label(0xF0) + ", ";
                s += std::to_string(-std::max(0, std::min(0x28, pen * over))) + "% " + sw_label(0x2A4);
            }
            sw_font(m, font_text, 0xF9);
            sw_text(this, m, s, x, y);
            y += lh;
        }
        int32_t cost = social_upheaval(faction, choices) - f.SE_upheaval_cost_paid;
        if (cost < 0) {
            sw_font(m, font_text, 0xA0);
            sw_text(this, m, sw_label(0x578) + ": +" + std::to_string(-cost) + " " + sw_label(0x57C), x, y);
        } else if (cost > 0) {
            sw_font(m, font_text, 0xF9);
            sw_text(this, m, sw_label(0x574) + ": " + std::to_string(cost) + " " + sw_label(0x57C), x, y);
        }
    }
    // Right pane (FUN_004b0410): SOCIAL EFFECTS, the value and SOCIO's text for it.
    UiRect ep = inset(mi_rect(kMiPaneRight), 4);
    UiRect et{ep.left + 3, ep.top + 3, ep.right - 3, ep.top + 0x17};
    Buffer_box_sprite(m, &ep, box_def(3));
    Buffer_box_sprite(m, &et, box_def(6));
    sw_font(m, font_title, 0x9F);
    sw_text_cent(this, m, sw_label(0xB90), et);
    y = et.bottom + 3;
    UiRect eclip{ep.left + 3, et.bottom, ep.right - 3, ep.bottom - 3};
    Buffer_set_clip(m, &eclip);
    for (int32_t e = 0; e < 11; e++) {
        if (e == 3) continue;
        std::string desc;
        StrBuffer[0] = 0;
        std::string sec = std::string("SOC") + SocialParam[e].set2;
        if (!text_open(AlphaxFileName, sec.c_str())) {
            text_get();
            int32_t n = text_item_number();
            desc = text_item();
            while (n < effects[e]) {
                text_get();
                if (!std::strlen(TextBufferGetPtr)) break;
                n = text_item_number();
                desc = text_item();
            }
            text_close();
        }
        int32_t pend = pending_effects(faction)[e];
        int32_t color = effects[e] == pend ? 0xA0 : (pend < effects[e] ? 0xFA : 0xF9);
        sw_font(m, font_small, color);
        UiRect vr{ep.left + 4, y, ep.left + 0x16, y + font_small->height};
        sw_text_right(this, m, signed_num(effects[e]), vr);
        sw_text(this, m, desc, ep.left + 0x19, y);
        y += font_small->height;
    }
    Buffer_set_clip(m, &mall);
    // The allocation (FUN_004b08a0 / 004b0d50 / 004b0b00): name and share, the bar.
    static const uint32_t names[3] = {0x5B8, 0x4F8, 0xF0};
    Faction& f = fac(faction);
    int32_t shares[3] = {10 - f.SE_alloc_psych - f.SE_alloc_labs, f.SE_alloc_psych, f.SE_alloc_labs};
    for (int32_t i = 0; i < 3; i++) {
        UiRect r = alloc_rect(i);
        Buffer_box_sprite(m, &r, box_def(6));
        UiRect t{r.left + 0x1A, r.top + 3, r.right - 0x1A, r.top + 0x17};
        Buffer_box_2(m, &t, 0xBC, 0xBC);
        sw_font(m, font_title, 0x9F);
        sw_text_cent(this, m, sw_upper(sw_label(names[i]) + " " + std::to_string(shares[i] * 10) + "%"), t);
        int32_t bl = r.left + 3 + checks[0].full_width, br = r.right - 3, bb = r.bottom - 3,
                bt = bb - checks[0].full_height;
        int32_t split = bl + ((br - bl) * shares[i]) / 10;
        if (split < br) Buffer_tile(m, &tiles[i * 2], split, bt, split, bt, br - split, bb - bt);
        int32_t fill_end = split + (shares[i] != 0 ? 1 : 0);
        if (split >= br) fill_end = br;
        if (bl < fill_end) Buffer_tile(m, &tiles[i * 2 + 1], bl, bt, bl, bt, fill_end - bl, bb - bt);
        Sprite* ck = &checks[locks[i] ? 1 : 0];
        sprite_copy1(ck, m, 9, r.left + 3, r.bottom - 0x11, 1, 1);
    }
}

void SocialWinState::log_state() {
    std::fprintf(stderr, "socialstate f%d choice %d %d %d %d effects", faction, choices[0], choices[1], choices[2],
                 choices[3]);
    for (int32_t e = 0; e < 11; e++) std::fprintf(stderr, " %d", effects[e]);
    Faction& f = fac(faction);
    std::fprintf(stderr, " upheaval %d income %d breakthrough %d alloc %d %d %d locks %d %d %d\n",
                 social_upheaval(faction, choices) - f.SE_upheaval_cost_paid, net_income, breakthrough,
                 10 - f.SE_alloc_psych - f.SE_alloc_labs, f.SE_alloc_psych, f.SE_alloc_labs, locks[0], locks[1],
                 locks[2]);
}

void load_art(SocialWinState* s) {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, "socwin.pcx", nullptr, 10, 0xEC) != 0) return;
    pfncSpriteFromCanvasRectTrans(&s->checks[0], &art, 9, 0xF5, 1, 0x18, 0xE, 0);
    pfncSpriteFromCanvasRectTrans(&s->checks[1], &art, 9, 0xF5, 0x10, 0x18, 0xE, 0);
    for (int32_t i = 0, y = 1; y < 0x40; y += 0x15) {
        pfncSpriteFromCanvasRectTrans(&s->plusminus[i++], &art, 9, 0x13B, y, 0x14, 0x14, 0);
        pfncSpriteFromCanvasRectTrans(&s->plusminus[i++], &art, 9, 0x150, y, 0x14, 0x14, 0);
    }
    int32_t k = 0;
    for (int32_t col = 0, x = 0x10E; col < 3; col++, x += 0xF) {
        for (int32_t row = 0, y = 1; row < 2; row++, y += 0xF, k++) {
            Buffer_ctor(&s->tiles[k]);
            Buffer_init(&s->tiles[k], 0xE, 0xE, 8, 0);
            Buffer_copy_4(&art, &s->tiles[k], x, y, 0, 0, 0xE, 0xE);
        }
    }
}

}  // namespace

// SocialWin_exec (0x4B2740): builds the window for faction_id and runs it; negative faction_id:
// the tutorial tour's display (built and shown, without the modal loop). The first time, the
// SOCWIN hint (tut_check2 8, TutorialArrowShown 5) points at the first category.
void THISCALL SocialWin_exec(uint8_t* self, int32_t faction_id) {
    SocialWinState* s = &g_sw;
    s->name = "social";
    s->slide = true;  // FUN_004B2120: slide_show / slide_hide
    bool tour = faction_id < 0;
    if (tour) faction_id = -faction_id;
    *reinterpret_cast<int32_t*>(self + 0xCF4) = faction_id;
    s->faction = faction_id;
    s->limit_hit = 0;
    s->effect_focus = -1;
    s->effect_selected = -1;
    s->resetting = 0;
    int32_t w = 0x341, h = 0x162;
    basewin_mi_init();  // MiConsoleHeight
    sw_init(s, (ScreenWidth - 0x321) / 2, ScreenHeight - MiConsoleHeight - h, w, h, true);
    load_art(s);
    s->font_title = find_font(14, 1);
    s->font_button = find_font(14, 1);
    s->font_text = find_font(12, 0);
    s->font_small = find_font(11, 0);
    Faction& f = Factions[faction_id];
    for (int32_t* a : {s->alloc_open, s->alloc, s->alloc_work}) {
        a[0] = 10 - f.SE_alloc_labs - f.SE_alloc_psych;
        a[1] = f.SE_alloc_psych;
        a[2] = f.SE_alloc_labs;
    }
    s->reset();
    sw_clear_buttons(s);
    for (int32_t cat = 0; cat < 4; cat++) {
        for (int32_t m = 0; m < 4; m++) {
            s->model_btn[cat * 4 + m] = nullptr;
            if (!society_avail(cat, m, faction_id)) continue;
            UiRect r = model_rect(cat, m);
            s->model_btn[cat * 4 + m] = sw_button(s, sw_upper(SocialField[cat].soc_name[m]).c_str(), cat * 4 + m,
                                                  r.left, r.top, r.right - r.left, r.bottom - r.top, 0, s->font_button);
        }
    }
    for (int32_t i = 0; i < 10; i++) {
        int32_t y = 0x1D + i * 0x16;
        s->effect_btn[i] = sw_button(s, sw_upper(SocialParam[effect_of_button(i)].set2).c_str(), 0x11 + i, 0x274, y,
                                     0x303 - 0x274, 0x14, 0, s->font_button);
    }
    sw_button(s, sw_upper(sw_label(0x198)).c_str(), kCancel, 0x1E4, 0x13A, 0x286 - 0x1E4, 0x14, 0, s->font_button);
    sw_button(s, sw_upper(sw_label(0x4F4)).c_str(), kReset, 0x13F, 0x13A, 0x1E1 - 0x13F, 0x14, 0, s->font_button);
    sw_button(s, sw_upper(sw_label(0x194)).c_str(), kOk, 0x9A, 0x13A, 0x13C - 0x9A, 0x14, 0, s->font_button);
    // The allocation's -/+ buttons (economy 0x1D/0x1E, labs 0x1F/0x20, psych 0x21/0x22) and locks.
    static const int32_t ids[3][2] = {{0x1D, 0x1E}, {0x21, 0x22}, {0x1F, 0x20}};
    for (int32_t i = 0; i < 3; i++) {
        UiRect r = alloc_rect(i);
        for (int32_t k = 0; k < 2; k++) {
            int32_t x = k ? r.right - 0x17 : r.left + 3;
            BaseButton* b = sw_button(s, nullptr, ids[i][k], x, r.top + 3, 0x14, 0x14, 1, s->font_button);
            for (int32_t st = 0; st < 3; st++) {
                b->tl[st] = b->top[st] = b->tr[st] = nullptr;
                b->sprite[st] = &s->plusminus[k * 3 + st];
            }
        }
        spot_add(&s->spots[1], i, 1, r.left + 3, r.bottom - 0x11, 0x18, 0xE);
    }
    if (tour) {
        s->open = true;
        sw_show(s);
        return;
    }
    if (tut_check2(8)) {
        sw_show(s);
        TutorialArrowShown = 5;
        int32_t x = 0, y = 0;
        socialwin_point(0, &x, &y);
        TutWin_tut_win(&TutWin, MapWins[0], "SOCWIN", x, y, nullptr, 1, -1, -1);
    }
    sw_run(s);
}

bool socialwin_open() { return g_sw.open; }

// FUN_004B26B0 / FUN_004B26D0 after the tour's display: the window hidden, its buttons closed.
void socialwin_tour_close() {
    g_sw.open = false;
    sw_hide(&g_sw);
    sw_clear_buttons(&g_sw);
}

// The centres on the screen of SocialWin's rectangles the tour points at (TutWin_soc_rect /
// TutWin_iface_rect): 0..3 the categories (+0x1F110..), 10..19 the effect rows (+0x1F070..),
// 20 the effects box (+0x1F060), 21 the faction's power box (+0x1F2A0), 22 CANCEL, 23 RESET,
// 24 OK, 30 the right pane (+0x1F250), 31 economy, 32 psych, 33 labs (+0x1F260 / 0x1F280 /
// 0x1F270), 34 the centre pane (+0x1F2E0).
void socialwin_point(int32_t which, int32_t* x, int32_t* y) {
    UiRect r{};
    bool iface = which >= 30;
    if (which < 4) r = cat_rect(which);
    else if (which >= 10 && which < 20)
        r = {0x274, 0x1D + (which - 10) * 0x16, 0x303, 0x1D + (which - 10) * 0x16 + 0x14};
    else if (which == 20) r = {0x271, 4, 0x306, 0xFA};
    else if (which == 21) r = {0x271, 0xFD, 0x306, 0x12D};
    else if (which == 22) r = {0x1E4, 0x13A, 0x286, 0x14E};
    else if (which == 23) r = {0x13F, 0x13A, 0x1E1, 0x14E};
    else if (which == 24) r = {0x9A, 0x13A, 0x13C, 0x14E};
    else if (which == 30) r = inset(mi_rect(kMiPaneRight), 4);
    else if (which >= 31 && which <= 33) r = alloc_rect(which - 31);
    else if (which == 34) r = inset(mi_rect(kMiPaneCenter), 4);
    *x = (r.right - r.left) / 2 + r.left;
    *y = (r.bottom - r.top) / 2 + r.top;
    if (iface) {
        *x += mi_rect(kMiPos).left;
        *y += mi_rect(kMiPos).top;
    } else {
        *x += g_sw.x;
        *y += g_sw.y;
    }
}

// SocialWin_close (FUN_004b26d0): the buttons and groups closed, the hover state cleared.
void THISCALL SocialWin_close(uint8_t*) {
    sw_clear_buttons(&g_sw);
    g_sw.resetting = 0;
    g_sw.effect_focus = -1;
}
