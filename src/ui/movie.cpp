// Movies (ui module): amovie_project (0x403BA0: the secret project's movie by its line in
// movlist.txt's #PROJECTS) and amovie_project_2 (0x403BE0: movies\NAME.wve, else the CD's, else
// movies\NAME.avi; the opening, faction, defeat and victory movies), played by FUN_004BEA50 (the
// EA movie player of the video module, 640 x 480: Enter, Esc, space or a click end it).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"
#include "ui/sound.h"
#include "ui/win.h"

void GAMEAPI amovie_project(int32_t project_id) {
    if (text_open("MOVLIST", "PROJECTS")) return;
    text_get();
    for (int32_t i = 0; i < project_id; i++) text_get();
    amovie_project_2(text_item());
}

void GAMEAPI amovie_project_2(const char* name) {
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

bool movie_play(const char*) { return false; }
