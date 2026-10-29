// The game's screen size and scale (display.h).
#include "platform/display.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace display {

void parse_size(const char* text, int32_t* width, int32_t* height) {
    *width = *height = 0;
    int w = 0, h = 0;
    if (text && std::sscanf(text, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        *width = w;
        *height = h;
    }
}

int32_t parse_scale(const char* text) {
    int s = 0;
    if (!text || std::sscanf(text, "%d", &s) != 1 || s < 1 || s > 8) return 0;
    return s;
}

Mode choose(const Settings& s, int32_t area_w, int32_t area_h) {
    Mode m;
    area_w = std::max(area_w, 1);
    area_h = std::max(area_h, 1);
    if (s.width > 0 && s.height > 0) {
        m.width = std::max(s.width, kMinWidth);
        m.height = std::max(s.height, kMinHeight);
        // The largest scale at which the size fits the area.
        m.scale = s.scale > 0 ? s.scale : std::max(1, std::min(area_w / m.width, area_h / m.height));
    } else {
        m.scale = s.scale > 0 ? s.scale : std::max(1, std::min(area_h / kMinHeight, area_w / kMinWidth));
        m.width = area_w / m.scale;
        m.height = area_h / m.scale;
    }
    m.width = std::max(m.width, kMinWidth);
    m.height = std::max(m.height, kMinHeight);
    return m;
}

}  // namespace display
