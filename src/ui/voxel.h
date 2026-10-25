// The original's voxel model renderer ("Caviar", gfx2 0x616BC0..0x618D20 and the video module
// 0x6381F0..0x6404F0, the span drawers in the _SELFMOD section 0x664000..0x667380): the 3D unit
// models of the map (vehdraw.cpp).
//
// * Models (*.cvr): a chunked file (Caviar_load_file 0x6381F0): a palette with a 24 x 256
//   shading table (colour x light level), and objects, each with a voxel mesh (one or more
//   levels of detail) and per animation frame a visibility flag, a position and a 3x3 matrix.
//   A model is held by a 12-byte handle object at a fixed address of the original
//   (CaviarHandle: load 0x616BD0, free 0x616C60); vehdraw addresses models by these addresses.
// * Scene (the object at 0x8CC828, 0x13D0 bytes): up to 200 objects (a model object's frame,
//   mesh and colour table), the view (rotation, light, shadow direction and offsets) and the
//   camera (position, matrix, scale, clip). Scene_render (0x617230) draws it centred on a point
//   of a Buffer: the voxels are splatted (2x2 pixels, or 1 pixel when small) with a 16-bit
//   z-buffer into a 256x256 work canvas, the shadow (the voxels projected along the shadow
//   direction) into a second canvas; both are then blitted to the destination (the shadow
//   through the shadow table).
// * Floating point follows the original's x87 code operation by operation (ui/x87.h).
//
// Native layout: the objects live in native memory (they hold pointers); field comments give
// the original offsets.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/memmap.h"
#include "ui/gfx.h"

// ---- models ----
struct CaviarModel {  // 0x44 bytes in the original (Caviar_load_file)
    uint32_t flags = 0;               // +0x00: bit 0: the last mesh was read with a size
    int32_t state = 0;                // +0x04: 3 when loaded
    float version = 0;                // +0x08 (chunk 0x01000000)
    std::string name;                 // +0x0C (0x02000000)
    std::string palette_name;         // +0x10 (0x01010000)
    std::vector<uint8_t> colors;      // +0x14 (0x01020000): 2 bytes, 256 RGB, 24 x 256 shading table at +0x302
    std::vector<uint8_t> chunk_0103;  // +0x18 (0x01030000)
    std::string object_set_name;      // +0x1C (0x04010000)
    int32_t frame_count = 0;          // +0x20 (0x04030000)
    int32_t object_count = 0;         // +0x24 (0x04020000)
    std::vector<std::string> object_names;  // +0x28
    struct Mesh {                     // +0x2C: 8 bytes per object
        uint32_t size = 0;            // the mesh's byte size
        std::vector<uint8_t> data;    // level of detail nodes (see vx_draw_node)
    };
    std::vector<Mesh> meshes;
    struct Frames {                   // +0x30: 12 bytes per object
        std::vector<uint8_t> flags;   // per frame (0x80: hidden)
        std::vector<float> pos;       // per frame: x, y, z
        std::vector<float> mat;       // per frame: 3x3
    };
    std::vector<Frames> frames;
    int32_t user_size = 0;            // +0x40
    const uint8_t* shading() const { return colors.size() >= 0x1B02 ? colors.data() + 0x302 : nullptr; }
};
// The handle object of a model (12 bytes): +0 unused, +4 the file handle while loading, +8 the
// model. vehdraw keeps them at fixed addresses (load_voxels); caviar_handle(addr) returns the
// handle of an address (created empty when unknown).
struct CaviarHandle {
    int32_t unused = 0;
    int32_t fd = 0;
    CaviarModel* data = nullptr;
};
CaviarHandle* caviar_handle(uint32_t addr);
ADDR_NATIVE(0x616BC0) void THISCALL CaviarHandle_ctor(CaviarHandle* self);
// Loads a .cvr file into the handle (the previous model is freed): 0, 0x10 (no name), 6 (not
// found), 0x17 (bad file).
ADDR_NATIVE(0x616BD0) int32_t THISCALL CaviarHandle_load(CaviarHandle* self, const char* file);
ADDR_NATIVE(0x616C60) void THISCALL CaviarHandle_free(CaviarHandle* self);

// ---- scene ----
struct VoxelObject {  // scene +0x74C: 12 bytes per object
    const uint8_t* flag;  // frame flag byte (0x80: hidden)
    float* pos;           // position (copied into the scene's storage)
    float* mat;           // 3x3 matrix
};
struct VoxelMeshRef {  // scene +0x10C: 8 bytes per object
    uint32_t size;
    const uint8_t* data;
};
struct VoxelScene {
    float f00 = 0;                    // +0x00 (2/3)
    uint32_t flags = 0;               // +0x04: 1 initialised; 8 / 0x10 fixed colour tables; 0x20 double size
    Buffer* dest = nullptr;           // +0x14
    // View (+0x18; the renderer's view parameter).
    int32_t v_unused = 0;             // +0x18
    int32_t v_count = 0;              // +0x1C: object count
    int32_t v_frame = 0;              // +0x20
    float v_pos[3]{};                 // +0x2C: translation
    float v_mat[9]{};                 // +0x38: rotation
    float v_scale = 0;                // +0x5C
    float v_light[3]{};               // +0x60: light direction
    float v_shadow[3]{};              // +0x6C: shadow projection direction
    float v_shadow_z = 0;             // +0x78
    float v_offset[2]{};              // +0x7C: offset (scaled by v_zoom)
    float v_shift[2]{};               // +0x88: offset (pixels)
    float v_zoom = 0;                 // +0x90
    float v_shadow_shift[2]{};        // +0x94
    float v_shadow_zoom[2]{};         // +0x9C
    uint8_t v_shadows = 0;            // +0xA4: draw the shadow
    // Camera (+0xA5).
    float c_pos[3]{};                 // +0xA5
    float c_mat[9]{};                 // +0xB1
    float c_scale = 0;                // +0xD5
    float c_persp = 0;                // +0xD9
    float c_near = 0, c_far = 0;      // +0xDD, +0xE1
    float c_zmin = 0, c_zmax = 0;     // +0xE5, +0xE9
    int32_t c_clip[6]{};              // +0xED: x0, y0, x1, y1, width, height
    float f108 = 0;                   // +0x108
    VoxelMeshRef meshes[200]{};       // +0x10C
    VoxelObject objects[200]{};       // +0x74C
    const uint8_t* colors[200]{};     // +0x10AC: shading table per object
    int32_t count = 0;                // +0x13CC
    // Native: the per object storage the original allocates in Scene_init (flag byte, position,
    // matrix).
    uint8_t s_flag[200]{};
    float s_pos[200][3]{};
    float s_mat[200][9]{};
};
// The unit scene (0x8CC828).
extern VoxelScene VehScene;

ADDR_NATIVE(0x616DA0) void THISCALL Scene_ctor(VoxelScene* self);
// Allocates the object storage and sets the default view and camera; 0 or 4 (no memory).
ADDR_NATIVE(0x616DE0) int32_t THISCALL Scene_init(VoxelScene* self);
// Adds the objects of a model's frame (frame modulo the frame count): 0, 0x10 (no handle),
// 7 (no model), 1 (scene full). Variants: with an offset added to the positions, with a colour
// table (instead of the model's), or both.
ADDR_NATIVE(0x617F20) int32_t THISCALL Scene_add(VoxelScene* self, CaviarHandle* model, int32_t frame);
ADDR_NATIVE(0x617920) int32_t THISCALL Scene_add_offset(VoxelScene* self, CaviarHandle* model, const float* offset,
                                                        int32_t frame);
ADDR_NATIVE(0x617AA0) int32_t THISCALL Scene_add_colors_offset(VoxelScene* self, CaviarHandle* model,
                                                               const uint8_t* colors, const float* offset,
                                                               int32_t frame);
ADDR_NATIVE(0x618080) int32_t THISCALL Scene_add_colors(VoxelScene* self, CaviarHandle* model, const uint8_t* colors,
                                                        int32_t frame);
// Camera position and matrix.
ADDR_NATIVE(0x6182A0) void THISCALL Scene_set_camera(VoxelScene* self, const float* pos, const float* mat);
// View rotation from Euler angles (radians).
ADDR_NATIVE(0x618370) void THISCALL Scene_set_rotation(VoxelScene* self, float a, float b, float c);
ADDR_NATIVE(0x6183B0) void THISCALL Scene_set_scale(VoxelScene* self, float scale);
ADDR_NATIVE(0x6183C0) float THISCALL Scene_get_scale(VoxelScene* self);
// Shadow translation table of the renderer (256 bytes).
ADDR_NATIVE(0x618430) int32_t THISCALL Scene_set_shadow_table(VoxelScene* self, const uint8_t* table);
// Clears the work canvases (keep_depth = 0: also the depth buffer and the dirty rectangles).
ADDR_NATIVE(0x618470) void THISCALL Scene_clear(VoxelScene* self, int32_t keep_depth);
// Draws the scene centred on (x, y) of dest. flags: 1 keep the depth buffer, 2 no shadow,
// 4 centre the objects' bounding box, 8 render only (no blit to dest), 0x10 (with 4) identity
// rotation for the bounding box. 0, or 7 (not initialised / no dest).
ADDR_NATIVE(0x617230) int32_t THISCALL Scene_render(VoxelScene* self, Buffer* dest, int32_t x, int32_t y,
                                                    uint32_t flags);
// Renders the scene over a canvas filled with `key` and blits it to dest through `table`
// (a 256 x 256 translation: dest = table[model pixel * 256 + dest pixel]).
ADDR_NATIVE(0x6171A0) int32_t THISCALL Scene_render_table(VoxelScene* self, Buffer* dest, int32_t x, int32_t y,
                                                          int32_t key, const uint8_t* table, uint32_t flags);

// The renderer's set-up (gfx2 0x6185A0, part of the graphics init): the 256x256 work canvases
// (colour 0x9B9108, shadow 0x9B96B0), the depth buffer (0x9B9100) and the renderer (0x9BB478).
void voxel_renderer_init();

// ---- vector and matrix helpers (pop module 0x627D00..0x6282E0; float[3] / float[9]) ----
ADDR_NATIVE(0x627D00) void GAMEAPI mat3_from_euler(const float* angles, float* out);
ADDR_NATIVE(0x627E20) void GAMEAPI mat3_mul(const float* a, const float* b, float* out);
ADDR_NATIVE(0x627F50) void GAMEAPI mat3_inverse(const float* m, float* out);
ADDR_NATIVE(0x6280E0) void GAMEAPI mat3_apply(const float* m, const float* v, float* out);
ADDR_NATIVE(0x628150) void GAMEAPI vec3_add(const float* a, const float* b, float* out);
ADDR_NATIVE(0x628180) void GAMEAPI vec3_sub(const float* a, const float* b, float* out);
ADDR_NATIVE(0x6281B0) void GAMEAPI vec3_scale(const float* a, float s, float* out);
ADDR_NATIVE(0x628220) void GAMEAPI vec3_normalize(const float* a, float* out);
ADDR_NATIVE(0x628290) void GAMEAPI vec3_cross(const float* a, const float* b, float* out);
// Length (returned in st(0) by the original: a double here).
double vec3_length(const float* a);  // 0x6281E0
