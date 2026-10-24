// Text file parsing (original module text, 0x5FD4C0-0x5FE2A0).
//
// Game text files are line based: a section starts with a "#NAME" line; lines hold
// comma-separated items. The global Text object (TextBuffer) is used through the text_* free
// functions; other Text objects use the Text_* methods. TextIndex objects cache the file offsets
// of all section headers of a file. Text is kept as Windows-1252 bytes.
#pragma once
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/heap.h"

struct Text {
    char filename[80];
    char filepath[256];  // full path of the open file
    char* position;      // parse position in buffer
    CrtFile* file;
    char* buffer;        // current line (0x200 bytes)
    char* item;          // current item (0x200 bytes)
};
SMAC_POINTER_STRUCT(Text);
SMAC_LAYOUT(Text, 0x160);

struct TextIndex {
    char filename[256];
    int32_t count;       // number of section headers
    Heap heap;           // entries: {int32_t file offset; char name[]} (name without '#')
};
SMAC_POINTER_STRUCT(TextIndex);
SMAC_LAYOUT(TextIndex, 0x118);

GLOBAL_NATIVE(Text, TextBuffer, 0x9B7BA0);
GLOBAL_NATIVE(char*, TextBufferGetPtr, 0x9B7D00);   // scratch line buffer (= TextBuffer.buffer)
GLOBAL_NATIVE(char*, TextBufferItemPtr, 0x9B7D04);  // last item (= TextBuffer.item)
GLOBAL_NATIVE(TextIndex[4], TextIndexes, 0x9B7D08);

// Static initializers of the module (run by the CRT before WinMain).
ADDR(0x5FD4C0) void GAMEAPI text_set_get_ptr();
ADDR(0x5FD4D0) void GAMEAPI text_set_item_ptr();

// ---- global Text object ----
ADDR(0x5FD530) void GAMEAPI text_close();
// Opens file (".txt" appended when it has no extension) and positions after the line "#section"
// (case-insensitive; section may be null). Returns 0 on success, 1 on failure.
ADDR(0x5FD550) int32_t GAMEAPI text_open(const char* filename, const char* section);
// Reads the next line (LF removed, blanks trimmed). Returns nullptr at end of file.
ADDR(0x5FD570) char* GAMEAPI text_get();
// Reads the next line into the string table and returns the copy.
ADDR(0x5FD5E0) char* GAMEAPI text_string();
// Next comma-separated item of the current line (blanks trimmed).
ADDR(0x5FD670) char* GAMEAPI text_item();
// Next item copied into the string table.
ADDR(0x5FD6D0) char* GAMEAPI text_item_string();
// Next item as a number (stoi: decimal, 0b binary, 0x hex, 0d decimal).
ADDR(0x5FD740) int32_t GAMEAPI text_item_number();
// Next item as a binary number.
ADDR(0x5FD7A0) int32_t GAMEAPI text_item_binary();
ADDR(0x5FD800) int32_t GAMEAPI text_item_hex();

// ---- Text methods ----
// Constructor with buffers of `size` bytes (0x5FD880).
ADDR(0x5FD880) Text* THISCALL Text_ctor(Text* self, int32_t size);
// (Re)allocates the buffers. Returns 0 on success, 4 if out of memory.
ADDR(0x5FD8D0) int32_t THISCALL Text_init(Text* self, int32_t size);
ADDR(0x5FD970) void THISCALL Text_shutdown(Text* self);
ADDR(0x5FD9D0) void THISCALL Text_close(Text* self);
// See text_open; a null filename reuses the open file (searching from the current position,
// wrapping around once).
ADDR(0x5FDA00) int32_t THISCALL Text_open(Text* self, const char* filename, const char* section);
ADDR(0x5FDC10) char* THISCALL Text_get(Text* self);
ADDR(0x5FDC80) char* THISCALL Text_string(Text* self);
ADDR(0x5FDD10) char* THISCALL Text_item(Text* self);
ADDR(0x5FDD80) char* THISCALL Text_item_string(Text* self);
ADDR(0x5FDDF0) int32_t THISCALL Text_item_number(Text* self);
ADDR(0x5FDE60) int32_t THISCALL Text_item_binary(Text* self);
ADDR(0x5FDED0) int32_t THISCALL Text_item_hex(Text* self);

// ---- TextIndex ----
// Constructor (0x5FDF40).
ADDR(0x5FDF40) TextIndex* THISCALL TextIndex_ctor(TextIndex* self);
// Indexes the "#NAME" headers (NAME starting with an upper-case letter) of a file.
ADDR(0x5FDF80) void THISCALL TextIndex_make_index(TextIndex* self, const char* filename);
// File offset of the header "#section" if this index covers filename, else -1.
ADDR(0x5FE120) int32_t THISCALL TextIndex_search_index(TextIndex* self, const char* filename, const char* section);
// Indexes filename into the first free TextIndexes slot.
ADDR(0x5FE1F0) void GAMEAPI text_make_index(const char* filename);
ADDR(0x5FE230) int32_t GAMEAPI text_search_index(const char* filename, const char* section);
ADDR(0x5FE270) void GAMEAPI text_clear_index();

// Native builds: the module's static initializers (0x5FD400, 0x5FD4C0, 0x5FD4D0, 0x5FD4E0).
void text_static_init();
