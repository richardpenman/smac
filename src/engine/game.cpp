// Game setup and top-level flow (see game.h).
#include "engine/game.h"

#include "engine/clock.h"
#include "engine/crt.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/path.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"

// Default OK/Cancel popup button texts from labels 101 (OK) and 102 (CANCEL), upper case.
static void set_default_button_texts() {
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(102));
    if (GameLanguage != 2) win32::CharUpperA(StrBuffer);
    BasePop_set_def_cancel_text(StrBuffer);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(101));
    win32::CharUpperA(StrBuffer);
    BasePop_set_def_ok_text(StrBuffer);
}

int32_t GAMEAPI system_init() {
    crt_mkdir("saves");
    crt_mkdir("saves\\auto");
    crt_mkdir("maps");
    crt_mkdir("scenarios");
    GameNotStarted = 1;
    log_set_state(0);
    set_language(prefs_get_2("Language", 0, 0));
    textcolor_init();
    Strings_init(&StringTable, 0xC320);
    if (labels_init()) return 1;
    set_default_button_texts();
    load_fixed_sprites();
    config_popups();
    for (int32_t i = 1; i < 8; i++) {
        uint8_t* mem = (uint8_t*)crt_new(0x22480);
        MapWins[i] = mem ? MapWin_ctor(mem, 1) : nullptr;
    }
    MapWinOptions &= 0xFFFFF73F;
    MapWins[0] = &MainConsole;  // ppMain
    MainInterface_init(&MainInterfaceWin, 0);
    BattleWin_init(&BattleWin);
    Console_init(&MainConsole, 0);
    MultiDebug_init(&MultiDebugWin, &NetState, ui_console_vbase(), &MultiDebugParam);
    MessageWin_init(&MessageWin);
    Datalink_init(&DatalinkWin);
    bung_momma();
    return 0;
}

void GAMEAPI system_close() {
    release_sound();
    ConsoleVBase = ui_console_vbase();
    if (TopMenuWin) {
        ui_delete_object(TopMenuWin, false);
        TopMenuWin = nullptr;
    }
    DesignWin_close(&DesignWin);
    MultiDebug_close(&MultiDebugWin);
    Console_close(&MainConsole);
    Path_shutdown(&Paths);
    map_shutdown();
    for (int32_t i = 1; i < 8; i++) {
        if (MapWins[i]) {
            ui_delete_object(MapWins[i], true);
            MapWins[i] = nullptr;
        }
    }
}

int32_t GAMEAPI game_init(int32_t init_text, int32_t init_rules) {
    GameNotStarted = 1;
    // timeGetTime() seeds (the harness reseeds with its "seed" command)
    game_srand(engine_real_time());
    game_reseed(engine_real_time());
    if (!init_text) {
        log_set_state(0);
    } else {
        Strings_init(&StringTable, 0xC320);
        if (labels_init()) return 1;
        set_default_button_texts();
    }
    load_sprites();
    if (read_rules(init_rules)) return 1;
    start_timers();
    PopupSpriteAPtr = &PopupSpriteA;
    PopupSpriteBPtr = &PopupSpriteB;
    MenuIconSpritesPtr = &MenuIconSprites;
    return 0;
}

void GAMEAPI game_close() {
    stop_timers();
    labels_shutdown();
    Strings_shutdown(&StringTable);
}

int32_t GAMEAPI game_reload(int32_t close_windows, int32_t init_rules) {
    if (close_windows) {
        DesignWin_close(&DesignWin);
        BaseWin_close(&BaseWin);
        mapwin_system_shutdown();
        StatusWin_close(&InfoWin);
        GraphicWin_close(&WorldWin);
        GraphicWin_close(&MultiWin);
        Win_hide(&MainInterfaceWin);
    }
    stop_timers();
    labels_shutdown();
    Strings_shutdown(&StringTable);
    if (game_init(1, init_rules)) return 1;
    if (close_windows) desktop_init(1);
    GameNotStarted = 0;
    return 0;
}

#ifndef SMACX_HYBRID
uint8_t* GAMEAPI headless_basepop_alloc();

void engine_static_init(const char* ini_path) {
    // CRT static constructors (0x682680 ...): text buffers and indexes, string table
    text_static_init();
    Strings_ctor(&StringTable);
    prefs_static_init(ini_path);
    rules_static_init();
    // pfncWinMain (0x45F950) before control_game
    GameCmdLine[0] = 0;
    ExpansionEnabled = 1;
    dword_9469FC = 0;
    BasePopAlloc = headless_basepop_alloc;
    GameDialogFlags |= 4;
}
#endif
