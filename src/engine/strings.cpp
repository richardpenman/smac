// File lookup, string helpers, the string table and interface labels (see strings.h).
#include "engine/strings.h"

#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"

// ---- filefind ----

char* GAMEAPI filefind_cd_drive_letter() { return FilefindPaths.cd_path; }

void GAMEAPI filefind_set_alternate(const char* path) {
    FilefindPaths.alt_path[0] = 0;
    if (path) {
        if (!crt_strchr(path, ':') && *path != '\\') crt_strcat(FilefindPaths.alt_path, FilefindPaths.exe_dir);
        crt_strcat(FilefindPaths.alt_path, path);
    }
}

int32_t GAMEAPI filefind_init(const char* name, int32_t no_prompt) {
    char drive[12];
    char cwd[256];
    if (!name) return 0x10;
    FilefindPaths.alt_path[0] = 0;
    uint8_t* pop = BasePopAlloc();
    if (!pop) return 4;
    FilefindPaths.relative_path[0] = 0;
    crt_strcat(FilefindPaths.relative_path, name);
    FilefindPaths.cd_path[0] = 0;
    FilefindPaths.exe_dir[0] = 0;
    FilefindPaths.last_path[0] = 0;
    win32::GetCurrentDirectoryA(0x100, FilefindPaths.exe_dir);
    cwd[0] = 0;
    crt_strcat(cwd, FilefindPaths.exe_dir);
    crt_strcat(FilefindPaths.exe_dir, "\\");
    for (;;) {
        drive[0] = 0;
        crt_strcat(drive, "A:\\");
        for (int32_t i = 0; i < 26; i++) {
            if (win32::GetDriveTypeA(drive) == 5 /* DRIVE_CDROM */) {
                FilefindPaths.last_path[0] = 0;
                crt_strcat(FilefindPaths.last_path, drive);
                crt_strcat(FilefindPaths.last_path, name);
                if (win32::file_exists(FilefindPaths.last_path)) {
                    crt_strcat(FilefindPaths.cd_path, drive);
                    win32::SetCurrentDirectoryA(cwd);
                    BasePop_delete(pop);
                    return 0;
                }
            }
            drive[0]++;
        }
        if (no_prompt) {
            BasePop_delete(pop);
            return 0;
        }
        int32_t result = BasePop_start(pop, "jackal", "FILEFIND_NOCD", -1, 0, 0, 0);
        if (result) {
            BasePop_delete(pop);
            return result;
        }
        // (the GOG executable has the original's CD check after the popup patched out here)
        if (BasePop_choice(pop) != 1) break;  // 1: retry
    }
    win32::SetCurrentDirectoryA(cwd);
    BasePop_delete(pop);
    return 0;
}

char* GAMEAPI filefind_get(const char* name) {
    char* last = FilefindPaths.last_path;
    if (!name) return nullptr;
    if (name == last) return last;
    if (name[1] == ':') {
        last[0] = 0;
        crt_strcat(last, name);
        return win32::file_exists(last) ? last : nullptr;
    }
    if (FilefindPaths.alt_path[0]) {
        last[0] = 0;
        crt_strcat(last, FilefindPaths.alt_path);
        crt_strcat(last, name);
        if (win32::file_exists(last)) return last;
    }
    last[0] = 0;
    crt_strcat(last, FilefindPaths.exe_dir);
    crt_strcat(last, name);
    if (win32::file_exists(last)) return last;
    if (FilefindPaths.cd_path[0]) {
        last[0] = 0;
        crt_strcat(last, FilefindPaths.cd_path);
        crt_strcat(last, name);
        if (win32::file_exists(last)) return last;
    }
    return nullptr;
}

CrtFile* GAMEAPI env_open(const char* name, const char* mode) {
    const char* path = filefind_get(name);
    if (!path) path = name;
    return crt_fopen(path, mode);
}

// ---- string helpers ----

void GAMEAPI purge_leading(char* str) {
    // BUG(original): the scan result is discarded; the string is not modified.
    COMPAT_ORIGINAL(purge_leading_noop);
    while (*str == ' ') str++;
}

void GAMEAPI purge_trailing(char* str) {
    uint32_t len = crt_strlen(str);
    if (!len) return;
    char* p = str + (len - 1);
    if (p < str) return;
    while (*p == ' ') {
        *p = 0;
        if (p == str) return;
        p--;
        if (p < str) return;
    }
}

void GAMEAPI purge_spaces(char* str) {
    char tmp[512];
    uint32_t len = crt_strlen(str);
    if (len) {
        char* p = str + (len - 1);
        if (p >= str) {
            while (*p == ' ') {
                *p = 0;
                if (p == str) break;
                p--;
                if (p < str) break;
            }
        }
    }
    char* p = str;
    while (*p == ' ') p++;
    crt_strcpy(tmp, p);
    crt_strcpy(str, tmp);
}

void GAMEAPI kill_lf(char* str) {
    char* p = crt_strrchr(str, '\n');
    if (p) *p = 0;
}

void GAMEAPI add_lf(char* str) {
    uint32_t len = crt_strlen(str);
    str[len] = '\n';
    str[len + 1] = 0;
}

// ---- string table ----

Strings* THISCALL Strings_ctor(Strings* self) {
    Heap_ctor(&self->heap);
    self->active = 0;
    return self;
}

int32_t THISCALL Strings_init(Strings* self, int32_t size) {
    if (self->active) {
        Heap_free(&self->heap);
        self->active = 0;
    }
    if (Heap_init(&self->heap, size)) return 1;
    char* nil = (char*)Heap_get(&self->heap, (int32_t)crt_strlen("-Nil-") + 1);
    crt_strcpy(nil, "-Nil-");
    self->active = 1;
    return 0;
}

int32_t THISCALL Strings_shutdown(Strings* self) {
    Heap_free(&self->heap);
    self->active = 0;
    return 0;
}

char* THISCALL Strings_put(Strings* self, const char* str) {
    char* copy = (char*)Heap_get(&self->heap, (int32_t)crt_strlen(str) + 1);
    crt_strcpy(copy, str);
    return copy;
}

char* THISCALL Strings_get(Strings* self, const char* str) {
    uintptr_t p = (uintptr_t)str, base = (uintptr_t)self->heap.base;
    if (p <= base) return (char*)self->heap.base;
    if (base + (uint32_t)self->heap.base_size < p) return nullptr;
    return (char*)str;
}

// ---- labels ----

void GAMEAPI labels_shutdown() {
    if (TextLabels) {
        mem_free(TextLabels);
        TextLabels = nullptr;
    }
    TextLabelCount = 0;
}

int32_t GAMEAPI labels_init() {
    if (TextLabels) {
        mem_free(TextLabels);
        TextLabels = nullptr;
    }
    TextLabelCount = 0;
    if (text_open("labels", "labels")) return 1;
    text_get();
    TextLabelCount = text_item_number();
    // The original allocates 4 bytes per label pointer.
    TextLabels = (char**)mem_get((uint32_t)TextLabelCount * (uint32_t)sizeof(char*));
    if (!TextLabels) return 1;
    for (int32_t i = 0; i < TextLabelCount; i++) TextLabels[i] = text_string();
    text_close();
    return 0;
}
