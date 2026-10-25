#include "engine/net_misc.h"

#include <cstring>

#include "engine/crt.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"

#include "engine/strings.h"
#include "platform/win32.h"

void GAMEAPI check_tamper() {
    if (!PbemActive || !TamperKeyA) return;
    read_tamper();
    int32_t key_b = TamperKeyB;
    int32_t turn = CurrentTurn;
    int32_t player = CurrentPlayerFaction;
    for (int32_t i = 0; i < 50; i++) {
        int32_t* rec = TamperRecords[i];
        if (rec[0] == TamperKeyA && rec[1] == TamperKeyB && rec[2] == CurrentTurn && rec[3] == CurrentPlayerFaction &&
            rec[5] == TamperKeyC) {
            int32_t count = rec[4];
            rec[4] = count + 1;
            for (int32_t f = 1; f < 8; f++) {
                uint8_t bit = (uint8_t)(1 << (f & 31));
                if (f != player && (FactionStatus[0] & bit) && (FactionStatus[1] & bit)) {
                    TamperFaction[f] = player;
                    TamperCount[f] = count + 1;
                }
            }
            set_tamper();
            X_pop("HOTNOTAMPER", 0);
            return;
        }
    }
    int32_t* rec = TamperRecords[TamperIndex];
    rec[0] = TamperKeyA;
    rec[1] = key_b;
    rec[2] = turn;
    rec[3] = CurrentPlayerFaction;
    rec[4] = 0;
    rec[5] = TamperKeyC;
    TamperIndex = (TamperIndex + 1) % 50;
    set_tamper();
}

void GAMEAPI clear_tamper() {
    for (int32_t i = 0; i < 8; i++) TamperCount[i] = 0;
    for (int32_t i = 0; i < 8; i++) TamperFaction[i] = 0;
    TamperKeyA = 0;
    TamperKeyB = 0;
    TamperKeyC = 0;
}

void GAMEAPI say_checksum(const char* label, int32_t value) {
    char line[256];
    char num[80];
    line[0] = 0;
    crt_itoa(value, num, 10);
    std::strcat(line, num);
    std::strcat(line, " ");
    std::strcat(line, label);
    log_say_hex_2(line, value, 0, 0);
}

void GAMEAPI do_checksums(int32_t stage) {
    if (!MultiDebugActive && stage != 4) return;
    FullChecksum sums;
    NetDaemon_game_checksum(&NetState, &sums);
    log_say_2(" ", 0, 0, 0);
    switch (stage) {
    case 0: log_say_2("Checksums at end of turn", CurrentTurn, 0, 0); break;
    case 1: log_say_2("Checksums at game start", CurrentTurn, 0, 0); break;
    case 2: log_say_2("Checksums before world regen", CurrentTurn, 0, 0); break;
    case 3: log_say_2("Checksums after world regen", CurrentTurn, 0, 0); break;
    case 4:
        log_say_2("Checksums at beginning of turn", CurrentTurn, 0, 0);
        MasterChecksum = static_cast<GameChecksum&>(sums);
        break;
    default: break;
    }
    char num[80];
    say_checksum("GAME checksum = ", (int32_t)sums.game);
    say_checksum("LEADERS checksum = ", (int32_t)sums.leaders);
    say_checksum("MAP checksum = ", (int32_t)sums.map_all);
    say_checksum("VEHICLES checksum = ", (int32_t)sums.vehs);
    say_checksum("BASES checksum = ", (int32_t)sums.bases);
    log_say_2(" ", 0, 0, 0);
    for (int32_t i = 0; i < 16; i++) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, "Map SECTION ");
        crt_itoa(i, num, 10);
        std::strcat(StrBuffer, num);
        std::strcat(StrBuffer, " = ");
        say_checksum(StrBuffer, (int32_t)sums.map_section[i]);
    }
    say_checksum("Rules checksum = ", (int32_t)sums.rules);
    say_checksum("Projects checksum = ", (int32_t)sums.projects);
    say_checksum("Tech checksum = ", (int32_t)sums.tech);
    for (int32_t i = 1; i < 8; i++) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, MFactions[i].adj_name_faction);
        std::strcat(StrBuffer, " checksum = ");
        say_checksum(StrBuffer, (int32_t)sums.faction[i - 1]);
    }
    say_checksum("Map climate checksum = ", (int32_t)sums.map_climate);
    say_checksum("Map region checksum = ", (int32_t)sums.map_region);
    say_checksum("Map bit checksum = ", (int32_t)sums.map_bit);
    say_checksum("Map contour checksum = ", (int32_t)sums.map_contour);
    say_checksum("Map terr checksum = ", (int32_t)sums.map_owner);
    say_checksum("Map bitwho[0] checksum = ", (int32_t)sums.map_bitwho);
    say_checksum("Params checksum = ", (int32_t)sums.params);
    for (int32_t i = 0; i < 8; i++) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, MFactions[i].adj_name_faction);
        std::strcat(StrBuffer, " Vehicles checksum = ");
        say_checksum(StrBuffer, (int32_t)sums.faction_vehs[i]);
    }
    say_checksum("Bases checksum = ", (int32_t)sums.bases2);
}

// ---- diplomacy module range 0x538F30-0x539160 ----

void GAMEAPI danger(const char* label, const char* text, int32_t v1, int32_t v2, int32_t v3) {
    parse_says(0, label, -1, -1);
    parse_says(1, text, -1, -1);
    parse_num(0, v1);
    parse_num(1, v2);
    parse_num(2, v3);
    log_say(label, text, v1, v2, v3);
    popp(ScriptFileName, "DANGER", 0, "hasty_sm.pcx", 0);
}

int32_t GAMEAPI checksum_file(const char* filename) {
    // The original maps the file (Filemap_open_read); a file that cannot be opened or mapped
    // (empty files cannot) gives 0.
    const char* path = filefind_get(filename);
    uint32_t size = 0;
    uint8_t* data = win32::read_file(path ? path : filename, &size);
    if (!data) return 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < size; i++) sum ^= data[i];
    win32::free_file(data);
    return sum;
}

int32_t GAMEAPI checksum(const uint8_t* data, int32_t size, int32_t seed) {
    uint8_t sum = (uint8_t)seed;
    if (!size) return sum;
    for (uint32_t n = (uint32_t)size; n; n--) sum ^= *data++;
    // EAX of the original: the size's upper half, the result in AH (xor loop) and AL.
    return (int32_t)(((uint32_t)size & 0xFFFF0000u) | ((uint32_t)sum << 8) | sum);
}

int32_t GAMEAPI checksum_password(const char* password) {
    if (!std::strlen(password)) return 0;
    char upper[264];
    std::strcpy(upper, password);
    win32::CharUpperA(upper);
    uint8_t sum = 0;
    for (const char* p = upper; *p; p++) sum ^= (uint8_t)*p;
    return sum + 1;
}

// The human turn's and the multiplayer waits' message pump: one pass of wait_task; in multiplayer
// the network tasks, then the diplomatic channels computer factions asked the player for.
void GAMEAPI wait_loop() {
    if (!MultiplayerActive) {
        wait_task();
        return;
    }
    NetDaemon_net_tasks();
    if (ControlUpkeepA || ControlTurnC || (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31)))) return;
    for (int32_t i = 1; i < 8; i++) {
        if (i == CurrentPlayerFaction || (FactionStatus[0] & (1 << i)) || !NetChannelRequests[i - 1]) continue;
        log_say_2("Initiating diplomatic channel", CurrentPlayerFaction, i, 0);
        if (!NetDaemon_open_channel(&NetState, CurrentPlayerFaction, i)) communicate(CurrentPlayerFaction, i, 0);
    }
}
