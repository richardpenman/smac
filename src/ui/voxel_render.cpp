// The voxel renderer (see voxel.h): the scene's render and clear (gfx2 0x617230, 0x618470), the
// view set-up and object loop (video 0x639E60, 0x6396A0, 0x63A280), the per-object node drawing
// (0x63E950 model, 0x63F000 shadow) and the span drawers of the _SELFMOD section (0x664000
// clipped, 0x6647F0 unclipped, 0x666A80 / 0x666FD0 shadow).
//
// FP: every float step follows the original's x87 sequence (ui/x87.h); comments mark where a
// value stays in a register (double) and where the original stores it as float.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "ui/voxel.h"
#include "ui/x87.h"
#include "engine/compat.h"

namespace {

// ---- renderer state ------------------------------------------------------------------------------

struct Rect6 {  // x0, y0, x1, y1 (inclusive), width, height
    int32_t v[6];
};

// The surface (0x9BB448) and the renderer (0x9BB478, 0x80 bytes).
struct Renderer {
    uint32_t flags = 0;  // +0x00: bit 0 flips the depth
    Rect6 rect{};  // +0x08: bounds of the model (vx_bounds)
    Rect6 srect{};  // +0x20: bounds of the shadow
    Rect6 clip{};  // +0x38
    std::vector<uint8_t> colors;  // +0x50: the current shading table (0x1800)
    uint8_t shadow_table[256]{};  // +0x54
    uint8_t shadow_value = 0x80;  // +0x68
};

bool g_ready = false;
Buffer g_color;  // 0x9B9108: model canvas (key 9)
Buffer g_shadow;  // 0x9B96B0: shadow canvas (key 0)
std::vector<uint16_t> g_depth;  // 0x9B9100: 256 x 256
Renderer g_r;  // *0x9BB478
uint8_t g_default_colors[0x1800];  // 0x9B9C38
UiRect g_dirty{};  // 0x9B9690: canvas area drawn since the last clear
UiRect g_sdirty{};  // 0x9BB438: shadow area
bool g_busy = false;  // 0x9BE6C4
VoxelScene* g_scene = nullptr;  // 0x9BB470

// View (0x9C0ACC..): model space -> canvas: x, y in pixels, z in depth units.
float T[3];  // 0x9C0ACC translation
float M[9];  // 0x9C0AD8 matrix
float g_persp;  // 0x9C0AFC
float S;  // 0x9C0B00 scale
float ZS;  // 0x9C0B04 depth scale
int32_t VR[6];  // 0x9C0B08 view rectangle
float sT[3];  // 0x9C0B20 shadow translation
float sM[9];  // 0x9C0B2C shadow matrix
float s_lx, s_ly;  // 0x9C0B50, 0x9C0B54: lengths of the shadow axes
int32_t SR[6];  // 0x9C0B58 shadow rectangle
float g_proj[9] = {1.0f, 0.0f, 0.1f, 0.0f, 1.0f, 0.1f, 0.0f, 0.0f, 0.0f};  // 0x698A78

// Node matrix in 8.8 fixed point (0x63FA80) and the voxel steps (0x6404F0).
float m256[9];  // 0x9C0A7C
int16_t A[6];  // 0x9C0A40: x row (A[0..2]), y row (A[3..5])
int32_t AZ[3];  // 0x9C0A4C: z row
int32_t D[26][3];  // 0x9C0840: dx, dy (8.8), dz (16.16) per direction code
float g_inv[9];  // 0x9C0AA0
float g_lv[3];  // 0x9C0A64
const uint8_t* g_light = nullptr;  // 0x9BE6A0: light level by normal (11 bits)
int32_t g_vis_done = 0;  // 0x9C0AC8

int32_t round_away(float v) {  // x * 256 +- 0.5, __ftol (the callers pass x * 256)
    const double d = (double)v * 256.0;
    return x87_ftol(d + (d < 0.0 ? -0.5 : 0.5));
}

bool rect_empty(const UiRect& r) { return r.right <= r.left || r.bottom <= r.top; }
// IntersectRect / UnionRect (user32).
UiRect rect_intersect(const UiRect& a, const UiRect& b) {
    UiRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
             std::min(a.bottom, b.bottom)};
    if (rect_empty(r)) r = UiRect{0, 0, 0, 0};
    return r;
}
UiRect rect_union(const UiRect& a, const UiRect& b) {
    if (rect_empty(a)) return rect_empty(b) ? UiRect{0, 0, 0, 0} : b;
    if (rect_empty(b)) return a;
    return UiRect{std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
                  std::max(a.bottom, b.bottom)};
}

// ---- node matrix (0x63FA80) and steps (0x6404F0) -----------------------------------------------------

void vx_set_matrix(const float* m) {
    for (int32_t i = 0; i < 9; i++) m256[i] = (float)((double)m[i] * 256.0);
    auto r = [](float v) {
        // FP: the float m * 256 +- 0.5 in a register, __ftol.
        return x87_ftol((double)v + (v < 0.0f ? -0.5 : 0.5));
    };
    for (int32_t i = 0; i < 6; i++) A[i] = (int16_t)r(m256[i]);
    for (int32_t i = 0; i < 3; i++) AZ[i] = r(m256[6 + i]);
    g_vis_done = 0;
}

void vx_steps() {
    // The 26 neighbour steps (i, j, k) in {-1, 0, 1}^3 without the centre, in the original's order
    // (0x9C0840 + code * 16): x = i * A[0] + j * A[1] + k * A[2], y likewise with A[3..5], depth
    // with AZ (<< 8).
    static const int8_t kStep[26][3] = {{-1, -1, -1}, {-1, -1, 0}, {-1, -1, 1}, {-1, 0, -1}, {-1, 0, 0}, {-1, 0, 1},
                                        {-1, 1, -1},  {-1, 1, 0},  {-1, 1, 1},  {0, -1, -1}, {0, -1, 0}, {0, -1, 1},
                                        {0, 0, -1},   {0, 0, 1},   {0, 1, -1},  {0, 1, 0},   {0, 1, 1},  {1, -1, -1},
                                        {1, -1, 0},   {1, -1, 1},  {1, 0, -1},  {1, 0, 0},   {1, 0, 1},  {1, 1, -1},
                                        {1, 1, 0},    {1, 1, 1}};
    for (int32_t d = 0; d < 26; d++) {
        const int32_t i = kStep[d][0], j = kStep[d][1], k = kStep[d][2];
        D[d][0] = i * A[0] + j * A[1] + k * A[2];
        D[d][1] = i * A[3] + j * A[4] + k * A[5];
        D[d][2] = (i * AZ[0] + j * AZ[1] + k * AZ[2]) * 0x100;
    }
}

// 0x6401B0: canvas bounds of a voxel box (min corner p, size d) under the node matrix, relative
// to the node's position, 2 pixels of margin.
void vx_box_bounds(const int16_t* p, const int16_t* d, int32_t* out) {
    auto R = [](int32_t v) { return (v + (v >= 0 ? 0x80 : -0x80)) >> 8; };
    int32_t x = R(A[0] * p[0] + A[2] * p[2] + A[1] * p[1]);
    int32_t y = R(A[3] * p[0] + A[5] * p[2] + A[4] * p[1]);
    const int32_t xx = (int16_t)R(d[0] * A[0]), yx = (int16_t)R(d[0] * A[3]);
    const int32_t xy = (int16_t)R(d[1] * A[1]), yy = (int16_t)R(d[1] * A[4]);
    const int32_t xz = (int16_t)R(d[2] * A[2]), yz = (int16_t)R(d[2] * A[5]);
    out[4] = out[5] = 0;
    x += xx;
    y += yx;
    out[0] = 0x7FFF;
    out[1] = 0x7FFF;
    out[2] = -0x7FFF;
    out[3] = -0x7FFF;
    if (x < 0x7FFF) out[0] = x;
    if (-0x7FFF < x) out[2] = x;
    if (y < 0x7FFF) out[1] = y;
    if (-0x7FFF < y) out[3] = y;
    auto add = [&](int32_t cx, int32_t cy) {
        if (cx < out[0]) out[0] = cx;
        if (out[2] < cx) out[2] = cx;
        if (cy < out[1]) out[1] = cy;
        if (out[3] < cy) out[3] = cy;
    };
    x += xy, y += yy, add(x, y);
    x -= xx, y -= yx, add(x, y);
    x -= xy, y -= yy, add(x, y);
    x += xz, y += yz, add(x, y);
    x += xx, y += yx, add(x, y);
    x += xy, y += yy, add(x, y);
    x -= xx, y -= yx, add(x, y);
    out[0] -= 2;
    out[1] -= 2;
    out[3] += 2;
    out[2] += 2;
    if (out[2] >= 0 && out[3] >= 0) {
        out[4] = out[2] - out[0] + 1;
        out[5] = out[3] - out[1] + 1;
    } else {
        out[4] = out[5] = 0;
    }
}

// 0x63FCA0: the light table for the node: the light direction in the node's voxel space, as a
// latitude (acos of z, 24 steps) and a longitude (89 steps) into the static tables at 0x699100.
void vx_select_light(const float* m, const float* light, float s, const float* cam) {
    const float sx = (float)((double)s * S);
    const float sz = (float)((double)s * ZS);
    mat3_inverse(m, g_inv);
    float v[3];
    mat3_apply(cam, light, v);
    v[0] = (float)((double)sx * v[0]);
    v[1] = (float)((double)sx * v[1]);
    v[2] = (float)((double)sz * v[2]);
    mat3_apply(g_inv, v, g_lv);
    if (g_lv[2] < 0.0f) {
        g_lv[0] = -g_lv[0];
        g_lv[1] = -g_lv[1];
        g_lv[2] = -g_lv[2];
    }
    const ext lat = g_lv[2] < 1.0f ? crt_acos(g_lv[2]) : (ext)0.0;
    ext lon = x87_fpatan((ext)g_lv[1], (ext)g_lv[0]);
    double lon_d = 0;
    bool lon_rounded = false;
    if (lon < 0) {
        lon_d = x87_sub(lon, (ext)-6.2831854820251465);  // fsub -2pi (float)
        lon_rounded = true;
    }
    const int32_t i = x87_ftol(0.5 - x87_mul(lat, (ext)-14.642254829406738f));
    int32_t j =
        x87_ftol(0.5 - (lon_rounded ? lon_d * (double)-14.164789199829102f : x87_mul(lon, (ext)-14.164789199829102f)));
    if (j == 0x59) j = 0;
    uint32_t table = 0;
    std::memcpy(&table, mem::host(0x6A5A00 + 4 * (uint32_t)i), 4);
    g_light = mem::host(table) + (0x59 - j);
}

// ---- span drawers -----------------------------------------------------------------------------------
// A node's voxels are a stream of 3-byte records: direction code << 3 | normal bits 8..10,
// normal bits 0..7, colour. Each record steps from the previous voxel to a neighbour (D[code])
// and draws it: 2x2 pixels (1 when small) where the depth (16 bits, smaller is nearer) passes,
// in the shading table's colour for (light level of the normal, colour). The canvas index is
// y * 256 + x (the depth buffer has the same layout).
// Segmented streams (node flag 4 / part flag 1: runs with visibility masks and absolute
// starts) are not used by the game's models and are not reconstructed.

struct SpanArgs {
    int32_t x, y, z;  // 8.8, 8.8, depth (<< 8 inside)
    int32_t count;
    const uint8_t* data;
    uint32_t flags;  // 1 small, 0x10 segmented, 0x20 skip the last voxel
};

inline void plot(uint32_t idx, uint16_t z, uint8_t c) {
    // (A splat at the canvas' last column or row writes past it in the original.)
    if (idx >= 0x10000) return;
    if (z < g_depth[idx]) {
        g_depth[idx] = z;
        g_color.data[idx] = c;
    }
}

// 0x6647F0 (unclipped) and 0x664000 (clipped to clip[0..3]).
void span_model(const SpanArgs& a, const int32_t* clip) {
    if (a.flags & 0x10) return;
    const uint8_t* colors = g_r.colors.data();
    uint32_t n = (uint32_t)a.count;
    if (a.flags & 0x20) n--;
    if (n == 0) return;  // (the original loops 2^32 times)
    int32_t x = a.x, y = a.y;
    uint32_t z = (uint32_t)a.z << 8;
    const uint8_t* p = a.data;
    const bool small = a.flags & 1;
    const int32_t x0 = clip ? clip[0] : 0, y0 = clip ? clip[1] : 0, x1 = clip ? clip[2] : 0, y1 = clip ? clip[3] : 0;
    do {
        const uint32_t code = p[0] & 0xF8u;
        const uint32_t normal = ((uint32_t)p[0] << 8 | p[1]) & 0x7FF;
        const int32_t* d = D[code >> 3];
        x += d[0];
        y += d[1];
        z += (uint32_t)d[2];
        const uint8_t c = colors[(uint32_t)g_light[normal] << 8 | p[2]];
        const uint16_t zz = (uint16_t)(z >> 16);
        int32_t cx = x >> 8, cy = y >> 8;
        if (!small) {
            bool draw = true;
            if (clip && !(cx < x1 && cx >= x0 && cy < y1 && cy >= y0)) {
                // A 2x2 splat over the edge: moved one pixel in.
                if (cx > x1 || cx < x0 - 1 || cy > y1 || cy < y0 - 1) draw = false;
                else {
                    if (cx >= x1) cx--;
                    else if (cx <= x0 - 1) cx++;
                    if (cy >= y1) cy--;
                    else if (cy <= y0 - 1) cy++;
                }
            }
            if (draw) {
                const uint32_t i = (uint32_t)(cy * 256 + cx);
                plot(i, zz, c);
                plot(i + 1, zz, c);
                plot(i + 256, zz, c);
                plot(i + 257, zz, c);
            }
        } else if (!clip || (cy < y1 && cy >= y0 && cx < x1 && cx >= x0)) {
            plot((uint32_t)(cy * 256 + cx), zz, c);
        }
        p += 3;
    } while (--n);
}

// 0x666FD0 (unclipped) and 0x666A80 (clipped): the shadow, no depth.
void span_shadow(const SpanArgs& a, const int32_t* clip) {
    if (a.flags & 0x10) return;
    uint32_t n = (uint32_t)a.count;
    if (a.flags & 0x20) n--;
    if (n == 0) return;
    int32_t x = a.x, y = a.y;
    const uint8_t* p = a.data;
    const bool small = a.flags & 1;
    const uint8_t v = g_r.shadow_value;
    const int32_t x0 = clip ? clip[0] : 0, y0 = clip ? clip[1] : 0, x1 = clip ? clip[2] : 0, y1 = clip ? clip[3] : 0;
    uint8_t* s = g_shadow.data;
    do {
        const int32_t* d = D[(p[0] & 0xF8u) >> 3];
        x += d[0];
        y += d[1];
        int32_t cx = x >> 8, cy = y >> 8;
        if (!small) {
            bool draw = true;
            if (clip && !(cx < x1 && cx >= x0 && cy < y1 && cy >= y0)) {
                if (cx > x1 || cx < x0 - 1 || cy > y1 || cy < y0 - 1) draw = false;
                else {
                    if (cx >= x1) cx--;
                    else if (cx <= x0 - 1) cx++;
                    if (cy >= y1) cy--;
                    else if (cy <= y0 - 1) cy++;
                }
            }
            if (draw) {
                const uint32_t i = (uint32_t)(cy * 256 + cx);
                for (uint32_t k : {i, i + 1, i + 257, i + 256})
                    if (k < 0x10000) s[k] = v;
            }
        } else if (!clip || (cy < y1 && cy >= y0 && cx < x1 && cx >= x0)) {
            if ((uint32_t)(cy * 256 + cx) < 0x10000) s[cy * 256 + cx] = v;
        }
        p += 3;
    } while (--n);
}

// ---- node drawing -------------------------------------------------------------------------------------

int32_t rd32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
int16_t rd16(const uint8_t* p) {
    int16_t v;
    std::memcpy(&v, p, 2);
    return v;
}
float rdf(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

// Node layout: +0 flags (1 next level of detail, 2 parts, 4 segmented, 8 skip the last voxel),
// +8 scale, +0xC offset of the next level, +0x14 box size (3 shorts), +0x1A box minimum,
// +0x20 centre, +0x26 voxel count, +0x2A voxels (or the parts: +0 / +6 corners, +0xC box size,
// +0x12 offset, +0x18 count, +0x1C flags, +0x20 voxels).

// The clip rectangle of the node drawers: the renderer's clip within the view rectangle.
void node_clip(const int32_t* view, int32_t* L) {
    for (int32_t i = 0; i < 6; i++) L[i] = g_r.clip.v[i];
    if (view[4] != 0 && view[5] != 0) {
        if (L[0] < view[0]) L[0] = view[0];
        if (L[1] < view[1]) L[1] = view[1];
        if (view[2] < L[2]) L[2] = view[2];
        if (view[3] < L[3]) L[3] = view[3];
    }
    if (L[2] < 0 || L[3] < 0) {
        L[4] = L[5] = 0;
    } else {
        L[4] = L[2] - L[0] + 1;
        L[5] = L[3] - L[1] + 1;
    }
}

bool outside(const int32_t* B, const int32_t* L) { return B[2] < L[0] || B[3] < L[1] || B[0] > L[2] || B[1] > L[3]; }
bool inside(const int32_t* B, const int32_t* L) { return B[0] >= L[0] && B[1] >= L[1] && B[2] <= L[2] && B[3] <= L[3]; }

// A part's position: matrix * (corner + offset) + pos.
void part_pos(const uint8_t* q, const float* m, const float* pos, float* out) {
    float c[3] = {(float)(rd16(q) + rd16(q + 0x12)), (float)(rd16(q + 2) + rd16(q + 0x14)),
                  (float)(rd16(q + 4) + rd16(q + 0x16))};
    float t[3];
    mat3_apply(m, c, t);
    vec3_add(pos, t, out);
}
void part_box(const uint8_t* q, int32_t* B) {
    const int16_t d[3] = {(int16_t)(rd16(q + 6) - rd16(q + 0x12)), (int16_t)(rd16(q + 8) - rd16(q + 0x14)),
                          (int16_t)(rd16(q + 10) - rd16(q + 0x16))};
    const int16_t size[3] = {rd16(q + 0xC), rd16(q + 0xE), rd16(q + 0x10)};
    vx_box_bounds(d, size, B);
}
const uint8_t* part_next(const uint8_t* q) {
    const int32_t n = rd32(q + 0x18);
    return (q[0x1C] & 1) ? q + rd32(q + 0x20) + 0x20 : q + n * 3 + 0x20;
}

// 0x63E950: draws a node of an object at pos (canvas space) with the node matrix m.
void vx_draw_node(const uint8_t* node, const float* pos, const float* m, const float* cam, const float* light,
                  float scale, int32_t small) {
    vx_set_matrix(m);
    vx_select_light(m, light, scale, cam);
    int32_t L[6];
    node_clip(VR, L);
    vx_steps();
    uint32_t flags = small != 0;
    g_vis_done = 0;
    const uint32_t nf = (uint32_t)rd32(node);
    int32_t B[6];
    if (nf & 2) {
        B[0] = B[1] = 0x7FFF;
        B[2] = B[3] = -0x7FFF;
        B[4] = B[5] = 0;
        const uint8_t* q = node + 0x2A;
        const uint32_t total = (uint32_t)rd32(node + 0x26);
        for (uint32_t done = 0; done < total;) {
            float P[3];
            part_pos(q, m, pos, P);
            part_box(q, B);
            const int32_t X = round_away(P[0]), Y = round_away(P[1]), Z = round_away(P[2]);
            const int32_t ix = x87_ftol(P[0]), iy = x87_ftol(P[1]);
            if (B[4] != 0 && B[5] != 0) {
                B[0] += ix;
                B[1] += iy;
                B[2] += ix;
                B[3] += iy;
            }
            if (q[0x1C] & 1) flags |= 0x10;  // (segmented: not reconstructed)
            else flags &= ~0x10u;
            if (!outside(B, L)) {
                if (nf & 8) flags |= 0x20;
                else flags &= ~0x20u;
                SpanArgs a{X, Y, Z, rd32(q + 0x18), q + 0x20, flags};
                span_model(a, inside(B, L) ? nullptr : L);
            }
            done += (uint32_t)rd32(q + 0x18);
            q = part_next(q);
        }
        return;
    }
    int32_t X, Y, Z;
    if (nf & 4) {
        X = round_away(pos[0]);
        Y = round_away(pos[1]);
        Z = round_away(pos[2]);
    } else {
        const float c[3] = {(float)rd16(node + 0x20), (float)rd16(node + 0x22), (float)rd16(node + 0x24)};
        float t[3], P[3];
        mat3_apply(m, c, t);
        vec3_add(pos, t, P);
        X = round_away(P[0]);
        Y = round_away(P[1]);
        Z = round_away(P[2]);
    }
    const int32_t ix = x87_ftol(pos[0]), iy = x87_ftol(pos[1]);
    const int16_t bmin[3] = {rd16(node + 0x1A), rd16(node + 0x1C), rd16(node + 0x1E)};
    const int16_t bsize[3] = {rd16(node + 0x14), rd16(node + 0x16), rd16(node + 0x18)};
    vx_box_bounds(bmin, bsize, B);
    if (B[4] != 0 && B[5] != 0) {
        B[0] += ix;
        B[1] += iy;
        B[2] += ix;
        B[3] += iy;
    }
    if (nf & 4) flags |= 0x10;
    if (outside(B, L)) return;
    if (nf & 8) flags |= 0x20;
    SpanArgs a{X, Y, Z, rd32(node + 0x26), node + 0x2A, flags};
    span_model(a, inside(B, L) ? nullptr : L);
}

// 0x63F000: the shadow of a node (m: the shadow matrix of the object).
void vx_draw_shadow(const uint8_t* node, const float* pos, const float* m, int32_t small) {
    vx_set_matrix(m);
    int32_t L[6];
    node_clip(SR, L);
    vx_steps();
    uint32_t flags = small != 0;
    g_vis_done = 0;
    const uint32_t nf = (uint32_t)rd32(node);
    int32_t B[6];
    if (nf & 2) {
        B[0] = B[1] = 0x7FFF;
        B[2] = B[3] = -0x7FFF;
        B[4] = B[5] = 0;
        const uint8_t* q = node + 0x2A;
        const uint32_t total = (uint32_t)rd32(node + 0x26);
        for (uint32_t done = 0; done < total;) {
            float P[3];
            part_pos(q, m, pos, P);
            part_box(q, B);
            const int32_t X = round_away(P[0]), Y = round_away(P[1]);
            const int32_t ix = x87_ftol(P[0]), iy = x87_ftol(P[1]);
            if (B[4] != 0 && B[5] != 0) {
                B[0] += ix;
                B[1] += iy;
                B[2] += ix;
                B[3] += iy;
            }
            if (q[0x1C] & 1) flags |= 0x10;
            else flags &= ~0x10u;
            if (!outside(B, L)) {
                if (nf & 8) flags |= 0x20;
                else flags &= ~0x20u;
                SpanArgs a{X, Y, 0, rd32(q + 0x18), q + 0x20, flags};
                span_shadow(a, inside(B, L) ? nullptr : L);
            }
            done += (uint32_t)rd32(q + 0x18);
            q = part_next(q);
        }
        return;
    }
    int32_t X, Y;
    if (nf & 4) {
        X = round_away(pos[0]);
        Y = round_away(pos[1]);
    } else {
        const float c[3] = {(float)rd16(node + 0x20), (float)rd16(node + 0x22), (float)rd16(node + 0x24)};
        float t[3], P[3];
        mat3_apply(m, c, t);
        vec3_add(pos, t, P);
        X = round_away(P[0]);
        Y = round_away(P[1]);
    }
    const int32_t ix = x87_ftol(pos[0]), iy = x87_ftol(pos[1]);
    const int16_t bmin[3] = {rd16(node + 0x1A), rd16(node + 0x1C), rd16(node + 0x1E)};
    const int16_t bsize[3] = {rd16(node + 0x14), rd16(node + 0x16), rd16(node + 0x18)};
    vx_box_bounds(bmin, bsize, B);
    if (B[4] != 0 && B[5] != 0) {
        B[0] += ix;
        B[1] += iy;
        B[2] += ix;
        B[3] += iy;
    }
    if (nf & 4) flags |= 0x10;
    if (outside(B, L)) return;
    if (nf & 8) flags |= 0x20;
    SpanArgs a{X, Y, 0, rd32(node + 0x26), node + 0x2A, flags};
    span_shadow(a, inside(B, L) ? nullptr : L);
}

// 0x63F5E0: the canvas bounds of a node (within the clip and the rectangle r).
void vx_node_bounds(const uint8_t* node, const float* pos, const float* m, const int32_t* r, int32_t* out) {
    out[4] = out[5] = 0;
    out[0] = out[1] = 0x7FFF;
    out[2] = out[3] = -0x7FFF;
    vx_set_matrix(m);
    int32_t L[6];
    for (int32_t i = 0; i < 6; i++) L[i] = g_r.clip.v[i];
    if (r[4] != 0 && r[5] != 0) {
        if (L[0] < r[0]) L[0] = r[0];
        if (L[1] < r[1]) L[1] = r[1];
        if (r[2] < L[2]) L[2] = r[2];
        if (r[3] < L[3]) L[3] = r[3];
    }
    const uint32_t nf = (uint32_t)rd32(node);
    if (!(nf & 2)) {
        const int32_t ix = x87_ftol(pos[0]), iy = x87_ftol(pos[1]);
        const int16_t bmin[3] = {rd16(node + 0x1A), rd16(node + 0x1C), rd16(node + 0x1E)};
        const int16_t bsize[3] = {rd16(node + 0x14), rd16(node + 0x16), rd16(node + 0x18)};
        vx_box_bounds(bmin, bsize, out);
        if (out[4] != 0 && out[5] != 0) {
            out[0] += ix;
            out[1] += iy;
            out[2] += ix;
            out[3] += iy;
        }
        if (out[2] < L[0] || out[3] < L[1] || L[2] < out[0] || L[3] < out[1]) {
            out[2] = out[3] = -0x7FFF;
            out[0] = out[1] = 0x7FFF;
            out[4] = out[5] = 0;
            return;
        }
        if (out[0] < L[0]) out[0] = L[0];
        if (out[1] < L[1]) out[1] = L[1];
        if (L[2] < out[2]) out[2] = L[2];
        if (L[3] < out[3]) out[3] = L[3];
    } else {
        int32_t B[6] = {0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF, 0, 0};
        const uint8_t* q = node + 0x2A;
        const uint32_t total = (uint32_t)rd32(node + 0x26);
        for (uint32_t done = 0; done < total;) {
            float P[3];
            part_pos(q, m, pos, P);
            part_box(q, B);
            const int32_t ix = x87_ftol(P[0]), iy = x87_ftol(P[1]);
            if (B[4] != 0 && B[5] != 0) {
                B[1] += iy;
                B[0] += ix;
                B[2] += ix;
                B[3] += iy;
            }
            if (L[0] <= B[2] && L[1] <= B[3] && B[0] <= L[2] && B[1] <= L[3]) {
                if (B[0] < L[0]) B[0] = L[0];
                if (B[1] < L[1]) B[1] = L[1];
                if (L[2] < B[2]) B[2] = L[2];
                if (L[3] < B[3]) B[3] = L[3];
                if (B[2] < 0 || B[3] < 0) {
                    B[5] = B[4] = 0;
                } else {
                    B[4] = B[2] - B[0] + 1;
                    B[5] = B[3] - B[1] + 1;
                }
                if (B[4] != 0 && B[5] != 0) {
                    if (B[0] < out[0]) out[0] = B[0];
                    if (B[1] < out[1]) out[1] = B[1];
                    if (out[2] < B[2]) out[2] = B[2];
                    if (out[3] < B[3]) out[3] = B[3];
                }
            }
            done += (uint32_t)rd32(q + 0x18);
            q = part_next(q);
        }
    }
    if (out[2] >= 0 && out[3] >= 0) {
        out[4] = out[2] - out[0] + 1;
        out[5] = out[3] - out[1] + 1;
    } else {
        out[4] = out[5] = 0;
    }
}

// ---- view ----------------------------------------------------------------------------------------------

// 0x6385D0: the level of detail for scale s.
const uint8_t* vx_lod(const uint8_t* node, float s) {
    const float lim = 1.4142f;  // 0x670FE8
    while ((node[0] & 1) && (double)rdf(node + 8) * s < lim) {
        const uint8_t* next = node + rd32(node + 0xC);
        if (!((double)rdf(next + 8) * s <= lim)) break;
        node = next;
    }
    return node;
}

// 0x6282E0: culled (true) when pos is out of the depth range or more than the node's reach
// outside the camera's canvas.
bool vx_cull(const float* p, float scale, const VoxelScene* sc) {
    if (p[2] < sc->c_zmin) return true;
    if (p[2] > sc->c_zmax) return true;
    const float s = (float)(((double)scale * S) * 128.0);
    const float ns = -s;
    if (p[0] < ns) return true;
    if (p[0] > (double)sc->c_clip[4] + s) return true;
    if (p[1] < ns) return true;
    if (p[1] > (double)sc->c_clip[5] + s) return true;
    return false;
}

// floor(i * f), __ftol.
int32_t floor_mul(int32_t i, float f) { return x87_ftol(crt_floor((double)i * f)); }

// 0x6396A0: the view and shadow transforms for the scene.
void vx_setup_view(VoxelScene* sc, int32_t frame) {
    mat3_mul(sc->c_mat, sc->v_mat, M);
    for (int32_t i = 0; i < 9; i++) M[i] = (float)((double)sc->v_scale * M[i]);
    float tmp[3], p0[3];
    vec3_sub(sc->v_pos, sc->c_pos, tmp);
    mat3_apply(sc->c_mat, tmp, T);
    mat3_apply(M, sc->objects[0].pos + frame * 3, p0);
    vec3_add(p0, T, p0);
    if (p0[2] <= 0.0f || sc->c_persp <= 0.0f) g_persp = 1.0f;
    else g_persp = (float)(1.0 / ((double)p0[2] * sc->c_persp - -1.0));
    S = (float)(((double)sc->c_scale * sc->v_zoom) * g_persp);
    ZS = (float)(((double)sc->c_zmax - sc->c_zmin) / ((double)sc->c_far - sc->c_near));
    for (int32_t i = 0; i < 6; i++) M[i] = (float)((double)M[i] * S);
    T[0] = (float)((double)T[0] * S);
    T[1] = (float)((double)T[1] * S);
    const float hw = (float)((double)(sc->c_clip[4] >> 1) - -0.5);
    const float hh = (float)((double)(sc->c_clip[5] >> 1) - -0.5);
    T[0] = (float)((((double)sc->v_offset[0] * sc->v_zoom) + sc->v_shift[0]) + hw + T[0]);
    T[1] = (float)((((double)sc->v_offset[1] * sc->v_zoom) + sc->v_shift[1]) + hh + T[1]);
    T[2] = (float)((double)T[2] - sc->c_near);
    double base;
    if (g_r.flags & 1) {
        M[6] = (float)-((double)M[6] * ZS);
        M[7] = (float)-((double)M[7] * ZS);
        M[8] = (float)-((double)M[8] * ZS);
        T[2] = (float)-((double)T[2] * ZS);
        base = (double)sc->v_offset[2] + sc->c_zmax;
    } else {
        M[6] = (float)((double)M[6] * ZS);
        M[7] = (float)((double)M[7] * ZS);
        M[8] = (float)((double)M[8] * ZS);
        T[2] = (float)((double)T[2] * ZS);
        base = (double)sc->v_offset[2] + sc->c_zmin;
    }
    T[2] = (float)(base + T[2]);
    S = (float)((double)sc->v_scale * S);
    ZS = (float)((double)sc->v_scale * ZS);
    VR[0] = floor_mul(sc->c_clip[0], sc->v_zoom);
    VR[1] = floor_mul(sc->c_clip[1], sc->v_zoom);
    VR[4] = floor_mul(sc->c_clip[4] + 1, sc->v_zoom) + 1;
    VR[5] = floor_mul(sc->c_clip[5] + 1, sc->v_zoom) + 1;
    VR[2] = VR[0] + VR[4] - 1;
    VR[3] = VR[1] + VR[5] - 1;
    if (!sc->v_shadows || g_shadow.data == nullptr) return;
    // The shadow: the objects projected along the shadow direction onto the plane z = 0.
    float& sz = sc->v_shadow[2];
    if (-1e-06f < sz && sz < 0.0f) sz = -1e-06f;  // 0xB58637BD
    else if (0.0f <= sz && sz < 1e-06f) sz = 1e-06f;
    const double inv = 1.0 / (double)sz;
    const double fx = (double)sc->v_shadow[0] * inv, fy = (double)sc->v_shadow[1] * inv;
    const float ax = (float)fx, ay = (float)fy;
    g_proj[2] = (float)-fx;
    g_proj[5] = (float)-fy;
    mat3_apply(g_proj, sc->v_pos, sT);
    sT[0] = (float)((double)ax * sc->v_shadow_z + sT[0]);
    sT[1] = (float)((double)ay * sc->v_shadow_z + sT[1]);
    sT[2] = (float)((double)sc->v_shadow_z + sT[2]);
    vec3_sub(sT, sc->c_pos, tmp);
    mat3_apply(sc->c_mat, tmp, sT);
    float cp[9];
    mat3_mul(sc->c_mat, g_proj, cp);
    mat3_mul(cp, sc->v_mat, sM);
    const double k = ((double)sc->v_scale * sc->c_scale) * g_persp;
    const double kx = k * sc->v_shadow_zoom[0];
    sT[0] = (float)(kx * sT[0]);
    const float m0 = (float)(kx * sM[0]), m1 = (float)(kx * sM[1]), m2 = (float)(kx * sM[2]);
    sM[0] = m0;
    sM[1] = m1;
    sM[2] = m2;
    const double ky = k * sc->v_shadow_zoom[1];
    sT[1] = (float)(ky * sT[1]);
    sM[3] = (float)(ky * sM[3]);
    sM[4] = (float)(ky * sM[4]);
    sM[5] = (float)(ky * sM[5]);
    sT[0] = (float)((((double)sc->v_offset[0] * sc->v_shadow_zoom[0]) + sc->v_shadow_shift[0]) + hw + sT[0]);
    sT[1] = (float)((((double)sc->v_offset[1] * sc->v_shadow_zoom[1]) + sc->v_shadow_shift[1]) + hh + sT[1]);
    s_lx = (float)crt_hypot(sM[0], sM[2]);
    // BUG(original): the y axis length uses the elements 4 and 5 (x: 0 and 2).
    const double ry = g_compat.voxel_y_axis_elements ? crt_hypot(sM[4], sM[5]) : crt_hypot(sM[3], sM[5]);
    s_ly = (float)ry;
    if (s_lx < 1e-06f) s_lx = 1e-06f;
    if (ry < (double)1e-06f) s_ly = 1e-06f;
    SR[0] = floor_mul(sc->c_clip[0], sc->v_shadow_zoom[0]);
    SR[1] = floor_mul(sc->c_clip[1], sc->v_shadow_zoom[1]);
    SR[4] = floor_mul(sc->c_clip[4] + 1, sc->v_shadow_zoom[0]) + 1;
    SR[5] = floor_mul(sc->c_clip[5] + 1, sc->v_shadow_zoom[1]) + 1;
    SR[2] = SR[0] + SR[4] - 1;
    SR[3] = SR[1] + SR[5] - 1;
}

// The node of object i at its level of detail (nullptr: no mesh).
const uint8_t* object_node(const VoxelScene* sc, int32_t i) {
    const uint8_t* mesh = sc->meshes[i].data;
    return mesh ? vx_lod(mesh, S) : nullptr;
}

// The shadow test of 0x639E60 / 0x63A280: pos within the shadow canvas, with the node's reach.
bool shadow_visible(const float* p, float scale, const VoxelScene* sc) {
    const float s = (float)((double)scale * 128.0);
    const double tx = (double)s * s_lx;
    if (!(-tx <= p[0])) return false;
    if (!(p[0] <= (double)sc->c_clip[4] + tx)) return false;
    const double ty = (double)s * s_ly;
    if (p[1] < -ty) return false;
    if (!(p[1] <= (double)sc->c_clip[5] + ty)) return false;
    return true;
}

// 0x63A280: the canvas bounds of the scene (model and shadow).
void vx_bounds(VoxelScene* sc, int32_t frame) {
    int32_t a[6] = {0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF, 0, 0}, b[6] = {0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF, 0, 0};
    for (int32_t i = 0; i < sc->v_count; i++) {
        const VoxelObject& o = sc->objects[i];
        if (o.flag[frame] & 0x80) continue;
        const uint8_t* node = object_node(sc, i);
        if (!node) continue;
        const float scale = rdf(node + 8);
        float P[3], m[9];
        mat3_apply(M, o.pos + frame * 3, P);
        vec3_add(P, T, P);
        int32_t r[6];
        if (!vx_cull(P, scale, sc)) {
            mat3_mul(M, o.mat + frame * 9, m);
            for (float& v : m) v = (float)((double)v * scale);
            vx_node_bounds(node, P, m, VR, r);
            if (r[4] != 0 && r[5] != 0) {
                if (r[0] < a[0]) a[0] = r[0];
                if (r[1] < a[1]) a[1] = r[1];
                if (a[2] < r[2]) a[2] = r[2];
                if (a[3] < r[3]) a[3] = r[3];
            }
        }
        if (sc->v_shadows && g_shadow.data) {
            mat3_apply(sM, o.pos + frame * 3, P);
            vec3_add(P, sT, P);
            if (shadow_visible(P, scale, sc)) {
                mat3_mul(sM, o.mat + frame * 9, m);
                for (int32_t k = 0; k < 6; k++) m[k] = (float)((double)m[k] * scale);
                vx_node_bounds(node, P, m, SR, r);
                if (r[4] != 0 && r[5] != 0) {
                    if (r[0] < b[0]) b[0] = r[0];
                    if (r[1] < b[1]) b[1] = r[1];
                    if (b[2] < r[2]) b[2] = r[2];
                    if (b[3] < r[3]) b[3] = r[3];
                }
            }
        }
    }
    if (a[2] < 0 || a[3] < 0) a[4] = a[5] = 0;
    else {
        a[4] = a[2] - a[0] + 1;
        a[5] = a[3] - a[1] + 1;
    }
    if (b[2] < 0 || b[3] < 0) b[4] = b[5] = 0;
    else {
        b[4] = b[2] - b[0] + 1;
        b[5] = b[3] - b[1] + 1;
    }
    std::memcpy(g_r.rect.v, a, sizeof(a));
    std::memcpy(g_r.srect.v, b, sizeof(b));
}

// 0x639E60: renders the scene's objects into the canvases.
void vx_render(VoxelScene* sc, int32_t frame) {
    if (g_busy) return;  // (the original hangs)
    g_busy = true;
    vx_setup_view(sc, frame);
    vx_bounds(sc, frame);
    for (int32_t i = 0; i < sc->v_count; i++) {
        const VoxelObject& o = sc->objects[i];
        if (o.flag[frame] & 0x80) continue;
        const uint8_t* node = object_node(sc, i);
        if (!node) continue;
        const float scale = rdf(node + 8);
        float P[3], m[9];
        mat3_apply(M, o.pos + frame * 3, P);
        vec3_add(P, T, P);
        const int32_t small = (double)S * scale <= 0.5;
        if (!vx_cull(P, scale, sc)) {
            mat3_mul(M, o.mat + frame * 9, m);
            for (float& v : m) v = (float)((double)v * scale);
            // The renderer's object callback (0x618DA0): the object's shading table.
            const uint8_t* colors = (sc->flags & 8) ? g_default_colors : sc->colors[i];
            if (colors) std::memcpy(g_r.colors.data(), colors, 0x1800);
            vx_draw_node(node, P, m, sc->c_mat, sc->v_light, scale, small);
        }
        if (sc->v_shadows && g_shadow.data) {
            mat3_apply(sM, o.pos + frame * 3, P);
            vec3_add(P, sT, P);
            if (shadow_visible(P, scale, sc)) {
                mat3_mul(sM, o.mat + frame * 9, m);
                for (int32_t k = 0; k < 6; k++) m[k] = (float)((double)m[k] * scale);
                vx_draw_shadow(node, P, m, small);
            }
        }
    }
    g_busy = false;
}

// The blits of Scene_render at scale 1: Buffer_draw_2 (0x5D84C0: every pixel but `key`
// copied) and Buffer_draw_dest_source_2 (0x5D8EE0: dest = table[dest] where the source is not
// `key`), clipped to dest's clip rectangle.
void blit_key(const Buffer* src, Buffer* dest, int32_t key, int32_t x, int32_t y) {
    UiRect r = rect_intersect(UiRect{x, y, x + src->width, y + src->height}, dest->clip);
    if (rect_empty(r)) return;
    for (int32_t py = r.top; py < r.bottom; py++) {
        const uint8_t* s = src->data + (py - y) * src->stride + (r.left - x);
        uint8_t* d = dest->data + py * dest->stride + r.left;
        for (int32_t px = r.left; px < r.right; px++, s++, d++)
            if (*s != (uint8_t)key) *d = *s;
    }
}
void blit_table(const Buffer* src, Buffer* dest, int32_t key, int32_t x, int32_t y, const uint8_t* table) {
    UiRect r = rect_intersect(UiRect{x, y, x + src->width, y + src->height}, dest->clip);
    if (rect_empty(r)) return;
    for (int32_t py = r.top; py < r.bottom; py++) {
        const uint8_t* s = src->data + (py - y) * src->stride + (r.left - x);
        uint8_t* d = dest->data + py * dest->stride + r.left;
        for (int32_t px = r.left; px < r.right; px++, s++, d++)
            if (*s != (uint8_t)key) *d = table[*d];
    }
}

}  // namespace

void voxel_renderer_init() {
    if (g_ready) return;
    g_ready = true;
    Buffer_ctor(&g_color);
    Buffer_ctor(&g_shadow);
    Buffer_init(&g_color, 0x100, 0x100, 0, 0);
    Buffer_init(&g_shadow, 0x100, 0x100, 0, 0);
    g_depth.assign(0x10000, 0xFFFF);
    Buffer_fill_2(&g_color, 9);
    Buffer_fill_2(&g_shadow, 0);
    // 0x6393C0: the renderer (flags 0, the default tables, shadow value 0x80).
    g_r.flags = 0;
    g_r.colors.assign(g_default_colors, g_default_colors + 0x1800);
    std::memcpy(g_r.shadow_table, g_default_colors, 0x100);
    g_r.shadow_value = 0x80;
    g_r.rect = Rect6{{0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF, 0, 0}};
    g_r.srect = g_r.rect;
    g_r.clip = Rect6{{0, 0, 0xFF, 0xFF, 0x100, 0x100}};
    g_dirty = UiRect{0, 0, 0, 0};
    g_sdirty = UiRect{0, 0, 0, 0};
}

int32_t THISCALL Scene_set_shadow_table(VoxelScene*, const uint8_t* table) {
    if (!g_ready) return 7;
    std::memcpy(g_r.shadow_table, table, 0x100);
    return 0;
}

void THISCALL Scene_clear(VoxelScene*, int32_t keep_depth) {
    if (!g_ready) return;
    const int32_t w = g_dirty.right - g_dirty.left, h = g_dirty.bottom - g_dirty.top;
    Buffer_fill(&g_color, g_dirty.left, g_dirty.top, w, h, 9);
    Buffer_fill_2(&g_shadow, 0);
    if (keep_depth) return;
    for (int32_t r = 0; r < h; r++) {
        // (The dirty rectangle can reach row / column 256: the original clears past the rows.)
        const int32_t row = g_dirty.top + r;
        if (row < 0 || row > 0xFF) continue;
        for (int32_t c = g_dirty.left; c < g_dirty.left + w; c++)
            if (c >= 0 && row * 0x100 + c < 0x10000) g_depth[(size_t)(row * 0x100 + c)] = 0xFFFF;
    }
    g_dirty = UiRect{0, 0, 0, 0};
    g_sdirty = UiRect{0, 0, 0, 0};
}

int32_t THISCALL Scene_render(VoxelScene* self, Buffer* dest, int32_t x, int32_t y, uint32_t flags) {
    if (!(self->flags & 1)) return 7;
    if (!dest) return 7;
    if (self->count == 0) return 0;
    if (!g_ready) return 7;
    if (self->flags & 0x20) self->c_scale = (float)((double)self->c_scale + self->c_scale);
    self->v_count = self->count;
    self->c_clip[2] = 0xFF;
    self->c_clip[3] = 0xFF;
    self->c_clip[0] = 0;
    self->c_clip[1] = 0;
    self->c_clip[4] = 0x100;
    self->c_clip[5] = 0x100;
    self->dest = dest;
    self->v_frame = 1;
    self->v_shadows = (flags & 2) ? 0 : 1;
    UiRect r = rect_intersect(UiRect{x - 0x80, y - 0x80, x + 0x80, y + 0x80}, dest->clip);
    r.left += 0x80 - x;
    r.right += 0x80 - x;
    r.top += 0x80 - y;
    r.bottom += 0x80 - y;
    g_r.clip = Rect6{{r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top}};
    // (flags 4: centre the bounding box of the objects, 0x63A6F0; not used by the units)
    g_scene = self;
    vx_render(self, 0);
    g_scene = nullptr;
    g_dirty = rect_union(g_dirty, UiRect{g_r.rect.v[0], g_r.rect.v[1], g_r.rect.v[2] + 1, g_r.rect.v[3] + 1});
    const bool dbl = self->flags & 0x20;
    if (self->v_shadows) {
        g_sdirty = rect_union(g_sdirty, UiRect{g_r.srect.v[0], g_r.srect.v[1], g_r.srect.v[2] + 1, g_r.srect.v[3] + 1});
        // (flags 0x20: drawn at half size, not used by the units)
        if (!dbl) blit_table(&g_shadow, dest, 0, x - 0x80, y - 0x80, g_r.shadow_table);
    }
    if (!(flags & 8)) {
        if (!dbl) blit_key(&g_color, dest, 9, x - 0x80, y - 0x80);
        Scene_clear(self, flags & 1);
    }
    return 0;
}

int32_t THISCALL Scene_render_table(VoxelScene* self, Buffer* dest, int32_t x, int32_t y, int32_t key,
                                    const uint8_t* table, uint32_t flags) {
    if (!g_ready) return 7;
    Buffer_fill_2(&g_color, key);
    int32_t r = Scene_render(self, dest, x, y, flags | 8);
    if (r != 0) return r;
    // Buffer_draw_multi_table_dest_2 (0x5D94B0) at scale 1: dest = table[src * 256 + dest] for
    // the pixels that are not `key`.
    UiRect c = rect_intersect(UiRect{x - 0x80, y - 0x80, x + 0x80, y + 0x80}, dest->clip);
    for (int32_t py = c.top; py < c.bottom; py++)
        for (int32_t px = c.left; px < c.right; px++) {
            const uint8_t s = g_color.data[(py - (y - 0x80)) * 0x100 + (px - (x - 0x80))];
            uint8_t& d = dest->data[py * dest->stride + px];
            if (s != (uint8_t)key) d = table[(uint32_t)s << 8 | d];
        }
    Buffer_fill_2(&g_color, 9);
    Scene_clear(self, flags & 1);
    return 0;
}
