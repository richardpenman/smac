// Debug log (original module around 0x626040). Appends lines to a log file when a log file name
// is configured and logging is enabled.
#pragma once
#include "engine/memmap.h"

GLOBAL_NATIVE(const char*, LogFileName, 0x9BBFF8);
GLOBAL(int32_t, LogDisabled, 0x9BBFFC);
GLOBAL(int32_t, LogSuspended, 0x9BC004);

ADDR(0x626230) void GAMEAPI log_reset();
ADDR(0x626250) void GAMEAPI log_say(const char* label, const char* text, int32_t v1, int32_t v2, int32_t v3);
ADDR(0x6262F0) void GAMEAPI log_say_values(const char* label, int32_t v1, int32_t v2, int32_t v3);
// Like log_say_values with the values in hex ("%s %04x %04x %04x").
ADDR(0x6263F0) void GAMEAPI log_say_hex(const char* label, int32_t v1, int32_t v2, int32_t v3);
ADDR(0x626450) void GAMEAPI log_set_state(int32_t enabled);
