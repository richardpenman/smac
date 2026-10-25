// StatusWin (ui 0x4B4990..0x4BA1A0) and its text helpers: the unit / tile description of the
// console's left panes. See statuswin.h.
#include "ui/statuswin.h"

#include <cstring>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/veh.h"

void GAMEAPI say_orders(char* buffer, int32_t veh_id) {
    const VEH& v = Vehs[veh_id];
    uint32_t state = v.state;
    // Automated units: "(automation) " first, unless on a patrol still under way.
    if ((state & 0x200) && (state & 0x1000200) != 0x1000200 &&
        ((state & 0x2000200) != 0x2000200 || v.patrol_current_point >= v.waypoint_count)) {
        crt_strcat(buffer, "(");
        if (v.order_auto_type == 0xA) {
            char text[80];
            text[0] = 0;
            crt_strcat(text, label_get(444));  // bombing run to $BASENAME0
            int32_t base_id = base_at(v.waypoint_x[1], v.waypoint_y[1]);
            if (base_id >= 0) {
                parse_says(0, Bases[base_id].name, -1, -1);
                char out[80];
                parse_string(text, out);
                crt_strcat(buffer, out);
            }
        } else if (v.order_auto_type == 0xB) {
            crt_strcat(buffer, label_get(445));
        } else if (Units[v.unit_id].plan == PLAN_TERRAFORM && v.order_auto_type) {
            crt_strcat(buffer, label_get(395 + v.order_auto_type));
        } else {
            crt_strcat(buffer, label_get(395));
        }
        crt_strcat(buffer, ")");
        crt_strcat(buffer, " ");
    }
    state = v.state;
    if (state & 0x4000) {  // exploring
        crt_strcat(buffer, Order[ORDER_EXPLORE].order);
        return;
    }
    if ((state & 0x1000200) == 0x1000200) {
        // Waypoints of a group move.
        crt_strcat(buffer, label_get(407));
        crt_strcat(buffer, ": ");
        for (int32_t i = 0; i < v.waypoint_count + 1; i++) {
            say_loc(buffer, v.waypoint_x[i], v.waypoint_y[i], 0, 2, 0);
            if (i < v.waypoint_count) {
                crt_strcat(buffer, " ");
                crt_strcat(buffer, label_get(408));
                crt_strcat(buffer, " ");
            }
        }
        return;
    }
    int32_t is_sea = (map_tile(v.x, v.y)->climate & 0xE0) < 0x60;
    const COrder& o = Order[v.order & 0x3F];
    crt_strcat(buffer, is_sea ? o.order_sea : o.order);
    if (v.order == ORDER_CONVOY) {
        crt_strcat(buffer, " ");
        crt_strcat(buffer, ResName[v.order_auto_type].name_plural);
        if (base_at(v.x, v.y) >= 0) {
            crt_strcat(buffer, " ");
            crt_strcat(buffer, label_get(434));
            crt_strcat(buffer, " ");
            say_base(buffer, v.home_base_id);
        } else {
            crt_strcat(buffer, "(");
            char num[80];
            crt_itoa(resource_yield(v.order_auto_type, v.faction_id, v.home_base_id, v.x, v.y), num, 10);
            crt_strcat(buffer, num);
            crt_strcat(buffer, label_get(379));
            crt_strcat(buffer, ")");
            crt_strcat(buffer, " ");
        }
    }
    if ((v.order & 0x3F) == ORDER_MOVE_TO) {
        crt_strcat(buffer, " ");
        bool patrol = (v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count;
        say_loc(buffer, v.waypoint_x[0], v.waypoint_y[0], 0, patrol ? 2 : 1, 0);
        if ((v.state & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count) {
            for (int32_t i = 0; i < v.waypoint_count; i++) {
                crt_strcat(buffer, " ");
                crt_strcat(buffer, label_get(409));
                crt_strcat(buffer, " ");
                say_loc(buffer, v.waypoint_x[i + 1], v.waypoint_y[i + 1], 0, 2, 0);
            }
        }
    }
    if (v.order == ORDER_MOVE) {
        crt_strcat(buffer, " ");
        crt_strcat(buffer, Compass[v.waypoint_x[0]]);
    }
    if (v.order == ORDER_ROAD_TO || v.order == ORDER_MAGTUBE_TO) {
        crt_strcat(buffer, " ");
        say_loc(buffer, v.waypoint_x[0], v.waypoint_y[0], 0, 2, 0);
    }
}

void GAMEAPI say_orders_2(int32_t veh_id) { say_orders(StrBuffer, veh_id); }
