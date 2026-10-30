// Text layout of the original UI's Buffer (see font_text.h).
#include <cstdio>
#include <cstdlib>
#include "ui/font_text.h"

#include <cstring>
#include <unordered_map>
#include <vector>

#include "platform/ui_platform.h"

int32_t FlyX, FlyY, FlyWidth, FlyUsed;
char FlyLine[1024];

BufferText& buffer_text(Buffer* self) {
    static std::unordered_map<const Buffer*, BufferText> table;
    return table[self];
}

namespace {

int32_t slen(const char* s) { return s ? (int32_t)std::strlen(s) : 0; }

// Font of a slot (the main font for an empty slot).
Font* slot_font(Buffer* self, int32_t slot) {
    Font* f = (slot >= 0 && slot < 4) ? self->fonts[slot] : nullptr;
    return f ? f : self->fonts[0];
}

// Font_height (inlined everywhere in the original): tmHeight + tmExternalLeading, or the font's
// own line height when field_0 is set.
int32_t font_line_height(const Font* f) {
    if (!f) return 0;
    return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0;
}

bool is_markup(char c, bool links) {
    return c == '{' || c == '}' || c == '[' || c == ']' || c == '$' || (links && c == '=');
}

// OPENSMAC_TEXTCHECK=1 (a test aid): the right edge of the box Buffer_write_strings wraps its text in
// (-1: none); a run whose ink passes it is reported ("text beyond its box").
int32_t g_box_right = -1;

// TextOut (TA_BASELINE) of one run in a palette colour, transparent background.
void text_out(Buffer* self, Font* font, int32_t x, int32_t baseline, const char* text, int32_t len, int32_t color) {
    std::vector<uint8_t> mask;
    int32_t w = 0, h = 0;
    if (!platform::font_render(font->handle, text, len, &mask, &w, &h)) return;
    static const bool check = std::getenv("OPENSMAC_TEXTCHECK") != nullptr;
    if (check && g_box_right >= 0) {
        int32_t ink = -1;  // the rightmost ink column
        for (int32_t j = 0; j < h; j++)
            for (int32_t i = w - 1; i > ink; i--)
                if (mask[(size_t)j * w + i]) ink = i;
        if (ink >= 0 && x + ink >= g_box_right)
            std::fprintf(stderr, "text beyond its box: ink to %d, box right %d: %.*s\n", x + ink, g_box_right, len, text);
    }
    int32_t top = baseline - font->ascent;
    const UiRect& c = self->clip;
    for (int32_t j = 0; j < h; j++) {
        int32_t y = top + j;
        if (y < c.top || y >= c.bottom || y < 0 || y >= self->height) continue;
        uint8_t* row = self->data + (size_t)y * self->stride;
        for (int32_t i = 0; i < w; i++) {
            int32_t px = x + i;
            if (mask[(size_t)j * w + i] && px >= c.left && px < c.right && px >= 0 && px < self->width)
                row[px] = (uint8_t)color;
        }
    }
}

// Buffer_write_raw_l (0x5DBD00) in the multi-font mode: font of the current write slot, colour of
// that slot unless it is -1, baseline at the largest ascent of the three fonts, shadow first.
// ("~" colour runs, flag +0x524, are not used by the reconstructed callers.)
int32_t write_raw(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len) {
    if (len == 0) return x;
    BufferText& bt = buffer_text(self);
    int32_t slot = self->font_index;
    int32_t font_slot = 0, color_slot = 0, ascent = 0;
    Font* f0 = self->fonts[0];
    if (!f0 || !f0->handle) return x;
    if (!bt.multi_font) {
        ascent = f0->ascent_above;
    } else {
        font_slot = self->fonts[slot] ? slot : 0;
        color_slot = self->text_color[slot] != -1 ? slot : 0;
        for (int32_t i = 0; i < 3; i++)
            if (self->fonts[i] && self->fonts[i]->ascent_above > ascent) ascent = self->fonts[i]->ascent_above;
    }
    Font* font = self->fonts[font_slot];
    int32_t baseline = y + ascent;
    if (self->shadow_color[color_slot] != -1)
        text_out(self, font, x + self->shadow_dx[color_slot], baseline + self->shadow_dy[color_slot], text, len,
                 self->shadow_color[color_slot]);
    text_out(self, font, x, baseline, text, len, self->text_color[color_slot]);
    return x + Font_width_2(font, text, len);
}

// The markup scan shared by the three multi-font functions: the next markup character from p
// (at most *len characters), or p + *len.
const char* scan(const char* p, int32_t* len, bool links) {
    while (*len && !is_markup(*p, links)) {
        p++;
        (*len)--;
    }
    return p;
}

// What a markup character at p does: the new font slot (-1: none, the character is text, as "{{",
// "}}", a "$" that is not "$LINK<" or "="). *skip_pair is set for doubled characters.
int32_t markup_slot(BufferText& bt, const char* p, int32_t current, bool* doubled) {
    *doubled = false;
    switch (*p) {
    case '$':
        if (!std::strncmp(p, "$LINK<", 6)) {
            bt.link_state = 1;
            return 3;
        }
        return -1;
    case '=': bt.link_state = 2; return -1;
    case '[':
        if (p[1] != '[') return 2;
        *doubled = true;
        return -1;
    case ']':
        if (p[1] != ']') return 0;
        *doubled = true;
        return -1;
    case '{':
        if (p[1] != '{') return 1;
        *doubled = true;
        return -1;
    case '}':
        if (p[1] != '}') return 0;
        *doubled = true;
        return -1;
    default: (void)current; return -1;
    }
}

// Line height of the main font (Buffer_text_line_height inlined).
int32_t line_height(Buffer* self) { return font_line_height(self->fonts[0]); }

// The common length clamp of the writers: min(len, strlen(text)).
int32_t clamp_len(const char* text, int32_t len) {
    int32_t n = slen(text);
    return n < len ? n : len;
}

}  // namespace

// ---- widths and line breaks ----

const char* THISCALL Font_find_line_break_l(Font* self, const char* text, int32_t* width, int32_t len) {
    int32_t rem = *width;
    const char* s = text;
    for (;;) {
        if (len == 0) {
            FlyUsed += *width - rem;
            *width = rem;
            return nullptr;
        }
        const char* sp = (const char*)std::memchr(s + 1, ' ', (size_t)len);
        if (!sp) break;
        int32_t n = (int32_t)(sp - s);
        if (slen(s) <= n) n = slen(s);
        rem -= platform::font_text_width(self->handle, s, n);
        if (s == text) {
            if (rem < 0) {
                *width = 0;
                return FlyUsed ? s : sp + 1;
            }
        } else if (rem < 0) {
            *width = 0;
            return s + 1;
        }
        len -= (int32_t)(sp - s);
        s = sp;
    }
    int32_t n = len;
    if (slen(s) <= n) n = slen(s);
    rem -= platform::font_text_width(self->handle, s, n);
    if (s == text) {
        if (rem < 0) {
            *width = 0;
            return FlyUsed ? s : nullptr;
        }
    } else if (rem < 0) {
        *width = 0;
        return s + 1;
    }
    FlyUsed += *width - rem;
    *width = rem;
    return nullptr;
}

const char* THISCALL Buffer_find_line_break_l(Buffer* self, const char* text, int32_t* width, int32_t len) {
    if (!width || !self->fonts[0]) return nullptr;
    BufferText& bt = buffer_text(self);
    if (!bt.multi_font) return Font_find_line_break_l(self->fonts[0], text, width, len);
    const char* seg = text;
    for (;;) {
        const char* p = scan(seg, &len, bt.break_font == 3);
        int32_t next = bt.break_font;
        if (len) {
            bool doubled;
            int32_t s = markup_slot(bt, p, bt.break_font, &doubled);
            if (s >= 0) next = s;
            else if (doubled) {
                p++;
                len--;
            }
        }
        const char* r = Font_find_line_break_l(slot_font(self, bt.break_font), seg, width, (int32_t)(p - seg));
        if (r) {
            bt.link_state = 0;
            // (r == seg tested first: the original reads the byte before the text there, same result.)
            if (r == seg || r[-1] == ' ') return r;
            // memrchr (0x628AF0): the last space before r
            for (const char* q = r; q != text; q--)
                if (*q == ' ') return q;
            return nullptr;
        }
        if (!len) break;
        if (bt.link_state == 1) {
            p += 5;
            len -= 5;
        }
        p++;
        seg = p;
        if (bt.link_state == 2) {
            const char* gt = std::strchr(p, '>');
            next = 0;
            if (gt) {
                len -= (int32_t)(gt + 1 - p);
                seg = gt + 1;
            }
            bt.link_state = 0;
        }
        bt.break_font = next;
        len--;
    }
    bt.link_state = 0;
    return nullptr;
}

int32_t THISCALL Buffer_text_width(Buffer* self, const char* text) {
    return text ? Buffer_text_width_2(self, text, slen(text)) : 0;
}

int32_t THISCALL Buffer_text_width_2(Buffer* self, const char* text, int32_t len) {
    if (!self->fonts[0]) return 0;
    BufferText& bt = buffer_text(self);
    if (!bt.multi_font) return Font_width_2(self->fonts[0], text, len);
    int32_t total = 0;
    const char* seg = text;
    for (;;) {
        const char* p = scan(seg, &len, bt.width_font == 3);
        if (!len) {
            bt.link_state = 0;
            return total + Font_width_2(slot_font(self, bt.width_font), seg, (int32_t)(p - seg));
        }
        bool doubled;
        int32_t next = bt.width_font;
        int32_t s = markup_slot(bt, p, bt.width_font, &doubled);
        if (s >= 0) next = s;
        else if (doubled) {
            p++;
            len--;
        }
        total += Font_width_2(slot_font(self, bt.width_font), seg, (int32_t)(p - seg));
        if (bt.link_state == 1) {
            p += 5;
            len -= 5;
        }
        p++;
        seg = p;
        if (bt.link_state == 2) {
            const char* gt = std::strchr(p, '>');
            next = 0;
            if (gt) {
                len -= (int32_t)(gt + 1 - p);
                seg = gt + 1;
            }
            bt.link_state = 0;
        }
        bt.width_font = next;
        len--;
    }
}

int32_t THISCALL Buffer_text_line_height(Buffer* self) { return line_height(self); }

void buffer_clear_links(Buffer* self) {
    BufferText& bt = buffer_text(self);
    bt.link_spots.clear();
    bt.links = 0;
}

const std::string* buffer_link_at(Buffer* self, int32_t x, int32_t y) {
    BufferText& bt = buffer_text(self);
    for (const BufferText::LinkSpot& l : bt.link_spots)
        if (x >= l.r.left && x < l.r.right && y >= l.r.top && y < l.r.bottom) return &l.target;
    return nullptr;
}

int32_t THISCALL Buffer_text_height(Buffer* self) { return self->fonts[0] ? self->fonts[0]->ascent_above : 0; }

// ---- writers ----

int32_t THISCALL Buffer_write_multi_font_raw_l(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len) {
    BufferText& bt = buffer_text(self);
    if (!bt.multi_font) return write_raw(self, text, x, y, len);
    if (self->font_index == 3 && bt.link_y != y) {
        // The link continues on another line: the part written so far is a spot of its own.
        bt.link_parts.push_back({bt.link_x, bt.link_y, bt.link_end_x, bt.link_y + line_height(self)});
        bt.link_x = x;
        bt.link_y = y;
    }
    const char* seg = text;
    for (;;) {
        const char* p = scan(seg, &len, self->font_index == 3);
        if (!len) {
            int32_t end = write_raw(self, seg, x, y, (int32_t)(p - seg));
            if (self->font_index == 3) bt.link_end_x = end;
            return end;
        }
        bool doubled;
        int32_t next = self->font_index;
        int32_t s = markup_slot(bt, p, self->font_index, &doubled);
        if (s == 3) bt.links++;
        if (s >= 0) next = s;
        else if (doubled) {
            p++;
            len--;
        }
        x = write_raw(self, seg, x, y, (int32_t)(p - seg));
        if (s == 3) {
            bt.link_x = x;
            bt.link_y = y;
            bt.link_parts.clear();
        }
        if (bt.link_state == 1) {
            p += 5;
            len -= 5;
        }
        p++;
        seg = p;
        if (bt.link_state == 2) {
            const char* gt = std::strchr(p, '>');
            if (gt) {
                // The link's spot: its text in the link font, from where it started.
                BufferText::LinkSpot spot;
                spot.r = {bt.link_x, bt.link_y, x, bt.link_y + line_height(self)};
                spot.target.assign(p, gt);
                bt.link_spots.push_back(spot);
                for (const UiRect& part : bt.link_parts) bt.link_spots.push_back({part, spot.target});
                bt.link_parts.clear();
                len -= (int32_t)(gt + 1 - p);
                seg = gt + 1;
            }
            next = 0;
            bt.link_state = 0;
        }
        self->font_index = next;
        len--;
    }
}

int32_t THISCALL Buffer_write_l_4(Buffer* self, const char* text, const UiRect* rect, int32_t len) {
    if (!text || !rect || !self->fonts[0] || !self->fonts[0]->handle) return 0;
    len = clamp_len(text, len);
    if (len <= 0) return 0;
    int32_t y = (rect->bottom - self->fonts[0]->ascent_above - rect->top) / 2 + rect->top;
    return Buffer_write_multi_font_raw_l(self, text, rect->left, y, len);
}

int32_t THISCALL Buffer_write_cent_l_5(Buffer* self, const char* text, int32_t x, int32_t y, int32_t w, int32_t len) {
    if (!text) return x;
    if (!self->fonts[0] || !self->fonts[0]->handle) return 3;
    len = clamp_len(text, len);
    if (len <= 0) return x;
    int32_t tw = Buffer_text_width_2(self, text, len);
    return Buffer_write_multi_font_raw_l(self, text, x + (w - tw) / 2, y, len);
}

int32_t THISCALL Buffer_write_right_l_6(Buffer* self, const char* text, const UiRect* rect, int32_t len) {
    if (!text || !rect) return 0;
    if (!self->fonts[0] || !self->fonts[0]->handle) return 3;
    len = clamp_len(text, len);
    if (len <= 0) return 0;
    int32_t tw = Buffer_text_width_2(self, text, len);
    return Buffer_write_multi_font_raw_l(
        self, text, rect->right - tw, (rect->bottom - self->fonts[0]->ascent_above - rect->top) / 2 + rect->top, len);
}

int32_t THISCALL Buffer_wrap_2(Buffer* self, const char* text, const UiRect* rect) {
    if (!text || !rect) return rect ? rect->top : 0;
    BufferText& bt = buffer_text(self);
    FlyUsed = 0;
    bt.lines = 0;
    bt.indent = bt.hang;
    int32_t x = rect->left, y = rect->top, w = rect->right - rect->left;
    const char* s = text;
    do {
        const char* next = nullptr;
        if (s) {
            FlyUsed = 0;
            int32_t width = w;
            next = Buffer_find_line_break_l(self, s, &width, slen(s));
            int32_t n = next ? (int32_t)(next - s) : slen(s);
            if (self->fonts[0] && self->fonts[0]->handle) {
                n = clamp_len(s, n);
                if (n > 0) Buffer_write_multi_font_raw_l(self, s, x, y, n);
            }
        }
        y += line_height(self);
        bt.lines++;
        if (bt.indent) {
            w -= bt.indent;
            x += bt.indent;
            bt.indent = 0;
        }
        s = next;
    } while (s);
    return y;
}

int32_t THISCALL Buffer_wrap_cent_4(Buffer* self, const char* text, int32_t x, int32_t y, int32_t w) {
    if (!text) return y;
    BufferText& bt = buffer_text(self);
    FlyUsed = 0;
    bt.lines = 0;
    const char* s = text;
    do {
        const char* next = nullptr;
        if (s) {
            FlyUsed = 0;
            int32_t width = w;
            next = Buffer_find_line_break_l(self, s, &width, slen(s));
            Buffer_write_cent_l_5(self, s, x, y, w, next ? (int32_t)(next - s) : slen(s));
        }
        bt.lines++;
        y += line_height(self);
        s = next;
    } while (s);
    return y;
}

int32_t THISCALL Buffer_wrap_cent_6(Buffer* self, const char* text, const UiRect* rect) {
    if (!text || !rect) return rect ? rect->top : 0;
    return Buffer_wrap_cent_4(self, text, rect->left, rect->top, rect->right - rect->left);
}

int32_t THISCALL Buffer_wrap_height(Buffer* self, const char* text, int32_t w) {
    if (!text) return 0;
    BufferText& bt = buffer_text(self);
    bt.lines = 0;
    FlyUsed = 0;
    bt.indent = bt.hang;
    int32_t h = 0;
    const char* s = text;
    do {
        int32_t width = w;
        s = Buffer_find_line_break_l(self, s, &width, slen(s));
        FlyUsed = 0;
        h += line_height(self);
        bt.lines++;
        if (bt.indent) {
            w -= bt.indent;
            bt.indent = 0;
        }
    } while (s);
    return h;
}

// ---- the flying writer ----

void THISCALL Buffer_wrap_height_flying(Buffer* self, const char* text, int32_t w) {
    BufferText& bt = buffer_text(self);
    bt.lines = 0;
    bt.indent = bt.hang;
    bt.link_state = 0;
    FlyY = 0;
    FlyUsed = 0;
    FlyWidth = w;
    Buffer_wrap_height_flying_2(self, text);
}

int32_t THISCALL Buffer_wrap_height_flying_2(Buffer* self, const char* text) {
    if (!FlyWidth) return 0;
    if (text && *text) {
        BufferText& bt = buffer_text(self);
        const char* s = text;
        for (;;) {
            int32_t width = FlyWidth - FlyUsed;
            s = Buffer_find_line_break_l(self, s, &width, slen(s));
            if (!s) break;
            bt.lines++;
            FlyUsed = 0;
            FlyY += line_height(self);
            if (bt.indent) {
                FlyWidth -= bt.indent;
                bt.indent = 0;
            }
        }
        FlyUsed += Buffer_text_width_2(self, " ", 1);  // the separator before the next string
    }
    return FlyY;
}

int32_t THISCALL Buffer_wrap_height_flying_flush(Buffer* self) {
    BufferText& bt = buffer_text(self);
    if (FlyUsed) {
        FlyY += line_height(self);
        bt.lines++;
    }
    FlyWidth = 0;
    self->font_index = 0;
    bt.width_font = 0;
    bt.break_font = 0;
    bt.link_state = 0;
    return FlyY;
}

void THISCALL Buffer_wrap_flying(Buffer* self, const char* text, int32_t x, int32_t y, int32_t w) {
    BufferText& bt = buffer_text(self);
    bt.indent = bt.hang;
    bt.lines = 0;
    FlyX = x;
    FlyUsed = 0;
    FlyY = y;
    FlyWidth = w;
    bt.link_state = 0;
    FlyLine[0] = 0;
    Buffer_wrap_flying_2(self, text);
}

int32_t THISCALL Buffer_wrap_flying_2(Buffer* self, const char* text) {
    if (!FlyWidth) return 0;
    if (!text || !*text) return FlyY;
    BufferText& bt = buffer_text(self);
    const char* s = text;
    for (;;) {
        if (FlyLine[0]) {
            std::strncat(FlyLine, " ", sizeof(FlyLine) - std::strlen(FlyLine) - 1);
            FlyUsed += Buffer_text_width_2(self, " ", 1);
        }
        const char* brk = s;
        if (FlyUsed < FlyWidth) {
            int32_t width = FlyWidth - FlyUsed;
            brk = Buffer_find_line_break_l(self, s, &width, slen(s));
        }
        if (!brk) break;
        size_t have = std::strlen(FlyLine), n = (size_t)(brk - s);
        if (have + n >= sizeof(FlyLine)) n = sizeof(FlyLine) - 1 - have;
        std::memcpy(FlyLine + have, s, n);
        FlyLine[have + n] = 0;
        int32_t len = slen(FlyLine);
        if (self->fonts[0] && self->fonts[0]->handle && len)
            Buffer_write_multi_font_raw_l(self, FlyLine, FlyX, FlyY, len);
        FlyLine[0] = 0;
        FlyUsed = 0;
        bt.lines++;
        FlyY += line_height(self);
        s = brk;
        if (bt.indent) {
            FlyWidth -= bt.indent;
            FlyX += bt.indent;
            bt.indent = 0;
        }
    }
    std::strncat(FlyLine, s, sizeof(FlyLine) - std::strlen(FlyLine) - 1);
    return FlyY;
}

int32_t THISCALL Buffer_wrap_flying_flush(Buffer* self) {
    BufferText& bt = buffer_text(self);
    if (FlyLine[0]) {
        int32_t len = slen(FlyLine);
        if (self->fonts[0] && self->fonts[0]->handle && len)
            Buffer_write_multi_font_raw_l(self, FlyLine, FlyX, FlyY, len);
        FlyY += line_height(self);
        bt.lines++;
    }
    FlyLine[0] = 0;
    FlyWidth = 0;
    self->font_index = 0;
    bt.width_font = 0;
    bt.break_font = 0;
    bt.link_state = 0;
    return FlyY;
}

int32_t THISCALL Buffer_write_strings(Buffer* self, const TextLine* lines, int32_t count, int32_t x, int32_t y,
                                      int32_t w, int32_t) {
    if (!lines || count <= 0) return y;
    BufferText& bt = buffer_text(self);
    struct BoxCheck {  // OPENSMAC_TEXTCHECK: the box of this text
        BoxCheck(int32_t right) { g_box_right = right; }
        ~BoxCheck() { g_box_right = -1; }
    } box_check(x + w);
    auto start = [&](const char* text, int32_t sy) {
        bt.indent = bt.hang;
        bt.lines = 0;
        FlyX = x;
        FlyY = sy;
        FlyWidth = w;
        FlyUsed = 0;
        bt.link_state = 0;
        FlyLine[0] = 0;
        Buffer_wrap_flying_2(self, text);
    };
    int32_t bottom = y;
    // The first entry.
    int32_t mode = lines[0].mode;
    if (mode == 3) {
        y += line_height(self);
        start(nullptr, y);
    } else if (mode == 2) {
        const char* s = lines[0].text;
        if (s) {
            FlyUsed = 0;
            bt.lines = 0;
            for (;;) {
                FlyUsed = 0;
                int32_t width = w;
                const char* next = Buffer_find_line_break_l(self, s, &width, slen(s));
                Buffer_write_cent_l_5(self, s, x, y, w, next ? (int32_t)(next - s) : slen(s));
                bt.lines++;
                y += line_height(self);
                if (!next) break;
                s = next;
            }
        }
        start(nullptr, y);
    } else {
        start(lines[0].text, y);
    }
    bottom = y;
    for (int32_t i = 1; i < count; i++) {
        if (bottom > self->height) break;  // below the canvas
        mode = lines[i].mode;
        if (mode == 0) {
            Buffer_wrap_flying_2(self, lines[i].text);
            continue;
        }
        // A new line: the pending text is written first.
        if (FlyLine[0]) {
            int32_t len = slen(FlyLine);
            if (self->fonts[0] && self->fonts[0]->handle && len)
                Buffer_write_multi_font_raw_l(self, FlyLine, FlyX, FlyY, len);
            FlyY += line_height(self);
            bt.lines++;
        }
        FlyLine[0] = 0;
        FlyWidth = 0;
        self->font_index = 0;
        bt.width_font = 0;
        bt.break_font = 0;
        bt.link_state = 0;
        if (bottom <= FlyY) bottom = FlyY;
        if (mode == 3) {
            bottom += line_height(self);
            Buffer_wrap_flying(self, nullptr, x, bottom, w);
        } else if (mode == 2) {
            bottom = Buffer_wrap_cent_4(self, lines[i].text, x, bottom, w);
            Buffer_wrap_flying(self, nullptr, x, bottom, w);
        } else {
            Buffer_wrap_flying(self, lines[i].text, x, bottom, w);
        }
    }
    int32_t end = Buffer_wrap_flying_flush(self);
    return bottom <= end ? end : bottom;
}

int32_t buffer_strings_height(Buffer* self, const TextLine* lines, int32_t count, int32_t w) {
    // BasePop_calculate_dimensions' measuring loop.
    int32_t total = 0;
    if (count <= 0) return 0;
    auto first = [&](const TextLine& l) {
        if (l.mode == 3) {
            total += line_height(self);
            Buffer_wrap_height_flying(self, nullptr, w);
        } else if (l.mode == 2) {
            total += Buffer_wrap_height(self, l.text, w);
            Buffer_wrap_height_flying(self, nullptr, w);
        } else {
            Buffer_wrap_height_flying(self, l.text, w);
        }
    };
    first(lines[0]);
    for (int32_t i = 1; i < count; i++) {
        if (lines[i].mode == 0) {
            Buffer_wrap_height_flying_2(self, lines[i].text);
            continue;
        }
        total += Buffer_wrap_height_flying_flush(self);
        first(lines[i]);
    }
    total += Buffer_wrap_height_flying_flush(self);
    return total;
}

// ---- text colours of the other font slots ----

void THISCALL Buffer_set_text_color2(Buffer* self, int32_t color, int32_t shadow, int32_t dx, int32_t dy) {
    self->text_color[1] = color;
    self->shadow_color[1] = shadow;
    self->shadow_dx[1] = dx;
    self->shadow_dy[1] = dy;
}

void THISCALL Buffer_set_text_color3(Buffer* self, int32_t color, int32_t shadow, int32_t dx, int32_t dy) {
    self->text_color[2] = color;
    self->shadow_color[2] = shadow;
    self->shadow_dx[2] = dx;
    self->shadow_dy[2] = dy;
}

void THISCALL Buffer_set_text_color_hyper(Buffer* self, int32_t color, int32_t shadow, int32_t dx, int32_t dy) {
    self->text_color[3] = color;
    self->shadow_color[3] = shadow;
    self->shadow_dx[3] = dx;
    self->shadow_dy[3] = dy;
}
