// Headless UI area "popup": popups and message boxes (engine/ui_bridge.h). Nothing is shown; each
// returns the default choice (0), like the oracle's forced no-display path. Replaced in smacx by
// src/ui/win_popup.cpp and src/ui/popup_menus.cpp (the reconstructed BasePop / Popup, popup.h).
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/crt.h"
#include "engine/ui_bridge.h"

#include <cstdio>

int32_t GAMEAPI popp(const char*, const char*, int32_t, const char*, int32_t) { return 0; }
int32_t GAMEAPI X_pop(const char* label, int32_t) {
    std::fprintf(stderr, "popup: %s\n", label ? label : "");
    return 0;
}
int32_t GAMEAPI X_pop_2(const char*, const char*, int32_t) { return 0; }
int32_t GAMEAPI X_pops(const char*, const char*, int32_t) { return 0; }
int32_t GAMEAPI X_pops_11(const char*, int32_t, const char*, int32_t) { return 0; }
// Alerts are dismissed (0).
int32_t GAMEAPI popb(const char*, int32_t, int32_t, const char*, int32_t) { return 0; }
int32_t GAMEAPI POP2(const char*, const char*, int32_t) { return 0; }
// Only shown to human players (hotseat/PBEM); never reached by an unattended observer.
int32_t GAMEAPI POP3(const char*, int32_t, int32_t, int32_t, int32_t) { return 0; }
// Message box dismissed (returns 0).
int32_t THISCALL NetMsg_pop(uint8_t*, const char*, int32_t, int32_t, const char*) { return 0; }
void THISCALL NetMsg_close(uint8_t*) {}
// crash_landing: no animation; the first base keeps its name (the FIRSTBASE question is not asked).
void ui_crash_landing_drop(int32_t, int32_t, bool) {}
void ui_crash_landing_close() {}
void ui_first_base_name(const char* default_name) { crt_strcpy(ParseStrBuffer[0], default_name); }
// The popups built on Popup objects on the original functions' stacks: not shown; each returns
// its "cancelled" value.
void ui_pact_ends_dialog(int32_t) {}
void ui_tech_popup_open() {}
void ui_tech_popup_show(int32_t, int32_t, int32_t, bool) {}
void ui_tech_popup_exec() {}
void ui_production_popup_open(int32_t) {}
void ui_production_popup_blurb(bool) {}
void ui_production_popup_exec() {}
void ui_council_menu_open(int32_t) {}
void ui_council_menu_add(const char*, int32_t) {}
int32_t ui_council_menu_exec() { return -1; }
void ui_council_menu_close() {}
int32_t ui_supply_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_artifact_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_disembark_menu(int32_t) { return 0; }
void THISCALL Popup_ctor(uint8_t*) {}
void THISCALL Popup_dtor(uint8_t*) {}
#endif
