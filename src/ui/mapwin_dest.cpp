// Destination lines of the map (MapWin_dest_line 0x467430, MapWin_direct_dest_line 0x4672C0,
// MapWin_draw_base_dest 0x467970) and the dashed line they draw with (Buffer_line 0x5E09D0).
//
// MapWin_dest_line follows the path a unit would take (Path_find, compass_move for the last steps)
// and sums its cost (hex_cost) into DestLineCost (0x7D3B70, the "Turns" of the go-to drag); it
// draws a straight line when the destination is in another region (land unit: another continent;
// sea unit: no port-to-port connection; air units always). The drag preview (param 11 = 1) draws
// solid lines on DesktopWin's canvas (the screen overlay, win.cpp); the map's destination lines
// (map display options 0x40000 rally points, 0x400000 patrols, 0x800000 go-to orders) are dashed
// on the map view's canvas.
#include <algorithm>
#include <cstdlib>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/veh.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/path.h"
#include "engine/proto.h"
#include "engine/ui_bridge.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/mapwin.h"
#include "ui/win.h"

GLOBAL(int32_t, DashPhase, 0x939FD4);  // dash pattern counter of the map's destination lines

namespace {

// One pixel of a dashed line: `on` pixels of colour c1 (-1: skipped), `off` pixels of c2; the
// phase counter as in Buffer_line (negative: in the c1 run).
struct Dash {
    int32_t c1, c2, on, off, phase;
    void put(Buffer* b, int32_t x, int32_t y) {
        uint8_t* p = b->data + (size_t)y * b->stride + x;
        if (phase < 0) {
            if (c1 != -1) *p = (uint8_t)c1;
            if (++phase == 0) phase = off;
        } else {
            if (c2 != -1) *p = (uint8_t)c2;
            if (--phase == 0) phase = -on;
        }
    }
};

int32_t x_dist(int32_t dx) {
    int32_t a = std::abs(dx);
    if (!(MapToggleFlat & 1) && a > MapHalfX) a = MapAreaX - a;
    return a;
}

// The tile is inside the view (rows origin .. origin + rows; columns with the round map's wrap).
bool in_view(uint8_t* win, int32_t x, int32_t y) {
    int32_t oy = map_win(win).origin_y, ox = map_win(win).origin_x;
    if (y < oy || y >= map_win(win).rows_below + map_win(win).rows_above + oy) return false;
    int32_t w = map_win(win).cols_even + map_win(win).cols_odd + ox;
    if (!(MapToggleFlat & 1)) {
        if (x < ox) x += MapAreaX;
        if (w <= x) x -= MapAreaX;
    }
    return x >= ox && x < w;
}

}  // namespace

// Buffer_line (0x5E09D0): a line from (x1, y1) to (x2, y2) in runs of `on` pixels of colour c1 and
// `off` pixels of c2 (-1: transparent), starting at `phase` of the pattern; clipped to the clip
// rectangle (the pattern starts at the first visible pixel). Drawn from the left end.
void buffer_dashed_line(Buffer* b, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t c1, int32_t c2, int32_t on,
                        int32_t off, int32_t phase) {
    if (!b->data) return;
    if (x2 < x1) {
        std::swap(x1, x2);
        std::swap(y1, y2);
        std::swap(c1, c2);
        phase = off - phase + on;
        std::swap(on, off);
    }
    UiRect c = b->clip;
    phase %= on + off;
    phase = phase < on ? phase - on : on - phase + off;
    Dash d{c1, c2, on, off, phase};
    int32_t dx = x2 - x1, dy = std::abs(y2 - y1), sy = y2 >= y1 ? 1 : -1;
    bool steep = dy > dx;
    int32_t major = steep ? dy : dx, minor = steep ? dx : dy;
    int32_t err = minor * 2 - major, x = x1, y = y1;
    bool started = false;
    for (int32_t i = 0; i <= major; i++) {
        bool in = x >= c.left && x < c.right && y >= c.top && y < c.bottom;
        if (in) {
            started = true;
            d.put(b, x, y);
        } else if (started) {
            break;
        }
        if (err < 1) {
            err += minor * 2;
            if (steep) y += sy;
            else x++;
        } else {
            err += (minor - major) * 2;
            x++;
            y += sy;
        }
    }
}

// MapWin_direct_dest_line (0x4672C0): a straight line between the tiles' centres; colour by the
// triad (0 land 0xFA, 1 sea 0xFE, 2 air 0xF9; a group drag 0xFB); solid on the screen overlay
// (solid != 0), else dashed (on, off, phase from the pattern counter 0x939FD4) on the map.
void THISCALL MapWin_direct_dest_line(uint8_t* win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t triad,
                                      int32_t on, int32_t off, int32_t solid) {
    int32_t px1, py1, px2, py2, color = 0;
    pfncTileToPoint(win, x1, y1, &px1, &py1);
    pfncTileToPoint(win, x2, y2, &px2, &py2);
    px1 += map_win(win).half_w;
    py1 += map_win(win).half_h;
    px2 += map_win(win).half_w;
    py2 += map_win(win).half_h;
    if (triad == 0) {
        color = 0xFA;
    } else if (triad == 1) {
        color = 0xFE;
        if (!solid) px1++, px2++, py1++, py2++;
    } else if (triad == 2) {
        color = 0xF9;
        if (!solid) py1--, py2--;
    }
    if (map_win(win).group_drag) color = 0xFB;
    if (!solid) {
        MapWinNative* n = ui_mapwin_native(win);
        if (n) buffer_dashed_line(n->canvas, px1, py1, px2, py2, 0, color, on, off, (on - 1 + off) & DashPhase);
        return;
    }
    Buffer* d = ui_desktop_canvas();
    Buffer_line_2(d, px1, py1, px2, py2, color);
    ui_invalidate();
}

// MapWin_dest_line (0x467430): see the file comment. mode: the triad (2 air: always straight).
void THISCALL MapWin_dest_line(uint8_t* win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id,
                               int32_t faction_id, int32_t mode, int32_t patrol, int32_t on, int32_t off,
                               int32_t solid) {
    bool direct = false;
    if (mode == 0) {
        if (map_tile(x1, y1)->region != map_tile(x2, y2)->region) direct = true;
    } else if (mode == 1) {
        int32_t b1 = base_at(x1, y1), b2 = base_at(x2, y2), r;
        if (b1 < 0) {
            if (b2 >= 0) r = base_on_sea(b2, map_tile(x1, y1)->region);
            else r = map_tile(x1, y1)->region == map_tile(x2, y2)->region;
        } else if (b2 < 0) {
            r = base_on_sea(b1, map_tile(x2, y2)->region);
        } else {
            r = port_to_port(b1, b2);
        }
        if (!r) direct = true;
    } else if (mode == 2) {
        direct = true;
    }
    for (int32_t pass = 0;; pass++) {
        if (direct) {
            DestLineCost = (x_dist(x1 - x2) + std::abs(y1 - y2)) / 2 * Rules.move_rate_roads;
            MapWin_direct_dest_line(win, x1, y1, x2, y2, mode, on, off, solid);
            return;
        }
        if (pass > 1) return;
        // Pass 0 follows the path to the end (an unknown tile or no path: a straight line);
        // pass 1 draws it and sums its cost.
        int32_t steps = 0, dir = -1, x = x1, y = y1;
        DestLineCost = 0;
        for (;;) {
            uint32_t flags = patrol ? 0x80 : 0xC0;
            if (!Weapon[Units[unit_id].weapon_id].offense_value &&
                (SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First] < 0 ||
                 Bases[SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First]].faction_id != faction_id))
                flags |= 0x10;
            if ((x_dist(x - x2) + std::abs(y - y2) & ~1) > 2) {
                dir = Path_find(&Paths, x, y, x2, y2, unit_id, faction_id, (int32_t)flags, dir);
                if (dir < 0) {
                    direct = true;
                    break;
                }
            } else {
                dir = compass_move(x, y, x2, y2);
            }
            int32_t nx = x + NearOffsetX[dir], ny = y + NearOffsetY[dir];
            if (!(MapToggleFlat & 1)) {
                if (nx < 0) nx += MapAreaX;
                else if (nx >= MapAreaX) nx -= MapAreaX;
            }
            if (pass) {
                if (unit_id >= 0)
                    DestLineCost +=
                        hex_cost(unit_id, faction_id, x, y, nx, ny, speed_proto(unit_id) == Rules.move_rate_roads);
                if (in_view(win, x, y) || in_view(win, nx, ny))
                    MapWin_direct_dest_line(win, x, y, nx, ny, mode, on, off, solid);
            }
            if (nx == x2 && ny == y2) break;
            if (!(Factions[faction_id].player_flags & 0x200) &&
                !(map_tile(nx, ny)->visibility & (1 << (faction_id & 31))) && !(GameState & STATE_OMNISCIENT_VIEW) &&
                !(GameRules & RULES_NO_UNITY_SURVEY))
                direct = true;
            if (++steps > MapAreaX * 2) {
                direct = true;
                break;
            }
            x = nx;
            y = ny;
            if (direct) break;
        }
    }
}

// MapWin_draw_base_dest (0x467970): the map's destination lines by display option: 0x40000 the
// player's bases' rally points (dashed 3/5), 0x400000 patrol routes, 0x800000 go-to orders (2/2);
// `redraw`: the view is updated when a line was drawn.
void THISCALL MapWin_draw_base_dest(uint8_t* win, int32_t redraw) {
    uint32_t opts = (uint32_t)map_win(win).options;
    bool drawn = false;
    if (opts & 0x40000) {
        for (int32_t b = 0; b < BaseCount; b++) {
            if (Bases[b].faction_id != CurrentPlayerFaction) continue;
            int32_t triad = 10;
            for (int32_t k = 0; k < 3; k++) {
                int32_t to = reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(&Bases[b]) + 0x110)[k];
                if (to < 0) continue;
                int32_t bx = Bases[b].x, by = Bases[b].y, tx = Bases[to].x, ty = Bases[to].y;
                if (!in_view(win, bx, by) && !in_view(win, tx, ty)) continue;
                MapWin_dest_line(win, bx, by, tx, ty, triad, CurrentPlayerFaction, k, 0, 3, 5, 0);
                drawn = true;
            }
        }
    }
    if (opts & 0xC00000) {
        for (int32_t v = 0; v < VehCount; v++) {
            VEH& u = Vehs[v];
            if (u.faction_id != CurrentPlayerFaction) continue;
            bool patrol = (u.state & 0x1000200) == 0x1000200 && (opts & 0x400000);
            if (!patrol && !(u.order == 0x18 && (opts & 0x800000))) continue;
            int32_t triad = Chassis[Units[u.unit_id].chassis_id].triad;
            bool pflag = (u.state & 0x1000200) == 0x1000200;
            if (patrol) {
                int32_t n = u.waypoint_count;
                for (int32_t i = 0; i < n + (n > 1); i++) {
                    int32_t a = i % (n + 1), c = (i + 1) % (n + 1);
                    MapWin_dest_line(win, u.waypoint_x[a], u.waypoint_y[a], u.waypoint_x[c], u.waypoint_y[c], u.unit_id,
                                     u.faction_id, triad, pflag, 3, 5, 0);
                    n = u.waypoint_count;
                }
            } else {
                MapWin_dest_line(win, u.x, u.y, u.waypoint_x[0], u.waypoint_y[0], u.unit_id, u.faction_id, triad, pflag,
                                 2, 2, 0);
                if (u.state & 0x2000000) {
                    for (int32_t i = 0; i < u.waypoint_count; i++)
                        MapWin_dest_line(win, u.waypoint_x[i], u.waypoint_y[i], u.waypoint_x[i + 1],
                                         u.waypoint_y[i + 1], u.unit_id, u.faction_id, triad, pflag, 2, 2, 0);
                }
            }
            drawn = true;
        }
    }
    if (redraw && drawn) ui_invalidate();
}
