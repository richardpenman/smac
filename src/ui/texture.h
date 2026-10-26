// Texture blitters of the original (gfx2 0x61A760..0x624E9F): textured polygons with optional
// gouraud light, transparency, thresholds, dithering and masks. They are what the map's terrain
// is drawn with (MapWin_gen_terrain_poly).
//
// All of them share one scan converter, reproduced bit-exactly (texture.cpp): the polygon is
// walked from its first top vertex down two edges (the "left" one in -1 vertex order, the
// "right" one in +1 order) with integer Bresenham x stepping and 16.16 u/v/light interpolation
// along the edges (fixed_div); per row the u/v/light steps across the span are computed with
// the x87 in single precision and chop rounding (1 / width, then products, fistp); the span
// loop walks the texture with the original's carry trick. The bottom vertex row is not drawn.
// Spans are clipped to the destination's clip rectangle.
//
// pts: n screen points (x, y); uv: n texture coordinates; light: n values added to the texel
// index (integer part of the interpolated 16.16 value). Where a `color` argument is not -1,
// rows with even y are drawn in that colour (the original's dithered "not currently in view"
// look) and odd rows normally; color -1 forwards to the undithered variant.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Light clamp range of the gouraud blitters that clamp (DAT_006972CC / DAT_006972D0): the light
// values are clamped in place, in the caller's array.
GLOBAL(int32_t, TextureLightMin, 0x6972CC);
GLOBAL(int32_t, TextureLightMax, 0x6972D0);
// Transparent texel index (DAT_009BB548), copied by the transparent blitters at entry.
GLOBAL(int32_t, TextureKey, 0x9BB548);

// Texture_draw_trans_3 (0x61A240): the texture's own reference points (4 corners, or 8 points
// with `eight`) mapped onto pts, texels equal to TextureKey skipped, no light (the map cursor).
ADDR_NATIVE(0x61A240)
void THISCALL Texture_draw_trans_3(Texture* self, Buffer* dest, const int32_t* pts, int32_t eight);
// Gouraud textured (lights clamped).
ADDR_NATIVE(0x61B1F0)
void THISCALL
    Texture_draw_2(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n, const int32_t* uv);
// Texture_draw_2, or dithered with `color`; the dithered variant's textured rows lag one texel
// behind (pixel k shows texel k - 1, as in the original's loop).
ADDR_NATIVE(0x6233C0)
void THISCALL pfncImageCopytoCanvas2(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                     const int32_t* uv, int32_t color);
// Gouraud textured, texels equal to TextureKey skipped (lights clamped).
ADDR_NATIVE(0x61C090)
void THISCALL
    Texture_draw_trans_4(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n, const int32_t* uv);
// Texture_draw_trans_4, or dithered: even rows `color` where the texel is not transparent.
ADDR_NATIVE(0x622DB0)
void THISCALL Texture_draw_trans_5(Texture* self, Buffer* dest, const int32_t* pts, int32_t* light, int32_t n,
                                   const int32_t* uv, int32_t color);
// Gouraud textured where the light's integer part (signed byte) is below `threshold`; nothing
// when no vertex light is below it (lights not clamped).
ADDR_NATIVE(0x61AC60)
void THISCALL Texture_draw(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                           const int32_t* uv, int32_t threshold);
// Texture_draw, or dithered: even rows all `color`.
ADDR_NATIVE(0x624670)
void THISCALL Texture_draw_4(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, int32_t n,
                             const int32_t* uv, int32_t threshold, int32_t color);
// Textured without light, texels equal to TextureKey skipped.
ADDR_NATIVE(0x61A760)
void THISCALL
    Texture_draw_upper_threshold_trans(Texture* self, Buffer* dest, const int32_t* pts, int32_t n, const int32_t* uv);
// Texture_draw_upper_threshold_trans, or dithered: even rows `color` where not transparent.
ADDR_NATIVE(0x6239E0)
void THISCALL Texture_draw_upper_threshold_trans_2(Texture* self, Buffer* dest, const int32_t* pts, int32_t n,
                                                   const int32_t* uv, int32_t color);
// Coast: `shape` (a texture with the same layout) is sampled at the same texel; mask[shape
// texel] 0: nothing, 1: this texture's texel + light, other: that colour.
ADDR_NATIVE(0x61EAD0)
void THISCALL Texture_draw_coast(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light,
                                 const Texture* shape, const uint8_t* mask, int32_t n, const int32_t* uv);
// Texture_draw_coast, or dithered: even rows `color` where mask[shape texel] != 0.
ADDR_NATIVE(0x623F80)
void THISCALL
    Texture_draw_coast_2(Texture* self, Buffer* dest, const int32_t* pts, const int32_t* light, const Texture* shape,
                         const uint8_t* mask, int32_t n, const int32_t* uv, int32_t color);
// Colour translation through a mask: m = mask[texel]; where m != 0 the destination pixel d
// becomes table[(m - 1) * 256 + d] (territory borders: the faction colour tables).
ADDR_NATIVE(0x61E540)
void THISCALL Texture_draw_mask_dest(Texture* self, Buffer* dest, const int32_t* pts, const uint8_t* mask,
                                     const uint8_t* table, int32_t n, const int32_t* uv);
