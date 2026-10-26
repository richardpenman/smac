// Texture blitters (see texture.h), bit-exact with the original's scan converter.
#include "ui/texture.h"

#include <cstring>

namespace {

// Edge of the scan converter (Texture_setup_edge_gouraud 0x61FC90 / Texture_setup_edge
// 0x624D80; 15 dwords on the original's stack).
struct Edge {
    int32_t dir;     // vertex order: -1 left edge, +1 right edge
    int32_t count;   // rows left on this edge
    int32_t end;     // end vertex
    int32_t u, v;    // 16.16 (gouraud: + 0x8000)
    int32_t du, dv;  // per row
    int32_t x;       // integer x
    int32_t xstep;   // integer part of dx / dy
    int32_t sign;    // +-1: Bresenham correction step
    int32_t err;     // Bresenham error
    int32_t rem;     // |dx| % dy
    int32_t dy;
    int32_t light;   // 16.16
    int32_t dlight;
};

// The polygon being drawn (the original's globals DAT_009BB4F4, _490, _4FC, _4B0, _4B4).
struct Poly {
    const int32_t* pts;
    const int32_t* uv;
    const int32_t* light;  // nullptr: no light values (mask_dest reads stale ones; unused)
    int32_t n;
    int32_t bottom;  // first vertex with the largest y
    bool gouraud;
};

// fixed_div (0x628AD0): (a << 16) / b with a 64-bit dividend.
int32_t fixed_div(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * 65536) / b); }

bool setup_edge(const Poly& p, Edge* e, int32_t start) {
    if (start == p.bottom) return false;
    int32_t next;
    for (;;) {
        next = e->dir + start;
        if (p.n - 1 < next) next = 0;
        else if (next < 0) next = p.n - 1;
        e->count = p.pts[next * 2 + 1] - p.pts[start * 2 + 1];
        if (e->count != 0) break;
        start = next;
        if (next == p.bottom) return false;
    }
    if (e->count < 1) return false;
    e->end = next;
    int32_t dy16 = e->count << 16;
    int32_t bias = p.gouraud ? 0x8000 : 0;
    e->u = (p.uv[start * 2] << 16) + bias;
    e->v = (p.uv[start * 2 + 1] << 16) + bias;
    e->du = fixed_div((p.uv[next * 2] << 16) - e->u, dy16);
    e->dv = fixed_div((p.uv[next * 2 + 1] << 16) - e->v, dy16);
    e->x = p.pts[start * 2];
    int32_t dx = p.pts[next * 2] - p.pts[start * 2];
    if (p.gouraud) {
        e->xstep = dx / e->count;
        if (dx < 0) {
            dx = -dx;
            e->sign = -1;
            e->err = 1 - e->count;
        } else {
            e->sign = 1;
            e->err = 0;
        }
    } else {
        // Texture_setup_edge: -(|dx| / dy) for negative dx (the same truncation).
        if (dx < 0) {
            dx = -dx;
            e->err = 1 - e->count;
            e->sign = -1;
            e->xstep = -(dx / e->count);
        } else {
            e->sign = 1;
            e->xstep = dx / e->count;
            e->err = 0;
        }
    }
    e->dy = e->count;
    e->rem = dx % e->count;
    if (p.gouraud) {
        int32_t l0 = p.light ? p.light[start] : 0, l1 = p.light ? p.light[next] : 0;
        e->light = l0 << 16;
        e->dlight = fixed_div((l1 << 16) - (l0 << 16), dy16);
    }
    return true;
}

void step_edge(Edge* e) {
    e->u += e->du;
    e->v += e->dv;
    e->x += e->xstep;
    e->light += e->dlight;
    e->err += e->rem;
    if (e->err > 0) {
        e->x += e->sign;
        e->err -= e->dy;
    }
}

// x87 single precision with chop rounding (the blitters set the control word to 0x0C7F): the
// reciprocal of the span width and the products with it are truncated to 24 significant bits.
struct Recip {
    uint64_t m = 0;  // mantissa, 2^23 <= m < 2^24
    int32_t s = 0;   // value = m * 2^-s
};

int32_t bit_length(uint64_t v) {
    int32_t n = 0;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

Recip recip(int32_t w) {  // 1 / w, w > 0
    Recip r;
    int32_t b = bit_length((uint64_t)w);
    r.s = b + 23;
    // 2^s / w lies in (2^23, 2^24]; for a power of two it is exactly 2^24.
    uint64_t num = (uint64_t)1 << r.s;  // s <= 54 (w < 2^31)
    r.m = num / (uint64_t)w;
    if (r.m >= (1u << 24)) {
        r.m >>= 1;
        r.s -= 1;
    }
    return r;
}

// fistp(chop(d * (1 / w)) * 2^shift): the span step of a 16.16 (shift 16) or 8.8 (shift 8) value.
int64_t span_step(int32_t d, const Recip& r, int32_t shift) {
    if (d == 0) return 0;
    uint64_t a = d < 0 ? (uint64_t)(-(int64_t)d) : (uint64_t)d;
    uint64_t prod = a * r.m;
    int32_t len = bit_length(prod);
    if (len > 24) prod = (prod >> (len - 24)) << (len - 24);
    int32_t e = r.s - shift;
    uint64_t v = e >= 0 ? (e >= 64 ? 0 : prod >> e) : prod << -e;
    return d < 0 ? -(int64_t)v : (int64_t)v;
}

int32_t fist32(int64_t v) {
    if (v > 0x7FFFFFFFLL || v < -0x80000000LL) return (int32_t)0x80000000;
    return (int32_t)v;
}
int16_t fist16(int64_t v) {
    if (v > 0x7FFF || v < -0x8000) return (int16_t)0x8000;
    return (int16_t)v;
}

enum SpanKind {
    SPAN_TEX,          // Texture_draw_2: texel + light
    SPAN_TEX_LAG,      // pfncImageCopytoCanvas2 (dithered): odd rows texel(k - 1) + light
    SPAN_TRANS,        // Texture_draw_trans_4
    SPAN_TRANS_DITHER, // Texture_draw_trans_5
    SPAN_THRESH,       // Texture_draw
    SPAN_THRESH_DITHER,// Texture_draw_4
    SPAN_UPPER,        // Texture_draw_upper_threshold_trans
    SPAN_UPPER_DITHER, // Texture_draw_upper_threshold_trans_2
    SPAN_COAST,        // Texture_draw_coast
    SPAN_COAST_DITHER, // Texture_draw_coast_2
    SPAN_MASK_DEST,    // Texture_draw_mask_dest
};

struct Draw {
    SpanKind kind;
    uint8_t color = 0;      // DAT_009BB494
    uint8_t key = 0;        // DAT_009BB514 (TextureKey or threshold)
    const Texture* shape = nullptr;  // coast shape (DAT_009BB558)
    const uint8_t* mask = nullptr;   // DAT_009BB550
    const uint8_t* table = nullptr;  // DAT_009BB564
};

// The span state as the original's inner loops see it.
struct Span {
    uint8_t* dst;
    int32_t count;
    const uint8_t* tex;    // texture bits
    const uint8_t* shape;  // coast shape bits (same layout) or nullptr
    int32_t start;         // offset of the span start's integer (u, v) in the texture
    int32_t off;           // esi: offset from `start`
    uint32_t edx;          // v fraction << 16 | u step fraction
    uint16_t ufrac;        // sp
    int32_t step0, step1;  // texel offset steps without / with a v carry
    uint32_t vstep_hi;     // v step fraction << 16
    uint16_t light;        // cx: 8.8
    uint16_t dlight;
    uint8_t texel() const { return tex[start + off]; }
    uint8_t shape_texel() const { return shape[start + off]; }
};

inline void advance(Span& s) {
    uint32_t e = s.edx + s.vstep_hi;
    bool carry_v = e < s.edx;
    s.edx = e;
    uint32_t u = (uint32_t)s.ufrac + (s.edx & 0xFFFF);
    bool carry_u = u > 0xFFFF;
    s.ufrac = (uint16_t)u;
    s.off += (carry_v ? s.step1 : s.step0) + (carry_u ? 1 : 0);
}

void draw_span(const Draw& d, Span& s, bool odd_row) {
    uint8_t* p = s.dst;
    switch (d.kind) {
    case SPAN_TEX:
        for (int32_t k = 0; k < s.count; k++) {
            p[k] = (uint8_t)(s.texel() + (s.light >> 8));
            s.light = (uint16_t)(s.light + s.dlight);
            advance(s);
        }
        break;
    case SPAN_TEX_LAG:
        if (!odd_row) {
            std::memset(p, d.color, (size_t)s.count);
            break;
        }
        {
            uint8_t al = s.texel();
            for (int32_t k = 0; k < s.count; k++) {
                p[k] = (uint8_t)(al + (s.light >> 8));
                s.light = (uint16_t)(s.light + s.dlight);
                al = s.texel();  // read before the step: one texel behind
                advance(s);
            }
        }
        break;
    case SPAN_TRANS:
    case SPAN_TRANS_DITHER:
        for (int32_t k = 0; k < s.count; k++) {
            uint8_t t = s.texel();
            if (t != d.key) {
                if (d.kind == SPAN_TRANS || odd_row) p[k] = (uint8_t)(t + (s.light >> 8));
                else p[k] = d.color;
            }
            s.light = (uint16_t)(s.light + s.dlight);
            advance(s);
        }
        break;
    case SPAN_THRESH:
    case SPAN_THRESH_DITHER:
        if (d.kind == SPAN_THRESH_DITHER && !odd_row) {
            std::memset(p, d.color, (size_t)s.count);
            break;
        }
        for (int32_t k = 0; k < s.count; k++) {
            int8_t li = (int8_t)(s.light >> 8);
            if (li < (int8_t)d.key) p[k] = (uint8_t)(s.texel() + (uint8_t)li);
            s.light = (uint16_t)(s.light + s.dlight);
            advance(s);
        }
        break;
    case SPAN_UPPER:
    case SPAN_UPPER_DITHER:
        for (int32_t k = 0; k < s.count; k++) {
            uint8_t t = s.texel();
            if (t != d.key) p[k] = (d.kind == SPAN_UPPER || odd_row) ? t : d.color;
            advance(s);
        }
        break;
    case SPAN_COAST:
    case SPAN_COAST_DITHER:
        for (int32_t k = 0; k < s.count; k++) {
            uint8_t m = d.mask[s.shape_texel()];
            if (m != 0) {
                if (d.kind == SPAN_COAST_DITHER && !odd_row) p[k] = d.color;
                else if (m == 1) p[k] = (uint8_t)(s.texel() + (s.light >> 8));
                else p[k] = m;
            }
            s.light = (uint16_t)(s.light + s.dlight);
            advance(s);
        }
        break;
    case SPAN_MASK_DEST:
        for (int32_t k = 0; k < s.count; k++) {
            uint8_t m = d.mask[s.texel()];
            if (m != 0) p[k] = d.table[(m - 1) * 256 + p[k]];
            advance(s);
        }
        break;
    }
}

// The scan converter shared by all blitters.
void raster(const Texture* t, Buffer* dest, const Poly& poly, const Draw& d) {
    if (!dest || !dest->data || t->pixels.empty() || poly.n <= 0) return;
    const UiRect& clip = dest->clip;
    Poly p = poly;
    int32_t top_y = 0x7FFF, bottom_y = -0x7FFD, top = 0;
    p.bottom = 0;
    for (int32_t i = 0; i < p.n; i++) {
        int32_t y = p.pts[i * 2 + 1];
        if (y < top_y) {
            top_y = y;
            top = i;
        }
        if (y > bottom_y) {
            bottom_y = y;
            p.bottom = i;
        }
    }
    if (top_y >= bottom_y) return;
    int32_t row = top_y;           // DAT_009BB508 as a row number
    int32_t parity = top_y & 1;    // DAT_009BB4D0
    Edge l{}, r{};
    l.dir = -1;
    if (!setup_edge(p, &l, top)) return;
    r.dir = 1;
    if (!setup_edge(p, &r, top)) return;
    // Span steps (DAT_009BB4E4, _4C8, _52C) and the x87 operands of the next row.
    int32_t ustep = 0, vstep = 0;
    int16_t lstep = 0;
    auto widen = [](const Edge& a, const Edge& b) { return (b.x - a.x) << 16; };
    int32_t w = widen(l, r);
    int32_t fu = 0, fv = 0, fl = 0;
    if (w > 0) {
        fu = r.u - l.u;
        fv = r.v - l.v;
        fl = r.light - l.light;
    }
    if (row >= clip.bottom) return;
    for (;;) {
        bool done = false;
        int32_t xl = l.x, xr = r.x, ul = l.u, vl = l.v, ll = l.light;
        if (--l.count == 0) {
            if (!setup_edge(p, &l, l.end)) done = true;
        } else {
            step_edge(&l);
        }
        if (!done) {
            if (--r.count == 0) {
                if (!setup_edge(p, &r, r.end)) done = true;
            } else {
                step_edge(&r);
            }
        }
        if (w > 0) {
            Recip inv = recip(w);
            ustep = fist32(span_step(fu, inv, 16));
            vstep = fist32(span_step(fv, inv, 16));
            if (p.gouraud) lstep = fist16(span_step(fl, inv, 8));
        }
        w = widen(l, r);
        if (w > 0) {
            fu = r.u - l.u;
            fv = r.v - l.v;
            fl = r.light - l.light;
        }
        if (row >= clip.bottom) return;
        if (row >= clip.top && xr > clip.left && xl < clip.right && xr - xl > 0) {
            int32_t end = xr < clip.right ? xr : clip.right;
            if (xl < clip.left) {
                int32_t n16 = (clip.left - xl) << 16;
                ul += (int32_t)(((int64_t)ustep * n16) >> 16);
                vl += (int32_t)(((int64_t)vstep * n16) >> 16);
                ll += (int32_t)(((int64_t)(uint16_t)lstep * n16) >> 8);
                xl = clip.left;
            }
            Span s;
            s.dst = dest->data + (size_t)row * dest->stride + xl;
            s.count = end - xl;
            s.tex = t->pixels.data();
            s.shape = d.shape ? d.shape->pixels.data() : nullptr;
            s.start = t->width * (int32_t)(uint16_t)((uint32_t)vl >> 16) + (int32_t)(uint16_t)((uint32_t)ul >> 16);
            s.off = 0;
            s.light = (uint16_t)(ll >> 8);
            s.dlight = (uint16_t)lstep;
            s.step0 = (vstep >> 16) * t->width + (ustep >> 16);
            s.step1 = s.step0 + t->width;
            s.vstep_hi = (uint32_t)(uint16_t)vstep << 16;
            s.edx = ((uint32_t)(uint16_t)vl << 16) | (uint16_t)ustep;
            s.ufrac = (uint16_t)ul;
            draw_span(d, s, (parity & 1) != 0);
        }
        if (done) return;
        row++;
        parity++;
    }
}

void clamp_lights(int32_t* light, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        int32_t v = light[i];
        if (TextureLightMax <= v) v = TextureLightMax;
        if (v <= TextureLightMin) v = TextureLightMin;
        light[i] = v;
    }
}

bool any_below(const int32_t* light, int32_t n, int32_t threshold) {
    for (int32_t i = 0; i < n; i++)
        if (light[i] < threshold) return true;
    return false;
}

Poly poly(const int32_t* pts, const int32_t* uv, const int32_t* light, int32_t n, bool gouraud) {
    return Poly{pts, uv, light, n, 0, gouraud};
}

}  // namespace

void THISCALL Texture_draw_2(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                             const int32_t* uv) {
    clamp_lights(light, n);
    raster(self, dest, poly(pts, uv, light, n, true), Draw{SPAN_TEX});
}

void THISCALL pfncImageCopytoCanvas2(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                     const int32_t* uv, int32_t color) {
    if (color == -1) {
        Texture_draw_2(self, dest, pts, light, n, uv);
        return;
    }
    clamp_lights(light, n);
    Draw d{SPAN_TEX_LAG};
    d.color = (uint8_t)color;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_trans_4(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                   const int32_t* uv) {
    clamp_lights(light, n);
    Draw d{SPAN_TRANS};
    d.key = (uint8_t)TextureKey;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_trans_5(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                   const int32_t* uv, int32_t color) {
    if (color == -1) {
        Texture_draw_trans_4(self, dest, pts, light, n, uv);
        return;
    }
    clamp_lights(light, n);
    Draw d{SPAN_TRANS_DITHER};
    d.key = (uint8_t)TextureKey;
    d.color = (uint8_t)color;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                           const int32_t* uv, int32_t threshold) {
    if (!any_below(light, n, threshold)) return;
    Draw d{SPAN_THRESH};
    d.key = (uint8_t)threshold;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_4(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                             const int32_t* uv, int32_t threshold, int32_t color) {
    if (color == -1) {
        Texture_draw(self, dest, pts, light, n, uv, threshold);
        return;
    }
    if (!any_below(light, n, threshold)) return;
    Draw d{SPAN_THRESH_DITHER};
    d.key = (uint8_t)threshold;
    d.color = (uint8_t)color;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_upper_threshold_trans(Texture* self, Buffer* dest, const int32_t* pts, int32_t n,
                                                 const int32_t* uv) {
    Draw d{SPAN_UPPER};
    d.key = (uint8_t)TextureKey;
    raster(self, dest, poly(pts, uv, nullptr, n, false), d);
}

void THISCALL Texture_draw_trans_3(Texture* self, Buffer* dest, const int32_t* pts, int32_t eight) {
    // The same scan converter and span as Texture_draw_upper_threshold_trans (the two functions
    // differ only in where the vertex count and the texture points come from).
    Texture_draw_upper_threshold_trans(self, dest, pts, eight ? 8 : 4, &self->points[eight ? 4 : 0][0]);
}

void THISCALL Texture_draw_upper_threshold_trans_2(Texture* self, Buffer* dest, const int32_t* pts, int32_t n,
                                                   const int32_t* uv, int32_t color) {
    if (color == -1) {
        Texture_draw_upper_threshold_trans(self, dest, pts, n, uv);
        return;
    }
    Draw d{SPAN_UPPER_DITHER};
    d.key = (uint8_t)TextureKey;
    d.color = (uint8_t)color;
    raster(self, dest, poly(pts, uv, nullptr, n, false), d);
}

void THISCALL Texture_draw_coast(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light,
                                 const Texture* shape, const uint8_t* mask, int32_t n, const int32_t* uv) {
    if (!pts || !dest || self->pixels.empty() || !light || !mask || !shape || shape->pixels.empty()) return;
    Draw d{SPAN_COAST};
    d.shape = shape;
    d.mask = mask;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_coast_2(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light,
                                   const Texture* shape, const uint8_t* mask, int32_t n, const int32_t* uv,
                                   int32_t color) {
    if (color == -1) {
        Texture_draw_coast(self, dest, pts, light, shape, mask, n, uv);
        return;
    }
    if (!pts || !dest || self->pixels.empty() || !light || !mask || !shape || shape->pixels.empty()) return;
    Draw d{SPAN_COAST_DITHER};
    d.shape = shape;
    d.mask = mask;
    d.color = (uint8_t)color;
    raster(self, dest, poly(pts, uv, light, n, true), d);
}

void THISCALL Texture_draw_mask_dest(Texture* self, Buffer* dest, const int32_t* pts, const uint8_t* mask,
                                     const uint8_t* table, int32_t n, const int32_t* uv) {
    if (!pts || !dest || self->pixels.empty() || !mask) return;
    Draw d{SPAN_MASK_DEST};
    d.mask = mask;
    d.table = table;
    // Gouraud edges (u, v + 0x8000); the original reads stale light values, which are unused.
    raster(self, dest, poly(pts, uv, nullptr, n, true), d);
}
