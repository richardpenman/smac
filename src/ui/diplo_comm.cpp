// communicate (0x54FFD0): the diplomacy dialog between a human player (faction_id) and a computer
// player (faction_id_with, whose leader is on the commlink), reconstructed from the disassembly.
// The original is one 34 KB function: the topics the computer player raises (withdrawal demands,
// pleas for help, introductions, map and technology trades, tribute and technology demands, loans,
// social priorities, vendettas, truces, treaties, reports), then the human's DIPLOMACY menu.
// The control flow keeps the original's labels (L_<address>) and its stack slots (sXX, named by
// their offset from ebp) where the original reuses one slot for several purposes.
// Game state changes go through the engine (net_* in single player call the plain functions).
// Multiplayer paths: the inlined "diplomatic lock" and message sending are the engine's net_*
// functions (identical in single player); smacx plays single player games only.
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/council.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
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
#include "ui/diplowin.h"
#include "ui/popup.h"

namespace {

constexpr int32_t kPopWait = 0x5398E0;

uint32_t& ds(int32_t a, int32_t b) { return Factions[a].diplo_status[b]; }
bool alive(int32_t f) { return (FactionStatus[1] & (1 << f)) != 0; }
bool human(int32_t f) { return (FactionStatus[0] & (1 << f)) != 0; }
bool alien(int32_t f) { return (MFactions[f].rule_flags & 0x80) != 0; }
const char* portrait(int32_t f) { return FactionPortraits[f]; }
int32_t pop(const char* label, int32_t fw) { return X_pops_11(label, 0x100000, portrait(fw), kPopWait); }
void say_leader(int32_t n, int32_t f) {
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(n, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, m.name_leader, -1, -1);
}
void say_noun(int32_t n, int32_t f) {
    const MFaction& m = MFactions[f];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(n, m.noun_faction, -1, -1);
}
void says_n(int32_t n, const char* text) { parse_says(n, text, -1, -1); }
void label_num(const char* label, int32_t n) {
    char num[80];
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label);
    crt_itoa(n, num, 10);
    crt_strcat(StrBuffer, num);
}
// The inlined diplomatic lock (diplo_lock without the timeout).
void lock() {
    log_say_2("Getting DIPLOMATIC LOCK", 0, 0, 0);
    engine_real_time();
    while (!ControlTurnA && MultiplayerActive && NetDaemon_add_global(&NetState)) {
        check_spock();
        engine_real_time();
    }
}
void unlock() { diplo_unlock(); }
// Friction of faction_with toward faction_id raised (the dialog's copy DiploFriction too).
void friction_add(int32_t f, int32_t fw, int32_t n) {
    Factions[fw].diplo_friction[f] = range(Factions[fw].diplo_friction[f] + n, 0, 20);
    if (DiploFrictionFactionID == fw && DiploFrictionFactionIDWith == f) DiploFriction += n;
}
void say_tech_slot(int32_t n, int32_t tech_id) {
    clear();
    say_tech(tech_id, 1);
    parse_says(n, StrBuffer, -1, -1);
}
int32_t base_region(int32_t base_id) { return map_tile(Bases[base_id].x, Bases[base_id].y)->region; }
// X_pops_13 (0x5BF830): X_pops_18 of another script file.
int32_t X_pops_13(const char* file, const char* label, int32_t flags, const char* pcx, int32_t a5) {
    return X_pops_18(file, label, -1, nullptr, (uint32_t)flags, pcx, 1, 1, a5);
}

}  // namespace

int32_t GAMEAPI communicate(int32_t f, int32_t fw, int32_t a3) {
    int32_t s14 = 0, s1c = 0, s20 = 0, s34 = 0, s38 = 0, s3c = 0, s48 = 0, s54 = 0;
    int32_t base_id = 0;
    int32_t declare = 0;
    int32_t variant = 0, price = 0, amount = 0, r = 0;
    int32_t gift = 0;
    DiploSecondFaction = fw;
    DiploWinState = 1;
    DiploAcceptedFaction = 0;
    int32_t first_contact = (!(ds(fw, f) & 0x8000000) || (MultiplayerActive && (DiploStateA[fw] & 2))) ? 1 : 0;
    DiploMapsTraded = 0;
    uint32_t sf = ds(f, fw);
    int32_t vendetta = ((sf & 4) && !(sf & 0x10000)) ? 1 : 0;
    DiploCaptionMode = a3 ? 3 : 0;
    if (first_contact) {
        const MFaction& m = MFactions[fw];
        int32_t cat = m.soc_priority_category;
        if (cat >= 0 && m.soc_priority_model) {
            int32_t cur = (&Factions[f].SE_Politics_pending)[cat];
            if (cur && cur != m.soc_priority_model) friction_add(f, fw, 3);
        }
        int32_t eff = m.soc_priority_effect;
        if (eff >= 0 && (&Factions[f].SE_economy_pending)[eff] < 0) friction_add(f, fw, 3);
    }
    diplomacy_check(f, fw, 1);
    int32_t flags = DiploStateA[fw];
    DiploStateA[fw] = 0;
    if (!(flags & 0x2C)) goto L_550a8b;
    if (flags & 0x24) DiploCaptionMode = 8;
    if (!wants_to_speak(f, fw)) goto END;
    if (flags & 4) {
        // GETOUT: faction_id's units in faction_id_with's territory.
        territory(fw, f, 0, &base_id, 0);
        diplomacy_caption(f, fw);
        if ((uint8_t)Bases[base_id].faction_id != f && !(Bases[base_id].visibility & (1 << f))) {
            spot_base(base_id, f);
            draw_tile(Bases[base_id].x, Bases[base_id].y, 2);
            Console_focus(&MainConsole, Bases[base_id].x, Bases[base_id].y, f);
        }
        says_n(0, Bases[base_id].name);
        say_leader(1, fw);
        say_leader(3, f);
        says_n(5, MFactions[f].adj_insult_faction);
        says_n(6, MFactions[fw].adj_name_faction);
        label_num("GETOUT", game_random(0, 5) + 1);
        r = pop(StrBuffer, fw);
        if (r == 0) {
            lock();
            net_withdrawal(fw, f, 1);
            unlock();
            goto L_550a82;
        }
        if (r == 1) {
            if (!DiploHostility || (!(ds(fw, f) & 0x800) && DiploFearScale)) {
                lock();
                net_withdrawal(fw, f, 1);
                net_withdrawal(f, fw, 1);
                unlock();
                pop("BOTHOUT", fw);
                goto L_550a82;
            }
            uint32_t s = ds(fw, f);
            if (!(s & 0x20)) {
                if (DiploFriction < (5 - Factions[fw].AI_fight) * 3) {
                    lock();
                    net_withdrawal(fw, f, 1);
                    net_withdrawal(f, fw, 1);
                    unlock();
                    pop("BOTHOUT", fw);
                    goto L_550a82;
                }
                if (Factions[fw].mil_strength_1 <= Factions[f].mil_strength_1) {
                    lock();
                    net_withdrawal(fw, f, 1);
                    net_withdrawal(f, fw, 1);
                    unlock();
                    pop("BOTHOUT", fw);
                    goto L_550a82;
                }
            }
            s38 = 1;
            goto L_550522;
        }
        friction_add(f, fw, 10);
        lock();
        net_double_cross(f, fw, -1, 1);
        unlock();
        {
            const MFaction& m = MFactions[f];
            GenderDefault = m.is_leader_female;
            PluralDefault = 0;
            says_n(0, m.title_leader);
            GenderDefault = m.is_leader_female;
            PluralDefault = 0;
            says_n(1, m.name_leader);
            GenderDefault = m.is_leader_female;
            PluralDefault = 0;
            says_n(2, m.insult_leader);
            says_n(3, m.adj_insult_faction);
        }
        label_num("OUTRAGE", game_random(0, 3));
        pop(StrBuffer, fw);
        goto END;
    }
    if (flags & 8) {
        territory(fw, f, 0, &base_id, 0);
        if (flags & 0x10) net_set_treaty(f, fw, 0x400, 1, 1);
        if ((uint8_t)Bases[base_id].faction_id != f && !(Bases[base_id].visibility & (1 << f))) {
            spot_base(base_id, f);
            draw_tile(Bases[base_id].x, Bases[base_id].y, 2);
        }
        Console_focus(&MainConsole, Bases[base_id].x, Bases[base_id].y, f);
        says_n(0, Bases[base_id].name);
        label_num("GOAWAY", game_random(0, 2));
        pop(StrBuffer, fw);
    } else if (flags & 0x20) {
        territory(fw, f, 0, &base_id, 0);
        if (base_id >= 0 && (uint8_t)Bases[base_id].faction_id != fw &&
            !(Bases[base_id].visibility & (1 << fw))) {
            spot_base(base_id, fw);
            draw_map(1);
        }
        // BUG(original): base_id may be -1 here (the name is read before Bases).
        says_n(0, (&Bases[0] + base_id)->name);
        says_n(1, MFactions[fw].adj_name_faction);
        pop("INOURTERRITORY2", fw);
    }
L_550a82:
    if (!(flags & 3)) goto END;
L_550a8b:
    if (!a3) {
        Factions[fw].diplo_patience[f] = 0;
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        net_treaty_off(fw, f, 0x4000000, 1);
        net_agenda_off(fw, f, 0x100, 1);
        if (!(ds(fw, f) & 0x10000) && flags < 4) net_treaty_off(fw, f, 4, 1);
    } else {
        if (ds(f, fw) & 0x2000) {
            Factions[fw].diplo_patience[f] = 0;
            net_treaty_off(fw, f, 0x100, 1);
        }
        say_leader(2, f);
        if (!first_contact) {
            uint32_t s = ds(fw, f);
            int32_t limit;
            if (s & 0x10) {
                limit = 1;
            } else if (s & 1) {
                limit = (s & 0x2000000) ? 0x1F4 : 6;
            } else {
                limit = (int32_t)((s >> 1) & 1) - (DiploFriction + 3) / 8 + 3;
            }
            if (Factions[fw].diplo_patience[f] >= limit || (s & 0x140)) {
                if (!DiploCaptionOff) diplomacy_caption(f, fw);
                if (ds(f, fw) & 3) {
                    say_leader(0, fw);
                    pop("NOPATIENCEFRIEND", fw);
                } else {
                    say_noun(0, fw);
                    pop("NOPATIENCE", fw);
                }
                goto END;
            }
        }
        wants_to_speak(f, fw);
    }
    {
        uint32_t s = ds(fw, f);
        if (s & 0x40000) {
            DiploHostility = 1;
            DiploTributeDemand = -1;
            goto L_550522;
        }
        if (s & 1) {
            if (s & 0x2000000) {
                tech_trade(f, fw, 1, 4, 4);
                goto L_5571e7;
            }
            if (DiploNearEndgame && DiploRivalOutranks) DiploHelpFaction = 0;
        } else if (DiploFearScale && !DiploHostility && !a3 && !(flags & 0x40) && (s & 7)) {
            tech_trade(f, fw, 1, 4, 4);
            goto END;
        }
    }
    tech_analysis(f, fw);
    if (flags & 0x40) {
        int32_t help = DiploHelpFaction;
        if (!help || !wants_to_speak(f, fw)) goto L_55362d;
        help = DiploHelpFaction;
        if (Factions[help].mil_strength_1 >= 2 * Factions[fw].mil_strength_1 &&
            Factions[fw].energy_credits >= 10) {
            // PLEA: faction_id_with asks for help against DiploHelpFaction and pays for it.
            int32_t energy = Factions[fw].energy_credits;
            amount = energy / 2 / 25 * 25;
            if (amount < 25) amount = energy < 25 ? energy : 25;
            parse_num(0, amount);
            say_leader(0, help);
            say_noun(2, help);
            says_n(3, get_pact_hood(f, fw));
            says_n(5, get_pact_hood(f, DiploHelpFaction));
            says_n(6, get_his_her(DiploHelpFaction, 0));
            says_n(8, MFactions[DiploHelpFaction].adj_insult_leader);
            says_n(9, get_phrase(DiploHelpFaction, 2));
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "PLEA");
            if (ds(f, DiploHelpFaction) & 1) crt_strcat(StrBuffer, "PACT");
            if (DiploTradeOfferTech1 >= 0) {
                crt_strcat(StrBuffer, "TECH");
                parse_say(4, Tech[DiploTradeOfferTech1].name, -1, -1);
            }
            if (pop(StrBuffer, fw)) {
                lock();
                pact_of_brotherhood(f, fw);
                net_treaty_off(f, fw, 0x60, 0);
                net_double_cross(f, DiploHelpFaction, fw, 0);
                if (DiploTradeOfferTech1 >= 0) {
                    s14 = DiploTradeOfferTech1;
                    net_tech(f, s14, fw, 0);
                    DiploTradeOfferTech1 = -1;
                }
                net_energy(fw, -amount, f, amount, 1);
                s14 = 1;
                unlock();
                DiploAcceptedFaction = DiploHelpFaction;
                goto L_5516c7;
            }
            net_treaty_on(f, fw, 0x800000, 0);
            net_treaty_off(f, fw, 0x1000000, 0);
            DiploEnemyAlly = DiploHelpFaction;
            goto L_55362d;
        }
        // PROPOSE: help without payment.
        say_leader(0, help);
        say_noun(2, DiploHelpFaction);
        says_n(3, get_him_her(DiploHelpFaction, 0));
        says_n(4, get_pact_hood(f, fw));
        PluralDefault = 0;
        GenderDefault = MFactions[f].is_leader_female;
        says_n(5, MFactions[f].title_leader);
        says_n(6, get_pact(DiploHelpFaction));
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "PROPOSE");
        if (ds(f, DiploHelpFaction) & 1) {
            crt_strcat(StrBuffer, "PACT");
            PluralDefault = 0;
            GenderDefault = MFactions[fw].is_leader_female;
            says_n(8, MFactions[fw].title_leader);
            says_n(9, get_phrase(DiploHelpFaction, 2));
        } else {
            says_n(8, MFactions[DiploHelpFaction].adj_insult_faction);
            says_n(9, get_phrase(DiploHelpFaction, 5));
        }
        if (pop(StrBuffer, fw)) {
            lock();
            pact_of_brotherhood(f, fw);
            net_treaty_off(f, fw, 0x60, 0);
            net_double_cross(f, DiploHelpFaction, fw, 1);
            unlock();
            DiploAcceptedFaction = DiploHelpFaction;
            goto L_5516c7;
        }
        net_treaty_on(f, fw, 0x800000, 0);
        net_treaty_off(f, fw, 0x1000000, 0);
        DiploEnemyAlly = DiploHelpFaction;
        goto L_55362d;
    L_5516c7:
        DiploHelpFaction = 0;
        DiploFearScale = 0;
        DiploTributeDemand = 0;
        DiploPrevHostility = 0;
        DiploHostility = 0;
        s20 = 1;
        DiploEnemyAllyCount = 0;
        s34 = 1;
        goto L_55362d;
    }
    if (Factions[fw].diplo_agenda[f] & 0x2408) goto L_55362d;
    if (ds(fw, f) & 0x40) goto L_55362d;
    if (DiploAttackChance && !first_contact) goto L_55362d;
    // DEMANDATTACK: faction_id_with asks faction_id to attack DiploHelpFaction.
    if (DiploHelpFaction && !(ds(f, DiploHelpFaction) & 0x80)) {
        if (!DiploHostility && DiploConquestFear) goto L_551ecb;
        bool talk = !DiploFearScale || DiploAlterFear > 0 || (DiploHostility && !DiploCanBetray);
        if (!talk) {
            if (Factions[f].ranking >= 6 && Factions[fw].ranking <= 4 && !DiploPlayerPactCount &&
                !DiploRivalStrongPacts) {
                talk = true;
            } else if (great_satan(DiploHelpFaction, 0) || (ds(fw, f) & 1)) {
                talk = true;
            }
        }
        if (talk && wants_to_speak(f, fw)) {
            int32_t help = DiploHelpFaction;
            says_n(0, get_pact(help));
            say_leader(1, DiploHelpFaction);
            say_noun(3, DiploHelpFaction);
            says_n(4, get_his_her(DiploHelpFaction, 0));
            says_n(5, get_him_her(DiploHelpFaction, 0));
            says_n(6, MFactions[DiploHelpFaction].adj_name_faction);
            char suffix[80];
            suffix[0] = 0;
            help = DiploHelpFaction;
            uint32_t s = ds(f, fw);
            int32_t n;
            if ((s & 1) && ((s & 0x2000) || !(ds(f, help) & 8))) {
                n = 3;
            } else {
                uint32_t t = ds(f, help);
                if (t & 1) {
                    n = 0;
                    says_n(9, get_phrase(help, 5));
                } else if (t & 2) {
                    n = 1;
                    says_n(6, get_noun_phrase(help, 8));
                    says_n(9, get_phrase(DiploHelpFaction, 5));
                    says_n(8, get_phrase(DiploHelpFaction, 2));
                    if (game_random(0, 2) == 0 && !(ds(f, fw) & 0x20)) crt_strcat(suffix, "b");
                    else crt_strcat(suffix, "a");
                } else if ((t & 4) && !(t & 0x10000)) {
                    n = 2;
                    if (game_random(0, 2)) {
                        crt_strcat(suffix, "a");
                        says_n(8, MFactions[DiploHelpFaction].adj_insult_leader);
                        says_n(9, MFactions[DiploHelpFaction].adj_insult_faction);
                    } else {
                        crt_strcat(suffix, "b");
                        says_n(8, get_noun_phrase(fw, 9));
                        says_n(9, get_phrase(DiploHelpFaction, 2));
                    }
                } else {
                    n = 4;
                }
            }
            label_num("DEMANDATTACK", n);
            crt_strcat(StrBuffer, suffix);
            if (pop(StrBuffer, fw) == 0) {
                uint32_t s2 = ds(fw, f);
                if (s2 & 1) {
                    s38 = 1;
                    if (s2 & 0x2000) DiploHostility = 1;
                    int32_t v = Factions[fw].diplo_friction[f] + 4;
                    Factions[fw].diplo_friction[f] = v < 0 ? 0 : (v > 20 ? 20 : v);
                    if (DiploFrictionFactionID == fw && DiploFrictionFactionIDWith == f) DiploFriction += 4;
                } else if (DiploEnemyAllyCount && DiploFriction >= 10 && !DiploFearScale &&
                           Factions[fw].mil_strength_1 > Factions[f].mil_strength_1) {
                    DiploHostility = 1;
                }
                net_treaty_on(f, fw, 0x800000, 0);
                net_treaty_off(f, fw, 0x1000000, 0);
                DiploEnemyAlly = DiploHelpFaction;
                diplomacy_caption(f, fw);
                goto L_553464;
            }
            lock();
            net_treaty_on(f, fw, 0x1000000, 0);
            net_treaty_off(f, fw, 0x800000, 0);
            net_double_cross(f, DiploHelpFaction, fw, 1);
            unlock();
            DiploAcceptedFaction = DiploHelpFaction;
            DiploHelpFaction = 0;
            s20 = 1;
            DiploEnemyAllyCount = 0;
            diplomacy_caption(f, fw);
            goto L_553464;
        }
    }
    if (DiploHostility) goto L_553464;
L_551ecb:
    if (DiploCanBetray) goto L_553464;
    {
        // INTRODUCEENEMY: a contact with faction_id_with's enemy that faction_id has not met.
        int32_t u = DiploUnmetEnemy;
        if (u && !(ds(f, u) & 0x8000008) &&
            !(human(u) && ((MultiplayerActive && Rules.tgl_humans_always_contact_net) ||
                           (PbemActive && Rules.tgl_humans_always_contact_pbem))) &&
            !(ds(f, fw) & 0x31) && !great_satan(f, 0) &&
            (Factions[f].ranking <= 5 || Factions[fw].ranking <= 4)) {
            int32_t c = great_satan(DiploUnmetEnemy, 0);
            c += Factions[DiploUnmetEnemy].ranking > Factions[f].ranking;
            c += DiploPlayerPactCount == 0;
            c += MultiplayerActive == 0;
            if (c >= 2 && !alien(f) && !alien(fw) && wants_to_speak(f, fw)) {
                say_leader(0, DiploUnmetEnemy);
                say_noun(2, DiploUnmetEnemy);
                says_n(3, get_phrase(DiploUnmetEnemy, 5));
                says_n(4, get_pact_hood(f, fw));
                // BUG(original): the alien text is chosen by DiploHelpFaction, not the faction met.
                if (!pop(alien(DiploHelpFaction) ? "INTRODUCEENEMYALIEN" : "INTRODUCEENEMY", fw)) goto L_553464;
                lock();
                net_set_treaty(f, DiploUnmetEnemy, 0x10000000, 1, 0);
                pact_of_brotherhood(f, fw);
                net_treaty_on(f, fw, 0x1000000, 0);
                net_treaty_off(f, fw, 0x800000, 0);
                net_double_cross(f, DiploUnmetEnemy, fw, 1);
                net_treaty_on(f, DiploUnmetEnemy, 0x8000008, 0);
                unlock();
                DiploAcceptedFaction = DiploUnmetEnemy;
                DiploUnmetEnemy = 0;
                goto L_552331;
            }
        }
    }
    // BETRAYFRIEND: faction_id_with asks faction_id to turn on a faction it has not met / both know.
    for (int32_t pass = 0; pass < 2; pass++) {
        int32_t& g = pass == 0 ? DiploUnmetFaction : DiploMutualFaction;
        if (!g || has_treaty(f, fw, 0x31) || has_treaty(f, g, 0x10)) continue;
        if (DiploPlayerPactCount >= 2 && !great_satan(g, 0) && !human(g)) continue;
        if (!wants_to_attack(fw, g, f)) continue;
        if (Factions[f].ranking > 5 && Factions[fw].ranking > 4 && !great_satan(g, 0)) continue;
        if (!contiguous(f, g, 0, 0) && contiguous(fw, g, 0, 0)) continue;
        if (!wants_to_speak(f, fw)) continue;
        says_n(0, get_name(f));
        says_n(1, get_title(g));
        says_n(2, get_name(g));
        says_n(3, get_adjective_insult_faction(g));
        says_n(4, get_phrase(g, 2));
        says_n(5, get_pact_hood(f, fw));
        says_n(6, get_noun(g));
        if (!pop("BETRAYFRIEND", fw)) goto L_553464;
        if (pass == 0) {
            net_set_treaty(f, g, 0x10000000, 1, 0);
            diplo_lock(0);
        } else {
            diplo_lock(0);
            net_set_treaty(f, g, 0x10000000, 1, 0);
        }
        pact_of_brotherhood(f, fw);
        net_treaty_on(f, fw, 0x1000000, 0);
        net_treaty_off(f, fw, 0x800000, 0);
        net_double_cross(f, g, fw, 1);
        net_treaty_on(f, g, 0x8000008, 0);
        diplo_unlock();
        DiploAcceptedFaction = g;
        g = 0;
        goto L_552331;
    }
    // ENEMYMAP: faction_id buys faction_id_with's map of faction_id's enemy.
    {
        int32_t pe = DiploPlayerEnemy;
        price = CurrentTurn < 50 ? 25 : 50;
        if (pe && !has_treaty(f, fw, 0x31) && !has_treaty(f, pe, 0x10000001) &&
            !(Factions[f].traded_maps & (1 << pe)) && (!human(pe) || (Factions[fw].traded_maps & (1 << pe))) &&
            ((DiploTradeWantTech1 >= 0 && !(GameRules & 0x2000000)) || Factions[f].energy_credits >= price) &&
            wants_to_speak(f, fw)) {
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            says_n(2, get_title(DiploPlayerEnemy));
            says_n(3, get_name(DiploPlayerEnemy));
            says_n(4, get_adjective(DiploPlayerEnemy));
            say_tech_slot(5, DiploTradeWantTech1);
            price = CurrentTurn < 50 ? 25 : 50;
            parse_num(0, price);
            clear();
            says("ENEMYMAP");
            says(Factions[f].energy_credits >= price ? "1" : "2");
            net_set_treaty(f, DiploPlayerEnemy, 0x10000000, 1, 0);
            if (!pop(StrBuffer, fw)) goto L_553464;
            net_treaty_on(f, fw, 0x1000000, 0);
            net_tech(fw, 9999, DiploPlayerEnemy, 1);
            net_tech(f, 9999, DiploPlayerEnemy, 1);
            if (Factions[f].energy_credits >= price) net_energy(f, -price, fw, price, 1);
            else net_tech(fw, DiploTradeWantTech1, f, 1);
            DiploMapsTraded = 1;
            DiploTradeWantTech1 = DiploTradeWantTech2;
            DiploTradeWantTech2 = -1;
            s20 = 1;
            DiploPlayerEnemy = 0;
            goto L_553464;
        }
    }
    // METFRIEND: faction_id_with offers a commlink with a faction faction_id has not met.
    {
        int32_t uf = DiploUnmetFaction;
        if (uf && !has_treaty(f, fw, 0x30) && !has_project(4, f) && !has_treaty(f, uf, 8) &&
            !(human(DiploUnmetFaction) && auto_contact()) && wants_to_speak(f, fw)) {
            int32_t kind = 0;
            says_n(2, get_title(DiploUnmetFaction));
            says_n(3, get_name(DiploUnmetFaction));
            says_n(4, get_noun(DiploUnmetFaction));
            says_n(5, get_noun_phrase(DiploUnmetFaction, 9));
            int32_t best = -1, best_val = 0, v = -1;
            for (int32_t t = 0; t < 89; t++) {
                if (!has_tech(t, DiploUnmetFaction) || has_tech(t, f) || has_tech(t, fw)) continue;
                v = tech_val(t, f, 1);
                if (v > best_val) {
                    best_val = v;
                    best = t;
                }
            }
            amount = v;
            int32_t p = (CurrentTurn / 25 + 1) * 10;
            if (Factions[f].ranking < 5) p /= 2;
            if (Factions[f].ranking == 7) p += p / 2;
            if (Factions[f].energy_credits >= p) {
                kind = 2;
                int32_t rr = game_random(0, CurrentTurn / 10 + 2);
                amount = p + rr * 5;
                int32_t e = Factions[f].energy_credits;
                if (amount > e) amount -= (int32_t)((uint32_t)(amount - e + 4) / 5) * 5;
                parse_num(0, amount);
                if (amount > 0) goto L_552f36;
                kind = 0;
            }
            if (DiploTradeWantTech1 >= 0 && CurrentTurn > 25 && !(GameRules & 0x2000000)) {
                kind = 1;
                say_tech_slot(6, DiploTradeWantTech1);
            }
        L_552f36:
            if (best >= 0) parse_say(8, Tech[best].name, -1, -1);
            clear();
            says(alien(DiploUnmetFaction) ? "METALIEN" : "METFRIEND");
            say_num(kind);
            if (best >= 0) says("a");
            if (!pop(StrBuffer, fw)) goto L_553464;
            net_treaty_on(f, DiploUnmetFaction, 8, 0);
            net_treaty_on(f, fw, 0x1000000, 0);
            if (kind == 1) {
                cause_friction(fw, f, -1);
                net_tech(fw, DiploTradeWantTech1, f, 1);
                DiploTradeWantTech1 = -1;
            } else if (kind == 2) {
                cause_friction(fw, f, -1);
                net_energy(f, -amount, fw, amount, 1);
            }
            goto L_553464;
        }
    }
    // BOTHFRIEND: maps of a faction both know, for a technology.
    {
        int32_t mf = DiploMutualFaction;
        if (mf && !has_treaty(f, fw, 0x30) && !has_treaty(f, mf, 0x10000001) &&
            !(Factions[f].traded_maps & (1 << mf)) && (!human(mf) || (Factions[fw].traded_maps & (1 << mf))) &&
            has_treaty(f, mf, 0x8000000) && DiploTradeWantTech1 >= 0 && wants_to_speak(f, fw)) {
            net_maps(fw, DiploMutualFaction, 1);
            says_n(2, get_title(DiploMutualFaction));
            says_n(3, get_name(DiploMutualFaction));
            says_n(4, get_adjective_insult_faction(DiploMutualFaction));
            says_n(5, get_adjective(DiploMutualFaction));
            say_tech_slot(6, DiploTradeWantTech1);
            net_set_treaty(f, DiploMutualFaction, 0x10000000, 1, 0);
            if (!pop(alien(DiploMutualFaction) ? "BOTHFRIENDALIEN" : "BOTHFRIEND", fw)) goto L_553464;
            net_treaty_on(f, fw, 0x1000000, 0);
            net_maps(f, fw, 1);
            net_tech(fw, DiploTradeWantTech1, f, 1);
            DiploMapsTraded = 1;
            DiploTradeWantTech1 = DiploTradeWantTech2;
            DiploTradeWantTech2 = -1;
            s20 = 1;
            DiploPlayerEnemy = 0;
            goto L_553464;
        }
    }
    // ANYNEWS: faction_id_with buys a commlink with a faction faction_id knows.
    if (!has_treaty(f, fw, 0x30) && !has_project(4, fw) &&
        Factions[fw].energy_credits >= (CurrentTurn / 25 + 1) * 10) {
        int32_t i = 1;
        for (; i < 8; i++) {
            if (alive(i) && i != f && i != fw && has_treaty(f, i, 8) && !has_treaty(fw, i, 8) &&
                !has_treaty(fw, i, 0x10000000) && !wants_to_attack(i, fw, 0) && !wants_to_attack(fw, i, 0))
                break;
        }
        if (i < 8 && wants_to_speak(f, fw)) {
            says_n(0, get_title(i));
            says_n(1, get_name(i));
            says_n(2, get_adjective(i));
            amount = (CurrentTurn / 25 + 1) * 10;
            parse_num(0, amount);
            if (pop(alien(i) ? "ANYALIENNEWS" : "ANYNEWS", fw)) {
                net_set_treaty(fw, i, 0x10000000, 1, 0);
                net_treaty_on(fw, i, 8, 0);
                net_energy(fw, -amount, f, amount, 1);
                s14 = 1;
                cause_friction(fw, f, -2);
            }
        }
    }
    goto L_553464;
L_552331:
    DiploHelpFaction = 0;
    s20 = 1;
    DiploEnemyAllyCount = 0;
    s34 = 1;
L_553464:
    // RANSOMDEMAND: faction_id_with wants back a base faction_id captured from it.
    if (!has_treaty(fw, f, 3) && !DiploFearScale && !has_treaty(f, fw, 0x2000000) && DiploTributeDemand > 0 &&
        DiploHostility && !DiploCanBetray && !MultiplayerActive && !s20 &&
        Factions[fw].mil_strength_1 > Factions[f].mil_strength_1) {
        int32_t b = -1, best = 999;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id == f && (uint8_t)Bases[i].faction_id_former == fw &&
                Bases[i].pop_size < best) {
                best = Bases[i].pop_size;
                b = i;
            }
        }
        if (b >= 0 && wants_to_speak(f, fw)) {
            base_id = b;
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            says_n(2, Bases[b].name);
            if (pop("RANSOMDEMAND", fw)) {
                s20 = 1;
                net_cede_base(b, fw, 1);
                net_set_treaty(f, fw, 0x2000000, 0, 0);
                net_treaty_off(fw, f, 0x20, 1);
                diplomacy_caption(f, fw);
            } else {
                s38 = 1;
            }
        }
    }
L_55362d:
    if (!(GameRules & 0x2000000)) tech_trade(f, fw, 1, 4, 4);
    if (great_satan(f, 0) || (Factions[f].ranking >= 6 && Factions[fw].ranking <= 4 && !DiploRunawayLeader)) {
        if (!DiploHostility && !DiploCanBetray && !has_agenda(f, fw, 0x2000) &&
            (!has_treaty(f, fw, 1) || (!(GameRules & 0x1000) && !DiploRivalWarCount))) {
            s1c = 1;
            if (DiploTributeDemand == 0 && Factions[f].energy_credits >= 125)
                DiploTributeDemand = round_down(Factions[f].energy_credits / 5, 25);
        }
    }
    if (a3) {
        if (!has_treaty(fw, f, 0x10)) {
            DiploTributeDemand = 0;
            DiploHostility = 0;
            goto L_553d24;
        }
    } else if (has_treaty(f, fw, 1) && !s20 && !has_agenda(f, fw, 0x2000) && !(GameRules & 0x1000) &&
               !DiploRivalWarCount &&
               ((DiploNearEndgame && DiploRivalOutranks) || DiploReputation > 4 || DiploFriction > 8)) {
        s1c = 1;
    }
    // DEMANDBRIBE: tribute.
    {
        int32_t demand = DiploTributeDemand;
        if (demand <= 0 || Factions[f].energy_credits < demand || s20) goto L_553d24;
        if ((DiploExposedBaseCount || DiploAttackChance) && DiploTradeWantTech2 >= 0) goto L_553d24;
        if (has_agenda(fw, f, 0x2008)) goto L_553d24;
        if (!DiploHostility && DiploConquestFear) goto L_553d24;
        int32_t w1 = DiploTradeWantTech1;
        bool talk;
        if (has_treaty(f, fw, 1) && w1 >= 0) {
            if (!DiploHostility) goto L_553d24;
            talk = !DiploCanBetray;
        } else {
            talk = DiploHostility && !DiploCanBetray;
        }
        if (!talk && !s38 && (!s1c || Factions[f].energy_credits < 125 || w1 >= 0)) goto L_553d24;
        if (!wants_to_speak(f, fw)) goto L_553d24;
        if (!has_treaty(fw, f, 0x11)) mention_prototypes(f, fw);
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_title(fw));
        says_n(3, get_name(fw));
        says_n(4, get_adjective(fw));
        says_n(5, get_pact(f));
        says_n(6, get_noun(fw));
        parse_num(0, DiploTributeDemand);
        int32_t small = DiploTributeDemand / 10 * 10;
        if (!small) small = 1;
        price = small;
        parse_num(1, small);
        clear();
        says("DEMANDBRIBE");
        s14 = 1;
        variant = game_random(0, 4);
        if (s1c) variant = 8;
        if (has_treaty(fw, f, 0x20) &&
            (DiploFearScale || Factions[fw].mil_strength_1 < Factions[f].mil_strength_1))
            variant = 9;
        if (great_satan(f, 0) && Factions[f].mil_strength_1 > Factions[fw].mil_strength_1 * 4 / 3 &&
            DiploPrevHostility == 2)
            variant = 7;
        if (has_treaty(fw, f, 1)) variant = 6;
        if (has_treaty(fw, f, 0x10)) variant = a3 ? 4 : 5;
        if (has_treaty(fw, f, 0x40)) variant = 10;
        if (!DiploHostility && !DiploPrevHostility) s48 = 1;
        s3c = 1;
        say_num(variant);
        switch (variant) {
        case 0:
            says_n(5, get_adjective_leader(f));
            says_n(6, get_insult_leader(fw));
            says_n(8, get_noun_phrase(fw, 11));
            says_n(9, get_phrase(fw, 12));
            break;
        case 1:
            says_n(8, get_noun_phrase(f, 9));
            says_n(9, get_phrase(fw, 13));
            break;
        case 2:
            says_n(6, get_noun_phrase(fw, 10));
            says_n(8, get_noun_phrase(f, has_treaty(fw, f, 0x20) ? 8 : 9));
            says_n(9, get_phrase(fw, 1));
            break;
        case 3:
            says_n(9, get_phrase(fw, 14));
            break;
        case 5:
            if (!DiploFearScale && Factions[fw].diplo_combat_count[f] > Factions[f].diplo_combat_count[fw]) {
                says("a");
            } else if (has_treaty(fw, f, 0x20)) {
                clear();
                says("DEMANDBRIBE9");
            } else {
                says("b");
            }
            break;
        case 6:
            says_n(9, get_noun_phrase(fw, 9));
            break;
        default:
            break;
        }
        r = pop(StrBuffer, fw);
        int32_t pay;
        if (r == 2) {
            int32_t e = small;
            if (variant == 6) {
                if (!game_random(0, 2)) goto pay_small;
                if (DiploHostility || (DiploNearEndgame && DiploRivalOutranks) || DiploExposedBaseCount ||
                    DiploAttackChance) {
                    pop("HALFBRIBE", fw);
                    goto L_553d0c;
                }
                goto pay_small;
            }
            {
                int32_t w = 0;
                if (great_satan(f, 0) || (DiploNearEndgame && DiploRivalOutranks) || DiploExposedBaseCount ||
                    DiploAttackChance || has_treaty(fw, f, 0x20)) {
                    w = has_treaty(fw, f, 0x400) ? 1 : game_random(0, 3);
                    if (game_random(0, 2) == 0) goto L_553d0c;
                }
                if (DiploHostility && (w || game_random(0, 2) == 0)) {
                    says_n(6, get_insult_leader(fw));
                    if (!pop("WEASELOUT", fw)) goto L_553d0c;
                    pay = DiploTributeDemand;
                    goto L_553eee;
                }
                parse_num(2, small);
                pop("WEASELEDOUT", fw);
                e = small;
            }
        pay_small:
            DiploTributeDemand = e;
            pay = e;
            goto L_553eee;
        }
        if (r == 0) goto L_553d0c;
        pay = DiploTributeDemand;
    L_553eee:
        s20 = 1;
        if (variant == 10 && Factions[f].diplo_gifts[fw]) Factions[f].diplo_gifts[fw]--;
        net_energy(f, -pay, fw, pay, 1);
        InfoWindowTechReset = 0;
        Console_update_data(&MainConsole, 0);
        diplomacy_caption(f, fw);
        net_treaty_on(fw, f, 0x5000000, 0);
        if (vendetta) net_treaty_on(fw, f, 4, 1);
        goto L_553d24;
    }
L_553d0c:
    net_treaty_off(fw, f, 0x1000000, 0);
    s38 = 1;
L_553d24:
    if (first_contact && !s20 && (DiploTradeWantTech1 >= 0 || s38) && !DiploCanBetray && !DiploFearScale &&
        (DiploPrevHostility ||
         ((DiploRivalStrongPacts || Factions[fw].ranking >= 6) && Factions[f].ranking >= 6 && !DiploRunawayLeader))) {
        int32_t d = (GameRules & 0x40) ? 5 : Factions[f].diff_level;
        if (DiploFriction + Factions[fw].AI_fight * 2 > (5 - d) / 2 + 10) {
            DiploHostility = 1;
            goto L_553fdd;
        }
    }
    if (!DiploHostility || s20) goto L_5540e7;
L_553fdd:
    if (s38 || DiploFearScale) goto L_5540e7;
    if ((DiploExposedBaseCount || DiploAttackChance) && DiploTradeWantTech2 >= 0) goto L_5540e7;
    if (DiploTributeDemand <= Factions[f].energy_credits) goto L_5540e7;
    if (!great_beelzebub(f, 0) &&
        (Factions[f].ranking < 6 || (!DiploRivalStrongPacts && Factions[fw].ranking < 6)))
        goto L_5540e7;
    {
        int32_t mult = (GameRules & 0x40) ? 2 : 3;
        if (!great_satan(f, 0)) mult++;
        int32_t val = tech_val(DiploTradeWantTech1, fw, 1) * ((CurrentTurn + 7) / 8) * mult;
        if (DiploTributeDemand > val / 2) {
            DiploTradeWantTech1 = -1;
            goto L_554a4c;
        }
    }
L_5540e7:
    // DEMANDTECH: faction_id_with wants a technology.
    {
        int32_t w = DiploTradeWantTech1;
        if (w < 0 || w == 9999 || s20 || s38 || (GameRules & 0x2000000)) goto L_554a4c;
        int32_t ac = DiploAttackChance;
        if (ac && DiploTradeWantTech2 < 0) goto L_554a4c;
        if (ac >= 2 && ac + Factions[fw].tech_ranking / 2 >= Factions[f].tech_ranking / 2) goto L_554a4c;
        if (has_agenda(fw, f, 0x2008)) goto L_554a4c;
        if (has_treaty(fw, f, 0x40) && DiploTradeWantTech2 < 0 && Factions[fw].ranking <= Factions[f].ranking)
            goto L_554a4c;
        if (!DiploHostility && DiploConquestFear) goto L_554a4c;
        if ((DiploFearScale || has_treaty(fw, f, 1)) && !s1c) goto L_554a4c;
        if (Factions[fw].tech_commerce_bonus > Factions[f].tech_commerce_bonus && FactionRankings[7] != f &&
            !s1c && !has_treaty(fw, f, 0x20) && (has_treaty(fw, f, 2) || first_contact))
            goto L_554a4c;
        if (!wants_to_speak(f, fw)) goto L_554a4c;
        if (!has_treaty(fw, f, 0x11)) mention_prototypes(f, fw);
        for (;;) {
            if (DiploTradeWantTech2 >= 0 && !DiploExposedBaseCount && !DiploAttackChance && DiploTradeDeclined &&
                !has_treaty(fw, f, 0x40))
                swap(&DiploTradeWantTech1, &DiploTradeWantTech2);
            say_tech_slot(0, DiploTradeWantTech1);
            says_n(1, get_title(f));
            says_n(2, get_name(f));
            says_n(3, get_title(fw));
            says_n(4, get_name(fw));
            says_n(5, get_pact(fw));
            if (DiploTradeWantTech2 >= 0) say_tech_slot(6, DiploTradeWantTech2);
            clear();
            says("DEMANDTECH");
            variant = game_random(0, 3);
            if (!DiploHostility) {
                if (!DiploPrevHostility) s48 = 1;
                variant = game_random(0, 3) + 9;
                if (has_treaty(fw, f, 0x20) && !s1c) variant = 8;
            }
            if (great_satan(f, 0) || s1c) variant = 7;
            if (has_treaty(fw, f, 0x10))
                variant = (Factions[fw].diplo_combat_count[f] <= Factions[f].diplo_combat_count[fw]) + 3;
            if (has_treaty(fw, f, 1)) variant = 5;
            if (has_treaty(fw, f, 0x40) && !has_treaty(fw, f, 0x20)) {
                int32_t gifts = Factions[f].diplo_gifts[fw];
                bool set = false;
                if (!gifts || gifts <= Factions[f].diplo_wrongs[fw]) {
                    if (DiploTradeWantTech3 >= 0) {
                        say_tech_slot(8, DiploTradeWantTech3);
                        variant = 14;
                    }
                    if (DiploTradeWantTech4 >= 0) {
                        say_tech_slot(9, DiploTradeWantTech4);
                        variant = 15;
                        set = true;
                    }
                }
                if (!set && variant < 14) variant = (DiploTradeWantTech2 < 0) + 12;
            }
            say_num(variant);
            bool menu = false;
            switch (variant) {
            case 0:
                says_n(9, get_noun_phrase(f, 8));
                break;
            case 1:
                says_n(9, get_phrase(f, 7));
                break;
            case 2:
                says_n(9, get_phrase(f, 6));
                break;
            case 9:
            case 10:
                says_n(9, get_noun_phrase(fw, 9));
                menu = true;
                break;
            default:
                menu = variant >= 9 && variant < 12;
                break;
            }
            if (!menu) {
                r = pop(StrBuffer, fw);
                if (r == 2) {
                    help_topic(14, DiploTradeWantTech1);
                    continue;
                }
            } else {
                int32_t owners = bit_count(TechOwners[DiploTradeWantTech1]) + 1;
                price = round_down((CurrentTurn + 7) / 8 * tech_val(DiploTradeWantTech1, f, 1) / owners + 24, 25);
                parse_num(0, price);
                ui_diplo_menu_start(StrBuffer, 0x100000);
                says("A");
                r = 0;
                if (!X_text_open(ScriptFileName, StrBuffer)) {
                    if (DiploTradeOfferTech1 >= 0) say_tech_slot(6, DiploTradeOfferTech1);
                    for (int32_t k = 0; k < 5; k++) {
                        text_get();
                        clear();
                        parse_string(TextBufferGetPtr, StrBuffer);
                        if ((k == 2 && price <= 0) || (k == 3 && DiploTradeOfferTech1 < 0)) continue;
                        ui_diplo_menu_item(StrBuffer, k);
                    }
                    r = ui_diplo_menu_exec(fw);
                }
                ui_diplo_menu_close();
            }
            if (r != 4) break;
            help_topic(14, DiploTradeWantTech1);
        }
        say_tech_slot(0, DiploTradeWantTech1);
        says_n(1, get_title(f));
        says_n(2, get_name(f));
        says_n(3, get_title(fw));
        says_n(4, get_name(fw));
        int32_t rr = r;
        if (rr == 2) {
            if (Factions[fw].energy_credits / 2 >= price &&
                tech_val(DiploTradeWantTech1, fw, 1) * ((CurrentTurn + 7) / 8) > price) {
                pop("AGREEDENERGY", fw);
                net_energy(fw, -price, f, price, 0);
                goto L_554e74;
            }
            if (pop("DEMANDTECHAGAIN1", fw)) goto L_554e74;
            rr = -1;
            goto L_5549ea;
        }
        if (rr == 3) {
            if (!DiploPrevHostility) {
                int32_t ov = tech_val(DiploTradeOfferTech1, fw, 1) * 2 / 3;
                if (ov <= tech_val(DiploTradeWantTech1, fw, 1)) {
                    pop("AGREEDTECH", fw);
                    net_tech(f, DiploTradeOfferTech1, fw, 0);
                    DiploTradeOfferTech1 = -1;
                    goto L_554e74;
                }
            }
            if (pop("DEMANDTECHAGAIN2", fw)) goto L_554e74;
            rr = -1;
            goto L_5549ea;
        }
        if (rr > 0) goto L_554e74;
    L_5549ea:
        net_treaty_off(fw, f, 0x1000000, 0);
        s38 = 1;
        if (has_treaty(fw, f, 1) && rr == 0) {
            cause_friction(fw, f, (DiploReputation * 2 + 3) / 5);
            Factions[fw].diplo_patience[f]++;
        }
        goto L_554a42;
    L_554e74:
        net_tech(fw, DiploTradeWantTech1, f, 1);
        if (variant >= 15) net_tech(fw, DiploTradeWantTech4, f, 1);
        if (variant >= 14) {
            net_tech(fw, DiploTradeWantTech3, f, 1);
            variant = 6;
        } else if (variant == 12) {
            variant = 6;
        } else if ((DiploExposedBaseCount || DiploAttackChance) && DiploHostility && DiploTradeWantTech2 >= 0) {
            for (bool again = false;; again = true) {
                if (again) help_topic(14, DiploTradeWantTech2);
                say_tech_slot(0, DiploTradeWantTech1);
                says_n(1, get_title(f));
                says_n(2, get_name(f));
                says_n(3, get_title(fw));
                says_n(4, get_name(fw));
                variant = 6;
                clear();
                says("DEMANDTECH");
                say_num(6);
                rr = pop(StrBuffer, fw);
                if (rr <= 1) break;
            }
        }
        if (rr == 0) {
            s38 = 1;
            net_treaty_off(fw, f, 0x1000000, 0);
            goto L_554a42;
        }
        DiploTradeWantTech1 = DiploTradeWantTech2;
        DiploTradeWantTech2 = -1;
        if (variant == 6) {
            net_tech(fw, DiploTradeWantTech1, f, 1);
            DiploTradeWantTech1 = -1;
        }
        net_treaty_on(fw, f, 0x5000000, 0);
        if (!has_treaty(fw, f, 0x20)) DiploCanBetray = 0;
        s20 = 1;
        if (vendetta) net_treaty_on(fw, f, 4, 1);
        if (rr == 1) {
            if (Factions[fw].diplo_patience[f]) Factions[fw].diplo_patience[f]--;
            if (variant >= 9 && variant < 12) {
                cause_friction(fw, f, -(tech_val(DiploTradeWantTech1, fw, 1) / 2));
                int32_t& gifts = Factions[f].diplo_gifts[fw];
                if (!gifts || gifts < Factions[f].integrity_blemishes) gifts++;
            }
            if (variant >= 12 && Factions[f].diplo_gifts[fw]) Factions[f].diplo_gifts[fw]--;
        }
    L_554a42:
        diplomacy_caption(f, fw);
    }
L_554a4c:
    if (has_treaty(f, fw, 1)) DiploRivalWarCount++;
    if (!crt_strcmp(MFactions[fw].filename, "MORGAN")) DiploRivalWarCount++;
    // ASKFORLOAN: faction_id_with asks faction_id for a loan.
    {
        Faction& F = Factions[f];
        Faction& W = Factions[fw];
        if (F.energy_credits <= W.energy_credits || F.energy_credits < 200 || DiploReputation >= 4) goto L_5551f7;
        if (F.energy_credits < W.energy_credits * 2 && (!has_treaty(f, fw, 1) || F.ranking <= W.ranking))
            goto L_5551f7;
        if (DiploHostility && !s20) goto L_5551f7;
        if (s14) goto L_5551f7;
        if (!s20 && s38 && !s48 && !has_treaty(f, fw, 1)) goto L_5551f7;
        if (has_treaty(f, fw, 0x10) || !has_treaty(f, fw, 0x10003) || has_treaty(fw, f, 0x20)) goto L_5551f7;
        if (F.loan_balance[fw] || W.loan_balance[f]) goto L_5551f7;
        if (W.energy_credits >= (DiploRivalWarCount * 5 + 5) * 20 &&
            (DiploHelpFaction <= 0 || W.energy_credits >= Factions[DiploHelpFaction].energy_credits * 3 / 4))
            goto L_5551f7;
        if (!wants_to_speak(f, fw)) goto L_5551f7;
        int32_t loan = F.energy_credits / 2;
        int32_t cap = (W.energy_surplus_total - W.facility_maint_total) * 100;
        if (loan >= cap) loan = cap;
        s1c = loan;
        if (loan < 50) goto L_5551f7;
        int32_t rate = (W.integrity_blemishes + (F.tech_commerce_bonus - W.tech_commerce_bonus) * 2 + 10) * 5;
        if (has_treaty(f, fw, 1)) rate -= 10;
        if (rate < 10) rate = 10;
        int32_t term = (10 - W.integrity_blemishes) * 10;
        if (term < 20) term = 20;
        s14 = rate + 100;
        int32_t total = s14 * loan / 100;
        if (total < term) term = total;
        int32_t payment = (total + term - 1) / (term < 1 ? 1 : term);
        int32_t balance = term * payment;
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_title(fw));
        says_n(3, get_name(fw));
        parse_num(0, loan);
        parse_num(1, payment);
        parse_num(2, term);
        clear();
        says("ASKFORLOAN");
        say_num((payment != 1) + 1);
        r = pop(StrBuffer, fw);
        if (r == 0) {
            if (has_treaty(f, fw, 1)) cause_friction(fw, f, 1);
            goto L_5551f7;
        }
        if (r == 2) {
            loan /= 2;
            s1c = loan;
            total = s14 * loan / 100;
            if (total < term) term = total;
            payment = (total + term - 1) / (term < 1 ? 1 : term);
            balance = term * payment;
        }
        net_treaty_on(fw, f, 0x5000000, 0);
        net_energy(f, -loan, fw, loan, 1);
        net_loan(fw, f, balance, payment);
        cause_friction(fw, f, -energy_value(loan));
        s20 = 1;
    }
L_5551f7:
    // SOCIAL: faction_id_with's social priority (a pact ends or a vendetta begins).
    if (!a3 && !s20 &&
        (DiploRivalStrongPacts || !DiploFearScale || has_treaty(fw, f, 1) || has_agenda(fw, f, 0x400)) &&
        has_agenda(fw, f, 1) && !has_agenda(f, fw, 0x2000)) {
        const MFaction& m = MFactions[fw];
        int32_t cat = m.soc_priority_category;
        if (cat >= 0 && m.soc_priority_model) {
            int32_t cur = (&Factions[f].SE_Politics_pending)[cat];
            if (cur && cur != m.soc_priority_model) {
                wants_to_speak(f, fw);
                if (!DiploCaptionOff) diplomacy_caption(f, fw);
                says_n(0, get_title(f));
                says_n(1, get_name(f));
                says_n(2, get_pact_hood(f, fw));
                says_n(3, get_noun(fw));
                clear();
                says("SOCIAL");
                say_num(m.soc_priority_category);
                says("CAT");
                say_num((&Factions[f].SE_Politics_pending)[m.soc_priority_category]);
                says(has_treaty(f, fw, 1) ? "PACT" : "WAR");
                say_num(m.soc_priority_model);
                pop(StrBuffer, fw);
                if (has_treaty(f, fw, 1)) {
                    diplo_lock(0);
                    net_pact_ends(fw, f, 1);
                    diplo_unlock();
                } else {
                    net_set_treaty(fw, f, 0x40, 1, 1);
                    net_treaty_on(fw, f, 0x10, 1);
                }
                Factions[f].diplo_spoke[fw] = CurrentTurn;
                goto END;
            }
        }
    }
    {
        int32_t gifts = Factions[f].diplo_gifts[fw];
        gift = (gifts && gifts > Factions[f].diplo_wrongs[fw] && !has_treaty(fw, f, 0x50) &&
                !has_agenda(fw, f, 0x2008) && !has_treaty(fw, f, 0x40020))
                   ? 1
                   : 0;
    }
    if (has_treaty(fw, f, 1)) {
        if (s20) goto L_55565d;
        if (a3 || has_agenda(fw, f, 0x2000)) goto L_55564d;
        if (!DiploHostility) {
            const Faction& F = Factions[f];
            const Faction& W = Factions[fw];
            if (F.ranking < 6 || W.ranking < 6) {
                if (DiploFriction >= (W.tech_commerce_bonus - F.tech_commerce_bonus) * 2 + 10) {
                    if (W.ranking >= 5 || !(GameRules & 0x1000) || CurrentTurn < 200) goto L_555562;
                }
                if (!DiploNearEndgame || !DiploRivalOutranks || (GameRules & 0x1000)) goto L_55564d;
            }
        }
    L_555562:
        if (gift) {
            DiploHostility = 0;
            Factions[f].diplo_gifts[fw]--;
            goto L_55564d;
        }
        // BREAKPACT
        wants_to_speak(f, fw);
        if (!DiploCaptionOff) diplomacy_caption(f, fw);
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_pacts_2(f, fw));
        says_n(3, get_noun_phrase(f, 8));
        pop("BREAKPACT", fw);
        diplo_lock(0);
        net_pact_ends(fw, f, 1);
        diplo_unlock();
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        goto END;
    }
    if (s20) goto L_55565d;
L_55564d:
    if (has_treaty(f, fw, 1)) {
    L_55565d:
        DiploHostility = 0;
    } else if (DiploHostility) {
        if (gift) {
            DiploHostility = 0;
            Factions[f].diplo_gifts[fw]--;
        } else if (Factions[f].ranking > Factions[fw].ranking &&
                   DiploCapturedBases > Factions[f].integrity_blemishes + DiploRivalStrongPacts && !DiploAttackChance &&
                   !DiploExposedBaseCount && has_treaty(fw, f, 0x10) && !has_agenda(fw, f, 8) &&
                   has_treaty(fw, f, 0x2000)) {
            goto L_555c42;
        }
    }
    // COLLABORATOR / ALIENHATRED
    if (DiploEnemyAllyCount && !has_treaty(f, fw, 0x10) &&
        ((DiploHostility && DiploHelpFaction) || (alien(fw) && alien(DiploEnemyAlly)))) {
        wants_to_speak(f, fw);
        net_set_treaty(fw, f, 0x40, 1, 1);
        net_treaty_on(fw, f, 0x10, 1);
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        diplomacy_caption(f, fw);
        clear();
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_title(DiploEnemyAlly));
        says_n(3, get_name(DiploEnemyAlly));
        pop(alien(fw) && alien(DiploEnemyAlly) ? "ALIENHATRED" : "COLLABORATOR", fw);
        goto END;
    }
    // BULLY
    if (DiploHostility && s38 &&
        (has_treaty(f, fw, 6) || (!has_treaty(fw, f, 0x10) && DiploSpokeTurn > 0 && !has_treaty(fw, f, 0x2000)))) {
        wants_to_speak(f, fw);
        net_treaty_on(fw, f, 0x10, 1);
        diplomacy_caption(f, fw);
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        clear();
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_adjective_insult_faction(f));
        says_n(3, get_adjective_leader(f));
        says_n(4, get_phrase(fw, 13));
        says_n(5, get_noun_phrase(f, 10));
        says("BULLY");
        say_num(game_random(0, 7));
        pop(StrBuffer, fw);
        goto END;
    }
L_550522:
    declare = (DiploSpokeTurn < 0 || a3 || has_treaty(fw, f, 0x2000) || has_agenda(fw, f, 4)) ? 1 : 0;
    // VENDETTA declared.
    if (DiploHostility) {
        if (!s38 && !declare) goto END;
        if (DiploTributeDemand >= 0) wants_to_speak(f, fw);
        int32_t had_truce = has_treaty(fw, f, 0x10);
        net_treaty_on(fw, f, 0x10, 1);
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        diplomacy_caption(f, fw);
        clear();
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_adjective_insult_leader(f));
        says_n(3, get_adjective_insult_faction(f));
        says_n(4, get_noun_phrase(f, 8));
        says_n(5, get_phrase(f, 3));
        says_n(6, get_phrase(fw, 13));
        says_n(8, get_phrase(f, 4));
        says_n(9, get_insult_leader(f));
        says("VENDETTA");
        int32_t n;
        if (s38 && !had_truce) n = game_random(0, 3);
        else n = game_random(0, 6) + 3;
        if (great_satan(f, 0) && game_random(0, 3)) n = game_random(0, 4) + 15;
        if (declare && DiploTributeDemand >= 0) n = 14;
        if (has_treaty(fw, f, 0x40000) && has_treaty(fw, f, 0x20)) n = game_random(0, 5) + 9;
        say_num(n);
        if (n == 5 && !crt_strcmp(MFactions[f].filename, "CYBORG")) says("X");
        if (has_agenda(fw, f, 4)) {
            clear();
            says("ATROCIOUS");
        }
        pop(StrBuffer, fw);
        goto END;
    }
L_555c42:
    if (vendetta && s20) net_treaty_on(fw, f, 4, 1);
    // SWEARAPACT: faction_id_with offers a pact.
    if ((has_treaty(fw, f, 2) || (DiploAcceptedFaction && !has_treaty(fw, f, 0x10))) && !has_treaty(fw, f, 0x21) &&
        !has_agenda(fw, f, 0x2000) && !DiploGreatSatan) {
        s1c = 0;
        s14 = 0;
        bool skip = false;
        for (int32_t i = 1; i < 8 && !skip; i++) {
            if (!alive(i) || !has_treaty(fw, i, 0x10)) continue;
            if (has_treaty(f, i, 0x14)) s14 = 1;
            if (has_treaty(f, i, 1)) {
                skip = true;
                break;
            }
            for (int32_t g = 1; g < 128; g++) {
                if (!bad_reg(g) && Factions[fw].region_total_bases[g] && Factions[i].region_total_bases[g]) {
                    s1c++;
                    break;
                }
            }
        }
        if (skip || great_satan(f, 0) || has_agenda(fw, f, 0x400) || s3c) goto L_556113;
        const Faction& F = Factions[f];
        const Faction& W = Factions[fw];
        int32_t hive = !crt_strcmp(MFactions[fw].filename, "HIVE");
        int32_t t = range(W.tech_commerce_bonus - F.tech_commerce_bonus, 0, 99) + hive + ((GameRules >> 12) & 1) +
                    (DiploAcceptedFaction != 0) + 3;
        if (DiploReputation >= t) goto L_556113;
        s3c = F.diplo_reputation <= 3 ? F.diplo_reputation : 3;
        int32_t fl = ((GameRules & 0x1000) && !(GameRules & 0x40)) ? 1 : 0;
        int32_t u = range(W.tech_commerce_bonus - F.tech_commerce_bonus + 1, 0, 99) +
                    !crt_strcmp(MFactions[fw].filename, "HIVE");
        int32_t lim = (s14 ? 11 : 6) + fl + u + (DiploAcceptedFaction != 0);
        if (DiploReputation - s3c + DiploFriction >= lim) goto L_556113;
        if (s1c && !DiploPlayerPactCount && !DiploRivalStrongPacts) {
            if (!DiploFearScale && !s14 && F.ranking > 5 && W.ranking > 5) goto L_556113;
        } else if (F.ranking > 5 || W.ranking > 5) {
            goto L_556113;
        }
        if (!wants_to_speak(f, fw)) goto L_556113;
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_pact_hood(f, fw));
        if (pop("SWEARAPACT", fw)) goto L_556113;
        if (DiploHelpFaction) {
            says_n(5, get_title(f));
            says_n(6, get_noun(DiploHelpFaction));
            if (pop("GIMMEHELP", fw)) {
                diplo_lock(0);
                net_double_cross(f, DiploHelpFaction, fw, 0);
                net_treaty_off(f, fw, 0x800000, 0);
                net_set_treaty(f, DiploHelpFaction, 0x40, 1, 1);
                diplo_unlock();
                DiploHelpFaction = 0;
                DiploEnemyAllyCount = 0;
            } else if (!DiploFearScale) {
                says_n(2, get_pacts_2(f, fw));
                pop("REJPACTNOHELP", fw);
                s34 = 1;
                goto L_556113;
            } else if (!s34) {
                s34 = 1;
                goto L_556113;
            }
        }
        pact_of_brotherhood(f, fw);
        diplomacy_caption(f, fw);
        s34 = 1;
    }
L_556113:
    // RANSOMTREATY: a base of faction_id_with's captured by faction_id for a treaty.
    if (has_treaty(fw, f, 0x10) && DiploFearScale && DiploReputation <= 2 && !has_treaty(fw, f, 0x40000) &&
        !has_treaty(fw, f, 0x2000000) && !has_agenda(fw, f, 0x2000) && DiploFearScale > DiploAlterFear + 2 &&
        !MultiplayerActive && wants_to_speak(f, fw)) {
        int32_t b = -1, best = 999;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id == fw && (uint8_t)Bases[i].faction_id_former == f &&
                Bases[i].pop_size < best) {
                best = Bases[i].pop_size;
                b = i;
            }
        }
        if (b >= 0 && wants_to_speak(f, fw)) {
            base_id = b;
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            says_n(2, Bases[b].name);
            if (pop("RANSOMTREATY", fw) == 0) {
                s34 = 1;
                net_cede_base(b, f, 1);
                net_set_treaty(fw, f, 0x2000000, 1, 0);
                pledge_truce(f, fw, 0);
                make_treaty(f, fw);
                diplomacy_caption(f, fw);
            }
        }
    }
    // Truce.
    if (has_treaty(fw, f, 0x10) && wants_to_speak(f, fw)) {
        int32_t ans;
        if (DiploCanSurrender &&
            (has_agenda(fw, f, 0x800) ||
             (has_treaty(fw, f, 0x20) && game_random(0, has_treaty(fw, f, 0x400) ? 2 : 3) == 0))) {
            ans = 1;
            goto L_5564d7;
        }
        if (s20) {
            ans = pop("OFFERTRUCE", fw);
        } else if (DiploHostility) {
            ans = pop("MUSTTRUCE", fw);
        } else {
            mention_prototypes(f, fw);
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            says_n(2, get_title(fw));
            says_n(3, get_name(fw));
            clear();
            int32_t n = game_random(0, 4);
            if ((n == 2 && has_treaty(fw, f, 0x20)) || n >= 3) {
                if (alien(f)) says("ALIEN");
                says("FACTIONTRUCE");
                ans = X_pops_13(MFactions[fw].filename, StrBuffer, 0x100000, portrait(fw), kPopWait);
            } else {
                says("WANTTOTRUCE");
                say_num(n);
                ans = pop(StrBuffer, fw);
            }
        }
    L_5564d7:
        s1c = ans;
        {
            int32_t k = DiploReputation + 2;
            if (has_treaty(fw, f, 0x20)) k *= 2;
            int32_t lim = range(k, 0, Factions[fw].base_count + 1);
            if (ans == 0) goto L_556b38;
            if (game_random(0, lim) >= DiploFearScale && !DiploCanSurrender) goto L_556c2a;
        }
        if (DiploTradeOfferTech1 >= 0 && DiploFearScale >= tech_val(DiploTradeOfferTech1, fw, 1) / 2 &&
            !DiploCanSurrender && !DiploCanBetray && !(GameRules & 0x2000000)) {
            say_tech_slot(0, DiploTradeOfferTech1);
            int32_t t = pop("TECHTRUCE", fw);
            while (t > 1) {
                help_topic(14, DiploTradeOfferTech1);
                say_tech_slot(0, DiploTradeOfferTech1);
                t = pop("TECHTRUCE", fw);
            }
            if (t == 0) {
                s1c = 0;
                net_tech(f, DiploTradeOfferTech1, fw, 1);
                s54 = 1;
                goto L_556723;
            }
        }
        if ((DiploTradeOfferTech1 < 0 || DiploFearScale < 4) && !DiploCanSurrender && !DiploCanBetray) {
            int32_t a = DiploFearScale * 100;
            if (a >= Factions[fw].energy_credits) a = Factions[fw].energy_credits;
            a = round_down(a, 25);
            if (a) {
                parse_num(0, a);
                if (pop("ENERGYTRUCE", fw) == 0) {
                    s1c = 0;
                    s54 = 1;
                    net_energy(fw, -a, f, a, 1);
                    InfoWindowTechReset = 0;
                    Console_update_data(&MainConsole, 0);
                }
            }
        }
    L_556723: {
        const Faction& F = Factions[f];
        const Faction& W = Factions[fw];
        int32_t mine = (F.mil_strength_2 + W.total_combat_units) *
                           (F.diplo_combat_total[fw] + F.diplo_combat_count[fw]) / 4 + 1;
        int32_t theirs = W.diplo_combat_total[f] + W.diplo_combat_count[f] + 1;
        for (int32_t g = 0; g < 128; g++) {
            if (!W.region_total_bases[g]) {
                mine += F.region_force_rating[g] >> 1;
            } else {
                theirs += W.region_total_combat_units[g] + W.region_force_rating[g];
                mine += F.region_force_rating[g];
                for (int32_t i = 1; i < 8; i++) {
                    if (has_treaty(i, fw, 1) || !has_treaty(i, f, 0x10)) theirs += Factions[i].region_force_rating[g];
                }
            }
        }
        if (s1c == 0) goto L_556b2c;
        if (alien(fw)) goto L_556c2a;
        if (!DiploCanSurrender) {
            int32_t bc = W.base_count;
            if (DiploFearScale < bc) goto L_556c2a;
            if (bc > 1) {
                if (bc > mine / theirs || bc > 4 || W.mil_strength_1 >= F.mil_strength_1) goto L_556c2a;
            }
        }
    }
        // TRUCEPLEASE: surrender.
        {
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            parse_num(0, Factions[fw].energy_credits);
            int32_t cnt = 0;
            for (int32_t t = 0; t < 89; t++)
                if (has_tech(t, fw) && !has_tech(t, f)) cnt++;
            parse_num(1, cnt);
            if (pop("TRUCEPLEASE", fw)) {
                net_set_agenda(fw, f, 8, 1, 0);
                net_treaty_on(fw, f, 0x20, 1);
                goto L_556c2a;
            }
            s54 = 1;
            if (!(GameRules & 0x2000000)) {
                for (int32_t t = 0; t < 89; t++)
                    if (has_tech(t, fw) && !has_tech(t, f)) net_tech(f, t, fw, 0);
            }
            int32_t e = Factions[fw].energy_credits;
            net_energy(f, e, fw, -e, 1);
            InfoWindowTechReset = 0;
            Console_update_data(&MainConsole, 0);
            Factions[f].diplo_friction[fw] = 0;
            net_treaty_off(fw, f, 0x20, 1);
            net_treaty_on(f, fw, 2, 0);
            net_set_treaty(fw, f, 0x2000000, 1, 0);
            pact_of_brotherhood(f, fw);
            diplomacy_caption(f, fw);
            if (!MultiplayerActive && (GameRules & 2)) {
                int32_t n = 0, pacts = 0;
                for (int32_t i = 1; i < 8; i++) {
                    if (!alive(i)) continue;
                    n++;
                    if (has_treaty(i, CurrentPlayerFaction, 1) && has_treaty(i, CurrentPlayerFaction, 0x2000000)) {
                        n--;
                    } else if (has_treaty(i, CurrentPlayerFaction, 1) && (GameRules & 0x1000) && pacts < 2) {
                        pacts++;
                    }
                }
                if (n - pacts < 2) {
                    GameVictoryType = 4;
                    GameState |= 0x2001;
                    if (pacts) GameVictoryType = 5;
                }
            }
        }
    L_556b2c:
        DiploSpokeTurn = CurrentTurn;
    L_556b38:
        s34 = 1;
        if (!has_treaty(f, fw, 1)) pledge_truce(f, fw, 1);
        diplomacy_caption(f, fw);
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        if (DiploReputation > 5) pop("NOTORIETY", fw);
        if (Factions[f].loan_balance[fw]) {
            parse_num(0, Factions[f].loan_payment[fw]);
            parse_num(1, Factions[f].loan_balance[fw]);
            pop("TRUCEMEPAY", fw);
        } else if (Factions[fw].loan_balance[f]) {
            parse_num(0, Factions[fw].loan_payment[f]);
            pop("TRUCEYOUPAY", fw);
        }
        goto L_556cde;
    L_556c2a:
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_adjective_insult_faction(f));
        pop("MOREWAR", fw);
        Factions[f].diplo_spoke[fw] = CurrentTurn;
        goto END;
    }
L_556cde:
    if (first_contact && !has_treaty(fw, f, 0x10)) net_treaty_on(fw, f, 0x10004, 1);
    if (DiploReputation > 5 && has_treaty(fw, f, 0x20) &&
        (DiploRivalPactCount || DiploGreatSatan || Factions[fw].mil_strength_1 > Factions[f].mil_strength_1))
        DiploCanBetray = 1;
    // Treaty offer.
    if (!has_treaty(f, fw, 2) && !has_agenda(f, fw, 0x2000)) {
        s14 = (vendetta && !has_treaty(f, fw, 4)) ? 1 : 0;
        const Faction& F = Factions[f];
        const Faction& W = Factions[fw];
        int32_t odds = (F.mil_strength_1 + 1) * (4 - W.AI_fight) * 2 / (W.mil_strength_1 + 1);
        if (!crt_strcmp(MFactions[fw].filename, "GAIANS")) odds += 2;
        if (s14) odds += 3 - W.AI_fight;
        if (F.base_count && W.tech_commerce_bonus >= F.tech_commerce_bonus) {
            odds += F.energy_surplus_total / F.base_count;
            if (W.tech_commerce_bonus > F.tech_commerce_bonus) odds++;
        }
        int32_t lim = odds;
        if (!great_satan(f, 0) && (F.ranking <= 4 || W.ranking <= 4)) {
            lim = odds + 1;
            if (!DiploRivalStrongPacts) lim++;
        }
        if (!DiploFearScale && DiploFriction > lim) goto L_5571e7;
        if (DiploGreatSatan) goto L_5571e7;
        int32_t g = s14 ? 1 : !crt_strcmp(MFactions[fw].filename, "GAIANS");
        if (DiploReputation + DiploCapturedBases >=
            range(W.tech_commerce_bonus - F.tech_commerce_bonus, 0, 99) + g + (DiploFearScale != 0) + 5)
            goto L_5571e7;
        if (has_treaty(f, fw, 0x20) && (!DiploFearScale || !s14 || DiploReputation > 4)) goto L_5571e7;
        if (!wants_to_speak(f, fw)) goto L_5571e7;
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_adjective_faction(f));
        says_n(3, get_name(fw));
        says_n(4, get_title(fw));
        int32_t ans;
        clear();
        if (!vendetta) {
            if (alien(f)) says("ALIEN");
            says("FACTIONTREATY");
            ans = X_pops_13(MFactions[fw].filename, StrBuffer, 0x100000, portrait(fw), kPopWait);
        } else {
            says("OFFERTREATY");
            if (!has_treaty(f, fw, 4)) says("2");
            ans = pop(StrBuffer, fw);
        }
        s1c = ans;
        if (ans && !has_treaty(fw, f, 0x20)) {
            int32_t a = DiploFearScale * 50;
            if (Factions[fw].energy_credits < a) a = Factions[fw].energy_credits;
            a = round_down(a, 25);
            if (a) {
                says_n(0, get_title(fw));
                says_n(1, get_name(fw));
                says_n(2, get_noun_phrase(f, 9));
                parse_num(0, a);
                if (pop("ENERGYTREATY", fw) == 0) {
                    s54 = 1;
                    s1c = 0;
                    net_energy(fw, -a, f, a, 1);
                    InfoWindowTechReset = 0;
                    Console_update_data(&MainConsole, 0);
                }
            }
        }
        s34 = 1;
        if (!s1c) {
            make_treaty(f, fw);
            diplomacy_caption(f, fw);
        }
    }
L_5571e7:
    if (first_contact) wants_to_speak(f, fw);
    if ((!DiploCaptionOff || !DiploCaptionMode) && !a3) goto END;
    // Greetings.
    if (s34) {
        if (has_treaty(fw, f, 0x60) && (a3 || DiploReputation >= 5)) goto L_55778a;
        int32_t w = -1, best = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (i == f || i == fw || has_treaty(f, i, 0x10)) continue;
            if (Factions[f].diplo_wrongs[i] > best) {
                w = i;
                best = Factions[f].diplo_wrongs[i];
            }
        }
        if (w < 0) goto L_55778a;
        says_n(0, get_title(w));
        says_n(1, get_name(w));
        says_n(2, get_noun(w));
        says_n(3, get_him_her(w, 0));
        says_n(4, get_title(f));
        says_n(5, get_name(f));
        Factions[f].diplo_wrongs[w] = 0;
        clear();
        says("WRONGED");
        if (has_treaty(fw, w, 0x10)) says("ENEMY");
        else says(Factions[fw].mil_strength_1 > Factions[f].mil_strength_1 ? "MIGHTY" : "MEEK");
        pop(StrBuffer, fw);
        mention_prototypes(f, fw);
    } else if (s38 && !s20) {
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_pacts_2(f, fw));
        says_n(3, get_pact(f));
        if (s48) {
            if (has_treaty(fw, f, 1)) {
                pop("MILDPACT", fw);
            } else if (has_treaty(f, fw, 2)) {
                pop("MILDTREATY", fw);
            } else {
                pop("MILD", fw);
                mention_prototypes(f, fw);
            }
        } else if (has_treaty(fw, f, 1)) {
            pop("REBUFFEDPACT", fw);
        } else {
            pop(has_treaty(f, fw, 6) ? "REBUFFEDTREATY" : "REBUFFED", fw);
            mention_prototypes(f, fw);
        }
    } else if (a3) {
        if (Factions[fw].diplo_patience[f] > 1) {
            if (has_treaty(fw, f, 1)) {
                says_n(0, get_pact(f));
                pop("LOWPATIENCEPACT", fw);
            } else {
                says_n(0, get_title(f));
                pop("LOWPATIENCE", fw);
            }
        } else {
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            if (has_treaty(fw, f, 1)) pop("WHATSUPPACT", fw);
            else if (has_treaty(fw, f, 2)) pop("WHATSUPTREATY", fw);
            else pop("WHATSUP", fw);
        }
    } else {
        says_n(0, get_pact_hood(f, fw));
        says_n(1, get_title(f));
        says_n(2, get_name(f));
        says_n(3, get_noun_phrase(fw, 9));
        if (has_treaty(fw, f, 1)) {
            pop("HELLOPACT", fw);
        } else if (has_treaty(fw, f, 2)) {
            pop("HELLOTREATY", fw);
        } else {
            pop("HELLO", fw);
            mention_prototypes(f, fw);
        }
    }
L_55778a:
    // Social priorities.
    DiploSocConflict = 0;
    {
        const MFaction& m = MFactions[fw];
        if (m.soc_priority_category >= 0 && m.soc_priority_model) {
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            clear();
            says("SOCIAL");
            say_num(m.soc_priority_category);
            says("CAT");
            int32_t cur = (&Factions[f].SE_Politics_pending)[m.soc_priority_category];
            if (cur == m.soc_priority_model) {
                say_num(m.soc_priority_model);
                says("GOOD");
                pop(StrBuffer, fw);
            } else if (cur) {
                DiploSocConflict = 1;
                if (!DiploFearScale && (!has_treaty(fw, f, 1) || !has_treaty(fw, f, 0x2000000))) {
                    say_num(cur);
                    says("BAD");
                    say_num(m.soc_priority_model);
                    pop(StrBuffer, fw);
                    net_set_agenda(fw, f, 1, 1, 1);
                }
            }
        }
    }
    if (!DiploSocConflict) net_set_agenda(fw, f, 1, 0, 1);
    // Atrocities.
    {
        Faction& F = Factions[f];
        if (F.atrocities && (!has_treaty(fw, f, 0x200000) || has_treaty(fw, f, 0x400000)) &&
            F.atrocities > Factions[fw].AI_fight + 1 && !alien(fw) &&
            (!has_treaty(fw, f, 1) || !has_treaty(fw, f, 0x2000000))) {
            int32_t i = 1;
            for (; i < 8; i++)
                if (i != f && i != fw && has_treaty(i, f, 0x40000)) break;
            says_n(0, get_title(f));
            says_n(1, get_name(f));
            if (i < 8) {
                says_n(2, get_noun(i));
                pop("COMMITTEDATROCITY", fw);
            } else {
                pop("SELFATROCITY", fw);
            }
            if (!has_treaty(fw, f, 0x200000) && F.atrocities > 1) {
                F.atrocities--;
                if (F.integrity_blemishes > 1) F.integrity_blemishes--;
            }
            net_set_treaty(fw, f, 0x200000, 1, 0);
            net_set_treaty(fw, f, 0x400000, 0, 0);
        }
    }
    if (has_treaty(f, fw, 1)) {
        says_n(0, get_pact(f));
        net_maps(f, fw, 1);
        if (DiploMapMsg) pop("HERESTHEMAP", fw);
    }
    // Reports on the allies' wars.
    if ((has_treaty(f, fw, 1) || has_treaty(fw, f, 0x80)) && !(flags & 0x40)) {
        int32_t max_mil = 0, min_mil = 9999, even = 0, losing = 0, strongest = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (!alive(i) || i == f || i == fw) continue;
            if (!has_treaty(fw, i, 0x10) || !has_treaty(fw, i, 8) || !has_treaty(fw, i, 0x8000) ||
                !has_treaty(f, i, 8))
                continue;
            if (!has_treaty(fw, i, 0x40) && !has_treaty(fw, f, 1)) continue;
            int32_t mil = Factions[i].mil_strength_1;
            if (mil > max_mil) {
                max_mil = mil;
                strongest = i;
            }
            bool found = false;
            if (mil < min_mil) {
                for (int32_t g = 1; g < 64; g++) {
                    if (!bad_reg(g) && Factions[fw].region_total_bases[g] && Factions[i].region_total_bases[g] &&
                        Factions[i].region_force_rating[g] <=
                            Factions[fw].region_total_combat_units[g] + Factions[fw].region_force_rating[g] &&
                        contiguous(fw, i, g, 0)) {
                        found = true;
                        break;
                    }
                }
            }
            if (found) {
                losing = i;
                min_mil = mil;
            } else if (losing != i && strongest != i) {
                even = i;
            }
        }
        s3c = 0;
        if (strongest) {
            for (int32_t g = 1; g < 128; g++) {
                if (!bad_reg(g) && (DiploRegionFactions[g] & (1 << fw)) &&
                    Factions[strongest].region_force_rating[g] > Factions[fw].region_total_combat_units[g]) {
                    s3c = 1;
                    break;
                }
            }
            if (has_treaty(f, fw, 0x2000) || s3c) {
                says_n(1, get_title(strongest));
                says_n(2, get_name(strongest));
                says_n(3, get_noun(strongest));
                says_n(4, get_his_her(strongest, 0));
                says_n(5, get_adjective_insult_faction(strongest));
                pop("REPORTLOSING", fw);
                goto L_558152;
            }
        }
        if (losing && !has_treaty(f, fw, 0x2000)) {
            for (int32_t g = 1; g < 128; g++) {
                // BUG(original): the faction bit of DiploRegionFactions is not tested (1 << losing only).
                if (!bad_reg(g) && DiploRegionFactions[g] &&
                    Factions[fw].region_force_rating[g] > Factions[losing].region_total_combat_units[g]) {
                    says_n(0, get_adjective(losing));
                    says_n(1, get_title(losing));
                    says_n(2, get_name(losing));
                    says_n(3, get_phrase(losing, 5));
                    pop("REPORTWINNING", fw);
                    goto L_558152;
                }
            }
        }
        if (even) {
            for (int32_t g = 1; g < 128; g++) {
                if (!bad_reg(g) && Factions[even].region_total_bases[g] && Factions[fw].region_total_bases[g]) {
                    says_n(1, get_title(even));
                    says_n(2, get_name(even));
                    says_n(3, get_noun(even));
                    says_n(4, get_adjective(even));
                    says_n(5, get_noun_phrase(even, 8));
                    pop("REPORTEVEN", fw);
                    break;
                }
            }
        }
    }
L_558152:
    if (has_treaty(f, fw, 1)) mention_prototypes(f, fw);
    // TEAMTECH: a common research goal.
    if (has_treaty(f, fw, 1) && !(GameRules & 0x200) && Factions[fw].tech_research_id >= 0 &&
        !has_tech(Factions[fw].tech_research_id, f) &&
        Factions[fw].tech_research_id != Factions[f].tech_research_id && !(GameRules & 0x6000000) &&
        Factions[f].tech_accumulated >= tech_rate(f) / 2) {
        int32_t best = -1, best_val = 0;
        for (int32_t t = 0; t < 89; t++) {
            if (has_tech(t, f) || t == Factions[f].tech_research_id || t == Factions[fw].tech_research_id) continue;
            if (!tech_avail(t, f)) continue;
            if ((t - Factions[f].tech_ranking + f + 2) % 3 == 0) continue;
            if (!has_tech(Tech[t].preq_tech1, f) || !has_tech(Tech[t].preq_tech2, f)) continue;
            int32_t v = tech_val(t, fw, 0);
            if (v > best_val) {
                best_val = v;
                best = t;
            }
        }
        if (best >= 0) {
            int32_t off, mask;
            int32_t goal = Factions[fw].tech_research_id;
            bitmask(goal, &off, &mask);
            Factions[fw].tech_pact_shared_goals[off] |= mask;
            parse_say(0, Tech[goal].name, -1, -1);
            bitmask(best, &off, &mask);
            Factions[f].tech_pact_shared_goals[off] |= mask;
            parse_say(1, Tech[best].name, -1, -1);
            pop("TEAMTECH", fw);
        }
    }
    if (!has_treaty(fw, f, 3) && territory(fw, f, 0, &base_id, 0)) {
        spot_base(base_id, f);
        MapWin_focus(MapWins[0], Bases[base_id].x, Bases[base_id].y);
        says_n(1, get_adjective(fw));
        pop("TERRITORY", fw);
        mention_prototypes(f, fw);
    }
    if (DiploEnemyAllyCount) {
        says_n(0, get_title(DiploEnemyAlly));
        says_n(1, get_name(DiploEnemyAlly));
        says_n(2, get_adjective_insult_leader(DiploEnemyAlly));
        pop("QUISLING", fw);
    }
    DiploMenuFlagA = 0;
    DiploMenuFlagB = 0;
    {
        int8_t& pat = Factions[fw].diplo_patience[f];
        for (;;) {
            if (pat < 0) pat = 0;
            DiploCommonEnemy = -1;
            DiploPeaceFaction = -1;
            for (int32_t i = 1; i < 8; i++) {
                if (!alive(i) || i == f || i == fw) continue;
                if (!has_treaty(fw, i, 0x10) || !has_treaty(f, i, 8)) continue;
                if (has_treaty(f, i, 7)) DiploPeaceFaction = i;
                if (has_treaty(f, i, 0x10)) DiploCommonEnemy = i;
            }
            if (MultiplayerActive && (GameState & 0x800)) break;
            int32_t choice = diplomacy_menu(f, fw);
            switch (choice) {
            case 0:
                goto END;
            case 1:
            case 2:
                make_a_proposal(f, fw, choice == 1);
                if (DiploCounterProposal == 6) DiploMenuFlagA = 0;
                break;
            case 3:
                if (!pop("REALLYBROKE", fw)) {
                    says_n(0, get_pact_hood(f, fw));
                    pop("DROIDS", fw);
                    pat--;
                    break;
                }
                diplo_lock(0);
                Factions[f].integrity_blemishes++;
                net_treaty_off(fw, f, 1, 1);
                diplomacy_check(f, fw, 0);
                diplomacy_caption(f, fw);
                says_n(0, get_pact(f));
                says_n(1, get_title(f));
                says_n(2, get_name(f));
                pop("BROKEPACT", fw);
                net_pact_ends(f, fw, 1);
                diplo_unlock();
                break;
            case 4:
                demand_withdrawal(f, fw);
                break;
            case 5:
                battle_plans(f, fw);
                break;
            case 6:
                call_off_vendetta(f, fw);
                break;
            case 7:
                DiploCurrentProposal = 12;
                DiploCounterProposal = 1;
                make_a_proposal(f, fw, 1);
                DiploMenuFlagA = 0;
                break;
            default:
                break;
            }
            pat++;
            if (pat >= get_patience(f, fw)) break;
        }
    }
    diplomacy_caption(f, fw);
    if (!has_treaty(f, fw, 0x10)) {
        says_n(0, get_title(f));
        says_n(1, get_name(f));
        says_n(2, get_name(fw));
        pop(has_treaty(fw, f, 1) ? "TIMESUPALLY" : "TIMESUP", fw);
    }
END:
    net_treaty_off(fw, f, 0x2000, 0);
    net_agenda_off(fw, f, 4, 0);
    net_set_agenda(fw, f, 0x10, (DiploAttackChance && !has_treaty(f, fw, 0x11)) ? 1 : 0, 0);
    net_set_treaty(fw, f, 0x400, (DiploCanBetray && !has_treaty(f, fw, 0x11)) ? 1 : 0, 1);
    if (s54 || (DiploHostility && !has_treaty(f, fw, 0x10))) {
        int8_t& pat = Factions[fw].diplo_patience[f];
        int32_t lo = get_patience(f, fw);
        pat = (int8_t)range(pat, lo, 999);
    }
    diplomacy_ends(f, fw);
    if (vendetta && !has_treaty(f, fw, 0x10013) && !a3) {
        says_n(0, get_title(fw));
        says_n(1, get_name(fw));
        says_n(2, get_noun(fw));
        pop(has_treaty(f, fw, 4) ? "RENEWTRUCE" : "ENDTRUCE", fw);
    }
    return 0;
}
