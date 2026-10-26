#include "engine/goody.h"

#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/diplomacy.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/prefs.h"
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

inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1 << (faction_id & 31))) != 0; }

// Owner of the units at a tile (val2 low nibble when the tile has units or a base), or -1.
inline int32_t tile_veh_owner(MAP* sq) {
    if (!(sq->items & 3)) return -1;
    int32_t owner = sq->val2 & 0xF;
    return owner > 7 ? -1 : owner;
}

inline int32_t dist2(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
    return std::abs(y1 - y2) + dx;
}

void set_leader_title(int32_t n, int32_t faction_id) {
    GenderDefault = MFactions[faction_id].is_leader_female;
    PluralDefault = 0;
    parse_says(n, MFactions[faction_id].title_leader, -1, -1);
}
void set_leader_name(int32_t n, int32_t faction_id) {
    GenderDefault = MFactions[faction_id].is_leader_female;
    PluralDefault = 0;
    parse_says(n, MFactions[faction_id].name_leader, -1, -1);
}
void set_faction_noun(int32_t n, int32_t faction_id) {
    GenderDefault = MFactions[faction_id].noun_gender;
    PluralDefault = MFactions[faction_id].is_noun_plural;
    parse_says(n, MFactions[faction_id].noun_faction, -1, -1);
}

// Sites of the factions that see a monolith at (x, y) (visible_items bit 0x2000).
void add_monolith_sites(int32_t x, int32_t y) {
    for (int32_t f = 1; f < 8; f++)
        if (map_tile(x, y)->visible_items[f - 1] & 0x2000) add_site(f, 0, 2, x, y);
}

// CharLowerA on an ASCII text.
void lower_text(char* s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + ('a' - 'A'));
}

}  // namespace

// ---- tut_check ------------------------------------------------------------------------------------------

int32_t GAMEAPI tut_check(int32_t hint) {
    if (MultiplayerActive) return 0;
    if (!(GamePreferences & 0x20)) return 0;
    if (hint & TutHintsShown) return 0;
    if (MFactions[CurrentPlayerFaction].rule_flags & 0x80) return 0;
    TutHintsShown |= hint;
    return 1;
}

// ---- want_monolith, monolith ------------------------------------------------------------------------------

int32_t GAMEAPI want_monolith(int32_t veh_id) {
    if (Vehs[veh_id].state & 0x100) return 0;
    return morale_veh(veh_id, 1, 0) < 6 && Vehs[veh_id].morale < 6 &&
           Weapon[Units[Vehs[veh_id].unit_id].weapon_id].offense_value != 0;
}

void GAMEAPI monolith(int32_t veh_id) {
    VEH& v = Vehs[veh_id];
    int32_t unit_id = v.unit_id;
    int32_t faction_id = v.faction_id;
    int32_t x = v.x;
    int32_t y = v.y;
    if (unit_id == 16 || unit_id == 17 || unit_id == 18) return;
    if (faction_id == CurrentPlayerFaction && !MultiplayerActive) {
        if (v.order == 0x18 && v.waypoint_x[0] == x && v.waypoint_y[0] == y) v.order = 0;
        if (v.order) return;
    }
    const char* label;
    int32_t delay;
    if (Weapon[Units[v.unit_id].weapon_id].offense_value == 0) {
        if (v.damage_taken == 0) {
            if (v.state & 0x100) return;
            v.state |= 0x100;
            if (faction_id != CurrentPlayerFaction || (GameMorePreferences & 0x800) || MultiplayerActive) return;
            label = "SEEMONOLITH";
            delay = -5000;
        } else {
            v.damage_taken = 0;
            veh_skip(veh_id);
            draw_tile(x, y, 2);
            if (faction_id != CurrentPlayerFaction) return;
            label = "MONOLITHHEAL";
            delay = 5000;
        }
        NetMsg_pop(&NetMsg, label, delay, 0, "monolith_sm.pcx");
        GameState |= 4;
        return;
    }
    for (int32_t f = 1; f < 8; f++) del_site(f, 0, x, y, 0);
    if (!is_human(faction_id)) {
        if ((v.state & 0x100) || morale_veh(veh_id, 1, 0) > 5 || v.morale > 5 ||
            Weapon[Units[v.unit_id].weapon_id].offense_value == 0) {
            if (v.damage_taken) {
                v.damage_taken = 0;
                veh_skip(veh_id);
                draw_tile(x, y, 2);
            }
            add_monolith_sites(x, y);
            return;
        }
    } else if (!MultiplayerActive && !(GameMorePreferences & 0x800)) {
        FX_play(&Sounds, 6);
        int32_t choice = popp(ScriptFileName, "MONOLITH", 0, "monolith_sm.pcx", 0);
        FX_stop(&Sounds, 6);
        if (choice == 2) {
            GameMorePreferences |= 0x800;
            prefs_save(0);
        } else if (choice == 0) {
            add_monolith_sites(x, y);
            return;
        }
    }
    if (v.damage_taken) {
        v.damage_taken = 0;
        veh_skip(veh_id);
        draw_tile(x, y, 2);
        if ((v.state & 0x100) || morale_veh(veh_id, 1, 0) > 5) {
            if (faction_id != CurrentPlayerFaction) return;
            delay = (GameMorePreferences & 0x800) ? 5000 : -5000;
            NetMsg_pop(&NetMsg, "MONOLITHHEAL", delay, 0, "monolith_sm.pcx");
            GameState |= 4;
            return;
        }
    }
    if (!(v.state & 0x100) && morale_veh(veh_id, 1, 0) < 6) {
        int32_t keep = game_rand() % 32;
        if (!(map_tile(x, y)->items & 0x2000000) || CurrentTurn < 100) {
            keep = 1;
            bit_set(x, y, 0x2000000, 1);
        }
        v.state |= 0x100;
        uint32_t morale = v.morale + 1u;
        if (morale > 6) morale = 6;
        v.morale = (uint8_t)morale;
        veh_skip(veh_id);
        StrBuffer[0] = 0;
        say_morale_2(veh_id, 0);
        parse_says(0, StrBuffer, -1, -1);
        lower_text(ParseStrBuffer[0]);
        if (keep == 0) {
            // the monolith crumbles
            bit_set(x, y, 0x2000, 0);
            synch_bit(x, y, faction_id);
        } else {
            add_monolith_sites(x, y);
        }
        draw_tile(x, y, 2);
        if (faction_id == CurrentPlayerFaction) {
            FX_play(&Sounds, 0x22);
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, "MONOLITH");
            std::strcat(StrBuffer, keep != 0 ? "1" : "0");
            delay = (GameMorePreferences & 0x800) ? 5000 : -5000;
            NetMsg_pop(&NetMsg, StrBuffer, delay, 0, "monolith_sm.pcx");
            GameState |= 4;
        }
    } else if (faction_id == CurrentPlayerFaction) {
        if (GameMorePreferences & 0x800) NetMsg_pop(&NetMsg, "MONOLITHNO", 5000, 0, "monolith_sm.pcx");
        else NetMsg_pop(&NetMsg, "MONOLITHNO", -5000, 0, "monolith_sm.pcx");
    }
}

// ---- goody_box -------------------------------------------------------------------------------------------

int32_t GAMEAPI goody_box(int32_t veh_id, int32_t) {
    int32_t retry = 0;
    int32_t y = Vehs[veh_id].y;
    int32_t x = Vehs[veh_id].x;
    int32_t faction_id = Vehs[veh_id].faction_id;
    int32_t is_sea = (map_tile(x, y)->climate & 0xE0) < 0x60;
    if (do_unity_crash(x, y, faction_id)) return 0;
    bool viewer = faction_id == CurrentPlayerFaction;
    Faction& fac = Factions[faction_id];
    int32_t opened = fac.goody_opened;
    fac.goody_opened = opened + 1;
    if (MultiplayerActive) game_srand(MapRandomSeed + (opened + 1) * 37);
    if (fac.SE_research_pending * -5 <= CurrentTurn && !(GameRules & 0x4000000)) fac.tech_accumulated++;
    bool forced = false;  // a scenario-placed pod (items 0x4000000/0x8000000)
    int32_t kind;
    {
        int32_t b = base_find(x, y);
        MAP* sq = map_tile(x, y);
        uint32_t items = sq->items;
        if (!(items & 0xC000000) || (fac.base_count && (b < 0 || (uint8_t)Bases[b].faction_id != faction_id))) {
            if (BaseFindDist < 3 && b >= 0 && (uint8_t)Bases[b].faction_id != faction_id) {
                retry = 1;
                kind = 16;
            } else {
                if ((items & 0x20) && (sq->climate & 0xE0) >= 0x40) goto reroll;
                if (is_sea) goto reroll;
                int32_t n = 40;
                if (BaseFindDist >= 13 && fac.region_total_bases[sq->region]) n = 20;
                int32_t plan = Units[Vehs[veh_id].unit_id].plan;
                if ((plan == 9 && (BaseFindDist < 12 || CurrentTurn < 40)) || plan == 8) n += 10;
                kind = n <= 1 ? 0 : game_rand() % n;
            }
        } else {
            kind = 10 - ((items & 0x4000000) != 0);
            forced = true;
            bit_set(x, y, 0xC000000, 0);
        }
    }
dispatch:
    if (Units[Vehs[veh_id].unit_id].plan == 12 && game_rand() % 3 == 0 && !(Vehs[veh_id].flags & 0x20)) {
    vanish:
        if (viewer) NetMsg_pop(&NetMsg, is_sea ? "GOODYWHIRLPOOL" : "GOODYVANISH", 5000, 0, nullptr);
        kill(veh_id);
        return 1;
    }
    if (kind > 18) {
        if (kind == 9999) goto vanish;
        goto resource;
    }
    if (kind == 18) {
        // clone of the unit
        bool go;
        if (!is_sea) go = Weapon[Units[Vehs[veh_id].unit_id].weapon_id].offense_value != 0;
        else go = game_rand() % 4 == 0;
        if (go && Units[Vehs[veh_id].unit_id].plan != 12) {
            if (MultiplayerActive) {
                bool any = false;
                for (int32_t i = 1; i < 8; i++) {
                    uint32_t bit = 1u << i;
                    if (!(bit & FactionStatus[1]) || !(FactionStatus[0] & (uint8_t)bit) || i == faction_id) continue;
                    if (!is_sea) {
                        if (fac.total_combat_units <= Factions[i].total_combat_units) any = true;
                    } else if (fac.unk_70 <= Factions[i].unk_70) {
                        any = true;
                    }
                    if (Factions[i].total_combat_units < fac.total_combat_units / 2) goto reroll;
                }
                if (!any) goto reroll;
            }
            int32_t nv = veh_init(Vehs[veh_id].unit_id, faction_id, x, y);
            if (nv >= 0) {
                Vehs[nv].home_base_id = -1;
                Vehs[nv].morale = Vehs[veh_id].morale;
                Vehs[nv].damage_taken = Vehs[veh_id].damage_taken;
                parse_says(0, Units[Vehs[veh_id].unit_id].name, -1, -1);
                if (viewer) NetMsg_pop(&NetMsg, "GOODYCLONE", -5000, 0, "clone_sm.pcx");
                return 0;
            }
        }
        goto reroll;
    }
    switch (kind) {
    case 0: {
        // energy
        int32_t r = game_rand() % (is_sea + 3);
        int32_t amount = (CurrentTurn >= (is_sea ? 100 : 50) ? 50 : 25) * (r + 1);
        fac.energy_credits += amount;
        if (!viewer) return 0;
        parse_num(0, amount);
        FX_play(&Sounds, 0x16);
        NetMsg_pop(&NetMsg, is_sea ? "GOODYHYDROGEN" : "GOODYURANIUM", -5000, 0, is_sea ? nullptr : "supply_sm.pcx");
        FX_fade(&Sounds, 0x16);
        InfoWin_on_redraw(&InfoWindow);
        return 0;
    }
    case 1:
        // river source
        if (!is_sea && !(map_tile(x, y)->items & 0x80)) {
            for (int32_t i = 0; i < 9; i++) {
                int32_t tx = map_wrap_x(TableOffsetX[i] + x);
                int32_t ty = TableOffsetY[i] + y;
                if (map_in_bounds(tx, ty) && ((map_tile(tx, ty)->items & 0x80) || map_is_ocean(tx, ty))) goto reroll;
            }
            if (viewer) {
                FX_play(&Sounds, 0x26);
                NetMsg_pop(&NetMsg, "GOODYRIVER", 5000, 0, "supply_sm.pcx");
            }
            bit_set(x, y, 0x100, 1);
            world_climate();
            draw_map(1);
            return 0;
        }
        break;
    case 2:
        // map of the surroundings
        if (is_sea || (map_tile(x, y)->climate & 0xE0) > 0x9F) {
            for (int32_t i = 0; i < 121; i++) {
                int32_t tx = map_wrap_x(TableOffsetX[i] + x);
                int32_t ty = TableOffsetY[i] + y;
                if (!map_in_bounds(tx, ty)) continue;
                if (!is_sea || (map_is_ocean(tx, ty) && map_tile(x, y)->region == map_tile(tx, ty)->region))
                    spot_loc(tx, ty, faction_id);
            }
            if (!viewer) return 0;
            FX_play(&Sounds, 0x16);
            NetMsg_pop(&NetMsg, is_sea ? "GOODYSONAR" : "GOODYVIEW", 5000, 0, "supply_sm.pcx");
            FX_fade(&Sounds, 0x16);
            draw_map(1);
            return 0;
        }
        if (CurrentTurn < 50) {
            kind = 8;
            goto dispatch;
        }
        break;
    case 3: {
        // earthquake
        if ((GameRules & 0x1000000) && DiffLevel <= 1) break;
        if ((map_tile(x, y)->climate & 0xE0) > 0x80 || fac.base_count <= 1 || is_sea || MultiplayerActive) break;
        int32_t n = fac.goody_earthquake * 2 + 2;
        if (!(n == 1 || fac.goody_earthquake * 2 + 1 < 0 || game_rand() % n == 0)) break;
        fac.goody_earthquake++;
        FX_play(&Sounds, 8);
        world_raise_alt(x, y);
        world_raise_alt(x, y);
        for (int32_t i = 0; i < 25; i++) {
            int32_t tx = map_wrap_x(TableOffsetX[i] + x);
            int32_t ty = TableOffsetY[i] + y;
            if (!map_in_bounds(tx, ty)) continue;
            if ((map_tile(tx, ty)->items & 4) && game_rand() % 3 == 0) {
                bit_set(tx, ty, 4, 0);
                synch_bit(tx, ty, faction_id);
            }
            if (i < 9) rocky_set(tx, ty, minerals_at(tx, ty));
        }
        world_climate();
        draw_map(1);
        if (!viewer) return 0;
        NetMsg_pop(&NetMsg, "GOODYQUAKE", -5000, 0, "quake_sm.pcx");
        if (!tut_check(0x800)) return 0;
        TutWin_tut_map(&TutWin, "TERRAFORM", x, y, -1, nullptr, 0, -1, -1);
        return 0;
    }
    case 4: {
        // teleport
        if (!is_sea && (GameState & 0x100000)) {
            kind = 7;
            goto dispatch;
        }
        int32_t plan = Units[Vehs[veh_id].unit_id].plan;
        if (MultiplayerActive || plan == 12 || ((GameRules & 0x1000000) && DiffLevel <= 1)) break;
        if (CurrentTurn < 25 || (CurrentTurn < 50 && DiffLevel < 2)) {
            kind = 8;
            goto dispatch;
        }
        int32_t tries = 0;
        uint8_t region = map_tile(x, y)->region;
        int32_t max_dist;
        if (veh_triad(veh_id) == 0 && plan != 8) {
            max_dist = plan != 9 ? 24 : 8;
        } else {
            if (game_rand() % 2 == 0) break;
            max_dist = 8;
        }
        int32_t tx, ty;
        do {
            do {
                while (true) {
                    tx = MapAreaX <= 1 ? 0 : game_rand() % MapAreaX;
                    ty = MapAreaY <= 1 ? 0 : game_rand() % MapAreaY;
                    if (tx & 1) tx--;
                    if (ty & 1) tx++;
                    if (++tries > 999) goto reroll;
                    MAP* sq = map_tile(tx, ty);
                    if (((sq->climate & 0xE0) < 0x60) != (is_sea != 0)) continue;
                    if (sq->region != region) continue;
                    if ((sq->items & 3) && (sq->val2 & 0xF) < 8) continue;
                    if (zoc_any(tx, ty, faction_id)) continue;
                    if (fac.player_flags & 0x200) continue;
                    if (map_tile(tx, ty)->visibility & (uint8_t)(1 << (faction_id & 31))) continue;
                    break;
                }
            } while ((int32_t)(dist2(x, y, tx, ty) & ~1u) < 9);
        } while (max_dist < dist2(x, y, tx, ty) >> 1);
        if (viewer) NetMsg_pop(&NetMsg, is_sea ? "GOODYWAVE" : "GOODYGATE", -5000, 0, is_sea ? nullptr : "stars_sm.pcx");
        VEH& v = Vehs[veh_id];
        v.moves_spent = 0;
        v.order = 0;
        v.state &= 0xFCFFFDFF;
        stack_put(stack_veh(veh_id, 1), tx, ty);
        if (Vehs[veh_id].faction_id == CurrentPlayerFaction)
            draw_tile_fixup(x, y, has_abil(Vehs[veh_id].unit_id, 2), 2);
        else
            draw_tile(x, y, 2);
        if (viewer) {
            Console_focus(&MainConsole, tx, ty, faction_id);
            GraphicWin_redraw(&WorldWin);
        }
        spot_all(veh_id, 1);
        return 0;
    }
    case 5: {
        // production boost
        if ((GameState & 0x100000) && !is_sea) {
            kind = 7;
            goto dispatch;
        }
        int32_t b = base_find_faction(x, y, faction_id);
        if (!MultiplayerActive && b >= 0) {
            int32_t item = Bases[b].queue_items[0];
            if (item > -70) {
                int32_t cost;
                bool skip = false;
                if (item < 0) {
                    cost = Facility[-item].cost;
                    parse_say(0, Facility[-item].name, -1, -1);
                } else if (Units[item].plan == 8 && Bases[b].pop_size == 1) {
                    skip = true;
                    cost = 0;
                } else {
                    cost = veh_cost(item, b, nullptr);
                    parse_says(0, Units[item].name, -1, -1);
                }
                if (skip) break;
                int32_t cf = cost_factor(faction_id, 1, -1);
                int32_t t = cf * cost * 3;
                if (Bases[b].minerals_accumulated < t / 4) {
                    Bases[b].minerals_accumulated = cf * cost;
                    if (!viewer) return 0;
                    parse_says(1, Bases[b].name, -1, -1);
                    NetMsg_pop(&NetMsg, "GOODYBUILD", -5000, 0, "indbm_sm.pcx");
                    return 0;
                }
            }
        }
        if (is_sea || (GameRules & 0x40000)) break;
        int32_t artifacts = fac.units_active[7];
        for (int32_t i = 0; i < BaseCount; i++)
            if ((uint8_t)Bases[i].faction_id == faction_id && (Bases[i].state_flags & 0x400)) artifacts++;
        if (artifacts < fac.base_count || (GameState & 0x40000)) goto artifact;
        break;
    }
    case 6:
    artifact:
        if (GameRules & 0x40000) break;
        if (retry > 1 && kind == 6) {
            kind = 5;
            goto dispatch;
        }
        if (is_sea && stack_check(veh_id, 8, -1, -1, -1) <= 0) break;
        if (MultiplayerActive && !(GameState & 0x40000)) {
            for (int32_t i = 1; i < 8; i++)
                if (i != faction_id && is_human(i) && Factions[i].goody_artifact < fac.goody_artifact) goto reroll;
        }
        fac.goody_artifact++;
        if (Units[Vehs[veh_id].unit_id].plan == 12) break;
        {
            int32_t nv = veh_init(7, faction_id, x, y);
            if (nv < 0) return 0;
            veh_skip(nv);
            draw_tile(x, y, 2);
            if (!viewer) return 0;
            NetMsg_pop(&NetMsg, "GOODYARTIFACT", -5000, 0, "art_dis_sm.pcx");
            FX_fade(&Sounds, 0x1B);
            return 0;
        }
    case 7: {
        // fungus bloom
        if ((GameRules & 0x200000) || ((GameRules & 0x1000000) && DiffLevel <= 1) || retry >= 2) break;
        if (is_sea) {
            kind = 8;
            goto dispatch;
        }
        base_find(x, y);
        if (BaseFindDist <= 2) break;
        if (MultiplayerActive && faction_id == RankingFactionIDUnk2 && game_rand() % 2 == 0) break;
        bool spawned = false;
        bool may_spawn = false;
        if (game_rand() % 2 != 0 && ExpansionEnabled) may_spawn = true;
        if (viewer) {
            FX_play(&Sounds, 9);
            NetMsg_pop(&NetMsg, "GOODYFUNGUS", -5000, 0, "fung_sm.pcx");
        }
        site_radius(x, y);
        for (int32_t i = 0; i < 9; i++) {
            int32_t tx = map_wrap_x(TableOffsetX[i] + x);
            int32_t ty = TableOffsetY[i] + y;
            if (!map_in_bounds(tx, ty) || (map_tile(tx, ty)->climate & 0xE0) < 0x40) continue;
            bit_set(tx, ty, 0x20, 1);
            bit_set(tx, ty, 0x28805C, 0);
            synch_bit(tx, ty, faction_id);
            if (!spawned && may_spawn && game_rand() % 2 != 0) {
                MAP* sq = map_tile(tx, ty);
                if (tile_veh_owner(sq) == -1 && ExpansionEnabled && (sq->climate & 0xE0) >= 0x60) {
                    veh_init(19, 0, tx, ty);
                    may_spawn = false;
                    spawned = true;
                }
            }
            draw_tile(tx, ty, 2);
        }
        return 0;
    }
    case 8:
        // monolith
        if ((GameRules & 0x20000) || bonus_at(x, y) != 0 || is_sea) break;
        site_radius(x, y);
        bit_set(x, y, 0x2000, 1);
        bit_set(x, y, 0x20, 0);
        synch_bit(x, y, faction_id);
        draw_tiles(x, y, 2);
        monolith(veh_id);
        return 0;
    case 9: {
        // Unity rover, chopper or foil (battle ogres in the expansion)
        if ((GameMoreRules & 1) || TechOwners[77]) break;
        if (is_sea && stack_check(veh_id, 8, -1, -1, -1) <= 0) break;
        int32_t unit_id = 11;
        if (TechOwners[38] && game_rand() % 3 == 0 && !fac.units_active[12]) {
            base_find_faction(x, y, faction_id);
            if (BaseFindDist <= speed_proto(12) * 3) unit_id = 12;
        }
        uint8_t foils = fac.units_active[13];
        int32_t n = foils + 3;
        if (Continents[map_tile(x, y)->region].tile_count < 51) n = foils + 2;
        if (foils < 2 && (n <= 1 || game_rand() % n == 0)) {
            if (!is_sea) {
                for (int32_t d = 0; d < 8; d++) {
                    int32_t tx = map_wrap_x(NearOffsetX[d] + x);
                    int32_t ty = NearOffsetY[d] + y;
                    if (map_in_bounds(tx, ty) && map_is_ocean(tx, ty) &&
                        Continents[map_tile(tx, ty)->region].tile_count > 0x4F) {
                        x = tx;
                        y = ty;
                        unit_id = 13;
                        break;
                    }
                }
            } else {
                unit_id = 13;
            }
        }
        if (fac.units_active[unit_id] &&
            ((fac.base_count + 1) / 2 < fac.units_active[11] + fac.units_active[12] + fac.units_active[13] ||
             fac.units_active[unit_id] > 1)) {
            kind = game_rand() % 2 + 12;
            x = Vehs[veh_id].x;
            y = Vehs[veh_id].y;
            goto dispatch;
        }
        if (unit_id == 11 && game_rand() % 5 == 0 && ExpansionEnabled) {
            int32_t level = 0;
            for (int32_t f = 1; f < 8; f++) {
                if (has_tech(82, f) && level == 0) level = 1;
                if (has_tech(83, f)) level = 2;
            }
            unit_id = 16;
            if (level != 0) unit_id = 17;
            if (level > 1) unit_id = 18;
        }
        int32_t nv = veh_init(unit_id, faction_id, x, y);
        if (nv < 0) break;
        if (forced || CurrentTurn < 50) Vehs[nv].home_base_id = -1;
        Vehs[nv].morale = 2;
        if (unit_id == 16) Vehs[nv].morale = 2;
        else if (unit_id == 17) Vehs[nv].morale = 4;
        else if (unit_id == 18) Vehs[nv].morale = 6;
        if (is_sea && veh_triad(nv) == 0) sleep(nv);
        if (!viewer) return 0;
        parse_says(0, Units[unit_id].name, -1, -1);
        if (unit_id != 16 && unit_id != 17 && unit_id != 18)
            NetMsg_pop(&NetMsg, "GOODYPOD", is_sea ? 5000 : -5000, 0, "supply_sm.pcx");
        else
            NetMsg_pop(&NetMsg, "OGREPOD", is_sea ? 5000 : -5000, 0, "ogre_sm.pcx");
        return 0;
    }
    case 10: {
        // commlink frequency or a technology
        if (GameMoreRules & 2) break;
        if (game_rand() % 4 == 0) {
            int32_t o = game_rand() % 7 + 1;
            uint32_t bit = 1u << o;
            if (o != faction_id && !(Factions[faction_id].diplo_status[o] & 8) && (FactionStatus[1] & (uint8_t)bit) &&
                !(FactionStatus[0] & bit) && is_human(faction_id) && !(MFactions[o].rule_flags & 0x80)) {
                treaty_on(faction_id, o, 8);
                if (!viewer) return 0;
                set_leader_title(0, o);
                set_leader_name(1, o);
                set_faction_noun(2, o);
                NetMsg_pop(&NetMsg, "GOODYCOMM", is_sea ? 5000 : -5000, 0, "supply_sm.pcx");
                return 0;
            }
        }
        if (GameRules & 0x4000000) break;
        if (MultiplayerActive && !forced) {
            for (int32_t i = 1; i < 8; i++) {
                if (i != faction_id && is_human(i) && Factions[i].goody_tech + 1 < fac.goody_tech &&
                    (Factions[i].ranking < fac.ranking || Factions[i].tech_ranking < fac.tech_ranking))
                    goto reroll;
            }
        }
        int32_t r = game_rand();
        for (int32_t i = 0; i < 89; i++) {
            int32_t t = (r % 89 + i) % 89;
            if (has_tech(t, faction_id) || !tech_avail(t, faction_id)) continue;
            if (tech_is_preq(t, 48, 9999) && !TechOwners[48]) continue;
            int32_t basic = 0;
            for (int32_t k = 0; k < 2; k++) {
                int32_t p = k == 0 ? Tech[t].preq_tech1 : Tech[t].preq_tech2;
                if (p == -1) basic++;
                if (p >= 0 && Tech[p].preq_tech1 == -1 && Tech[p].preq_tech2 == -1) {
                    basic++;
                    break;
                }
            }
            if (basic <= 1) continue;
            if (i < 89 && t != 78 && t != 79 && t != 81 && t != 82 && t != 83) {
                if (!forced) fac.goody_tech++;
                if (viewer) {
                    StrBuffer[0] = 0;
                    std::strcat(StrBuffer, "GOODYDATA");
                    if (MultiplayerActive) {
                        std::strcat(StrBuffer, "1");
                        parse_say(0, Tech[t].name, -1, -1);
                    }
                    NetMsg_pop(&NetMsg, StrBuffer, is_sea ? 5000 : -5000, 0, "supply_sm.pcx");
                }
                tech_achieved(faction_id, t, 0, 0);
                if (!forced) return 0;
                fac.tech_ranking--;
                return 0;
            }
            break;
        }
        break;
    }
    case 11: {
        // forest, farm, mine, solar collectors (land) or kelp (sea)
        MAP* sq = map_tile(x, y);
        if ((sq->region & 0x3F) == 0x3F || (sq->region & 0x3F) == 0 || (!is_sea && (sq->val3 & 0xC0) > 0x40)) break;
        if ((sq->climate & 0xE0) < 0x40) {
            kind = 12;
            goto dispatch;
        }
        int32_t type = 0;
        if (!is_sea) {
            base_find(x, y);
            if (BaseFindDist < 3) break;
            type = game_rand() % 4;
        }
        site_radius(x, y);
        int32_t count = 0;
        for (int32_t i = 0; i < 9; i++) {
            if (i != 0 && game_rand() % ((type > 1 ? -1 : 0) + 3) == 0) continue;
            int32_t tx = map_wrap_x(TableOffsetX[i] + x);
            int32_t ty = TableOffsetY[i] + y;
            if (!map_in_bounds(tx, ty)) continue;
            if (i != 0 && (map_tile(tx, ty)->items & 0x100000)) continue;
            MAP* t = map_tile(tx, ty);
            if (!is_sea) {
                uint8_t rocky = t->val3 & 0xC0;
                if ((t->climate & 0xE0) < 0x60 || !(rocky <= 0x40 || type > 1) || !(rocky != 0 || type != 2)) continue;
            } else {
                uint8_t c = t->climate & 0xE0;
                if (c >= 0x60) continue;
                if (c < 0x40) {
                    if (i != 0) continue;
                    alt_set_both(tx, ty, 2);
                }
            }
            bit_set(tx, ty, 0x288070, 0);
            if (!is_sea) {
                uint32_t clear;
                switch (type) {
                case 0: bit_set(tx, ty, 0x200000, 1); clear = 0x8050; break;
                case 1: bit_set(tx, ty, 0x8000, 1); clear = 0x200000; break;
                case 2: bit_set(tx, ty, 0x14, 1); clear = 0x200040; break;
                default: bit_set(tx, ty, 0x40, 1); clear = 0x200010; break;
                }
                bit_set(tx, ty, clear, 0);
            } else {
                bit_set(tx, ty, 0x8000, 1);
            }
            synch_bit(tx, ty, faction_id);
            draw_tile(tx, ty, 2);
            count++;
        }
        if (!count) break;
        if (!viewer) return 0;
        if (!is_sea) {
            switch (type) {
            case 0: NetMsg_pop(&NetMsg, "GOODYFOREST", -5000, 0, "forgr_sm.pcx"); break;
            case 1: NetMsg_pop(&NetMsg, "GOODYFARM", -5000, 0, "forgr_sm.pcx"); break;
            case 2: NetMsg_pop(&NetMsg, "GOODYMINE", -5000, 0, "supply_sm.pcx"); break;
            case 3: NetMsg_pop(&NetMsg, "GOODYSOLAR", -5000, 0, "supply_sm.pcx"); break;
            }
            return 0;
        }
        NetMsg_pop(&NetMsg, "GOODYKELP", -5000, 0, "kelp_sm.pcx");
        return 0;
    }
    default: {
        // 12..17: native life
        bool spore = false;
        if ((GameRules & 0x200000) || ((GameRules & 0x1000000) && DiffLevel <= 1)) break;
        base_find_faction(x, y, faction_id);
        int32_t own_dist = BaseFindDist;
        base_find(x, y);
        if (BaseFindDist <= 1) break;
        if (kind > 13) {
            MAP* sq = map_tile(x, y);
            int32_t m = ((sq->items & 0x20) && (sq->climate & 0xE0) >= 0x40) ? 2 : 4;
            if ((((GameState & 0x100000) != 0) + 1) * own_dist < (kind - 13) * m + (4 - MapNativeLifeForms) * 2) break;
        }
        if (MultiplayerActive && faction_id == RankingFactionIDUnk2 && game_rand() % 2 == 0) break;
        if (is_sea) {
            if ((int32_t)(((GameState >> 20) & 1) + 12) < kind) break;
            Vehs[veh_id].moves_spent += (uint8_t)Rules.move_rate_roads;
        }
        int32_t count = 0;
        int32_t i = 0;
        do {
            int32_t d = (i + CurrentTurn) % 8;
            int32_t tx = map_wrap_x(NearOffsetX[d] + x);
            int32_t ty = NearOffsetY[d] + y;
            if (map_in_bounds(tx, ty)) {
                MAP* t = map_tile(tx, ty);
                if (((t->climate & 0xE0) < 0x60) == (is_sea != 0) && (!(t->items & 3) || (t->val2 & 0xF) > 7)) {
                    if (!is_sea) {
                        base_find(tx, ty);
                        if (BaseFindDist > 1) {
                            int32_t nv;
                            if (game_rand() % 5 == 0 && ExpansionEnabled) {
                                nv = veh_init(15, 0, tx, ty);
                                spore = true;
                            } else {
                                nv = veh_init(8, 0, tx, ty);
                            }
                            if (nv >= 0) {
                                count++;
                                Vehs[nv].visibility |= map_tile(tx, ty)->visibility;
                                draw_tile(tx, ty, 2);
                                if ((int32_t)(((GameState >> 20) & 1) + 12) < kind || CurrentTurn < 15 ||
                                    BaseFindDist < 4)
                                    break;
                            }
                        }
                    } else {
                        int32_t nv = veh_init(9, 0, tx, ty);
                        if (nv >= 0) {
                            count++;
                            Vehs[nv].visibility |= map_tile(tx, ty)->visibility;
                            int32_t n = game_rand() % 2 + 1;
                            if (veh_cargo(nv) <= n) n = veh_cargo(nv);
                            for (; n > 0; n--) {
                                if (game_rand() % 5 == 0 && ExpansionEnabled) {
                                    veh_init(15, 0, tx, ty);
                                    spore = true;
                                } else {
                                    veh_init(8, 0, tx, ty);
                                }
                            }
                            break;
                        }
                    }
                }
            }
            int32_t step = 1;
            if (count) {
                step = 5 - fac.base_count / 2;
                if (step < 1) step = 1;
                else if (step > 4) step = 4;
            }
            i += step;
        } while (i < 8);
        if (!count) break;
        if (!viewer) return 0;
        FX_play(&Sounds, 0x11);
        const char* label;
        const char* pcx;
        if (spore) {
            label = is_sea ? "GOODYISLE" : "GOODYWORMS";
            pcx = is_sea ? "isle_sm.pcx" : "sporlnch_sm.pcx";
        } else {
            label = is_sea ? "GOODYISLE" : "GOODYWORMS";
            pcx = is_sea ? "isle_sm.pcx" : "mindworm_sm.pcx";
        }
        NetMsg_pop(&NetMsg, label, -5000, 0, pcx);
        FX_fade(&Sounds, 0x11);
        return 0;
    }
    }
    goto reroll;
resource:
    // resource bonus at the tile (also the fallback of every failed outcome)
    if (retry > 1) return 0;
    {
        MAP* sq = map_tile(x, y);
        if ((!is_sea || (sq->climate & 0xE0) >= 0x40) && (!(sq->items & 0x20) || (sq->climate & 0xE0) < 0x40) &&
            y > 1 && y < MapAreaY - 2 && !(GameRules & 0x10000)) {
            bit_set(x, y, 0x400, 1);
            int32_t bonus = bonus_at(x, y);
            site_radius(x, y);
            if (viewer && bonus) {
                if (bonus == 1) {
                    wave_it(0x28);
                } else if (bonus == 2) {
                    wave_it(0x2A);
                } else if (bonus == 3) {
                    ambience(0xD2);
                    wave_it(0x29);
                }
                parse_say(0, TextLabels[91 + bonus], -1, -1);
                NetMsg_pop(&NetMsg, "GOODYRESOURCE", 5000, 0, "supply_sm.pcx");
            }
            draw_tile(x, y, 2);
            if (game_rand() % 3 == 0) return 0;
            if (retry) return 0;
            retry = 2;
        }
    }
reroll:
    x = Vehs[veh_id].x;
    y = Vehs[veh_id].y;
    kind = game_rand() % 14;
    if (retry == 0) {
        retry = 1;
    } else {
        if (retry > 2) return 0;
        if (retry == 2) retry = 3;
    }
    if ((y == 0 || y == MapAreaY - 1) && game_rand() % 3 == 0) kind = 12;
    if (game_rand() % 100 == 0) kind = 18;
    goto dispatch;
}

// ---- study_artifact ---------------------------------------------------------------------------------------

int32_t GAMEAPI study_artifact(int32_t veh_id) {
    char label[80];
    label[0] = 0;
    int16_t x = Vehs[veh_id].x;
    int32_t y = Vehs[veh_id].y;
    int32_t faction_id = Vehs[veh_id].faction_id;
    std::strcat(label, "ARTIFACTTECH");
    int32_t b = base_at(x, y);
    if (SecretProjects[20] != b && b >= 0 && (Bases[b].state_flags & 0x400)) {
        if (!(Bases[b].state_flags & 0x800) && faction_id == CurrentPlayerFaction) {
            parse_says(0, Bases[b].name, -1, -1);
            NetMsg_pop(&NetMsg, "ALREADYARTIFACT", -5000, 0, "art_dis_sm.pcx");
            Bases[b].state_flags |= 0x800;
        }
        return 0;
    }
    veh_kill(veh_id);
    draw_tile(x, y, 2);
    if (faction_id == CurrentPlayerFaction) {
        FX_play(&Sounds, 0x23);
        popp(ScriptFileName, label, 0, "art_dis_sm.pcx", 0);
    }
    Faction& f = Factions[faction_id];
    if (!(f.player_flags & 0x10)) f.player_flags |= 0x10;
    else f.player_flags |= 4;
    if (b >= 0) Bases[b].state_flags |= 0x400;
    int32_t best_value = -1;
    int32_t best = -1;
    for (int32_t t = 0; t < 89; t++) {
        if (has_tech(t, faction_id) || !tech_avail(t, faction_id)) continue;
        int32_t p1 = has_tech(Tech[t].preq_tech1, faction_id);
        int32_t p2 = has_tech(Tech[t].preq_tech2, faction_id);
        if (!p1 && !p2) continue;
        int32_t n = 8;
        if (!p1 || !p2) {
            if (!valid_tech_leap(t, faction_id)) continue;
            n = (11 - f.ranking) * 2;
        }
        int32_t value = n <= 1 ? 0 : game_rand() % n;
        if (best_value < value) {
            best = t;
            best_value = value;
        }
    }
    if (best >= 0) tech_achieved(faction_id, best, 0, 0);
    return 1;
}
