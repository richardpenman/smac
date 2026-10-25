// Graphics classes of the original UI (see gfx.h).
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
uint8_t ShadowTable[256];

// ---- Palette ------------------------------------------------------------------------------------

// The 20 static colours of the Windows system palette (indices 0..9 and 246..255), which an 8-bit
// game palette keeps.
static const uint8_t kStaticColors[20][3] = {
    {0, 0, 0},       {128, 0, 0},     {0, 128, 0},     {128, 128, 0},   {0, 0, 128},
    {128, 0, 128},   {0, 128, 128},   {192, 192, 192}, {192, 220, 192}, {166, 202, 240},
    {255, 251, 240}, {160, 160, 164}, {128, 128, 128}, {255, 0, 0},     {0, 255, 0},
    {255, 255, 0},   {0, 0, 255},     {255, 0, 255},   {0, 255, 255},   {255, 255, 255},
};

int32_t THISCALL Palette_get_nearest_palette_index(Palette* pal, int32_t r, int32_t g, int32_t b) {
    // Approximation: least squared RGB distance over the game colours 10..245 (the original
    // matches in HSV space, Palette_get_nearest_palette_index_2).
    int32_t best = 10, best_d = 0x7FFFFFFF;
    for (int32_t i = 10; i < 246; i++) {
        int32_t dr = pal->rgb[i][0] - r, dg = pal->rgb[i][1] - g, db = pal->rgb[i][2] - b;
        int32_t d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void GAMEAPI load_palette() {
    Buffer canvas;
    Buffer_ctor(&canvas);
    int32_t gamma = prefs_get_2("Gamma Correction", 100, 0);
    for (int32_t i = 0; i < 10; i++) std::memcpy(GamePalette.rgb[i], kStaticColors[i], 3);
    for (int32_t i = 0; i < 10; i++) std::memcpy(GamePalette.rgb[246 + i], kStaticColors[10 + i], 3);
    Buffer_load_pcx(&canvas, "PALETTE.PCX", &GamePalette, 10, 0xEC);
    // FUN_005C8E60: gamma correction of the game colours (identity at 100%).
    if (gamma > 0 && gamma != 100) {
        double inv = 100.0 / gamma;
        for (int32_t i = 10; i < 246; i++)
            for (int32_t c = 0; c < 3; c++)
                GamePalette.rgb[i][c] = (uint8_t)std::lround(255.0 * std::pow(GamePalette.rgb[i][c] / 255.0, inv));
    }
    // colortables_init: the shadow table (Palette_create_table(ShadowTable, 0, 256, -43)): every
    // colour with its HSV value lowered by 43%. Approximation: RGB scaled to 57%, nearest colour.
    for (int32_t i = 0; i < 256; i++) {
        if (i < 10 || i >= 246) {
            ShadowTable[i] = (uint8_t)i;
            continue;
        }
        ShadowTable[i] = (uint8_t)Palette_get_nearest_palette_index(
            &GamePalette, GamePalette.rgb[i][0] * 57 / 100, GamePalette.rgb[i][1] * 57 / 100,
            GamePalette.rgb[i][2] * 57 / 100);
    }
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

static inline void put_clipped(Buffer* self, int32_t x, int32_t y, int32_t color) {
    if (x >= self->clip.left && x < self->clip.right && y >= self->clip.top && y < self->clip.bottom)
        self->data[(size_t)y * self->stride + x] = (uint8_t)color;
}

void THISCALL Buffer_box_2(Buffer* self, const UiRect* rect, int32_t color_tl, int32_t color_br) {
    int32_t l = rect->left, t = rect->top, r = rect->right - 1, b = rect->bottom - 1;
    for (int32_t x = l; x <= r; x++) {
        put_clipped(self, x, t, color_tl);
        put_clipped(self, x, b, color_br);
    }
    for (int32_t y = t; y <= b; y++) {
        put_clipped(self, l, y, color_tl);
        put_clipped(self, r, y, color_br);
    }
}

void THISCALL Buffer_line_2(Buffer* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color) {
    // Bresenham (the original's run-slice variant draws the same pixel set for these lines).
    int32_t dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
    int32_t sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, err = dx + dy;
    for (;;) {
        put_clipped(self, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
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
    return Buffer_write_raw_l(self, text, x, y, len);
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
    self->width = w;
    self->height = h;
    self->stride = w;
    self->key = key;
    self->x_offset = 0;
    self->y_offset = 0;
    self->pixels.assign((size_t)w * h, (uint8_t)key);
    for (int32_t j = 0; j < h; j++)
        for (int32_t i = 0; i < w; i++)
            self->pixels[(size_t)j * w + i] = (uint8_t)Buffer_get_pixel(canvas, x + i, y + j);
    return 0;
}

// Scaling table (0x9B3AD8, 800 entries) and the scale it was built for.
static int32_t ScaleTable[800];
static int32_t ScaleTableDest = 1, ScaleTableSource = 1;  // DAT_00696D20 / DAT_00696D24

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
        step2 = small * 2;
        rem2 = big % small << 1;
        err = (rem2 >> 1) - small * 2;
        quot = big / small;
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

// Destination size of `n` source pixels (expanding) or source pixels consumed per destination
// pixel (shrinking), following the scaling table.
static void build_axis_map(int32_t src_len, std::vector<int32_t>* map) {
    map->clear();
    bool expand = BlitDestScale > BlitSourceScale;
    if (BlitDestScale == BlitSourceScale) {
        for (int32_t i = 0; i < src_len; i++) map->push_back(i);
    } else if (expand) {
        for (int32_t i = 0; i < src_len && i < 800; i++)
            for (int32_t k = 0; k < ScaleTable[i]; k++) map->push_back(i);
    } else {
        int32_t s = 0;
        for (int32_t i = 0; s < src_len && i < 800; i++) {
            map->push_back(s);
            s += ScaleTable[i];
        }
    }
}

static int32_t sprite_blit(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table) {
    if (self->pixels.empty() || !dest || !dest->data) return 3;
    if (BlitDestScale != BlitSourceScale && (ScaleTableDest != BlitDestScale || ScaleTableSource != BlitSourceScale)) {
        if ((float)BlitDestScale / (float)BlitSourceScale > 4.0f) return 0;
        Sprite_fill_scaling_table();
        ScaleTableDest = BlitDestScale;
        ScaleTableSource = BlitSourceScale;
    }
    std::vector<int32_t> mx, my;
    build_axis_map(self->width, &mx);
    build_axis_map(self->height, &my);
    int32_t ox = x + self->x_offset, oy = y + self->y_offset;
    for (size_t j = 0; j < my.size(); j++) {
        int32_t dy = oy + (int32_t)j;
        if (dy < dest->clip.top || dy >= dest->clip.bottom) continue;
        const uint8_t* srow = &self->pixels[(size_t)my[j] * self->stride];
        uint8_t* drow = dest->data + (size_t)dy * dest->stride;
        for (size_t i = 0; i < mx.size(); i++) {
            int32_t dx = ox + (int32_t)i;
            if (dx < dest->clip.left || dx >= dest->clip.right) continue;
            uint8_t p = srow[mx[i]];
            if (p == (uint8_t)key) continue;
            if (p == 0 && table) drow[dx] = table[drow[dx]];
            else drow[dx] = p;
        }
    }
    return 0;
}

int32_t THISCALL Sprite_draw_dest(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table) {
    return sprite_blit(self, dest, key, x, y, table);
}

int32_t THISCALL pfncSpriteStretchCopyToCanvas(Sprite* self, Buffer* dest, int32_t key, int32_t x, int32_t y) {
    return sprite_blit(self, dest, key, x, y, nullptr);
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

// Scan conversion shared by the texture blitters: convex polygon, per row the edge crossings
// with interpolated (u, v, light). key >= 0: skip texels equal to key; threshold: skip pixels
// whose light is at or above it.
static void texture_poly(Texture* t, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                         const int32_t* uv, int32_t color, int32_t key, int32_t threshold) {
    if (!dest || !dest->data || t->pixels.empty() || n < 3) return;
    int32_t top = 0x7FFF, bottom = -0x7FFF;
    for (int32_t i = 0; i < n; i++) {
        top = std::min(top, pts[i * 2 + 1]);
        bottom = std::max(bottom, pts[i * 2 + 1]);
    }
    const UiRect& c = dest->clip;
    for (int32_t y = std::max(top, c.top); y < std::min(bottom, c.bottom); y++) {
        double yc = y + 0.5;
        double xl = 1e9, xr = -1e9, ul = 0, vl = 0, ll = 0, ur = 0, vr = 0, lr = 0;
        for (int32_t i = 0; i < n; i++) {
            int32_t j = (i + 1) % n;
            double y0 = pts[i * 2 + 1], y1 = pts[j * 2 + 1];
            if ((yc < y0) == (yc < y1)) continue;
            double s = (yc - y0) / (y1 - y0);
            double x = pts[i * 2] + s * (pts[j * 2] - pts[i * 2]);
            double u = uv[i * 2] + s * (uv[j * 2] - uv[i * 2]);
            double v = uv[i * 2 + 1] + s * (uv[j * 2 + 1] - uv[i * 2 + 1]);
            double l = light[i] + s * (light[j] - light[i]);
            if (x < xl) {
                xl = x;
                ul = u;
                vl = v;
                ll = l;
            }
            if (x > xr) {
                xr = x;
                ur = u;
                vr = v;
                lr = l;
            }
        }
        if (xr < xl) continue;
        int32_t x0 = (int32_t)std::ceil(xl - 0.5), x1 = (int32_t)std::ceil(xr - 0.5);
        double span = xr - xl > 0 ? xr - xl : 1;
        uint8_t* row = dest->data + (size_t)y * dest->stride;
        bool fill_row = color != -1 && !(y & 1);
        for (int32_t x = std::max(x0, c.left); x < std::min(x1, c.right); x++) {
            double s = (x + 0.5 - xl) / span;
            int32_t l = (int32_t)std::floor(ll + s * (lr - ll));
            if (l >= threshold) continue;
            int32_t u = std::max(0, std::min(t->width - 1, (int32_t)(ul + s * (ur - ul))));
            int32_t v = std::max(0, std::min(t->height - 1, (int32_t)(vl + s * (vr - vl))));
            uint8_t texel = t->pixels[(size_t)v * t->width + u];
            if (key >= 0 && texel == (uint8_t)key) continue;
            row[x] = fill_row ? (uint8_t)color : (uint8_t)(texel + l);
        }
    }
}

static void clamp_light(int32_t* light, int32_t n) {
    for (int32_t i = 0; i < n; i++) light[i] = std::max(TextureLightMin, std::min(TextureLightMax, light[i]));
}

void THISCALL pfncImageCopytoCanvas2(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                    const int32_t* uv, int32_t color) {
    clamp_light(light, n);
    texture_poly(self, dest, pts, light, n, uv, color, -1, 0x7FFFFFFF);
}

void THISCALL Texture_draw_trans_5(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                  const int32_t* uv, int32_t color) {
    clamp_light(light, n);
    texture_poly(self, dest, pts, light, n, uv, color, TextureKey, 0x7FFFFFFF);
}

void THISCALL Texture_draw_4(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                            const int32_t* uv, int32_t threshold, int32_t color) {
    bool any = false;
    for (int32_t i = 0; i < n; i++) any |= light[i] < threshold;
    if (any) texture_poly(self, dest, pts, light, n, uv, color, -1, threshold);
}
