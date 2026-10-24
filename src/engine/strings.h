// File lookup, string helpers, the string table and interface labels (original module gfx2:
// filefind 0x6003A0-0x600760, purge_*/kill_lf/add_lf 0x600760-0x600860, Strings 0x6168D0-0x6169D0,
// labels 0x6169D0-0x616BC0).
#pragma once
#include "engine/globals.h"
#include "engine/crt.h"
#include "engine/heap.h"

// ---- filefind: locate game files (CD drive, alternate directory, game directory) ----
GLOBAL(Filefind, FilefindPaths, 0x9B8198);  // cd_path doubles as FilefindState

// Returns the CD path ("" when no CD was found).
ADDR(0x6003A0) char* GAMEAPI filefind_cd_drive_letter();
// Sets the alternate directory searched first (relative names are taken relative to exe_dir).
ADDR(0x6003B0) void GAMEAPI filefind_set_alternate(const char* path);
// Records the game directory and looks for a CD drive holding the file `name`. Without a CD and
// no_prompt == 0 the "insert CD" popup is shown. Returns 0, or 0x10 / 4 / the popup's result.
ADDR(0x600400) int32_t GAMEAPI filefind_init(const char* name, int32_t no_prompt);
// Finds file `name` (alternate directory, game directory, CD). Returns the full path in
// FilefindPaths.last_path, or nullptr.
ADDR(0x6005D0) char* GAMEAPI filefind_get(const char* name);

// Opens a game file found with filefind_get (or `name` as given) with fopen (original video module).
ADDR(0x634BB0) CrtFile* GAMEAPI env_open(const char* name, const char* mode);

// ---- string helpers ----
// Does nothing (the original scans past leading blanks without storing the result).
ADDR(0x600760) void GAMEAPI purge_leading(char* str);
ADDR(0x600780) void GAMEAPI purge_trailing(char* str);
// Removes leading and trailing blanks (' ' only).
ADDR(0x6007B0) void GAMEAPI purge_spaces(char* str);
// Cuts the string at its last '\n'.
ADDR(0x600820) void GAMEAPI kill_lf(char* str);
ADDR(0x600840) void GAMEAPI add_lf(char* str);

// ---- string table: game text strings live in one Heap ----
struct Strings {
    Heap heap;
    int32_t active;
};
SMAC_POINTER_STRUCT(Strings);
SMAC_LAYOUT(Strings, 0x18);

GLOBAL_NATIVE(Strings, StringTable, 0x9B90D8);

ADDR(0x6168D0) Strings* THISCALL Strings_ctor(Strings* self);
// Allocates the table (size bytes) and stores "-Nil-" as its first string. Returns 0 on success.
ADDR(0x6168F0) int32_t THISCALL Strings_init(Strings* self, int32_t size);
ADDR(0x616950) int32_t THISCALL Strings_shutdown(Strings* self);
// Copies str into the table and returns the copy.
ADDR(0x616970) char* THISCALL Strings_put(Strings* self, const char* str);
// Validates a string pointer: pointers at or below the table start yield the table start
// ("-Nil-"), pointers past its end yield nullptr.
ADDR(0x6169A0) char* THISCALL Strings_get(Strings* self, const char* str);

// ---- interface labels (labels.txt, #labels) ----
GLOBAL_NATIVE(char**, TextLabels, 0x9B90F8);
GLOBAL(int32_t, TextLabelCount, 0x9B90FC);

ADDR(0x6169D0) void GAMEAPI labels_shutdown();
// Loads labels.txt into the string table. Returns 0 on success.
ADDR(0x616A00) int32_t GAMEAPI labels_init();

// Label string by index (Strings_get(TextLabels[id])).
inline char* label_get(int32_t id) { return Strings_get(&StringTable, TextLabels[id]); }
