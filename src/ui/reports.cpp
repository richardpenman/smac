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
//   * labs: a click on the research line chooses the research (Console_set_tech).
// Approximated (not compared with the original's pixels): the layout inside the window (rows
// and columns at the original's rectangles, text instead of some pictures: the citizens of a
// base, the technology and project pictures), the scroll bars, the base locator map, the
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
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
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
    std::vector<std::string> state;  // what log_state prints

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;

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
    if (id == 0xCF4) {  // ADVANCED SETTINGS: the governor dialog (BaseWin_governor), not reconstructed
        console_not_reconstructed("ReportIf: ADVANCED SETTINGS (BaseWin_governor)");
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

void ReportScreen::draw_labs() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xAC4)) + "]");
    Faction& f = Factions[faction];
    energy_compute(faction, 0);
    UiRect in = inner(), tr = title_rect();
    int32_t y = tr.bottom + 8, x = in.left + 12;
    std::string res;
    if (!(GameState & 0x80) && (GameRules & 0x200)) {
        res = up(sw_label(0xAC4));  // blind research
    } else {
        StrBuffer[0] = 0;
        res = sw_label(0x254) + ": ";
        if (f.tech_research_id >= 0) {
            say_tech_2(StrBuffer, f.tech_research_id, 0);
            res += up(StrBuffer);
            if (f.tech_research_id == 0x58) {
                res += " " + num(f.tech_count_transcendent + 1);
            } else {
                int32_t cat = tech_category(f.tech_research_id);
                res += " (" + std::string(label_get((int32_t)(0x824 / 4) + cat)) + " " +
                       num(tech_recurse(f.tech_research_id, 0)) + ")";
            }
        }
    }
    UiRect rr{in.left + 6, y - 3, in.right - 6, y + line_h() + 6};
    Buffer_box_sprite(c, &rr, box_def(1));
    spot_add(&spots[0], 0x29A, 0, rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top);
    sw_font(c, f_bold, 0xE1);
    sw_text(this, c, res, x, y);
    state.push_back("labs " + res);
    y = rr.bottom + 8;
    // What the research gives (ReportWin_labs_line per category).
    int32_t t = f.tech_research_id;
    if (t < 0) return;
    struct Cat {
        uint32_t label;
        std::vector<std::string> items;
    };
    std::vector<Cat> cats = {{0x234, {}}, {0x120, {}}, {0x124, {}}, {0x128, {}}, {0x12C, {}},
                             {0x110, {}}, {0x118, {}}};
    for (int32_t i = 0; i < 4; i++)
        if (Reactor[i].preq_tech == t) cats[0].items.push_back(Reactor[i].name);
    for (int32_t i = 0; i < 9; i++)
        if (Chassis[i].preq_tech == t) cats[1].items.push_back(Chassis[i].offsv1_name);
    for (int32_t i = 0; i < 26; i++)
        if (Weapon[i].preq_tech == t) cats[2].items.push_back(Weapon[i].name);
    for (int32_t i = 0; i < 14; i++)
        if (Armor[i].preq_tech == t) cats[3].items.push_back(Armor[i].name);
    for (int32_t i = 0; i < 29; i++)
        if (Ability[i].preq_tech == t) cats[4].items.push_back(Ability[i].name);
    for (int32_t i = 1; i < 134; i++)
        if (Facility[i].preq_tech == t && Facility[i].name) cats[5].items.push_back(Facility[i].name);
    for (int32_t k = 0; k < 4; k++)
        for (int32_t m = 0; m < 4; m++)
            if (SocialField[k].soc_preq_tech[m] == t) cats[6].items.push_back(SocialField[k].soc_name[m]);
    for (const Cat& ct : cats) {
        if (ct.items.empty()) continue;
        std::string s = sw_label(ct.label) + ": ";
        for (size_t i = 0; i < ct.items.size(); i++) s += (i ? ", " : "") + ct.items[i];
        sw_font(c, f_text, 0x99);
        sw_text(this, c, s, x, y);
        state.push_back("labs gives " + s);
        y += line_h() + 4;
    }
}

void ReportScreen::iface_labs() {
    // ReportIf_draw_labs: TECH COST (tech_rate), TECH ACCUMULATED, TECH PER TURN (energy_compute's
    // research points / 100, one decimal).
    Buffer* m = &mi;
    Faction& f = Factions[faction];
    UiRect p = mi_rect(kMiPaneQueue);
    UiRect r{p.left + 6, p.top + 6, p.right - 6, p.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(3));
    int32_t y = r.top + 6, lh = f_small->height + 2;
    int32_t cost = tech_rate(faction);
    energy_compute(faction, 0);
    std::string per = num(FactionCurrentLabs / 100);
    if (FactionCurrentLabs % 100) per += "." + num((FactionCurrentLabs % 100) / 10);
    sw_font(m, f_small, 0x99);
    sw_text(this, m, sw_label(0x5E0) + ": " + num(cost), r.left + 6, y);
    y += lh;
    sw_text(this, m, sw_label(0x5E4) + ": " + num(f.tech_accumulated), r.left + 6, y);
    y += lh;
    sw_text(this, m, sw_label(0x5E8) + ": " + per, r.left + 6, y);
    state.push_back("labs cost " + num(cost) + " accumulated " + num(f.tech_accumulated) + " per turn " + per);
}

// ---- energy ----

void ReportScreen::draw_energy() {
    Buffer* c = &canvas;
    title("[" + up(adj(faction)) + " " + up(sw_label(0xA98)) + "]");
    UiRect in = inner(), tr = title_rect();
    int32_t y = tr.bottom + 6, lh = line_h() + 2;
    rows_shown = (in.bottom - y - 6) / lh;
    std::vector<int32_t> list;
    for (int32_t i = 0; i < BaseCount; i++)
        if (Bases[i].faction_id == faction) list.push_back(i);
    rows_total = (int32_t)list.size();
    int32_t c1 = in.left + 0xC8, c2 = in.left + 0x140, c3 = in.left + 0x1B8, c4 = in.right - 12;
    sw_font(c, f_bold, 0x99);
    sw_text(this, c, up(sw_label(0x2F8)), in.left + 12, y);
    sw_text_right(this, c, up(sw_label(0x5B8)), {c1 - 60, y, c1, y + lh});
    sw_text_right(this, c, up(sw_label(0x4F8)), {c2 - 60, y, c2, y + lh});
    sw_text_right(this, c, up(sw_label(0xF0)), {c3 - 60, y, c3, y + lh});
    sw_text_right(this, c, up(sw_label(0xF08)), {c4 - 150, y, c4, y + lh});
    y += lh + 2;
    for (int32_t r = 0; r < rows_total; r++) {
        int32_t id = list[r];
        set_base(id);
        base_compute(0);
        const BASE& b = Bases[id];
        int32_t maint = 0;
        for (int32_t fac = 1; fac < 0x46; fac++)
            if (b.facilities_built[fac >> 3] & (1 << (fac & 7))) maint += Facility[fac].maint;
        std::string l = "energy " + std::string(b.name) + " " + num(b.economy_total) + " " + num(b.psych_total) + " " +
                        num(b.labs_total) + " " + num(maint);
        state.push_back(l);
        if (r < scroll || r >= scroll + rows_shown - 1) continue;
        sw_font(c, f_text, 0xE1);
        sw_text(this, c, b.name, in.left + 12, y);
        sw_text_right(this, c, num(b.economy_total), {c1 - 60, y, c1, y + lh});
        sw_text_right(this, c, num(b.psych_total), {c2 - 60, y, c2, y + lh});
        sw_text_right(this, c, num(b.labs_total), {c3 - 60, y, c3, y + lh});
        sw_text_right(this, c, num(maint), {c4 - 150, y, c4, y + lh});
        y += lh;
    }
}

void ReportScreen::iface_energy() {
    Buffer* m = &mi;
    Faction& f = Factions[faction];
    energy_compute(faction, 0);
    UiRect p = mi_rect(kMiPaneQueue);
    UiRect r{p.left + 6, p.top + 6, p.right - 6, p.bottom - 6};
    Buffer_box_sprite(m, &r, box_def(3));
    int32_t y = r.top + 6, lh = f_small->height + 2;
    int32_t loans = 0;
    for (int32_t i = 0; i < 8; i++) loans += f.loan_payment[i];
    auto line = [&](uint32_t label, const std::string& v) {
        sw_font(m, f_small, 0x99);
        sw_text(this, m, up(sw_label(label)) + ": " + v, r.left + 6, y);
        y += lh;
    };
    line(0xE4, num(f.energy_credits));
    line(0xF08, num(f.facility_maint_total));
    line(0x5FC, num(loans));
    line(0xEFC, num(FactionCurrentNetIncome) + sw_label(0x5EC));
    std::string alloc = num((10 - f.SE_alloc_psych - f.SE_alloc_labs) * 10) + "/" + num(f.SE_alloc_psych * 10) + "/" +
                        num(f.SE_alloc_labs * 10);
    line(0xAC8, alloc);
    state.push_back("energy reserves " + num(f.energy_credits) + " maintenance " + num(f.facility_maint_total) +
                    " net " + num(FactionCurrentNetIncome) + " alloc " + alloc);
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
        sw_text_cent(this, &mi, h, {cp.left, cp.top + 4, cp.right, cp.top + 4 + f_bold->height});
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
    for (int32_t i = 0; i < 7; i++)
        if (report_btn[i]) bw_button_set(report_btn[i], kind() == i + 2);
    for (BaseButton* b : ops_iface) {
        b->shown = kind() == kOps && (b->id == 0xD0A || b->id == 0xD0B || !ops_best);
        if (b->id == 0xD0A || b->id == 0xD0B) bw_button_set(b, (b->id == 0xD0B) == ops_best);
    }
    for (int32_t i = 0; i < 3; i++) {
        if (!ops_btn[i]) continue;
        ops_btn[i]->shown = kind() == kOps;
        bw_button_set(ops_btn[i], g_ops_view == i);
    }
}

void ReportScreen::log_state() {
    std::fprintf(stderr, "reportstate kind %d faction %d scroll %d\n", kind(), faction, scroll);
    for (const std::string& s : state) std::fprintf(stderr, "report %s\n", s.c_str());
}

// ReportIf_report: switches the shown report (the scroll position restarts).
void switch_report(int32_t k) {
    kind() = k;
    g_rs.scroll = 0;
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

void ReportScreen::on_click(int32_t parent, int32_t index, int32_t type, int32_t, int32_t, bool, bool dbl) {
    if (parent == 1) {
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
    if (kind() == kLabs && index == 0x29A) {
        Console_set_tech(&MainConsole, CurrentPlayerFaction);
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
    sw_run(s);
    sw_clear_buttons(s);
    s->ops_iface.clear();
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
