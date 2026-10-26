// Diplomacy (original module around 0x538F30-0x55F930), first half (0x538F30-0x54B140) and the
// shared treaty/agenda helpers; the second half is in diplomacy_b.cpp, the start of diplo_act
// (0x50B8A0-0x50E890) in diplo_act.cpp, action.cpp and probe.cpp.
//
// Classification of the functions of modules diplomacy and diplo_act (re/out/index.tsv):
//   (a) game logic, reconstructed (engine code; popups go through the UI bridge),
//   (b) human dialog / UI, bridged (ADDR_NATIVE in engine/ui_bridge.h, headless stubs in
//       src/headless/ui_headless_diplomacy.cpp); their game logic, where any, is in (a) callees,
//   (c) reconstructed earlier (other files), listed for completeness.
// Functions that build a menu on a Popup object on their stack keep the menu behind a
// "not original" ui_* function (ui_bridge.h / ui_bridge_diplomacy.cpp) and the logic here.
//
//   diplo_act 0x50B8A0 say_num ................. (c) probe.cpp
//   diplo_act 0x50B8D0 POP3_2 .................. (a) diplo_act.cpp (POP3 wrapper)
//   diplo_act 0x50B8F0 FUN_0050b8f0 ............ (a) diplo_act.cpp veh_stack_next
//   diplo_act 0x50B910 get_adjective ........... (c) probe.cpp
//   diplo_act 0x50B930 get_noun ................ (c) probe.cpp
//   diplo_act 0x50B970 popp_2 .................. (c) probe.cpp
//   diplo_act 0x50B9A0 say_stats_3_2 ........... (a) diplo_act.cpp
//   diplo_act 0x50B9C0 FUN_0050b9c0 ............ (a) diplo_act.cpp NetMsg_pop_3
//   diplo_act 0x50B9E0 NetMsg_pop_2 ............ (c) probe.cpp
//   diplo_act 0x50BA00-0x50E5C0 bitmask .. convoy (c) diplo_act.cpp, action.cpp, probe.cpp
//   diplo_act 0x50E820 mandate_color ........... (a) diplo_act.cpp (report colours, pure)
//   0x538F30-0x539160 danger, game_srand, checksum* (c) net_misc.cpp, random.cpp
//   0x539160 auto_contact ...................... (a)
//   0x5391C0-0x5397B0 net_* .................... (a) single player: the plain change
//            (net_treaty_on/off, net_set_treaty, net_tech in probe.cpp; the rest here)
//   0x539820 diplo_lock, 0x5398C0 diplo_unlock . (a) multiplayer lock; single player: log only
//   0x5398E0 pop_wait, 0x539920 pop_expire ..... (a) popup wait callbacks
//   0x5399A0 diplomacy_caption ................. (a) mood caption (friction clamp), redraw bridged
//   0x539B70-0x539EF0 great_beelzebub .. at_climax (c)
//   0x53A030 cause_friction .................... (c) action.cpp
//   0x53A090 get_mood, 0x53A150 get_patience ... (a)
//   0x53A100 reputation ........................ (c)
//   0x53A1C0 energy_value ...................... (a)
//   0x53A230 mention_prototypes ................ (a) prototype exchange
//   0x53A4A0 scan_prototypes ................... (a)
//   0x53A780 contiguous ........................ (c)
//   0x53A980 diplomacy_check ................... (a) the AI's attitude (Diplo* globals)
//   0x53C370 pact_withdraw, 0x53C650 pact_ends . (c)
//   0x53CAF0 pact_of_brotherhood ............... (a) pact signed
//   0x53CE10 make_treaty ....................... (a) treaty signed
//   0x53D160 pledge_truce ...................... (a) truce signed
//   0x53D360 introduce ......................... (a) opening words (random), planet buster warning
//   0x53DCE0 wants_to_speak .................... (a) COMMLINK call (COMM menu)
//   0x53E470 diplomacy_ends .................... (a) dialog state reset (DiploPop_stop bridged)
//   0x53E4C0 demands_withdrawal ................ (a) panel action (commlink_attempter bridged)
//   0x53E5C0 renounce_pact ..................... (a) panel action
//   0x53E800 tech_analysis ..................... (a)
//   0x53EB50 buy_council_vote .................. (a) vote buying (BUYVOTEMENU)
//   0x5401A0 buy_tech .......................... (a)
//   0x540630 energy_trade ...................... (a) gifts, technology for energy, loans
//   0x541100 tech_trade ........................ (a) technology trades (TRADETECH menu)
//   0x543A20 trade_maps ........................ (c)
//   0x543BC0 propose_pact ...................... (a) the AI's acceptance, bribes, help requests
//   0x544E40 propose_treaty .................... (a) as propose_pact
//   0x545D60 propose_trade_maps ................ (a)
//   0x545E80 propose_attack .................... (a) war price (MAYBEWARENERGY menu)
//   0x547C10 make_gift ......................... (a) (OFFERENERGY menu)
//   0x548DD0 dont_withdrawal, 0x548F80 do_withdrawal, 0x549270 demand_withdrawal (a)
//   0x549930 threaten .......................... (a) extortion, war on demand, base cession
//   0x54ACC0 suggest_plan, 0x54AFA0 attack_from  (a) battle plan targets (pure)
//   0x54B140 battle_routine .................... (b) map highlight callback of battle_plans
//   0x54B1C0 battle_plans ...................... (b) map dialog; game effects (trade_maps, the
//            partner's base_id_attack_target / target_x,y) are not reconstructed
//   0x54BEC0 call_off_vendetta ................. (a) (CALLOFF menu)
//   0x54C560 diplomacy_menu .................... (b) topic menu
//   0x54CB50 value_of_base, 0x54CF40 give_a_base (c) diplomacy_b.cpp
//   0x54D410 base_swap ......................... (a) base trades (no menu)
//   0x54DC90 FUN_0054dc90 ...................... (b) map highlight callback of proposal_menu
//   0x54DCF0 proposal_menu ..................... (b) proposal selection menu
//   0x54F420 make_a_proposal ................... (b) dispatcher to the (a) proposals
//   0x54FFD0 communicate ....................... (b) diplomacy_b.cpp / ui_bridge.h
//   0x5589E0 commlink_attempter ................ (b) UI checks (keys, multiplayer channels)
//   0x558C60 commlink_attempt .................. (b) click handler
//   0x558FC0 BasePop_exec, 0x558FE0 BasePop_item, 0x559000 FUN_00559000,
//   0x559040 Popup_start_3 ..................... (b) Popup wrappers (not called by the engine)
//   0x559060-0x559210 get_name .. round_down ... (c)
//   0x559290 FUN_00559290, 0x5592D0 FUN_005592d0, 0x5593E0 pick_top_veh, 0x5594F0 veh_draw,
//   0x55A150 veh_scoot, 0x55AF20 base_draw ..... (b) drawing (veh_scoot bridged earlier)
//   0x55B760-0x55F770 treaty_off .. break_treaty (c) diplomacy.cpp, diplomacy_b.cpp, action.cpp
// Also reconstructed here as callees: energy_compute (ui module, the energy budget that
// diplomacy_check reads) and the game-module text helpers say_fac_special, get_phrase,
// get_noun_phrase, get_pact, get_pacts, get_pacts_2.
#include "engine/diplomacy.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/clock.h"
#include "engine/compat.h"
#include "engine/council.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/game_state.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"

void GAMEAPI treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status) {
    if (status & 7) status |= 0x1100000;
    if (status & 0x10) status |= 0x8040;
    if (status & 0x20) {
        game().factions[faction_id].diplo_agenda[faction_id_with] &= 0xFFFFF3FF;
        game().factions[faction_id_with].diplo_agenda[faction_id] &= 0xFFFFF3FF;
    }
    game().factions[faction_id].diplo_status[faction_id_with] &= ~status;
    game().factions[faction_id_with].diplo_status[faction_id] &= ~status;
}

void GAMEAPI agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda) {
    if (agenda & 0x20) {
        game().factions[faction_id].diplo_agenda[faction_id_with] &= 0xFFFFFFBF;
        game().factions[faction_id_with].diplo_agenda[faction_id] &= 0xFFFFFFBF;
    }
    game().factions[faction_id].diplo_agenda[faction_id_with] |= agenda;
    game().factions[faction_id_with].diplo_agenda[faction_id] |= agenda;
}

void GAMEAPI set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add) {
    if (!add) {
        game().factions[faction_id].diplo_status[faction_id_with] &= ~status;
        return;
    }
    game().factions[faction_id].diplo_status[faction_id_with] |= status;
    if (status & 0x40) game().factions[faction_id].diplo_merc[faction_id_with] = 50;
}

void GAMEAPI set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add) {
    if (add)
        game().factions[faction_id].diplo_agenda[faction_id_with] |= agenda;
    else
        game().factions[faction_id].diplo_agenda[faction_id_with] &= ~agenda;
}

int32_t GAMEAPI great_satan(int32_t faction_id, int32_t is_aggressive) {
    if (!(FactionStatus[0] & (1 << faction_id)) || cs.FactionRankings[7] != faction_id) return 0;
    const Faction& f = game().factions[faction_id];
    int32_t min_bases = (CurrentTurn + 25) / 50;
    if (min_bases < 4) min_bases = 4;
    if (f.base_count <= min_bases) return 0;
    int32_t delay;
    if (f.diff_level <= 1 && !(GameRules & RULES_INTENSE_RIVALRY) && !is_aggressive) return 0;
    if (GameRules & RULES_INTENSE_RIVALRY)
        delay = 0;
    else
        delay = (5 - f.diff_level) * 50;
    if (CurrentTurn <= delay + 100) return 0;
    if (climactic_battle() && aah_ooga(faction_id, -1) == faction_id) return 1;
    int32_t mine, other;
    if (GameRules & RULES_INTENSE_RIVALRY) {
        mine = 4;
        other = 5;
    } else if (f.diff_level < 3 && !(GameRules & RULES_VICTORY_CONQUEST) && ObjectiveReqVictory > 1000) {
        mine = 1;
        other = 2;
    } else {
        mine = 2;
        other = 3;
    }
    return FactionRankingsUnk[cs.FactionRankings[7]] * mine >= FactionRankingsUnk[cs.FactionRankings[6]] * other;
}

int32_t GAMEAPI aah_ooga(int32_t faction_id, int32_t faction_id_vs) {
    if (!(GameRules & RULES_VICTORY_DIPLOMATIC)) return 0;
    int32_t total = 0;
    for (int32_t i = 1; i < 8; i++) total += council_votes(i);
    // The faction that could be elected (half of the votes, the governor proposal's tech known
    // to faction_id or reachable); faction_id itself wins ties.
    int32_t result = 0;
    for (int32_t i = 1; i < 8; i++) {
        int32_t votes = council_votes(i);
        if (i == faction_id_vs) continue;
        // BUG(original): with faction_id_vs == -1 this reads the word before diplo_status
        // (last_base_turn).
        COMPAT_ORIGINAL(out_of_range_reads);
        const uint32_t* status =
            reinterpret_cast<const uint32_t*>(&game().factions[i]) + offsetof(Faction, diplo_status) / 4;
        if (faction_id_vs && (status[faction_id_vs] & DIPLO_PACT) &&
            (GameRules & RULES_VICTORY_COOPERATIVE))
            continue;
        int32_t tech_id = Proposal[1].preq_tech;
        if (!has_tech(tech_id, faction_id)) {
            if (tech_id < 0) continue;
            if (!has_tech(Tech[tech_id].preq_tech1, faction_id) && !has_tech(Tech[tech_id].preq_tech2, faction_id))
                continue;
        }
        if (votes * 2 >= total && (!result || i == faction_id)) result = i;
    }
    return result;
}

int32_t GAMEAPI climactic_battle() {
    for (int32_t i = 1; i < 8; i++) {
        if ((FactionStatus[0] & (1 << i)) && game().factions[i].corner_market_turn > CurrentTurn) return 1;
    }
    if (aah_ooga(0, -1)) return 1;
    if (SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1) {
        for (int32_t i = 1; i < 8; i++) {
            if (!(FactionStatus[0] & (1 << i))) continue;
            if (has_tech(Facility[FAC_PSI_GATE].preq_tech, i)) return 1;
            if (has_tech(Facility[FAC_VOICE_OF_PLANET].preq_tech, i)) return 1;
        }
    }
    return 0;
}

int32_t GAMEAPI reputation(int32_t faction_id, int32_t faction_id_with) {
    int32_t value =
        game().factions[faction_id].integrity_blemishes - game().factions[faction_id].diplo_gifts[faction_id_with];
    if (value < 0) return 0;
    if (value > 99) return 99;
    return value;
}

void GAMEAPI agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda) {
    game().factions[faction_id].diplo_agenda[faction_id_with] &= ~agenda;
    game().factions[faction_id_with].diplo_agenda[faction_id] &= ~agenda;
}

void GAMEAPI treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status) {
    uint32_t& mine = game().factions[faction_id].diplo_status[faction_id_with];
    uint32_t& theirs = game().factions[faction_id_with].diplo_status[faction_id];
    bool viewer = faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction;
    if (status & 7) {
        // A truce, treaty or pact ends the viewer's war music.
        if (viewer && (mine & DIPLO_VENDETTA)) AmbienceFlags &= ~1;
        mine &= 0xFFEE7FAF;
        theirs &= 0xFFEE7FAF;
    }
    if (status & 1) status |= 2;
    if (status & DIPLO_VENDETTA) {
        if (mine & 7) {
            game().factions[faction_id_with].diplo_combat_count[faction_id] = 0;
            game().factions[faction_id].diplo_combat_count[faction_id_with] = 0;
        }
        status |= 0x80000108;
        mine &= 0xFAEEF378;
        theirs &= 0xFAEEF378;
        if (viewer) {
            AmbienceFlags |= 1;
            ambience(0xCC);
        }
    }
    if (status & 0x17) {
        mine &= 0xFF7FFFFF;
        theirs &= 0xFF7FFFFF;
    }
    mine |= status;
    theirs |= status;
    if ((faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction) && (status & 0x1F) &&
        MultiWinVisible)
        GraphicWin_redraw(&MultiWin);
}

// ---- pact ends ------------------------------------------------------------------------------------

int32_t GAMEAPI pact_withdraw(int32_t faction_id, int32_t faction_id_with) {
    int32_t moved = 0;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        VEH& veh = game().vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id) continue;
        int32_t near = base_find(veh.x, veh.y);
        if (near < 0) continue;
        if ((uint8_t)game().bases[near].faction_id != (uint32_t)faction_id_with &&
            whose_territory(faction_id, veh.x, veh.y, nullptr, 0) != faction_id_with) {
            continue;
        }
        int32_t best = -1, best_dist = 9999;
        for (int32_t i = 0; i < game().base_count; i++) {
            const BASE& base = game().bases[i];
            if ((uint8_t)base.faction_id != (uint32_t)faction_id) continue;
            if (veh_triad(veh_id) == TRIAD_SEA) {
                // BUG(original): is_coast is given the location of the base whose id is this
                // base's x coordinate, and this base's y coordinate as the radius flag.
                COMPAT_ORIGINAL(is_coast_base_x);
                const BASE& odd = game().bases[base.x];
                if (!is_coast(odd.x, odd.y, base.y)) continue;
            }
            if (veh_triad(veh_id) == TRIAD_SEA && map_is_ocean(veh.x, veh.y) &&
                !base_on_sea(i, map_tile(veh.x, veh.y)->region)) {
                continue;
            }
            int32_t dist = veh_vector_dist(base.x, base.y, veh.x, veh.y);
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
        if (best < 0) continue;
        veh_drop(veh_lift(veh_id), game().bases[best].x, game().bases[best].y);
        veh.order = ORDER_NONE;
        veh.state &= 0xFCFFBDFF;
        if (veh_triad(veh_id) == TRIAD_AIR) veh.movement_turns = 0;
        moved++;
        if (MapWinInVehMode && veh_id == MapWinVehA) Console_set_move(&MainConsole, 1);
    }
    return moved;
}

void GAMEAPI pact_ends(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    treaty_off(faction_id, faction_id_with, DIPLO_PACT);
    int32_t withdrawn = pact_withdraw(faction_id, faction_id_with);
    int32_t withdrawn_with = pact_withdraw(faction_id_with, faction_id);
    draw_map(1);
    int32_t count;
    if (faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction) {
        int32_t other = faction_id == CurrentPlayerFaction ? faction_id_with : faction_id;
        GenderDefault = game().mfactions[other].noun_gender;
        PluralDefault = game().mfactions[other].is_noun_plural;
        parse_says(0, game().mfactions[other].noun_faction, -1, -1);
        parse_says(1, game().mfactions[other].adj_name_faction, -1, -1);
        count = faction_id == CurrentPlayerFaction ? withdrawn : withdrawn_with;
    } else {
        const Faction& viewer = game().factions[CurrentPlayerFaction];
        if (spying(faction_id) || spying(faction_id_with) || (GameState & STATE_OMNISCIENT_VIEW) ||
            (viewer.diplo_status[faction_id] & DIPLO_PACT) || (viewer.diplo_status[faction_id_with] & DIPLO_PACT)) {
            GenderDefault = game().mfactions[faction_id].noun_gender;
            PluralDefault = game().mfactions[faction_id].is_noun_plural;
            parse_says(0, game().mfactions[faction_id].noun_faction, -1, -1);
            GenderDefault = game().mfactions[faction_id_with].noun_gender;
            PluralDefault = game().mfactions[faction_id_with].is_noun_plural;
            parse_says(1, game().mfactions[faction_id_with].noun_faction, -1, -1);
            parse_says(2, get_pact_hood(faction_id, faction_id_with), -1, -1);
            X_pop("ENDPACT3", 0);
        }
        return;
    }
    parse_num(0, count);
    parse_says(2, get_pact_hood(faction_id, faction_id_with), -1, -1);
    ui_pact_ends_dialog(count);
}

// ---- first half of the module (0x539160-0x54B140) -------------------------------------------------

namespace {

// X_pops_11's wait callback: pop_wait (0x5398E0), passed by its address in the original.
constexpr int32_t kPopWait = 0x5398E0;

uint32_t& dstatus(int32_t faction_id, int32_t faction_id_with) {
    return game().factions[faction_id].diplo_status[faction_id_with];
}
uint32_t& dagenda(int32_t faction_id, int32_t faction_id_with) {
    return game().factions[faction_id].diplo_agenda[faction_id_with];
}
bool alive(int32_t faction_id) { return (FactionStatus[1] & (1 << faction_id)) != 0; }
bool human(int32_t faction_id) { return (FactionStatus[0] & (1 << faction_id)) != 0; }
int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The parse_says blocks the original repeats inline: leader title ($n) and name ($n+1), faction
// noun ($n).
void say_leader(int32_t n, int32_t faction_id) {
    const MFaction& m = game().mfactions[faction_id];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(n, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, m.name_leader, -1, -1);
}
void say_noun(int32_t n, int32_t faction_id) {
    const MFaction& m = game().mfactions[faction_id];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(n, m.noun_faction, -1, -1);
}
// Faction of the units on a tile (the original inlines veh_who): -1 none.
int32_t tile_veh_owner(const MAP* sq) {
    if (!(sq->items & BIT_VEH_IN_TILE)) return -1;
    uint32_t f = sq->val2 & 0xF;
    return f > 7 ? -1 : (int32_t)f;
}
// Vehs[veh_id].faction_id; veh_id -1 (no unit) reads Vehs[-1], which lies in the Reactor table.
uint32_t veh_faction_or_oob(int32_t veh_id) {
    return (uint32_t)veh_id < 2049 ? game().vehs[veh_id].faction_id
                                   : rules_orig_byte(0x952828 + (uint32_t)veh_id * 0x34 + offsetof(VEH, faction_id));
}
// strcat of label + the decimal number n (the original's "LABELn" popup names).
void label_with_number(char* buffer, int32_t n) {
    char num[80];
    crt_itoa(n, num, 10);
    crt_strcat(buffer, num);
}

}  // namespace

int32_t GAMEAPI auto_contact() {
    if (MultiplayerActive && Rules.tgl_humans_always_contact_net) return 1;
    if (PbemActive && Rules.tgl_humans_always_contact_pbem) return 1;
    return 0;
}

void GAMEAPI net_agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t wait) {
    if (!MultiplayerActive) {
        agenda_on(faction_id, faction_id_with, agenda);
        return;
    }
    log_say_values("Sending AGENDA_ON", faction_id, faction_id_with, (int32_t)agenda);
    message_data(0x2444, 0, faction_id, faction_id_with, (int32_t)agenda, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x444);
}

void GAMEAPI net_agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t wait) {
    if (!MultiplayerActive) {
        agenda_off(faction_id, faction_id_with, agenda);
        return;
    }
    log_say_values("Sending AGENDA_OFF", faction_id, faction_id_with, (int32_t)agenda);
    message_data(0x2445, 0, faction_id, faction_id_with, (int32_t)agenda, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x445);
}

void GAMEAPI net_set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add, int32_t wait) {
    if (!MultiplayerActive) {
        set_agenda(faction_id, faction_id_with, agenda, add);
        return;
    }
    log_say_values("Sending SET_AGENDA", faction_id, faction_id_with, (int32_t)agenda);
    message_data(0x2446, 0, faction_id, faction_id_with, (int32_t)agenda, add);
    if (wait) NetDaemon_await_diplo(&NetState, 0x446);
}

void GAMEAPI net_energy(int32_t faction_id, int32_t amount, int32_t faction_id_with, int32_t amount_with,
                        int32_t wait) {
    if (!MultiplayerActive) {
        if (faction_id) game().factions[faction_id].energy_credits += amount;
        if (faction_id_with) game().factions[faction_id_with].energy_credits += amount_with;
        Console_update_data(&MainConsole, 1);
        return;
    }
    log_say_values("Sending ENERGY", faction_id, faction_id_with, amount);
    message_data(0x2447, 0, faction_id, amount, faction_id_with, amount_with);
    if (wait) NetDaemon_await_diplo(&NetState, 0x447);
}

void GAMEAPI net_loan(int32_t faction_id, int32_t faction_id_with, int32_t balance, int32_t payment) {
    if (MultiplayerActive) {
        log_say_values("Sending LOAN", faction_id, faction_id_with, balance);
        message_data(0x244E, 0, faction_id, faction_id_with, balance, payment);
        return;
    }
    game().factions[faction_id].loan_balance[faction_id_with] = balance;
    game().factions[faction_id].loan_payment[faction_id_with] = payment;
}

void GAMEAPI net_maps(int32_t faction_id, int32_t faction_id_with, int32_t wait) {
    if (!MultiplayerActive) {
        trade_maps(faction_id, faction_id_with);
        return;
    }
    log_say_values("Sending TRADE_MAPS", faction_id, faction_id_with, 0);
    message_data(0x2448, 0, faction_id, faction_id_with, 0, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x448);
}

void GAMEAPI net_pact_ends(int32_t faction_id, int32_t faction_id_with, int32_t wait) {
    if (!MultiplayerActive) {
        pact_ends(faction_id, faction_id_with);
        return;
    }
    log_say_values("Sending PACT_ENDS", 0, 0, 0);
    message_data(0x244D, 0, faction_id, faction_id_with, 0, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x44D);
}

int32_t GAMEAPI net_withdrawal(int32_t faction_id, int32_t faction_id_with, int32_t wait) {
    if (MultiplayerActive) {
        log_say_values("Sending WITHDRAWAL", faction_id, faction_id_with, 0);
        message_data(0x244A, 0, faction_id, faction_id_with, 0, 0);
        if (wait) NetDaemon_await_diplo(&NetState, 0x44A);
        return 1;
    }
    return do_withdrawal(faction_id, faction_id_with);
}

void GAMEAPI net_cede_base(int32_t base_id, int32_t faction_id, int32_t wait) {
    if (!MultiplayerActive) {
        give_a_base(base_id, faction_id);
        return;
    }
    log_say_values("Sending CEDE_BASE", base_id, faction_id, 0);
    message_data(0x2449, 0, base_id, faction_id, 0, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x449);
}

void GAMEAPI net_double_cross(int32_t faction_id, int32_t faction_id_with, int32_t a3, int32_t wait) {
    if (!MultiplayerActive) {
        double_cross(faction_id, faction_id_with, a3);
        return;
    }
    log_say_values("Sending DOUBLE_CROSS", faction_id, faction_id_with, a3);
    message_data(0x2440, 0, faction_id, faction_id_with, a3, 0);
    if (wait) NetDaemon_await_diplo(&NetState, 0x440);
}

int32_t GAMEAPI diplo_lock(int32_t timeout) {
    int32_t tries = 0;
    log_say_values("Getting DIPLOMATIC LOCK", 0, 0, 0);
    if (timeout && timeout < 100) timeout = 100;
    uint32_t start = engine_real_time();
    // NetDaemon_add_global returns 0 at once in single player (no lock to wait for).
    while (!ControlTurnA && MultiplayerActive && NetDaemon_add_global(&NetState)) {
        check_spock();
        uint32_t now = engine_real_time();
        if (timeout && (now - start >= 10000 || ++tries >= 10000)) {
            tries = X_pop("NEEDGLOBAL", 0);
            if (tries) return 1;
            tries = 0;
            start = engine_real_time();
        }
    }
    return 0;
}

void GAMEAPI diplo_unlock() {
    log_say_values("Releasing DIPLOMATIC LOCK", 0, 0, 0);
    if (MultiplayerActive) NetDaemon_unlock(&NetState);  // nothing to release in single player
}

int32_t GAMEAPI pop_wait() {
    if (MultiplayerActive) {
        if (ControlTurnA) return 0;
        PopWaitActive = 1;
        wait_loop();
        PopWaitActive = 0;
    }
    return 1;
}

int32_t GAMEAPI pop_expire() {
    if (MultiplayerActive) {
        if (ControlTurnA) return 0;
        if (GameState & 0x800) return 0;
        PopWaitActive = 1;
        wait_loop();
        PopWaitActive = 0;
    }
    return 1;
}

int32_t GAMEAPI get_mood(int32_t friction) {
    if (friction < 1) return 0;
    if (friction < 3) return 1;
    if (friction < 5) return 2;
    if (friction < 9) return 3;
    if (friction < 13) return 4;
    if (friction < 16) return 5;
    if (friction < 18) return 6;
    return (friction > 19) + 7;
}

void GAMEAPI diplomacy_caption(int32_t faction_id, int32_t faction_id_with) {
    int32_t mood;
    if (!(dstatus(faction_id, faction_id_with) & DIPLO_VENDETTA) || DiploFriction > 14) {
        mood = get_mood(DiploFriction);
    } else {
        DiploFriction = 15;
        mood = 5;
    }
    char buffer[256];
    buffer[0] = 0;
    const MFaction& m = game().mfactions[faction_id_with];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    crt_strcat(buffer, m.noun_faction);
    crt_strcat(buffer, " - ");
    uint32_t theirs = dstatus(faction_id_with, faction_id);
    if ((mood == 0 && DiploFearScale && !(dstatus(faction_id, faction_id_with) & DIPLO_PACT)) ||
        ((theirs & DIPLO_PACT) && (theirs & 0x2000000))) {
        crt_strcat(buffer, label_get(488));
    } else {
        say_fac_special(buffer, Strings_get(&StringTable, Mood[mood]), faction_id_with);
    }
    parse_says(7, buffer, -1, -1);
    if (Win_is_visible(&DiploPopWin)) {
        DiploPop_draw_info(&DiploPopWin);
        GraphicWin_soft_update_3(&DiploPopWin, &DiploPopInfoRect);
    }
}

int32_t GAMEAPI get_patience(int32_t faction_id, int32_t faction_id_with) {
    uint32_t s = dstatus(faction_id_with, faction_id);
    if (s & DIPLO_VENDETTA) return 1;
    if (s & DIPLO_PACT) return (s & 0x2000000) ? 500 : 6;
    return (int32_t)((s >> 1) & 1) - (DiploFriction + 3) / 8 + 3;
}

int32_t GAMEAPI energy_value(int32_t energy) {
    int32_t value = 0, divisor = 2, chunk = 10;
    for (int32_t left = energy / 5; left > 0; left -= chunk, chunk = 20) {
        value += (left > chunk ? chunk : left) / divisor;
        divisor++;
    }
    return (value + 4) / 5;
}

void GAMEAPI mention_prototypes(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (DiploMentionUnitID < 0 || !wants_to_speak(faction_id, faction_id_with)) return;
    for (int32_t i = 0; i < 64; i++) Units[faction_id_with * 64 + i].unit_flags |= 0x10;
    int32_t unit_id = DiploMentionUnitID;
    parse_says(0, Units[unit_id].name, -1, -1);
    StrBuffer[0] = 0;
    say_stats_labeled(StrBuffer, unit_id);
    parse_says(1, StrBuffer, -1, -1);
    Units[unit_id].unit_flags |= 0x10;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "PROTO");
    if (dstatus(faction_id, faction_id_with) & DIPLO_PACT) {
        // With a pact nothing is shown or traded here: only the mentioned prototype is marked
        // traded, and DiploMentionUnitID stays set.
        Units[unit_id].unit_flags |= 0x20;
        crt_strcat(StrBuffer, "PACT");
        return;
    }
    crt_strcat(StrBuffer, "RIVAL");
    if (X_pops_11(StrBuffer, 0x100000, FactionPortraits[faction_id_with], kPopWait)) {
        for (int32_t i = 0; i < 64; i++) {
            if (wants_prototype(faction_id * 64 + i, faction_id_with))
                net_tech(faction_id_with, faction_id * 64 + i + 97, faction_id, 1);
            Units[faction_id * 64 + i].unit_flags |= 0x20;
            if (wants_prototype(faction_id_with * 64 + i, faction_id))
                net_tech(faction_id, faction_id_with * 64 + i + 97, faction_id_with, 1);
            Units[faction_id_with * 64 + i].unit_flags |= 0x20;
        }
    }
    DiploBestUnitID = -1;
    DiploMentionUnitID = -1;
}

void GAMEAPI scan_prototypes(int32_t faction_id, int32_t faction_id_with) {
    DiploBestUnitID = -1;
    DiploMentionUnitID = -1;
    int32_t best = 0, mention = 0;
    for (int32_t i = 0; i < 64; i++) {
        const int32_t unit_id = faction_id_with * 64 + i;
        const UNIT& u = Units[unit_id];
        if (!(u.unit_flags & UNIT_ACTIVE) || !(u.unit_flags & UNIT_PROTOTYPED) ||
            (u.obsolete_factions & (1 << faction_id_with)))
            continue;
        bool unmatched = true;  // faction_id has no prototype of this plan and triad as good
        int32_t value = Weapon[u.weapon_id].offense_value * 3 +
                        ((7 - (uint8_t)u.plan) * 0x4000 + Armor[u.armor_id].defense_value) * 4 +
                        Chassis[u.chassis_id].speed;
        for (int32_t j = 0; unmatched && j < 64; j++) {
            const UNIT& v = Units[faction_id * 64 + j];
            if (!(v.unit_flags & UNIT_ACTIVE) || !(v.unit_flags & UNIT_PROTOTYPED) ||
                (v.obsolete_factions & (1 << faction_id)) || v.plan != u.plan ||
                Chassis[v.chassis_id].triad != Chassis[u.chassis_id].triad) {
                continue;
            }
            if (u.reactor_id < v.reactor_id) unmatched = false;
            if (Chassis[u.chassis_id].speed < Chassis[v.chassis_id].speed) unmatched = false;
            int32_t weap_v = weap_strat(v.weapon_id, faction_id);
            int32_t weap_u = weap_strat(u.weapon_id, faction_id);
            // BUG(original): the armor values are arm_strat of the weapon ids.
            COMPAT_ORIGINAL(armor_from_weapon_ids);
            int32_t arm_v = arm_strat(v.weapon_id, faction_id);
            int32_t arm_u = arm_strat(u.weapon_id, faction_id);
            if (Chassis[v.chassis_id].speed >= Chassis[u.chassis_id].speed && weap_v >= weap_u && arm_v >= arm_u &&
                (weap_v > weap_u || arm_v > arm_u)) {
                unmatched = false;
            }
        }
        int32_t score = unmatched ? value : value >> 4;
        if (score > best) {
            DiploBestUnitID = unit_id;
            best = score;
        }
        uint8_t traded = (uint8_t)u.unit_flags & ((dstatus(faction_id, faction_id_with) & DIPLO_PACT) ? 0x20 : 0x30);
        uint8_t plan = (uint8_t)u.plan;
        if (!traded && plan < 7 && plan != 3) {
            if (!unmatched && (!(dstatus(faction_id, faction_id_with) & DIPLO_VENDETTA) || (value >>= 4, plan != 2)))
                value = 0;
            if (value > mention) {
                DiploMentionUnitID = unit_id;
                mention = value;
            }
        }
    }
}

void GAMEAPI diplomacy_check(int32_t faction_id, int32_t faction_id_with, int32_t is_full) {
    const int32_t p = faction_id, r = faction_id_with;
    Faction& P = game().factions[p];
    Faction& R = game().factions[r];
    DiploFrictionFactionIDWith = p;
    DiploFrictionFactionID = r;
    DiploSecondFaction = r;
    DiploHostility = 0;
    DiploAttackChance = 0;
    DiploTributeDemand = 0;
    DiploFearScale = 0;
    DiploConquestFear = 0;
    DiploExposedBaseCount = 0;
    DiploOpenBaseID = 0;
    DiploCapturedBases = 0;
    DiploNearEndgame = at_climax(p);
    DiploRivalOutranks = R.ranking > P.ranking;
    if (!great_satan(p, 0) || R.base_count < 2 ||
        ((P.diplo_status[r] & DIPLO_PACT) &&
         (R.ranking < 5 || ((GameRules & RULES_VICTORY_COOPERATIVE) && CurrentTurn >= 200)))) {
        DiploGreatSatan = 0;
    } else {
        DiploGreatSatan = 1;
    }
    DiploRunawayLeader = FactionRankingsUnk[FactionRankings[7]] > FactionRankingsUnk[p] * 2 && R.ranking != 7;
    if (R.player_flags & PFLAG_TEAM_UP_VS_HUMAN) {
        DiploGreatSatan = 1;
        DiploRunawayLeader = 0;
    }
    if (R.player_flags & PFLAG_COOP_WITH_HUMAN) {
        DiploGreatSatan = 0;
        DiploRunawayLeader = 1;
    }
    DiploFriction = R.diplo_friction[p];
    DiploReputation = clamp(P.integrity_blemishes - P.diplo_gifts[r], 0, 99);
    // Social agenda conflict: 1 faction_id runs a model of faction_id_with's agenda category
    // other than the agenda's, 2 when faction_id_with also holds it against faction_id.
    int32_t conflict = 0;
    {
        const MFaction& m = game().mfactions[r];
        const int32_t* pending = &P.SE_Politics_pending;
        if (m.soc_priority_category >= 0 && m.soc_priority_model && pending[m.soc_priority_category] &&
            pending[m.soc_priority_category] != m.soc_priority_model) {
            conflict = 1;
            if (R.diplo_agenda[p] & 1) conflict = 2;
        }
    }
    int32_t combat_p = P.diplo_combat_count[r] + 1 + CurrentTurn / 50;  // faction_id's war score
    int32_t combat_r = R.diplo_combat_count[p] + 1 + CurrentTurn / 50;
    if (!(P.diplo_status[r] & DIPLO_VENDETTA)) {
        combat_p += 2 + P.diplo_combat_total[r] + CurrentTurn / 25;
        combat_r += 2 + R.diplo_combat_total[p] + CurrentTurn / 25;
    }
    int32_t fight = R.AI_fight;
    DiploPlayerPactCount = 0;
    DiploRivalPactCount = 0;
    DiploRivalStrongPacts = 0;
    DiploRivalPactScore = 0;
    if ((R.diplo_status[p] & DIPLO_WANT_REVENGE) && fight < 0) fight = -fight;
    int32_t most_bases[7];  // per faction 1-7: its largest base count in a region (at least 1)
    for (int32_t i = 0; i < 7; i++) most_bases[i] = 1;
    for (int32_t i = 1; i < 8; i++) {
        if (!alive(i)) continue;
        if ((P.diplo_status[i] & DIPLO_PACT) && !(game().factions[i].diplo_status[p] & DIPLO_HAVE_SURRENDERED))
            DiploPlayerPactCount++;
        if (R.diplo_status[i] & DIPLO_PACT) {
            if (game().factions[i].ranking > 4 && R.ranking < 5 && i != p && !DiploRunawayLeader)
                DiploRivalStrongPacts++;
            DiploRivalPactCount++;
            DiploRivalPactScore += FactionRankingsUnk[i];
        }
    }
    if (is_full) DiploSpokeTurn = P.diplo_spoke[r];
    int32_t morale_p = game().mfactions[p].rule_morale + 16 + clamp(P.SE_morale_pending, -4, 4);
    int32_t morale_r = game().mfactions[r].rule_morale + 16 + clamp(R.SE_morale_pending, -4, 4);
    for (int32_t region = 1; region < 128; region++) DiploRegionFactions[region] = 0;
    for (int32_t region = 1; region < 128; region++) {
        for (int32_t i = 0; i < 7; i++) {
            int32_t n = game().factions[i + 1].region_total_bases[region];
            if (n >= most_bases[i]) most_bases[i] = n;
        }
    }
    for (int32_t base_id = 0; base_id < game().base_count; base_id++) {
        const BASE& b = game().bases[base_id];
        int32_t offset, mask;
        bitmask(1, &offset, &mask);  // headquarters
        if (b.facilities_built[offset] & (uint8_t)mask)
            DiploRegionFactions[map_tile(b.x, b.y)->region] |= 1 << (b.faction_id & 0x1F);
        uint32_t owner = (uint8_t)b.faction_id;
        if (owner == (uint32_t)p) {
            if ((uint8_t)b.faction_id_former == (uint32_t)r) DiploCapturedBases++;
        } else if (owner != (uint32_t)r) {
            continue;
        }
        if (P.diplo_status[r] & DIPLO_PACT) continue;
        const int32_t x = b.x, y = b.y;
        const MAP* sq = map_tile(x, y);
        if (tile_veh_owner(sq) < 0) {
            // An empty base: faction_id's is exposed to faction_id_with's units nearby (and
            // vice versa: an open base for faction_id).
            for (int32_t k = 0; k < 21; k++) {
                int32_t nx = map_wrap_x(TableOffsetX[k] + x);
                int32_t ny = TableOffsetY[k] + y;
                if (!map_in_bounds(nx, ny)) continue;
                const MAP* near = map_tile(nx, ny);
                if (near->region != sq->region) continue;
                if (owner == (uint32_t)p) {
                    if (tile_veh_owner(near) == r) {
                        DiploExposedBaseCount++;
                        break;
                    }
                } else if (tile_veh_owner(near) == p) {
                    DiploOpenBaseID = base_id;
                    DiploFearScale += b.pop_size * 8;
                    break;
                }
            }
        } else if (owner == (uint32_t)p) {
            int32_t defense = stack_check(veh_at(x, y), 0xE, -1, -1, -1);
            int32_t threat = 0;
            for (int32_t k = 0; k < 21; k++) {
                int32_t nx = map_wrap_x(TableOffsetX[k] + b.x);
                int32_t ny = TableOffsetY[k] + b.y;
                if (!map_in_bounds(nx, ny)) continue;
                int32_t veh_id = stack_fix(veh_at(nx, ny));
                // BUG(original): with no unit there (-1) the owner is read from Vehs[-1].
                COMPAT_ORIGINAL(out_of_range_reads);
                if (veh_faction_or_oob(veh_id) == (uint32_t)r) {
                    int32_t a = stack_check(veh_id, 2, 1, -1, -1);
                    int32_t c = stack_check(veh_id, 2, 0, -1, -1);
                    threat += c + a;
                }
            }
            if (threat > defense) {
                DiploAttackChance++;
                DiploTributeDemand++;
            }
        }
    }
    for (int32_t region = 1; region < 128; region++) {
        uint32_t& present = reinterpret_cast<uint32_t&>(DiploRegionFactions[region]);
        for (int32_t i = 1; i < 8; i++) {
            int32_t most = most_bases[i - 1];
            if (most - most / 4 <= game().factions[i].region_total_bases[region]) present |= 1u << i;
        }
        int32_t bases_p = P.region_total_bases[region];
        int32_t bases_r = R.region_total_bases[region];
        int32_t units_p = P.region_total_combat_units[region];
        int32_t units_r = R.region_total_combat_units[region];
        if ((!units_p && !bases_p) || (!units_r && !bases_r)) continue;
        int32_t weapon_p = P.best_weapon_value + 1;
        int32_t weapon_r = R.best_weapon_value + 1;
        int32_t force_p = P.region_force_rating[region] * morale_p / morale_r * weapon_p / weapon_r;
        int32_t force_r = R.region_force_rating[region] * morale_r / morale_p * weapon_r / weapon_p;
        uint32_t main_r = present & (1u << r);  // a main region of faction_id_with
        if (main_r) {
            int32_t defense = units_r;
            if (bases_p) defense = force_r / (3 - R.AI_fight - ((present & (1u << p)) != 0)) + units_r;
            if (defense < force_p) {
                defense = clamp(defense, 1, 99999);
                DiploFearScale += (force_p + 1) * 32 / defense;
                continue;
            }
        }
        DiploHostility = 1;
        if (!bases_p) {
            if (bases_r) DiploTributeDemand -= (force_p - units_r) / (2 - (main_r != 0));
        } else {
            DiploTributeDemand += force_r;
            if ((present & (1u << p)) && units_p < force_r) {
                DiploTributeDemand += force_r;
                if ((force_p + units_p) * 3 / 2 < force_r) DiploAttackChance++;
            }
            if (bases_r || (P.diplo_status[r] & DIPLO_PACT)) DiploTributeDemand -= force_p;
        }
    }
    DiploTributeDemand = (DiploExposedBaseCount + 1) * DiploTributeDemand;
    if (DiploExposedBaseCount && !(P.diplo_status[r] & (DIPLO_PACT | DIPLO_TREATY | DIPLO_TRUCE))) DiploHostility = 1;
    if (DiploNearEndgame && DiploRivalOutranks) DiploHostility = 3 - (DiploGreatSatan != 0);
    if (P.base_count && R.base_count) {
        if (!(R.diplo_status[p] & DIPLO_WANT_REVENGE) && !(R.diplo_agenda[p] & 0x200)) {
            int32_t v = clamp(R.AI_wealth - P.tech_commerce_bonus + R.tech_commerce_bonus - fight, 0, 99);
            DiploTributeDemand -= P.energy_surplus_total / P.base_count * v;
            if (R.diplo_status[p] & DIPLO_WANT_TO_TALK) DiploTributeDemand /= 2;
        } else {
            int32_t v = clamp(P.tech_commerce_bonus - R.tech_commerce_bonus + fight, 0, 99);
            DiploTributeDemand = (R.energy_surplus_total * v / R.base_count + DiploTributeDemand) * 2;
        }
    }
    if (R.best_weapon_value * 3 / 2 <= P.best_weapon_value || R.best_armor_value * 2 < P.best_weapon_value)
        DiploFearScale *= 2;
    if (combat_r * 2 <= combat_p) DiploFearScale *= 2;
    if (combat_r * 4 <= combat_p && !(R.diplo_status[p] & DIPLO_WANT_REVENGE) && DiploFearScale < 9) DiploFearScale = 8;
    if (combat_p * 2 < combat_r) DiploFearScale /= 2;
    if (combat_p * 4 < combat_r) DiploFearScale /= 2;
    if (DiploFearScale ||
        (P.diff_level <= ((R.diplo_status[p] & DIPLO_WANT_REVENGE) ? 2 : 4) && !(GameRules & RULES_INTENSE_RIVALRY)))
        DiploAttackChance = 0;
    DiploTributeDemand = (CurrentTurn / 10 + combat_r) * DiploTributeDemand / (combat_p + CurrentTurn / 10);
    int32_t delay = MultiplayerActive ? 0 : (5 - P.diff_level) * 25;
    bool hostile;  // the rival turns hostile as against a leading player
    if (CurrentTurn < delay + 50 || DiploRunawayLeader || P.ranking < (DiffLevel < 3) + 5) {
        hostile = false;
    } else if (R.ranking < (P.ranking < 6) + 5) {
        hostile = DiploRivalStrongPacts && (P.diplo_status[r] & 0x8000000) && P.ranking >= 6;
        if (hostile) DiploGreatSatan = 1;
    } else {
        hostile = true;
        DiploGreatSatan = 1;
    }
    if (!hostile) {
        hostile = DiploGreatSatan ||
                  (((R.diplo_status[p] & DIPLO_WANT_REVENGE) || (R.diplo_agenda[p] & 0x200)) && !DiploOpenBaseID &&
                   (!DiploFearScale || R.mil_strength_1 * 2 >= P.mil_strength_1));
    }
    if (hostile) {
        DiploFearScale = 0;
        int32_t demand = R.ranking * 2;
        DiploHostility = (DiploGreatSatan != 0) + 1;
        if (DiploTributeDemand < demand) DiploTributeDemand = demand;
    }
    int32_t busters_r = R.planet_busters;
    if (busters_r && R.major_atrocities) DiploFearScale = 0;
    if (P.planet_busters && P.major_atrocities) {
        if (!busters_r) DiploFearScale = 32;
        DiploTributeDemand = busters_r * DiploTributeDemand / P.planet_busters;
    }
    DiploConquestFear = DiploFearScale;
    uint32_t pact = R.diplo_status[p] & DIPLO_PACT;
    if (pact || !DiploCapturedBases) DiploConquestFear = 0;
    uint32_t rules_hard = GameRules & RULES_INTENSE_RIVALRY;
    int32_t diff = rules_hard ? 5 : P.diff_level;
    int32_t blemishes = clamp(P.integrity_blemishes / 2, 1, 99);
    DiploFearScale = DiploFearScale / ((diff + 3) * (blemishes + 1) / 2);
    DiploEnemyAllyCount = 0;
    DiploEnemyAlly = 0;
    if (!pact || !(R.diplo_status[p] & DIPLO_HAVE_SURRENDERED)) {
        for (int32_t i = 1; i < 8; i++) {
            uint32_t to_p = game().factions[i].diplo_status[p];
            if (alive(i) && i != p && i != r && (R.diplo_status[i] & DIPLO_VENDETTA) &&
                (!(to_p & DIPLO_PACT) || !(to_p & 0x2000000)) &&
                ((P.diplo_status[i] & DIPLO_PACT) || ((P.diplo_status[i] & DIPLO_TREATY) && fight > 0))) {
                DiploEnemyAllyCount++;
                DiploEnemyAlly = i;
            }
        }
    }
    DiploFriction = DiploEnemyAllyCount * 3 + DiploFriction;
    if (pact) DiploFriction -= 5;
    if (R.diplo_status[p] & (DIPLO_VENDETTA | DIPLO_WANT_REVENGE)) DiploFriction += 5;
    uint32_t agenda = R.diplo_agenda[p];
    if (agenda & 0x400) DiploFriction += 3;
    if (agenda & 0x800) DiploFriction += 3;
    if (DiploReputation > 1) DiploFriction += DiploReputation - 1;
    int32_t commerce_r = R.tech_commerce_bonus, commerce_p = P.tech_commerce_bonus;
    if (commerce_p < commerce_r) DiploFriction += (commerce_p - commerce_r) * 2;
    if (commerce_r < commerce_p) DiploFriction += commerce_p - commerce_r;
    if (conflict) {
        DiploFriction += 5;
        if (DiploFriction < 5) DiploFriction = 5;
    }
    if (conflict > 1 && !DiploFearScale) DiploFriction += 5;
    uint32_t grudge = agenda & 0x200;
    if (grudge) DiploHostility = 1;
    if (DiploFriction < 5) DiploHostility = 0;
    if (DiploFriction > 14) DiploHostility = 4;
    uint32_t vendetta = R.diplo_status[p] & 0x40;  // vendetta intended (set_treaty 0x40)
    if (vendetta) DiploHostility = 1;
    if (!(GameRules & RULES_VICTORY_TRANSCENDENCE) && R.SE_alloc_labs == 0) DiploHostility = 1;
    if (DiploFearScale || R.mil_strength_1 * 3 < P.mil_strength_1) DiploHostility = 0;
    if (R.best_weapon_value * 2 <= P.best_weapon_value) DiploHostility = 0;
    diff = rules_hard ? 5 : P.diff_level;
    int32_t demand =
        (diff + 1) * (DiploReputation + 2) * (DiploFriction + 10) * (conflict + 1) * DiploTributeDemand >> 6;
    if (vendetta) demand *= 2;
    int32_t credits = P.energy_credits;
    if (R.energy_credits * 2 <= credits) {
        if (credits > 1999) demand = demand * 3 / 2;
        if (credits > 3999) demand = demand * 3 / 2;
    }
    if ((DiploGreatSatan ||
         (((R.diplo_status[p] & DIPLO_WANT_REVENGE) || grudge) && !DiploOpenBaseID &&
          (!DiploFearScale || P.mil_strength_1 <= R.mil_strength_1 * 2))) &&
        P.ranking > 5) {
        int32_t share = (R.ranking + P.ranking) * credits / 21;
        if (demand < share) demand = share;
    }
    DiploTributeDemand = (demand + 24) / 25 * 25;
    if (DiploTributeDemand == 25) DiploTributeDemand = 50;
    if (credits < DiploTributeDemand && DiploTributeDemand < credits * 2 && (vendetta ? 100 : 50) <= credits &&
        !(P.diplo_status[r] & (DIPLO_WANT_REVENGE | DIPLO_ATROCITY_VICTIM))) {
        DiploTributeDemand = (credits + 24) / 25 * 25;
    }
    if (!(R.diplo_status[p] & 0x60) && (!DiploGreatSatan || (R.diplo_status[p] & 0x4000000))) {
        energy_compute(p, 0);
        // BUG(original): divides by faction_id's ranking, which may be 0.
        COMPAT_ORIGINAL(ranking_divide_zero);
        if (FactionCurrentNetIncome * 5 < DiploTributeDemand &&
            (int32_t)((uint32_t)credits << 1) / 3 * R.ranking / P.ranking <= R.energy_credits) {
            DiploTributeDemand = FactionCurrentNetIncome * 5 / 25 * 25;
            if (DiploTributeDemand < 50) DiploTributeDemand = 50;
        }
    }
    uint32_t s = R.diplo_status[p];
    if ((((s & 7) && !(s & 0x10000)) || DiploFearScale) && !DiploGreatSatan && !(s & 0x20)) DiploAttackChance = 0;
    if (!DiploTributeDemand && !DiploAttackChance && !(R.diplo_agenda[p] & 0x200)) DiploHostility = 0;
    DiploPrevHostility = DiploHostility;
    DiploGoodwill = 0;
    if (is_full) {
        DiploHelpFaction = 0;
        DiploUnmetEnemy = 0;
        DiploPlayerEnemy = 0;
        DiploUnmetFaction = 0;
        DiploMutualFaction = 0;
    }
    DiploRivalWarCount = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (!alive(i) || i == p || i == r) continue;
        uint32_t r_i = R.diplo_status[i];
        if (!(r_i & DIPLO_COMMLINK)) continue;
        uint32_t p_i = P.diplo_status[i];
        if (is_full && (p_i & DIPLO_VENDETTA) && !(r_i & DIPLO_VENDETTA) && !(p_i & 0x10000000))
            DiploPlayerEnemy = i;
        if (!(r_i & DIPLO_PACT)) {
            if (!(r_i & DIPLO_VENDETTA)) {
                if (is_full && !human(i)) {
                    if (!(p_i & DIPLO_COMMLINK)) {
                        if ((!DiploUnmetFaction || contiguous(p, i, 0, 0)) &&
                            ((R.diplo_status[i] & DIPLO_TREATY) || !DiploUnmetFaction)) {
                            DiploUnmetFaction = i;
                        }
                    } else if (!(p_i & 0x10000000)) {
                        DiploMutualFaction = i;
                    }
                }
            } else {
                DiploRivalWarCount++;
                int32_t mil_r = R.mil_strength_1, mil_i = game().factions[i].mil_strength_1;
                DiploGoodwill += (mil_r / 2 + mil_i) * 2 / (mil_r + 1);
                p_i = P.diplo_status[i];
                if (!((uint8_t)DiploStateA[r] & 0x40)) {
                    uint32_t i_p = game().factions[i].diplo_status[p];
                    if (!(p_i & DIPLO_VENDETTA) && ((p_i & DIPLO_COMMLINK) || (P.diplo_status[r] & DIPLO_PACT)) &&
                        (!(i_p & DIPLO_PACT) || !(i_p & 0x2000000))) {
                        DiploHelpFaction = i;
                    } else if (!DiploHelpFaction && is_full && !(p_i & DIPLO_COMMLINK) && !human(i)) {
                        if (mil_r < mil_i || !wants_to_attack_fresh(r, i, 0))
                            DiploUnmetEnemy = i;
                        else if (DiploFearScale)
                            DiploUnmetEnemy = i;
                    }
                } else if ((p_i & 0x18) == DIPLO_COMMLINK && mil_r < mil_i) {
                    DiploHelpFaction = i;
                }
            }
        } else {
            if (P.mil_strength_1 < game().factions[i].mil_strength_1) DiploGoodwill--;
            if ((great_satan(p, 0) || DiploGreatSatan) && (game().factions[i].diplo_status[p] & DIPLO_VENDETTA)) {
                DiploFearScale -= 3;
                if (DiploFearScale < 0) DiploFearScale = 0;
                if (DiploFearScale >= 10000) {
                    DiploFearScale = 9999;
                } else if (DiploFearScale == 0) {
                    DiploConquestFear = 0;
                    DiploPrevHostility = 2;
                    DiploHostility = 2;
                }
            }
        }
    }
    agenda = R.diplo_agenda[p];
    if (agenda & 0x800) DiploGoodwill--;
    if (agenda & 0x400) DiploGoodwill--;
    s = R.diplo_status[p];
    uint32_t atrocity = s & 0x20;
    if (atrocity) DiploGoodwill--;
    if (agenda & 0x200) DiploGoodwill--;
    DiploGoodwill += -((DiploReputation - 1) / 3) - DiploExposedBaseCount - fight;
    if (P.tech_commerce_bonus < R.tech_commerce_bonus && fight < 1) DiploGoodwill++;
    if (!(GameRules & RULES_VICTORY_TRANSCENDENCE) && R.SE_alloc_labs == 0) DiploGoodwill--;
    if (P.mil_strength_1 < R.mil_strength_1) DiploGoodwill--;
    if (P.best_armor_value * 2 < R.best_weapon_value) DiploGoodwill--;
    if ((int32_t)((P.diplo_status[r] >> 4) & 1) < DiploGoodwill && !DiploGreatSatan && !(s & 0x41) &&
        !DiploAttackChance) {
        DiploHostility = 0;
        DiploTributeDemand = 0;
    }
    if (!(s & DIPLO_PACT) || !(s & 0x2000000)) {
        if ((DiploExposedBaseCount || DiploAttackChance) && atrocity) {
            DiploPrevHostility = 1;
            DiploHostility = 1;
        }
    } else {
        DiploAttackChance = 0;
        DiploExposedBaseCount = 0;
        DiploPrevHostility = 0;
        DiploHostility = 0;
    }
    if (!great_satan(p, 1) || DiploRivalPactCount > 1 ||
        FactionRankingsUnk[p] < DiploRivalPactScore + FactionRankingsUnk[r] * 3 || R.ranking > 5 ||
        P.mil_strength_1 <= R.mil_strength_1) {
        DiploCanSurrender = 0;
    } else {
        DiploCanSurrender = 1;
        if ((R.diplo_status[p] & DIPLO_VENDETTA) && !(R.diplo_agenda[p] & 8) &&
            !(R.diplo_status[p] & DIPLO_ATROCITY_VICTIM))
            DiploHostility = 0;
    }
    if (is_full) {
        scan_prototypes(p, r);
        uint32_t hates = R.player_flags_ext & PFLAG_EXT_SHAMELESS_BETRAY_HUMANS;
        s = R.diplo_status[p];
        if ((P.diff_level < 2 && !hates) || DiploExposedBaseCount || (s & DIPLO_PACT) || (R.diplo_agenda[p] & 8) ||
            ((s & DIPLO_VENDETTA) && DiploCapturedBases < 2 && !hates) ||
            (!DiploNearEndgame && !DiploHostility && !conflict && (!hates || DiploFearScale) &&
             (!DiploEnemyAllyCount || !(s & 0x800000) || DiploFriction < 12)) ||
            (!(s & 0x20) && (!hates || !DiploHostility) && DiploReputation < 6 &&
             ((!DiploHostility && conflict < 2) || (P.mil_strength_1 <= R.mil_strength_1 && !hates)))) {
            DiploCanBetray = 0;
        } else {
            DiploCanBetray = 1;
        }
    }
    if ((R.player_flags & PFLAG_COOP_WITH_HUMAN) && !(R.diplo_status[p] & DIPLO_WANT_REVENGE)) DiploHostility = 0;
    if (!(R.diplo_agenda[p] & 8)) {
        if (DiploCanBetray) {
            DiploHostility = 0;
            if (DiploFriction > 6) DiploFriction = 6;
        }
    } else {
        DiploHostility = 1;
    }
    uint32_t mine = P.diplo_status[r];
    if ((mine & DIPLO_PACT) && (!(mine & 0x20) || DiploRivalWarCount > 1) && (!(mine & 0x200) || DiploRivalWarCount) &&
        R.ranking < P.ranking && ((GameRules & RULES_VICTORY_COOPERATIVE) || DiploRivalWarCount)) {
        DiploHostility = 0;
    }
    if (R.diplo_agenda[p] & 0x2000) {
        DiploCanBetray = 0;
        DiploHostility = (R.diplo_status[p] >> 4) & 1;
    }
    if (!DiploPrevHostility && (DiploFearScale || (mine & DIPLO_PACT)) && DiploFriction > 14) DiploFriction = 14;
    if ((DiploHostility || (mine & DIPLO_VENDETTA) || (R.diplo_status[p] & DIPLO_WANT_REVENGE)) && DiploFriction < 6)
        DiploFriction = 6;
}

void GAMEAPI tech_analysis(int32_t faction_id, int32_t faction_id_with) {
    DiploTradeTechProject = -1;
    DiploTradeWantTech4 = DiploTradeWantTech3 = DiploTradeWantTech2 = DiploTradeWantTech1 = -1;
    DiploTradeOfferTech2 = DiploTradeOfferTech1 = -1;
    DiploTradeOfferValue2 = DiploTradeWantValue2 = DiploTradeOfferValue1 = DiploTradeWantValue1 = 0;
    DiploTradeWantValue4 = DiploTradeWantValue3 = 0;
    DiploTradeOfferProject2 = DiploTradeOfferProject1 = -1;
    if (GameRules & RULES_SCN_NO_TECH_TRADING) return;  // technology trading disabled
    for (int32_t t = 0; t < 89; t++) {
        if (!has_tech(t, faction_id_with)) {
            if (!has_tech(t, faction_id)) continue;
            int32_t v = tech_val(t, faction_id_with, 0);
            if (v >= DiploTradeWantValue1) {
                DiploTradeWantTech4 = DiploTradeWantTech3;
                DiploTradeWantTech3 = DiploTradeWantTech2;
                DiploTradeWantTech2 = DiploTradeWantTech1;
                DiploTradeWantValue4 = DiploTradeWantValue3;
                DiploTradeWantValue3 = DiploTradeWantValue2;
                DiploTradeWantValue2 = DiploTradeWantValue1;
                DiploTradeWantValue1 = v;
                DiploTradeWantTech1 = t;
            } else if (v >= DiploTradeWantValue2) {
                DiploTradeWantValue4 = DiploTradeWantValue3;
                DiploTradeWantValue3 = DiploTradeWantValue2;
                DiploTradeWantTech4 = DiploTradeWantTech3;
                DiploTradeWantTech3 = DiploTradeWantTech2;
                DiploTradeWantTech2 = t;
                DiploTradeWantValue2 = v;
            } else if (v >= DiploTradeWantValue3) {
                DiploTradeWantValue4 = DiploTradeWantValue3;
                DiploTradeWantTech4 = DiploTradeWantTech3;
                DiploTradeWantTech3 = t;
                DiploTradeWantValue3 = v;
            } else if (v >= DiploTradeWantValue4) {
                DiploTradeWantValue4 = v;
                DiploTradeWantTech4 = t;
            }
        } else if (!has_tech(t, faction_id)) {
            // A project that needs this technology and is in faction_id_with's production
            // (project index), or that faction_id could build (-2 - index, harder AI only).
            int32_t project = -1;
            if (game().factions[faction_id].diff_level > 2 ||
                !std::strcmp(game().mfactions[faction_id_with].filename, "HIVE")) {
                for (int32_t j = 0; j < 64; j++) {
                    if (SecretProjects[j] != -1 || Facility[FAC_HUMAN_GENOME_PROJECT + j].preq_tech != t) continue;
                    for (int32_t i = 0; i < game().base_count; i++) {
                        if ((uint8_t)game().bases[i].faction_id == (uint32_t)faction_id_with &&
                            game().bases[i].queue_items[0] == -(FAC_HUMAN_GENOME_PROJECT + j)) {
                            DiploTradeTechProject = j;
                            project = j;
                        }
                    }
                    if (((game().factions[faction_id].diff_level > 4 &&
                          (!MultiplayerActive || faction_id != RankingFactionIDUnk2)) ||
                         (MultiplayerActive && faction_id == RankingFactionIDUnk1)) &&
                        DiploTradeTechProject == -1) {
                        DiploTradeTechProject = -2 - j;
                        project = -2 - j;
                    }
                }
            }
            int32_t v = tech_val(t, faction_id, 0);
            if (project != -1) v = (v + 1) / 2;
            if (v >= DiploTradeOfferValue1) {
                DiploTradeOfferValue2 = DiploTradeOfferValue1;
                DiploTradeOfferProject2 = DiploTradeOfferProject1;
                DiploTradeOfferTech2 = DiploTradeOfferTech1;
                DiploTradeOfferTech1 = t;
                DiploTradeOfferProject1 = project;
                DiploTradeOfferValue1 = v;
            } else if (v >= DiploTradeOfferValue2) {
                DiploTradeOfferValue2 = v;
                DiploTradeOfferTech2 = t;
                DiploTradeOfferProject2 = project;
            }
        }
    }
    if (DiploTradeOfferProject2 != -1) DiploTradeOfferTech2 = -1;
}

// ---- callees of other modules ----------------------------------------------------------------------

void GAMEAPI energy_compute(int32_t faction_id, int32_t recompute) {
    for (int32_t i = 0; i < 70; i++) EnergyFacilityCount[i] = 0;
    FactionCurrentLabs = 0;
    FactionCurrentNetIncome = 0;
    FactionCurrentTotalMaint = 0;
    FactionCurrentGrossIncome = 0;
    FactionCurrentTotalLoan = 0;
    uint32_t carry = 0;  // odd maintenance left over when the Self-Aware Colony halves it
    for (int32_t base_id = 0; base_id < game().base_count; base_id++) {
        EnergyBaseMaint[base_id] = 0;
        BASE& b = game().bases[base_id];
        if ((uint8_t)b.faction_id != (uint32_t)faction_id) continue;
        if (recompute) {
            set_base(base_id);
            base_compute(1);
        }
        int32_t maint = 0;
        for (int32_t fac = 0; fac < 70; fac++) {
            if (fac >= 65) continue;
            int32_t offset, mask;
            bitmask(fac, &offset, &mask);
            if (b.facilities_built[offset] & (uint8_t)mask) {
                EnergyFacilityCount[fac]++;
                maint += fac_maint(fac, faction_id);
            }
        }
        int32_t colony = SecretProjects[FAC_SELF_AWARE_COLONY - FAC_HUMAN_GENOME_PROJECT];
        if (colony >= 0 && (uint8_t)game().bases[colony].faction_id == (uint32_t)faction_id) {
            if (carry) maint++;
            carry = maint & 1;
            maint /= 2;
        }
        EnergyBaseMaint[base_id] = maint;
        FactionCurrentLabs += b.labs_total;
        FactionCurrentGrossIncome += b.economy_total;
        FactionCurrentNetIncome += b.economy_total - maint;
        FactionCurrentTotalMaint += maint;
    }
    int32_t cost = tech_rate(faction_id);
    Faction& f = game().factions[faction_id];
    FactionCurrentLabs = (clamp(f.SE_research_pending, -5, 5) + 10) * FactionCurrentLabs * 10;
    if (FactionCurrentLabs < 1)
        FactionCurrentBreakthroughTurns = 9999;
    else
        FactionCurrentBreakthroughTurns = (FactionCurrentLabs - 1 + cost * 100) / FactionCurrentLabs;
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || !alive(i) || !alive(faction_id) || (dstatus(faction_id, i) & DIPLO_VENDETTA)) continue;
        if (f.loan_balance[i] && !game().factions[i].sanction_turns) {
            FactionCurrentNetIncome -= f.loan_payment[i];
            FactionCurrentTotalLoan -= f.loan_payment[i];
        }
        if (game().factions[i].loan_balance[faction_id] && !f.sanction_turns) {
            FactionCurrentNetIncome += game().factions[i].loan_payment[faction_id];
            FactionCurrentTotalLoan += game().factions[i].loan_payment[faction_id];
        }
    }
}

void GAMEAPI say_fac_special(char* buffer, const char* text, int32_t faction_id) {
    char markup[256], parsed[256];
    markup[0] = 0;
    int32_t plural = game().mfactions[faction_id].is_noun_plural;
    int32_t gender = game().mfactions[faction_id].noun_gender;
    crt_strcat(markup, "$<");
    if (gender == 0)
        crt_strcat(markup, "M");
    else if (gender == 1)
        crt_strcat(markup, "F");
    else if (gender == 2)
        crt_strcat(markup, "N");
    if (plural == 0)
        crt_strcat(markup, "1");
    else if (plural == 1)
        crt_strcat(markup, "2");
    crt_strcat(markup, ":");
    crt_strcat(markup, text);
    crt_strcat(markup, ">");
    parse_string(markup, parsed);
    crt_strcat(buffer, parsed);
}

const char* GAMEAPI get_phrase(int32_t faction_id, int32_t n) {
    const MFaction& m = game().mfactions[faction_id];
    if (text_open(m.filename, m.search_key) && text_open(m.filename, m.filename)) {
        parse_says(0, m.search_key, -1, -1);
        parse_says(1, m.filename, -1, -1);
        X_pop("PLAYERFILE", 0);
        return Strings_get(&StringTable, nullptr);
    }
    for (int32_t i = n + 9; i > 0; i--) text_get();
    return TextBufferGetPtr;
}

const char* GAMEAPI get_noun_phrase(int32_t faction_id, int32_t n) {
    const MFaction& m = game().mfactions[faction_id];
    if (text_open(m.filename, m.search_key) && text_open(m.filename, m.filename)) {
        parse_says(0, m.search_key, -1, -1);
        parse_says(1, m.filename, -1, -1);
        X_pop("PLAYERFILE", 0);
        return Strings_get(&StringTable, nullptr);
    }
    for (int32_t i = n + 9; i > 0; i--) text_get();
    char noun[256];
    crt_strcpy(noun, text_item());
    const char* kind = text_item();
    GenderDefault = 0;
    if (kind[0] == 'F' || kind[0] == 'f') GenderDefault = 1;
    if (kind[0] == 'N' || kind[0] == 'n') GenderDefault = 2;
    PluralDefault = kind[1] == '2';
    crt_strcpy(TextBufferGetPtr, noun);
    return TextBufferGetPtr;
}

const char* GAMEAPI get_pact(int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    return label_get((GenderDefault != 0) + 0xC9);
}

const char* GAMEAPI get_pacts(int32_t faction_id) {
    PluralDefault = 1;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    return label_get((GenderDefault != 0) + 0xCB);
}

const char* GAMEAPI get_pacts_2(int32_t faction_id, int32_t faction_id_with) {
    bool both_female =
        game().mfactions[faction_id].is_leader_female && game().mfactions[faction_id_with].is_leader_female;
    GenderDefault = both_female ? 1 : 0;
    PluralDefault = 1;
    return label_get(both_female ? 0xCC : 0xCB);
}

// ---- treaties signed in the dialog ------------------------------------------------------------------

void GAMEAPI pact_of_brotherhood(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    net_treaty_on(faction_id_with, faction_id, 0x1000001, 0);
    game().factions[faction_id_with].diplo_patience[faction_id] = 0;
    game().factions[faction_id].diplo_spoke[faction_id_with] = CurrentTurn;
    diplomacy_check(faction_id, faction_id_with, 0);
    DiploCanBetray = 0;
    game().factions[faction_id].diplo_reputation++;
    game().factions[faction_id_with].diplo_reputation++;
    diplomacy_caption(faction_id, faction_id_with);
    say_leader(0, faction_id_with);
    say_noun(2, faction_id_with);
    parse_says(3, get_pact(faction_id), -1, -1);
    parse_says(4, get_phrase(faction_id, 0), -1, -1);
    parse_says(5, get_phrase(faction_id_with, 0), -1, -1);
    X_pops_11("MAKEPACT", 0x100000, FactionPortraits[faction_id_with], kPopWait);
    net_maps(faction_id, faction_id_with, 1);
    parse_says(0, get_pact(faction_id), -1, -1);
    X_pops_11("HERESTHEMAP", 0x100000, FactionPortraits[faction_id_with], kPopWait);
    scan_prototypes(faction_id, faction_id_with);
    mention_prototypes(faction_id, faction_id_with);
    if (DiffLevel > 1 && !tut_check2(0x1000)) return;
    if (!MultiplayerActive) {
        say_noun(0, faction_id_with);
        say_leader(1, faction_id_with);
        parse_says(3, game().mfactions[faction_id_with].adj_name_faction, -1, -1);
        X_pop_2("TUTOR", "PACT", 0);
    }
}

void GAMEAPI make_treaty(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    net_treaty_on(faction_id_with, faction_id, 0x1000202, 1);
    game().factions[faction_id_with].diplo_patience[faction_id] = 0;
    game().factions[faction_id].diplo_spoke[faction_id_with] = CurrentTurn;
    diplomacy_check(faction_id, faction_id_with, 0);
    game().factions[faction_id].diplo_reputation++;
    game().factions[faction_id_with].diplo_reputation++;
    diplomacy_caption(faction_id, faction_id_with);
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    say_leader(3, faction_id_with);
    say_noun(5, faction_id_with);
    parse_says(6, get_noun_phrase(faction_id, 9), -1, -1);
    parse_says(7, get_noun_phrase(faction_id_with, 9), -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "MAKETREATY");
    label_with_number(StrBuffer, game_random(0, 2));
    X_pops_11(StrBuffer, 0x100000, FactionPortraits[faction_id_with], kPopWait);
    if ((DiffLevel <= 1 || tut_check2(0x800)) && !MultiplayerActive) {
        say_noun(0, faction_id_with);
        X_pop_2("TUTOR", "COMMERCE", 0);
    }
    if (territory(faction_id_with, faction_id, 2, nullptr, nullptr)) {
        say_noun(0, faction_id_with);
        parse_says(1, game().mfactions[faction_id_with].adj_name_faction, -1, -1);
        NetMsg_pop(&NetMsg, "TIMETOLEAVE", 5000, 0, nullptr);
    }
}

void GAMEAPI pledge_truce(int32_t faction_id, int32_t faction_id_with, int32_t show) {
    DiploSecondFaction = faction_id_with;
    net_treaty_on(faction_id_with, faction_id, 0x204, 0);
    game().factions[faction_id].diplo_spoke[faction_id_with] = CurrentTurn;
    diplomacy_check(faction_id, faction_id_with, 0);
    game().factions[faction_id].diplo_reputation++;
    game().factions[faction_id_with].diplo_reputation++;
    if (!show) return;
    diplomacy_caption(faction_id, faction_id_with);
    say_leader(0, faction_id);
    say_noun(2, faction_id);
    say_leader(3, faction_id_with);
    say_noun(5, faction_id_with);
    X_pops_11("MAKETRUCE", 0x100000, FactionPortraits[faction_id_with], kPopWait);
}

void GAMEAPI diplomacy_ends(int32_t faction_id, int32_t faction_id_with) {
    DiploWinState = 0;
    DiploTalkActive = 0;
    DiploCaptionOff = 0;
    DiploCaptionMode = 0;
    DiploPop_stop(&DiploPopWin);
    // synch_diplo and NetDaemon_close_channel only send messages in multiplayer games.
    if (MultiplayerActive) {
        synch_diplo(faction_id, faction_id_with);
        NetDaemon_close_channel(&NetState);
    }
    DiploFocusY = -1;
    DiploFocusX = -1;
}

void GAMEAPI demands_withdrawal(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (commlink_attempter(faction_id, faction_id_with)) return;
    if (MultiplayerActive && human(faction_id_with)) return;
    int32_t result = demand_withdrawal(faction_id, faction_id_with);
    if (SunspotDuration > 0) {
        say_noun(0, faction_id_with);
        if (result == 0)
            NetMsg_pop(&NetMsg, "WITHSUNYES", 5000, 0, nullptr);
        else if (result == 1)
            NetMsg_pop(&NetMsg, "WITHSUNNO", 5000, 0, nullptr);
        else if (result == 2)
            NetMsg_pop(&NetMsg, "WITHSUNWAR", 5000, 0, nullptr);
    }
}

void GAMEAPI renounce_pact(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (commlink_attempter(faction_id, faction_id_with)) return;
    if (diplo_lock(1)) return;  // inlined in the original
    game().factions[faction_id].integrity_blemishes++;
    net_treaty_off(faction_id_with, faction_id, DIPLO_PACT, 1);
    if (SunspotDuration < 1 && !human(faction_id_with)) {
        diplomacy_check(faction_id, faction_id_with, 0);
        diplomacy_caption(faction_id, faction_id_with);
        parse_says(0, get_pact(faction_id), -1, -1);
        say_leader(1, faction_id);
        X_pops_11("BROKEPACT", 0x100000, FactionPortraits[faction_id_with], kPopWait);
    }
    net_pact_ends(faction_id, faction_id_with, 1);
    diplo_unlock();  // inlined in the original
}

// ---- withdrawal of units from a faction's territory ---------------------------------------------

int32_t GAMEAPI do_withdrawal(int32_t faction_id, int32_t faction_id_with) {
    int32_t moved = 0;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        VEH& veh = game().vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id_with || Units[veh.unit_id].plan == PLAN_PROBE) continue;
        const int32_t x = veh.x, y = veh.y;
        const MAP* sq = map_tile(x, y);
        if ((sq->climate & 0xE0) < 0x60 || sq->owner != faction_id) continue;
        if (Chassis[Units[veh.unit_id].chassis_id].triad == TRIAD_LAND && veh_cargo(veh_id)) {
            // A land transport (carrying units) steps off to an adjacent tile of its own territory
            // or onto an adjacent friendly sea transport. The loop does not stop: it may move
            // several times.
            for (int32_t k = 0; k < 8; k++) {
                int32_t nx = map_wrap_x(NearOffsetX[k] + x);
                int32_t ny = NearOffsetY[k] + y;
                if (!map_in_bounds(nx, ny)) continue;
                const MAP* near = map_tile(nx, ny);
                if ((near->climate & 0xE0) < 0x60) {
                    if (stack_check(stack_fix(veh_at(nx, ny)), 8, faction_id_with, -1, -1) <= 0) continue;
                } else if (near->owner != faction_id_with) {
                    continue;
                }
                uint32_t occupant = near->val2 & 0xF;
                if ((near->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && occupant < 8 &&
                    occupant != (uint32_t)faction_id_with)
                    continue;
                veh_drop(veh_lift(veh_id), nx, ny);
                veh.visibility = 0;
                veh.order = ORDER_NONE;
                veh.state &= 0xFCFFBDFF;
                draw_tiles(x, y, 2);
                moved++;
            }
        }
        int32_t base_id = base_find_faction(x, y, faction_id_with);
        if (base_id < 0) continue;
        veh_drop(veh_lift(veh_id), game().bases[base_id].x, game().bases[base_id].y);
        veh.visibility = 0;
        veh.order = ORDER_NONE;
        veh.state &= 0xFCFFBDFF;
        draw_tile_fixup(x, y, has_abil(veh.unit_id, 2), 2);
        moved++;
    }
    if (moved) {
        set_agenda(faction_id_with, faction_id, 2, 1);
        set_treaty(faction_id_with, faction_id, 0x100000, 1);
    }
    return moved;
}

int32_t GAMEAPI dont_withdrawal(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (!(dstatus(faction_id, faction_id_with) & 0x800000)) return 0;
    for (int32_t veh_id = 0; veh_id < game().veh_count; veh_id++) {
        const VEH& veh = game().vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id_with || Units[veh.unit_id].plan == PLAN_PROBE) continue;
        int32_t x = veh.x, y = veh.y;
        if (map_is_ocean(x, y)) continue;
        if (base_find_faction(x, y, faction_id) < 0) continue;
        if (whose_territory(faction_id, x, y, nullptr, 0) != faction_id_with || veh.order != ORDER_MOVE_TO) continue;
        int32_t target = base_at(veh.waypoint_x[0], veh.waypoint_y[0]);
        if (target < 0) continue;
        uint32_t owner = (uint8_t)game().bases[target].faction_id;
        if (owner == (uint32_t)faction_id || owner == (uint32_t)faction_id_with || owner == 0 ||
            !(dstatus(faction_id_with, owner) & DIPLO_VENDETTA)) {
            continue;
        }
        // On its way to attack a base of an enemy of faction_id_with that faction_id is allied to.
        if (dstatus(faction_id, owner) & DIPLO_PACT) return owner;
        if ((dstatus(faction_id, owner) & DIPLO_TREATY) && DiploAlterFear > 0) return owner;
    }
    return 0;
}

int32_t GAMEAPI demand_withdrawal(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    bool shown = false;
    DiploSecondFaction = r;
    diplomacy_caption(f, r);
    uint32_t theirs = dstatus(r, f);
    if (!(theirs & 0x200) && !(theirs & DIPLO_PACT) &&
        (DiploCanBetray || DiploAttackChance || ((DiploPrevHostility ? 5 : 15) < DiploFriction && !DiploFearScale))) {
        // The demand is answered with a vendetta.
        net_treaty_on(r, f, DIPLO_VENDETTA, 1);
        game().factions[f].diplo_spoke[r] = CurrentTurn;
        if (SunspotDuration < 1) {
            diplomacy_caption(f, r);
            say_leader(0, f);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "NEVER");
            X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
        }
        return 2;
    }
    uint32_t mine = dstatus(f, r);
    int32_t ally;
    if (!(mine & DIPLO_PACT) && (ally = dont_withdrawal(f, r)) != 0 && (dstatus(f, r) & DIPLO_TREATY)) {
        // Its units are on their way to faction_id's ally: faction_id_with breaks the treaty.
        net_double_cross(r, f, -1, 1);
        if (SunspotDuration < 1) {
            say_leader(0, f);
            say_leader(2, ally);
            X_pops_11("RUNNINGDOG", 0x100000, portrait, kPopWait);
            DiploCanBetray = 0;
            game().factions[f].diplo_spoke[r] = CurrentTurn;
        }
        return 2;
    }
    int32_t fear = DiploFearScale;
    if (!(dstatus(f, r) & DIPLO_TREATY)) {
        int32_t& friction = game().factions[r].diplo_friction[f];
        friction = clamp(friction + 1, 0, 20);
        if (DiploFrictionFactionID == r && DiploFrictionFactionIDWith == f) DiploFriction++;
        if ((dstatus(r, f) & 0x20) || DiploCanBetray || DiploAttackChance || fear + 3 < DiploReputation ||
            (DiploPrevHostility && !fear)) {
            if (SunspotDuration < 1) {
                say_leader(0, f);
                X_pops_11("NOWAYJOSE", 0x100000, portrait, kPopWait);
            }
            return 1;
        }
        if (territory(r, f, 0, nullptr, nullptr)) {
            // faction_id_with demands the same in return.
            if (SunspotDuration > 0) return 1;
            shown = true;
            PluralDefault = 0;
            GenderDefault = game().mfactions[f].is_leader_female;
            parse_says(0, game().mfactions[f].title_leader, -1, -1);
            if (X_pops_11("TURNABOUT", 0x100000, portrait, kPopWait)) net_withdrawal(r, f, 0);
        }
    }
    int32_t moved = net_withdrawal(f, r, 1);
    if (!shown && SunspotDuration < 1) {
        PluralDefault = 0;
        GenderDefault = game().mfactions[f].is_leader_female;
        parse_says(0, game().mfactions[f].name_leader, -1, -1);
        parse_says(1, game().mfactions[f].adj_leader, -1, -1);
        if (!moved) {
            X_pops_11("NOWITHDRAW", 0x100000, portrait, kPopWait);
        } else if (dstatus(f, r) & DIPLO_PACT) {
            parse_says(2, get_pact(f), -1, -1);
            X_pops_11("WITHDRAWPACT", 0x100000, portrait, kPopWait);
        } else if (dstatus(f, r) & DIPLO_TREATY) {
            X_pops_11("WITHDRAW", 0x100000, portrait, kPopWait);
        } else {
            X_pops_11("WITHDRAWTRUCE", 0x100000, portrait, kPopWait);
        }
    }
    return 0;
}

// ---- battle plans (pacts) -------------------------------------------------------------------------

namespace {
// The original's inlined distance of two tiles (vector_dist with the horizontal wrap).
int32_t tile_distance_inline(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    int32_t dy = std::abs(y1 - y2);
    int32_t hi = dx > dy ? dx : dy;
    int32_t lo = dx < dy ? dx : dy;
    return hi - ((((dx + dy) >> 1) - lo + 1) >> 1);
}
}  // namespace

int32_t GAMEAPI suggest_plan(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    const Faction& f = game().factions[faction_id];
    int32_t best = -1, best_score = 9999;
    for (int32_t base_id = 0; base_id < game().base_count; base_id++) {
        const BASE& b = game().bases[base_id];
        uint32_t owner = (uint8_t)b.faction_id;
        if (owner == (uint32_t)faction_id || owner == (uint32_t)faction_id_with ||
            !(dstatus(faction_id, owner) & DIPLO_VENDETTA) || !(dstatus(faction_id_with, owner) & DIPLO_VENDETTA) ||
            (owner != (uint32_t)faction_id && !(b.visibility & (1 << faction_id)))) {
            continue;
        }
        int32_t region = map_tile(b.x, b.y)->region;
        int32_t dist = 0;
        if (f.target_x >= 0) {
            dist = tile_distance_inline(f.target_x, f.target_y, b.x, b.y) * 2;
            if (region != map_tile(f.target_x, f.target_y)->region) dist += 0x200;
        }
        if (base_find_filtered(b.x, b.y, faction_id, region, -1, -1) < 0) {
            base_find_faction(b.x, b.y, faction_id);
            BaseFindDist += 0x400;
        }
        int32_t own = ((f.target_x >= 0) + 1) * BaseFindDist;
        if (base_find_filtered(b.x, b.y, faction_id_with, region, -1, -1) < 0) {
            base_find_faction(b.x, b.y, faction_id_with);
            BaseFindDist += 0x100;
        }
        int32_t score = dist + own + BaseFindDist;
        if (f.base_id_attack_target == base_id) return base_id;
        if (score < best_score) {
            best = base_id;
            best_score = score;
        }
    }
    return best;
}

int32_t GAMEAPI attack_from(int32_t base_id, int32_t faction_id) {
    const BASE& target = game().bases[base_id];
    int32_t region = map_tile(target.x, target.y)->region;
    int32_t best = 0, best_dist = 9999;
    for (int32_t i = 0; i < game().base_count; i++) {
        const BASE& b = game().bases[i];
        if ((uint8_t)b.faction_id != (uint32_t)faction_id) continue;
        int32_t dist = tile_distance_inline(b.x, b.y, target.x, target.y);
        if (b.x == game().factions[faction_id].target_x && b.y == game().factions[faction_id].target_y) dist = 0;
        if (map_tile(b.x, b.y)->region != region) dist += 1000;
        if (dist <= best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    return best;
}

// ---- the AI asks to speak -----------------------------------------------------------------------------

int32_t GAMEAPI wants_to_speak(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    uint32_t alien = game().mfactions[faction_id].rule_flags, alien_with = game().mfactions[faction_id_with].rule_flags;
    if ((alien ^ alien_with) & 0x80) {
        // Humans and aliens talk only once one of them has the technology to understand the other.
        bool understood = (alien & 0x80)
                              ? (has_tech(TECH_PrPsych, faction_id_with) || has_tech(TECH_Psych, faction_id))
                              : (has_tech(TECH_PrPsych, faction_id) || has_tech(TECH_Psych, faction_id_with));
        if (!understood) {
            DiploCaptionMode = 0;
            return 0;
        }
    }
    if (DiploCaptionOff) return DiploCaptionMode;
    if (!DiploCaptionMode) {
        bool busters = false;
        InfoWin_set_state(&InfoWindow, 2, 1);
        while (!DiploCaptionOff) {
            if (game().factions[faction_id_with].planet_busters && !(dstatus(faction_id_with, faction_id) & 0x4000))
                busters = true;
            diplomacy_check(faction_id, faction_id_with, 0);
            if (DiploFocusY >= 0 && DiploFocusY < game().map.y && DiploFocusX >= 0 && DiploFocusX < game().map.x &&
                !Console_focus(&MainConsole, DiploFocusX, DiploFocusY, faction_id)) {
                draw_tiles(DiploFocusX, DiploFocusY, 2);
            }
            diplomacy_caption(faction_id, faction_id_with);
            say_leader(0, faction_id_with);
            say_noun(2, faction_id_with);
            parse_num(0, game_random(0, 10) + 1);
            parse_says(3, get_him_her(faction_id_with, 0), -1, -1);
            if (busters) {
                X_pops("URGENTCOMM", FactionPortraits[faction_id_with], kPopWait);
                break;
            }
            ui_diplo_menu_start("COMM", 0);
            int32_t choice = ui_diplo_menu_exec(faction_id_with);
            ui_diplo_menu_close();
            if (choice < 2) {
                DiploCaptionOff = 1;
                DiploCaptionMode = choice == 0;
                break;
            }
            report_intel(faction_id_with);
        }
    } else {
        DiploCaptionOff = 1;
        if (DiploCaptionMode >= 8) {
            diplomacy_check(faction_id, faction_id_with, 0);
            diplomacy_caption(faction_id, faction_id_with);
            say_leader(0, faction_id_with);
            say_noun(2, faction_id_with);
            parse_num(0, game_random(0, 10) + 1);
            parse_says(3, get_him_her(faction_id_with, 0), -1, -1);
            X_pops("URGENTCOMM", FactionPortraits[faction_id_with], kPopWait);
        }
    }
    if (DiploCaptionMode) {
        DiploPop_start(&DiploPopWin, faction_id_with);
        introduce(faction_id, faction_id_with);
    }
    return DiploCaptionMode;
}

void GAMEAPI introduce(int32_t faction_id, int32_t faction_id_with) {
    const MFaction& m = game().mfactions[faction_id];
    DiploSecondFaction = faction_id_with;
    DiploWinState = 1;
    diplomacy_caption(faction_id, faction_id_with);
    say_leader(0, faction_id);
    say_leader(2, faction_id_with);
    say_noun(4, faction_id_with);
    parse_says(8, game().mfactions[faction_id_with].adj_name_faction, -1, -1);
    parse_says(9, m.adj_leader, -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "INTRO");
    uint32_t s = dstatus(faction_id, faction_id_with);
    int32_t variant;
    if (!(s & 0x8000000) || ((MultiplayerActive || PbemActive) && (DiploStateA[faction_id_with] & 2))) {
        // First words.
        crt_strcat(StrBuffer, "NEW");
        variant = game_random(0, 6);
        parse_says(6, get_phrase(faction_id_with, 1), -1, -1);
        if (variant == 2) {
            say_noun(5, faction_id);
        } else if (variant == 3) {
            char might[100];
            might[0] = 0;
            int32_t i = clamp(7 - game().factions[faction_id].ranking, 0, 6);
            const char* adj = Strings_get(&StringTable, Might[i].adj);
            if (!GameLanguage) {
                char c = adj[0];
                crt_strcat(might, label_get(c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' ? 277 : 276));
                crt_strcat(might, " ");
            }
            crt_strcat(might, adj);
            parse_says(5, might, -1, -1);
        }
    } else if (s & 0x20) {
        // Seeking revenge.
        crt_strcat(StrBuffer, "MAD");
        variant = game_random(0, 5);
        switch (variant) {
        case 0:
            parse_says(5, get_phrase(faction_id, 3), -1, -1);
            parse_says(6, get_noun_phrase(faction_id, 8), -1, -1);
            break;
        case 2:
            parse_says(6, get_noun_phrase(faction_id, 8), -1, -1);
            break;
        case 1:
            parse_says(5, m.adj_insult_faction, -1, -1);
            parse_says(6, get_phrase(faction_id, 3), -1, -1);
            break;
        case 4:
            parse_says(5, m.adj_insult_leader, -1, -1);
            GenderDefault = m.is_leader_female;
            PluralDefault = 0;
            parse_says(6, m.insult_leader, -1, -1);
            parse_says(8, get_phrase(faction_id, 6), -1, -1);
            break;
        case 3:
            parse_says(8, get_phrase(faction_id, 6), -1, -1);
            break;
        default:
            break;
        }
    } else {
        variant = game_random(0, 4);
        bool friendly = (dstatus(faction_id, faction_id_with) & DIPLO_PACT) || !DiploHostility;
        switch (variant) {
        case 0:
            parse_says(5, get_noun_phrase(faction_id, friendly ? 9 : 10), -1, -1);
            break;
        case 1:
            parse_says(9, friendly ? m.adj_leader : m.adj_insult_leader, -1, -1);
            break;
        case 2:
            parse_says(5, friendly ? m.adj_faction : m.adj_insult_faction, -1, -1);
            break;
        case 3:
            parse_says(9, friendly ? m.adj_leader : m.adj_insult_leader, -1, -1);
            parse_says(5, friendly ? m.adj_faction : m.adj_insult_faction, -1, -1);
            break;
        default:
            break;
        }
    }
    label_with_number(StrBuffer, variant);
    X_pops_11(StrBuffer, 0x100000, FactionPortraits[faction_id_with], kPopWait);
    if (game().factions[faction_id_with].planet_busters)
        X_pops_11("PLANETBUSTER0", 0x100000, FactionPortraits[faction_id_with], kPopWait);
    if (game().factions[faction_id].planet_busters && !(dstatus(faction_id, faction_id_with) & 0x4000)) {
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "PLANETBUSTER");
        label_with_number(StrBuffer, (game().factions[faction_id_with].planet_busters != 0) + 1);
        X_pops_11(StrBuffer, 0x100000, FactionPortraits[faction_id_with], kPopWait);
        net_set_treaty(faction_id, faction_id_with, 0x4000, 1, 1);
    }
}

// ---- trades proposed by the human ------------------------------------------------------------------

namespace {
// TechOwners[tech_id]. BUG(original): callers pass technology ids that may be -1 or prototype
// ids (97 + unit_id), past the 89 entries of the table; those bytes are read from the memory image.
uint8_t tech_owners(int32_t tech_id) {
    COMPAT_ORIGINAL(out_of_range_reads);
    return (uint32_t)tech_id < 89 ? TechOwners[tech_id] : rules_orig_byte(0x9A6670 + (uint32_t)tech_id);
}
}  // namespace

int32_t GAMEAPI buy_tech(int32_t faction_id, int32_t faction_id_with, int32_t offer, int32_t eager, int32_t mode) {
    const char* portrait = FactionPortraits[faction_id_with];
    DiploSecondFaction = faction_id_with;
    if (mode == 5) {
        if (DiploBestUnitID < 0) {
            scan_prototypes(faction_id, faction_id_with);
            if (DiploBestUnitID < 0) return 0;
        }
        DiploTradeOfferTech1 = DiploBestUnitID + 97;
    } else if (mode != 13) {
        if (DiploTradeOfferTech1 < 0) return 0;
        if (!std::strcmp(game().mfactions[faction_id_with].filename, "UNIV") && DiploTradeWantTech1 >= 0) return 0;
    }
    if (offer == 1) return 0;
    int32_t value = tech_val(DiploTradeOfferTech1, faction_id_with, 1) * ((CurrentTurn + 7) / 8);
    if (great_satan(faction_id, 0)) value *= 2;
    if (DiploGreatSatan) value *= 2;
    if (eager) value = value * 3 / 2;
    value /= clamp(bit_count(tech_owners(DiploTradeOfferTech1)) - 1, 1, 999);
    const Faction& f = game().factions[faction_id];
    const Faction& w = game().factions[faction_id_with];
    if ((dstatus(faction_id, faction_id_with) & DIPLO_PACT) && f.ranking < w.ranking) {
        if (f.tech_ranking + 8 < w.tech_ranking)
            value /= 4;
        else if (f.tech_ranking < w.tech_ranking)
            value /= 2;
    }
    int32_t price = (value + 25) / 25 * 25;
    if (mode != 13 && price < 100) price = 100;
    if (f.energy_credits < price && offer != 5 && mode != 5) return 0;
    StrBuffer[0] = 0;
    say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
    parse_says(0, StrBuffer, -1, -1);
    parse_num(0, price);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "BUY");
    if (mode == 5) {
        crt_strcat(StrBuffer, "PROTO");
    } else if (mode == 13) {
        crt_strcat(StrBuffer, "COMMLINK");
        parse_says(0, game().mfactions[DiploProposalFaction].adj_name_faction, -1, -1);
    } else {
        crt_strcat(StrBuffer, "TECH");
    }
    if (price <= game().factions[faction_id].energy_credits) {
        label_with_number(StrBuffer, (mode == 5 || mode == 13) ? 0 : game_random(0, 2));
        if (X_pops_11(StrBuffer, 0x100000, portrait, kPopWait) == 1) {
            net_energy(faction_id, -price, faction_id_with, price, 0);
            net_tech(faction_id, DiploTradeOfferTech1, faction_id_with, 1);
            if (mode == 5) DiploBestUnitID = -1;
            InfoWindowTechReset = 0;
            Console_update_data(&MainConsole, 0);
            diplomacy_caption(faction_id, faction_id_with);
        }
        return 1;
    }
    crt_strcat(StrBuffer, "HIGH");
    label_with_number(StrBuffer, (mode == 5 || mode == 13) ? 0 : game_random(0, 2));
    X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
    return 1;
}

int32_t GAMEAPI energy_trade(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    DiploSecondFaction = r;
    tech_analysis(f, r);
    bool sell_tech = DiploCounterProposal == 4 || DiploCounterProposal == 2;  // the human asks for energy
    bool loan = false;
    if ((dstatus(r, f) & DIPLO_PACT) && DiploCounterProposal == 6) {
        loan = true;
    } else if (dstatus(r, f) & DIPLO_PACT) {
        Faction& P = game().factions[f];
        Faction& R = game().factions[r];
        if (R.ranking > P.ranking + 1 && R.energy_credits > P.energy_credits * 2 && R.tech_ranking > P.tech_ranking &&
            R.energy_credits > CurrentTurn * 4 && R.diplo_patience[f] < 2 &&
            (DiploTradeWantTech1 < 0 || DiploCounterProposal == 1)) {
            // A wealthy pact partner gives energy away.
            int32_t credits = R.energy_credits;
            int32_t gift = clamp(credits / 8, 1, 500);
            gift = (gift + 24) / 25 * 25;
            // BUG(original): max instead of min: the whole treasury is given.
            COMPAT_ORIGINAL(gift_whole_treasury);
            if (gift < credits) gift = credits;
            net_energy(r, -gift, f, gift, 1);
            StrBuffer[0] = 0;
            R.diplo_patience[f] += 2;
            crt_strcat(StrBuffer, "ENERGYFREEBIE");
            label_with_number(StrBuffer, game_random(0, 2));
            X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
            return 0;
        }
    }
    if (!loan && (DiploCounterProposal == 6 || DiploCounterProposal == 1 ||
                  (DiploCounterProposal == 2 && DiploTradeWantTech1 < 0))) {
        loan = true;
    }
    if (!loan) {
        if (sell_tech) {
            // The human sells technology for energy.
            say_leader(0, f);
            const char* filename = game().mfactions[r].filename;
            if (DiploTradeWantTech1 < 0 || !std::strcmp(filename, "BELIEVE")) {
                X_pops_11("REJSELLNONE", 0x100000, portrait, kPopWait);
                return 0;
            }
            int32_t base = (CurrentTurn + 7) / 8 * 32;
            if (great_satan(f, 0)) base = base * 3 / 4;
            int32_t owners = clamp(bit_count(tech_owners(DiploTradeOfferTech1)) - 1, 1, 99);
            int32_t price = tech_val(DiploTradeWantTech1, r, 1) * (base / owners) / 32;
            int32_t price2 = 0;
            if (DiploTradeWantTech2 >= 0) price2 = tech_val(DiploTradeWantTech2, r, 1) * (base / owners) / 32;
            int32_t half = game().factions[r].energy_credits / 2;
            if (price < 0 || half < 0)
                price = 0;
            else if (price > half)
                price = half;
            price = price / 25 * 25;
            if (price < 25) price = 25;
            if (price2 < 25) price2 = 25;
            if (!price) {
                X_pops_11("REJSELLAFFORD", 0x100000, portrait, kPopWait);
                return 0;
            }
            for (;;) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
                parse_says(0, StrBuffer, -1, -1);
                if (DiploTradeWantTech2 >= 0) {
                    StrBuffer[0] = 0;
                    say_tech_2(StrBuffer, DiploTradeWantTech2, 1);
                    parse_says(1, StrBuffer, -1, -1);
                }
                parse_num(0, price);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "ENERGYTECH");
                if (DiploTradeWantTech2 >= 0)
                    crt_strcat(StrBuffer, "2");
                else
                    label_with_number(StrBuffer, game_random(0, 2));
                int32_t choice = X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                if (choice == 0) return 0;
                if (choice == 3 || (choice == 2 && DiploTradeWantTech2 < 0)) {
                    help_topic(0xE, DiploTradeWantTech1);
                    continue;
                }
                if (choice == 2) {
                    if (!std::strcmp(filename, "HIVE") || (price < price2 && std::strcmp(filename, "UNIV"))) {
                        X_pops_11("REJSELLSECOND", 0x100000, portrait, kPopWait);
                        return 0;
                    }
                    X_pops_11("SELLSECOND", 0x100000, portrait, kPopWait);
                    net_tech(r, DiploTradeWantTech2, f, 0);
                } else {
                    net_tech(r, DiploTradeWantTech1, f, 0);
                    DiploTradeWantTech1 = DiploTradeWantTech2;
                }
                DiploTradeWantTech2 = -1;
                net_energy(r, -price, f, price, 1);
                InfoWindowTechReset = 0;
                Console_update_data(&MainConsole, 0);
                return 1;
            }
        }
    } else {
        // A loan.
        Faction& P = game().factions[f];
        Faction& R = game().factions[r];
        int32_t credits = R.energy_credits;
        int32_t amount;
        if (R.tech_commerce_bonus < P.tech_commerce_bonus)
            amount = credits / 4;
        else if (R.tech_commerce_bonus == P.tech_commerce_bonus)
            amount = credits / 3;
        else
            amount = credits / 2;
        if (credits < P.energy_credits && std::strcmp(game().mfactions[r].filename, "MORGAN")) amount = 0;
        if (P.loan_balance[r] || R.loan_balance[f]) amount = 0;
        uint32_t s = dstatus(r, f);
        if (s & 0x30) amount = 0;
        if (!(s & DIPLO_TREATY) || amount < 10) amount = 0;
        int32_t rate = (DiploReputation + (R.tech_commerce_bonus - P.tech_commerce_bonus) * 2 + 10) * 5;
        if (dstatus(f, r) & DIPLO_PACT) {
            rate -= 10;
            if (P.ranking < R.ranking) rate -= 10;
        }
        if (rate < 0) rate = 0;
        int32_t term = (10 - DiploReputation) * 10;
        if (term < 20) term = 20;
        if (amount) {
            int32_t total = (rate + 100) * amount / 100;
            if (term < 10 || total < 10)
                term = 10;
            else if (total < term)
                term = total;
            int32_t payment = (total - 1 + term) / term;
            int32_t balance = payment * term;
            parse_num(0, amount);
            parse_num(1, payment);
            parse_num(2, term);
            say_leader(0, f);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "ENERGYLOAN");
            label_with_number(StrBuffer, (payment != 1) + 1);
            if (!X_pops_11(StrBuffer, 0x100000, portrait, kPopWait)) return 0;
            net_loan(f, r, balance, payment);
            net_energy(r, -amount, f, amount, 1);
            return 1;
        }
    }
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "REJENERGY");
    label_with_number(StrBuffer, game_random(0, 2));
    X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
    return 0;
}

void GAMEAPI propose_trade_maps(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (((dstatus(faction_id, faction_id_with) & DIPLO_PACT) || DiploFriction < 1) && DiploFriction < 5) {
        X_pops_11("TRADEMAPS", 0x100000, FactionPortraits[faction_id_with], kPopWait);
        game().factions[faction_id_with].diplo_patience[faction_id] += 2;
        net_maps(faction_id, faction_id_with, 1);
        DiploMapMsg = 1;
        return;
    }
    parse_says(0, game().mfactions[faction_id_with].adj_name_faction, -1, -1);
    X_pops_11("REJMAPS", 0x100000, FactionPortraits[faction_id_with], kPopWait);
}

// ---- treaties and pacts proposed by the human ----------------------------------------------------

namespace {
// faction_id_with's friction toward faction_id drops by delta (clamped to 0..20; the cached
// DiploFriction too when it belongs to this pair).
void lower_friction(int32_t faction_id, int32_t faction_id_with, int32_t delta) {
    int32_t& friction = game().factions[faction_id_with].diplo_friction[faction_id];
    friction = clamp(friction - delta, 0, 20);
    if (DiploFrictionFactionID == faction_id_with && DiploFrictionFactionIDWith == faction_id) DiploFriction -= delta;
}
}  // namespace

int32_t GAMEAPI propose_treaty(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    const char* filename = game().mfactions[r].filename;
    const Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    DiploSecondFaction = r;
    int32_t lenient = (!std::strcmp(filename, "GAIANS") && DiploAlterFear < 0) + (DiploAlterFear < 0);
    int32_t help = DiploHelpFaction ? DiploHelpFaction : DiploAcceptedFaction;
    // faction_id_with is losing a war against the faction it wants help against.
    bool needs_help = help && (dstatus(r, help) & 0x8000) && game().factions[help].mil_strength_1 > R.mil_strength_1 &&
                      !(dagenda(r, f) & 0x800);
    bool eager = DiploCounterProposal == 2 || DiploCounterProposal == 4 || DiploCounterProposal == 5 ||
                 DiploCounterProposal == 8;
    int32_t bonus = 0;
    bool accept = false;
    if (!DiploGreatSatan && !DiploRivalStrongPacts && R.ranking < 5 && P.ranking > 4 && !DiploPlayerPactCount)
        lenient++;
    if (eager) bonus = lenient + 2;
    if (DiploFriction <= bonus + 15 && !(dagenda(r, f) & 0x400) &&
        (!(dstatus(r, f) & 0x20) ||
         (DiploReputation < 7 &&
          DiploReputation <= R.tech_commerce_bonus - P.tech_commerce_bonus + (eager ? 2 : 0) - DiploAlterFear + 2))) {
        if (needs_help) {
            accept = true;
        } else if (!DiploGreatSatan) {
            int32_t diff = clamp(P.diff_level, 0, 2);
            if (DiploReputation <= (eager ? 2 : 0) - diff + 5 + lenient &&
                DiploCapturedBases <= eager + 4 + lenient + (DiploCounterProposal == 3 ? 2 : 0) &&
                (DiploFriction <= eager * 5 + 10 + lenient * 3 || DiploCounterProposal == 3)) {
                accept = true;
            }
        }
    }
    if ((R.player_flags & PFLAG_COOP_WITH_HUMAN) && !(dstatus(r, f) & 0x20)) accept = true;
    if (dagenda(f, r) & 0x2000) accept = false;
    const MFaction& m = game().mfactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.name_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.title_leader, -1, -1);
    say_noun(2, f);
    parse_says(3, m.adj_name_faction, -1, -1);
    if (!accept) {
        if (DiploReputation > 4 && (DiploFearScale || DiploFriction < 3) && !(dstatus(r, f) & 0x20)) {
            X_pops_11("REJTREATYMEEK", 0x100000, portrait, kPopWait);
            return 1;
        }
        if (DiploCounterProposal == 3) return 0;
        if (!(dstatus(r, f) & 0x20) && DiploReputation < 5) {
            if (!DiploGreatSatan) {
                X_pops_11("REJTREATY", 0x100000, portrait, kPopWait);
                return 1;
            }
            parse_says(4, get_noun_phrase(r, 9), -1, -1);
            parse_says(5, get_phrase(f, 3), -1, -1);
            X_pops_11("REJTREATYAHEAD", 0x100000, portrait, kPopWait);
            return 1;
        }
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(4, m.insult_leader, -1, -1);
        X_pops_11("REJTREATYHATE", 0x100000, portrait, kPopWait);
        return 1;
    }
    // The bribe faction_id_with asks for.
    int32_t bribe = (P.energy_credits + DiploReputation) * R.mil_strength_1 /
                    (P.mil_strength_1 + 1 + R.mil_strength_1) / 50 * 50;
    if (!(dstatus(r, f) & 0x20)) bribe -= clamp(R.tech_commerce_bonus - P.tech_commerce_bonus, 0, 99) * 50;
    if (!std::strcmp(filename, "MORGAN") && P.energy_credits < bribe) {
        int32_t credits = P.energy_credits;
        if (DiploTradeWantTech1 < 0 || tech_val(DiploTradeWantTech1, f, 1) * ((CurrentTurn + 7) / 8) <= credits)
            bribe = credits;
    }
    if (DiploCounterProposal != 5 && P.energy_credits < bribe)
        bribe -= (int32_t)(((uint32_t)(bribe - P.energy_credits) + 49) / 50) * 50;
    if (bribe < 50) bribe = 0;
    bool wants_bribe = false, offer = false, offered = false, paid = false;
    if ((!needs_help && lenient * 2 + 10 < DiploFriction && R.tech_commerce_bonus <= P.tech_commerce_bonus) ||
        DiploFriction > 15 || (dstatus(r, f) & 0x20) ||
        clamp(R.tech_commerce_bonus - P.tech_commerce_bonus, 0, 99) + 2 < DiploReputation) {
        wants_bribe = true;
    }
    if (DiploCounterProposal == 5 || DiploCounterProposal == 4) offer = true;
    bool bargain;
    if (DiploCounterProposal == 2) {
        offer = true;
        wants_bribe = false;
        bargain = true;
    } else {
        if (DiploCounterProposal == 3) wants_bribe = false;
        bargain = wants_bribe || offer;
    }
    if (bargain) {
        if (DiploTradeWantTech1 >= 0 && std::strcmp(filename, "BELIEVE") &&
            (std::strcmp(filename, "MORGAN") || !bribe || P.energy_credits < bribe ||
             bribe < tech_val(DiploTradeWantTech1, f, 1) * 100) &&
            (DiploCounterProposal == 4 || DiploCounterProposal == 2 ||
             (DiploCounterProposal == 1 && DiploFriction < 10))) {
            // faction_id_with asks for a technology.
            offered = true;
            int32_t choice;
            for (;;) {
                GenderDefault = game().mfactions[r].is_leader_female;
                PluralDefault = 0;
                parse_says(1, game().mfactions[r].title_leader, -1, -1);
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
                parse_says(2, StrBuffer, -1, -1);
                parse_says(3, get_noun_phrase(r, 9), -1, -1);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "GIMMETECH");
                label_with_number(StrBuffer, game_random(0, 2));
                choice = X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                if (choice < 2) break;
                help_topic(0xE, DiploTradeWantTech1);
            }
            if (!choice) {
                accept = false;
            } else {
                paid = true;
                net_tech(r, DiploTradeWantTech1, f, 1);
                lower_friction(f, r, tech_val(DiploTradeWantTech1, f, 1) / 2);
                diplomacy_caption(f, r);
                DiploTradeWantTech1 = DiploTradeWantTech2;
                DiploTradeWantTech2 = -1;
            }
        }
        if ((wants_bribe || offer) && bribe && bribe <= game().factions[f].energy_credits && !offered &&
            std::strcmp(filename, "PEACE") &&
            (DiploCounterProposal == 5 || DiploCounterProposal == 2 ||
             (DiploCounterProposal == 1 && DiploFriction < 10))) {
            // faction_id_with asks for energy.
            offered = true;
            parse_num(0, bribe);
            PluralDefault = 0;
            GenderDefault = game().mfactions[r].is_leader_female;
            parse_says(1, game().mfactions[r].title_leader, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "GIMMEENERGY");
            label_with_number(StrBuffer, game_random(0, 2));
            if (!X_pops_11(StrBuffer, 0x100000, portrait, kPopWait)) {
                X_pops_11("REJTREATYNOBRIBE", 0x100000, portrait, kPopWait);
                return 1;
            }
            net_energy(f, -bribe, r, bribe, 1);
            paid = true;
            lower_friction(f, r, energy_value(bribe));
            diplomacy_caption(f, r);
            InfoWindowTechReset = 0;
            Console_update_data(&MainConsole, 0);
        }
    }
    if (!accept) {
        if (offered) {
            X_pops_11("REJTREATYNOBRIBE", 0x100000, portrait, kPopWait);
            return 1;
        }
    } else if (!wants_bribe || offered) {
        if ((DiploCounterProposal == 4 || DiploCounterProposal == 5) && paid) DiploHelpFaction = 0;
        if (help && !DiploAcceptedFaction) {
            // faction_id_with asks faction_id to break with the faction it fights.
            GenderDefault = m.is_leader_female;
            PluralDefault = 0;
            parse_says(5, m.title_leader, -1, -1);
            say_noun(6, help);
            if (!X_pops_11("GIMMEHELP", 0x100000, portrait, kPopWait)) {
                if ((needs_help || FactionRankings[7] == f || DiploReputation > 3 || DiploCounterProposal == 1) &&
                    !paid && std::strcmp(filename, "GAIANS")) {
                    X_pops_11("REJTREATYNOHELP", 0x100000, portrait, kPopWait);
                    R.diplo_patience[f]++;
                    return 1;
                }
            } else {
                diplo_lock(0);  // inlined in the original
                net_treaty_off(f, r, 0x800000, 0);
                net_double_cross(f, help, r, 1);
                diplo_unlock();  // inlined in the original
                DiploHelpFaction = 0;
                DiploEnemyAllyCount = 0;
            }
        }
        make_treaty(f, r);
        return 1;
    }
    if (DiploCounterProposal != 1) {
        X_pops_11("REJTREATYBADBRIBE", 0x100000, portrait, kPopWait);
        return 1;
    }
    return 0;
}

int32_t GAMEAPI propose_pact(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    DiploSecondFaction = r;
    bool surrender_ally = false;  // faction_id_with is losing its war against the help faction
    int32_t score = DiploRivalPactCount + DiploPlayerPactCount;
    if (DiploPlayerPactCount) score = DiploRivalPactCount + DiploPlayerPactCount + 1;
    int32_t help = DiploHelpFaction ? DiploHelpFaction : DiploAcceptedFaction;
    if (MultiplayerActive) {
        int32_t second = RankingFactionIDUnk2;
        if (f == RankingFactionIDUnk1 &&
            (game().factions[second].ranking < 6 ||
             (FactionRankingsUnk[f] * 2 - FactionRankingsUnk[second] * 3 != 0 &&
              FactionRankingsUnk[second] * 3 <= FactionRankingsUnk[f] * 2))) {
            score++;
        }
        if (f == second && (P.ranking < 6 || (FactionRankingsUnk[FactionRankings[7]] * 2 - FactionRankingsUnk[f] * 3 != 0 &&
                                               FactionRankingsUnk[f] * 3 <= FactionRankingsUnk[FactionRankings[7]] * 2))) {
            score--;
        }
    }
    if ((GameRules & RULES_INTENSE_RIVALRY) && f == RankingFactionIDUnk1) score++;
    if ((GameRules & RULES_VICTORY_COOPERATIVE) && !(GameRules & RULES_INTENSE_RIVALRY)) score--;
    if (R.player_flags & PFLAG_COOP_WITH_HUMAN) score--;
    int32_t rank = P.ranking;
    if (rank >= 6) {
        if (!DiploRunawayLeader && R.ranking >= (rank != 7) + 5) {
            score++;
            if (CurrentTurn >= (MultiplayerActive ? 50 : 100)) score++;
        } else if (R.ranking >= 4 && CurrentTurn >= 50 && !DiploRunawayLeader) {
            if (DiploRivalStrongPacts)
                score++;
            else if (!DiploPlayerPactCount)
                score--;
        }
    }
    if (DiploRivalStrongPacts) score++;
    if (help) {
        score -= great_satan(help, 0) ? 2 : 1;
        if (R.ranking < game().factions[help].ranking) score--;
        if (dstatus(r, help) & 0x8000) {
            int32_t mil = R.mil_strength_1;
            if (mil < game().factions[help].mil_strength_1) {
                surrender_ally = true;
                score -= (mil / 2 + game().factions[help].mil_strength_1) / (mil + 1);
            }
        }
    }
    if (DiploFriction > 9 && DiploCounterProposal != 3) score += DiploFriction - 10;
    if (P.diplo_reputation > 1 && !DiploReputation) score--;
    if (P.diplo_reputation < 2) score++;
    if (R.tech_commerce_bonus < P.tech_commerce_bonus + 1) score++;
    if (P.tech_commerce_bonus < R.tech_commerce_bonus) score--;
    if (!MultiplayerActive && !std::strcmp(game().mfactions[r].filename, "HIVE")) score--;
    if (DiploFearScale) score--;
    if (DiploPrevHostility) score++;
    if (DiploGreatSatan) score++;
    rank = P.ranking;
    int32_t rank_r = R.ranking;
    if (rank < rank_r) {
        if (rank_r == 7) score += 2;
        if (rank_r == 6) score++;
        if (rank < 5) score--;
    } else {
        if (rank == 7) score += DiploPlayerPactCount;
        if (rank - rank_r > 2) score--;
        if (great_satan(f, 0)) score++;
    }
    if (P.ranking < 6 && R.ranking < 6) score--;
    bool accept =
        !((DiploGoodwill <= score && P.diff_level) || (dagenda(f, r) & 0x2000) ||
          (P.diff_level ? 2 : 4) + ((GameRules >> 12) & 1) + (surrender_ally ? 2 : 0) < DiploReputation ||
          (dstatus(r, f) & 0x20) || DiploCanBetray);
    diplomacy_caption(f, r);
    parse_says(0, get_pact(f), -1, -1);
    say_leader(1, f);
    if (DiploCounterProposal == 3 && DiploCanSurrender) {
        net_set_treaty(r, f, 0x2000000, 1, 0);
        pact_of_brotherhood(f, r);
        return 1;
    }
    if (DiploCounterProposal == 3 || !accept) {
        if ((DiploReputation > 2 || DiploCapturedBases) && (DiploFearScale || DiploFriction < 3) &&
            !(dstatus(r, f) & 0x20)) {
            X_pops_11("REJPACTMEEK", 0x100000, portrait, kPopWait);
            return 1;
        }
        if (DiploCounterProposal == 3) return 0;
        if (!(dstatus(r, f) & 0x20) && DiploReputation < 3 && !DiploCapturedBases) {
            if (great_satan(f, 0) || DiploGreatSatan) {
                parse_says(3, get_phrase(f, 3), -1, -1);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "REJPACTAHEAD");
                label_with_number(StrBuffer, game_random(0, 2));
                X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                return 1;
            }
            parse_says(3, get_noun_phrase(r, 9), -1, -1);
            if (R.ranking <= P.ranking && DiploFriction < 11)
                X_pops_11("REJPACT", 0x100000, portrait, kPopWait);
            else
                X_pops_11("REJPACTPOWER", 0x100000, portrait, kPopWait);
            return 1;
        }
        parse_says(3, get_phrase(f, 5), -1, -1);
        X_pops_11("REJPACTHATE", 0x100000, portrait, kPopWait);
        R.diplo_patience[f]++;
        return 1;
    }
    int32_t commerce_r = R.tech_commerce_bonus;
    int32_t bribe = (P.energy_credits + (P.tech_commerce_bonus - commerce_r + DiploReputation) * 50) *
                    R.mil_strength_1 / (P.mil_strength_1 + 1 + R.mil_strength_1) / 50 * 50;
    if (DiploCounterProposal != 5) {
        while (bribe && bribe > P.energy_credits) {
            bribe -= 50;
            if (bribe < 0) {
                bribe = 0;
                break;
            }
            if (bribe >= 100000) bribe = 99999;
        }
    }
    bool wants_bribe = false, offer = false, paid = false, offered = false;
    if (!surrender_ally && ((DiploFriction > 4 && commerce_r <= P.tech_commerce_bonus) || DiploFriction > 10 ||
                            (DiploReputation && commerce_r - P.tech_commerce_bonus < DiploReputation))) {
        wants_bribe = true;
    }
    if (DiploCounterProposal == 4 || DiploCounterProposal == 5) offer = true;
    if (DiploCounterProposal == 2) {
        if (!surrender_ally && !DiploHelpFaction && (DiploTradeWantTech1 >= 0 || bribe)) offer = true;
        if (DiploTradeWantTech1 < 0 && !bribe) wants_bribe = false;
    }
    if (!great_satan(f, 0) && DiploPlayerPactCount < 2 &&
        (!MultiplayerActive || f != RankingFactionIDUnk1 ||
         (!DiploPlayerPactCount && R.ranking <= game().factions[RankingFactionIDUnk2].ranking))) {
        DiploTradeWantTech2 = -1;
    } else {
        // A leading faction pays double, and both technologies.
        int32_t doubled = bribe * 2 > CurrentTurn * 5 ? bribe * 2 : CurrentTurn * 5;
        bribe = doubled / 50 * 50;
        if (P.energy_credits < bribe ? (DiploCounterProposal == 5 || DiploTradeWantTech2 < 0)
                                     : (DiploTradeWantTech2 < 0 && DiploCounterProposal == 4)) {
            parse_says(3, get_noun_phrase(r, 9), -1, -1);
            X_pops_11("REJPACT", 0x100000, portrait, kPopWait);
            return 1;
        }
    }
    if (wants_bribe || offer) {
        const char* filename = game().mfactions[r].filename;
        if (DiploTradeWantTech1 >= 0 && std::strcmp(filename, "BELIEVE") &&
            (std::strcmp(filename, "MORGAN") || !bribe || P.energy_credits < bribe) &&
            (DiploCounterProposal == 4 || (DiploCounterProposal == 1 && DiploFriction < 4) ||
             DiploCounterProposal == 2)) {
            offered = true;
            int32_t choice;
            for (;;) {
                PluralDefault = 0;
                GenderDefault = game().mfactions[r].is_leader_female;
                parse_says(1, game().mfactions[r].title_leader, -1, -1);
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
                parse_says(2, StrBuffer, -1, -1);
                parse_says(3, get_noun_phrase(r, 9), -1, -1);
                if (DiploTradeWantTech2 >= 0) {
                    // BUG(original): the second technology's name is written and then discarded.
                    COMPAT_ORIGINAL(trade_tech2_name_discarded);
                    StrBuffer[0] = 0;
                    say_tech_2(StrBuffer, DiploTradeWantTech2, 1);
                }
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "GIMMETECH");
                label_with_number(StrBuffer, DiploTradeWantTech2 < 0 ? game_random(0, 2) : 2);
                choice = X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                if (choice < 2) break;
                help_topic(0xE, DiploTradeWantTech1);
            }
            if (choice) {
                paid = true;
                net_tech(r, DiploTradeWantTech1, f, 1);
                if (DiploTradeWantTech2 >= 0) net_tech(r, DiploTradeWantTech2, f, 1);
                lower_friction(f, r, tech_val(DiploTradeWantTech1, r, 1) / 2);
                diplomacy_caption(f, r);
                DiploTradeWantTech2 = -1;
                DiploTradeWantTech1 = -1;
            }
        }
        if (bribe && bribe <= P.energy_credits && !offered && std::strcmp(filename, "PEACE") &&
            (DiploCounterProposal == 5 || (DiploCounterProposal == 1 && DiploFriction < 4) ||
             DiploCounterProposal == 2)) {
            offered = true;
            parse_num(0, bribe);
            GenderDefault = game().mfactions[r].is_leader_female;
            PluralDefault = 0;
            parse_says(1, game().mfactions[r].title_leader, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "GIMMEENERGY");
            label_with_number(StrBuffer, game_random(0, 2));
            if (X_pops_11(StrBuffer, 0x100000, portrait, kPopWait)) {
                net_energy(f, -bribe, r, bribe, 1);
                offer = false;
                paid = true;
                lower_friction(f, r, energy_value(bribe));
                InfoWindowTechReset = 0;
                Console_update_data(&MainConsole, 0);
                diplomacy_caption(f, r);
            }
        }
    }
    if ((wants_bribe || offered) && !paid) {
        if (offered) {
            X_pops_11("REJPACTNOBRIBE", 0x100000, portrait, kPopWait);
            return 1;
        }
        if (DiploCounterProposal == 3) return 0;
        X_pops_11("REJPACTBADBRIBE", 0x100000, portrait, kPopWait);
        return 1;
    }
    if (help && !DiploAcceptedFaction) {
        // faction_id_with asks faction_id to break with the faction it fights.
        GenderDefault = game().mfactions[f].is_leader_female;
        PluralDefault = 0;
        parse_says(5, game().mfactions[f].title_leader, -1, -1);
        say_noun(6, help);
        if (!X_pops_11("GIMMEHELP", 0x100000, portrait, kPopWait)) {
            if (surrender_ally || (FactionRankings[7] == f && DiploAlterFear >= 0) || DiploReputation > 2 ||
                DiploAlterFear > 0) {
                R.diplo_patience[f]++;
                if (!paid) {
                    parse_says(2, get_pacts_2(f, r), -1, -1);
                    X_pops_11("REJPACTNOHELP", 0x100000, portrait, kPopWait);
                    return 1;
                }
            }
        } else {
            diplo_lock(0);  // inlined in the original
            net_double_cross(f, help, r, 0);
            net_treaty_off(f, r, 0x800000, 0);
            net_set_treaty(r, help, DIPLO_UNK_40, 1, 1);
            diplo_unlock();  // inlined in the original
            DiploHelpFaction = 0;
            DiploEnemyAllyCount = 0;
        }
    }
    pact_of_brotherhood(f, r);
    return 1;
}

// ---- threats --------------------------------------------------------------------------------------

void GAMEAPI threaten(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    DiploSecondFaction = r;
    R.diplo_patience[f] = (int8_t)get_patience(f, r);  // inlined in the original
    tech_analysis(f, r);
    if (DiploCanBetray) {
        // faction_id_with was waiting for a pretext.
        P.diplo_spoke[r] = CurrentTurn;
        net_treaty_on(r, f, DIPLO_VENDETTA, 1);
        diplomacy_caption(f, r);
        say_leader(0, f);
        parse_says(2, get_noun_phrase(f, 8), -1, -1);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "PROVOKED");
        label_with_number(StrBuffer, game_random(0, 2));
        X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
        return;
    }
    if ((P.diplo_status[r] & DIPLO_PACT) && !DiploFearScale) {
        // A weaker pact partner may still give in to a leading faction.
        int32_t rank = P.ranking;
        if (rank < 6 && R.ranking > 5 && DiploCurrentProposal != 8) {
            DiploFearScale = R.tech_ranking / 2 - P.tech_ranking / 2 - P.tech_commerce_bonus - R.AI_fight +
                             R.tech_commerce_bonus - rank + R.ranking;
            if (DiploCurrentProposal == 4)
                DiploFearScale += R.AI_tech - R.AI_power;
            else if (DiploCurrentProposal == 6)
                DiploFearScale += R.AI_wealth - R.AI_power;
            if (!(R.diplo_status[f] & DIPLO_HAVE_SURRENDERED) || DiploFearScale > 3)
                DiploFearScale = clamp(DiploFearScale, 0, 999);
            else
                DiploFearScale = 4;
        }
    }
    if ((R.diplo_status[f] & DIPLO_PACT) && (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED) && DiploFearScale < 2)
        DiploFearScale = 2;
    int32_t demand = (CurrentTurn - DiploSpokeTurn) * DiploFearScale * 25 / 16;
    if (R.energy_credits < demand) demand = R.energy_credits / 25 * 25;
    if (demand < 0) demand = 0;
    if (!DiploFearScale) lower_friction(f, r, -2);
    diplomacy_caption(f, r);
    bool done = false;
    if (DiploCurrentProposal == 8) {
        // faction_id demands that faction_id_with declares war on DiploProposalFaction.
        const int32_t target = DiploProposalFaction;
        if (R.diplo_status[target] & DIPLO_VENDETTA) {
            say_leader(0, target);
            X_pops_11("REJWARALREADY", 0x100000, portrait, kPopWait);
            return;
        }
        bool pact_war = false;
        if ((P.diplo_status[r] & DIPLO_PACT) &&
            (((P.diplo_status[target] & DIPLO_VENDETTA) && !(R.diplo_status[target] & DIPLO_PACT)) ||
             (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED))) {
            pact_war = true;
            if (wants_to_attack_fresh(r, target, 0) || wants_to_attack(target, r, 0) ||
                (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED) || (R.ranking < 5 && P.ranking > 5) ||
                ((P.diplo_status[target] & DIPLO_WANT_REVENGE) && P.ranking < game().factions[target].ranking) ||
                (P.ranking < game().factions[target].ranking && R.ranking < game().factions[target].ranking)) {
                say_leader(0, target);
                say_noun(2, target);
                parse_says(3, get_pact(f), -1, -1);
                X_pops_11("WARPACT", 0x100000, portrait, kPopWait);
                net_double_cross(r, target, f, 1);
                done = true;
            }
        }
        if (!pact_war || DiploCurrentProposal == 8) {
            int32_t mil = game().factions[target].mil_strength_1;
            if (DiploFearScale && !(R.diplo_status[target] & DIPLO_PACT) &&
                ((P.diplo_status[r] & DIPLO_TREATY) || !(R.diplo_status[target] & DIPLO_TREATY)) &&
                mil * mil / (R.mil_strength_1 + 1) < P.mil_strength_1 * DiploFearScale) {
                say_leader(0, target);
                say_noun(2, target);
                X_pops_11("WARTHREAT", 0x100000, portrait, kPopWait);
                net_double_cross(r, target, f, 1);
                done = true;
            }
        }
    }
    if (demand && DiploCurrentProposal == 6) {
        say_leader(0, f);
        parse_num(0, demand);
        X_pops_11("EXTORTENERGY", 0x100000, portrait, kPopWait);
        net_energy(f, demand, r, -demand, 1);
        InfoWindowTechReset = 0;
        Console_update_data(&MainConsole, 0);
        done = true;
    }
    if (DiploFearScale && DiploCurrentProposal == 13) {
        parse_says(0, game().mfactions[DiploProposalFaction].adj_name_faction, -1, -1);
        X_pops_11("EXTORTCOMMLINK", 0x100000, portrait, kPopWait);
        net_tech(f, DiploProposalFaction + 89, r, 1);
        done = true;
    }
    if (DiploTradeOfferTech1 >= 0 && DiploFearScale && DiploCurrentProposal == 4) {
        PluralDefault = 0;
        GenderDefault = game().mfactions[f].is_leader_female;
        parse_says(0, game().mfactions[f].title_leader, -1, -1);
        StrBuffer[0] = 0;
        say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
        parse_says(1, StrBuffer, -1, -1);
        X_pops_11("EXTORTTECH", 0x100000, portrait, kPopWait);
        net_tech(f, DiploTradeOfferTech1, r, 1);
        DiploTradeOfferTech1 = -1;
        done = true;
    }
    if (DiploCurrentProposal == 9) {
        // faction_id demands base DiploAskBaseSwapID.
        const int32_t base_id = DiploAskBaseSwapID;
        if (R.diplo_status[f] & DIPLO_PACT) {
            int32_t region = map_tile(game().bases[base_id].x, game().bases[base_id].y)->region;
            int32_t bases = R.base_count;
            if (!(R.diplo_status[f] & DIPLO_HAVE_SURRENDERED) || bases < 2 ||
                ((P.diplo_agenda[r] & 0x1000) && (uint8_t)game().bases[base_id].faction_id_former != (uint32_t)f &&
                 (P.region_total_bases[region] < R.region_total_bases[region] ||
                  bases / 2 < R.region_total_bases[region]))) {
                say_leader(0, f);
                X_pops_11("BASENOCEDE", 0x100000, portrait, kPopWait);
                return;
            }
            net_agenda_on(f, r, 0x1000, 1);
        }
        int32_t value = value_of_base(base_id, r, f, DiploFearScale, (DiploPrevHostility || DiploAttackChance) ? 1 : 0);
        if (value > 0 && value < DiploFearScale * 25) {
            diplo_lock(0);  // inlined in the original
            parse_says(0, game().bases[DiploAskBaseSwapID].name, -1, -1);
            net_cede_base(DiploAskBaseSwapID, f, 1);
            X_pops_11("BASEOK", 0x100000, portrait, kPopWait);
            if (!(R.diplo_status[f] & DIPLO_HAVE_SURRENDERED)) net_set_treaty(r, f, 0x20, 1, 1);
            diplo_unlock();  // inlined in the original
            done = true;
        }
    }
    if (!done) {
        if (R.diplo_status[f] & DIPLO_PACT) {
            if ((DiploPrevHostility || DiploGreatSatan || DiploFriction > 10) &&
                (R.ranking < P.ranking || R.tech_ranking < P.tech_ranking) &&
                !(R.diplo_status[f] & DIPLO_HAVE_SURRENDERED) && P.ranking > 4) {
                // A pact partner does not accept threats.
                say_leader(0, f);
                parse_says(2, get_pact(f), -1, -1);
                X_pops_11("ENDPACT", 0x100000, portrait, kPopWait);
                diplo_lock(0);  // inlined in the original
                net_pact_ends(f, r, 1);
                diplo_unlock();  // inlined in the original
            }
            PluralDefault = 0;
            GenderDefault = game().mfactions[f].is_leader_female;
            parse_says(0, game().mfactions[f].name_leader, -1, -1);
            X_pops_11("FEND", 0x100000, portrait, kPopWait);
            return;
        }
        if ((DiploPrevHostility ? 5 : 15) < DiploFriction && !DiploFearScale &&
            (P.best_weapon_value <= R.best_weapon_value || DiploAttackChance || DiploExposedBaseCount) &&
            (P.diff_level / 2 + 4 + P.integrity_blemishes) * P.mil_strength_1 / 4 < R.mil_strength_1) {
            net_treaty_on(f, r, DIPLO_VENDETTA, 1);
            P.diplo_spoke[r] = CurrentTurn;
            diplomacy_caption(f, r);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "PROVOKED");
            label_with_number(StrBuffer, game_random(0, 2));
            say_leader(0, f);
            parse_says(2, get_noun_phrase(f, 8), -1, -1);
            X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
            return;
        }
        say_leader(0, f);
        X_pops_11("IGNORE", 0x100000, portrait, kPopWait);
        return;
    }
    if (P.diplo_status[r] & DIPLO_TREATY) return;
    net_treaty_on(r, f, DIPLO_TRUCE, 1);
}

// ---- base swaps -----------------------------------------------------------------------------------

int32_t GAMEAPI base_swap(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    DiploSecondFaction = r;
    int32_t ask = value_of_base(DiploAskBaseSwapID, r, f, DiploFearScale, (DiploPrevHostility || DiploAttackChance) ? 1 : 0);
    int32_t bid = value_of_base(DiploBidBaseSwapID, r, f, 0, 0);
    if (DiploAskBaseSwapID >= 0) {
        parse_says(0, game().bases[DiploAskBaseSwapID].name, -1, -1);
        if ((uint8_t)game().bases[DiploAskBaseSwapID].faction_id_former == (uint32_t)f) ask /= 2;
        if ((dstatus(r, f) & DIPLO_PACT) && (dstatus(r, f) & 0x2000000)) ask /= 2;
    }
    if (DiploBidBaseSwapID >= 0) {
        parse_says(1, game().bases[DiploBidBaseSwapID].name, -1, -1);
        if ((uint8_t)game().bases[DiploBidBaseSwapID].faction_id_former == (uint32_t)r) bid *= 2;
        if (DiploBidBaseSwapID >= 0 && (bid < 0 || game().factions[f].base_count == 1)) {
            X_pops_11("NOHQ", 0x100000, portrait, kPopWait);
            return 1;
        }
    }
    if (DiploAskBaseSwapID >= 0) {
        // faction_id_with keeps its last base and its headquarters.
        bool refuse = true;
        if (ask >= 0 && game().factions[r].base_count != 1) {
            int32_t offset, mask;
            bitmask(1, &offset, &mask);
            refuse = (game().bases[DiploAskBaseSwapID].facilities_built[offset] & (uint8_t)mask) != 0;
        }
        if (refuse) {
            X_pops_11("NOMYHQ", 0x100000, portrait, kPopWait);
            return 1;
        }
    }
    if ((dstatus(r, f) & 0x20) && DiploCounterProposal != 3) {
        X_pops_11("NEVERBASESWAP", 0x100000, portrait, kPopWait);
        return 1;
    }
    if (DiploBidBaseSwapID < 0) {
        if (DiploAskBaseSwapID < 0) return 1;
        // faction_id buys the base with energy or technology.
        const Faction& P = game().factions[f];
        const Faction& R = game().factions[r];
        // BUG(original): divides by faction_id_with's ranking, which may be 0.
        COMPAT_ORIGINAL(ranking_divide_zero);
        int32_t price = (P.ranking + R.ranking) * ask / (R.ranking * 2);
        if (FactionRankings[7] == f) price *= 2;
        if (DiploCounterProposal == 5 || DiploCounterProposal == 2) {
            if (price <= game().factions[f].energy_credits) {
                parse_num(0, price);
                if (!X_pops_11("PAYBASESWAP", 0x100000, portrait, kPopWait)) return 1;
                diplo_lock(0);  // inlined in the original
                net_energy(f, -price, r, price, 0);
                net_cede_base(DiploAskBaseSwapID, f, 1);
                diplo_unlock();  // inlined in the original
                X_pops_11("BASEOK", 0x100000, portrait, kPopWait);
                return 0;
            }
            if (DiploCounterProposal == 5) {
                parse_num(0, price);
                X_pops_11("NOBASESWAP", 0x100000, portrait, kPopWait);
                return 1;
            }
        }
        if (DiploCounterProposal != 4 && DiploCounterProposal != 2) {
            parse_num(0, price);
            X_pops_11("NOBASESWAP", 0x100000, portrait, kPopWait);
            return 1;
        }
        int32_t value = 0;
        for (int32_t t = 0; t < 89; t++) {
            if (!has_tech(t, r) && has_tech(t, f)) value += tech_val(t, r, 1);
        }
        if (P.tech_ranking / 2 + 2 < R.tech_ranking / 2 || (CurrentTurn + 7) / 8 * value < price) {
            X_pops_11("NOBASESWAP2", 0x100000, portrait, kPopWait);
            return 1;
        }
        PluralDefault = 0;
        GenderDefault = game().mfactions[r].is_leader_female;
        parse_says(2, game().mfactions[r].name_leader, -1, -1);
        if (X_pops_11("TECHBASESWAP", 0x100000, portrait, kPopWait)) {
            diplo_lock(0);  // inlined in the original
            for (int32_t t = 0; t < 89; t++) {
                if (!has_tech(t, r) && has_tech(t, f)) net_tech(r, t, f, 0);
            }
            net_cede_base(DiploAskBaseSwapID, f, 1);
            diplo_unlock();  // inlined in the original
            X_pops_11("BASEOK", 0x100000, portrait, kPopWait);
            return 0;
        }
        return 1;
    }
    if (DiploAskBaseSwapID < 0) return 1;
    int32_t roll_bid = game_random(0, 100);
    int32_t roll_ask = game_random(0, 100);
    if (roll_bid + bid < roll_ask + ask) {
        diplo_lock(0);  // inlined in the original
        net_cede_base(DiploAskBaseSwapID, f, 1);
        net_cede_base(DiploBidBaseSwapID, r, 1);
        diplo_unlock();  // inlined in the original
        X_pops_11("YESBASESWAP", 0x100000, portrait, kPopWait);
        return 0;
    }
    X_pops_11("NOBASESWAP3", 0x100000, portrait, kPopWait);
    return 1;
}

// ---- gifts ------------------------------------------------------------------------------------------

namespace {
// A gift that counts against faction_id's integrity blemishes (diplo_gifts toward faction_id_with
// grows up to the blemishes, or beyond for a great satan or in multiplayer).
void count_gift(int32_t faction_id, int32_t faction_id_with) {
    int32_t& gifts = game().factions[faction_id].diplo_gifts[faction_id_with];
    if (gifts < game().factions[faction_id].integrity_blemishes || great_satan(faction_id, 0) || MultiplayerActive)
        gifts++;
}
}  // namespace

void GAMEAPI make_gift(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    DiploSecondFaction = r;
    if (DiploCounterProposal == 4 || DiploCounterProposal == 7) {
        // Technology.
        tech_analysis(f, r);
        if (DiploTradeWantTech1 < 0) {
            say_noun(1, r);
            X_pops_11("REJTECHNOTHING", 0x100000, portrait, kPopWait);
            return;
        }
        int32_t value = 0;
        if (DiploCounterProposal == 7) {
            for (int32_t t = 0; t < 89; t++) {
                if (has_tech(t, f) && !has_tech(t, r)) {
                    value += tech_val(t, r, 1);
                    net_tech(r, t, f, 1);
                }
            }
        } else {
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
            parse_says(0, StrBuffer, -1, -1);
            parse_says(2, get_noun_phrase(r, 9), -1, -1);
            int32_t choice;
            if (DiploTradeWantTech2 < 0) {
                choice = X_pops_11("OFFERTECH", 0x100000, portrait, kPopWait);
            } else {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech2, 1);
                parse_says(1, StrBuffer, -1, -1);
                choice = X_pops_11("OFFERTECH2", 0x100000, portrait, kPopWait);
            }
            if (!choice) return;
            int32_t tech_id = choice == 1 ? DiploTradeWantTech1 : DiploTradeWantTech2;
            value = tech_val(tech_id, r, 1);
            net_tech(r, tech_id, f, 1);
        }
        const char* key = game().mfactions[r].search_key;
        if (!std::strcmp(key, "UNIV") || !std::strcmp(key, "SPARTANS")) value *= 2;
        if (!std::strcmp(key, "BELIEVE")) value /= 2;
        lower_friction(f, r, value);
        char label[80];
        label[0] = 0;
        crt_strcat(label, "GAVETECH");
        if (value >= 16) {
            crt_strcat(label, ((dstatus(r, f) & 0x60) && DiploFriction) ? "2" : "1");
            net_treaty_off(r, f, 0x60, 1);
            int32_t& gifts = game().factions[f].diplo_gifts[r];
            if (gifts < game().factions[f].integrity_blemishes) gifts++;
            count_gift(f, r);
        } else if (!game().factions[f].diplo_gifts[r]) {
            count_gift(f, r);
        }
        diplomacy_caption(f, r);
        X_pops_11(label, 0x100000, portrait, kPopWait);
        game().factions[r].diplo_patience[f] -= 2;
        return;
    }
    if (DiploCounterProposal == 5) {
        // Energy.
        // OFFERENERGY: the credits rounded down to 25, halved down to 0 (the item value is the amount).
        ui_diplo_menu_start("OFFERENERGY", 0x100000);
        int32_t amount = game().factions[f].energy_credits * 2;
        do {
            amount = amount / 2 / 25 * 25;
            char num[192];
            StrBuffer[0] = 0;
            crt_itoa(amount, num, 10);
            crt_strcat(StrBuffer, num);
            ui_diplo_menu_item(StrBuffer, amount);
        } while (amount);
        amount = ui_diplo_menu_exec(r);
        ui_diplo_menu_close();
        if (!amount) return;
        const char* key = game().mfactions[f].search_key;
        int32_t value = amount;
        if (!std::strcmp(key, "MORGAN") || !std::strcmp(key, "SPARTANS")) value = amount * 2;
        if (!std::strcmp(key, "SPARTANS")) value /= 2;
        const Faction& P = game().factions[f];
        const Faction& R = game().factions[r];
        bool large = value >= R.energy_credits / 2 && value >= P.energy_credits / 3 && value >= CurrentTurn * 2;
        bool fair = value >= R.energy_credits / 4 && value >= 25 && value >= P.energy_credits / 6 && value >= CurrentTurn;
        net_energy(f, -amount, r, amount, 1);
        InfoWindowTechReset = 0;
        Console_update_data(&MainConsole, 0);
        lower_friction(f, r, energy_value(value) * 3 / 2);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "GAVEENERGY");
        if (large) {
            crt_strcat(StrBuffer, (!(dstatus(r, f) & 0x60) && !great_satan(f, 0)) ? "1" : "2");
            net_treaty_off(f, r, 0x60, 1);
            int32_t& gifts = game().factions[f].diplo_gifts[r];
            if (gifts < game().factions[f].integrity_blemishes) gifts++;
            count_gift(f, r);
        } else if (fair && !game().factions[f].diplo_gifts[r]) {
            count_gift(f, r);
        }
        diplomacy_caption(f, r);
        say_leader(0, f);
        X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
        game().factions[r].diplo_patience[f] += -2 - (large ? 1 : 0);
        return;
    }
    if (DiploCounterProposal != 8) return;
    // A base (DiploBidBaseSwapID).
    int32_t value = value_of_base(DiploBidBaseSwapID, r, f, DiploFearScale, (DiploPrevHostility || DiploAttackChance) ? 1 : 0);
    lower_friction(f, r, value * 3 / 2);
    if (!std::strcmp(game().mfactions[f].search_key, "SPARTANS")) value *= 2;
    const Faction& P = game().factions[f];
    const Faction& R = game().factions[r];
    bool large = value >= R.energy_credits / 2 && value >= P.energy_credits / 3 && value >= CurrentTurn * 2;
    if (!std::strcmp(game().mfactions[f].filename, "BELIEVE")) large = true;
    diplo_lock(0);  // inlined in the original
    diplomacy_caption(f, r);
    // BUG(original): the label with the suffix is built in a local buffer that is never used; the
    // popup shows plain BASEGIFT.
    COMPAT_ORIGINAL(basegift_label_unused);
    char label[80];
    label[0] = 0;
    crt_strcat(label, "BASEGIFT");
    if (large) {
        crt_strcat(label, (!(dstatus(r, f) & 0x60) && !great_satan(f, 0)) ? "1" : "2");
        net_treaty_off(f, r, 0x60, 0);
        int32_t& gifts = game().factions[f].diplo_gifts[r];
        if (gifts < game().factions[f].integrity_blemishes) gifts++;
    }
    count_gift(f, r);
    PluralDefault = 0;
    GenderDefault = game().mfactions[f].is_leader_female;
    parse_says(0, game().mfactions[f].title_leader, -1, -1);
    net_cede_base(DiploBidBaseSwapID, r, 1);
    diplo_unlock();  // inlined in the original
    game().factions[r].diplo_patience[f] += -2 - (large ? 1 : 0);
    X_pops_11("BASEGIFT", 0x100000, portrait, kPopWait);
}

// ---- the human asks faction_id_with to end a vendetta -------------------------------------------

void GAMEAPI call_off_vendetta(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    DiploSecondFaction = r;
    // The CALLOFF menu offers the factions faction_id_with fights that faction_id has an agreement
    // with (an ATTACKSTRING line each, the faction as the item value).
    ui_diplo_menu_start("CALLOFF", 0x100042);
    int32_t count = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (alive(i) && i != f && i != r && (dstatus(r, i) & DIPLO_VENDETTA) && (dstatus(f, i) & DIPLO_COMMLINK) &&
            (dstatus(f, i) & 7)) {
            if (X_text_open(ScriptFileName, "ATTACKSTRING")) break;
            text_get();
            say_leader(0, i);
            say_noun(2, i);
            StrBuffer[0] = 0;
            parse_string(TextBufferGetPtr, StrBuffer);
            count++;
            ui_diplo_menu_item(StrBuffer, i);
        }
    }
    if (!count) {
        ui_diplo_menu_close();
        return;
    }
    int32_t target = ui_diplo_menu_exec(r);
    ui_diplo_menu_close();
    if (target < 0) return;
    say_leader(0, target);
    parse_says(2, game().mfactions[target].adj_insult_faction, -1, -1);
    say_noun(3, target);
    uint32_t s = dstatus(r, target);
    if ((s & 0x20) || (game().factions[target].major_atrocities && !game().factions[r].major_atrocities)) {
        X_pops_11("TOTHEDEATH", 0x100000, portrait, 0);
        return;
    }
    if ((s & DIPLO_UNK_40) && !(dstatus(r, f) & 0x80)) {
        X_pops_11("NOPEHIRED", 0x100000, portrait, 0);
        return;
    }
    net_set_treaty(r, target, 0x4000000, 1, 0);
    net_set_treaty(r, target, DIPLO_UNK_40, 0, 0);
    if (wants_to_attack_fresh(target, r, 0)) {
        X_pops_11("THEIRFAULT", 0x100000, portrait, 0);
        return;
    }
    X_pops_11("CALLEDOFF", 0x100000, portrait, 0);
    if (MultiplayerActive) {
        log_say_values("Sending TREATY_ON", r, target, DIPLO_TREATY);
        message_data(0x2441, 0, r, target, DIPLO_TREATY, 0);
        NetDaemon_await_diplo(&NetState, 0x441);
    } else {
        enemies_treaty(r, target, 0);
    }
}

// ---- the human asks faction_id_with to attack DiploProposalFaction --------------------------------

int32_t GAMEAPI propose_attack(int32_t faction_id, int32_t faction_id_with) {
    const int32_t f = faction_id, r = faction_id_with;
    const int32_t target = DiploProposalFaction;
    const char* portrait = FactionPortraits[r];
    Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    Faction& T = game().factions[target];
    DiploSecondFaction = r;
    int32_t distant = 0;  // 1: no common region, but bases in a common region; 2: none at all
    if (R.diplo_status[target] & DIPLO_VENDETTA) {
        say_leader(0, target);
        X_pops_11("REJWARALREADY", 0x100000, portrait, kPopWait);
        return 1;
    }
    if (!(R.diplo_status[target] & DIPLO_COMMLINK) && !contiguous(r, target, 0, 1)) {
        if (!(R.diplo_status[f] & DIPLO_PACT)) {
            PluralDefault = 0;
            GenderDefault = game().mfactions[f].is_leader_female;
            parse_says(0, game().mfactions[f].title_leader, -1, -1);
            parse_says(2, game().mfactions[target].adj_name_faction, -1, -1);
            X_pops_11("REJWARFAR", 0x100000, portrait, kPopWait);
            return 1;
        }
        distant = 2;
        for (int32_t region = 1; region < 128; region++) {
            if ((region & 0x3F) != 0x3F && (region & 0x3F) && R.region_total_bases[region] &&
                T.region_total_bases[region]) {
                distant = 1;
                break;
            }
        }
    }
    // The price of the war.
    int32_t price = ((DiploReputation - T.integrity_blemishes) * (CurrentTurn / 10) + T.mil_strength_1 * 5) *
                    (2 - R.AI_fight);
    if (T.ranking > R.ranking && T.ranking > P.ranking && !DiploRivalStrongPacts && (P.ranking >= 4 || R.ranking >= 4))
        price /= 2;
    if (FactionRankings[7] == target) {
        int32_t second = FactionRankingsUnk[FactionRankings[6]];
        if (FactionRankingsUnk[FactionRankings[7]] >= second * 2)
            price /= 4;
        else if (great_satan(target, 0))
            price /= 3;
        else if (FactionRankingsUnk[FactionRankings[7]] * 2 >= second * 3)
            price /= 2;
    }
    if (R.player_flags & PFLAG_COOP_WITH_HUMAN) price /= 2;
    uint32_t pact_target = R.diplo_status[target] & DIPLO_PACT;
    if (pact_target) {
        price *= 3 - R.AI_fight;
        if (P.ranking >= 6 && T.ranking >= 5 && !DiploRunawayLeader && R.ranking <= 4 && CurrentTurn >= 50 &&
            (DiploPlayerPactCount || P.integrity_blemishes > 2)) {
            price *= 2;
        }
    } else if (R.diplo_status[f] & DIPLO_PACT) {
        price /= 2;
    }
    if (T.integrity_blemishes > P.integrity_blemishes) {
        price /= 2;
        if ((P.diplo_status[target] & DIPLO_WANT_REVENGE) && !(R.diplo_status[f] & DIPLO_WANT_REVENGE) &&
            !(T.diplo_status[f] & DIPLO_WANT_REVENGE))
            price /= 2;
    }
    if (DiploGreatSatan) price *= 2;
    if ((R.diplo_status[f] & DIPLO_PACT) && (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED)) price /= 2;
    uint32_t war_p = P.diplo_status[target] & DIPLO_VENDETTA;
    if (!war_p && !human(target)) price += price / 2;
    if (T.ranking >= 6 && R.ranking >= 6) price /= 2;
    if (T.diplo_gifts[r] > T.diplo_wrongs[r]) price = (T.diplo_gifts[r] - T.diplo_wrongs[r] + 1) * price;
    price = ((4 - distant) * price / 4 + 24) / 25 * 25;
    bool declare = false;
    if ((P.diplo_status[r] & DIPLO_PACT) && (DiploCounterProposal == 1 || DiploCounterProposal == 3) && war_p &&
        !pact_target) {
        // A pact partner joins faction_id's war at once.
        if (wants_to_attack_fresh(r, target, 0) || wants_to_attack(target, r, 0) ||
            ((P.diplo_status[target] & DIPLO_WANT_REVENGE) && T.ranking > P.ranking) ||
            (T.ranking > P.ranking && T.ranking > R.ranking)) {
            say_leader(0, target);
            say_noun(2, target);
            parse_says(3, get_pact(f), -1, -1);
            X_pops_11("WARPACT", 0x100000, portrait, kPopWait);
            net_double_cross(r, target, f, 1);
            return 1;
        }
    }
    uint32_t s = R.diplo_status[f];
    bool bribe;
    if ((s & DIPLO_PACT) && (s & 0x2000000)) {
        // A surrendered faction obeys.
        bribe = !(DiploCounterProposal == 3 || DiploCounterProposal == 1);
        declare = !bribe;
    } else if (!(s & DIPLO_PACT) && ((s & 0x20) || DiploReputation > 5)) {
        if (DiploCounterProposal == 3) return 0;
        parse_says(0, game().mfactions[f].adj_name_faction, -1, -1);
        say_leader(1, f);
        say_noun(3, target);
        X_pops_11("REJWARTRUST", 0x100000, portrait, kPopWait);
        return 1;
    } else if (DiploCounterProposal == 1 || DiploCounterProposal == 3) {
        bribe = wants_to_attack_fresh(r, target, 0) ||
                (((s & DIPLO_PACT) || DiploFearScale) && wants_to_attack(target, r, 0) &&
                 (P.diplo_status[target] & DIPLO_VENDETTA));
        if (!bribe) {
            if (DiploCounterProposal == 3) return 0;
            PluralDefault = 0;
            GenderDefault = game().mfactions[f].is_leader_female;
            parse_says(0, game().mfactions[f].title_leader, -1, -1);
            X_pops_11("REJWARNOBRIBE", 0x100000, portrait, kPopWait);
            return 1;
        }
    } else {
        bribe = true;
    }
    if (bribe) {
        // faction_id_with asks for technology or energy.
        bool paid = false, offered = false;
        int32_t turns = (CurrentTurn + 7) / 8;
        int32_t v1 = DiploTradeWantTech1 >= 0 ? tech_val(DiploTradeWantTech1, r, 1) * turns : 0;
        int32_t v2 = v1;
        if (DiploTradeWantTech2 >= 0) v2 += tech_val(DiploTradeWantTech2, r, 1) * turns;
        int32_t v3 = v2;
        if (DiploTradeWantTech3 >= 0) v3 += tech_val(DiploTradeWantTech3, r, 1) * turns;
        int32_t v4 = v3;
        if (DiploTradeWantTech4 >= 0) v4 += tech_val(DiploTradeWantTech4, r, 1) * turns;
        const char* filename = game().mfactions[r].filename;
        if (!std::strcmp(filename, "UNIV")) {
            v4 *= 2;
            v1 *= 2;
            v2 *= 2;
            v3 *= 2;
        } else if (!std::strcmp(filename, "PEACE")) {
            // BUG(original): the partial sums all take 1.5 times the scaled total.
            COMPAT_ORIGINAL(peace_partial_sums);
            v4 = v4 * 3 / 2;
            v1 = v2 = v3 = v4 * 3 / 2;
        } else if (!std::strcmp(filename, "MORGAN")) {
            v4 /= 2;
            v1 /= 2;
            v2 /= 2;
            v3 /= 2;
        }
        // Only as many technologies as needed (to 4/3 of the price).
        int32_t enough = price * 4 / 3;
        if (v3 >= enough) DiploTradeWantTech4 = -1;
        if (v2 >= enough) DiploTradeWantTech3 = -1;
        if (v1 >= enough) DiploTradeWantTech2 = -1;
        if (DiploTradeWantTech1 >= 0 && std::strcmp(filename, "BELIEVE") && v4 >= price &&
            (DiploCounterProposal == 4 ||
             (DiploCounterProposal == 2 && (R.energy_credits > P.energy_credits || v4 >= price * 3 / 2)))) {
            say_noun(0, target);
            say_leader(1, target);
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
            parse_says(3, StrBuffer, -1, -1);
            if (DiploTradeWantTech2 >= 0) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech2, 1);
                parse_says(4, StrBuffer, -1, -1);
            }
            if (DiploTradeWantTech3 >= 0) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech3, 1);
                parse_says(8, StrBuffer, -1, -1);
            }
            if (DiploTradeWantTech4 >= 0) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech4, 1);
                parse_says(9, StrBuffer, -1, -1);
            }
            PluralDefault = 0;
            GenderDefault = game().mfactions[r].is_leader_female;
            parse_says(5, game().mfactions[r].title_leader, -1, -1);
            parse_says(6, game().mfactions[target].adj_name_faction, -1, -1);
            offered = true;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "MAYBEWARTECH");
            if (R.diplo_status[target] & DIPLO_PACT)
                crt_strcat(StrBuffer, "PACT");
            else if (distant)
                crt_strcat(StrBuffer, "FAR");
            if (DiploTradeWantTech4 >= 0)
                crt_strcat(StrBuffer, "4");
            else if (DiploTradeWantTech3 >= 0)
                crt_strcat(StrBuffer, "3");
            else if (DiploTradeWantTech2 >= 0)
                crt_strcat(StrBuffer, "2");
            if (X_pops_11(StrBuffer, 0x100000, portrait, kPopWait)) {
                paid = true;
                if (DiploTradeWantTech4 >= 0) net_tech(r, DiploTradeWantTech4, f, 1);
                if (DiploTradeWantTech3 >= 0) net_tech(r, DiploTradeWantTech3, f, 1);
                if (DiploTradeWantTech2 >= 0) net_tech(r, DiploTradeWantTech2, f, 1);
                net_tech(r, DiploTradeWantTech1, f, 1);
                DiploTradeWantTech4 = DiploTradeWantTech3 = DiploTradeWantTech2 = DiploTradeWantTech1 = -1;
            }
        } else if (std::strcmp(filename, "PEACE") && (DiploCounterProposal == 5 || DiploCounterProposal == 2)) {
            say_noun(0, target);
            parse_says(1, get_pact(target), -1, -1);
            parse_says(2, game().mfactions[target].adj_insult_faction, -1, -1);
            parse_num(0, price);
            parse_says(6, game().mfactions[target].adj_name_faction, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "MAYBEWARENERGY");
            if (R.diplo_status[target] & DIPLO_PACT)
                crt_strcat(StrBuffer, "PACT");
            else if (distant)
                crt_strcat(StrBuffer, "FAR");
            offered = true;
            ui_diplo_menu_start(StrBuffer, 0x100000);
            if (P.energy_credits >= price) ui_diplo_menu_start("MAYBEWARPRICE", 0x100080);
            const int32_t pay = ui_diplo_menu_exec(r);
            ui_diplo_menu_close();
            if (pay > 0) {
                net_energy(f, -price, r, price, 1);
                paid = true;
            }
        }
        InfoWindowTechReset = 0;
        Console_update_data(&MainConsole, 0);
        if (!paid) {
            if (!offered) {
                say_leader(0, target);
                parse_says(2, get_him_her(target, 0), -1, -1);
                X_pops_11("REJWAR", 0x100000, portrait, kPopWait);
            }
            return 1;
        }
        declare = true;
    }
    if (!declare) return 1;
    // faction_id_with declares war on the target.
    diplo_lock(0);  // inlined in the original
    net_treaty_on(r, target, 0x80000018, 0);
    net_set_treaty(r, target, DIPLO_UNK_40, 1, 0);
    net_set_treaty(r, f, 0x80, 1, 0);
    say_leader(0, target);
    say_noun(2, target);
    parse_says(3, game().mfactions[target].adj_insult_faction, -1, -1);
    parse_says(4, get_phrase(target, 5), -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "INCITED");
    label_with_number(StrBuffer, game_random(0, 2));
    X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
    parse_says(3, get_pacts(target), -1, -1);
    parse_says(4, game().mfactions[target].adj_name_faction, -1, -1);
    uint32_t mine = P.diplo_status[target];
    if (mine & DIPLO_PACT) {
        // The target learns who incited the war.
        if (P.diff_level > 1 && !game_random(0, 2)) {
            if (!MultiplayerActive) lower_friction(f, target, -4);
            X_pop("INCITEDPACT", kPopWait);
            net_pact_ends(f, target, 1);
        }
    } else if (mine & DIPLO_TREATY) {
        if (P.diff_level > 1 && !game_random(0, 2)) {
            if (!MultiplayerActive) lower_friction(f, target, -4);
            X_pop("INCITEDTREATY", kPopWait);
            net_treaty_on(f, target, DIPLO_VENDETTA, 0);
            net_set_treaty(target, f, 0x20, 1, 1);
        }
    }
    diplo_unlock();  // inlined in the original
    return 1;
}

// ---- council votes bought in the dialog -------------------------------------------------------------

void GAMEAPI buy_council_vote(int32_t faction_id, int32_t faction_id_with, int32_t proposal, int32_t candidate) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    DiploSecondFaction = r;
    diplomacy_check(f, r, 0);
    diplomacy_caption(f, r);
    tech_analysis(f, r);
    if (ExpansionEnabled && ((game().mfactions[f].rule_flags & 0x80) || (game().mfactions[r].rule_flags & 0x80)))
        return;
    if (R.diplo_status[f] & (DIPLO_VENDETTA | DIPLO_WANT_REVENGE | DIPLO_ATROCITY_VICTIM)) {
        if (ControlWaitLoop) {
            say_leader(0, r);
            X_pop("BUYNOTALK", kPopWait);
        }
        return;
    }
    int32_t want = wants_prop(r, proposal, candidate > 0 ? candidate : f);
    if (proposal != 1 && DiploFearScale) want++;
    if (candidate == -2) want = -want;
    if (proposal == 0) {
        // An election: how much faction_id_with prefers another candidate.
        int32_t least = want;
        for (int32_t i = 1; i < 8; i++) {
            if (i == candidate || !eligible(i)) continue;
            if (least >= want - wants_prop(r, 0, i)) least = want - wants_prop(r, 0, i);
        }
        want = least;
    }
    int32_t promised = R.unk_29[proposal];  // the vote faction_id_with promised for this proposal
    if (promised) {
        int32_t to = R.unk_30[proposal];
        if (!(R.diplo_status[to] & (DIPLO_VENDETTA | DIPLO_WANT_REVENGE))) {
            say_leader(0, to);
            X_pops_11("BUYPROMISED", 0x100000, portrait, kPopWait);
            return;
        }
    }
    if (R.SE_economy_base > 0 && proposal > 1 && !(R.diplo_status[f] & DIPLO_WANT_REVENGE) && want < -2) want = -2;
    if (ControlWaitLoop && candidate == CouncilVoteFor[r]) {
        say_leader(0, f);
        X_pop("BUYALREADY", kPopWait);
        if (MultiplayerActive && (P.diplo_status[r] & DIPLO_TREATY) && !(R.diplo_status[f] & DIPLO_WANT_REVENGE)) {
            R.unk_29[proposal] = candidate;
            R.unk_30[proposal] = proposal ? f : candidate;
            synch_leader(r);
        }
        return;
    }
    int32_t level;  // how much the vote costs
    uint32_t s = R.diplo_status[f];
    bool count_rivals = false;
    if ((s & DIPLO_PACT) && (s & 0x2000000)) {
        level = 0;
        count_rivals = proposal == 0;
    } else if (proposal == 0 && candidate == r) {
        if (want < 0) X_pops_11("BUYNO", 0x100000, portrait, kPopWait);
        return;
    } else if (want < -3) {
        X_pops_11("BUYNO", 0x100000, portrait, kPopWait);
        return;
    } else if (want == -3) {
        if (proposal) {
            candidate = -3;  // only an abstention
            level = 3;
        } else {
            level = 4;
            count_rivals = true;
        }
    } else {
        level = want == -2 ? 3 : (want == -1) + 1;
        count_rivals = proposal == 0;
    }
    int32_t rivals = 0;  // factions with more votes than faction_id_with
    if (count_rivals) {
        for (int32_t i = 1; i < 8; i++) {
            if (i != r && council_votes(i) > council_votes(r)) rivals++;
        }
    }
    int32_t income_p = P.energy_surplus_total - P.facility_maint_total;
    if (income_p < P.base_count) income_p = P.base_count;
    int32_t income_r = R.energy_surplus_total - R.facility_maint_total;
    if (income_r < R.base_count) income_r = R.base_count;
    int32_t price = P.energy_credits / 5 + (income_r + income_p) * level * 2;
    if (DiploFearScale) price /= 2;
    if (DiploFearScale >= R.base_count) price /= 2;
    if (r == GovernorFaction) price *= 2;
    if (!ControlWaitLoop) price = price * 3 / 4;
    if (proposal == 0 && !rivals && CouncilVoteFor[r] == r) price *= 2;
    if (DiploTradeWantTech1 >= 0) {
        // As many technologies as worth 3/4 of the price.
        int32_t need = price * 3 / 4;
        int32_t turns = (CurrentTurn + 7) / 8;
        int32_t value = tech_val(DiploTradeWantTech1, r, 1) * turns;
        if (value < need && DiploTradeWantTech2 >= 0) {
            value += tech_val(DiploTradeWantTech2, r, 1) * ((CurrentTurn + 7) / 8);
            if (value < need && DiploTradeWantTech3 >= 0) {
                value += tech_val(DiploTradeWantTech3, r, 1) * ((CurrentTurn + 7) / 8);
                if (value < need && DiploTradeWantTech4 >= 0)
                    value += tech_val(DiploTradeWantTech4, r, 1) * ((CurrentTurn + 7) / 8);
                else
                    DiploTradeWantTech4 = -1;
            } else {
                DiploTradeWantTech4 = DiploTradeWantTech3 = -1;
            }
        } else {
            DiploTradeWantTech4 = DiploTradeWantTech3 = DiploTradeWantTech2 = -1;
        }
        if (value < need) DiploTradeWantTech4 = DiploTradeWantTech3 = DiploTradeWantTech2 = DiploTradeWantTech1 = -1;
    }
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "BUYVOTE");
    if (!price) {
        label_with_number(StrBuffer, 0);
        say_leader(0, f);
        X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
    } else {
        if (!proposal)
            crt_strcat(StrBuffer, "GOV");
        else if (candidate == -3)
            crt_strcat(StrBuffer, "ABSTAIN");
        else if (candidate == -2)
            crt_strcat(StrBuffer, "NAY");
        else if (candidate == -1)
            crt_strcat(StrBuffer, "YEA");
        say_leader(2, f);
        // BUG(original): with a YEA/NAY/ABSTAIN vote (candidate < 0) the candidate's leader is
        // read from before MFactions (memory image).
        COMPAT_ORIGINAL(out_of_range_reads);
        const MFaction& c = *reinterpret_cast<const MFaction*>(mem::host(0x946A50 + (uint32_t)candidate * 0x59C));
        PluralDefault = 0;
        GenderDefault = c.is_leader_female;
        parse_says(6, c.title_leader, -1, -1);
        GenderDefault = c.is_leader_female;
        PluralDefault = 0;
        parse_says(8, c.name_leader, -1, -1);
        parse_num(0, price);
        if (DiploTradeWantTech1 >= 0) {
            parse_say(0, Tech[DiploTradeWantTech1].name, -1, -1);
            if (DiploTradeWantTech2 < 0) {
                crt_strcat(StrBuffer, "1");
            } else {
                parse_say(1, Tech[DiploTradeWantTech2].name, -1, -1);
                if (DiploTradeWantTech3 < 0) {
                    crt_strcat(StrBuffer, "2");
                } else {
                    parse_say(4, Tech[DiploTradeWantTech3].name, -1, -1);
                    if (DiploTradeWantTech4 < 0) {
                        crt_strcat(StrBuffer, "3");
                    } else {
                        parse_say(5, Tech[DiploTradeWantTech4].name, -1, -1);
                        crt_strcat(StrBuffer, "4");
                    }
                }
            }
        }
        if (!proposal && candidate > 0 && candidate != f) crt_strcat(StrBuffer, "A");
        // BUYVOTEMENU's items: 0 refuse, 1 pay the price (when affordable), 2-5 one to four of
        // DiploTradeWantTech1-4, whichever are set (the item value is the choice).
        ui_diplo_menu_start(StrBuffer, 0x100000);
        if (X_text_open(ScriptFileName, "BUYVOTEMENU")) {
            ui_diplo_menu_close();
            return;
        }
        const int32_t wants[4] = {DiploTradeWantTech1, DiploTradeWantTech2, DiploTradeWantTech3, DiploTradeWantTech4};
        for (int32_t item = 0; item < 6; item++) {
            text_get();
            StrBuffer[0] = 0;
            parse_string(TextBufferGetPtr, StrBuffer);
            bool shown;
            if (item == 0) {
                shown = true;
            } else if (item == 1) {
                shown = game().factions[f].energy_credits >= price;
            } else {
                // Item n offers n - 1 technologies: the first n - 1 are set and the next is not.
                shown = true;
                for (int32_t k = 0; k < item - 1; k++) shown = shown && wants[k] >= 0;
                if (item < 5) shown = shown && wants[item - 1] < 0;
            }
            if (shown) ui_diplo_menu_item(StrBuffer, item);
        }
        int32_t choice = ui_diplo_menu_exec(r);
        ui_diplo_menu_close();
        if (choice < 1) return;
        if (choice == 1) {
            net_energy(f, -price, r, price, 1);
        } else {
            net_tech(r, DiploTradeWantTech1, f, 1);
            if (choice >= 3) net_tech(r, DiploTradeWantTech2, f, 1);
            if (choice >= 4) net_tech(r, DiploTradeWantTech3, f, 1);
            if (choice >= 5) net_tech(r, DiploTradeWantTech4, f, 1);
        }
        DiploTradeWantTech4 = DiploTradeWantTech3 = DiploTradeWantTech2 = DiploTradeWantTech1 = -1;
    }
    R.unk_29[proposal] = candidate;
    R.unk_30[proposal] = proposal ? f : candidate;
    if (MultiplayerActive) {
        synch_leader(r);
        if (ControlWaitLoop) message_data(0x2601, 0, r, candidate, 0, 0);
    } else if (ControlWaitLoop) {
        CouncilWin_vote(&CouncilWin, r, candidate);
    }
}

// ---- technology trades ----------------------------------------------------------------------------

namespace {
// Bases of `owner` that faction_id_seer knows of.
int32_t known_bases(int32_t owner, int32_t seer) {
    int32_t n = 0;
    for (int32_t i = 0; i < game().base_count; i++) {
        uint32_t o = (uint8_t)game().bases[i].faction_id;
        if (o == (uint32_t)owner && (o == (uint32_t)seer || (game().bases[i].visibility & (1 << seer)))) n++;
    }
    return n;
}
// The popup label with a random variant (none in commlink mode 13).
void label_variant(const char* label, int32_t mode, int32_t variants) {
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label);
    label_with_number(StrBuffer, mode == 13 ? 0 : game_random(0, variants));
}
}  // namespace

int32_t GAMEAPI tech_trade(int32_t faction_id, int32_t faction_id_with, int32_t counter, int32_t offer, int32_t mode) {
    const int32_t f = faction_id, r = faction_id_with;
    const char* portrait = FactionPortraits[r];
    Faction& P = game().factions[f];
    Faction& R = game().factions[r];
    bool eager = offer == 4 || offer == 2;
    DiploSecondFaction = r;
    DiploTradeDeclined = 0;
    for (;;) {
        tech_analysis(f, r);
        if (mode == 13) DiploTradeOfferTech1 = DiploProposalFaction + 89;
        diplomacy_caption(f, r);
        if (!(P.diplo_status[r] & DIPLO_PACT) &&
            ((R.diplo_status[f] & 0x8020) || DiploCapturedBases || DiploFriction >= 18 ||
             (DiploHostility && std::strcmp(game().mfactions[r].filename, "UNIV")))) {
            if (counter) return 0;
            say_leader(0, f);
            GenderDefault = game().mfactions[f].is_leader_female;
            PluralDefault = 0;
            parse_says(2, game().mfactions[f].insult_leader, -1, -1);
            parse_says(3, game().mfactions[f].adj_insult_faction, -1, -1);
            parse_says(4, get_phrase(f, 3), -1, -1);
            label_variant("REJTECHHATE", mode, 3);
            X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
            return 0;
        }
        bool slave = (R.diplo_status[f] & DIPLO_PACT) && (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED);
        if (!slave) {
            bool reactor = false;  // faction_id_with has a reactor technology faction_id lacks
            for (int32_t i = 0; i < 4; i++) {
                if (has_tech(Reactor[i].preq_tech, r) && !has_tech(Reactor[i].preq_tech, f)) reactor = true;
            }
            bool fear = false;
            if (great_satan(f, 0) && !(P.diplo_status[r] & DIPLO_PACT) && !(R.player_flags & PFLAG_COOP_WITH_HUMAN)) {
                int32_t cost = game().mfactions[r].rule_techcost - 100;
                int32_t sign = cost > 0 ? 1 : (cost < 0 ? -1 : 0);
                if (DiploFriction >= ((R.diplo_status[f] & DIPLO_TREATY) ? 3 : 0) + (2 - sign) * 2) fear = true;
            }
            if (!fear && (MultiplayerActive || (GameRules & RULES_INTENSE_RIVALRY)) && P.ranking == 7 &&
                !(P.diplo_status[r] & DIPLO_PACT) && !(R.player_flags & PFLAG_COOP_WITH_HUMAN) &&
                (great_satan(f, 0) || (FactionRankingsUnk[f] * 3 > FactionRankingsUnk[RankingFactionIDUnk2] * 4 &&
                                       P.tech_ranking * 3 > game().factions[RankingFactionIDUnk2].tech_ranking * 4))) {
                fear = true;
            }
            if (fear) {
                if (counter) return 0;
                if (buy_tech(f, r, offer, 1, mode)) return 0;
                say_leader(0, f);
                parse_says(2, get_phrase(f, 3), -1, -1);
                label_variant("REJTECHFEAR", mode, 2);
                X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                return 0;
            }
            if (R.tech_ranking > P.tech_ranking || reactor) {
                if (counter) return 0;
                int32_t limit = ((R.diplo_status[f] & DIPLO_TREATY) ? 3 : 0) + (reactor ? 3 : 9) -
                                (std::strcmp(game().mfactions[r].filename, "HIVE") ? 0 : 3);
                if (DiploFriction >= limit && !(P.diplo_status[r] & DIPLO_PACT) &&
                    !(R.player_flags & PFLAG_COOP_WITH_HUMAN)) {
                    // faction_id_with keeps its lead.
                    if (!reactor && buy_tech(f, r, offer, 1, mode)) return 0;
                    if (offer == 5 && mode != 13) {
                        say_leader(0, f);
                        parse_says(2, get_phrase(f, 3), -1, -1);
                        X_pops_11("REJTECHFEAR1", 0x100000, portrait, kPopWait);
                        return 0;
                    }
                    if (!eager) return 0;
                    parse_says(0, game().mfactions[f].adj_insult_faction, -1, -1);
                    StrBuffer[0] = 0;
                    crt_strcat(StrBuffer, "REJTECHADVANCED");
                    label_with_number(StrBuffer, reactor ? 2 : game_random(0, 3));
                    X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                    return 0;
                }
            }
            if (DiploTradeOfferProject1 != -1 && mode != 13) {
                // The technology would let faction_id build a project faction_id_with wants.
                if (counter) return 0;
                int32_t fac = DiploTradeOfferProject1 >= 0 ? DiploTradeOfferProject1 + FAC_HUMAN_GENOME_PROJECT
                                                           : 68 - DiploTradeOfferProject1;
                parse_say(0, Facility[fac].name, -1, -1);
                say_leader(1, f);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "REJTECHPROJ");
                label_with_number(StrBuffer, DiploTradeOfferProject1 >= 0 ? game_random(0, 3) : 2);
                X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
                return 0;
            }
        }
        if (DiploTradeWantTech1 < 0 && mode != 5 && mode != 13 && !(R.traded_maps & (1 << f)) && !DiploMapsTraded &&
            !(P.diplo_status[r] & DIPLO_PACT) && !(R.player_flags & PFLAG_MAP_REVEALED)) {
            // Nothing to give: maps (9999) instead, if faction_id_with knows few of faction_id's bases.
            int32_t n = known_bases(f, r);
            if (GameRules & RULES_NO_UNITY_SURVEY) {
                if (!n && DiploFriction <= 7) DiploTradeWantTech1 = 9999;
            } else if (n < (P.base_count + 1) / 2 && DiploFriction <= 7) {
                DiploTradeWantTech1 = 9999;
            }
        }
        if (mode == 13 && (R.diplo_status[f] & DIPLO_PACT)) {
            // A pact partner gives the commlink frequency.
            if (!wants_to_speak(f, r)) return 0;
            parse_says(0, game().mfactions[DiploProposalFaction].adj_name_faction, -1, -1);
            X_pops("COMMLINKFREE", portrait, kPopWait);
            net_tech(f, DiploTradeOfferTech1, r, 1);
            DiploTradeOfferTech1 = -1;
            return 0;
        }
        if (DiploTradeOfferTech1 >= 0 && mode != 13 && (R.diplo_status[f] & DIPLO_PACT) &&
            (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED)) {
            // A surrendered faction hands its technology over.
            if (!wants_to_speak(f, r)) return 0;
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
            parse_says(0, StrBuffer, -1, -1);
            X_pops_11("TECHSLAVE", 0x100000, portrait, kPopWait);
            net_tech(f, DiploTradeOfferTech1, r, 1);
            DiploTradeOfferTech1 = -1;
            return 0;
        }
        if (DiploTradeWantTech1 < 0 && DiploTradeOfferTech1 >= 0 && mode != 5 && mode != 13 &&
            (R.diplo_status[f] & DIPLO_PACT) && DiploFriction < 10 &&
            (!(MultiplayerActive || (GameRules & RULES_INTENSE_RIVALRY)) || f != RankingFactionIDUnk1) &&
            !DiploGreatSatan) {
            int32_t rank = P.ranking, rank_r = R.ranking;
            if ((rank_r > rank || (rank >= 6 && rank_r >= 3)) &&
                R.tech_ranking >
                    (rank - rank_r) / 2 + R.AI_power * 2 + 4 - R.AI_tech + R.AI_fight + P.tech_ranking) {
                // A strong pact partner gives a technology away.
                if (!wants_to_speak(f, r)) return 0;
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
                parse_says(0, StrBuffer, -1, -1);
                X_pops_11("TECHFREEBIE", 0x100000, portrait, kPopWait);
                net_tech(f, DiploTradeOfferTech1, r, 1);
                DiploTradeOfferTech1 = -1;
                R.diplo_patience[f] = (int8_t)get_patience(f, r);  // inlined in the original
                return 0;
            }
        }
        int32_t seen = known_bases(r, f);
        bool trade = DiploTradeWantTech1 >= 0 && eager && !DiploGreatSatan;
        uint32_t agenda = P.diplo_agenda[r];
        if (trade && MultiplayerActive) {
            if ((agenda & 0x80) && f == RankingFactionIDUnk1 && !(P.diplo_status[r] & DIPLO_PACT) &&
                (game().factions[RankingFactionIDUnk2].ranking < 5 ||
                 FactionRankingsUnk[f] * 2 > FactionRankingsUnk[RankingFactionIDUnk2] * 3)) {
                trade = false;
            }
            if ((agenda & 0x100) && (f != RankingFactionIDUnk2 || (P.ranking >= 6 && !DiploRunawayLeader)))
                trade = false;
        }
        if (trade &&
            ((R.diplo_status[f] & DIPLO_WANT_REVENGE) || (R.diplo_agenda[f] & 0x200) || P.integrity_blemishes > 3))
            trade = false;
        if (trade && (GameRules & RULES_INTENSE_RIVALRY)) {
            if ((agenda & 0x80) && f == RankingFactionIDUnk1 && !(P.diplo_status[r] & DIPLO_PACT) &&
                FactionRankingsUnk[FactionRankings[7]] * 3 > FactionRankingsUnk[FactionRankings[6]] * 4) {
                trade = false;
            }
            if ((agenda & 0x100) && !(P.diplo_status[r] & DIPLO_PACT) && P.ranking >= 6 && !DiploRunawayLeader)
                trade = false;
        }
        if (trade && DiploTradeOfferTech1 < 0) {
            // Maps in return, unless faction_id_with already knows enough.
            if (DiploMapsTraded || (P.diplo_status[r] & DIPLO_PACT) || (P.player_flags & PFLAG_MAP_REVEALED)) {
                trade = false;
            } else if (P.traded_maps & (1 << r)) {
                int32_t limit = (GameRules & RULES_NO_UNITY_SURVEY) ? (R.base_count + 3) / 4 : (R.base_count + 1) / 2;
                if (seen >= limit) trade = false;
            }
        }
        if (!trade) {
            if (counter) return 0;
            if (buy_tech(f, r, offer, 0, mode)) return 0;
            StrBuffer[0] = 0;
            if (mode == 13) {
                crt_strcat(StrBuffer, "REJCOMMLINK");
            } else {
                crt_strcat(StrBuffer, "REJTECHLATER");
                int32_t variant = game_random(0, 3);
                if (P.diplo_status[r] & (DIPLO_PACT | DIPLO_TREATY)) {
                    if (DiploTradeWantTech1 < 0)
                        variant = 4;
                    else if (DiploTradeOfferTech1 < 0)
                        variant = 3;
                }
                label_with_number(StrBuffer, variant);
            }
            X_pops_11(StrBuffer, 0x100000, portrait, kPopWait);
            return 0;
        }
        if (!wants_to_speak(f, r)) return 0;
        int32_t value;  // friction falls by this much
        for (;;) {
            if (DiploTradeWantTech1 != 9999) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech1, 1);
                parse_says(0, StrBuffer, -1, -1);
            }
            if (DiploTradeOfferTech1 >= 0) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
                parse_says(1, StrBuffer, -1, -1);
            }
            if (mode == 13) {
                // A commlink frequency for a technology.
                parse_says(2, game().mfactions[DiploProposalFaction].adj_name_faction, -1, -1);
                if (!X_pops("COMMLINKTECH", portrait, kPopWait)) return 0;
                net_tech(r, DiploTradeWantTech1, f, 1);
                net_tech(f, DiploTradeOfferTech1, r, 1);
                DiploTradeOfferTech1 = -1;
                DiploTradeWantTech1 = DiploTradeWantTech2;
                DiploTradeWantTech2 = -1;
                return 1;
            }
            if (DiploTradeWantTech2 >= 0) {
                StrBuffer[0] = 0;
                say_tech_2(StrBuffer, DiploTradeWantTech2, 1);
                parse_says(2, StrBuffer, -1, -1);
            }
            say_leader(3, f);
            parse_says(5, get_noun_phrase(f, 9), -1, -1);
            parse_says(6, get_noun_phrase(r, 9), -1, -1);
            GenderDefault = game().mfactions[r].is_leader_female;
            PluralDefault = 0;
            parse_says(8, game().mfactions[r].name_leader, -1, -1);
            parse_says(9, get_noun_phrase(r, 7), -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "TRADETECH");
            if (DiploTradeWantTech1 == 9999)
                label_with_number(StrBuffer, DiploTradeOfferTech1 < 0 ? 5 : 4);
            else if (DiploTradeOfferTech1 < 0)
                label_with_number(StrBuffer, 3);
            else
                label_with_number(StrBuffer, game_random(0, 3));
            // TRADETECHOPT's items (besides the accept / decline lines of the label): 4 ask for the
            // second technology, 2 offer the second technology, 3 help.
            ui_diplo_menu_start(StrBuffer, 0x100000);
            if (!X_text_open(ScriptFileName, "TRADETECHOPT")) {
                text_get();
                if (DiploTradeOfferTech2 >= 0 && DiploTradeOfferTech2 != DiploTradeOfferTech1) {
                    StrBuffer[0] = 0;
                    say_tech_2(StrBuffer, DiploTradeOfferTech2, 1);
                    parse_says(9, StrBuffer, -1, -1);
                    StrBuffer[0] = 0;
                    parse_string(TextBufferGetPtr, StrBuffer);
                    ui_diplo_menu_item(StrBuffer, 4);
                }
                text_get();
                if (DiploTradeWantTech2 >= 0 && DiploTradeWantTech2 != DiploTradeWantTech1) {
                    StrBuffer[0] = 0;
                    parse_string(TextBufferGetPtr, StrBuffer);
                    ui_diplo_menu_item(StrBuffer, 2);
                }
                text_get();
                ui_diplo_menu_item(TextBufferGetPtr, 3);
            }
            int32_t choice = ui_diplo_menu_exec(r);
            ui_diplo_menu_close();
            if (!choice) {
                DiploTradeDeclined = 1;
                return 0;
            }
            if (choice == 3) {
                help_topic(0xE, DiploTradeWantTech1 == 9999 ? DiploTradeOfferTech1 : DiploTradeWantTech1);
                continue;
            }
            value = DiploTradeWantValue1 / 4;
            if (choice == 4) {
                // faction_id asks for faction_id_with's second technology instead.
                if (!(R.diplo_status[f] & DIPLO_PACT) && DiploTradeWantTech2 < 0) {
                    int32_t second = tech_val(DiploTradeOfferTech2, r, 0);
                    if (second > tech_val(DiploTradeWantTech1, r, 0)) {
                        if (DiploTradeOfferTech1 >= 0) {
                            StrBuffer[0] = 0;
                            say_tech_2(StrBuffer, DiploTradeOfferTech1, 1);
                            parse_says(0, StrBuffer, -1, -1);
                        }
                        X_pops_11("REJTECHINSTEAD", 0x100000, portrait, kPopWait);
                        DiploTradeOfferTech2 = -1;
                        continue;
                    }
                }
                swap(&DiploTradeOfferTech1, &DiploTradeOfferTech2);
                value = DiploTradeWantValue1 / 8;
            } else if (choice == 2) {
                // faction_id offers its second technology instead.
                if (!((R.diplo_status[f] & DIPLO_PACT) && (R.diplo_status[f] & DIPLO_HAVE_SURRENDERED))) {
                    const char* filename = game().mfactions[r].filename;
                    if (!std::strcmp(filename, "HIVE") || (MultiplayerActive && f == RankingFactionIDUnk1) ||
                        (std::strcmp(filename, "UNIV") && (great_satan(f, 0) || R.tech_ranking > P.tech_ranking))) {
                        X_pops_11("REJTECHSECOND", 0x100000, portrait, kPopWait);
                        R.diplo_patience[f]++;
                        return 0;
                    }
                }
                swap(&DiploTradeWantTech1, &DiploTradeWantTech2);
                value = DiploTradeWantValue2 / 8;
            }
            break;
        }
        if (counter) net_treaty_on(r, f, 0x1000000, 0);
        net_agenda_on(r, f, 0x180, 0);
        if (DiploTradeOfferTech1 < 0) DiploTradeOfferTech1 = 9999;
        net_tech(r, DiploTradeWantTech1, f, 1);
        net_tech(f, DiploTradeOfferTech1, r, 1);
        if (DiploTradeWantTech1 == 9999) DiploMapsTraded = 1;
        if (DiploTradeOfferTech1 == 9999) DiploMapsTraded = 1;
        DiploTradeWantTech1 = DiploTradeWantTech2;
        DiploTradeWantTech2 = -1;
        lower_friction(f, r, value);
        DiploHostility = 0;
        if (!counter) return 1;
    }
}
