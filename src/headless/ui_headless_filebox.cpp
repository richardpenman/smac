// Headless UI area "filebox": the save / load file dialogs (FileDialog, FileBox, FileList;
// engine/ui_bridge.h) of save_game and load_game.
// Replaced in smacx by src/ui/console_filebox.cpp.
#include <cstring>

#include "engine/ui_bridge.h"

// ---- file dialogs: nothing is shown; every dialog is cancelled (an unattended observer neither
// saves nor loads through the menus) ----
void THISCALL FileDialog_ctor(uint8_t* self) { std::memset(self, 0, kFileDialogSize); }
void THISCALL FileDialog_dtor(uint8_t*) {}
char* THISCALL FileDialog_save(uint8_t*) { return nullptr; }
char* THISCALL FileDialog_open(uint8_t*) { return nullptr; }
int32_t THISCALL FileDialog_add_filter(uint8_t*, const char*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_ext(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_name(uint8_t*, const char*) { return 0; }
void THISCALL FileBox_ctor(uint8_t* self) { std::memset(self, 0, kFileBoxSize); }
void THISCALL FileBox_init(uint8_t*) {}
void THISCALL FileBox_close(uint8_t*) {}
char* THISCALL FileBox_save(uint8_t*) { return nullptr; }
char* THISCALL FileBox_load(uint8_t*) { return nullptr; }
int32_t THISCALL FileList_set_path(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileList_add_ext(uint8_t*, const char*) { return 0; }
void THISCALL FileList_close(uint8_t*) {}
void ui_filebox_destroy(uint8_t*) {}
