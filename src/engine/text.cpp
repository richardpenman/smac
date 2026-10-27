// Text file parsing (see text.h).
#include "engine/text.h"

#include "engine/compat.h"
#include "engine/log.h"
#include "engine/strings.h"
#include "engine/util.h"
#include "platform/win32.h"

// ---- static initialization ----

void GAMEAPI text_set_get_ptr() { TextBufferGetPtr = TextBuffer.buffer; }

void GAMEAPI text_set_item_ptr() { TextBufferItemPtr = TextBuffer.item; }

void text_static_init() {
    // 0x5FD400: buffers of the global Text object
    TextBuffer.filename[0] = 0;
    TextBuffer.position = nullptr;
    TextBuffer.file = nullptr;
    TextBuffer.buffer = nullptr;
    TextBuffer.item = nullptr;
    TextBuffer.buffer = (char*)mem_get(0x200);
    if (TextBuffer.buffer) TextBuffer.item = (char*)mem_get(0x200);
    text_set_get_ptr();
    text_set_item_ptr();
    // 0x5FD4E0: TextIndex array constructor
    for (TextIndex& index : TextIndexes) TextIndex_ctor(&index);
}

// ---- global Text object ----

void GAMEAPI text_close() {
    if (TextBuffer.file) {
        crt_fclose(TextBuffer.file);
        TextBuffer.file = nullptr;
    }
}

int32_t GAMEAPI text_open(const char* filename, const char* section) {
    return Text_open(&TextBuffer, filename, section);
}

char* GAMEAPI text_get() {
    if (crt_feof(TextBuffer.file)) {
        *TextBuffer.buffer = 0;
        return nullptr;
    }
    if (!crt_fgets(TextBuffer.buffer, 0x1FF, TextBuffer.file)) {
        *TextBuffer.buffer = 0;
        return TextBuffer.buffer;
    }
    kill_lf(TextBuffer.buffer);
    purge_spaces(TextBuffer.buffer);
    TextBuffer.position = TextBuffer.buffer;
    return TextBuffer.buffer;
}

char* GAMEAPI text_string() {
    if (crt_feof(TextBuffer.file)) {
        *TextBuffer.buffer = 0;
        // BUG(original): stores a null string (Strings_put(nullptr) reads address 0).
        return Strings_put(&StringTable, g_compat.text_null_string ? nullptr : TextBuffer.buffer);
    }
    if (!crt_fgets(TextBuffer.buffer, 0x1FF, TextBuffer.file)) {
        *TextBuffer.buffer = 0;
        return Strings_put(&StringTable, TextBuffer.buffer);
    }
    kill_lf(TextBuffer.buffer);
    purge_spaces(TextBuffer.buffer);
    TextBuffer.position = TextBuffer.buffer;
    return Strings_put(&StringTable, TextBuffer.buffer);
}

// Copies the next comma-separated item of the global Text object into its item buffer.
static char* next_item() {
    char* out = TextBuffer.item;
    while (*TextBuffer.position && *TextBuffer.position != ',') *out++ = *TextBuffer.position++;
    *out = 0;
    if (*TextBuffer.position) TextBuffer.position++;
    purge_spaces(TextBuffer.item);
    return TextBuffer.item;
}

char* GAMEAPI text_item() { return next_item(); }

char* GAMEAPI text_item_string() { return Strings_put(&StringTable, next_item()); }

int32_t GAMEAPI text_item_number() { return stoi(next_item()); }

int32_t GAMEAPI text_item_binary() { return btoi(next_item()); }

int32_t GAMEAPI text_item_hex() { return htoi(next_item()); }

// ---- Text methods ----

Text* THISCALL Text_ctor(Text* self, int32_t size) {
    self->filename[0] = 0;
    self->position = nullptr;
    self->file = nullptr;
    self->buffer = nullptr;
    self->item = nullptr;
    self->buffer = (char*)mem_get((uint32_t)size);
    if (self->buffer) self->item = (char*)mem_get((uint32_t)size);
    return self;
}

int32_t THISCALL Text_init(Text* self, int32_t size) {
    if (self->file) {
        crt_fclose(self->file);
        self->file = nullptr;
    }
    if (self->buffer) {
        mem_free(self->buffer);
        self->buffer = nullptr;
    }
    if (self->item) {
        mem_free(self->item);
        self->item = nullptr;
    }
    self->buffer = (char*)mem_get((uint32_t)size);
    if (!self->buffer) return 4;
    self->item = (char*)mem_get((uint32_t)size);
    return self->item ? 0 : 4;
}

void THISCALL Text_shutdown(Text* self) {
    if (self->file) {
        crt_fclose(self->file);
        self->file = nullptr;
    }
    if (self->buffer) {
        mem_free(self->buffer);
        self->buffer = nullptr;
    }
    if (self->item) {
        mem_free(self->item);
        self->item = nullptr;
    }
}

void THISCALL Text_close(Text* self) {
    if (self->file) {
        crt_fclose(self->file);
        self->file = nullptr;
    }
}

int32_t THISCALL Text_open(Text* self, const char* filename, const char* section) {
    char header[80];
    bool rewind_once = false;
    if (filename) {
        crt_strcpy(self->filename, filename);
        if (!crt_strchr(self->filename, '.')) crt_strcat(self->filename, ".txt");
        if (self->file) {
            crt_fclose(self->file);
            self->file = nullptr;
        }
        self->file = env_open(self->filename, "rt");
        if (!self->file) return 1;
        crt_strcpy(self->filepath, FilefindPaths.last_path);
    } else if (self->file) {
        rewind_once = true;  // continue in the open file, wrapping around once
    } else {
        self->file = env_open(self->filename, "rt");
        if (!self->file) return 1;
        crt_strcpy(self->filepath, FilefindPaths.last_path);
    }
    if (!section) return 0;
    crt_strcpy(header, "#");
    crt_strcat(header, section);
    for (TextIndex& index : TextIndexes) {
        if (index.count) {
            int32_t offset = TextIndex_search_index(&index, self->filename, section);
            if (offset >= 0) {
                log_say("Seeking to", section, offset, 0, 0);
                crt_fseek(self->file, offset, 0);
                rewind_once = true;
                break;
            }
        }
    }
    for (;;) {
        if (crt_feof(self->file)) {
            if (!rewind_once) break;
            rewind_once = false;
            crt_rewind(self->file);
        }
        if (!crt_fgets(self->buffer, 0x1FF, self->file)) break;
        kill_lf(self->buffer);
        purge_spaces(self->buffer);
        if (!crt_stricmp(header, self->buffer)) {
            self->position = self->buffer;
            return 0;
        }
    }
    if (self->file) {
        crt_fclose(self->file);
        self->file = nullptr;
    }
    return 1;
}

char* THISCALL Text_get(Text* self) {
    if (crt_feof(self->file)) {
        *self->buffer = 0;
        return nullptr;
    }
    if (!crt_fgets(self->buffer, 0x1FF, self->file)) {
        *self->buffer = 0;
        return self->buffer;
    }
    kill_lf(self->buffer);
    purge_spaces(self->buffer);
    self->position = self->buffer;
    return self->buffer;
}

char* THISCALL Text_string(Text* self) {
    if (crt_feof(self->file)) {
        *self->buffer = 0;
        // BUG(original): see text_string.
        return Strings_put(&StringTable, g_compat.text_null_string ? nullptr : self->buffer);
    }
    if (!crt_fgets(self->buffer, 0x1FF, self->file)) {
        *self->buffer = 0;
        return Strings_put(&StringTable, self->buffer);
    }
    kill_lf(self->buffer);
    purge_spaces(self->buffer);
    self->position = self->buffer;
    return Strings_put(&StringTable, self->buffer);
}

char* THISCALL Text_item(Text* self) {
    char* out = self->item;
    while (*self->position && *self->position != ',') *out++ = *self->position++;
    *out = 0;
    if (*self->position) self->position++;
    purge_spaces(self->item);
    return self->item;
}

char* THISCALL Text_item_string(Text* self) { return Strings_put(&StringTable, Text_item(self)); }

int32_t THISCALL Text_item_number(Text* self) { return stoi(Text_item(self)); }

int32_t THISCALL Text_item_binary(Text* self) { return btoi(Text_item(self)); }

int32_t THISCALL Text_item_hex(Text* self) { return htoi(Text_item(self)); }

// ---- TextIndex ----

TextIndex* THISCALL TextIndex_ctor(TextIndex* self) {
    Heap_ctor(&self->heap);
    self->count = 0;
    self->filename[0] = 0;
    return self;
}

void THISCALL TextIndex_make_index(TextIndex* self, const char* filename) {
    Heap_free(&self->heap);
    self->count = 0;
    crt_strcpy(self->filename, filename);
    if (!crt_strchr(self->filename, '.')) crt_strcat(self->filename, ".txt");
    const char* path = filefind_get(self->filename);
    uint32_t size = 0;
    uint8_t* data = win32::read_file(path ? path : self->filename, &size);
    if (!data) return;
    const uint8_t* end = data + size;
    Heap_init(&self->heap, 0x8000);
    for (const uint8_t* line = data; line < end;) {
        const uint8_t* nl = (const uint8_t*)crt_memchr(line, '\n', (uint32_t)(end - line));
        if (!nl) break;
        // the line is copied with its CR LF and without a terminator; kill_lf cuts it
        crt_memcpy(TextBufferGetPtr, line, (uint32_t)(nl - line + 1));
        kill_lf(TextBufferGetPtr);
        purge_spaces(TextBufferGetPtr);
        if (*TextBufferGetPtr == '#' && crt_isupper((int8_t)TextBufferGetPtr[1])) {
            // entries are packed (offset + name), so the offset is usually unaligned
            char* entry = (char*)Heap_get(&self->heap, (int32_t)crt_strlen(TextBufferGetPtr + 1) + 5);
            int32_t offset = (int32_t)(line - data);
            crt_memcpy(entry, &offset, 4);
            crt_strcpy(entry + 4, TextBufferGetPtr + 1);
            self->count++;
        }
        line = nl + 1;
    }
    Heap_squeeze(&self->heap, 1);
    win32::free_file(data);
}

int32_t THISCALL TextIndex_search_index(TextIndex* self, const char* filename, const char* section) {
    char name[512];
    crt_strcpy(name, filename);
    if (!crt_strchr(name, '.')) crt_strcat(name, ".txt");
    if (crt_stricmp(name, self->filename)) return -1;
    if (*section == '#') section++;
    int32_t left = self->count;
    char* entry = (char*)self->heap.base;
    for (;;) {
        char* key = entry + 4;
        if (!crt_stricmp(section, key)) {
            int32_t offset;
            crt_memcpy(&offset, entry, 4);  // unaligned
            return offset;
        }
        while (*key) key++;
        entry = key + 1;
        if (!--left) return -1;
    }
}

void GAMEAPI text_make_index(const char* filename) {
    for (TextIndex& index : TextIndexes) {
        if (!index.count) {
            TextIndex_make_index(&index, filename);
            return;
        }
    }
}

int32_t GAMEAPI text_search_index(const char* filename, const char* section) {
    for (TextIndex& index : TextIndexes) {
        if (index.count) {
            int32_t offset = TextIndex_search_index(&index, filename, section);
            if (offset >= 0) return offset;
        }
    }
    return -1;
}

void GAMEAPI text_clear_index() {
    for (TextIndex& index : TextIndexes) {
        if (index.count) {
            Heap_free(&index.heap);
            index.count = 0;
        }
    }
}
