#include "engine/log.h"

#include <cstdio>

static bool log_active() { return LogFileName && !LogDisabled && !LogSuspended; }

void GAMEAPI log_reset() {
    if (FILE* f = std::fopen(LogFileName, "w")) std::fclose(f);
}

void GAMEAPI log_say(const char* label, const char* text, int32_t v1, int32_t v2, int32_t v3) {
    if (!log_active()) return;
    if (FILE* f = std::fopen(LogFileName, "a")) {
        if (text) std::fprintf(f, "%s %s %d %d %d\n", label, text, v1, v2, v3);
        else std::fprintf(f, "%s %d %d %d\n", label, v1, v2, v3);
        std::fclose(f);
    }
}

void GAMEAPI log_say_values(const char* label, int32_t v1, int32_t v2, int32_t v3) {
    if (!log_active()) return;
    if (FILE* f = std::fopen(LogFileName, "a")) {
        std::fprintf(f, "%s %d %d %d\n", label, v1, v2, v3);
        std::fclose(f);
    }
}

void GAMEAPI log_say_hex(const char* label, int32_t v1, int32_t v2, int32_t v3) {
    if (!log_active()) return;
    if (FILE* f = std::fopen(LogFileName, "a")) {
        std::fprintf(f, "%s %04x %04x %04x\n", label, (uint32_t)v1, (uint32_t)v2, (uint32_t)v3);
        std::fclose(f);
    }
}

void GAMEAPI log_set_state(int32_t enabled) { LogDisabled = enabled == 0; }
