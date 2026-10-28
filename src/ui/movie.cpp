// Movies (ui module): amovie_project (0x403BA0: the secret project's movie by its line in
// movlist.txt's #PROJECTS) and amovie_project_name (0x403BE0: movies\NAME.wve, else the CD's, else
// movies\NAME.avi; the opening, faction, defeat and victory movies), played by amovie_play
// (0x4BEA50) (the EA movie player of the video module, 640 x 480: Enter, Esc, space or a click end
// it). The WVE files are decoded by wve.cpp; the frames (15 per second, timed by the clock from the
// start of the sound) fill the window keeping their aspect, the other sounds are switched off
// meanwhile (as the player's SoundSys_off (0x4C51D0) / SoundSys_stop_all (0x4C4FC0)). Unattended
// runs (--hidden, scripted keys) do not wait: they decode the first SMACX_MOVIE_FRAMES frames
// (default none) and, when it is set, write the last one to OUTDIR/movie.bmp (test aid).
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <string>

#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/sound.h"
#include "ui/win.h"
#include "ui/wve.h"

void GAMEAPI amovie_project(int32_t project_id) {
    if (text_open("MOVLIST", "PROJECTS")) return;
    text_get();
    for (int32_t i = 0; i < project_id; i++) text_get();
    amovie_project_name(text_item());
}

void GAMEAPI amovie_project_name(const char* name) {
    sound_log("movie %s", name ? name : "");
    if (!name) return;
    std::string path = std::string("movies\\") + name + ".wve";
    if (filefind_get(path.c_str())) {
        movie_play(path.c_str());
        return;
    }
    // The CD's movies (prefs Firaxis\...) and movies\NAME.avi (an AVI through MCI): not present in
    // the GOG data.
}

namespace {
void write_bmp(const std::string& path, const std::vector<uint32_t>& px, int32_t w, int32_t h) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    int32_t row = (w * 3 + 3) & ~3;
    uint32_t size = 54 + (uint32_t)(row * h);
    uint8_t hdr[54] = {'B', 'M'};
    auto put = [&](int32_t o, uint32_t v) {
        for (int32_t k = 0; k < 4; k++) hdr[o + k] = (uint8_t)(v >> (8 * k));
    };
    put(2, size);
    put(10, 54);
    put(14, 40);
    put(18, (uint32_t)w);
    put(22, (uint32_t)h);
    hdr[26] = 1;
    hdr[28] = 24;
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> line((size_t)row, 0);
    for (int32_t y = h - 1; y >= 0; y--) {
        for (int32_t x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * w + x];
            line[(size_t)x * 3] = (uint8_t)p;
            line[(size_t)x * 3 + 1] = (uint8_t)(p >> 8);
            line[(size_t)x * 3 + 2] = (uint8_t)(p >> 16);
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fclose(f);
}
}  // namespace

bool movie_play(const char* path) {
    WveMovie m;
    if (!m.open(path)) {
        std::fprintf(stderr, "movie %s: cannot be played\n", path);
        return false;
    }
    auto snd = m.sound();
    std::fprintf(stderr, "movie %s: %dx%d, %d frames, sound %u ms\n", path, m.width(), m.height(), m.frames(),
                 snd ? snd->ms() : 0);
    std::vector<uint32_t> rgb;
    if (const char* e = std::getenv("SMACX_MOVIE_FRAMES")) {  // test aid: the first N frames decoded
        int32_t n = std::min(std::atoi(e), m.frames());
        int32_t good = 0;
        for (int32_t i = 0; i < n; i++) good += m.decode(i, &rgb) ? 1 : 0;
        if (n > 0) {
            std::fprintf(stderr, "movie %s: %d of %d frames decoded\n", path, good, n);
            write_bmp(g_ui_outdir + "/movie.bmp", rgb, m.width(), m.height());
        }
    }
    if (g_ui_unattended) return true;
    sound_enable_master(false);
    int32_t voice = snd ? platform::audio_play(snd, platform::VoiceParams{}) : 0;
    uint32_t start = platform::ticks_ms();
    int32_t shown = -1;
    for (;;) {
        int32_t frame = (int32_t)((platform::ticks_ms() - start) * WveMovie::kFps / 1000);
        if (frame >= m.frames()) break;
        if (frame != shown && m.decode(frame, &rgb)) {
            platform::video_present_rgb(rgb.data(), m.width(), m.height());
            shown = frame;
        }
        platform::Event ev;
        bool skip = false;
        while (platform::poll_event(&ev)) {
            if ((ev.type == platform::EV_KEY_DOWN &&
                 (ev.key == VKEY_RETURN || ev.key == VKEY_ESCAPE || ev.key == VKEY_SPACE)) ||
                ev.type == platform::EV_LBUTTON_DOWN || ev.type == platform::EV_RBUTTON_DOWN ||
                ev.type == platform::EV_QUIT)
                skip = true;
            // The close button: before a game runs (the opening movie) smacx quits; in a game
            // the movie plays on (win.h ui_window_close_modal).
            if (ev.type == platform::EV_CLOSE && ui_window_close_modal()) skip = true;
        }
        if (skip) break;
        platform::delay_ms(5);
    }
    platform::audio_stop(voice);
    sound_enable_master(true);
    ui_invalidate();
    ui_present();
    return true;
}
