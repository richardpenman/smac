#include "engine/diplo_act.h"

#include <cstddef>
#include <cstdlib>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/clock.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/zoc.h"

void GAMEAPI bitmask(int32_t bit, int32_t* offset, int32_t* mask) {
    *offset = bit / 8;
    *mask = 1 << (bit & 7);
}

int32_t GAMEAPI bit_count(uint32_t value) {
    int32_t count = 0;
    for (; value; value >>= 1) count += value & 1;
    return count;
}

void GAMEAPI clock_wait(int32_t msec) {
    // Delay with the message pump running. In deterministic mode the oracle's fast clock advances
    // 1000 ms per read: the loop runs once per started second.
    uint32_t start = engine_is_deterministic() ? 0 : engine_real_time();
    uint32_t now = start;
    do {
        do_all_draws();
        do_non_input();
        now = engine_is_deterministic() ? now + 1000 : engine_real_time();
    } while (now - start < (uint32_t)msec);
}

// ---- intervention, double cross ------------------------------------------------------------------

namespace {
// "<title> <name> of the <noun>" slots of a faction, with the gender/plural flags the original sets
// before each parse_says.
void say_title(int32_t n, int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = MFactions[faction_id].is_leader_female;
    parse_says(n, MFactions[faction_id].title_leader, -1, -1);
}
void say_name(int32_t n, int32_t faction_id) {
    GenderDefault = MFactions[faction_id].is_leader_female;
    PluralDefault = 0;
    parse_says(n, MFactions[faction_id].name_leader, -1, -1);
}
void say_noun(int32_t n, int32_t faction_id) {
    GenderDefault = MFactions[faction_id].noun_gender;
    PluralDefault = MFactions[faction_id].is_noun_plural;
    parse_says(n, MFactions[faction_id].noun_faction, -1, -1);
}
inline bool is_human_bit(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1 << faction_id)) != 0; }
}  // namespace

void GAMEAPI intervention(int32_t faction_id, int32_t faction_id_with) {
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || i == faction_id_with || !(FactionStatus[1] & (uint8_t)(1 << i))) continue;
        if (!(Factions[i].diplo_status[faction_id] & DIPLO_PACT) || (Factions[i].diplo_status[faction_id_with] & 0x11))
            continue;
        say_noun(0, i);
        say_noun(1, faction_id);
        GenderDefault = MFactions[i].is_leader_female;
        PluralDefault = 0;
        parse_says(2, MFactions[i].title_leader, -1, -1);
        GenderDefault = MFactions[i].is_leader_female;
        PluralDefault = 0;
        parse_says(3, MFactions[i].name_leader, -1, -1);
        say_title(4, faction_id_with);
        say_name(5, faction_id_with);
        say_noun(6, faction_id_with);
        bool human_with = is_human_bit(faction_id_with);
        if (human_with || is_human_bit(faction_id)) {
            if (human_with) Factions[faction_id_with].diplo_spoke[i] = CurrentTurn;
            treaty_on(i, faction_id_with, 0x80000018);
            set_treaty(i, faction_id_with, 0x40, 1);
        }
        if (faction_id_with == CurrentPlayerFaction) {
            X_pop("AIDSTHEM", 0);
        } else if (faction_id == CurrentPlayerFaction) {
            X_pop("AIDSUS", 0);
        }
    }
}

void GAMEAPI double_cross(int32_t faction_id, int32_t faction_id_with, int32_t a3) {
    if (MultiplayerActive) {
        if (faction_id_with == CurrentPlayerFaction && a3 > 0) {
            say_title(0, a3);
            say_name(1, a3);
            say_noun(2, a3);
            say_title(3, faction_id);
            say_name(4, faction_id);
            say_noun(5, faction_id);
            NetMsg_pop(&NetMsg, "INCITED", 5000, 0, nullptr);
        } else if (faction_id_with == CurrentPlayerFaction && is_human_bit(faction_id)) {
            say_title(0, faction_id);
            say_name(1, faction_id);
            say_noun(2, faction_id);
            NetMsg_pop(&NetMsg, "VENDETTAWARNING", 5000, 0, nullptr);
        }
    }
    Faction& f = Factions[faction_id];
    Faction& w = Factions[faction_id_with];
    w.loan_balance[faction_id] = 0;
    uint32_t status = f.diplo_status[faction_id_with];
    // The victim had it coming (an atrocity) or is an atrocity maker: no reputation loss.
    bool excused = (status & 0x40000) || w.major_atrocities != 0;
    bool same_treaty_broken = false;  // a3's treaty with faction_id is not shared with the victim
    if (a3 >= 0) {
        uint32_t third = f.diplo_status[a3];
        if (third & DIPLO_PACT) {
            same_treaty_broken = !(status & DIPLO_PACT);
        } else if (third & DIPLO_TREATY) {
            same_treaty_broken = !(status & DIPLO_TREATY);
        } else if (third & DIPLO_TRUCE) {
            same_treaty_broken = !(status & DIPLO_TRUCE);
        }
        f.diplo_gifts[a3]++;
    }
    status = f.diplo_status[faction_id_with];
    if (status & DIPLO_PACT) {
        if (!(f.diplo_agenda[faction_id_with] & 0x20)) {
            f.diplo_reputation = f.diplo_reputation - 1 < 0 ? 0 : f.diplo_reputation - 1;
            if (f.diff_level && !excused && !same_treaty_broken) {
                f.integrity_blemishes++;
                w.diplo_betrayed[faction_id]++;
            }
            if (a3 < 0) {
                if (!excused) f.integrity_blemishes++;
                f.diplo_wrongs[faction_id_with]++;
                w.diplo_betrayed[faction_id]++;
            } else {
                cause_friction(a3, faction_id, -4);
            }
            if (is_human_bit(faction_id)) {
                if (w.diplo_agenda[faction_id] & 0x400) set_agenda(faction_id_with, faction_id, 0x800, 1);
                if (w.diplo_status[faction_id] & 0x20) set_agenda(faction_id_with, faction_id, 0x400, 1);
                set_treaty(faction_id_with, faction_id, 0x20, 1);
            }
        }
        pact_ends(faction_id, faction_id_with);
        treaty_on(faction_id, faction_id_with, DIPLO_VENDETTA);
        return;
    }
    if (!(f.diplo_agenda[faction_id_with] & 0x20)) {
        bool human_part = false;
        if (status & (DIPLO_TREATY | DIPLO_TRUCE)) {
            if (!(status & 0x10000)) {
                f.diplo_reputation = f.diplo_reputation - 1 < 0 ? 0 : f.diplo_reputation - 1;
                if ((great_satan(faction_id, 0) || (f.ranking >= 6 && w.ranking <= 4)) && a3 < 0 && !excused) {
                    f.integrity_blemishes++;
                    w.diplo_betrayed[faction_id]++;
                }
            }
            if (f.diff_level && !excused && !same_treaty_broken) {
                f.integrity_blemishes++;
                w.diplo_betrayed[faction_id]++;
            }
            if (a3 < 0) f.diplo_wrongs[faction_id_with]++;
            human_part = true;
        } else if (a3 >= 0) {
            human_part = true;
        }
        if (human_part) {
            if (is_human_bit(faction_id)) {
                if (w.diplo_agenda[faction_id] & 0x400) set_agenda(faction_id_with, faction_id, 0x800, 1);
                if (w.diplo_status[faction_id] & 0x20) set_agenda(faction_id_with, faction_id, 0x400, 1);
                set_treaty(faction_id_with, faction_id, 0x20, 1);
            }
            if (a3 >= 0) cause_friction(a3, faction_id, -2);
        }
    }
    treaty_on(faction_id, faction_id_with, DIPLO_VENDETTA);
    if (!(f.diplo_agenda[faction_id_with] & 0x20)) {
        // BUG(original): compares the two factions' human bits in place (different bit positions),
        // so this holds whenever either faction is human.
        if ((FactionStatus[0] & (uint8_t)(1 << faction_id)) != (FactionStatus[0] & (uint8_t)(1 << faction_id_with))) {
            set_treaty(faction_id, faction_id_with, 0x40, 1);
            f.diplo_spoke[faction_id_with] = CurrentTurn;
        }
        intervention(faction_id_with, faction_id);
    }
}

// ---- capture_base ----------------------------------------------------------------------------------

namespace {
// Name of Facility[id] as the original reads it: the "survive project" message indexes the table
// with the (negative) queue item. Native builds cannot follow the original's pointer there and use
// the project's name (only reached when the viewer captures a base).
const char* facility_name_raw(int32_t id) {
#ifdef SMACX_HYBRID
    return *reinterpret_cast<char* const*>(reinterpret_cast<const uint8_t*>(&Facility[0]) + id * 0x30);
#else
    return Facility[id < 0 ? -id : id].name;
#endif
}

// Original address of Bases[base_id].name (interlude takes it as an integer argument).
inline int32_t base_name_addr(int32_t base_id) {
    return (int32_t)(0x97D040 + base_id * (int32_t)sizeof(BASE) + (int32_t)offsetof(BASE, name));
}

// Free facilities of a faction's bonuses (FCB_FREEFAC, FCB_FREEFAC_PREQ when faction_id has the
// facility's prerequisite) in CurrentBaseID.
void capture_free_facilities(int32_t bonus_faction, int32_t faction_id) {
    MFaction& m = MFactions[bonus_faction];
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        int32_t id = m.faction_bonus_id[i];
        int32_t fac = m.faction_bonus_val1[i];
        if (id == 2 || (id == 12 && has_tech(Facility[fac].preq_tech, faction_id))) {
            if (fac < 1) fac = 1;
            else if (fac > 69) fac = 69;
            set_fac(fac, CurrentBaseID, 1);
        }
    }
}
}  // namespace

void GAMEAPI capture_base(int32_t base_id, int32_t faction_id, int32_t is_probe) {
    log_say("Capture Base", Bases[base_id].name, base_id, faction_id, 0);
    const int32_t old = (uint8_t)Bases[base_id].faction_id;
    const int32_t y = Bases[base_id].y;
    const int32_t liberated = (uint8_t)Bases[base_id].faction_id_former == (uint32_t)faction_id;
    const int32_t x = Bases[base_id].x;
    const int32_t region = map_tile(x, y)->region;
    int32_t pop = Bases[base_id].pop_size;
    Factions[faction_id].diplo_combat_count[old] += pop * 2;
    Factions[faction_id].diplo_combat_total[old] += pop * 2;
    char name[25];
    crt_strcpy(name, Bases[base_id].name);
    treaty_off(faction_id, old, 0x40);
    const uint32_t new_bit = 1u << faction_id;
    if ((new_bit & FactionStatus[0]) || (FactionStatus[0] & (1u << old))) treaty_on(faction_id, old, 0x2000);
    for (int32_t i = 1; i < 8; i++) {
        if (i != faction_id && i != old && (Factions[old].diplo_status[i] & DIPLO_PACT) &&
            !(Factions[i].diplo_status[faction_id] & DIPLO_VENDETTA)) {
            treaty_on(old, i, 0x2000);
        }
    }
    if (faction_id == CurrentPlayerFaction) {
        // A secret project under construction here.
        int32_t item = Bases[base_id].queue_items[0];
        if (item <= -70 && SecretProjects[(item < 0 ? -item : item) - 70] == -1) {
            int32_t i = 0;
            for (; i < BaseCount; i++) {
                if (i != base_id && (uint8_t)Bases[i].faction_id == old &&
                    Bases[i].queue_items[0] == Bases[base_id].queue_items[0]) {
                    break;
                }
            }
            if (i < BaseCount) {
                int32_t other = Bases[i].queue_items[0];
                parse_says(0, MFactions[old].adj_name_faction, -1, -1);
                // BUG(original): indexes Facility with the negative queue item.
                parse_say(1, facility_name_raw(other), -1, -1);
                popp(ScriptFileName, "SURVIVEPROJECT", 0, "secproj_sm.pcx", 0);
            } else {
                say_title(0, old);
                say_name(1, old);
                say_noun(2, old);
                item = Bases[base_id].queue_items[0];
                parse_say(3, Facility[item < 0 ? -item : item].name, -1, -1);
                popp(ScriptFileName, "HALTPROJECT", 0, "secproj_sm.pcx", 0);
                item = Bases[base_id].queue_items[0];
                int32_t offset, mask;
                bitmask((item < 0 ? -item : item) - 70, &offset, &mask);
                // BUG(original): the byte offset of the bit indexes the dword array.
                Factions[old].secret_project_intel[offset] &= ~(uint32_t)mask;
            }
        }
    }
    if (faction_id == CurrentPlayerFaction || old == CurrentPlayerFaction) {
        for (int32_t i = 0; i < 64; i++) {
            if (SecretProjects[i] != base_id) continue;
            parse_say(0, Facility[70 + i].name, -1, -1);
            if (faction_id == CurrentPlayerFaction) {
                popp(ScriptFileName, "SEIZEPROJECT", 0, "secproj_sm.pcx", 0);
            } else {
                popp(ScriptFileName, "LOSEPROJECT", 0, "secproj_sm.pcx", 0);
            }
        }
    }
    // Energy reserves taken: the base's share of the owner's population.
    int32_t loot;
    {
        Faction& o = Factions[(uint8_t)Bases[base_id].faction_id];
        loot = o.energy_credits <= 0 ? 0 : Bases[base_id].pop_size * o.energy_credits / (o.pop_total + 1);
    }
    // The headquarters escapes to the best remaining base if the owner can pay 1000 credits.
    if (base_has_fac(base_id, FAC_HEADQUARTERS) && Factions[old].energy_credits >= 1000) {
        int32_t best = -1, best_score = 9999;
        for (int32_t i = 0; i < BaseCount; i++) {
            if (i == base_id || (uint8_t)Bases[i].faction_id != old) continue;
            int32_t ix = Bases[i].x, iy = Bases[i].y;
            uint32_t ireg = map_tile(ix, iy)->region;
            int32_t sum = 1;
            for (int32_t j = 0; j < BaseCount; j++) {
                if (j == i || j == base_id || (uint8_t)Bases[j].faction_id != old) continue;
                int32_t d = veh_vector_dist(ix, iy, Bases[j].x, Bases[j].y);
                if (map_tile(Bases[j].x, Bases[j].y)->region != ireg) d *= 2;
                sum += d;
            }
            int32_t score = (sum << 8) / (Factions[old].region_total_bases[ireg] + Bases[i].pop_size * 2);
            if (score < best_score) {
                best_score = score;
                best = i;
            }
        }
        // BUG(original): base 0 never receives the headquarters (tests best > 0).
        if (best > 0) {
            parse_says(0, name, -1, -1);
            parse_says(1, Bases[best].name, -1, -1);
            if (MultiplayerActive || old != CurrentPlayerFaction || X_pop("ESCAPE", 0)) {
                Factions[old].energy_credits -= 1000;
                set_fac(FAC_HEADQUARTERS, best, 1);
                set_fac(FAC_HEADQUARTERS, base_id, 0);
                if (old != CurrentPlayerFaction) {
                    say_noun(2, old);
                    NetMsg_pop(&NetMsg, "ESCAPED", 5000, 0, nullptr);
                } else if (MultiplayerActive) {
                    NetMsg_pop(&NetMsg, "ESCAPED2", 5000, 0, nullptr);
                }
                if (!(FactionStatus[0] & (uint8_t)(1 << old))) base_reset(best, 0);
            }
        }
    }
    // Capturing the headquarters foils the owner's Corner the Global Energy Market.
    {
        Faction& o = Factions[old];
        if (base_has_fac(base_id, FAC_HEADQUARTERS) && CurrentTurn < o.corner_market_turn && o.corner_market_cost > 0) {
            o.energy_credits += o.corner_market_cost;
            loot += o.corner_market_cost / 2;
            o.corner_market_cost = 0;
            o.corner_market_turn = 0;
            if (old == CurrentPlayerFaction) {
                popp(ScriptFileName, "CORNERFOILED", 0, "econwin_sm.pcx", 0);
            } else {
                parse_says(0, MFactions[old].adj_name_faction, -1, -1);
                popp(ScriptFileName, "CORNERTHEMFOILED", 0, "econwin_sm.pcx", 0);
            }
        }
    }
    Factions[faction_id].energy_credits += loot;
    {
        int32_t credits = Factions[old].energy_credits - loot;
        if (credits < 0) credits = 0;
        else if (credits > 9999999) credits = 9999999;
        Factions[old].energy_credits = credits;
    }
    BASE& base = Bases[base_id];
    if (!is_probe && !liberated && !(base.state_flags & 0x40000) && !is_objective(base_id) &&
        (base.pop_size > 1 || (FactionStatus[0] & base.visibility) || MultiplayerActive) && --base.pop_size == 0) {
        // A size 1 base is destroyed.
        base_kill(base_id);
        draw_map(1);
        base_id = -1;
    } else if (base_id >= 0) {
        base.faction_id = (int8_t)faction_id;
        for (int32_t i = 1; i < 8; i++)
            if (Factions[i].base_id_attack_target == base_id) Factions[i].base_id_attack_target = -1;
        base.nerve_staple_turns_left = 0;
        if ((uint8_t)base.faction_id_former == (uint32_t)faction_id) {
            base.assimilation_turns_left = 0;
        } else {
            base.faction_id_former = (int8_t)old;
            base.assimilation_turns_left = 50;
        }
        Factions[faction_id].last_base_turn = CurrentTurn;
        Factions[old].base_count--;
        Factions[faction_id].base_count++;
        replay_base(REPLAY_CAPTURE, x, y, faction_id);
        base.minerals_accumulated = 0;
        base.minerals_accumulated_2 = 0;
        if (base.queue_size >= 0) {
            for (int32_t i = 0; i <= base.queue_size; i++)
                if (base.queue_items[i] >= 0) Factions[old].units_queue[base.queue_items[i]]--;
        }
        base.queue_size = 0;
        base.production_id_last = -69;
        base.queue_items[0] = -69;
        // The governor settings of the new owner's largest base (headquarters count double).
        int32_t best = -1, best_value = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            if (base_id == i || (uint8_t)Bases[i].faction_id != (uint32_t)faction_id) continue;
            int32_t value = Bases[i].pop_size;
            if (base_has_fac(i, FAC_HEADQUARTERS)) value *= 2;
            if (value > best_value) {
                best_value = value;
                best = i;
            }
        }
        if (best >= 0) base.governor_flags = Bases[best].governor_flags;
        base.state_flags &= 0xFFDEF7B1;
        if (!is_probe && !liberated) {
            for (int32_t fac = 1; fac < 70; fac++) {
                if (fac != 26 && game_rand() % 3 == 0) set_fac(fac, base_id, 0);
            }
        }
        set_fac(FAC_HEADQUARTERS, base_id, 0);
        if (!liberated) {
            set_fac(6, base_id, 0);
            set_fac(3, base_id, 0);
        }
        for (int32_t i = 0; i < 21; i++) {
            int32_t tx = map_wrap_x(TableOffsetX[i] + x);
            int32_t ty = TableOffsetY[i] + y;
            if (!map_in_bounds(tx, ty)) continue;
            using_set(tx, ty, faction_id);
            map_tile(tx, ty)->visibility |= (uint8_t)(1 << faction_id);
            synch_bit(tx, ty, faction_id);
            int32_t veh_id = veh_at(tx, ty);
            if (veh_id >= 0 && Vehs[veh_id].faction_id == (uint32_t)faction_id) {
                for (; veh_id >= 0; veh_id = Vehs[veh_id].next_veh_id_stack) Vehs[veh_id].state &= ~0x40000u;
            }
        }
        spot_base(base_id, old);
        reset_territory();
        // The old owner's units supported by the base move their home or are disbanded.
        for (int32_t i = VehCount - 1; i >= 0; i--) {
            if (i >= VehCount || Vehs[i].faction_id != (uint32_t)old || Vehs[i].home_base_id != base_id) continue;
            Bases[base_id].faction_id = (int8_t)0xFF;
            int32_t home = base_find_2(x, y, old);
            Bases[base_id].faction_id = (int8_t)faction_id;
            if (home >= 0) {
                int32_t dx = std::abs(x - Vehs[i].x);
                if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
                int32_t dy = std::abs(y - Vehs[i].y);
                if ((dx + dy) & ~1) {
                    Vehs[i].home_base_id = (int16_t)home;
                    continue;
                }
            }
            kill(i);
        }
        capture_free_facilities(faction_id, faction_id);
        // BUG(original): the old owner's free facilities too (with the new owner's techs).
        capture_free_facilities(old, faction_id);
        base_first(base_id);
    }
    owner_set(x, y, faction_id);
    draw_map(1);
    GameDrawState |= 2;
    GraphicWin_redraw(&WorldWindow);
    say_noun(0, faction_id);
    parse_says(1, name, -1, -1);
    say_noun(2, old);
    parse_num(0, loot);
    char msg[256];
    msg[0] = 0;
    crt_strcat(msg, liberated ? "LIBERATE" : "SEIZE");
    crt_strcat(msg, "BASE");
    const uint32_t old_bit = 1u << old;
    if ((FactionStatus[0] & old_bit) && !(new_bit & FactionStatus[0]) && base_id >= 0 &&
        !(Bases[base_id].state_flags & 0x80) && !liberated) {
        Bases[base_id].state_flags |= 0x10000;
    }
    if (faction_id != CurrentPlayerFaction && old != CurrentPlayerFaction) {
        draw_tile_fixup(x, y, 1, 2);
        const Faction& viewer = Factions[CurrentPlayerFaction];
        const char* pcx = nullptr;
        if ((viewer.diplo_status[faction_id] & DIPLO_PACT) ||
            ((Factions[faction_id].diplo_status[CurrentPlayerFaction] & 0x80) &&
             (Factions[faction_id].diplo_status[old] & 0x40))) {
            crt_strcat(msg, "2");
            pcx = "liberate_sm.pcx";
        } else if ((viewer.diplo_status[old] & DIPLO_PACT) ||
                   ((Factions[old].diplo_status[CurrentPlayerFaction] & 0x80) &&
                    (Factions[old].diplo_status[faction_id] & 0x40))) {
            crt_strcat(msg, "3");
            pcx = "seized_sm.pcx";
        } else if (spying(faction_id) || spying(old)) {
            crt_strcat(msg, "4");
            pcx = "seized_sm.pcx";
        }
        if (pcx) popp(ScriptFileName, msg, 0, pcx, 0);
    } else {
        if (!Console_focus(&MainConsole, x, y, CurrentPlayerFaction)) draw_tile_fixup(x, y, 1, 2);
        Console_update_data(&MainConsole, 0);
        popp(ScriptFileName, msg, 0, faction_id == CurrentPlayerFaction ? "liberate_sm.pcx" : "seized_sm.pcx", 0);
        ambience(0xD5);
    }
    // Capture between aliens and humans: half the population flees as colony pods of the old owner.
    if (((MFactions[old].rule_flags ^ MFactions[faction_id].rule_flags) & RFLAG_ALIEN) && base_id >= 0 &&
        Bases[base_id].pop_size > 1) {
        int32_t refugees = Bases[base_id].pop_size / 2;
        say_noun(0, faction_id);
        parse_says(1, name, -1, -1);
        parse_num(0, refugees);
        Bases[base_id].pop_size = 1;
        draw_tile(Bases[base_id].x, Bases[base_id].y, 2);
        if (MFactions[faction_id].rule_flags & RFLAG_ALIEN) {
            if (faction_id == CurrentPlayerFaction) {
                if (refugees == 1) popp(ScriptFileName, "HOMELESSONE", 0, "humanref_sm.pcx", 0);
                else if (refugees) popp(ScriptFileName, "HOMELESS", 0, "humanref_sm.pcx", 0);
                interlude(0x25, base_name_addr(base_id), 1, 0);
            }
            if (old == CurrentPlayerFaction) {
                if (refugees == 1) popp(ScriptFileName, "HOMELESSONE", 0, "humanref_sm.pcx", 0);
                else if (refugees) popp(ScriptFileName, "HOMELESS", 0, "humanref_sm.pcx", 0);
                interlude(0x18, base_name_addr(base_id), 1, 0);
            }
        } else {
            if (faction_id == CurrentPlayerFaction) {
                if (refugees == 1) popp(ScriptFileName, "HOMELESSONE", 0, "alienref_sm.pcx", 0);
                else if (refugees) popp(ScriptFileName, "HOMELESS", 0, "alienref_sm.pcx", 0);
                interlude(0x19, base_name_addr(base_id), 1, 0);
            }
            if (old == CurrentPlayerFaction) {
                if (refugees == 1) popp(ScriptFileName, "HOMELESSONE", 0, "alienref_sm.pcx", 0);
                else if (refugees) popp(ScriptFileName, "HOMELESS", 0, "alienref_sm.pcx", 0);
            }
        }
        for (int32_t i = 1; i < 21; i++) {
            int32_t k = i < 9 ? game_rand() % 8 + 1 : i;
            int32_t tx = map_wrap_x(TableOffsetX[k] + Bases[base_id].x);
            int32_t ty = TableOffsetY[k] + Bases[base_id].y;
            if (!map_in_bounds(tx, ty)) continue;
            MAP* sq = map_tile(tx, ty);
            if ((sq->items & 3) && (sq->val2 & 0xF) < 8 && (sq->val2 & 0xF) != old) continue;
            veh_init((sq->climate & 0xE0) < 0x60 ? 0x15 : 0, old, tx, ty);
            draw_tile(tx, ty, 2);
            if (--refugees == 0) break;
        }
    }
    // Technology: spoils of war, or a share of the techs the new owner lacks as research.
    if (!(GameRules & 0x4000) && !(MFactions[faction_id].rule_flags & 0x10)) {
        int32_t count = 0;
        for (int32_t t = 0; t < 89; t++)
            if (has_tech(t, old) && !has_tech(t, faction_id)) count++;
        int32_t mult = base_id < 0 ? 1 : Bases[base_id].pop_size + 1;
        Factions[faction_id].tech_accumulated += (Factions[faction_id].tech_ranking + 3) * count * mult / 8;
    } else {
        steal_tech(faction_id, old, 1);
        if (base_id >= 0 && SecretProjects[20] == base_id) steal_tech(faction_id, old, 1);
    }
    // BUG(original): passes the faction as the region and the region as the faction.
    if (!(old_bit & FactionStatus[0])) bases_reset(old, region, 0);
    mon_conquer_base(faction_id, name);
    if (faction_id == CurrentPlayerFaction && base_id >= 0 && (Bases[base_id].state_flags & 0x40000) &&
        !(GamePreferences & 0x40000) && !MultiplayerActive) {
        // The viewer renames a captured objective base after its assistant city.
        parse_says(6, MFactions[CurrentPlayerFaction].assistant_city, -1, -1);
        char* base_name = Bases[base_id].name;
        parse_says(5, base_name, -1, -1);
        parse_says(7, MFactions[old].adj_name_faction, -1, -1);
        int32_t best_pop = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id || i == base_id) continue;
            if (base_has_fac(i, FAC_HEADQUARTERS)) {
                parse_says(2, Bases[i].name, -1, -1);
                break;
            }
            if (Bases[i].pop_size > best_pop) {
                best_pop = Bases[i].pop_size;
                parse_says(2, Bases[i].name, -1, -1);
            }
        }
        int32_t near = base_find_2(x, y, old);
        int32_t label;
        if (near < 0) {
            label = 403;
        } else {
            int32_t nx = Bases[near].x, ny = Bases[near].y;
            int32_t dx = std::abs(nx - x);
            int32_t dy = std::abs(ny - y);
            if (dx * 3 / 2 < dy) {
                label = y > ny ? 403 : 405;
            } else {
                int32_t d = x - nx;
                if (d > MapAreaX / 2) d -= MapAreaX;
                if (d < -(MapAreaX / 2)) d += MapAreaX;
                label = d > 0 ? 406 : 404;
            }
        }
        parse_says(4, label_get(label), -1, -1);
        crt_strcpy(base_name, MFactions[CurrentPlayerFaction].assistant_city);
        make_base_unique(base_id);
        interlude(7, 0, 1, 1);
        draw_map(1);
        text_close();
    }
    eliminate_player(old, faction_id);
    if (base_id >= 0) {
        set_base(base_id);
        base_compute(1);
        if (faction_id == CurrentPlayerFaction) {
            if (Console_ready_search(&MainConsole, -1) < 0) {
                GameState |= 4;
                return;
            }
            BaseWin_zoom(&BaseWin, base_id, 0);
        }
    }
}

// ---- small wrappers (0x50B8D0-0x50B9E0) and mandate_color --------------------------------------

int32_t GAMEAPI POP3_2(const char* label, int32_t faction_id, int32_t x, int32_t y) {
    return POP3(label, 0, faction_id, x, y);
}

int32_t GAMEAPI veh_stack_next(int32_t veh_id) { return veh_id >= 0 ? Vehs[veh_id].next_veh_id_stack : veh_id; }

void GAMEAPI say_stats_3_2(int32_t unit_id) { say_stats_3(StrBuffer, unit_id); }

int32_t GAMEAPI NetMsg_pop_3(const char* label, const char* pcx) { return NetMsg_pop(&NetMsg, label, 5000, 0, pcx); }

int32_t GAMEAPI mandate_color(int32_t mandate) {
    if (mandate == 0) return MandateColors[0];
    if (mandate == 1) return MandateColors[2];
    if (mandate == 2) return MandateColors[4];
    return MandateColors[6];
}
