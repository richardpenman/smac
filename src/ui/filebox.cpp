// The save / load file dialogs of save_game, load_game and load_map (smacx's replacement of the
// headless "filebox" area: FileDialog, FileBox, FileList; engine/ui_bridge.h).
//
// FileBox (ui 0x407350..0x407BE0) and its FileList (gfx2 0x613900..0x614C50), as the original lays
// them out (FileBox_init 0x407410): a window of 334 x 393 pixels at ((width - 316) / 2, above the
// main interface's height) at the main menu, 394 x 401 shifted 23 pixels left in a game; its frame
// (the large frame at the menu, box 9 in a game), box 0 inside, the title box (box 6) with SAVE or
// LOAD (labels 674 / 673, find_font(14) bold, colour 0xA0), the list (directories then the files of
// the dialog's extensions, as DlgDirList lists them: "..", the sub-directories, the files, sorted;
// the jackal.pcx icons: file, folder, up; rows of 24 pixels, the selected row in box 29), the file
// name's edit field and OK / CANCEL. Keys: typing edits the name, Up / Down / Home / End / PgUp /
// PgDn move in the list (the selected entry's name goes to the edit field), Enter is OK, Esc is
// CANCEL; a click selects, a double click is OK.
// OK (FUN_006146A0): ".." goes up; a directory is entered; a file name (the dialog's extension
// added when it has none) is the answer when loading it exists (else FILEWIN_LOAD), when saving it
// is new (else FILEWIN_SAVEEXISTS asks) and has no illegal character (FILEWIN_ILLEGALCHAR).
// The original changes the process's current directory while browsing and answers an absolute
// path; here the directory is kept relative to the game directory (Windows separators), so the
// answer is "saves\<name>.SAV" as the engine's file layer resolves it; an absolute path typed in
// the edit field is used as it is.
// The Windows common dialog (FileDialog, preference WindowsFileBox) is not available: FileBox is used.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/game.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/basewin_internal.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/scroll.h"
#include "ui/win.h"

namespace fs = std::filesystem;

namespace {

int32_t mem_i32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }

bool ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((uint8_t)a[i]) != std::tolower((uint8_t)b[i])) return false;
    return true;
}
bool has_ext(const std::string& s, const std::string& ext) {
    return s.size() >= ext.size() && ieq(s.substr(s.size() - ext.size()), ext);
}
std::string join(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") return name;
    return dir + (dir.back() == '\\' || dir.back() == '/' ? "" : "\\") + name;
}
std::string host(const std::string& p) {
    char out[1024];
    platform::resolve_path(p.empty() ? "." : p.c_str(), out, sizeof(out));
    return out;
}

// FileList's state (FileList_set_path / add_ext; the FileBox object in the memory image is not used).
std::string g_path;               // +0x410 the name, +0x531 the directory (FileList_set_path)
std::vector<std::string> g_exts;  // +0x518 (FileList_add_ext, up to 4)
std::string g_cwd = "";           // the browsing directory, relative to the game directory
char g_result[1024];

struct FileBoxWin : ScreenWin {
    int32_t kind = 200;      // +0x638: 100 save, 200 load, 300 cancelled
    bool in_game = false;    // pfncWinIsVisible(main interface)
    struct Entry {
        std::string name;
        int32_t type;  // 0 directory, 1 file, 2 ".."
    };
    std::vector<Entry> entries;
    int32_t sel = -1, top = 0, row_h = 24;
    std::string edit;
    bool fresh = true;  // the edit text is still the selected default
    UiRect list_r{}, edit_r{}, title_r{};
    Sprite icons[3];
    Font* list_font = nullptr;
    Font* title_font = nullptr;
    Font btn_font;
    std::string answer;

    int32_t rows() const { return std::max(1, (list_r.bottom - list_r.top) / row_h); }

    // FUN_00614360: the directory's entries.
    void fill(const std::string& dir) {
        std::error_code ec;
        std::string h = host(dir);
        if (!fs::is_directory(h, ec)) return;
        g_cwd = dir;
        entries.clear();
        std::vector<std::string> dirs, files;
        for (const auto& e : fs::directory_iterator(h, ec)) {
            std::string n = e.path().filename().string();
            if (n == "." || n == "..") continue;
            if (e.is_directory(ec)) dirs.push_back(n);
            else
                for (const std::string& x : g_exts)
                    if (has_ext(n, x)) {
                        files.push_back(n);
                        break;
                    }
        }
        auto ci = [](const std::string& a, const std::string& b) {
            return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
                return std::tolower((uint8_t)x) < std::tolower((uint8_t)y);
            });
        };
        std::sort(dirs.begin(), dirs.end(), ci);
        std::sort(files.begin(), files.end(), ci);
        entries.push_back({"..", 2});
        for (const std::string& d : dirs) entries.push_back({d, 0});
        for (const std::string& f : files) entries.push_back({f, 1});
        sel = entries.empty() ? -1 : 0;
        top = 0;
    }

    void layout(int32_t kind_) {
        kind = kind_;
        in_game = !ui_desktop() && !GameNotStarted;  // pfncWinIsVisible(main interface)
        int32_t u4 = in_game ? 0x17 : 0, u3 = in_game ? 0 : 1;
        int32_t mi_h = 213;  // DAT_007AF59C: the main interface's height
        int32_t w = (in_game ? 0x3C : 0) + 0x14E, h = (in_game ? 9 : 0) + 0x16E + u3 + 0x1A;
        int32_t x = (ScreenWidth - 0x13C) / 2 - u4, y = ScreenHeight - mi_h - (u3 + 0x1A) - 0x177;
        sw_init(this, x, y, w, h, false);
        list_r = UiRect{u4 + 5, u3 + 0x20, u4 + 0x136, u3 + 0x140};
        edit_r = UiRect{u4 + 8, u3 + 0x142, u4 + 0x134, u3 + 0x156};
        int32_t mid = ((u4 + 0x134 - (u4 + 10)) + 1) / 2 - 3 + (u4 + 10);
        UiRect ok{u4 + 10, u3 + 0x15C, mid, u3 + 0x172}, cancel{mid + 3, u3 + 0x15C, u4 + 0x132, u3 + 0x172};
        if (!in_game) {
            list_r = UiRect{u4 + 0xE, u3 + 0x29, u4 + 0x13F, u3 + 0x149};
            edit_r = UiRect{u4 + 0x11, u3 + 0x14B, u4 + 0x13D, u3 + 0x15F};
            ok = UiRect{u4 + 0x13, u3 + 0x165, mid + 9, u3 + 0x17B};
            cancel = UiRect{mid + 0xC, u3 + 0x165, u4 + 0x13B, u3 + 0x17B};
        }
        Font_ctor(&btn_font);
        Font_init(&btn_font, "Arial", 0xE, 1);  // MainInterface +0x2166C
        list_font = find_font(0xC, 0);          // MainInterface +0x21694: Arial 12
        title_font = find_font(0xE, 1);
        for (const UiRect* r : {&ok, &cancel}) {
            const char* label = r == &ok ? label_get(101) : label_get(102);
            std::string t = sw_upper(label);
            sw_button(this, t.c_str(), r == &ok ? -1 : -2, r->left, r->top, r->right - r->left, r->bottom - r->top,
                      0, &btn_font);
        }
        // jackal.pcx: the up, folder and file icons (FUN_00613BA0).
        Buffer j;
        Buffer_ctor(&j);
        if (!Buffer_load_pcx(&j, "jackal.pcx", nullptr, 10, 0xEC)) {
            pfncSpriteFromCanvasRectTrans(&icons[0], &j, 9, 1, 1, 0x10, 0x10, 0);
            pfncSpriteFromCanvasRectTrans(&icons[1], &j, 9, 0x12, 1, 0x10, 0x10, 0);
            pfncSpriteFromCanvasRectTrans(&icons[2], &j, 9, 0x23, 1, 0x10, 0x10, 0);
        }
        spot_init(&spots[0], 4);
        spot_add(&spots[0], 0, 0, list_r.left, list_r.top, list_r.right - list_r.left, list_r.bottom - list_r.top);
    }

    void redraw() override {
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 9);
        UiRect r = all;
        if (!in_game) {
            Buffer_box_sprite(&canvas, &r, box_def_large());
            r = UiRect{r.left + 9, r.top + 9, r.right - 9, r.bottom - 9};
        } else {
            r = UiRect{r.left + 0x17, r.top, r.right - 0x37, r.bottom + 10};
            Buffer_box_sprite(&canvas, &r, box_def(9));
        }
        UiRect in{r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
        Buffer_box_sprite(&canvas, &in, box_def(0));
        // The title (FUN_00407920 / FUN_00407A80).
        title_r = UiRect{r.left + 6, r.top + 6, r.right - 6, r.top + 0x1A};
        Buffer_box_sprite(&canvas, &title_r, box_def(6));
        sw_font(&canvas, title_font, 0xA0);
        sw_text_cent(this, &canvas, sw_upper(label_get(kind == 100 ? 0xA88 / 4 : 0xA84 / 4)), title_r);
        // The list.
        Buffer_set_clip(&canvas, &list_r);
        int32_t n = rows();
        for (int32_t i = top; i < (int32_t)entries.size() && i < top + n; i++) {
            int32_t y = list_r.top + 3 + (i - top) * row_h;
            bool s = i == sel;
            if (s) {
                UiRect band{list_r.left + 4, y, list_r.right + 3, y + 0x10};
                Buffer_box_sprite(&canvas, &band, box_def(29));
            }
            int32_t t = entries[i].type;
            Sprite* ic = &icons[t == 2 ? 2 : (t == 0 ? 1 : 0)];  // FUN_00614C50
            if (ic->width) sprite_copy1(ic, &canvas, ic->key, list_r.left + 9, y, 1, 1);
            sw_font(&canvas, list_font, s ? mem_i32(0x8C6C8C) : mem_i32(0x8C6B5C));
            sw_text(this, &canvas, entries[i].name, list_r.left + 0x1D, y + 1);
        }
        // The list's scroll bar (scroll_normal) when the entries exceed the rows.
        if ((int32_t)entries.size() > n) {
            UiRect lr = list_r;
            scroll_draw(&canvas, scroll_for_list(&lr, false, top, (int32_t)entries.size(), n));
        }
        Buffer_set_clip(&canvas, &all);
        // The edit field.
        Buffer_box_2(&canvas, &edit_r, mem_i32(0x8C6B7C), mem_i32(0x8C6B7C));
        sw_font(&canvas, list_font, mem_i32(0x8C6B6C));
        UiRect et{edit_r.left + 4, edit_r.top + 3, edit_r.right - 4, edit_r.bottom - 2};
        Buffer_set_clip(&canvas, &et);
        int32_t tw = Buffer_text_width(&canvas, edit.c_str());
        if (fresh && !edit.empty()) {
            UiRect hl{et.left, et.top, et.left + tw, et.bottom};
            Buffer_fill_3(&canvas, &hl, mem_i32(0x8C6B74));
        }
        sw_text(this, &canvas, edit, et.left, et.top);
        if (!edit.empty()) Buffer_vline_2(&canvas, et.left + tw, et.top, et.bottom - 1, mem_i32(0x8C6B6C));
        Buffer_set_clip(&canvas, &all);
    }

    void select(int32_t i) {
        if (entries.empty()) return;
        sel = std::max(0, std::min((int32_t)entries.size() - 1, i));
        if (sel < top) top = sel;
        if (sel >= top + rows()) top = sel - rows() + 1;
        edit = entries[sel].name;
        fresh = true;
    }

    // FUN_006146A0: OK.
    void ok() {
        std::string name = edit;
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!name.empty() && name[0] == ' ') name.erase(0, 1);
        if (name.empty()) return;
        if (name == "..") {
            std::string up = g_cwd.empty() || g_cwd == "." ? ".." : g_cwd + "\\..";
            // Collapse "dir\.." to the parent.
            size_t s = g_cwd.find_last_of("\\/");
            if (!g_cwd.empty() && g_cwd != "." && g_cwd.substr(s == std::string::npos ? 0 : s + 1) != "..")
                up = s == std::string::npos ? "" : g_cwd.substr(0, s);
            fill(up);
            edit.clear();
            return;
        }
        bool absolute = name[0] == '/' || name[0] == '\\' || (name.size() > 1 && name[1] == ':');
        std::string full = absolute ? name : join(g_cwd, name);
        std::error_code ec;
        std::string part = name.substr(name.find_last_of("\\/") == std::string::npos ? 0 : name.find_last_of("\\/") + 1);
        bool has_dot = part.find('.') != std::string::npos;
        if (!has_ext(name, g_exts.empty() ? "" : g_exts[0]) && fs::is_directory(host(full), ec)) {
            fill(full);  // a directory: entered
            edit.clear();
            return;
        }
        // The file: the dialog's extension unless it has one of them.
        bool ext_ok = false;
        for (const std::string& x : g_exts)
            if (has_ext(name, x)) ext_ok = true;
        if (!ext_ok && !has_dot && !g_exts.empty()) full += g_exts[0];
        bool exists = fs::is_regular_file(host(full), ec);
        std::string file = full.substr(full.find_last_of("\\/") == std::string::npos ? 0 : full.find_last_of("\\/") + 1);
        if (kind == 200) {
            if (!exists) {
                parse_says(0, file.c_str(), -1, -1);
                pops_18("jackal", "FILEWIN_LOAD", -1, nullptr, 0x200000, nullptr, 1, 1, 0);
                return;
            }
        } else {
            if (exists) {
                parse_says(0, file.c_str(), -1, -1);
                if (pops_18("jackal", "FILEWIN_SAVEEXISTS", -1, nullptr, 0x200040, nullptr, 1, 1, 0) != 0) return;
            } else if (file.find_first_of("\\\"/[]:;=") != std::string::npos) {
                pops_18("jackal", "FILEWIN_ILLEGALCHAR", -1, nullptr, 0x200040, nullptr, 1, 1, 0);
                return;
            }
        }
        answer = full;
        sw_close(this);
    }

    void on_button(int32_t id) override {
        if (id == -2) {
            kind = 300;
            sw_close(this);
        } else {
            ok();
        }
    }
    void on_key(int32_t key) override {
        int32_t vk = key & 0xFFFF;
        switch (vk) {
        case VKEY_RETURN: ok(); return;
        case VKEY_ESCAPE: on_button(-2); return;
        case VKEY_UP: select(sel - 1); return;
        case VKEY_DOWN: select(sel + 1); return;
        case VKEY_HOME: select(0); return;
        case VKEY_END: select((int32_t)entries.size() - 1); return;
        case VKEY_PRIOR: select(sel - rows()); return;
        case VKEY_NEXT: select(sel + rows()); return;
        case VKEY_BACK:
            if (fresh) edit.clear();
            else if (!edit.empty()) edit.pop_back();
            fresh = false;
            return;
        default: break;
        }
        // A character (US layout, as the original's WM_CHAR).
        bool shift = key & 0x10000;
        char c = 0;
        if (vk >= 'A' && vk <= 'Z') c = shift ? (char)vk : (char)(vk + 0x20);
        else if (vk >= '0' && vk <= '9') c = shift ? ")!@#$%^&*("[vk - '0'] : (char)vk;
        else if (vk == VKEY_SPACE) c = ' ';
        else {
            static const int32_t kVk[] = {0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0xDE};
            static const char kPlain[] = ";=,-./`[\\]'", kShift[] = ":+<_>?~{|}\"";
            for (int32_t i = 0; i < 11; i++)
                if (vk == kVk[i]) c = shift ? kShift[i] : kPlain[i];
        }
        if (!c || (key & 0x20000)) return;
        if (fresh) edit.clear();
        fresh = false;
        if (edit.size() < 0x103) edit += c;
    }
    void on_click(int32_t parent, int32_t index, int32_t, int32_t x, int32_t y, bool right, bool dbl) override {
        if (parent != 0 || right || index != 0) return;
        if ((int32_t)entries.size() > rows()) {
            UiRect lr = list_r;
            ScrollBar sb = scroll_for_list(&lr, false, top, (int32_t)entries.size(), rows());
            if (x >= sb.r.left) {
                top = std::max(0, std::min(top + scroll_click(sb, x, y, rows()), sb.max));
                return;
            }
        }
        int32_t i = top + (y - list_r.top - 3) / row_h;
        if (i < 0 || i >= (int32_t)entries.size()) return;
        select(i);
        if (dbl) ok();
    }
    void log_state() override {
        std::fprintf(stderr, "screenstate filebox kind=%d dir=\"%s\" selected=%d edit=\"%s\" entries", kind,
                     g_cwd.c_str(), sel, edit.c_str());
        for (const Entry& e : entries) std::fprintf(stderr, " [%s%s]", e.type == 1 ? "" : "<", e.name.c_str());
        std::fprintf(stderr, "\n");
    }
};

// FileBox_save / FileBox_load (FUN_00613D50 / FUN_00613F60): the list of the path's directory, the
// path's file selected (load) or its name in the edit field (save), the modal loop.
char* run(int32_t kind) {
    FileBoxWin w;
    w.name = "filebox";
    w.layout(kind);
    std::string dir, name = g_path;
    size_t s = g_path.find_last_of("\\/");
    if (s != std::string::npos) {
        dir = g_path.substr(0, s);
        name = g_path.substr(s + 1);
    }
    w.fill(dir.empty() ? std::string(".") : dir);
    if (name.find('*') != std::string::npos) {
        // A pattern ("saves\*.SAV"): FindFirstFile's first file is selected.
        name.clear();
        for (size_t i = 0; i < w.entries.size(); i++)
            if (w.entries[i].type == 1) {
                w.select((int32_t)i);
                break;
            }
    }
    if (!name.empty()) {
        bool found = false;
        for (size_t i = 0; i < w.entries.size(); i++)
            if (ieq(w.entries[i].name, name) || ieq(w.entries[i].name, name + (g_exts.empty() ? "" : g_exts[0]))) {
                w.select((int32_t)i);
                found = true;
                break;
            }
        // EditBox_set_text: the proposed name, the extension added when it has none.
        if (!found && kind == 100) w.edit = name.find('.') == std::string::npos && !g_exts.empty() ? name + g_exts[0] : name;
        if (!found && kind == 200) w.edit.clear();
    }
    std::fprintf(stderr, "filebox %s: %s (%d entries, path %s)\n", kind == 100 ? "save" : "load", g_cwd.c_str(),
                 (int32_t)w.entries.size(), g_path.c_str());
    sw_run(&w);
    sw_clear_buttons(&w);
    if (w.answer.empty()) {
        std::fprintf(stderr, "filebox: cancelled\n");
        return nullptr;
    }
    std::snprintf(g_result, sizeof(g_result), "%s", w.answer.c_str());
    std::fprintf(stderr, "filebox: %s\n", g_result);
    return g_result;
}

}  // namespace

void THISCALL FileDialog_ctor(uint8_t* self) { std::memset(self, 0, kFileDialogSize); }
void THISCALL FileDialog_dtor(uint8_t*) {}
// The Windows dialog ("WindowsFileBox" preference) is not available: the in-game box is used.
char* THISCALL FileDialog_save(uint8_t* self) { return FileBox_save(self); }
char* THISCALL FileDialog_open(uint8_t* self) { return FileBox_load(self); }
int32_t THISCALL FileDialog_add_filter(uint8_t*, const char*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_ext(uint8_t*, const char*) { return 0; }
int32_t THISCALL FileDialog_set_name(uint8_t*, const char*) { return 0; }
void THISCALL FileBox_ctor(uint8_t* self) {
    std::memset(self, 0, kFileBoxSize);
    g_exts.clear();
    g_path.clear();
}
void THISCALL FileBox_init(uint8_t*) {}
void THISCALL FileBox_close(uint8_t*) {}
int32_t THISCALL FileList_set_path(uint8_t*, const char* path) {
    g_path = path ? path : "";
    return 0;
}
int32_t THISCALL FileList_add_ext(uint8_t*, const char* ext) {
    if ((int32_t)g_exts.size() == 4) return 1;
    if (!ext) return 0x10;
    if (std::strlen(ext) > 4) return 3;
    g_exts.push_back(ext);
    return 0;
}
void THISCALL FileList_close(uint8_t*) {}
void ui_filebox_destroy(uint8_t*) {}

char* THISCALL FileBox_save(uint8_t*) { return run(100); }
char* THISCALL FileBox_load(uint8_t*) { return run(200); }
