// The help module (0x44B380..0x44CBC0): help_tech_info, tech_heck and the help_* entry points of
// the datalinks (help.h). Asm-faithful in the order and form of the lines; the globals the
// original also sets for the chassis and weapon items (HelpChassisShown / HelpWeaponShown, the last
// name shown) are not written.
#include "ui/help.h"

#include <cstring>
#include <string>

#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"

namespace {

int32_t g_heck_section = -1;  // DAT_00684634: the section tech_heck writes for (-1: all)
int32_t g_heck_header = 0;    // DAT_007591BC: the "^" before the first line was written

// tech_heck (0x44B380): the lines of helpx.txt #TECHHECK that apply to tech_id.
void tech_heck(const std::function<void(const char*)>& out, int32_t t) {
    if (t >= 89) return;
    g_heck_header = 0;
    if (text_open("HELPX", "TECHHECK")) return;
    auto line = [&](bool cond, const char* text) {
        if (!cond) return;
        if (!g_heck_header && g_heck_section == -1) {
            g_heck_header = 1;
            out("^");
        }
        out(text);
    };
    const CRules& r = Rules;
    const int32_t rules[7] = {r.tech_preq_improv_fungus,       r.tech_preq_ease_fungus_mov,
                              r.tech_preq_build_road_fungus,   r.tech_preq_allow_2_spec_abil,
                              r.tech_preq_allow_3_nutrients_sq, r.tech_preq_allow_3_minerals_sq,
                              r.tech_preq_allow_3_energy_sq};
    for (int32_t v : rules) line(t == v, text_get());
    const uint32_t flags[8] = {1, 2, 4, 8, 0x10, 0x40, 0x80, 0x100};
    for (uint32_t b : flags) line(((uint32_t)Tech[t].flags & b) != 0, text_get());
    line(t == r.tech_preq_orb_insert_wo_space, text_get());
    line(t == r.tech_preq_mining_platform_bonus, text_get());
    line(t == r.tech_preq_economic_victory, text_get());
    line(t == Proposal[1].preq_tech, text_get());
    std::string tmpl = text_get();  // "Formers can execute the "$TERRAFORM0" order."
    for (int32_t i = 0; i < 20; i++) {
        for (int32_t k = 0; k < 2; k++) {
            int32_t preq = k ? Terraform[i].preq_tech_sea : Terraform[i].preq_tech;
            if (t != preq || (k == 1 && Terraform[i].preq_tech == Terraform[i].preq_tech_sea)) continue;
            parse_say(0, k ? Order[4 + i].order_sea : Order[4 + i].order, -1, -1);
            char buf[1024];
            buf[0] = 0;
            parse_string(tmpl.c_str(), buf);
            line(true, buf);
        }
    }
    for (int32_t i = 2; i < 11; i++) {
        if (t != Proposal[i].preq_tech) continue;
        std::string s = std::string("^") + label_get(498) + ": " + Proposal[i].name;
        line(true, s.c_str());
    }
    text_close();
}

// One group of items: with section -1, "^LABEL: a, b, c" as one line; else one "^item" per line.
struct Group {
    const std::function<void(const char*)>& out;
    int32_t section;
    std::string buf;
    bool any = false;
    Group(const std::function<void(const char*)>& o, int32_t s) : out(o), section(s) {}
    void add(int32_t label, const std::string& item) {
        if (section < 0) {
            buf += any ? ", " : std::string("^") + label_get(label) + ": ";
        } else {
            buf += "^";
        }
        buf += item;
        any = true;
        if (section >= 0 && !buf.empty()) {
            out(buf.c_str());
            buf.clear();
            any = false;
        }
    }
    void end() {
        if (section < 0 && any) out(buf.c_str());
        buf.clear();
        any = false;
    }
};

}  // namespace

std::string help_link(const char* name, int32_t cat, int32_t index) {
    return std::string("$LINK<") + (name ? name : "") + "=" + std::to_string(cat * 10000 + index) + ">";
}

void help_tech_info(const std::function<void(const char*)>& out, int32_t t, int32_t, int32_t section) {
    if (section < 0 || section == 2) {
        Group g(out, section);
        for (int32_t i = 0; i < 4; i++)
            if (Reactor[i].preq_tech == t) g.add(141, help_link(Reactor[i].name, 5, i + 1));
        g.end();
        for (int32_t i = 0; i < 9; i++)
            if (Chassis[i].preq_tech == t)
                g.add(72, help_link(Chassis[i].offsv1_name, 4, i) + " (" + std::to_string(Chassis[i].speed) + ")");
        g.end();
        for (int32_t i = 0; i < 26; i++) {
            if (Weapon[i].preq_tech != t) continue;
            std::string v = Weapon[i].offense_value < 0 ? label_get(196)
                            : Weapon[i].mode > 2        ? label_get(128)
                                                        : std::to_string(Weapon[i].offense_value);
            g.add(73, help_link(Weapon[i].name, 6, i) + " (" + v + ")");
        }
        g.end();
        for (int32_t i = 0; i < 14; i++) {
            if (Armor[i].preq_tech != t) continue;
            std::string v = Armor[i].defense_value < 0 ? label_get(196) : std::to_string(Armor[i].defense_value);
            g.add(74, help_link(Armor[i].name, 7, i) + " (" + v + ")");
        }
        g.end();
        for (int32_t i = 0; i < 29; i++)
            if (Ability[i].preq_tech == t) g.add(75, help_link(Ability[i].name, 8, i));
        g.end();
        for (int32_t i = 0; i < 64; i++)
            if ((Units[i].unit_flags & 1) && Units[i].preq_tech == t) g.add(198, help_link(Units[i].name, 3, i));
        g.end();
    }
    if (section < 0 || section == 0) {
        Group g(out, section);
        for (int32_t i = 1; i < 134; i++) {
            const CFacility& f = Facility[i];
            if (f.preq_tech != t && f.free_tech != t) continue;
            std::string s;
            if (f.free_tech == t) s += "(";
            s += help_link(f.name, i > 0x45 ? 11 : 10, i);
            if (f.free_tech == t) s += std::string(" ") + label_get(311) + ")";
            g.add(68, s);
        }
        g.end();
    }
    if (section < 0 || section == 3) {
        // The social models and citizens write the line after every match without starting a
        // new one (as the original).
        std::string buf;
        for (int32_t field = 0; field < 4; field++)
            for (int32_t m = 0; m < 4; m++) {
                if (SocialField[field].soc_preq_tech[m] != t) continue;
                buf += section < 0 ? (buf.empty() ? std::string("^") + label_get(70) + ": " : ", ") : "^";
                buf += help_link(SocialField[field].soc_name[m], 12, 1 + field * 5 + m);
                out(buf.c_str());
            }
        buf.clear();
        for (int32_t i = 0; i < 10; i++) {
            if (Citizen[i].preq_tech != t) continue;
            buf += section < 0 ? (buf.empty() ? std::string("^") + label_get(307) + ": " : ", ") : "^";
            buf += Citizen[i].singular_name;
            out(buf.c_str());
        }
    }
    if (section < 0 || section == 1) {
        Group g(out, section);
        for (int32_t i = 0; i < 89; i++)
            if (Tech[i].preq_tech1 == t || Tech[i].preq_tech2 == t) g.add(76, help_link(Tech[i].name, 14, i));
        g.end();
    }
    if (section < 0 || section == 3) {
        g_heck_section = section;
        tech_heck(out, t);
        g_heck_section = -1;
    }
}

// help_any (0x44CB30), help_tech (0x44C880), help_topic (0x44CB60), help_project (0x44C9E0).
void GAMEAPI help_any() { Datalink_exec(&DatalinkWin, 0, 0); }
void GAMEAPI help_tech(int32_t tech_id) { Datalink_exec(&DatalinkWin, 0xE, tech_id); }
void GAMEAPI help_topic(int32_t kind, int32_t id) { Datalink_exec(&DatalinkWin, kind, id); }
void GAMEAPI help_project(int32_t project_id) { Datalink_exec(&DatalinkWin, 0xB, project_id + 0x46); }

void HelpText::add(const char* line) { store.push_back(line ? line : ""); }

void HelpText::finish() {
    lines.clear();
    for (std::string& str : store) {
        const char* t = str.c_str();
        int32_t mode = 0;
        if (*t == '^') {
            if (!t[1]) {
                mode = 3;
                t++;
            } else if (t[1] == '^') {
                mode = 2;
                t += t[2] == '^' ? 3 : 2;
            } else {
                mode = 1;
                t++;
            }
        }
        lines.push_back({const_cast<char*>(t), mode});
    }
}

bool help_load_text(HelpText* out, const char* file, const char* label, bool parse) {
    out->clear();
    if (text_open(file, label)) return false;
    for (;;) {
        char* line = text_get();
        if (!line || !*line || *line == '#') break;
        if (parse) {
            char buf[2048];
            buf[0] = 0;
            parse_string(line, buf);
            out->add(buf);
        } else {
            out->add(line);
        }
    }
    text_close();
    out->finish();
    return true;
}
