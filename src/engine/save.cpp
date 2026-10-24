// Saving and loading (original module save, 0x5A66E0-0x5ABFF0).
#include "engine/save.h"

#include <cstddef>
#include <cstring>

#include "engine/clock.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/heap.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/path.h"
#include "engine/random.h"
#include "engine/setup.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/world.h"
#include "engine/zoc.h"

static_assert(offsetof(Faction, facility_announced) == 0x458, "");
static_assert(offsetof(Faction, clean_minerals_modifier) == 0x464, "");
static_assert(offsetof(Faction, secret_project_intel) == 0x205C, "");
static_assert(offsetof(Faction, corner_market_turn) == 0x207C, "");
static_assert(offsetof(BASE, facilities_built) == 0x8C, "");
static_assert(offsetof(BASE, mineral_surplus_final) == 0x98, "");

// ---- I/O primitives ----

int32_t GAMEAPI encrypt_write(const void* buf, uint32_t size, uint32_t count, CrtFile* file) {
    const uint8_t* src = (const uint8_t*)buf;
    uint32_t n = size * count;
    uint32_t padded = n;
    if (padded & 1) padded++;
    uint8_t* out = (uint8_t*)mem_get(padded);
    if (!out) return 0;
    uint8_t key1 = 0x80, key2 = 0x80;
    uint8_t* dst = out;
    for (uint32_t i = 0; i < padded >> 1; i++) {
        key1++;
        key2--;
        uint8_t first = *src++;
        // BUG(original): compares the pair index with the byte count, so an odd-sized buffer has
        // one byte past its end read as the padding byte (all callers use even sizes).
        uint8_t second = i < n ? *src++ : 0;
        if (first && (uint8_t)(first ^ key1)) first ^= key1;
        if (second && (uint8_t)(second ^ key2)) second ^= key2;
        *dst++ = second;
        *dst++ = first;
    }
    int32_t result = (int32_t)crt_fwrite(out, padded, 1, file);
    mem_free(out);
    return result;
}

int32_t GAMEAPI encrypt_read(void* buf, uint32_t size, uint32_t count, CrtFile* file) {
    uint8_t* dst = (uint8_t*)buf;
    uint32_t left = size * count;
    uint32_t padded = left;
    if (padded & 1) padded++;
    uint8_t* in = (uint8_t*)mem_get(padded);
    if (!in) return 0;
    if (!crt_fread(in, padded, 1, file)) {
        mem_free(in);
        return 0;
    }
    uint8_t key1 = 0x80, key2 = 0x80;
    const uint8_t* src = in;
    for (uint32_t pairs = padded >> 1; pairs; pairs--) {
        key1++;
        uint8_t first = src[1];
        key2--;
        uint8_t second = src[0];
        src += 2;
        if (first && (uint8_t)(first ^ key1)) first ^= key1;
        if (second && (uint8_t)(second ^ key2)) second ^= key2;
        *dst++ = first;
        if (left > 1) *dst++ = second;
        left -= 2;
    }
    mem_free(in);
    return 1;
}

// The transfer game_data, map_data and game_io select inline for each block: fread/fwrite or
// their encrypted versions, by SaveFileWrite and SaveFileEncrypt. Returns 0 on failure.
static uint32_t save_io(void* buf, uint32_t size, CrtFile* file) {
    if (SaveFileEncrypt) {
        return SaveFileWrite ? (uint32_t)encrypt_write(buf, size, 1, file) : (uint32_t)encrypt_read(buf, size, 1, file);
    }
    return SaveFileWrite ? crt_fwrite(buf, size, 1, file) : crt_fread(buf, size, 1, file);
}

int32_t GAMEAPI game_io(uint8_t* win, CrtFile* file) {
    static const uint32_t kFields[6] = {0x1DD78, 0x1DD74, 0x1DD70, 0x1DD98, 0x1DD9C, 0x1DDA0};
    for (uint32_t offset : kFields) {
        if (!save_io(&mapwin_field(win, offset), 4, file)) return 1;
    }
    return 0;
}

// ---- game_data helpers ----

// The TimeControl block (6 records of 0x20 bytes); reading keeps the current name pointers.
static uint32_t save_io_time_control(CrtFile* file, int32_t write) {
#ifdef SMACX_HYBRID
    char* names[6];
    if (!write) {
        for (int32_t i = 0; i < 6; i++) names[i] = TimeControl[i].name;
    }
    if (!save_io(TimeControl, 0xC0, file)) return 0;
    if (!write) {
        for (int32_t i = 0; i < 6; i++) TimeControl[i].name = names[i];
    }
    return 1;
#else
    // Native TimeControl holds host pointers: the block is built in the image layout, with the
    // name slots taken from the memory image (pointers of the original process).
    uint8_t* image = mem::host(0x94F1B8);
    uint8_t block[0xC0];
    std::memcpy(block, image, sizeof(block));
    for (int32_t i = 0; i < 6; i++) {
        std::memcpy(block + i * 0x20 + 4, &TimeControl[i].turn, 0x1C);
    }
    if (!save_io(block, 0xC0, file)) return 0;
    if (!write) {
        for (int32_t i = 0; i < 6; i++) {
            std::memcpy(&TimeControl[i].turn, block + i * 0x20 + 4, 0x1C);
            std::memcpy(image + i * 0x20 + 4, block + i * 0x20 + 4, 0x1C);
        }
    }
    return 1;
#endif
}

// Version 2 faction record (0x1CBC bytes: unit tables of 256 prototypes) to version 10 (0x20BC).
static void convert_faction_v2(LegacyFactionV10* dst, const LegacyFactionV2* src) {
    std::memcpy(dst, src, sizeof(*src));
    std::memset(dst->units_active, 0, sizeof(dst->units_active));
    std::memset(dst->units_queue, 0, sizeof(dst->units_queue));
    std::memset(dst->units_lost, 0, sizeof(dst->units_lost));
    for (int32_t k = 0; k < 8; k++) {
        for (int32_t j = 0; j < 32; j++) {
            dst->units_active[k * 64 + j] = src->units_active[k * 32 + j];
            dst->units_queue[k * 64 + j] = src->units_queue[k * 32 + j];
            dst->units_lost[k * 64 + j] = src->units_lost[k * 32 + j];
        }
    }
    // The original copies the remaining fields one by one; all keep their relative order.
    std::memcpy(&dst->total_combat_units, src->common_tail, sizeof(src->common_tail));
}

// Version 10 faction record (0x20BC bytes) to the current layout (0x20CC).
static void convert_faction_v10(Faction* faction, const uint8_t* src) {
    uint8_t* dst = (uint8_t*)faction;
    std::memcpy(dst, src, 0x20BC);
    // BUG(original): of the 8 old facility_announced bytes only 5 are kept and bytes 5-8 cleared;
    // bytes 9-11 keep what the first copy put there (the old clean_minerals_modifier's high bytes).
    std::memcpy(dst + 0x458, src + 0x458, 5);
    std::memset(dst + 0x45D, 0, 4);
    std::memcpy(dst + offsetof(Faction, clean_minerals_modifier), src + 0x460, 0x205C - 0x464);
    std::memcpy(faction->secret_project_intel, src + 0x2058, 5 * 4);
    for (int32_t i = 5; i < 8; i++) faction->secret_project_intel[i] = 0;
    std::memcpy(&faction->corner_market_turn, src + 0x206C, 0x20CC - 0x207C);
}

// Version 2 vehicle record (0x30 bytes, byte unit ids) to the current layout.
static void convert_veh_v2(VEH* veh, const LegacyVehV2* src) {
    veh->x = src->x;
    veh->y = src->y;
    veh->state = src->state;
    veh->flags = src->flags;
    veh->unit_id = src->unit_id;
    veh->faction_id = src->faction_id;
    veh->damage_taken = src->damage_taken;
    veh->order = src->order;
    veh->waypoint_count = src->waypoint_count;
    veh->patrol_current_point = src->patrol_current_point;
    veh->year_end_lurking = 0;
    veh->pad_0 = 0;
    for (int32_t i = 0; i < 4; i++) {
        veh->waypoint_x[i] = src->waypoint_x[i];
        veh->waypoint_y[i] = src->waypoint_y[i];
    }
    veh->morale = src->morale;
    veh->movement_turns = src->movement_turns;
    veh->order_auto_type = src->order_auto_type;
    veh->visibility = src->visibility;
    veh->moves_spent = src->moves_spent;
    veh->rotate_angle = src->rotate_angle;
    veh->iter_count = src->iter_count;
    veh->status_icon = src->status_icon;
    veh->probe_action = src->probe_action;
    veh->probe_sabotage_id = src->probe_sabotage_id;
    veh->home_base_id = src->home_base_id;
    veh->next_veh_id_stack = src->next_veh_id_stack;
    veh->prev_veh_id_stack = src->prev_veh_id_stack;
}

// Version 10 base record (0x130 bytes: 8 facility bytes) to the current layout (0x134).
static void convert_base_v10(BASE* base, const uint8_t* src) {
    uint8_t* dst = (uint8_t*)base;
    std::memcpy(dst, src, 0x130);
    // BUG(original): as for factions, 5 of the 8 facility bytes are kept, bytes 5-8 cleared and
    // bytes 9-11 keep the old mineral_surplus_final's high bytes; pad_5 is not copied either and
    // keeps the old talent_total's high half.
    std::memcpy(dst + 0x8C, src + 0x8C, 5);
    std::memset(dst + 0x91, 0, 4);
    std::memcpy(dst + 0x98, src + 0x94, 0x116 - 0x98);
    std::memcpy(dst + 0x118, src + 0x114, 0x134 - 0x118);
    for (int32_t i = 0; i < 10; i++) {
        if (base->queue_items[i] < -0x21) base->queue_items[i] -= 0x1F;  // facility ids moved up
    }
}

// Version 2 unit ids (32 prototypes per faction) to the current numbering (64 per faction).
static int32_t convert_unit_id_v2(int32_t id) { return id / 32 * 64 + id % 32; }

// PBEM message list: count, then id, length and text of each entry (plain fread/fwrite).
// Returns 1 on failure.
static int32_t save_io_stringlist(uint8_t* list, int32_t write, CrtFile* file) {
    int32_t count, id, len;
    if (!write) {
        ui_stringlist_clear(list);
        if (!crt_fread(&count, 4, 1, file)) return 1;
        for (int32_t i = 0; i < count; i++) {
            if (!crt_fread(&id, 4, 1, file)) return 1;
            if (!crt_fread(&len, 4, 1, file)) return 1;
            // BUG(original): fread of an empty text returns 0 and fails the load.
            if (!crt_fread(StrBuffer, (uint32_t)len, 1, file)) return 1;
            StrBuffer[len] = 0;
            ui_stringlist_add(list, id, StrBuffer);
        }
    } else {
        count = ui_stringlist_count(list);
        if (!crt_fwrite(&count, 4, 1, file)) return 1;
        for (int32_t i = 0; i < count; i++) {
            const char* text = ui_stringlist_entry(list, i, &id);
            if (!crt_fwrite(&id, 4, 1, file)) return 1;
            crt_strcpy(StrBuffer, text);
            len = (int32_t)crt_strlen(StrBuffer);
            if (!crt_fwrite(&len, 4, 1, file)) return 1;
            if (len && !crt_fwrite(StrBuffer, (uint32_t)len, 1, file)) return 1;
        }
    }
    return 0;
}

// ---- game_data ----

int32_t GAMEAPI game_data(CrtFile* file, int32_t write) {
    // The leading block is the preferences block of version 10 and older savegames. Newer
    // versions only use its version field; the original writes this block from an uninitialised
    // stack buffer (BUG(original): the file gets 0x31C bytes of stack contents, and a version
    // field below 11 there would even be copied into the preferences when saving). The
    // reconstruction writes zeros with SaveFileVersion = 12, which the original reads as a
    // current savegame.
    LegacyGamePrefs legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    legacy.SaveFileVersion = kSaveFileVersion;
    SaveFileWrite = write;
    SaveFileEncrypt = 0;
    SaveFileStatus = 0;
    if (!(write ? crt_fwrite(&legacy, sizeof(legacy), 1, file) : crt_fread(&legacy, sizeof(legacy), 1, file))) {
        return 1;
    }
    if (legacy.SaveFileVersion < 0xB) {
        std::memcpy(&cs, &legacy, sizeof(legacy));
        std::memcpy(SecretProjects, legacy.SecretProjects, sizeof(legacy.SecretProjects));
        for (int32_t i = 33; i < 64; i++) SecretProjects[i] = -1;
        GovernorFaction = legacy.GovernorFaction;
        for (int32_t i = 0; i < 11; i++) {
            ProposalPassCount[i] = legacy.ProposalPassCount[i];
            ProposalCallTurn[i] = legacy.ProposalCallTurn[i];
        }
        std::memcpy(TechOwners, legacy.TechOwners, sizeof(legacy.TechOwners));
        std::memcpy(LastSavePath, legacy.LastSavePath, sizeof(legacy.LastSavePath));
        ClimateValueA = legacy.ClimateValueA;
        ClimateValueB = legacy.ClimateValueB;
        ScoreBonusPts = legacy.ScoreBonusPts;
        ClimateValueC = legacy.ClimateValueC;
        ClimateFutureChange = legacy.ClimateFutureChange;
        std::memcpy(ScorePopTotal, legacy.ScorePopTotal, sizeof(legacy.ScorePopTotal));
        // SunspotDuration .. ClimateLevel (includes SaveFileVersion)
        std::memcpy(&SunspotDuration, &legacy.SunspotDuration, 10 * 4);
    } else if (!save_io(&cs, 0x398, file)) {
        return 1;
    }
    if (SaveFileVersion < 4) {
        SaveFileEncrypt = 0;
    } else {
        SaveFileEncrypt = GameState & 0x10000000;
        if (SaveFileEncrypt) {
            if (!save_io(&cs, 0x398, file)) return 1;
            if (!save_io(DiploPlrList, 0xCE0, file)) return 1;
            if (!save_io(&NetDiploSeq, 4, file)) return 1;
            if (!save_io_time_control(file, write)) return 1;
        }
    }
    if (!save_io(&CurrentPlayerFaction, 4, file)) return 1;

    uint8_t* factions10 = (uint8_t*)mem_get(sizeof(LegacyFactionV10) * 8);
    if (SaveFileVersion < 3) {
        uint8_t* factions2 = (uint8_t*)mem_get(sizeof(LegacyFactionV2) * 8);
        if (!save_io(factions2, sizeof(LegacyFactionV2) * 8, file)) return 1;
        for (int32_t i = 0; i < 8; i++) {
            convert_faction_v2((LegacyFactionV10*)(factions10 + i * sizeof(LegacyFactionV10)),
                               (const LegacyFactionV2*)(factions2 + i * sizeof(LegacyFactionV2)));
        }
        if (factions2) mem_free(factions2);
    }
    if (SaveFileVersion < 0xB) {
        if (SaveFileVersion > 2 && !save_io(factions10, sizeof(LegacyFactionV10) * 8, file)) return 1;
        for (int32_t i = 0; i < 8; i++) convert_faction_v10(&Factions[i], factions10 + i * sizeof(LegacyFactionV10));
    } else if (!save_io(Factions, 0x10660, file)) {
        return 1;
    }
    if (factions10) mem_free(factions10);
    if (!save_io(MFactions, 0x2CE0, file)) return 1;
    if (!save_io(FactionRankingsUnk, 0x20, file)) return 1;

    int32_t map_result;
    if (!SaveFileWrite) {
        if (!SaveFileMenu && !(GameState & 1)) {
            // Hotseat/PBEM: refuse a game that another human player has in progress.
            uint32_t humans = FactionStatus[1] & FactionStatus[0];
            if (bit_count(humans) > 1) {
                for (int32_t i = 1; i < 8; i++) {
                    uint8_t bit = (uint8_t)(1 << i);
                    if ((FactionStatus[1] & bit) && (FactionStatus[0] & bit) && Factions[i].unk_102) {
                        SaveFileStatus = 1;
                        return 1;
                    }
                }
            }
        }
        map_result = map_read(file);
    } else {
        map_result = map_write(file);
    }
    if (map_result) return 1;

    if (!save_io(FactionTurnMightBlock, sizeof(FactionTurnMightBlock), file)) return 1;
    if (!save_io(&ReplayEventSize, 4, file)) return 1;
    if (ReplayEventSize && !save_io(ReplayEvents, (uint32_t)ReplayEventSize, file)) return 1;
    if (!save_io(Continents, 0xE00, file)) return 1;
    if (SaveFileVersion < 3) {
        // 32 prototypes per faction: the second half of each faction's 64 slots is cleared.
        for (int32_t f = 0; f < 8; f++) {
            if (!save_io(&Units[f * 64], 32 * sizeof(UNIT), file)) return 1;
            for (int32_t i = f * 64 + 32; i < f * 64 + 64; i++) {
                Units[i].name[0] = 0;
                Units[i].unit_flags = 0;
                Units[i].group_id = 0;
            }
        }
    } else if (!save_io(Units, 0x6800, file)) {
        return 1;
    }
    if (SaveFileVersion < 6) {
        for (int32_t i = 0; i < 8; i++) clear_bunglist(i);
    } else if (!save_io(BungList, 0x1000, file)) {
        return 1;
    }
    int32_t version = SaveFileVersion;
    if (version < 0xB) {
        for (int32_t i = 0; i < 512; i++) {
            if (Units[i].weapon_id >= 0xC) Units[i].weapon_id += 3;  // three weapons inserted
        }
    }
    if (VehCount) {
        if (version < 3) {
            uint32_t size = (uint32_t)VehCount * sizeof(LegacyVehV2);
            LegacyVehV2* old = (LegacyVehV2*)mem_get(size);
            if (!save_io(old, size, file)) return 1;
            for (int32_t i = 0; i < VehCount; i++) convert_veh_v2(&Vehs[i], &old[i]);
            if (old) mem_free(old);
        } else if (!save_io(Vehs, (uint32_t)VehCount * sizeof(VEH), file)) {
            return 1;
        }
    }
    if (SaveFileVersion < 8) {
        Units[8].icon_offset = 3;
        Units[7].icon_offset = 2;
    }
    if (SaveFileVersion < 0xB) {
        if (BaseCount) {
            uint32_t size = (uint32_t)BaseCount * 0x130;
            uint8_t* old = (uint8_t*)mem_get(size);
            if (!save_io(old, size, file)) return 1;
            for (int32_t i = 0; i < BaseCount; i++) convert_base_v10(&Bases[i], old + i * 0x130);
            if (old) mem_free(old);
        }
    } else if (BaseCount && !save_io(Bases, (uint32_t)BaseCount * sizeof(BASE), file)) {
        return 1;
    }
    if (SaveFileVersion < 3) {
        for (int32_t i = 0; i < VehCount; i++) Vehs[i].unit_id = (int16_t)convert_unit_id_v2(Vehs[i].unit_id);
        for (int32_t i = 0; i < BaseCount; i++) {
            // BUG(original): items 0..queue_size (one more than the queue holds) are converted.
            int32_t* queue = &Bases[i].queue_size;
            for (int32_t k = 0; k <= queue[0]; k++) {
                if (queue[1 + k] >= 0) queue[1 + k] = convert_unit_id_v2(queue[1 + k]);
            }
        }
    }
    if ((GameState & 0x10) && !save_io(ScenarioFileName, 0x100, file)) return 1;
    if (SaveFileVersion < 1) {
        crt_strcpy(GameAltPath, EmptyPathA);
    } else if (!save_io(GameAltPath, 0x100, file)) {
        return 1;
    }
    if (SaveFileVersion < 2) {
        clear_scenario();
    } else if (!save_io(&ObjectiveReqVictory, 0x98, file)) {
        return 1;
    }
    if (SaveFileVersion < 7) {
        clear_monuments();
    } else if (!save_io(Monuments, 0x27A0, file)) {
        return 1;
    }
    if (SaveFileVersion < 0xC) {
        for (int32_t i = 0; i < 8; i++) TectonicDetonationCount[i] = 0;
    } else if (!save_io(TectonicDetonationCount, 0x20, file)) {
        return 1;
    }
    // Main view state.
    if (!save_io(&MapWinInVehMode, 4, file)) return 1;
    if (!save_io(&MapWinVehA, 4, file)) return 1;
    if (!save_io(&MapWinCursorX[MapWinCursorIndex], 4, file)) return 1;
    if (!save_io(&MapWinCursorY[MapWinCursorIndex], 4, file)) return 1;
    if (SaveFileWrite) MapWinField23D20 = 0;
    if (!save_io(&MapWinField23D20, 4, file)) return 1;
    for (int32_t i = 0; i < 8; i++) {
        if (game_io(MapWins[i], file)) return 1;
    }
    if (game_io(&WorldMapWin, file)) return 1;

    if (GameMoreRules & 0x20) {  // PBEM
        if (!save_io(DiploStateC, 0x2400, file)) return 1;
        if (!save_io(DiploStateB, 0x100, file)) return 1;
        if (!save_io(FactionCombatWin, 0x20, file)) return 1;
        if (!save_io(FactionCombatLoss, 0x20, file)) return 1;
        for (int32_t i = 1; i < 8; i++) {
            if (save_io_stringlist(DiploTextLists[i], write, file)) return 1;
        }
        for (int32_t j = 1; j < 8; j++) {
            for (int32_t i = 1; i < j; i++) {
                if (save_io_stringlist(DiploMessageLists[i][j], write, file)) return 1;
            }
        }
        for (int32_t i = 0; i < 4; i++) {
            if (!save_io(&CouncilWinState[i], 4, file)) return 1;
        }
        if (!save_io(CouncilWinVotesA, 0x20, file)) return 1;
        if (!save_io(CouncilWinVotesB, 0x20, file)) return 1;
        if (!save_io(&CouncilSessionPending, 4, file)) return 1;
        if (!save_io(CouncilProposal, 0x20, file)) return 1;
        if (!save_io(CouncilVoteState, 0x20, file)) return 1;
        if (!save_io(&dword_93A9AC, 4, file)) return 1;
        if (!save_io(&dword_93A9B0, 4, file)) return 1;
        if (!save_io(&dword_93A9B4, 4, file)) return 1;
        if (!save_io(&dword_93A9B8, 0x20, file)) return 1;
        if (!save_io(&dword_93A9D8, 0x20, file)) return 1;
    }
    if (!write) {
        int32_t width, height;
        if (!crt_fread(&width, 4, 1, file)) return 1;
        if (!crt_fread(&height, 4, 1, file)) return 1;
        if (width != CanvasWidth || height != CanvasHeight) {
            for (int32_t i = 1; i < 8; i++) {
                if (MapWins[i]) mapwin_field(MapWins[i], kMapWinActive) = 0;
            }
        }
    } else {
        if (!crt_fwrite(&CanvasWidth, 4, 1, file)) return 1;
        if (!crt_fwrite(&CanvasHeight, 4, 1, file)) return 1;
    }
    return 0;
}

int32_t GAMEAPI map_data(CrtFile* file, int32_t write, int32_t is_save) {
    // BUG(original): for savegames the blocks are read with the layout of an old format that had
    // no leading legacy preferences block, so a current savegame's map is read from the wrong
    // offset. When writing (never done with is_save) the buffers would be uninitialised.
    static uint8_t prefs[0x398], factions[0x10660], mfactions[0x2CE0], rankings[0x20];
    SaveFileWrite = write;
    SaveFileEncrypt = 0;
    if (is_save) {
        if (!(write ? crt_fwrite(prefs, sizeof(prefs), 1, file) : crt_fread(prefs, sizeof(prefs), 1, file))) return 1;
        if (!save_io(&write, 4, file)) return 1;  // the player faction, read over the argument
        int32_t version = reinterpret_cast<GamePrefs*>(prefs)->SaveFileVersion;
        if (!save_io(factions, version < 3 ? 0xE5E0 : 0x10660, file)) return 1;
        if (!save_io(mfactions, sizeof(mfactions), file)) return 1;
        if (!save_io(rankings, sizeof(rankings), file)) return 1;
    }
    int32_t result = SaveFileWrite ? map_write(file) : map_read(file);
    return result != 0;
}

// ---- savegames ----

int32_t GAMEAPI save_daemon(const char* filename) {
    char path[1024];  // 256 in the original (longer names overflow it)
    if (PbemActive) dword_93A9B4++;
    uint32_t stamp = engine_logic_time();
    if ((GameRules & 0x400) && !MultiplayerActive) game_srand(stamp);
    SaveFileVersion = kSaveFileVersion;
    if (GameRules & 0x400) {  // iron man: encrypted
        NetDiploSeq = 0;
        GameState |= 0x10000000;
    }
    if (PbemActive) {
        GameMoreRules |= 0x20;
        GameState |= 0x10000000;
        NetDiploSeq = 0;
    }
    if (MultiplayerActive) {
        GameState |= 0x10000000;
        NetDiploSeq = NetPlayerListSeq;
        std::memcpy(DiploPlrList, NetPlayerList, 0xCE0);
    }
    path[0] = 0;
    crt_strcat(path, filename);
    if (!crt_strchr(path, '.')) {
        crt_strcat(path, ".");
        crt_strcat(path, "SAV");
    }
    CrtFile* file = env_open(path, "wb");
    if (!file) return 1;
    int32_t result = 2;
    header_write("TERRAN", file);
    int32_t magic = 0x56;
    if (crt_fwrite(&magic, 4, 1, file)) {
        result = 3;
        if (!game_data(file, 1) && crt_fwrite(&stamp, 4, 1, file)) result = 0;
    }
    crt_fclose(file);
    if (result) crt_remove(path);
    return result;
}

void GAMEAPI see_map_check() {
    for (int32_t i = 1; i < 8; i++) {
        Faction& faction = Factions[i];
        faction.player_flags &= ~0x200u;  // PFLAG_MAP_REVEALED
        const int32_t* satellites = &faction.satellites_nutrient;
        for (int32_t k = 0; k < 4; k++) {
            if (satellites[k]) faction.player_flags |= 0x200;
        }
        for (int32_t t = 0; t < 89; t++) {
            if ((Tech[t].flags & 8) && has_tech(t, i)) faction.player_flags |= 0x200;  // TFLAG_REVEALS_MAP
        }
    }
}

int32_t GAMEAPI load_daemon(const char* filename, int32_t relinearize) {
    CrtFile* file = env_open(filename, "rb");
    if (!file) return 1;
    int32_t result = 2;
    char header[256];
    header_check(header, file);
    if (!crt_strcmp(header, "TERRAN")) {
        result = 3;
        int32_t magic;
        if (crt_fread(&magic, 4, 1, file) && magic >= 0x56) {
            for (int32_t i = 2; i < 8; i++) {
                if (MapWins[i] && mapwin_field(MapWins[i], kMapWinActive)) {
                    mapwin_field(MapWins[i], kMapWinActive) = 0;
                    MapWin_close(MapWins[i]);
                }
            }
            MapWinField23BE4 = 0;
            MapWinField23BE8 = 0;
            result = 4;
            if (!game_data(file, 0)) {
                uint32_t seed;
                if ((GameRules & 0x400) && crt_fread(&seed, 4, 1, file)) game_srand(seed);
                if (!MultiplayerActive) {
                    for (int32_t i = 2; i < 8; i++) {
                        if (MapWins[i]) mapwin_field(MapWins[i], kMapWinActive) = 0;
                    }
                }
                ComputeBaseID = -1;
                result = 0;
                Path_init(&Paths);
                if (relinearize) world_linearize_contours();
                std::memcpy(AltNatural, AltNaturalLinear, sizeof(AltNatural));
                see_map_check();
                // Alive factions: those with a base or a colony pod.
                for (int32_t f = 1; f < 8; f++) {
                    bool alive = false;
                    for (int32_t i = 0; i < BaseCount; i++) {
                        if ((uint8_t)Bases[i].faction_id == f) {
                            alive = true;
                            break;
                        }
                    }
                    if (!alive) {
                        for (int32_t i = 0; i < VehCount; i++) {
                            if (Vehs[i].faction_id == f && Units[Vehs[i].unit_id].plan == 8) alive = true;
                        }
                    }
                    if (alive) {
                        FactionStatus[1] |= (uint8_t)(1 << f);
                    } else {
                        FactionStatus[1] &= (uint8_t)~(1 << f);
                    }
                }
                if (GameRules & 0x400) crt_remove(filename);  // iron man: one load per save
                for (int32_t i = 0; i < 32; i++) {
                    MapWinCursorX[i] = MapWinCursorX[MapWinCursorIndex];
                    MapWinCursorY[i] = MapWinCursorY[MapWinCursorIndex];
                }
                rebuild_vehicle_bits();
                rebuild_base_bits();
                if (crt_strstr(filename, "multi") && !MultiplayerActive) {
                    MapWinInVehMode = 0;
                    GameState |= 0x10C0;
                    ui_editor_panel_show((GameState & 0x80) && (GameState & 0x40));
                }
                ControlTurnMove = CurrentPlayerFaction;
                if (!MultiplayerActive) {
                    if (!(GameMoreRules & 0x20) && bit_count(FactionStatus[0]) < 2) {
                        PbemActive = 0;
                    } else {
                        PbemActive = 1;
                        check_tamper();
                    }
                } else {
                    PbemActive = 0;
                }
                GameDrawState |= 4;
            } else if (SaveFileStatus) {
                crt_fclose(file);
                return 5;
            }
        }
    }
    crt_fclose(file);
    return result;
}

int32_t GAMEAPI save_map_daemon(const char* filename) {
    char path[1024];  // 256 in the original
    int32_t result = 1;
    path[0] = 0;
    crt_strcat(path, filename);
    if (!crt_strchr(path, '.')) {
        crt_strcat(path, ".");
        crt_strcat(path, "MP");
    }
    CrtFile* file = env_open(path, "wb");
    if (file) {
        result = 2;
        header_write("TERRANMAP", file);
        int32_t magic = 5;
        if (crt_fwrite(&magic, 4, 1, file)) {
            result = 3;
            if (!map_data(file, 1, 0)) result = 0;
        }
    }
    crt_fclose(file);  // BUG(original): also called when the file could not be created
    if (result) crt_remove(path);
    return result;
}

int32_t GAMEAPI load_map_daemon(const char* filename) {
    int32_t is_save = 0;
    const char* dot = crt_strchr(filename, '.');
    if (dot && !crt_stricmp(dot + 1, "SAV")) is_save = 1;
    CrtFile* file = env_open(filename, "rb");
    if (!file) return 1;
    int32_t result = 2;
    char header[256];
    header_check(header, file);
    if (!crt_strcmp(header, is_save ? "TERRAN" : "TERRANMAP")) {
        result = 3;
        int32_t magic;
        if (crt_fread(&magic, 4, 1, file) && magic >= (is_save ? 0x56 : 5)) {
            for (int32_t i = 1; i < 8; i++) {
                if (MapWins[i] && mapwin_field(MapWins[i], kMapWinActive)) {
                    mapwin_field(MapWins[i], kMapWinActive) = 0;
                    MapWin_close(MapWins[i]);
                }
            }
            MapWinField23BE4 = 0;
            result = 4;
            if (!map_data(file, 0, is_save)) {
                BaseCount = 0;
                VehCount = 0;
                MapWinInVehMode = 0;
                MapWinVehA = -1;
                MAP* sq = MapTiles;
                for (int32_t i = 0; i < MapAreaTiles; i++, sq++) {
                    sq->visibility = 0;
                    sq->items &= ~3u;  // no bases or vehicles
                }
                ComputeBaseID = -1;
                std::memcpy(AltNatural, AltNaturalLinear, sizeof(AltNatural));
                result = 0;
                Path_init(&Paths);
            }
        }
    }
    crt_fclose(file);
    return result;
}

void GAMEAPI yearmotize(char* name) {
    char* p = name;
    while (*p) {
        char* next = p + 1;
        if (crt_strlen(p) < 4) return;
        if (crt_isdigit((int8_t)p[0]) && crt_isdigit((int8_t)p[1]) && crt_isdigit((int8_t)p[2]) &&
            crt_isdigit((int8_t)p[3]) && !crt_isdigit((int8_t)p[4]) && (p == name || !crt_isdigit((int8_t)p[-1]))) {
            char digits[80];
            char year[80] = {};
            year[0] = 0;
            crt_itoa(game_year(CurrentTurn), digits, 10);
            crt_strcat(year, digits);
            p[0] = year[0];
            p[1] = year[1];
            p[2] = year[2];
            p[3] = year[3];
        }
        p = next;
    }
}

void GAMEAPI kill_auto_save() { crt_remove("saves\\auto\\Alpha Centauri Autosave 1.SAV"); }

void GAMEAPI auto_save() {
    if (PbemActive && !MultiplayerActive) return;
    if ((GameRules & 0x400) && !(GameState & 0x40)) {
        crt_remove("saves\\auto\\Alpha Centauri Autosave 30.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 20.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 10.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 5.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 4.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 3.SAV");
        crt_remove("saves\\auto\\Alpha Centauri Autosave 2.SAV");
        save_daemon("saves\\auto\\Alpha Centauri Autosave 1");
        return;
    }
    if (CurrentTurn % 10 == 0) {
        crt_remove("saves\\auto\\Alpha Centauri Autosave 30.SAV");
        crt_rename("saves\\auto\\Alpha Centauri Autosave 20.SAV", "saves\\auto\\Alpha Centauri Autosave 30.SAV");
        crt_rename("saves\\auto\\Alpha Centauri Autosave 10.SAV", "saves\\auto\\Alpha Centauri Autosave 20.SAV");
        save_daemon("saves\\auto\\Alpha Centauri Autosave 10");
    }
    crt_remove("saves\\auto\\Alpha Centauri Autosave 5.SAV");
    crt_rename("saves\\auto\\Alpha Centauri Autosave 4.SAV", "saves\\auto\\Alpha Centauri Autosave 5.SAV");
    crt_rename("saves\\auto\\Alpha Centauri Autosave 3.SAV", "saves\\auto\\Alpha Centauri Autosave 4.SAV");
    crt_rename("saves\\auto\\Alpha Centauri Autosave 2.SAV", "saves\\auto\\Alpha Centauri Autosave 3.SAV");
    crt_rename("saves\\auto\\Alpha Centauri Autosave 1.SAV", "saves\\auto\\Alpha Centauri Autosave 2.SAV");
    save_daemon("saves\\auto\\Alpha Centauri Autosave 1");
}

// The undo files are named without extension, while save_daemon appends ".SAV": loading,
// deleting and rotating them only works on files of that exact name (BUG(original)).
static const char kUndoName[] = "saves\\auto\\Scenario Editor Undo ";

void GAMEAPI load_undo(int32_t direction) {
    char digits[80];
    if (direction < 0 && UndoIndex > 1) UndoIndex--;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, kUndoName);
    crt_itoa(UndoIndex, digits, 10);
    crt_strcat(StrBuffer, digits);
    if (direction > 0 && UndoIndex < 9) UndoIndex++;
    load_daemon(StrBuffer, 0);
}

void GAMEAPI wipe_undo() {
    char digits[80];
    for (int32_t i = 9; i > 0; i--) {
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, kUndoName);
        crt_itoa(i, digits, 10);
        crt_strcat(StrBuffer, digits);
        crt_remove(StrBuffer);
    }
}

void GAMEAPI auto_undo() {
    if (!(GamePreferences & 2)) return;
    char prev[256], name[256], digits[80];
    UndoIndex = 1;
    prev[0] = 0;
    for (int32_t i = 9; i > 0; i--) {
        name[0] = 0;
        crt_strcat(name, kUndoName);
        crt_itoa(i, digits, 10);
        crt_strcat(name, digits);
        if (!crt_strlen(prev)) {
            crt_remove(name);
        } else {
            // BUG(original): renames the previous (higher, already moved) slot onto this one, so
            // the slots do not rotate.
            crt_rename(prev, name);
        }
        crt_strcpy(prev, name);
    }
    save_daemon(prev);
}

void GAMEAPI clear_monuments() {
    for (int32_t i = 0; i < 8; i++) {
        for (int32_t k = 0; k < 21; k++) Monuments[i].events[k].achieved = 0;
        Monuments[i].latest_event_id = -1;
    }
}

// ---- file dialogs (native builds; hybrid builds call the original UI functions) ----
#ifndef SMACX_HYBRID
#include "engine/game.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/text.h"

namespace {
// The original's stack objects: the Windows file dialog and the in-game file box.
struct FileDialogs {
    uint8_t dialog[kFileDialogSize];
    uint8_t box[kFileBoxSize];
    uint8_t* list() { return box + kFileBoxList; }
    void destroy() {
        ui_filebox_destroy(box);
        FileDialog_dtor(dialog);
    }
};

// File name part of a path (after the last '\\' or ':').
char* path_file_name(char* path) {
    char* p = crt_strrchr(path, '\\');
    if (!p) p = crt_strrchr(path, ':');
    return p ? p + 1 : path;
}
}  // namespace

int32_t GAMEAPI save_game(int32_t scenario) {
    static FileDialogs d;  // 0x6880 bytes of stack in the original
    FileDialog_ctor(d.dialog);
    FileBox_ctor(d.box);
    if (Win_is_visible(ui_console_vbase()) && !GameNotStarted) FileBox_init(d.box);
    // Default name: "<dir><leader> <label 180> <faction>, <year>"
    const MFaction& m = MFactions[CurrentPlayerFaction];
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, scenario ? "scenarios\\" : "saves\\");
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    crt_strcat(StrBuffer, m.name_leader);
    crt_strcat(StrBuffer, " ");
    crt_strcat(StrBuffer, label_get(180));
    crt_strcat(StrBuffer, " ");
    PluralDefault = m.is_noun_plural;
    GenderDefault = m.noun_gender;
    crt_strcat(StrBuffer, m.noun_faction);
    crt_strcat(StrBuffer, ", ");
    say_year(StrBuffer);
    char name[256];
    crt_strcpy(name, scenario ? ScenarioFileName : LastSavePath);
    yearmotize(name);
    const char* initial = crt_strlen(name) ? name : StrBuffer;
    FileDialog_set_name(d.dialog, initial);
    FileList_set_path(d.list(), initial);
    const char* ext = scenario ? "SC" : "SAV";
    FileDialog_set_ext(d.dialog, ext);
    name[0] = 0;
    crt_strcat(name, "*.");
    crt_strcat(name, ext);
    FileDialog_add_filter(d.dialog, name, label_get(scenario ? 626 : 413));
    name[0] = 0;
    crt_strcat(name, ".");
    crt_strcat(name, ext);
    FileList_add_ext(d.list(), name);
    int32_t windows_box = prefs_get_2("WindowsFileBox", 0, 0);
    prefs_put_2("WindowsFileBox", (uint32_t)windows_box, 0);
    char* chosen;
    if (Win_is_visible(ui_console_vbase()) && !GameNotStarted && !windows_box) {
        chosen = FileBox_save(d.box);
    } else {
        chosen = FileDialog_save(d.dialog);
    }
    do_all_draws();
    if (!chosen) {
        d.destroy();
        return 1;
    }
    char path[256], base[256];
    crt_strcpy(path, chosen);
    if (!crt_strcmp(path, "?")) crt_strcpy(path, StrBuffer);
    crt_strcpy(base, path);
    if (char* dot = crt_strchr(base, '.')) *dot = 0;
    // A name left at its default is not remembered.
    bool renamed = crt_stricmp(path_file_name(base), path_file_name(StrBuffer)) != 0;
    if (scenario) {
        crt_strcpy(ScenarioFileName, renamed ? base : EmptyPathB);
        GameState |= 0x10;
        for (int32_t i = 0; i < 8; i++) Factions[i].unk_101 = 0;
    } else {
        crt_strcpy(LastSavePath, renamed ? base : EmptyPathC);
        prefs_put("Latest Save", base);
    }
    if (save_daemon(path)) {
        parse_says(0, path, -1, -1);
        X_pop("SAVEFAILED", 0);
        do_all_draws();
        d.destroy();
        return 1;
    }
    if (PbemActive) {
        ControlTurnA = 1;
        d.destroy();
        return 1;
    }
    if (!scenario && (GameRules & 0x400)) {  // iron man: saving ends the session
        auto_save();
        ControlTurnA = 1;
        if (MultiplayerActive) {
            NetDaemon_hang_up(&NetState);
            static const uint8_t kCleared[] = {0,  1,  2,  3,  12, 11, 15, 14, 19, 30, 28, 29, 39, 38,
                                               8,  9,  10, 37, 36, 32, 31, 33, 4,  5,  42, 41, 6,  7};
            for (uint8_t i : kCleared) NetSessionState[i] = 0;
            NetSessionState[40] = 0xFF;
            NetSessionState[18] = 0;
            for (int32_t i = 20; i < 28; i++) NetSessionState[i] = 0;
            Lock_clear(&NetLock);
            AlphaNet_close(&NetState);
            MultiplayerActive = 0;
        }
        X_pop("IRONMAN", 0);
        do_all_draws();
    }
    d.destroy();
    return 0;
}

int32_t GAMEAPI load_game(int32_t scenario, int32_t relinearize) {
    static FileDialogs d;
    char pattern[256], filter[256], path[256], base[336], old_alt[256], digits[80];
    FileDialog_ctor(d.dialog);
    FileBox_ctor(d.box);
    for (;;) {
        FileBox_init(d.box);
        pattern[0] = 0;
        filter[0] = 0;
        crt_strcat(pattern, scenario ? "scenarios\\*." : "saves\\*.");
        crt_strcat(filter, "*.");
        const char* ext = scenario ? "SC" : "SAV";
        crt_strcat(pattern, ext);
        crt_strcat(filter, ext);
        prefs_get(scenario ? "Latest Scenario" : "Latest Save", pattern, 0);
        crt_strncpy(path, TextBufferGetPtr, 0xFF);  // prefs_get's result
        if (!crt_strlen(path)) crt_strcpy(path, pattern);
        FileDialog_set_name(d.dialog, path);
        FileDialog_set_ext(d.dialog, ext);
        FileDialog_add_filter(d.dialog, filter, label_get(scenario ? 626 : 413));
        pattern[0] = 0;
        crt_strcat(pattern, ".");
        crt_strcat(pattern, ext);
        FileList_set_path(d.list(), path);
        FileList_add_ext(d.list(), pattern);
        int32_t windows_box = prefs_get_2("WindowsFileBox", 0, 0);
        prefs_put_2("WindowsFileBox", (uint32_t)windows_box, 0);
        char* chosen = windows_box ? FileDialog_open(d.dialog) : FileBox_load(d.box);
        do_all_draws();
        if (!chosen) {
            d.destroy();
            return 1;
        }
        crt_strcpy(path, chosen);
        crt_strcpy(base, chosen);
        if (char* dot = crt_strchr(base, '.')) *dot = 0;
        if (!crt_strstr(base, "auto\\")) prefs_put(scenario ? "Latest Scenario" : "Latest Save", base);
        crt_strcpy(old_alt, GameAltPath);
        int32_t result = load_daemon(path, relinearize);
        if (MultiplayerActive && !result) {
            NetGameFileName[0] = 0;
            crt_strcat(NetGameFileName, base);
            NetGameFileKind = (scenario != 0) + 2;
        }
        if (scenario) {
            for (int32_t i = 1; i < 8; i++) load_faction_art(i);
            load_flags();
            GameState &= (int32_t)0xF79FCF16;
            ui_editor_panel_show((GameState & 0x80) && (GameState & 0x40));
            // The scenario's directory becomes the alternate data directory.
            crt_strcpy(GameAltPath, base);
            if (char* slash = crt_strrchr(GameAltPath, '\\')) {
                slash[1] = 0;
            } else if (crt_strlen(GameAltPath)) {
                crt_strcat(GameAltPath, "\\");
            }
            prefs_use();
        }
        GameAltPathChanged = crt_strcmp(GameAltPath, old_alt);
        filefind_set_alternate(GameAltPath);
        scenario |= GameState & 0x10;
        if (!MultiplayerActive && !PbemActive) {
            int32_t i = 1;
            while (i < 8 && !Factions[i].unk_102) i++;
            if (i < 8) {  // a hotseat/PBEM game with a password
                X_pop_2("SCRIPT", "PASSWORDEDLOADATTEMPT", 0);
                if (!GameNotStarted) ControlTurnA = 1;
                d.destroy();
                return 5;
            }
        }
        if (result) {
            parse_says(0, path, -1, -1);
            parse_num(0, result);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "LOAD");
            static const char* const kErrors[5] = {"NONE", "NOT", "OLD", "ERROR", "SECURITY"};
            if (result >= 1 && result <= 5) crt_strcat(StrBuffer, kErrors[result - 1]);
            X_pop(StrBuffer, 0);
            do_all_draws();
        } else if (!scenario) {
            for (int32_t i = 1; i < 8; i++) load_faction_art(i);
            load_flags();
            const MFaction& m = MFactions[CurrentPlayerFaction];
            parse_say(0, Difficulty[DiffLevel], -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(1, m.title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(2, m.name_leader, -1, -1);
            GenderDefault = m.noun_gender;
            PluralDefault = m.is_noun_plural;
            parse_says(3, m.noun_faction, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, label_get(0));
            crt_strcat(StrBuffer, " ");
            say_year(StrBuffer);
            parse_says(4, StrBuffer, -1, -1);
            int32_t choice = X_pops_11("LOADGAME", 0x40, FactionPortraits[CurrentPlayerFaction], 0);
            do_all_draws();
            if (choice < 0) {  // not this one: back to the file box
                FileBox_close(d.box);
                FileList_close(d.list());
                scenario = 0;
                continue;
            }
            for (int32_t i = 9; i > 0; i--) {  // wipe_undo, inline
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, kUndoName);
                crt_itoa(i, digits, 10);
                crt_strcat(StrBuffer, digits);
                crt_remove(StrBuffer);
            }
        }
        if (GamePreferences & 0x20000000) {
            GameDialogFlags &= ~4;
        } else {
            GameDialogFlags |= 4;
        }
        d.destroy();
        return result;
    }
}

int32_t GAMEAPI load_map() {
    static FileDialogs d;
    char ext[256], pattern[256], path[256], base[256];
    FileDialog_ctor(d.dialog);
    FileBox_ctor(d.box);
    FileBox_init(d.box);
    ext[0] = 0;
    crt_strcat(ext, ".");
    crt_strcat(ext, "MP");
    FileList_add_ext(d.list(), ext);
    ext[0] = 0;
    crt_strcat(ext, ".");
    crt_strcat(ext, "SAV");
    FileList_add_ext(d.list(), ext);
    pattern[0] = 0;
    crt_strcat(pattern, "maps\\*.");
    crt_strcat(pattern, "MP");
    prefs_get("Latest Map", pattern, 0);
    crt_strncpy(path, TextBufferGetPtr, 0xFF);  // prefs_get's result
    if (!crt_strlen(path)) crt_strcpy(path, pattern);
    FileList_set_path(d.list(), path);
    char* chosen = FileBox_load(d.box);
    do_all_draws();
    if (!chosen) {
        d.destroy();
        return 1;
    }
    crt_strcpy(path, chosen);
    crt_strcpy(base, chosen);
    if (char* dot = crt_strchr(base, '.')) *dot = 0;
    prefs_put("Latest Map", base);
    int32_t result = load_map_daemon(path);
    if (MultiplayerActive && !result) {
        NetGameFileName[0] = 0;
        crt_strcat(NetGameFileName, base);
        NetGameFileKind = 1;
    } else if (result) {
        parse_says(0, path, -1, -1);
        parse_num(0, result);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "LOAD");
        crt_strcat(StrBuffer, result == 1 ? "NONE" : result == 2 ? "NOT" : result == 3 ? "OLD" : "ERROR");
        X_pop(StrBuffer, 0);
        do_all_draws();
    }
    d.destroy();
    return result;
}

void GAMEAPI save_map() {
    static uint8_t dialog[kFileDialogSize];
    char filter[256], path[256], base[256];
    FileDialog_ctor(dialog);
    FileDialog_set_name(dialog, MapFilePath);
    FileDialog_set_ext(dialog, "MP");
    filter[0] = 0;
    crt_strcat(filter, "*.");
    crt_strcat(filter, "MP");
    FileDialog_add_filter(dialog, filter, label_get(414));
    char* chosen = FileDialog_save(dialog);
    do_all_draws();
    if (chosen) {
        crt_strcpy(path, chosen);
        if (!crt_strcmp(path, "?")) crt_strcpy(path, StrBuffer);
        crt_strcpy(base, path);
        if (char* dot = crt_strchr(base, '.')) *dot = 0;
        crt_strncpy(MapFilePath, base, 0x4B);
        prefs_put("Latest Map", base);
        save_map_daemon(path);
    }
    FileDialog_dtor(dialog);
}
#endif
