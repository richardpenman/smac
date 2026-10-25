// The diplomacy dialog's interface mode (DiploPop, ui 0x43F0F0..0x440EF0) and the human side of
// the diplomacy dialog (diplomacy module): commlink_attempt (0x558C60), commlink_attempter
// (0x5589E0, the single player path), diplomacy_menu (0x54C560), proposal_menu (0x54DCF0),
// make_a_proposal (0x54F420) and battle_plans (0x54B1C0), reconstructed from the disassembly;
// communicate (0x54FFD0) is in diplo_comm.cpp.
//
// DiploPop: while a commlink is open (DiploPop_start .. DiploPop_stop) the main interface is in
// the diplomacy mode: its left pane shows the bioscan animation (dip_<a|f|m>_<mood>.flc at
// 0x1A, 0x5A of the interface; mood = DiploFriction * 3 / 20, 0..2) and, in the pane's right
// part (DiploPop_init's rectangle +0x20E4), the leader's animation (<faction>.flc, faction.flc
// when missing), the leader's name in capitals, "Attitude:" (Repute[integrity_blemishes]) and
// "Mood:" (Mood[get_mood(friction)]) as DiploPop_draw_portrait writes them. The dialog's texts
// and menus are the script popups (X_pops_11, BasePop menus) above the interface, as the
// original's BasePop_set_def_external_window places them.
// FLC files are decoded here (COLOR_256, BRUN, COPY, BLACK, SS2/DELTA_FLC, LC) and their colours
// mapped to the game palette (the original's player loads their palette into the reserved range).
// Not reconstructed: DiploPop_draw_info / DiploPop_draw_hq (the info and HQ panes stay empty),
// the PlanWin map previews of the base and battle plan menus (FUN_0048b3f0.., battle_routine),
// the multiplayer paths (channels, diplo lock messages, PBEM diplo).
#include "ui/diplowin.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "engine/base.h"
#include "engine/council.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/turn.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/basewin.h"
#include "ui/colortables.h"
#include "ui/popup.h"
#include "ui/win.h"

namespace {

constexpr int32_t kPopWait = 0x5398E0;

// ---- FLC animations ----
struct Flc {
    int32_t width = 0, height = 0, speed = 100;
    std::vector<std::vector<uint8_t>> frames;  // game palette indices
};

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

bool flc_load(const std::string& name, Flc* out) {
    CrtFile* f = crt_fopen(name.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    for (;;) {
        uint32_t n = crt_fread(buf, 1, sizeof(buf), f);
        if (!n) break;
        d.insert(d.end(), buf, buf + n);
    }
    crt_fclose(f);
    if (d.size() < 128 || rd16(&d[4]) != 0xAF12) return false;
    int32_t frames = rd16(&d[6]);
    out->width = rd16(&d[8]);
    out->height = rd16(&d[10]);
    out->speed = (int32_t)rd32(&d[16]);
    if (out->speed <= 0) out->speed = 100;
    uint32_t off = rd32(&d[80]);
    if (!off) off = 128;
    int32_t w = out->width, h = out->height;
    std::vector<uint8_t> pix((size_t)w * h, 0);
    uint8_t pal[256][3]{};
    std::vector<uint8_t> map(256, 0);
    auto remap = [&]() {
        for (int32_t i = 0; i < 256; i++)
            map[i] = (uint8_t)Palette_get_nearest_palette_index(&GamePalette, pal[i][0] << 2 >> 2, pal[i][1], pal[i][2], 1);
    };
    for (int32_t fr = 0; fr < frames && off + 16 <= d.size(); fr++) {
        uint32_t fsize = rd32(&d[off]);
        if (rd16(&d[off + 4]) == 0xF1FA) {
            int32_t nch = rd16(&d[off + 6]);
            uint32_t co = off + 16;
            for (int32_t c = 0; c < nch && co + 6 <= d.size(); c++) {
                uint32_t csize = rd32(&d[co]);
                int32_t type = rd16(&d[co + 4]);
                const uint8_t* p = &d[co + 6];
                const uint8_t* end = &d[0] + std::min<size_t>(d.size(), co + csize);
                if (type == 4 || type == 11) {  // COLOR_256 / COLOR_64
                    int32_t packets = rd16(p);
                    p += 2;
                    int32_t idx = 0;
                    for (int32_t k = 0; k < packets && p + 2 <= end; k++) {
                        idx += *p++;
                        int32_t n = *p++;
                        if (!n) n = 256;
                        for (int32_t j = 0; j < n && idx < 256 && p + 3 <= end; j++, idx++) {
                            int32_t s = type == 11 ? 2 : 0;
                            pal[idx][0] = (uint8_t)(p[0] << s);
                            pal[idx][1] = (uint8_t)(p[1] << s);
                            pal[idx][2] = (uint8_t)(p[2] << s);
                            p += 3;
                        }
                    }
                    remap();
                } else if (type == 15) {  // BRUN
                    for (int32_t y = 0; y < h && p < end; y++) {
                        p++;  // packet count (unused)
                        int32_t x = 0;
                        while (x < w && p < end) {
                            int8_t n = (int8_t)*p++;
                            if (n >= 0) {
                                for (int32_t j = 0; j < n && x < w; j++) pix[(size_t)y * w + x++] = *p;
                                p++;
                            } else {
                                for (int32_t j = 0; j < -n && x < w && p < end; j++) pix[(size_t)y * w + x++] = *p++;
                            }
                        }
                    }
                } else if (type == 16) {  // COPY
                    for (size_t i = 0; i < pix.size() && p < end; i++) pix[i] = *p++;
                } else if (type == 13) {  // BLACK
                    std::fill(pix.begin(), pix.end(), 0);
                } else if (type == 7) {  // SS2 (DELTA_FLC)
                    int32_t lines = rd16(p);
                    p += 2;
                    int32_t y = 0;
                    for (int32_t l = 0; l < lines && p + 2 <= end && y < h;) {
                        int16_t word = (int16_t)rd16(p);
                        p += 2;
                        if ((word & 0xC000) == 0xC000) {
                            y += -word;
                            continue;
                        }
                        if ((word & 0xC000) == 0x8000) {
                            if (w > 0) pix[(size_t)y * w + w - 1] = (uint8_t)(word & 0xFF);
                            continue;
                        }
                        int32_t x = 0;
                        for (int32_t k = 0; k < word && p + 2 <= end; k++) {
                            x += *p++;
                            int8_t n = (int8_t)*p++;
                            if (n >= 0) {
                                for (int32_t j = 0; j < n && p + 2 <= end; j++, x += 2) {
                                    if (x < w) pix[(size_t)y * w + x] = p[0];
                                    if (x + 1 < w) pix[(size_t)y * w + x + 1] = p[1];
                                    p += 2;
                                }
                            } else {
                                for (int32_t j = 0; j < -n; j++, x += 2) {
                                    if (x < w) pix[(size_t)y * w + x] = p[0];
                                    if (x + 1 < w) pix[(size_t)y * w + x + 1] = p[1];
                                }
                                p += 2;
                            }
                        }
                        y++;
                        l++;
                    }
                } else if (type == 12) {  // LC (DELTA_FLI)
                    int32_t y = rd16(p), lines = rd16(p + 2);
                    p += 4;
                    for (int32_t l = 0; l < lines && y < h && p < end; l++, y++) {
                        int32_t packets = *p++, x = 0;
                        for (int32_t k = 0; k < packets && p + 2 <= end; k++) {
                            x += *p++;
                            int8_t n = (int8_t)*p++;
                            if (n >= 0) {
                                for (int32_t j = 0; j < n && x < w; j++) pix[(size_t)y * w + x++] = *p++;
                            } else {
                                for (int32_t j = 0; j < -n && x < w; j++) pix[(size_t)y * w + x++] = *p;
                                p++;
                            }
                        }
                    }
                }
                co += csize;
            }
            std::vector<uint8_t> mapped(pix.size());
            for (size_t i = 0; i < pix.size(); i++) mapped[i] = map[pix[i]];
            out->frames.push_back(std::move(mapped));
        }
        off += fsize;
        if (!fsize) break;
    }
    return !out->frames.empty();
}

const Flc* flc_get(const std::string& name) {
    static std::map<std::string, Flc> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.frames.empty() ? nullptr : &it->second;
    Flc& f = cache[name];
    flc_load(name, &f);
    return f.frames.empty() ? nullptr : &f;
}

void flc_draw(Buffer* s, const Flc* f, int32_t x0, int32_t y0, int32_t frame) {
    if (!f) return;
    const std::vector<uint8_t>& px = f->frames[(size_t)frame % f->frames.size()];
    for (int32_t y = 0; y < f->height; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= s->height) continue;
        for (int32_t x = 0; x < f->width; x++) {
            int32_t sx = x0 + x;
            if (sx >= 0 && sx < s->width) s->data[(size_t)sy * s->stride + sx] = px[(size_t)y * f->width + x];
        }
    }
}

// ---- DiploPop state ----
int32_t g_faction = -1;       // +0xA94: the faction on the commlink
std::string g_bioscan, g_portrait;
uint32_t g_start_ms = 0;
bool g_animate = false;

void say_leader(int32_t n, int32_t f) {
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(n, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, m.name_leader, -1, -1);
}
int32_t pop(const char* label, int32_t fw) { return X_pops_11(label, 0x100000, FactionPortraits[fw], kPopWait); }
uint32_t& ds(int32_t a, int32_t b) { return Factions[a].diplo_status[b]; }

}  // namespace

namespace {
int32_t tech_count(int32_t f) {
    int32_t n = 0;
    for (int32_t t = 0; t < 89; t++) n += has_tech(t, f) ? 1 : 0;
    return n;
}
}  // namespace

void diplowin_log_state() {
    int32_t v = CurrentPlayerFaction;
    int32_t techs = 0;
    for (int32_t t = 0; t < 89; t++) techs += has_tech(t, v) ? 1 : 0;
    std::fprintf(stderr, "diplostate f%d credits %d techs %d bases %d governor %d\n", v, Factions[v].energy_credits, techs,
                 Factions[v].base_count, GovernorFaction);
    for (int32_t i = 1; i < 8; i++) {
        if (i == v || !(FactionStatus[1] & (1 << i))) continue;
        const Faction& F = Factions[v];
        std::fprintf(stderr, "diplo %d status %08X agenda %08X friction %d patience %d loan %d/%d credits %d bases %d techs %d\n",
                     i, F.diplo_status[i], F.diplo_agenda[i], Factions[i].diplo_friction[v],
                     Factions[i].diplo_patience[v], F.loan_balance[i], F.loan_payment[i], Factions[i].energy_credits,
                     Factions[i].base_count, tech_count(i));
    }
}

void diplowin_set_animate(bool on) { g_animate = on; }
int32_t diplowin_faction() { return g_faction; }

bool diplowin_draw_console(Buffer* s, int32_t ox, int32_t oy) {
    if (g_faction < 0) return false;
    int32_t fw = g_faction;
    uint32_t t = g_animate ? platform::ticks_ms() - g_start_ms : 0;
    // The bioscan (left part of the left pane).
    if (const Flc* b = flc_get(g_bioscan)) flc_draw(s, b, ox + 0x1A, oy + 0x5A, (int32_t)(t / (uint32_t)b->speed));
    // The portrait rectangle: the left pane's right part, as wide as the animation (at least 40).
    const Flc* pf = flc_get(g_portrait);
    int32_t fw_w = pf ? pf->width : 0;
    if (fw_w < 0x28) fw_w = 0x28;
    UiRect r = {ox + 0xF5 - 4 - fw_w - 2, oy + 0x38 + 4, ox + 0xF5 - 4, oy + 0xFC - 4};
    if (BoxDef* box = box_def(0)) Buffer_box_sprite(s, &r, box);
    if (pf) flc_draw(s, pf, r.left + 1, r.top + (r.bottom - r.top) / 4 + 1, (int32_t)(t / 2000));
    // DiploPop_draw_portrait: name (capitals), attitude, mood.
    int32_t x = r.left + 1 + 4, y = r.top + 1 + 4;
    char text[256];
    std::snprintf(text, sizeof(text), "%s", MFactions[fw].name_leader);
    win32::CharUpperA(text);
    Font* bold = find_font(14, 1);
    Font* plain = find_font(12, 0);
    Buffer_set_font(s, bold, nullptr, nullptr, nullptr);
    Buffer_set_text_color(s, 0xA0, -1, 1, 1);
    Buffer_write_l_3(s, text, x, y, (int32_t)std::strlen(text));
    y += bold ? bold->ascent_above + 2 : 16;
    Buffer_set_font(s, plain, nullptr, nullptr, nullptr);
    int32_t rep = Factions[fw].integrity_blemishes;
    rep = rep < 0 ? 0 : (rep > 7 ? 7 : rep);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0xE90 / 4));
    crt_strcat(StrBuffer, " ");
    say_fac_special(StrBuffer, Strings_get(&StringTable, Repute[rep]), fw);
    Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    y += plain ? plain->ascent_above + 2 : 14;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label_get(0xE94 / 4));
    crt_strcat(StrBuffer, " ");
    int32_t mood = get_mood(Factions[fw].diplo_friction[CurrentPlayerFaction]);
    say_fac_special(StrBuffer, Strings_get(&StringTable, Mood[mood]), fw);
    Buffer_write_l_3(s, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    return true;
}

// ---- DiploPop methods ----

void THISCALL DiploPop_draw_info(uint8_t*) { ui_invalidate(); }
void THISCALL GraphicWin_soft_update_3(uint8_t*, uint8_t*) { ui_invalidate(); }
void THISCALL InfoWin_set_state(uint8_t*, int32_t, int32_t) { ui_invalidate(); }

// DiploPop_start (0x43F2B0): diplomacy_check for the viewer, then (for a new faction) the
// animations: dip_<alien a | female f | male m>_<DiploFriction * 3 / 20, 0..2>.flc and
// <faction file>.flc (faction.flc when missing).
void THISCALL DiploPop_start(uint8_t*, int32_t faction_id_with) {
    diplomacy_check(CurrentPlayerFaction, faction_id_with, 0);
    if (g_faction != faction_id_with) {
        g_faction = faction_id_with;
        const char* kind = (ExpansionEnabled && (MFactions[faction_id_with].rule_flags & 0x80))
                               ? "a_"
                               : (MFactions[faction_id_with].is_leader_female ? "f_" : "m_");
        int32_t mood = DiploFriction * 3 / 20;
        mood = mood < 0 ? 0 : (mood > 2 ? 2 : mood);
        g_bioscan = std::string("dip_") + kind + std::to_string(mood) + ".flc";
        g_portrait = std::string(MFactions[faction_id_with].filename) + ".flc";
        if (!flc_get(g_portrait)) g_portrait = "faction.flc";
        g_start_ms = platform::ticks_ms();
    }
    ui_invalidate();
}

// DiploPop_stop (0x43F5C0): the interface mode released.
void THISCALL DiploPop_stop(uint8_t*) {
    g_faction = -1;
    ui_invalidate();
}

// get_he_she (0x58FCE0): labels 209/210 by the leader's gender.
char* GAMEAPI get_he_she(int32_t faction_id, int32_t mode) {
    crt_strcpy(HisHerBuffer, label_get((MFactions[faction_id].is_leader_female != 0) + 209));
    if (mode == 1) {
        char first[2] = {HisHerBuffer[0], 0};
        win32::CharUpperA(first);
        HisHerBuffer[0] = first[0];
    } else if (mode) {
        win32::CharUpperA(HisHerBuffer);
    }
    return HisHerBuffer;
}

// commlink_attempter (0x5589E0), single player: faction_id_with | 0x10 (not met / eradicated)
// shows the message and refuses. The debug dialog (Ctrl/Shift in debug mode) and the
// multiplayer locks and channels are not reconstructed.
int32_t GAMEAPI commlink_attempter(int32_t, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (!(faction_id_with & 0x10)) return 0;
    int32_t f = faction_id_with & ~0x10;
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.name_leader, -1, -1);
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(2, m.noun_faction, -1, -1);
    NetMsg_pop(&NetMsg, (FactionStatus[1] & (1 << f)) ? "NOTMETYET" : "ERADICATED", 5000, 0, nullptr);
    return 1;
}

// commlink_attempt (0x558C60): the viewer calls faction_id_with (commlink panel, F12 keys).
void GAMEAPI commlink_attempt(int32_t fw) {
    int32_t f = CurrentPlayerFaction;
    if (f == fw || !(FactionStatus[1] & (1 << fw))) return;
    if (SunspotDuration > 0) {
        NetMsg_pop(&NetMsg, "SUNSPOTSNO", 5000, 0, nullptr);
        return;
    }
    bool alien_f = MFactions[f].rule_flags & 0x80, alien_w = MFactions[fw].rule_flags & 0x80;
    if (alien_f && alien_w) {
        GenderDefault = MFactions[f].is_leader_female;
        PluralDefault = 0;
        parse_says(1, MFactions[f].title_leader, -1, -1);
        GenderDefault = MFactions[f].is_leader_female;
        PluralDefault = 0;
        parse_says(3, MFactions[f].name_leader, -1, -1);
        X_pops_11("ALIENHATRED2", 0x100000, FactionPortraits[fw], kPopWait);
        return;
    }
    if (alien_f && !has_tech(0x4E, fw) && !has_tech(4, f)) {
        NetMsg_pop(&NetMsg, "WENEEDHUMANPSYCH", -5000, 0, "Alopdir.pcx");
        return;
    }
    if (alien_w && !has_tech(0x4E, f) && !has_tech(4, fw)) {
        NetMsg_pop(&NetMsg, "WENEEDALIENPSYCH", -5000, 0, "talent_sm.pcx");
        return;
    }
    if (commlink_attempter(f, fw)) return;
    if (MultiplayerActive) return;  // channels and DIPLOCOMM: not reconstructed
    if (PbemActive && (FactionStatus[0] & (1 << fw))) {
        diplo(f, fw);
        return;
    }
    communicate(f, fw, 1);
    treaty_on(f, fw, 0x88000008);
}

// diplomacy_menu (0x54C560): the DIPLO menu (DIPLOMENU lines) of the human's topics.
int32_t GAMEAPI diplomacy_menu(int32_t f, int32_t fw) {
    DiploSecondFaction = fw;
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(0, MFactions[f].name_leader, -1, -1);
    parse_says(1, get_pact_hood(f, fw), -1, -1);
    diplomacy_caption(f, fw);
    ui_diplo_menu_start("DIPLO", 0x100000);
    if (X_text_open(ScriptFileName, "DIPLOMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    const bool shown[9] = {
        true,
        DiploMenuFlagA != 0,
        DiploMenuFlagB == 0,
        DiploMenuFlagB != 0,
        !MultiplayerActive && DiploCommonEnemy > 0,
        !MultiplayerActive && DiploPeaceFaction > 0,
        can_call_council(f, 0) && !(MFactions[f].rule_flags & 0x80) && !(MFactions[fw].rule_flags & 0x80),
        (ds(fw, f) & 1) != 0,
        (ds(f, fw) & 7) != 0,
    };
    const int32_t value[9] = {0, 1, 2, 2, 5, 6, 7, 3, 4};
    for (int32_t i = 0; i < 9; i++) {
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
    }
    text_close();
    int32_t r = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    return r;
}

namespace {
// The faction list of PROPOSEATTACK / PROPOSECOMMLINK (ATTACKSTRING's line for each faction).
void faction_items(int32_t f, int32_t fw, bool commlink) {
    const char* line = text_get();
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || i == f || i == fw) continue;
        if (commlink) {
            if (ds(f, i) & 8) continue;
        } else if (!(ds(f, i) & 8) && !(FactionStatus[0] & (1 << i))) {
            continue;
        }
        const MFaction& m = MFactions[i];
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        StrBuffer[0] = 0;
        parse_string(line ? line : TextBufferGetPtr, StrBuffer);
        ui_diplo_menu_item(StrBuffer, i);
    }
}
bool has_hq(int32_t base_id) {
    int32_t off, mask;
    bitmask(1, &off, &mask);
    return (Bases[base_id].facilities_built[off] & (uint8_t)mask) != 0;
}
// PROPOSEBASE / COUNTERBASE: the bases (without headquarters) to swap; -1 when none was chosen.
int32_t base_menu(const char* label, int32_t owner, int32_t viewer, int32_t fw) {
    ui_diplo_menu_start(label, 0x100040);
    int32_t n = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != owner) continue;
        if (viewer >= 0 && owner != viewer && !(Bases[i].visibility & (1 << viewer))) continue;
        if (has_hq(i)) continue;
        StrBuffer[0] = 0;
        say_base(StrBuffer, i);
        ui_diplo_menu_item(StrBuffer, i);
        n++;
    }
    int32_t r = n ? ui_diplo_menu_exec(fw) : -1;
    ui_diplo_menu_close();
    return r;
}
}  // namespace

// proposal_menu (0x54DCF0): PROPOSAL (the human's proposal), its sub-menus (a faction to attack or
// a commlink to share, a base to ask for) and COUNTER (what is offered in exchange). Sets
// DiploCurrentProposal / DiploCounterProposal (and the base and faction choices); returns the
// proposal (0: none).
int32_t GAMEAPI proposal_menu(int32_t f, int32_t fw) {
    DiploBidBaseSwapID = -1;
    DiploAskBaseSwapID = -1;
    DiploSecondFaction = fw;
    DiploCounterProposal = 0;
    DiploCurrentProposal = 0;
    DiploProposalFaction = 0;
    int32_t swap_bases = 0, known = 0, unknown = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((uint8_t)Bases[i].faction_id != fw) continue;
        if (fw != f && !(Bases[i].visibility & (1 << f))) continue;
        if (!has_hq(i)) swap_bases++;
    }
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || i == f || i == fw) continue;
        uint32_t c = ds(f, i) & 8;
        if (c) known++;
        if (!c) {
            int32_t p = SecretProjects[4];
            if (p < 0 || (uint8_t)Bases[p].faction_id != f) unknown++;
        }
    }
    PluralDefault = 0;
    GenderDefault = MFactions[fw].is_leader_female;
    parse_says(0, MFactions[fw].title_leader, -1, -1);
    parse_says(1, get_pact_hood(f, fw), -1, -1);
    parse_num(0, Factions[f].loan_balance[fw]);
    diplomacy_caption(f, fw);
    ui_diplo_menu_start("PROPOSAL", 0x100000);
    if (X_text_open(ScriptFileName, "PROPOSALMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    uint32_t s = ds(f, fw);
    const bool shown[12] = {
        true,
        true,
        (s & 2) && !(s & 1),
        !(s & 2),
        !(GameRules & 0x2000000),
        false,
        unknown != 0,
        true,
        Factions[f].loan_balance[fw] && Factions[f].energy_credits >= Factions[f].loan_balance[fw],
        false,
        known != 0,
        Factions[fw].base_count > 1 && swap_bases && !MultiplayerActive,
    };
    const int32_t value[12] = {0, 1, 2, 3, 4, 0, 13, 6, 11, 0, 8, 9};
    for (int32_t i = 0; i < 12; i++) {
        text_get();
        StrBuffer[0] = 0;
        parse_string(TextBufferGetPtr, StrBuffer);
        if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
    }
    text_get();
    StrBuffer[0] = 0;
    parse_string(TextBufferGetPtr, StrBuffer);
    DiploCurrentProposal = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (DiploCurrentProposal == 8) {
        // PROPOSEATTACK: the faction to attack.
        ui_diplo_menu_start("PROPOSEATTACK", 0x100040);
        if (X_text_open(ScriptFileName, "ATTACKSTRING")) {
            ui_diplo_menu_close();
            goto none;
        }
        faction_items(f, fw, false);
        DiploProposalFaction = ui_diplo_menu_exec(fw);
        ui_diplo_menu_close();
        if (DiploProposalFaction < 0) goto none;
        // BUG(original): the loop's counter (8) is used as the faction: diplo_status[8] of
        // faction_id_with is its diplo_agenda[0].
        if (Factions[fw].diplo_agenda[0] & 0x10) {
            int32_t p = DiploProposalFaction;
            PluralDefault = 0;
            GenderDefault = MFactions[p].is_leader_female;
            parse_says(0, MFactions[p].title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = MFactions[DiploProposalFaction].is_leader_female;
            parse_says(1, MFactions[DiploProposalFaction].name_leader, -1, -1);
            pop("REJWARALREADY", fw);
            goto none;
        }
    }
    if (DiploCurrentProposal == 13) {
        // PROPOSECOMMLINK: a commlink to share.
        ui_diplo_menu_start("PROPOSECOMMLINK", 0x100040);
        if (X_text_open(ScriptFileName, "ATTACKSTRING")) {
            ui_diplo_menu_close();
            goto none;
        }
        faction_items(f, fw, true);
        int32_t p = ui_diplo_menu_exec(fw);
        ui_diplo_menu_close();
        DiploProposalFaction = p;
        if (p < 0) goto none;
        if (!(ds(fw, p) & 8)) {
            int32_t sp = SecretProjects[4];
            if (sp < 0 || (uint8_t)Bases[sp].faction_id != fw) {
                const MFaction& m = MFactions[p];
                PluralDefault = 0;
                GenderDefault = m.is_leader_female;
                parse_says(0, m.title_leader, -1, -1);
                PluralDefault = 0;
                GenderDefault = MFactions[DiploProposalFaction].is_leader_female;
                parse_says(1, MFactions[DiploProposalFaction].name_leader, -1, -1);
                const MFaction& m2 = MFactions[DiploProposalFaction];
                PluralDefault = m2.is_noun_plural;
                GenderDefault = m2.noun_gender;
                parse_says(2, m2.noun_faction, -1, -1);
                DiploCurrentProposal = 0;
                X_pops("NOCOMMLINK", FactionPortraits[fw], kPopWait);
            }
        }
    }
    if (DiploCurrentProposal == 9) {
        DiploAskBaseSwapID = base_menu("PROPOSEBASE", fw, f, fw);
        if (DiploAskBaseSwapID < 0) {
            DiploAskBaseSwapID = -1;
            DiploCurrentProposal = 0;
        }
    }
    {
        int32_t p = DiploCurrentProposal;
        if (p == 0 || p == 7 || p == 10 || p == 11 || p == 5 || p == 12) {
            DiploCounterProposal = 1;
            return p;
        }
    }
    // COUNTER: what faction_id offers.
    PluralDefault = 0;
    GenderDefault = MFactions[fw].is_leader_female;
    parse_says(0, MFactions[fw].title_leader, -1, -1);
    GenderDefault = MFactions[fw].is_leader_female;
    PluralDefault = 0;
    parse_says(1, MFactions[fw].name_leader, -1, -1);
    parse_says(2, get_pact_hood(f, fw), -1, -1);
    diplomacy_caption(f, fw);
    {
        char label[64];
        std::snprintf(label, sizeof(label), "COUNTER%d", DiploCurrentProposal == 1 ? 1 : 0);
        ui_diplo_menu_start(label, 0x100000);
    }
    if (X_text_open(ScriptFileName, "COUNTERMENU")) {
        ui_diplo_menu_close();
        return 0;
    }
    {
        int32_t p = DiploCurrentProposal;
        const bool shown[10] = {
            true,
            p == 6,
            p != 1,
            p != 1,
            p != 1 && !(s & 1),
            p != 1 && (s & 1),
            !(GameRules & 0x2000000),
            p == 1,
            p != 6,
            Factions[f].base_count > 1 && !MultiplayerActive && (p == 9 || p == 1),
        };
        const int32_t value[10] = {0, 6, 1, 2, 3, 3, 4, 7, 5, 8};
        for (int32_t i = 0; i < 10; i++) {
            text_get();
            StrBuffer[0] = 0;
            parse_string(TextBufferGetPtr, StrBuffer);
            if (shown[i]) ui_diplo_menu_item(StrBuffer, value[i]);
        }
    }
    DiploCounterProposal = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (DiploCounterProposal == 8) {
        DiploBidBaseSwapID = base_menu("COUNTERBASE", f, -1, fw);
        if (DiploBidBaseSwapID < 0) DiploCurrentProposal = 0;
    }
    if (DiploCounterProposal == 0) DiploCurrentProposal = 0;
    return DiploCurrentProposal;
none:
    DiploCurrentProposal = 0;
    DiploCounterProposal = 1;
    return 0;
}

// make_a_proposal (0x54F420): the proposal chosen (proposal_menu, unless `direct`: the council
// topic) carried out by the engine's propose_* / trade functions.
void GAMEAPI make_a_proposal(int32_t f, int32_t fw, int32_t direct) {
    DiploSecondFaction = fw;
    DiploMenuFlagA = 0;
    if (!direct && !proposal_menu(f, fw)) return;
    DiploMenuFlagB = 1;
    bool threat = DiploCounterProposal == 3;
    int32_t p = DiploCurrentProposal;
    switch (p) {
    case 2:
        if (!propose_pact(f, fw) && threat) threaten(f, fw);
        return;
    case 3:
        if (!propose_treaty(f, fw) && threat) threaten(f, fw);
        return;
    case 7:
        DiploSecondFaction = fw;
        if ((!(ds(f, fw) & 1) && DiploFriction > 0) || DiploFriction > 4) {
            parse_says(0, MFactions[fw].adj_name_faction, -1, -1);
            pop("REJMAPS", fw);
        } else {
            pop("TRADEMAPS", fw);
            Factions[fw].diplo_patience[f] += 2;
            net_maps(f, fw, 1);
            DiploMapMsg = 1;
        }
        return;
    case 10:
        return;
    default:
        break;
    }
    if (p == 8 && (ds(fw, f) & 1) && (ds(fw, f) & 0x2000000)) {
        propose_attack(f, fw);
        return;
    }
    if (threat) {
        threaten(f, fw);
        return;
    }
    switch (p) {
    case 8:
        propose_attack(f, fw);
        return;
    case 9:
        base_swap(f, fw);
        return;
    case 4:
    case 5:
    case 13:
        DiploMenuFlagA = tech_trade(f, fw, 0, DiploCounterProposal, p);
        return;
    case 6:
        DiploMenuFlagA = energy_trade(f, fw);
        return;
    case 1:
        make_gift(f, fw);
        return;
    case 11: {
        // The loan repaid.
        const MFaction& m = MFactions[f];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        int32_t bal = Factions[f].loan_balance[fw];
        net_energy(f, -bal, fw, bal, 1);
        net_loan(f, fw, 0, 0);
        pop("REPAIDLOAN", fw);
        break;
    }
    default:
        break;
    }
    if (DiploCurrentProposal != 12) return;
    // COUNCILMATTER: a vote asked of faction_id_with.
    ui_diplo_menu_start("COUNCILMATTER", 0x100040);
    for (int32_t i = 0; i < 11; i++) {
        if (!has_tech(Proposal[i].preq_tech, f) && !((GameState & 0x80) && (GameState & 0x1000))) continue;
        if (i == 1 && !(GameRules & 8)) continue;
        bool ok = true;
        switch (i) {
        case 1:
            ok = !(GameState & 1) && !ProposalPassCount[1] && eligible(f);
            break;
        case 2:
        case 5:
            ok = !ProposalPassCount[i];
            break;
        case 3:
            ok = ProposalPassCount[3] <= ProposalPassCount[4];
            break;
        case 4:
            ok = ProposalPassCount[4] != ProposalPassCount[3];
            break;
        case 6:
            ok = ProposalPassCount[5] != 0;
            break;
        case 8:
            ok = ProposalPassCount[8] <= ProposalPassCount[9];
            break;
        case 9:
            ok = ProposalPassCount[9] < ProposalPassCount[8];
            break;
        default:
            break;
        }
        if (!ok) continue;
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[i].name));
        crt_strcat(StrBuffer, "|");
        crt_strcat(StrBuffer, Strings_get(&StringTable, Proposal[i].description));
        ui_diplo_menu_item(StrBuffer, i);
    }
    int32_t prop = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (prop < 0) return;
    if (prop != 0) {
        int32_t r = pop("COUNCILVOTE", fw);
        int32_t vote = -1 - r;
        if (vote) buy_council_vote(f, fw, prop, vote);
        return;
    }
    // COUNCILVOTEGOV: the candidate for governor.
    ui_diplo_menu_start("COUNCILVOTEGOV", 0x100000);
    for (int32_t i = 1; i < 8; i++) {
        if (!(FactionStatus[1] & (1 << i)) || !eligible(i)) continue;
        const MFaction& m = MFactions[i];
        StrBuffer[0] = 0;
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        crt_strcat(StrBuffer, m.title_leader);
        crt_strcat(StrBuffer, " ");
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        crt_strcat(StrBuffer, m.name_leader);
        ui_diplo_menu_item(StrBuffer, i);
        if (i == f) ui_diplo_menu_set_active(i);
    }
    int32_t cand = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (cand >= 0) buy_council_vote(f, fw, 0, cand);
}

// battle_plans (0x54B1C0): the BATTLEPLANS dialog: maps traded first (not between pact
// partners), each side's suggested target (suggest_plan), then a common target (BATTLEPLAN menu)
// that faction_id_with accepts (base_id_attack_target; its current target dropped when in
// another region) or not (BATTLENO).
void GAMEAPI battle_plans(int32_t f, int32_t fw) {
    DiploSecondFaction = fw;
    DiploBattlePlayer = f;
    DiploBattleRival = fw;
    int32_t tf = suggest_plan(f, fw);
    int32_t tw = suggest_plan(fw, f);
    attack_from(tf, f);
    attack_from(tw, fw);
    if (!(ds(f, fw) & 1)) {
        if ((ds(fw, f) & 0x8020) || DiploCapturedBases || DiploFriction >= 0x12 || DiploHostility) {
            pop("NEVERBATTLEPLANS", fw);
            return;
        }
        if (!DiploMapsTraded && pop("MAPBATTLEPLANS", fw) == 0) return;
        net_maps(f, fw, 1);
    }
    if (tf < 0 || tw < 0) {
        pop("NOBATTLEPLANS", fw);
        return;
    }
    Faction& W = Factions[fw];
    W.base_id_attack_target = tw;
    parse_says(0, Bases[tw].name, -1, -1);
    parse_says(1, Bases[tf].name, -1, -1);
    if (!pop("BATTLEPLANS", fw)) return;
    {
        const MFaction& m = MFactions[f];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
    }
    ui_diplo_menu_start("BATTLEPLAN", 0x100042);
    int32_t n = 0;
    for (int32_t b = 0; b < BaseCount; b++) {
        int32_t owner = (uint8_t)Bases[b].faction_id;
        if (owner == f || owner == fw) continue;
        if (!(ds(f, owner) & 0x10) || !(ds(fw, owner) & 0x10)) continue;
        uint8_t vis = Bases[b].visibility;
        if (!(vis & (1 << f)) && !(vis & (1 << fw))) continue;
        ui_diplo_menu_item(Bases[b].name, b);
        if (!n) {
            attack_from(b, DiploBattlePlayer);
            attack_from(b, DiploBattleRival);
        }
        n++;
    }
    if (!n) {
        ui_diplo_menu_close();
        return;
    }
    int32_t target = ui_diplo_menu_exec(fw);
    ui_diplo_menu_close();
    if (target < 0) return;
    BASE& t = Bases[target];
    parse_says(0, t.name, -1, -1);
    if ((uint8_t)t.faction_id != fw && !(t.visibility & (1 << fw))) spot_loc(t.x, t.y, fw);
    int32_t reg = map_tile(t.x, t.y)->region;
    int32_t reg2 = map_tile(Bases[tw].x, Bases[tw].y)->region;
    bool go = (reg < 64 && W.region_total_bases[reg]) || !W.region_total_bases[reg2];
    for (int32_t r = 0; r < 64 && !go; r++) {
        int32_t m = r & 0x3F;
        if (m != 0x3F && m && W.region_total_bases[r] && W.region_base_plan[r] == 7) go = true;
    }
    if (!go) {
        pop("BATTLENO", fw);
        return;
    }
    base_find_2(Bases[tw].x, Bases[tw].y, fw);
    int32_t d1 = BaseFindDist;
    base_find_2(t.x, t.y, fw);
    if (BaseFindDist >= d1 * 2) {
        pop("BATTLENO", fw);
        return;
    }
    W.base_id_attack_target = target;
    pop("BATTLEYES", fw);
    if (W.target_x < 0) return;
    if (map_tile(W.target_x, W.target_y)->region == map_tile(t.x, t.y)->region) return;
    for (int32_t v = 0; v < VehCount; v++) {
        if (Vehs[v].faction_id == (uint32_t)fw && (Vehs[v].state & 0x40000)) Vehs[v].state &= 0xFFF9FFFF;
    }
    W.target_y = -1;
    W.target_x = -1;
    W.unk_72 = 0;
    W.unk_71 = 0;
}
