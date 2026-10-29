// smacx's display settings (platform/display.h): the ini's Screen Size and UI Scale and the
// logical screen and scale chosen for a display.
#include "platform/display.h"
#include "unit.h"

using display::choose;
using display::Mode;
using display::Settings;

namespace {
Mode auto_mode(int32_t w, int32_t h) { return choose(Settings{}, w, h); }
}  // namespace

TEST_CASE("display: the ini's values") {
    int32_t w = -1, h = -1;
    display::parse_size("auto", &w, &h);
    CHECK((w == 0 && h == 0));
    display::parse_size("1920x1080", &w, &h);
    CHECK((w == 1920 && h == 1080));
    display::parse_size("", &w, &h);
    CHECK((w == 0 && h == 0));
    display::parse_size("1920", &w, &h);
    CHECK((w == 0 && h == 0));
    CHECK(display::parse_scale("auto") == 0);
    CHECK(display::parse_scale("2") == 2);
    CHECK(display::parse_scale("0") == 0);
    CHECK(display::parse_scale("12") == 0);
}

TEST_CASE("display: auto size and scale") {
    // 1080p and 1440p: scale 1, the whole display.
    Mode m = auto_mode(1920, 1080);
    CHECK((m.width == 1920 && m.height == 1080 && m.scale == 1));
    m = auto_mode(2560, 1440);
    CHECK((m.width == 2560 && m.height == 1440 && m.scale == 1));
    // 4K: scale 2 and 1920 x 1080 logical; 5K: 3.
    m = auto_mode(3840, 2160);
    CHECK((m.width == 1920 && m.height == 1080 && m.scale == 2));
    m = auto_mode(5120, 2880);
    CHECK((m.width == 1706 && m.height == 960 && m.scale == 3));
    // A Retina MacBook's 2880 x 1800 pixels: 1440 x 900 at 2.
    m = auto_mode(2880, 1800);
    CHECK((m.width == 1440 && m.height == 900 && m.scale == 2));
    // 1366 x 768 keeps its odd width; smaller displays get the 1024 x 768 minimum (scaled down).
    m = auto_mode(1366, 768);
    CHECK((m.width == 1366 && m.height == 768 && m.scale == 1));
    m = auto_mode(1280, 720);
    CHECK((m.width == 1280 && m.height == 768 && m.scale == 1));
    m = auto_mode(800, 600);
    CHECK((m.width == 1024 && m.height == 768 && m.scale == 1));
    // A narrow, tall display: the width limits the scale (1024 logical pixels at least).
    m = auto_mode(1600, 2560);
    CHECK((m.scale == 1 && m.width == 1600 && m.height == 2560));
}

TEST_CASE("display: sizes and scales given") {
    Settings s;
    s.scale = 2;
    Mode m = choose(s, 3840, 2160);
    CHECK((m.width == 1920 && m.height == 1080 && m.scale == 2));
    s.scale = 1;
    m = choose(s, 3840, 2160);
    CHECK((m.width == 3840 && m.height == 2160 && m.scale == 1));
    s.width = 1280;
    s.height = 1024;
    s.scale = 0;
    m = choose(s, 3840, 2160);  // the size kept, the largest scale that fits
    CHECK((m.width == 1280 && m.height == 1024 && m.scale == 2));
    m = choose(s, 2560, 1440);
    CHECK((m.width == 1280 && m.height == 1024 && m.scale == 1));
    s.width = 800;
    s.height = 600;
    m = choose(s, 1920, 1080);  // at least 1024 x 768
    CHECK((m.width == 1024 && m.height == 768 && m.scale == 1));
}
