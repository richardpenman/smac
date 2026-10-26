// MapWin_gen_terrain_poly (0x4632D0): the terrain of one map tile (see mapwin.h).
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

#include "engine/base.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/colortables.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"
#include "ui/texture.h"

uint32_t TileVariantBase = 0;

// The unit being moved by the interface (its tile counts as seen for its faction).
GLOBAL(int32_t, MoveTileX, 0x68E564);
GLOBAL(int32_t, MoveTileY, 0x68E568);
GLOBAL(int32_t, MoveVehID, 0x68E56C);

namespace {

int32_t trunc_div16(int32_t v) { return (v + ((v >> 31) & 0xF)) >> 4; }
int32_t trunc_div4(int32_t v) { return (v + ((v >> 31) & 3)) >> 2; }
// The zoom unit: (zoom + 16) * 50 / 16 (piZoomNum * 50 / piZoomDenom with a fixed zoom).
int32_t zoom_unit(int32_t zoom) {
    if (ZoomNum < 1) return trunc_div16((zoom * 5 + 0x50) * 10);
    return ZoomNum * 0x32 / ZoomDenom;
}
int32_t wrap_x(int32_t x) {
    if ((MapToggleFlat & 1) == 0) {
        if (x < 0) x += MapAreaX;
        else if (x >= MapAreaX) x -= MapAreaX;
    }
    return x;
}
bool in_map(int32_t x, int32_t y) { return y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX; }
MAP* tile(int32_t x, int32_t y) { return &MapTiles[MapHalfX * y + (x >> 1)]; }
bool is_land(const MAP* t) { return (t->climate & 0xE0) >= 0x60; }
uint8_t* tile_bytes(MAP* t) { return reinterpret_cast<uint8_t*>(t); }
// Items as faction f knows them: the visible items at +0xC + 4 * f (faction 0 reads the
// landmarks dword there, as in the original).
uint32_t known_items(MAP* t, int32_t f) { return *reinterpret_cast<uint32_t*>(tile_bytes(t) + 0xC + f * 4); }
bool omniscient(int32_t f) { return (Factions[f].player_flags & 0x200) != 0; }
// Faction of the base / units on a tile (items bits 1 / 2 and the occupant nibble), or -1.
int32_t occupant(MAP* t, uint32_t bits) {
    if (!(t->items & bits)) return -1;
    int32_t f = t->val2 & 0xF;
    return f < 8 ? f : -1;
}
uint32_t diplo_bits(int32_t f, int32_t g) {  // Factions[f].diplo_status[g], g may be -1
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(&Factions[f]) + 0x18 + g * 4);
}
int32_t triad_of(int32_t veh_id) { return Chassis[Units[Vehs[veh_id].unit_id].chassis_id].triad; }
// vector_dist-like measure used by the GamePreferences 0x200 outline.
int32_t outline_dist(int32_t x, int32_t y) {
    int32_t dx = std::abs(x - OutlineCenterX);
    if ((MapToggleFlat & 1) == 0 && MapHalfX < dx) dx = MapAreaX - dx;
    int32_t dy = std::abs(y - OutlineCenterY);
    int32_t a = std::abs(dx), b = std::abs(dy);
    int32_t hi = a > b ? a : b, lo = a < b ? a : b;
    return hi - ((((a + b) >> 1) - lo + 1) >> 1);
}
// Base on (x, y) as the viewer knows it, or -1.
int32_t known_base(int32_t x, int32_t y, int32_t f) {
    int32_t b = base_at(x, y);
    if (b >= 0 && f >= 0 && (uint8_t)Bases[b].faction_id != (uint32_t)f && !(Bases[b].visibility & (1 << f))) b = -1;
    return b;
}

void sprite(Buffer* dest, uint32_t addr, int32_t px, int32_t py) {
    Sprite* s = ArtSprite(addr);
    pfncSpriteStretchCopyToCanvas(s, dest, s->key & 0xFF, px, py);
}
void sprite_shadow(Buffer* dest, uint32_t addr, int32_t px, int32_t py) {
    Sprite* s = ArtSprite(addr);
    Sprite_draw_dest(s, dest, s->key & 0xFF, px, py, ShadowTable);
}

// The quad of a landmark overlay: the tile corners rotated by `rot` with their lights.
void rotated_quad(int32_t rot, const int32_t light[4], int32_t pts[4][2], int32_t lights[4]) {
    static const int32_t kOrder[4] = {0, 1, -2, -1};
    for (int32_t k = 0; k < 4; k++) {
        int32_t c = (rot + kOrder[k]) & 3;
        pts[k][0] = TileCorners[c][0];
        pts[k][1] = TileCorners[c][1];
        lights[k] = light[c];
    }
}

}  // namespace

int32_t GAMEAPI MapWin_gen_terrain_poly(Buffer* dest, uint8_t* win, int32_t zoom, int32_t faction_id, int32_t a5,
                                        int32_t options, int32_t x, int32_t y, int32_t px, int32_t py) {
    if (y < 0 || MapAreaY <= y || x < 0 || MapAreaX <= x) return 0;
    const uint32_t opts = (uint32_t)options;
    const int32_t f = faction_id;
    MAP* sq = tile(x, y);
    uint32_t& landmarks = sq->landmarks;
    uint32_t lm = landmarks;  // local_34
    if (win && win == &WorldMapWin && !(opts & 0x80000000u) && !(lm & 0x400000)) return 1;
    // Random_reseed(tile address): one variant draw per tile below.
    uint32_t seed = TileVariantBase + (uint32_t)(sq - MapTiles) * 0x2C;
    auto random_get = [&seed](int32_t lo, int32_t hi) {
        seed = seed * 0x19660D + 0x3C6EF35F;
        return (int32_t)(((uint32_t)(hi - lo) * (seed & 0xFFFF)) >> 16) + lo;
    };
    const bool ocean = (sq->climate & 0xE0) < 0x60;  // local_18
    if (!ocean) {
        TextureLightMin = -4;
        TextureLightMax = 4;
    }
    int32_t lm_code = -1;  // local_94 (uninitialised in the original when unused)
    if (lm & 0x103) lm_code = (int32_t)lm >> 24;
    const int32_t q = trunc_div4(zoom_unit(zoom) + 1);  // local_98
    const int32_t q2 = q * 2;  // local_78
    int32_t lift = 0;  // local_44
    if (win)
        lift = (pfncMapGetCornerYOffset(win, x, y, 4) + pfncMapGetCornerYOffset(win, x, y, 3) +
                pfncMapGetCornerYOffset(win, x, y, 2) + pfncMapGetCornerYOffset(win, x, y, 1)) >>
               2;
    py += lift;
    int32_t terr = f;  // local_6c[8]: faction whose territory colours apply
    if (f < 0) {
        terr = sq->val3 & 7;
        if (terr < 1) terr = CurrentPlayerFaction;
    }
    int32_t rain = (sq->climate & 0x18) >> 3;  // local_84
    const int32_t alt = sq->climate >> 5;  // local_8c
    int32_t rocky = sq->val3 >> 6;  // local_38
    uint32_t items = (f < 0 || omniscient(f)) ? sq->items : known_items(sq, f);  // local_24
    int32_t small = (zoom < -6 || (opts & 0x80000000u)) ? 1 : 0;  // local_90
    const uint32_t flat_look = opts & 0x1000000;  // local_11c
    bool unexplored = false;  // local_70
    bool seen = true;  // local_1c: currently in view (else dithered)
    if (flat_look) {
        rain = 0;
        rocky = 0;
        items = 0;
        unexplored = true;
        seen = true;
        landmarks &= ~0x200000u;
    } else if ((GameRules & RULES_NO_UNITY_SURVEY) && !(GameState & STATE_OMNISCIENT_VIEW) && !omniscient(f) &&
               !(sq->visibility & (uint8_t)(1u << (f & 31)))) {
        rain = 0;
        rocky = 0;
        items &= ~0x20u;
        unexplored = true;
        seen = false;
        landmarks |= 0x200000;
    } else if ((GameState & STATE_OMNISCIENT_VIEW) || f < 1 || !(GameMorePreferences & MPREF_MAP_SHOW_FOG_WAR) ||
               zoom <= -8) {
        seen = true;
        landmarks &= ~0x200000u;
    } else {
        // Currently in view: a unit, base or sensor of the faction (or of a pact partner whose
        // units it can see) within 2 tiles; the moved unit; a capable unit of the faction in a
        // stack nearby; with Alien Crossfire a base with facility 0x29 within radius 5.
        seen = false;
        bool found = false;
        for (int32_t i = 0; i < 25 && !found; i++) {
            int32_t nx = wrap_x(TableOffsetX[i] + x), ny = TableOffsetY[i] + y;
            if (!in_map(nx, ny)) continue;
            if (i < 9) {
                int32_t occ = occupant(tile(nx, ny), 3);
                if (occ == f ||
                    (MoveTileX >= 0 && Vehs[MoveVehID].faction_id == f && MoveTileX == nx && MoveTileY == ny)) {
                    found = true;
                    break;
                }
                if (diplo_bits(f, occ) & 1) {
                    for (int32_t v = veh_at(nx, ny); v >= 0; v = Vehs[v].next_veh_id_stack) {
                        if (Vehs[v].faction_id == f) {
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                    if (seen) break;  // a capable unit was found in an earlier stack
                }
            }
            if (is_sensor(nx, ny) && tile(nx, ny)->owner == f) {
                found = true;
                break;
            }
            if (occupant(tile(nx, ny), 1) == f) {
                found = true;
                break;
            }
            if (MoveTileX == nx && MoveTileY == ny && Vehs[MoveVehID].faction_id == f &&
                has_abil(Vehs[MoveVehID].unit_id, 2)) {
                int32_t triad = triad_of(MoveVehID);
                if (triad == 2 || ocean == (triad == 1)) {
                    found = true;
                    break;
                }
            }
            int32_t occ = occupant(tile(nx, ny), 2);
            if (occ == f || (diplo_bits(occ, f) & 1)) {
                int32_t v = veh_at(nx, ny);
                if (v >= 0) {
                    while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
                    for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
                        if (Vehs[v].faction_id == f && has_abil(Vehs[v].unit_id, 2)) {
                            int32_t triad = triad_of(v);
                            if (triad == 2 || ocean == (triad == 1)) {
                                seen = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (found) seen = true;
        if (!seen && ExpansionEnabled) {
            for (int32_t i = 0; i < 81; i++) {
                int32_t ox = TableOffsetX[i], oy = TableOffsetY[i];
                int32_t a = (oy - ox) >> 1, b = (ox + oy) >> 1;
                if (a * a + b * b >= 0x19) continue;
                int32_t nx = wrap_x(ox + x), ny = oy + y;
                if (!in_map(nx, ny) || occupant(tile(nx, ny), 1) != f) continue;
                int32_t base_id = base_at(nx, ny);
                int32_t off, mask;
                bitmask(0x29, &off, &mask);
                if (reinterpret_cast<const uint8_t*>(&Bases[base_id])[0x8C + off] & (uint8_t)mask) {
                    seen = true;
                    break;
                }
            }
        }
        if (seen) landmarks &= ~0x200000u;
        else landmarks |= 0x200000;
    }

    // Neighbour masks.
    uint32_t coast[4] = {0, 0, 0, 0};  // local_108[0..3]: water next to each quadrant
    uint32_t rain_mask = 0;  // local_28
    uint32_t fungus_mask = 0;  // local_40
    uint32_t jungle_mask = 0;  // local_2c
    bool jungle = false;  // local_30
    const bool no_terrain = (opts & 0x80000000u) != 0;  // local_20
    if (!no_terrain) {
        if (!ocean) {
            for (int32_t k = 0; k < 8; k++) {
                int32_t nx = wrap_x(NearOffsetX[k] + x), ny = NearOffsetY[k] + y;
                if (!in_map(nx, ny) || is_land(tile(nx, ny))) continue;
                int32_t h = k >> 1;
                if ((k & 1) == 0) {
                    coast[(h + 1) & 3] |= 4;
                    coast[(h - 2) & 3] |= 1;
                } else {
                    coast[(h - 2) & 3] |= 2;
                }
            }
            jungle = (lm & 0x80000004) == 4;
            if (rain || (items & 0x20) || jungle) {
                for (int32_t k = 0; k < 8; k++) {
                    int32_t d = (k - 1) & 7;
                    int32_t nx = wrap_x(NearOffsetX[d] + x), ny = NearOffsetY[d] + y;
                    if (!in_map(nx, ny)) continue;
                    MAP* n = tile(nx, ny);
                    if ((n->items & 0x20) && (n->climate & 0xE0) > 0x3F) fungus_mask |= 1u << k;
                    if (jungle && (n->landmarks & 0x80000004) == 4 && (n->climate & 0xE0) > 0x5F)
                        jungle_mask |= 1u << k;
                    if (rain && ((n->climate & 0xE0) < 0x60 || rain <= ((n->climate >> 3) & 3))) rain_mask |= 1u << k;
                }
            }
        } else if (items & 0x20) {
            for (int32_t k = 0; k < 8; k++) {
                int32_t d = (k - 1) & 7;
                int32_t nx = wrap_x(NearOffsetX[d] + x), ny = NearOffsetY[d] + y;
                if (!in_map(nx, ny)) continue;
                MAP* n = tile(nx, ny);
                if ((n->items & 0x20) && (n->climate & 0xE0) > 0x3F) fungus_mask |= 1u << k;
            }
        }
    }
    py += q2;
    px += q2 * 2;
    auto restore_and_return = [&](int32_t result) {
        if (0 < ZoomNum && win) mw(win, 8) = 99999;
        BlitDestScale = 1;
        BlitSourceScale = 1;
        TextureLightMin = -0x100;
        TextureLightMax = 0x100;
        return result;
    };
    int32_t pts[12][2], uv[12][2], uv_rocky[12][2];
    // Unexplored: only the outline.
    if (a5 > 0x15 ||
        (!(GameRules & RULES_NO_UNITY_SURVEY) && f >= 0 && !omniscient(f) && !(sq->visibility & (1 << f)))) {
        if (!no_terrain) {
            for (int32_t i = 0; i < 12; i++) {
                pts[i][0] = TilePolyX[i] * q2 + px;
                pts[i][1] = TilePolyY[i] * q + py;
                if (win) {
                    int32_t c = 0;
                    if (y < MapAreaY && x < MapAreaX) {
                        pfncMapGetCornerYOffset(win, x, y, TilePolyCorner[i]);
                        uint8_t* e = ui_mapwin_cache_entry(win, x, y);
                        c = e ? (int8_t)e[4 + TilePolyCorner[i]] : 0;
                    }
                    pts[i][1] -= c;
                }
            }
            for (int32_t i = 0; i < 4; i++) {
                TileCorners[i][0] = pts[i * 3 + 1][0];
                TileCorners[i][1] = pts[i * 3 + 1][1];
            }
            for (int32_t i = 1; i <= 4; i++)
                Buffer_line_2(dest, TileCorners[i - 1][0], TileCorners[i - 1][1], TileCorners[i & 3][0],
                              TileCorners[i & 3][1], 0xC2);
            if (MapWinCursorX[MapWinCursorIndex] == x && MapWinCursorY[MapWinCursorIndex] == y)
                std::memcpy(&CursorTileCorners[0][0], &TileCorners[0][0], sizeof(int32_t) * 8);
        }
        return restore_and_return(0);
    }
    uint32_t variant = 0;  // local_bc
    const int32_t rnd_variant = random_get(0, 2);  // local_c4
    const uint32_t keep_points = opts & 0x10000000;  // local_48
    int32_t coast_pts[16][2], coast_uv[16][2];  // local_2ec, auStack_26c
    if (!keep_points) {
        for (int32_t i = 0; i < 4; i++) {
            for (int32_t j = 0; j < 3; j++) {
                int32_t idx = i * 3 + j;
                pts[idx][0] = TilePolyX[idx] * q2 + px;
                pts[idx][1] = TilePolyY[idx] * q + py;
                int32_t r = idx;
                if (!ocean && !(items & 0x8000) && rain && !no_terrain)
                    r = j + ((RainTransition[rain_mask][1] + i) & 3) * 3;
                uv[idx][0] = TilePolyUV[r][0];
                uv[idx][1] = TilePolyUV[r][1];
                if (rocky) {
                    uv_rocky[idx][0] = TilePolyUV[idx][0];
                    uv_rocky[idx][1] = TilePolyUV[idx][1];
                }
                if (alt > 2 && rain) variant = RainTransition[rain_mask][0];
            }
        }
        for (int32_t p = 0; p < 4; p++) {
            for (int32_t k = 0; k < 4; k++) {
                int32_t idx = ((p - 2 + k) & 3) + p * 4;
                coast_pts[p * 4 + k][0] = CornerTileX[idx] * q2 + px;
                coast_pts[p * 4 + k][1] = CornerTileY[idx] * q + py;
                coast_uv[p * 4 + k][0] = CoastQuadUV[p * 4 + k][0];
                coast_uv[p * 4 + k][1] = CoastQuadUV[p * 4 + k][1];
            }
        }
    }
    bool territory_look = false;  // local_80
    const uint32_t territory = opts & 0x10000;  // local_c0
    if (territory &&
        (omniscient(CurrentPlayerFaction) || (sq->visibility & (1 << CurrentPlayerFaction)) ||
         (GameState & STATE_OMNISCIENT_VIEW)) &&
        no_terrain) {
        territory_look = true;
        terr = whose_territory(CurrentPlayerFaction, x, y, nullptr, 0);
    }
    int32_t corner_light[4] = {0, 0, 0, 0};  // local_b8
    const int32_t color = seen ? -1 : 0x41;
    for (int32_t i = 0; i < 4; i++) {
        int32_t light[3];  // local_118
        for (int32_t j = 0; j < 3; j++) {
            if (alt < 3) {
                light[j] = i;
            } else if (win) {
                int32_t idx = i * 3 + j;
                pts[idx][1] -= pfncMapGetCornerYOffset(win, x, y, TilePolyCorner[idx]);
                light[j] = seen ? MapWin_get_point_light(win, x, y, TilePolyCorner[idx]) : TerrainDefaultLight;
            } else {
                light[j] = 0;
            }
        }
        TileCorners[i][0] = pts[i * 3 + 1][0];
        TileCorners[i][1] = pts[i * 3 + 1][1];
        corner_light[i] = light[1];
        if (ocean || (territory_look && terr > 0) || no_terrain) continue;
        const int32_t* p = &pts[i * 3][0];
        const int32_t* t = &uv[i * 3][0];
        if ((items & 0x8000) && (opts & 1)) {
            pfncImageCopytoCanvas2(ArtTexture(kTexFarm, rnd_variant * 3 + rain), dest, p, light, 3, t, color);
        } else if (lm & 0x40) {
            pfncImageCopytoCanvas2(ArtTexture(kTexDune), dest, p, light, 3, &TilePolyUV[i * 3][0], color);
        } else if (rain == 0) {
            pfncImageCopytoCanvas2(ArtTexture(kTexArid), dest, p, light, 3, t, color);
        } else if (rain == 1) {
            pfncImageCopytoCanvas2(ArtTexture(kTexMoist, (int32_t)variant), dest, p, light, 3, t, color);
        } else if (rain == 2) {
            pfncImageCopytoCanvas2(ArtTexture(kTexRainy, (int32_t)variant), dest, p, light, 3, t, color);
        }
        if (rocky)
            Texture_draw_trans_5(ArtTexture(kTexRocky, rnd_variant + rocky * 2 - 2), dest, p, light, 3,
                                 &uv_rocky[i * 3][0], color);
    }
    // Flat looks (option 0x1000000 / territory / no terrain): a polygon in one colour.
    int32_t poly_color = -1;
    if (flat_look) {
        if (ocean) poly_color = 0xA1;
        else poly_color = terr == -1 ? 0x11 : FactionColorFill[terr];
    } else if (!ocean && territory_look && terr > 0) {
        poly_color = FactionColorFill[terr];
    } else if (!ocean && no_terrain) {
        poly_color = (opts & 0x10) ? ((tile(x, y)->climate & 0x18) >> 3) * 2 + 0x95 : (0xE - alt) * 2;
    }
    if (poly_color == -1) {
        // Coast quads: per quadrant the centre, the midpoint of the two outer vertices, ...
        for (int32_t k = 1; k <= 4; k++) {
            int32_t m = k & 3, a = (k - 1) * 3;
            int32_t* c = &coast_pts[m * 4][0];
            c[0] = pts[a][0];
            c[1] = pts[a][1];
            c[2] = (pts[a + 1][0] + pts[a + 2][0]) >> 1;
            c[3] = (pts[a + 1][1] + pts[a + 2][1]) >> 1;
            c[4] = pts[a + 2][0];
            c[5] = pts[a + 2][1];
            c[6] = (pts[m * 3 + 2][0] + pts[m * 3 + 1][0]) >> 1;
            c[7] = (pts[m * 3 + 2][1] + pts[m * 3 + 1][1]) >> 1;
        }
        // Water: deep where the depth light reaches 16, shallow blended over it below.
        if (!(opts & 0x2000000)) {
            int32_t quad_uv[4][4][2];  // local_36c
            for (int32_t p = 0; p < 4; p++) {
                if (!ocean && coast[p] == 0) continue;
                for (int32_t k = 0; k < 4; k++) {
                    WaterQuadLight[p][k] = 0;
                    int32_t idx = ((p - 2 + k) & 3) + p * 4;
                    WaterQuadPoints[p][k][0] = CornerTileX[idx] * q2 + px;
                    WaterQuadPoints[p][k][1] = CornerTileY[idx] * q + py;
                    WaterQuadLight[p][k] = OceanDepthLight[alt_get_ocean_detail(x, y, p, k)];
                    quad_uv[p][k][0] = OceanQuadUV[p * 4 + k][0];
                    quad_uv[p][k][1] = OceanQuadUV[p * 4 + k][1];
                }
                if (ocean && !no_terrain) {
                    int32_t* l = WaterQuadLight[p];
                    if (l[0] > 0xF || l[1] > 0xF || l[2] > 0xF || l[3] > 0xF) {
                        int32_t deep[4] = {l[0] - 0x10, l[1] - 0x10, l[2] - 0x10, l[3] - 0x10};
                        if (seen)
                            Texture_draw_2(ArtTexture(kTexWater, 1), dest, &WaterQuadPoints[p][0][0], deep, 4,
                                           &quad_uv[p][0][0]);
                        else
                            pfncImageCopytoCanvas2(ArtTexture(kTexWater, 1), dest, &WaterQuadPoints[p][0][0], deep, 4,
                                                   &quad_uv[p][0][0], 0x41);
                    }
                    Texture_draw_4(ArtTexture(kTexWater, 0), dest, &WaterQuadPoints[p][0][0], l, 4, &quad_uv[p][0][0],
                                   0x10, color);
                }
            }
        }
        if (ocean && no_terrain) poly_color = 0xBE;
    }
    if (poly_color != -1) {
        Buffer_poly(dest, &TileCorners[0][0], 4, poly_color);
        TextureLightMin = -0x100;
        TextureLightMax = 0x100;
        return 1;
    }

    // Landmark overlays: volcano (2), mesa (0x100), crater (1).
    int32_t quad[4][2], quad_light[4];
    int32_t quad_uv[4][2];
    auto set_uv = [&quad_uv](std::initializer_list<int32_t> v) {
        int32_t i = 0;
        for (int32_t c : v) quad_uv[i / 2][i % 2] = c, i++;
    };
    if (!unexplored && (lm & 2) && lm_code >= 0 && VolcanoOverlay[lm_code][0] != -1) {
        rotated_quad(VolcanoOverlay[lm_code][1], corner_light, quad, quad_light);
        set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
        Texture* t = ArtTexture(kTexVolcano, VolcanoOverlay[lm_code][0]);
        if (lm_code != 0) Texture_draw_trans_4(t, dest, &quad[0][0], quad_light, 4, &quad_uv[0][0]);
        else Texture_draw_upper_threshold_trans(t, dest, &quad[0][0], 4, &quad_uv[0][0]);
    }
    if ((lm & 0x100) && !unexplored && lm_code >= 0 && MesaOverlay[lm_code][0] != -1) {
        rotated_quad(MesaOverlay[lm_code][1], corner_light, quad, quad_light);
        set_uv({0, 0x37, 0, 0, 0x37, 0, 0x37, 0x37});
        Texture_draw_trans_4(ArtTexture(kTexMesa, MesaOverlay[lm_code][0]), dest, &quad[0][0], quad_light, 4,
                             &quad_uv[0][0]);
    }
    if ((lm & 1) && !unexplored && lm_code >= 0 && CraterOverlay[lm_code][0] != -1) {
        rotated_quad(CraterOverlay[lm_code][1], corner_light, quad, quad_light);
        set_uv({0, 0x37, 0, 0, 0x37, 0, 0x37, 0x37});
        Texture_draw_trans_4(ArtTexture(kTexCrater, CraterOverlay[lm_code][0]), dest, &quad[0][0], quad_light, 4,
                             &quad_uv[0][0]);
    }
    // Grid (preference 0x100): the two upper edges.
    if ((((GamePreferences & PREF_MAP_SHOW_GRID) && !no_terrain) || (opts & 0x4000000)) &&
        (is_land(sq) || no_terrain || (GameMorePreferences & MPREF_MAP_SHOW_GRID_OCEAN_SQ))) {
        int32_t nx = wrap_x(x - 1);
        for (int32_t i = 1; i <= 2; i++) {
            int32_t ny = y - 1;
            if (in_map(nx, ny)) {
                uint8_t c = tile(nx, ny)->climate & 0xE0;
                int32_t line = -1;
                if (c < 0x60) {
                    if (no_terrain || (GameMorePreferences & MPREF_MAP_SHOW_GRID_OCEAN_SQ)) line = 0xBE;
                } else {
                    line = is_land(sq) ? 0x25 : 0xBE;
                }
                if (line >= 0)
                    Buffer_line_2(dest, TileCorners[i - 1][0], TileCorners[i - 1][1], TileCorners[i & 3][0],
                                  TileCorners[i & 3][1], line);
            }
            nx = wrap_x(nx + 2);
        }
    }
    if (MapWinCursorX[MapWinCursorIndex] == x && MapWinCursorY[MapWinCursorIndex] == y)
        std::memcpy(&CursorTileCorners[0][0], &TileCorners[0][0], sizeof(int32_t) * 8);
    px -= q2 * 2;
    py -= lift + q2;
    if (ZoomNum < 1) {
        BlitSourceScale = 0x10;
        BlitDestScale = zoom + 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    if ((opts & 0x200) && !unexplored) {
        if ((lm & 0x80000008) == 8) sprite(dest, kSprSpecial, px, py);
        else if ((lm & 0x80000400) == 0x400) sprite(dest, kSprSpecial + 0x2C, px, py);
    }
    // Monsoon Jungle (no light).
    if (jungle && !ocean && !unexplored) {
        TextureKey = kArtKey;
        int32_t rot = RainTransition[jungle_mask][1];
        for (int32_t k = 0; k < 4; k++) {
            quad_uv[k][0] = TilePolyUV[((rot + k) & 3) * 3 + 1][0];
            quad_uv[k][1] = TilePolyUV[((rot + k) & 3) * 3 + 1][1];
        }
        Texture_draw_upper_threshold_trans(ArtTexture(kTexJungle, RainTransition[jungle_mask][0]), dest,
                                           &TileCorners[0][0], 4, &quad_uv[0][0]);
    }
    if (!no_terrain) {
        // Forest, joined with forests on the diagonal neighbours.
        if ((items & 0x200000) && alt > 2 && (opts & 1) && !unexplored) {
            int32_t mask = 0;
            for (int32_t k = 0; k < 4; k++) {
                int32_t nx = wrap_x(DiagOffsetX[k] + x), ny = DiagOffsetY[k] + y;
                if (in_map(nx, ny) && (tile(nx, ny)->items & 0x200000)) mask |= 1 << k;
            }
            set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
            TextureKey = kArtKey;
            Texture_draw_trans_4(ArtTexture(kTexForest, mask), dest, &TileCorners[0][0], corner_light, 4,
                                 &quad_uv[0][0]);
        }
        // Xenofungus (sea fungus on water).
        if ((items & 0x20) && alt > 1 && !unexplored && zoom > -10) {
            TextureKey = kArtKey;
            int32_t rot = RainTransition[fungus_mask][1];
            for (int32_t k = 0; k < 4; k++) {
                quad_uv[k][0] = TilePolyUV[((rot + k) & 3) * 3 + 1][0];
                quad_uv[k][1] = TilePolyUV[((rot + k) & 3) * 3 + 1][1];
            }
            Texture_draw_upper_threshold_trans_2(
                ArtTexture(kTexFungus, RainTransition[fungus_mask][0] + (ocean ? 15 : 0)), dest, &TileCorners[0][0], 4,
                &quad_uv[0][0], color);
        }
    }
    // Territory borders: edges to tiles of the same region owned by someone else.
    if (territory &&
        (omniscient(CurrentPlayerFaction) || (sq->visibility & (1 << CurrentPlayerFaction)) ||
         (GameState & STATE_OMNISCIENT_VIEW)) &&
        !no_terrain) {
        terr = whose_territory(CurrentPlayerFaction, x, y, nullptr, 0);
        if (terr >= 0) {
            uint8_t mask[10];
            std::memset(mask, 0, sizeof(mask));
            bool border = false;
            uint8_t region = sq->region;
            for (int32_t k = 0; k < 8; k++) {
                int32_t nx = wrap_x(NearOffsetX[k] + x), ny = NearOffsetY[k] + y;
                if (!in_map(nx, ny)) continue;
                MAP* n = tile(nx, ny);
                if (((n->climate & 0xE0) < 0x60) != ((sq->climate & 0xE0) < 0x60) || n->region != region) continue;
                if (whose_territory(CurrentPlayerFaction, nx, ny, nullptr, 0) == terr) continue;
                border = true;
                uint8_t c = (uint8_t)(terr + 1);
                if ((k & 1) == 0) {
                    if (k == 0) {
                        mask[8] = c;
                        mask[2] = c;
                        mask[1] = c;
                        continue;
                    }
                    mask[k] = c;
                    mask[k + 2] = c;
                }
                mask[k + 1] = c;
            }
            if (border) {
                set_uv({0, 0, 0x3B, 0, 0x3B, 0x3B, 0, 0x3B});
                Texture_draw_mask_dest(ArtTexture(kTexBorder), dest, &TileCorners[0][0], mask, &FactionColorTable[0][0],
                                       4, &quad_uv[0][0]);
            }
        }
    }
    // Coasts: the shallow water texture through the coast shape of each quadrant's coast bits.
    if (zoom > -8 && !ocean) {
        for (int32_t p = 0; p < 4; p++) {
            uint32_t bits = coast[p];
            if (!bits) continue;
            uint8_t mask[0x35];
            for (int32_t i = 0; i < 0x35; i++) {
                uint8_t v = CoastMask[i][bits];
                mask[i] = v > 1 ? CoastColors[v + 2] : v;  // DAT_00787E56[v]
            }
            if (bits == 7 && ((y - x) & 2)) {
                mask[5] = 0;
                mask[0xE] = mask[0xF] = mask[0x10] = mask[0x11] = 0;
                for (int32_t base : {0x12, 0x16, 0x1A})
                    for (int32_t i = 0; i < 4; i++) mask[base + i] = CoastColors[7 - i];  // 0x787E5B down to 0x787E58
            }
            int32_t uvq[4][2];
            for (int32_t k = 0; k < 4; k++) {
                uvq[k][0] = coast_uv[p * 4 + k][0];
                uvq[k][1] = coast_uv[p * 4 + k][1];
            }
            Texture_draw_coast_2(ArtTexture(kTexWater, 0), dest, &coast_pts[p * 4][0], WaterQuadLight[p],
                                 ArtTexture(kTexCoastShape), mask, 4, &uvq[0][0], color);
        }
    }
    // Rivers.
    if ((opts & 0x400) && (sq->items & 0x80) && !ocean) {
        int32_t mask = 0;
        for (int32_t k = 0; k < 4; k++) {
            int32_t nx = wrap_x(DiagOffsetX[k] + x), ny = DiagOffsetY[k] + y;
            if (in_map(nx, ny) && (tile(nx, ny)->items & 0x80)) mask |= 1 << k;
        }
        set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
        TextureKey = kArtKey;
        Texture_draw_trans_5(ArtTexture(kTexRiver, mask), dest, &TileCorners[0][0], corner_light, 4, &quad_uv[0][0],
                             color);
        if (sq->items & 0x200)
            Texture_draw_trans_5(ArtTexture(kTexRiver), dest, &TileCorners[0][0], corner_light, 4, &quad_uv[0][0],
                                 color);
    }

    // ---- sprites ----
    if (ZoomNum < 1) {
        BlitSourceScale = 0x10;
        BlitDestScale = zoom + 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    if ((no_terrain && !keep_points) || (f >= 0 && !omniscient(f) && !(sq->visibility & (1 << f))))
        return restore_and_return(0);
    // Outline (GamePreferences 0x200) around OutlineCenter for unclaimed tiles.
    if ((GamePreferences & PREF_MAP_SHOW_BASE_GRID) && !no_terrain) {
        auto inside = [&](int32_t tx, int32_t ty) {
            int32_t owner = tile(tx, ty)->val3 & 7;
            if (f >= 0 && owner != f && !(diplo_bits(f, owner) & 8)) owner = 0;
            return (outline_dist(tx, ty) <= 2) + owner > 0 ? 1 : 0;
        };
        int32_t self_in = inside(x, y);
        for (int32_t c = 1; c <= 4; c++) {
            int32_t nx = wrap_x(DiagOffsetX[c - 1] + x), ny = DiagOffsetY[c - 1] + y;
            if (!in_map(nx, ny)) continue;
            if (inside(nx, ny) != self_in)
                Buffer_line_2(dest, TileCorners[c & 3][0], TileCorners[c & 3][1], TileCorners[(c + 1) & 3][0],
                              TileCorners[(c + 1) & 3][1], 0xF9);
        }
    }
    if ((opts & 1) && (items & 0x8000) && ocean) sprite(dest, kSprKelp, px, py);
    int32_t food = 0;
    const uint32_t improvements = opts & 0x20;  // local_30
    if (improvements) {
        int32_t base_id = known_base(x, y, f);  // local_88
        if (!ocean && ((items & 4) || base_id >= 0)) {
            // Roads and mag tubes towards the neighbours that have them (or a base).
            bool mag = (items & 8) || base_id >= 0;
            bool any = false, mag_drawn = false;
            set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
            for (int32_t k = 0; k < 8; k++) {
                int32_t nx = wrap_x(NearOffsetX[k] + x), ny = NearOffsetY[k] + y;
                if (!in_map(nx, ny)) continue;
                MAP* n = tile(nx, ny);
                if (!is_land(n)) continue;
                int32_t dir = 1 + ((k + 2) & 7);
                uint32_t n_items = f >= 0 ? known_items(n, f) : n->items;
                int32_t n_base = known_base(nx, ny, f);
                if (!(n_items & 4) && n_base < 0) continue;
                any = true;
                set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
                if (mag && ((n_items & 8) || n_base >= 0)) {
                    Texture_draw_trans_4(ArtTexture(kTexMagTube, dir), dest, &TileCorners[0][0], corner_light, 4,
                                         &quad_uv[0][0]);
                    mag_drawn = true;
                } else {
                    Texture_draw_trans_4(ArtTexture(kTexRoad, dir), dest, &TileCorners[0][0], corner_light, 4,
                                         &quad_uv[0][0]);
                }
            }
            if (base_id < 0 && ((mag && !mag_drawn) || !any)) {
                set_uv({0, 0, 0x37, 0, 0x37, 0x37, 0, 0x37});
                Texture_draw_trans_4(ArtTexture((mag && !mag_drawn) ? kTexMagTube : kTexRoad), dest, &TileCorners[0][0],
                                     corner_light, 4, &quad_uv[0][0]);
            }
        }
    }
    if ((improvements && (items & 0x8000) && zoom > -8) || (opts & 0x1000)) food = crop_yield(terr, -1, x, y, 0);
    if ((opts & 0x200)) {
        int32_t bonus = bonus_at(x, y);
        if (bonus && zoom > -8) {
            if (bonus == 4) {
                sprite(dest, kSprSpecial + (ocean ? 0x2C : 0), px, py);
            } else {
                int32_t idx = (((x >> 1) + y) & 1) + ((ocean ? 1 : 0) + bonus * 2) * 2 - 4;
                sprite_shadow(dest, kSprBonus + idx * 0x2C, px, py);
            }
        }
    }
    if (improvements) {
        if ((items & 0x800) && zoom > -8 && !ocean) sprite(dest, kSprBunker, px, py);
        if ((items & 0x40000) && zoom > -8 && !ocean) sprite(dest, kSprAirbase, px, py);
        if ((items & 0x80000000u) && zoom > -8) sprite_shadow(dest, kSprSensor, px, py);
        bool farm = (items & 0x8000) != 0;
        if (farm && zoom > -8 && !ocean) {
            int32_t k = food - 1;
            if (k < 0) k = 0;
            else if (k > 3) k = 3;
            sprite(dest, ((items & 0x80000) ? kSprEnricher : kSprFarm) + k * 0x2C, px, py);
        }
        if ((items & 0x10) && zoom > -8) {
            if (!ocean) sprite(dest, kSprMine, px, py);
            else if (ExpansionEnabled || alt == 2) sprite(dest, kSprMineSea, px, py);
        }
        if ((items & 0x40) && zoom > -8) {
            if (!ocean) sprite(dest, kSprSolar, px, py);
            else if (ExpansionEnabled || alt == 2) sprite(dest, kSprSolarSea, px, py);
        }
        if ((items & 0x400000) && zoom > -8) sprite(dest, kSprCondenser, px, py);
        if ((items & 0x800000) && zoom > -8) sprite(dest, kSprMirror, px, py);
        if ((items & 0x1000000) && zoom > -8)
            sprite(dest, (sq->landmarks & 0x80001000) == 0x1000 ? kSprBoreholeGeo : kSprBorehole, px, py);
        if (farm && zoom > -8 && !ocean && food > 4 && (items & 0x80000)) sprite(dest, kSprEnricher, px, py);
        uint32_t l = sq->landmarks;
        if ((l & 0x80002000) == 0x2000) {
            int32_t code = (int32_t)l >> 24;
            uint32_t hi = l & 0xFF000000;
            bool draw = true;
            if ((int32_t)hi >= 0x2000000) {
                if ((int32_t)hi <= 0x4000000) draw = false;
                else code -= 3;
            }
            if (draw) sprite(dest, kSprLandmark2000 + code * 0x2C, px, py);
        }
        l = sq->landmarks;
        if ((l & 0x80004000) == 0x4000 && ExpansionEnabled) {
            uint32_t hi = l & 0xFF000000;
            uint32_t addr = 0;
            if (hi == 0) addr = goody_at(x, y) ? kSprLandmark4000 : 0x799658;
            else if ((int32_t)hi < 0x8000000) addr = kSprLandmark4000 + ((int32_t)l >> 24) * 0x2C;
            else if (hi == 0x8000000) addr = goody_at(x, y) ? 0x7768B8 : 0x799684;
            else if (hi == 0xA000000) addr = goody_at(x, y) ? 0x7768E4 : 0x7996B0;
            else if (hi == 0xC000000) addr = 0x776910;
            else if (hi == 0xF000000) addr = 0x77693C;
            else if (hi == 0x10000000) addr = 0x776968;
            else if (hi == 0x13000000) addr = goody_at(x, y) ? 0x776994 : 0x7996DC;
            else if (hi == 0x14000000) addr = 0x7769C0;
            if (addr) sprite(dest, addr, px, py);
        }
        l = sq->landmarks;
        if ((l & 0x80008000) == 0x8000 && ExpansionEnabled && (int32_t)(l & 0xFF000000) < 0x6000000)
            sprite(dest, kSprLandmark8000 + ((int32_t)l >> 24) * 0x2C, px, py);
    }
    if ((opts & 0x200) && zoom > -8) {
        if (goody_at(x, y)) {
            uint32_t l = sq->landmarks, hi = l & 0xFF000000;
            if ((l & 0x80004000) != 0x4000 || (hi != 0 && hi != 0x8000000 && hi != 0xA000000 && hi != 0x13000000))
                sprite(dest, kSprPod + ((ocean ? 1 : 0) + ((x * 7 + y * 5) % 3) * 2) * 0x2C, px, py);
        }
        if (sq->items & 0x2000) sprite_shadow(dest, kSprMonolith, px, py);
    }
    if (!ocean) {
        if (opts & 8) sprite(dest, kSprElevation + (small + (sq->climate & 7) * 2) * 0x2C, px, py);
        if (!no_terrain && (opts & 0x10)) {
            if (small && (opts & 8)) small++;
            sprite(dest, kSprRainfall + (small + ((sq->climate & 0x18) >> 3) * 3) * 0x2C, px, py);
        }
    }
    // Debug overlays (options 4, 0x4000, 0x80000: site values, path data, territory locks) are
    // not reconstructed.
    return restore_and_return(1);
}
