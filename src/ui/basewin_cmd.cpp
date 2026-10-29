// Base window commands (basewin.h): the clicks on the window's panes and buttons and what they do
// to the base: worked tiles, specialists, facilities (scrap), hurry, the governor, nerve staple,
// the queue. Each follows the original function's order of engine calls.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/veh.h"
#include "ui/basewin_internal.h"
#include "ui/tutwin.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/win.h"

static_assert(offsetof(Faction, energy_credits) == 0x220, "Faction layout");
static_assert(offsetof(Faction, hurry_cost_total) == 0x224, "Faction layout");
static_assert(offsetof(Faction, AI_fight) == 0x3DC, "Faction layout");
static_assert(offsetof(Faction, base_governor_adv) == 0x1D0, "Faction layout");
static_assert(offsetof(MFaction, rule_hurry) == 0x4F4, "MFaction layout");
static_assert(offsetof(MFaction, rule_flags) == 0x508, "MFaction layout");

namespace {

uint8_t* self_win() { return &BaseWin; }

// Script file of the base window's popups (DAT_009B8AA8 / the ScriptFileName pointer 0x691B0C).
const char* script() { return ScriptFileName; }

bool alien(int32_t faction_id) { return (MFactions[faction_id].rule_flags & 0x80) != 0; }

// "set base, recompute (1), redraw, redraw the main interface" of the commands' ends.
void recompute_redraw(uint8_t* self) {
    if (Win_is_visible(self)) {
        set_base(base_win().base);
        base_compute(1);
        bw_redraw();
        GraphicWin_redraw(&MainInterfaceWin);
    }
}

// The specialist type of citizen slot i (4 bits per slot for the first 16).
int32_t specialist_type(int32_t i) {
    if (i < 0x10) return (cur_base().specialist_types[i >> 3] >> ((i & 7) << 2)) & 0xF;
    return best_specialist();
}
void set_specialist_type(int32_t i, int32_t type) {
    if (i >= 0x10) return;
    int32_t sh = (i & 7) << 2;
    cur_base().specialist_types[i >> 3] &= ~(0xF << sh);
    cur_base().specialist_types[i >> 3] |= type << sh;
}

}  // namespace

// ==== resource map ================================================================================

int32_t THISCALL BaseWin_farm_get(uint8_t* self, int32_t x, int32_t y) {
    const UiRect& m = base_win().rect_map;
    x += (0xC0 - (m.right - m.left) / 2) - m.left;
    if (x < 0 || x >= 0x180) return -1;
    y -= m.top;
    if (y < 0 || y >= 0xC0) return -1;
    int32_t dx = (x / 0x60) * 2 - 3, dy = (y / 0x30) * 2 - 3;
    int32_t p = Buffer_get_pixel(&g_bw.mask, x % 0x60, y % 0x30) - 10;
    int32_t q = (p + ((p >> 31) & 0xF)) >> 4;  // the diamond's quadrant colours
    static const int32_t kQx[5] = {0, 1, 1, -1, -1}, kQy[5] = {0, -1, 1, 1, -1};
    if (q != 0 && q >= 1 && q <= 4) return radius_move(dx + kQx[q], dy + kQy[q], 0x15);
    if (q != 0) return radius_move(dx, dy, 0x15);  // (not reached: the mask has quadrants 1..4)
    return radius_move(dx, dy, 0x15);
}

void THISCALL BaseWin_farm_click(uint8_t* self, int32_t x, int32_t y, int32_t) {
    int32_t from = -1;
    bw_set_base();
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    if (faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) return;
    if (base_win().pane != 0) return;
    if (base_win().drag_type == 0) from = BaseWin_farm_get(self, base_win().down_x, base_win().down_y);
    int32_t tile = BaseWin_farm_get(self, x, y);
    if (from == tile) from = -1;
    else if (from == 0) return;
    if (tile < 0) return;
    if (bw_lock_base(CurrentBaseID)) return;
    log_say("Farm Click", cur_base().name, tile, 0, 0);
    BASE* b = &cur_base();
    if (b->governor_flags & 0x40) b->governor_flags &= ~0x40;
    uint32_t bit = 1u << tile;
    bool changed = true;
    if (base_win().drag_type == 1) {
        // A citizen dragged onto the tile: the specialist becomes a worker.
        if (b->worked_tiles & bit) {
            changed = false;
        } else {
            b->worked_tiles |= bit;
            int32_t i = base_win().drag_index;
            for (int32_t j = i + 1; i < cur_base().specialist_total; i++, j++) {
                int32_t t =
                    j < 0x10 ? (cur_base().specialist_types[j >> 3] >> ((j & 7) << 2)) & 0xF : best_specialist();
                if (j < 0x11) set_specialist_type(i, t);
            }
            if (cur_base().specialist_total) cur_base().specialist_total--;
        }
    } else if (from > 0) {
        // A worker dragged from tile `from` to this tile.
        uint32_t fbit = 1u << from;
        if (!(b->worked_tiles & fbit) || (b->worked_tiles & bit)) changed = false;
        else {
            b->worked_tiles &= ~fbit;
            cur_base().worked_tiles |= bit;
        }
    } else if (tile == 0) {
        // The base tile: every worker is reset (the base chooses again).
        b->worked_tiles = 0;
        cur_base().specialist_total = 0;
        cur_base().specialist_adjust = 0;
    } else if (b->worked_tiles & bit) {
        // A worked tile: its worker becomes a specialist.
        b->worked_tiles &= ~bit;
        int32_t t = best_specialist();
        int32_t n = cur_base().specialist_total;
        if (n < 0x10) set_specialist_type(n, t);
        cur_base().specialist_total++;
    } else if (BaseTileFlags[tile] != 0) {
        changed = false;  // not workable
    } else if (b->specialist_total == 0) {
        // No specialist to put there: the worker of the least valuable worked tile moves.
        int32_t w_food, w_min_tech, w_energy, w_power, w_fight;
        Faction& f = Factions[faction_id];
        if ((FactionStatus[0] & (1 << faction_id)) && (b->governor_flags & 0x80000000)) {
            uint32_t g = (uint32_t)b->governor_flags;
            w_food = (g >> 24) & 1;
            w_min_tech = (g >> 25) & 1;
            w_energy = (g >> 26) & 1;
            w_power = (g >> 27) & 1;
            if (w_power) w_fight = 1;
            else if (w_food) w_fight = 0;
            else if (!w_min_tech && !w_energy) w_fight = f.AI_fight;
            else w_fight = -1;
        } else {
            w_food = f.AI_growth;
            w_min_tech = f.AI_tech;
            w_energy = f.AI_wealth;
            w_power = f.AI_power;
            w_fight = f.AI_fight;
        }
        int32_t best = 999, best_i = 0;
        for (int32_t i = 1; i < 0x15; i++) {
            BASE& cb = cur_base();
            int32_t tx = TableOffsetX[i] + cb.x;
            if (!(MapToggleFlat & 1)) {
                if (tx < 0) tx += MapAreaX;
                else if (tx >= MapAreaX) tx -= MapAreaX;
            }
            int32_t ty = TableOffsetY[i] + cb.y;
            if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX || !(cb.worked_tiles & (1u << i))) continue;
            int32_t nut = cb.nutrient_intake_2 - cb.pop_size * Rules.nutrient_intake_req_citizen;
            nut = nut < 1 ? 1 : (nut > 99 ? 99 : nut);
            int32_t s = resource_yield(0, faction_id, CurrentBaseID, tx, ty) * (w_food + 3) * 4 / nut;
            int32_t mins = cur_base().mineral_intake_2 - BaseForcesMaintCost;
            mins = mins < 1 ? 1 : (mins > 99 ? 99 : mins);
            s += resource_yield(1, faction_id, CurrentBaseID, tx, ty) * (w_fight + w_power + 4) * cur_base().pop_size /
                 mins;
            int32_t en = cur_base().energy_intake_2;
            en = en < 1 ? 1 : (en > 99 ? 99 : en);
            s += resource_yield(2, faction_id, CurrentBaseID, tx, ty) * cur_base().pop_size *
                 (w_energy + w_min_tech + 2) / en;
            if (s < best) {
                best = s;
                best_i = i;
            }
        }
        if (!best_i) changed = false;
        else {
            cur_base().worked_tiles &= ~(1u << best_i);
            cur_base().worked_tiles |= bit;
        }
    } else {
        cur_base().specialist_total--;
        cur_base().worked_tiles |= bit;
    }
    recompute_redraw(self);
    if (changed) log_say_values("BASE: changed farming locations", CurrentBaseID, 0, 0);
    bw_unlock_base(CurrentBaseID);
}

// ==== citizens ====================================================================================

void THISCALL BaseWin_pop_click(uint8_t* self, int32_t index, int32_t, int32_t, int32_t) {
    bw_set_base();
    if (cur_base().faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) return;
    int32_t current = index < 0 ? 999 : specialist_type(index);
    if (current < 0) current = 1;
    BasePop* pop = popup_new();
    Popup_start(pop, "SCRIPT.txt", "CITIZEN", -1, nullptr, 0x40);
    // The first item: a worker (id 999).
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0x2C8 / 4));
    BasePop_item(pop, StrBuffer, 999);
    int32_t too_small = 0, active = 999;
    for (int32_t i = 0; i < 7; i++) {
        const CCitizen& c = Citizen[i];
        if (!has_tech(c.preq_tech, (uint8_t)cur_base().faction_id)) continue;
        if (has_tech(c.obsol_tech, (uint8_t)cur_base().faction_id) && i != current) continue;
        // Below min_base_size_specialists only the specialists with 2 psych or more.
        if (cur_base().pop_size < Rules.min_base_size_specialists && c.psych_bonus < 2) {
            too_small = 1;
            continue;
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, Strings_get(&StringTable, c.singular_name));
        crt_strcat(StrBuffer, "|");
        char line[224];
        line[0] = 0;
        const int32_t* bonus = &c.econ_bonus;
        for (int32_t k = 0; k < 3; k++) {
            if (!bonus[k]) continue;
            if (std::strlen(line)) crt_strcat(line, ", ");
            crt_strcat(line, "+");
            char num[80];
            std::snprintf(num, sizeof(num), "%d", bonus[k]);
            crt_strcat(line, num);
            crt_strcat(line, " ");
            crt_strcat(line, Strings_get(&StringTable, Energy[k].name));
        }
        crt_strcat(StrBuffer, line);
        BasePop_item(pop, StrBuffer, i);
        if (i == current) active = i;
    }
    if (too_small) {
        parse_num(0, Rules.min_base_size_specialists);
        Popup_start(pop, "SCRIPT.txt", "CITIZEN2", -1, nullptr, 0x80);
    }
    BasePop_set_active(pop, active);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0 || r == current) return;
    if (bw_lock_base(CurrentBaseID)) return;
    if (cur_base().governor_flags & 0x40) cur_base().governor_flags &= ~0x40;
    if (r == 999) {
        if (cur_base().specialist_total) cur_base().specialist_total--;
    } else {
        if (current == 999) {
            // A worker becomes a specialist: the dragged-from tile, else the last worked tile.
            int32_t t = -1;
            if (base_win().drag_type == 0) t = BaseWin_farm_get(self, base_win().down_x, base_win().down_y);
            else
                for (t = 0x14; t > 0 && !(cur_base().worked_tiles & (1u << t)); t--) {}
            if (t > 0) cur_base().worked_tiles &= ~(1u << t);
            index = cur_base().specialist_total++;
        }
        set_specialist_type(index, r);
    }
    log_say_values("BASE: changed citizen professions", CurrentBaseID, 0, 0);
    if (Win_is_visible(self)) {
        set_base(base_win().base);
        base_compute(1);
        bw_redraw();
        GraphicWin_redraw(&MainInterfaceWin);
    }
    bw_unlock_base(CurrentBaseID);
}

// ==== facilities ==================================================================================

void THISCALL BaseWin_facilities_click(uint8_t* self, int32_t fac, int32_t, int32_t, int32_t) {
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    if (faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) return;
    parse_says(0, cur_base().name, -1, -1);
    parse_say(1, Facility[fac].name, -1, -1);
    bool al = alien(faction_id);
    int32_t r = popp(script(), "FACMENU", 0, al ? "alfacblt_sm.pcx" : "facblt_sm.pcx", 0);
    if (r == 0) return;
    if (r == 1) {
        help_topic(fac >= 0x46 ? 0xB : 0xA, fac);
        return;
    }
    if (fac >= 0x46) {
        popp(script(), "NOTPROJECT", 0, "secproj_sm.pcx", 0);
        return;
    }
    const char* pcx = al ? "alfacblt_sm.pcx" : "facblt_sm.pcx";
    int32_t base_id = CurrentBaseID;
    if (fac >= 0x41 || !base_fac(base_id, fac)) {
        popp(script(), "DONTHAVEFAC", 0, pcx, 0);
        return;
    }
    if (ControlTurnC && MultiplayerActive) {
        X_pop("DURINGUPKEEP", 0);
        return;
    }
    if ((cur_base().state_flags & 0x200) && !(GameState & STATE_OMNISCIENT_VIEW)) {
        parse_says(0, cur_base().name, -1, -1);
        popp(script(), "ONERECYCLE", 0, pcx, 0);
        return;
    }
    if (fac == 1) {
        popp(script(), "HQRECYCLE", 0, pcx, 0);
        return;
    }
    if (fac == 0x1A && (map_tile(cur_base().x, cur_base().y)->climate & 0xE0) < 0x60) {
        popp(script(), "PRESSURERECYCLE", 0, pcx, 0);
        return;
    }
    // Free facilities of the faction (rules FACILITY, or FREEFAC with its tech).
    MFaction& mf = MFactions[faction_id];
    for (int32_t i = 0; i < mf.faction_bonus_count; i++) {
        int32_t id = mf.faction_bonus_id[i];
        int32_t v = mf.faction_bonus_val1[i];
        if ((id == 2 || (id == 0xC && has_tech(Facility[v].preq_tech, faction_id))) && fac == v) {
            parse_say(1, Facility[fac].name, -1, -1);
            popp(script(), "FREERECYCLE", 0, pcx, 0);
            return;
        }
    }
    if (bw_lock_base(CurrentBaseID, r == 3 ? 2 : 0)) return;
    int32_t refund;
    if (r == 3) {
        // Everywhere: the bases of the faction that have it (land bases only for pressure domes).
        int32_t refundable = 0, count = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id) continue;
            if (fac == 0x1A && (map_tile(Bases[i].x, Bases[i].y)->climate & 0xE0) < 0x60) continue;
            if (!base_fac(i, fac)) continue;
            count++;
            if (!(cur_base().state_flags & 0x200) || !(GameState & STATE_OMNISCIENT_VIEW)) refundable++;
        }
        refund = Facility[fac].cost * refundable * 5;
        parse_num(0, refund);
        parse_num(1, count);
        parse_say(0, Facility[fac].name, -1, -1);
    } else {
        refund = Facility[fac].cost * 5;
        parse_say(0, Facility[fac].name, -1, -1);
        parse_num(0, refund);
    }
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "RECYCLEME");
    if (r == 3) crt_strcat(StrBuffer, "ALL");
    else if (fac == 8 && (cur_base().state_flags & 0x400)) {
        refund = 0;
        crt_strcat(StrBuffer, "0");
    }
    char label[64];
    crt_strcpy(label, StrBuffer);
    if (!popp(script(), label, 0, pcx, 0)) {
        ComputeBaseID = -1;
        base_compute(0);
        bw_unlock_base(CurrentBaseID);
        return;
    }
    if (r == 3) {
        for (int32_t i = 0; i < BaseCount; i++) {
            if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id) continue;
            if (fac == 0x1A && (map_tile(Bases[i].x, Bases[i].y)->climate & 0xE0) < 0x60) continue;
            Bases[i].state_flags |= 0x200;
            set_fac(fac, i, 0);
            if (i != CurrentBaseID) synch_base(i);
        }
        Factions[faction_id].energy_credits += refund;
        log_say_values("BASE: global scrapped facility", CurrentBaseID, 0, 0);
    } else {
        cur_base().state_flags |= 0x200;
        set_fac(fac, CurrentBaseID, 0);
        Factions[faction_id].energy_credits += refund;
        log_say_values("BASE: scrapped facility", CurrentBaseID, 0, 0);
    }
    ComputeBaseID = -1;
    base_compute(0);
    bw_unlock_base(CurrentBaseID);
    *reinterpret_cast<int32_t*>(mem::host(0x7AD330)) = 0;
    InfoWin_on_redraw(&InfoWin);
    bw_redraw();
    GraphicWin_redraw(&MainInterfaceWin);
}

// ==== hurry =======================================================================================

int32_t GAMEAPI X_pop_ask_number(const char* label, int32_t value, int32_t a3) {
    char text[80];
    std::snprintf(text, sizeof(text), "%d", value);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, text);
    char def[256];
    crt_strcpy(def, StrBuffer);
    int32_t r = X_pop_ask_6(ScriptFileName, label, 9, def, a3, 0);
    ParseNumTable[0] = std::atoi(ParseStrBuffer[0]);
    return r;
}

void THISCALL BaseWin_hurry(uint8_t* self) {
    bool locked = false;
    bw_set_base();
    if ((uint8_t)Bases[base_win().base].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW))
        return;
    BASE* b = &cur_base();
    int32_t faction_id = (uint8_t)b->faction_id;
    int32_t item = b->queue_items[0];
    int32_t cost = item >= 0 ? veh_cost(item, CurrentBaseID, nullptr) : Facility[-item].cost;
    int32_t total = cost_factor(faction_id, 1, -1) * cost;  // minerals for the whole item
    int32_t rest = total - cur_base().minerals_accumulated;
    // The original UI's hurry cost: 2 credits per mineral for facilities, 2r + r^2 / 20 for units;
    // doubled for secret projects (again below 4 rows of minerals), below the retooling exemption,
    // and while SecretProjects[31] exists; then the faction's HURRY rule (percent).
    int32_t price = item >= 0 ? rest * rest / 20 + rest * 2 : rest * 2;
    int32_t minerals = cur_base().minerals_accumulated;
    if (-item >= 0x46) {
        price *= 2;
        minerals = cur_base().minerals_accumulated;
        if (minerals < cost_factor(faction_id, 1, -1) * 4) price *= 2;
    }
    if (minerals < Rules.retool_exemption) price *= 2;
    if (SecretProjects[FAC_VOICE_OF_PLANET - SP_ID_First] >= 0) price *= 2;
    if (MFactions[faction_id].rule_hurry != 100) price = MFactions[faction_id].rule_hurry * price / 100;
    if (price <= 0) return;
    parse_says(0, cur_base().name, -1, -1);
    parse_says(1, item >= 0 ? Units[item].name : Strings_get(&StringTable, Facility[-item].name), -1, -1);
    parse_num(0, price);
    Faction& f = Factions[faction_id];
    parse_num(1, f.energy_credits - f.hurry_cost_total);
    BasePop* pop = popup_new();
    Popup_start(pop, "SCRIPT.txt", "HURRY", -1, nullptr, 0);
    if ((cur_base().state_flags & 2) && (cur_base().queue_items[0] >= 0 || cur_base().queue_items[0] <= -0x41)) {
        // Drone riots: only facilities can be hurried.
        Popup_start(pop, "SCRIPT.txt", "HURRYDRONE", -1, nullptr, 0x80);
    } else if (f.energy_credits - f.hurry_cost_total >= 0) {
        if (X_text_open(ScriptFileName, "HURRYOPTIONS")) {
            popup_delete(pop);
            return;
        }
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        BasePop_item(pop, StrBuffer, 0);
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        if (f.energy_credits - f.hurry_cost_total >= price) BasePop_item(pop, StrBuffer, 1);
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        BasePop_item(pop, StrBuffer, 2);
        if (bw_lock_base(CurrentBaseID)) {
            popup_delete(pop);
            return;
        }
        locked = true;
    }
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    FX_play(&Sounds, 0xF);
    bw_set_base();
    if (r < 1 || (GameState & 0x800)) {
        if (locked) bw_unlock_base(CurrentBaseID);
        return;
    }
    if (cur_base().state_flags & 0x40000000) {
        X_pop("PARTIALONCE", 0);
        if (locked) bw_unlock_base(CurrentBaseID);
        return;
    }
    int32_t pay = price, result = total;
    if (r > 1) {
        // A partial payment (once per turn).
        int32_t avail = f.energy_credits - f.hurry_cost_total;
        parse_num(0, price < avail ? price : avail);
        avail = f.energy_credits - f.hurry_cost_total;
        if (X_pop_ask_number("HURRYPAYMENT", price < avail ? price : avail, 0)) {
            if (locked) bw_unlock_base(CurrentBaseID);
            return;
        }
        avail = f.energy_credits - f.hurry_cost_total;
        int32_t amount = ParseNumTable[0];
        pay = (amount < 0 || avail < 0) ? 0 : (amount > avail ? avail : amount);
        int32_t m = cur_base().minerals_accumulated;
        result = (total - m) * pay / price + m;
        cur_base().state_flags |= 0x40000000;
    }
    if (MultiplayerActive && ControlTurnC) {
        f.hurry_cost_total += pay;
    } else {
        f.energy_credits -= pay;
        EnergyCredits -= pay;
        if (MultiplayerActive) synch_energy(faction_id);
    }
    base_win().hurried = result;
    cur_base().minerals_accumulated_2 = result;
    cur_base().minerals_accumulated = result;
    cur_base().production_id_last = cur_base().queue_items[0];
    log_say_values("BASE: hurried production", CurrentBaseID, 0, 0);
    bw_unlock_base(CurrentBaseID);
    bw_redraw();
    GraphicWin_redraw(&MainInterfaceWin);
    *reinterpret_cast<int32_t*>(mem::host(0x7AD330)) = 0;
    InfoWin_on_redraw(&InfoWin);
}

// ==== governor ====================================================================================

void THISCALL BaseWin_gov_on(uint8_t* self, uint32_t flag) {
    if (bw_lock_base(CurrentBaseID)) return;
    BASE& b = cur_base();
    uint32_t g = (uint32_t)b.governor_flags;
    if (flag == 0) {
        g |= 0x80000000;
    } else {
        if (!(g & 0x200000)) g = (g & 0xF0FFFFFF) | flag;
        else g ^= flag;
        if (!(g & 0xF000000)) g &= 0x7FFFFFFF;
        else g |= 0x80000041;
    }
    if (!(FactionStatus[0] & (1 << ((uint8_t)b.faction_id & 31)))) g &= 0x7FFFFFFF;
    g &= 0xBFFFFFFF;
    b.governor_flags = (int32_t)g;
    if (g & 0x80000000) {
        if (g & 1) {
            cur_base().state_flags &= 0x7FFFFFFF;
            cur_base().queue_size = 0;
            base_reset(CurrentBaseID, 1);
        }
        if (cur_base().governor_flags & 0x40) {
            cur_base().worked_tiles = 0;
            cur_base().specialist_total = 0;
            cur_base().specialist_adjust = 0;
            base_compute(1);
            base_doctors();
        }
    }
    bw_unlock_base(CurrentBaseID);
    bw_redraw();
    GraphicWin_redraw(&MainInterfaceWin);
    if (!(cur_base().governor_flags & 0x80000000)) draw_radius(cur_base().x, cur_base().y, 2, 2);
}

void THISCALL BaseWin_gov_activate(uint8_t*) {
    BASE& b = cur_base();
    if (!(b.governor_flags & 0x80000000)) return;
    if (b.governor_flags & 1) {
        b.state_flags &= 0x7FFFFFFF;
        b.queue_size = 0;
        base_reset(CurrentBaseID, 1);
    }
    if (cur_base().governor_flags & 0x40) {
        cur_base().worked_tiles = 0;
        cur_base().specialist_total = 0;
        cur_base().specialist_adjust = 0;
        base_compute(1);
        base_doctors();
    }
}

// GOVOPTIONS: the governor's check boxes <-> governor_flags bits.
static const uint32_t kGovBits[19] = {0x80000000, 0x200000, 0x80,    0x40,    1,       0x400000, 0x200,
                                      0x400,      0x800,    0x1000,  0x2000,  0x80000, 0x800000, 0x100000,
                                      0x8000,     0x20000,  0x10000, 0x40000, 0x20};

int32_t THISCALL BaseWin_governor(uint8_t* self, int32_t a1) {
    bw_set_base();
    if ((uint8_t)Bases[base_win().base].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW))
        return 1;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    uint32_t g = (uint32_t)cur_base().governor_flags;
    DialogChoices = 0;
    for (int32_t i = 0; i < 19; i++)
        if (g & kGovBits[i]) DialogChoices |= 1 << i;
    parse_says(0, cur_base().name, -1, -1);
    if (bw_lock_base(CurrentBaseID)) return 1;
    int32_t r = X_pop_9("SCRIPT.txt", "GOVOPTIONS", -1, nullptr, 0x41, 0);  // X_pop_6
    if (r < 0) {
        bw_unlock_base(CurrentBaseID);
        return 1;
    }
    uint32_t n = (uint32_t)cur_base().governor_flags & 0xF000000;
    for (int32_t i = 0; i < 19; i++)
        if (DialogChoices & (1 << i)) n |= kGovBits[i];
    cur_base().governor_flags = (int32_t)n;
    Factions[faction_id].base_governor_adv = (int32_t)n;
    if (!a1) {
        BASE& b = cur_base();
        if (b.governor_flags & 0x80000000) {
            if (b.governor_flags & 1) {
                b.state_flags &= 0x7FFFFFFF;
                b.queue_size = 0;
                base_reset(CurrentBaseID, 1);
            }
            if (cur_base().governor_flags & 0x40) {
                cur_base().worked_tiles = 0;
                cur_base().specialist_total = 0;
                cur_base().specialist_adjust = 0;
                base_compute(1);
                base_doctors();
            }
        }
        if (!(cur_base().governor_flags & 0x80000000) || !(cur_base().governor_flags & 1))
            draw_radius(cur_base().x, cur_base().y, 2, 2);
    }
    bw_unlock_base(CurrentBaseID);
    bw_redraw();
    GraphicWin_redraw(&MainInterfaceWin);
    return 0;
}

void THISCALL BaseWin_update_gov_buttons(uint8_t* self) {
    BASE& b = cur_base();
    int32_t faction_id = (uint8_t)b.faction_id;
    uint32_t g = (uint32_t)b.governor_flags;
    bool human = (FactionStatus[0] & (1 << faction_id)) != 0;
    Faction& f = Factions[faction_id];
    bw_button_set(bw_button(8), (int32_t)(g >> 31));
    struct {
        int32_t button;
        uint32_t bit;
        int32_t ai;
    } kGov[4] = {{4, 0x1000000, f.AI_growth},
                 {5, 0x2000000, f.AI_tech},
                 {6, 0x4000000, f.AI_wealth},
                 {7, 0x8000000, f.AI_power}};
    for (auto& k : kGov) {
        bool on = human ? ((g & k.bit) && (g & 0x80000000)) : k.ai != 0;
        bw_button_set(bw_button(k.button), on ? 1 : 0);
        if (!on) bw_button(k.button)->hover = 0;
    }
}

// ==== nerve staple ================================================================================

void THISCALL BaseWin_nerve_staple(uint8_t* self) {
    bw_set_base();
    if ((uint8_t)Bases[base_win().base].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW))
        return;
    int32_t faction_id = (uint8_t)cur_base().faction_id;
    bool al = alien(faction_id);
    parse_says(0, cur_base().name, -1, -1);
    if (Factions[faction_id].SE_police_pending + has_fac(FAC_BROOD_PIT, CurrentBaseID, 0) * 2 < 0) {
        popp(ScriptFileName, "NERVESTAPLEHELLNO", 0, al ? "Alopdir.pcx" : "talent_sm.pcx", 0);
        return;
    }
    if (cur_base().nerve_staple_turns_left) {
        popp(ScriptFileName, "NERVESTAPLED", 0, al ? "ALdrone_sm.pcx" : "drone_sm.pcx", 0);
        return;
    }
    if (bw_lock_base(CurrentBaseID)) return;
    int32_t r = popp(ScriptFileName, "NERVESTAPLE", 0, al ? "ALdrone_sm.pcx" : "drone_sm.pcx", 0);
    if (r == 0) {
        bw_unlock_base(CurrentBaseID);
        return;
    }
    if (MultiplayerActive) {
        message_data(0x2414, 0, CurrentBaseID, 0, 0, 0);
        NetDaemon_await_exec(&NetState, 1);
        bw_unlock_base(CurrentBaseID);
        return;
    }
    action_staple(CurrentBaseID);
    bw_unlock_base(CurrentBaseID);
}

// ==== the queue ===================================================================================

void THISCALL BaseWin_add_queue(uint8_t* self, int32_t item, int32_t insert) {
    int32_t sel = base_win().queue_sel;
    BASE* b = &cur_base();
    if (sel == 0 || sel == 9) return;
    if (!insert && b->queue_items[sel] == item && sel <= b->queue_size) return;
    // A facility or project already in the queue moves (it is removed from its slot).
    if (item < 0 && (-item < 0x41 || (item != -0x44 && -item > 0x43))) {
        for (int32_t i = 1; i < 9; i++) {
            if (b->queue_items[i] != item) continue;
            if (i < sel) base_win().queue_sel = sel - 1;
            b->queue_size--;
            for (int32_t j = i; j < 8; j++) b->queue_items[j] = b->queue_items[j + 1];
            break;
        }
    }
    if (insert) {
        for (int32_t j = 9; base_win().queue_sel < j; j--) b->queue_items[j] = b->queue_items[j - 1];
        if (b->queue_size < 9) b->queue_size++;
    }
    b->queue_items[base_win().queue_sel] = item;
    if (b->queue_size < base_win().queue_sel) b->queue_size++;
    if (insert) {
        base_win().queue_hilite = 0;
        base_win().queue_sel = b->queue_size + 1;
    }
    // Stockpile energy only as the last item.
    for (int32_t i = 0; i < b->queue_size + 1; i++) {
        if (b->queue_items[i] == -0x45 && i < b->queue_size) {
            for (int32_t j = i; j < 9; j++) b->queue_items[j] = b->queue_items[j + 1];
            b->queue_size--;
            if (b->queue_size < base_win().queue_sel) base_win().queue_sel = b->queue_size < 2 ? 1 : b->queue_size;
            BaseWin_draw_queue(self, 1);
        }
        BaseWin_draw_production(self, 1);
    }
    BaseWin_draw_queue(self, 1);
}

void THISCALL BaseWin_delete_queue(uint8_t* self, int32_t slot) {
    BASE* b = &cur_base();
    if (b->queue_size < slot) {
        int32_t s = base_win().queue_sel - 1;
        base_win().queue_sel = s < 2 ? 1 : s;
        return;
    }
    for (int32_t j = slot; j < 9; j++) b->queue_items[j] = b->queue_items[j + 1];
    b->queue_size--;
    if (b->queue_size < base_win().queue_sel) base_win().queue_sel = b->queue_size < 2 ? 1 : b->queue_size;
    BaseWin_draw_queue(self, 1);
}

// ==== clicks ======================================================================================

void THISCALL BaseWin_click(uint8_t* self, int32_t x, int32_t y, int32_t right, int32_t a4) {
    TutWin_reset(&TutWin);
    base_win().drag_state = -1;
    if (!Win_is_visible(self) || (MultiplayerActive && NetInputPending)) return;
    // Right clicks on the panes: their help bubbles (Win_set_bubble_text at the click).
    if (right) {
        uint32_t label = 0;
        // (The energy pane's two layouts at 800 x 600; above, both panes are shown.) BUG(original):
        // the test is ScreenWidth == 1024, so on a wider screen the bubbles of one of the two panes
        // are missing (the one the 800 x 600 layout would hide).
        bool both = g_compat.wide_base_bubbles ? ScreenWidth == 0x400 : ScreenWidth != 800;
        bool alt_mode = both || base_win().energy_layout;
        bool alt_mode2 = both || !base_win().energy_layout;
        if (in_box(x, y, base_win().rect_map)) {
            int32_t pane = base_win().pane;
            label = pane == 0 ? 0xC98 : pane == 2 ? 0xC20 : pane == 1 ? 0xC24 : 0;
            if (!label) return;
        } else if (in_box(x, y, base_win().rect_commerce_pane)) {
            label = (MFactions[(uint8_t)Bases[base_win().base].faction_id].rule_flags & 0x80) ? 0x1190 : 0xC74;
        } else if (in_box(x, y, base_win().rect_fac_pane)) {
            label = 0xC78;
        } else if (in_box(x, y, base_win().rect_nut_pane)) {
            label = 0xC7C;
        } else if (in_box(x, y, base_win().rect_exp[0]) && alt_mode2) {
            label = 0xC80;
        } else if (in_box(x, y, base_win().rect_exp[2]) && alt_mode2) {
            label = 0xC84;
        } else if (in_box(x, y, base_win().rect_exp[1]) && alt_mode2) {
            label = 0xC88;
        } else if (in_box(x, y, base_win().rect_exp[3]) && alt_mode) {
            label = 0xC8C;
        } else if (in_box(x, y, base_win().rect_exp[4]) && alt_mode) {
            label = 0xC90;
        } else if (in_box(x, y, base_win().rect_exp[5]) && alt_mode) {
            label = 0xC94;
        }
        if (label) {
            int32_t sx = x, sy = y;
            basewin_client_to_screen(&sx, &sy);
            ui_bubble_set(label_get(label / 4), sx, sy);
            return;
        }
    }
    int32_t index, type;
    if (spot_check(&g_bw.spots_win, x, y, &index, &type) < 0) return;
    bw_set_base();
    int32_t owner = (uint8_t)cur_base().faction_id;
    if ((owner == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) ||
         (Factions[CurrentPlayerFaction].diplo_status[owner] & 1) || type == 6) &&
        a4 == 0) {
        if (type == 0) BaseWin_farm_click(self, x, y, right);
        else if (type == 2) BaseWin_facilities_click(self, index, x, y, right);
    }
}

void THISCALL BaseWin_iface_click(uint8_t* self, int32_t x, int32_t y, int32_t right, int32_t a4) {
    TutWin_reset(&TutWin);
    base_win().drag_state = -1;
    if (!Win_is_visible(self) || (MultiplayerActive && NetInputPending)) return;
    int32_t index = -1, type = -1;
    if (spot_check(&g_bw.spots_iface, x, y, &index, &type) < 0) {
        // A right click outside the spots: the panes' help bubbles (the citizens row, the
        // nutrients' box, the units present; the production pane's text is not shown, the
        // original passes an uninitialised local there).
        if (right) {
            uint32_t label = 0;
            if (in_box(x, y, base_win().rect_pop_mi)) label = 0xCA4;
            else if (in_box(x, y, base_win().rect_prod_pane_mi)) label = 0xC9C;
            else if (in_box(x, y, base_win().rect_garrison_mi)) label = 0xCA0;
            if (label) {
                ui_bubble_set(label_get(label / 4), x + main_interface().pos.left, y + main_interface().pos.top);
                return;
            }
        }
        // The citizens row outside a citizen: a worker.
        if (!in_box(x, y, base_win().rect_pop_mi)) return;
        type = 1;
    }
    if (Win_is_visible(self) && type != 8) {
    } else {
    }
    if (!Win_is_visible(self) || type == 8) bw_set_base();
    else bw_set_base();
    int32_t owner = (uint8_t)cur_base().faction_id;
    if (!(owner == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) ||
          (Factions[CurrentPlayerFaction].diplo_status[owner] & 1) || type == 6) ||
        a4 != 0)
        return;
    if (base_win().drag_type == 0 && type == 1) index = -1;
    switch (type) {
    case 1: BaseWin_pop_click(self, index, x, y, right); return;
    case 5: BaseWin_production(self, 0, 0); return;
    case 6:
    case 7: BaseWin_garrison_click(self, index, x, y, right); return;
    case 8: BaseWin_queue_click(self, index, x, y, right); return;
    default: return;
    }
}

void THISCALL BaseWin_on_button_clicked(uint8_t* self, int32_t id) {
    bw_set_base();
    TutWin_reset(&TutWin);
    if (InfcList[InfcUsed] != 3) return;
    bool own = cur_base().faction_id == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW);
    switch (id) {
    case 1: BaseWin_rename(self); return;
    case 2: BaseWin_prev(self); return;
    case 3: BaseWin_next(self); return;
    case 4:
        if (own) BaseWin_gov_on(self, 0x1000000);
        return;
    case 5:
        if (own) BaseWin_gov_on(self, 0x2000000);
        return;
    case 6:
        if (own) BaseWin_gov_on(self, 0x4000000);
        return;
    case 7:
        if (own) BaseWin_gov_on(self, 0x8000000);
        return;
    case 8:
        if (!own) return;
        if (!(cur_base().governor_flags & 0x80000000)) {
            BaseWin_gov_on(self, 0);
            return;
        }
        cur_base().governor_flags &= 0x7FFFFFFF;
        bw_redraw();
        GraphicWin_redraw(&MainInterfaceWin);
        return;
    case 9:
        if (base_win().pane != 2) {
            base_win().pane = 2;
            bw_redraw();
        }
        return;
    case 10:
        if (base_win().pane != 0) {
            base_win().pane = 0;
            bw_redraw();
        }
        return;
    case 0xB:
        if (base_win().pane != 1) {
            base_win().pane = 1;
            bw_redraw();
        }
        return;
    case 0xC: BaseWin_production(self, 0, 0); return;
    case 0xD: BaseWin_hurry(self); return;
    case 0x10:
        if (own) BaseWin_nerve_staple(self);
        return;
    case 0x12:
    case 0x13:
        if (own) BaseWin_governor(self, 0);
        return;
    case 0x14: {
        int32_t s = cur_base().queue_size + 1;
        base_win().queue_sel = s > 9 ? 9 : s;
        BaseWin_draw_queue(self, 1);
        if (!Win_is_visible(self)) BaseWin_production(self, base_win().queue_sel, 0);
        return;
    }
    case 0x15:
        if (own) BaseWin_base_editor(self);
        return;
    case -2:
    case 0: BaseWin_exit(self); return;
    default: return;
    }
}
