// The engine's popup calls in smacx (the headless "popup" area replaced; see win.h and popup.h):
// popp, popt, popb, POP2, POP3, the X_pop* / X_pops* wrappers of the veh module with the pop module
// they call (pop_9, pops_18, pop_ask_6), NetMsg_pop / NetMsg_close, and the crash landing's base
// naming. Each builds a Popup from a section of a script file (BasePop_start), shows it
// (BasePop_exec_3) and returns the original's value.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/infowin.h"
#include "ui/win.h"

namespace {

// The alien factions' scripts (Crossfire): FUN_00645646 = fopen(name, "rb") as an existence test.
bool file_exists(const char* name) {
    if (CrtFile* f = crt_fopen(name, "rb")) {
        crt_fclose(f);
        return true;
    }
    return false;
}

bool is_main_script(const char* file) {
    return file && (!crt_strcmp(file, ScriptFileName) || !crt_strcmp(file, "SCRIPT.txt"));
}

// Popup_start / X_pop_9 / X_pops_18 / X_pop_ask_6's script choice: with the expansion, the alien
// player reads alienIscript, a human player talking to an alien alienuscript; a label missing there
// falls back to the main script. Returns the alternative file to try first, or null.
const char* alien_script(const char* file) {
    if (!is_main_script(file) || !ExpansionEnabled) return nullptr;
    if (MFactions[CurrentPlayerFaction].rule_flags & 0x80) {
        if (file_exists("alienIscript.txt")) return "alienIscript";
    } else if (DiploSecondFaction > 0 && DiploSecondFaction < 8 && (MFactions[DiploSecondFaction].rule_flags & 0x80)) {
        if (file_exists("alienuscript.txt")) return "alienuscript";
    }
    return nullptr;
}

// Popup_start (0x406380).
int32_t popup_start(BasePop* pop, const char* file, const char* label, int32_t active, const char* edit,
                    uint32_t flags) {
    if (const char* alt = alien_script(file))
        if (!BasePop_start(pop, alt, label, active, edit, flags, nullptr)) return 0;
    return BasePop_start(pop, file, label, active, edit, flags, nullptr);
}

struct PopupHolder {  // the Popup object a function constructs on its stack
    BasePop* pop = popup_new();
    ~PopupHolder() { popup_delete(pop); }
};

}  // namespace

// Popup_start (0x406380) for the other UI files (popup.h).
int32_t Popup_start(BasePop* pop, const char* file, const char* label, int32_t active, const char* edit,
                    uint32_t flags) {
    return popup_start(pop, file, label, active, edit, flags);
}

// ---- pop module (0x627100..) ----

int32_t GAMEAPI pops_18(const char* file, const char* label, int32_t active, const char* edit, uint32_t flags,
                        const char* pcx, int32_t scale_d, int32_t scale_s, int32_t a9) {
    PopupHolder h;
    if (BasePop_start(h.pop, file, label, active, edit, flags, nullptr)) return -1;
    if (pcx) BasePop_set_picture(h.pop, pcx);
    (void)scale_d;
    (void)scale_s;
    BasePop_set_check_state(h.pop, DialogChoices);
    int32_t r = BasePop_exec_3(h.pop, 0, a9);
    if (flags & 0x8000) return r;
    DialogChoices = BasePop_check_state(h.pop);
    DialogToggle = BasePop_button_pressed(h.pop);
    return r;
}

int32_t GAMEAPI pop_9(const char* file, const char* label, int32_t active, const char* edit, uint32_t flags,
                      int32_t a6) {
    return pops_18(file, label, active, edit, flags, nullptr, 1, 1, a6);
}

int32_t GAMEAPI pop_ask_6(const char* file, const char* label, int32_t active, const char* edit, int32_t a5,
                          uint32_t flags) {
    PopupHolder h;
    if (BasePop_start(h.pop, file, label, active, edit, flags | 0x44, nullptr)) return -1;
    BasePop_set_check_state(h.pop, DialogChoices);
    if (!BasePop_item_count(h.pop)) return -1;
    int32_t r = BasePop_exec_3(h.pop, 0, a5);
    DialogToggle = BasePop_button_pressed(h.pop);
    return r;
}

// ---- veh module wrappers (0x5BF310..) ----

int32_t GAMEAPI X_pop_9(const char* file, const char* label, int32_t active, const char* edit, uint32_t flags,
                        int32_t a6) {
    if (const char* alt = alien_script(file)) {
        int32_t r = pop_9(alt, label, active, edit, flags, a6);
        if (r != -1) return r;
    }
    return pop_9(file, label, active, edit, flags, a6);
}

int32_t GAMEAPI X_pops_18(const char* file, const char* label, int32_t active, const char* edit, uint32_t flags,
                          const char* pcx, int32_t scale_d, int32_t scale_s, int32_t a9) {
    if (const char* alt = alien_script(file)) {
        int32_t r = pops_18(alt, label, active, edit, flags, pcx, scale_d, scale_s, a9);
        if (r != -1) return r;
    }
    return pops_18(file, label, active, edit, flags, pcx, scale_d, scale_s, a9);
}

int32_t GAMEAPI X_pop_ask_6(const char* file, const char* label, int32_t active, const char* edit, int32_t a5,
                            uint32_t flags) {
    if (const char* alt = alien_script(file)) {
        int32_t r = pop_ask_6(alt, label, active, edit, a5, flags);
        if (r != -1) return r;
    }
    return pop_ask_6(file, label, active, edit, a5, flags);
}

int32_t GAMEAPI X_pop(const char* label, int32_t a2) { return X_pop_9(ScriptFileName, label, -1, nullptr, 0, a2); }
// X_pop_6 (0x5BF3F0): X_pop_9 of the main script with flags (check boxes from DialogChoices).
int32_t GAMEAPI X_pop_6(const char* label, int32_t flags, int32_t a3) {
    return X_pop_9(ScriptFileName, label, -1, nullptr, (uint32_t)flags, a3);
}
// X_pop_2 (0x5BF330): X_pop_9(a1, a2, -1, 0, 0, a3): the first argument is the file (alt_set's
// BASESUBMERGED call passes the label and the picture instead, so the original shows nothing).
int32_t GAMEAPI X_pop_2(const char* file, const char* label, int32_t a3) {
    return X_pop_9(file, label, -1, nullptr, 0, a3);
}
int32_t GAMEAPI X_pops(const char* label, const char* pcx, int32_t a3) {
    return X_pops_18(ScriptFileName, label, -1, nullptr, 0, pcx, 1, 1, a3);
}
int32_t GAMEAPI X_pops_11(const char* label, int32_t flags, const char* pcx, int32_t a4) {
    return X_pops_18(ScriptFileName, label, -1, nullptr, (uint32_t)flags, pcx, 1, 1, a4);
}
int32_t GAMEAPI X_pop_ask(const char* label, int32_t active, const char* text, int32_t a4, int32_t a5) {
    return X_pop_ask_6(ScriptFileName, label, active, text, a4, (uint32_t)a5);
}

// ---- game module: popup styles (0x588460..) ----

void GAMEAPI popups_normal() { PopupDialogState = 0; }
void GAMEAPI popups_tutorial() { PopupDialogState = 2; }

// popt (0x48C320): a popup in the tutorial style.
int32_t GAMEAPI popt(const char* file, const char* label, int32_t active, int32_t edit, int32_t flags) {
    popups_tutorial();
    const char* text = edit ? reinterpret_cast<const char*>(mem::host((uint32_t)edit)) : nullptr;
    int32_t r = X_pop_9(file, label, active, text, (uint32_t)flags, 0);
    popups_normal();
    return r;
}

// ---- ui module ----

// popp (0x48C0A0): a popup of `file` with a picture; returns the choice.
int32_t GAMEAPI popp(const char* file, const char* label, int32_t flags, const char* pcx, int32_t a5) {
    PopupHolder h;
    popup_start(h.pop, file, label, -1, nullptr, (uint32_t)flags);
    if (pcx) BasePop_set_picture(h.pop, pcx);
    int32_t r = BasePop_exec_3(h.pop, 0, a5);
    DialogToggle = BasePop_button_pressed(h.pop);
    return r;
}

// popb (0x48C650): an alert about the current base, shown to its (human) owner with a sound; the
// warning bit `flags` of GameWarnings makes it a dialog (else a message only) whose answers act on
// the base: 0 zoom to the base (BaseUpkeepFlag), 2 let the governor handle it, 3 nerve staple; the
// extra button stops this kind of warning.
int32_t GAMEAPI popb(const char* label, int32_t flags, int32_t sound_id, const char* pcx, int32_t a5) {
    BASE& base = cur_base();
    int32_t owner = (uint8_t)base.faction_id;
    if (!(FactionStatus[0] & (1 << (owner & 31)))) return 0;
    if ((GameState & STATE_GAME_DONE) && !(GameState & STATE_FINAL_SCORE_DONE)) return 0;
    if (owner != CurrentPlayerFaction && !PbemActive) return 0;
    char name[256];
    crt_strcpy(name, label);
    PopupHolder h;
    parse_says(0, base.name, -1, -1);
    if (sound_id >= 0) FX_play(&Sounds, sound_id);
    else if (MultiplayerActive) FX_play(&Sounds, 0x21);
    popup_start(h.pop, ScriptFileName, name, -1, nullptr, 0);
    int32_t extra;
    if (!flags || !(GameWarnings & flags) || PbemActive) {
        extra = 2;
    } else {
        extra = 0x3EA;
        if ((owner == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW) ||
             (uint8_t)base.faction_id == CurrentPlayerFaction || (base.visibility & (1 << CurrentPlayerFaction))) &&
            !(GameMorePreferences & MPREF_ADV_ZOOM_BASE_NO_RECENTER_MAP))
            Console_focus(&MainConsole, base.x, base.y, owner);
        if (pcx) BasePop_set_picture(h.pop, pcx);
    }
    BasePop_set_mode(h.pop, extra);
    if (!BaseUpkeepFlag && owner == CurrentPlayerFaction && !MultiplayerActive && !PbemActive) {
        BasePop_item(h.pop, label_get(0x2C), 0);
        BasePop_item(h.pop, label_get(0x2D), 1);
        if ((flags & 8) && base.pop_size > 1 && !MultiplayerActive &&
            Factions[owner].SE_police + (base_fac(CurrentBaseID, 0x23) ? 2 : 0) >= 0)
            BasePop_item(h.pop, label_get(0x1EB), 3);
        if (flags) BasePop_button(h.pop, label_get(0x1BB));
    }
    if (a5) BasePop_set_picture(h.pop, reinterpret_cast<const char*>(mem::host((uint32_t)a5)));  // SpriteBox_sprite
    int32_t r = BasePop_exec_3(h.pop, 0, 0);
    if (MultiplayerActive || PbemActive) {
        r = 1;
        if (MultiplayerActive && owner == CurrentPlayerFaction && (FactionStatus[0] & (1 << owner)))
            base.state_flags |= 0x10;
    }
    if (sound_id >= 0) FX_fade(&Sounds, sound_id);
    if (!BasePop_button_pressed(h.pop)) {
        if (r == 0) BaseUpkeepFlag = 1;
        else if (r == 2) base.governor_flags |= (int32_t)0x80000040;
        else if (r == 3) action_staple(CurrentBaseID);
        return r;
    }
    GameWarnings &= ~flags;
    prefs_save(0);
    return 0;
}

// POP2 (0x405140): the popup's text goes to the message line; shown as a popup with a picture
// when the "pop up" preference bit 0x20000 of GameWarnings is set.
int32_t GAMEAPI POP2(const char* label, const char* pcx, int32_t) {
    PopupHolder h;
    popup_start(h.pop, ScriptFileName, label, -1, nullptr, 0);
    std::string text = BasePop_text(h.pop);
    crt_strcpy(StrBuffer, text.c_str());
    if (PbemActive) return 0;  // the PBEM message list is not reconstructed
    MainInterface_set_message(&MainInterfaceWin, text.c_str());
    if (!(GameWarnings & WARN_STOP_RANDOM_EVENT)) return 0;
    if (pcx) BasePop_set_picture(h.pop, pcx);
    return BasePop_exec_3(h.pop, 0, 0);
}

// POP3 (0x4055A0): a message (the message line; hotseat/PBEM: the player's message list).
int32_t GAMEAPI POP3(const char* label, int32_t, int32_t, int32_t, int32_t) {
    PopupHolder h;
    popup_start(h.pop, ScriptFileName, label, -1, nullptr, 0);
    std::string text = BasePop_text(h.pop);
    crt_strcpy(StrBuffer, text.c_str());
    if (!PbemActive) MainInterface_set_message(&MainInterfaceWin, text.c_str());
    return 0;
}

// NetMsg_pop (0x47A890): in single player (a3 0, or no interface window open) a modal popup with
// an OK button. In a network game the original shows the message box without waiting (a timer of
// `delay` ms or NetMsg_close hides it); approximated: the text goes to the message line (and a
// "netmsg LABEL: text" line to stderr) and the game goes on at once.
int32_t THISCALL NetMsg_pop(uint8_t*, const char* label, int32_t delay, int32_t a3, const char* pcx) {
    PopupHolder h;
    popup_start(h.pop, ScriptFileName, label, -1, nullptr, 0);
    if (MultiplayerActive) {
        std::string text = BasePop_text(h.pop);
        std::fprintf(stderr, "netmsg %s: %s\n", label, text.c_str());
        ui_set_message(text);
        return 0;
    }
    if (pcx && !MultiplayerActive && delay < 0) BasePop_set_picture(h.pop, pcx);
    (void)a3;
    BasePop_exec_3(h.pop, 0, 0);
    return 0;
}
void THISCALL NetMsg_close(uint8_t*) {}

// The crash landing's FIRSTBASE question (a Popup of crash_landing with an edit field and OK only).
void ui_first_base_name(const char* default_name) {
    PopupHolder h;
    popup_start(h.pop, ScriptFileName, "FIRSTBASE", 0x17, default_name, 4);
    BasePop_exec_3(h.pop, 0, 0);
    crt_strcpy(ParseStrBuffer[0], BasePop_edit_text(h.pop, 0));
}
void ui_crash_landing_drop(int32_t, int32_t, bool) {}
void ui_crash_landing_close() {}

// Console_terraform's question: the cost section, then (affordable) the pay section's choices.
int32_t ui_script_popup(const char* file, const char* label, const char* choices_label) {
    PopupHolder h;
    popup_start(h.pop, file, label, -1, nullptr, 0);
    if (choices_label) popup_start(h.pop, file, choices_label, -1, nullptr, 0x80);
    return BasePop_exec_3(h.pop, 0, 0);
}

// Test aid: "FILE/LABEL[/FLAGS[/PCX]]" (FILE "SCRIPT" is the main script, "tutor" the tutorial
// in its style; FLAGS hexadecimal).
void ui_popup_command(const std::string& spec) {
    std::vector<std::string> f;
    size_t start = 0;
    for (;;) {
        size_t e = spec.find('/', start);
        f.push_back(spec.substr(start, e == std::string::npos ? std::string::npos : e - start));
        if (e == std::string::npos) break;
        start = e + 1;
    }
    if (f.size() < 2) return;
    const char* file = f[0] == "SCRIPT" ? ScriptFileName : f[0].c_str();
    uint32_t flags = f.size() > 2 ? (uint32_t)std::strtoul(f[2].c_str(), nullptr, 16) : 0;
    const char* pcx = f.size() > 3 && !f[3].empty() ? f[3].c_str() : nullptr;
    // The tutorial file shows in the tutorial style (popt), as TutWin's popups do.
    int32_t r = f[0] == "tutor" ? popt(file, f[1].c_str(), -1, 0, (int32_t)flags)
                                : popp(file, f[1].c_str(), (int32_t)flags, pcx, 0);
    std::fprintf(stderr, "test popup %s/%s -> %d\n", f[0].c_str(), f[1].c_str(), r);
}
