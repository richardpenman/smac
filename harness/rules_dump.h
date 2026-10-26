/* Rule tables as normalized text ("dumprules <file>" script command).
 *
 * Shared by the oracle (oracle.dll / hybrid.dll, built with SMACX_HYBRID so the engine's GLOBAL
 * names bind to the original process memory) and the headless runner (native storage). String
 * pointers are dereferenced, so the output does not depend on heap addresses. Header-only and
 * free of engine function calls (oracle.dll does not link the engine).
 */
#pragma once
#include <cstdint>
#include <cstdio>

#include "engine/globals.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/strings.h"

namespace rules_dump_detail {

inline void str(FILE* f, const char* s) {
    if (!s) {
        std::fputs("(null)", f);
        return;
    }
    std::fputc('"', f);
    for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
        if (*p < 0x20 || *p >= 0x7F || *p == '"' || *p == '\\') std::fprintf(f, "\\x%02X", *p);
        else std::fputc(*p, f);
    }
    std::fputc('"', f);
}

inline void ints(FILE* f, const void* data, size_t size) {
    const int32_t* v = (const int32_t*)data;
    for (size_t i = 0; i < size / 4; i++) std::fprintf(f, "%s%d", i ? " " : "", v[i]);
}

inline void bytes(FILE* f, const void* data, size_t size) {
    const uint8_t* b = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) std::fprintf(f, "%02X", b[i]);
}

}  // namespace rules_dump_detail

inline void rules_dump(FILE* f) {
    using namespace rules_dump_detail;
#define S(x) (std::fputc(' ', f), str(f, (x)))
    std::fprintf(f, "ExpansionEnabled %d GameLanguage %d TechValidCount %d TechCommerceCount %d\n", ExpansionEnabled,
                 GameLanguage, TechValidCount, TechCommerceCount);
    std::fprintf(f, "Rules ");
    ints(f, &Rules, sizeof(Rules));
    std::fprintf(f, "\nWorldBuilder ");
    ints(f, &WorldBuilder, sizeof(WorldBuilder));
    std::fprintf(f, "\nResInfo ");
    ints(f, &ResInfo, sizeof(ResInfo));
    std::fprintf(f, "\nAlphaIniPrefs ");
    ints(f, &AlphaIniPrefs, sizeof(AlphaIniPrefs));
    std::fprintf(f, "\nDefaultPrefs ");
    ints(f, &DefaultPrefs, sizeof(DefaultPrefs));
    std::fprintf(f, "\nGamePrefs %d %d %d %d\n", GamePreferences, GameMorePreferences, GameWarnings, GameRules);
    for (int i = 0; i < 89; i++) {
        const CTech& t = Tech[i];
        std::fprintf(f, "Tech[%d] %d", i, t.flags);
        S(t.name);
        std::fprintf(f, " %.8s %d %d %d %d %d %d\n", t.short_name, t.AI_growth, t.AI_tech, t.AI_wealth, t.AI_power,
                     t.preq_tech1, t.preq_tech2);
    }
    for (int i = 0; i < 134; i++) {
        const CFacility& x = Facility[i];
        std::fprintf(f, "Facility[%d]", i);
        S(x.name);
        S(x.effect);
        std::fprintf(f, " %d %d %d %d %d %d %d %d %d %d\n", x.pad, x.cost, x.maint, x.preq_tech, x.free_tech,
                     x.AI_fight, x.AI_growth, x.AI_tech, x.AI_wealth, x.AI_power);
    }
    for (int i = 0; i < 9; i++) {
        const CChassis& c = Chassis[i];
        std::fprintf(f, "Chassis[%d]", i);
        S(c.offsv1_name);
        S(c.offsv2_name);
        S(c.offsv_name_lrg);
        S(c.defsv1_name);
        S(c.defsv2_name);
        S(c.defsv_name_lrg);
        std::fprintf(f, " ");
        ints(f, &c.offsv1_gender, 12 * 4);
        // (sprite_* coordinates are set by the UI's sprite loading, not by the rules)
        std::fprintf(f, " %d %d %d %d %d %d %d\n", c.speed, c.triad, c.range, c.cargo, c.cost, c.missile, c.preq_tech);
    }
    for (int i = 0; i < 26; i++) {
        const CWeapon& w = Weapon[i];
        std::fprintf(f, "Weapon[%d]", i);
        S(w.name);
        S(w.name_short);
        std::fprintf(f, " %d %d %d %d %d %d\n", w.offense_value, w.icon, w.mode, w.cost, w.preq_tech, w.padding);
    }
    for (int i = 0; i < 14; i++) {
        const CArmor& a = Armor[i];
        std::fprintf(f, "Armor[%d]", i);
        S(a.name);
        S(a.name_short);
        std::fprintf(f, " %d %d %d %d %d %d\n", a.defense_value, a.mode, a.cost, a.padding1, a.preq_tech, a.padding2);
    }
    for (int i = 0; i < 4; i++) {
        const CReactor& r = Reactor[i];
        std::fprintf(f, "Reactor[%d]", i);
        S(r.name);
        S(r.name_short);
        std::fprintf(f, " %d %d\n", r.preq_tech, r.power);
    }
    for (int i = 0; i < 29; i++) {
        const CAbility& a = Ability[i];
        std::fprintf(f, "Ability[%d]", i);
        S(a.name);
        S(a.description);
        S(a.abbreviation);
        std::fprintf(f, " %d %d %d %d %d\n", a.cost, a.unk_1, a.flags, a.preq_tech, a.pad);
    }
    for (int i = 0; i < 10; i++) {
        const CCitizen& c = Citizen[i];
        std::fprintf(f, "Citizen[%d]", i);
        S(c.singular_name);
        S(c.plural_name);
        std::fprintf(f, " %d %d %d %d %d\n", c.preq_tech, c.obsol_tech, c.econ_bonus, c.psych_bonus, c.labs_bonus);
    }
    for (int i = 0; i < 20; i++) {
        const CTerraform& t = Terraform[i];
        std::fprintf(f, "Terraform[%d]", i);
        S(t.name);
        S(t.name_sea);
        std::fprintf(f, " %d %d %u %u %d", t.preq_tech, t.preq_tech_sea, t.bit, t.bit_incompatible, t.rate);
        S(t.shortcuts);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 30; i++) {
        std::fprintf(f, "Order[%d]", i);
        S(Order[i].order);
        S(Order[i].order_sea);
        S(Order[i].letter);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 16; i++) {
        std::fprintf(f, "Natural[%d]", i);
        S(Natural[i].name);
        S(Natural[i].name_short);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 4; i++) {
        std::fprintf(f, "Mandate[%d]", i);
        S(Mandate[i].name);
        S(Mandate[i].name_caps);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 11; i++) {
        std::fprintf(f, "Proposal[%d]", i);
        S(Proposal[i].name);
        S(Proposal[i].description);
        std::fprintf(f, " %d\n", Proposal[i].preq_tech);
    }
    for (int i = 0; i < 7; i++) {
        std::fprintf(f, "Might[%d]", i);
        S(Might[i].adj_start);
        S(Might[i].adj);
        std::fprintf(f, "\nMorale[%d]", i);
        S(Morale[i].name);
        S(Morale[i].name_lifecycle);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 4; i++) {
        const CSocialField& s = SocialField[i];
        std::fprintf(f, "SocialField[%d]", i);
        S(s.field_name);
        for (int j = 0; j < 4; j++) {
            std::fprintf(f, " | %d", s.soc_preq_tech[j]);
            S(s.soc_name[j]);
            std::fprintf(f, " ");
            ints(f, &s.soc_effect[j], sizeof(s.soc_effect[j]));
        }
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 11; i++) {
        std::fprintf(f, "SocialParam[%d]", i);
        S(SocialParam[i].set1);
        S(SocialParam[i].set2);
        std::fprintf(f, " ");
        bytes(f, SocialParam[i].padding, sizeof(SocialParam[i].padding));
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 4; i++) {
        std::fprintf(f, "ResName[%d]", i);
        S(ResName[i].name_singular);
        S(ResName[i].name_plural);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 3; i++) {
        std::fprintf(f, "Energy[%d]", i);
        S(Energy[i].abbrev);
        S(Energy[i].name);
        std::fprintf(f, "\nOffenseMode[%d]", i);
        S(OffenseMode[i].name);
        S(OffenseMode[i].hyphened);
        S(OffenseMode[i].abbrev);
        S(OffenseMode[i].letter);
        std::fprintf(f, "\nDefenseMode[%d]", i);
        S(DefenseMode[i].name);
        S(DefenseMode[i].hyphened);
        S(DefenseMode[i].abbrev);
        S(DefenseMode[i].letter);
        std::fprintf(f, "\nTriadName[%d]", i);
        S(TriadName[i]);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 6; i++) {
        const CTimeControl& t = TimeControl[i];
        std::fprintf(f, "TimeControl[%d]", i);
        S(t.name);
        std::fprintf(f, " %d %d %d %d %d %d %d\n", t.turn, t.base, t.unit, t.event, t.extra, t.refresh, t.accumulated);
        std::fprintf(f, "Difficulty[%d]", i);
        S(Difficulty[i]);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 41; i++) {
        std::fprintf(f, "BonusName[%d]", i);
        S(BonusName[i].key);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 8; i++) {
        std::fprintf(f, "Compass[%d]", i);
        S(Compass[i]);
        std::fprintf(f, " Repute[%d]", i);
        S(Repute[i]);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 9; i++) {
        std::fprintf(f, "Mood[%d]", i);
        S(Mood[i]);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 15; i++) {
        std::fprintf(f, "Plans[%d]", i);
        S(PlansShortName[i]);
        S(PlansFullName[i]);
        std::fprintf(f, "\n");
    }
    std::fprintf(f, "Labels %d\n", TextLabelCount);
    for (int i = 0; i < TextLabelCount && TextLabels; i++) {
        std::fprintf(f, "Label[%d]", i);
        S(TextLabels[i]);
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 8; i++) {
        std::fprintf(f, "MFaction[%d] ", i);
        bytes(f, &MFactions[i], sizeof(MFactions[i]));
        std::fprintf(f, "\n");
    }
    for (int i = 0; i < 64; i++) {
        std::fprintf(f, "Unit[%d] ", i);
        bytes(f, &Units[i], sizeof(Units[i]));
        std::fprintf(f, "\n");
    }
#undef S
}
