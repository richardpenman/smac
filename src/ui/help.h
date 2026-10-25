// The help module of the original UI (0x44B380..0x44CBC0): the texts of a technology's
// consequences (help_tech_info, tech_heck) and the help_* entry points of the datalinks
// (Datalink_exec, datalink.cpp). Links are written as "$LINK<name=id>" with id = category *
// 10000 + entry (Datalink_construct_id).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/memmap.h"
#include "ui/font_text.h"

// help_tech_info (0x44B9E0): calls `out` with the lines of section `section` of tech_id's
// consequences (0 facilities and projects, 1 technologies it leads to, 2 unit advances: reactors,
// chassis, weapons, armour, abilities, unit types; 3 miscellaneous: social models, citizens,
// tech_heck), one item per line ("^" + link); section -1: all of them, each a "^HEADER: " line
// with the items separated by ", " (help_tech_info_2, the tech popup).
void help_tech_info(const std::function<void(const char*)>& out, int32_t tech_id, int32_t faction_id,
                    int32_t section);
// The "$LINK<name=id>" of entry `index` of datalink category `cat` (help_create_link with
// Datalink_construct_id).
std::string help_link(const char* name, int32_t cat, int32_t index);

// The datalinks (Datalink_exec on DatalinkWin, 0x429180): opens the help browser on category
// `cat` with entry `id` selected (ids as Datalink_construct_id; 0, 0: the first page).
ADDR_NATIVE(0x429180) void THISCALL Datalink_exec(uint8_t* win, int32_t cat, int32_t id);
ADDR_NATIVE(0x44CB30) void GAMEAPI help_any();
ADDR_NATIVE(0x44C880) void GAMEAPI help_tech(int32_t tech_id);

// Text lines of a section of a game text file as StringList_load reads them (the lines up to an
// empty one or the next section, "$" markers expanded with parse_string when `parse`), with the
// layout modes of their "^" prefixes (as BasePop_string), ready for Buffer_write_strings.
struct HelpText {
    std::vector<std::string> store;
    std::vector<TextLine> lines;
    void add(const char* line);
    void clear() {
        store.clear();
        lines.clear();
    }
    void finish();  // fills `lines` from `store`
};
bool help_load_text(HelpText* out, const char* file, const char* label, bool parse);
