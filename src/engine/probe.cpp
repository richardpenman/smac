#include "engine/probe.h"

#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/base_misc.h"
#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/compat.h"
#include "engine/design.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/setup.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"

namespace {

inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0; }
inline bool is_alive(int32_t faction_id) { return (FactionStatus[1] & (uint8_t)(1 << (faction_id & 31))) != 0; }
inline uint32_t dstat(int32_t faction_id, int32_t other_id) {
    return game().factions[faction_id].diplo_status[other_id];
}

// Wrapped |dx| and |dy| between two tiles, combined by vector_dist (the original's inline form).
int32_t tile_vector_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    return vector_dist(dx, std::abs(y1 - y2));
}

// The original reads timeGetTime() & 1 at four places of probe() (Algorithmic Enhancement and
// Hunter-Seeker Algorithm luck). Under the harness these reads get the fast-forward clock, which
// advances by 1000 per read (harness/oracle fast_clock_sites.h), so the low bit is always 0.
// Deterministic reconstruction: always even. (Not one of the engine_logic_time() sites.)
inline bool probe_clock_odd() { return false; }

const char* infil_pcx(bool alien) { return alien ? "al_infil_sm.pcx" : "infil_sm.pcx"; }
const char* capture_pcx(bool alien) { return alien ? "al_cap_sm.pcx" : "capture_sm.pcx"; }

// Monument event `e` of a faction as raw fields (the original also reads past events[20]).
inline int32_t& mon_field(int32_t faction_id, int32_t event, int32_t offset) {
    uint8_t* p = reinterpret_cast<uint8_t*>(&Monuments[faction_id]) + event * (int32_t)sizeof(MonumentEvent) + offset;
    return *reinterpret_cast<int32_t*>(p);
}

// First port of faction_id reachable by a sea probe at the probe's position, or -1.
int32_t sea_return_base(int32_t veh_id, int32_t faction_id) {
    for (int32_t i = 0; i < game().base_count; i++) {
        if ((uint8_t)game().bases[i].faction_id != faction_id || !is_port(i, 0)) continue;
        if (is_ocean(game().vehs[veh_id].x, game().vehs[veh_id].y) &&
            !base_on_sea(i, region_at(game().vehs[veh_id].x, game().vehs[veh_id].y)))
            continue;
        return i;
    }
    return -1;
}

}  // namespace

// ---- small helpers -------------------------------------------------------------------------------------

int32_t GAMEAPI vulnerable(int32_t faction_id, int32_t x, int32_t y) {
    for (int32_t i = 0; i < game().base_count; i++) {
        if ((uint8_t)game().bases[i].faction_id == faction_id && base_fac(i, 1)) {
            int32_t dx = std::abs(x - game().bases[i].x);
            if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
            int32_t dy = std::abs(y - game().bases[i].y);
            int32_t a = std::abs(dx), b = std::abs(dy);
            int32_t big = a <= b ? b : a;
            int32_t small = b <= a ? b : a;
            return big - ((((a + b) >> 1) - small + 1) >> 1);
        }
    }
    return 12;
}

int32_t GAMEAPI tile_distance(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    int32_t dy = std::abs(y1 - y2);
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t big = a <= b ? b : a;
    int32_t small = b <= a ? b : a;
    return big - ((((a + b) >> 1) - small + 1) >> 1);
}

void GAMEAPI say_label(int32_t id) { std::strcat(StrBuffer, label_get(id)); }

void GAMEAPI space() { std::strcat(StrBuffer, " "); }

void GAMEAPI parse_set(int32_t gender, int32_t plural) {
    GenderDefault = gender;
    PluralDefault = plural;
}

const char* GAMEAPI get_adjective(int32_t faction_id) { return game().mfactions[faction_id].adj_name_faction; }

const char* GAMEAPI get_noun(int32_t faction_id) {
    GenderDefault = game().mfactions[faction_id].noun_gender;
    PluralDefault = game().mfactions[faction_id].is_noun_plural;
    return game().mfactions[faction_id].noun_faction;
}

const char* GAMEAPI get_name(int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    return game().mfactions[faction_id].name_leader;
}

const char* GAMEAPI get_title(int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    return game().mfactions[faction_id].title_leader;
}

const char* GAMEAPI get_pact_hood(int32_t faction_id, int32_t faction_id_with) {
    GenderDefault = 0;
    PluralDefault = 0;
    int32_t id = 0xCE;
    if (!game().mfactions[faction_id].is_leader_female || !game().mfactions[faction_id_with].is_leader_female)
        id = 0xCD;
    return label_get(id);
}

int32_t GAMEAPI popp_2(const char* label, const char* pcx, int32_t a3) {
    return popp(ScriptFileName, label, 0, pcx, a3);
}

int32_t GAMEAPI NetMsg_pop_2(const char* label, const char* pcx) { return NetMsg_pop(&NetMsg, label, -5000, 0, pcx); }

void GAMEAPI say_num(int32_t value) {
    char buf[80];
    crt_itoa(value, buf, 10);
    std::strcat(StrBuffer, buf);
}

void GAMEAPI say_tech(int32_t tech_id, int32_t with_category) { say_tech_2(StrBuffer, tech_id, with_category); }

int32_t GAMEAPI spying(int32_t faction_id) {
    if (!(game().factions[CurrentPlayerFaction].diplo_status[faction_id] & DIPLO_HAVE_INFILTRATOR) &&
        (SecretProjects[FAC_EMPATH_GUILD - SP_ID_First] < 0 ||
         (uint8_t)game().bases[SecretProjects[FAC_EMPATH_GUILD - SP_ID_First]].faction_id != CurrentPlayerFaction) &&
        (CurrentPlayerFaction != GovernorFaction || (game().mfactions[faction_id].rule_flags & 0x80)))
        return 0;
    return 1;
}

int32_t GAMEAPI steal_energy(int32_t base_id) {
    int32_t owner = (uint8_t)game().bases[base_id].faction_id;
    if (game().factions[owner].energy_credits < 1) return 0;
    return (game().bases[base_id].pop_size * game().factions[owner].energy_credits) /
           (game().factions[owner].pop_total + 1);
}

int32_t GAMEAPI steal_tech(int32_t faction_id, int32_t faction_id_from, int32_t is_steal) {
    if (faction_id == 0 || faction_id_from == 0 ||
        (MultiplayerActive && is_human(faction_id) && faction_id != CurrentPlayerFaction))
        return 0;
    int32_t tech_id = tech_pick(faction_id, 0, faction_id_from, is_steal ? "STEALTECH" : "ACQUIRETECH");
    if (tech_id < 0) tech_id = 9999;
    if (MultiplayerActive && faction_id == CurrentPlayerFaction) {
        message_data(0x244C, 0, faction_id, tech_id, faction_id_from, 0);
        return tech_id == 9999;
    }
    if (faction_id_from == CurrentPlayerFaction && tech_id != 9999) {
        GenderDefault = game().mfactions[faction_id].noun_gender;
        PluralDefault = game().mfactions[faction_id].is_noun_plural;
        parse_says(0, game().mfactions[faction_id].noun_faction, -1, -1);
        StrBuffer[0] = 0;
        say_tech_2(StrBuffer, tech_id, 0);
        parse_says(1, StrBuffer, -1, -1);
        NetMsg_pop(&NetMsg, "STOLETECH", 5000, 0, nullptr);
    }
    tech_achieved(faction_id, tech_id, faction_id_from, 0);
    if (!is_human(faction_id) && tech_id != 9999) bases_reset(-1, faction_id, 0);
    return tech_id == 9999;
}

void GAMEAPI net_treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t wait) {
    if (!MultiplayerActive) {
        treaty_on(faction_id, faction_id_with, status);
        return;
    }
    log_say_values("Sending TREATY_ON", faction_id, faction_id_with, (int32_t)status);
    message_data(0x2441, 0, faction_id, faction_id_with, (int32_t)status, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x441);
}

void GAMEAPI net_treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t wait) {
    if (!MultiplayerActive) {
        treaty_off(faction_id, faction_id_with, status);
        return;
    }
    log_say_values("Sending TREATY_OFF", faction_id, faction_id_with, (int32_t)status);
    message_data(0x2442, 0, faction_id, faction_id_with, (int32_t)status, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x442);
}

void GAMEAPI net_set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add, int32_t wait) {
    if (!MultiplayerActive) {
        set_treaty(faction_id, faction_id_with, status, add);
        return;
    }
    log_say_values("Sending SET_TREATY", faction_id, faction_id_with, (int32_t)status);
    message_data(0x2443, 0, faction_id, faction_id_with, (int32_t)status, add);
    if (wait) NetDaemon_await_diplo(&NetState, 0x443);
}

void GAMEAPI net_tech(int32_t faction_id, int32_t tech_id, int32_t from, int32_t wait) {
    if (!MultiplayerActive) {
        tech_achieved(faction_id, tech_id, from, 0);
        return;
    }
    log_say_values("Sending TECH", faction_id, tech_id, from);
    message_data(0x244B, 0, faction_id, tech_id, from, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 1099);
}

// ---- mind_control, corner_market, success_rates ------------------------------------------------------

int32_t GAMEAPI mind_control(int32_t base_id, int32_t faction_id, int32_t is_corner_market) {
    BASE& b = game().bases[base_id];
    int32_t y = b.y;
    int32_t owner = (uint8_t)b.faction_id;
    int32_t x = b.x;
    int32_t dist = 12;
    for (int32_t i = 0; i < game().base_count; i++) {
        if ((uint8_t)game().bases[i].faction_id == owner && has_fac(FAC_HEADQUARTERS, i, 0)) {
            dist = tile_vector_dist(x, y, game().bases[i].x, game().bases[i].y);
            if (dist < 1) {
                if (!is_corner_market) return -1;
                dist = 1;
            }
            break;
        }
    }
    if (base_fac(base_id, 22)) dist *= 2;
    if (base_fac(base_id, 2)) dist /= 2;
    if (base_fac(base_id, 23)) dist /= 2;
    if (b.nerve_staple_turns_left) dist /= 2;
    int32_t s = stack_fix(veh_at(b.x, b.y));
    int32_t c1 = stack_check(s, 2, 1, -1, -1);
    int32_t c0 = stack_check(s, 2, 0, -1, -1);
    int32_t pol = stack_check(s, 6, 0x1000, -1, -1);
    Faction& o = game().factions[owner];
    int32_t cost = ((c1 + c0) * (pol + 1) + game().factions[faction_id].mind_control_total / 4 + b.pop_size) *
                   ((o.corner_market_cost + 1200 + o.energy_credits) / (dist + 4));
    if (!is_human(faction_id) && is_human(owner) && o.diff_level > 3) cost = (cost * 3) / o.diff_level;
    uint32_t ds = dstat(faction_id, owner);
    if (!is_corner_market) {
        if (ds & 1) cost *= 2;
    } else {
        if (ds & 1) cost /= 2;
        if (ds & 2) cost /= 2;
        int32_t a = o.tech_commerce_bonus;
        int32_t c = game().factions[faction_id].tech_commerce_bonus;
        cost = ((a * a + 1) * cost) / (c * c + 1);
    }
    if ((uint8_t)b.faction_id_former == faction_id) cost /= 2;
    if (b.state_flags & BSTATE_DRONE_RIOTS_ACTIVE) cost /= 2;
    if (b.state_flags & BSTATE_GOLDEN_AGE_ACTIVE) cost *= 2;
    uint32_t back = dstat(owner, faction_id);
    if (back & 0x40000) return cost * 2;
    if (back & 0x20) cost += cost / 2;
    return cost;
}

int32_t GAMEAPI corner_market(int32_t faction_id) {
    int32_t sum = 0;
    if (game().base_count <= 0) return 1000;
    for (int32_t i = 0; i < game().base_count; i++) {
        int32_t o = (uint8_t)game().bases[i].faction_id;
        if (o != faction_id && (!(dstat(o, faction_id) & 1) || !(dstat(o, faction_id) & 0x2000000)))
            sum += mind_control(i, faction_id, 1);
    }
    return sum > 999 ? sum : 1000;
}

int32_t GAMEAPI success_rates(int32_t n, int32_t morale, int32_t difficulty, int32_t base_id) {
    char buf[80];
    StrBuffer[0] = 0;
    if (morale < 1) morale = 1;
    int32_t chance = difficulty;
    if (difficulty < 0) {
        std::strcat(StrBuffer, "100%");
    } else {
        int32_t bonus = base_id == -1 ? 0 : (base_fac(base_id, 34) ? 2 : 0);
        int32_t p = game().factions[ProbeTargetFactionID].SE_probe + bonus;
        p = p < -2 ? -2 : (p > 0 ? 0 : p);
        int32_t fail = (difficulty * 100) / (morale / 2 - p + 1);
        if (ProbeHasAlgoEnhancement && !ProbeTargetHasHSA) fail /= 2;
        chance = 100 - fail;
        if (ProbeTargetHasHSA) chance /= 2;
        crt_itoa(chance, buf, 10);
        std::strcat(StrBuffer, buf);
        std::strcat(StrBuffer, "%");
        bonus = base_id == -1 ? 0 : (base_fac(base_id, 34) ? 2 : 0);
        p = game().factions[ProbeTargetFactionID].SE_probe + bonus;
        p = p < -2 ? -2 : (p > 0 ? 0 : p);
        int32_t lose = ((difficulty * 5 + 5) * 20) / (morale - p);
        if (ProbeHasAlgoEnhancement && !ProbeTargetHasHSA) lose /= 2;
        int32_t survive = 100 - lose;
        // BUG(original): halves the success chance a second time instead of the survival chance.
        COMPAT_ORIGINAL(probe_survival_halved);
        if (ProbeTargetHasHSA) chance /= 2;
        if (survive != chance) {
            std::strcat(StrBuffer, ", ");
            crt_itoa(survive, buf, 10);
            std::strcat(StrBuffer, buf);
            std::strcat(StrBuffer, "%");
        }
    }
    parse_says(n, StrBuffer, -1, -1);
    return chance;
}

// ---- probe ----------------------------------------------------------------------------------------------

int32_t GAMEAPI probe(int32_t veh_id, int32_t base_id, int32_t veh_id_tgt, int32_t commit) {
    int32_t busted_action = 0;  // action shown to a human target that caught the probe
    int32_t refined = 0;  // the AI choice has been refined once (headquarters checks)
    int32_t freed = -1;  // captured leader to free
    int32_t f = game().vehs[veh_id].faction_id;
    int32_t t = base_id < 0 ? game().vehs[veh_id_tgt].faction_id : (uint8_t)game().bases[base_id].faction_id;
    ProbeTargetFactionID = t;
    bool alien = (game().mfactions[f].rule_flags & 0x80) != 0;
    bool human = is_human(f);
    auto shown = [&]() { return f == CurrentPlayerFaction && human && (VehAttackFlags & 1); };
    int32_t tx = base_id < 0 ? game().vehs[veh_id_tgt].x : game().bases[base_id].x;
    int32_t ty = base_id < 0 ? game().vehs[veh_id_tgt].y : game().bases[base_id].y;
    // amphibious probes only between land and sea
    if (map_is_ocean(game().vehs[veh_id].x, game().vehs[veh_id].y)) {
        if (!map_is_ocean(tx, ty) && veh_triad(veh_id) != 1 && !has_abil(game().vehs[veh_id].unit_id, 8)) {
            if (f == CurrentPlayerFaction && commit && human && (VehAttackFlags & 1)) {
                parse_say(0, Ability[abil_index(8)].name, -1, -1);
                NetMsg_pop(&NetMsg, "PROBEABILAMPHIBIOUS", 5000, 0, nullptr);
            }
            return 0;
        }
    }
    if (map_is_ocean(tx, ty) && !map_is_ocean(game().vehs[veh_id].x, game().vehs[veh_id].y) &&
        !has_abil(game().vehs[veh_id].unit_id, 8)) {
        if (f == CurrentPlayerFaction && commit && human && (VehAttackFlags & 1)) {
            parse_say(0, Ability[abil_index(8)].name, -1, -1);
            NetMsg_pop(&NetMsg, "PROBEABILAMPHIBIOUS2", 5000, 0, nullptr);
        }
        return 0;
    }
    // Hunter-Seeker Algorithm
    ProbeTargetHasHSA = 0;
    ProbeHasAlgoEnhancement = 0;
    if (!has_abil(game().vehs[veh_id].unit_id, 0x10000000)) {
        if (SecretProjects[FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First] >= 0 &&
            (uint8_t)game().bases[SecretProjects[FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First]].faction_id == t) {
            if (!commit && human) return 0;
            if (!(VehAttackFlags & 2) || !commit) return 1;
            if (f == CurrentPlayerFaction) {
                parse_says(0, game().mfactions[t].adj_name_faction, -1, -1);
                NetMsg_pop(&NetMsg, "HUNTERSEEKER", -5000, 0, "huntsk_sm.pcx");
            } else if (t == CurrentPlayerFaction) {
                parse_says(0, game().mfactions[f].adj_name_faction, -1, -1);
                NetMsg_pop(&NetMsg, "HUNTERSEEKER2", -5000, 0, "huntsk_sm.pcx");
            }
            kill(veh_id);
            return 1;
        }
    } else {
        ProbeHasAlgoEnhancement = 1;
        if (SecretProjects[FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First] >= 0 &&
            (uint8_t)game().bases[SecretProjects[FAC_HUNTER_SEEKER_ALGORITHM - SP_ID_First]].faction_id == t) {
            if (!commit && human) return 0;
            if (!(VehAttackFlags & 2) || !commit) return 1;
            ProbeTargetHasHSA = 1;
        }
    }
    int32_t has_plague_tech = 0;  // knows a technology with flag 0x10 (genetic warfare)
    int32_t plague_techs = 0;  // technologies with flag 0x20 / those the target knows
    int32_t plague_known = 0;
    int32_t tech = 0;
    for (; tech < 89; tech++) {
        if ((Tech[tech].flags & 0x10) && has_tech(tech, f)) has_plague_tech = 1;
        if (Tech[tech].flags & 0x20) {
            plague_techs++;
            if (has_tech(tech, t)) plague_known++;
        }
    }
    int32_t action;
    int32_t risk = 0;
    int32_t sab = 0;
    char production[256];
    production[0] = 0;
    int32_t frame = 0;
    int32_t dest = 0;
    int32_t cost = 0;
    int32_t tgt_mod = 0;
    if (base_id < 0) {
        action = -1;
    } else if (shown()) {
        parse_says(1, game().mfactions[t].adj_name_faction, -1, -1);
        uint32_t items = 0x1C;
        if (!(dstat(f, t) & 0x1000)) items |= 1;
        if (Rules.tgl_probe_steal_tech) items |= 2;
        items |= base_fac(base_id, 1) ? 0x20 : 0x40;
        if (has_plague_tech) items |= 0x80;
        if (ExpansionEnabled && !MultiplayerActive && base_fac(base_id, 1)) {
            bool any = false;
            for (int32_t k = 0; k < 7; k++)
                if (mon_field(t, 15 + k, 0x10)) any = true;
            if (any) items |= 0x100;
        }
        action = ui_probe_menu(t, items, alien);
        if (action < 0) return 0;
        game().vehs[veh_id].probe_action = (uint8_t)action;
    } else {
        if (!human) goto ai_choose;
        action = game().vehs[veh_id].probe_action & 7;
    }
dispatch:
    while (true) {
        if (action == 0 && (VehAttackFlags & 2)) {
            if (!(dstat(f, t) & 0x1000)) {
                set_treaty(f, t, 0x1000, 1);
                if (f == CurrentPlayerFaction) report_intel(t);
            }
            if (t == CurrentPlayerFaction) {
                PluralDefault = 0;
                GenderDefault = game().mfactions[f].is_leader_female;
                parse_says(0, game().mfactions[f].title_leader, -1, -1);
                GenderDefault = game().mfactions[f].is_leader_female;
                PluralDefault = 0;
                parse_says(1, game().mfactions[f].name_leader, -1, -1);
                PluralDefault = game().mfactions[f].is_noun_plural;
                GenderDefault = game().mfactions[f].noun_gender;
                parse_says(2, game().mfactions[f].noun_faction, -1, -1);
                NetMsg_pop(&NetMsg, "DETECTINFILTRATE", -5000, 0, infil_pcx(alien));
            }
        }
        if (shown() && ((uint8_t)dstat(f, t) & 7) && (!(dstat(t, f) & 1) || !(dstat(t, f) & 0x2000000)) && action > 0) {
            PluralDefault = 0;
            GenderDefault = game().mfactions[t].is_leader_female;
            parse_says(0, game().mfactions[t].title_leader, -1, -1);
            GenderDefault = game().mfactions[t].is_leader_female;
            PluralDefault = 0;
            parse_says(1, game().mfactions[t].name_leader, -1, -1);
            if (!popp(ScriptFileName, "RISKEXCUSE", 0, infil_pcx(alien), 0)) return 0;
        }
        risk = 0;
        if (action != 1) break;
        // procure research data
        if ((game().bases[base_id].state_flags & BSTATE_RESEARCH_DATA_STOLEN) && (risk = 1, shown())) {
            parse_says(0, game().bases[base_id].name, -1, -1);
            success_rates(1, morale_veh(veh_id, 1, 0), -1, base_id);
            if (!success_rates(2, morale_veh(veh_id, 1, 0), 1, base_id)) {
                NetMsg_pop(&NetMsg, "ADVDECIPHER1", -5000, 0, nullptr);
                return 0;
            }
            if (!popp(ScriptFileName, "ADVDECIPHER", 0, infil_pcx(alien), 0)) return 0;
        }
        int32_t r;
        if (!MultiplayerActive || !human) {
            r = game_rand() % 89;
        } else {
            game_reseed(MapRandomSeed + CurrentTurn + f);
            r = game_random(0, 89);
            game_reseed(engine_logic_time());
        }
        {
            int32_t k = 0;
            for (; k < 89; k++) {
                tech = (r + tech) % 89;
                if (has_tech(tech, t) && !has_tech(tech, f)) break;
            }
            if (k >= 89) tech = 9999;
        }
        if (f == CurrentPlayerFaction && human) {
            int32_t level = risk;
            success_rates(1, morale_veh(veh_id, 1, 0), level, base_id);
            if (success_rates(2, morale_veh(veh_id, 1, 0), level + 1, base_id)) {
                int32_t choice;
                if (!(VehAttackFlags & 2)) {
                    choice = popp(ScriptFileName, "DECIPHER", 0x40, infil_pcx(alien), 0);
                    if (choice < 0) return 0;
                    game().vehs[veh_id].probe_action |= (uint8_t)(choice << 3);
                } else {
                    choice = (game().vehs[veh_id].probe_action & 8) >> 3;
                }
                if (choice == 1) {
                    tech = -1;
                    risk = level + 1;
                    goto resolve;
                }
            }
        }
        if (tech != 9999 || !(game().factions[f].player_flags & PFLAG_MAP_REVEALED)) goto resolve;
        if (human) {
            if (f != CurrentPlayerFaction || !(VehAttackFlags & 2)) goto resolve;
            NetMsg_pop(&NetMsg, "STOLENOTHING", -5000, 0, nullptr);
            return 0;
        }
        action = (dstat(f, t) & 0x30) ? 2 : 0;
    }
    if (action == 3) {
        // drain energy reserves
        if (!(game().bases[base_id].state_flags & BSTATE_ENERGY_RESERVES_DRAINED) ||
            (risk = 1, f != CurrentPlayerFaction) || !human || !(VehAttackFlags & 1))
            goto resolve;
        parse_says(0, game().bases[base_id].name, -1, -1);
        success_rates(1, morale_veh(veh_id, 1, 0), -1, base_id);
        if (!success_rates(2, morale_veh(veh_id, 1, 0), 1, base_id)) {
            NetMsg_pop(&NetMsg, "ADVENERGY1", -5000, 0, nullptr);
            return 0;
        }
        if (popp(ScriptFileName, "ADVENERGY", 0, infil_pcx(alien), 0)) goto resolve;
        return 0;
    }
    if (action == 2) {
        // activate sabotage virus
        sab = 0;
        production[0] = 0;
        int32_t item = game().bases[base_id].queue_items[0];
        int32_t item_cost;
        if (item < 0) {
            std::strcat(production, Strings_get(&StringTable, Facility[-item].name));
            item_cost = Facility[-game().bases[base_id].queue_items[0]].cost;
        } else {
            std::strcat(production, Units[item].name);
            item_cost = Units[game().bases[base_id].queue_items[0]].cost;
        }
        int32_t r;
        if (!MultiplayerActive || !human) {
            int32_t m = morale_veh(veh_id, 1, 0) / 2;
            r = (m == -1 || m + 1 < 0) ? 0 : game_rand() % (m + 2);
        } else {
            game_reseed(MapRandomSeed + CurrentTurn + f);
            r = game_random(0, morale_veh(veh_id, 1, 0) / 2 + 2);
            game_reseed(engine_logic_time());
        }
        int32_t cf = cost_factor(f, 1, -1);
        if (game().bases[base_id].minerals_accumulated < cf * item_cost || r != 0) {
            int32_t rr = game_rand() % 69;
            bool chosen = false;
            for (int32_t k = 1; k < 70; k++) {
                int32_t fid = (k + rr) % 69 + 1;
                if (fid == 1) {
                    if (stack_check(veh_at(game().bases[base_id].x, game().bases[base_id].y), 2, 5, -1, -1)) {
                        sab = 98;
                        chosen = true;
                        break;
                    }
                } else if ((fid != 26 || !map_is_ocean(game().bases[base_id].x, game().bases[base_id].y)) && fid < 65) {
                    if (base_fac(base_id, fid)) {
                        sab = fid;
                        chosen = true;
                        break;
                    }
                }
            }
            if (!chosen && !human) {
                action = game().bases[base_id].pop_size < 4 ? 0 : 4;
                goto dispatch;
            }
        }
        if (!human) {
            if (morale_veh(veh_id, 1, 0) > 4) {
                if (stack_check(veh_at(game().bases[base_id].x, game().bases[base_id].y), 2, 5, -1, -1)) {
                    sab = 98;
                    risk = 1;
                } else if (base_fac(base_id, 5)) {
                    sab = 5;
                    risk = 1;
                } else if (base_fac(base_id, 4)) {
                    sab = 4;
                    risk = 1;
                } else if (base_fac(base_id, 2)) {
                    sab = 2;
                    risk = 1;
                } else if (base_fac(base_id, 27)) {
                    sab = 27;
                    risk = 1;
                }
            }
            goto resolve;
        }
        success_rates(1, morale_veh(veh_id, 1, 0), 0, base_id);
        int32_t choice;
        if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
            if (!success_rates(2, morale_veh(veh_id, 1, 0), 1, base_id)) choice = 0;
            else choice = popp(ScriptFileName, "ADVVIRUS", 0, infil_pcx(alien), 0);
            game().vehs[veh_id].probe_action |= (uint8_t)(choice << 3);
        } else {
            choice = (game().vehs[veh_id].probe_action & 8) >> 3;
        }
        if (choice == 0) goto resolve;
        risk = 1;
        if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
            bool buster = stack_check(veh_at(game().bases[base_id].x, game().bases[base_id].y), 2, 5, -1, -1) != 0;
            sab = ui_probe_sabotage_menu(base_id, buster, production);
            if (sab < 0) return 0;
            game().vehs[veh_id].probe_sabotage_id = (uint8_t)sab;
        } else {
            sab = (int8_t)game().vehs[veh_id].probe_sabotage_id;
        }
        if (sab != 99) {
            if (!base_fac(base_id, 1)) {
                if (sab != 4 && sab != 5) goto resolve;
                if (shown()) {
                    parse_says(0, game().bases[base_id].name, -1, -1);
                    parse_say(1, Facility[sab].name, -1, -1);
                    success_rates(2, morale_veh(veh_id, 1, 0), 2, base_id);
                    if (!popp(ScriptFileName, "MILVIRUS", 0, infil_pcx(alien), 0)) return 0;
                }
            } else if (shown()) {
                parse_says(0, game().bases[base_id].name, -1, -1);
                parse_says(1, game().mfactions[t].adj_name_faction, -1, -1);
                success_rates(2, morale_veh(veh_id, 1, 0), 2, base_id);
                if (!popp(ScriptFileName, "HQVIRUS", 0, infil_pcx(alien), 0)) return 0;
            }
            risk = 2;
        }
        goto resolve;
    }
    if (action == 5) {
        // assassinate prominent researchers
        risk = 1;
        if (!success_rates(1, morale_veh(veh_id, 1, 0), 1, base_id)) {
            if (shown()) NetMsg_pop(&NetMsg, "NOASSASSIN", -5000, 0, nullptr);
        } else {
            if (!shown()) goto resolve;
            if (popp(ScriptFileName, "ASSASSIN", 0, infil_pcx(alien), 0)) goto resolve;
        }
        return 0;
    }
    if (action == 8) {
        // free captured faction leader
        risk = 1;
        if (!success_rates(1, morale_veh(veh_id, 1, 0), 1, base_id)) {
            if (f == CurrentPlayerFaction && human) NetMsg_pop(&NetMsg, "NOFREEWILLY", -5000, 0, nullptr);
        } else {
            if (!shown()) goto resolve;
            if (popp(ScriptFileName, "FREEWILLY", 0, infil_pcx(alien), 0)) goto resolve;
        }
        return 0;
    }
    if (action == -1) {
        // mind control unit
        if (stack_check(veh_id_tgt, 1, -1, -1, -1) > 1) return 0;
        if (veh_triad(veh_id_tgt) == 2 && !has_abil(game().vehs[veh_id].unit_id, 0x20)) {
            if (f == CurrentPlayerFaction && commit && human && (VehAttackFlags & 1)) {
                parse_say(0, Ability[abil_index(0x20)].name, -1, -1);
                NetMsg_pop(&NetMsg, "PROBEABILSUPERIORITY", 5000, 0, nullptr);
            }
            return 0;
        }
        int32_t unit = game().vehs[veh_id_tgt].unit_id;
        if (t == 0 || (unit <= 63 && (Weapon[Units[unit].weapon_id].offense_value < 0 || unit == 15))) {
            if (f == CurrentPlayerFaction && commit && human && (VehAttackFlags & 1))
                NetMsg_pop(&NetMsg, "ALIENPROBE", -5000, 0, nullptr);
            return 0;
        }
        int32_t uy = game().vehs[veh_id_tgt].y;
        int32_t ux = game().vehs[veh_id_tgt].x;
        int32_t dist = 12;
        for (int32_t i = 0; i < game().base_count; i++) {
            if ((uint8_t)game().bases[i].faction_id == t && has_fac(FAC_HEADQUARTERS, i, 0)) {
                dist = tile_vector_dist(ux, uy, game().bases[i].x, game().bases[i].y);
                break;
            }
        }
        int32_t home = game().vehs[veh_id_tgt].home_base_id;
        if (home >= 0 && base_fac(home, 23)) dist /= 2;
        cost = ((game().factions[t].energy_credits + 800) / (dist + 2)) * Units[game().vehs[veh_id_tgt].unit_id].cost;
        if (!human && is_human(t) && game().factions[t].diff_level > 3)
            cost = (cost * 3) / game().factions[t].diff_level;
        if (game().mfactions[t].rule_flags & 0x800) {
            if (f == CurrentPlayerFaction && human && commit && (VehAttackFlags & 1)) {
                parse_says(0, game().mfactions[t].adj_name_faction, -1, -1);
                NetMsg_pop(&NetMsg, "MINDIMMUNITY2", -5000, 0, nullptr);
            }
            return 0;
        }
        for (int32_t i = 0; i < game().mfactions[f].faction_bonus_count; i++)
            if (game().mfactions[f].faction_bonus_id[i] == 16)
                cost = (cost * game().mfactions[f].faction_bonus_val1[i]) / 100;
        if (SecretProjects[FAC_NETHACK_TERMINUS - SP_ID_First] >= 0 &&
            (uint8_t)game().bases[SecretProjects[FAC_NETHACK_TERMINUS - SP_ID_First]].faction_id == f)
            cost = (cost * 3) / 4;
        int32_t p = game().factions[t].SE_probe;
        if ((uint32_t)(p + 6) <= 15) {
            switch (p) {
            case 0: break;
            case 1: cost += cost / 2; break;
            case 2: cost *= 2; break;
            case -6:
            case -5:
            case -4:
            case -3:
            case -2: cost /= 2; break;
            case -1: cost += -(cost / 4); break;
            default:
                if (f == CurrentPlayerFaction && human && commit && (VehAttackFlags & 1)) {
                    parse_says(0, game().mfactions[t].adj_name_faction, -1, -1);
                    NetMsg_pop(&NetMsg, "IMMUNITY", -5000, 0, nullptr);
                }
                return 0;
            }
        }
        int32_t c = cost;
        if (has_abil(game().vehs[veh_id_tgt].unit_id, 0x1000)) {
            c *= 2;
            cost = c;
        }
        int32_t tplan = Units[game().vehs[veh_id_tgt].unit_id].plan;
        if (tplan != 8 && tplan != 9) cost = c / 2;
        parse_says(0, Units[game().vehs[veh_id_tgt].unit_id].name, -1, -1);
        if (!human || (game().vehs[veh_id].state & VSTATE_ON_ALERT)) {
            // AI: worth it?
            Faction& F = game().factions[f];
            int32_t w = ((F.AI_power - F.AI_wealth) + 2) * cost * 4;
            if (is_human(t)) w /= 2;
            int32_t ws = weap_strat(Units[game().vehs[veh_id_tgt].unit_id].weapon_id, f);
            int32_t as = arm_strat(Units[game().vehs[veh_id_tgt].unit_id].armor_id, f);
            int32_t region = map_tile(game().vehs[veh_id_tgt].x, game().vehs[veh_id_tgt].y)->region;
            if (F.region_total_bases[region] &&
                (uint32_t)(F.region_force_rating[region] >> 1) + F.region_total_combat_units[region] <
                    game().factions[t].region_force_rating[region])
                w /= 2;
            if (game().factions[t].region_total_combat_units[region] < F.region_force_rating[region]) w *= 2;
            if (F.best_weapon_value < ws) w /= 2;
            if (F.best_armor_value < as) w /= 2;
            if (F.best_land_speed < (int32_t)Chassis[Units[game().vehs[veh_id_tgt].unit_id].chassis_id].speed)
                w = (w * 2) / 3;
            uint32_t ds = dstat(f, t);
            if (ds & 2) w *= 2;
            if (!(ds & 0x20) && !(GameState & 0x200) && !at_climax(f) && (ds & 0x1000000) && (ds & 6) &&
                whose_territory(f, game().vehs[veh_id_tgt].x, game().vehs[veh_id_tgt].y, nullptr, 0) != f)
                return 0;
            if (ws < F.best_weapon_value && as < F.best_armor_value) w *= 2;
            if (F.best_weapon_value < ws) w /= 2;
            MAP* sq = map_tile(game().vehs[veh_id_tgt].x, game().vehs[veh_id_tgt].y);
            if ((sq->val3 & 0xC0) > 0x7F || ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) ||
                (sq->items & 0x800))
                w /= 2;
            if ((uint8_t)Units[game().vehs[veh_id_tgt].unit_id].plan < 2) w /= 2;
            else w *= 3;
            if (F.energy_credits < w || cost > F.energy_credits) return 0;
        }
        if (!commit) return 1;
        if (human) {
            int32_t choice;
            if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
                parse_num(0, cost);
                parse_num(1, game().factions[f].energy_credits);
                success_rates(1, morale_veh(veh_id, 1, 0), 0, -1);
                success_rates(2, morale_veh(veh_id, 1, 0), 1, -1);
                choice = ui_probe_confirm_menu("SUBVERT", "SUBVERTMENU", cost <= game().factions[f].energy_credits);
                if (choice == 0) return 0;
                game().vehs[veh_id].probe_action |= (uint8_t)((choice + 31) * 8);
            } else {
                choice = ((game().vehs[veh_id].probe_action & 8) != 0) + 1;
            }
            risk = choice - 1;
        }
        if (VehAttackFlags & 2) {
            game().factions[f].energy_credits -= cost;
            if (f == CurrentPlayerFaction) Console_update_data(&MainConsole, 0);
        }
        goto resolve;
    }
    if (action != 6) goto resolve;
    {
        // mind control city
        cost = mind_control(base_id, f, 0);
        if (cost < 0) {
            if (human) return 0;
            action = 1;
            goto dispatch;
        }
        if (game().mfactions[t].rule_flags & 0x800) {
            if (!human) goto ai_choose;
            if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
                parse_says(0, game().mfactions[t].adj_name_faction, -1, -1);
                NetMsg_pop(&NetMsg, "MINDIMMUNITY", -5000, 0, nullptr);
            }
            return 0;
        }
        for (int32_t i = 0; i < game().mfactions[f].faction_bonus_count; i++)
            if (game().mfactions[f].faction_bonus_id[i] == 16)
                cost = (cost * game().mfactions[f].faction_bonus_val1[i]) / 100;
        if (SecretProjects[FAC_NETHACK_TERMINUS - SP_ID_First] >= 0 &&
            (uint8_t)game().bases[SecretProjects[FAC_NETHACK_TERMINUS - SP_ID_First]].faction_id == f)
            cost = (cost * 3) / 4;
        int32_t p = game().factions[t].SE_probe;
        if (base_fac(base_id, 22)) p--;
        if (base_fac(base_id, 34)) p += 2;
        if ((uint32_t)(p + 2) <= 5) {
            switch (p) {
            case 1: cost += cost / 2; break;
            case 2: cost *= 2; break;
            case 3:
                if (!human) {
                    action = 1;
                    goto dispatch;
                }
                if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
                    parse_says(0, game().mfactions[t].adj_name_faction, -1, -1);
                    NetMsg_pop(&NetMsg, "IMMUNITY", -5000, 0, nullptr);
                }
                return 0;
            case -2: cost /= 2; break;
            case -1: cost += -(cost / 4); break;
            }
        }
        if (!human) {
            bool afford;
            if (!is_human(t)) {
                afford = (((game().factions[f].AI_power - game().factions[f].AI_wealth) + 3) * cost * 2) / 3 <=
                         game().factions[f].energy_credits;
            } else {
                int32_t e = game().factions[f].energy_credits / 10;
                if (e < 0) e = 0;
                else if (e > 50) e = 50;
                afford = cost + 2 + e <= game().factions[f].energy_credits;
            }
            if (!afford) goto ai_choose;
        } else {
            int32_t choice;
            if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
                parse_says(0, game().bases[base_id].name, -1, -1);
                parse_num(0, cost);
                parse_num(1, game().factions[f].energy_credits);
                success_rates(1, morale_veh(veh_id, 1, 0), 0, -1);
                success_rates(2, morale_veh(veh_id, 1, 0), 1, -1);
                if (game().factions[f].energy_credits < cost) {
                    wave_it(9);
                    NetMsg_pop(&NetMsg, "THOUGHTCONTROL", -5000, 0, nullptr);
                    return 0;
                }
                wave_it(0x20);
                choice = ui_probe_confirm_menu("THOUGHTCONTROL", "THOUGHTMENU", true);
                if (choice < 1) return 0;
                game().vehs[veh_id].probe_action |= (uint8_t)((choice + 31) * 8);
            } else {
                choice = ((game().vehs[veh_id].probe_action & 8) != 0) + 1;
            }
            if (choice > 1) risk = 1;
        }
        if (!(VehAttackFlags & 2)) goto resolve;
        game().factions[f].energy_credits -= cost;
        game().factions[f].mind_control_total += 4;
        game().factions[t].diplo_mind_control[f] += 4;
        if (f == CurrentPlayerFaction) Console_update_data(&MainConsole, 0);
        goto resolve;
    }

ai_choose:
    {
        // the AI's choice of action against the base
        uint32_t ds = dstat(f, t);
        action = (ds & 0x30) ? 2 : 0;
        BASE& b = game().bases[base_id];
        uint8_t region = map_tile(b.x, b.y)->region;
        if ((ds & 0x30) && game().factions[f].region_base_plan[region] && b.talent_total == b.drone_total) {
            if (game_rand() % 2 == 0 && b.pop_size > 3) action = 4;
        }
        if (has_plague_tech) {
            if (b.pop_size > 5 && ((ds & 0x40000) || (ProposalPassCount[9] < ProposalPassCount[8] && (ds & 0x20))) &&
                game().factions[f].region_base_plan[region] == 2)
                action = 7;
            if (b.pop_size > 3 && (game().factions[f].player_flags & PFLAG_COMMIT_ATROCITIES_WANTONLY) &&
                (!(ds & 7) || (ds & 0x20)))
                action = 7;
        }
        int32_t near = 0;
        if ((uint8_t)ds & 0x30) {
            for (int32_t i = 0; i < 21; i++) {
                int32_t x2 = map_wrap_x(TableOffsetX[i] + b.x);
                int32_t y2 = TableOffsetY[i] + b.y;
                if (!map_in_bounds(x2, y2) || map_is_ocean(x2, y2)) continue;
                int32_t s = stack_fix(veh_at(x2, y2));
                if (s >= 0 && game().vehs[s].faction_id == f && stack_check(s, 4, f, -1, -1)) {
                    near = 1;
                    action = 2;
                    break;
                }
            }
        }
        if (Rules.tgl_probe_steal_tech &&
            (!(b.state_flags & BSTATE_RESEARCH_DATA_STOLEN) || morale_veh(veh_id, 1, 0) > 3 ||
             (action == 0 && (ds & 0x1000))) &&
            !near && game().factions[t].region_base_plan[region] != 2)
            action = 1;
        if (has_plague_tech && b.pop_size > 3 && aah_ooga(f, f) == t && climactic_battle() &&
            (game().factions[f].AI_fight > 0 || (game().factions[f].AI_fight == 0 && ((uint8_t)ds & 0x20))) &&
            (ds & 0xC30))
            action = 7;
        if (!refined) {
            refined = base_fac(base_id, 1);
            if (!refined) {
                if (((uint8_t)ds & 0x30) && (game().factions[t].diff_level > 2 || !is_human(t)) &&
                    game().factions[t].SE_probe < 3)
                    action = 6;
            } else {
                if (action != 1 && game().factions[t].tech_cost / 2 <= game().factions[t].tech_accumulated &&
                    (((uint8_t)ds & 0x30) ||
                     game().factions[f].tech_ranking / 2 + 4 < game().factions[t].tech_ranking / 2))
                    action = 5;
                if (ExpansionEnabled && !MultiplayerActive && base_fac(base_id, 1)) {
                    bool any = false;
                    for (int32_t k = 0; k < 7; k++) {
                        uint32_t st = game().factions[f].diplo_status[k + 1];
                        if (mon_field(t, 15 + k, 0x10) && !(st & 0x10)) {
                            any = true;
                            // BUG(original): compares with the status of faction freed + 1
                            COMPAT_ORIGINAL(probe_freed_status_offset);
                            if (freed == -1 || (st & 1) ||
                                ((st & 2) && !((uint8_t)(&game().factions[f].diplo_status[1])[freed] & 1)))
                                freed = k + 1;
                        }
                    }
                    if (any) {
                        action = 8;
                        refined = 1;
                        goto dispatch;
                    }
                }
            }
            refined = 1;
        }
        goto dispatch;
    }

resolve:
    {
        // framing another faction
        frame = 0;
        if ((action < 6 && action > 0) || action == 8) {
            if (!human) {
                if (morale_veh(veh_id, 1, 0) > 4 && risk == 0 && !is_human(t)) {
                    for (int32_t k = 1; k < 8; k++) {
                        if (k != f && k != t && is_human(k) && is_alive(k) && k == RankingFactionIDUnk1 &&
                            !(dstat(t, k) & 0x10) && (dstat(t, k) & 8)) {
                            frame = k;
                            break;
                        }
                    }
                    if (frame) risk++;
                }
            } else if (!is_human(t)) {
                if (f == CurrentPlayerFaction && (VehAttackFlags & 1)) {
                    success_rates(2, morale_veh(veh_id, 1, 0), risk, base_id);
                    if (success_rates(3, morale_veh(veh_id, 1, 0), risk + 1, base_id))
                        frame = ui_probe_frame_menu(f, t);
                    game().vehs[veh_id].probe_action |= (uint8_t)(frame << 5);
                } else {
                    frame = game().vehs[veh_id].probe_action >> 5;
                }
                if (frame) risk++;
            }
        }
        if (!(VehAttackFlags & 2)) return 1;
        tgt_mod = action + 1;
        int32_t defense = tgt_mod == 0 ? 0 : (has_fac(FAC_COVERT_OPS_CENTER, base_id, 0) ? 2 : 0);
        int32_t morale = morale_veh(veh_id, 1, 0) < 1 ? 1 : morale_veh(veh_id, 1, 0);
        int32_t p = range(defense + game().factions[t].SE_probe, -2, 0);
        if (rnd((morale / 2 - p) + 1) < risk) {
            if (!ProbeHasAlgoEnhancement || ProbeTargetHasHSA || probe_clock_odd()) goto busted;
        } else if (ProbeTargetHasHSA && probe_clock_odd()) {
            goto busted;
        }
        goto success;
    busted:
        frame = -frame;
        if (f == CurrentPlayerFaction) NetMsg_pop_2("BUSTED", capture_pcx(game().mfactions[f].rule_flags & 0x80));
        if (t == CurrentPlayerFaction) busted_action = action;
        kill(veh_id);
        goto after;
    }

success:
    switch (action) {
    case 1:
        game().factions[t].diplo_stolen_techs[f]++;
        if (tech < 0) {
            steal_tech(f, t, 1);
        } else {
            if (f == CurrentPlayerFaction || t == CurrentPlayerFaction) {
                parse_says(0, get_adjective(t), -1, -1);
                parse_says(2, get_adjective(f), -1, -1);
                clear();
                if (tech == 9999) {
                    says("STOLEMAP");
                } else {
                    say_tech(tech, 0);
                    parse_says(1, StrBuffer, -1, -1);
                    clear();
                    says("DECIPHERED");
                }
                says(f == CurrentPlayerFaction ? "0" : "1");
                NetMsg_pop_2(StrBuffer, infil_pcx(alien));
            }
            tech_achieved(f, tech, t, 0);
        }
        game().bases[base_id].state_flags |= BSTATE_RESEARCH_DATA_STOLEN;
        goto survive;
    case 2:
        if (sab == 99) {
            NetMsg_pop_2("VIRUSABORT", nullptr);
            return 1;
        }
        clear();
        if (sab == 0) {
            game().bases[base_id].minerals_accumulated_2 = 0;
            game().bases[base_id].minerals_accumulated = 0;
            parse_says(0, production, -1, -1);
            says("PRODVIRUS");
            parse_says(1, game().bases[base_id].name, -1, -1);
            parse_says(2, get_adjective(f), -1, -1);
            says(f == CurrentPlayerFaction ? "0" : "1");
            if (f == CurrentPlayerFaction || t == CurrentPlayerFaction) NetMsg_pop_2(StrBuffer, infil_pcx(alien));
        } else if (sab == 98) {
            int32_t v = veh_at(game().bases[base_id].x, game().bases[base_id].y);
            if (v < 0) return 1;
            do {
                if (Units[game().vehs[v].unit_id].plan == 5) {
                    if (game().vehs[v].faction_id == CurrentPlayerFaction) {
                        parse_says(0, Units[game().vehs[v].unit_id].name, -1, -1);
                        parse_says(1, get_adjective(f), -1, -1);
                        NetMsg_pop_2("BUSTMYPLANET", infil_pcx(alien));
                    } else if (f == CurrentPlayerFaction) {
                        parse_says(0, Units[game().vehs[v].unit_id].name, -1, -1);
                        parse_says(2, get_adjective(game().vehs[v].faction_id), -1, -1);
                        NetMsg_pop_2("BUSTYOURPLANET", infil_pcx(alien));
                    }
                    kill(v);
                    if (v < veh_id) veh_id--;
                    break;
                }
                v = v >= 0 ? game().vehs[v].next_veh_id_stack : v;
            } while (v >= 0);
        } else {
            set_fac(sab, base_id, 0);
            parse_say(0, Facility[sab].name, -1, -1);
            says("FACVIRUS");
            parse_says(1, game().bases[base_id].name, -1, -1);
            parse_says(2, get_adjective(f), -1, -1);
            says(f == CurrentPlayerFaction ? "0" : "1");
            if (f == CurrentPlayerFaction || t == CurrentPlayerFaction) NetMsg_pop_2(StrBuffer, infil_pcx(alien));
        }
        BaseWin_check_base(&BaseWin, base_id);
        draw_tile(game().bases[base_id].x, game().bases[base_id].y, 2);
        goto survive;
    case 3: {
        int32_t e = steal_energy(base_id);
        int32_t a = (morale_veh(veh_id, 1, 0) * e) / 6;
        int32_t r = rnd(e - a);
        e = range(r / 2 + a, 0, game().factions[t].energy_credits);
        if (e == 1) e = 0;
        game().factions[t].energy_credits -= e;
        game().factions[f].energy_credits += e;
        if (f == CurrentPlayerFaction) {
            Console_update_data(&MainConsole, 0);
            parse_num(0, e);
            parse_says(0, game().bases[base_id].name, -1, -1);
            parse_says(2, get_adjective(t), -1, -1);
            NetMsg_pop_2(e == 0 ? "GOTNOENERGY" : "GOTENERGY", infil_pcx(alien));
        } else if (t == CurrentPlayerFaction) {
            Console_update_data(&MainConsole, 0);
            parse_num(0, e);
            parse_says(0, game().bases[base_id].name, -1, -1);
            parse_says(2, get_adjective(f), -1, -1);
            NetMsg_pop_2(e == 0 ? "TOOKNOENERGY" : "TOOKENERGY", infil_pcx(alien));
        }
        goto survive;
    }
    case 4: {
        BASE& b = game().bases[base_id];
        int32_t at = b.assimilation_turns_left;
        int32_t lim = (b.pop_size * 10) / 4;
        if (lim < at) lim = at;
        b.assimilation_turns_left = (uint8_t)range(at + 10, 0, lim);
        set_base(base_id);
        base_compute(1);
        parse_says(0, b.name, -1, -1);
        bool show = f == CurrentPlayerFaction || t == CurrentPlayerFaction;
        if (cur_base().talent_total < cur_base().drone_total) {
            cur_base().state_flags |= BSTATE_DRONE_RIOTS_ACTIVE;
            draw_tile(b.x, b.y, 2);
            if (!(game().mfactions[f].rule_flags & 0x80)) {
                if (show) NetMsg_pop_2("DRONEINCITE0", "drone_sm.pcx");
            } else if (show) {
                NetMsg_pop_2("DRONEINCITE0", "ALdrone_sm.pcx");
            }
        } else if (!(game().mfactions[f].rule_flags & 0x80)) {
            if (show) NetMsg_pop_2("DRONEINCITE1", "drone_sm.pcx");
        } else if (show) {
            NetMsg_pop_2("DRONEINCITE1", "ALdrone_sm.pcx");
        }
        goto survive;
    }
    case 5: {
        int32_t lost = rnd(game().factions[t].tech_accumulated);
        game().factions[t].tech_accumulated -= lost;
        if (f == CurrentPlayerFaction || t == CurrentPlayerFaction) {
            parse_num(0, lost);
            parse_says(0, game().bases[base_id].name, -1, -1);
            parse_says(1, get_adjective(t), -1, -1);
            NetMsg_pop_2("ASSASSINATED", infil_pcx(alien));
        }
        goto survive;
    }
    case 8: {
        int32_t lid = freed;
        if (human && f == CurrentPlayerFaction) {
            parse_says(1, get_adjective(t), -1, -1);
            lid = ui_probe_free_menu(t, alien);
            if (lid < 0) return 0;
        }
        if (lid != -1) {
            dest = game().vehs[veh_id].home_base_id;
            if (dest < 0) veh_kill(veh_id);
            FactionStatus[1] |= (uint8_t)(1 << (lid & 31));
            setup_player(lid, t, 1);
            game().factions[t].eliminated_count--;
            for (int32_t k = 0; k < 6; k++)
                if (mon_field(t, 15 + k, 0x10) && mon_field(t, 15 + k, 0x8) == lid) mon_field(t, 15 + k, 0x10) = 0;
            if (f == CurrentPlayerFaction || t == CurrentPlayerFaction) {
                parse_says(0, get_title(lid), -1, -1);
                parse_says(1, get_name(lid), -1, -1);
                parse_says(2, get_noun(lid), -1, -1);
                NetMsg_pop_2("WILLYFREE", "fac_escape_sm.pcx");
            }
            if (!(GameRules & RULES_SCN_NO_TECH_TRADING)) {
                for (int32_t k = 0; k < 89; k++)
                    if (has_tech(k, lid) && !has_tech(k, f)) net_tech(f, k, lid, 0);
            }
            *reinterpret_cast<int32_t*>(&InfoWindow + 0x90) = 0;
            Console_update_data(&MainConsole, 0);
            game().factions[f].diplo_friction[lid] = 0;
            net_treaty_off(lid, f, 0x20, 1);
            net_treaty_on(f, lid, 0x800000A, 0);
            net_set_treaty(lid, f, 0x2000000, 1, 0);
            net_treaty_on(lid, f, 0x1000001, 0);
            game().factions[lid].diplo_patience[f] = 0;
            game().factions[f].diplo_spoke[lid] = 0;
            diplomacy_check(f, lid, 0);
            game().factions[f].diplo_reputation++;
            game().factions[lid].diplo_reputation++;
        }
        goto survive;
    }
    case 7: {
        atrocity(f, t, 1, 0);
        if (plague_techs < 1) plague_techs++;
        if (game().factions[t].player_flags & PFLAG_GENETIC_PLAGUE_INTRO) plague_known++;
        int32_t k = range(plague_known, 1, plague_techs);
        game().factions[t].player_flags |= PFLAG_GENETIC_PLAGUE_INTRO;
        BASE& b = game().bases[base_id];
        int32_t loss = ((plague_techs - k) * b.pop_size) / plague_techs;
        if (b.state_flags & BSTATE_GENETIC_PLAGUE_INTRO) loss /= 2;
        if (has_fac(FAC_RESEARCH_HOSPITAL, base_id, 0)) loss /= 2;
        if (has_fac(FAC_NANOHOSPITAL, base_id, 0)) loss /= 2;
        loss++;
        if (b.pop_size <= loss) loss = b.pop_size - 1;
        for (int32_t v = veh_at(b.x, b.y); v >= 0; v = v >= 0 ? game().vehs[v].next_veh_id_stack : v) {
            int32_t r = rnd((veh_health(v) + 1) / 2);
            int32_t d = r + veh_health(v) / 2;
            if (has_fac(FAC_RESEARCH_HOSPITAL, base_id, 0)) d /= 2;
            if (has_fac(FAC_NANOHOSPITAL, base_id, 0)) d /= 2;
            if (b.state_flags & BSTATE_GENETIC_PLAGUE_INTRO) d /= 2;
            game().vehs[v].damage_taken += (uint8_t)d;
        }
        b.state_flags |= BSTATE_GENETIC_PLAGUE_INTRO;
        if (loss) b.pop_size -= (int8_t)loss;
        b.nutrients_accumulated = 0;
        spot_base(base_id, f);
        draw_tile(b.x, b.y, 2);
        if (f == CurrentPlayerFaction || t == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) {
            parse_says(0, b.name, -1, -1);
            parse_says(1, get_noun(f), -1, -1);
            clear();
            says("GENETICWARFARE");
            says(f == CurrentPlayerFaction ? "0" : "1");
            popp_2(StrBuffer, infil_pcx(alien), 0);
        }
        goto survive;
    }
    case -1: {
        VEH& u = game().vehs[veh_id_tgt];
        int32_t unit = u.unit_id;
        game().factions[t].units_active[unit]--;
        if (unit > 64) {
            int32_t nu = propose_proto(f, Units[unit].chassis_id, Units[unit].weapon_id, Units[unit].armor_id,
                                       Units[unit].ability_flags, Units[unit].reactor_id, -2, Units[unit].name);
            if (nu >= 0) u.unit_id = (int16_t)nu;
        }
        u.faction_id = (uint8_t)f;
        game().factions[f].units_active[u.unit_id]++;
        game().factions[f].mind_control_total++;
        game().factions[t].diplo_mind_control[f]++;
        owner_set(u.x, u.y, f);
        u.home_base_id = (int16_t)base_find_faction(u.x, u.y, f);
        u.moves_spent = 0;
        u.order = 0;
        u.state &= 0xFCFFBDFF;
        spot_stack(veh_id_tgt, t);
        draw_tile(u.x, u.y, 2);
        if (t == CurrentPlayerFaction) {
            Console_focus(&MainConsole, u.x, u.y, f);
            parse_says(1, get_noun(f), -1, -1);
            NetMsg_pop_2("SUBVERTED", infil_pcx(alien));
        }
        if (risk) return 1;
        goto diplomacy;
    }
    default: goto survive;
    }

survive:
    {
        int32_t defense = tgt_mod == 0 ? 0 : (has_fac(FAC_COVERT_OPS_CENTER, base_id, 0) ? 2 : 0);
        int32_t p = range(defense + game().factions[t].SE_probe, -2, 0);
        if (risk < rnd(morale_veh(veh_id, 1, 0) - p)) {
            // the probe team returns home
            dest = base_find_faction(game().vehs[veh_id].x, game().vehs[veh_id].y, f);
            if (veh_triad(veh_id) == 1) {
                if (dest < 0) goto lost;
                if (!is_port(dest, 0)) {
                    dest = sea_return_base(veh_id, f);
                    if (ProbeTargetHasHSA && probe_clock_odd()) goto caught;
                }
            }
            if (dest < 0) goto lost;
        home:
            veh_put(veh_id, game().bases[dest].x, game().bases[dest].y);
            game().vehs[veh_id].movement_turns = 0;
            draw_tiles(game().bases[base_id].x, game().bases[base_id].y, 2);
            draw_tile(game().bases[dest].x, game().bases[dest].y, 2);
            parse_says(1, game().bases[dest].name, -1, -1);
            int32_t before = morale_veh(veh_id, 1, 0);
            game().vehs[veh_id].morale = (uint8_t)range(game().vehs[veh_id].morale + 1, 0, 6);
            veh_skip(veh_id);
            if (f == CurrentPlayerFaction) {
                parse_says(0, Units[game().vehs[veh_id].unit_id].name, -1, -1);
                parse_say(2, Morale[morale_veh(veh_id, 1, 0)].name, -1, -1);
                clear();
                says("PROBESURVIVAL");
                if (action == 8) {
                    says("X");
                } else {
                    int32_t up = (before == morale_veh(veh_id, 1, 0) || morale_veh(veh_id, 1, 0) < 1) ? 0 : 1;
                    say_num(up);
                }
                if (f == CurrentPlayerFaction) NetMsg_pop_2(StrBuffer, infil_pcx(alien));
            }
        } else {
            if (!ProbeTargetHasHSA && ProbeHasAlgoEnhancement && probe_clock_odd()) {
                dest = base_find_faction(game().vehs[veh_id].x, game().vehs[veh_id].y, f);
                if (veh_triad(veh_id) == 1 && dest >= 0 && !is_port(dest, 0)) dest = sea_return_base(veh_id, f);
                if (dest >= 0) goto home;
                goto lost;
            }
        caught:
            frame = -frame;
            dest = -1;
        lost:
            if (action == 8 && Units[game().vehs[veh_id].unit_id].weapon_id != 21)
                dest = game().vehs[veh_id].home_base_id;
            if (dest >= 0) goto home;
            veh_kill(veh_id);
        }
        if (action == 6) {
            if (f == CurrentPlayerFaction || t == CurrentPlayerFaction || spying(f) || spying(t) ||
                (GameState & STATE_OMNISCIENT_VIEW)) {
                if ((GameState & STATE_OMNISCIENT_VIEW) ||
                    (uint8_t)game().bases[base_id].faction_id == CurrentPlayerFaction ||
                    (game().bases[base_id].visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31))))
                    Console_focus(&MainConsole, game().bases[base_id].x, game().bases[base_id].y,
                                  (uint8_t)game().bases[base_id].faction_id);
                parse_says(0, get_title(f), -1, -1);
                parse_says(1, get_name(f), -1, -1);
                parse_says(2, get_noun(f), -1, -1);
                parse_says(3, game().bases[base_id].name, -1, -1);
                parse_says(4, get_title(t), -1, -1);
                parse_says(5, get_name(t), -1, -1);
                clear();
                says("MINDCONTROL");
                says(f == CurrentPlayerFaction ? "0" : (t == CurrentPlayerFaction ? "1" : "2"));
                popp_2(StrBuffer, infil_pcx(alien), 0);
            }
            // the units in and next to the base change sides
            for (int32_t v = game().veh_count - 1; v >= 0; v--) {
                if (v >= game().veh_count || game().vehs[v].faction_id != t) continue;
                int32_t d =
                    tile_distance(game().vehs[v].x, game().vehs[v].y, game().bases[base_id].x, game().bases[base_id].y);
                if (d >= 2 || (d != 0 && base_who(game().vehs[v].x, game().vehs[v].y) >= 0)) continue;
                VEH& u = game().vehs[v];
                int32_t unit = u.unit_id;
                game().factions[t].units_active[unit]--;
                if (unit > 64) {
                    int32_t nu = propose_proto(f, Units[unit].chassis_id, Units[unit].weapon_id, Units[unit].armor_id,
                                               Units[unit].ability_flags, Units[unit].reactor_id, -2, Units[unit].name);
                    if (nu >= 0) u.unit_id = (int16_t)nu;
                }
                uint8_t count = game().factions[f].units_active[u.unit_id];
                u.faction_id = (uint8_t)f;
                u.home_base_id = (int16_t)base_id;
                u.moves_spent = 0;
                game().factions[f].units_active[u.unit_id] = (uint8_t)(count + 1);
                if (Chassis[Units[u.unit_id].chassis_id].range) u.movement_turns = 0;
                if (u.order != 2) u.order = 0;
                owner_set(u.x, u.y, f);
                u.visibility |= (uint8_t)(1 << (t & 31));
            }
            if (risk == 0) act_of_aggression(f, t);
            capture_base(base_id, f, 1);
        }
        if (f == CurrentPlayerFaction && dest < 0) {
            if (!(game().mfactions[f].rule_flags & 0x80))
                NetMsg_pop_2(action != 6 ? "PROBECAUGHT" : "PROBESUCCUMB", "capture_sm.pcx");
            else NetMsg_pop_2(action != 6 ? "PROBECAUGHT" : "PROBESUCCUMB", "al_cap_sm.pcx");
        }
    }

after:
    if (action == 0) return 1;
diplomacy:
    {
        // diplomatic consequences
        if (!has_treaty(f, t, 7) || (has_treaty(t, f, 1) && has_treaty(t, f, 0x2000000))) return 1;
        int32_t fr = frame;
        if (action == 6 && risk != 0) {
            set_treaty(t, f, 0x420, 1);
            return 1;
        }
        if (fr < 0) {
            int32_t k = -fr;
            if (!is_human(k)) {
                if (f == CurrentPlayerFaction) {
                    parse_says(0, get_title(k), -1, -1);
                    parse_says(1, get_name(k), -1, -1);
                    NetMsg_pop_2("FRAMEFLOP", capture_pcx(game().mfactions[f].rule_flags & 0x80));
                }
                treaty_on(k, f, 0x10);
            }
            if (is_human(k)) {
                parse_says(0, get_title(f), -1, -1);
                parse_says(1, get_name(f), -1, -1);
                parse_says(2, get_pact_hood(f, k), -1, -1);
                parse_says(3, get_noun(f), -1, -1);
                parse_says(4, get_his_her(f, 0), -1, -1);
                parse_says(5, get_him_her(f, 0), -1, -1);
                parse_says(6, get_title(t), -1, -1);
                parse_says(7, get_name(t), -1, -1);
                parse_says(8, get_noun(t), -1, -1);
                if (!MultiplayerActive) {
                    if (!has_treaty(k, f, 1)) {
                        if (popp_2("FRAMEEXCUSE", capture_pcx(game().mfactions[f].rule_flags & 0x80), 0)) {
                            treaty_on(f, k, 0x10);
                            game().factions[k].diplo_spoke[f] = CurrentTurn;
                        }
                    } else if (popp_2("PACTFRAMEEXCUSE", capture_pcx(game().mfactions[f].rule_flags & 0x80), 0)) {
                        pact_ends(f, k);
                        game().factions[k].diplo_spoke[f] = CurrentTurn;
                    }
                } else {
                    agenda_on(k, f, 0x20);
                    if (-CurrentPlayerFaction == fr) NetMsg_pop_2("HAVEFRAMEEXCUSE", nullptr);
                    if (f == CurrentPlayerFaction) {
                        parse_says(0, get_title(k), -1, -1);
                        parse_says(1, get_name(k), -1, -1);
                        parse_says(2, get_noun(k), -1, -1);
                        NetMsg_pop_2("HASFRAMEEXCUSE", capture_pcx(game().mfactions[f].rule_flags & 0x80));
                    }
                }
            }
        }
        if (is_human(t)) {
            parse_says(0, get_title(f), -1, -1);
            parse_says(1, get_name(f), -1, -1);
            parse_says(2, get_pact_hood(f, t), -1, -1);
            parse_says(3, get_noun(f), -1, -1);
            parse_says(4, get_his_her(f, 0), -1, -1);
            parse_says(5, get_him_her(f, 0), -1, -1);
            bool al = (game().mfactions[f].rule_flags & 0x80) != 0;
            if (!MultiplayerActive) {
                if (!has_treaty(t, f, 1)) {
                    if (busted_action != 0) {
                        clear();
                        says("BUSTED");
                        say_num(busted_action);
                    }
                    if (ui_probe_excuse("EXCUSE", busted_action, al)) {
                        if (action < 6) treaty_on(f, t, 0x10);
                        else double_cross(f, t, -1);
                        game().factions[t].diplo_spoke[f] = CurrentTurn;
                    }
                } else {
                    if (busted_action != 0) {
                        clear();
                        says("BUSTED");
                        say_num(busted_action);
                    }
                    if (ui_probe_excuse("PACTEXCUSE", busted_action, al)) {
                        pact_ends(f, t);
                        game().factions[t].diplo_spoke[f] = CurrentTurn;
                    }
                }
            } else {
                agenda_on(t, f, 0x20);
                if (action > 5) game().factions[f].integrity_blemishes++;
                if (t == CurrentPlayerFaction) {
                    if (busted_action != 0) {
                        clear();
                        says("BUSTED");
                        say_num(busted_action);
                    }
                    ui_probe_excuse("NETEXCUSE", busted_action, al);
                }
                if (f == CurrentPlayerFaction) {
                    parse_says(0, get_title(t), -1, -1);
                    parse_says(1, get_name(t), -1, -1);
                    parse_says(2, get_noun(t), -1, -1);
                    NetMsg_pop_2("HASNETEXCUSE", nullptr);
                }
            }
            return 1;
        }
        if (fr > 0) {
            treaty_on(t, fr, 0x58);
            set_treaty(t, fr, 0x20, 1);
            if (fr == CurrentPlayerFaction) {
                parse_says(0, get_title(t), -1, -1);
                parse_says(1, get_name(t), -1, -1);
                parse_says(2, get_noun(t), -1, -1);
                NetMsg_pop_2("FRAMEJOB", infil_pcx(alien));
            }
            if (f != CurrentPlayerFaction) return 1;
            parse_says(0, get_title(t), -1, -1);
            parse_says(1, get_name(t), -1, -1);
            parse_says(2, get_noun(t), -1, -1);
            parse_says(3, get_title(fr), -1, -1);
            parse_says(4, get_name(fr), -1, -1);
            parse_says(5, get_noun(fr), -1, -1);
            NetMsg_pop_2("FRAMED", infil_pcx(alien));
            return 1;
        }
        diplomacy_check(f, t, 0);
        diplomacy_caption(f, t);
        parse_says(0, get_title(f), -1, -1);
        parse_says(1, get_name(f), -1, -1);
        parse_says(2, get_pact_hood(f, t), -1, -1);
        if (!has_treaty(f, t, 1)) {
            if (!has_treaty(t, f, 0x20) && action < 6 &&
                (((action == 1 || action == 5) && game().factions[f].tech_ranking < game().factions[t].tech_ranking &&
                  game().factions[f].ranking < 6 && !DiploGreatSatan && rnd(8)) ||
                 ((action == 2 || action == 4) &&
                  ((DiploFearScale && !DiploGreatSatan) ||
                   (game().factions[f].ranking < game().factions[t].ranking && !DiploTributeDemand && rnd(8)))))) {
                cause_friction(t, f, rnd(4));
                if (f == CurrentPlayerFaction) {
                    clear();
                    says("KEPT");
                    say_num(action);
                    X_pops(StrBuffer, FactionPortraits[t], 0);
                }
            } else {
                if (f == CurrentPlayerFaction) X_pops("USEDEXCUSE", FactionPortraits[t], 0);
                if (action < 6) treaty_on(f, t, 0x10);
                else double_cross(f, t, -1);
            }
            return 1;
        }
        if (action == 1) {
            if (game().factions[f].tech_ranking < game().factions[t].tech_ranking && game().factions[f].ranking < 6 &&
                !DiploGreatSatan) {
                cause_friction(t, f, rnd(4));
                if (f == CurrentPlayerFaction) {
                    clear();
                    says("KEPTPACT");
                    say_num(1);
                    X_pops(StrBuffer, FactionPortraits[t], 0);
                }
                return 1;
            }
        }
        if (action < 6 && game().factions[f].ranking < game().factions[t].ranking && game().factions[f].ranking < 5) {
            if (f == CurrentPlayerFaction) X_pops("EXCUSEDPACT", FactionPortraits[t], 0);
            return 1;
        }
        if (f == CurrentPlayerFaction) X_pops("USEDEXCUSE", FactionPortraits[t], 0);
        if (action < 6) pact_ends(f, t);
        else {
            double_cross(f, t, -1);
            treaty_on(f, t, 0x10);
        }
        return 1;
    }
}
