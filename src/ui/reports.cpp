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
// Approximated (not compared with the original's pixels): the layout inside the window of the
// other reports (rows and columns at the original's rectangles, text instead of some pictures:
// the citizens of a base, the project pictures), the list boxes' look, the scroll bars, the
// faction logo of the main interface's centre pane, the base locator map, the
// satellite animation, the hover help. The faction buttons of ReportIf (viewing other factions'
// reports, only with GameState 0x80) are not drawn.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

void ReportScreen::draw_ops() {
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
            sw_font(c, f_text, 0xE1);
            sw_text(this, c, what, col, row.top + 5);
            sw_font(c, f_text, 0xF2);
            UiRect rr{col, row.bottom - line_h() - 1, row.right - 4, row.bottom - 1};
            std::string amount = "(" + num(have) + "/" + num(cost) + ")    " + turns;
            sw_text_right(this, c, amount, rr);
            line += " build " + what + " " + amount;
        } else if (g_ops_view == 1) {
            int32_t workers = b.pop_size - b.talent_total - b.specialist_total - std::max(b.drone_total, 0);
            std::string s = num(b.talent_total) + " " + sw_label(0x4CC) + "+, " + num(std::max(workers, 0)) + " workers, " +
                            num(b.drone_total) + " drones, " + num(b.specialist_total) + " specialists";
            sw_font(c, f_text, 0xE1);
            sw_text(this, c, s, col, row.top + 12);
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

void ReportScreen::draw_sat() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xAD4)) + "]");
    UiRect in = inner(), tr = title_rect();
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
    int32_t y = tr.bottom + 10, lh = line_h() + 4;
    int32_t c0 = in.left + 12, step = 110;
    sw_font(c, f_bold, 0x99);
    static const uint32_t heads[4] = {0xA4, 0xA0, 0xE4, 0};
    sw_text(this, c, "", c0, y);
    for (int32_t k = 0; k < 3; k++)
        sw_text_right(this, c, up(sw_label(heads[k])), {c0 + 180 + k * step, y, c0 + 180 + (k + 1) * step - 10, y + lh});
    sw_text_right(this, c, "ODP", {c0 + 180 + 3 * step, y, c0 + 180 + 4 * step - 10, y + lh});
    y += lh + 4;
    for (int32_t f = 1; f < 8; f++) {
        if (!(FactionStatus[1] & (1 << f)) && !Factions[f].base_count) continue;
        if (f != faction && !(Factions[faction].diplo_status[f] & 1) && !(GameState & 0x80)) continue;
        const Faction& fa = Factions[f];
        int32_t v[4] = {fa.satellites_nutrient, fa.satellites_mineral, fa.satellites_energy, fa.satellites_ODP};
        sw_font(c, f_text, text_color(0x68FA30 + (uint32_t)f * 4));
        sw_text(this, c, MFactions[f].noun_faction, c0, y);
        std::string l = "sat " + std::string(MFactions[f].noun_faction);
        for (int32_t k = 0; k < 4; k++) {
            sw_text_right(this, c, num(v[k]), {c0 + 180 + k * step, y, c0 + 180 + (k + 1) * step - 10, y + lh});
            l += " " + num(v[k]);
        }
        state.push_back(l);
        y += lh;
    }
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
    case kSat: draw_sat(); break;
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

void ReportScreen::on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool, bool dbl) {
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
    }
}

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
