// Colour tables (see colortables.h).
#include "ui/colortables.h"

#include <cmath>
#include <cstring>
#include <string>

#include "engine/globals.h"

void GAMEAPI RGB_to_HSV(const uint8_t* rgb, Hsv* out) {
    if (!out || !rgb) return;
    double r = rgb[0] * 0.00390625, g = rgb[1] * 0.00390625, b = rgb[2] * 0.00390625;
    double mx = g > r ? g : r;  // ties: r
    if (b > mx) mx = b;
    double mn = g < r ? g : r;  // ties: r
    // BUG(original): blue is compared with the maximum instead of the minimum.
    if (b < mx) mn = b;
    out->v = mx;
    double s = mx == 0.0 ? 0.0 : (mx - mn) / mx;
    out->s = s;
    if (s == 0.0) {
        out->h = -1.0;
        return;
    }
    double delta = mx - mn, h;
    if (r == mx) h = (g - b) / delta;
    else if (g == mx) h = (b - r) / delta + 2.0;
    else h = (r - g) / delta + 4.0;
    h *= 60.0;
    if (h < 0.0) h += 360.0;
    out->h = h;
}

int32_t THISCALL Palette_get_nearest_palette_index(Palette* pal, int32_t r, int32_t g, int32_t b, int32_t game_only) {
    int32_t best_d = 200000, best = 0;
    // No palette cycles are active (load_cycling): no entries are excluded.
    for (int32_t i = game_only ? 10 : 0; i < (game_only ? 0xF6 : 0x100); i++) {
        int32_t dr = pal->rgb[i][0] - (r & 0xFF), dg = pal->rgb[i][1] - (g & 0xFF), db = pal->rgb[i][2] - (b & 0xFF);
        int32_t d = dg * dg + dr * dr + db * db;
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

int32_t THISCALL Palette_get_nearest_palette_index_2(Palette*, const Hsv* t, const Hsv* table, int32_t game_only) {
    const long double rad = 0.017453292519444445;
    double best_d = 200000.0;
    int32_t best = 0;
    for (int32_t i = game_only ? 10 : 0; i < (game_only ? 0xF6 : 0x100); i++) {
        const Hsv& c = table[i];
        long double dv = (long double)(t->v - c.v);
        long double th = (long double)t->h * rad, ch = (long double)c.h * rad;
        double ct = (double)(cosl(th) * (long double)t->s), st = (double)(sinl(th) * (long double)t->s);
        double dcos = (double)((long double)ct - cosl(ch) * (long double)c.s);
        long double dsin = (long double)st - sinl(ch) * (long double)c.s;
        double cos2 = (double)((long double)dcos * (long double)dcos);
        long double d = dv * dv + dsin * (long double)(double)dsin + (long double)cos2;
        if (d < (long double)best_d) {
            best_d = (double)d;
            best = i;
        }
    }
    return best;
}

int32_t THISCALL Palette_create_table(Palette* pal, uint8_t* out, int32_t start, int32_t count, int32_t percent) {
    if (!out) return 0x10;
    if (percent <= -0x65 || percent >= 0x65) return 3;
    for (int32_t i = 0; i < start; i++) out[i] = (uint8_t)i;
    int32_t end = start + count;
    for (int32_t i = end; i < 0x100; i++) out[i] = (uint8_t)i;
    Hsv table[256];
    for (int32_t i = 0; i < 256; i++) RGB_to_HSV(pal->rgb[i], &table[i]);
    for (int32_t i = start; i < end; i++) {
        Hsv h = table[i];
        h.v = (double)(percent + 100) * h.v * 0.01;
        out[i] = (uint8_t)Palette_get_nearest_palette_index_2(pal, &h, table, 1);
    }
    return 0;
}

int32_t THISCALL Palette_create_table_from_color(Palette* pal, int32_t color, uint8_t* out, int32_t start,
                                                 int32_t count, int32_t wa, int32_t wb) {
    if (!out) return 0x10;
    for (int32_t i = 0; i < start; i++) out[i] = (uint8_t)i;
    int32_t end = start + count;
    for (int32_t i = end; i < 0x100; i++) out[i] = (uint8_t)i;
    const uint8_t* c = pal->rgb[color & 0xFF];
    int32_t w = wa + wb;
    for (int32_t i = start; i < end; i++) {
        int32_t r = (int32_t)(pal->rgb[i][0] * wa + c[0] * wb) / w;
        int32_t g = (int32_t)(pal->rgb[i][1] * wa + c[1] * wb) / w;
        int32_t b = (int32_t)(pal->rgb[i][2] * wa + c[2] * wb) / w;
        out[i] = (uint8_t)Palette_get_nearest_palette_index(pal, r & 0xFF, g & 0xFF, b & 0xFF, 1);
    }
    return 0;
}

void GAMEAPI colortables_init() {
    // Only the tables the map uses so far; the others (0x6FEB7C, 0x6F0F7C, 0x6F117C, 0x6F1A7C,
    // 0x6F127C.., 0x6F1B7C..) are not reconstructed yet.
    Palette_create_table(&GamePalette, ShadowTable, 0, 0x100, -0x2B);
}

void GAMEAPI colortables_init_faction(int32_t faction_id) {
    Buffer canvas;
    Buffer_ctor(&canvas);
    std::string file = std::string(MFactions[faction_id].filename) + ".pcx";
    if (Buffer_load_pcx(&canvas, file.c_str(), nullptr, 10, 0xEC)) return;
    int32_t color = Buffer_get_pixel(&canvas, 0xA1, 0x2EB);
    uint8_t p = (uint8_t)Buffer_get_pixel(&canvas, 0xA1, 0x2F5);
    if (p == *mem::host(0x696D14)) return;
    int32_t pct = ((uint32_t)(uint8_t)(p - 10) * 100) / 0xFF;
    // (0x1B3, 0x2E9) gives the unit colouring base colour (tables at 0x79A7EC, not yet used).
    Palette_create_table_from_color(&GamePalette, color, FactionColorTable[faction_id], 10, 0xEC, 100 - pct, pct);
}
