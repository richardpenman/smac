// Hotseat / PBEM handover (control_turn 0x5275B0, the part from 0x5277A0 to 0x527D92): a human
// faction other than the viewer (always in a hotseat / PBEM game) takes over the screen.
//
// ui_hotseat_switch: the battle report stopped (interface mode 10), the base window, the menu
// column and the commlink panel closed, the screen cleared (the original hides the game's windows
// and draws the desktop's box sprites over it; here the screen is black, as the opening's), then
// SWITCHING (NEXT PLAYER TURN, the leader and the mission year; "Set Password or Press ENTER") or,
// when the faction has a password (Faction +0x2088), SWITCHING2 ("Enter Password"), with flags
// 0x800004 (a password edit field). Its SAVE AND EXIT button saves (GameMoreRules 0x40 while
// save_game runs) and leaves the game. The text entered is checked with checksum_password: equal
// to the faction's: the turn starts; a wrong password: the popup again, the third wrong one in
// control_turn saves and leaves; a faction without a password: the text (non empty: its checksum
// is not 0) confirmed with VERIFYPASSWORD becomes its password. Then the view is centred on the
// faction's first base, else its first unit.
//
// ui_hotseat_show: the screen shown again, the message list cleared, WONLOST (the combat won and
// lost since the player's last turn), Console_update_data, the timers.
// read_tamper / set_tamper (0x51E000 / 0x51E090): the PBEM tamper records (TamperIndex, value
// "MVM", 4 bytes; TamperRecords, value "MVB", 0x4B0 bytes) are kept per machine. The original keeps
// them in the registry (HKLM\SOFTWARE\Microsoft\DirectPlay\Applications\Sid Meier's Alpha
// Centauri); OpenSMAC keeps the two values in the file tamper.reg of its per-user directory (the
// --outdir with tests): a missing file reads nothing, as a missing registry value.
// Approximated: the players' own message lists (sub_51E530: each faction's messages kept between
// its turns, the message mode then set) are not kept; the desktop's box sprites are not drawn.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/popup.h"
#include "ui/reports.h"
#include "ui/statuswin.h"
#include "ui/win.h"

static_assert(offsetof(Faction, unk_102) == 0x2088, "the faction's password");

namespace {
int32_t& password(int32_t f) { return Factions[f].unk_102; }

void leave_game() {
    ControlTurnA = 1;
    ControlTurnB = 0;
    ui_set_desktop(false);  // (Win_hide)
}
}  // namespace

int32_t ui_hotseat_switch(int32_t f, int32_t* tries) {
    if (InfcList[InfcUsed] == 10) {
        BattleWin_stop_timer(&BattleWin);
        SubInterface_release_iface_mode(&BattleWin);
    }
    stop_timers();
    if (Win_is_visible(&BaseWin)) BaseWin_exit(&BaseWin);
    AlphaMenu_hide_main();
    multiwin_hide();
    ui_set_desktop(true);  // the windows hidden, the screen cleared
    ui_present();
    std::fprintf(stderr, "hotseat: switching to faction %d (%s)\n", f, password(f) ? "password" : "no password");
    for (;;) {
        BasePop* pop = popup_new();
        Popup_start(pop, ScriptFileName, password(f) ? "SWITCHING2" : "SWITCHING", -1, nullptr, 0x800004);
        int32_t r = BasePop_exec_3(pop, 0, 0);
        int32_t button = BasePop_button_pressed(pop);
        popup_delete(pop);
        if (r < 0) {
            if (ControlTurnA) return 1;
            continue;
        }
        if (button) {  // SAVE AND EXIT
            GameMoreRules |= 0x40;
            // (save_game returns 1 after a hotseat / PBEM save: ControlTurnA is set, the game ends.)
            if (save_game(0) == 0) {
                leave_game();
                return 1;
            }
            if (ControlTurnA) std::fprintf(stderr, "pbem: saved and exited, save count %d\n", PbemSaveCount);
            GameMoreRules &= ~0x40;
            if (ControlTurnA) return 1;
            continue;
        }
        int32_t sum = checksum_password(ParseStrBuffer[0]);
        if (sum == password(f)) break;
        if (password(f)) {
            std::fprintf(stderr, "hotseat: wrong password (%d)\n", *tries + 1);
            if (++*tries > 2) {
                GameMoreRules |= 0x40;
                save_game(0);
                leave_game();
                return 1;
            }
            if (ControlTurnA) return 1;
            continue;
        }
        if (X_pop_ask("VERIFYPASSWORD", 0x50, reinterpret_cast<const char*>(mem::host(0x93AA08)), 0, 0x800000)) {
            if (ControlTurnA) return 1;
            continue;
        }
        if (checksum_password(ParseStrBuffer[0]) != sum) {
            if (ControlTurnA) return 1;
            continue;
        }
        password(f) = sum;
        std::fprintf(stderr, "hotseat: faction %d password set\n", f);
        break;
    }
    if (ControlTurnA) return 1;
    // The view on the faction's first base, else its first unit.
    bool done = false;
    for (int32_t i = 0; i < BaseCount && !done; i++) {
        if (Bases[i].faction_id != CurrentPlayerFaction) continue;
        pfncMoveMap(MapWins[0], Bases[i].x, Bases[i].y, 1);
        Console_set_cursor(&MainConsole, Bases[i].x, Bases[i].y);
        done = true;
    }
    for (int32_t i = 0; i < VehCount && !done; i++) {
        if (Vehs[i].faction_id != CurrentPlayerFaction) continue;
        pfncMoveMap(MapWins[0], Vehs[i].x, Vehs[i].y, 1);
        Console_set_cursor(&MainConsole, Vehs[i].x, Vehs[i].y);
        done = true;
    }
    return 0;
}

void ui_hotseat_show(int32_t f) {
    ui_set_desktop(false);  // Win_show of the game's windows
    draw_map(1);
    MessageWin_clear(&MessageWin);
    if (FactionCombatWin[f] || FactionCombatLoss[f]) {
        parse_num(0, FactionCombatWin[f]);
        parse_num(1, FactionCombatLoss[f]);
        popp(ScriptFileName, "WONLOST", 0, "bad_sm.pcx", 0);
    }
    Console_update_data(&MainConsole, 0);
    start_timers();
    ui_invalidate();
    std::fprintf(stderr, "hotseat: faction %d takes the turn (turn %d)\n", f, CurrentTurn);
}

// ---- the tamper records (the registry's MVM / MVB values) ----

namespace {
std::string tamper_path() { return g_ui_outdir + "/tamper.reg"; }
}  // namespace

void GAMEAPI read_tamper() {
    FILE* f = std::fopen(tamper_path().c_str(), "rb");
    if (!f) return;
    int32_t index;
    if (std::fread(&index, 4, 1, f) == 1) {
        TamperIndex = index;
        static int32_t records[50][6];
        if (std::fread(records, sizeof(records), 1, f) == 1) std::memcpy(TamperRecords, records, sizeof(records));
    }
    std::fclose(f);
}

void GAMEAPI set_tamper() {
    FILE* f = std::fopen(tamper_path().c_str(), "wb");
    if (!f) return;
    std::fwrite(&TamperIndex, 4, 1, f);
    std::fwrite(TamperRecords, 0x4B0, 1, f);
    std::fclose(f);
    for (const auto& rec : TamperRecords) {
        if (rec[0] == TamperKeyA && rec[1] == TamperKeyB && rec[2] == CurrentTurn && rec[3] == CurrentPlayerFaction &&
            rec[5] == TamperKeyC) {
            std::fprintf(stderr, "pbem: tamper record %s (turn %d faction %d save %d loads %d)\n",
                         rec[4] ? "again" : "new", CurrentTurn, CurrentPlayerFaction, TamperKeyC, rec[4] + 1);
        }
    }
}
