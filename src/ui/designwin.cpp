// The design workshop (DesignWin 0x71F2B0; ui module 0x4328B0..0x43EFA0): DesignWin_exec and the
// methods it runs. Opened by design_new_veh ('u' on the main view, the base window's production
// picker, tech_achieved's ASKSEEDESIGN) for a faction and a prototype (-1: a new design in the
// first empty slot).
//
// The game state changes are the original's, in its order of calls (asm-faithful):
//   * DesignWin_exec: enemy_capabilities, cost_factor, setup_veh, proto_sort_2, the second
//     ability slot dropped without two-ability tech, the DESIGNLABELS texts;
//   * DesignWin_setup_veh: a prototype's components (flags 0x80 and, for a name equal to the
//     automatic one, 2 cleared), or for an empty slot the best chassis / weapon / armour the
//     faction has refined by consider_designs; an invalid prototype (psi / intrinsic 15) gives the
//     first empty slot of the sorted list;
//   * the component pickers (FUN_00433F70: a grid of the enabled components; only researched ones
//     can be chosen) and DesignWin_select_chassis / weapon / armor / reactor with their rules
//     (missile chassis and payloads, sea / air weapons, transports), DesignWin_suggest_abil
//     (GamePreferences 0x4000: abilities suggested for the new component) and check_abil
//     (abil_cond, two abilities only with the tech); select_special: TWOSPECIAL / MISSILESPECIAL
//     or the UNITSPECIAL list (available abilities that are not a free faction ability or in the
//     other slot);
//   * the buttons (DesignWin_on_button_clicked): APPLY (1) and DONE (4, Enter) save the design
//     (DesignWin_design_it: an identical prototype is reused and un-obsoleted, else the slot
//     (an empty one, the first own unused non-obsolete one...; OVERWRITEPROTO / retire_proto when
//     full, INTRINSICNAME / INTRINSICTYPE for the basic units), make_proto, the name (name_proto
//     when empty, flag 2 for a custom one), synch_proto, prune_protos); RENAME (5: DESIGNNAME,
//     X_pop_ask); OBSOLETE (6, m / o: the faction's obsolete bit, flags 8 / 0x200, synch_obs,
//     then the next prototype); UPGRADE (7, u: WHICHUPGRADE2 with the faction's better designs of
//     the same chassis, upgrade_prototype, NOUPGRADE / YESUPGRADE); RETIRE (0: RETIRETYPE(2),
//     retire_proto); CANCEL (8, Esc); the presets (9..22: consider_designs with
//     DesignSuggestPlan, NOPRESET); the prototype list's arrows (0x17 / 0x18, Left / Right, PgUp /
//     PgDn / Home / End) and a click on a prototype (on_iface_left_click: setup_veh);
//   * the drawing's own writes: Units[63]'s components (the preview's fake unit: chassis, weapon,
//     armour, reactor, abilities, icon -1 after drawing), the automatic-name flag of the slot and
//     DesignWin_draw_info's choice of the slot a new design would take.
// Multiplayer (diplo_lock / message_data / NetDaemon locks) is not reconstructed: those paths
// stop at the first network call (native builds).
//
// Drawing (approximated, not compared pixel for pixel): the window (deswin.pcx above the main
// interface) with the six component boxes (the component's 3D model where it has one, its name
// and values), the design's 3D model turning in the centre (vehdraw_construct_* through the voxel
// renderer, scale 0.14), its name, stats and costs (COST, PROTOTYPE COST); the main interface
// with the info pane (DESIGN WORKSHOP, the design's state), the component list (W:, A:, C:, R:,
// SP1:, SP2:), the presets and the prototype list (10 at a time: model, name, stats). The
// pickers are grids of names and values (no models); bubble help, the tutorial and the copter
// animation step are not reconstructed.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/vehdraw.h"
#include "ui/voxel.h"

namespace {

bool alien(int32_t f) { return (MFactions[f].rule_flags & 0x80) != 0; }
const char* unit_pcx(int32_t f) { return alien(f) ? "alunit_sm.pcx" : "unit_sm.pcx"; }
UNIT& U(int32_t id) { return Units[id]; }

// The preset buttons' design plans (DesignSuggestPlan per button 9..22).
const int32_t kPresetPlan[14] = {2, 3, 1, 4, 10, 6, 7, 8, 9, 0x11, 0xD, 0xE, 0xB, 0xC};

struct DesignWinState : ScreenWin {
    int32_t faction = 0;          // +0x141F4
    int32_t unit = 0;             // +0x141F8
    int32_t chassis = 0;          // +0x141FC
    int32_t weapon = 0;           // +0x14200
    int32_t armor = 0;            // +0x14204
    uint32_t abil = 0;            // +0x14208
    int32_t reactor = 1;          // +0x1420C
    int32_t f14210 = 0;           // +0x14210
    int32_t cf = 0;               // +0x14214: cost_factor
    int32_t slot[2] = {-1, -1};   // +0x14218: the abilities shown in SP1 / SP2
    char name[0x50]{};            // +0x14220
    char labels[16][45]{};        // +0x14270: DESIGNLABELS
    int32_t result = 0;           // +0xCDC
    int32_t hover = -1;           // +0xCE0: the prototype clicked in the list
    int32_t stack_pos = 0;        // +0xCE4
    int32_t frames = 0;           // +0xCE8: redraws (the "Obsolete" blink)
    float angle = 0;              // +0x12770: the model's rotation
    VoxelScene scene;             // +0x1279C
    bool scene_ready = false;
    Font f10, f12, f16;           // +0x14540.., +0x14568, +0x14590
    Buffer back;                  // deswin.pcx
    std::vector<uint8_t> back_px;
    std::vector<BaseButton*> win_buttons;
    // Costs shown by the last redraw (test aid).
    int32_t cost_shown = 0, proto_cost_shown = -1;

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right,
                  bool dbl) override;
    void log_state() override;

    // The original's methods.
    void rebuild_slots();
    void setup_veh(int32_t unit_id, int32_t fresh);  // 0x43E490
    int32_t design_it();                             // 0x437BF0
    void check_abil();                               // 0x439C00
    void suggest_abil(uint32_t flag);                // 0x439D30
    void select_chassis();                           // 0x439F20
    void select_weapon();                            // 0x43A300
    void select_armor();                             // 0x43A760
    void select_reactor();                           // 0x43AA00
    void select_special(int32_t which);              // 0x43AC70
    void select_name();                              // 0x43BE30
    void center_stack();                             // the stack position around the unit
    bool same_as_unit(int32_t u) const;
    void draw_info(Buffer* c);
    void draw_vehicle(Buffer* c);
    void draw_component(Buffer* c, int32_t kind);
    void draw_stack(Buffer* c);
    void draw_flash(Buffer* c);
    void render_model(Buffer* c, int32_t cx, int32_t cy, int32_t ch, int32_t w, int32_t ar, int32_t re,
                      uint32_t ab, float rot, float scale, int32_t parts);
};

DesignWinState* g_dw = nullptr;

bool DesignWinState::same_as_unit(int32_t u) const {
    const UNIT& x = U(u);
    return chassis == x.chassis_id && weapon == x.weapon_id && armor == x.armor_id && reactor == x.reactor_id &&
           abil == x.ability_flags;
}

void DesignWinState::rebuild_slots() {
    slot[0] = slot[1] = -1;
    for (int32_t i = 0; i < 29; i++) {
        if (!(abil & (1u << i))) continue;
        for (int32_t k = 0; k < 2; k++)
            if (slot[k] < 0) {
                slot[k] = i;
                break;
            }
    }
}

void DesignWinState::center_stack() {
    proto_sort_2(faction);
    int32_t n = ProtoSortCount;
    for (int32_t i = stack_pos; i < stack_pos + n; i++)
        if (unit == ProtoSortList[i % n]) {
            stack_pos = (i % n - 5 + n) % n;
            return;
        }
}

void DesignWinState::setup_veh(int32_t p, int32_t fresh) {
    if (p < 0 || !(U(p).unit_flags & 1) || (p < 64 && (Weapon[U(p).weapon_id].offense_value < 0 || p == 15))) {
        proto_sort_2(faction);
        int32_t i = 0;
        for (; i < ProtoSortCount; i++)
            if (!(U(ProtoSortList[i]).unit_flags & 1)) break;
        unit = i == ProtoSortCount ? faction << 6 : ProtoSortList[i];
    } else {
        unit = p;
    }
    UNIT& u = U(unit);
    reactor = best_reactor(faction);
    if (!(u.unit_flags & 1)) {
        armor = 0;
        weapon = 0;
        if (!fresh) {
            chassis = 0;
            abil = 0;
            reactor = 1;
            slot[0] = slot[1] = -1;
            f14210 = 0;
            return;
        }
        // The best chassis (land, fastest), weapon (strongest non-psi... below 99) and armour.
        abil = 0;
        int32_t best = 0;
        uint8_t speed = 0;
        for (chassis = 0; chassis < 9; chassis++)
            if (has_tech(Chassis[chassis].preq_tech, faction) && !Chassis[chassis].triad &&
                speed <= Chassis[chassis].speed) {
                best = chassis;
                speed = Chassis[chassis].speed;
            }
        chassis = best;
        int8_t v = 0;
        best = 0;
        for (weapon = 0; weapon < 26; weapon++)
            if (has_tech(Weapon[weapon].preq_tech, faction)) {
                int8_t o = Weapon[weapon].offense_value;
                if (o < 'c' && v <= o) {
                    best = weapon;
                    v = o;
                }
            }
        weapon = best;
        v = 0;
        best = 0;
        for (armor = 0; armor < 14; armor++)
            if (has_tech(Armor[armor].preq_tech, faction) && v <= Armor[armor].defense_value) {
                best = armor;
                v = Armor[armor].defense_value;
            }
        armor = best;
        name[0] = 0;
        DesignSuggestWeapon = weapon;
        DesignSuggestArmor = armor;
        DesignSuggestChassis = chassis;
        DesignSuggestAbilities = abil;
        DesignSuggestMode = 1;
        DesignSuggestPlan = -1;
        consider_designs(faction);
        weapon = DesignSuggestWeapon;
        armor = DesignSuggestArmor;
        chassis = DesignSuggestChassis;
        abil = DesignSuggestAbilities;
        rebuild_slots();
        DesignSuggestMode = 0;
        u.unit_flags &= ~0x80;  // (the byte: & 0x7F)
        f14210 = 0;
        return;
    }
    u.unit_flags &= 0xFF7F;
    chassis = u.chassis_id;
    weapon = u.weapon_id;
    armor = u.armor_id;
    abil = u.ability_flags;
    reactor = u.reactor_id;
    rebuild_slots();
    if ((u.unit_flags & 2) && unit > 63) {
        std::snprintf(name, sizeof(name), "%s", u.name);
        char auto_name[0x50];
        name_proto(auto_name, unit, faction, chassis, weapon, armor, abil, reactor);
        if (!std::strcmp(name, auto_name)) {
            name[0] = 0;
            u.unit_flags &= ~2;
        }
        f14210 = 0;
        return;
    }
    u.unit_flags &= 0xFFFD;
    name[0] = 0;
    f14210 = 0;
}

int32_t DesignWinState::design_it() {
    for (int32_t k = 0; k < 0x80; k++) {
        int32_t u = k < 64 ? k : faction * 64 + (k & 0x3F);
        const UNIT& x = U(u);
        if (!(x.unit_flags & 1) || !same_as_unit(u)) continue;
        if (u < 64 && !has_tech(x.preq_tech, faction)) continue;
        // An identical prototype exists: it is used (and no longer obsolete).
        if (u != unit) {
            if (x.obsolete_factions & (1u << faction)) U(u).unit_flags |= 8;
            U(u).obsolete_factions &= (uint8_t)~(1u << faction);
        }
        if (u < 64) {
            if (std::strlen(name) && std::strcmp(name, x.name)) {
                parse_says(0, x.name, -1, -1);
                popp(ScriptFileName, "INTRINSICNAME", 0, unit_pcx(faction), 0);
                synch_proto(u);
                unit = u;
                return 1;
            }
        } else if (std::strlen(name) && (U(unit).unit_flags & 2)) {
            U(u).unit_flags |= 2;
            std::snprintf(U(u).name, sizeof(U(u).name), "%s", name);
        }
        synch_proto(u);
        unit = u;
        return 1;
    }
    if (unit < 64) {
        if (same_as_unit(unit)) {
            if (!std::strlen(name) || !std::strcmp(name, U(unit).name)) return 0;
            parse_says(0, U(unit).name, -1, -1);
            // (X_pop_2's first argument is the file: the original shows nothing here)
            X_pop_2("INTRINSICNAME", unit_pcx(faction), 0);
            return 0;
        }
        int32_t f = faction;
        Faction& fa = Factions[f];
        for (int32_t i = 0; i < 64; i++) {
            int32_t u = f * 64 + i;
            if ((U(u).obsolete_factions & (1u << f)) && !fa.units_active[u] && !fa.units_queue[u]) {
                unit = u;
                break;
            }
            if (!(U(u).unit_flags & 1)) unit = u;
        }
        if (unit < 64) {
            parse_says(0, U(unit).name, -1, -1);
            popp(ScriptFileName, "INTRINSICTYPE", 0, unit_pcx(faction), 0);
            return 0;
        }
    }
    if ((U(unit).unit_flags & 1) && !same_as_unit(unit)) {
        // A modified design: the slot of an unused prototype (an empty one first).
        int32_t f = faction;
        Faction& fa = Factions[f];
        bool found = false;
        bool empty = false;
        for (int32_t i = 0; i < 64; i++) {
            int32_t u = f * 64 + i;
            if (!(U(u).unit_flags & 1)) {
                unit = u;
                empty = true;
                break;
            }
            int8_t group = U(unit).group_id;  // (read again after every change of the slot)
            if ((U(u).obsolete_factions & (1u << f)) && (U(u).group_id == group || !fa.units_active[u]) &&
                !fa.units_active[u]) {
                if (!fa.units_queue[u]) {
                    unit = u;
                    found = true;
                } else if (!found && U(u).group_id == group) {
                    unit = u;
                }
            }
        }
        if (!empty && !found && fa.units_active[unit] && (U(unit).unit_flags & 1)) {
            parse_says(0, U(unit).name, -1, -1);
            parse_num(0, fa.units_active[unit]);
            if (!popp(ScriptFileName, "OVERWRITEPROTO", 0, unit_pcx(f), 0)) return 0;
            retire_proto(unit, faction);
        }
    }
    make_proto(unit, chassis, weapon, armor, (int32_t)abil, reactor);
    U(unit).unit_flags &= 0xFF3F;  // (the byte: & 0x3F)
    if (!std::strlen(name)) {
        name_proto(name, unit, faction, chassis, weapon, armor, abil, reactor);
        name[31] = 0;
        U(unit).unit_flags &= ~2;
    } else {
        char auto_name[0x100];
        name_proto(auto_name, unit, faction, chassis, weapon, armor, abil, reactor);
        if (std::strcmp(auto_name, name)) U(unit).unit_flags |= 2;
    }
    std::snprintf(U(unit).name, sizeof(U(unit).name), "%s", name);
    synch_proto(unit);
    prune_protos(faction, unit, 1);
    return 1;
}

void DesignWinState::check_abil() {
    if (Chassis[chassis].missile) {
        abil = 0;
        slot[0] = slot[1] = -1;
        return;
    }
    for (int32_t i = 0; i < 29; i++) {
        uint32_t bit = 1u << i;
        if (!(bit & abil)) continue;
        if ((bit == 0x80 && (abil & 0x40)) || !abil_cond(i, chassis, weapon, armor) ||
            (bit_count(abil) > 1 &&
             (bit_count(abil) != 2 || !has_tech(Rules.tech_preq_allow_2_spec_abil, faction)))) {
            abil &= ~bit;
            rebuild_slots();
        }
    }
}

void DesignWinState::suggest_abil(uint32_t flag) {
    check_abil();
    if (!(GamePreferences & 0x4000)) return;
    if (!has_tech(Ability[abil_index(flag)].preq_tech, faction)) return;
    if (!abil_cond(abil_index(flag), chassis, weapon, armor)) return;
    if (bit_count(abil) > 1) return;
    if (bit_count(abil) && !has_tech(Rules.tech_preq_allow_2_spec_abil, faction)) return;
    // A free ability of the viewer's faction (bonus 15) is not suggested. The original reads 41
    // entries of the bonus arrays (MFaction + 0x510 / + 0x530).
    const uint8_t* m = reinterpret_cast<const uint8_t*>(&MFactions[CurrentPlayerFaction]);
    for (int32_t i = 0; i < 0x29; i++) {
        int32_t id, val;
        std::memcpy(&id, m + 0x510 + i * 4, 4);
        std::memcpy(&val, m + 0x530 + i * 4, 4);
        if (id == 0xF && val == abil_index(flag)) return;
    }
    abil |= flag;
    rebuild_slots();
}

// ---- the component picker (FUN_00433F70) ----

struct PickerState : ScreenWin {
    int32_t kind = 0;        // 0 chassis, 1 weapon, 2 armour, 3 reactor
    int32_t faction = 0;
    std::vector<int32_t> items;  // component ids listed
    int32_t sel = -1;        // +0xAE0
    int32_t valid = 0;       // +0x4648
    int32_t first = 0;       // +0xAD8 (scroll position; 20 visible)
    bool missile_armor = false;
    void redraw() override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) override;
    bool available(int32_t i) const;
    int32_t hit(int32_t x, int32_t y) const;
    void log_state() override {
        std::string l;
        for (int32_t id : items) l += " " + std::to_string(id);
        std::fprintf(stderr, "picker kind %d sel %d items%s\n", kind, sel, l.c_str());
    }
};

bool PickerState::available(int32_t i) const {
    if (i < 0 || i >= (int32_t)items.size()) return false;
    int32_t id = items[i];
    switch (kind) {
    case 0: return has_tech(Chassis[id].preq_tech, faction) != 0;
    case 1: return has_tech(Weapon[id].preq_tech, faction) != 0;
    case 2: return missile_armor || has_tech(Armor[id].preq_tech, faction) != 0;
    default: return has_tech(Reactor[id].preq_tech, faction) != 0;
    }
}

int32_t PickerState::hit(int32_t x, int32_t y) const {
    if (x < 0x1B || y < 4 || x >= 5 * 0x6C + 0x1B || y >= 4 * 0x5E + 4) return -1;
    int32_t i = (x - 0x1B) / 0x6C + ((y - 4) / 0x5E) * 5 + first;
    return i < (int32_t)items.size() ? i : -1;
}

std::string component_name(int32_t kind, int32_t id) {
    switch (kind) {
    case 0: return Chassis[id].offsv1_name;
    case 1: return Weapon[id].name;
    case 2: return Armor[id].name;
    default: return Reactor[id].name;
    }
}

std::string component_value(int32_t kind, int32_t id) {
    switch (kind) {
    case 0: return std::string(TriadName[Chassis[id].triad]) + " " + std::to_string(Chassis[id].speed);
    case 1:
        return Weapon[id].offense_value < 0 ? label_get(196)
               : Weapon[id].mode > 2        ? label_get(128)
                                            : std::to_string(Weapon[id].offense_value);
    case 2: return Armor[id].defense_value < 0 ? label_get(196) : std::to_string(Armor[id].defense_value);
    default: return std::to_string(id);
    }
}

void PickerState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    UiRect r{0x17, 0, c->width - 0x37, c->height};
    Buffer_box_sprite(c, &r, box_def(0));
    Font* f = find_font(0xC, 1);
    for (int32_t k = 0; k < 20 && first + k < (int32_t)items.size(); k++) {
        int32_t i = first + k;
        int32_t x = (k % 5) * 0x6C + 0x1B, y = (k / 5) * 0x5E + 4;
        UiRect b{x, y, x + 0x6C - 1, y + 0x5E - 1};
        Buffer_box_sprite(c, &b, box_def(i == sel ? 2 : 0));
        bool ok = available(i);
        sw_font(c, f, ok ? 0xE0 : 0x98);
        std::string n = component_name(kind, items[i]);
        UiRect t{b.left + 3, b.top + 4, b.right - 3, b.top + 40};
        Buffer_set_clip(c, &t);
        Buffer_wrap_cent_4(c, n.c_str(), t.left, t.top, t.right - t.left);
        Buffer_set_clip(c, &all);
        UiRect v{b.left + 3, b.bottom - 22, b.right - 3, b.bottom - 4};
        sw_text_cent(this, c, component_value(kind, items[i]), v);
        texts.push_back("item " + std::to_string(items[i]) + " " + n + (ok ? "" : " (unavailable)"));
    }
}

void PickerState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    int32_t n = (int32_t)items.size();
    int32_t cur = sel;
    switch (vk) {
    case 0xD: sw_close(this); return;
    case 0x1B:
        sel = -1;
        sw_close(this);
        return;
    case 0x24: sel = 0; break;
    case 0x23: sel = n - 1; break;
    case 0x25: case 0x64:
        if (cur % 5 > 0) sel = cur - 1;
        break;
    case 0x27: case 0x66:
        if (cur % 5 < 4 && cur + 1 < n) sel = cur + 1;
        break;
    case 0x26: case 0x68:
        if (cur >= 5) sel = cur - 5;
        break;
    case 0x28: case 0x62:
        if (cur + 5 < n) sel = cur + 5;
        break;
    default: break;
    }
    if (sel >= first + 20) first = (sel / 5 - 3) * 5;
    if (sel >= 0 && sel < first) first = (sel / 5) * 5;
    valid = available(sel);
}

void PickerState::on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) {
    if (parent || right) return;
    int32_t i = hit(x, y);
    if (i < 0) return;
    sel = i;
    valid = available(i);
    if (valid) sw_close(this);
}

// FUN_00433F70: the picker over the design window; returns the index of the chosen component
// among the enabled ones (preq_tech >= -1), or `current` when nothing was chosen.
int32_t run_picker(DesignWinState* dw, int32_t kind, int32_t current) {
    PickerState p;
    p.name = "picker";
    p.kind = kind;
    p.faction = dw->faction;
    switch (kind) {
    case 0:
        for (int32_t i = 0; i < 9; i++)
            if (Chassis[i].preq_tech > -2) p.items.push_back(i);
        break;
    case 1:
        for (int32_t i = 0; i < 26; i++)
            if (Weapon[i].preq_tech > -2) p.items.push_back(i);
        break;
    case 2:
        if (!Chassis[dw->chassis].missile) {
            for (int32_t i = 0; i < 14; i++)
                if (Armor[i].preq_tech > -2) p.items.push_back(i);
        } else {
            p.items.push_back(0);
            p.missile_armor = true;
        }
        break;
    default:
        for (int32_t i = 1; i < 4; i++)  // (reactor 0 is not listed: the index is one less)
            if (Reactor[i].preq_tech > -2) p.items.push_back(i);
        break;
    }
    int32_t start = kind == 3 ? current - 1 : current;
    p.sel = start;
    p.valid = 0;
    if (p.sel >= 20) p.first = (p.sel / 5 - 3) * 5;
    sw_init(&p, dw->x, dw->y, dw->canvas.width, dw->canvas.height, false);
    sw_run(&p);
    sw_clear_buttons(&p);
    if (p.sel >= 0 && p.valid) return kind == 3 ? p.sel + 1 : p.sel;
    return current;
}

// The filtered index of `id` among the enabled components of a table, and back.
template <typename T> int32_t filtered_index(const T* table, int32_t n, int32_t id) {
    int32_t k = 0;
    for (int32_t i = 0; i < n; i++) {
        if (table[i].preq_tech < -1) continue;
        if (i == id) break;
        k++;
    }
    return k;
}
template <typename T> int32_t from_filtered(const T* table, int32_t n, int32_t k) {
    int32_t i = 0;
    for (; i < n; i++) {
        if (table[i].preq_tech < -1) continue;
        if (k == 0) break;
        k--;
    }
    return i;
}

void DesignWinState::select_chassis() {
    int32_t k = run_picker(this, 0, filtered_index(&Chassis[0], 9, chassis));
    int32_t c = from_filtered(&Chassis[0], 9, k);
    uint8_t old_triad = Chassis[chassis].triad;
    chassis = c;
    if (!Chassis[c].missile) {
        if (Weapon[weapon].offense_value > 'b' || weapon == 0x17 || weapon == 0x18 || weapon == 0x19) weapon = 0;
    } else {
        armor = 0;
        if (Weapon[weapon].mode > 1) weapon = 0;
        if (weapon != 0x10 && weapon != 0x18 && weapon != 0x19) weapon = 0x17;
    }
    if (Chassis[c].triad != old_triad) {
        if (Chassis[c].triad == 0) {
            if (abil & 2) {
                abil &= ~2u;
                rebuild_slots();
            }
        } else {
            suggest_abil(2);
            if (Chassis[chassis].triad == 2) {
                if (Weapon[weapon].mode == 0xB) weapon = 0;
                if (Weapon[weapon].mode == 9 && Chassis[chassis].range) weapon = 0;
            }
        }
    }
    check_abil();
}

void DesignWinState::select_weapon() {
    int32_t k = run_picker(this, 1, filtered_index(&Weapon[0], 26, weapon));
    int32_t w = from_filtered(&Weapon[0], 26, k);
    int32_t old = weapon;
    weapon = w;
    const CChassis& ch = Chassis[chassis];
    bool missile_done = false;
    if (ch.triad == 2) {
        if (Weapon[w].mode == 0xB) weapon = old;
        if (Weapon[weapon].mode == 9 && ch.range) weapon = old;
        w = weapon;
        if (ch.missile) {
            if (w != 0x10 && w != 0x18 && w != 0x19) weapon = 0x17;
            missile_done = true;
        }
    }
    if (!missile_done) {
        if (w == 0x10) weapon = old;
        if (weapon == 0x17) weapon = old;
        if (weapon == 0x18) weapon = old;
        if (weapon == 0x19) weapon = old;
    }
    if (Weapon[weapon].offense_value > 'b') {
        slot[0] = slot[1] = -1;
        abil = 0;
    }
    if (!abil && weapon != old) {
        if (Weapon[weapon].mode == 7) suggest_abil(0x80000);
        if (Weapon[weapon].mode == 9) suggest_abil(1);
        if (Weapon[weapon].mode == 9) suggest_abil(0x2000);
        if (Armor[armor].defense_value * 2 <= Weapon[weapon].offense_value) suggest_abil(0x20000);
    }
    check_abil();
}

void DesignWinState::select_armor() {
    int32_t k = run_picker(this, 2, filtered_index(&Armor[0], 14, armor));
    armor = from_filtered(&Armor[0], 14, k);
    if (Armor[armor].defense_value > 1 && Weapon[weapon].offense_value < 2) suggest_abil(0x200);
    check_abil();
}

void DesignWinState::select_reactor() {
    int32_t k = run_picker(this, 3, filtered_index(&Reactor[0], 4, reactor));
    reactor = from_filtered(&Reactor[0], 4, k);
    check_abil();
}

void DesignWinState::select_special(int32_t which) {
    int32_t f = faction;
    if (which && !has_tech(Rules.tech_preq_allow_2_spec_abil, f)) {
        parse_say(0, Tech[Rules.tech_preq_allow_2_spec_abil].name, -1, -1);
        popp(ScriptFileName, "TWOSPECIAL", 0, unit_pcx(f), 0);
        return;
    }
    if (Chassis[chassis].missile) {
        popp(ScriptFileName, "MISSILESPECIAL", 0, unit_pcx(f), 0);
        return;
    }
    BasePop* pop = popup_new();
    Popup_start(pop, ScriptFileName, "UNITSPECIAL", -1, nullptr, 0x200042);
    BasePop_item(pop, label_get(136), 0x1D);
    const uint8_t* m = reinterpret_cast<const uint8_t*>(&MFactions[f]);
    for (int32_t i = 0; i < 29; i++) {
        if (!has_tech(Ability[i].preq_tech, f) || !abil_cond(i, chassis, weapon, armor)) continue;
        bool skip = false;
        for (int32_t b = 0; b < 0x29 && !skip; b++) {
            int32_t id, val;
            std::memcpy(&id, m + 0x510 + b * 4, 4);
            std::memcpy(&val, m + 0x530 + b * 4, 4);
            if (id == 0xF && val == i) skip = true;
        }
        for (int32_t s = 0; s < 2 && !skip; s++)
            if (s != which && slot[s] == i) skip = true;
        if (skip) continue;
        // "Name: description|Cost: ..." (the cost text is a summary of the original's)
        std::string t = std::string(Ability[i].name) + ": " + Ability[i].description + "|" + label_get(88) + ": " +
                        std::to_string(Ability[i].cost);
        BasePop_item(pop, t.c_str(), i);
    }
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0) return;
    if (r > 0x1C) r = -1;
    slot[which] = r;
    abil = 0;
    for (int32_t s = 0; s < 2; s++)
        if (slot[s] >= 0) abil |= 1u << slot[s];
}

void DesignWinState::select_name() {
    char cur[0x50];
    std::snprintf(cur, sizeof(cur), "%s", name);
    if (!std::strlen(cur)) {
        name_proto(cur, unit, faction, chassis, weapon, armor, abil, reactor);
        U(unit).unit_flags &= ~2;
    }
    cur[31] = 0;
    if (X_pop_ask("DESIGNNAME", 0x1F, cur, 0, 0)) return;
    const char* typed = ParseStrBuffer[0];
    if (!std::strlen(typed)) {
        name[0] = 0;
        U(unit).unit_flags &= ~2;
    } else if (std::strcmp(cur, typed)) {
        std::strncpy(name, typed, 0x20);
        name[31] = 0;
        U(unit).unit_flags |= 2;
    }
}

void DesignWinState::on_button(int32_t id) {
    int32_t f = faction;
    switch (id) {
    case 0: {  // RETIRE
        parse_says(0, U(unit).name, -1, -1);
        int32_t r;
        if (!Factions[f].units_active[unit]) {
            r = popp(ScriptFileName, "RETIRETYPE2", 0, unit_pcx(f), 0);
        } else {
            parse_num(0, Factions[f].units_active[unit]);
            r = popp(ScriptFileName, "RETIRETYPE", 0, unit_pcx(f), 0);
        }
        if (!r) break;
        retire_proto(unit, f);
        setup_veh(unit, 0);
        center_stack();
        break;
    }
    case 1:  // APPLY
        result = design_it();
        setup_veh(unit, 0);
        center_stack();
        break;
    case 2:  // scenario editor: the prototype flag
        if (GameState & 0x40) {
            result = design_it();
            setup_veh(unit, 0);
            center_stack();
            U(unit).unit_flags ^= 4;
        }
        break;
    case 3:  // scenario editor: RETIRE ALL
        if (GameState & 0x40) {
            int32_t r = popp(ScriptFileName, "RETIREALL", 0, unit_pcx(f), 0);
            if (!r) break;
            for (int32_t g = 1; g < 8; g++) {
                if (r <= 1 && g != f) continue;
                for (int32_t i = 0; i < 512; i++) {
                    U(i).obsolete_factions &= (uint8_t)~(1u << g);
                    if (i >= g * 64 && i < g * 64 + 64) U(i).unit_flags &= 0xFFFE;
                }
                clear_bunglist(g);
            }
            setup_veh(0, 0);
            proto_sort_2(f);
            int32_t n = ProtoSortCount;
            for (int32_t i = stack_pos; i < stack_pos + n; i++)
                if (ProtoSortList[i % n] == 0) {
                    stack_pos = (i % n - 5 + n) % n;
                    break;
                }
        }
        break;
    case 5: select_name(); break;
    case 6: {  // OBSOLETE
        UNIT& u = U(unit);
        if (!(u.obsolete_factions & (1u << f))) {
            u.unit_flags &= 0xFDF7;
        } else {
            if (u.unit_flags & 8) u.unit_flags |= 0x200;
            u.unit_flags |= 8;
        }
        u.obsolete_factions ^= (uint8_t)(1u << f);
        synch_obs(unit);
        int32_t n = ProtoSortCount;
        for (int32_t i = stack_pos; i < stack_pos + n; i++)
            if (unit == ProtoSortList[i % n]) {
                unit = ProtoSortList[(i + 1) % n];
                break;
            }
        setup_veh(unit, 0);
        center_stack();
        break;
    }
    case 7: {  // UPGRADE
        parse_says(0, U(unit).name, -1, -1);
        BasePop* pop = popup_new();
        Popup_start(pop, ScriptFileName, "WHICHUPGRADE2", -1, nullptr, 0);
        int32_t count = 0, last = 0;
        const UNIT& cur = U(unit);
        for (int32_t i = 0; i < 64; i++) {
            int32_t u = f * 64 + i;
            const UNIT& x = U(u);
            if (u == unit || !(x.unit_flags & 1) || (x.obsolete_factions & (1u << f))) continue;
            if (x.chassis_id != cur.chassis_id || ((x.ability_flags ^ cur.ability_flags) & 0x8000)) continue;
            if (!((uint8_t)x.plan == (uint8_t)cur.plan || ((uint8_t)cur.plan < 4 && (uint8_t)x.plan < 4))) continue;
            int8_t o = Weapon[cur.weapon_id].offense_value;
            if (o == 0) {
                if (cur.weapon_id != x.weapon_id) continue;
            } else if (o > Weapon[x.weapon_id].offense_value) {
                continue;
            }
            if (!(Armor[cur.armor_id].defense_value <= Armor[x.armor_id].defense_value ||
                  (Armor[x.armor_id].defense_value != 1 && (o == 0 || o < Weapon[x.weapon_id].offense_value))))
                continue;
            count++;
            char stats[256];
            stats[0] = 0;
            say_stats_3(stats, u);
            std::string t = std::string(x.name) + "|" + stats;
            if (!(x.unit_flags & 4) && (cur.unit_flags & 4)) t += " *";
            BasePop_item(pop, t.c_str(), u);
            last = u;
        }
        int32_t r = last;
        if (!count) {
            popup_delete(pop);
            popp(ScriptFileName, "NOUPGRADE", 0, unit_pcx(f), 0);
            break;
        }
        if (count != 1) r = BasePop_exec_3(pop, 0, 0);
        popup_delete(pop);
        if (r < 0) break;
        uint8_t active = Factions[f].units_active[unit];
        if (upgrade_prototype(f, r, unit, 0)) {
            unit = r;
            setup_veh(r, 0);
            center_stack();
        }
        if (!active) popp(ScriptFileName, "YESUPGRADE", 0, unit_pcx(f), 0);
        break;
    }
    case 0x17:
        if (--stack_pos < 0) stack_pos = ProtoSortCount;
        break;
    case 0x18:
        if (++stack_pos == ProtoSortCount) stack_pos = 0;
        break;
    case -2:
    case 8:  // CANCEL
        result = 0;
        sw_close(this);
        break;
    case -1:
    case 4:  // DONE
        result = design_it();
        if (result) sw_close(this);
        break;
    default:
        if (id >= 9 && id <= 0x16) {  // the presets
            DesignSuggestPlan = kPresetPlan[id - 9];
            DesignSuggestMode = 1;
            DesignSuggestWeapon = weapon;
            DesignSuggestArmor = armor;
            DesignSuggestChassis = chassis;
            DesignSuggestAbilities = abil;
            DesignSuggestReactor = reactor;
            consider_designs(f);
            weapon = DesignSuggestWeapon;
            armor = DesignSuggestArmor;
            abil = DesignSuggestAbilities;
            chassis = DesignSuggestChassis;
            reactor = DesignSuggestReactor;
            rebuild_slots();
            if (DesignSuggestMode < 2) popp(ScriptFileName, "NOPRESET", 0, unit_pcx(f), 0);
            DesignSuggestMode = 0;
        }
        break;
    }
}

void DesignWinState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    int32_t n = ProtoSortCount;
    switch (vk) {
    case 0xD: on_button(4); break;
    case 0x1B: on_button(8); break;
    case 0x21: case 0x69: stack_pos = std::max(0, (stack_pos - 9 + n) % std::max(n, 1)); break;
    case 0x22: case 0x63: stack_pos = std::max(0, (stack_pos + 9 + n) % std::max(n, 1)); break;
    case 0x23: case 0x61: stack_pos = std::max(0, n - 10); break;
    case 0x24: case 0x67: stack_pos = 0; break;
    case 0x25: case 0x64: on_button(0x17); break;
    case 0x27: case 0x66: on_button(0x18); break;
    default: break;
    }
    if (!open || (key & 0x60000)) return;
    // The characters (m / o: obsolete, u: upgrade).
    if (vk == 'M' || vk == 'O') on_button(6);
    else if (vk == 'U') on_button(7);
}

void DesignWinState::on_click(int32_t parent, int32_t index, int32_t, int32_t, int32_t, bool right, bool) {
    if (right || index < 0) return;
    if (parent == 1) {  // the prototype list (DesignWin_on_iface_left_click)
        hover = index;
        setup_veh(index, 0);
        return;
    }
    switch (index) {
    case 0: select_chassis(); break;
    case 1: select_weapon(); break;
    case 2: select_armor(); break;
    case 3: select_reactor(); break;
    case 4: select_special(0); break;
    case 5: select_special(1); break;
    }
}

// ---- drawing ----

// The component boxes (spots 0..5 of the window: chassis, weapon, armour / reactor, SP1, SP2).
const UiRect kBoxes[6] = {{0x1E, 7, 0x81, 0x5C},   {0x1E, 0x65, 0x81, 0xBA},  {0x1E, 0xC3, 0x81, 0x118},
                          {0x1CE, 7, 0x231, 0x5C}, {0x1CE, 0x65, 0x231, 0xBA}, {0x1CE, 0xC3, 0x231, 0x118}};
const UiRect kVehicle{0x8C, 9, 0x1C3, 0x140};

void DesignWinState::render_model(Buffer* c, int32_t cx, int32_t cy, int32_t ch, int32_t w, int32_t ar, int32_t re,
                                  uint32_t ab, float rot, float scale, int32_t parts) {
    VoxelScene* sc = &scene;
    if (!scene_ready) {
        Scene_ctor(sc);
        Scene_init(sc);
        scene_ready = true;
    }
    float angles[3] = {0.0f, 0.0f, 0.785f};
    float mat[9];
    mat3_from_euler(angles, mat);
    mat[6] = -mat[6];
    mat[7] = -mat[7];
    mat[8] = -mat[8];
    const float pos[3] = {(float)((double)mat[6] * -10000.0f), (float)((double)mat[7] * -10000.0f),
                          (float)((double)mat[8] * -10000.0f)};
    Scene_set_camera(sc, pos, mat);
    const float light[3] = {-0.57735f, 0.57735f, 0.57735f};
    std::memcpy(sc->v_light, light, 12);
    std::memcpy(sc->v_shadow, light, 12);
    Scene_set_rotation(sc, rot, 0.0f, 0.0f);
    Scene_set_scale(sc, scale);
    sc->count = 0;
    int32_t chassis_drawn = ch;
    if (w == 0x15 && ch == 1 && Armor[ar].defense_value < 2) chassis_drawn = 0;
    if (parts & 1) vehdraw_construct_chassis(faction, chassis_drawn, w, ar, re, sc);
    if (parts & 2) vehdraw_construct_weapon(faction, chassis_drawn, w, sc);
    if (parts & 4) vehdraw_construct_armor(CurrentPlayerFaction, chassis_drawn, w, ar, 0, sc);
    if (parts & 8) vehdraw_construct_reactor(CurrentPlayerFaction, chassis_drawn, w, re, ab, sc);
    if (parts & 0x10) vehdraw_construct_abil(ab, sc, faction);
    if (!sc->count) return;
    Scene_render(sc, c, cx, cy, 0x13);
    Scene_clear(sc, 0);
}

void DesignWinState::draw_component(Buffer* c, int32_t kind) {
    UiRect r = kBoxes[kind];
    Buffer_box_sprite(c, &r, box_def(3));
    UiRect in{r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
    Buffer_box_sprite(c, &in, box_def(0));
    static const int32_t kLabel[6] = {10, 8, 9, 11, 12, 13};  // C:, W:, A:, R:, SP1:, SP2:
    std::string nm, val;
    int32_t parts = 0;
    switch (kind) {
    case 0:
        nm = Chassis[chassis].offsv1_name;
        val = component_value(0, chassis);
        parts = 1;
        break;
    case 1:
        nm = Weapon[weapon].name;
        val = component_value(1, weapon);
        parts = 2;
        break;
    case 2:
        nm = Armor[armor].name;
        val = component_value(2, armor);
        parts = 4;
        break;
    case 3:
        nm = Reactor[reactor].name;
        val = std::to_string(reactor);
        parts = 1 | 8;
        break;
    default: {
        int32_t s = slot[kind - 4];
        nm = s >= 0 ? Ability[s].name : label_get(136);
        break;
    }
    }
    if (parts) {
        Buffer_set_clip(c, &in);
        render_model(c, (in.left + in.right) / 2, (in.top + in.bottom) / 2 + 6, kind == 3 ? chassis : chassis,
                     kind == 1 || kind == 0 ? weapon : 0, kind == 2 ? armor : 0, kind == 3 ? reactor : 0, 0,
                     0.5233f, 0.07f, parts);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
    }
    sw_font(c, &f10, 0xE0);
    sw_text(this, c, std::string(labels[kLabel[kind]]) + " " + nm, in.left + 2, in.top + 2);
    if (!val.empty()) sw_text_right(this, c, val, {in.left, in.bottom - 14, in.right - 2, in.bottom - 2});
}

void DesignWinState::draw_vehicle(Buffer* c) {
    frames++;
    UiRect v = kVehicle;
    Buffer_set_clip(c, &v);
    UNIT& scratch = U(63);
    bool model = U(unit).icon_offset < 0 || !same_as_unit(unit);
    if (model && (Weapon[weapon].icon < 0 || chassis != 0)) {
        render_model(c, (v.left + v.right) / 2, (v.top + v.bottom) / 2, chassis, weapon, armor, reactor, abil, angle,
                     0.14f, 0x1F);
        angle = (float)((double)angle + 0.05);
    } else {
        scratch.chassis_id = (uint8_t)chassis;
        scratch.weapon_id = (uint8_t)weapon;
        scratch.armor_id = (uint8_t)armor;
        scratch.reactor_id = (uint8_t)reactor;
        scratch.ability_flags = abil;
        scratch.icon_offset = U(unit).icon_offset;
        int32_t veh = veh_fake(63, faction);
        veh_draw(c, veh, v.left + ((v.right - 0xBB) - v.left) / 2, v.top + ((v.bottom - 0x8C) - v.top) / 2, 0xE,
                 0x44, 0);
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    // The name.
    std::string nm;
    if (unit < 64 && same_as_unit(unit)) {
        nm = U(unit).name;
    } else if (!std::strlen(name) || !(U(unit).unit_flags & 2)) {
        char buf[0x100];
        name_proto(buf, unit, faction, chassis, weapon, armor, abil, reactor);
        nm = buf;
        U(unit).unit_flags &= ~2;
    } else {
        nm = name;
    }
    sw_font(c, &f16, 0xE1);
    UiRect nr{v.left + 0x2D, v.top + 4, v.right - 0x2D, v.top + 24};
    sw_text_cent(nullptr, c, sw_upper(nm), nr);
    texts.push_back("name " + sw_upper(nm));
    // Units[63]: the preview's fake unit (as the original leaves it).
    scratch.chassis_id = (uint8_t)chassis;
    scratch.weapon_id = (uint8_t)weapon;
    scratch.armor_id = (uint8_t)armor;
    scratch.reactor_id = (uint8_t)reactor;
    scratch.ability_flags = abil;
    scratch.icon_offset = -1;
    char stats[256];
    stats[0] = 0;
    say_stats_3(stats, 63);
    std::string ab;
    for (int32_t i = 0; i < 29; i++)
        if ((abil & (1u << i)) && Ability[i].abbreviation && *Ability[i].abbreviation) {
            if (!ab.empty()) ab += ", ";
            ab += Ability[i].abbreviation;
        }
    sw_font(c, &f12, 0xE0);
    UiRect sr{v.left, v.bottom - 54, v.right, v.bottom - 38};
    sw_text_cent(this, c, std::string(stats) + " " + ab, sr);
    // COST: N (Min Cost when the reactor's minimum is above it).
    int32_t pc = proto_cost(chassis, weapon, armor, (int32_t)abil, reactor);
    std::string cost = std::string(label_get(88)) + ": ";
    if (pc < reactor) {
        cost_shown = reactor * cf;
        cost += "(" + std::string(label_get(122)) + ": " + std::to_string(cost_shown) + ")";
    } else {
        cost_shown = cf * pc;
        cost += std::to_string(cost_shown);
    }
    UiRect cr{v.left, v.bottom - 36, v.right, v.bottom - 20};
    sw_text_cent(this, c, sw_upper(cost), cr);
    // PROTOTYPE COST: a faction design whose weapon, armour or chassis no prototyped (or basic)
    // unit has an equal or better one of; "Prototype Complete" when they all have; "Obsolete"
    // (blinking) for an unchanged obsolete design.
    proto_cost_shown = -1;
    bool need_proto = unit > 63;
    if (need_proto) {
        bool kw = false, ka = false, kc = false;
        for (int32_t k = 0; k < 0x80; k++) {
            int32_t id = k < 64 ? k : faction * 64 + (k & 0x3F);
            const UNIT& x = U(id);
            if (!(x.unit_flags & 1)) continue;
            if (id < 64 ? !has_tech(x.preq_tech, faction) : !(x.unit_flags & 4)) continue;
            if (weapon == x.weapon_id) kw = true;
            if (armor == x.armor_id) ka = true;
            if (chassis == x.chassis_id) kc = true;
            int8_t o = Weapon[weapon].offense_value;
            if (o > 0 && o <= Weapon[x.weapon_id].offense_value) kw = true;
            int8_t d = Armor[armor].defense_value;
            if (d > 0 && d <= Armor[x.armor_id].defense_value) ka = true;
            if (Chassis[chassis].triad == Chassis[x.chassis_id].triad &&
                Chassis[chassis].speed <= Chassis[x.chassis_id].speed)
                kc = true;
        }
        if (kw && ka && kc) need_proto = false;
    }
    UiRect pr{v.left, v.bottom - 18, v.right, v.bottom - 2};
    if (!(U(unit).obsolete_factions & (1u << faction)) || !same_as_unit(unit)) {
        if (need_proto) {
            int32_t base = proto_cost(chassis, weapon, armor, 0, reactor);
            proto_cost_shown = cost_shown + ((base * prototype_factor(unit) + 50) / 100) * cf;
            sw_text_cent(this, c, sw_upper(std::string(label_get(191)) + ": " + std::to_string(proto_cost_shown)), pr);
        } else if (unit > 63) {
            sw_text_cent(this, c, sw_upper(label_get(233)), pr);
        }
    } else if ((frames / 12) % 3 != 0) {
        sw_text_cent(this, c, sw_upper(label_get(912)), pr);
    }
}

// DesignWin_draw_info (0x4346A0): the info pane; also chooses the slot a modified design would
// take (as the original does while drawing).
void DesignWinState::draw_info(Buffer* c) {
    UiRect p = mi_rect(kMiPaneCenter);
    UiRect r{p.left + 4, p.top + 4, p.right - 4, p.bottom - 4};
    Buffer_set_clip(c, &r);
    Buffer_box_sprite(c, &r, box_def(3));
    UiRect in{r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
    Buffer_box_sprite(c, &in, box_def(0));
    sw_font(c, &f16, 0xE0);
    UiRect t{in.left + 3, in.top + 3, in.right - 3, in.top + 24};
    sw_text_cent(this, c, label_get(915), t);
    std::string s;
    if (unit < 64) {
        s = label_get(same_as_unit(unit) ? 913 : 917);
    } else {
        int32_t f = faction;
        bool slot_text = true;
        if ((U(unit).unit_flags & 1) && !(U(unit).obsolete_factions & (1u << f))) {
            slot_text = false;
            if (same_as_unit(unit)) {
                s = label_get(916);
            } else {
                bool found = false, empty = false;
                Faction& fa = Factions[f];
                for (int32_t i = 0; i < 64; i++) {
                    int32_t u = f * 64 + i;
                    if (!(U(u).unit_flags & 1)) {
                        unit = u;
                        empty = true;
                        break;
                    }
                    int8_t group = U(unit).group_id;
                    if ((U(u).obsolete_factions & (1u << f)) && (U(u).group_id == group || !fa.units_active[u]) &&
                        !fa.units_active[u] && (!fa.units_queue[u] || (!found && U(u).group_id == group))) {
                        unit = u;
                        found = true;
                    }
                }
                s = label_get(empty || found ? 917 : 782);
            }
        }
        if (slot_text) {
            s = std::string(label_get(781)) + " (" + label_get(914) + " #" + std::to_string((unit & 0x3F) + 1) + ")";
            s = sw_upper(s);
        }
    }
    sw_font(c, &f12, 0xE0);
    UiRect b{in.left + 3, in.bottom - 24, in.right - 3, in.bottom - 4};
    sw_text_cent(this, c, s, b);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
}

// The component list beside the info pane (DesignWin_draw_flash's labels).
void DesignWinState::draw_flash(Buffer* c) {
    UiRect q = mi_rect(kMiPaneQueue);
    UiRect r{q.left + 3, q.top + 3, q.right - 3, q.bottom - 3};
    Buffer_box_sprite(c, &r, box_def(0));
    sw_font(c, &f10, 0xE0);
    int32_t y = r.top + 4;
    auto line = [&](int32_t label, const std::string& v) {
        sw_text(this, c, std::string(labels[label]) + " " + v, r.left + 5, y);
        y += 15;
    };
    line(8, Weapon[weapon].name);
    line(9, Armor[armor].name);
    line(10, Chassis[chassis].offsv1_name);
    line(11, Reactor[reactor].name);
    line(12, slot[0] >= 0 ? Ability[slot[0]].name : "-");
    line(13, slot[1] >= 0 ? Ability[slot[1]].name : "-");
}

// DesignWin_draw_stack (0x43D510): ten prototypes of the sorted list from stack_pos.
void DesignWinState::draw_stack(Buffer* c) {
    UiRect p = mi_rect(kMiPaneBottom);
    UiRect area{p.left + 0x1A, p.top + 3, p.right - 0x1A, p.bottom - 3};
    spot_init(&spots[1], 0x40);
    proto_sort_2(faction);
    int32_t n = ProtoSortCount;
    int32_t w = ((area.right - area.left) + 2) / 10 - 2;
    int32_t x = area.left, shown = 0;
    for (int32_t i = stack_pos; i < stack_pos + n && shown < 10; i++) {
        int32_t u = ProtoSortList[i % n];
        int32_t owner = u >> 6;
        if (owner == 0) {
            if (!has_tech(U(u).preq_tech, faction) ||
                !(u > 63 || (Weapon[U(u).weapon_id].offense_value >= 0 && u != 15)))
                continue;
        } else if (owner != faction) {
            continue;
        }
        shown++;
        UiRect b{x, area.top, x + w, area.bottom};
        Buffer_box_sprite(c, &b, box_def(u == unit ? 2 : (u == hover ? 1 : 0)));
        spot_add(&spots[1], u, 0, b.left, b.top, b.right - b.left, b.bottom - b.top);
        UiRect in{b.left + 2, b.top + 2, b.right - 2, b.bottom - 2};
        Buffer_set_clip(c, &in);
        sw_font(c, &f10, 0x9F);
        std::string t;
        if (!(U(u).unit_flags & 1)) {
            t = std::string(label_get(110)) + " #" + std::to_string((u & 0x3F) + 1);
            Buffer_wrap_cent_4(c, t.c_str(), in.left, in.top + 10, in.right - in.left);
        } else {
            int32_t veh = veh_fake(u, faction);
            veh_draw(c, veh, b.left + 0xF, area.top + 0xC, -10, 0x1048, 0);
            if (U(u).obsolete_factions & (1u << faction)) t = label_get(912);
            else t = U(u).name;
            Buffer_write_l_4(c, t.c_str(), &in, (int32_t)t.size());
            char stats[256];
            stats[0] = 0;
            say_stats_3(stats, Vehs[veh].unit_id);
            UiRect sr{in.left, in.bottom - 14, in.right, in.bottom};
            Buffer_write_cent_l_7(c, stats, &sr, (int32_t)std::strlen(stats));
        }
        texts.push_back("stack " + std::to_string(u) + " " + t);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        x += w + 2;
    }
}

void DesignWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    if (back_px.size() == (size_t)c->stride * c->height) std::memcpy(c->data, back_px.data(), back_px.size());
    for (BaseButton* b : win_buttons)
        if (b->id == 2 || b->id == 3) b->shown = (GameState & 0x40) != 0;
    for (int32_t k = 0; k < 6; k++) draw_component(c, k);
    draw_vehicle(c);
    sw_restore_iface(this);
    draw_info(&mi);
    draw_flash(&mi);
    draw_stack(&mi);
}

void DesignWinState::log_state() {
    std::fprintf(stderr, "designwin at %d %d size %d %d\n", x, y, canvas.width, canvas.height);
    std::fprintf(stderr,
                 "designstate f%d unit %d chassis %d weapon %d armor %d reactor %d abil %#x slots %d %d name '%s' "
                 "cost %d proto %d stack %d result %d\n",
                 faction, unit, chassis, weapon, armor, reactor, abil, slot[0], slot[1], name, cost_shown,
                 proto_cost_shown, stack_pos, result);
}

}  // namespace

int32_t THISCALL DesignWin_exec(uint8_t*, int32_t f, int32_t unit_id) {
    DesignWinState w;
    w.ScreenWin::name = "design";
    bool tour = unit_id == -2;
    if (unit_id < 0) {
        int32_t found = unit_id;
        int32_t i = 0;
        for (; i < 64; i++) {
            int32_t u = f * 64 + i;
            if (!(U(u).unit_flags & 1)) {
                found = u;
                break;
            }
            if (U(u).obsolete_factions & (1u << (f & 31))) found = u;
        }
        unit_id = found < 0 ? f << 6 : found;
    }
    w.unit = unit_id;
    w.faction = f < 0 ? -f : f;
    enemy_capabilities(w.faction);
    w.cf = cost_factor(w.faction, 1, -1);
    w.slot[0] = w.slot[1] = -1;
    w.setup_veh(unit_id, 1);
    w.center_stack();
    w.f14210 = 0;
    if (!has_tech(Rules.tech_preq_allow_2_spec_abil, w.faction)) {
        int32_t n = 0;
        for (int32_t k = 0; k < 2; k++)
            if (w.slot[k] >= 0 && ++n > 1) w.slot[k] = -1;
    }
    if (!text_open(ScriptFileName, "DESIGNLABELS")) {
        for (int32_t i = 0; i < 16; i++) {
            const char* t = text_get();
            std::snprintf(w.labels[i], sizeof(w.labels[i]), "%s", t ? t : "");
        }
        text_close();
    }
    if (tour) return 0;
    // The window: deswin.pcx above the main interface.
    Buffer art;
    Buffer_ctor(&art);
    int32_t W = 0x24F, H = 600 - 0xD5;
    bool have_art = Buffer_load_pcx(&art, "deswin.pcx", nullptr, 10, 0xEC) == 0;
    if (have_art) W = art.width;
    sw_init(&w, (ScreenWidth - W + 0x20) / 2, ScreenHeight - 0xD5 - H, W, H, true);
    if (have_art) Buffer_copy_4(&art, &w.canvas, 0, 0, 0, 0, std::min(art.width, W), std::min(art.height, H));
    w.back_px.assign(w.canvas.data, w.canvas.data + (size_t)w.canvas.stride * w.canvas.height);
    for (int32_t i = 0; i < 6; i++) {
        const UiRect& r = kBoxes[i];
        spot_add(&w.spots[0], i, i, r.left, r.top, r.right - r.left, r.bottom - r.top);
    }
    Font_ctor(&w.f10);
    Font_ctor(&w.f12);
    Font_ctor(&w.f16);
    Font_init(&w.f10, "Arial", 10, 0);
    Font_init(&w.f12, "Arial", 0xC, 1);
    Font_init(&w.f16, "Arial", 0x10, 1);
    // The window's buttons: DONE, RENAME, OBSOLETE, UPGRADE, CANCEL; above them APPLY, (PROTO),
    // RETIRE, (RETIRE ALL).
    Font* bf = find_font(0xE, 1);
    int32_t l0 = 0x1B, step = 0, top = H - 0x28, bottom = H - 0x14;
    int32_t r0 = ((W - l0) - 0x38) / 5 - 3 + l0;
    step = r0 - l0 + 3;
    const int32_t ids[5] = {4, 5, 6, 7, 8};
    const int32_t labs[5] = {270, 125, 756, 757, 102};
    for (int32_t i = 0; i < 5; i++)
        w.win_buttons.push_back(
            sw_button(&w, sw_upper(label_get(labs[i])).c_str(), ids[i], l0 + i * step, top, r0 - l0, bottom - top, 0, bf));
    int32_t up = (top - bottom) - 6;
    w.win_buttons.push_back(sw_button(&w, sw_upper(label_get(918)).c_str(), 1, l0, top + up, r0 - l0, bottom - top, 0, bf));
    w.win_buttons.push_back(
        sw_button(&w, sw_upper(label_get(1033)).c_str(), 2, l0 + step, top + up, r0 - l0, bottom - top, 0, bf));
    w.win_buttons.push_back(
        sw_button(&w, sw_upper(label_get(925)).c_str(), 0, l0 + 4 * step, top + up, r0 - l0, bottom - top, 0, bf));
    w.win_buttons.push_back(
        sw_button(&w, sw_upper(label_get(1037)).c_str(), 3, l0 + 3 * step, top + up, r0 - l0, bottom - top, 0, bf));
    // The presets (DESIGNAUTO) in the right pane, two columns of seven; the list's arrows.
    UiRect pr = mi_rect(kMiPaneRight);
    int32_t px = pr.left + 7, py = pr.top + 7 + 0x18;
    int32_t pw = ((pr.right - 7 - px) + 3) / 2 - 3;
    if (!text_open(ScriptFileName, "DESIGNAUTO")) {
        int32_t id = 9;
        for (int32_t col = 0; col < 2; col++)
            for (int32_t row = 0; row < 7; row++) {
                const char* t = text_get();
                std::string label = sw_upper(t ? t : "");
                sw_button(&w, label.c_str(), id++, px + col * (pw + 3), py + row * 0x17, pw, 0x15, 1, find_font(0xA, 1));
            }
        text_close();
    }
    UiRect pb = mi_rect(kMiPaneBottom);
    sw_button(&w, "<", 0x17, pb.left + 3, pb.top + 3, 0x14, pb.bottom - pb.top - 6, 1, bf);
    sw_button(&w, ">", 0x18, pb.right - 0x17, pb.top + 3, 0x14, pb.bottom - pb.top - 6, 1, bf);
    g_dw = &w;
    sw_run(&w);
    g_dw = nullptr;
    sw_clear_buttons(&w);
    if (w.scene_ready) Scene_clear(&w.scene, 0);
    return w.result;
}

// PROTOSTATE (test aid): the viewer's prototypes ("proto U name 'N' chassis C weapon W armor A
// reactor R abil X flags F obsolete O cost C").
void designwin_log_protos() {
    int32_t f = CurrentPlayerFaction;
    for (int32_t i = 0; i < 64; i++) {
        int32_t u = f * 64 + i;
        const UNIT& x = Units[u];
        if (!(x.unit_flags & 1)) continue;
        std::fprintf(stderr, "proto %d name '%s' chassis %d weapon %d armor %d reactor %d abil %#x flags %#x obsolete %d cost %d\n",
                     u, x.name, x.chassis_id, x.weapon_id, x.armor_id, x.reactor_id, x.ability_flags, x.unit_flags,
                     (x.obsolete_factions >> f) & 1, x.cost);
    }
}
