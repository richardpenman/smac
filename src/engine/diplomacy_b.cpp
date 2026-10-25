// Diplomacy, second half of the original module (0x54B140-0x55F930): AI attitude
// (wants_to_attack), treaties and pacts between AI factions met on the map (encounter,
// enemies_*), atrocities, territory violations, base value and transfer. The human diplomacy
// dialogs of this range (communicate and its menus) are UI (engine/ui_bridge.h).
#include <cstddef>
#include <cstdlib>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/base_misc.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/win32.h"
#ifdef SMACX_HYBRID
#include "engine/hybrid_shadow.h"
#endif

namespace {

bool is_alien(int32_t faction_id) { return (MFactions[faction_id].rule_flags & 0x80) != 0; }
bool is_human(int32_t faction_id) { return (FactionStatus[0] & (1 << faction_id)) != 0; }
uint32_t& status(int32_t faction_id, int32_t faction_id_with) {
    return Factions[faction_id].diplo_status[faction_id_with];
}

// The parse_says blocks the original repeats inline: leader title and name ($n, $n+1), faction
// noun ($n).
void say_leader(int32_t n, int32_t faction_id) {
    const MFaction& m = MFactions[faction_id];
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, m.name_leader, -1, -1);
}
void say_noun(int32_t n, int32_t faction_id) {
    const MFaction& m = MFactions[faction_id];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(n, m.noun_faction, -1, -1);
}

// Whether the viewer learns of an event between two other factions (enemies_war,
// enemies_treaty): infiltrator or Empath Guild, governor view of non-alien factions, omniscient
// view. Evaluated in the original's order (has_project once per faction).
bool viewer_learns(int32_t faction_id, int32_t faction_id_with) {
    int32_t v = CurrentPlayerFaction;
    if (status(v, faction_id) & 0x1000) return true;
    if (has_project(4, v)) return true;
    if (v == GovernorFaction && !is_alien(faction_id)) return true;
    if (status(v, faction_id_with) & 0x1000) return true;
    if (has_project(4, v)) return true;
    if (v == GovernorFaction && !is_alien(faction_id_with)) return true;
    return (GameState & 0x80) != 0;
}

// The viewer owns the Empath Guild (enemies_unpact, enemies_treaty test the project's base).
bool viewer_has_empath_guild() {
    return SecretProjects[4] >= 0 && Bases[SecretProjects[4]].faction_id == CurrentPlayerFaction;
}

}  // namespace

// ---- wants_to_attack ---------------------------------------------------------------------------

// Native emulation of the uninitialised array at [ebp-0x134] (docs/RECONSTRUCTION.md, known
// uninitialised read #4): index f is faction f's slot ([ebp-0x134+4f]); the original keeps
// whatever its frame position held, so consecutive calls at the same stack depth see the previous
// call's reduced values. Native code keeps them here between calls (canonical start: 0).
int32_t WantsToAttackResidue[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// The body of wants_to_attack; slots: the residue array (read, and updated as the original's
// frame is when the region scan is reached).
static int32_t wants_to_attack_core(int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override,
                                    int32_t* slots) {
    if (is_alien(faction_id) && is_alien(faction_id_tgt)) return 1;
    Faction& f1 = Factions[faction_id];
    Faction& f2 = Factions[faction_id_tgt];
    uint32_t s = f1.diplo_status[faction_id_tgt];
    if (s & 0x40060) return 1;
    if (f2.major_atrocities != 0 && f1.major_atrocities == 0) return 1;
    if (s & 0x4000000) return 0;
    if (f1.base_count <= 1) return 0;
    if (!is_human(faction_id_tgt) && (f1.player_flags & 0x800000)) return 0;
    int32_t score = 0;
    int32_t surrendered_to = 0;
    int32_t ally_at_war = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || i == faction_id_tgt) continue;
        uint32_t u = f1.diplo_status[i];
        if ((u & 0x2000000) && (u & 1)) surrendered_to = i;
        if ((u & 0x10) && !(f2.diplo_status[i] & 1)) {
            score++;
            if (Factions[i].mil_strength_1 > f2.mil_strength_1 * 3 / 2) score++;
        }
        if (great_beelzebub(i, 0) && (CurrentTurn >= 100 || (GameRules & 0x40))) {
            uint32_t t = f2.diplo_status[i];
            if (t & 0x10) score++;
            if ((t & 8) && (f1.diplo_status[i] & 8)) score++;
        }
        uint32_t a = f1.diplo_status[i];
        if ((a & 1) && is_human(i)) {
            uint32_t c = f2.diplo_status[i];
            if (c & 1) score += 2;
            uint32_t surr = a & 0x2000000;
            if (surr && (Factions[i].diplo_status[faction_id_tgt] & 3)) return 0;
            if (c & 0x10) {
                ally_at_war = 1;
                score -= surr ? 4 : 2;
            }
        }
    }
    if (surrendered_to) {
        uint32_t t = f2.diplo_status[surrendered_to];
        if (t & 0x10) return 1;
        if (t & 3) return 0;
    }
    if (f1.AI_fight < 0 && !ally_at_war && FactionRankings[7] != faction_id_tgt) return 0;
    // BUG(original): the per-faction maxima of region_total_bases start from uninitialised
    // stack slots (known uninitialised read #4).
    for (int32_t r = 1; r < 64; r++) {
        for (int32_t k = 1; k < 8; k++) {
            int32_t v = Factions[k].region_total_bases[r];
            if (slots[k] > v) v = slots[k];
            slots[k] = v;
        }
    }
    for (int32_t k = 1; k < 8; k++) slots[k] -= slots[k] / 4;
    int32_t hq_region = -1, hq_region_tgt = -1;
    for (int32_t i = 0; i < BaseCount; i++) {
        int32_t offset, mask;
        bitmask(1, &offset, &mask);
        if (!(Bases[i].facilities_built[offset] & (uint8_t)mask)) continue;
        if (Bases[i].faction_id == faction_id)
            hq_region = map_tile(Bases[i].x, Bases[i].y)->region;
        else if (Bases[i].faction_id == faction_id_tgt)
            hq_region_tgt = map_tile(Bases[i].x, Bases[i].y)->region;
    }
    int32_t attack = 0, defense = 1, regions = 0;
    for (int32_t r = 1; r < 64; r++) {
        uint16_t force = f1.region_force_rating[r];
        if (!force) continue;
        uint16_t units_tgt = f2.region_total_combat_units[r];
        if (!units_tgt && !f2.region_total_bases[r]) continue;
        if (f1.region_total_bases[r] >= slots[faction_id] * 3 / 4 || r == hq_region) {
            uint32_t extra = odds_override > 0 ? Factions[odds_override].region_force_rating[r] >> 2 : 0;
            if ((int32_t)f2.region_force_rating[r] > (int32_t)(force + extra + f1.region_total_combat_units[r]))
                return 0;
        }
        if (f2.region_total_bases[r]) {
            uint32_t extra = odds_override > 0 ? Factions[odds_override].region_force_rating[r] >> 1 : 0;
            attack += force + extra;
        }
        if ((f2.region_total_bases[r] >= slots[faction_id_tgt] * 3 / 4 || r == hq_region_tgt) && force > units_tgt) {
            uint32_t extra = odds_override > 0 ? Factions[odds_override].region_force_rating[r] >> 1 : 0;
            attack += force + extra;
        }
        uint8_t own_bases = f1.region_total_bases[r];
        defense += units_tgt + (own_bases ? f2.region_force_rating[r] >> 1 : 0);
        if (own_bases) regions++;
    }
    score -= f1.AI_fight * 2;
    if (f1.tech_commerce_bonus > f2.tech_commerce_bonus * 3 / 2) score++;
    if (f1.tech_commerce_bonus < f2.tech_commerce_bonus * 2 / 3) score--;
    if (f1.best_weapon_value > f2.best_armor_value * 2) score--;
    if (f1.best_weapon_value <= f2.best_armor_value) score++;
    if (!(s = f1.diplo_status[faction_id_tgt], s & 0x10)) score++;
    if (s & 1) score++;
    if (odds_override > 0 && !great_satan(odds_override, 0)) score--;
    if ((f1.diplo_agenda[faction_id_tgt] & 0x200) && !(GameRules & 0x40)) score--;
    int32_t integrity = (f2.integrity_blemishes - f1.integrity_blemishes + 2) / 3;
    if (integrity < 0)
        integrity = 0;
    else if (integrity > 2)
        integrity = 2;
    score -= integrity;
    int32_t morale = f1.SE_morale_pending;
    morale = morale < -4 ? -4 : (morale > 4 ? 4 : morale);
    int32_t mine = MFactions[faction_id].rule_morale + morale + 16;
    morale = f2.SE_morale_pending;
    morale = morale < -4 ? -4 : (morale > 4 ? 4 : morale);
    int32_t theirs = MFactions[faction_id_tgt].rule_morale + morale + 16;
    if (regions == 0 && score <= 0 && !(f1.diplo_status[faction_id_tgt] & 0x20000000)) return 1;
    if (mine * attack * 6 / (theirs * defense) < score + 6) return 0;
    return 1;
}

#ifdef SMACX_HYBRID
// Hybrid: the slots are read from the stack image the shadow thunk saved at entry (1 KB below the
// entry ESP; faction f's slot is [entry ESP - 0x138 + 4f]), so the result matches the original
// whatever its caller left there; the thunk then re-runs the original to leave its stack image.
extern "C" uint32_t wants_to_attack_shadow_buf[256];
extern "C" int32_t GAMEAPI wants_to_attack_impl_c(int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override);
int32_t GAMEAPI SHADOWABLE(wants_to_attack)(int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override) {
    int32_t slots[8];
    for (int32_t k = 0; k < 8; k++) slots[k] = (int32_t)wants_to_attack_shadow_buf[178 + k];
    return wants_to_attack_core(faction_id, faction_id_tgt, odds_override, slots);
}
HYBRID_SHADOW_THUNK(int32_t, wants_to_attack, (int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override), 3,
                    0x55BC89, "push %ebp\n mov %esp, %ebp\n sub $0x134, %esp")
#else
int32_t GAMEAPI wants_to_attack(int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override) {
    return wants_to_attack_core(faction_id, faction_id_tgt, odds_override, WantsToAttackResidue);
}
#endif

int32_t(GAMEAPI* g_harness_wants_to_attack_fresh)(int32_t, int32_t, int32_t) = nullptr;

int32_t wants_to_attack_fresh(int32_t faction_id, int32_t faction_id_tgt, int32_t odds_override) {
    if (g_harness_wants_to_attack_fresh) return g_harness_wants_to_attack_fresh(faction_id, faction_id_tgt, odds_override);
    for (int32_t k = 0; k < 8; k++) WantsToAttackResidue[k] = 0;
    return wants_to_attack(faction_id, faction_id_tgt, odds_override);
}

int32_t GAMEAPI great_beelzebub(int32_t faction_id, int32_t is_aggressive) {
    if (!is_human(faction_id) || FactionRankings[7] != faction_id) return 0;
    int32_t min_bases = (CurrentTurn + 25) / 50;
    if (min_bases < 4) min_bases = 4;
    if (Factions[faction_id].base_count > min_bases &&
        (Factions[faction_id].diff_level > 1 || (GameRules & 0x40) || is_aggressive))
        return 1;
    return 0;
}

// ---- first contact and AI-to-AI diplomacy --------------------------------------------------------

void GAMEAPI comm_check(int32_t faction_id, int32_t faction_id_with) {
    uint32_t humans = FactionStatus[0];
    if (!(humans & (1u << faction_id))) return;
    uint32_t s = status(faction_id, faction_id_with);
    uint32_t commlink = s & 8;
    int32_t spoke = Factions[faction_id].diplo_spoke[faction_id_with];
    if (commlink && !(s & 0x2000) && spoke >= 0 && CurrentTurn - spoke < 16) return;
    bool with_human = (humans & (1u << faction_id_with)) != 0;
    if (!MultiplayerActive) {
        if (PbemActive) {
            if (with_human) {
                if (commlink) return;
                treaty_on(faction_id, faction_id_with, 0x10004);
                return;
            }
            if (faction_id != CurrentPlayerFaction) {
                int32_t old = DiploStateB[faction_id][faction_id_with];
                DiploStateB[faction_id][faction_id_with] = old | 1;
                if (commlink) return;
                DiploStateB[faction_id][faction_id_with] = old | 3;
                treaty_on(faction_id, faction_id_with, 0x10004);
                return;
            }
        }
        communicate(faction_id, faction_id_with, 0);
        return;
    }
    if (faction_id == CurrentPlayerFaction && !with_human) {
        int32_t old = DiploStateA[faction_id_with];
        DiploStateA[faction_id_with] = old | 1;
        if (!commlink) {
            DiploStateA[faction_id_with] = old | 3;
            net_treaty_on(faction_id, faction_id_with, 0x10004, 0);
        }
        return;
    }
    if (with_human && !commlink) treaty_on(faction_id, faction_id_with, 0x10004);
    if (faction_id == CurrentPlayerFaction && !(status(faction_id, faction_id_with) & 0x8000000)) {
        say_leader(0, faction_id_with);
        say_noun(2, faction_id_with);
        NetMsg_pop(&NetMsg, "NETMET", 5000, 0, nullptr);
    }
    if (faction_id_with == CurrentPlayerFaction && !(status(faction_id, faction_id_with) & 0x8000000)) {
        say_leader(0, faction_id);
        say_noun(2, faction_id);
        NetMsg_pop(&NetMsg, "NETMET", 5000, 0, nullptr);
    }
}

void GAMEAPI pact_attacks(int32_t faction_id, int32_t faction_id_tgt) {
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    parse_says(3, get_he_she(faction_id, 0), -1, -1);
    say_noun(4, faction_id_tgt);
    NetMsg_pop(&NetMsg, "PACTATTACKS", 5000, 0, nullptr);
}

void GAMEAPI enemies_war(int32_t faction_id, int32_t faction_id_with) {
    Faction& f1 = Factions[faction_id];
    if (f1.diplo_status[faction_id_with] & 0x10) return;
    int32_t v = CurrentPlayerFaction;
    const char* label = nullptr;
    int32_t speaker = -1, other = -1;
    if (status(v, faction_id) & 1) {
        speaker = faction_id;
        other = faction_id_with;
    } else if (status(v, faction_id_with) & 1) {
        speaker = faction_id_with;
        other = faction_id;
    } else if (viewer_learns(faction_id, faction_id_with)) {
        say_noun(0, faction_id);
        say_noun(1, faction_id_with);
        label = "ENEMYWAR";
    }
    if (speaker >= 0) {
        say_leader(0, speaker);
        parse_says(2, get_noun(speaker), -1, -1);
        parse_says(3, get_he_she(speaker, 0), -1, -1);
        parse_says(4, get_noun(other), -1, -1);
        label = "PACTATTACKS";
    }
    if (label) NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
    treaty_on(faction_id, faction_id_with, 0x10);
    uint32_t& mine = f1.diplo_status[faction_id_with];
    if (!(mine & 0x40000000)) trade_maps(faction_id, faction_id_with);
    Faction& f2 = Factions[faction_id_with];
    uint32_t& theirs = f2.diplo_status[faction_id];
    if ((mine & 0x20) && !(mine & 0x40000) && DiffLevel > 1 && game_rand() % DiffLevel != 0) {
        f1.diplo_agenda[faction_id_with] &= 0xFFFFF3FF;
        f2.diplo_agenda[faction_id] &= 0xFFFFF3FF;
        mine &= ~0x20u;
        theirs &= ~0x20u;
        mine |= 0x40;
        f1.diplo_merc[faction_id_with] = 50;
    }
    if ((theirs & 0x20) && !(theirs & 0x40000) && DiffLevel - 1 > 0) {
        int32_t level = DiffLevel;
        if (game_rand() % level != 0) {
            f2.diplo_agenda[faction_id] &= 0xFFFFF3FF;
            f1.diplo_agenda[faction_id_with] &= 0xFFFFF3FF;
            theirs &= ~0x20u;
            mine &= ~0x20u;
            set_treaty(faction_id_with, faction_id, 0x40, 1);
        }
    }
}

void GAMEAPI pact_unpact(int32_t faction_id, int32_t faction_id_with) {
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    parse_says(3, get_he_she(faction_id, 0), -1, -1);
    parse_says(4, get_his_her(faction_id, 0), -1, -1);
    parse_says(5, get_pact_hood(faction_id, faction_id_with), -1, -1);
    say_leader(6, faction_id_with);
    say_noun(8, faction_id_with);
    NetMsg_pop(&NetMsg, "PACTUNPACT", 5000, 0, nullptr);
}

void GAMEAPI enemies_unpact(int32_t faction_id, int32_t faction_id_with) {
    if (!(status(faction_id, faction_id_with) & 1)) return;
    int32_t v = CurrentPlayerFaction;
    if (status(faction_id, v) & 1)
        pact_unpact(faction_id, faction_id_with);
    else if (status(faction_id_with, v) & 1)
        pact_unpact(faction_id_with, faction_id);
    if ((status(v, faction_id) & 0x1000) || viewer_has_empath_guild() ||
        (v == GovernorFaction && !is_alien(faction_id)) || (status(v, faction_id_with) & 0x1000) ||
        viewer_has_empath_guild() || (v == GovernorFaction && !is_alien(faction_id_with)) || (GameState & 0x80)) {
        say_noun(0, faction_id);
        say_noun(1, faction_id_with);
        parse_says(2, get_pact_hood(faction_id, faction_id_with), -1, -1);
        NetMsg_pop(&NetMsg, "ENEMYPACTENDS", 5000, 0, nullptr);
    }
    status(faction_id, faction_id_with) &= 0xFEEFFFFE;
    status(faction_id_with, faction_id) &= 0xFEEFFFFE;
}

void GAMEAPI pact_truce(int32_t faction_id, int32_t faction_id_with) {
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    parse_says(3, get_he_she(faction_id, 0), -1, -1);
    say_leader(4, faction_id_with);
    say_noun(6, faction_id_with);
    NetMsg_pop(&NetMsg, "PACTTRUCE", 5000, 0, nullptr);
}

void GAMEAPI pact_treaty(int32_t faction_id, int32_t faction_id_with) {
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    parse_says(3, get_he_she(faction_id, 0), -1, -1);
    say_noun(4, faction_id_with);
    NetMsg_pop(&NetMsg, "PACTTREATY", 5000, 0, nullptr);
}

void GAMEAPI enemies_team_up(int32_t faction_id, int32_t faction_id_with, int32_t faction_id_tgt) {
    uint32_t& mine = status(faction_id, faction_id_tgt);
    if ((mine & 0x10) || !is_human(faction_id_tgt)) return;
    uint32_t theirs = status(faction_id_with, faction_id_tgt);
    if (theirs & 0x10) {
        if (!great_satan(faction_id_tgt, 0) && !(mine & 0x20)) {
            treaty_on(faction_id, faction_id_tgt, 0x2000);
            return;
        }
        if (faction_id_tgt == CurrentPlayerFaction) {
            say_noun(0, faction_id);
            NetMsg_pop(&NetMsg, "TEAM", 5000, 0, nullptr);
        }
        Factions[faction_id_tgt].diplo_spoke[faction_id_with] = CurrentTurn;
        Factions[faction_id_tgt].diplo_spoke[faction_id] = CurrentTurn;
        treaty_on(faction_id, faction_id_tgt, 0x10);
        return;
    }
    if (!great_satan(faction_id_tgt, 0) && !(mine & 0x20) && !(status(faction_id_with, faction_id_tgt) & 0x20)) {
        treaty_on(faction_id, faction_id_tgt, 0x2000);
        return;
    }
    mine |= 0x400;
}

// Whether faction_id may pass tech_id on to faction_id_to: not when its source (the faction it
// got the tech from) is a pact partner, nor a treaty partner it has little friction with.
// friction_of is the faction whose friction (and source) the original reads.
static bool tech_tradeable(int32_t owner, int32_t tech_id, uint32_t pact_status, int32_t friction_of) {
    int32_t src = Factions[owner].tech_trade_source[tech_id];
    if (src != 0 && (diplo_status_raw(owner, src) & 1)) return false;
    if (!(diplo_status_raw(owner, src) & 2)) return true;
    // BUG(original): when faction_id gives a tech, the friction is read with faction_id_with's
    // source of the tech (friction_of is the receiver in both directions).
    int32_t src2 = Factions[friction_of].tech_trade_source[tech_id];
    const int32_t* friction = reinterpret_cast<const int32_t*>(
        reinterpret_cast<const uint8_t*>(&Factions[0].diplo_friction[0]) + friction_of * (int32_t)sizeof(Faction) +
        (owner == friction_of ? src : src2) * 4);
    return *friction >= ((pact_status & 1) ? 6 : 16);
}

void GAMEAPI enemies_trade_tech(int32_t faction_id, int32_t faction_id_with) {
    int32_t tech_for_1 = -1, tech_for_2 = -1;
    int32_t best_1 = -9999, best_2 = -9999;
    if (GameRules & 0x2000000) return;
    for (int32_t t = 0; t < 89; t++) {
        if (!has_tech(t, faction_id) && has_tech(t, faction_id_with)) {
            if (tech_tradeable(faction_id_with, t, status(faction_id, faction_id_with), faction_id_with)) {
                int32_t v = tech_val(t, faction_id, 0);
                int32_t r = v - 1 > 0 ? game_rand() % v : 0;
                if (r > best_1) {
                    best_1 = r;
                    tech_for_1 = t;
                }
            }
        } else if (!has_tech(t, faction_id_with) && has_tech(t, faction_id)) {
            if (tech_tradeable(faction_id, t, status(faction_id, faction_id_with), faction_id_with)) {
                int32_t v = tech_val(t, faction_id_with, 0);
                int32_t r = v - 1 > 0 ? game_rand() % v : 0;
                if (r > best_2) {
                    best_2 = r;
                    tech_for_2 = t;
                }
            }
        }
    }
    if (tech_for_1 >= 0 && tech_for_2 >= 0) {
        tech_achieved(faction_id, tech_for_1, faction_id_with, 0);
        tech_achieved(faction_id_with, tech_for_2, faction_id, 0);
        return;
    }
    if (!(status(faction_id, faction_id_with) & 1) || !great_satan(FactionRankings[7], 0)) return;
    if (tech_for_1 >= 0 && Factions[RankingFactionIDUnk1].tech_ranking >= Factions[faction_id_with].tech_ranking)
        tech_achieved(faction_id, tech_for_1, faction_id_with, 0);
    if (tech_for_2 >= 0 && Factions[RankingFactionIDUnk1].tech_ranking >= Factions[faction_id].tech_ranking)
        tech_achieved(faction_id_with, tech_for_2, faction_id, 0);
}

void GAMEAPI enemies_treaty(int32_t faction_id, int32_t faction_id_with, int32_t trade_tech) {
    const int32_t p1 = faction_id, p2 = faction_id_with;
    if (trade_tech) enemies_trade_tech(p1, p2);
    int32_t v = CurrentPlayerFaction;
    uint32_t s = status(p1, p2);
    bool truce = false;
    if (!(s & 2) && (s & 0x14)) {
        truce = true;
        if (!(s & 0x60) && !(status(p2, p1) & 0x60) && (Factions[p2].ranking < 6 || Factions[p1].ranking < 6)) {
            int32_t level = (GameRules & 0x40) ? 5 : DiffLevel;
            int32_t r = game_rand() % 16;
            if (Factions[p1].AI_fight + Factions[p2].AI_fight - level + 8 <= r) truce = false;
        }
    }
    if (truce) {
        const char* label = nullptr;
        int32_t speaker = -1, other = -1;
        if (status(v, p1) & 1) {
            speaker = p1;
            other = p2;
        } else if (status(v, p2) & 1) {
            speaker = p2;
            other = p1;
        } else if (viewer_learns(p1, p2)) {
            say_noun(0, p1);
            say_noun(1, p2);
            label = "ENEMYTRUCE";
        }
        if (speaker >= 0) {
            say_leader(0, speaker);
            say_noun(2, speaker);
            parse_says(3, get_he_she(speaker, 0), -1, -1);
            say_leader(4, other);
            parse_says(6, get_noun(other), -1, -1);
            label = "PACTTRUCE";
        }
        if (label) NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
        treaty_on(p1, p2, 4);
        return;
    }
    s = status(p1, p2);
    if (!(s & 2)) {
        const char* label = nullptr;
        int32_t speaker = -1, other = -1;
        if (status(v, p1) & 1) {
            speaker = p1;
            other = p2;
        } else if (status(v, p2) & 1) {
            speaker = p2;
            other = p1;
        } else if (viewer_learns(p1, p2)) {
            say_noun(0, p1);
            say_noun(1, p2);
            label = "ENEMYTREATY";
        }
        if (speaker >= 0) {
            say_leader(0, speaker);
            say_noun(2, speaker);
            parse_says(3, get_he_she(speaker, 0), -1, -1);
            parse_says(4, get_noun(other), -1, -1);
            label = "PACTTREATY";
        }
        if (label) NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
        treaty_on(p1, p2, 2);
        return;
    }
    // A treaty: a pact against a common enemy (an aggressive leader, or a vendetta with a faction
    // ranked above one of them), unless pacts with others outnumber the enemy's blemishes.
    if ((FactionRankings[7] != p1 || Factions[p2].ranking < 5) && (FactionRankings[7] != p2 || Factions[p1].ranking < 5) &&
        !(s & 0x40020) && !(status(p2, p1) & 0x40020) && Factions[p1].major_atrocities == 0 &&
        Factions[p2].major_atrocities == 0) {
        int32_t pacts = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (i == p1 || i == p2) continue;
            uint32_t a8 = status(p1, i) & 8;
            if (!a8 && !(status(p2, i) & 8)) continue;
            if (!(a8 && (status(p2, i) & 8)) && !great_satan(i, 0)) continue;
            if ((status(p1, i) & 1) || (status(p2, i) & 1)) {
                int32_t rank = Factions[i].ranking;
                if (rank >= Factions[p1].ranking || rank >= Factions[p2].ranking) pacts++;
                continue;
            }
            if (Factions[i].diff_level == 0) continue;
            bool common_enemy = false;
            if (great_satan(i, 0)) {
                common_enemy = true;
            } else {
                uint32_t a = status(p1, i);
                if ((a & 7) || (status(p2, i) & 7)) continue;
                if (status(p1, p2) & 1)
                    common_enemy = true;
                else if ((a & 0x10) &&
                         (Factions[p1].ranking < Factions[i].ranking || Factions[p2].ranking < Factions[i].ranking))
                    common_enemy = true;
                if (!common_enemy) continue;
            }
            if (status(p1, p2) & 1) return;
            int32_t limit = great_satan(i, 0) ? Factions[i].integrity_blemishes : 0;
            if (pacts > limit) break;
            treaty_on(p1, p2, 1);
            enemies_team_up(p1, p2, i);
            enemies_team_up(p2, p1, i);
            if (i != v && !(status(v, p1) & 0x1000) && !viewer_has_empath_guild() &&
                (v != GovernorFaction || is_alien(p1)) && !(status(v, p2) & 0x1000) && !viewer_has_empath_guild() &&
                (v != GovernorFaction || is_alien(p2)))
                return;
            say_noun(0, p1);
            say_noun(1, p2);
            say_noun(2, i);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "ENEMIESTEAMUP");
            if (i != v)
                crt_strcat(StrBuffer, "1");
            else if (!(status(p1, i) & 0x10) && !(status(p2, i) & 0x10))
                crt_strcat(StrBuffer, "0");
            NetMsg_pop(&NetMsg, StrBuffer, 5000, 0, nullptr);
            return;
        }
    }
    enemies_unpact(p1, p2);
}

GLOBAL(int32_t, EncounterX, 0x68D43C);
GLOBAL(int32_t, EncounterY, 0x68D440);

void GAMEAPI encounter(int32_t faction_id, int32_t faction_id_with, int32_t x, int32_t y, int32_t is_monolith,
                       int32_t is_native) {
    const int32_t p1 = faction_id, p2 = faction_id_with;
    if (p1 == 0 || CurrentTurn == 0) return;
    if (p2 == 0) {
        if (p1 != CurrentPlayerFaction || !is_alien(p1)) return;
        if (is_monolith) interlude(0x2C, 0, 1, 0);
        if (is_native) interlude(0x2B, 0, 1, 0);
        return;
    }
    bool on_map = y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX;
    int32_t v = CurrentPlayerFaction;
    if (is_alien(p1) && is_alien(p2)) {
        treaty_on(p1, p2, 0x80000008);
        if (on_map) treaty_on(p1, p2, 0x40000000);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, MFactions[p1].search_key);
        win32::CharUpperA(StrBuffer);
        if (!crt_strcmp(StrBuffer, "CARETAKE")) {
            if (p1 == v) interlude(0x27, 0, 1, 0);
            if (p2 == v) interlude(0x28, 0, 1, 0);
        } else {
            if (p1 == v) interlude(0x28, 0, 1, 0);
            if (p2 == v) interlude(0x27, 0, 1, 0);
        }
        return;
    }
    if (is_alien(p1)) {
        if (!has_tech(0x4E, p2) && !has_tech(4, p1)) {
            treaty_on(p1, p2, 0x80000008);
            if (on_map) treaty_on(p1, p2, 0x40000000);
            if (p1 == v) interlude(0x22, 0, 1, 0);
            if (p2 == v) interlude(0x16, 0, 1, 0);
            return;
        }
        if (p1 == v) interlude(0x23, 0, 1, 0);
        if (p2 == v) interlude(0x17, 0, 1, 0);
    }
    if (is_alien(p2)) {
        if (!has_tech(0x4E, p1) && !has_tech(4, p2)) {
            treaty_on(p1, p2, 0x80000008);
            if (on_map) treaty_on(p1, p2, 0x40000000);
            if (p1 == v) interlude(0x16, 0, 1, 0);
            if (p2 == v) interlude(0x22, 0, 1, 0);
            return;
        }
        if (p1 == v) interlude(0x17, 0, 1, 0);
        if (p2 == v) interlude(0x23, 0, 1, 0);
    }
    if (SunspotDuration > 0) {
        treaty_on(p1, p2, 0x80000008);
        if (on_map) treaty_on(p1, p2, 0x40000000);
        return;
    }
    EncounterX = x;
    EncounterY = y;
    comm_check(p1, p2);
    comm_check(p2, p1);
    EncounterY = -1;
    EncounterX = -1;
    uint32_t s = status(p1, p2);
    if (!(s & 8) && (is_alien(p1) || is_alien(p2))) {
        const char* label = nullptr;
        if (p1 == v || p2 == v) {
            int32_t self = p1 == v ? p1 : p2, other = p1 == v ? p2 : p1;
            say_noun(0, other);
            if (!is_alien(self)) {
                if (!has_tech(0x4E, self) && !has_tech(4, other)) {
                    if (tut_check2(0x8000)) label = "GUYMEETSALNOPSYCH";
                } else if (tut_check2(0x8000)) {
                    label = "GUYMEETSAL";
                }
            } else if (!is_alien(other)) {
                if (!has_tech(4, self) && !has_tech(0x4E, other)) {
                    if (tut_check2(0x4000)) label = "ALMEETSGUYNOPSYCH";
                } else if (tut_check2(0x4000)) {
                    label = "ALMEETSGUY";
                }
            } else if (tut_check2(0x10000)) {
                label = "ALMEETSAL";
            }
        }
        if (label) TutWin_tut_map(&TutWin, label, x, y, -1, nullptr, 0, -1, -1);
    }
    treaty_on(p1, p2, 0x88000008);
    if (on_map) treaty_on(p1, p2, 0x40000000);
    if (!is_human(p1) && !is_human(p2) && (((CurrentTurn + p1 + p2) & 3) == 0 || !(s & 8)) &&
        (!is_alien(p1) || !is_alien(p2))) {
        if ((Factions[p1].diplo_agenda[p2] & 0x2000) && (!is_alien(p1) || !is_alien(p2))) {
            enemies_trade_tech(p1, p2);
            return;
        }
        if (!wants_to_attack_fresh(p1, p2, 0) && !wants_to_attack(p2, p1, 0)) {
            enemies_treaty(p1, p2, 1);
            return;
        }
        enemies_war(p1, p2);
    }
}

int32_t GAMEAPI territory(int32_t faction_id, int32_t faction_id_with, int32_t flags, int32_t* base_id_out,
                          int32_t* count_out) {
    int32_t count = 0;
    if (base_id_out) *base_id_out = -1;
    if (count_out) *count_out = 0;
    uint32_t s = status(faction_id, faction_id_with);
    if (s & 0x11) return 0;
    if (!(flags & 2) && (s & 0x200)) return 0;
    // Regions where both have a common enemy with bases: violations there are tolerated.
    int32_t base_id = s & 0x20;  // also the whose_territory output slot (see below)
    int32_t tolerated[64];
    for (int32_t r = 0; r < 64; r++) {
        tolerated[r] = 0;
        if (base_id == 0 && Factions[faction_id_with].integrity_blemishes < 5) {
            for (int32_t i = 1; i < 8; i++) {
                if (i != faction_id && i != faction_id_with && (status(faction_id, i) & 0x10) &&
                    (status(faction_id_with, i) & 0x10) && Factions[i].region_total_bases[r]) {
                    tolerated[r] = 1;
                    break;
                }
            }
        }
    }
    if (VehCount <= 0) return count;
    for (int32_t i = 0; i < VehCount; i++) {
        VEH& veh = Vehs[i];
        veh.state &= ~0x400u;
        if (veh.faction_id != faction_id_with) continue;
        uint8_t plan = (uint8_t)Units[veh.unit_id].plan;
        if (!(plan <= 8 || (plan == 10 && veh.order == 3))) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((sq->climate & 0xE0) < 0x60 || tolerated[sq->region]) continue;
        // BUG(original): base_id keeps its previous value (initially status bit 0x20) when
        // whose_territory does not write it.
        if (whose_territory(faction_id_with, veh.x, veh.y, &base_id, 1) != faction_id || base_id < 0) continue;
        if (base_id_out) *base_id_out = base_id;
        count++;
        veh.state |= 0x400;
        if (map_tile(veh.x, veh.y)->items & 0x81608850) count++;
        if (!(flags & 1)) continue;
        int32_t dx = std::abs(Bases[base_id].x - veh.x);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
        int32_t dist = (std::abs(Bases[base_id].y - veh.y) + dx) & ~1;
        if (dist <= 4 || (status(faction_id, faction_id_with) & 0xC20) || DiploFriction > 12) {
            if (count_out && (veh.flags & 1)) (*count_out)++;
            veh.flags |= 3;
            if (!(veh.state & 0x800) && SunspotDuration < 1)
                veh.state |= 0x800;
            else
                count += ((status(faction_id, faction_id_with) & 2) << 1) | 1;
        }
    }
    if (count) Factions[faction_id].player_flags |= 0x10000;
    return count;
}

// ---- atrocities --------------------------------------------------------------------------------

void GAMEAPI atrocity(int32_t faction_id, int32_t faction_id_vs, int32_t count_it, int32_t is_probe) {
    const int32_t f = faction_id;
    uint32_t& victim = status(faction_id_vs, f);
    uint32_t was_victim = victim & 0x40000;
    victim |= 0x40020;
    Factions[faction_id_vs].diplo_agenda[f] |= 4;
    if (ProposalPassCount[9] < ProposalPassCount[8]) return;  // atrocity prohibitions repealed
    if (SunspotDuration > 0) {
        if (f == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "ATROCITYSPOTS", 5000, 0, nullptr);
        return;
    }
    if (ExpansionEnabled && !is_probe && (is_alien(f) || is_alien(faction_id_vs))) return;
    int32_t i = 1;
    for (; i < 8; i++) {
        if (i == f || i == faction_id_vs) continue;
        uint32_t& theirs = status(i, f);
        uint32_t t = theirs;
        if (!(t & 8)) continue;
        if (ExpansionEnabled && is_probe && !is_alien(f) && is_alien(i)) continue;
        if (t & 0x200000) {
            cause_friction(i, f, 5);
            uint32_t mine = status(f, i);
            if (!is_human(i) && is_human(f) && (mine & 8) && !(mine & 0x11) && !was_victim) {
                bool declare = true;
                if (!(theirs & 0x20)) {
                    if (great_satan(f, 0)) {
                        int32_t r = game_rand() % 16;
                        declare = r - Factions[f].atrocities <= 8;
                    } else {
                        int32_t m = (8 - ((GameRules & 0x40) ? 5 : DiffLevel)) * 2;
                        int32_t r = m - 1 > 0 ? game_rand() % m : 0;
                        declare = r == 0;
                    }
                }
                if (declare) {
                    if (f == CurrentPlayerFaction) {
                        say_leader(0, f);
                        diplomacy_caption(f, i);
                        X_pops("ATROCIOUSITY", FactionPortraits[i], 0);
                    }
                    treaty_on(f, i, 0x10);
                    theirs |= 0x40;
                    Factions[i].diplo_merc[f] = 50;
                }
            }
        }
        if (!(theirs & 0x10)) treaty_on(i, f, 0x400000);
    }
    Faction& fac = Factions[f];
    if (count_it || fac.atrocities == 0) {
        fac.atrocities++;
        if (!(status(f, faction_id_vs) & 0x40000)) fac.integrity_blemishes++;
    }
    int32_t atrocities = fac.atrocities;
    if (atrocities > (8 - fac.diff_level) * 4) {
        major_atrocity(f, faction_id_vs);
        return;
    }
    // BUG(original): the "victim is an AI" test uses the loop counter (8) instead of the victim:
    // always true.
    if (is_human(f) && !(FactionStatus[0] & (1u << i)) && !was_victim && atrocities >= 5)
        Factions[faction_id_vs].player_flags |= 0x1000000;
    say_leader(0, f);
    say_leader(2, faction_id_vs);
    parse_num(0, fac.atrocities * 10);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "ATROCITY");
    // BUG(original): "1" is meant for the victim being the viewer; the test is victim == f.
    if (f == CurrentPlayerFaction)
        crt_strcat(StrBuffer, "2");
    else if (faction_id_vs == f)
        crt_strcat(StrBuffer, "1");
    if (!is_alien(f)) NetMsg_pop(&NetMsg, StrBuffer, 5000, 0, nullptr);
    fac.sanction_turns += atrocities * 10;
}

void GAMEAPI major_atrocity(int32_t faction_id, int32_t faction_id_vs) {
    const int32_t f = faction_id;
    if (faction_id_vs >= 0) {
        status(faction_id_vs, f) |= 0xC0020;
        Factions[faction_id_vs].diplo_agenda[f] |= 4;
    }
    if (ProposalPassCount[9] < ProposalPassCount[8]) return;
    Faction& fac = Factions[f];
    if (faction_id_vs < 0) {
        fac.integrity_blemishes += 2;
    } else if (!(status(f, faction_id_vs) & 0x80000)) {
        int32_t v = fac.integrity_blemishes + 2;
        if (v < 6) v = 6;
        if (v > 99) v = 99;
        fac.integrity_blemishes = v;
    }
    fac.major_atrocities++;
    for (int32_t i = 1; i < 8; i++) {
        if (i == f || i == faction_id_vs || is_human(i) || !(FactionStatus[1] & (1 << i))) continue;
        if (status(f, i) & 0x10) continue;
        if (status(f, i) & 1) pact_ends(f, i);
        treaty_on(i, f, 0x18);
        status(i, f) |= 0x40;
        Factions[i].diplo_merc[f] = 50;
        fac.diplo_spoke[i] = CurrentTurn;
        say_leader(0, i);
        say_noun(2, i);
        say_leader(3, f);
        say_noun(5, f);
        char num[80];
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "MAJORATROCITY");
        crt_itoa(f != CurrentPlayerFaction, num, 10);
        crt_strcat(StrBuffer, num);
        popp(ScriptFileName, StrBuffer, 0, "council_sm.pcx", 0);
    }
}

int32_t GAMEAPI break_treaty(int32_t faction_id, int32_t faction_id_with, int32_t status_bits) {
    uint32_t s = status(faction_id, faction_id_with);
    if ((s & 0x10) || !((uint32_t)status_bits & s)) return 0;
    say_leader(0, faction_id_with);
    say_noun(2, faction_id_with);
    parse_says(3, get_pact_hood(faction_id, faction_id_with), -1, -1);
    parse_says(4, MFactions[faction_id_with].adj_name_faction, -1, -1);
    s = status(faction_id, faction_id_with);
    if (s & 1) {
        NetMsg_pop(&NetMsg, "BREAKINGPACT", -5000, 0, "break_sm.pcx");
        return 1;
    }
    if (s & 2) return popp(ScriptFileName, "BREAKINGTREATY", 0, "break_sm.pcx", 0) == 0;
    if ((s & 4) && !(s & 0x10000)) return popp(ScriptFileName, "BREAKINGTRUCE", 0, "break_sm.pcx", 0) == 0;
    if (!((uint32_t)status_bits & 0x10000)) return popp(ScriptFileName, "BEGINVENDETTA", 0, "break_sm.pcx", 0) == 0;
    return 0;
}

// ---- maps and bases ----------------------------------------------------------------------------

void GAMEAPI trade_maps(int32_t faction_id, int32_t faction_id_with) {
    DiploMapMsg = 0;
    uint32_t a = Factions[faction_id].traded_maps | (1u << faction_id_with) | Factions[faction_id_with].traded_maps;
    Factions[faction_id].traded_maps = a;
    Factions[faction_id_with].traded_maps |= (1u << faction_id) | a;
    uint8_t bit1 = (uint8_t)(1 << faction_id), bit2 = (uint8_t)(1 << faction_id_with);
    for (int32_t i = 0; i < MapAreaTiles; i++) {
        MAP& sq = MapTiles[i];
        uint8_t vis = sq.visibility;
        if (vis & bit1) {
            sq.visibility = vis | bit2;
            if (faction_id && faction_id_with)
                sq.visible_items[faction_id_with - 1] |= sq.visible_items[faction_id - 1] & sq.items;
        } else if (vis & bit2) {
            DiploMapMsg = 1;
            sq.visibility |= bit1;
            if (faction_id && faction_id_with)
                sq.visible_items[faction_id - 1] |= sq.visible_items[faction_id_with - 1] & sq.items;
        }
    }
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((Bases[i].visibility & bit1) || Bases[i].faction_id == faction_id)
            spot_base(i, faction_id_with);
        else if ((Bases[i].visibility & bit2) || Bases[i].faction_id == faction_id_with)
            spot_base(i, faction_id);
    }
    if (faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction) {
        GameDrawState |= 4;
        GraphicWin_redraw(&WorldWin);
        draw_map(1);
    }
}

int32_t GAMEAPI value_of_base(int32_t base_id, int32_t faction_id, int32_t faction_id_with, int32_t is_threat,
                              int32_t is_gift) {
    if (base_id < 0) return -1;
    BASE& base = Bases[base_id];
    int32_t dist = vulnerable(faction_id, base.x, base.y);
    if (dist < 1) return -1;
    int32_t region = map_tile(base.x, base.y)->region;
    for (int32_t k = 1; k < 169; k++) {
        int32_t x = map_wrap_x(TableOffsetX[k] + base.x);
        int32_t y = TableOffsetY[k] + base.y;
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        int32_t b = base_at(x, y);
        if (b < 0) continue;
        if (Bases[b].faction_id == faction_id) {
            if (map_tile(x, y)->region == region) dist--;
            break;
        }
        if (map_tile(x, y)->region == region) {
            if (Bases[b].faction_id != faction_id_with) break;
            dist++;
        }
    }
    int32_t veh_id = stack_fix(veh_at(base.x, base.y));
    int32_t credits = Factions[faction_id_with].energy_credits;
    if (Factions[faction_id].energy_credits > credits) credits = Factions[faction_id].energy_credits;
    int32_t pop = base.pop_size < 3 ? 3 : base.pop_size;
    int32_t value = (credits + 1000) / (dist + 4) * pop;
    if (value < 100) value = 100;
    MAP* sq = map_tile(base.x, base.y);
    if (!(sq->items & 2) || (sq->val2 & 0xF) >= 8) value /= 2;
    uint8_t bases_with = Factions[faction_id_with].region_total_bases[region];
    uint8_t bases_own = Factions[faction_id].region_total_bases[region];
    if (!bases_with) value *= 2;
    if (bases_own == 1) {
        if (!bases_with) value *= 2;
        if (bases_with == 1) value *= 2;
    }
    if (is_gift) value <<= 2;
    if (bases_with && bases_own) {
        if (bases_with >= bases_own * 5) value /= 2;
        if (is_threat) value /= 2;
    }
    int32_t built = 0;
    for (int32_t i = 1; i < 70; i++) {
        if (i >= 65) continue;
        int32_t offset, mask;
        bitmask(i, &offset, &mask);
        if (base.facilities_built[offset] & (uint8_t)mask) {
            built++;
            value += Facility[i].cost * built;
        }
    }
    for (int32_t i = 0; i < 64; i++)
        if (SecretProjects[i] == base_id) value += Facility[70 + i].cost * 25;
    for (int32_t i = stack_top(veh_id); i >= 0; i = Vehs[i].next_veh_id_stack)
        if (Vehs[i].faction_id == faction_id) value += Units[Vehs[i].unit_id].cost * 2;
    if (!crt_strcmp(MFactions[faction_id].filename, "BELIEVE")) value *= 2;
    if (is_objective(base_id)) value <<= 2;
    return value;
}

void GAMEAPI give_a_base(int32_t base_id, int32_t faction_id) {
    BASE& base = Bases[base_id];
    int32_t bx = base.x, by = base.y;
    uint8_t old = base.faction_id;
    for (int32_t k = 0; k < 21; k++) {
        int32_t x = map_wrap_x(TableOffsetX[k] + bx);
        int32_t y = TableOffsetY[k] + by;
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        map_tile(x, y)->visibility |= (uint8_t)(1 << faction_id);
        synch_bit(x, y, faction_id);
    }
    for (int32_t i = VehCount - 1; i >= 0; i--) {
        VEH& veh = Vehs[i];
        if (veh.faction_id != old) continue;
        int32_t dx = std::abs(veh.x - bx);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
        int32_t dy = std::abs(veh.y - by);
        dx = std::abs(dx);
        dy = std::abs(dy);
        int32_t hi = dx > dy ? dx : dy, lo = dx < dy ? dx : dy;
        int32_t dist = hi - ((((dx + dy) >> 1) - lo + 1) >> 1);
        bool land = (map_tile(veh.x, veh.y)->climate & 0xE0) >= 0x60;
        if (dist > (land ? 1 : 0)) continue;
        int32_t plan = (uint8_t)Units[veh.unit_id].plan;
        bool transfer = ((plan == 2 || plan == 3) && dist == 0) ||
                        (unit_triad(veh.unit_id) == 1 && (dist == 0 || land));
        if (!transfer) {
            base.faction_id = (uint8_t)faction_id;
            int32_t nb = base_find_2(bx, by, old);
            base.faction_id = old;
            if (nb >= 0) {
                int32_t ndx = std::abs(Bases[nb].x - bx);
                if (!(MapToggleFlat & 1) && ndx > MapHalfX) ndx = MapAreaX - ndx;
                if ((std::abs(Bases[nb].y - by) + ndx) & ~1) {
                    stack_put(stack_veh(i, 1), Bases[nb].x, Bases[nb].y);
                    veh.order = 0;
                    if (veh.home_base_id == base_id) veh.home_base_id = (int16_t)nb;
                    continue;
                }
            }
        }
        Factions[old].units_active[veh.unit_id]--;
        veh.faction_id = (uint8_t)faction_id;
        Factions[faction_id].units_active[veh.unit_id]++;
        veh.home_base_id = (int16_t)base_id;
        veh.moves_spent = 0;
        if (veh.order != 2) veh.order = 0;
        owner_set(veh.x, veh.y, faction_id);
    }
    base.faction_id = (uint8_t)faction_id;
    base.minerals_accumulated_2 = 0;
    base.minerals_accumulated = 0;
    for (int32_t k = 0; k <= base.queue_size; k++)
        if (base.queue_items[k] >= 0) Factions[old].units_queue[base.queue_items[k]]--;
    base.queue_size = 0;
    base.production_id_last = -0x45;
    base.queue_items[0] = -0x45;
    replay_base(1, bx, by, faction_id);
    base.state_flags &= 0xFFDEF7B1;
    base.nerve_staple_turns_left = 0;
    if (DiploCounterProposal == 3) {
        if (base.faction_id_former == faction_id) {
            base.assimilation_turns_left = 0;
        } else {
            base.faction_id_former = old;
            base.assimilation_turns_left = 50;
        }
    } else {
        if (base.faction_id_former == faction_id) base.assimilation_turns_left = 0;
        base.faction_id_former = (uint8_t)faction_id;
    }
    spot_base(base_id, old);
    reset_territory();
    for (int32_t i = VehCount - 1; i >= 0; i--)
        if (i < VehCount && Vehs[i].faction_id == old && Vehs[i].home_base_id == base_id) kill(i);
    owner_set(bx, by, faction_id);
    draw_tile_fixup(bx, by, 1, 2);
    set_base(base_id);
    base_compute(1);
}

// ---- small text helpers of the diplomacy dialogs ------------------------------------------------

const char* GAMEAPI get_adjective_leader(int32_t faction_id) { return MFactions[faction_id].adj_leader; }
const char* GAMEAPI get_adjective_insult_leader(int32_t faction_id) {
    return MFactions[faction_id].adj_insult_leader;
}
const char* GAMEAPI get_adjective_faction(int32_t faction_id) { return MFactions[faction_id].adj_faction; }
const char* GAMEAPI get_adjective_insult_faction(int32_t faction_id) {
    return MFactions[faction_id].adj_insult_faction;
}
const char* GAMEAPI get_insult_leader(int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = MFactions[faction_id].is_leader_female;
    return MFactions[faction_id].insult_leader;
}
int32_t GAMEAPI bad_reg(int32_t region) { return (region & 0x3F) == 0x3F || (region & 0x3F) == 0; }
uint32_t GAMEAPI has_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda) {
    return Factions[faction_id].diplo_agenda[faction_id_with] & agenda;
}
int32_t GAMEAPI round_down(int32_t value, int32_t step) { return value / step * step; }

// ---- first-half callee (0x53A780), needed by the AI strategy ----------------------------------

int32_t GAMEAPI contiguous(int32_t faction_id, int32_t faction_id_with, int32_t region, int32_t a4) {
    if (region != 0 &&
        (!Factions[faction_id].region_total_bases[region] || !Factions[faction_id_with].region_total_bases[region]))
        return 0;
    for (int32_t i = 0; i < MapAreaTiles; i++) MapTiles[i].unk_1 = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id != faction_id) continue;
        MAP* sq = map_tile(Bases[i].x, Bases[i].y);
        int32_t r = sq->region;
        if ((region != 0 && r != region) || sq->unk_1) continue;
        Path_territory(&Paths, Bases[i].x, Bases[i].y, r, faction_id);
        for (int32_t j = 0; j < BaseCount; j++) {
            if (Bases[j].faction_id != faction_id_with) continue;
            MAP* sq2 = map_tile(Bases[j].x, Bases[j].y);
            if (sq2->region != r || sq2->unk_1) continue;
            Path_territory(&Paths, Bases[j].x, Bases[j].y, r, faction_id_with);
            // BUG(original)? faction_id is passed as the unit id.
            if (Path_find(&Paths, Bases[i].x, Bases[i].y, Bases[j].x, Bases[j].y, faction_id, faction_id_with,
                          (a4 ? 0xC00 : 0) | 0x201, -1) >= 0)
                return 1;
        }
    }
    return 0;
}

// at_climax (0x539EF0, first half of the module), needed by the AI and base production.
int32_t GAMEAPI at_climax(int32_t faction_id) {
    if (is_human(faction_id) || (GameState & 1) || DiffLevel == 0 || !climactic_battle()) return 0;
    if (aah_ooga(faction_id, faction_id)) return 1;
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || Factions[i].corner_market_turn <= CurrentTurn) continue;
        if (!(status(faction_id, i) & 1) || !(GameRules & 0x1000)) return 1;
    }
    int32_t mine = 0, theirs = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].queue_items[0] != -FAC_ASCENT_TO_TRANSCENDENCE) continue;
        int32_t minerals = Bases[i].minerals_accumulated;
        if (Bases[i].faction_id == faction_id) {
            if (mine <= minerals) mine = minerals;
        } else if (theirs <= minerals) {
            theirs = minerals;
        }
    }
    // The original asks ascending(i) && !ascending(faction_id) for every other faction; ascending
    // takes no argument (Voice of Planet built), so that can never hold.
    for (int32_t i = 1; i < 8; i++)
        if (i != faction_id && ascending() && !ascending()) return 1;
    return theirs != 0 && theirs > mine;
}
