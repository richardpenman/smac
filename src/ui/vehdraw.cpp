// Units on the map (see vehdraw.h).
#include "ui/vehdraw.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/compat.h"
#include "engine/map.h"
#include "engine/battle.h"
#include "engine/globals.h"
#include "engine/stack.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/colortables.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"
#include "ui/voxel.h"
#include "ui/x87.h"
#include "engine/text.h"
#include "engine/tech.h"

int8_t VehSpriteOffsets[19][8][8];

namespace {

Buffer g_canvas;  // poLoadingCanvas while the unit art is read

int32_t trunc_div16(int32_t v) { return (v + ((v >> 31) & 0xF)) >> 4; }
// v * (zoom + 16) / 16 (or the fixed zoom factor).
int32_t zscale(int32_t zoom, int32_t v) {
    if (ZoomNum < 1) return trunc_div16((zoom + 0x10) * v);
    return ZoomNum * v / ZoomDenom;
}

void cell(Sprite* s, int32_t x, int32_t y, int32_t w, int32_t h) {
    Buffer_change_color(&g_canvas, 0x107, 0x109);
    Buffer_change_color(&g_canvas, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(s, &g_canvas, 0x109, x, y, w, h, 0);
}

// Sprite_copy (0x5EAE4F) and Sprite_change_color (0x5EAD9F).
void sprite_copy(const Sprite* from, Sprite* to) { *to = *from; }
void sprite_change_color(Sprite* s, int32_t from, int32_t to) {
    for (uint8_t& p : s->pixels)
        if (p == (uint8_t)from) p = (uint8_t)to;
}

bool is_spartans(int32_t faction_id) { return !std::strcmp(MFactions[faction_id].search_key, "SPARTANS"); }

GLOBAL(int32_t, MapWinField23BF8, 0x9392A8);
GLOBAL(int32_t, MapWinField23C00, 0x9392B0);

}  // namespace

void GAMEAPI load_vehicles() {
    // XI.PCX: the shots of the weapons (9 rows of 16 frames, 43 x 47, 0x7A9B30..); XF.PCX: the
    // explosions (4 kinds of 16 frames, 100 x 150: the first 8 above, the last 8 below,
    // 0x7AB460..). Their pixels are levels of the explosion tables (boom.cpp); 0xFD is transparent.
    Buffer_load_pcx(&g_canvas, "XI.PCX", nullptr, 0, 0x12);
    Buffer_change_color(&g_canvas, 0xFD, 0xFF);
    for (int32_t r = 0, y = 2; r < 9; r++, y += 0x31)
        for (int32_t k = 0, x = 2; k < 16; k++, x += 0x2D)
            pfncSpriteFromCanvasRectTrans(ArtSprite(0x7A9B30u + (uint32_t)(r * 16 + k) * 0x2C), &g_canvas, 0xFF, x, y, 0x2B,
                                          0x2F, 0);
    Buffer_load_pcx(&g_canvas, "XF.PCX", nullptr, 0, 0x12);
    Buffer_change_color(&g_canvas, 0xFD, 0xFF);
    for (int32_t t = 0, y = 2; t < 4; t++, y += 0x130)
        for (int32_t c = 0, x = 2; c < 8; c++, x += 0x66) {
            uint32_t base = 0x7AB460u + (uint32_t)(t * 16) * 0x2C;
            pfncSpriteFromCanvasRectTrans(ArtSprite(base + (uint32_t)c * 0x2C), &g_canvas, 0xFF, x, y, 100, 0x96, 0);
            pfncSpriteFromCanvasRectTrans(ArtSprite(base + (uint32_t)(c + 8) * 0x2C), &g_canvas, 0xFF, x, y + 0x98, 100,
                                          0x96, 0);
        }
    // Faction 0 (native life) takes its colours from PALETTE.PCX.
    Buffer_load_pcx(&g_canvas, "PALETTE.PCX", nullptr, 10, 0xEC);
    FactionColorFill[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x17);
    FactionColorFrame[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x1F);
    FactionColorText[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x27);
    FactionColorShadow[0] = Buffer_get_pixel(&g_canvas, 0x244, 0x2F);
    Buffer_load_pcx(&g_canvas, "UNITS.PCX", nullptr, 10, 0xEC);
    // Special sprites 9..18, one row each (2 + 0x4D * (index - 9)); rows with variants: morale
    // levels (7) or kinds (3). Marker pixels left of / above each cell give the flag position
    // (colour 5) and a second point (colour 2).
    int32_t last_count = 0;
    for (int32_t idx = 9; idx < 19; idx++) {
        int32_t y = 2 + 0x4D * (idx - 9);
        int32_t count = 1;
        if (y == 0xE9 || y == 0x4F || y == 0x136 || y == 0x183) count = 7;
        else if (y == 0x1D0) count = 3;
        last_count = count;
        for (int32_t k = 0, x = 2; k < count; k++, x += 0x66) {
            int32_t cx = x, cy = y;
            if (idx == 18) {
                cx = 0x2CC;
                cy = 0xE9;
            }
            cell(ArtSprite(kSprUnits + (idx * 8 + k) * 0x2C), cx, cy, 100, 0x4B);
            int8_t(&o)[8][8] = VehSpriteOffsets[idx];
            o[0][k] = o[1][k] = o[2][k] = o[3][k] = 0;
            o[4][k] = o[5][k] = o[6][k] = o[7][k] = (int8_t)0x9D;
            for (int32_t i = 0; i < 100; i++)
                for (int32_t r = cy - 2; r <= cy - 1; r++) {
                    int32_t p = Buffer_get_pixel(&g_canvas, cx + i, r);
                    if (p == 5) o[0][k] = (int8_t)i;
                    if (p == 2) o[2][k] = (int8_t)i;
                }
            for (int32_t i = 0; i < 0x4B; i++)
                for (int32_t c = cx - 2; c <= cx - 1; c++) {
                    int32_t p = Buffer_get_pixel(&g_canvas, c, cy + i);
                    if (p == 5) o[1][k] = (int8_t)i;
                    if (p == 2) o[3][k] = (int8_t)i;
                }
        }
    }
    // Chassis 0..8: the flag markers right of the model column (x 0x394..0x3F9). The vertical
    // position is stored in the variant slot of the last special row's count (BUG(original):
    // a stale loop counter; veh_draw reads slot 0, which stays 0).
    COMPAT_ORIGINAL(flag_marker_stale_slot);
    for (int32_t c = 0; c < 9; c++) {
        int32_t y0 = 2 + 0x4D * c;
        for (int32_t i = 0; i < 100; i++)
            for (int32_t r = y0 - 1; r <= y0; r++)
                if (Buffer_get_pixel(&g_canvas, 0x396 + i, r) == 5) VehSpriteOffsets[c][0][0] = (int8_t)i;
        for (int32_t i = 0; i < 0x4B; i++)
            for (int32_t x = 0x394; x <= 0x395; x++)
                if (Buffer_get_pixel(&g_canvas, x, y0 + i + 1) == 5) VehSpriteOffsets[c][1][last_count] = (int8_t)i;
    }
    load_flags();
}

void GAMEAPI load_flags() {
    Sprite flags[14];
    Buffer_load_pcx(&g_canvas, "UNITS.PCX", nullptr, 10, 0xEC);
    for (int32_t i = 0, x = 1; i < 7; i++, x += 0x18) {
        cell(&flags[i * 2], x, 0x2BC, 0x17, 0x1E);
        cell(&flags[i * 2 + 1], x, 0x2DB, 0x17, 0x1E);
    }
    for (int32_t f = 0; f < 8; f++)
        for (int32_t j = 0; j < 14; j++) {
            Sprite* s = ArtSprite(kSprFlags + (f * 14 + j) * 0x2C);
            sprite_copy(&flags[j], s);
            sprite_change_color(s, 0x104 & 0xFF, FactionColorFill[f]);
            sprite_change_color(s, 0x103 & 0xFF, FactionColorFrame[f]);
        }
    // Factions with their own flag (MFactions + 0x508 bit 0x80) use FLAGS.PCX.
    Buffer_load_pcx(&g_canvas, "FLAGS.PCX", nullptr, 10, 0xEC);
    for (int32_t i = 0, x = 1; i < 7; i++, x += 0x18) {
        cell(&flags[i * 2], x, 1, 0x17, 0x1E);
        cell(&flags[i * 2 + 1], x, 0x20, 0x17, 0x1E);
    }
    for (int32_t f = 0; f < 8; f++) {
        if (!(reinterpret_cast<const uint8_t*>(&MFactions[f])[0x508] & 0x80)) continue;
        for (int32_t j = 0; j < 14; j++) {
            Sprite* s = ArtSprite(kSprFlags + (f * 14 + j) * 0x2C);
            sprite_copy(&flags[j], s);
            sprite_change_color(s, 0x104 & 0xFF, FactionColorFill[f]);
            sprite_change_color(s, 0x103 & 0xFF, FactionColorFrame[f]);
        }
    }
}

int32_t GAMEAPI pick_top_veh(int32_t veh_id) {
    int32_t best = -99;
    if (veh_id < 0) return veh_id;
    int32_t v = veh_id;
    while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
    for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
        if (VehMovingID != -1 && v == VehMovingID) continue;
        if (v == MapWinVehA && Vehs[v].faction_id == CurrentPlayerFaction && !(Vehs[v].state & 0x4200) &&
            MapWinInVehMode && MapWinField23BE4)
            return v;
        int32_t value = Chassis[Units[Vehs[v].unit_id].chassis_id].triad +
                        (Vehs[v].faction_id != CurrentPlayerFaction ? 0 : 4);
        if (best < value) {
            best = value;
            veh_id = v;
        }
    }
    return veh_id;
}

// ---- unit models ----------------------------------------------------------------------------------

namespace {

GLOBAL(float[3], VehLightDir, 0x687AD8);    // VEH_LIGHT_DIR_MAP, normalised
GLOBAL(float[3], VehModelOffset, 0x687AE8);  // offset of the infantry parts (-400, 200, 0)
GLOBAL(float, CameraYaw, 0x8E9F40);          // compute_camera
GLOBAL(int32_t, VehAnimFrame, 0x8E9F50);     // animation frame of the animated parts

float fbits(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
float* fptr(uint32_t addr) { return reinterpret_cast<float*>(mem::host(addr)); }
const uint8_t* table(uint32_t addr) { return mem::host(addr); }
uint32_t faction_colors(int32_t f) { return 0x79A7EC + (uint32_t)f * 0x1800; }
bool alien(int32_t f) { return reinterpret_cast<const uint8_t*>(&MFactions[f])[0x508] & 0x80; }
int32_t frame15() { return VehAnimFrame % 15; }

void add(VoxelScene* sc, uint32_t h, int32_t frame = 0) { Scene_add(sc, caviar_handle(h), frame); }
void addc(VoxelScene* sc, uint32_t h, uint32_t colors, int32_t frame = 0) {
    Scene_add_colors(sc, caviar_handle(h), table(colors), frame);
}
void addo(VoxelScene* sc, uint32_t h, uint32_t offset, int32_t frame) {
    Scene_add_offset(sc, caviar_handle(h), fptr(offset), frame);
}
void addco(VoxelScene* sc, uint32_t h, uint32_t colors, uint32_t offset, int32_t frame) {
    Scene_add_colors_offset(sc, caviar_handle(h), table(colors), fptr(offset), frame);
}
// add with the colour table when there is one.
void add_or_c(VoxelScene* sc, uint32_t h, uint32_t colors) {
    if (colors) addc(sc, h, colors);
    else add(sc, h);
}

// The design workshop's window (0x71F2B0) is visible (pfncWinIsVisible): never on the map.
bool design_window_visible() { return false; }

// The models of load_voxels: handle address (0: a temporary handle, whose first object's motion
// from frame 0 to frame 1 is stored at `vec`), file.
struct VoxelFile {
    uint32_t addr;
    const char* file;
};
const VoxelFile kVoxelFiles[] = {
        {0x7ACCA0, "ssf.cvr"},
        {0x79A6A0, "sdp.cvr"},
        {0x787E48, "sas.cvr"},
        {0x799228, "scd.cvr"},
        {0x7ACC90, "scj.cvr"},
        {0x77A578, "sags.cvr"},
        {0x79A6B0, "sft.cvr"},
        {0x7765B0, "vhr.cvr"},
        {0x78A5E0, "sht.cvr"},
        {0x78A300, "srb.cvr"},
        {0x7AC030, "asas.cvr"},
        {0x7ACCC0, "reslaser.cvr"},
        {0x7A7900, "resbolt.cvr"},
        {0x7ACC10, "funload.cvr"},
        {0x788028, "tecload.cvr"},
        {0x7AD130, "SP-SOPORIFIC.cvr"},
        {0x799E38, "SP-DISSWAVE.cvr"},
        {0x7792A0, "SP-MARINED.cvr"},
        {0x75AEF0, "SP-NANOO.cvr"},
        {0x7AD040, "AA01.cvr"},
        {0x75AFC0, "AA-Rover.cvr"},
        {0x779288, "AA-WING0.cvr"},
        {0x779294, "AA-WING.cvr"},
        {0x78A610, "ACP00.cvr"},
        {0x78A61C, "ACP01.cvr"},
        {0x78A628, "ACP02.cvr"},
        {0x78A634, "ACP03.cvr"},
        {0x789A50, "AA.cvr"},
        {0x78A510, "SSFA.CVR"},
        {0x79A640, "SFTA.CVR"},
        {0x7871C0, "vw00.cvr"},
        {0x79A700, "viptawal.cvr"},
        {0x7AB3F0, "viptasgn.cvr"},
        {0x7795A0, "viptapsi.cvr"},
        {0x799628, "viptr00.cvr"},
        {0x799634, "viptr01.cvr"},
        {0x799640, "viptr02.cvr"},
        {0x79964C, "viptr03.cvr"},
        {0x7791D0, "vipta00.cvr"},
        {0x7791DC, "vipta01.cvr"},
        {0x78A500, "ptmod.cvr"},
        {0x7592D0, "vpt.cvr"},
        {0x75AF00, "a.cvr"},
        {0x78A490, "apwall.cvr"},
        {0x7ACCB0, "apsid.cvr"},
        {0x7AD120, "asgen.cvr"},
        {0x779230, "VB.CVR"},
        {0x78E968, "VBP.CVR"},
        {0x75AED0, "VGMC.CVR"},
        {0x76EBE0, "VGMCP.CVR"},
        {0x75B848, "VLIGHTS.CVR"},
        {0x7AD0E0, "VA01.cvr"},
        {0x76E560, "VR00.cvr"},
        {0x76E56C, "VR01.cvr"},
        {0x76E578, "VR02.cvr"},
        {0x76E584, "VR03.cvr"},
        {0x779560, "VHR.CVR"},
        {0x75ADB8, "VRCP00.cvr"},
        {0x75ADC4, "VRCP01.cvr"},
        {0x75ADD0, "VRCP02.cvr"},
        {0x75ADDC, "VRCP03.cvr"},
        {0x78B740, "VI.CVR"},
        {0x77A5B8, "VGMT.CVR"},
        {0x78B720, "VGMTP.CVR"},
        {0x787100, "VW01.CVR"},
        {0x78710C, "VW02.CVR"},
        {0x787118, "VW03.CVR"},
        {0x787124, "VW04.CVR"},
        {0x787130, "VW05.CVR"},
        {0x78713C, "VW06.CVR"},
        {0x787148, "VW07.CVR"},
        {0x787154, "VW08.CVR"},
        {0x787160, "VW09.CVR"},
        {0x78716C, "VW10.CVR"},
        {0x787178, "VW11.CVR"},
        {0x787184, "VW12.CVR"},
        {0x78719C, "VW14.CVR"},
        {0x7871A8, "VW12.CVR"},
        {0x79A6F0, "AX.CVR"},
        {0x78A600, "ACOLPOD.CVR"},
        {0x787E60, "AT.CVR"},
        {0x7880C0, "VTA.CVR"},
        {0x7ABFC0, "ATp.CVR"},
        {0, "AVGMTGUN.CVR"},
        {0x7ACD60, "drop.cvr"},
        {0x76EBD0, "droplet.cvr"},
        {0, "adroper.cvr"},
        {0x7796A0, "vwntu.cvr"},
        {0x75AD78, "vt.cvr"},
        {0x7AD170, "VCL.cvr"},
        {0x789B58, "VCLT00.CVR"},
        {0x7A6820, "VSP.cvr"},
        {0x779270, "VSPA01.CVR"},
        {0x77927C, "VSPA02.CVR"},
        {0x7A7290, "vsptf.cvr"},
        {0x7A7280, "vsptb.cvr"},
        {0x779608, "vht-vbp.cvr"},
        {0x78A640, "vhtp.cvr"},
        {0x779648, "vhta00.cvr"},
        {0x779654, "vhta01.cvr"},
        {0x759310, "vhttp.cvr"},
        {0x78B750, "vhtta00.cvr"},
        {0x78B75C, "vhtta01.cvr"},
        {0, "AVHT-GunReposition.cvr"},
        {0, "AVHT-LightsReposition.cvr"},
        {0x7ACC50, "VFL.cvr"},
        {0x7A7240, "VGS.cvr"},
        {0x798BF0, "VGSP.CVR"},
        {0x7795B0, "VJTP.CVR"},
        {0x78A2B8, "VJT00.cvr"},
        {0x78A2C4, "VJT01.cvr"},
        {0x75AEE0, "VCU.cvr"},
        {0x75B440, "VCUP.CVR"},
        {0x78CAB8, "VCUA00.cvr"},
        {0x78CAC4, "VCUA01.cvr"},
        {0x75B088, "VCUW.CVR"},
        {0x79A680, "vct.cvr"},
        {0x7ACFA0, "VCTP.CVR"},
        {0x79A628, "vct00.cvr"},
        {0x79A634, "vct01.cvr"},
        {0x76E890, "VCTB.CVR"},
        {0x78B730, "VWNTT.CVR"},
        {0x78DE20, "VWNST.CVR"},
        {0x7AC1A0, "VWNAA.CVR"},
        {0x76E9B0, "vpbr00.cvr"},
        {0x76E9BC, "vpbr01.cvr"},
        {0x76E9C8, "vpbr02.cvr"},
        {0x76E9D4, "vpbr03.cvr"},
        {0x791D78, "VM.CVR"},
        {0x78A5F0, "vw13.cvr"},
        {0x79A690, "nw.CVR"},
        {0x7791C0, "ni.CVR"},
        {0x779660, "nlc.cvr"},
};

// Recolours VBP.CVR's shading table with a base colour (colortables_init, 0x423F55).
void recolor(uint32_t out, int32_t color) {
    const CaviarHandle* h = caviar_handle(0x78E968);
    const uint8_t* shading = h->data ? h->data->shading() : nullptr;
    if (!shading) return;
    const uint8_t* base = GamePalette.rgb[color & 0xFF];
    uint8_t* dst = mem::host(out);
    for (int32_t i = 0; i < 0x1800; i++) {
        const uint8_t* p = GamePalette.rgb[shading[i]];
        // FP: base * palette (exact) * (1/255 as float), __ftol.
        const int32_t r = x87_ftol((double)(base[0] * p[0]) * (double)fbits(0x3B808081));
        const int32_t g = x87_ftol((double)(base[1] * p[1]) * (double)fbits(0x3B808081));
        const int32_t b = x87_ftol((double)(base[2] * p[2]) * (double)fbits(0x3B808081));
        dst[i] = (uint8_t)Palette_get_nearest_palette_index(&GamePalette, r & 0xFF, g & 0xFF, b & 0xFF, 0);
    }
}

}  // namespace

void vehdraw_color_tables_init() {
    int32_t vehicle[8] = {0x60, 0x69, 0x9C, 0, 0x55, 0x2F, 0x59, 0xE3};
    int32_t armor[4] = {0, 0, 0, 0};
    int32_t trans_color = 0, trans_from = 0x32, trans_to = 0x64;
    if (!text_open("system", "VEHICLECOLORING"))
        for (int32_t& v : vehicle) {
            text_get();
            v = (text_item_number() + 10) & 0xFF;
        }
    if (!text_open("system", "ARMORCOLORING"))
        for (int32_t& v : armor) {
            text_get();
            v = (text_item_number() + 10) & 0xFF;
        }
    if (!text_open("system", "ARMORCOLORING_TRANS")) {
        text_get();
        trans_color = (text_item_number() + 10) & 0xFF;
        trans_from = text_item_number();
        trans_to = text_item_number();
    }
    for (int32_t f = 0; f < 8; f++) recolor(faction_colors(f), vehicle[f]);
    for (int32_t a = 0; a < 4; a++) recolor(0x792664 + (uint32_t)a * 0x1800, armor[a]);
    // The light level table: row r all r.
    for (int32_t r = 0; r < 24; r++) std::memset(mem::host(0x788250 + (uint32_t)r * 0x100), r, 0x100);
    // The shield of armour 5: per light level a blend towards the shield colour.
    for (int32_t i = 0, n = 0x18; n > 0; i++, n--) {
        const int32_t pct = (n * trans_from + i * trans_to) / 24;
        Palette_create_table_from_color(&GamePalette, trans_color, mem::host(0x6EEF7C + (uint32_t)i * 0x100), 10, 0xEC,
                                        100 - pct, pct);
    }
}

void vehdraw_faction_colors(int32_t faction_id, int32_t color) { recolor(faction_colors(faction_id), color); }

void GAMEAPI load_voxels() {
    voxel_renderer_init();
    Scene_ctor(&VehScene);
    CaviarHandle temp;
    CaviarHandle_ctor(&temp);
    Scene_init(&VehScene);
    vehdraw_compute_facet_normals();
    static const uint32_t kTempVec[] = {0x7AC1B0, 0x79A618, 0x798C30, 0x789B48};
    int32_t temps = 0;
    for (const VoxelFile& v : kVoxelFiles) {
        if (v.addr) {
            CaviarHandle_load(caviar_handle(v.addr), v.file);
            continue;
        }
        CaviarHandle_load(&temp, v.file);
        float* out = fptr(kTempVec[temps]);
        if (!temp.data) {
            out[0] = out[1] = out[2] = 0.0f;
        } else if (temp.data->frame_count == 2 && !temp.data->frames.empty() && temp.data->frames[0].pos.size() >= 6) {
            const float* p = temp.data->frames[0].pos.data();
            vec3_sub(p + 3, p, out);
        }
        // (AVHT-GunReposition stays loaded until the next file replaces it.)
        if (temps != 2) CaviarHandle_free(&temp);
        temps++;
    }
    if (!text_open("system", "VEH_LIGHT_DIR_MAP")) {
        text_get();
        VehLightDir[0] = (float)text_item_number();
        VehLightDir[1] = (float)text_item_number();
        VehLightDir[2] = (float)text_item_number();
        vec3_normalize(VehLightDir, VehLightDir);
    }
    // 0x7A81A0: 24 rows, row r all r (the fixed colour table of the renderer's 0x10 mode).
    for (int32_t r = 0; r < 24; r++) std::memset(mem::host(0x7A81A0 + (uint32_t)r * 0x100), r, 0x100);
    CaviarHandle_free(&temp);
}

void GAMEAPI vehdraw_compute_facet_normals() {
    // FP: the corner heights as floats; cross products and normalisation (voxel.cpp).
    const int32_t base = *reinterpret_cast<int32_t*>(mem::host(0x93341C));
    float* t0 = fptr(0x8CDD10);
    const uint32_t stride = 0x1C23;  // floats per table
    uint32_t k = 0;
    for (int32_t a = 0x1E; a < 0xF0; a += 0x1E) {
        const float h0 = (float)((a >> 2) - base);
        for (int32_t b = 0x1E; b < 0xF0; b += 0x1E) {
            const float h1 = (float)((b >> 2) - base);
            for (int32_t c = 0x1E; c < 0xF0; c += 0x1E) {
                const float h2 = (float)((c >> 2) - base);
                for (int32_t d = 0x1E; d < 0xF0; d += 0x1E, k += 3) {
                    const float h3 = (float)((d >> 2) - base);
                    const float va[3] = {-50.0f, 0.0f, h0}, vb[3] = {0.0f, -50.0f, h3};
                    const float vc[3] = {50.0f, 0.0f, h2}, vd[3] = {0.0f, 50.0f, h1};
                    float n0[3], n1[3], n2[3], n3[3];
                    vec3_cross(vd, va, n0);
                    vec3_cross(vc, vd, n1);
                    vec3_cross(vb, vc, n2);
                    vec3_cross(va, vb, n3);
                    vec3_normalize(n0, n0);
                    vec3_normalize(n1, n1);
                    vec3_normalize(n2, n2);
                    vec3_normalize(n3, n3);
                    std::memcpy(t0 + k, n0, 12);
                    std::memcpy(t0 + stride + k, n1, 12);
                    std::memcpy(t0 + 2 * stride + k, n2, 12);
                    std::memcpy(t0 + 3 * stride + k, n3, 12);
                }
            }
        }
    }
}

void GAMEAPI compute_camera(const float* up, float* out) {
    float a[3] = {0.0f, 0.0f, fbits(0xBF85F92D)};  // -1.0466667
    float r1[9], r2[9], v[3];
    mat3_from_euler(a, r1);
    mat3_apply(r1, up, v);
    // FP: atan of a quotient computed in a register (double), the CRT atan's 64-bit result
    // stored as float.
    float yaw;
    if (v[0] == 0.0f) yaw = 0.0f;
    else if (v[1] == 0.0f) yaw = fbits(0x3FC8F5C3);  // 1.57
    else yaw = (float)crt_atan((double)v[0] / v[1]);
    CameraYaw = yaw;
    a[0] = yaw;
    a[1] = 0.0f;
    a[2] = 0.0f;
    mat3_from_euler(a, r1);
    const float t[3] = {v[0], v[1], 0.0f};
    const double len = vec3_length(t);
    const float lenf = (float)len;
    float pitch;
    if (len == 0.0) pitch = 0.0f;
    else if (v[2] == 0.0f) pitch = fbits(0x3FC8F5C3);
    else pitch = (float)crt_atan((double)lenf / v[2]);
    a[0] = 0.0f;
    a[1] = 0.0f;
    a[2] = pitch;
    mat3_from_euler(a, r2);
    mat3_mul(r1, r2, out);
    out[6] = -out[6];
    out[7] = -out[7];
    out[8] = -out[8];
}

void GAMEAPI vehdraw_construct_chassis(int32_t f, int32_t chassis, int32_t weapon, int32_t armor, int32_t reactor,
                                       VoxelScene* sc) {
    if (f < 0 || f > 7) f = 0;
    const uint32_t fcol = faction_colors(f);
    // The armour: plated (1) or not (0, 2: special), its colour table.
    int32_t plated = 0;
    uint32_t acol = 0;
    switch (armor) {
    case 0: case 9: break;
    case 1: plated = 1; break;
    case 2: plated = 1; acol = 0x792664; break;
    case 3: plated = 1; acol = 0x793E64; break;
    case 4: case 8: plated = 2; break;
    case 5: plated = 1; break;
    case 6: plated = 1; acol = 0x795664; break;
    case 7: plated = 1; acol = 0x796E64; break;
    case 10: plated = 1; acol = 0x792664; add(sc, 0x7ACC90); break;
    case 11: plated = 1; acol = 0x792664; add(sc, 0x78A5E0); break;
    case 12: plated = 1; acol = 0x795664; add(sc, 0x7ACC90); break;
    case 13: plated = 1; acol = 0x795664; add(sc, 0x78A5E0); break;
    default: break;
    }
    if (weapon == 0x15 && chassis == 1) {
        if (Armor[armor].defense_value <= 1) chassis = 0;
    } else if ((uint32_t)chassis > 8) {
        return;
    }
    // The reactor (by armour colour for the alien factions).
    auto reactor_part = [&]() {
        if (!reactor) return;
        if (alien(f)) add_or_c(sc, 0x78A604 + (uint32_t)reactor * 0xC, acol);
        else add(sc, 0x75ADAC + (uint32_t)reactor * 0xC);
    };
    switch (chassis) {
    case 0: {  // infantry
        const uint8_t mode = Weapon[weapon].mode;
        if (mode < 3) {
            if (weapon == 0x11) return;
            addo(sc, alien(f) ? 0x79A6F0 : 0x78B740, 0x687AE8, frame15());
            return;
        }
        if (weapon == 0x15) {
            if (alien(f)) addco(sc, 0x79A6F0, 0x796E64, 0x687AE8, frame15());
            else addo(sc, 0x7592D0, 0x687AE8, frame15());
            return;
        }
        if (weapon == 0x16) return;
        if (alien(f) && weapon == 0x11) return;
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x7AD170);
        add(sc, 0x789B58);
        add(sc, 0x75B848);
        return;
    }
    case 1:  // speeder
        reactor_part();
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x7A6820);
        add(sc, 0x7A7280, VehAnimFrame);
        add(sc, 0x7A7290, VehAnimFrame);
        add(sc, 0x75B848);
        return;
    case 2:  // hovertank
        add(sc, 0x779230);
        addc(sc, 0x779608, fcol);
        addc(sc, 0x78A640, fcol);
        addc(sc, 0x759310, fcol);
        if (!plated) {
            add(sc, 0x779648);
            add(sc, 0x78B750);
        }
        addo(sc, 0x75B848, 0x789B48, 0);
        return;
    case 3:  // foil
        reactor_part();
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x7ACC50);
        return;
    case 4:  // cruiser
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        add(sc, 0x75AEE0);
        addc(sc, 0x75B440, fcol);
        add(sc, 0x75B088);
        if (!plated) add(sc, 0x78CAB8);
        return;
    case 5:  // needlejet
        reactor_part();
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        addc(sc, 0x7795B0, fcol);
        if (plated) return;
        add(sc, alien(f) ? 0x779288 : 0x78A2B8);
        return;
    case 6:  // copter
        reactor_part();
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        add(sc, 0x79A680);
        addc(sc, 0x7ACFA0, fcol);
        add(sc, 0x76E890, frame15());
        if (design_window_visible()) VehAnimFrame++;
        if (!plated) addc(sc, 0x79A628, fcol);
        return;
    case 7:  // gravship
        reactor_part();
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        add(sc, 0x75AED0);
        addc(sc, 0x76EBE0, fcol);
        add(sc, 0x7A7240);
        addc(sc, 0x798BF0, fcol);
        return;
    case 8:  // missile
        if (weapon == 0x10) {
            add(sc, 0x78A5F0);
        } else if (weapon == 0x18) {
            add(sc, 0x791D78);
            add(sc, 0x788028);
        } else if (weapon == 0x19) {
            add(sc, 0x791D78);
            add(sc, 0x7ACC10);
        } else {
            add(sc, 0x791D78);
        }
        add(sc, 0x779230);
        addc(sc, 0x78E968, fcol);
        return;
    }
}

void GAMEAPI vehdraw_construct_weapon(int32_t f, int32_t chassis, int32_t weapon, VoxelScene* sc) {
    // Model by offense value (-1: none).
    static const int32_t kGun[26] = {-1, -1, 0, 0, 1, 3, 2, 2, 4, 4, 5, 5, 6, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 10, 13};
    if (chassis == 8) return;
    if (weapon == 0x10) return add(sc, 0x78A5F0);
    if (weapon == 0x17) return add(sc, 0x791D78);
    if (weapon == 0x18) {
        add(sc, 0x791D78);
        return add(sc, 0x788028);
    }
    if (weapon == 0x19) {
        add(sc, 0x791D78);
        return add(sc, 0x7ACC10);
    }
    if (f < 0 || f > 7) f = 0;
    const int32_t off = Weapon[weapon].offense_value;
    const uint8_t mode = Weapon[weapon].mode;
    const uint32_t fcol = faction_colors(f);
    if (mode < 3) {
        int32_t k;
        if (off == -1) k = 0xB;
        else if (off == 0x63) k = 0xC;
        else k = kGun[off < 0 ? 0 : (off > 0x19 ? 0x19 : off)];
        if (k != -1) {
            const uint32_t h = weapon == 0xC ? 0x7ACCC0 : 0x787100 + (uint32_t)k * 0xC;
            if (chassis == 0) {
                addo(sc, h, 0x7AC1B0, 0);
                add(sc, 0x77A5B8);
                addc(sc, 0x78B720, fcol);
            } else if (chassis == 2) {
                addo(sc, h, 0x798C30, 0);
            } else {
                add(sc, h);
            }
        }
    } else {
        switch (mode) {
        case 7: add(sc, 0x78B730); break;
        case 8:
            if (chassis == 0) {
                if (alien(f)) add(sc, 0x78A600, frame15());
                else {
                    add(sc, 0x7ACD60);
                    add(sc, 0x76EBD0);
                }
            } else {
                addo(sc, 0x76EBD0, 0x79A618, 0);
            }
            break;
        case 9:
            if (chassis == 0) add(sc, alien(f) ? 0x7880C0 : 0x75AD78);
            if (alien(f)) {
                add(sc, 0x787E60);
                add(sc, 0x7ABFC0);
            } else {
                add(sc, 0x7796A0);
            }
            break;
        case 10: add(sc, 0x78DE20); break;
        case 11:
            if (chassis != 0) add(sc, 0x78A500);
            break;
        case 12: add(sc, 0x7AC1A0); break;
        default: break;
        }
    }
    if (weapon != 0xD) return;
    if (chassis == 0) addo(sc, 0x7A7900, 0x7AC1B0, 0);
    else if (chassis == 2) addo(sc, 0x7A7900, 0x798C30, 0);
    else add(sc, 0x7A7900);
}

void GAMEAPI vehdraw_construct_abil(uint32_t abil, VoxelScene* sc, int32_t f) {
    auto models = [&](uint32_t bits) {
        if (bits & 0x1000000) add(sc, 0x7AD130);
        if (bits & 0x2000000) add(sc, 0x799E38);
        if (bits & 0x8000000) add(sc, 0x75AEF0);
        if (bits & 0x4000000) add(sc, 0x7792A0);
        if (alien(f)) {
            if (bits & 1) add(sc, 0x78A510);
            if (bits & 0x2000) add(sc, 0x79A640);
        } else {
            if (bits & 1) add(sc, 0x7ACCA0);
            if (bits & 0x2000) add(sc, 0x79A6B0);
        }
        if (bits & 0x20) add(sc, 0x787E48);
        if (bits & 0x80) add(sc, 0x799228);
        if (bits & 0x400) add(sc, 0x77A578);
        if (bits & 0x8000) add(sc, 0x7765B0);
        if (bits & 0x200000) add(sc, 0x78A300);
        if (bits & 0x10) add(sc, 0x79A6A0);
        if (bits & 0x200) add(sc, 0x7ACC90);
        if (bits & 0x40000) add(sc, 0x78A5E0);
    };
    // BUG(original): the faction's free abilities (bonus FREEABIL) the unit has are drawn twice.
    COMPAT_ORIGINAL(free_abilities_twice);
    const MFaction& m = MFactions[f];
    for (int32_t k = 0; k < m.faction_bonus_count; k++) {
        if (m.faction_bonus_id[k] != 0xF) continue;
        const int32_t ab = m.faction_bonus_val1[k];
        if (!has_tech(Ability[ab].preq_tech, f)) continue;
        models((1u << (ab & 0x1F)) & abil);
    }
    models(abil);
}

void GAMEAPI vehdraw_construct_armor(int32_t f, int32_t chassis, int32_t weapon, int32_t armor, int32_t overlay,
                                     VoxelScene* sc) {
    int32_t plated = 0, done = 0;
    uint32_t acol = 0;
    // Armour with its own infantry model (4, 8, 9) or none outside infantry.
    auto special = [&](uint32_t infantry, uint32_t other) {
        done = 1;
        const uint8_t mode = Weapon[weapon].mode;
        if (chassis == 0 && (mode < 3 || mode == 0xB)) {
            addo(sc, infantry, 0x687AE8, VehAnimFrame);
        } else {
            add(sc, 0x75AF00);
            add(sc, other);
        }
    };
    switch (armor) {
    case 1: plated = 1; break;
    case 2: case 10: case 11: plated = 1; acol = 0x792664; break;
    case 3: plated = 1; acol = 0x793E64; break;
    case 4: special(0x79A700, 0x78A490); break;
    case 5:
        if (!overlay) done = 1;
        else {
            plated = 1;
            acol = 0x788250;
        }
        break;
    case 6: case 12: case 13: plated = 1; acol = 0x795664; break;
    case 7: plated = 1; acol = 0x796E64; break;
    case 8: special(0x7AB3F0, 0x7AD120); break;
    case 9: special(0x7795A0, 0x7ACCB0); break;
    default: break;
    }
    const bool al = alien(f);
    // The generic plate: 0x7AD040 (alien) / 0x7AD0E0.
    auto plate = [&]() { add_or_c(sc, al ? 0x7AD040 : 0x7AD0E0, acol); };
    switch (chassis) {
    case -1:
        if (!plated || done) return;
        plate();
        return;
    case 0: {
        if (done || !plated) return;
        const bool gun = weapon != -1 && (Weapon[weapon].mode < 3 || Weapon[weapon].mode == 0xB);
        if (al) {
            if (!gun) return plate();
            if (acol) addco(sc, 0x789A50, acol, 0x687AE8, frame15());
            else addo(sc, 0x789A50, 0x687AE8, frame15());
        } else {
            if (!gun) return add_or_c(sc, 0x7AD0E0, acol);
            const uint32_t h = 0x7791C4 + (uint32_t)plated * 0xC;
            if (acol) addco(sc, h, acol, 0x687AE8, frame15());
            else addo(sc, h, 0x687AE8, frame15());
        }
        return;
    }
    case 1:
        if (done || !plated) return;
        if (al) {
            add_or_c(sc, 0x7AD040, acol);
            add_or_c(sc, 0x75AFC0, acol);
        } else {
            add_or_c(sc, 0x7AD0E0, acol);
            add_or_c(sc, 0x779264 + (uint32_t)plated * 0xC, acol);
        }
        return;
    case 2:
        if (done) {
            add(sc, 0x779648);
            add(sc, 0x78B750);
            return;
        }
        if (!plated) return;
        add_or_c(sc, 0x779654, acol);
        add_or_c(sc, 0x78B75C, acol);
        return;
    case 3:
        if (done || !plated) return;
        plate();
        return;
    case 4:
        if (done) {
            if (armor == 5) return;
            add(sc, 0x78CAB8);
            return;
        }
        if (!plated) return;
        add_or_c(sc, 0x78CAC4, acol);
        return;
    case 5:
        if (done) {
            if (armor == 5) return;
            add(sc, al ? 0x779288 : 0x78A2B8);
            return;
        }
        if (!plated) return;
        if (al) {
            add_or_c(sc, 0x7AD040, acol);
            add_or_c(sc, 0x779294, acol);
        } else {
            add_or_c(sc, 0x7AD0E0, acol);
            add_or_c(sc, 0x78A2C4, acol);
        }
        return;
    case 6:
        if (done) {
            if (armor == 5) return;
            add(sc, 0x79A628);
            add(sc, al ? 0x7AD040 : 0x7AD0E0);
            return;
        }
        if (!plated) return;
        add_or_c(sc, 0x79A634, acol);
        add_or_c(sc, al ? 0x7AD040 : 0x7AD0E0, acol);
        return;
    case 7:
        if (!plated) return;
        plate();
        return;
    default:
        return;
    }
}

void GAMEAPI vehdraw_construct_vehicle(int32_t veh_id, VoxelScene* sc) {
    sc->count = 0;
    const VEH& veh = Vehs[veh_id];
    const UNIT& u = Units[veh.unit_id];
    // Infantry with small arms (and the probe team...) are drawn larger, others smaller.
    // FP: the scale times a float constant, stored as float.
    if (u.chassis_id == 0) {
        if (u.weapon_id < 3 || u.weapon_id == 0x15)
            Scene_set_scale(sc, (float)((double)Scene_get_scale(sc) * 1.25f));
        else
            Scene_set_scale(sc, (float)((double)Scene_get_scale(sc) * fbits(0x3F4CCCCD)));  // 0.8
    } else if (u.chassis_id == 1 && u.weapon_id == 0x15) {
        Scene_set_scale(sc, (float)((double)Scene_get_scale(sc) * 1.25f));
    }
    int32_t morale = veh.faction_id ? morale_veh(veh_id, 0, 0) : morale_alien(veh_id, CurrentPlayerFaction);
    // Native life: scaled by morale, animated.
    auto native = [&](int32_t lo_add, int32_t hi, uint32_t h, int32_t frame) {
        if (morale < 0) morale = 0;
        else if (morale > hi) morale = hi;
        Scene_set_scale(sc, (float)((double)Scene_get_scale(sc) * (morale + lo_add) * 0.125f));
        add(sc, h, frame);
    };
    switch (veh.unit_id) {
    case 8: return native(0xC, 6, 0x79A690, VehAnimFrame % 5 + 0x1E);
    case 9: return native(6, 6, 0x7791C0, VehAnimFrame % 5 + 0x1E);
    case 10: return native(5, 7, 0x779660, VehAnimFrame);
    }
    const uint32_t abil = u.ability_flags;
    const int32_t reactor = u.reactor_id, weapon = u.weapon_id, armor = u.armor_id;
    int32_t chassis = u.chassis_id;
    const int32_t f = veh.faction_id;
    sc->count = 0;
    if (weapon == 0x15 && chassis == 1 && Armor[armor].defense_value <= 1) chassis = 0;
    vehdraw_construct_chassis(f, chassis, weapon, armor, reactor, sc);
    vehdraw_construct_weapon(f, chassis, weapon, sc);
    vehdraw_construct_abil(abil, sc, f);
    vehdraw_construct_armor(f, chassis, weapon, armor, 0, sc);
    vehdraw_construct_reactor(f, chassis, weapon, reactor, abil, sc);
}

void GAMEAPI vehdraw_construct_reactor(int32_t, int32_t chassis, int32_t weapon, int32_t reactor, uint32_t abil,
                                       VoxelScene* sc) {
    if (!reactor) return;
    if (chassis == 0) {
        const uint8_t mode = Weapon[weapon].mode;
        if (mode < 3 || mode == 0xB) {
            addo(sc, 0x79961C + (uint32_t)reactor * 0xC, 0x687AE8, 0);
            return;
        }
    } else if (chassis == 8) {
        add(sc, 0x76E9A4 + (uint32_t)reactor * 0xC);
        return;
    }
    add(sc, 0x76E554 + (uint32_t)reactor * 0xC);
    if (abil & 0x8000) add(sc, 0x779560);
}

namespace {

// The unit's tilt on its tile (vehdraw_draw's a7, vehdraw_turn_start / vehdraw_scoot_start): the
// sum of the tile's four facet normals when the tile is rolling or rocky and the unit is not
// infantry with a hand weapon; else straight up.
void tile_up(int32_t veh_id, int32_t x, int32_t y, bool tilt, float* up) {
    up[0] = 0.0f;
    up[1] = 0.0f;
    up[2] = 1.0f;
    const UNIT& u = Units[Vehs[veh_id].unit_id];
    if (!tilt || (MapTiles[MapHalfX * y + (x >> 1)].climate & 0xE0) < 0x60 || (u.chassis_id == 0 && Weapon[u.weapon_id].mode < 3))
        return;
    uint8_t* win = &MainConsole;
    int32_t r[4];
    for (int32_t c = 1; c <= 4; c++) {
        pfncMapGetCornerYOffset(win, x, y, c);
        const uint8_t* e = ui_mapwin_cache_entry(win, x, y);
        r[c - 1] = e ? ((e[10] | e[11] << 8) >> ((c - 1) * 4)) & 0xF : 0;
    }
    const int32_t k = r[3] + ((r[0] * 7 + r[1]) * 7 + r[2]) * 7 - 400;
    const float* t = fptr(0x8CDD10) + k * 3;
    const uint32_t stride = 0x1C23;
    vec3_add(t, t + stride, up);
    vec3_add(up, t + 2 * stride, up);
    vec3_add(up, t + 3 * stride, up);
    vec3_normalize(up, up);
}

// The model of veh_id at (px, py) with the camera tilted by `up`, turned by `angle` (radians,
// before the camera's yaw); the shield of armour 5 over it (vehdraw_draw, vehdraw_turn,
// vehdraw_scoot).
// `angle`: the facing in the x87 register; less the camera's yaw (CameraYaw) once the camera is
// computed when `less_yaw` (vehdraw_draw, vehdraw_scoot: compute_camera runs before the
// rotation), else already final (vehdraw_turn, which keeps the last camera).
void render_vehicle(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t mode, int32_t zoom, const float* up_in,
                    double angle, bool less_yaw) {
    const float light[3] = {fbits(0xBF13CD36), fbits(0x3F13CD36), fbits(0x3F13CD36)};
    const float ldir[3] = {VehLightDir[0], VehLightDir[1], VehLightDir[2]};
    const UNIT& u = Units[Vehs[veh_id].unit_id];
    float up[3] = {up_in[0], up_in[1], up_in[2]};
    float cam[9], inv[9];
    compute_camera(up, cam);
    VoxelScene* sc = &VehScene;
    std::memcpy(sc->v_light, ldir, 12);
    mat3_inverse(cam, inv);
    float sd[3];
    mat3_apply(inv, light, sd);
    std::memcpy(sc->v_shadow, sd, 12);
    Scene_set_shadow_table(sc, ShadowTable);
    // The camera 10000 units away along its view axis.
    const float pos[3] = {(float)((double)cam[6] * -10000.0f), (float)((double)cam[7] * -10000.0f),
                          (float)((double)cam[8] * -10000.0f)};
    // FP: (zoom unit * 0.07) in a register, stored as float.
    float scale;
    if (ZoomNum > 0) scale = (float)((double)ZoomNum * fbits(0x3D8F5C29) / ZoomDenom);
    else scale = (float)((double)(zoom + 0x10) * fbits(0x3D8F5C29) * 0.0625f);
    Scene_set_scale(sc, scale);
    Scene_set_camera(sc, pos, cam);
    vehdraw_construct_vehicle(veh_id, sc);
    Scene_set_rotation(sc, (float)(less_yaw ? angle - (double)CameraYaw : angle), 0.0f, 0.0f);
    const int32_t ox = ZoomNum > 0 ? ZoomNum * 0x32 / ZoomDenom : trunc_div16((zoom * 5 + 0x50) * 10);
    const int32_t oy = ox;
    Scene_render(sc, dest, px + ox, py + oy, (uint32_t)((mode & 0x2000) | 0x1000) >> 12);
    if (u.armor_id == 5) {
        // The shield of armour 5: drawn over the unit through the blend tables.
        sc->count = 0;
        vehdraw_construct_armor(Vehs[veh_id].faction_id, u.chassis_id, u.weapon_id, 5, 1, sc);
        Scene_render_table(sc, dest, px + ox, py + oy, 0x18, table(0x6EEF7C), (uint32_t)(mode >> 12) & 2);
        return;
    }
    Scene_clear(sc, 0);
}

float facing_angle(int32_t dir) { return (float)((double)((dir - 3) & 7) * fbits(0x3F490E56)); }

// vehdraw_turn_start / vehdraw_scoot_start's state (0x8CC258.., 0x8CC820, 0x8CDCF8).
struct TurnAnim {
    int32_t veh = -1;
    float start = 0.0f, step = 0.0f;  // 0x8CC260, 0x8CC258
    float up[3] = {0.0f, 0.0f, 1.0f};
} g_turn;
struct ScootAnim {
    int32_t veh = -1, frames = 0, dir = 0;  // 0x8CC820, 0x8CDCF8, 0x8CC25C
    float up_src[3] = {0, 0, 1}, up_mid[3] = {0, 0, 1}, up_dst[3] = {0, 0, 1};
} g_scoot;

}  // namespace

void GAMEAPI vehdraw_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t mode, int32_t zoom, int32_t a7) {
    if (!dest) return;
    if (mode & 1) veh_id = pick_top_veh(veh_id);
    int32_t x, y;
    if (VehScootX == -1 || VehScootID != veh_id) {
        x = Vehs[veh_id].x;
        y = Vehs[veh_id].y;
    } else {
        x = VehScootX;
        y = VehScootY;
    }
    int32_t rot = Vehs[veh_id].rotate_angle;
    if (rot == -1) rot = 0;
    float up[3];
    tile_up(veh_id, x, y, a7 != 0, up);
    render_vehicle(dest, veh_id, px, py, mode, zoom, up, (double)((rot - 3) & 7) * fbits(0x3F490E56), true);
}

// vehdraw_turn_start (0x4BF8D0): the unit (the top of its stack for flags 1) turns from its facing
// to `dir` (set at once) over `per_turn` frames for a whole turn; the frames of this turn (the
// shorter way round). The tilt is the lifted unit's tile's.
int32_t GAMEAPI vehdraw_turn_start(int32_t veh_id, int32_t dir, int32_t per_turn, uint32_t flags) {
    if (flags & 1) veh_id = pick_top_veh(veh_id);
    int32_t from = Vehs[veh_id].rotate_angle;
    if (from == -1) from = 0;
    g_turn.veh = veh_id;
    Vehs[veh_id].rotate_angle = (int8_t)dir;
    tile_up(veh_id, VehLiftX, VehLiftY, true, g_turn.up);
    g_turn.start = facing_angle(from);
    float to = facing_angle(dir), a, b;
    const float two_pi = fbits(0x40C90E56);  // 6.283
    if (g_turn.start <= to) {
        a = g_turn.start + two_pi - to;
        b = to - g_turn.start;
    } else {
        a = g_turn.start - to;
        b = to + two_pi - g_turn.start;
    }
    // FP: ftol(per_turn * angle / (2 pi)).
    if (a < b) {
        int32_t n = (int32_t)((float)per_turn * a * fbits(0x3E22FABE));
        g_turn.step = n ? -(a / (float)n) : 0.0f;
        return n;
    }
    int32_t n = (int32_t)((float)per_turn * b * fbits(0x3E22FABE));
    g_turn.step = n ? b / (float)n : 0.0f;
    return n;
}

// vehdraw_turn (0x4BFC70): frame `frame` of the turn.
void GAMEAPI vehdraw_turn(Buffer* dest, int32_t px, int32_t py, int32_t frame, int32_t zoom) {
    if (g_turn.veh < 0) return;
    render_vehicle(dest, g_turn.veh, px, py, 0, zoom, g_turn.up,
                   (double)frame * g_turn.step + g_turn.start - (double)CameraYaw, false);
}

// vehdraw_scoot_start (0x4BFE00): the unit's slide toward `dir` in `frames` frames, its tilt
// going from its tile's to the next tile's (approximated: through their mean at the half way).
void GAMEAPI vehdraw_scoot_start(int32_t veh_id, int32_t dir, int32_t frames, uint32_t flags) {
    if (flags & 1) veh_id = pick_top_veh(veh_id);
    g_scoot.frames = frames;
    g_scoot.veh = veh_id;
    g_scoot.dir = (dir - 3) & 7;
    int32_t x2 = VehLiftX + NearOffsetX[dir & 7], y2 = VehLiftY + NearOffsetY[dir & 7];
    if (!(MapToggleFlat & 1)) {
        if (x2 < 0) x2 += MapAreaX;
        else if (x2 >= MapAreaX) x2 -= MapAreaX;
    }
    tile_up(veh_id, VehLiftX, VehLiftY, true, g_scoot.up_src);
    if (y2 >= 0 && y2 < MapAreaY && x2 >= 0 && x2 < MapAreaX) tile_up(veh_id, x2, y2, true, g_scoot.up_dst);
    else std::memcpy(g_scoot.up_dst, g_scoot.up_src, 12);
    vec3_add(g_scoot.up_src, g_scoot.up_dst, g_scoot.up_mid);
    vec3_normalize(g_scoot.up_mid, g_scoot.up_mid);
}

// vehdraw_scoot (0x4C0F50): frame `frame` of the slide (the animated parts step on).
void GAMEAPI vehdraw_scoot(Buffer* dest, int32_t px, int32_t py, int32_t frame, int32_t zoom) {
    if (g_scoot.veh < 0) return;
    int32_t half = g_scoot.frames >> 1;
    const float *a = g_scoot.up_src, *b = g_scoot.up_mid;
    if (half < frame) {
        frame -= half;
        a = g_scoot.up_mid;
        b = g_scoot.up_dst;
    }
    float t = half > 0 ? (float)frame / (float)half : 1.0f;
    float up[3] = {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
    vec3_normalize(up, up);
    VehAnimFrame++;
    render_vehicle(dest, g_scoot.veh, px, py, 0, zoom, up, (double)g_scoot.dir * fbits(0x3F490E56), true);
}

int32_t GAMEAPI veh_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t zoom, int32_t mode, int32_t a7) {
    const uint32_t m = (uint32_t)mode;
    if (m & 1) {
        veh_id = pick_top_veh(veh_id);
        if (VehMovingID != -1 && VehMovingID == veh_id) return 0;
    }
    VEH& veh = Vehs[veh_id];
    const int32_t f = veh.faction_id;
    const UNIT& unit = Units[veh.unit_id];
    if ((zoom < -8 || (m & 0x100)) && !(m & 8) && !(m & 0x800)) {
        // Small zoom: a box in the faction colour.
        if (f != CurrentPlayerFaction && unit.plan == 0xB && !(GameState & STATE_OMNISCIENT_VIEW)) return 0;
        if (m & 0x400) return 0;
        int32_t right = zscale(zoom, 100) + px, bottom = zscale(zoom, 0x32) + py;
        int32_t left = px, top = py;
        if (!(m & 0x200)) {
            int32_t d = ZoomNum < 1 ? trunc_div16(zoom * 8 + 0x80) : ZoomNum * 8 / ZoomDenom;
            left += d;
            right -= d;
            top += d;
            bottom -= d;
        } else {
            left -= 2;
            right += 2;
            top -= 2;
            bottom += 2;
        }
        int32_t q = (bottom - top + (((bottom - top) >> 31) & 3)) >> 2;
        top += q;
        bottom += q;
        int32_t adj = ((right - left) - bottom + top) / 2;
        UiRect r{left + adj, top, right - adj, bottom};
        int32_t frame = is_spartans(f) ? 0x2C : 0;
        Buffer_box_2(dest, &r, frame, frame);
        r = {r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        Buffer_fill_3(dest, &r, FactionColorFill[f]);
        return 0;
    }
    if (ZoomNum < 1) {
        BlitDestScale = zoom + 0x10;
        BlitSourceScale = 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    const int32_t x0 = px, y0 = py;
    // Sprite index: the chassis, or 9 + the weapon's / the unit's icon.
    int32_t idx = unit.chassis_id;
    if (Weapon[unit.weapon_id].icon >= 0 && idx == 0) idx = Weapon[unit.weapon_id].icon + 9;
    if (unit.icon_offset >= 0) idx = unit.icon_offset + 9;
    int32_t morale = f == 0 ? morale_alien(veh_id, CurrentPlayerFaction) : morale_veh(veh_id, 0, 0);
    int32_t variant = 0;
    if (idx == 0xC || idx == 10 || idx == 0xD || idx == 0xE) variant = morale;
    else if (idx == 0xF) variant = veh.unit_id == 0x11 ? 1 : (veh.unit_id == 0x12 ? 2 : 0);
    if (!(m & 0x1000)) {
        px += zscale(zoom, (uint8_t)VehSpriteOffsets[idx][0][variant]);
        py += zscale(zoom, (uint8_t)VehSpriteOffsets[idx][1][variant]);
    }
    const uint32_t flag_only = m & 0x800;
    if (!flag_only) {
        if (idx > 8) {
            Sprite* s = ArtSprite(kSprUnits + (variant + idx * 8) * 0x2C);
            Sprite_draw_dest(s, dest, s->key & 0xFF, x0, y0, ShadowTable);
        }
        BlitDestScale = 1;
        BlitSourceScale = 1;
        if (!(m & 0x80) && idx < 9) {
            // Modes 0x10 / 0x20: the turning and moving animations (a7: the frame).
            if (m & 0x10) vehdraw_turn(dest, x0, y0, a7, zoom);
            else if (m & 0x20) vehdraw_scoot(dest, x0, y0, a7, zoom);
            else vehdraw_draw(dest, veh_id, x0, y0, mode, zoom, a7);
        }
    }
    if (ZoomNum < 1) {
        BlitSourceScale = 0x10;
        BlitDestScale = zoom + 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    bool skip_flag = (((m & 0x40) || (m & 0x400)) && !flag_only) ||
                     (f != CurrentPlayerFaction && unit.plan == 0xB && !(GameState & STATE_OMNISCIENT_VIEW));
    if (!skip_flag) {
        // Flag: [faction][morale][no moves left]; behind it a silhouette for stacks.
        int32_t moved = 0;
        if (MultiplayerActive || f == CurrentPlayerFaction) {
            int32_t sp = speed(veh_id, 0), left = sp - veh.moves_spent;
            if (left < 0 || (left < 1000 && sp == veh.moves_spent)) moved = 1;
        }
        Sprite* flag = ArtSprite(kSprFlags + (moved + (f * 7 + morale) * 2) * 0x2C);
        const int32_t six = ZoomNum < 1 ? trunc_div16((zoom * 3 + 0x30) * 2) : ZoomNum * 6 / ZoomDenom;
        if ((m & 2) && stack_check(veh_id, 1, -1, -1, -1) > 1) {
            if ((uint8_t)VehSpriteOffsets[idx][0][variant] > 0x32) px -= six;
            Sprite_draw_mono_2(flag, dest, flag->key & 0xFF, px, py, FactionColorFrame[f]);
            px += six;
        }
        pfncSpriteStretchCopyToCanvas(flag, dest, flag->key & 0xFF, px, py);
        // Health bar along the flag pole.
        UiRect bar;
        bar.left = six + px;
        bar.top = six + py;
        bar.right = bar.left + (ZoomNum < 1 ? trunc_div16(zoom * 2 + 0x20) : ZoomNum * 2 / ZoomDenom);
        bar.bottom = bar.top + zscale(zoom, 0x15);
        Buffer_fill_3(dest, &bar, 0);
        auto max_hp = [&]() -> uint32_t {
            if (unit.plan == 0xC) return 1;
            uint32_t r = unit.reactor_id;
            if (r == 0) r = 1;
            else if (r > 100) r = 100;
            return r * 10;
        };
        uint32_t dmg = veh.damage_taken;
        int32_t hp = (int32_t)(max_hp() - dmg);
        if (hp >= 0 && (hp > 9999 || max_hp() != dmg)) {
            if (hp > 9999) hp = 9999;
            int32_t color = 0xF9;
            bar.bottom = ((bar.bottom - bar.top) * hp) / (int32_t)max_hp() + bar.top;
            if (dmg < (max_hp() * 2) / 3) color = 0xFB;
            if (dmg < max_hp() / 3 || veh.damage_taken == 0) color = 0xFA;
            Buffer_fill_3(dest, &bar, color);
        }
        // Order letter in the flag.
        Font* font = ui_label_font(zoom, 1, 4, 1);
        Buffer_set_font(dest, font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(dest, is_spartans(f) ? 0x2C : 0, -1, 1, 1);
        int32_t order = (int8_t)veh.order;
        if (order < 0) order = 0;
        else if (order > 0x1D) order = 0x1D;
        UiRect box;
        box.left = (ZoomNum < 1 ? trunc_div16((zoom * 5 + 0x50) * 2) : ZoomNum * 10 / ZoomDenom) + px;
        box.top = (ZoomNum < 1 ? trunc_div16(zoom * 5 + 0x50) : ZoomNum * 5 / ZoomDenom) + py;
        box.right = box.left + zscale(zoom, 0xB);
        box.bottom = box.top + zscale(zoom, 0xE);
        char text[4] = {0, 0, 0, 0};
        auto set = [&](const char* sv) { text[0] = sv ? sv[0] : 0; };
        set(Order[order].letter);
        if (veh.state & 0x4000) set("/");
        if ((veh.state & 0x1000200) == 0x1000200) set("P");
        if (veh.state & 0x200) {
            if (veh.order_auto_type == 0xB) set("*");
            if ((veh.state & 0x200) && veh.order_auto_type == 0xA) set("B");
        }
        text[2] = 0;
        text[1] = 0;
        auto brackets = [&]() {
            if (text[0] == '[' || text[0] == ']') text[1] = text[0];
        };
        if (f == CurrentPlayerFaction && (FactionStatus[0] & (1 << f))) {
            brackets();
        } else if ((GameState & STATE_OMNISCIENT_VIEW) || !(FactionStatus[0] & (1 << CurrentPlayerFaction))) {
            if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) text[0] = (char)veh.status_icon;
            if (text[0] != '-' && (veh.state & 0x40000))
                Buffer_set_text_color(dest, (veh.state & 0x20000) ? 0xF8 : 0xFB, -1, 1, 1);
            brackets();
        } else {
            text[0] = '-';
        }
        if (f == CurrentPlayerFaction) {
            if (veh.state & 0x200) {
                Buffer_set_text_color(dest, FactionColorFrame[f], -1, 1, 1);
                if (text[0] == '-') text[0] = 'a';
                if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE))
                    text[0] = (char)veh.status_icon;
            }
            if (veh.state & 0x8000000) Buffer_set_text_color(dest, (CurrentPlayerFaction != 3 ? 5 : 0) - 6, -1, 1, 1);
        }
        Buffer_write_cent_l_7(dest, text, &box, (int32_t)std::strlen(text));
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
    if (flag_only) return 0;
    return idx < 9;
}

void THISCALL MapWin_draw_vehicles(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id) {
    int32_t mode = 2;
    if (!ControlTurnStarted && !ControlTurnMove && !(GameRules & RULES_TIME_WARP)) return;
    uint32_t opts = (uint32_t)mw(win, kMwOptions);
    if ((opts & 0x1000000) || !(opts & 0x802000C0) || (opts & 0x1000)) return;
    MAP* sq = &MapTiles[MapHalfX * y + (x >> 1)];
    auto known = [&]() {
        return (GameState & STATE_OMNISCIENT_VIEW) || (Factions[faction_id].player_flags & 0x200) ||
               (sq->visibility & (1 << faction_id));
    };
    if ((!(opts & 0x80000000u) || (opts & 0x80)) && known()) MapWin_draw_bases(win, x, y, px, py, faction_id);
    if (mw(win, kMwOptions) & 0x80000000u) return;
    int32_t v;
    if (VehMovingID != -1 && Vehs[VehMovingID].x == x && Vehs[VehMovingID].y == y) {
        mode = 3;
        v = VehMovingID;
    } else if (VehDrawAttackID >= 0 && x == Vehs[VehDrawAttackID].x && y == Vehs[VehDrawAttackID].y) {
        v = VehDrawAttackID;
        if (Units[Vehs[VehDrawAttackID].unit_id].chassis_id == 8) {
            v = Vehs[VehDrawAttackID].next_veh_id_stack;
            if (v < 0) return;
        }
    } else if (VehDrawDefendID >= 0 && x == Vehs[VehDrawDefendID].x && y == Vehs[VehDrawDefendID].y) {
        v = VehDrawDefendID;
    } else {
        if (!known()) return;
        mode = 3;
        int32_t sel = MapWinVehA;
        if (x == Vehs[sel].x && y == Vehs[sel].y && (FactionStatus[0] & (1 << Vehs[sel].faction_id)) &&
            veh_selectable(sel) && !(Vehs[sel].state & 0x4200)) {
            v = sel;
            if (MapWinInVehMode && MapWinField23BF8 && MapWinField23BE4 && Vehs[sel].order == 0 && !MapWinField23C00 &&
                (!MultiplayerActive || !ControlTurnC)) {
                // The selected unit blinks: hidden while the mouse is near it, else drawn
                // with mode 0x403. (Mouse position: not reconstructed, taken as far away.)
                if (GameMorePreferences & MPREF_AV_WHOLE_VEH_BLINKS) return;
                mode = 0x403;
            }
        } else {
            if (!(mw(win, kMwOptions) & 0x200040)) return;
            v = veh_at(x, y);
        }
        if (mw(win, kMwOptions) & 0x200000) {
            v = veh_at(x, y);
            if (v < 0) return;
            while (Vehs[v].home_base_id != BaseUpkeepDrawID) {
                v = Vehs[v].next_veh_id_stack;
                if (v < 0) return;
            }
        }
        if (v < 0) return;
        if (!(GameState & STATE_OMNISCIENT_VIEW) &&
            ((Vehs[v].faction_id != CurrentPlayerFaction || (Vehs[v].flags & 0x440) == 0x440) &&
             !(Vehs[v].visibility & (1 << CurrentPlayerFaction))))
            return;
        if ((sq->items & 1) && (sq->val2 & 0xF) < 8) {
            // Units in bases are hidden, except the selected one while it is ready.
            if (!MapWinInVehMode || v != MapWinVehA || !veh_ready(v) || (Vehs[v].state & 0x4200) || !MapWinField23BE4)
                return;
        }
    }
    veh_draw(ui_mapwin_native(win)->canvas, v, px, py, mw(win, kMwZoom), mode, 1);
}
