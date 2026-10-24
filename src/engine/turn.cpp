// Turn processing (original module around 0x51F2E0-0x52DBA0): random events, native life, turn
// and faction upkeep, the game loops and the Planetary Council's AI.
#include "engine/turn.h"

#include <cstddef>
#include <cstdlib>
#include <initializer_list>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/game.h"
#include "engine/goals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/social.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/world.h"
#include "engine/util.h"
#include "engine/zoc.h"
#include "platform/win32.h"

using win32::CharUpperA;

namespace {
// X_pops_11's wait callback (pop_wait, 0x5398E0; an address of the original).
constexpr int32_t kPopWaitCallback = 0x5398E0;

inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
inline bool is_alive(int32_t faction_id) { return (FactionStatus[1] & (uint8_t)(1u << (faction_id & 31))) != 0; }
// Upkeep steps that only run while the game goes on (or after the final score).
inline bool full_game_turn() { return !(GameState & STATE_GAME_DONE) || (GameState & STATE_FINAL_SCORE_DONE); }
bool base_has_facility(int32_t base_id, int32_t facility_id) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}
void call_council_body(int32_t faction_id, int32_t aliens_residue);
// $STR(slot), $STR(slot+1): the faction leader's title and name.
void say_leader(int32_t faction_id, int32_t slot) {
    const MFaction& m = MFactions[faction_id];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(slot, m.title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(slot + 1, m.name_leader, -1, -1);
}
// $STR0..2: title, leader name and faction noun.
void say_faction(int32_t faction_id) {
    say_leader(faction_id, 0);
    GenderDefault = MFactions[faction_id].noun_gender;
    PluralDefault = MFactions[faction_id].is_noun_plural;
    parse_says(2, MFactions[faction_id].noun_faction, -1, -1);
}
// $NUM0 and $STR0: a number (a year) as number and as text.
void say_number_text(int32_t value) {
    parse_num(0, value);
    StrBuffer[0] = 0;
    char text[80];
    crt_itoa(value, text, 10);
    crt_strcat(StrBuffer, text);
    parse_says(0, StrBuffer, -1, -1);
}
}  // namespace

void GAMEAPI reset_territory() {
    for (int32_t f = 0; f < 8; f++) {
        Faction& fac = Factions[f];
        for (int32_t r = 0; r < 128; r++) {
            fac.region_good_tiles[r] = 0;
            fac.region_territory_tiles[r] = 0;
            fac.region_visible_tiles[r] = 0;
            fac.region_unk_5[r] = 0;
            fac.region_territory_pods[r] = 0;
            fac.region_unk_6[r] = 0;
        }
    }
    for (Continent& c : Continents) {
        c.pods = 0;
        c.world_site = 0;
    }
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2) {
            MAP* sq = map_tile(x, y);
            uint32_t region = sq->region;
            for (int32_t f = 1; f < 8; f++) {
                uint32_t flags = Factions[f].player_flags;
                bool seen = (flags & PFLAG_STRAT_SEARCH_OBJECTIVES) ? (sq->items & BIT_SUPPLY_REMOVE) != 0
                                                                     : (flags & PFLAG_MAP_REVEALED) || (sq->visibility & (1 << f));
                if (seen) Factions[f].region_visible_tiles[region]++;
            }
            // Owner: the nearest base within the territory distance (half at sea), and at sea any
            // base next to the tile.
            int32_t owner = -1;
            bool ocean = (sq->climate & 0xE0) < 0x60;
            int32_t base_id = base_find_3(x, y, -1, region, -1, -1);
            if (base_id >= 0) {
                int32_t limit = Rules.territory_max_dist_base;
                if (ocean) limit >>= 1;
                if (BaseFindDist < limit) owner = Bases[base_id].faction_id;
            }
            if (ocean) {
                for (int32_t i = 0; i < 9; i++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                    int32_t y2 = y + TableOffsetY[i];
                    if (!map_in_bounds(x2, y2)) continue;
                    int32_t near_base = base_at(x2, y2);
                    if (near_base >= 0) {
                        owner = Bases[near_base].faction_id;
                        break;
                    }
                }
            }
            if (owner != sq->owner) {
                sq->landmarks |= 0x400000;
                GameDrawState |= 1;
            }
            sq->owner = (int8_t)owner;
            if (owner < 0) owner = 0;
            Faction& fac = Factions[owner];
            fac.region_territory_tiles[region]++;
            if ((sq->climate & 0x18) && (sq->val3 & 0xC0) < 0x80 && !(sq->items & BIT_FUNGUS)) {
                fac.region_good_tiles[region]++;
                if (sq->items & BIT_BASE_RADIUS) fac.region_unk_5[region]++;
            }
            int32_t site = 0;
            if (!(sq->items & BIT_BASE_RADIUS)) {
                site = sq->val2 >> 4;
                if (!site) {
                    site = world_site(x, y, 0);
                    site_set(x, y, site);
                }
            }
            Continent& cont = Continents[region];
            if (site > cont.world_site) cont.world_site = site;
            if (owner && site > fac.region_unk_6[region]) fac.region_unk_6[region] = (uint8_t)site;
            if (!goody_at(x, y)) continue;
            cont.pods++;
            fac.region_territory_pods[region]++;
            if (owner) {
                uint8_t bit = (uint8_t)(1 << owner);
                if ((sq->visibility & bit) || (!(FactionStatus[0] & bit) && DiffLevel > 3)) add_site(owner, 3, 1, x, y);
                continue;
            }
            // Unclaimed pod: a site for factions present on the landmass at medium range of a base.
            for (int32_t f = 1; f < 8; f++) {
                if (!Factions[f].region_total_bases[region]) continue;
                uint8_t bit = (uint8_t)(1 << f);
                if (!(sq->visibility & bit) && ((FactionStatus[0] & bit) || DiffLevel <= 3)) continue;
                if (at_site(f, 3, x, y)) continue;
                if (base_find_3(x, y, f, region, -1, -1) < 0) continue;
                if (BaseFindDist >= Rules.territory_max_dist_base && BaseFindDist <= Rules.territory_max_dist_base * 2)
                    add_site(f, 3, 1, x, y);
            }
        }
    }
}

int32_t GAMEAPI council_votes(int32_t faction_id) {
    if (ExpansionEnabled && (MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return 0;
    int32_t votes = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id == faction_id) votes += Bases[i].pop_size;
    }
    if (SecretProjects[4] >= 0 && Bases[SecretProjects[4]].faction_id == faction_id) votes += votes / 2;
    if (SecretProjects[26] >= 0 && Bases[SecretProjects[26]].faction_id == faction_id) votes *= 2;
    const MFaction& m = MFactions[faction_id];
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] == RULE_VOTES && m.faction_bonus_val1[i] >= 0) votes *= m.faction_bonus_val1[i];
    }
    return votes;
}

// ---- random events --------------------------------------------------------------------------------

namespace {
// The faction-indexed memory word of a tile: 0 the landmarks, 1..7 visible_items[f - 1] (the
// original indexes visible_items by faction without a range check).
uint32_t& tile_seen_items(MAP* sq, int32_t faction_id) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(sq) + offsetof(MAP, landmarks) + faction_id * 4);
}
// Tile known to a faction (map revealed or seen).
bool tile_known(int32_t faction_id, MAP* sq) {
    return (Factions[faction_id].player_flags & PFLAG_MAP_REVEALED) || (sq->visibility & (uint8_t)(1 << (faction_id & 31)));
}
// Tile not owned by another faction.
bool tile_claimable(int32_t faction_id, int32_t x, int32_t y) {
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    return owner < 0 || owner == faction_id;
}
// Whether the event's base faction (or its base) concerns the viewer.
bool event_base_seen(int32_t faction_id, const BASE& base) {
    return faction_id == CurrentPlayerFaction || base.faction_id == CurrentPlayerFaction ||
           (base.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)));
}
// Delay unless shift is held (or in multiplayer).
void event_wait(int32_t msec) {
    if (!ui_shift_pressed() && !MultiplayerActive) clock_wait(msec);
}
// Base-condition events need a high-ranking faction (5+, 6+ after turn 150).
bool event_rank_ok(int32_t faction_id) { return Factions[faction_id].ranking >= (CurrentTurn >= 150) + 4; }
// Bonus/disaster events against the leader are rarer: ok unless the faction leads, is not a human
// at the lowest difficulties, and game_random(0, 4) is non-zero.
bool event_lead_ok(int32_t faction_id) {
    if (cs.FactionRankings[7] != faction_id) return true;
    if (is_human(faction_id) && Factions[faction_id].diff_level <= 1) return true;
    return game_random(0, 4) == 0;
}
// Base event (event_flags bit) with its popup; returns true when the popup was shown.
void base_event(int32_t base_id, uint32_t flag, const char* label, const char* pcx) {
    BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    base.event_flags |= flag;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label);
    if (!crt_strlen(StrBuffer)) return;
    if (base.event_flags & 0x7E00) base.random_event_turns = 10;
    if (faction_id == CurrentPlayerFaction || PbemActive) POP2(StrBuffer, pcx, base_id);
}
// Kelp farms / tidal platforms of a sea base destroyed (random events 20 and 21).
// BUG(original): requires an ocean tile that is also land, so it never happens.
void sea_wipe(int32_t base_id, uint32_t item, const char* label) {
    const BASE& base = Bases[base_id];
    int32_t faction_id = (uint8_t)base.faction_id;
    bool shown = false;
    for (int32_t k = 0; k < 21; k++) {
        int32_t x = map_wrap_x(base.x + TableOffsetX[k]);
        int32_t y = base.y + TableOffsetY[k];
        if (!map_in_bounds(x, y)) continue;
        MAP* sq = map_tile(x, y);
        if ((sq->climate & 0xE0) >= 0x60 || !(sq->items & item) || !tile_known(faction_id, sq)) continue;
        if (!tile_claimable(faction_id, x, y) || (sq->climate & 0xE0) < 0x60) continue;
        if (event_base_seen(faction_id, base)) {
            if (!shown) {
                Console_focus(&MainConsole, base.x, base.y, CurrentPlayerFaction);
                event_wait(200);
            }
            shown = true;
        }
        bit_set(x, y, item, 0);
        synch_bit(x, y, faction_id);
        for (int32_t f = 1; f < 8; f++) sq->visible_items[f - 1] &= ~item;
        if (shown) {
            draw_tile(x, y, 2);
            event_wait(50);
        }
    }
    if (!shown) return;
    parse_says(0, Bases[base_id].name, -1, -1);
    POP2(label, nullptr, faction_id == CurrentPlayerFaction ? base_id : -1);
}
}  // namespace

void GAMEAPI random_events(int32_t is_extra) {
    if (!is_extra) {
        if (SolarFlaresEvent & 2)
            SolarFlaresEvent = 0;
        else if (SolarFlaresEvent & 1)
            SolarFlaresEvent = 2;
        int32_t sunspots = SunspotDuration < -1000 ? -1000 : SunspotDuration;
        if (DustCloudDuration < 0) DustCloudDuration = 0;
        SunspotDuration = sunspots - 1;
        if (!SunspotDuration) POP2("NOMORESPOTS", "space_sm.pcx", -1);
        // AIs forget betrayal plans against humans they are not fighting.
        for (int32_t human = 1; human < 8; human++) {
            if (!is_human(human)) continue;
            for (int32_t ai = 1; ai < 8; ai++) {
                if (!is_human(ai) && !(Factions[ai].diplo_status[human] & 0x30)) set_treaty(ai, human, 0xC00, 0);
            }
        }
        if (DustCloudDuration > 0 && --DustCloudDuration == 0) POP2("NOMOREDUST", "stars_sm.pcx", -1);
        for (int32_t i = 0; i < BaseCount; i++) {
            BASE& base = Bases[i];
            if (!(base.event_flags & 0x7E00)) continue;
            if (base.random_event_turns >= 0) base.random_event_turns--;
            if (base.random_event_turns == 0) base.event_flags &= 0xFFFF81FF;
        }
        if ((GameState & STATE_PERIHELION_ACTIVE) && (game_year(CurrentTurn) - 2210) % 80 == 0) {
            GameState &= ~STATE_PERIHELION_ACTIVE;
            POP2("PERIHELIONENDS", "stars_sm.pcx", -1);
            return;
        }
    }
    if (CurrentTurn > 50 && !is_extra && (game_year(CurrentTurn) - 2190) % 80 == 0) {
        GameState |= STATE_PERIHELION_ACTIVE;
        POP2("PERIHELION", "heat_sm.pcx", -1);
        return;
    }
    if (GameRules & RULES_BELL_CURVE) return;
    if (CurrentTurn < 75 - DiffLevel * 10) return;
    if (!is_extra) game_reseed(MapRandomSeed + CurrentTurn * 13);
    int32_t base_id = game_random(0, BaseCount > 100 ? BaseCount : 100);
    if (base_id >= BaseCount) return;
    BASE& base = Bases[base_id];
    if (base.pop_size <= 3) return;
    int32_t faction_id = (uint8_t)base.faction_id;
    Faction& f = Factions[faction_id];
    if (f.base_count <= 1) return;
    if (base.event_flags & 0x7E00) return;
    int32_t event = game_random(0, 22);
    int32_t bx = base.x;
    int32_t by = base.y;
    parse_num(0, 10);
    parse_says(0, base.name, -1, -1);
    StrBuffer[0] = 0;
    switch (event) {
    case 0:
        if (event_lead_ok(faction_id)) base_event(base_id, 0x200, "BUMPER", "bump_sm.pcx");
        return;
    case 1:
        if (event_rank_ok(faction_id))
            base_event(base_id, 0x400, "FAMINE",
                       (MFactions[faction_id].rule_flags & RFLAG_ALIEN) ? "al_starv_sm.pcx" : "starv_sm.pcx");
        return;
    case 2:
        if (event_lead_ok(faction_id)) base_event(base_id, 0x800, "INDUSTRY", "indbm_sm.pcx");
        return;
    case 3:
        if (event_rank_ok(faction_id)) base_event(base_id, 0x1000, "BUST", "genwarning_sm.pcx");
        return;
    case 4:
        if (event_lead_ok(faction_id)) base_event(base_id, 0x2000, "HEATWAVE", "heat_sm.pcx");
        return;
    case 5:
        if (event_rank_ok(faction_id)) base_event(base_id, 0x4000, "CLOUDCOVER", "cloud_sm.pcx");
        return;
    case 6: {
        // Plague: hospitals protect the base; the genetic secret projects protect the faction.
        if (base_has_facility(base_id, FAC_RESEARCH_HOSPITAL) || base_has_facility(base_id, FAC_NANOHOSPITAL)) {
            bool nano = base_has_facility(base_id, FAC_NANOHOSPITAL);
            parse_say(1, Facility[nano ? FAC_NANOHOSPITAL : FAC_RESEARCH_HOSPITAL].name, -1, -1);
            if (faction_id == CurrentPlayerFaction) {
                Console_focus(&MainConsole, base.x, base.y, CurrentPlayerFaction);
                POP2("PROMETHEUS1", "biohazd_sm.pcx", base_id);
            } else if (PbemActive) {
                POP2("PROMETHEUS1", "biohazd_sm.pcx", base_id);
            }
            return;
        }
        if (has_sp(0, faction_id) || has_sp(14, faction_id) || has_sp(26, faction_id)) {
            int32_t project = has_sp(0, faction_id) ? 70 : !has_sp(14, faction_id) ? 96 : 84;
            parse_say(1, Facility[project].name, -1, -1);
            if (faction_id == CurrentPlayerFaction) {
                Console_focus(&MainConsole, base.x, base.y, CurrentPlayerFaction);
                POP2("PROMETHEUS2", "biohazd_sm.pcx", base_id);
            } else if (PbemActive) {
                POP2("PROMETHEUS2", "biohazd_sm.pcx", base_id);
            }
            return;
        }
        if (!event_rank_ok(faction_id)) return;
        int32_t radius = (base.pop_size + 1) / 2;
        parse_num(1, radius);
        // shown: 1 + the last affected base the viewer sees (BUG(original): 1, i.e. base 0, when
        // only the event base's visibility counts).
        int32_t shown = 0;
        if (base.faction_id == CurrentPlayerFaction || (base.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)))) {
            shown = 1;
            Console_focus(&MainConsole, base.x, base.y, CurrentPlayerFaction);
        }
        for (int32_t i = 0; i < BaseCount; i++) {
            BASE& b = Bases[i];
            int32_t dx = std::abs(b.x - base.x);
            if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
            int32_t dy = std::abs(b.y - base.y);
            if ((dx + dy) >> 1 > radius) continue;
            if (base_has_facility(i, FAC_RESEARCH_HOSPITAL) || base_has_facility(i, FAC_NANOHOSPITAL)) continue;
            uint8_t owner = (uint8_t)b.faction_id;
            if (SecretProjects[0] >= 0 && (uint8_t)Bases[SecretProjects[0]].faction_id == owner) continue;
            if (SecretProjects[14] >= 0 && (uint8_t)Bases[SecretProjects[14]].faction_id == owner) continue;
            if (SecretProjects[26] >= 0 && (uint8_t)Bases[SecretProjects[26]].faction_id == owner) continue;
            b.pop_size = (int8_t)((b.pop_size + 1) / 2);
            draw_tile(b.x, b.y, 2);
            if (owner == CurrentPlayerFaction) {
                Console_focus(&MainConsole, b.x, b.y, CurrentPlayerFaction);
                shown = i + 1;
            } else if (PbemActive) {
                shown = i + 1;
            }
        }
        if (shown) POP2("PROMETHEUS0", "biohazd_sm.pcx", shown - 1);
        return;
    }
    case 7:
    case 8: {
        // A resource appears (or, near the base, one runs out after turn 75).
        if (PbemActive) return;
        int32_t offset = game_random(0, 20) + 1;
        bool nearby = false;
        for (int32_t k = 0; k < 9; k++) {
            int32_t x = map_wrap_x(bx + TableOffsetX[k]);
            int32_t y = by + TableOffsetY[k];
            if (!map_in_bounds(x, y) || !bonus_at(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if (!tile_known(faction_id, sq)) continue;
            if ((sq->climate & 0xE0) < 0x60 || tile_claimable(faction_id, x, y)) {
                offset = k;
                nearby = true;
                break;
            }
        }
        int32_t x = map_wrap_x(bx + TableOffsetX[offset]);
        int32_t y = by + TableOffsetY[offset];
        if (!map_in_bounds(x, y)) return;
        MAP* sq = map_tile(x, y);
        if (!tile_known(faction_id, sq)) return;
        if ((sq->climate & 0xE0) >= 0x60 && !tile_claimable(faction_id, x, y)) return;
        if (nearby && bonus_at(x, y)) {
            if (CurrentTurn < 75) return;
            parse_say(1, ResName[bonus_at(x, y) - 1].name_plural, -1, -1);
            bit_set(x, y, BIT_BONUS_RES, 0);
            synch_bit(x, y, faction_id);
            draw_tile(x, y, 2);
            if (bonus_at(x, y) || faction_id != CurrentPlayerFaction) return;
            pfncMoveMap(MapWins[0], x, y, 1);
            if (!ui_shift_pressed()) {
                for (int32_t i = 10; i; i--) {
                    bit_set(x, y, BIT_BONUS_RES, 1);
                    synch_bit(x, y, faction_id);
                    draw_tile(x, y, 2);
                    event_wait(20);
                    bit_set(x, y, BIT_BONUS_RES, 0);
                    synch_bit(x, y, faction_id);
                    draw_tile(x, y, 2);
                    event_wait(20);
                }
            }
            event_wait(100);
            POP2("PETERSOUT", "genwarning_sm.pcx", -1);
            return;
        }
        bit_set(x, y, BIT_BONUS_RES, 1);
        synch_bit(x, y, faction_id);
        draw_tile(x, y, 2);
        int32_t resource = bonus_at(x, y) - 1;
        if (resource < 0 || faction_id != CurrentPlayerFaction) return;
        pfncMoveMap(MapWins[0], x, y, 1);
        if (!ui_shift_pressed()) {
            for (int32_t i = 10; i; i--) {
                bit_set(x, y, BIT_BONUS_RES, 0);
                synch_bit(x, y, faction_id);
                draw_tile(x, y, 2);
                event_wait(20);
                bit_set(x, y, BIT_BONUS_RES, 1);
                synch_bit(x, y, faction_id);
                draw_tile(x, y, 2);
                event_wait(20);
            }
        }
        parse_say(1, ResName[resource].name_plural, -1, -1);
        bool ocean = (map_tile(x, y)->climate & 0xE0) < 0x60;
        if (resource == 0)
            POP2("NEWRESOURCE", ocean ? "rdneuw_sm.pcx" : "rdneul_sm.pcx", -1);
        else
            POP2("NEWRESOURCE", ocean ? "rdminw_sm.pcx" : "rdminl_sm.pcx", -1);
        return;
    }
    case 9: {
        if (SolarFlaresEvent || game_random(0, 5)) return;
        bool hit = false;
        for (int32_t i = 1; i < 8; i++) {
            if (Factions[i].satellites_energy || Factions[i].satellites_ODP) {
                Factions[i].satellites_energy = 0;
                Factions[i].satellites_ODP = 0;
                hit = true;
            }
        }
        SolarFlaresEvent = 1;
        if (hit)
            POP2("SOLARSTORM", "sun_sm.pcx", -1);
        else
            POP2("SOLARFLARE", "heat_sm.pcx", -1);
        return;
    }
    case 10: {
        if (game_random(0, 5)) return;
        bool hit = false;
        for (int32_t i = 1; i < 8; i++) {
            if (Factions[i].satellites_mineral) {
                Factions[i].satellites_mineral = 0;
                hit = true;
            }
        }
        if (hit) POP2("ASTEROID", "astm_sm.pcx", -1);
        return;
    }
    case 11:
        if (CurrentTurn < 50 || SunspotDuration > -40) return;
        if (SunspotDuration > -80 && CurrentTurn >= 80 && game_random(0, 3)) return;
        SunspotDuration = game_random(0, 11) + 10;
        parse_num(0, 20);
        POP2("SUNSPOTS", "solar_sm.pcx", -1);
        return;
    case 12: {
        int32_t credits = f.energy_credits;
        if (credits <= 1000) {
            if (f.ranking > 4 || credits > 500) return;
            f.energy_credits = credits * 2;
            if (faction_id != CurrentPlayerFaction) return;
            parse_num(0, credits * 2);
            Console_update_data(&MainConsole, 0);
            POP2("ENERGYBOOM", "markbm_sm.pcx", -1);
            return;
        }
        if (!is_human(faction_id) && credits <= 2000) return;
        f.energy_credits = credits / 4;
        if (faction_id == CurrentPlayerFaction) {
            parse_num(0, credits / 4);
            Console_update_data(&MainConsole, 0);
            POP2("INFLATION", "markbm_sm.pcx", -1);
        } else if (PbemActive) {
            parse_num(0, credits / 4);
            POP2("INFLATION", "markbm_sm.pcx", -1 - faction_id);
        }
        return;
    }
    case 13: {
        // Hail: solar collectors in the base radius destroyed.
        if (CurrentTurn < 75 || !event_rank_ok(faction_id)) return;
        int32_t shown = 0;
        for (int32_t k = 0; k < 21; k++) {
            int32_t x = map_wrap_x(base.x + TableOffsetX[k]);
            int32_t y = base.y + TableOffsetY[k];
            if (!map_in_bounds(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if (!(sq->items & BIT_SOLAR) || !tile_known(faction_id, sq)) continue;
            if (!tile_claimable(faction_id, x, y) || (sq->climate & 0xE0) < 0x60) continue;
            if (PbemActive) {
                shown = 1;
            } else if (event_base_seen(faction_id, base)) {
                if (!shown) {
                    Console_focus(&MainConsole, bx, by, CurrentPlayerFaction);
                    event_wait(200);
                }
                shown |= (faction_id == CurrentPlayerFaction) + 1;
            }
            bit_set(x, y, BIT_SOLAR, 0);
            synch_bit(x, y, faction_id);
            for (int32_t i = 1; i < 8; i++) sq->visible_items[i - 1] &= ~BIT_SOLAR;
            if (shown) {
                draw_tile(x, y, 2);
                if (!ui_shift_pressed() && !MultiplayerActive && !PbemActive) clock_wait(50);
            }
        }
        if (shown) POP2("HAIL", "hail_sm.pcx", base_id);
        return;
    }
    case 14: {
        // Mount Planet erupts near the base.
        if (PbemActive || CurrentTurn < 75 || f.base_count < 8 || (GameState & STATE_IS_SCENARIO)) return;
        if (MountPlanetY < 0 || MountPlanetY >= MapAreaY || MountPlanetX < 0 || MountPlanetX >= MapAreaX) return;
        MAP* mount = map_tile(MountPlanetX, MountPlanetY);
        if ((mount->landmarks & 0x80000002) != 2) return;
        int32_t dx = std::abs(base.x - MountPlanetX);
        if (!(MapToggleFlat & 1) && dx > MapHalfX) dx = MapAreaX - dx;
        int32_t dy = std::abs(base.y - MountPlanetY);
        if ((int32_t)((uint32_t)(dx + dy) & ~1u) > 10) return;
        if (!tile_known(faction_id, mount) || f.ranking < 6) return;
        bool shown = tile_known(CurrentPlayerFaction, mount);
        int32_t shown_base = base_id;
        for (int32_t k = 0; k < 81; k++) {
            int32_t x = map_wrap_x(MountPlanetX + TableOffsetX[k]);
            int32_t y = MountPlanetY + TableOffsetY[k];
            if (!map_in_bounds(x, y)) continue;
            bit_set(x, y, 0x81E8887C, 0);
            synch_bit(x, y, CurrentPlayerFaction);
            rocky_set(x, y, 2);
            if ((map_tile(x, y)->landmarks & 0x80000002) == 2) {
                for (int32_t v = VehCount - 1; v >= 0; v--)
                    if (Vehs[v].x == x && Vehs[v].y == y) veh_kill(v);
            } else {
                for (int32_t v = VehCount - 1; v >= 0; v--) {
                    VEH& veh = Vehs[v];
                    if (veh.x != x || veh.y != y) continue;
                    const UNIT& u = Units[veh.unit_id];
                    int32_t hp = u.plan == 12 ? 1 : (u.reactor_id < 1 ? 1 : u.reactor_id > 100 ? 100 : u.reactor_id) * 10;
                    int32_t left = hp - veh.damage_taken;
                    left = left < 0 ? 0 : (left > 9999 ? 9999 : left);
                    veh.damage_taken = (uint8_t)(veh.damage_taken + left / 2);
                }
            }
            int32_t hit = base_at(x, y);
            if (hit < 0) continue;
            if (Bases[hit].faction_id == CurrentPlayerFaction && Bases[shown_base].faction_id != CurrentPlayerFaction) {
                parse_says(0, Bases[hit].name, -1, -1);
                shown_base = hit;
                shown = true;
            }
            if (k < 9 && !is_objective(hit)) {
                base_kill(hit);
            } else if (k < 25) {
                Bases[hit].pop_size = (int8_t)((Bases[hit].pop_size + 3) / 4);
            } else if (k < 49) {
                Bases[hit].pop_size = (int8_t)((Bases[hit].pop_size + 1) / 2);
            } else {
                Bases[hit].pop_size = (int8_t)((Bases[hit].pop_size + 1) * 3 / 4);
            }
        }
        if (!shown || !Console_focus(&MainConsole, MountPlanetX, MountPlanetY, CurrentPlayerFaction)) draw_map(1);
        DustCloudDuration = 10;
        popp(ScriptFileName, "BIGFATVOLCANO", 0, "volc_sm.pcx", 0);
        return;
    }
    case 15: {
        // Asteroid impact on an inland base.
        if (PbemActive || CurrentTurn < 75 || f.base_count < 8 || (GameState & STATE_IS_SCENARIO)) return;
        if (near_landmark(bx, by)) return;
        bool shown = tile_known(CurrentPlayerFaction, map_tile(bx, by));
        int32_t ocean = 0;
        for (int32_t k = 0; k < 49; k++) {
            int32_t x = map_wrap_x(bx + TableOffsetX[k]);
            int32_t y = by + TableOffsetY[k];
            if (!map_in_bounds(x, y) || (map_tile(x, y)->climate & 0xE0) >= 0x60) continue;
            if (k < 25) return;
            if (++ocean > 5) return;
        }
        if (f.ranking < 7) return;
        int32_t shown_base = base_id;
        for (int32_t k = 0; k < 49; k++) {
            int32_t x = map_wrap_x(bx + TableOffsetX[k]);
            int32_t y = by + TableOffsetY[k];
            if (!map_in_bounds(x, y)) continue;
            bit_set(x, y, 0x81E8887C, 0);
            synch_bit(x, y, CurrentPlayerFaction);
            int32_t hit = base_at(x, y);
            if (hit >= 0) {
                if (Bases[hit].faction_id == CurrentPlayerFaction &&
                    Bases[shown_base].faction_id != CurrentPlayerFaction) {
                    shown_base = hit;
                    parse_says(0, Bases[hit].name, -1, -1);
                    shown = true;
                }
                base_kill(hit);
            }
            for (int32_t v = VehCount - 1; v >= 0; v--) {
                if (Vehs[v].x != x || Vehs[v].y != y) continue;
                if (Vehs[v].faction_id == (uint32_t)CurrentPlayerFaction) shown = true;
                veh_kill(v);
            }
        }
        world_crater(bx, by);
        world_climate();
        if (!shown || !Console_focus(&MainConsole, bx, by, CurrentPlayerFaction)) draw_map(1);
        DustCloudDuration = 10;
        popp(ScriptFileName, "EATTHIS", 0, "astp_sm.pcx", 0);
        return;
    }
    case 16: {
        // Blight: forests and farms in the base radius lost (a Biology Lab protects: golden age).
        if (!has_tech(Facility[FAC_BIOLOGY_LAB].preq_tech, faction_id)) return;
        bool shown = false;
        bool lab = base_has_facility(base_id, FAC_BIOLOGY_LAB);
        if (!lab && !event_rank_ok(faction_id)) return;
        for (int32_t k = 0; k < 21; k++) {
            int32_t x = map_wrap_x(bx + TableOffsetX[k]);
            int32_t y = by + TableOffsetY[k];
            if (!map_in_bounds(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if ((sq->climate & 0xE0) < 0x60 || !(sq->items & 0x208000)) continue;
            if (!tile_known(faction_id, sq) || !tile_claimable(faction_id, x, y)) continue;
            if (!lab) {
                if (!shown) {
                    Console_focus(&MainConsole, bx, by, CurrentPlayerFaction);
                    event_wait(200);
                }
                bit_set(x, y, 0x288000, 0);
                synch_bit(x, y, faction_id);
                // BUG(original): clears the viewer's memory seven times instead of every
                // faction's (with viewer 0 the landmark bits).
                for (int32_t i = 7; i; i--) {
                    uint32_t& seen = tile_seen_items(sq, CurrentPlayerFaction);
                    if (seen & 0x288000) seen &= 0xFFD77FFF;
                }
                draw_tile(x, y, 2);
                event_wait(50);
            }
            shown = true;
        }
        if (!shown) return;
        if (lab) {
            base.event_flags |= 0x200;
            base.random_event_turns = 10;
        }
        if (faction_id == CurrentPlayerFaction) {
            if (!Console_focus(&MainConsole, base.x, base.y, faction_id)) draw_radius(base.x, base.y, 2, 2);
        } else if (!PbemActive) {
            return;
        }
        POP2(lab ? "BLIGHT1" : "BLIGHT", "biohazd_sm.pcx", base_id);
        return;
    }
    case 17: {
        // Mining accident: mines in the base radius destroyed (an Energy Bank or project 36
        // turns it into a +50 energy surge).
        if (!has_tech(Facility[FAC_ENERGY_BANK].preq_tech, faction_id)) return;
        bool shown = false;
        bool protect = base_has_facility(base_id, FAC_ENERGY_BANK) || has_sp(36, faction_id);
        if (!protect && !event_rank_ok(faction_id)) return;
        for (int32_t k = 0; k < 21; k++) {
            int32_t x = map_wrap_x(bx + TableOffsetX[k]);
            int32_t y = by + TableOffsetY[k];
            if (!map_in_bounds(x, y)) continue;
            MAP* sq = map_tile(x, y);
            if ((sq->climate & 0xE0) < 0x60 || !(sq->items & BIT_MINE)) continue;
            if (!tile_known(faction_id, sq) || !tile_claimable(faction_id, x, y)) continue;
            if (!protect) {
                if (!shown) {
                    Console_focus(&MainConsole, bx, by, CurrentPlayerFaction);
                    event_wait(200);
                }
                bit_set(x, y, BIT_MINE, 0);
                synch_bit(x, y, faction_id);
                for (int32_t i = 1; i < 8; i++)
                    if (sq->visible_items[i - 1] & BIT_MINE) sq->visible_items[i - 1] &= ~BIT_MINE;
                draw_tile(x, y, 2);
                event_wait(50);
            }
            shown = true;
        }
        if (!shown) return;
        if (protect) f.energy_credits += 50;
        if (faction_id == CurrentPlayerFaction) {
            if (!Console_focus(&MainConsole, base.x, base.y, faction_id)) draw_radius(base.x, base.y, 2, 2);
        } else if (!PbemActive) {
            return;
        }
        POP2(protect ? "SURGE1" : "SURGE", "markbm_sm.pcx", base_id);
        return;
    }
    case 18: {
        // Datalinks: research lost (bonus with a Network Node).
        if (!has_tech(Facility[FAC_NETWORK_NODE].preq_tech, faction_id)) return;
        if (f.tech_accumulated < tech_rate(faction_id) / 4) return;
        if (!base_has_facility(base_id, FAC_NETWORK_NODE)) {
            if (!event_rank_ok(faction_id)) return;
            f.tech_accumulated = 0;
            f.net_random_event = 0;
            if (faction_id == CurrentPlayerFaction) {
                Console_focus(&MainConsole, base.x, base.y, faction_id);
                POP2("NETCRASH", "netcr_sm.pcx", base_id);
            } else if (PbemActive) {
                POP2("NETCRASH", "netcr_sm.pcx", base_id);
            }
            return;
        }
        f.tech_accumulated = tech_rate(faction_id);
        if (faction_id == CurrentPlayerFaction) {
            Console_focus(&MainConsole, base.x, base.y, faction_id);
            POP2("NETBONUS", "markbm_sm.pcx", base_id);
        } else if (PbemActive) {
            POP2("NETBONUS", "markbm_sm.pcx", base_id);
        }
        return;
    }
    case 19: {
        // Population boom (with a Children's Creche: up to two citizens at once).
        if (!has_tech(Facility[FAC_CHILDREN_CRECHE].preq_tech, faction_id)) return;
        const char* pcx = (MFactions[faction_id].rule_flags & RFLAG_ALIEN) ? "al_pop_sm.pcx" : "pop_sm.pcx";
        if (!base_has_facility(base_id, FAC_CHILDREN_CRECHE)) {
            if (!event_rank_ok(faction_id) || base.assimilation_turns_left) return;
            base.assimilation_turns_left = 5;
            if (faction_id == CurrentPlayerFaction) {
                Console_focus(&MainConsole, base.x, base.y, faction_id);
            } else if (!PbemActive) {
                return;
            }
            POP2("CRECHE", pcx, base_id);
            return;
        }
        if (base.nutrient_surplus < 1) return;
        for (int32_t n = 1; n <= 2; n++) {
            base.pop_size++;
            set_base(base_id);
            base_compute(1);
            if (base.nutrient_surplus <= 0) break;
        }
        if (faction_id == CurrentPlayerFaction) {
            if (!Console_focus(&MainConsole, base.x, base.y, faction_id)) draw_tile(base.x, base.y, 2);
        } else if (!PbemActive) {
            return;
        }
        POP2("CRECHE1", pcx, base_id);
        return;
    }
    case 20:
        if (PbemActive || CurrentTurn < 75 || !event_rank_ok(faction_id)) return;
        sea_wipe(base_id, 0x8000, "KELPWIPE");
        return;
    case 21:
        if (PbemActive || CurrentTurn < 75 || !event_rank_ok(faction_id)) return;
        sea_wipe(base_id, BIT_MINE, "PLATFORMWIPE");
        return;
    default:
        return;
    }
}


// ---- turn upkeep ----------------------------------------------------------------------------------

namespace {
// Vehicle visible to the viewer (own and not hidden, or seen).
bool veh_visible_to_viewer(int32_t veh_id) {
    const VEH& veh = Vehs[veh_id];
    return (veh.faction_id == (uint32_t)CurrentPlayerFaction && (veh.flags & 0x440) != 0x440) ||
           (veh.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)));
}
// Mind worms, or (one in five with the expansion) a spore launcher.
int32_t native_spawn_unit() { return (game_rand() % 5 == 0 && ExpansionEnabled) ? 15 : 8; }
}  // namespace

void GAMEAPI alien_fauna() {
    // Forests and kelp spread to a neighbouring tile.
    // BUG(original): the visibility of the spreading tile, not the new one, decides who learns of
    // it; and the worm spawn below redraws a stale tile (draw_x/draw_y, display only).
    int32_t draw_x = 0, draw_y = 0;
    for (int32_t i = 0; i < MapAreaTiles / (CurrentTurn / 4 + 32); i++) {
        int32_t x = map_rand_mod(MapAreaX);
        int32_t y = map_rand_mod(MapAreaY);
        x = (y & 1) - (x & 1) + x;
        if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
        MAP* sq = map_tile(x, y);
        bool ocean = (sq->climate & 0xE0) < 0x60;
        const uint32_t growth = ocean ? 0x8000 : 0x200000;  // kelp (sea farm) / forest
        if (!(sq->items & growth)) continue;
        int32_t best = 0, best_x = -1, best_y = 0;
        for (int32_t k = 0; k < 8; k++) {
            int32_t x2 = map_wrap_x(x + NearOffsetX[k]);
            int32_t y2 = y + NearOffsetY[k];
            draw_x = x2;
            draw_y = y2;
            if (!map_in_bounds(x2, y2)) continue;
            MAP* sq2 = map_tile(x2, y2);
            uint8_t climate = sq2->climate;
            if (((climate & 0xE0) < 0x60) != ocean) continue;
            int32_t alt = climate >> 5;
            if (alt < 2 || (alt > 4 && !(climate & 0x18))) continue;
            if ((sq2->landmarks & 0x80000002) == 2 || (sq2->landmarks & 0x80000040) == 0x40) continue;
            int32_t score = (3 - ((climate >> 3) & 3)) * 4 - (sq2->val3 >> 6);
            if ((sq2->val3 & 0xC0) > 0x40 || (sq2->items & 0x208051)) continue;
            if (score > best) {
                best = score;
                best_x = x2;
                best_y = y2;
            }
        }
        if (best_x < 0) continue;
        MAP* sq2 = map_tile(best_x, best_y);
        if (sq2->items & growth) continue;
        sq2->items = (sq2->items | growth) & ~BIT_FUNGUS;
        for (int32_t f = 1; f < 8; f++) {
            if (!(Factions[f].player_flags & PFLAG_MAP_REVEALED) && !(sq->visibility & (uint8_t)(1 << f))) continue;
            if (!(sq->visible_items[f - 1] & growth)) continue;
            sq2->visible_items[f - 1] = (sq2->visible_items[f - 1] | growth) & ~BIT_FUNGUS;
            if (f != CurrentPlayerFaction) continue;
            int32_t base_id = base_find_3(x, y, -1, -1, -1, CurrentPlayerFaction);
            if (base_id < 0 || Bases[base_id].faction_id != CurrentPlayerFaction ||
                (BaseFindDist > 3 && whose_territory(CurrentPlayerFaction, x, y, nullptr, 0) != CurrentPlayerFaction)) {
                draw_tiles(x, y, 2);
                continue;
            }
            if (!MapWin_focus(MapWins[0], x, y)) draw_tiles(x, y, 2);
            parse_says(0, Bases[base_id].name, -1, -1);
            if (ocean)
                POP2("KELPGROWS", "kelp_sm.pcx", -1);
            else
                POP2("FORESTGROWS", "forgr_sm.pcx", -1);
        }
    }
    if (GameRules & RULES_SCN_NO_NATIVE_LIFE) return;
    bool early = CurrentTurn < ((3 - MapNativeLifeForms) * 10 - DiffLevel) * 2;
    if (SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1) return;
    if (VehCount >= 0x700) return;
    if (CurrentTurn % ((5 - MapNativeLifeForms) * 2) == 0) {
        // An Isle of the Deep full of worms (or a Sealurk) on fungus or in a large ocean.
        int32_t x, y;
        int32_t attempts = 0;
        bool found = true;
        for (;;) {
            x = map_rand_mod(MapAreaX);
            y = map_rand_mod(MapAreaY);
            x = (y & 1) - (x & 1) + x;
            MAP* sq = map_tile(x, y);
            int32_t region = sq->region;
            if (++attempts >= 1000) {
                found = false;
                break;
            }
            if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) break;
            if ((sq->climate & 0xE0) >= 0x60) continue;
            if ((sq->items & 3) && (sq->val2 & 0xF) < 8) continue;
            if ((region & 0x3F) == 0x3F || (region & 0x3F) == 0) continue;
            if (Continents[region].tile_count < 50) continue;
            break;
        }
        if (found) {
            int32_t count = ((GameState & STATE_PERIHELION_ACTIVE) ? 2 : 1) * CurrentTurn /
                                (((8 - MapNativeLifeForms) * 4 - DiffLevel) * 4) +
                            MapNativeLifeForms + 1;
            if (early) count = 1;
            bool spawned = true;
            if (ExpansionEnabled && (engine_logic_time() & 1)) {
                int32_t veh_id = veh_init(14, 0, x, y);  // Sealurk
                if (veh_id < 0)
                    spawned = false;
                else
                    veh_promote(veh_id);
            } else {
                int32_t veh_id = veh_init(9, 0, x, y);  // Isle of the Deep
                if (veh_id < 0) {
                    spawned = false;
                } else {
                    if (count >= veh_cargo(veh_id)) count = veh_cargo(veh_id);
                    for (; count > 0; count--) sleep(veh_init(native_spawn_unit(), 0, x, y));
                    veh_promote(veh_id);
                }
            }
            // Locusts of Chiron once someone knows Centauri Meditation or ... (techs 63 / 25).
            if (spawned && (TechOwners[63] || TechOwners[25])) veh_init(10, 0, x, y);
        }
    }
    // Mind worms on fungus: next to a unit away from bases, or in the wild near bases.
    for (int32_t i = 0; i < MapNativeLifeForms * 2 + 2; i++) {
        int32_t x = map_rand_mod(MapAreaX);
        int32_t y = map_rand_mod(MapAreaY);
        x = (y & 1) - (x & 1) + x;
        MAP* sq = map_tile(x, y);
        if ((sq->climate & 0xE0) < 0x60 || !(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40) continue;
        int32_t region = sq->region;
        if ((region & 0x3F) == 0x3F || (region & 0x3F) == 0) continue;
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) continue;
        int32_t base_id = base_find_3(x, y, -1, region, -1, -1);
        if ((sq->items & 2) && (sq->val2 & 0xF) < 8) {
            int32_t owner = sq->val2 & 0xF;
            if (owner > 0 && (base_id < 0 || BaseFindDist > 2)) {
                for (int32_t k = 9; k < 25; k++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[k]);
                    int32_t y2 = y + TableOffsetY[k];
                    draw_x = x2;
                    draw_y = y2;
                    if (!map_in_bounds(x2, y2)) continue;
                    MAP* sq2 = map_tile(x2, y2);
                    if ((sq2->climate & 0xE0) < 0x60 || (Factions[owner].player_flags & PFLAG_MAP_REVEALED) ||
                        (sq2->visibility & (uint8_t)(1 << owner)))
                        continue;
                    if ((sq2->items & 3) && (sq2->val2 & 0xF) < 8) continue;
                    if (zoc_any(x2, y2, 0)) continue;
                    int32_t veh_id = veh_init(native_spawn_unit(), 0, x2, y2);
                    if (veh_id >= 0 && veh_visible_to_viewer(veh_id)) draw_tile(x2, y2, 2);
                    break;
                }
            }
            continue;
        }
        if (BaseFindDist > 1 && BaseFindDist < 6 && (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 &&
            !goody_at(x, y)) {
            int32_t veh_id = veh_init(native_spawn_unit(), 0, x, y);
            if (veh_id >= 0 && veh_visible_to_viewer(veh_id)) draw_tile(draw_x, draw_y, 2);
        }
    }
    // BUG(original): the code after this loop (worms attacking the area of a base, with the
    // XMINDWORMS message) is unreachable: the loop never exits early.
}

void GAMEAPI do_fungal_towers() {
    for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
        VEH& veh = Vehs[veh_id];
        if (veh.unit_id != 19) continue;  // Fungal Tower
        veh.state |= 4;
        int32_t x = veh.x;
        int32_t y = veh.y;
        // Fungus grows on a random neighbour and on the tower's tile.
        if (game_rand() % 5 == 0) {
            int32_t k = game_rand() % 8;
            int32_t x2 = map_wrap_x(x + TableOffsetX[k + 1]);
            int32_t y2 = y + TableOffsetY[k + 1];
            if (map_in_bounds(x2, y2)) {
                bit_set(x2, y2, BIT_FUNGUS, 1);
                bit_set(x2, y2, 0x28805C, 0);
                for (int32_t f = 1; f < 8; f++) synch_bit(x2, y2, f);
                draw_tile(x2, y2, 2);
            }
        }
        bit_set(x, y, BIT_FUNGUS, 1);
        bit_set(x, y, 0x28805C, 0);
        for (int32_t f = 1; f < 8; f++) synch_bit(x, y, f);
        draw_tile(x, y, 2);
        veh.morale = (uint8_t)morale_alien(veh_id, 0);
        spot_all(veh_id, 1);
    }
}

void GAMEAPI turn_upkeep() {
    // Aircraft out of their base: range turns count up; at the limit they crash.
    for (int32_t veh_id = VehCount - 1; veh_id >= 0; veh_id--) {
        VEH& veh = Vehs[veh_id];
        const CChassis& chassis = Chassis[Units[veh.unit_id].chassis_id];
        if (chassis.triad != TRIAD_AIR || !chassis.range) continue;
        int32_t moves = speed(veh_id, 0) - veh.moves_spent;
        if (moves <= 0) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) continue;
        if (sq->items & BIT_AIRBASE) continue;
        if (stack_check(veh_id, 6, ABL_CARRIER, -1, -1)) continue;
        if (veh.movement_turns < chassis.range - 1) {
            // BUG(original): returns from turn_upkeep, skipping the rest of the turn's upkeep (the
            // turn counter included) while an aircraft in flight still has moves.
            veh.movement_turns++;
            return;
        }
        veh.state |= 4;
        const UNIT& u = Units[veh.unit_id];
        if (chassis.range == 1 && u.chassis_id == 6) {  // copter: loses three reactor steps
            veh.damage_taken = (uint8_t)(veh.damage_taken + (int8_t)u.reactor_id * 3);
        } else {
            int32_t hp = u.plan == 12 ? 1 : range(u.reactor_id, 1, 100) * 10;
            int32_t value = hp - veh.damage_taken;
            if (value < 0) value = 0;
            else if (value > 9999) value = 9999;
            veh.damage_taken = (uint8_t)(veh.damage_taken + value);
        }
        int32_t hp = Units[veh.unit_id].plan == 12 ? 1 : range(Units[veh.unit_id].reactor_id, 1, 100) * 10;
        if (hp - veh.damage_taken <= 0) kill(veh_id);
    }
    dword_90DB84 = 0;
    rankings(1);
    CurrentTurn++;
    CurrentMissionYear = game_year(CurrentTurn);
    MapWin_main_caption(MapWins[0]);
    if (ClimateFutureChange) {
        ClimateValueC += ClimateValueA;
        if (ClimateValueC >= ClimateValueB) {
            ClimateValueC -= ClimateValueB;
            int32_t step = ClimateFutureChange > 0 ? 1 : (ClimateFutureChange >= 0) - 1;
            ClimateFutureChange -= step;
            MapSeaLevel += step;
            world_climate();
            draw_map(1);
            if (step > 0)
                popp(ScriptFileName, "SEARISING", 0, "searis_sm.pcx", 0);
            else
                popp(ScriptFileName, "SEAFALLING", 0, "seafal_sm.pcx", 0);
        }
    }
    alien_fauna();
    do_fungal_towers();
    // Late arrivals (Progenitors, the fungal faction) between turns 4 and 7.
    if ((CurrentTurn == 4 && game_rand() % 4 == 0) || (CurrentTurn == 5 && game_rand() % 3 == 0) ||
        (CurrentTurn == 6 && game_rand() % 2 == 0) || CurrentTurn >= 7) {
        bool aliens = false;
        for (int32_t i = 1; i < 8; i++) {
            const Faction& f = Factions[i];
            StrBuffer[0] = 0;
            crt_strcpy(StrBuffer, MFactions[i].filename);
            CharUpperA(StrBuffer);
            bool alien = (MFactions[i].rule_flags & RFLAG_ALIEN) != 0;
            if (!alien && crt_strcmp(StrBuffer, "FUNGBOY") != 0) continue;
            if (f.base_count || (f.player_flags | PFLAG_MAP_REVEALED) != 0xFFFFFFFF || f.diff_level != -1) continue;
            uint8_t bit = (uint8_t)(1 << i);
            if ((FactionStatus[0] & bit) || (FactionStatus[1] & bit)) continue;
            if (alien) aliens = true;
            FactionStatus[1] |= bit;
            setup_player(i, -282, 0);
        }
        if (aliens) {
            popp(ScriptFileName, "ALIENSARRIVE", 0, "al_land_sm.pcx", 0);
            interlude(21, 0, 1, 0);
        }
    }
    // The Caretakers declare vendetta on anyone building the Ascent to Transcendence.
    int32_t caretakers = 0;
    for (int32_t i = 1; i < 8; i++) {
        StrBuffer[0] = 0;
        crt_strcpy(StrBuffer, MFactions[i].filename);
        CharUpperA(StrBuffer);
        if (!crt_strcmp(StrBuffer, "CARETAKE")) caretakers = i;
    }
    if (caretakers) {
        for (int32_t i = 0; i < BaseCount; i++) {
            int32_t owner = (uint8_t)Bases[i].faction_id;
            if (Bases[i].queue_items[0] != -FAC_ASCENT_TO_TRANSCENDENCE ||
                (Factions[caretakers].diplo_status[owner] & DIPLO_VENDETTA))
                continue;
            if (Factions[owner].diplo_status[caretakers] & DIPLO_PACT) pact_ends(owner, caretakers);
            treaty_on(caretakers, owner, DIPLO_VENDETTA | DIPLO_COMMLINK);
            set_treaty(caretakers, owner, DIPLO_UNK_40, 1);
            bool viewer = owner == CurrentPlayerFaction;
            Factions[owner].diplo_spoke[caretakers] = CurrentTurn;
            if (viewer) X_pops_11("NOTRANSCEND", 0x100000, FactionPortraits[caretakers], kPopWaitCallback);
        }
    }
    // The two alien factions are permanently at war.
    for (int32_t i = 1; i < 8; i++) {
        for (int32_t j = 1; j < 8; j++) {
            if (i == j || !(MFactions[i].rule_flags & RFLAG_ALIEN) || !(MFactions[j].rule_flags & RFLAG_ALIEN)) continue;
            set_treaty(j, i, 0xC80C0C38, 1);
            set_agenda(j, i, 0x2C2D, 1);
            Factions[i].diplo_spoke[j] = CurrentTurn;
            Factions[j].diplo_spoke[i] = CurrentTurn;
        }
    }
    random_events(0);
    if (CurrentTurn == 75 && has_tech(Facility[FAC_BIOLOGY_LAB].preq_tech, CurrentPlayerFaction)) interlude(1, 0, 1, 0);
    int32_t viewer = CurrentPlayerFaction;
    if (ascending() && !(Factions[viewer].player_flags & PFLAG_UNK_2000)) {
        Factions[viewer].player_flags |= PFLAG_UNK_2000;
        say_leader(viewer, 0);
        popp(ScriptFileName, "ASCENT", 0, "asctran_sm.pcx", 0);
    }
    if ((CurrentTurn & 3) == 1) reset_territory();
    do_all_non_input();
}


// ---- faction upkeep -------------------------------------------------------------------------------

void GAMEAPI repair_phase(int32_t faction_id) {
    Factions[faction_id].ODP_deployed = 0;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& veh = Vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id) continue;
        int32_t x = veh.x;
        int32_t y = veh.y;
        uint8_t damage_before = veh.damage_taken;
        veh.iter_count = 0;
        veh.moves_spent = 0;
        veh.state &= 0xFBFFDFFD;
        veh.flags &= ~0x1000;
        if (((CurrentTurn + veh_id) & 3) == 0) {
            veh.state &= ~0x800;
            veh.flags &= (veh.flags & 2) ? 0xFFFD : 0xFFFE;
        }
        // Sentry/hold with a turn count: wakes up when it runs out.
        if ((veh.order == ORDER_SENTRY_BOARD || veh.order == ORDER_HOLD) && veh.waypoint_y[0] &&
            --veh.waypoint_y[0] == 0)
            veh.order = ORDER_NONE;
        if (veh.state & 8) {
            MAP* sq = map_tile(x, y);
            int32_t triad = veh_triad(veh_id);
            if (((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) || (triad == TRIAD_LAND && (sq->items & BIT_BUNKER)) ||
                (triad == TRIAD_AIR && (sq->items & BIT_AIRBASE)))
                veh.state &= ~8;
        }
        if (veh.unit_id == 19) veh.faction_id = 0;  // Fungal Tower
        int32_t unit_id = veh.unit_id;
        // Battle ogres never heal; other units heal when they did not move (towers always).
        if (unit_id == 16 || unit_id == 17 || unit_id == 18) continue;
        if (unit_id != 19 && (veh.state & 4)) continue;
        if (!veh.damage_taken) continue;
        uint32_t rate = 1;
        if (unit_id != 19) {
            MAP* sq = map_tile(x, y);
            if (Weapon[Units[unit_id].weapon_id].offense_value < 0 && unit_id < 64 && (sq->items & BIT_FUNGUS) &&
                (sq->climate & 0xE0) > 0x3F)
                rate = 2;
            if (whose_territory(faction_id, x, y, nullptr, 0) == faction_id) rate++;
            if (veh_triad(veh_id) == TRIAD_AIR && (map_tile(x, y)->items & BIT_AIRBASE)) rate++;
            if (veh_triad(veh_id) == TRIAD_LAND && (map_tile(x, y)->items & BIT_BUNKER)) rate++;
        }
        int32_t base_id = base_at(x, y);
        if (base_id >= 0 && !(Bases[base_id].state_flags & 2)) {
            rate++;
            int32_t unit = veh.unit_id;
            if (Weapon[Units[unit].weapon_id].offense_value < 0 && unit < 64) {
                if (breed_mod(base_id, Bases[base_id].faction_id)) rate = veh.damage_taken;
            } else {
                // Command Center / Naval Yard / Aerospace Complex, or the matching secret project
                // of the vehicle's faction: full repair.
                int32_t facility, project;
                int32_t triad = unit_triad(unit);
                if (triad == TRIAD_SEA) {
                    facility = FAC_NAVAL_YARD;
                    project = 10;
                } else if (triad == TRIAD_AIR) {
                    facility = FAC_AEROSPACE_COMPLEX;
                    project = 35;
                } else {
                    facility = FAC_COMMAND_CENTER;
                    project = 1;
                }
                int32_t offset, mask;
                bitmask(facility, &offset, &mask);
                if ((Bases[base_id].facilities_built[offset] & (uint8_t)mask) || has_sp(project, faction_id))
                    rate = veh.damage_taken;
            }
        }
        // Land units carried by a transport with repair equipment heal twice as fast.
        bool repair_bay = false;
        if (veh_triad(veh_id) == TRIAD_LAND && veh_cargo(veh_id) &&
            (veh.order == ORDER_SENTRY_BOARD || (map_tile(x, y)->climate & 0xE0) < 0x60)) {
            int32_t top = stack_top(veh_id);
            if (top >= 0) {
                for (int32_t i = top; i >= 0; i = Vehs[i].next_veh_id_stack)
                    if (i != veh_id && has_abil(Vehs[i].unit_id, ABL_REPAIR)) repair_bay = true;
                if (repair_bay) rate *= 2;
            }
        }
        bool nano_factory = has_sp(FAC_NANO_FACTORY - 70, faction_id);
        if (nano_factory) rate = veh.damage_taken;
        uint32_t reactor = Units[veh.unit_id].reactor_id;
        int32_t repair = (int32_t)(reactor * rate);
        // Units in the field heal only down to a floor of twice the reactor (none on fungus for
        // natives or with the Xenoempathy Dome, which also heals one more reactor step).
        uint32_t floor = 0;
        if (faction_id && !repair_bay && base_id < 0 && !nano_factory) {
            floor = reactor * 2;
            MAP* sq = map_tile(x, y);
            if (reactor && (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) > 0x3F) {
                if (Weapon[Units[veh.unit_id].weapon_id].offense_value < 0 && veh.unit_id < 64) floor = 0;
                if (has_sp(FAC_XENOEMPATHY_DOME - 70, faction_id)) {
                    floor = 0;
                    repair += reactor;
                }
            }
        }
        uint32_t damage = veh.damage_taken;
        uint32_t value = floor;
        uint32_t remaining = damage - repair;
        if ((int32_t)floor <= (int32_t)remaining && floor < 1000) value = (int32_t)remaining > 999 ? 999 : remaining;
        if ((int32_t)damage <= (int32_t)value) value = damage;
        veh.damage_taken = (uint8_t)value;
        // A human's sentried unit wakes up once healed (land units not at sea).
        if ((value & 0xFF) <= floor && is_human(faction_id) && veh.order == ORDER_SENTRY_BOARD &&
            (value & 0xFF) != damage_before &&
            (veh_triad(veh_id) != TRIAD_LAND || (map_tile(x, y)->climate & 0xE0) > 0x5F))
            veh.order = ORDER_NONE;
    }
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        const VEH& veh = Vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((!(sq->items & BIT_BASE_IN_TILE) || (sq->val2 & 0xF) > 7) &&
            (veh.faction_id == (uint32_t)CurrentPlayerFaction ||
             (veh.visibility & (uint8_t)(1 << (CurrentPlayerFaction & 31)))))
            draw_tile(veh.x, veh.y, -1);
    }
    do_all_draws();
}

void GAMEAPI production_phase(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    f.best_mineral_output = 0;
    f.energy_surplus_total = 0;
    f.facility_maint_total = 0;
    f.turn_commerce_income = 0;
    tech_effects(faction_id);
    // Loan payments (suspended at war: the payment is added to the balance).
    for (int32_t i = 1; i < 8; i++) {
        if (i == faction_id || !is_alive(i) || !is_alive(faction_id)) continue;
        if (!f.loan_balance[i] || Factions[i].sanction_turns) continue;
        if (f.diplo_status[i] & DIPLO_VENDETTA) {
            f.loan_balance[i] = f.loan_payment[i] + f.loan_balance[i];
            continue;
        }
        int32_t payment = f.loan_payment[i];
        if (f.energy_credits <= payment) payment = f.energy_credits;
        Factions[i].energy_credits += payment;
        f.loan_balance[i] -= payment;
        f.energy_credits -= payment;
        if (payment < f.loan_payment[i]) f.loan_balance[i] += f.loan_payment[i] - payment;
    }
    f.player_flags &= ~PFLAG_SELF_AWARE_COLONY_LOST_MAINT;
    EnergyCredits = f.energy_credits;
    for (int32_t i = 0; i < 9; i++) f.social_effic[i] = 0;
    for (int32_t i = 0; i < 8; i++) {
        f.social_support[i] = 0;
        for (int32_t j = 0; j < 9; j++) f.social_psych[i][j] = 0;
    }
    f.unk_45 = 0;
    f.unk_46 = 0;
    f.unk_47 = 0;
    f.nutrient_surplus_total = 0;
    f.labs_total = 0;
    for (int32_t base_id = 0; base_id < BaseCount; base_id++) {
        if (Bases[base_id].faction_id != faction_id) continue;
        if (base_upkeep(base_id)) base_id--;  // the base was destroyed
        do_all_non_input();
    }
    // Hidden facility maintenance discount for AIs at Transcend (1/3) and Thinker (2/3).
    if (!is_human(faction_id) && DiffLevel > 3 && f.facility_maint_total) {
        int32_t maint = f.facility_maint_total;
        int32_t discount = DiffLevel == 4 ? maint / 3 : maint * 2 / 3;
        f.energy_credits += discount;
        f.facility_maint_total = maint - discount;
    }
    // INTEREST: a percentage of the reserves, or +1 per base.
    const MFaction& m = MFactions[faction_id];
    if (m.rule_flags & RFLAG_INTEREST) {
        if (m.rule_interest == 0) {
            for (int32_t i = 0; i < BaseCount; i++) {
                if (Bases[i].faction_id == faction_id) {
                    f.energy_credits++;
                    f.energy_surplus_total++;
                }
            }
        } else {
            f.energy_credits = m.rule_interest * f.energy_credits / 100 + f.energy_credits;
        }
    }
    f.unk_18 = f.energy_surplus_total;
    f.unk_17 = f.turn_commerce_income - f.facility_maint_total + f.energy_surplus_total;
    if (CurrentTurn == 1) {
        SkipTechScreenA = 1;
        for (int32_t i = 0; i < MFactions[faction_id].rule_tech_selected; i++) {
            log_say("Freebie Advance", MFactions[faction_id].name_leader, faction_id, 0, 0);
            tech_advance(faction_id);
        }
        SkipTechScreenA = 0;
    }
    if (full_game_turn() && f.sanction_turns && --f.sanction_turns == 0 && faction_id == CurrentPlayerFaction) {
        if (MFactions[faction_id].rule_flags & RFLAG_ALIEN)
            popp(ScriptFileName, "SANCTIONSENDALIEN", 0, "Alopdir.pcx", 0);
        else
            popp(ScriptFileName, "SANCTIONSEND", 0, "council_sm.pcx", 0);
    }
}

void GAMEAPI allocate_energy(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    if (is_human(faction_id)) {
        // Warning to the viewer when its reserves fall although energy goes to economy.
        if (f.diff_level < 2 || faction_id != CurrentPlayerFaction || f.base_count < 2 || f.energy_credits > 99 ||
            f.energy_credits * 11 - EnergyCredits * 10 >= 0)
            return;
        if (10 - f.SE_alloc_labs - f.SE_alloc_psych < 5) {
            if (popp(ScriptFileName, "INFRASTRUCTURE", 0, "genwarning_sm.pcx", 0)) social_select(faction_id);
            return;
        }
        if (!has_tech(Units[1].preq_tech, faction_id)) return;
        if (CurrentTurn % 5 != 0) return;
        popp(ScriptFileName, "INFRASTRUCTURE2", 0, "genwarning_sm.pcx", 0);
        return;
    }
    int32_t economy = 10 - f.SE_alloc_psych - f.SE_alloc_labs;
    int32_t police = f.SE_police_pending;
    int32_t psych_goal = police < -2 ? 4 : police == -2 ? 2 : police <= 1;
    // Psych allocation: up while bases riot, down when no base needs specialists.
    bool lower = true;
    bool raise = false;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id != faction_id) continue;
        // BUG(original): tests the current base (CurrentBase), not base i.
        const BASE& cb = cur_base();
        if (cb.state_flags & 2) {
            raise = true;
            break;
        }
        if (cb.specialist_total > cb.specialist_adjust) lower = false;
    }
    if (raise) {
        bool done = f.SE_alloc_psych >= psych_goal || ++f.SE_alloc_psych >= psych_goal;
        // Severe police states: one more step per rioting base whose talents are outnumbered.
        while (!done && f.SE_police_pending < -2) {
            bool found = false;
            for (int32_t i = 0; i < BaseCount; i++) {
                if (Bases[i].faction_id != faction_id) continue;
                set_base(i);
                base_compute(1);
                const BASE& cb = cur_base();
                if (cb.talent_total < cb.drone_total && (cb.state_flags & 2)) {
                    found = true;
                    break;
                }
            }
            if (!found) break;
            done = ++f.SE_alloc_psych >= psych_goal;
        }
    } else if (lower) {
        if (f.SE_alloc_psych > psych_goal / 2) f.SE_alloc_psych--;
    }
    int32_t psych_min = f.SE_police_pending > -2 ? 0 : 2;
    f.SE_alloc_psych = (psych_min <= f.SE_alloc_psych && psych_min < 5) ? (f.SE_alloc_psych < 5 ? f.SE_alloc_psych : 4)
                                                                        : psych_min;
    if (has_sp(FAC_TELEPATHIC_MATRIX - 70, faction_id)) f.SE_alloc_psych = 0;
    // Labs allocation from the AI priorities.
    f.SE_alloc_labs = (f.AI_tech - f.AI_power - f.SE_alloc_psych + 11) / 2 - f.AI_wealth;
    if (CurrentTurn < (f.AI_tech - f.AI_power - f.AI_wealth + 3) * 25) f.SE_alloc_labs++;
    f.SE_alloc_labs += f.energy_credits /
                       ((MFactions[faction_id].rule_techcost * (f.AI_power + 1 + f.AI_wealth) + CurrentTurn) * 2);
    int32_t region = 1;
    for (; region < 128; region++) {
        if (!f.region_total_bases[region]) continue;
        if (f.region_base_plan[region] == 2) {  // defensive
            f.SE_alloc_labs--;
            break;
        }
        if (f.region_base_plan[region] == 0 && (f.AI_wealth || f.AI_power)) f.SE_alloc_labs--;
    }
    if (f.AI_tech && !f.AI_wealth && !f.AI_power && region > 127) f.SE_alloc_labs++;
    if (economy && economy <= 10 - f.SE_alloc_labs - f.SE_alloc_psych) {
        // Net energy of the bases after facility maintenance against a per-base need.
        int32_t net = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            if (Bases[i].faction_id != faction_id) continue;
            net += Bases[i].unk_total;
            for (int32_t fac = 0; fac < 70; fac++) {
                if (fac >= 65) continue;
                int32_t offset, mask;
                bitmask(fac, &offset, &mask);
                if (Bases[i].facilities_built[offset] & (uint8_t)mask) net -= fac_maint(fac, faction_id);
            }
        }
        int32_t need = (f.AI_wealth * 2 + 4 - f.AI_tech) * f.base_count / 4;
        if (need < net) {
            while (economy <= 10 - f.SE_alloc_labs - f.SE_alloc_psych) f.SE_alloc_labs++;
        } else if (net < need / 2) {
            f.SE_alloc_labs--;
        }
    }
    if (f.tech_research_id == 88) f.SE_alloc_labs--;  // Transcendent Thought
    int32_t cap = 10 - f.SE_alloc_psych;
    if (energy_limit(faction_id) <= cap) cap = energy_limit(faction_id);
    int32_t labs = f.SE_alloc_labs;
    f.SE_alloc_labs = (labs < 0 || cap < 0) ? 0 : (labs <= cap ? labs : cap);
    while (energy_limit(faction_id) < 10 - f.SE_alloc_labs - f.SE_alloc_psych) f.SE_alloc_labs++;
    int32_t effic = f.SE_effic_2 < 0 ? 0 : (f.SE_effic_2 > 99 ? 99 : f.SE_effic_2);
    int32_t small = 3 - f.base_count;
    small = small < 0 ? 0 : (small > 3 ? 3 : small);
    effic = small + 1 + effic;
    labs = f.SE_alloc_labs;
    f.SE_alloc_labs = labs < 0 ? 0 : (labs > 10 ? 10 : labs);
    while (effic - f.SE_alloc_labs - f.SE_alloc_psych + 10 < f.SE_alloc_labs) f.SE_alloc_labs--;
    while (f.SE_alloc_labs + effic < 10 - f.SE_alloc_labs - f.SE_alloc_psych) f.SE_alloc_labs++;
}

void GAMEAPI faction_upkeep(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    ControlUpkeepA = 1;
    social_upkeep(faction_id);
    do_all_non_input();
    repair_phase(faction_id);
    do_all_non_input();
    production_phase(faction_id);
    do_all_non_input();
    if (full_game_turn()) {
        allocate_energy(faction_id);
        do_all_non_input();
        enemy_diplomacy(faction_id);
        do_all_non_input();
        enemy_strategy(faction_id);
        do_all_non_input();
        social_ai(faction_id, -1, -1, -1, -1, 0);
        do_all_non_input();
        // An AI with economic victory available corners the Global Energy Market when it can pay.
        if (!is_human(faction_id) && corner_market(faction_id) < f.energy_credits &&
            has_tech(Rules.tech_preq_economic_victory, faction_id) && (GameRules & RULES_VICTORY_ECONOMIC)) {
            f.corner_market_turn = Rules.turns_corner_global_energy_market + CurrentTurn;
            f.corner_market_cost = corner_market(faction_id);
            f.energy_credits -= corner_market(faction_id);
            say_faction(faction_id);
            parse_says(3, MFactions[faction_id].adj_name_faction, -1, -1);
            parse_num(0, game_year(f.corner_market_turn));
            popp(ScriptFileName, "CORNERWARNING", 0, "econwin_sm.pcx", 0);
        }
    }
    for (int32_t i = 0; i < BaseCount; i++)
        if (Bases[i].faction_id == faction_id) Bases[i].state_flags &= 0xBFFFFFFE;
    f.energy_credits -= f.hurry_cost_total;
    f.hurry_cost_total = 0;
    if (f.energy_credits < 0) f.energy_credits = 0;
    if (!f.base_count && !f.units_active[0] && !f.units_active[21]) eliminate_player(faction_id, 0);
    ControlUpkeepA = 0;
    Paths.xDst = -1;
    Paths.yDst = -1;
    if (full_game_turn()) {
        if (faction_id == CurrentPlayerFaction && !(GameState & (STATE_COUNCIL_HAS_CONVENED | STATE_DISPLAYED_COUNCIL_AVAIL_MSG)) &&
            can_call_council(faction_id, 0) && !(GameState & STATE_GAME_DONE)) {
            GameState |= STATE_DISPLAYED_COUNCIL_AVAIL_MSG;
            popp(ScriptFileName, "COUNCILOPEN", 0, "council_sm.pcx", 0);
        }
        // call_council's uninitialised flag: corner_market's residue (see call_council).
        if (!is_human(faction_id)) call_council_body(faction_id, 1);
    }
    if (!MultiplayerActive && (GamePreferences & PREF_BSC_AUTOSAVE_EACH_TURN) && faction_id == CurrentPlayerFaction)
        auto_save();
}


// ---- small Planet functions -----------------------------------------------------------------------

void GAMEAPI ascent() {
    if (CurrentTurn == 75 && has_tech(Facility[FAC_BIOLOGY_LAB].preq_tech, CurrentPlayerFaction)) interlude(1, 0, 1, 0);
    int32_t viewer = CurrentPlayerFaction;
    if (ascending() && !(Factions[viewer].player_flags & PFLAG_UNK_2000)) {
        Factions[viewer].player_flags |= PFLAG_UNK_2000;
        say_leader(viewer, 0);
        popp(ScriptFileName, "ASCENT", 0, "asctran_sm.pcx", 0);
    }
}

void GAMEAPI climactic_changes() {
    if (!ClimateFutureChange) return;
    ClimateValueC += ClimateValueA;
    if (ClimateValueC < ClimateValueB) return;
    ClimateValueC -= ClimateValueB;
    int32_t step = ClimateFutureChange > 0 ? 1 : (ClimateFutureChange >= 0) - 1;
    ClimateFutureChange -= step;
    MapSeaLevel += step;
    world_climate();
    draw_map(1);
    if (step > 0)
        popp(ScriptFileName, "SEARISING", 0, "searis_sm.pcx", 0);
    else
        popp(ScriptFileName, "SEAFALLING", 0, "seafal_sm.pcx", 0);
}

int32_t GAMEAPI generators(int32_t faction_id, int32_t* out) {
    if (out) *out = 0;
    if (!(MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return 0;
    int32_t count = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != (uint32_t)faction_id || !base_has_facility(i, FAC_SUBSPACE_GENERATOR))
            continue;
        if (Bases[i].pop_size < Rules.base_size_subspace_gen) {
            if (out) ++*out;
        } else {
            count++;
        }
    }
    return count;
}

void GAMEAPI mon_winning_unify(int32_t faction_id, int32_t other_id) {
    int32_t year = game_year(CurrentTurn);
    Monument& m = Monuments[faction_id];
    MonumentEvent& e = m.events[13];
    if ((MFactions[faction_id].rule_flags & RFLAG_ALIEN) || e.achieved) return;
    e.year = year;
    e.faction_id = faction_id;
    e.prev_count = 1;
    e.achieved = 1;
    e.param2 = other_id;
    bool viewer = faction_id == CurrentPlayerFaction;
    m.latest_event_id = 13;
    if (viewer) monument(1);
}

// ---- end of game ------------------------------------------------------------------------------------

int32_t GAMEAPI end_of_game(int32_t check) {
    int32_t winner = -1;
    if ((GameState & STATE_GAME_DONE) && (GameState & STATE_FINAL_SCORE_DONE)) return 0;
    if (!(GameState & STATE_GAME_DONE)) {
        if (!check) return 0;
        if (CurrentMissionYear >= EndingMissionYear) {
            GameState |= STATE_GAME_DONE;
            GameVictoryType = 8;
        }
        for (int32_t f = 1; f < 8; f++) {
            uint8_t bit = (uint8_t)(1 << f);
            if (!(FactionStatus[1] & bit)) continue;
            Faction& fac = Factions[f];
            // Alien beacon: enough subspace generators in large enough bases.
            int32_t gens = 0;
            if (MFactions[f].rule_flags & RFLAG_ALIEN) {
                for (int32_t b = 0; b < BaseCount; b++) {
                    if ((uint8_t)Bases[b].faction_id == f && has_fac(FAC_SUBSPACE_GENERATOR, b, 0) &&
                        Bases[b].pop_size >= Rules.base_size_subspace_gen)
                        gens++;
                }
            }
            bool coop = (Factions[f].diplo_status[CurrentPlayerFaction] & DIPLO_PACT) && (GameRules & RULES_VICTORY_COOPERATIVE);
            if (gens >= Rules.subspace_gen_req) {
                fac.player_flags |= PFLAG_UNK_100000;
                if (f == CurrentPlayerFaction)
                    GameVictoryType = 16;
                else if (coop) {
                    if (GameVictoryType != 16) GameVictoryType = 17;
                } else if (GameVictoryType != 16 && GameVictoryType != 17) {
                    GameVictoryType = 18;
                }
                GameState |= STATE_GAME_DONE;
                if (winner < 0 || f == CurrentPlayerFaction) winner = f;
                GameMoreRules |= 0x80;
            }
            // Economic victory: the Global Energy Market cornered this turn.
            if (fac.corner_market_turn > 0 && fac.corner_market_turn == CurrentTurn && fac.corner_market_cost > 0) {
                fac.player_flags |= PFLAG_UNK_40000;
                bool coop2 = (Factions[f].diplo_status[CurrentPlayerFaction] & DIPLO_PACT) &&
                             (GameRules & RULES_VICTORY_COOPERATIVE);
                if (f == CurrentPlayerFaction)
                    GameVictoryType = 12;
                else if (coop2) {
                    if (GameVictoryType != 12) GameVictoryType = 13;
                } else if (GameVictoryType != 12 && GameVictoryType != 13) {
                    GameVictoryType = 14;
                }
                GameState |= STATE_GAME_DONE | STATE_VICTORY_ECONOMIC;
                winner = f;
                if (f == CurrentPlayerFaction) break;
                continue;
            }
            // Scenario sudden death by objectives.
            if (((FactionStatus[0] & bit) || !(GameRules & RULES_SCN_VICT_SOLO_MISSION)) &&
                ObjectivesSuddenDeathVictory > 0 &&
                num_objectives(f, GameRules & RULES_VICTORY_COOPERATIVE) >= ObjectivesSuddenDeathVictory) {
                GameState |= STATE_GAME_DONE;
                GameVictoryType = 9;
                if (winner < 0 || f == CurrentPlayerFaction) winner = f;
            }
        }
        // Retirement and scenario deadline warnings.
        if (!(GameState & STATE_IS_SCENARIO) && !(GameState & STATE_GAME_DONE) &&
            CurrentMissionYear == EndingMissionYear - 20) {
            say_number_text(EndingMissionYear);
            X_pop("RETIREWARNING", 0);
            if (!(GameState & STATE_GAME_DONE)) return 0;
        } else if (!(GameState & STATE_IS_SCENARIO)) {
            if (!(GameState & STATE_GAME_DONE)) return 0;
        } else if (!(GameState & STATE_GAME_DONE)) {
            if (CurrentMissionYear == EndingMissionYear - 20 || CurrentMissionYear == EndingMissionYear - 10) {
                say_number_text(EndingMissionYear);
                X_pop_2("SCENARIO", "SCENARIOWARNING", 0);
            }
            return 0;
        }
    } else if (check && (GameState & STATE_IS_SCENARIO) == 0) {
        // (already over: the warnings are skipped)
    }
    if (GameState & STATE_FINAL_SCORE_DONE) return 0;
    // The faction that won with a council or economic victory (prefer the viewer's partners).
    int32_t victor = 0;
    for (int32_t f = 1; f < 8; f++) {
        uint32_t flags = Factions[f].player_flags;
        if (flags & PFLAG_UNK_20000) victor = f;
        if ((flags & PFLAG_UNK_40000) &&
            (victor == 0 || (Factions[victor].diplo_status[CurrentPlayerFaction] & DIPLO_PACT) || victor == CurrentPlayerFaction))
            victor = f;
    }
    bool credits = false;
    switch (GameVictoryType) {
    case 1:
    case 2:
    case 3:
        credits = true;
        break;
    case 6:
        popp(ScriptFileName, "DIPLOMATICVICTORY", 0, "dipvic_sm.pcx", 0);
        break;
    case 7:
        if (GamePreferences & 0x10000000) {
            if (MFactions[CurrentPlayerFaction].rule_flags & RFLAG_ALIEN)
                amovie_project_2("losealien");
            else
                amovie_project_2(MFactions[CurrentPlayerFaction].is_leader_female ? "losewoman" : "loseman");
        }
        break;
    case 8:
        parse_num(0, CurrentMissionYear);
        StrBuffer[0] = 0;
        say_year(StrBuffer);
        parse_says(0, StrBuffer, -1, -1);
        if (!(GameState & STATE_IS_SCENARIO)) {
            X_pop("TIMELIMIT", 0);
        } else if (!ObjectiveReqVictory || (GameState & STATE_SCN_VICT_HIGHEST_AC_SCORE_WINS)) {
            int32_t best = -9999;
            int32_t best_id = CurrentPlayerFaction;
            for (int32_t f = 1; f < 8; f++) {
                if (!(FactionStatus[1] & (uint8_t)(1 << f))) continue;
                int32_t value;
                int32_t table[9];
                if (GameState & STATE_SCN_VICT_HIGHEST_AC_SCORE_WINS)
                    compute_score(f, &value, table, 1);
                else
                    value = num_objectives(f, GameRules & RULES_VICTORY_COOPERATIVE);
                if (best < value || (value == best && f == CurrentPlayerFaction)) {
                    best_id = f;
                    best = value;
                }
            }
            say_faction(best_id);
            if (best_id == CurrentPlayerFaction)
                X_pop_2("SCENARIO", "SCENTIMEWIN", 0);
            else
                X_pop_2("SCENARIO", "SCENTIMELOSS", 0);
        } else {
            X_pop_2("SCENARIO", "SCENTIMELIMIT", 0);
        }
        break;
    case 9: {
        say_faction(winner);
        if (winner == CurrentPlayerFaction)
            X_pop_2("SCENARIO", "SUDDENDEATH0", 0);
        else
            X_pop_2("SCENARIO", "SUDDENDEATH", 0);
        break;
    }
    case 10:
        say_faction(victor);
        popp(ScriptFileName, "DIPLOMATICCOOP", 0, "dipvic_sm.pcx", 0);
        break;
    case 11:
        say_faction(victor);
        popp(ScriptFileName,
             (Factions[victor].diplo_status[CurrentPlayerFaction] & 0x40020) ? "DIPLOMATICLOSE2" : "DIPLOMATICLOSE", 0,
             "dipvic_sm.pcx", 0);
        break;
    case 12:
        popp(ScriptFileName, "ECONOMICVICTORY", 0, "econwin_sm.pcx", 0);
        break;
    case 13:
        say_faction(victor);
        popp(ScriptFileName, "ECONOMICCOOP", 0, "econwin_sm.pcx", 0);
        break;
    case 14:
        say_faction(victor);
        popp(ScriptFileName,
             (Factions[victor].diplo_status[CurrentPlayerFaction] & 0x40020) ? "ECONOMICLOSE2" : "ECONOMICLOSE", 0,
             "econwin_sm.pcx", 0);
        break;
    case 15:
        if (GameLanguage) X_pop("YOULOSE2", 0);
        break;
    case 16:
        interlude(0x20, 0, 4, 0);
        say_faction(victor);
        popp(ScriptFileName, "ENDBEACON", 0, "beacon_sm.pcx", 0);
        if (GamePreferences & 0x10000000) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, MFactions[CurrentPlayerFaction].search_key);
            CharUpperA(StrBuffer);
            amovie_project_2(!crt_strcmp(StrBuffer, "CARETAKE") ? "close_ct" : "close_us");
        }
        break;
    case 17:
        say_faction(victor);
        popp(ScriptFileName, "ENDBEACONCOOP", 0, "beacon_sm.pcx", 0);
        break;
    case 18:
        if (!(MFactions[CurrentPlayerFaction].rule_flags & RFLAG_ALIEN)) {
            GenderDefault = MFactions[victor].noun_gender;
            PluralDefault = MFactions[victor].is_noun_plural;
            interlude(0x1D, (int32_t)(kMFactionsAddr + victor * (uint32_t)sizeof(MFaction) + offsetof(MFaction, noun_faction)),
                      1, 0);
        }
        say_faction(victor);
        popp(ScriptFileName, "ENDBEACONLOSE", 0, "beaconlose_sm.pcx", 0);
        break;
    default:
        break;
    }
    switch (GameVictoryType) {
    case 4:
    case 5:
    case 6:
    case 12:
        mon_winning_unify(CurrentPlayerFaction, -1);
        break;
    case 10:
    case 13:
        mon_winning_unify(victor, -1);
        mon_winning_unify(CurrentPlayerFaction, victor);
        break;
    case 11:
    case 14:
        mon_winning_unify(victor, -1);
        break;
    default:
        break;
    }
    if (!(GamePreferences & 0x40000)) {
        switch (GameVictoryType) {
        case 4:
        case 6:
        case 12:
            interlude((MFactions[CurrentPlayerFaction].rule_flags & RFLAG_ALIEN) ? 0x21 : 0x13, 0, 4, 0);
            credits = true;
            break;
        case 5:
        case 10:
        case 13:
            interlude(0x14, 0, 4, 0);
            credits = true;
            break;
        default:
            break;
        }
    }
    if (GameVictoryType == 1 || GameVictoryType == 2 || GameVictoryType == 3 || credits) show_credits();
    report_score(1);
    if (!(GameState & STATE_IS_SCENARIO)) {
        quayle(CurrentPlayerFaction);
        hall_of_fame(1);
    }
    show_replay();
    GameState |= STATE_FINAL_SCORE_DONE;
    if (!MultiplayerActive && GameVictoryType != 7 &&
        popp(ScriptFileName, "GAMEOVERMAN", 0, "stars_sm.pcx", 0))
        return 0;
    ControlTurnB = !MultiplayerActive;
    if (MultiplayerActive) {
        NetDaemon_hang_up(&NetState);
        for (int32_t i : {0, 1, 2, 3, 12, 11, 15, 14, 19, 30, 28, 29, 39, 38, 8, 9, 10, 37, 36, 32, 31, 33, 4, 5, 42, 41, 6, 7})
            NetSessionState[i] = 0;
        dword_93E960 = 0xFF;
        NetSessionState[18] = 0;
        for (int32_t i = 0; i < 8; i++) NetSessionState[20 + i] = 0;
        Lock_clear(&NetLock);
        AlphaNet_close(&NetState);
        MultiplayerActive = 0;
    }
    ControlTurnA = 1;
    return 1;
}


// ---- Planetary Council ----------------------------------------------------------------------------

namespace {
// Number of other living factions with more council votes than faction_id.
int32_t votes_rank(int32_t faction_id) {
    int32_t votes = council_votes(faction_id);
    int32_t above = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i != faction_id && is_alive(i) && council_votes(i) > votes) above++;
    }
    return above;
}
// Eligible candidate: alive, not an alien (with the expansion) and among the two largest voters.
bool council_candidate(int32_t faction_id) {
    if (ExpansionEnabled && (MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return false;
    return votes_rank(faction_id) < 2;
}
}  // namespace

int32_t GAMEAPI eligible(int32_t faction_id) {
    if (ExpansionEnabled && (MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return 0;
    int32_t votes = council_votes(faction_id);
    int32_t above = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i != faction_id && is_alive(i) && votes < council_votes(i)) above++;
    }
    return above < 2;
}

int32_t GAMEAPI wants_prop(int32_t faction_id, int32_t proposal, int32_t candidate) {
    Faction& f = Factions[faction_id];
    int32_t value = 0;
    switch (proposal) {
    case PROP_ELECT_PLANETARY_GOVERNOR: {
        if (!is_alive(candidate) || (ExpansionEnabled && (MFactions[candidate].rule_flags & RFLAG_ALIEN)) ||
            votes_rank(candidate) >= 2)
            return -9;
        // Factions with at least as many votes as faction_id.
        int32_t rivals = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (i != faction_id && council_votes(i) >= council_votes(faction_id)) rivals++;
        }
        bool penalty = false;  // the candidate follows social choices faction_id opposes
        if (candidate == faction_id) {
            value = rivals > 1 ? 5 : 6;
            if (rivals == 0) value++;
            if (GovernorFaction < 0) value = value * 2 - 1;
            if (GovernorFaction == faction_id)
                value += 2;
            else if (GovernorFaction >= 0 && (f.diplo_status[GovernorFaction] & 0x70))
                value++;
        } else {
            uint32_t status = f.diplo_status[candidate];
            bool pact = (status & DIPLO_PACT) != 0;
            if (pact && (status & DIPLO_HAVE_SURRENDERED)) {
                value = 99;
            } else {
                // A pact partner that surrendered to faction_id backs the candidate: 999.
                for (int32_t i = 1; i < 8; i++) {
                    uint32_t st = f.diplo_status[i];
                    if (i != faction_id && (st & DIPLO_PACT) && (st & DIPLO_HAVE_SURRENDERED) &&
                        CouncilVoteFor[i] == candidate)
                        value = 999;
                }
                if (GovernorFaction == faction_id) value -= 2;
                if (status & DIPLO_VENDETTA) value--;
                if (status & 0x60) value--;
                if (status & DIPLO_ATROCITY_VICTIM) value--;
                if (value == 0) {
                    if (pact) {
                        value = 1;
                        if (!wants_to_attack(faction_id, candidate, 0) && !wants_to_attack(candidate, faction_id, 0))
                            value = 2;
                    } else {
                        const MFaction& m = MFactions[faction_id];
                        const MFaction& mc = MFactions[candidate];
                        Faction& c = Factions[candidate];
                        if (f.AI_fight == -c.AI_fight) value = -1;
                        if (m.soc_priority_category >= 0) {
                            int32_t choice = (&c.SE_Politics_pending)[m.soc_priority_category];
                            if (choice) {
                                if (choice == m.soc_priority_model) {
                                    value++;
                                } else {
                                    value--;
                                    penalty = true;
                                }
                            }
                        }
                        if (m.soc_opposition_category >= 0) {
                            int32_t choice = (&c.SE_Politics_pending)[m.soc_opposition_category];
                            if (choice && choice == m.soc_opposition_model) {
                                penalty = true;
                                value--;
                            }
                        }
                        if (m.soc_priority_category == mc.soc_opposition_category &&
                            m.soc_priority_model == mc.soc_opposition_model) {
                            value -= 2;
                            penalty = true;  // (the original stores 2)
                        }
                        if ((status & DIPLO_TREATY) && !wants_to_attack(faction_id, candidate, 0) &&
                            !wants_to_attack(candidate, faction_id, 0))
                            value++;
                        if (great_satan(candidate, 0)) {
                            value--;
                        } else if (is_human(candidate)) {
                            if (MultiplayerActive && candidate == RankingFactionIDUnk1 && c.ranking >= 5) {
                                value--;
                            } else if (candidate != RankingFactionIDUnk1 || c.diff_level < 2) {
                                value++;
                            }
                        } else {
                            // BUG(original): mixes ranks and faction ids: the highest rank whose
                            // faction is not human and toward whose *rank number* faction_id has
                            // no war flags; the candidate gains if its id equals that rank.
                            int32_t rank = 7;
                            for (; rank >= 0; rank--) {
                                if (!is_human(cs.FactionRankings[rank]) && !(f.diplo_status[rank] & 0x70)) break;
                            }
                            if (candidate == rank) value++;
                        }
                    }
                    if (GovernorFaction >= 0 && candidate != GovernorFaction && faction_id != GovernorFaction) {
                        uint32_t gov = f.diplo_status[GovernorFaction];
                        if ((gov & DIPLO_PACT) && !(status & DIPLO_PACT)) value -= 2;
                        if ((gov & DIPLO_TREATY) && !(status & DIPLO_TREATY)) value--;
                        if (gov & DIPLO_VENDETTA) value++;
                        if (gov & 0x60) value += 2;
                    }
                    if (value >= 0) {
                        if (rivals <= 1) value++;
                        if (value == 0 && candidate == GovernorFaction) value = 1;
                    }
                    if (!(status & DIPLO_PACT)) {
                        bool human = is_human(candidate);
                        if (human && Factions[candidate].diff_level >= 3 && value > -2) value--;
                        if (human && (GameRules & RULES_INTENSE_RIVALRY) && value > -2) value--;
                    }
                }
            }
        }
        int32_t vote_for = CouncilVoteFor[candidate];
        if (vote_for > 0 && vote_for != candidate) {
            if (candidate == faction_id) return value >= 1 ? value : 1;
            value--;
            if (!is_human(candidate)) value /= 2;
        }
        if (faction_id == candidate) return value >= 1 ? value : 1;
        if (penalty && !(f.diplo_status[candidate] & DIPLO_PACT)) value = value > 0 ? 0 : value;
        if (!is_human(faction_id) && !is_human(candidate)) return value;
        if (f.diplo_status[candidate] & DIPLO_UNK_8000000) return value;
        return value > 0 ? 0 : value;
    }
    case PROP_UNITE_SUPREME_LEADER: {
        if (!is_alive(candidate) || (ExpansionEnabled && (MFactions[candidate].rule_flags & RFLAG_ALIEN)) ||
            votes_rank(candidate) >= 2)
            return -9;
        Faction& c = Factions[candidate];
        if (candidate == faction_id) return f.ranking == 7 ? 5 : 0;
        uint32_t status = f.diplo_status[candidate];
        if ((status & DIPLO_PACT) && (status & DIPLO_HAVE_SURRENDERED)) return 2;
        if (status & DIPLO_PACT) {
            if (!(GameRules & RULES_VICTORY_COOPERATIVE)) return 0;
            if (f.ranking < c.ranking && f.pop_total < c.pop_total && CurrentTurn > 200) return 1;
            if (c.ranking < f.ranking && c.pop_total < f.pop_total) return -2;
            return CurrentTurn > 200 ? 0 : -1;
        }
        if (status & DIPLO_ATROCITY_VICTIM) return -5;
        if (status & 0x30) return -4;
        if (c.ranking != 7 || f.ranking > 5 || CurrentTurn < 200) return -4;
        if (c.mil_strength_1 < f.mil_strength_1 * 2 || c.mil_strength_2 < f.mil_strength_2 * 2 ||
            c.pop_total < f.pop_total * 2)
            return -4;
        return -2;
    }
    case PROP_SALVAGE_UNITY_CORE: {
        value = f.AI_power != 0;
        if (f.AI_fight < 0) value--;
        if (f.energy_credits > 499) value--;
        if (f.energy_credits < 100) value++;
        if (f.energy_credits < 200) value++;
        int32_t poorer = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id) continue;
            poorer += f.energy_credits < Factions[i].energy_credits ? 1 : -1;
            poorer += f.energy_surplus_total - f.facility_maint_total <
                              Factions[i].energy_surplus_total - Factions[i].facility_maint_total
                          ? 1
                          : -1;
        }
        if (poorer > 0) return value + poorer / 2;
        return value;
    }
    case PROP_GLOBAL_TRADE_PACT:
    case PROP_REPEAL_GLOBAL_TRADE_PACT: {
        int32_t n = 0;
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id) continue;
            if (f.tech_commerce_bonus < Factions[i].tech_commerce_bonus) n--;
            n += bit_count(f.diplo_status[i] & 3);
        }
        if (f.AI_wealth) n++;
        value = n;
        if (f.AI_power) value = n - 1;
        if (f.AI_fight > 0) value--;
        if (f.SE_economy_pending > 1) value++;
        if (f.SE_economy_pending < 0) value--;
        return proposal == PROP_REPEAL_GLOBAL_TRADE_PACT ? -value : value;
    }
    case PROP_LAUNCH_SOLAR_SHADE:
    case PROP_INCREASE_SOLAR_SHADE:
    case PROP_MELT_POLAR_CAPS: {
        int32_t dir = proposal == PROP_MELT_POLAR_CAPS ? 1 : -1;
        int32_t trend = MapSeaLevelCouncil > 0 ? 1 : (MapSeaLevelCouncil >= 0) - 1;
        const MFaction& m = MFactions[faction_id];
        if (f.AI_growth || m.rule_psi) {
            if (MapSeaLevelCouncil && dir != trend)
                value = m.rule_psi ? 4 : 2;
            else
                value = m.rule_psi ? -4 : -2;
        }
        if (trend == dir) {
            value -= 2;
        } else if (!MapSeaLevelCouncil) {
            value--;
        } else {
            if (MapSeaLevel >= 0) value++;
            if (f.AI_power) value += dir > 0 ? -1 : 1;
            if (f.AI_wealth) value += dir > 0 ? 1 : -1;
            if (f.AI_tech) value += dir > 0 ? 1 : -1;
        }
        // Bases helped or flooded by the change, weighted by population (own bases 4x, others by
        // how hostile faction_id is to their owner).
        int32_t sum = 0;
        for (int32_t i = 0; i < BaseCount; i++) {
            const BASE& b = Bases[i];
            MAP* sq = map_tile(b.x, b.y);
            int32_t alt = sq->climate & 0xE0;
            int32_t weight = 0;
            if (alt < 0x60) {
                if (dir >= 0)
                    weight = -1;
                else
                    weight = (int32_t)sq->contour - MapSeaLevel >= AltNatural[3] - WorldBuilder.sea_level_rises ? 2 : 1;
            } else if (dir <= 0) {
                if (alt > 0x60) weight = -1;
            } else if (!(base_has_facility(i, FAC_PRESSURE_DOME) && alt == 0x60)) {
                if ((int32_t)sq->contour - MapSeaLevel <= WorldBuilder.sea_level_rises + AltNatural[3])
                    weight = -3;
                else if (alt == 0x60)
                    weight = -1;
                else
                    weight = 1;
            }
            int32_t w = b.pop_size * weight * 2;
            if ((uint8_t)b.faction_id == (uint32_t)faction_id)
                sum += w * 2;
            else
                sum -= bit_count(f.diplo_status[(uint8_t)b.faction_id] & 0xC0030) * w;
        }
        int32_t pop = f.pop_total;
        if (pop < 1) return value + sum / 1;
        if (pop < 100000) return value + sum / pop;
        return value + sum / 99999;
    }
    case PROP_REPEAL_UN_CHARTER:
    case PROP_REINSTATE_UN_CHARTER: {
        bool repeal = proposal == PROP_REPEAL_UN_CHARTER;
        int32_t buster_chassis = Chassis[8].preq_tech;
        int32_t buster_weapon = Weapon[16].preq_tech;
        if (!has_tech(buster_chassis, faction_id) || !has_tech(buster_weapon, faction_id)) {
            for (int32_t i = 1; i < 8; i++) {
                if (has_tech(buster_weapon, i) || has_tech(buster_chassis, i)) {
                    value = repeal ? -2 : 2;
                    break;
                }
            }
        }
        if (!f.planet_busters) {
            if (repeal ? value < 0 : value > 0) value += repeal ? -1 : 1;
            for (int32_t i = 1; i < 8; i++)
                if (Factions[i].planet_busters) value += repeal ? -1 : 1;
        }
        if (f.AI_fight < 0) value += repeal ? -1 : 1;
        if (f.AI_tech && f.AI_growth) value += repeal ? -2 : 2;
        if (repeal) {
            value += f.atrocities;
            if (value >= 0) {
                if (f.AI_fight > 0) value++;
                if (f.AI_power) return value + 1;
            }
        } else {
            value -= f.atrocities;
            if (value < 1) {
                if (f.AI_fight > 0) value--;
                if (f.AI_power) return value - 1;
            }
        }
        return value;
    }
    case 10:
        return -9;
    default:
        return 0;
    }
}

int32_t GAMEAPI council_get_vote(int32_t faction_id, int32_t proposal, int32_t candidate) {
    Faction& f = Factions[faction_id];
    if (proposal == PROP_ELECT_PLANETARY_GOVERNOR) {
        int32_t best = 0;
        int32_t choice = -3;  // abstain
        for (int32_t i = 1; i < 8; i++) {
            int32_t value = wants_prop(faction_id, 0, i);
            if (best < value) {
                best = value;
                choice = i;
            }
        }
        // A bribed vote for a candidate it is not at war with.
        if (f.unk_29[0] && !(f.diplo_status[f.unk_30[0]] & 0x40C30)) return f.unk_30[0];
        return choice;
    }
    int32_t value = wants_prop(faction_id, proposal, candidate);
    uint32_t status = f.diplo_status[candidate];
    if (status & DIPLO_VENDETTA) value--;
    if (status & DIPLO_WANT_REVENGE) value--;
    if (status & DIPLO_PACT) value++;
    if (f.unk_29[proposal]) return f.unk_29[proposal];
    if (value > 0) return -1;
    if (value < 0) return -2;
    return -1 - (CurrentTurn + faction_id) % 3;
}

void GAMEAPI council_action(int32_t proposal, int32_t winner) {
    ProposalPassCount[proposal]++;
    ProposalCallTurn[proposal] = CurrentTurn;
    switch (proposal) {
    case PROP_UNITE_SUPREME_LEADER: {
        GameState |= STATE_VICTORY_DIPLOMATIC;
        if (winner == CurrentPlayerFaction) {
            if (!MultiplayerActive) {
                // Factions with a grievance (atrocities, vendetta wishes) defy the new leader.
                bool defied = false;
                for (int32_t i = 1; i < 8; i++) {
                    if (i == winner || !is_alive(i) || !(GameRules & RULES_VICTORY_CONQUEST)) continue;
                    if (!(Factions[i].diplo_status[winner] & 0xC0000) && !(Factions[winner].diplo_status[i] & 0x80000) &&
                        !(Factions[i].diplo_agenda[winner] & 0x800) && !(Factions[i].diplo_agenda[winner] & 8))
                        continue;
                    defied = true;
                    say_faction(i);
                    popp(ScriptFileName, "DEFIES", 0, "dipvic_sm.pcx", 0);
                    treaty_on(i, winner, DIPLO_VENDETTA);
                    agenda_on(i, winner, 0x2008);
                }
                if (defied) {
                    // The others surrender to the leader.
                    for (int32_t i = 1; i < 8; i++) {
                        if (i == winner || !is_alive(i) || (Factions[i].diplo_agenda[winner] & 8)) continue;
                        treaty_on(i, winner, 0x43);
                        treaty_off(i, winner, 0xC20);
                        agenda_off(i, winner, 0xC00);
                        set_treaty(i, winner, DIPLO_HAVE_SURRENDERED, 1);
                    }
                    return;
                }
            }
            GameVictoryType = 6;
            Factions[winner].player_flags |= PFLAG_UNK_20000;
            GameState |= STATE_GAME_DONE;
            return;
        }
        // The viewer accedes (game over) or defies the new leader.
        bool coop = (GameRules & RULES_VICTORY_COOPERATIVE) && (Factions[CurrentPlayerFaction].diplo_status[winner] & DIPLO_PACT);
        say_faction(winner);
        if (MultiplayerActive || !popp(ScriptFileName, coop ? "ACCEDECOOP" : "ACCEDE", 0, "dipvic_sm.pcx", 0)) {
            GameVictoryType = coop ? 10 : 11;
            GameState |= STATE_GAME_DONE;
            Factions[winner].player_flags |= PFLAG_UNK_20000;
            return;
        }
        popp(ScriptFileName, "DEFY", 0, "council_sm.pcx", 0);
        for (int32_t i = 1; i < 8; i++) {
            int32_t viewer = CurrentPlayerFaction;
            uint32_t status = Factions[i].diplo_status[viewer];
            if (i == viewer || !is_alive(i) || ((status & DIPLO_PACT) && (status & DIPLO_HAVE_SURRENDERED))) continue;
            treaty_on(i, viewer, 0x50);
            if (i != winner) {
                treaty_on(i, winner, DIPLO_PACT);
                agenda_on(i, winner, 0x2000);
            }
            set_agenda(i, CurrentPlayerFaction, 8, 1);
        }
        return;
    }
    case PROP_SALVAGE_UNITY_CORE:
        for (int32_t i = 1; i < 8; i++)
            if (!(MFactions[i].rule_flags & RFLAG_ALIEN)) Factions[i].energy_credits += 500;
        Console_update_data(&MainConsole, 0);
        return;
    case PROP_LAUNCH_SOLAR_SHADE:
    case PROP_INCREASE_SOLAR_SHADE:
    case PROP_MELT_POLAR_CAPS: {
        bool rise = proposal == PROP_MELT_POLAR_CAPS;
        MapSeaLevelCouncil += rise ? 3 : -3;
        ClimateFutureChange += rise ? WorldBuilder.sea_level_rises : -WorldBuilder.sea_level_rises;
        ClimateValueA = ClimateFutureChange < 0 ? -ClimateFutureChange : ClimateFutureChange;
        ClimateValueB = 20;
        ClimateValueC = 0;
        parse_num(0, 20);
        parse_num(1, WorldBuilder.sea_level_rises * 1000 / 15);
        bool alien = (MFactions[CurrentPlayerFaction].rule_flags & RFLAG_ALIEN) != 0;
        if (rise)
            popp(ScriptFileName, alien ? "WILLRISEALIEN" : "WILLRISE", 0, "searis_sm.pcx", 0);
        else
            popp(ScriptFileName, alien ? "WILLFALLALIEN" : "WILLFALL", 0, "seafal_sm.pcx", 0);
        return;
    }
    default:
        return;
    }
}

int32_t GAMEAPI can_call_council(int32_t faction_id, int32_t show_reason) {
    if (MFactions[faction_id].rule_flags & RFLAG_ALIEN) return 0;
    if ((GameMoreRules & MRULES_NO_PLANETARY_COUNCIL) && (!(GameState & STATE_SCENARIO_EDITOR) || !(GameState & STATE_OMNISCIENT_VIEW)))
        return 0;
    bool tell = show_reason && faction_id == CurrentPlayerFaction && is_human(faction_id);
    if (SunspotDuration > 0) {
        if (tell) NetMsg_pop(&NetMsg, "SUNSPOTSNO2", -5000, 0, "solar_sm.pcx");
        return 0;
    }
    if (PbemActive && CouncilSessionPending) {
        if (tell) NetMsg_pop(&NetMsg, "COUNCILHOT", -5000, 0, nullptr);
        return 0;
    }
    // Without the UN Headquarters, faction_id needs commlinks with every living faction.
    if (SecretProjects[4] >= 0 && (uint8_t)Bases[SecretProjects[4]].faction_id == (uint32_t)faction_id) return 1;
    for (int32_t i = 1; i < 8; i++) {
        if (MFactions[i].rule_flags & RFLAG_ALIEN) continue;
        if (!is_alive(i) || i == faction_id || (Factions[faction_id].diplo_status[i] & 0x8000008)) continue;
        if ((GameState & STATE_OMNISCIENT_VIEW) && is_human(faction_id) && faction_id == CurrentPlayerFaction) continue;
        if (MultiplayerActive && is_human(faction_id) && is_human(i)) continue;
        if (tell) NetMsg_pop(&NetMsg, "COUNCILNEEDALL", -5000, 0, "council_sm.pcx");
        return 0;
    }
    return 1;
}

// call_council's [ebp-0x14] (the "aliens alive" flag) is only written when proposal 1 is
// considered. BUG(original): otherwise it holds the residue of the previous call at that stack
// depth: in faction_upkeep, corner_market's pushed argument 1 (it calls mind_control for foreign
// bases), so AI factions then never propose anything but governor elections.
namespace {
void call_council_body(int32_t faction_id, int32_t aliens_residue) {
    if (ExpansionEnabled && (MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return;
    if (!faction_id) return;
    bool human = is_human(faction_id);
    if (ControlTurnC && (human || MultiplayerActive)) return;
    if (!can_call_council(faction_id, faction_id == CurrentPlayerFaction && human)) return;
    // AIs call it every 16 turns once there is a governor; never when the game is over.
    if (!human && GovernorFaction > 0 && ((CurrentTurn + faction_id * 2) & 0xF)) return;
    if (!human && (GameState & STATE_GAME_DONE)) return;
    if (MultiplayerActive && (GameMoreRules & MRULES_UNK_10) && human && CurrentFaction != faction_id) {
        NetMsg_pop(&NetMsg, "SIMULNO", 5000, 0, nullptr);
        return;
    }
    for (int32_t i = 1; i < 8; i++)
        if (!human && is_human(i) && (MFactions[i].rule_flags & RFLAG_ALIEN)) return;
    int32_t aliens = aliens_residue;
    int32_t best = 0;
    int32_t best_proposal = -1;
    int32_t best_candidate = faction_id;
    int32_t items = 0;
    ui_council_menu_open(faction_id);
    bool cheat = (GameState & STATE_OMNISCIENT_VIEW) && (GameState & STATE_DEBUG_MODE);
    for (int32_t p = 0; p < 11; p++) {
        if (!has_tech(Proposal[p].preq_tech, faction_id) && !cheat) continue;
        if (Proposal[p].preq_tech < -1) continue;
        bool offer = true;
        switch (p) {
        case PROP_ELECT_PLANETARY_GOVERNOR: {
            if (!council_candidate(faction_id) && !human && GovernorFaction > 0) {
                offer = false;
                break;
            }
            if (GovernorFaction == faction_id && !human) {
                offer = false;
                break;
            }
            if (human) break;
            if (GovernorFaction > 0 && (Factions[faction_id].diplo_status[GovernorFaction] & DIPLO_PACT)) {
                if (Factions[faction_id].ranking < Factions[GovernorFaction].ranking ||
                    (Factions[faction_id].diplo_status[GovernorFaction] & DIPLO_HAVE_SURRENDERED)) {
                    offer = false;
                    break;
                }
            }
            // Worth calling when faction_id would get half the votes, or the governor is no AI.
            int32_t total = 0, mine = 0;
            for (int32_t i = 1; i < 8; i++) {
                total += council_votes(i);
                if (council_get_vote(i, 0, best_candidate) == faction_id) mine += council_votes(i);
            }
            if (mine * 2 < total && GovernorFaction > 0 && !is_human(GovernorFaction)) offer = false;
            break;
        }
        case PROP_UNITE_SUPREME_LEADER:
            if (!(GameRules & RULES_VICTORY_DIPLOMATIC) || (GameState & STATE_GAME_DONE) || ProposalPassCount[1] ||
                !council_candidate(faction_id)) {
                offer = false;
                break;
            }
            aliens = 0;
            for (int32_t i = 1; i < 8; i++)
                if ((MFactions[i].rule_flags & RFLAG_ALIEN) && is_alive(i)) aliens = 1;
            break;
        case PROP_SALVAGE_UNITY_CORE:
        case PROP_LAUNCH_SOLAR_SHADE:
            offer = ProposalPassCount[p] == 0;
            break;
        case PROP_GLOBAL_TRADE_PACT:
            offer = ProposalPassCount[3] <= ProposalPassCount[4];
            break;
        case PROP_REPEAL_GLOBAL_TRADE_PACT:
            offer = ProposalPassCount[4] != ProposalPassCount[3];
            break;
        case PROP_INCREASE_SOLAR_SHADE:
            offer = ProposalPassCount[5] != 0;
            break;
        case PROP_REPEAL_UN_CHARTER:
            offer = ProposalPassCount[8] <= ProposalPassCount[9];
            break;
        case PROP_REINSTATE_UN_CHARTER:
            offer = ProposalPassCount[9] < ProposalPassCount[8];
            break;
        default:
            break;
        }
        if (!offer) continue;
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[p].name));
        crt_strcat(StrBuffer, "|");
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[p].description));
        ui_council_menu_add(StrBuffer, p);
        items++;
        if (human) continue;
        Faction& f = Factions[faction_id];
        bool governor_search = false;
        if (f.unk_29[p]) {
            // Committed (bribed) on this proposal: only the governor election is reconsidered,
            // when the candidate it promised to back cannot be elected.
            if (p != 0) continue;
            if (council_candidate(f.unk_30[0])) continue;
            governor_search = true;
        } else if (p != 0) {
            int32_t value = wants_prop(faction_id, p, faction_id);
            if (!aliens && value > best) {
                best = value;
                best_proposal = p;
                best_candidate = faction_id;
            }
            continue;
        } else {
            governor_search = true;
        }
        if (governor_search) {
            for (int32_t c = 1; c < 8; c++) {
                if (c == GovernorFaction || !council_candidate(c)) continue;
                int32_t value = wants_prop(faction_id, 0, c);
                if (value > best) {
                    best = value;
                    best_proposal = 0;
                    best_candidate = c;
                }
            }
        }
    }
    if (!items) {
        ui_council_menu_close();
        return;
    }
    int32_t choice = human ? ui_council_menu_exec() : best_proposal;
    ui_council_menu_close();
    if (aliens && choice == 1) {
        if (faction_id == CurrentPlayerFaction && human)
            NetMsg_pop(&NetMsg, "NOTWHILEALIENSLIVE", -5000, 0, "council_sm.pcx");
        return;
    }
    if (choice < 0) return;
    int32_t interval = Rules.min_turns_between_councils;
    if (choice != 0 && !(cheat && human)) {
        int32_t limit = faction_id == GovernorFaction ? interval / 2 : interval;
        if (CurrentTurn - Factions[faction_id].council_call_turn < limit) {
            if (faction_id == CurrentPlayerFaction && human) {
                parse_num(0, game_year(Factions[CurrentPlayerFaction].council_call_turn));
                parse_num(1, Rules.min_turns_between_councils);
                parse_num(2, Rules.min_turns_between_councils / 2);
                NetMsg_pop(&NetMsg, "COUNCILRECENTCALL", -5000, 0, "council_sm.pcx");
            }
            return;
        }
    }
    if (CurrentTurn - ProposalCallTurn[choice] < interval && !(cheat && human)) {
        if (faction_id == CurrentPlayerFaction && human) {
            parse_num(0, game_year(ProposalCallTurn[choice]));
            parse_num(1, Rules.min_turns_between_councils);
            NetMsg_pop(&NetMsg, "COUNCILRECENTPROP", -5000, 0, "council_sm.pcx");
        }
        return;
    }
    if (!MultiplayerActive) {
        council(faction_id, choice, best_candidate);
        return;
    }
    if (!human) {
        CouncilNetA = faction_id;
        CouncilNetB = choice;
        CouncilNetC = best_candidate;
        return;
    }
    NetMsg_pop(&NetMsg, "CONVENING", 3000, 0, nullptr);
    if (diplo_lock(50)) {
        NetMsg_close(&NetMsg);
        return;
    }
    message_data(0x2600, 0, faction_id, choice, best_candidate, 0);
    ControlWaitLoop = 1;
    while (!ControlTurnA) {
        wait_loop();
        if (!ControlWaitLoop) break;
    }
    diplo_unlock();
}
}  // namespace

#ifdef SMACX_HYBRID
extern "C" {
int32_t call_council_residue_in;
void __cdecl call_council_entry(int32_t faction_id) { call_council_body(faction_id, call_council_residue_in); }
}
// The original's [ebp-0x14] is [esp-0x18] at entry.
__attribute__((naked)) void GAMEAPI call_council(int32_t) {
    __asm__("mov -0x18(%esp), %eax\n mov %eax, _call_council_residue_in\n jmp _call_council_entry\n");
}
#else
void GAMEAPI call_council(int32_t faction_id) { call_council_body(faction_id, 1); }
#endif


// ---- game loops -------------------------------------------------------------------------------------

namespace {
int32_t call_turn_upkeep() {
    if (g_harness_turn_upkeep) return g_harness_turn_upkeep();
    turn_upkeep();
    return 0;
}
void call_faction_upkeep(int32_t faction_id) {
    if (g_harness_faction_upkeep)
        g_harness_faction_upkeep(faction_id);
    else
        faction_upkeep(faction_id);
}
void call_enemy_turn(int32_t faction_id) {
    if (g_harness_enemy_turn)
        g_harness_enemy_turn(faction_id);
    else
        enemy_turn(faction_id);
}
void call_human_turn() {
    if (g_harness_human_turn)
        g_harness_human_turn();
    else
        Console_human_turn(&MainConsole);
}
// Hotseat/PBEM: what the next human player is told when taking over (engine part).
void hotseat_take_turn(int32_t viewer) {
    // Tampering detected by the previous player's checks.
    int32_t other = HotseatTamperFaction[viewer];
    if (other) {
        say_faction(other);
        parse_num(0, HotseatTamperCount[CurrentPlayerFaction]);
        X_pop("HOTTAMPER2", 0);
        HotseatTamperFaction[CurrentPlayerFaction] = 0;
        HotseatTamperCount[CurrentPlayerFaction] = 0;
        viewer = CurrentPlayerFaction;
    }
    while (Factions[viewer].earned_techs_saved > 0) {
        Factions[viewer].earned_techs_saved--;
        tech_advance(viewer);
        viewer = CurrentPlayerFaction;
    }
    // Diplomatic contacts requested by the other players.
    for (int32_t i = 1; i < 8; i++) {
        DiploStateA[i] = DiploStateB[viewer][i];
        DiploStateB[viewer][i] = 0;
        if (i == viewer) continue;
        if (!is_human(i)) {
            if (DiploStateA[i]) {
                communicate(viewer, i, 0);
                viewer = CurrentPlayerFaction;
            }
        } else if (DiploStateA[i]) {
            if (DiploStateC[viewer][i][2] == 3) {
                DiploStateC[viewer][i][2] = 0;
                say_faction(i);
                X_pop("COMMCOMPLETE", 0);
            } else {
                diplo(viewer, i);
            }
            viewer = CurrentPlayerFaction;
        }
        DiploStateA[i] = 0;
    }
    // Council session or its result.
    if (CouncilSessionPending) {
        council(viewer, 0, 0);
        return;
    }
    if (!CouncilVoteState[viewer]) return;
    parse_say(0, Proposal[CouncilProposal[viewer]].name, -1, -1);
    const char* label;
    if (CouncilProposal[CurrentPlayerFaction] == 0) {
        if (GovernorFaction > 0) {
            int32_t g = GovernorFaction;
            PluralDefault = 0;
            GenderDefault = MFactions[g].is_leader_female;
            parse_says(1, MFactions[g].title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = MFactions[g].is_leader_female;
            parse_says(2, MFactions[g].name_leader, -1, -1);
            PluralDefault = MFactions[g].is_noun_plural;
            GenderDefault = MFactions[g].noun_gender;
            parse_says(3, MFactions[g].noun_faction, -1, -1);
        }
        if (CouncilVoteState[CurrentPlayerFaction] == 1)
            label = "COUNCILHOTGOVWIN";
        else
            label = GovernorFaction < 1 ? "COUNCILHOTGOVNONE" : "COUNCILHOTGOVLOSE";
    } else if (CouncilVoteState[CurrentPlayerFaction] == 1) {
        label = "COUNCILHOTPASS";
    } else if (CouncilVoteState[CurrentPlayerFaction] == -2) {
        label = "COUNCILHOTVETO";
    } else {
        label = "COUNCILHOTFAIL";
    }
    popp(ScriptFileName, label, 0, "council_sm.pcx", 0);
    clear_council_notify(CurrentPlayerFaction);
}
}  // namespace

void GAMEAPI control_turn() {
    int32_t password_tries = 0;
    (void)password_tries;
    if (!CurrentTurn && !ControlTurnMove && is_human(CurrentPlayerFaction)) {
        if (!PbemActive) crash_landing(CurrentPlayerFaction);
    }
    if (!PbemActive) draw_map(1);
    dword_93A940 = 1;
    if (ControlTurnMove && !is_human(CurrentPlayerFaction)) Console_human_turn(&MainConsole);
    do {
        if (!PbemActive) MessageWin_clear(&MessageWin);
        if (!ControlTurnMove) {
            ControlTurnC = 1;
            call_turn_upkeep();
            if (end_of_game(1)) return;
        } else {
            rankings(1);
        }
        bool human_moved = false;
        for (int32_t f = 0; f < 8; f++) {
            uint8_t bit = (uint8_t)(1 << f);
            if (!(FactionStatus[1] & bit) || f < ControlTurnMove) continue;
            CurrentFaction = f;
            if ((FactionStatus[0] & bit) &&
                (f != CurrentPlayerFaction || (GameMoreRules & MRULES_UNK_20) || PbemActive)) {
                // A human player takes over the screen (hotseat/PBEM).
                parse_says(0, ControlTurnPlayerText, -1, -1);
                PluralDefault = 0;
                GenderDefault = MFactions[f].is_leader_female;
                parse_says(1, MFactions[f].title_leader, -1, -1);
                GenderDefault = MFactions[f].is_leader_female;
                PluralDefault = 0;
                parse_says(2, MFactions[f].name_leader, -1, -1);
                PluralDefault = MFactions[f].is_noun_plural;
                GenderDefault = MFactions[f].noun_gender;
                parse_says(3, MFactions[f].noun_faction, -1, -1);
                parse_num(0, CurrentMissionYear);
                CurrentPlayerFaction = f;
                if (ui_hotseat_switch(f) || ControlTurnA) return;
                ui_hotseat_show(f);
                hotseat_take_turn(CurrentPlayerFaction);
            }
            if (ControlTurnA) return;
            if (!ControlTurnMove || (PbemActive && (GameMoreRules & MRULES_UNK_40))) {
                ControlTurnC = 1;
                call_faction_upkeep(f);
                if (end_of_game(0)) return;
            }
            GameMoreRules &= ~MRULES_UNK_40;
            ControlTurnC = 0;
            if (!(FactionStatus[0] & bit)) {
                call_enemy_turn(f);
            } else if (f == CurrentPlayerFaction || bit_count(FactionStatus[0]) > 1) {
                call_human_turn();
                if (ControlTurnMove) {
                    // BUG(original): restarts the round at faction 1 (the loop increment follows).
                    f = 0;
                    continue;
                }
                human_moved = true;
            }
            ControlTurnMove = 0;
            if (ControlTurnA || end_of_game(0)) return;
        }
        // The observer's "turn complete" pause after a round without a human turn.
        if (!human_moved) {
            if (g_harness_observer_pause)
                g_harness_observer_pause();
            else
                Console_human_turn(&MainConsole);
        }
    } while (!ControlTurnA);
}

void GAMEAPI control_game() {
    if (system_init()) return;
    if (game_init(0, 1)) return;
    ControlTurnB = 0;
    GameNotStarted = 1;
    ControlTurnA = 0;
    MultiplayerActive = 0;
    int32_t lobby = Net_check_for_lobby(&NetState, GameCmdLine, &LobbyParam, 3, 9);
    if (!lobby) {
        bool registered = ui_shift_pressed() || ui_directplay_registered();
        filefind_init("movies\\virtualWorld.wve", registered);
        prefs_load(0);
        GamePreferences = AlphaIniPrefs.preferences;
        int32_t no_movie = prefs_get_2("DisableOpeningMovie", 0, 0);
        prefs_put_2("DisableOpeningMovie", (uint32_t)no_movie, 0);
        if (!no_movie && (GamePreferences & 0x10000000)) amovie_project_2("openingx");
    }
    if (!ui_shift_pressed()) FX_init(&Sounds);
    for (;;) {
        if (ControlTurnB) {
            if (Wave_is_playing(&FacilityVoice)) Sound_fade_2(&FacilityVoice, 2000);
            Win_hide(ui_console_vbase());
            do_all_draws();
            crt_strcpy(ReloadFilePath, ReloadFileSource);
            filefind_set_alternate(ReloadFilePath);
            if (game_reload(0, 1)) break;
            VehCount = 0;
            setup_game(1);
            GameRules = AlphaIniPrefs.rules;
            GameMoreRules = 0;
        }
        ControlTurnB = 0;
        GameNotStarted = 1;
        ControlTurnA = 0;
        MultiplayerActive = 0;
        flush_input();
        if (lobby) {
            prefs_load(0);
            prefs_use();
            if (multiplayer_init(1)) {
                close_opening();
                break;
            }
        } else {
            int32_t quit = g_harness_top_menu ? g_harness_top_menu() : top_menu(0);
            if (quit) {
                close_opening();
                break;
            }
        }
        lobby = 0;
        if (PbemActive) {
            GraphicWin_fill(&DesktopWin, 0);
            Win_show(&DesktopWin, 0);
        } else {
            GraphicWin_fill(&DesktopWin, 9);
            Win_hide(&DesktopWin);
        }
        if (desktop_init(ControlTurnMove == 0)) {
            close_opening();
            break;
        }
        GameNotStarted = 0;
        ui_main_mapwin_set_options(0x8C0);
        MapWinVehB = -1;
        if (MultiplayerActive)
            net_control_turn();
        else
            control_turn();
        desktop_close();
        if (!ControlTurnB) break;
    }
    game_close();
    system_close();
}
