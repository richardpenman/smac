#include "engine/action.h"

#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/goals.h"
#include "engine/goody.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/path.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/zoc.h"

namespace {

// Map distance the original inlines: (wrapped |dx| + |dy|) / 2.
int32_t map_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    return (std::abs(y1 - y2) + dx) >> 1;
}

// Faction's diplomatic status bits with another faction (low byte as the original tests it).
inline uint32_t dstat(int32_t faction_id, int32_t other_id) {
    return game().factions[faction_id].diplo_status[other_id];
}

inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0; }

// Owner of the units at a tile (val2 low nibble when the tile has units), or -1.
inline int32_t tile_veh_owner(MAP* sq) {
    if (!(sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE))) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner > 7 ? -1 : owner;
}

// Hit points of a unit (as the original's inline: artifacts 1, else 10 * reactor clamped 1..100).
inline int32_t unit_hp(int32_t unit_id) {
    if (Units[unit_id].plan == 12) return 1;
    int32_t r = Units[unit_id].reactor_id;
    if (r == 0) r = 1;
    else if (r > 100) r = 100;
    return r * 10;
}

// Remaining moves of a vehicle clamped 0..999 (inline in the original).
inline int32_t moves_left(int32_t veh_id) {
    int32_t m = speed(veh_id, 0) - game().vehs[veh_id].moves_spent;
    if (m < 0) return 0;
    return m > 999 ? 999 : m;
}

void set_leader_title(int32_t n, int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    parse_says(n, game().mfactions[faction_id].title_leader, -1, -1);
}
void set_leader_name(int32_t n, int32_t faction_id) {
    GenderDefault = game().mfactions[faction_id].is_leader_female;
    PluralDefault = 0;
    parse_says(n, game().mfactions[faction_id].name_leader, -1, -1);
}
void set_faction_noun(int32_t n, int32_t faction_id) {
    GenderDefault = game().mfactions[faction_id].noun_gender;
    PluralDefault = game().mfactions[faction_id].is_noun_plural;
    parse_says(n, game().mfactions[faction_id].noun_faction, -1, -1);
}

}  // namespace

// ---- terraform_cost ---------------------------------------------------------------------------------

int32_t GAMEAPI terraform_cost(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    uint8_t climate = sq->climate;
    int32_t alt = climate >> 5;
    int32_t d = alt - 3;
    if (d < 0) d = 3 - alt;
    int32_t c = (d + 2) * (d + 2);
    if ((sq->items & BIT_FUNGUS) && (climate & 0xE0) >= 0x40) c *= 3;
    int32_t cost = c * 2;
    if (alt < 3) {
        cost = c * 4;
        if (!TechOwners[39]) cost = c << 3;
    }
    int32_t b = base_find_faction(x, y, faction_id);
    int32_t b3 = base_find_filtered(x, y, -1, -1, faction_id, -1);
    if (b >= 0) {
        int32_t dist = map_dist(x, y, game().bases[b].x, game().bases[b].y);
        if (dist < 1) dist = 1;
        else if (dist > 100) dist = 100;
        cost = dist * cost;
        if (b3 >= 0 && !(dstat(faction_id, (uint8_t)game().bases[b3].faction_id) & 1)) {
            int32_t d1 = (map_dist(x, y, game().bases[b].x, game().bases[b].y) * (game().bases[b3].pop_size + 2)) / 3;
            int32_t d2 = (map_dist(x, y, game().bases[b3].x, game().bases[b3].y) * (game().bases[b].pop_size + 2)) / 3;
            if (d2 != 0 && d1 != 0 && d2 < d1) cost = (d1 * cost) / d2;
        }
    }
    if (game().mfactions[faction_id].rule_flags & 0x2000) cost = cost / 2;
    cost = cost / 2;
    if (cost < 1) return 1;
    if (cost > 30000) return 30000;
    return cost;
}

// ---- action_build -----------------------------------------------------------------------------------

int32_t GAMEAPI action_build(int32_t veh_id, const char* name) {
    VEH& v = game().vehs[veh_id];
    int32_t faction_id = v.faction_id;
    int32_t y = v.y;
    int32_t x = v.x;
    log_say_values("Action Base", x, y, faction_id);
    log_say_values("Action Base Turn", CurrentTurn, 0, 0);
    if ((map_tile(x, y)->val3 & 0xC0) > 0x40) rocky_set(x, y, 1);
    if (faction_id == CurrentPlayerFaction ||
        (v.faction_id == CurrentPlayerFaction && (v.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
        (v.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31))) || (GameState & STATE_OMNISCIENT_VIEW))
        Console_focus(&MainConsole, x, y, faction_id);
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (game().factions[faction_id].base_count && owner >= 0 && owner != faction_id &&
        !(dstat(faction_id, owner) & 0x11) && (is_human(faction_id) || is_human(owner))) {
        if (!(dstat(faction_id, owner) & 2)) treaty_on(faction_id, owner, 0x10);
        else double_cross(faction_id, owner, -1);
    }
    int32_t base_id = base_init(faction_id, x, y);
    if (base_id < 0) {
        veh_skip(veh_id);
        return base_id;
    }
    if (is_human(faction_id)) {
        if (v.home_base_id >= 0) game().bases[base_id].governor_flags = game().bases[v.home_base_id].governor_flags;
        if (v.state & VSTATE_ON_ALERT) game().bases[base_id].governor_flags |= 0x80000000;
    }
    game().factions[faction_id].last_base_turn = CurrentTurn;
    if (name && std::strlen(name)) {
        std::strcpy(game().bases[base_id].name, name);
        make_base_unique(base_id);
    }
    veh_kill(veh_id);
    if (faction_id == CurrentPlayerFaction || (uint8_t)game().bases[base_id].faction_id == CurrentPlayerFaction ||
        (game().bases[base_id].visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31))) ||
        (GameState & STATE_OMNISCIENT_VIEW))
        draw_map(1);
    for (int32_t i = 1; i < 8; i++) del_site(i, 8, x, y, 2);
    mon_colony_founded(faction_id, game().bases[base_id].name);
    return base_id;
}

// ---- road_rate, contribution --------------------------------------------------------------------------

int32_t GAMEAPI road_rate(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    uint32_t items = sq->items;
    int32_t rate = Terraform[((items & 4) != 0) + 5].rate;
    if (items & 0x80) rate++;
    if ((items & 0x20) && (sq->climate & 0xE0) >= 0x40) rate += 2;
    if (items & 0x200000) return rate + 2;
    return rate + (sq->val3 >> 6);
}

int32_t GAMEAPI contribution(int32_t veh_id, int32_t terraform_id) {
    int32_t value = 2;
    uint8_t faction_id = game().vehs[veh_id].faction_id;
    if (has_abil(game().vehs[veh_id].unit_id, 1)) value = 4;
    if (terraform_id == 10 || terraform_id == 11) {
        if (SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First] >= 0 &&
            (uint8_t)game().bases[SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First]].faction_id == faction_id)
            return value * 2;
    } else if (SecretProjects[FAC_WEATHER_PARADIGM - SP_ID_First] >= 0 &&
               (uint8_t)game().bases[SecretProjects[FAC_WEATHER_PARADIGM - SP_ID_First]].faction_id == faction_id) {
        return (uint32_t)(value * 3) / 2;
    }
    return value;
}

// ---- action_terraform ---------------------------------------------------------------------------------

int32_t GAMEAPI action_terraform(int32_t veh_id, int32_t terraform_id, int32_t is_action) {
    int32_t veh_count = game().veh_count;
    VEH& v = game().vehs[veh_id];
    int32_t y = v.y;
    int32_t x = v.x;
    int32_t faction_id = v.faction_id;
    MAP* sq = map_tile(x, y);
    int32_t is_sea = (sq->climate & 0xE0) < 0x60;
    int32_t t = terraform_id;
    if (t == 5) {
        if ((sq->items & BIT_ROAD) && has_tech((&Terraform[6].preq_tech)[is_sea], faction_id)) t = 6;
    } else if (t == 0 && (sq->items & BIT_FARM) && (sq->climate & 0xE0) >= 0x60 &&
               has_tech((&Terraform[0].preq_tech)[is_sea], faction_id)) {
        t = 1;
    }
    if (is_action) v.order = (uint8_t)(t + 4);
    int32_t rate = Terraform[t].rate;
    switch (t) {
    case 3:
        rate += (int32_t)((sq->val3 >> 6) * rate) / 2;
        break;
    case 5:
    case 6: {
        uint32_t items = sq->items;
        rate = Terraform[((items & 4) != 0) + 5].rate;
        if (items & 0x80) rate++;
        if ((items & 0x20) && (sq->climate & 0xE0) >= 0x40) rate += 2;
        if (items & 0x200000) rate += 2;
        else rate += sq->val3 >> 6;
        break;
    }
    case 10:
        if (has_abil(v.unit_id, 0x2000)) rate /= 2;
        break;
    }
    uint32_t fbit = 1u << (faction_id & 31);
    if (!(FactionStatus[0] & (uint8_t)fbit) && DiffLevel > 3 && rate > 3) rate--;
    int32_t work = 0;
    int32_t done = 0;
    if (veh_id >= 0) {
        for (int32_t i = stack_top(veh_id); i >= 0; i = game().vehs[i].next_veh_id_stack) {
            if (i != veh_id && (int8_t)game().vehs[i].order == t + 4) {
                if (!is_action) {
                    work += contribution(i, t);
                    done += game().vehs[i].movement_turns;
                } else {
                    v.movement_turns += game().vehs[i].movement_turns;
                    game().vehs[i].movement_turns = 0;
                }
            }
        }
    }
    work += contribution(veh_id, t);
    done += v.movement_turns;
    if (!is_action) {
        int32_t sp = speed(veh_id, 0);
        int32_t left = sp - v.moves_spent;
        if (left >= 0 && (left > 999 || sp != v.moves_spent)) done += work;
        done = rate * 2 - done;
        if (work < 1) work = 1;
        else if (work > 255) return (done + 254) / 255;
        return (done - 1 + work) / work;
    }
    veh_skip(veh_id);
    uint8_t before = v.movement_turns;
    v.movement_turns = (uint8_t)(before + work);
    MAP* tile = map_tile(x, y);
    if ((tile->items & BIT_BASE_IN_TILE) && (tile->val2 & 0xF) < 8) {
        v.order = ORDER_NONE;
        return 0;
    }
    if ((int32_t)(uint8_t)(before + work) < rate * 2) {
        draw_tile(x, y, 2);
        BaseWin_check_veh(&BaseWin, veh_id);
        return 0;
    }
    v.movement_turns = 0;
    if (veh_id >= 0) {
        for (int32_t i = stack_top(veh_id); i >= 0; i = game().vehs[i].next_veh_id_stack)
            if ((int8_t)game().vehs[i].order == t + 4) game().vehs[i].order = ORDER_NONE;
    }
    uint32_t old_items = tile->items;
    bit_set(x, y, Terraform[t].bit_incompatible, 0);
    bit_set(x, y, Terraform[t].bit, 1);
    synch_bit(x, y, faction_id);
    switch (t) {
    case 10:
    case 11:
        site_radius(x, y);
        break;
    case 12:
    case 14:
        site_radius(x, y);
        world_climate();
        draw_map(1);
        break;
    case 15:
        bit_set(x, y, 0x100, 1);
        world_climate();
        draw_map(1);
        break;
    case 16:
        world_raise_alt(x, y);
        world_climate();
        draw_map(1);
        break;
    case 17:
        world_lower_alt(x, y);
        world_climate();
        draw_map(1);
        break;
    case 18: {
        uint8_t r = map_tile(x, y)->val3;
        if (r & 0xC0) {
            rocky_set(x, y, (r >> 6) - 1);
            site_radius(x, y);
        }
        break;
    }
    }
    if (Terraform[t].bit & 0x1208050) {
        int32_t b = base_find_faction(x, y, faction_id);
        if (b >= 0 && BaseFindDist < 3) game().bases[b].state_flags |= 0x100;
        if (t == 14 && faction_id == CurrentPlayerFaction && b >= 0 &&
            (uint8_t)game().bases[b].faction_id == faction_id) {
            parse_says(0, game().bases[b].name, -1, -1);
            NetMsg_pop(&NetMsg, "BOREHOLEMINE", 5000, 0, nullptr);
        }
    }
    bool climate_msgs;
    if (!(old_items & 0x200000) || (map_tile(x, y)->items & BIT_FOREST) || (map_tile(x, y)->climate & 0xE0) < 0x60) {
        climate_msgs = t == 16 || t == 17 || t == 12;
    } else if (t != 16) {
        // forest cleared: minerals for the home (or nearest) base
        int32_t home = v.home_base_id;
        if (home >= 0 || (home = base_find_faction(x, y, faction_id)) >= 0) {
            int32_t m = game().bases[home].minerals_accumulated + Rules.minerals_harvesting_forest;
            game().bases[home].minerals_accumulated = m;
            game().bases[home].minerals_accumulated_2 = m;
            game().bases[home].production_id_last = game().bases[home].queue_items[0];
            if (faction_id == CurrentPlayerFaction && (fbit & FactionStatus[0]) && Rules.minerals_harvesting_forest) {
                parse_says(0, game().bases[home].name, -1, -1);
                parse_num(0, Rules.minerals_harvesting_forest);
                NetMsg_pop(&NetMsg, "HARVESTFOREST", 5000, 0, nullptr);
            }
            BaseWin_check_base(&BaseWin, home);
        }
        climate_msgs = t == 17 || t == 12;
    } else {
        climate_msgs = true;
    }
    if (climate_msgs) {
        int32_t rf = RainfallBaseFaction;
        if (rf >= 0 && !(FactionStatus[0] & (1 << (rf & 31))) && (fbit & FactionStatus[0]) &&
            !((uint8_t)dstat(faction_id, rf) & 0x11)) {
            if (!((uint8_t)dstat(rf, faction_id) & 0x20)) cause_friction(rf, faction_id, 5);
            else set_treaty(rf, faction_id, 0x400, 1);
        }
        if (RainfallChangedBase >= 0) {
            parse_says(0, game().bases[RainfallChangedBase].name, -1, -1);
            parse_says(1, game().mfactions[faction_id].adj_name_faction, -1, -1);
            NetMsg_pop(&NetMsg, faction_id == CurrentPlayerFaction ? "TERRAMINE" : "TERRAYOURS", 5000, 0, nullptr);
        }
        // BUG(original): tests the viewer's entry but names the base of faction_id's entry.
        if (MapBaseIdClosestSubmergedVeh[CurrentPlayerFaction] >= 0) {
            parse_says(0, game().bases[MapBaseIdClosestSubmergedVeh[faction_id]].name, -1, -1);
            parse_says(1, game().mfactions[faction_id].adj_name_faction, -1, -1);
            NetMsg_pop(&NetMsg, faction_id == CurrentPlayerFaction ? "DROWNMINE" : "DROWNYOURS", 5000, 0, nullptr);
        }
    }
    synch_bit(x, y, faction_id);
    draw_tiles(x, y, 2);
    if (veh_count == game().veh_count) {
        BaseWin_check_veh(&BaseWin, veh_id);
        BaseWin_check_loc(&BaseWin, x, y, 2);
    } else {
        GraphicWin_redraw(&BaseWin);
    }
    if (faction_id != CurrentPlayerFaction) return 1;
    if ((v.state & VSTATE_ON_ALERT) && (GamePreferences & PREF_BSC_TUTORIAL_MSGS) && !MultiplayerActive) {
        uint32_t bit = 1u << (t & 31);
        if (!(bit & (uint32_t)TutWinMapStates[is_sea])) {
            uint32_t state = (uint32_t)TutWinMapStates[is_sea] | bit;
            TutWinMapStates[is_sea] = (int32_t)state;
            const char* label = nullptr;
            uint8_t* image = nullptr;
            int32_t veh = veh_id;
            if (!is_sea) {
                switch (t) {
                case 0: image = &ImgMapFarm; veh = -1; label = "MADEFARM"; break;
                case 1: image = &ImgMapSoilEnr; veh = -1; label = "MADESOIL"; break;
                case 2: image = &ImgMapMine; veh = -1; label = "MADEMINE"; break;
                case 3: image = &ImgMapSolar; veh = -1; label = "MADESOLAR"; break;
                case 4: label = "MADEFOREST"; break;
                case 5: label = "MADEROAD"; break;
                case 6: label = "MADETUBE"; break;
                case 9: label = "MADESENSOR"; break;
                case 12: label = "MADECONDENSER"; break;
                case 14: label = "MADEBOREHOLE"; break;
                case 15: label = "MADEAQUIFER"; break;
                default: TutWinMapStates[0] = (int32_t)(~bit & state); break;
                }
            } else if (t == 0) {
                image = &ImgMapFarm; label = "MADESEAFARM"; veh = -1;
            } else if (t == 2) {
                image = &ImgMapMine; label = "MADESEAMINE"; veh = -1;
            } else if (t == 3) {
                image = &ImgMapSolar; label = "MADESEASOLAR"; veh = -1;
            } else {
                TutWinMapStates[is_sea] = (int32_t)(~bit & state);
            }
            if (label) TutWin_tut_map(&TutWin, label, x, y, veh, image, 1, -1, -1);
        }
    }
    if (faction_id == CurrentPlayerFaction && v.order == ORDER_NONE && !(v.state & VSTATE_ON_ALERT) &&
        !TerraformDoneSoundPlayed) {
        TerraformDoneSoundPlayed = 1;
        wave_it(0x14);
        ambience(0xCF);
    }
    return 1;
}

// ---- action_staple ------------------------------------------------------------------------------------

void GAMEAPI action_staple(int32_t base_id) {
    BASE& b = game().bases[base_id];
    int32_t count = ++b.nerve_staple_count;
    uint8_t faction_id = b.faction_id;
    bool done = count < 3 || (count < 9 && game_rand() % 2 != 0);
    if (done) {
        b.state_flags &= ~BSTATE_DRONE_RIOTS_ACTIVE;
        b.nerve_staple_turns_left += 10;
        draw_tile(b.x, b.y, 2);
        if ((uint8_t)b.faction_id == CurrentPlayerFaction) {
            parse_says(0, b.name, -1, -1);
            if (!(game().mfactions[faction_id].rule_flags & 0x80))
                popp(ScriptFileName, "NERVESTAPLEDONE", 0, "drone_sm.pcx", 0);
            else
                popp(ScriptFileName, "NERVESTAPLEDONE", 0, "ALdrone_sm.pcx", 0);
        } else {
            parse_says(0, b.name, -1, -1);
            set_leader_title(1, (uint8_t)b.faction_id);
            PluralDefault = 0;
            GenderDefault = game().mfactions[(uint8_t)b.faction_id].is_leader_female;
            parse_says(2, game().mfactions[(uint8_t)b.faction_id].name_leader, -1, -1);
            NetMsg_pop(&NetMsg, "NERVESTAPLETHEM", 5000, 0, nullptr);
        }
    } else if ((uint8_t)b.faction_id == CurrentPlayerFaction) {
        parse_says(0, b.name, -1, -1);
        if (!(game().mfactions[faction_id].rule_flags & 0x80))
            popp(ScriptFileName, "NERVESTAPLEFAIL", 0, "drone_sm.pcx", 0);
        else
            popp(ScriptFileName, "NERVESTAPLEFAIL", 0, "ALdrone_sm.pcx", 0);
    }
    atrocity((uint8_t)b.faction_id, (uint8_t)b.faction_id_former, b.faction_id_former != b.faction_id, 1);
}

// ---- action_destroy -----------------------------------------------------------------------------------

void GAMEAPI action_destroy(int32_t veh_id, int32_t items, int32_t x, int32_t y) {
    bool bombard = false;
    VEH& v = game().vehs[veh_id];
    int32_t faction_id = v.faction_id;
    veh_skip(veh_id);
    v.state |= 0x40;
    if (y < 0 || y >= game().map.y || x < 0 || x >= game().map.x) {
        x = v.x;
        y = v.y;
    } else {
        MAP* sq = map_tile(x, y);
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) {
            if (faction_id == CurrentPlayerFaction) boom(x, y, 0x80);
            return;
        }
        if (game_rand() % 2 != 0) {
            if (faction_id == CurrentPlayerFaction) NetMsg_pop(&NetMsg, "ARTYBOMBFAILED", 5000, 0, nullptr);
            return;
        }
        bombard = true;
        if (faction_id == CurrentPlayerFaction ||
            whose_territory(CurrentPlayerFaction, x, y, nullptr, 0) == CurrentPlayerFaction) {
            if (faction_id != 0 && faction_id != CurrentPlayerFaction) {
                boom(x, y, 0x80);
                boom(v.x, v.y, 0x80);
            }
            boom(x, y, 0x80);
        }
        int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
        if (owner != faction_id) {
            // defenders next to the target: artillery duel instead
            for (int32_t i = 1; i < 25; i++) {
                int32_t tx = map_wrap_x(TableOffsetX[i] + v.x);
                int32_t ty = TableOffsetY[i] + v.y;
                if (!map_in_bounds(tx, ty)) continue;
                int32_t dx = std::abs(tx - x);
                if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
                if ((int32_t)((std::abs(ty - y) + dx) & ~1u) < 5) {
                    int32_t s = stack_fix(veh_at(tx, ty));
                    if (s >= 0 && stack_check(s, 0xF, owner, -1, -1)) {
                        battle_fight(veh_id, i, 1, 1, 0);
                        return;
                    }
                }
            }
        }
    }
    const char* msg = nullptr;
    int32_t triad = veh_triad(veh_id);
    if (triad == 2 && !Chassis[Units[v.unit_id].chassis_id].missile && game_rand() % 4 == 0) {
        if (faction_id != CurrentPlayerFaction) goto done;
        msg = "BOMBFAILED";
    } else {
        uint32_t found;
        uint32_t removed = (uint32_t)items;
        if (items < 1) {
            found = map_tile(x, y)->items;
        } else {
            removed &= map_tile(x, y)->items;
            if (!removed) goto done;
            bit_set(x, y, removed, 0);
            found = removed;
        }
        if (!has_abil(v.unit_id, 0x2000)) found &= ~0x20u;
        for (int32_t i = -1; i < 19; i++) {
            int32_t k = i >= 0 ? i : 9;
            if (!(Terraform[k].bit & found)) continue;
            removed |= Terraform[k].bit;
            bit_set(x, y, Terraform[k].bit, 0);
            parse_says(2, Strings_get(&StringTable, map_is_ocean(x, y) ? Terraform[k].name_sea : Terraform[k].name), -1, -1);
            if (veh_triad(veh_id) == 2 && faction_id == CurrentPlayerFaction)
                NetMsg_pop(&NetMsg, "BOMBSUCCEEDED", 5000, 0, nullptr);
            synch_bit(x, y, faction_id);
            int32_t who = whose_territory(faction_id ? faction_id : CurrentPlayerFaction, x, y, nullptr, 0);
            synch_bit(x, y, who);
            if (!(removed & 0x1C00000)) {
                if (!(removed & 0x80000000u) || who != CurrentPlayerFaction) draw_tiles(x, y, 2);
                else draw_radius(x, y, 2, 2);
            } else {
                world_climate();
                draw_map(1);
            }
            if (faction_id == 0) {
                int32_t b;
                if ((!(game().factions[CurrentPlayerFaction].player_flags & PFLAG_MAP_REVEALED) &&
                     !(map_tile(x, y)->visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)))) ||
                    who != CurrentPlayerFaction ||
                    (b = base_find_filtered(x, y, -1, -1, -1, CurrentPlayerFaction)) < 0 ||
                    (uint8_t)game().bases[b].faction_id != CurrentPlayerFaction)
                    goto done;
                parse_says(0, game().bases[b].name, -1, -1);
                parse_says(1, Units[v.unit_id].name, -1, -1);
                Console_focus(&MainConsole, x, y, 0);
                if (k == 4) {
                    if (v.unit_id == 15) {
                        popp(ScriptFileName, "SPOREFOREST", 0, "sporlnch_sm.pcx", 0);
                        goto done;
                    }
                    msg = "ATEFOREST";
                } else {
                    if (v.unit_id == 15) {
                        popp(ScriptFileName, "SPORESLAUNCHED", 0, "sporlnch_sm.pcx", 0);
                        goto done;
                    }
                    msg = "ATESTUFF";
                }
            } else {
                if (bombard && faction_id == CurrentPlayerFaction)
                    NetMsg_pop(&NetMsg, "BOMBARDSUCCEEDED", 5000, 0, nullptr);
                if (who < 0 || who == faction_id || !(dstat(faction_id, who) & 8) ||
                    (!(game().factions[who].player_flags & PFLAG_MAP_REVEALED) &&
                     !(map_tile(x, y)->visibility & (uint8_t)(1 << (who & 31)))))
                    goto done;
                synch_bit(x, y, who);
                add_goal(who, 0, 5, v.x, v.y, -1);
                if (who != CurrentPlayerFaction) goto done;
                int32_t b = base_find_filtered(x, y, -1, -1, -1, CurrentPlayerFaction);
                set_faction_noun(0, faction_id);
                parse_says(1, game().bases[b].name, -1, -1);
                parse_says(3, game().mfactions[faction_id].adj_name_faction, -1, -1);
                msg = bombard ? "HAVEDESTROYED1" : "HAVEDESTROYED";
            }
            goto pop;
        }
        log_say_values("Arty Failed The Hard Way", (int32_t)removed, (int32_t)found, 0);
        if (bombard) {
            if (faction_id != CurrentPlayerFaction) goto done;
            msg = "ARTYBOMBFAILED";
        } else {
            if (veh_triad(veh_id) != 2 || faction_id != CurrentPlayerFaction) goto done;
            msg = "BOMBFAILED";
        }
    }
pop:
    NetMsg_pop(&NetMsg, msg, 5000, 0, nullptr);
done:
    order_veh(veh_id, -1, 3);
}

// ---- action_go_to, action_road_to, action ---------------------------------------------------------------

namespace {
void net_synch_unlock(int32_t veh_id) {
    synch_veh(veh_id);
    NetDaemon_await_synch(&NetState);
    NetDaemon_unlock_veh(&NetState);
}
}  // namespace

void GAMEAPI action_go_to(int32_t veh_id) {
    // as the original: the multiplayer daemon may renumber veh_id through this pointer
    if (MultiplayerActive && !ControlTurnC) NetVehIDPtr = &veh_id;
    VEH& v = game().vehs[veh_id];
    int32_t dir;
    if (v.order == ORDER_MOVE) dir = v.waypoint_x[0];
    else dir = Path_move(&Paths, veh_id, (v.order & 0x40) ? -1 : v.faction_id);
    if (dir < 0) {
        bool mp = MultiplayerActive != 0;
        int32_t ctc = ControlTurnC;
        v.order = ORDER_NONE;
        if (mp && !ctc) net_synch_unlock(veh_id);
        return;
    }
    if (MultiplayerActive && !ControlTurnC) {
        if (NetDaemon_order_veh(&NetState, veh_id, dir, 1)) NetDaemon_await_exec(&NetState, 1);
        return;
    }
    if (!order_veh(veh_id, dir, 3)) return;
    if (v.order == ORDER_MOVE) {
        if (v.x == v.waypoint_x[1] && v.y == v.waypoint_y[1]) v.order = ORDER_NONE;
        return;
    }
    if (v.order != ORDER_MOVE_TO || v.x != v.waypoint_x[0] || v.y != v.waypoint_y[0]) return;
    v.order = ORDER_NONE;
    if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) {
        // shift the waypoints (the original runs past waypoint_x[3]: reads waypoint_y[0] and the
        // bytes after waypoint_y[3])
        uint8_t* wp = reinterpret_cast<uint8_t*>(&v.waypoint_x[0]);
        if (v.waypoint_count) {
            int32_t i = 0;
            do {
                std::memcpy(wp + i * 2, wp + i * 2 + 2, 2);
                std::memcpy(wp + i * 2 + 8, wp + i * 2 + 10, 2);
                i++;
            } while (i < v.waypoint_count);
        }
        v.order = ORDER_MOVE_TO;
        uint8_t count = v.waypoint_count;
        v.waypoint_count = count - 1;
        if ((uint8_t)(count - 1) == 0) v.state &= 0xFDFEFDFF;
    }
    if (!is_human(v.faction_id) && Chassis[Units[v.unit_id].chassis_id].cargo) {
        MAP* sq = map_tile(v.x, v.y);
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) veh_skip(veh_id);
    }
}

void GAMEAPI action_road_to(int32_t veh_id) {
    // as the original: the multiplayer daemon may renumber veh_id through this pointer
    if (MultiplayerActive && !ControlTurnC) NetVehIDPtr = &veh_id;
    VEH& v = game().vehs[veh_id];
    int32_t faction_id = v.faction_id;
    int32_t wx = v.waypoint_x[0];
    int32_t y = v.y;
    int32_t wy = v.waypoint_y[0];
    int32_t x = v.x;
    MAP* sq = map_tile(x, y);
    uint8_t climate = sq->climate & 0xE0;
    int32_t mp;
    int32_t order, type;
    if (climate < 0x60 || wy < 0 || wy >= game().map.y || wx < 0 || wx >= game().map.x) {
        v.order = ORDER_NONE;
        mp = MultiplayerActive;
    } else {
        if ((sq->items & BIT_FUNGUS) && climate >= 0x40 && !has_tech(Rules.tech_preq_build_road_fungus, faction_id) &&
            has_tech(Terraform[10].preq_tech, faction_id) &&
            (!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7)) {
            if (!MultiplayerActive || ControlTurnC) {
                uint8_t o = v.order;
                action_terraform(veh_id, 10, 1);
                v.order = o;
                return;
            }
            order = (int8_t)v.order;
            type = 14;
            goto send;
        }
        sq = map_tile(x, y);
        if ((!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40 ||
             has_tech(Rules.tech_preq_build_road_fungus, faction_id)) &&
            (!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7)) {
            if (!(sq->items & BIT_ROAD)) {
                if (!MultiplayerActive || ControlTurnC) {
                    uint8_t o = v.order;
                    action_terraform(veh_id, 5, 1);
                    v.order = o;
                    return;
                }
                order = (int8_t)v.order;
                type = 9;
                goto send;
            }
            if (v.order == ORDER_MAGTUBE_TO && !(sq->items & BIT_MAGTUBE) &&
                has_tech(Terraform[6].preq_tech, faction_id)) {
                if (!MultiplayerActive || ControlTurnC) {
                    action_terraform(veh_id, 5, 1);
                    v.order = ORDER_MAGTUBE_TO;
                    return;
                }
                order = 0x1C;
                type = 9;
                goto send;
            }
        }
        if (x == wx && y == wy) {
            int32_t o = (int8_t)v.order;
            v.order = ORDER_NONE;
            if (MultiplayerActive && !ControlTurnC) net_synch_unlock(veh_id);
            if (v.state & VSTATE_ON_ALERT) return;
            if (faction_id != CurrentPlayerFaction) return;
            StrBuffer[0] = 0;
            say_loc(StrBuffer, wx, wy, 0, 2, 0);
            parse_says(0, StrBuffer, -1, -1);
            NetMsg_pop(&NetMsg, o == 0x1B ? "ROADTODONE" : "TUBETODONE", 5000, 0, nullptr);
            return;
        }
        int32_t flags = has_tech(Rules.tech_preq_build_road_fungus, faction_id) ? 0 : 0x10;
        int32_t dir = Path_find(&Paths, x, y, wx, wy, 0, -1, flags, -1);
        mp = MultiplayerActive;
        if (dir >= 0 && dir < 8) {
            if (MultiplayerActive && !ControlTurnC) {
                if (NetDaemon_order_veh(&NetState, veh_id, dir, 1)) NetDaemon_await_exec(&NetState, 1);
                return;
            }
            order_veh(veh_id, dir, 3);
            return;
        }
        v.order = ORDER_NONE;
    }
    if (mp && !ControlTurnC) net_synch_unlock(veh_id);
    return;
send:
    message_data(0x2405, 0, veh_id, type, order, 0);
    NetDaemon_await_exec(&NetState, 1);
}

int32_t GAMEAPI action(int32_t veh_id) {
    int8_t order = (int8_t)game().vehs[veh_id].order;
    if (order < 4 || order > 23) {
        if (order == 27 || order == 28) {
            action_road_to(veh_id);
        } else {
            if (order != 24 && order != 88 && order != 25) return 0;
            action_go_to(veh_id);
        }
    } else {
        action_terraform(veh_id, order - 4, 1);
    }
    if (game().vehs[veh_id].order & 0x40) game().vehs[veh_id].order = ORDER_NONE;
    return 1;
}

// ---- action_home --------------------------------------------------------------------------------------

int32_t GAMEAPI action_home(int32_t veh_id, int32_t mode) {
    VEH& v = game().vehs[veh_id];
    int32_t best = 9999;
    int32_t y = v.y;
    int32_t faction_id = v.faction_id;
    int32_t x = v.x;
    uint8_t region = map_tile(x, y)->region;
    int32_t bx = -1;
    int32_t by = -1;
    int32_t left = moves_left(veh_id);
    int32_t unit_id = v.unit_id;
    uint8_t range = Chassis[Units[unit_id].chassis_id].range;
    int32_t reach = speed(veh_id, 0) * ((range - v.movement_turns) - 1) + left;
    if (range == 1) {
        int32_t hp = unit_hp(unit_id) - v.damage_taken;
        if (hp < 0) hp = 0;
        else if (hp > 9999) hp = 9999;
        reach += speed(veh_id, 0) * ((hp - 1) / (int32_t)Units[v.unit_id].reactor_id);
    }
    reach = reach / Rules.move_rate_roads;
    for (int32_t i = 0; i < game().base_count; i++) {
        BASE& b = game().bases[i];
        if ((uint8_t)b.faction_id != faction_id && !(dstat(faction_id, (uint8_t)b.faction_id) & 1)) continue;
        int32_t triad = veh_triad(veh_id);
        bool ok;
        if (triad == 0) ok = map_tile(b.x, b.y)->region == region;
        else ok = triad != 1 || (b.x == x && b.y == y) || base_on_sea(i, region);
        if (!ok) continue;
        if (mode == 2 && (b.state_flags & 0x200000)) continue;
        int32_t d = map_dist(x, y, b.x, b.y);
        if ((v.state & VSTATE_ON_ALERT) && v.order_auto_type == ORDERA_ON_ALERT && b.x == v.waypoint_x[1] &&
            b.y == v.waypoint_y[1] && d <= reach) {
            best = d;
            bx = b.x;
            by = b.y;
            goto target;
        }
        if (d < best) {
            by = b.y;
            bx = b.x;
            best = d;
        }
    }
    if (veh_triad(veh_id) == 2) {
        for (int32_t j = 0; j < game().veh_count; j++) {
            VEH& w = game().vehs[j];
            if (w.faction_id != faction_id || j == veh_id) continue;
            if (!has_abil(w.unit_id, 0x80) && !(Units[w.unit_id].plan == 10 && unit_triad(w.unit_id) == 2)) continue;
            int32_t d = map_dist(x, y, w.x, w.y);
            if ((mode & 1) || mode == 4) {
                if (!is_human(faction_id)) d += speed(j, 0) / Rules.move_rate_roads;
                if (Units[w.unit_id].plan == 10) {
                    int32_t d2 = map_dist(x, y, w.x, w.y);
                    if (moves_left(veh_id) / Rules.move_rate_roads < d2) continue;
                }
            }
            if (d < best) {
                bx = w.x;
                by = w.y;
                best = d;
            }
        }
        int32_t n = TableRange[best > 7 ? 8 : best];
        for (int32_t k = 0; k < n; k++) {
            int32_t tx = map_wrap_x(TableOffsetX[k] + x);
            int32_t ty = TableOffsetY[k] + y;
            if (!map_in_bounds(tx, ty)) continue;
            MAP* sq = map_tile(tx, ty);
            uint32_t items = sq->items;
            if (!(items & 0x40000)) continue;
            if (items & 2) {
                int32_t o = sq->val2 & 0xF;
                if (o <= 7 && o != faction_id && !(dstat(faction_id, o) & 1)) continue;
            }
            if (!is_human(faction_id)) {
                if (whose_territory(faction_id, tx, ty, nullptr, 0) != faction_id) continue;
                int32_t o = map_tile(tx, ty)->val2 & 0xF;
                if (o > 7) o = -1;
                if (o != faction_id) continue;
            }
            int32_t d = map_dist(x, y, tx, ty);
            if ((v.state & VSTATE_ON_ALERT) && v.order_auto_type == ORDERA_ON_ALERT && v.damage_taken == 0 &&
                tx == v.waypoint_x[1] && ty == v.waypoint_y[1] && d <= reach) {
                // the original keeps the previous best distance here
                bx = tx;
                by = ty;
                goto target;
            }
            if (d < best) {
                bx = tx;
                by = ty;
                best = d;
            }
        }
    }
    if (((mode & 1) || mode == 4) && Chassis[Units[v.unit_id].chassis_id].range == 0) return 0;
target:
    if (by < 0 || by >= game().map.y || bx < 0 || bx >= game().map.x) return 0;
    if ((mode & 1) && best + 1 <= reach - 1) return 0;
    if (mode == 4) {
        if (reach < best) return 0;
    } else if (mode < 0) {
        return 1;
    }
    if (bx == x && by == y) {
        v.order = ORDER_NONE;
        if (!is_human(faction_id)) {
            veh_skip(veh_id);
            return 1;
        }
    } else {
        v.order = ORDER_MOVE_TO;
        v.waypoint_x[0] = (int16_t)bx;
        v.waypoint_y[0] = (int16_t)by;
        if (Chassis[Units[v.unit_id].chassis_id].range && is_human(faction_id) && reach < best) {
            v.order = ORDER_NONE;
            v.state &= 0xFCFFBDFF;
        }
    }
    return 1;
}

// ---- action_airdrop -----------------------------------------------------------------------------------

namespace {
// The air drop refusal messages (human players only).
int32_t drop_refused(int32_t faction_id, const char* label) {
    if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction) {
        if (game().mfactions[faction_id].rule_flags & 0x80) NetMsg_pop(&NetMsg, label, -5000, 0, "al_drop_sm.pcx");
        else NetMsg_pop(&NetMsg, label, -5000, 0, "airdp_sm.pcx");
    }
    return 0;
}
}  // namespace

int32_t GAMEAPI action_airdrop(int32_t veh_id, int32_t x, int32_t y, int32_t flags) {
    VehAttackFlags = flags;
    VEH& v = game().vehs[veh_id];
    int32_t faction_id = v.faction_id;
    int32_t dx = std::abs(v.x - x);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    int32_t dist2 = std::abs(v.y - y) + dx;
    if (drop_range(faction_id) < dist2 >> 1) {
        if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction)
            parse_num(0, Rules.max_airdrop_rng_wo_orbital_insert);
        return drop_refused(faction_id, "DROPRANGE");
    }
    MAP* sq = map_tile(x, y);
    if ((sq->climate & 0xE0) < 0x60) return drop_refused(faction_id, "DROPSEA");
    if (sq->items & BIT_VEH_IN_TILE) {
        int32_t o = sq->val2 & 0xF;
        if (o < 8 && o != faction_id && !(dstat(faction_id, o) & 1)) return drop_refused(faction_id, "DROPUNIT");
    }
    bool noncombat = Weapon[Units[v.unit_id].weapon_id].offense_value == 0;
    if (noncombat && zoc_move(x, y, faction_id)) return drop_refused(faction_id, "NONCOMDROP");
    sq = map_tile(x, y);
    if (sq->items & BIT_BASE_IN_TILE) {
        int32_t o = sq->val2 & 0xF;
        if (o < 8 && o != faction_id && !(dstat(faction_id, o) & 1)) {
            if (Weapon[Units[v.unit_id].weapon_id].offense_value == 0) return drop_refused(faction_id, "NONCOMDROP");
            if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction && break_treaty(faction_id, o, 7)) return 0;
        }
    }
    for (int32_t i = 0; i < 25; i++) {
        int32_t tx = map_wrap_x(TableOffsetX[i] + x);
        int32_t ty = TableOffsetY[i] + y;
        if (!map_in_bounds(tx, ty)) continue;
        MAP* t = map_tile(tx, ty);
        if (!(t->items & BIT_BASE_IN_TILE)) continue;
        int32_t o = t->val2 & 0xF;
        if (o >= 8 || o == 0 || o == faction_id || !(dstat(faction_id, o) & 0x10)) continue;
        int32_t b = base_at(tx, ty);
        if (b < 0) continue;
        if (!base_fac(b, 0x1D) &&
            !(SecretProjects[FAC_CLOUDBASE_ACADEMY - SP_ID_First] >= 0 &&
              (uint8_t)game().bases[SecretProjects[FAC_CLOUDBASE_ACADEMY - SP_ID_First]].faction_id == o) &&
            !stack_check(veh_at(tx, ty), 2, 4, -1, -1))
            continue;
        if ((VehAttackFlags & 1) && faction_id == CurrentPlayerFaction) {
            parse_says(0, game().bases[b].name, -1, -1);
            if (game().mfactions[faction_id].rule_flags & 0x80)
                NetMsg_pop(&NetMsg, "AIRDEFENSES", -5000, 0, "alair_sm.pcx");
            else NetMsg_pop(&NetMsg, "AIRDEFENSES", -5000, 0, "air_sm.pcx");
        }
        return 0;
    }
    if (!(VehAttackFlags & 2)) return 1;
    int32_t pod = goody_at(x, y);
    int32_t seen = faction_id == CurrentPlayerFaction ||
                   tile_veh_owner(map_tile(x, y)) == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW);
    int32_t show = 0;
    if (!seen) {
        for (int32_t i = 0; i < 8; i++) {
            int32_t tx = map_wrap_x(NearOffsetX[i] + x);
            int32_t ty = NearOffsetY[i] + y;
            if (map_in_bounds(tx, ty) && tile_veh_owner(map_tile(tx, ty)) == CurrentPlayerFaction) {
                seen = 1;
                break;
            }
        }
    }
    if (seen) {
        show = 1;
        if (faction_id != CurrentPlayerFaction) {
            uint32_t pref = (dstat(faction_id, CurrentPlayerFaction) & 1)
                                ? (GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ALLY_VEH)
                                : (GamePreferences & PREF_BSC_DONT_QUICK_MOVE_ENEMY_VEH);
            if (!pref) show = 0;
        }
    }
    int32_t best = 999999;
    int32_t dir = -1;
    for (int32_t d = 0; d < 8; d++) {
        int32_t tx = map_wrap_x(NearOffsetX[d] + x);
        int32_t ty = NearOffsetY[d] + y;
        if (!map_in_bounds(tx, ty)) continue;
        int32_t r = game_rand();
        int32_t score = r % 10 + 50;
        if (d > 5 || d < 1) score = r % 10 + 25;
        if (d == 5 || d == 1) score -= 10;
        MAP* t = map_tile(tx, ty);
        int32_t value = score;
        if ((t->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (t->val2 & 0xF) < 8) {
            int32_t o = t->val2 & 0xF;
            value = score + 100;
            if (o != faction_id) {
                value = score + 1100;
                if (!(dstat(faction_id, o) & 1)) value = score + 11100;
            }
        }
        if (value < best) {
            best = value;
            dir = d;
        }
    }
    if (dir < 0) return 0;
    if (seen && faction_id != CurrentPlayerFaction) {
        int32_t b = base_find_filtered(x, y, -1, -1, -1, 1);
        parse_says(0, game().bases[b].name, -1, -1);
        set_faction_noun(1, faction_id);
        const char* label;
        const char* pcx;
        if (!(game().mfactions[faction_id].rule_flags & 0x80)) {
            label = drop_range(faction_id) <= Rules.max_airdrop_rng_wo_orbital_insert ? "MADEAIRDROP" : "MADEORBITAL";
            pcx = "airdp_sm.pcx";
        } else {
            label = drop_range(faction_id) <= Rules.max_airdrop_rng_wo_orbital_insert ? "MADEAIRDROP" : "MADEORBITAL";
            pcx = "al_drop_sm.pcx";
        }
        popp(ScriptFileName, label, 0, pcx, 0);
    }
    int32_t drop_veh = veh_id;
    v.state |= VSTATE_MADE_AIRDROP;
    if (Weapon[Units[v.unit_id].weapon_id].offense_value == 0) veh_skip(veh_id);
    int32_t ox = v.x;
    int32_t oy = v.y;
    int32_t sx = map_wrap_x(NearOffsetX[dir] + x);
    int32_t sdy = NearOffsetY[dir];
    stack_veh(veh_id, 1);
    draw_tile(ox, oy, 2);
    if (seen && show) veh_scoot(veh_id, sx, sdy + y, dir ^ 4, 0);
    unspot_stack(veh_id);
    if (veh_id >= 0) {
        for (int32_t i = stack_top(veh_id); i >= 0; i = game().vehs[i].next_veh_id_stack)
            if (i != veh_id) veh_skip(i);
    }
    if (veh_id == MapWinVehA && MapWinInVehMode) Console_cursor_next(&MainConsole, x, y);
    stack_put(veh_id, x, y);
    int32_t b = base_at(x, y);
    VEH& dv = game().vehs[drop_veh];
    if (b < 0) {
        dv.damage_taken += Units[dv.unit_id].reactor_id * 2;
        while (true) {
            int32_t hp = unit_hp(dv.unit_id);
            uint8_t dmg = dv.damage_taken;
            int32_t rest = hp - dmg;
            if (rest >= 0 && (rest > 9999 || hp != dmg)) break;
            dv.damage_taken = dmg - 1;
        }
    } else if (faction_id != (uint8_t)game().bases[b].faction_id &&
               !(dstat(faction_id, (uint8_t)game().bases[b].faction_id) & 1)) {
        if (!is_human(faction_id)) veh_skip(veh_id);
        CurrentVehID = veh_id;
        uint8_t base_owner = game().bases[b].faction_id;
        dv.damage_taken = 0;
        act_of_aggression(faction_id, base_owner);
        capture_base(b, faction_id, 0);
        veh_id = CurrentVehID;
    }
    spot_all(veh_id, 1);
    Console_update_data(&MainConsole, 0);
    if (pod) goody_box(veh_id, pod);
    if (faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW) && seen && !MapWinField23BF0 &&
        !ui_shift_pressed() && !MultiplayerActive) {
        int32_t ms = (!show || MapWinField23BF0) ? 50 : 200;
        clock_wait(ms);
    }
    return 1;
}

// ---- action_feature, action_arty, action_destruct, action_oblit -------------------------------------------

void GAMEAPI action_feature() {
    if (MultiplayerActive) NetMsg_pop(&NetMsg, "FEATURE", 5000, 0, nullptr);
}

void GAMEAPI action_arty(int32_t veh_id, int32_t x, int32_t y) {
    if (game().vehs[veh_id].faction_id == CurrentPlayerFaction && !veh_ready(veh_id)) {
        NetMsg_pop(&NetMsg, "UNITMOVED", 5000, 0, nullptr);
        return;
    }
    VEH& v = game().vehs[veh_id];
    int32_t dx = std::abs(v.x - x);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    if ((int32_t)((std::abs(v.y - y) + dx) & ~1u) >= 5) {
        NetMsg_pop(&NetMsg, "OUTOFRANGE", 5000, 0, nullptr);
        return;
    }
    int32_t s = stack_fix(veh_at(x, y));
    if (s < 0) {
        if (!MultiplayerActive) {
            action_destroy(veh_id, 0, x, y);
            return;
        }
        if (!NetDaemon_lock_veh(&NetState, &veh_id, 4, x, y, 0)) {
            message_data(0x2413, 0, veh_id, x, y, 0);
            NetDaemon_await_exec(&NetState, 1);
        }
        return;
    }
    if (game().vehs[s].faction_id == game().vehs[veh_id].faction_id ||
        (dstat(game().vehs[veh_id].faction_id, game().vehs[s].faction_id) & 1))
        return;
    int32_t dir =
        radius_move_tiles(game().vehs[veh_id].x, game().vehs[veh_id].y, x, y, TableRange[Rules.artillery_max_rng]);
    if (dir < 0) return;
    if (!MultiplayerActive) {
        VehAttackFlags = 3;
        battle_fight(veh_id, dir, 1, 1, 0);
        return;
    }
    if (!NetDaemon_maybe_lock(&NetState, &veh_id, 0, 0, x, y, 4)) {
        VehAttackFlags = 1;
        // battle_fight returns battle_fight_2's result (declared void in battle.h): its body inline
        int32_t tx = map_wrap_x(game().vehs[veh_id].x + TableOffsetX[dir]);
        if (battle_fight_2(veh_id, dir, tx, game().vehs[veh_id].y + TableOffsetY[dir], 1, 1, nullptr)) {
            NetDaemon_unlock_veh(&NetState);
            return;
        }
        message_veh(0x2402, veh_id, dir, 0);
        NetDaemon_await_exec(&NetState, 1);
    }
}

void GAMEAPI action_destruct(int32_t veh_id) {
    int16_t y = game().vehs[veh_id].y;
    int16_t x = game().vehs[veh_id].x;
    boom(x, y, 0);
    int32_t w = weap_val(game().vehs[veh_id].unit_id, game().vehs[veh_id].faction_id);
    if (w < 1) w = 1;
    else if (w > 20) w = 20;
    uint8_t reactor = Units[game().vehs[veh_id].unit_id].reactor_id;
    kill(veh_id);
    for (int32_t i = 0; i < 9; i++) {
        int32_t tx = map_wrap_x(TableOffsetX[i] + x);
        int32_t ty = TableOffsetY[i] + y;
        if (!map_in_bounds(tx, ty) || base_at(tx, ty) >= 0) continue;
        for (int32_t j = veh_at(tx, ty); j >= 0; j = game().vehs[j].next_veh_id_stack)
            game().vehs[j].damage_taken += (uint8_t)((int32_t)(reactor * w) / 2);
        while (true) {
            int32_t s = stack_fix(veh_at(tx, ty));
            while (s >= 0 && unit_hp(game().vehs[s].unit_id) > game().vehs[s].damage_taken)
                s = game().vehs[s].next_veh_id_stack;
            if (s < 0) break;
            stack_veh(s, 1);
            stack_kill(s);
        }
    }
}

void GAMEAPI action_oblit(int32_t veh_id, int32_t base_id) {
    int32_t faction_id = game().vehs[veh_id].faction_id;
    BASE& b = game().bases[base_id];
    int32_t former = (uint8_t)b.faction_id_former;
    parse_num(0, b.pop_size * 10);
    parse_says(0, b.name, -1, -1);
    set_leader_title(1, CurrentPlayerFaction);
    parse_says(2, game().mfactions[former].adj_name_faction, -1, -1);
    set_leader_title(3, faction_id);
    set_leader_name(4, faction_id);
    bool seen = (uint8_t)b.faction_id == CurrentPlayerFaction ||
                (b.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)));
    base_kill(base_id);
    draw_map(1);
    if (faction_id == CurrentPlayerFaction) popp(ScriptFileName, "OBLITTED", 0, "baseobl_sm.pcx", 0);
    else if (former == CurrentPlayerFaction || seen) popp(ScriptFileName, "OBLITTED2", 0, "baseobl_sm.pcx", 0);
    if (Rules.tgl_oblit_base_atrocity) atrocity(faction_id, former, 2, 0);
}

// ---- valid_patrol, action_patrol ------------------------------------------------------------------------

int32_t GAMEAPI valid_patrol(int32_t veh_id, int32_t x, int32_t y) {
    VEH& v = game().vehs[veh_id];
    if (x == v.x && y == v.y) return 0;
    if (!map_in_bounds(x, y)) return 0;
    int32_t triad = veh_triad(veh_id);
    if (triad == 0) return map_tile(x, y)->region == map_tile(v.x, v.y)->region;
    if (triad == 1) {
        int32_t b0 = base_at(v.x, v.y);
        int32_t b1 = base_at(x, y);
        int32_t r;
        if (b0 < 0) {
            if (b1 < 0) return map_tile(x, y)->region == map_tile(v.x, v.y)->region;
            r = base_on_sea(b1, map_tile(v.x, v.y)->region);
        } else if (b1 >= 0) {
            r = port_to_port(b0, b1);
        } else {
            r = base_on_sea(b0, map_tile(x, y)->region);
        }
        return r != 0;
    }
    if (triad != 2) return 1;
    if (!Chassis[Units[v.unit_id].chassis_id].range) return 1;
    // BUG(original): compares base_at() with 0 (base id 0 counts as no base, no base as a base).
    bool from_base = base_at(v.x, v.y) != 0 || (map_tile(v.x, v.y)->items & BIT_AIRBASE);
    bool to_base = base_at(x, y) != 0 || (map_tile(x, y)->items & BIT_AIRBASE);
    int32_t left = moves_left(veh_id);
    int32_t reach =
        (speed(veh_id, 0) * ((Chassis[Units[v.unit_id].chassis_id].range - v.movement_turns) - 1) + left) /
        Rules.move_rate_roads;
    int32_t d = map_dist(x, y, v.x, v.y);
    if (from_base && to_base) return d <= reach;
    return d <= reach / 2;
}

void GAMEAPI action_patrol(int32_t veh_id, int32_t x, int32_t y) {
    if (!valid_patrol(veh_id, x, y)) return;
    VEH& v = game().vehs[veh_id];
    // waypoint arrays indexed by the count without a range check, as the original
    uint8_t* wp = reinterpret_cast<uint8_t*>(&v.waypoint_x[0]);
    int16_t sx = (int16_t)x, sy = (int16_t)y;
    v.order = ORDER_MOVE_TO;
    std::memcpy(wp + v.waypoint_count * 2, &sx, 2);
    std::memcpy(wp + 8 + v.waypoint_count * 2, &sy, 2);
    uint8_t count = (uint8_t)(v.waypoint_count + 1);
    v.waypoint_count = count;
    std::memcpy(wp + count * 2, &v.x, 2);
    std::memcpy(wp + 8 + v.waypoint_count * 2, &v.y, 2);
    v.state |= 0x1000200;
    v.patrol_current_point = 0;
}

// ---- shoot_it -----------------------------------------------------------------------------------------

int32_t GAMEAPI shoot_it(int32_t faction_id_atk, int32_t faction_id_def, int32_t x, int32_t y, int32_t is_fungal) {
    // flechette defence of the defender's (any non-attacker) bases within two tiles
    int32_t flechette = 0;
    for (int32_t i = 0; i < 25; i++) {
        if (flechette) continue;
        int32_t tx = map_wrap_x(TableOffsetX[i] + x);
        int32_t ty = TableOffsetY[i] + y;
        if (!map_in_bounds(tx, ty)) continue;
        int32_t b = base_at(tx, ty);
        if (b >= 0 && (uint8_t)game().bases[b].faction_id != faction_id_atk && base_fac(b, 0x27)) flechette = 1;
    }
    Faction& def = game().factions[faction_id_def];
    if (!def.satellites_ODP && !flechette) return 0;
    int32_t shot = 0;
    int32_t avail = def.satellites_ODP - def.ODP_deployed;
    for (int32_t i = 0; i < avail; i++) {
        def.ODP_deployed++;
        if (game_rand() % 100 >= 50) {
            shot = 1;
            break;
        }
    }
    int32_t left = def.satellites_ODP - def.ODP_deployed;
    if (left < 0) left = 0;
    else if (left > 9999) left = 9999;
    const char* label;
    const char* pcx;
    if (!shot) {
        for (int32_t i = 0; i < 25; i++) {
            int32_t tx = map_wrap_x(TableOffsetX[i] + x);
            int32_t ty = TableOffsetY[i] + y;
            if (!map_in_bounds(tx, ty)) continue;
            int32_t b = base_at(tx, ty);
            if (b < 0 || (uint8_t)game().bases[b].faction_id == faction_id_atk) continue;
            if (!base_fac(b, 0x27) || game_rand() % 100 >= 50) continue;
            parse_says(0, game().bases[b].name, -1, -1);
            if (!is_fungal) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    label = "TWESHOTIT2";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 1;
                    parse_says(1, game().mfactions[(uint8_t)game().bases[b].faction_id].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    label = "TTHEYSHOTIT2";
                }
            } else if (faction_id_def == CurrentPlayerFaction) {
                parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                pcx = "space_sm.pcx";
                label = "FWESHOTIT2";
            } else {
                if (faction_id_atk != CurrentPlayerFaction) return 1;
                parse_says(1, game().mfactions[(uint8_t)game().bases[b].faction_id].adj_name_faction, -1, -1);
                pcx = "space_sm.pcx";
                label = "FTHEYSHOTIT2";
            }
            popp(ScriptFileName, label, 0, pcx, 0);
            return 1;
        }
        bool failed = false;
        if (!def.satellites_ODP) {
            failed = true;
        } else {
            // every orbital defence pod missed: one is sacrificed to stop the missile
            if (!MultiplayerActive && is_human(faction_id_def)) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_num(0, def.satellites_ODP);
                    parse_says(0, game().bases[base_find(x, y)].name, -1, -1);
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    if (popp(ScriptFileName, is_fungal ? "FUNGALSAC" : "TECTONICSAC", 0, "space_sm.pcx", 0))
                        failed = true;
                } else if (faction_id_atk == CurrentPlayerFaction) {
                    set_faction_noun(0, faction_id_def);
                    popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
                }
            } else if (faction_id_def != CurrentPlayerFaction) {
                if (faction_id_atk == CurrentPlayerFaction) {
                    set_faction_noun(0, faction_id_def);
                    popp(ScriptFileName, "THEYMUSTSAC", 0, "space_sm.pcx", 0);
                }
            } else {
                popp(ScriptFileName, is_fungal ? "FWEMUSTSAC" : "TWEMUSTSAC", 0, "space_sm.pcx", 0);
            }
            if (!failed) {
                int32_t n = --def.satellites_ODP;
                if (n - def.ODP_deployed < 0) def.ODP_deployed = n;
            }
        }
        if (failed) {
            if (flechette) {
                if (faction_id_def == CurrentPlayerFaction) {
                    parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    label = "WEFAILEDFLECHETTE";
                } else {
                    if (faction_id_atk != CurrentPlayerFaction) return 0;
                    parse_says(1, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
                    pcx = "space_sm.pcx";
                    label = "THEYFAILEDFLECHETTE";
                }
                popp(ScriptFileName, label, 0, pcx, 0);
            }
            return 0;
        }
    }
    int32_t b = base_find_faction(x, y, faction_id_def);
    if (b < 0) b = base_find(x, y);
    parse_says(0, game().bases[b].name, -1, -1);
    parse_num(0, def.satellites_ODP);
    parse_num(1, left);
    if (!is_fungal) {
        if (faction_id_def == CurrentPlayerFaction) {
            parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
            pcx = "space_sm.pcx";
            label = "TWESHOTIT";
        } else {
            if (faction_id_atk != CurrentPlayerFaction) return 1;
            parse_says(1, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
            pcx = "space_sm.pcx";
            label = "TTHEYSHOTIT";
        }
    } else if (faction_id_def == CurrentPlayerFaction) {
        parse_says(1, game().mfactions[faction_id_atk].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        label = "FWESHOTIT";
    } else {
        if (faction_id_atk != CurrentPlayerFaction) return 1;
        parse_says(1, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
        pcx = "space_sm.pcx";
        label = "FTHEYSHOTIT";
    }
    popp(ScriptFileName, label, 0, pcx, 0);
    return 1;
}

// ---- action_tectonic, action_fungal ------------------------------------------------------------------------

void GAMEAPI action_tectonic(int32_t veh_id, int32_t x, int32_t y) {
    int32_t focused = 0;
    int32_t shot = 0;
    FX_play_loc(&Sounds, 0x53, game().vehs[veh_id].x, game().vehs[veh_id].y);
    int32_t reactor = Units[game().vehs[veh_id].unit_id].reactor_id;
    int32_t radius = TableRange[reactor];
    int32_t base_id = base_find(x, y);
    int32_t viewer = CurrentPlayerFaction;
    if (game().vehs[veh_id].faction_id != viewer &&
        ((game().factions[viewer].player_flags & PFLAG_MAP_REVEALED) ||
         (map_tile(x, y)->visibility & (uint8_t)(1 << (viewer & 31)))) &&
        base_id >= 0 && (uint8_t)game().bases[base_id].faction_id == CurrentPlayerFaction) {
        Console_focus(&MainConsole, x, y, game().vehs[veh_id].faction_id);
        focused = 1;
    }
    int32_t faction_id = game().vehs[veh_id].faction_id;
    kill(veh_id);
    if (faction_id != CurrentPlayerFaction && base_id >= 0) {
        set_leader_title(0, faction_id);
        set_leader_name(1, faction_id);
        set_faction_noun(2, faction_id);
        parse_says(3, game().bases[base_id].name, -1, -1);
        popp(ScriptFileName, "TECTONICMISSILE", 0, "tectonic_sm.pcx", 0);
    }
    int32_t tried[8] = {};
    for (int32_t i = 0; i < radius; i++) {
        int32_t tx = map_wrap_x(TableOffsetX[i] + x);
        int32_t ty = TableOffsetY[i] + y;
        if (!map_in_bounds(tx, ty)) continue;
        int32_t s = stack_fix(veh_at(tx, ty));
        if (s < 0) continue;
        int32_t o = game().vehs[s].faction_id;
        if (faction_id == o || (dstat(faction_id, o) & 1) || tried[o]) continue;
        shot = shoot_it(faction_id, o, x, y, 0);
        if (shot) break;
        tried[o] = 1;
    }
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (owner >= 0 && owner != faction_id && !tried[owner]) shot = shoot_it(faction_id, owner, x, y, 0);
    if (shot) return;
    for (int32_t i = reactor; i > 0; i--) world_raise_alt(x, y);
    world_climate();
    draw_map(1);
    // BUG(original): Vehs[veh_id] was removed by kill(); these read the next vehicle's faction.
    if (focused) {
        parse_says(0, game().bases[base_id].name, -1, -1);
        set_leader_title(1, game().vehs[veh_id].faction_id);
        set_leader_name(2, game().vehs[veh_id].faction_id);
        set_faction_noun(3, game().vehs[veh_id].faction_id);
        popp(ScriptFileName, "TECMOFIED", 0, "tectonic_sm.pcx", 0);
    }
    TectonicDetonationCount[game().vehs[veh_id].faction_id]++;
    GameDrawState |= 4;
}

void GAMEAPI fungal_tile(int32_t x, int32_t y) {
    bit_set(x, y, 0x20, 1);
    bit_set(x, y, 0x28805C, 0);
    bit_set(x, y, FungusClearItems, 0);
    if ((map_tile(x, y)->climate & 0xE0) < 0x40) world_alt_set(x, y, 2, 0);
    if ((game().factions[CurrentPlayerFaction].player_flags & PFLAG_MAP_REVEALED) ||
        (map_tile(x, y)->visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31))) ||
        (GameState & STATE_OMNISCIENT_VIEW)) {
        synch_bit(x, y, CurrentPlayerFaction);
        draw_tile(x, y, 2);
    }
}

void GAMEAPI action_fungal(int32_t veh_id, int32_t x, int32_t y) {
    int32_t focused = 0;
    int32_t shot = 0;
    int32_t faction_id = game().vehs[veh_id].faction_id;
    int32_t base_id = base_find(x, y);
    int32_t radius = TableRange[1];
    FX_play_loc(&Sounds, 0x54, game().vehs[veh_id].x, game().vehs[veh_id].y);
    kill(veh_id);
    if (faction_id != CurrentPlayerFaction && base_id >= 0) {
        set_leader_title(0, faction_id);
        set_leader_name(1, faction_id);
        set_faction_noun(2, faction_id);
        parse_says(3, game().bases[base_id].name, -1, -1);
        popp(ScriptFileName, "FUNGALMISSILE", 0, "fungpayld_sm.pcx", 0);
    }
    int32_t tried[8] = {};
    for (int32_t i = 0; i < radius; i++) {
        int32_t tx = map_wrap_x(TableOffsetX[i] + x);
        int32_t ty = TableOffsetY[i] + y;
        if (!map_in_bounds(tx, ty)) continue;
        int32_t s = stack_fix(veh_at(tx, ty));
        if (s < 0) continue;
        int32_t o = game().vehs[s].faction_id;
        if (faction_id == o || (dstat(faction_id, o) & 1) || tried[o]) continue;
        shot = shoot_it(faction_id, o, x, y, 1);
        if (shot) break;
        tried[o] = 1;
    }
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (owner >= 0 && owner != faction_id && !tried[owner]) shot = shoot_it(faction_id, owner, x, y, 1);
    if (shot) return;
    // BUG(original): Vehs[veh_id] was removed by kill(); the reactor and faction read below are the
    // next vehicle's.
    int32_t count = Units[game().vehs[veh_id].unit_id].reactor_id * 2;
    for (int32_t i = 0; i < count; i++) {
        int32_t k = 0;
        if (i != 0) k = game_rand() % 8 + 1;
        int32_t tx = map_wrap_x(TableOffsetX[k] + x);
        int32_t ty = TableOffsetY[k] + y;
        if (!map_in_bounds(tx, ty)) continue;
        if (game_rand() % 2 != 0 && tile_veh_owner(map_tile(tx, ty)) == -1) veh_init(BSC_MIND_WORMS, 0, tx, ty);
        MAP* sq = map_tile(tx, ty);
        if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) continue;
        int32_t f = game().vehs[veh_id].faction_id;
        if (f != CurrentPlayerFaction &&
            ((game().factions[CurrentPlayerFaction].player_flags & PFLAG_MAP_REVEALED) ||
             (sq->visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)))) &&
            base_id >= 0 && (uint8_t)game().bases[base_id].faction_id == CurrentPlayerFaction) {
            Console_focus(&MainConsole, tx, ty, f);
            focused = 1;
        }
        bit_set(tx, ty, 0x20, 1);
        bit_set(tx, ty, 0x28805C, 0);
        bit_set(tx, ty, FungusClearItems, 0);
        if ((map_tile(tx, ty)->climate & 0xE0) < 0x40) world_alt_set(tx, ty, 2, 0);
        if ((game().factions[CurrentPlayerFaction].player_flags & PFLAG_MAP_REVEALED) ||
            (map_tile(tx, ty)->visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31))) ||
            (GameState & STATE_OMNISCIENT_VIEW)) {
            synch_bit(tx, ty, CurrentPlayerFaction);
            draw_tile(tx, ty, 2);
        }
    }
    if ((map_tile(x, y)->climate & 0xE0) >= 0x60) veh_init(BSC_FUNGAL_TOWER, 0, x, y);
    GameDrawState |= 4;
    if (focused) {
        parse_says(0, game().bases[base_id].name, -1, -1);
        set_leader_title(1, game().vehs[veh_id].faction_id);
        set_leader_name(2, game().vehs[veh_id].faction_id);
        set_faction_noun(3, game().vehs[veh_id].faction_id);
        popp(ScriptFileName, "FUNGMOTIZED", 0, "fungpayld_sm.pcx", 0);
    }
}

// ---- action_give, action_gate, action_sat_attack ----------------------------------------------------------

void GAMEAPI action_give(int32_t veh_id, int32_t faction_id) {
    int32_t y = game().vehs[veh_id].y;
    int32_t x = game().vehs[veh_id].x;
    uint8_t old = game().vehs[veh_id].faction_id;
    if (veh_id >= 0) {
        for (int32_t i = stack_top(veh_id); i >= 0; i = game().vehs[i].next_veh_id_stack) {
            VEH& w = game().vehs[i];
            if (i != veh_id && !(w.order == 1 && w.waypoint_x[0] == veh_id)) continue;
            int32_t unit_id;
            if (w.unit_id < 64) {
                unit_id = w.unit_id;
            } else {
                // an equivalent prototype of the new owner, a new one, or a similar one
                const UNIT& u = Units[w.unit_id];
                int32_t base = faction_id * 64;
                unit_id = -1;
                for (int32_t k = 0; k < 64; k++) {
                    const UNIT& p = Units[base + k];
                    if ((p.unit_flags & UNIT_ACTIVE) && p.chassis_id == u.chassis_id && p.weapon_id == u.weapon_id &&
                        p.armor_id == u.armor_id && p.ability_flags == u.ability_flags && p.reactor_id == u.reactor_id) {
                        unit_id = base + k;
                        break;
                    }
                }
                if (unit_id < 0) {
                    const UNIT& v = Units[w.unit_id];
                    unit_id = propose_proto(faction_id, v.chassis_id, v.weapon_id, v.armor_id, v.ability_flags,
                                            v.reactor_id, (uint8_t)v.group_id, nullptr);
                    if (unit_id < 0) {
                        for (int32_t k = 0; k < 64; k++) {
                            const UNIT& p = Units[base + k];
                            const UNIT& q = Units[w.unit_id];
                            if ((p.unit_flags & UNIT_ACTIVE) && p.chassis_id == q.chassis_id &&
                                p.weapon_id == q.weapon_id && p.armor_id == q.armor_id) {
                                unit_id = base + k;
                                break;
                            }
                        }
                        if (unit_id < 0) {
                            for (int32_t k = 0; k < 64; k++) {
                                const UNIT& p = Units[base + k];
                                if ((p.unit_flags & UNIT_ACTIVE) && p.plan == Units[w.unit_id].plan) {
                                    unit_id = base + k;
                                    break;
                                }
                            }
                            if (unit_id < 0) {
                                for (int32_t k = 0; k < 64; k++) {
                                    const UNIT& p = Units[base + k];
                                    if ((p.unit_flags & UNIT_ACTIVE) && p.chassis_id == Units[w.unit_id].chassis_id) {
                                        unit_id = base + k;
                                        break;
                                    }
                                }
                                if (unit_id < 0) unit_id = 3 - (unit_triad(w.unit_id) != 1);
                            }
                        }
                    }
                }
            }
            w.faction_id = (uint8_t)faction_id;
            w.unit_id = (int16_t)unit_id;
            w.order = 0;
            w.state &= 0xFCFFBDFF;
            int32_t home = base_find_faction(x, y, faction_id);
            if (home < 0) home = -1;
            w.home_base_id = (int16_t)home;
            MAP* sq = map_tile(x, y);
            if (!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7) owner_set(x, y, faction_id);
        }
    }
    draw_radius(x, y, 3, 2);
    if (faction_id == CurrentPlayerFaction) {
        parse_says(0, Units[game().vehs[veh_id].unit_id].name, -1, -1);
        set_leader_title(1, old);
        set_leader_name(2, old);
        set_faction_noun(3, old);
        Console_focus(&MainConsole, x, y, old);
        int32_t b = base_find(x, y);
        if (b >= 0) {
            parse_says(4, game().bases[b].name, -1, -1);
            if (GameLanguage || is_human(old)) {
                NetMsg_pop(&NetMsg, "GOTUNIT", 5000, 0, nullptr);
                return;
            }
            if (game().factions[faction_id].region_base_plan[map_tile(x, y)->region] != 2 &&
                Units[game().vehs[veh_id].unit_id].plan != 2) {
                X_pop("GOTUNITOFFEND", 0);
                return;
            }
            X_pop("GOTUNITDEFEND", 0);
        }
    } else if (old == CurrentPlayerFaction) {
        set_faction_noun(3, faction_id);
        NetMsg_pop(&NetMsg, faction_id ? "GAVEUNIT" : "GAVEWILD", 5000, 0, nullptr);
    }
}

void GAMEAPI action_gate(int32_t veh_id, int32_t base_id) {
    int32_t from = base_at(game().vehs[veh_id].x, game().vehs[veh_id].y);
    if (from >= 0) game().bases[from].state_flags |= BSTATE_PSI_GATE_USED;
    game().bases[base_id].state_flags |= BSTATE_PSI_GATE_USED;
    veh_drop(veh_lift(veh_id), game().bases[base_id].x, game().bases[base_id].y);
    if (from >= 0) draw_tile(game().bases[from].x, game().bases[from].y, 2);
    draw_tile(game().bases[base_id].x, game().bases[base_id].y, 2);
    if (veh_id == MapWinVehA)
        Console_focus(&MainConsole, game().vehs[veh_id].x, game().vehs[veh_id].y, game().vehs[veh_id].faction_id);
}

void GAMEAPI action_sat_attack(int32_t faction_id, int32_t faction_id_def, int32_t type, int32_t base_id) {
    if (!(dstat(faction_id, faction_id_def) & 0x10)) double_cross(faction_id, faction_id_def, -1);
    if (faction_id == CurrentPlayerFaction || faction_id_def == CurrentPlayerFaction) {
        set_leader_title(0, faction_id);
        set_leader_name(1, faction_id);
        set_faction_noun(2, faction_id);
        set_leader_title(3, faction_id_def);
        set_leader_name(4, faction_id_def);
        set_faction_noun(5, faction_id_def);
        parse_say(6, type == 4 ? Facility[41].name : Facility[65 + type].name, -1, -1);
        parse_says(7, game().mfactions[faction_id_def].adj_name_faction, -1, -1);
    }
    const char* label;
    if (game_rand() % 100 < 50) {
        if (type == 4) set_fac(FAC_GEOSYNC_SURVEY_POD, base_id, 0);
        else (&game().factions[faction_id_def].satellites_nutrient)[type]--;
        game().factions[faction_id].ODP_deployed++;
        if (faction_id == CurrentPlayerFaction) label = "WEKILLEDIT";
        else if (faction_id_def == CurrentPlayerFaction) label = "THEYKILLEDIT";
        else label = nullptr;
    } else {
        game().factions[faction_id].satellites_ODP--;
        if (faction_id == CurrentPlayerFaction) label = "WEMUFFEDIT";
        else if (faction_id_def == CurrentPlayerFaction) label = "THEYMUFFEDIT";
        else label = nullptr;
    }
    if (label) popp(ScriptFileName, label, 0, "satbat_sm.pcx", 0);
    if (ReportWinKind == 6 && InfcList[InfcUsed] == 5) ReportIf_on_redraw(&ReportWin);
}

// ---- mon_colony_founded -------------------------------------------------------------------------------

void GAMEAPI mon_colony_founded(int32_t faction_id, const char* base_name) {
    int32_t year = game_year(CurrentTurn);
    MonumentEvent& e = Monuments[faction_id].events[0];
    if ((game().mfactions[faction_id].rule_flags & 0x80) || e.achieved) return;
    e.prev_count = 0;
    for (int32_t i = 0; i < 8; i++)
        if (i != faction_id && Monuments[i].events[0].achieved) e.prev_count++;
    int32_t who = faction_id;
    if (!e.prev_count) {
        e.prev_count = 1;
    } else {
        e.prev_count = 0;
        for (int32_t i = 0; i < 8; i++) {
            const MonumentEvent& o = Monuments[i].events[0];
            if (i != faction_id && o.achieved && o.prev_count) who = i;
        }
    }
    e.faction_id = who;
    e.achieved = 1;
    e.year = year;
    std::strncpy(Monuments[faction_id].base_name, base_name, 24);
    Monuments[faction_id].latest_event_id = 0;
    if (faction_id == CurrentPlayerFaction) monument(1);
}

// ---- small callees of other modules (diplo_act 0x50C2E0, diplomacy 0x53A030) ------------------------------

int32_t GAMEAPI act_of_aggression(int32_t faction_id, int32_t faction_id_with) {
    int32_t had_treaty =
        (game().factions[faction_id_with].diplo_status[faction_id] & (DIPLO_PACT | DIPLO_TREATY | DIPLO_TRUCE)) != 0;
    if (had_treaty) double_cross(faction_id, faction_id_with, -1);
    treaty_on(faction_id, faction_id_with, 0x8010);
    return had_treaty;
}

void GAMEAPI cause_friction(int32_t faction_id, int32_t faction_id_with, int32_t amount) {
    int32_t& f = game().factions[faction_id].diplo_friction[faction_id_with];
    int32_t v = f + amount;
    if (v < 0) v = 0;
    else if (v > 20) v = 20;
    f = v;
    if (DiploFrictionFactionID == faction_id && DiploFrictionFactionIDWith == faction_id_with) DiploFriction += amount;
}
