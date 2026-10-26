// Graphics classes of the original UI (see gfx.h).
#include "ui/font_text.h"
#include "ui/gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/prefs.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"

Palette GamePalette;

// ---- Palette ------------------------------------------------------------------------------------

// The 20 static colours of the Windows system palette (indices 0..9 and 246..255), which an 8-bit
// game palette keeps.
static const uint8_t kStaticColors[20][3] = {
    {0, 0, 0},       {128, 0, 0},     {0, 128, 0},     {128, 128, 0},   {0, 0, 128},
    {128, 0, 128},   {0, 128, 128},   {192, 192, 192}, {192, 220, 192}, {166, 202, 240},
    {255, 251, 240}, {160, 160, 164}, {128, 128, 128}, {255, 0, 0},     {0, 255, 0},
    {255, 255, 0},   {0, 0, 255},     {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};

void GAMEAPI load_palette() {
    Buffer canvas;
    Buffer_ctor(&canvas);
    int32_t gamma = prefs_get_2("Gamma Correction", 100, 0);
    for (int32_t i = 0; i < 10; i++) std::memcpy(GamePalette.rgb[i], kStaticColors[i], 3);
    for (int32_t i = 0; i < 10; i++) std::memcpy(GamePalette.rgb[246 + i], kStaticColors[10 + i], 3);
    Buffer_load_pcx(&canvas, "PALETTE.PCX", &GamePalette, 10, 0xEC);
    // FUN_005C8E60 -> FUN_005C95E0: gamma correction of the game colours 10..245, also at 100%:
    // each component c becomes (int)(clamp(pow(c / 235, 1 / gamma), 0, 1) * 235.99), so 236..255
    // become 235.
    double inv = 1.0 / (double)(float)((float)gamma * 0.01);
    for (int32_t i = 10; i < 246; i++) {
        for (int32_t c = 0; c < 3; c++) {
            double v = std::pow((double)GamePalette.rgb[i][c] * 0.00425531914893617, inv);
            if (v < 0.0) v = 0.0;
            else if (v > 1.0) v = 1.0;
            GamePalette.rgb[i][c] = (uint8_t)(int32_t)(v * 235.99);
        }
    }
    // Palette_init: entries 8, 9, 246 and 247 are PC_EXPLICIT system colour references.
    for (int32_t i = 0; i < 2; i++) {
        GamePalette.rgb[8 + i][0] = (uint8_t)(8 + i);
        GamePalette.rgb[8 + i][1] = GamePalette.rgb[8 + i][2] = 0;
        GamePalette.rgb[246 + i][0] = (uint8_t)(0xF5 + i);
        GamePalette.rgb[246 + i][1] = GamePalette.rgb[246 + i][2] = 0;
    }
}

// ---- palette cycles -----------------------------------------------------------------------------

namespace {
struct PaletteCycle {
    int32_t id = -1;
    uint8_t first = 0, count = 0;
    int32_t period = 0;
    bool running = false;
    uint32_t next = 0;
};
PaletteCycle g_cycles[5];
// The slot of `id`: its own, or the first free one (the original's search; 5 when full).
int32_t cycle_slot(int32_t id) {
    int32_t i = 0;
    for (; i < 5; i++)
        if (g_cycles[i].id == id || g_cycles[i].id == -1) break;
    return i;
}
}  // namespace

int32_t THISCALL Palette_init_cycle(Palette* self, int32_t id, int32_t first, int32_t count, int32_t period) {
    first &= 0xFF;
    if (first < 10 || 0xF6 - first < count || id == -1) return 3;
    if (self != &GamePalette) return 7;
    int32_t i = cycle_slot(id);
    if (i >= 5) i = 4;  // (the original then writes the slot past the table)
    PaletteCycle& c = g_cycles[i];
    c.id = id;
    c.count = (uint8_t)count;
    c.first = (uint8_t)first;
    c.period = period;
    c.running = false;
    return 0;
}

int32_t THISCALL Palette_start_cycle(Palette*, int32_t id) {
    int32_t i = cycle_slot(id);
    if (i >= 5 || g_cycles[i].id != id) return 0xB;
    g_cycles[i].running = true;  // Time_start_3
    g_cycles[i].next = platform::ticks_ms() + (uint32_t)g_cycles[i].period;
    return 0;
}

void palette_cycle_rotate(Palette* self, int32_t id) {
    int32_t i = cycle_slot(id);
    if (i >= 5 || g_cycles[i].id != id) return;
    int32_t first = g_cycles[i].first, n = g_cycles[i].count;
    uint8_t keep[3] = {self->rgb[first][0], self->rgb[first][1], self->rgb[first][2]};
    for (int32_t k = 0; k < n - 1; k++)
        for (int32_t c = 0; c < 3; c++) self->rgb[first + k][c] = self->rgb[first + k + 1][c];
    for (int32_t c = 0; c < 3; c++) self->rgb[first + n - 1][c] = keep[c];
}

bool palette_cycles_tick(uint32_t now) {
    bool changed = false;
    for (PaletteCycle& c : g_cycles) {
        if (c.id == -1 || !c.running || c.period <= 0 || (int32_t)(now - c.next) < 0) continue;
        palette_cycle_rotate(&GamePalette, c.id);
        c.next = now + (uint32_t)c.period;
        changed = true;
    }
    return changed;
}

void palette_cycles_step(int32_t n) {
    for (const PaletteCycle& c : g_cycles) {
        if (c.id == -1) continue;
        if (c.running)
            for (int32_t k = 0; k < n; k++) palette_cycle_rotate(&GamePalette, c.id);
        std::string line;
        for (int32_t e = c.first; e < c.first + c.count; e++) {
            char b[24];
            std::snprintf(b, sizeof(b), " %02X%02X%02X", GamePalette.rgb[e][0], GamePalette.rgb[e][1], GamePalette.rgb[e][2]);
            line += b;
        }
        std::fprintf(stderr, "palette cycle %d: entries %d..%d every %d ms:%s\n", c.id, c.first, c.first + c.count - 1,
                     c.period, line.c_str());
    }
}

bool palette_cycle_excluded(const Palette* self, int32_t i) {
    if (self != &GamePalette) return false;
    for (const PaletteCycle& c : g_cycles)
        if (c.id != -1 && i >= c.first && i < c.first + c.count) return true;
    return false;
}

// ---- Font ---------------------------------------------------------------------------------------

void THISCALL Font_ctor(Font* self) {
    std::memset(self, 0, sizeof(*self));
    self->field_0 = -1;
}

int32_t THISCALL Font_init(Font* self, const char* face, int32_t size, int32_t style) {
    if (!face) return 3;
    if (!(self->flags & 1)) self->field_0 = -1;
    self->ascent_above = 0;
    self->height = 0;
    self->ascent = 0;
    self->descent = 0;
    if (self->handle) {
        platform::font_close(self->handle);
        self->handle = 0;
    }
    self->handle = platform::font_open(face, size, (style & 1) != 0, (style & 2) != 0, (style & 4) != 0);
    if (!self->handle) return 0xD;
    platform::FontMetrics m;
    platform::font_metrics(self->handle, &m);
    self->height = m.external_leading + m.height;
    self->ascent = m.ascent;
    self->ascent_above = m.ascent - m.internal_leading;
    self->internal_leading = m.internal_leading;
    self->descent = m.descent;
    return 0;
}

int32_t THISCALL Font_width(Font* self, const char* text) {
    if (!text) return 0;
    return platform::font_text_width(self->handle, text, (int32_t)std::strlen(text));
}

int32_t THISCALL Font_width_2(Font* self, const char* text, int32_t len) {
    if (!text) return 0;
    return platform::font_text_width(self->handle, text, len);
}

// ---- Buffer -------------------------------------------------------------------------------------

void THISCALL Buffer_ctor(Buffer* self) { *self = Buffer(); }

int32_t THISCALL Buffer_init(Buffer* self, int32_t width, int32_t height, int32_t, int32_t) {
    if (width <= 0 || height <= 0) return 3;
    self->width = width;
    self->height = height;
    self->stride = (width + 3) & ~3;  // DIB rows are DWORD aligned
    self->storage.assign((size_t)self->stride * height, 0);
    self->data = self->storage.data();
    self->clip = {0, 0, width, height};
    return 0;
}

int32_t THISCALL Buffer_load_pcx(Buffer* self, const char* file, Palette* pal, int32_t offset, int32_t count) {
    if (!file) return 3;
    std::string name = file;
    // ".pcx" is appended when the name has no extension.
    size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0) name += ".pcx";
    char resolved[1024];
    platform::resolve_path(name.c_str(), resolved, sizeof(resolved));
    uint32_t size = 0;
    uint8_t* data = win32::read_file(resolved, &size);
    if (!data) return 6;
    int32_t r = Buffer_load_pcx_2(self, data, (int32_t)size, pal, offset, count);
    win32::free_file(data);
    return r;
}

int32_t THISCALL Buffer_load_pcx_2(Buffer* self, const uint8_t* data, int32_t size, Palette* pal, int32_t offset,
                                   int32_t count) {
    static const char kMagic[] = "monkey sweats on a tuesday";
    if (!data) return 3;
    size_t magic_len = std::strlen(kMagic);
    if (size > (int32_t)magic_len && !std::strncmp((const char*)data, kMagic, magic_len)) {
        data += magic_len + 1;
        size -= (int32_t)magic_len + 1;
    }
    if (size < 0x80 + 0x300) return 3;
    const uint8_t* h = data;
    if (h[0] != 10 || h[2] != 1) return 3;    // manufacturer, RLE encoding
    if (h[1] < 4 && pal) return 3;            // no 256-colour palette before version 5
    auto u16 = [&](int32_t o) { return (int32_t)(h[o] | h[o + 1] << 8); };
    int32_t width = u16(8) - u16(4) + 1, height = u16(10) - u16(6) + 1;
    int32_t bpp = h[3], planes = h[0x41], bytes_per_line = u16(0x42);
    if (int32_t r = Buffer_init(self, width, height, 8, 0)) return r;
    if ((1 << bpp) * planes != 0x100) return 3;
    int32_t line = (8 / bpp) * bytes_per_line * planes;
    // RLE stream: a byte >= 0xC0 is a run count (low 6 bits) followed by the value. Runs may
    // cross rows; each row's bytes beyond the image width (padding) are dropped.
    const uint8_t* src = data + 0x80;
    const uint8_t* end = data + size;
    std::vector<uint8_t> row((size_t)line);
    int32_t x = 0, y = 0;
    while (y < height && src < end) {
        uint8_t v = *src++;
        int32_t n = 1;
        if ((v & 0xC0) == 0xC0) {
            n = v & 0x3F;
            if (src >= end) break;
            v = *src++;
        }
        while (n-- > 0 && y < height) {
            row[(size_t)x++] = (uint8_t)(v + offset);
            if (x >= line) {
                std::memcpy(self->data + (size_t)y * self->stride, row.data(), (size_t)std::min(width, line));
                x = 0;
                y++;
            }
        }
    }
    // Palette: the last 0x300 bytes; file entries 0.. go to colours offset..offset+count-1.
    const uint8_t* fp = data + size - 0x300;
    for (int32_t i = offset; i < offset + count && i < 256; i++, fp += 3) {
        self->colors[i][0] = fp[0];
        self->colors[i][1] = fp[1];
        self->colors[i][2] = fp[2];
    }
    self->pixel_offset = (uint8_t)offset;
    if (pal) {
        for (int32_t i = offset; i < offset + count && i < 256; i++) std::memcpy(pal->rgb[i], self->colors[i], 3);
    }
    return 0;
}

void THISCALL Buffer_change_color(Buffer* self, int32_t from, int32_t to) {
    for (uint8_t& p : self->storage)
        if (p == (uint8_t)from) p = (uint8_t)to;
}

int32_t THISCALL Buffer_get_pixel(Buffer* self, int32_t x, int32_t y) {
    if (!self->data || x < 0 || y < 0 || x >= self->width || y >= self->height) return 0;
    return self->data[(size_t)y * self->stride + x];
}

void THISCALL Buffer_set_clip(Buffer* self, const UiRect* rect) {
    UiRect r = rect ? *rect : UiRect{0, 0, self->width, self->height};
    r.left = std::max(r.left, 0);
    r.top = std::max(r.top, 0);
    r.right = std::min(r.right, self->width);
    r.bottom = std::min(r.bottom, self->height);
    self->clip = r;
}

void THISCALL Buffer_fill_3(Buffer* self, const UiRect* rect, int32_t color) {
    int32_t l = std::max(rect->left, self->clip.left), t = std::max(rect->top, self->clip.top);
    int32_t r = std::min(rect->right, self->clip.right), b = std::min(rect->bottom, self->clip.bottom);
    for (int32_t y = t; y < b; y++) std::memset(self->data + (size_t)y * self->stride + l, color, (size_t)std::max(0, r - l));
}

void THISCALL Buffer_fill_2(Buffer* self, int32_t color) { Buffer_fill_3(self, &self->clip, color); }

void THISCALL Buffer_fill(Buffer* self, int32_t x, int32_t y, int32_t w, int32_t h, int32_t color) {
    UiRect r{x, y, x + w, y + h};
    Buffer_fill_3(self, &r, color);
}

int32_t THISCALL Buffer_copy_4(Buffer* self, Buffer* dest, int32_t sx, int32_t sy, int32_t dx, int32_t dy, int32_t w,
                               int32_t h) {
    if (!self->data) return 7;
    if (!dest) return 3;
    if (sx < 0) {
        w += sx;
        dx -= sx;
        sx = 0;
    }
    if (sy < 0) {
        h += sy;
        dy -= sy;
        sy = 0;
    }
    if (self->width < sx || self->height < sy) return 0;
    if (self->width < w + sx) w = self->width - sx;
    if (self->height < sy + h) h = self->height - sy;
    UiRect r{std::max(dx, dest->clip.left), std::max(dy, dest->clip.top), std::min(dx + w, dest->clip.right),
             std::min(dy + h, dest->clip.bottom)};
    if (r.left >= r.right || r.top >= r.bottom) return 0;
    sx += r.left - dx;
    sy += r.top - dy;
    int32_t cw = r.right - r.left, ch = r.bottom - r.top;
    if (r.left >= dest->width || r.top >= dest->height) return 3;
    auto row = [&](int32_t j) {
        std::memmove(dest->data + (size_t)(r.top + j) * dest->stride + r.left, self->data + (size_t)(sy + j) * self->stride + sx, (size_t)cw);
    };
    if (r.top <= sy)
        for (int32_t j = 0; j < ch; j++) row(j);
    else
        for (int32_t j = ch - 1; j >= 0; j--) row(j);
    return 0;
}

static inline void put_clipped(Buffer* self, int32_t x, int32_t y, int32_t color) {
    if (x >= self->clip.left && x < self->clip.right && y >= self->clip.top && y < self->clip.bottom)
        self->data[(size_t)y * self->stride + x] = (uint8_t)color;
}

// Horizontal line x1..x2 (inclusive, either order) at y; nothing when x1 == x2.
void THISCALL Buffer_hline_2(Buffer* self, int32_t x1, int32_t x2, int32_t y, int32_t color) {
    if (!self->data || y < self->clip.top || y >= self->clip.bottom || x1 == x2) return;
    int32_t lo = x1, hi = x2;
    if (x2 < x1) {
        lo = x2;
        hi = x1;
    }
    if (lo >= self->clip.right || hi < self->clip.left) return;
    if (lo < self->clip.left) lo = self->clip.left;
    if (self->clip.right <= hi) hi = self->clip.right - 1;
    if (lo >= self->width || y >= self->height) return;
    std::memset(self->data + (size_t)y * self->stride + lo, color, (size_t)(hi - lo + 1));
}

// Vertical line y1..y2 (inclusive, either order) at x; nothing when y1 == y2.
void THISCALL Buffer_vline_2(Buffer* self, int32_t x, int32_t y1, int32_t y2, int32_t color) {
    if (!self->data || x < self->clip.left || x >= self->clip.right || y1 == y2) return;
    int32_t lo = y1, hi = y2;
    if (y2 < y1) {
        lo = y2;
        hi = y1;
    }
    if (lo >= self->clip.bottom || hi < self->clip.top) return;
    if (lo < self->clip.top) lo = self->clip.top;
    if (self->clip.bottom <= hi) hi = self->clip.bottom - 1;
    if (x >= self->width || lo >= self->height) return;
    for (int32_t y = lo; y <= hi; y++) self->data[(size_t)y * self->stride + x] = (uint8_t)color;
}

void THISCALL Buffer_box_2(Buffer* self, const UiRect* rect, int32_t color_tl, int32_t color_br) {
    if (!rect) return;
    int32_t l = rect->left, t = rect->top, r = rect->right, b = rect->bottom;
    Buffer_hline_2(self, l + 1, r - 1, t, color_tl);
    Buffer_hline_2(self, l, r - 2, b - 1, color_br);
    Buffer_vline_2(self, l, t, b - 2, color_tl);
    Buffer_vline_2(self, r - 1, t + 1, b - 1, color_br);
}

void THISCALL Buffer_line_2(Buffer* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color) {
    if (!self->data) return;
    if (x1 == x2) {
        Buffer_vline_2(self, x1, y1, y2, color);
        return;
    }
    if (y1 == y2) {
        Buffer_hline_2(self, x1, x2, y1, color);
        return;
    }
    if (x2 < x1) {
        std::swap(x1, x2);
        std::swap(y1, y2);
    }
    const int32_t L = self->clip.left, T = self->clip.top, R = self->clip.right, B = self->clip.bottom;
    const int32_t stride = self->stride;
    int32_t dx = x2 - x1, dy = y2 - y1;
    auto plot_at = [&](int32_t x, int32_t y) { self->data[(size_t)y * stride + x] = (uint8_t)color; };
    if (y1 < y2) {
        if (!(x1 <= R && y1 <= B && L <= x2 && T <= y2)) return;
        if (dx < dy) {
            int32_t d = dx * 2 - dy, inc = (dx - dy) * 2;
            while (x1 < L || y1 < T) {
                if (d < 1) {
                    d += dx * 2;
                    y1++;
                } else {
                    d += inc;
                    x1++;
                    y1++;
                }
            }
            if (x1 < R && y1 < B) {
                plot_at(x1, y1);
                if (B <= y2) y2 = B - 1;
                for (; y1 < y2; y1++) {
                    int32_t step = dx * 2;
                    if (d > 0) {
                        x1++;
                        if (R <= x1) break;
                        step = inc;
                    }
                    d += step;
                    plot_at(x1, y1 + 1);
                }
            }
        } else {
            int32_t d = dy * 2 - dx, inc = (dy - dx) * 2;
            while (x1 < L || y1 < T) {
                if (d < 1) {
                    d += dy * 2;
                    x1++;
                } else {
                    d += inc;
                    y1++;
                    x1++;
                }
            }
            if (x1 < R && y1 < B) {
                plot_at(x1, y1);
                if (R <= x2) x2 = R - 1;
                for (; x1 < x2; x1++) {
                    int32_t step = dy * 2;
                    if (d > 0) {
                        y1++;
                        if (B <= y1) break;
                        step = inc;
                    }
                    d += step;
                    plot_at(x1 + 1, y1);
                }
            }
        }
        return;
    }
    if (!(x1 <= R && T <= y1 && L <= x2 && y2 <= B)) return;
    dy = std::abs(dy);
    if (dx < dy) {
        int32_t d = dx * 2 - dy, inc = (dx - dy) * 2;
        while (x1 < L || B <= y1) {
            if (d < 1) {
                d += dx * 2;
                y1--;
            } else {
                d += inc;
                x1++;
                y1--;
            }
        }
        if (x1 < R && T <= y1) {
            plot_at(x1, y1);
            if (y2 < T) y2 = T;
            for (; y2 < y1; y1--) {
                int32_t step = dx * 2;
                if (d > 0) {
                    x1++;
                    if (R <= x1) break;
                    step = inc;
                }
                d += step;
                plot_at(x1, y1 - 1);
            }
        }
    } else {
        int32_t d = dy * 2 - dx, inc = (dy - dx) * 2;
        while (x1 < L || B <= y1) {
            if (d < 1) {
                d += dy * 2;
                x1++;
            } else {
                d += inc;
                y1--;
                x1++;
            }
        }
        if (x1 < R && T <= y1) {
            plot_at(x1, y1);
            if (R <= x2) x2 = R - 1;
            for (; x1 < x2; x1++) {
                int32_t step = dy * 2;
                if (d > 0) {
                    y1--;
                    if (y1 < T) break;
                    step = inc;
                }
                d += step;
                plot_at(x1 + 1, y1);
            }
        }
    }
}

namespace {
// Polygon edge (FUN_00626470): integer x with Bresenham steps.
struct PolyEdge {
    int32_t dir, count, end, x, xstep, sign, err, rem, dy;
};
bool poly_edge(const int32_t* pts, int32_t n, int32_t bottom, PolyEdge* e, int32_t start) {
    if (start == bottom) return false;
    int32_t next;
    for (;;) {
        next = e->dir + start;
        if (n - 1 < next) next = 0;
        else if (next < 0) next = n - 1;
        e->count = pts[next * 2 + 1] - pts[start * 2 + 1];
        if (e->count != 0) break;
        start = next;
        if (next == bottom) return false;
    }
    if (e->count < 0) return false;
    e->end = next;
    e->x = pts[start * 2];
    int32_t dx = pts[next * 2] - pts[start * 2];
    if (dx < 0) {
        e->sign = -1;
        dx = -dx;
        e->err = 1 - e->count;
        e->xstep = -(dx / e->count);
    } else {
        e->sign = 1;
        e->xstep = dx / e->count;
        e->err = 0;
    }
    e->rem = dx % e->count;
    e->dy = e->count;
    return true;
}
bool poly_step(const int32_t* pts, int32_t n, int32_t bottom, PolyEdge* e) {
    if (--e->count == 0) return poly_edge(pts, n, bottom, e, e->end);
    e->err += e->rem;
    e->x += e->xstep;
    if (e->err > 0) {
        e->x += e->sign;
        e->err -= e->dy;
    }
    return true;
}
}  // namespace

void THISCALL Buffer_poly(Buffer* self, const int32_t* pts, int32_t n, int32_t color) {
    if (!pts || !self || !self->data) return;
    int32_t right = self->clip.right - 1, bottom_clip = self->clip.bottom - 1;
    int32_t top_y = 0x7FFF, bottom_y = -0x7FFD, top = 0, bottom = 0;
    for (int32_t i = 0; i < n; i++) {
        int32_t y = pts[i * 2 + 1];
        if (y < top_y) {
            top_y = y;
            top = i;
        }
        if (bottom_y < y) {
            bottom_y = y;
            bottom = i;
        }
    }
    if (top_y >= bottom_y) return;
    PolyEdge a{}, b{};
    a.dir = -1;
    b.dir = 1;
    if (!poly_edge(pts, n, bottom, &a, top) || !poly_edge(pts, n, bottom, &b, top)) return;
    for (int32_t y = top_y; y < bottom_clip; y++) {
        if (self->clip.top <= y) {
            int32_t lo = a.x < b.x ? a.x : b.x, hi = a.x < b.x ? b.x : a.x;
            if (lo < hi && self->clip.left < hi && lo < right) {
                int32_t end = hi < right ? hi : right, start = lo < self->clip.left ? self->clip.left : lo;
                if (start < end) std::memset(self->data + (size_t)y * self->stride + start, color, (size_t)(end - start));
            }
        }
        if (!poly_step(pts, n, bottom, &a) || !poly_step(pts, n, bottom, &b)) break;
    }
}

int32_t THISCALL Buffer_set_font(Buffer* self, Font* font, Font* font1, Font* font2, Font* font3) {
    if (!font) return 3;
    if (font->handle) self->fonts[0] = font;
    self->fonts[1] = font1;
    self->fonts[3] = font3;
    self->fonts[2] = font2;
    return 0;
}

void THISCALL Buffer_set_text_color(Buffer* self, int32_t color, int32_t shadow, int32_t dx, int32_t dy) {
    self->text_color[0] = color;
    self->shadow_color[0] = shadow;
    self->shadow_dx[0] = dx;
    self->shadow_dy[0] = dy;
}

// TextOut of one run in a palette colour (transparent background).
static void text_out(Buffer* self, Font* font, int32_t x, int32_t y, const char* text, int32_t len, int32_t color) {
    std::vector<uint8_t> mask;
    int32_t w = 0, h = 0;
    if (!platform::font_render(font->handle, text, len, &mask, &w, &h)) return;
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            if (mask[(size_t)j * w + i]) put_clipped(self, x + i, y + j, color);
}

int32_t THISCALL Buffer_write_raw_l(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len) {
    if (len == 0) return x;
    // Single-font path (no {} [] font switching or ~ colour runs in map labels).
    Font* font = self->fonts[0];
    if (!font || !font->handle) return x;
    // The text is drawn baseline-aligned (TA_BASELINE) at y + the font's ascent above the
    // internal leading; text_out takes the cell top.
    y += font->ascent_above - font->ascent;
    int32_t slot = 0;
    if (self->shadow_color[slot] != -1)
        text_out(self, font, x + self->shadow_dx[slot], y + self->shadow_dy[slot], text, len, self->shadow_color[slot]);
    text_out(self, font, x, y, text, len, self->text_color[slot]);
    return x + Font_width_2(font, text, len);
}

int32_t THISCALL Buffer_write_l_3(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len) {
    if (!text) return x;
    if (!self->fonts[0] || !self->fonts[0]->handle) return 3;
    int32_t n = (int32_t)std::strlen(text);
    if (n < len) len = n;
    if (len == 0) return x;
    return Buffer_write_multi_font_raw_l(self, text, x, y, len);  // {} [] markup interpreted
}

int32_t THISCALL Buffer_write_cent_l_7(Buffer* self, const char* text, const UiRect* rect, int32_t len) {
    if (!text || !rect) return 0;
    Font* font = self->fonts[0];
    if (!font || !font->handle) return 3;
    int32_t n = (int32_t)std::strlen(text);
    if (n < len) len = n;
    if (len < 0 || len == 0) return 0;
    int32_t w = Buffer_text_width_2(self, text, n);  // of the whole string, markup aware
    int32_t x = ((rect->right - w) - rect->left) / 2 + rect->left;
    int32_t y = ((rect->bottom - font->ascent_above) - rect->top) / 2 + rect->top;
    return Buffer_write_multi_font_raw_l(self, text, x, y, len);
}

int32_t THISCALL Buffer_write_cent_l_8(Buffer* self, const char* text, int32_t x, int32_t y, int32_t len) {
    if (!text) return x;
    if (!self->fonts[0] || !self->fonts[0]->handle) return 3;
    int32_t n = (int32_t)std::strlen(text);
    if (n < len) len = n;
    if (len == 0) return x;
    int32_t w = Font_width_2(self->fonts[0], text, len);
    return Buffer_write_raw_l(self, text, x - (w >> 1), y, len);
}

// ---- Sprite -------------------------------------------------------------------------------------

int32_t THISCALL pfncSpriteFromCanvasRectTrans(Sprite* self, Buffer* canvas, int32_t key, int32_t x, int32_t y,
                                               int32_t w, int32_t h, int32_t) {
    if (!canvas || !canvas->data) return 3;
    *self = Sprite();
    if (w == 0 || h == 0 || x + w > canvas->width || y + h > canvas->height) return 3;
    uint8_t k = (uint8_t)key;
    self->key = k;
    self->full_width = w;
    self->full_height = h;
    auto px = [&](int32_t i, int32_t j) { return canvas->data[(size_t)(y + j) * canvas->stride + x + i]; };
    auto row_empty = [&](int32_t j) {
        for (int32_t i = 0; i < w; i++)
            if (px(i, j) != k) return false;
        return true;
    };
    // Rows of key at the top, then at the bottom.
    int32_t top = 0;
    while (top < h && row_empty(top)) top++;
    if (top == h) return 9;
    int32_t height = h - top;
    for (int32_t j = h - 1; j >= 0 && row_empty(j); j--) height--;
    if (height == 0) return 9;
    // Columns of key at the left and the right of the remaining rows.
    int32_t left = w, right = w;
    for (int32_t j = top; j < top + height; j++) {
        int32_t i = 0;
        while (i < w && px(i, j) == k) i++;
        left = std::min(left, i);
        int32_t n = 0;
        for (int32_t m = w; m > 0 && px(m - 1, j) == k; m--) n++;
        right = std::min(right, n);
    }
    int32_t width = w - left - right;
    if (width == 0) return 9;
    self->x_offset = left;
    self->y_offset = top;
    self->width = width;
    self->height = height;
    self->stride = width;
    self->pixels.resize((size_t)width * height);
    for (int32_t j = 0; j < height; j++)
        for (int32_t i = 0; i < width; i++) self->pixels[(size_t)j * width + i] = px(left + i, top + j);
    return 0;
}

void GAMEAPI Sprite_fill_scaling_table() {
    int32_t big = BlitDestScale, small = BlitSourceScale;
    if (big <= small) big = BlitSourceScale;
    if (BlitDestScale < BlitSourceScale) small = BlitDestScale;
    else small = BlitSourceScale;
    int32_t err, step2, rem2, quot;
    if (small < 2) {
        rem2 = 0;
        step2 = 0;
        err = 0;
        quot = big;
    } else {
        quot = big / small;
        rem2 = big % small << 1;
        step2 = small * 2;
        err = (rem2 >> 1) - small * 2;
    }
    if (quot & 1) err += big;
    if (small == 1) {
        for (int32_t& v : ScaleTable) v = quot;
        return;
    }
    for (int32_t& v : ScaleTable) {
        err += rem2;
        v = quot;
        if (err > 0) {
            err -= step2;
            v = quot + 1;
        }
    }
}

namespace {

enum SpriteOp { SPR_COPY, SPR_SHADOW, SPR_TRANSLATE, SPR_MONO, SPR_MULTI };

uint8_t g_mono_color = 0;  // DAT_009B53E0

inline void sprite_put(uint8_t* d, uint8_t p, SpriteOp op, const uint8_t* table) {
    if (op == SPR_MONO) *d = g_mono_color;
    else if (op == SPR_SHADOW && p == 0) *d = table[*d];
    else if (op == SPR_TRANSLATE) *d = table[p];
    else if (op == SPR_MULTI) *d = table[(uint32_t)p << 8 | *d];
    else *d = p;
}

bool intersect(UiRect* out, const UiRect& a, const UiRect& b) {  // IntersectRect
    UiRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right), std::min(a.bottom, b.bottom)};
    if (r.left >= r.right || r.top >= r.bottom) {
        *out = UiRect{0, 0, 0, 0};
        return false;
    }
    *out = r;
    return true;
}

// Sprite_get_clipped_{x,y}_table_expand (0x5EAB0F / 0x5EABBF) on one axis: lo/hi of the full and
// the clipped destination ranges. Out: source entries to skip, destination pixels clipped
// before, the table entries (destination pixels per source pixel; the first and last ones
// shortened by the clipping) and the visible destination length. False: empty.
bool clip_table_expand(int32_t full_lo, int32_t full_hi, int32_t clip_lo, int32_t clip_hi, int32_t* src_skip,
                       int32_t* dst_skip, std::vector<int32_t>* table, int32_t* visible) {
    int32_t skip = clip_lo - full_lo, len = clip_hi - clip_lo;
    if (len == 0) return false;
    *visible = len;
    *dst_skip = skip;
    table->clear();
    int32_t i = 0;
    while (skip > 0) skip -= ScaleTable[i++ % 800];
    if (skip < 0) {
        int32_t part = -skip;
        len -= part;
        *src_skip = i - 1;
        table->push_back(part);
    } else {
        *src_skip = i;
    }
    while (len > 0) {
        int32_t v = ScaleTable[i++ % 800];
        table->push_back(v);
        len -= v;
    }
    if (len < 0) table->back() += len;
    return true;
}

// Sprite_get_clipped_{x,y}_table_shrink (0x5EAC6F / 0x5EACFF): source pixels skipped for the
// clipped destination pixels, and the source step of each visible destination pixel.
bool clip_table_shrink(int32_t full_lo, int32_t full_hi, int32_t clip_lo, int32_t clip_hi, int32_t* dst_skip,
                       int32_t* src_skip, std::vector<int32_t>* table, int32_t* visible) {
    int32_t skip = clip_lo - full_lo, len = clip_hi - clip_lo;
    if (len == 0) return false;
    *dst_skip = skip;
    *visible = len;
    *src_skip = 0;
    for (int32_t i = 0; i < skip; i++) *src_skip += ScaleTable[i % 800];
    table->clear();
    for (int32_t j = 0; j < len; j++) table->push_back(ScaleTable[(skip + j) % 800]);
    return true;
}

// The blitters' common code (pfncSpriteStretchCopyToCanvas 0x5E4B9A, Sprite_draw_dest 0x5E5833,
// Sprite_draw_4 0x5E9D44).
int32_t sprite_blit(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, SpriteOp op,
                    const uint8_t* table) {
    if (self->pixels.empty() || !dest || !dest->data) return 3;
    float ratio = 1.0f;  // DAT_009B6624
    if (BlitDestScale != BlitSourceScale) {
        ratio = (float)((double)BlitDestScale / (double)BlitSourceScale);
        if ((ScaleTableDest != BlitDestScale || ScaleTableSource != BlitSourceScale)) {
            if (ratio > 4.0f) return 0;
            Sprite_fill_scaling_table();
            ScaleTableDest = BlitDestScale;
            ScaleTableSource = BlitSourceScale;
        }
    }
    uint8_t k = (uint8_t)key;
    const int32_t sstride = self->stride, dstride = dest->stride;
    // Source reads as in memory: rows follow each other; nothing beyond the bits.
    const size_t nbits = self->pixels.size();
    auto src_at = [&](int64_t off) -> uint8_t { return off >= 0 && (size_t)off < nbits ? self->pixels[(size_t)off] : k; };
    if (BlitDestScale == BlitSourceScale) {
        UiRect full{x + self->x_offset, y + self->y_offset, 0, 0};
        full.right = full.left + self->width;
        full.bottom = full.top + self->height;
        UiRect r;
        if (!intersect(&r, full, dest->clip)) return 0;
        for (int32_t j = r.top; j < r.bottom; j++) {
            const uint8_t* s = &self->pixels[(size_t)(j - full.top) * sstride + (r.left - full.left)];
            uint8_t* d = dest->data + (size_t)j * dstride + r.left;
            for (int32_t i = r.left; i < r.right; i++, s++, d++)
                if (*s != k) sprite_put(d, *s, op, table);
        }
        return 0;
    }
    // __ftol of the scaled size and position.
    int32_t h = (int32_t)((double)self->height * ratio);
    int32_t w = (int32_t)((double)self->width * ratio);
    int32_t top = (int32_t)((double)y + (double)self->y_offset * ratio);
    int32_t left = (int32_t)((double)x + (double)self->x_offset * ratio);
    UiRect full{left, top, left + w, top + h}, r;
    if (!intersect(&r, full, dest->clip)) return 0;
    std::vector<int32_t> tx, ty;
    if (BlitDestScale < BlitSourceScale) {
        int32_t dx_skip, sx_skip, dy_skip, sy_skip, vw, vh;
        intersect(&r, full, dest->clip);
        if (!clip_table_shrink(full.left, full.right, r.left, r.right, &dx_skip, &sx_skip, &tx, &vw)) return 0;
        if (!clip_table_shrink(full.top, full.bottom, r.top, r.bottom, &dy_skip, &sy_skip, &ty, &vh)) return 0;
        uint8_t* drow = dest->data + (size_t)(full.top + dy_skip) * dstride + full.left + dx_skip;
        int64_t srow = (int64_t)sstride * sy_skip + sx_skip;
        for (int32_t j = 0; j < vh; j++) {
            int64_t s = srow;
            for (int32_t i = 0; i < vw; i++) {
                uint8_t p = src_at(s);
                if (p != k) sprite_put(drow + i, p, op, table);
                s += tx[(size_t)i];
            }
            srow += (int64_t)sstride * ty[(size_t)j];
            drow += dstride;
        }
        return 0;
    }
    int32_t sx_skip, dx_skip, sy_skip, dy_skip, vw, vh;
    intersect(&r, full, dest->clip);
    if (!clip_table_expand(full.left, full.right, r.left, r.right, &sx_skip, &dx_skip, &tx, &vw)) return 0;
    if (!clip_table_expand(full.top, full.bottom, r.top, r.bottom, &sy_skip, &dy_skip, &ty, &vh)) return 0;
    uint8_t* drow = dest->data + (size_t)(full.top + dy_skip) * dstride + full.left + dx_skip;
    int64_t srow = (int64_t)sstride * sy_skip + sx_skip;
    for (size_t j = 0; j < ty.size(); j++) {
        int32_t rows = ty[j] & 0xFF;  // the row count is read as a byte
        if (rows < 1 || rows > 4) rows = 4;
        uint8_t* d = drow;
        int64_t s = srow;
        for (size_t i = 0; i < tx.size(); i++, s++) {
            int32_t n = tx[i];
            uint8_t p = src_at(s);
            if (p != k) {
                for (int32_t m = 0; m < rows; m++)
                    for (int32_t c = 0; c < n; c++) sprite_put(d + (size_t)m * dstride + c, p, op, table);
            }
            d += n;
        }
        drow += (size_t)rows * dstride;
        srow += sstride;
    }
    return 0;
}

}  // namespace

int32_t THISCALL Sprite_draw_dest(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table) {
    return sprite_blit(self, dest, key, x, y, SPR_SHADOW, table);
}

int32_t THISCALL pfncSpriteStretchCopyToCanvas(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y) {
    return sprite_blit(self, dest, key, x, y, SPR_COPY, nullptr);
}

int32_t THISCALL Sprite_draw_mono_2(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, int32_t color) {
    g_mono_color = (uint8_t)color;
    return sprite_blit(self, dest, key, x, y, SPR_MONO, nullptr);
}

int32_t THISCALL Sprite_draw_4(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table) {
    return sprite_blit(self, dest, key, x, y, SPR_TRANSLATE, table);
}

int32_t THISCALL Sprite_draw_multi_table_dest_2(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y,
                                                const uint8_t* table) {
    return sprite_blit(self, dest, key, x, y, SPR_MULTI, table);
}

int32_t THISCALL Buffer_draw_multi_table_dest_2(Buffer* self, Buffer* dest, int32_t key, int32_t x, int32_t y,
                                                int32_t dest_scale, int32_t source_scale, const uint8_t* table) {
    if (!self->data) return 3;
    // The whole buffer as an untrimmed sprite, through the sprite blitter's scaling.
    Sprite s;
    s.pixels.assign(self->data, self->data + (size_t)self->stride * self->height);
    s.stride = self->stride;
    s.width = s.full_width = self->width;
    s.height = s.full_height = self->height;
    s.key = key;
    int32_t keep_d = BlitDestScale, keep_s = BlitSourceScale;
    BlitDestScale = dest_scale;
    BlitSourceScale = source_scale;
    int32_t r = sprite_blit(&s, dest, key, x, y, SPR_MULTI, table);
    BlitDestScale = keep_d;
    BlitSourceScale = keep_s;
    return r;
}

// ---- Texture ------------------------------------------------------------------------------------

int32_t THISCALL pfncImageFromCanvas(Texture* self, Buffer* canvas, int32_t x, int32_t y, int32_t w, int32_t h,
                                     int32_t) {
    if (!canvas) return 0x10;
    self->pixels.clear();
    self->width = 0;
    self->height = 0;
    if (x + w > canvas->width || y + h > canvas->height) return 3;
    self->width = w;
    self->height = h;
    self->pixels.resize((size_t)w * h);
    for (int32_t j = 0; j < h; j++) std::memcpy(&self->pixels[(size_t)j * w], canvas->data + (size_t)(y + j) * canvas->stride + x, (size_t)w);
    int32_t r = w - 1, b = h - 1, cx = w / 2 - 1, cy = h / 2 - 1;
    const int32_t pts[12][2] = {{0, 0}, {r, 0}, {r, b}, {0, b}, {0, 0}, {cx, 0}, {r, 0}, {r, cy},
                                {r, b}, {cx, b}, {0, b}, {0, cy}};
    std::memcpy(self->points, pts, sizeof(pts));
    return 0;
}
