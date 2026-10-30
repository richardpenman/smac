// Headless defaults of the network game's UI hooks (engine/net.h): no windows; OpenSMAC's
// ui/netwin.cpp replaces this area.
#include <cstdio>

#include "engine/net.h"
#include "engine/ui_bridge.h"

bool ui_netwin_visible() { return false; }
void ui_netwin_redraw() {}
void ui_netwin_cancel() {}
void ui_netwin_fill_player(uint8_t*, int32_t) {}
void ui_netwin_faction_data(const uint8_t*) {}
void ui_netwin_pick_pass(int32_t) {}
void ui_netwin_chat(int32_t, const char*) {}
void ui_main_chat(const char*, int32_t) {}
void ui_diplo_chat(const char*, int32_t) {}
int32_t ui_diplo_net(int32_t, int32_t, int32_t) { return 0; }
void ui_diplo_closed(int32_t) {}
void ui_multiwin_draw() {}
const uint8_t* ui_netwin_faction_blob() { return nullptr; }
bool net_trace_enabled() { return false; }
const char* ui_user_dir() { return "."; }
bool ui_escape_pressed() { return false; }
void ui_net_close_windows() {}
void ui_console_set_ai(int32_t) {}
void ui_console_set_view() {}
void ui_net_end_of_turn() {}
void ui_net_center(int32_t, int32_t) {}
void ui_net_clear_messages() {}
void ui_net_turn(const char*) {}
void ui_net_note_text(const char* text) { std::fprintf(stderr, "%s\n", text); }
void GAMEAPI do_all_tasks() {}
int32_t THISCALL AlphaNet_setup(uint8_t*, int32_t) { return 1; }
