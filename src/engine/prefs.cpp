// Preferences (see prefs.h).
#include "engine/prefs.h"

#include "engine/crt.h"
#include "engine/game_state.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "platform/win32.h"

// GetPrivateProfileStringA on the preferences file.
static void ini_get(const char* key, const char* def) {
    win32::GetPrivateProfileStringA(PrefsSection, key, def, TextBufferGetPtr, 0x100, PrefsIniPath);
}

static void ini_put(const char* key, const char* value) {
    win32::WritePrivateProfileStringA(PrefsSection, key, value, PrefsIniPath);
}

// The file's "Prefs Format" is current (12): stored bit sets are valid.
static bool prefs_format_current() {
    ini_get("Prefs Format", "0");
    return crt_atoi(TextBufferGetPtr) == 12;
}

// StrBuffer = decimal value.
static void str_number(int32_t value) {
    char num[80];
    StrBuffer[0] = 0;
    crt_itoa(value, num, 10);
    crt_strcat(StrBuffer, num);
}

// Appends value as binary digits (no leading zeros, but at least 8 digits) to StrBuffer.
static void str_binary(uint32_t value, const char* one, const char* zero) {
    bool started = false;
    for (int32_t bit = 31; bit >= 0; bit--) {
        if (value & (1u << bit)) {
            started = true;
            crt_strcat(StrBuffer, one);
        } else if (started || bit < 8) {
            crt_strcat(StrBuffer, zero);
        }
    }
}

void GAMEAPI prefs_get(const char* key, const char* def, int32_t use_default) {
    if (!use_default && prefs_format_current()) {
        ini_get(key, def);
        TextBuffer.position = TextBuffer.buffer;
        return;
    }
    crt_strcpy(TextBufferGetPtr, def);
    TextBuffer.position = TextBuffer.buffer;
}

static uint32_t laptop_prefs(int32_t laptop) { return laptop ? 0xA3E1DD16u : 0xBBE1DD96u; }
static uint32_t laptop_prefs2(int32_t laptop) { return laptop ? 0x00327168u : 0x003A7168u; }

// Reads "Laptop" (default 0) the way default_prefs/default_prefs2 do.
static int32_t read_laptop() {
    str_number(0);
    ini_get("Laptop", StrBuffer);
    TextBuffer.position = TextBuffer.buffer;
    return crt_atoi(TextBuffer.buffer);
}

uint32_t GAMEAPI default_prefs() { return laptop_prefs(read_laptop()); }

uint32_t GAMEAPI default_prefs2() { return laptop_prefs2(read_laptop()); }

uint32_t GAMEAPI default_warn() { return 0x3C3A9; }

uint32_t GAMEAPI default_rules() { return 0x1A0E; }

int32_t GAMEAPI prefs_get_int(const char* key, int32_t def, int32_t use_default) {
    str_number(def);
    if (!use_default)
        ini_get(key, StrBuffer);
    else
        crt_strcpy(TextBufferGetPtr, StrBuffer);
    TextBuffer.position = TextBuffer.buffer;
    return crt_atoi(TextBuffer.buffer);
}

void GAMEAPI prefs_fac_load() {
    char num[80];
    for (int32_t i = 1; i < 8; i++) {
        char* name = game().mfactions[i].filename;
        if (!ExpansionEnabled) continue;
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "Faction");
        crt_strcat(StrBuffer, " ");
        crt_itoa(i, num, 10);
        crt_strcat(StrBuffer, num);
        if (prefs_format_current())
            ini_get(StrBuffer, name);
        else
            crt_strcpy(TextBufferGetPtr, name);
        TextBuffer.position = TextBuffer.buffer;
        crt_strcpy(name, TextBufferGetPtr);
        crt_strcpy(game().mfactions[i].search_key, TextBufferGetPtr);
    }
}

// Reads a number key with default 0 and parses it with text_item_number.
static int32_t load_number(const char* key, int32_t def) {
    str_number(def);
    ini_get(key, StrBuffer);
    TextBuffer.position = TextBuffer.buffer;
    crt_atoi(TextBuffer.buffer);
    return text_item_number();
}

// Reads a bit-set key (default in StrBuffer) unless defaults are requested or the format is old.
static void load_bits(const char* key, int32_t use_defaults) {
    if (!use_defaults && prefs_format_current())
        ini_get(key, StrBuffer);
    else
        crt_strcpy(TextBufferGetPtr, StrBuffer);
    TextBuffer.position = TextBuffer.buffer;
}

void GAMEAPI prefs_load(int32_t use_defaults) {
    str_number(0);
    ini_get("Language", StrBuffer);
    TextBuffer.position = TextBuffer.buffer;
    set_language(crt_atoi(TextBuffer.buffer));
    DefaultPrefs.difficulty = load_number("Difficulty", 0);
    DefaultPrefs.map_type = load_number("Map Type", 0);
    DefaultPrefs.top_menu = load_number("Top Menu", 0);
    DefaultPrefs.faction_id = load_number("Faction", 1);

    // BUG(original): prefs_get_int leaves "0" in StrBuffer, so the default reads "0" + digits.
    StrBuffer[0] = 0;
    uint32_t prefs = laptop_prefs(prefs_get_int("Laptop", 0, 0));
    if (DefaultPrefs.difficulty < 2) prefs |= 0x20;  // tutorial messages for beginners
    str_binary(prefs, "1", "0");
    load_bits("Preferences", use_defaults);
    AlphaIniPrefs.preferences = text_item_binary();

    int32_t laptop = prefs_get_int("Laptop", 0, 0);
    StrBuffer[0] = 0;
    str_binary(laptop_prefs2(laptop), "1", "0");
    load_bits("More Preferences", use_defaults);
    AlphaIniPrefs.more_preferences = text_item_binary();

    if (!use_defaults && prefs_format_current())
        ini_get("Semaphore", "00000000");
    else
        crt_strcpy(TextBufferGetPtr, "00000000");
    TextBuffer.position = TextBuffer.buffer;
    AlphaIniPrefs.semaphore = text_item_binary();

    AlphaIniPrefs.customize = load_number("Customize", 0);

    StrBuffer[0] = 0;
    str_binary(0x1A0E, "1", "0");
    load_bits("Rules", use_defaults);
    AlphaIniPrefs.rules = text_item_binary();

    StrBuffer[0] = 0;
    str_binary(0x3C3A9, "1", "0");
    load_bits("Announce", use_defaults);
    AlphaIniPrefs.announce = text_item_binary();

    StrBuffer[0] = 0;
    for (int32_t i = 0; i < 7; i++) crt_strcat(StrBuffer, i == 0 ? "2, " : "1, ");
    load_bits("Custom World", use_defaults);
    for (int32_t i = 0; i < 7; i++) AlphaIniPrefs.custom_world[i] = text_item_number();

    prefs_get_int("Time Controls", 1, use_defaults);
    AlphaIniPrefs.time_controls = text_item_number();
}

void GAMEAPI prefs_put(const char* key, const char* value) { ini_put(key, value); }

void GAMEAPI prefs_put_int(const char* key, uint32_t value, int32_t binary) {
    char num[80];
    StrBuffer[0] = 0;
    if (!binary) {
        crt_itoa((int32_t)value, num, 10);
        crt_strcat(StrBuffer, num);
    } else {
        str_binary(value, "1", "0");
    }
    ini_put(key, StrBuffer);
}

void GAMEAPI prefs_save(int32_t save_factions) {
    char num[80];
    str_number(12);
    ini_put("Prefs Format", StrBuffer);
    str_number(DefaultPrefs.difficulty);
    ini_put("Difficulty", StrBuffer);
    str_number(DefaultPrefs.map_type);
    ini_put("Map Type", StrBuffer);
    str_number(DefaultPrefs.top_menu);
    ini_put("Top Menu", StrBuffer);
    str_number(DefaultPrefs.faction_id);
    ini_put("Faction", StrBuffer);
    StrBuffer[0] = 0;
    str_binary((uint32_t)AlphaIniPrefs.preferences, "1", "0");
    ini_put("Preferences", StrBuffer);
    StrBuffer[0] = 0;
    str_binary((uint32_t)AlphaIniPrefs.more_preferences, "1", "0");
    ini_put("More Preferences", StrBuffer);
    prefs_put_int("Semaphore", (uint32_t)AlphaIniPrefs.semaphore, 1);
    prefs_put_int("Announce", (uint32_t)AlphaIniPrefs.announce, 1);
    prefs_put_int("Rules", (uint32_t)AlphaIniPrefs.rules, 1);
    prefs_put_int("Customize", (uint32_t)AlphaIniPrefs.customize, 0);
    StrBuffer[0] = 0;
    for (int32_t i = 0; i < 7; i++) {
        crt_itoa(AlphaIniPrefs.custom_world[i], num, 10);
        crt_strcat(StrBuffer, num);
        crt_strcat(StrBuffer, ", ");
    }
    ini_put("Custom World", StrBuffer);
    prefs_put_int("Time Controls", (uint32_t)AlphaIniPrefs.time_controls, 0);
    if (save_factions) {
        for (int32_t i = 1; i < 8; i++) {
            if (!ExpansionEnabled) continue;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "Faction");
            crt_strcat(StrBuffer, " ");
            crt_itoa(i, num, 10);
            crt_strcat(StrBuffer, num);
            ini_put(StrBuffer, game().mfactions[i].filename);
        }
    }
}

void GAMEAPI set_language(int32_t language) { GameLanguage = language; }

void GAMEAPI prefs_use() {
    GamePreferences = AlphaIniPrefs.preferences;
    GameMorePreferences = AlphaIniPrefs.more_preferences;
    GameWarnings = AlphaIniPrefs.announce;
}

#ifndef SMACX_HYBRID
void prefs_static_init(const char* ini_path) {
    PrefsIniPath = ini_path ? ini_path : ".\\Alpha Centauri.Ini";
    PrefsSection = "Alpha Centauri";
}
#endif
