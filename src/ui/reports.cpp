// The F-key reports (ReportWin 0x885F38 with its main interface part ReportIf; ui module
// 0x496960..0x4AD870): F2 labs (report_labs), F3 energy budget (report_energy), F4 base
// operations (report_ops), F5 secret projects (report_projects), F6 satellites (report_sat), F7
// security nexus (report_security), F8 score (report_score), and the intelligence file of
// another faction (report_intel: probe teams, the commlink). Each report_* sets the report kind
// (+8: 2..8, 0 intel) and opens the window, or switches the open one (ReportIf_report).
//
// Faithful to the original: the keys (ReportWin_on_key_click: the report's own F-key, Enter,
// Esc, letters and digits close; F2..F8 switch; Left / Right cycle; PgUp / PgDn / Home / End /
// Up / Down scroll), the report buttons of the main interface (ids 2..8), what each report
// lists and computes (the order of bases, prototypes and projects; the values: base_compute,
// energy_compute, tech_rate, compute_score and the faction fields) and the state changes:
//   * base operations: the eight sort keys of ReportIf_on_iface_selected (minerals, nutrients,
//     energy, labs, psych, population, garrison, infrastructure) with ReportIf_merge_sort's
//     order (descending, stable), the three views (production, citizens, garrison), a click on a
//     row selects the base (shift / ctrl extend), a double click opens it (BaseWin_zoom), a click
//     on the base's picture cycles its governor (ReportWin_on_left_click: BaseWin_gov_activate);
//   * security nexus: the unit type filter (land, naval, air, non-combat, all);
//   * labs: a click on the research line puts it at the tree's centre and chooses the research
//     (Console_set_tech; blind research: Console_set_ai); the technology tree (ReportWin_draw_labs,
//     labs.cpp: its buttons move the centre, the mouse highlights them); the main interface's
//     TECHNOLOGY / BASES list (a technology moves the tree, a base opens), the summary line and
//     the progress per category (ReportIf_draw_labs).
// From the asm too: the report buttons' help bubbles (a right click: BaseButton_set_bubble_text),
// the base locator on the world map pane (WorldWin_show_all in base operations and secret
// projects, ReportIf_bl_anim's dot0..3.pcx after a single base is selected or a project
// clicked), the satellites' ORBITAL ATTACK VIEW (satellite.flc and the orbiting satellites,
// ReportWin_sat_anim every 4000 ms), the production pictures of base operations.
// The base operations' citizens view, the satellites' table, the main interface's satellite
// panes and the orbital attack (sat_attack, ReportWin_start_attack, sat_edit) from the asm too.
// Approximated (not compared with the original's pixels): the layout inside the window of the
// other reports (rows and columns at the original's rectangles, text instead of some pictures:
// the project pictures of the projects report), the list boxes' look, the faction logo of the
// main interface's centre pane. The faction buttons of ReportIf (viewing other factions'
// reports) are not drawn.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/action.h"
#include "engine/save.h"
#include "engine/base.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/log.h"
#include "engine/net.h"
#include "engine/zoc.h"
#include "platform/ui_platform.h"
#include "ui/console_frame.h"
#include "ui/flic.h"
#include <cmath>
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/help.h"
#include "ui/labs.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/screenwin.h"
#include "ui/scroll.h"
#include "ui/slide.h"
#include "ui/sprites.h"
#include "ui/techwin.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

namespace {

// Report kinds (ReportWin + 8).
enum { kIntel = 0, kLabs = 2, kEnergy = 3, kOps = 4, kProjects = 5, kSat = 6, kSecurity = 7, kScore = 8 };

// Persistent state of the original's globals.
int32_t g_ops_sort = 0;        // DAT_0087BE80: the sort key (0 none, 1..8)
int32_t g_ops_ids[512];        // ReportIf + 0x1E254: base ids in display order
int32_t g_ops_keys[512];
bool g_ops_ids_ready = false;
int32_t g_ops_view = 0;        // ReportWin + 0x1F230 (0x8B3..0x8B5): production, citizens, garrison
int32_t g_sec_filter = 4;      // DAT_008A417C: land, naval, air, non-combat, all
int32_t g_intel_target = 0;    // DAT_008A4164
int32_t g_energy_subject = -1; // ReportWin + 0x59AC (DAT_0087BE24): the energy budget's base / facility
int32_t g_energy_base = 0;     // ReportWin + 0x59B0: 1 a base, 0 a facility

struct ReportScreen : ScreenWin {
    int32_t faction = 0;         // ReportIf + 0x1E228 (DAT_008A4160)
    int32_t scroll = 0;          // ReportWin + 0xC0FC: first row shown
    int32_t rows_shown = 0;      // rows that fit
    int32_t rows_total = 0;
    uint32_t selected[16]{};     // ReportWin + 0x59B4: selected bases (bits)
    int32_t last_click = 0;      // ReportWin + 0x59AC
    Font *f_title = nullptr, *f_text = nullptr, *f_bold = nullptr, *f_small = nullptr;
    Sprite buttons[7][3];        // report.pcx: the report buttons, three states
    BaseButton* report_btn[7]{};
    BaseButton* ops_btn[3]{};    // the views of base operations (ids 0x8B3..0x8B5)
    BaseButton* sat_btn[2]{};    // the satellites' views: the table (0xD3C), the animation (0xD3D)
    BaseButton* sat_target[8]{}; // ORBITAL ATTACK VIEW: the target factions (ReportIf +0x13778, group 0x6A)
    BaseButton* sat_type[5]{};   // the satellite kinds (+0x18720, ids 0x41.., group 0xCE)
    BaseButton* sat_attack_btn = nullptr;  // ATTACK (0xD3E, ReportWin)
    std::vector<BaseButton*> ops_iface;  // GOVERNOR / BEST BASES tabs, the governor buttons
    bool ops_best = false;       // the BEST BASES tab (0xD0B) is shown
    std::vector<BaseButton*> labs_iface;  // TECHNOLOGY / BASES tabs (0x92, 0x93)
    int32_t labs_tab = 1;        // ReportIf + 0x1E1D8: 1 the technologies, 0 the bases
    struct Row {
        std::string text;
        int32_t id;
    };
    std::vector<Row> labs_list;  // the left pane's list (ListBox items)
    int32_t labs_top = 0;
    int32_t energy_top = 0, energy_itop[2] = {0, 0};
    int32_t list_keep[8] = {-2, -2, -2, -2, -2, -2, -2, -2};  // the selection last kept in view
    ScrollBar list_bars[8];
    ScrollBar page_bar;          // the window's scroll bar (ReportWin's Scroll 0x882584)
    bool rail_shown = false;     // DAT_00882570: scroll_show ran
    bool drawn = false;          // drawn since the window opened
    int32_t* list_top(int32_t type) {
        switch (type) {
        case 4: return &labs_top;
        case 5: return &energy_top;
        case 6: return &energy_itop[0];
        case 7: return &energy_itop[1];
        default: return nullptr;
        }
    }
    int32_t line_h12() const {
        Font* f = find_font(12, 0);  // ReportWin + 0xE268 / ReportIf + 0x1C
        return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0;
    }
    void list_draw(Buffer* m, int32_t parent, const UiRect& lr, const std::vector<Row>& rows, int32_t sel_id,
                   int32_t sel_pos, int32_t& top, int32_t type, const char* tag, int32_t (*color3)(int32_t id));
    std::vector<std::string> state;  // what log_state prints

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;
    void on_move(int32_t x, int32_t y) override;

    // geometry (ReportWin_init at 1024 x 768)
    UiRect inner() const { return {0x1A, 3, 0x28A + 0x14, 0x161 + 200 - 0x35}; }
    int32_t line_h() const { return f_text ? f_text->height : 14; }
    UiRect title_rect() const {
        UiRect r = inner();
        return {r.left + 3, r.top + 3, r.right - 3, r.top + 9 + line_h()};
    }
    void title(const std::string& t);
    void draw_ops();
    void draw_labs();
    void draw_energy();
    void draw_projects();
    void draw_sat();
    void draw_security();
    void draw_score();
    void draw_intel();
    void iface_ops();
    void iface_labs();
    void iface_energy();
    void iface_security();
    void iface_sat();
    void iface_common(const std::string& head);
};

ReportScreen g_rs;

int32_t& kind() { return ReportWinKind; }

std::string up(const std::string& s) { return sw_upper(s); }
std::string num(int32_t v) { return std::to_string(v); }
std::string adj(int32_t f) { return MFactions[f].adj_name_faction; }

void ReportScreen::title(const std::string& t) {
    UiRect r = inner();
    Buffer_box_sprite(&canvas, &r, box_def(0));
    UiRect tr = title_rect();
    Buffer_box_sprite(&canvas, &tr, box_def(1));
    sw_font(&canvas, f_bold, 0x99);
    Buffer_set_text_color3(&canvas, 0xE1, -1, 1, 1);  // ReportIf_reset: "[...]" in 0xE1
    UiRect tt{tr.left, tr.top - 1, tr.right, tr.bottom - 1};
    sw_text_cent(this, &canvas, t, tt);
}

// ---- base operations ----

// ReportIf_on_iface_selected (kind 4): the sort keys of every base, then ReportIf_merge_sort.
int32_t ops_key(int32_t base_id, int32_t mode) {
    set_base(base_id);
    base_compute(0);
    const BASE& b = Bases[base_id];
    switch (mode) {
    case 1: return b.mineral_surplus;
    case 2: return b.nutrient_surplus;
    case 3: return b.economy_total;
    case 4: return b.labs_total;
    case 5: return b.psych_total;
    case 6: return b.pop_size;
    case 7: {
        int32_t n = 0, v = veh_at(b.x, b.y);
        if (v >= 0) {
            while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
            for (; v >= 0; v = Vehs[v].next_veh_id_stack) n++;
        }
        return n;
    }
    case 8: {
        int32_t n = 0;
        for (int32_t i = 0; i < 0x46; i++)
            if (b.facilities_built[i >> 3] & (1 << (i & 7))) n++;
        for (int32_t p = 0; p < 64; p++)
            if (SecretProjects[p] == base_id) n += 5;
        return n;
    }
    default: return 0;
    }
}

// ReportIf_merge_sort: descending by key, equal keys keep their order.
void merge_sort(int32_t lo, int32_t hi) {
    int32_t last = BaseCount - 1;
    lo = std::max(0, std::min(lo, std::max(last, 0)));
    hi = std::max(0, std::min(hi, std::max(last, 0)));
    if (lo == hi) return;
    if (hi < lo) std::swap(lo, hi);
    int32_t mid = (lo + hi) / 2;
    merge_sort(lo, mid);
    merge_sort(mid + 1, hi);
    std::vector<int32_t> ids, keys;
    int32_t a = lo, b = mid + 1;
    for (int32_t k = lo; k <= hi; k++) {
        bool take_b = b <= hi && (a > mid || g_ops_keys[a] < g_ops_keys[b]);
        int32_t i = take_b ? b++ : a++;
        ids.push_back(g_ops_ids[i]);
        keys.push_back(g_ops_keys[i]);
    }
    for (int32_t k = lo; k <= hi; k++) {
        g_ops_ids[k] = ids[k - lo];
        g_ops_keys[k] = keys[k - lo];
    }
}

void ops_reset_order() {
    for (int32_t i = 0; i < 512; i++) {
        g_ops_ids[i] = i;
        g_ops_keys[i] = 0;
    }
    g_ops_ids_ready = true;
}

void ops_sort(int32_t mode) {
    ops_reset_order();
    for (int32_t i = 0; i < BaseCount; i++) g_ops_keys[i] = ops_key(i, mode);
    merge_sort(0, BaseCount - 1);
    g_ops_sort = mode;
}

const uint32_t kSortLabels[9] = {0, 0xA0, 0xA4, 0xE4, 0xAAC, 0xAB0, 0xAB4, 0xFC, 0xAB8};

std::string governor_text(int32_t base_id) {
    const BASE& b = Bases[base_id];
    if (is_objective(base_id) && !(GameRules & 0x20000000)) return up(sw_label(0xE08));
    if (!(b.governor_flags & 0x80000000)) return sw_label(0xBFC);
    for (int32_t i = 0; i < 4; i++)
        if (b.governor_flags & (0x1000000 << i)) return sw_label(0x824 + i * 4);
    return sw_label(0xBFC);
}

// FUN_00493B70: the mandate whose colour a facility is drawn in.
int32_t report_facility_mandate(int32_t fac) {
    switch (fac) {
    case 3: case 0x1F: case 0x20: return 0;
    case 4: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x44: return 3;
    case 8: case 0xF: case 0x10: case 0x11: return 1;
    default: return 2;
    }
}

// The UI generator of ReportWin_draw_ops (Random_reseed(timeGetTime()), Random_get), as
// BaseWin_draw_pop's.
uint32_t g_ui_seed = 0;
int32_t g_ui_rand(int32_t lo, int32_t hi) {
    g_ui_seed = g_ui_seed * 0x19660D + 0x3C6EF35F;
    return (int32_t)(((uint32_t)(hi - lo) * (g_ui_seed & 0xFFFF)) >> 16) + lo;
}

void ReportScreen::draw_ops() {
    g_ui_seed = platform::ticks_ms();
    Buffer* c = &canvas;
    std::string t = "[" + up(adj(faction)) + " " + up(sw_label(0xACC));
    if (g_ops_sort) t += " - " + up(sw_label(kSortLabels[g_ops_sort]));
    title(t + "]");
    if (!g_ops_ids_ready) ops_reset_order();
    UiRect in = inner(), tr = title_rect();
    // The list below the view buttons (ReportWin_draw_ops: rows from inner top + 0x22 + the line
    // height + 5, 0x28 high every 0x2A).
    int32_t list_top = in.top + 0x22 + line_h(), row_h = 0x28 + 2, top = list_top + 5;
    UiRect lb{in.left, list_top, in.right, in.bottom};
    Buffer_box_sprite(c, &lb, box_def(0));
    rows_shown = (in.bottom - (list_top + 2) - 6) / row_h;
    (void)tr;
    std::vector<int32_t> list;
    for (int32_t i = 0; i < BaseCount; i++)
        if (Bases[g_ops_ids[i]].faction_id == faction) list.push_back(g_ops_ids[i]);
    rows_total = (int32_t)list.size();
    static const char* kViews[3] = {"production", "citizens", "garrison"};
    state.push_back(std::string("ops sort ") + num(g_ops_sort) + " view " + kViews[g_ops_view] + " bases " + num(rows_total));
    for (int32_t r = 0; r < rows_total; r++) {
        int32_t base_id = list[r];
        set_base(base_id);
        base_compute(0);
        const BASE& b = Bases[base_id];
        std::string line = "ops " + num(r) + " base " + num(base_id) + " " + b.name;
        if (r < scroll || r >= scroll + rows_shown) {
            state.push_back(line);
            continue;
        }
        int32_t y = top + (r - scroll) * row_h;
        UiRect row{in.left + 3, y, in.right - 3, y + 0x28};
        UiRect pic{row.left, row.top, row.left + 0x2B, row.bottom};
        bool sel = selected[base_id >> 5] & (1u << (base_id & 31));
        if (sel) {
            Buffer_box_sprite(c, &row, box_def(2));
            Buffer_box_sprite(c, &row, box_def(5));
        } else {
            Buffer_box_sprite(c, &row, box_def(1));
        }
        Buffer_box_sprite(c, &pic, box_def(1));
        spot_add(&spots[0], base_id + 0x400, 0, pic.right, row.top, row.right - pic.right, row.bottom - row.top);
        spot_add(&spots[0], base_id + 0xC00, 0, pic.left, pic.top, pic.right - pic.left, pic.bottom - pic.top);
        UiRect clip{pic.left + 1, pic.top + 1, pic.right - 1, pic.bottom - 1};
        Buffer_set_clip(c, &clip);
        base_draw(c, base_id, pic.left - 8, pic.top - 6, -6, 0x40);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        // The name: red with drone riots coming (drones > talents), "(!)" for a golden age.
        std::string name = b.name;
        int32_t color = 0xE1;
        if (b.talent_total < b.drone_total && (SecretProjects[0x23] < 0 || Bases[SecretProjects[0x23]].faction_id != faction)) {
            color = 0xF9;
        } else if (b.drone_total == 0 && b.pop_size > 2 && (b.pop_size + 1) / 2 <= b.talent_total) {
            name += "(!)";
            color = 0xFF;
        }
        int32_t x = pic.right + 5;
        sw_font(c, f_bold, color);
        sw_text(this, c, up(name), x, row.top + 5);
        std::string gov = governor_text(base_id);
        sw_font(c, f_small, 0xE1);
        sw_text(this, c, gov, x + 0x14, row.bottom - line_h() - 1);
        line += " gov " + gov;
        int32_t col = x + 0xC0;
        if (g_ops_view == 0) {
            int32_t item = b.queue_items[0];
            std::string what = item < 0 ? Facility[-item].name : Units[item].name;
            int32_t cost = item < 0 ? Facility[-item].cost : veh_cost(item, base_id, nullptr);
            cost *= cost_factor(b.faction_id, 1, -1);
            int32_t have = -item == 0x45 ? 0 : b.minerals_accumulated;
            std::string turns;
            if (b.mineral_surplus < 1) {
                turns = sw_label(0x10C);
            } else {
                int32_t n = cost - have <= 0 ? 0 : (b.mineral_surplus - 1 + cost - have) / std::min(b.mineral_surplus, 99999);
                turns = sw_label(0xC4) + ":  " + num(std::min(n, 999999));
            }
            // The item's picture (ReportWin_draw_ops 0x8B3): a facility's or project's in its
            // mandate's colour scaled to the row (Sprite_draw_mono_2 with the row's height over
            // the pictures' size), a unit's model (veh_fake, zoom -8, flags 0x4C); the name in the
            // mandate's colour at 0x3C.
            int32_t mandate;
            if (item < 0) {
                int32_t fac = -item;
                Sprite* sp = basewin_facility_picture(fac);
                int32_t od = BlitDestScale, os = BlitSourceScale;
                BlitDestScale = (row.bottom - row.top) - 2;
                BlitSourceScale = basewin_facility_picture(fac < 0x46 ? 0 : 0x46)->full_height;
                mandate = fac < 0x46 ? report_facility_mandate(fac) : 1;
                Buffer_set_clip(c, &row);
                Sprite_draw_mono_2(sp, c, sp->key & 0xFF, col + 8, row.top + 4, mandate_color(mandate));
                Buffer_set_clip(c, &all);
                BlitDestScale = od;
                BlitSourceScale = os;
            } else {
                const uint8_t* u = reinterpret_cast<const uint8_t*>(&Units[item]);
                int32_t plan = (int8_t)u[0x2A];
                mandate = (item > 0x3F && !(u[0x30] & 4)) ? 1
                          : (plan == 3 || plan == 7 || plan == 8) ? 0
                          : (plan == 9 || plan == 10) ? 2
                          : plan == 11 ? 1 : 3;  // FUN_00493AF0
                int32_t v = veh_fake(item, (uint8_t)b.faction_id);
                Buffer_set_clip(c, &row);
                veh_draw(c, v, col, row.top + 4, -8, 0x4C, 0);
                Buffer_set_clip(c, &all);
            }
            sw_font(c, f_text, mandate_color(mandate));
            sw_text(this, c, what, col + 0x3C, row.top + 5);
            sw_font(c, f_text, 0xF2);
            UiRect rr{col, row.bottom - line_h() - 1, row.right - 4, row.bottom - 1};
            std::string amount = "(" + num(have) + "/" + num(cost) + ")    " + turns;
            sw_text_right(this, c, amount, rr);
            line += " build " + what + " " + amount;
        } else if (g_ops_view == 1) {
            // ReportWin_draw_ops' citizens (0x8B4): the small faces from the row's right, right to
            // left: the specialists (the last first), the drones (superdrones first), the workers,
            // the talents; a step of (row width - 0xE2) / (pop - 1), 1..0x22; the faces' looks from
            // the UI generator (Random_reseed(timeGetTime())). Aliens: ALIENCIT.PCX's.
            int32_t workers = b.pop_size - b.talent_total - b.specialist_total - std::max(b.drone_total, 0);
            const int32_t left = in.left + 0x2D, right = row.right;
            int32_t step = b.pop_size < 2 ? 0x22 : std::max(1, std::min(0x22, ((right - left) - 0xE2) / (b.pop_size - 1)));
            int32_t x = step - 0x20 + right;
            const int32_t fy = row.top + 0x28 - 0x2A / 2 - 0x12;
            const bool al = ExpansionEnabled && (MFactions[faction].rule_flags & 0x80);
            auto put = [&](uint32_t addr) {
                x -= step;
                Sprite* sp = ArtSprite(addr);
                sprite_copy1(sp, c, sp->key, x, fy, 1, 1);
            };
            for (int32_t i = 0; i < b.specialist_total; i++) {
                int32_t idx = b.specialist_total - i - 1;
                int32_t type = idx < 0x10 ? (b.specialist_types[idx >> 3] >> ((idx & 7) << 2)) & 0xF : best_specialist();
                put((al ? 0x75B710u : 0x779B78u) + (uint32_t)type * 0x2C);
            }
            for (int32_t i = 0; i < b.drone_total; i++) {
                int32_t mood = (i < b.superdrone_total) + 2;
                put(al ? 0x75B180u + (uint32_t)mood * 0x2C : 0x776420u + (uint32_t)(mood + g_ui_rand(0, 2) * 4) * 0x2C);
            }
            for (int32_t i = 0; i < workers; i++) put(al ? 0x75B1ACu : 0x77644Cu + (uint32_t)g_ui_rand(0, 2) * 0xB0);
            for (int32_t i = 0; i < b.talent_total; i++) put(al ? 0x75B180u : 0x776420u + (uint32_t)g_ui_rand(0, 2) * 0xB0);
            std::string s = num(b.talent_total) + " talents, " + num(std::max(workers, 0)) + " workers, " + num(b.drone_total) +
                            " drones, " + num(b.specialist_total) + " specialists";
            line += " citizens " + s;
        } else {
            int32_t v = veh_at(b.x, b.y);
            int32_t n = v >= 0 ? stack_check(v, 1, -1, -1, -1) : 0;
            int32_t step = n < 2 ? 0x28 : std::max(1, std::min(0x28, ((row.right - row.left) - 0xE8) / (n - 1)));
            int32_t px = row.right - 0x2D + step;
            if (v >= 0) {
                while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
                std::string units;
                for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
                    px -= step;
                    Buffer_set_clip(c, &row);
                    veh_draw(c, v, px, row.top, -8, 8, 0);
                    Buffer_set_clip(c, &all);
                    spot_add(&spots[0], base_id + 0x800, v, px, row.top, step, row_h);
                    units += " " + num(v);
                }
                line += " units" + units;
            }
        }
        state.push_back(line);
    }
}

void ReportScreen::iface_ops() {
    // ReportIf_draw_ops: the GOVERNOR tab (its buttons, created at the opening) or BEST BASES: the
    // best base per sort key (the list box of ReportIf_draw_ops); a click on a line sorts.
    if (!ops_best) return;
    Buffer* m = &mi;
    UiRect p = mi_rect(kMiPaneQueue);
    int32_t y = p.top + 0x23, lh = f_small->height + 2;
    UiRect r{p.left + 9, y, p.right - 9, p.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(0));
    y += 3;
    for (int32_t mode = 1; mode <= 8; mode++) {
        int32_t best = -1, best_v = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id != faction) continue;
            int32_t v = ops_key(i, mode);
            if (best < 0 || best_v < v) {
                best = i;
                best_v = v;
            }
        }
        std::string s = up(sw_label(kSortLabels[mode])) + ": ";
        if (best >= 0) s += std::string(Bases[best].name) + " (" + num(best_v) + ")";
        sw_font(m, f_small, mode == g_ops_sort ? 0xFF : 0x99);
        UiRect clip{r.left, y, r.right, y + lh};
        Buffer_set_clip(m, &clip);
        sw_text(this, m, s, r.left + 4, y);
        UiRect all{0, 0, m->width, m->height};
        Buffer_set_clip(m, &all);
        spot_add(&spots[1], mode, 2, r.left, y, r.right - r.left, lh);
        y += lh;
    }
}

// ReportIf_on_iface_button_clicked (base operations, the viewer's own report): the governor of
// the selected bases.
void ops_governor(ReportScreen* s, int32_t id) {
    if (id == 0xCEF) {  // SELECT ALL
        for (int32_t i = 0; i < 0x200; i++)
            if (i < BaseCount && Bases[i].faction_id == CurrentPlayerFaction && Bases[i].faction_id == s->faction)
                s->selected[i >> 5] |= 1u << (i & 31);
        return;
    }
    if (id >= 0xCF0 && id <= 0xCF3) {
        log_say_2("Global EDBC base set", 0, 0, 0);
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id != CurrentPlayerFaction || !(s->selected[i >> 5] & (1u << (i & 31)))) continue;
            if (bw_lock_base(i)) continue;
            BaseWin_soft_zoom(&BaseWin, i);
            BASE& b = Bases[i];
            b.governor_flags = (int32_t)(((uint32_t)b.governor_flags | 0x80000000) & 0xF0FFFFFF);
            b.governor_flags |= (int32_t)(0x1000000u << (id - 0xCF0));
            BaseWin_gov_activate(&BaseWin);
            bw_unlock_base(i);
        }
        return;
    }
    if (id == 0xCF4) {  // ADVANCED SETTINGS: the governor dialog of the first selected base, then
                        // its settings (Faction.base_governor_adv) for every selected base
        log_say_2("Global governor advanced set", 0, 0, 0);
        bool asked = false;
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id != CurrentPlayerFaction || !(s->selected[i >> 5] & (1u << (i & 31)))) continue;
            if (!asked) {
                BaseWin_soft_zoom(&BaseWin, i);
                if (BaseWin_governor(&BaseWin, 1)) break;
                asked = true;
            }
            if (bw_lock_base(i)) continue;
            BaseWin_soft_zoom(&BaseWin, i);
            Bases[i].governor_flags = Factions[s->faction].base_governor_adv;
            BaseWin_gov_activate(&BaseWin);
            draw_radius(Bases[i].x, Bases[i].y, 2, 2);
            bw_unlock_base(i);
        }
        return;
    }
    if (id == 0xCF5) {  // TURN GOVERNOR OFF
        log_say_2("Global governor off", 0, 0, 0);
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id != CurrentPlayerFaction || !(s->selected[i >> 5] & (1u << (i & 31)))) continue;
            if (bw_lock_base(i)) continue;
            Bases[i].governor_flags &= 0x7FFFFFFF;
            draw_radius(Bases[i].x, Bases[i].y, 2, 2);
            bw_unlock_base(i);
        }
    }
}

// ---- labs ----

// ReportWin_draw_labs (labs.cpp): the research, the progress bar and the technology tree.
void ReportScreen::draw_labs() { labs_draw(this, &canvas, false, faction, inner(), &state); }

// ReportIf_draw_labs: the TECHNOLOGY / BASES list (left pane), the summary line (centre), the
// progress per category (right pane).
void ReportScreen::iface_labs() {
    Buffer* m = &mi;
    Faction& f = Factions[faction];
    Font* f10 = find_font(10, 0);
    Font* f12 = find_font(12, 0);
    Font* f14 = find_font(14, 0);
    int32_t lh = f12->field_0 < 0 ? f12->height : f12->ascent_above + f12->field_0;  // ReportIf + 0x1C
    UiRect all{0, 0, m->width, m->height};
    // The left pane: the tabs' bar, the list's box and the list (Dialog_init_3, font 12, 0x99;
    // labs_write: the category part in the technology's colour).
    UiRect p = mi_rect(kMiPaneQueue);
    int32_t x = p.left + 6, y = p.top + 6, w = (p.right - p.left) - 12, h = (p.bottom - p.top) - 12;
    UiRect r{x, y, x + w, y + 0x1A};
    Buffer_box_sprite(m, &r, box_def(3));
    r = {x, r.bottom + 2, x + w, p.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(0));
    y += 0x20;
    h -= 0x20;
    UiRect lr{x + 3, y + 3, x + w - 3, y + h - 3};
    labs_list.clear();
    if (!labs_tab) {
        for (int32_t i = 0; i < BaseCount; i++) {
            const BASE& b = Bases[i];
            if (b.faction_id != faction) continue;
            StrBuffer[0] = 0;
            say_base(StrBuffer, i);
            std::string t = std::string(StrBuffer) + "|+" + num(b.labs_total) + " (";
            int32_t rs = std::max(-5, std::min(5, f.SE_research_pending));
            int32_t v = (rs + 10) * b.labs_total * 10;
            t += num(v / 100);
            if (v % 100) t += "." + num((v % 100) / 10);
            t += ")" + std::string(label_get(0x5EC / 4));
            labs_list.push_back({t, i + 0x400});
        }
    } else {
        int32_t key[89];
        for (int32_t t = 0; t < 89; t++) key[t] = has_tech(t, faction) ? tech_category(t) + tech_recurse(t, 0) * 4 : 0;
        for (int32_t n = 0; n < 89; n++) {
            int32_t best = 89;
            for (int32_t t = 0; t < 89; t++)
                if (key[t] && (best == 89 || key[t] < key[best])) best = t;
            if (best >= 89) break;
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, best, 0);
            std::string t = std::string(StrBuffer) + "|[" + label_get(0x9D4 / 4 + (key[best] & 3)) + " " +
                            num(key[best] >> 2) + "]";
            labs_list.push_back({t, best});
            key[best] = 0;
        }
    }
    std::vector<Row> rows;
    for (const Row& r0 : labs_list) rows.push_back(r0);
    list_draw(m, 1, lr, rows, labs_tab ? labs_tree_tech() : -1, -1, labs_top, 4, "labs list",
              labs_tab ? +[](int32_t id) { return mandate_color(tech_category(id)); } : nullptr);
    // The centre pane: the title and the summary.
    UiRect c = mi_rect(kMiPaneCenter);
    bool wide = ScreenWidth != 800;  // ReportIf + 0x1E230
    int32_t cx = c.left + (wide ? 0x4C : 0), cw = (c.right - (wide ? 0x4C : 0)) - c.left, cy = c.top;
    Buffer_set_font(m, f14, find_font(14, 2), find_font(14, 1), nullptr);
    Buffer_set_text_color(m, 0xE1, -1, 1, 1);
    std::string t = up(MFactions[faction].adj_name_faction) + " " + label_get(0xAC4 / 4);
    if (wide) t += std::string(", ") + " " + label_get(0xB68 / 4) + " " + num(game_year(CurrentTurn));
    Buffer_write_cent_l_5(m, t.c_str(), cx, cy + 10, cw, (int32_t)t.size());
    texts.push_back(t);
    Buffer_set_font(m, f10, f10, f10, nullptr);
    Buffer_set_text_color(m, 0x99, -1, 1, 1);
    cy += lh * 2;
    int32_t cost = tech_rate(faction);
    std::string s = std::string(label_get(0x5E0 / 4)) + ": " + num(cost) + "    " + label_get(0x5E4 / 4) + ": " +
                    num(f.tech_accumulated);
    if (!wide) {
        Buffer_write_cent_l_5(m, s.c_str(), cx, cy, cw, (int32_t)s.size());
        texts.push_back(s);
        cy += lh;
        s.clear();
    } else {
        s += "    ";
    }
    energy_compute(faction, 0);
    std::string per = num(FactionCurrentLabs / 100);
    if (FactionCurrentLabs % 100) per += "." + num((FactionCurrentLabs % 100) / 10);
    s += std::string(label_get(0x5E8 / 4)) + ": " + per;
    Buffer_write_cent_l_5(m, s.c_str(), cx, cy, cw, (int32_t)s.size());
    texts.push_back(s);
    state.push_back("labs cost " + num(cost) + " accumulated " + num(f.tech_accumulated) + " per turn " + per);
    // The right pane: the title box, then per category its bar (known / defined technologies) and
    // its name with the percentage.
    UiRect q = mi_rect(kMiPaneRight);
    x = q.left + 6;
    y = q.top + 6;
    w = (q.right - q.left) - 12;
    h = (q.bottom - q.top) - 12;
    Buffer_set_font(m, f12, find_font(12, 1), find_font(14, 1), nullptr);
    r = {x, y, x + w, y + lh + 6};
    Buffer_box_sprite(m, &r, box_def(0));
    r = {x, r.bottom + 2, x + w, q.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(0));
    std::string head = "[";
    if (f.tech_count_transcendent == 0) {
        Buffer_set_text_color3(m, 0xE1, -1, 1, 1);
        head += label_get(0xB00 / 4);
    } else {
        Buffer_set_text_color3(m, mandate_color(1), -1, 1, 1);
        StrBuffer[0] = 0;
        say_tech_2(StrBuffer, f.tech_research_id, 0);
        head += up(StrBuffer) + " " + num(f.tech_count_transcendent);
    }
    head += "]";
    h -= 4;
    y += 4;
    Buffer_write_cent_l_5(m, head.c_str(), x, y, w, (int32_t)head.size());
    texts.push_back(head);
    h -= lh;
    y += lh;
    Buffer_set_text_color3(m, 0xE1, -1, 1, 1);
    h -= 2;
    x += 3;
    int32_t seg = h / 4 - 0xF;
    w -= 6;
    int32_t total[4] = {}, known[4] = {};
    for (int32_t i = 0; i < 89; i++) {
        if (Tech[i].preq_tech1 == -2 || Tech[i].preq_tech2 == -2) continue;
        int32_t k = tech_category(i);
        if (k < 0 || k > 3) continue;
        total[k]++;
        if (has_tech(i, faction)) known[k]++;
    }
    y += 8;
    static const int32_t frames[4] = {38, 41, 44, 47}, fills[4] = {36, 39, 42, 45};
    for (int32_t k = 0; k < 4; k++) {
        r = {x, y, x + w, y + 0xF};
        Buffer_box_sprite(m, &r, box_def(frames[k]));
        UiRect cr{x, y, w * known[k] / std::max(total[k], 1) + x, y + 0xF};
        Buffer_set_clip(m, &cr);
        Buffer_box_sprite(m, &r, box_def(fills[k]));
        Buffer_set_clip(m, &all);
        y += 0xF;
        Buffer_set_text_color3(m, mandate_color(k), -1, 1, 1);
        std::string l = "[" + std::string(label_get(0x824 / 4 + k)) + "   " + num(known[k] * 100 / std::max(total[k], 1)) +
                        "%]";
        Buffer_write_l_3(m, l.c_str(), x + 2, (seg - lh) / 2 + y, (int32_t)l.size());
        texts.push_back(l);
        state.push_back("labs progress " + num(k) + " " + num(known[k]) + "/" + num(total[k]));
        y += seg;
    }
}

// ---- energy ----

// ReportWin_draw_energy: the summary (left column) and the budget of a base or of a facility in
// every base (right column, a list: a facility shows its bases, a base its facilities).
void ReportScreen::draw_energy() {
    Buffer* c = &canvas;
    Font* f12 = find_font(12, 0);
    Font* f14 = find_font(14, 0);
    int32_t lh = line_h12();
    UiRect in = inner();
    int32_t mid = (in.left + in.right) / 2;
    int32_t t_bottom = lh + 9 + in.top;          // local_48
    int32_t h_bottom = lh + 9 + t_bottom;        // local_58
    UiRect title_r{in.left + 3, in.top + 3, in.right - 3, t_bottom};
    UiRect lhead{in.left + 3, t_bottom + 2, mid - 1, h_bottom};
    UiRect rhead{mid + 2, t_bottom + 2, in.right - 3, h_bottom};
    UiRect lbody{in.left + 3, h_bottom + 2, mid - 1, in.bottom - 3};
    UiRect rbody{mid + 2, h_bottom + 2, in.right - 3, in.bottom - 3};
    Buffer_box_sprite(c, &in, box_def(3));
    Buffer_box_sprite(c, &title_r, box_def(2));
    Buffer_box_sprite(c, &lhead, box_def(2));
    Buffer_box_sprite(c, &rhead, box_def(2));
    Buffer_box_sprite(c, &lbody, box_def(0));
    Buffer_box_sprite(c, &rbody, box_def(0));
    Buffer_set_font(c, f12, f12, f14, nullptr);
    Buffer_set_text_color(c, 0x99, -1, 1, 1);
    Buffer_set_text_color2(c, 0xF2, -1, 1, 1);
    Buffer_set_text_color3(c, 0xE1, -1, 1, 1);
    const MFaction& mf = MFactions[faction];
    const Faction& f = Factions[faction];
    energy_compute(faction, 0);
    auto cent = [&](const std::string& t, UiRect r) {
        r.top--;
        r.bottom--;
        Buffer_write_cent_l_7(c, t.c_str(), &r, (int32_t)t.size());
        texts.push_back(t);
    };
    GenderDefault = mf.noun_gender;
    PluralDefault = mf.is_noun_plural;
    cent("[" + up(mf.formal_name_faction) + " " + label_get(0xA98 / 4) + "]", title_r);
    cent("[" + std::string(label_get(0xB9C / 4)) + "]", lhead);
    int32_t x = lbody.left + 6, y = lbody.top + 6, w = (lbody.right - lbody.left) - 12;
    auto left = [&](const std::string& t) {
        Buffer_write_l_3(c, t.c_str(), x, y, (int32_t)t.size());
        texts.push_back(t);
    };
    auto right = [&](const std::string& t) {  // Buffer_write_right_l_4
        int32_t tw = Buffer_text_width_2(c, t.c_str(), (int32_t)t.size());
        Buffer_write_multi_font_raw_l(c, t.c_str(), x + (w - tw), y, (int32_t)t.size());
        texts.push_back(t);
    };
    left("{" + std::string(label_get(0xE4 / 4)) + ": ");
    right("{" + num(f.energy_credits) + "}");
    y += lh * 2;
    left("{" + std::string(label_get(0x5F4 / 4)) + ": ");
    right("{" + num(FactionCurrentGrossIncome) + "}");
    y += lh;
    left("{" + std::string(label_get(0x5F8 / 4)) + ": ");
    right("{-" + num(FactionCurrentTotalMaint) + "}");
    y += lh;
    left("{" + std::string(label_get(0x5FC / 4)) + ": ");
    right("{" + std::string(FactionCurrentTotalLoan > 0 ? "+" : "") + num(FactionCurrentTotalLoan) + "}");
    state.push_back("energy summary " + num(f.energy_credits) + " income " + num(FactionCurrentGrossIncome) +
                    " maint " + num(FactionCurrentTotalMaint) + " loan " + num(FactionCurrentTotalLoan) + " net " +
                    num(FactionCurrentNetIncome));
    if (f.sanction_turns) {
        y += lh * 2;
        left(up("{" + std::string(label_get((mf.rule_flags & 0x80 ? 0x1194 : 0x708) / 4)) + ": }"));
        right("{" + num(f.sanction_turns) + " " + label_get(0x71C / 4) + "}");
    }
    bool gap = false;
    auto flying = [&]() {
        Buffer_wrap_flying(c, StrBuffer, x, y, w);
        Buffer_wrap_flying_flush(c);
        texts.push_back(StrBuffer);
    };
    if (mf.rule_flags & 0x4000) {  // FACTENERGY: the faction's interest rule
        gap = true;
        y += lh;
        int32_t v = mf.rule_interest;
        parse_num(0, v);
        std::string l = std::string("FACTENERGY") + (v < 0 ? "PEN" : (v == 0 ? "BON2" : "BON"));
        parse_it(ScriptFileName, l.c_str());
        flying();
        y += lh * 2;
    }
    // The loans with the other factions (LOANPAYMENT / LOANINCOME), "*" for a vendetta or
    // sanctions.
    for (int32_t i = 1; i < 8; i++) {
        uint32_t st = FactionStatus[1];
        if (i == faction || !(st & (1u << i)) || !(st & (1u << faction))) continue;
        const Faction& o = Factions[i];
        if (!f.loan_balance[i] && !o.loan_balance[faction]) continue;
        if (!gap) {
            gap = true;
            y += 10;
        }
        y += lh * 2;
        StrBuffer[0] = 0;
        std::string pre;
        if (f.diplo_status[i] & 0x10) pre = "*" + std::string(label_get(0x604 / 4)) + " ";
        else if ((f.loan_balance[i] && o.sanction_turns) || (o.loan_balance[faction] && f.sanction_turns))
            pre = "*" + std::string(label_get(0x708 / 4)) + " ";
        const MFaction& m = MFactions[i];
        parse_says(3, pre.c_str(), -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        parse_num(1, f.loan_balance[i] ? f.loan_balance[i] : o.loan_balance[faction]);
        parse_num(0, f.loan_balance[i] ? f.loan_payment[i] : o.loan_payment[faction]);
        parse_it(ScriptFileName, f.loan_balance[i] ? "LOANPAYMENT" : "LOANINCOME");
        flying();
    }
    y += lh * 2;
    left("{" + std::string(label_get(0xEFC / 4)) + ": ");
    right("{" + std::string(FactionCurrentNetIncome > 0 ? "+" : "") + num(FactionCurrentNetIncome) + label_get(0x5EC / 4) +
          "}");
    // The right column's subject: the first base when none.
    if (g_energy_subject == -1) {
        g_energy_base = 0;
        for (int32_t i = 0; i < BaseCount; i++)
            if (Bases[i].faction_id == faction) {
                g_energy_base = 1;
                g_energy_subject = i;
                break;
            }
    }
    if (g_energy_subject == -1) return;
    std::vector<Row> rows;
    bool half = SecretProjects[25] >= 0 && Bases[SecretProjects[25]].faction_id == faction;
    int32_t total = 0;
    auto cost = [&](int32_t m) {
        total += m;
        if (!half) return num(m);
        return num(m / 2) + ((m & 1) ? ".5" : "");
    };
    if (!g_energy_base) {
        int32_t fac = g_energy_subject;
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, Facility[fac].name);  // say_facility(.., 0)
        cent(up("[" + up(StrBuffer) + " " + label_get(0x160 / 4) + "]"), rhead);
        for (int32_t b = 0; b < BaseCount; b++) {
            if (Bases[b].faction_id != faction || fac >= 0x41) continue;
            if (!(Bases[b].facilities_built[fac >> 3] & (1 << (fac & 7)))) continue;
            StrBuffer[0] = 0;
            say_base(StrBuffer, b);
            std::string t = std::string(StrBuffer) + "|{" + cost(fac_maint(fac, faction)) + "}";
            rows.push_back({t, b + 0x400});
        }
    } else {
        int32_t b = g_energy_subject;
        StrBuffer[0] = 0;
        say_base(StrBuffer, b);
        cent("[" + up(StrBuffer) + " " + label_get(0xA98 / 4) + "]", rhead);
        for (int32_t fac = 0; fac < 0x41; fac++) {
            if (Bases[b].faction_id != faction) break;
            if (!(Bases[b].facilities_built[fac >> 3] & (1 << (fac & 7)))) continue;
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, Facility[fac].name);  // say_facility(.., 0)
            std::string t = std::string(StrBuffer) + "|{" + cost(fac_maint(fac, faction)) + "}";
            rows.push_back({t, fac});
        }
    }
    rows.push_back({" | ", 99999998});
    std::string tt = "{" + std::string(label_get(0xA9C / 4)) + ": |";
    tt += half ? num(total / 2) + ((total & 1) ? ".5" : "") : num(total);
    rows.push_back({tt + "}", 99999999});
    UiRect lr{rbody.left + 3, rbody.top + 3, rbody.right - 3, rbody.bottom - 3};
    list_draw(c, 0, lr, rows, 99999999, -1, energy_top, 5, "energy budget", nullptr);
}

// ReportIf_draw_energy: BASE EXPENDITURES (left pane: each base's energy, maintenance and net),
// the title and the reserves (centre), MAINTENANCE COSTS (right pane: each facility's count and
// cost).
void ReportScreen::iface_energy() {
    Buffer* m = &mi;
    const Faction& f = Factions[faction];
    energy_compute(faction, 0);
    Font* f10 = find_font(10, 0);
    Font* f12 = find_font(12, 0);
    int32_t lh = line_h12();
    bool half = SecretProjects[25] >= 0 && Bases[SecretProjects[25]].faction_id == faction;
    auto pane = [&](const UiRect& p, const std::string& head, std::vector<Row>& rows, int32_t sel_id, int32_t sel_pos,
                    int32_t& top, int32_t type, const char* tag) {
        int32_t x = p.left + 6, y = p.top + 6, w = (p.right - p.left) - 12, h = (p.bottom - p.top) - 12;
        Buffer_set_font(m, find_font(14, 0), find_font(14, 2), find_font(14, 1), nullptr);
        UiRect r{x, y, x + w, y + lh + 6};
        Buffer_box_sprite(m, &r, box_def(0));
        Buffer_set_text_color3(m, 0xE1, -1, 1, 1);
        Buffer_write_cent_l_5(m, head.c_str(), x, y + 4, w, (int32_t)head.size());
        texts.push_back(head);
        r = {x, r.bottom + 2, x + w, y + h};
        Buffer_box_sprite(m, &r, box_def(0));
        UiRect lr{r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
        list_draw(m, 1, lr, rows, sel_id, sel_pos, top, type, tag, nullptr);
    };
    std::vector<Row> bases;
    for (int32_t i = 0; i < BaseCount; i++) {
        const BASE& b = Bases[i];
        if (b.faction_id != faction) continue;
        StrBuffer[0] = 0;
        say_base(StrBuffer, i);
        int32_t mt = EnergyBaseMaint[i];
        std::string t = std::string(StrBuffer) + "|+" + num(b.economy_total) + "/-" + num(mt) + " = " +
                        (mt <= b.economy_total ? "+" : "") + num(b.economy_total - mt) + label_get(0x5EC / 4);
        bases.push_back({t, i + 0x400});
    }
    // The selection: with a base shown the list's first row (the original compares the base id
    // with 0x3FF), else none.
    int32_t bsel = -1, bpos = -1;
    if (g_energy_base) {
        if (g_energy_subject > 0x3FF) bsel = g_energy_subject;
        else bpos = 0;
    }
    pane(mi_rect(kMiPaneQueue), "[" + std::string(label_get(0x2F8 / 4)) + " " + label_get(0xB98 / 4) + "]", bases, bsel,
         bpos, energy_itop[0], 6, "energy bases");
    // The centre pane.
    UiRect c = mi_rect(kMiPaneCenter);
    bool wide = ScreenWidth != 800;
    int32_t cx = c.left + (wide ? 0x4C : 0), cw = (c.right - (wide ? 0x4C : 0)) - c.left, cy = c.top;
    Buffer_set_font(m, find_font(14, 0), find_font(14, 2), find_font(14, 1), nullptr);
    Buffer_set_text_color(m, 0xE1, -1, 1, 1);
    std::string t = up(MFactions[faction].adj_name_faction) + " " + label_get(0xAC8 / 4);
    if (wide) t += std::string(", ") + " " + label_get(0xB68 / 4) + " " + num(game_year(CurrentTurn));
    Buffer_write_cent_l_5(m, t.c_str(), cx, cy + 10, cw, (int32_t)t.size());
    texts.push_back(t);
    Buffer_set_font(m, f10, f10, f10, nullptr);
    Buffer_set_text_color(m, 0x99, -1, 1, 1);
    cy += lh * 2;
    std::string r = std::string(label_get(0xE4 / 4)) + ": " + num(f.energy_credits);
    Buffer_write_cent_l_5(m, r.c_str(), cx, cy, cw, (int32_t)r.size());
    texts.push_back(r);
    (void)f12;
    std::vector<Row> facs;
    for (int32_t i = 0; i < 70; i++) {
        int32_t n = EnergyFacilityCount[i];
        if (!n) continue;
        int32_t mt = fac_maint(i, faction) * n;
        std::string s = num(n) + " " + Facility[i].name + "|-" + (half ? num(mt / 2) + ((mt & 1) ? ".5" : "") : num(mt)) +
                        label_get(0x5EC / 4);
        facs.push_back({s, i});
    }
    pane(mi_rect(kMiPaneRight), "[" + std::string(label_get(0xF08 / 4)) + "]", facs, g_energy_base ? -1 : g_energy_subject,
         -1, energy_itop[1], 7, "energy facilities");
    state.push_back("energy reserves " + num(f.energy_credits) + " maintenance " + num(f.facility_maint_total) +
                    " net " + num(FactionCurrentNetIncome) + " subject " + num(g_energy_subject) + " base " +
                    num(g_energy_base));
}

// ListBox (Dialog_init_3 in a rectangle, ListBox_item "left|right", font 12, colour 0x99, the
// selected item in 0xF2; rows of the line height + 3): rows from `top`, the selection (by id or
// position) kept in view; each row a spot (type) of parent's spots.
void ReportScreen::list_draw(Buffer* m, int32_t parent, const UiRect& lr, const std::vector<Row>& rows, int32_t sel_id,
                             int32_t sel_pos, int32_t& top, int32_t type, const char* tag,
                             int32_t (*color3)(int32_t id)) {
    Font* f12 = find_font(12, 0);
    int32_t lh = line_h12(), row = lh + 3, n = std::max(1, (lr.bottom - lr.top) / row);
    int32_t sel = sel_pos;
    for (size_t i = 0; i < rows.size(); i++)
        if (sel_id >= 0 && rows[i].id == sel_id) sel = (int32_t)i;
    if (sel >= 0 && (sel < top || sel >= top + n) && sel != list_keep[type & 7]) top = sel < top ? sel : sel - n + 1;
    list_keep[type & 7] = sel;
    top = std::max(0, std::min(top, std::max(0, (int32_t)rows.size() - n)));
    UiRect all{0, 0, m->width, m->height};
    UiRect lr2 = lr;
    if ((int32_t)rows.size() > n) {
        // The list's scroll bar (scroll_small in the main interface, scroll_normal in the window):
        // its buttons and the track above / below the thumb are spots of type + 100.
        ScrollBar sb = scroll_for_list(&lr2, parent == 1, top, (int32_t)rows.size(), n);
        scroll_draw(m, sb);
        int32_t w = sb.r.right - sb.r.left;
        spot_add(&spots[parent], -1, type + 100, sb.r.left, sb.r.top, w, w);
        spot_add(&spots[parent], 1, type + 100, sb.r.left, sb.r.bottom - w, w, w);
        spot_add(&spots[parent], -n, type + 100, sb.r.left, sb.r.top + w, w, (sb.r.bottom - sb.r.top) - 2 * w);
        list_bars[type & 7] = sb;
    }
    const UiRect& lr_ = lr2;
    Buffer_set_clip(m, &lr_);
    for (int32_t k = 0; k < n && top + k < (int32_t)rows.size(); k++) {
        int32_t i = top + k, ry = lr_.top + k * row;
        const std::string& t = rows[i].text;
        size_t bar = t.find('|');
        std::string l = t.substr(0, bar), rt = bar == std::string::npos ? "" : t.substr(bar + 1);
        int32_t col = i == sel ? 0xF2 : 0x99;
        Buffer_set_font(m, f12, f12, f12, nullptr);
        Buffer_set_text_color(m, col, -1, 1, 1);
        Buffer_set_text_color2(m, col, -1, 1, 1);
        Buffer_set_text_color3(m, color3 ? color3(rows[i].id) : col, -1, 1, 1);
        sw_text(this, m, l, lr_.left + 2, ry + 1);
        int32_t tw = Buffer_text_width_2(m, rt.c_str(), (int32_t)rt.size());
        if (!rt.empty()) Buffer_write_multi_font_raw_l(m, rt.c_str(), lr_.right - 2 - tw, ry + 1, (int32_t)rt.size());
        spot_add(&spots[parent], rows[i].id, type, lr_.left, ry, lr_.right - lr_.left, row);
        state.push_back(std::string(tag) + " " + num(rows[i].id) + (i == sel ? " * " : " ") + t);
    }
    Buffer_set_clip(m, &all);
}

// ---- secret projects ----

void ReportScreen::draw_projects() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xAD0)) + "]");
    UiRect in = inner(), tr = title_rect();
    int32_t row_h = 0x36, top = tr.bottom + 2, half = ((in.right - in.left) - 6) / 2 - 1;
    rows_shown = (in.bottom - top - 5) / row_h;
    // Pass 0 / 1: projects under construction (no base yet; the viewer's, then the others' it
    // knows of), pass 2: the projects built (SecretProjects order).
    struct Entry {
        int32_t project, base, built;
    };
    std::vector<Entry> list;
    for (int32_t pass = 0; pass < 2; pass++) {
        for (int32_t p = 0; p < 64; p++) {
            if (SecretProjects[p] != -1) continue;
            for (int32_t b = 0; b < BaseCount; b++) {
                if (Bases[b].queue_items[0] != -(0x46 + p)) continue;
                bool own = Bases[b].faction_id == faction;
                if ((pass == 0) != own) continue;
                list.push_back({p, b, 0});
            }
        }
    }
    for (int32_t p = 0; p < 64; p++)
        if (SecretProjects[p] >= 0) list.push_back({p, SecretProjects[p], 1});
    rows_total = ((int32_t)list.size() + 1) / 2;
    for (size_t i = 0; i < list.size(); i++) {
        const Entry& e = list[i];
        int32_t owner = e.base >= 0 && e.base < BaseCount ? Bases[e.base].faction_id : -1;
        std::string name = Facility[0x46 + e.project].name;
        std::string where;
        if (e.base >= 0 && e.base < BaseCount && (owner == faction || spying(owner) || e.built))
            where = Bases[e.base].name;
        else if (owner >= 0)
            where = MFactions[owner].noun_faction;
        std::string status = e.built ? std::string(owner >= 0 ? MFactions[owner].noun_faction : "")
                                     : std::string(sw_label(0x6BC));
        if (e.built) {
            name = "{" + name + "}";
            where = "{" + where + "}";
            status = "(" + status + ")";
        }
        state.push_back("project " + name + " " + where + " " + status);
        int32_t row = (int32_t)i / 2;
        if (row < scroll || row >= scroll + rows_shown) continue;
        int32_t x = in.left + 3 + (half + 2) * ((int32_t)i % 2), y = top + 2 + (row - scroll) * 0x38;
        UiRect box{x, y, x + half, y + row_h};
        Buffer_box_sprite(c, &box, box_def(1));
        spot_add(&spots[0], e.built ? e.project : e.base + 0x400, 0, box.left, box.top, half, row_h);
        int32_t color = owner >= 0 ? text_color(0x68FA30 + (uint32_t)owner * 4) : 0xE1;
        sw_font(c, f_text, owner >= 0 ? color : 0xE1);
        Buffer_set_font(c, f_text, nullptr, f_text, nullptr);
        Buffer_set_text_color3(c, owner >= 0 ? color : 0xE1, -1, 1, 1);
        sw_text(this, c, name, x + 8, y + 5);
        sw_text(this, c, where, x + 8, y + 5 + line_h());
        sw_font(c, f_text, e.built ? color : 0xF9);
        Buffer_set_font(c, f_text, nullptr, f_text, nullptr);
        Buffer_set_text_color3(c, e.built ? color : 0xF9, -1, 1, 1);
        sw_text(this, c, status, x + 8, y + 5 + 2 * line_h());
    }
}

// ---- satellites ----

namespace {
// ReportWin_draw_sat's animation view (button 0xD3D): satellite.flc in a box 0x240 x 0xD2 centred
// in the window and sixteen satellites on spring orbits (the first N, N the satellites of all the
// factions, shown as 2 x 2 dots); ReportWin_sat_anim every 4000 ms (sat_anim_daemon): the orbits
// stepped (dt 0.02), the film's next frame, the dots.
struct SatAnim {
    float x[16], y[16], vx[16], vy[16], k[16];
    int32_t on[16];
    UiRect box{};
    int32_t cx = 0, cy = 0, fx = 0, fy = 0, frame = 0;
    uint32_t last = 0;
    bool active = false;
    int32_t count = 0;  // +0xF918: the satellites of all factions (dots shown: the first 16)
    // ReportWin_start_attack's explosion (+0xF91C.., sat_expl_daemon every 50 ms): its place, the
    // background under it, its frame; the attack's arguments (+0xF924..).
    bool expl = false, expl_done = false;
    int32_t ex = 0, ey = 0, eframe = 0;
    Buffer ebg;
    int32_t af = 0, at = 0, atype = 0, abase = 0;
    uint32_t elast = 0;
} g_sat;
int32_t g_sat_target = -1;  // DAT_008996A0: the target faction's button (group 0x6A)
int32_t g_sat_kind = -1;    // DAT_0089E648: the satellite kind's button (0x41..0x45, group 0xCE)
// The bases of faction f with Facility 41 (bitmask(0x29)).
int32_t sat_pods(int32_t f) {
    int32_t n = 0;
    for (int32_t b = 0; b < BaseCount; b++)
        if (Bases[b].faction_id == f && has_fac(41, b, 0)) n++;
    return n;
}
int32_t sat_count(int32_t f, int32_t k) {
    const Faction& fa = Factions[f];
    const int32_t v[4] = {fa.satellites_nutrient, fa.satellites_mineral, fa.satellites_energy, fa.satellites_ODP};
    return k == 4 ? sat_pods(f) : v[k];
}
int32_t g_sat_view = 0xD3C;  // DAT_0088037C: the satellites report's view button (0xD3C the table)

void sat_frame(Buffer* c) {
    SatAnim& a = g_sat;
    UiRect in{a.box.left + 3, a.box.top + 3, a.box.right - 3, a.box.bottom - 3};
    Buffer_fill_3(c, &in, 0x41);  // GraphicWin_fill_2(+0xE2B0, 0x41)
    const float dt = 0.02f;
    for (int32_t i = 0; i < 16; i++) {
        a.vx[i] = a.vx[i] - a.k[i] * a.x[i];
        a.vy[i] = a.vy[i] - a.k[i] * a.y[i];
        a.x[i] = a.vx[i] * dt + a.x[i];
        a.y[i] = a.vy[i] * dt + a.y[i];
    }
    if (const Flc* f = flc_get("satellite.flc")) {
        a.frame++;
        Buffer_set_clip(c, &in);
        flc_draw(c, f, a.fx, a.fy, a.frame);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
    }
    for (int32_t i = 0; i < 16; i++) {
        if (!a.on[i]) continue;
        int32_t yy = (int32_t)a.y[i] + a.cy;
        int32_t xx = (int32_t)a.x[i] + a.cx;
        Buffer_fill(c, xx, yy, 2, 2, 0xFF);
    }
}

void sat_start(Buffer* c, const UiRect& win) {
    SatAnim& a = g_sat;
    int32_t x0 = (win.right - 0x240 + win.left) / 2, y0 = (win.bottom - 0xD2 + win.top) / 2;
    a.box = {x0, y0 + 0x14, x0 + 0x240, y0 + 0xE6};
    a.cx = (a.box.right + a.box.left) / 2;
    a.cy = (a.box.bottom + a.box.top) / 2;
    const Flc* f = flc_get("satellite.flc");
    a.fx = a.cx - (f ? f->width : 0) / 2;
    a.fy = a.cy - (f ? f->height : 0) / 2;
    a.frame = 0;
    Buffer_box_sprite(c, &a.box, box_def(4));
    a.box = {a.box.left + 3, a.box.top + 3, a.box.right - 3, a.box.bottom - 3};
    int32_t total = 0;
    for (int32_t fa = 1; fa < 8; fa++)
        total += Factions[fa].satellites_nutrient + Factions[fa].satellites_mineral + Factions[fa].satellites_energy +
                 Factions[fa].satellites_ODP;
    a.count = total;
    for (int32_t i = 0; i < 16; i++) {
        int32_t r = 0xD2 / 2 - 10;
        float k = (float)std::sqrt((double)(int32_t)(float)(r * r * 2));
        k = (float)(std::sqrt((double)(1.0f / (k * k * k))) * 700.0);
        float p = (float)i * 0.0625f;
        float a1 = p * 6.28f, a2 = ((i & 1) ? 0.125f : -0.125f) + p;
        a2 *= 6.28f;
        a.x[i] = (float)(std::cos((double)(k + a1)) * r);
        a.y[i] = (float)(std::sin((double)(k + a2)) * r);
        a.vx[i] = (float)(std::sin((double)(k + a1)) * -r * k);
        a.vy[i] = (float)(std::cos((double)(k + a2)) * k * r);
        a.k[i] = k * 0.02f * k;
        a.on[i] = i < total;
    }
    a.active = true;
    a.last = platform::ticks_ms();
    sat_frame(c);
}

// ReportWin_exp_anim (sat_expl_daemon, every 50 ms): the background restored and the next of
// XI.PCX's sixteen frames of row 3 drawn through table group 1 (0x6F2B7C); after the last, the
// background once more and action_sat_attack.
void sat_expl_step(Buffer* c) {
    SatAnim& a = g_sat;
    if (!a.expl) return;
    Buffer_set_clip(c, &a.box);
    if (!a.expl_done) {
        Buffer_copy_4(&a.ebg, c, 0, 0, a.ex, a.ey, 0x2B, 0x2F);
        Sprite* s = ArtSprite(0x7A9B30u + (uint32_t)(3 * 16 + a.eframe) * 0x2C);
        Sprite_draw_multi_table_dest_2(s, c, s->key, a.ex, a.ey, mem::host(0x6F2B7C));
        if (++a.eframe > 0xF) a.expl_done = true;
    } else {
        a.expl = false;
        Buffer_copy_4(&a.ebg, c, 0, 0, a.ex, a.ey, 0x2B, 0x2F);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        std::fprintf(stderr, "sat attack: faction %d on %d kind %d base %d\n", a.af, a.at, a.atype, a.abase);
        action_sat_attack(a.af, a.at, a.atype, a.abase);
        ui_invalidate();
        return;
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    ui_invalidate();
}

// ReportWin_start_attack (0x49FFD0): the arguments kept; one satellite fewer (its dot gone when it
// was one of the sixteen), the orbits stepped once, the explosion at that satellite's place
// (0x15 left and 0x17 above it), the background under it saved, the 50 ms timer started.
void sat_start_attack(Buffer* c, int32_t f, int32_t target, int32_t kind, int32_t base) {
    SatAnim& a = g_sat;
    a.af = f;
    a.at = target;
    a.atype = kind;
    a.abase = base;
    a.expl_done = false;
    a.eframe = 0;
    if (!a.count) return;
    a.count--;
    int32_t i = a.count & 15;
    if (a.count < 16) a.on[i] = 0;
    sat_frame(c);
    a.ey = (int32_t)a.y[i] - 0x17 + a.cy;
    a.ex = (int32_t)a.x[i] - 0x15 + a.cx;
    Buffer_ctor(&a.ebg);
    Buffer_init(&a.ebg, 0x2B, 0x2F, 8, 0);
    UiRect all{0, 0, 0x2B, 0x2F};
    Buffer_set_clip(&a.ebg, &all);
    Buffer_copy_4(c, &a.ebg, a.ex, a.ey, 0, 0, 0x2B, 0x2F);
    a.expl = true;
    a.elast = platform::ticks_ms();
    std::fprintf(stderr, "sat attack start: faction %d on %d kind %d base %d (explosion at %d,%d)\n", f, target, kind, base,
                 a.ex, a.ey);
    // Unattended runs: the frames at once (as the slides).
    if (g_ui_exit_when_script_done)
        while (a.expl) sat_expl_step(c);
}
}  // namespace

void ReportScreen::draw_sat() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xAD4)) + "]");
    UiRect in = inner(), tr = title_rect();
    g_sat.active = false;
    if (g_sat_view != 0xD3C && g_sat.expl) {
        // (An attack's explosion running: the view kept as it is, not started again.)
        Buffer_box_sprite(c, &g_sat.box, box_def(4));
        g_sat.active = true;
        state.push_back("sat anim attack");
        return;
    }
    if (g_sat_view != 0xD3C) {
        sat_start(c, in);
        int32_t n = 0;
        for (int32_t i = 0; i < 16; i++) n += g_sat.on[i];
        state.push_back("sat anim satellites " + num(n));
        return;
    }
    // Nobody has the satellites' technology (the Sky Hydro Lab's): "Orbital Spaceflight not yet
    // achieved" (label 0xCC4) in the middle.
    bool any = false;
    for (int32_t f = 1; f < 8; f++)
        if (has_tech(Facility[65].preq_tech, f)) any = true;
    if (!any) {
        sw_font(c, f_bold, 0xE1);
        sw_text_cent(this, c, sw_label(0xCC4), {in.left, (in.top + in.bottom) / 2 - 10, in.right, (in.top + in.bottom) / 2 + 10});
        state.push_back("sat none");
        return;
    }
    // ReportWin_draw_sat's table: a header row and the seven factions, rows of (height - 0x32 -
    // (top + 0x32) - 2) / 8 from 0x32 below the panel's top (the original mixes the panel's height
    // into the bottom), the name column a third of the width (a fifth with the expansion's fifth
    // column) and four or five columns: the satellites of the Sky Hydro Lab, Nessus Mining Station,
    // Orbital Power Transmitter, Orbital Defense Pods (the ODPs left / all) and, with the
    // expansion, the bases with Facility 41; the headers (Facility names) wrapped in white, the
    // numbers centred in their column's colour (0xF8 when none).
    const int32_t W = in.right - in.left, H = in.bottom - in.top;
    const int32_t top = in.top + 0x32, bottom = H - 0x32;
    const int32_t rh = ((bottom - top) - 2) / 8;
    const int32_t ncol = ExpansionEnabled ? 5 : 4;
    const int32_t namew = ExpansionEnabled ? W / 5 : W / 3;
    const int32_t colw = ((W - 4) - namew) / ncol;
    static const int32_t kColor[5] = {0x98, 0xDF, 0x62, 0xF9, 0xFF};
    int32_t y = top;
    for (int32_t f = 0; f < 8; f++) {
        int32_t x = in.left + 2;
        if (f) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            sw_font(c, f_text, FactionColorText[f], FactionColorShadow[f]);
            sw_text(this, c, MFactions[f].noun_faction, x, y);
        }
        x += namew;
        std::string l = "sat " + (f ? std::string(MFactions[f].noun_faction) : std::string("header"));
        int32_t pods = 0;
        if (f)
            for (int32_t b = 0; b < BaseCount; b++)
                if (Bases[b].faction_id == f && has_fac(41, b, 0)) pods++;
        for (int32_t k = 0; k < ncol; k++, x += colw) {
            UiRect cell{x, y, x + colw, y + rh};
            if (!f) {
                sw_font(c, f_text, 0xFF);
                std::string h = k == 4 ? Facility[41].name : Facility[65 + k].name;
                texts.push_back(h);
                Buffer_wrap_cent_4(c, h.c_str(), cell.left, cell.top, cell.right - cell.left);
                continue;
            }
            const Faction& fa = Factions[f];
            const int32_t sat[4] = {fa.satellites_nutrient, fa.satellites_mineral, fa.satellites_energy, fa.satellites_ODP};
            bool some = k == 4 ? pods != 0 : sat[k] != 0;
            sw_font(c, f_text, some ? kColor[k] : 0xF8);
            std::string v;
            if (k == 3) v = num(fa.satellites_ODP - fa.ODP_deployed) + "/";
            v += num(k == 4 ? pods : sat[k]);
            UiRect vr{cell.left, cell.top, cell.right, cell.top + line_h()};
            sw_text_cent(this, c, v, vr);
            spot_add(&spots[0], k, 80 + f, cell.left, cell.top, cell.right - cell.left, cell.bottom - cell.top);
            l += " " + v;
        }
        if (f) state.push_back(l);
        y += rh;
    }
}

// ReportIf_draw_sat (0x4AA970): the main interface's panes. The table: SAT0 (left) and SAT1
// (right) of the script in their boxes; ORBITAL ATTACK VIEW: "[label 0xF18]" over the target
// factions' buttons (those with satellites or Facility 41 bases; group 0x6A) on the left, "[label
// 0xF1C]" over the kinds the target has (the Facility names, ids 0x41..0x45; group 0xCE) on the
// right; ATTACK (0xD3E, in the window) shown once a kind is chosen (ReportIf_on_iface_group_clicked).
void ReportScreen::iface_sat() {
    Buffer* m = &mi;
    Font* f12 = find_font(12, 0);
    const int32_t lh = f12->field_0 < 0 ? f12->height : f12->ascent_above + f12->field_0;  // ReportIf +0x1C
    for (BaseButton* b : sat_target)
        if (b) b->shown = false;
    for (BaseButton* b : sat_type)
        if (b) b->shown = false;
    for (int32_t side = 0; side < 2; side++) {
        const UiRect p = mi_rect(side ? kMiPaneRight : kMiPaneQueue);
        int32_t x = p.left + 6, y = p.top + 6, w = (p.right - p.left) - 12, h = (p.bottom - p.top) - 12;
        UiRect clip = p;
        Buffer_set_clip(m, &clip);
        if (g_sat_view == 0xD3C) {
            UiRect bx{x, y, x + w, y + h};
            Buffer_box_sprite(m, &bx, box_def(0));
            // The section's lines joined (parse_string) and wrapped 4 in.
            std::string text;
            if (!X_text_open(ScriptFileName, side ? "SAT1" : "SAT0")) {
                while (const char* line = text_get()) {
                    if (!*line) break;
                    char buf[512];
                    buf[0] = 0;
                    parse_string(line, buf);
                    text += std::string(buf) + " ";
                }
            }
            text_close();
            sw_font(m, f12, 0xE1);
            texts.push_back(text);
            UiRect tr{x + 4, y + 4, x + w - 4, y + h - 4};
            Buffer_wrap_2(m, text.c_str(), &tr);
        } else {
            UiRect hb{x, y, x + w, y + lh + 6};
            Buffer_box_sprite(m, &hb, box_def(0));
            UiRect lb{x, hb.bottom + 2, x + w, p.bottom - 6};
            Buffer_box_sprite(m, &lb, box_def(0));
            std::string head = "[" + sw_label(side ? 0xF1C : 0xF18) + "]";
            Buffer_set_font(m, f12, find_font(14, 2), find_font(14, 1), nullptr);
            Buffer_set_text_color(m, 0xE1, -1, 1, 1);
            Buffer_set_text_color2(m, 0xE1, -1, 1, 1);
            Buffer_set_text_color3(m, 0xE1, -1, 1, 1);
            int32_t tw = Buffer_text_width_2(m, head.c_str(), (int32_t)head.size());
            sw_text(this, m, head, x + (w - tw) / 2, y + 4);
            // The buttons: 9 in, from 0x20 below the pane's top, 0x16 apart.
            int32_t bx = p.left + 9, bw = (p.right - p.left) - 0x12, by = p.top + 0x20;
            if (!side) {
                for (int32_t f = 1; f < 8; f++) {
                    BaseButton* b = sat_target[f];
                    if (!b) continue;
                    int32_t total = 0;
                    for (int32_t k = 0; k < 4; k++) total += sat_count(f, k);
                    b->shown = total + sat_pods(f) != 0;
                    bw_button_set(b, f == g_sat_target);
                }
            } else {
                int32_t t = g_sat_target >= 1 && g_sat_target <= 7 ? g_sat_target : -1;
                for (int32_t k = 0; k < 5; k++) {
                    BaseButton* b = sat_type[k];
                    if (!b) continue;
                    b->r = {bx, by, bx + bw, by + 0x14};
                    if (t > 0 && (k < 4 || ExpansionEnabled) && sat_count(t, k)) {
                        b->shown = true;
                        bw_button_set(b, 0x41 + k == g_sat_kind);
                        by += 0x16;
                    }
                }
                // A kind the target lacks: ATTACK hidden.
                int32_t k = g_sat_kind - 0x41;
                if (sat_attack_btn && (t < 0 || k < 0 || k > 4 || !sat_count(t, k))) sat_attack_btn->shown = false;
            }
        }
        UiRect all{0, 0, m->width, m->height};
        Buffer_set_clip(m, &all);
    }
    if (g_sat_view == 0xD3C && sat_attack_btn) sat_attack_btn->shown = false;
    std::string l = "sat iface view " + num(g_sat_view) + " target " + num(g_sat_target) + " kind " + num(g_sat_kind) +
                    " targets";
    for (int32_t f = 1; f < 8; f++)
        if (sat_target[f] && sat_target[f]->shown) l += " " + num(f) + "@" + num(sat_target[f]->r.left) + "," + num(sat_target[f]->r.top);
    l += " kinds";
    for (int32_t k = 0; k < 5; k++)
        if (sat_type[k] && sat_type[k]->shown) l += " " + num(k) + "@" + num(sat_type[k]->r.left) + "," + num(sat_type[k]->r.top);
    l += std::string(" attack ") + (sat_attack_btn && sat_attack_btn->shown ? "shown" : "hidden");
    state.push_back(l);
}

// sat_attack (0x4A0260): the orbital attack of f on faction target with kind (0..3 a kind of
// satellite, 4 the Facility 41 bases): without Orbital Defense Pods NOKILLERS, with all of them
// used this turn KILLERSDEPLOYED; when the target has that kind: break_treaty (a refusal ends it),
// diplo_lock, then (kind 4) WHICHGSP picks the base (satbat_sm.pcx) and ReportWin_start_attack.
void sat_attack(int32_t f, int32_t target, int32_t kind) {
    if (f == target || target < 1) return;
    const Faction& F = Factions[f];
    if (F.satellites_ODP == 0) {
        if (f == CurrentPlayerFaction) popp(ScriptFileName, "NOKILLERS", 0, "space_sm.pcx", 0);
        return;
    }
    if (F.satellites_ODP <= F.ODP_deployed) {
        if (f == CurrentPlayerFaction) popp(ScriptFileName, "KILLERSDEPLOYED", 0, "space_sm.pcx", 0);
        return;
    }
    if (!sat_count(target, kind)) return;
    if (break_treaty(f, target, 7)) return;
    if (diplo_lock(100)) return;
    int32_t base = 0;
    bool go = true;
    if (kind == 4) {
        BasePop* p = popup_new();
        Popup_start(p, ScriptFileName, "WHICHGSP", -1, nullptr, 0);
        BasePop_set_picture(p, "satbat_sm.pcx");
        // (The original tests the bases' Facility 41 bit at Bases + f * 0x20CC, the attacker's
        // faction offset, not the base's own facilities.)
        int32_t off = 0, mask = 0;
        bitmask(41, &off, &mask);
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(&Bases[0]);
        for (int32_t b = 0; b < BaseCount; b++) {
            if (Bases[b].faction_id != target) continue;
            if (raw[0x8C + (size_t)f * 0x20CC + (size_t)b * 0x134 + off] & mask) BasePop_item(p, Bases[b].name, b);
        }
        base = BasePop_exec_3(p, 0, 0);
        popup_delete(p);
        go = base >= 0;
    }
    if (go) sat_start_attack(&g_rs.canvas, f, target, kind, base);
    diplo_unlock();
}

// ---- security nexus ----

bool sec_passes(int32_t unit_id) {
    const UNIT& u = Units[unit_id];
    if (!(u.unit_flags & 1)) return false;
    bool combat = Weapon[u.weapon_id].offense_value != 0;
    if (g_sec_filter >= 4) return true;
    if (g_sec_filter == 3) return !combat;
    return Chassis[u.chassis_id].triad == g_sec_filter && combat;
}

void ReportScreen::draw_security() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xAD8)) + "]");
    UiRect in = inner(), tr = title_rect();
    int32_t row_h = 0x2B, top = tr.bottom + 2;
    rows_shown = (in.bottom - top) / row_h;
    std::vector<int32_t> list;
    for (int32_t i = 0; i < 0x80; i++) {
        int32_t id = i < 0x40 ? i : faction * 0x40 - 0x40 + i;
        if (!sec_passes(id)) continue;
        const Faction& f = Factions[faction];
        if (f.units_lost[id] + (f.units_queue[id] != 0) + (f.units_active[id] != 0) == 0) continue;
        list.push_back(id);
    }
    rows_total = (int32_t)list.size();
    state.push_back("security filter " + num(g_sec_filter) + " types " + num(rows_total));
    for (int32_t r = 0; r < rows_total; r++) {
        int32_t id = list[r];
        const Faction& f = Factions[faction];
        int32_t active = f.units_active[id] > 0xFA ? 0 : f.units_active[id];
        int32_t prod = f.units_queue[id] > 0xFA ? 0 : f.units_queue[id];
        int32_t lost = (int16_t)f.units_lost[id];
        std::string name = std::string((Units[id].obsolete_factions & (1 << faction)) ? "-" : "") + Units[id].name;
        StrBuffer[0] = 0;
        say_stats(StrBuffer, id, nullptr);
        std::string stats = StrBuffer;
        std::string counts = "{" + num(active) + " " + sw_label(0x614) + ", }" + num(prod) + " " + sw_label(0x618) + ", [" +
                             num(lost) + " " + sw_label(0x61C) + "]";
        state.push_back("security " + num(id) + " " + name + " " + stats + " " + counts);
        if (r < scroll || r >= scroll + rows_shown) continue;
        int32_t y = top + (r - scroll) * row_h;
        UiRect row{in.left + 3, y, in.right - 3, y + 0x29};
        Buffer_box_sprite(c, &row, box_def(1));
        spot_add(&spots[0], id, 0, row.left, row.top, row.right - row.left, row.bottom - row.top);
        int32_t ty = y - line_h() / 2 + 0x15;
        Buffer_set_clip(c, &row);
        veh_draw(c, veh_fake(id, faction), in.left, y + 2, -6, 8, 0);
        UiRect all{0, 0, c->width, c->height};
        Buffer_set_clip(c, &all);
        sw_font(c, f_text, 0x99);
        Buffer_set_text_color2(c, 0xF2, -1, 1, 1);
        Buffer_set_text_color3(c, 0xF9, -1, 1, 1);
        sw_text(this, c, name, in.left + 0x66, ty);
        sw_text(this, c, stats, (0x28A >> 1) + 0x28, ty);
        sw_text_right(this, c, counts, {in.left, ty, 0x28A - 2, ty + line_h()});
    }
}

void ReportScreen::iface_security() {
    Buffer* m = &mi;
    UiRect p = mi_rect(kMiPaneQueue);
    UiRect r{p.left + 6, p.top + 6, p.right - 6, p.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(3));
    int32_t y = r.top + 6, lh = f_small->height + 4;
    sw_font(m, f_bold, 0x99);
    sw_text(this, m, up(sw_label(0xE28)), r.left + 6, y);
    y += lh + 2;
    for (int32_t i = 0; i < 5; i++) {
        sw_font(m, f_small, i == g_sec_filter ? 0xFF : 0x99);
        sw_text(this, m, sw_label(0xE2C + (uint32_t)i * 4), r.left + 10, y);
        spot_add(&spots[1], i, 3, r.left, y, r.right - r.left, lh);
        y += lh;
    }
}

// ---- score ----

void ReportScreen::draw_score() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xADC)) + "]");
    compute_score(faction, nullptr, nullptr, 0);
    Faction& f = Factions[faction];
    int32_t* table = &f.unk_110;
    UiRect in = inner(), tr = title_rect();
    int32_t x = in.left + 3 + 4, w = (in.right - 3) - (in.left + 3) - 8, y = tr.bottom + 2 + 4;
    if (X_text_open(ScriptFileName, "SCORING")) return;
    for (int32_t i = 0; i < 9; i++) {
        text_get();
        if (!table[i] || i == 5) continue;
        std::string label = TextBufferGetPtr;
        if (i == 6) {
            int32_t own = num_objectives(faction, 0), all = num_objectives(faction, GameRules & 0x1000);
            if (own < all) {
                sw_font(c, f_text, 0x99);
                sw_text(this, c, num(own) + " " + sw_label(0xE0C), x, y);
                y += line_h();
                sw_text(this, c, num(all - own) + " " + sw_label(0x1030), x, y);
                y += line_h() * 3 / 2;
                label = "";
            } else {
                label = num(own) + " " + label;
            }
        } else if (i == 7) {
            if (GameState & 0x200000) label = sw_label(0xE70);
            else if (GameState & 0x400000) label = sw_label(0xE88);
            else if (f.player_flags & 0x100000) label = sw_label(0x1198);
            else if (!(GameState & 0x2000)) label = sw_label(0xE10);
        }
        std::string v = (table[i] < 0 ? "- " : "+ ") + num(std::abs(table[i]));
        sw_font(c, f_text, 0x99);
        sw_text(this, c, label, x, y);
        sw_text_right(this, c, v, {x, y, x + w, y + line_h()});
        state.push_back("score " + label + " " + v);
        y += line_h();
    }
    text_close();
    if (table[5]) {
        std::string label = sw_label(MapNativeLifeForms == 0 ? 0x904 : 0x908);
        std::string v = (table[5] < 0 ? "- " : "+ ") + num(std::abs(table[5]));
        sw_text(this, c, label, x, y);
        sw_text_right(this, c, v, {x, y, x + w, y + line_h()});
        state.push_back("score " + label + " " + v);
        y += line_h();
    }
    if ((GameRules & 0x400) && !(GameState & 0x20)) {
        sw_text(this, c, sw_label(0x1044), x, y);
        y += line_h();
    }
    y += line_h() / 2;
    sw_font(c, f_bold, 0xFF);
    std::string total = num(f.unk_109);
    sw_text(this, c, up(sw_label(0x6C0)), x, y);
    sw_text_right(this, c, total, {x, y, x + w, y + line_h()});
    state.push_back("score total " + total);
}

// ---- intelligence ----

void ReportScreen::draw_intel() {
    // ReportWin_draw_intel: the target's leader, the treaty, personality, interests, agenda,
    // headquarters, reserves, research and votes (the mood line, say_special of Mood, is not
    // written; the faction picture is not drawn).
    Buffer* c = &canvas;
    int32_t t = g_intel_target;
    title("[" + up(adj(t)) + " " + up(sw_label(0xAE0)) + "]");
    UiRect in = inner(), tr = title_rect();
    int32_t x = in.left + 12, y = tr.bottom + 8, lh = line_h() + 2;
    auto line = [&](const std::string& s, int32_t color = 0x99) {
        sw_font(c, f_text, color);
        sw_text(this, c, s, x, y);
        state.push_back("intel " + s);
        y += lh;
    };
    const MFaction& m = MFactions[t];
    const Faction& f = Factions[t];
    line(std::string(m.title_leader) + " " + m.name_leader + " " + sw_label(0x2D0) + " " + m.noun_faction, 0xFF);
    std::string status;
    uint32_t d = Factions[faction].diplo_status[t];
    if (d & 0x10) status = sw_label(0x358);      // VENDETTA
    else if (d & 1) status = sw_label(0x34C);    // PACT
    else if (d & 2) status = sw_label(0x350);    // TREATY
    else if (d & 4) status = sw_label(0x354);    // TRUCE
    line(sw_label(0xE98) + ":  " + status);
    line(sw_label(0x5A0) + ":  " + sw_label(f.AI_fight < 1 ? (f.AI_fight < 0 ? 0x5AC : 0x5A8) : 0x5A4));
    std::string interests;
    const int32_t* ai = &f.AI_growth;
    for (int32_t k = 0; k < 4; k++)
        if (ai[k]) interests += (interests.empty() ? "" : ", ") + sw_label(0x824 + (uint32_t)k * 4);
    line(sw_label(0x5B0) + ":  " + interests);
    if (m.soc_priority_category >= 0 && m.soc_priority_model)
        line(sw_label(0x5C0) + ":  " + std::string(SocialField[m.soc_priority_category].field_name) + ": " +
             SocialField[m.soc_priority_category].soc_name[m.soc_priority_model]);
    int32_t hq = -1;
    for (int32_t b = 0; b < BaseCount; b++)
        if (Bases[b].faction_id == t && (Bases[b].facilities_built[0] & 2)) hq = b;  // FAC_HEADQUARTERS
    if (hq >= 0) line(sw_label(0x5C4) + ":  " + Bases[hq].name);
    line(sw_label(0xE4) + ":  " + num(f.energy_credits));
    StrBuffer[0] = 0;
    if (f.tech_research_id >= 0) say_tech_2(StrBuffer, f.tech_research_id, 0);
    line(sw_label(0x254) + ":  " + StrBuffer);
    line(sw_label(0xF00) + ":  " + num(council_votes(t)));
}

void ReportScreen::iface_common(const std::string& head) {
    (void)head;
}

void ReportScreen::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    spot_init(&spots[0], 0x100);
    spot_init(&spots[1], 0x100);
    state.clear();
    UiRect frame{0x17, 0, 0x28A + 0x17, c->height};
    Buffer_box_sprite(c, &frame, box_def(9));
    sw_restore_iface(this);
    // ReportIf_draw_ops / draw_projects: WorldWin_show_all (the world map pane, the base locator's).
    if (kind() == kOps || kind() == kProjects) worldwin_draw_to(&mi);
    {
        // The centre pane's header (ReportIf_draw_*): the report, M.Y.; base operations: the
        // energy reserves.
        static const uint32_t names[9] = {0xAE0, 0, 0xAC4, 0xA98, 0xACC, 0xAD0, 0xAD4, 0xAD8, 0xADC};
        UiRect cp = mi_rect(kMiPaneCenter);
        int32_t k = kind() >= 0 && kind() <= 8 ? kind() : 0;
        std::string h = up(adj(faction)) + " " + up(sw_label(names[k])) + ",  " + sw_label(0xB68) + " " +
                        num(game_year(CurrentTurn));
        sw_font(&mi, f_bold, 0xE1);
        if (k != kLabs && k != kEnergy) sw_text_cent(this, &mi, h, {cp.left, cp.top + 4, cp.right, cp.top + 4 + f_bold->height});
        if (k == kOps) {
            sw_font(&mi, f_small, 0xF2);
            sw_text_cent(this, &mi, up(sw_label(0xE4)) + ": " + num(Factions[faction].energy_credits),
                         {cp.left, cp.top + 6 + f_bold->height, cp.right, cp.top + 6 + f_bold->height + f_small->height});
        }
    }
    switch (kind()) {
    case kLabs: draw_labs(); iface_labs(); break;
    case kEnergy: draw_energy(); iface_energy(); break;
    case kOps: draw_ops(); iface_ops(); break;
    case kProjects: draw_projects(); break;
    case kSat: draw_sat(); iface_sat(); break;
    case kSecurity: draw_security(); iface_security(); break;
    case kScore: draw_score(); break;
    default: draw_intel(); break;
    }
    // ReportIf_report: base operations, secret projects and the security nexus get the window's
    // scroll bar (0x882584) when their rows do not fit: scroll_show puts the rail and the arm at
    // the window's right edge (the first time, with its animation), scroll_hide removes them.
    {
        bool need = (kind() == kOps || kind() == kProjects || kind() == kSecurity) && rows_total > rows_shown;
        if (need && !rail_shown) {
            rail_shown = true;
            // (When the window opens it comes after slide_show; here the first frame has no
            // animation.)
            if (drawn) scroll_show_window(c, x, y);
            else std::fprintf(stderr, "scroll show %dx%d at %d,%d (opening)\n", c->width, c->height, x, y);
        } else if (!need && rail_shown) {
            rail_shown = false;
            scroll_hide_window(c, x, y);
        }
        if (need) {
            scroll_rail_draw(c, y);
            ScrollBar sb;
            sb.r = {c->width - 0x2B, 0x15, c->width - 0x2B + 0x18, 0x15 + scroll_bar_height(c->height)};
            sb.max = rows_total - rows_shown;
            sb.pos = std::min(scroll, sb.max);
            scroll_draw(c, sb);
            int32_t w = 0x18;
            spot_add(&spots[0], -1, 99, sb.r.left, sb.r.top, w, w);
            spot_add(&spots[0], 1, 99, sb.r.left, sb.r.bottom - w, w, w);
            spot_add(&spots[0], -rows_shown, 99, sb.r.left, sb.r.top + w, w, (sb.r.bottom - sb.r.top) - 2 * w);
            page_bar = sb;
            state.push_back("page scroll " + num(sb.pos) + " of " + num(sb.max));
        }
    }
    for (int32_t i = 0; i < 7; i++)
        if (report_btn[i]) bw_button_set(report_btn[i], kind() == i + 2);
    for (BaseButton* b : ops_iface) {
        b->shown = kind() == kOps && (b->id == 0xD0A || b->id == 0xD0B || !ops_best);
        if (b->id == 0xD0A || b->id == 0xD0B) bw_button_set(b, (b->id == 0xD0B) == ops_best);
    }
    drawn = true;
    for (BaseButton* b : labs_iface) {
        b->shown = kind() == kLabs;
        bw_button_set(b, (b->id == 0x92) == (labs_tab != 0));
    }
    for (int32_t i = 0; i < 2; i++) {
        if (!sat_btn[i]) continue;
        sat_btn[i]->shown = kind() == kSat;
        bw_button_set(sat_btn[i], g_sat_view == 0xD3C + i);
    }
    for (int32_t i = 0; i < 3; i++) {
        if (!ops_btn[i]) continue;
        ops_btn[i]->shown = kind() == kOps;
        bw_button_set(ops_btn[i], g_ops_view == i);
    }
}

void ReportScreen::on_move(int32_t x, int32_t y) {
    if (kind() == kLabs) labs_mouse(this, &canvas, x, y);
}

void ReportScreen::log_state() {
    std::fprintf(stderr, "reportstate kind %d faction %d scroll %d\n", kind(), faction, scroll);
    for (const std::string& s : state) std::fprintf(stderr, "report %s\n", s.c_str());
}

// ReportIf_report: switches the shown report (the scroll position restarts).
void switch_report(int32_t k) {
    kind() = k;
    g_rs.scroll = 0;
    g_energy_subject = -1;  // (ReportWin_on_redraw)
}

void ReportScreen::on_button(int32_t id) {
    // ReportIf_on_iface_button_clicked: the report buttons 2..8 (the shown one closes).
    if (id >= 2 && id <= 8) {
        if (kind() == id) sw_close(this);
        else switch_report(id);
    }
    // ReportWin_on_button_clicked: the views of base operations, DATALINKS (1), CLOSE (9).
    if (id >= 0x8B3 && id <= 0x8B5) g_ops_view = id - 0x8B3;
    if (id == 0xD3C || id == 0xD3D) g_sat_view = id;  // ReportWin_on_group_clicked
    // ReportIf_on_iface_group_clicked: a target (group 0x6A: ATTACK hidden), a kind (0xCE: shown).
    if (id > 0x6A00 && id < 0x6A08) {
        g_sat_target = id - 0x6A00;
        if (sat_attack_btn) sat_attack_btn->shown = false;
    }
    if (id >= 0xCE41 && id <= 0xCE45) {
        g_sat_kind = id - 0xCE00;
        if (sat_attack_btn) sat_attack_btn->shown = true;
    }
    // ReportWin_on_button_clicked: ATTACK.
    if (id == 0xD3E && kind() == kSat) {
        int32_t k = g_sat_kind - 0x41;
        if (g_sat_target > 0 && g_sat_target < 8 && k >= 0 && k < 5) sat_attack(CurrentPlayerFaction, g_sat_target, k);
        else if (sat_attack_btn) sat_attack_btn->shown = false;
    }
    if (id == 9) sw_close(this);
    if (id == 1) {  // DATALINKS: the entry of the report's subject
        switch (kind()) {
        case kIntel: Datalink_exec(&DatalinkWin, 0xF, g_intel_target); break;
        case kLabs: {
            int32_t t = Factions[faction].tech_research_id;
            Datalink_exec(&DatalinkWin, 0xE, t == -1 ? 0x3E : t);
            break;
        }
        case kEnergy: Datalink_exec(&DatalinkWin, 0, 0x2B); break;
        case kOps: Datalink_exec(&DatalinkWin, 10, 0x1D); break;
        case kProjects: Datalink_exec(&DatalinkWin, 0xB, 0x47); break;
        case kSat: Datalink_exec(&DatalinkWin, 0xE, Facility[65].preq_tech); break;
        case kSecurity: Datalink_exec(&DatalinkWin, 3, 7); break;
        case kScore: Datalink_exec(&DatalinkWin, 1, 4); break;
        }
    }
    if (id == 0xD0A || id == 0xD0B) ops_best = id == 0xD0B;
    if ((id == 0x92 || id == 0x93) && kind() == kLabs) labs_tab = id == 0x92;  // ReportIf_on_iface_group_clicked
    if (kind() == kLabs) labs_button(id);  // the tree's buttons (ReportWin_on_button_clicked)
    if (id >= 0xCEF && id <= 0xCF5 && kind() == kOps && faction == CurrentPlayerFaction) ops_governor(this, id);
}

// ReportWin_on_key_click.
void ReportScreen::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (key & 0x30000) return;
    if (vk == 0xD || vk == 0x1B) {
        sw_close(this);
        return;
    }
    if (vk == 0x70) return;  // F1: help (vtable + 0xB0), not reconstructed
    if (vk > 0x70 && vk < 0x78) {
        if (kind() == vk - 0x6F) sw_close(this);
        else switch_report(vk - 0x6F);
        return;
    }
    if (vk < 0x21 || vk > 0x28) {
        if (vk >= 0x30 && vk <= 0x5A) sw_close(this);
        return;
    }
    int32_t page = std::max(rows_shown, 1);
    int32_t last = std::max(rows_total - rows_shown, 0);
    switch (vk) {
    case 0x21: scroll -= page; break;              // PgUp
    case 0x22: scroll += page; break;              // PgDn
    case 0x23: scroll = last; break;               // End
    case 0x24: scroll = 0; break;                  // Home
    case 0x26: scroll -= 1; break;                 // Up
    case 0x28: scroll += 1; break;                 // Down
    case 0x25: switch_report((kind() + 4) % 7 + 2); return;  // Left: (kind + 6 - 2) % 7 + 2
    case 0x27: switch_report((kind() + 6) % 7 + 2); return;  // Right
    default: break;
    }
    scroll = std::max(0, std::min(scroll, last));
}

void locator_start(int32_t base_id);

void ReportScreen::on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) {
    // ReportWin_on_right_click in the satellites table with the editor's omniscient view: sat_edit
    // (0x4A0810), a satellite more (shift: fewer; the ODPs used then kept at most all of them).
    if (parent == 0 && right && type >= 81 && type <= 87 && kind() == kSat && (GameState & 0x40) && (GameState & 0x80)) {
        int32_t f = type - 80, k = index;
        if (k < 4 && faction != 0) {
            Faction& fa = Factions[f];
            int32_t* v[4] = {&fa.satellites_nutrient, &fa.satellites_mineral, &fa.satellites_energy, &fa.satellites_ODP};
            if (!(ui_key_state(0x10) & 0x8000)) {
                ++*v[k];
            } else {
                if (*v[k]) --*v[k];
                if (k == 3 && fa.satellites_ODP - fa.ODP_deployed < 0) fa.ODP_deployed = fa.satellites_ODP;
            }
            see_map_check();
            std::fprintf(stderr, "sat edit: faction %d kind %d -> %d\n", f, k, *v[k]);
        }
        return;
    }
    if (type == 99) {  // the window's scroll bar (Scroll 0x882584): a line or a page
        int32_t d = index;
        if (d < -1 || d > 1) d = scroll_click(page_bar, x, y, -d);
        scroll = std::max(0, std::min(scroll + d, std::max(rows_total - rows_shown, 0)));
        return;
    }
    if (type >= 100) {  // a list's scroll bar: a line (the buttons) or a page (the track)
        int32_t* t = list_top(type - 100);
        if (t) {
            int32_t d = index;
            if (d < -1 || d > 1) {
                const ScrollBar& sb = list_bars[(type - 100) & 7];
                d = scroll_click(sb, x, y, -d);
            }
            *t = std::max(0, *t + d);
        }
        return;
    }
    if (parent == 1) {
        // ReportIf_on_iface_selected (labs): a technology moves the tree, a base opens.
        if (kind() == kEnergy && (type == 6 || type == 7) && index >= 0) {
            // ReportIf_on_iface_selected (energy): a base or a facility in the right column.
            g_energy_subject = index > 0x3FF ? index - 0x400 : index;
            g_energy_base = index > 0x3FF;
            return;
        }
        if (kind() == kLabs && type == 4 && index >= 0) {
            if (index < 0x400) labs_tree_tech() = index;
            else BaseWin_zoom(&BaseWin, index - 0x400, 0);
            return;
        }
        if (kind() == kOps && type == 2 && index > 0 && index < 9) ops_sort(index);
        if (kind() == kSecurity && type == 3 && index >= 0 && index < 5) {
            g_sec_filter = index;
            scroll = 0;
        }
        return;
    }
    if (index < 0) {
        if (kind() == kOps) {
            // A click outside the rows: the selection and the order are reset.
            std::memset(selected, 0, sizeof(selected));
            ops_reset_order();
        }
        return;
    }
    if (kind() == kEnergy && type == 5) {
        // ReportWin_on_selected: a facility of the base, or a base with the facility.
        if (index < 0x400) {
            g_energy_subject = index;
            g_energy_base = 0;
        } else if (index <= 0x5F5E0FD) {
            g_energy_subject = index - 0x400;
            g_energy_base = 1;
        }
        return;
    }
    if (kind() == kLabs && index == 0x29A) {
        // ReportWin_on_left_click: the research at the tree's centre, then the research choice
        // (blind research: the priorities).
        int32_t r = Factions[faction].tech_research_id;
        if (r >= 0 && ((GameState & 0x80) || !(GameRules & 0x200))) labs_tree_tech() = r;
        if (!(GameRules & 0x200)) Console_set_tech(&MainConsole, CurrentPlayerFaction);
        else Console_set_ai(&MainConsole, CurrentPlayerFaction);
        return;
    }
    if (kind() != kOps) return;
    if (dbl) {
        // ReportWin_on_left_double_click: opens the base.
        if (index >= 0x400 && index - 0x400 < 0x400) BaseWin_zoom(&BaseWin, index - 0x400, 0);
        return;
    }
    if (index >= 0xC00 && Bases[index - 0xC00].faction_id == CurrentPlayerFaction) {
        // The base's picture: the governor on (cycling its mandate) / off.
        int32_t base_id = index - 0xC00;
        if (bw_lock_base(base_id)) return;
        BaseWin_soft_zoom(&BaseWin, base_id);
        BASE& b = Bases[base_id];
        uint32_t g = (uint32_t)b.governor_flags;
        if (!(g & 0x80000000) || !(g & 0x8000000)) {
            int32_t mode = (g & 0x1000000) ? 0 : ((g & 0x2000000) ? 1 : (int32_t)((~g & 0x4000000 | 0x8000000) >> 0x1A));
            b.governor_flags = (int32_t)((g | 0x80000000) & 0xF0FFFFFF);
            static const uint32_t bits[4] = {0x2000000, 0x4000000, 0x8000000, 0x1000000};
            if (mode >= 0 && mode < 4) b.governor_flags |= (int32_t)bits[mode];
            BaseWin_gov_activate(&BaseWin);
        } else {
            b.governor_flags = (int32_t)(g & 0x7FFFFFFF);
        }
        draw_radius(b.x, b.y, 2, 2);
        bw_unlock_base(base_id);
        return;
    }
    if (kind() == kProjects && index >= 0 && index < 0x800 && type == 0) {
        // ReportWin_on_left_click (5): a project or a base under construction: its base located
        // on the world map.
        int32_t base_id = index < 0x400 ? SecretProjects[index] : index - 0x400;
        if (base_id >= 0 && base_id < BaseCount) locator_start(base_id);
        return;
    }
    if (index >= 0x400 && index < 0x800) {
        // A row: the base selected (shift: a range, ctrl: toggled).
        int32_t id = index - 0x400;
        if (Bases[id].faction_id != CurrentPlayerFaction) return;
        bool shift = ui_key_state(VKEY_SHIFT) & 0x8000, ctrl = ui_key_state(VKEY_CONTROL) & 0x8000;
        if (!shift && ctrl) {
            selected[id >> 5] ^= 1u << (id & 31);
        } else if (shift && last_click >= 0x400) {
            if (!ctrl) std::memset(selected, 0, sizeof(selected));
            int32_t a = std::min(last_click - 0x400, id), b = std::max(last_click - 0x400, id);
            for (int32_t i = a; i <= b; i++)
                if (Bases[i].faction_id == CurrentPlayerFaction) selected[i >> 5] |= 1u << (i & 31);
        } else {
            std::memset(selected, 0, sizeof(selected));
            selected[id >> 5] |= 1u << (id & 31);
        }
        last_click = index;
        // A single base selected (+0x5A04): located on the world map (ReportIf_bl_anim).
        int32_t n = 0;
        for (int32_t i = 0; i < 0x200; i++) n += (selected[i >> 5] >> (i & 31)) & 1;
        if (n == 1) locator_start(id);
    }
}

// ---- the base locator (ReportIf_bl_anim 0x4A4060, bl_daemon every 150 ms) ----

namespace {
Sprite g_dots[4];          // ReportIf +0x20254: dot0.pcx .. dot3.pcx (12 x 12)
int32_t g_bl = 11;         // +0x1E248: the animation's step (-1 .. 10)
int32_t g_bl_x = 0, g_bl_y = 0;  // +0x1E24C: in the world map pane
uint32_t g_bl_last = 0;
}  // namespace

// ReportIf_bl_anim: the world map redrawn, the step's dot (0 0 1 2 3 2 3 2 1 0 0) at the base;
// the timer stops at step 10.
void locator_step() {
    if (g_bl > 10) return;
    worldwin_draw_to(&g_rs.mi);
    static const int8_t kFrame[12] = {0, 0, 1, 2, 3, 2, 3, 2, 1, 0, 0, -1};  // steps -1 .. 10
    int32_t fr = kFrame[g_bl + 1];
    if (fr >= 0 && g_dots[fr].width) {
        UiRect pane = mi_rect(0xCFC);
        BlitDestScale = BlitSourceScale = 1;
        pfncSpriteStretchCopyToCanvas(&g_dots[fr], &g_rs.mi, g_dots[fr].key, pane.left + g_bl_x, pane.top + g_bl_y);
    }
    g_bl++;
    ui_invalidate();
}

// ReportWin_on_left_click: a base the viewer knows (its own or seen) located: the dot's animation
// at its world map pixel less (7, 5).
void locator_start(int32_t base_id) {
    const BASE& b = Bases[base_id];
    if ((uint8_t)b.faction_id != CurrentPlayerFaction && !(b.visibility & (1 << CurrentPlayerFaction))) return;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        for (int32_t i = 0; i < 4; i++) {
            Buffer art;
            Buffer_ctor(&art);
            std::string file = "dot" + std::to_string(i) + ".pcx";
            if (Buffer_load_pcx(&art, file.c_str(), nullptr, 0, 0xEC) == 0)
                pfncSpriteFromCanvasRectTrans(&g_dots[i], &art, 9, 0, 0, 0xC, 0xC, 0);
        }
    }
    int32_t px = 0, py = 0;
    worldwin_tile_to_pixel(b.x, b.y, &px, &py);
    g_bl_x = px - 7;
    g_bl_y = py - 5;
    g_bl = -1;
    g_bl_last = platform::ticks_ms();
    std::fprintf(stderr, "report locator: base %d at %d,%d\n", base_id, g_bl_x, g_bl_y);
    locator_step();
}

}  // namespace

void reports_tick(uint32_t now) {
    if (g_rs.open && g_sat.expl && kind() == kSat && now - g_sat.elast >= 50) {  // sat_expl_daemon
        g_sat.elast = now;
        sat_expl_step(&g_rs.canvas);
    }
    if (g_rs.open && g_sat.active && !g_sat.expl && kind() == kSat && now - g_sat.last >= 4000) {  // sat_anim_daemon
        g_sat.last = now;
        sat_frame(&g_rs.canvas);
        ui_invalidate();
    }
    if (!g_rs.open || g_bl > 10 || now - g_bl_last < 150) return;
    g_bl_last = now;
    locator_step();
}

namespace {

void load_buttons() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, "report.pcx", nullptr, 10, 0xEC) != 0) return;
    for (int32_t i = 0, x = 0; i < 7; i++, x += 0x1D)
        for (int32_t s = 0, y = 0; s < 3; s++, y += 0x23)
            pfncSpriteFromCanvasRectTrans(&g_rs.buttons[i][s], &art, 9, x, y, 0x1E, 0x24, 0);
}

// ReportWin's modal run (ReportIf_on_status at the opening).
void open_reports(int32_t k) {
    ReportScreen* s = &g_rs;
    kind() = k;
    if (s->open) {
        switch_report(k);
        return;
    }
    s->name = "report";
    s->rail_shown = false;
    s->drawn = false;
    s->slide = true;  // ReportIf_on_status: slide_show / slide_hide
    basewin_mi_init();
    int32_t w = 0x28A + 0x4E, h = 0x161 + 200;
    sw_init(s, (ScreenWidth - 0x28A) / 2 - 0x17, ScreenHeight - h - 0xD4, w, h, true);
    load_buttons();
    s->faction = CurrentPlayerFaction;
    s->scroll = 0;
    s->f_title = find_font(16, 1);
    s->f_bold = find_font(14, 1);
    s->f_text = find_font(12, 0);
    s->f_small = find_font(12, 0);
    sw_clear_buttons(s);
    const UiRect& b = mi_rect(kMiPaneBottom);
    int32_t y = ((b.bottom + b.top) - 0x24) / 2;
    for (int32_t i = 0; i < 7; i++) {
        BaseButton* bt = sw_button(s, nullptr, i + 2, b.left + 0x11 + i * (0x1E + 12), y, 0x1E, 0x24, 1, s->f_small);
        for (int32_t st = 0; st < 3; st++) {
            bt->tl[st] = bt->top[st] = bt->tr[st] = nullptr;
            bt->sprite[st] = &s->buttons[i][st];
        }
        // ReportIf_on_status: the report's name as its help bubble (labels 0xAC4..0xADC).
        bt->bubble = label_get((0xAC4 + (uint32_t)i * 4) / 4);
        s->report_btn[i] = bt;
    }
    // The view buttons of base operations (production, citizens, garrison: ids 0x8B3..0x8B5)
    // below the title (ReportIf_on_status), a third of the inner width each.
    UiRect in = s->inner();
    int32_t bx = in.left + 3, by = s->line_h() + 0xB + in.top, bw = (in.right - in.left) - 6;
    static const uint32_t labels[3] = {0xBA0, 0xBA4, 0xFC};
    int32_t lefts[3] = {bx, bw / 3 + bx, bx + 2 + (bw / 3) * 2};
    int32_t rights[3] = {bw / 3 - 2 + bx, bx + (bw / 3) * 2, bx + bw};
    for (int32_t i = 0; i < 3; i++) {
        s->ops_btn[i] = sw_button(s, up(sw_label(labels[i])).c_str(), 0x8B3 + i, lefts[i], by, rights[i] - lefts[i], 0x14, 0,
                                  s->f_bold);
    }
    // The satellites' two views (ReportIf_on_status: labels 0xCB0 / 0xCB4, the halves of the same
    // row; the table set).
    s->sat_btn[0] = sw_button(s, up(sw_label(0xCB0)).c_str(), 0xD3C, bx, by, bw / 2 - 1, 0x14, 0, s->f_bold);
    s->sat_btn[1] = sw_button(s, up(sw_label(0xCB4)).c_str(), 0xD3D, bw / 2 + 1 + bx, by, bx + bw - (bw / 2 + 1 + bx), 0x14,
                              0, s->f_bold);
    g_sat_view = 0xD3C;
    // ReportIf_on_status: the ORBITAL ATTACK VIEW's buttons: the target factions in the left pane
    // (9 in, from 0x20 below its top, 0x16 apart; those with the satellites' technologies,
    // satellites or Facility 41 bases), the kinds in the right pane (placed by iface_sat), ATTACK
    // (label 0xCB8, boxes 47 / 46 / 45) under the view button 0xD3D at the panel's bottom.
    {
        const UiRect& lp = mi_rect(kMiPaneQueue);
        int32_t tx = lp.left + 9, tw = (lp.right - lp.left) - 0x12, ty = lp.top + 0x20;
        for (int32_t f = 0; f < 8; f++) s->sat_target[f] = nullptr;
        for (int32_t f = 1; f < 8; f++) {
            if (f == CurrentPlayerFaction) continue;
            int32_t total = sat_pods(f);
            for (int32_t k = 0; k < 4; k++) total += sat_count(f, k);
            if (!has_tech(Facility[65].preq_tech, f) && !total && !has_tech(Facility[41].preq_tech, f)) continue;
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            s->sat_target[f] = sw_button(s, MFactions[f].noun_faction, 0x6A00 + f, tx, ty, tw, 0x14, 1, s->f_bold);
            s->sat_target[f]->shown = false;
            ty += 0x16;
        }
        for (int32_t k = 0; k < 5; k++) {
            s->sat_type[k] = sw_button(s, k == 4 ? Facility[41].name : Facility[65 + k].name, 0xCE41 + k, tx, ty, tw, 0x14, 1,
                                       s->f_bold);
            s->sat_type[k]->shown = false;
        }
        const UiRect& vb = s->sat_btn[1]->r;
        s->sat_attack_btn = sw_button(s, sw_label(0xCB8).c_str(), 0xD3E, vb.left, in.bottom - 0x1A, vb.right - vb.left, 0x14, 0,
                                      s->f_bold);
        s->sat_attack_btn->box[0] = box_def(47);
        s->sat_attack_btn->box[1] = box_def(46);
        s->sat_attack_btn->box[2] = box_def(45);
        for (Sprite** sp : {s->sat_attack_btn->left, s->sat_attack_btn->right, s->sat_attack_btn->top, s->sat_attack_btn->bottom,
                            s->sat_attack_btn->tl, s->sat_attack_btn->tr, s->sat_attack_btn->bl, s->sat_attack_btn->br})
            sp[0] = sp[1] = sp[2] = nullptr;
        s->sat_attack_btn->shown = false;
        g_sat_target = g_sat_kind = -1;
    }
    // DATALINKS (1) and CLOSE (9) below the report (ReportIf_on_status).
    int32_t cx = (in.left + in.right) / 2, dl = in.left + 0x4B;
    sw_button(s, up(sw_label(0x86C)).c_str(), 1, dl, in.bottom + 7, cx - 1 - dl, 0x14, 0, s->f_bold);
    sw_button(s, up(sw_label(0xA64)).c_str(), 9, cx + 1, in.bottom + 7, cx - dl - 1, 0x14, 0, s->f_bold);
    // Base operations' main interface: the GOVERNOR / BEST BASES tabs and the governor buttons.
    s->ops_iface.clear();
    const UiRect& q = mi_rect(kMiPaneQueue);
    int32_t gx = q.left + 9, gw = (q.right - q.left) - 0x12;
    s->ops_iface.push_back(sw_button(s, up(sw_label(0x7B8)).c_str(), 0xD0A, gx, q.top + 9, gw / 2 - 1, 0x14, 1, s->f_bold));
    s->ops_iface.push_back(
        sw_button(s, up(sw_label(0xAA8)).c_str(), 0xD0B, gx + gw / 2 + 1, q.top + 9, gw / 2, 0x14, 1, s->f_bold));
    int32_t gy = q.top + 0x23;
    s->ops_iface.push_back(sw_button(s, up(sw_label(0xBA8)).c_str(), 0xCEF, gx, gy, gw, 0x14, 1, s->f_bold));
    for (int32_t k = 0; k < 4; k++) {
        gy += 0x16;
        std::string t = up(sw_label(0xBAC) + " " + sw_label(0x824 + (uint32_t)k * 4));
        s->ops_iface.push_back(sw_button(s, t.c_str(), 0xCF0 + k, gx, gy, gw, 0x14, 1, s->f_bold));
    }
    gy += 0x16;
    s->ops_iface.push_back(sw_button(s, up(sw_label(0xBB0)).c_str(), 0xCF4, gx, gy, gw, 0x14, 1, s->f_bold));
    gy += 0x16;
    s->ops_iface.push_back(sw_button(s, up(sw_label(0xBB4)).c_str(), 0xCF5, gx, gy, gw, 0x14, 1, s->f_bold));
    s->ops_best = false;
    // The labs report's tabs (ReportIf_on_status: TECHNOLOGY 0x92, BASES 0x93) and the tree's
    // technology: the research (0 with blind research or none).
    s->labs_iface.clear();
    s->labs_iface.push_back(sw_button(s, sw_label(0x5D4).c_str(), 0x92, gx, q.top + 9, gw / 2 - 1, 0x14, 1, s->f_bold));
    s->labs_iface.push_back(sw_button(s, sw_label(0x5D8).c_str(), 0x93, gx + gw / 2 + 1, q.top + 9, gw / 2, 0x14, 1, s->f_bold));
    s->labs_tab = 1;
    s->labs_top = 0;
    g_energy_subject = -1;
    s->energy_top = s->energy_itop[0] = s->energy_itop[1] = 0;
    {
        int32_t r = Factions[s->faction].tech_research_id;
        bool blind = !(GameState & 0x80) && (GameRules & 0x200);
        labs_tree_tech() = (blind || r < 0 || r > 0x59) ? 0 : r;
    }
    sw_run(s);
    sw_clear_buttons(s);
    s->ops_iface.clear();
    s->labs_iface.clear();
    for (auto& p : s->report_btn) p = nullptr;
    for (auto& p : s->ops_btn) p = nullptr;
}

}  // namespace

// report_labs .. report_score (0x4AC9A0..0x4ACB80): the kind, then the window opened or switched.
void GAMEAPI report_labs() { open_reports(kLabs); }
void GAMEAPI report_energy() { open_reports(kEnergy); }
void GAMEAPI report_ops() { open_reports(kOps); }
void GAMEAPI report_projects() { open_reports(kProjects); }
void GAMEAPI report_sat() { open_reports(kSat); }
void GAMEAPI report_security() { open_reports(kSecurity); }
void GAMEAPI report_score(int32_t) { open_reports(kScore); }
void GAMEAPI report_intel(int32_t faction_id) {
    g_intel_target = faction_id;
    open_reports(kIntel);
}

// The view of base operations (test aid and the main interface's view buttons).
void reports_set_ops_view(int32_t view) { g_ops_view = std::max(0, std::min(view, 2)); }
