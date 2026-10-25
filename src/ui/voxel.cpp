// Voxel models and the unit scene (see voxel.h): the .cvr loader (video 0x6381F0..), the model
// handles and the scene object (gfx2 0x616BC0..0x618470) and the vector helpers (0x627D00..).
#include "ui/voxel.h"

#include <cmath>
#include <cstring>
#include <map>
#include <memory>

#include "platform/win32.h"
#include "ui/x87.h"

VoxelScene VehScene;

// ---- vector and matrix helpers ----------------------------------------------------------------
// FP: each element as the original's x87 sequence: products of floats are exact in double, the
// sums round to double in the original's order, the result is stored as float. Elements are
// computed and stored one after the other (the original reads its inputs again after each
// store, which matters when out aliases an input).

void GAMEAPI mat3_from_euler(const float* a, float* out) {
    // FP: FSIN/FCOS results keep 64 bits; some products use the register value, some the float
    // copy the original stores first.
    const ext s2 = x87_fsin((ext)a[2]), s1 = x87_fsin((ext)a[1]);
    const ext c0 = x87_fcos((ext)a[0]), s0 = x87_fsin((ext)a[0]);
    const ext c1 = x87_fcos((ext)a[1]), c2 = x87_fcos((ext)a[2]);
    const float f1c = (float)c0, f18 = (float)s0, f8 = (float)c1, f0c = (float)s1, f4 = (float)c2, f0 = (float)s2;
    out[0] = (float)x87_mul(c1, f1c);
    const float f10 = (float)x87_mul(s2, f0c);
    out[1] = (float)(x87_mul(c0, f10) - (double)f4 * f18);
    const double t = x87_mul(c2, f0c);
    out[2] = (float)((double)f1c * t + (double)f0 * f18);
    out[3] = (float)((double)f8 * f18);
    out[4] = (float)(x87_mul(s0, f10) + (double)f4 * f1c);
    out[5] = (float)((double)f18 * t - (double)f0 * f1c);
    out[6] = (float)-s1;
    out[7] = (float)((double)f0 * f8);
    out[8] = (float)((double)f4 * f8);
}

void GAMEAPI mat3_mul(const float* a, const float* b, float* out) {
    out[0] = (float)(((double)a[1] * b[3] + (double)a[2] * b[6]) + (double)b[0] * a[0]);
    out[1] = (float)(((double)a[0] * b[1] + (double)b[4] * a[1]) + (double)b[7] * a[2]);
    out[2] = (float)(((double)a[0] * b[2] + (double)a[2] * b[8]) + (double)a[1] * b[5]);
    out[3] = (float)(((double)b[0] * a[3] + (double)b[6] * a[5]) + (double)b[3] * a[4]);
    out[4] = (float)(((double)b[1] * a[3] + (double)b[7] * a[5]) + (double)b[4] * a[4]);
    out[5] = (float)(((double)a[5] * b[8] + (double)a[4] * b[5]) + (double)a[3] * b[2]);
    out[6] = (float)(((double)b[0] * a[6] + (double)a[7] * b[3]) + (double)a[8] * b[6]);
    out[7] = (float)(((double)a[8] * b[7] + (double)a[7] * b[4]) + (double)a[6] * b[1]);
    out[8] = (float)(((double)a[6] * b[2] + (double)a[7] * b[5]) + (double)a[8] * b[8]);
}

void GAMEAPI mat3_inverse(const float* m, float* out) {
    // FP: the cofactors the original stores (A, E, G..K) are floats; B and C stay in registers.
    const float A = (float)((double)m[4] * m[8] - (double)m[5] * m[7]);
    const double B = (double)m[3] * m[8] - (double)m[6] * m[5];
    const double C = (double)m[3] * m[7] - (double)m[6] * m[4];
    const float E = (float)((double)m[1] * m[8] - (double)m[2] * m[7]);
    const float G = (float)((double)m[0] * m[8] - (double)m[2] * m[6]);
    const float H = (float)((double)m[0] * m[7] - (double)m[1] * m[6]);
    const float I = (float)((double)m[1] * m[5] - (double)m[2] * m[4]);
    const float d = (float)(1.0 / ((((double)m[0] * A) - B * m[1]) + C * m[2]));
    const float J = (float)((double)m[0] * m[5] - (double)m[2] * m[3]);
    const float K = (float)((double)m[0] * m[4] - (double)m[1] * m[3]);
    out[0] = (float)((double)d * A);
    out[1] = (float)-((double)d * E);
    out[2] = (float)((double)d * I);
    out[3] = (float)-((double)d * B);
    out[5] = (float)-((double)d * J);
    out[4] = (float)((double)d * G);
    out[6] = (float)((double)d * C);
    out[7] = (float)-((double)d * H);
    out[8] = (float)((double)d * K);
}

void GAMEAPI mat3_apply(const float* m, const float* v, float* out) {
    out[0] = (float)(((double)m[1] * v[1] + (double)m[2] * v[2]) + (double)v[0] * m[0]);
    out[1] = (float)(((double)m[4] * v[1] + (double)m[5] * v[2]) + (double)m[3] * v[0]);
    out[2] = (float)(((double)m[8] * v[2] + (double)m[7] * v[1]) + (double)m[6] * v[0]);
}

void GAMEAPI vec3_add(const float* a, const float* b, float* out) {
    out[0] = (float)((double)b[0] + a[0]);
    out[1] = (float)((double)a[1] + b[1]);
    out[2] = (float)((double)a[2] + b[2]);
}

void GAMEAPI vec3_sub(const float* a, const float* b, float* out) {
    out[0] = (float)((double)a[0] - b[0]);
    out[1] = (float)((double)a[1] - b[1]);
    out[2] = (float)((double)a[2] - b[2]);
}

void GAMEAPI vec3_scale(const float* a, float s, float* out) {
    out[0] = (float)((double)a[0] * s);
    out[1] = (float)((double)a[1] * s);
    out[2] = (float)((double)a[2] * s);
}

double vec3_length(const float* a) {
    return std::sqrt(((double)a[1] * a[1] + (double)a[2] * a[2]) + (double)a[0] * a[0]);
}

void GAMEAPI vec3_normalize(const float* a, float* out) {
    const float x = a[0];
    const float inv = (float)(1.0 / std::sqrt(((double)a[1] * a[1] + (double)a[2] * a[2]) + (double)x * x));
    out[0] = (float)((double)x * inv);
    out[1] = (float)((double)inv * a[1]);
    out[2] = (float)((double)inv * a[2]);
}

void GAMEAPI vec3_cross(const float* a, const float* b, float* out) {
    out[0] = (float)((double)a[1] * b[2] - (double)b[1] * a[2]);
    out[1] = (float)((double)b[0] * a[2] - (double)a[0] * b[2]);
    out[2] = (float)((double)a[0] * b[1] - (double)b[0] * a[1]);
}

// ---- .cvr loader ---------------------------------------------------------------------------------
// Chunks: {u32 tag, u32 length (header included)}; containers hold chunks. The original reads
// through the renderer's file callbacks (read, skip); here the file is read at once and a
// position plays the file pointer.

namespace {

struct CvrReader {
    std::vector<uint8_t> file;
    size_t pos = 0;
    // read (0x638650): fails when the file ends.
    bool read(void* out, int32_t n) {
        if (n < 1) return true;
        if (pos + (size_t)n > file.size()) return false;
        std::memcpy(out, file.data() + pos, (size_t)n);
        pos += (size_t)n;
        return true;
    }
    bool skip(int32_t n) {  // the seek callback (relative)
        if (n < 0 || pos + (size_t)n > file.size()) return false;
        pos += (size_t)n;
        return true;
    }
    bool header(int32_t* tag, int32_t* len) {
        int32_t h[2];
        if (!read(h, 8)) return false;
        *tag = h[0];
        *len = h[1];
        return true;
    }
    bool string(std::string* out, int32_t len) {  // a chunk of len - 8 characters
        std::vector<char> s((size_t)std::max(len - 8, 0) + 1, 0);
        if (!read(s.data(), len - 8)) return false;
        *out = s.data();
        return true;
    }
};

// 0x6391F0: the shading table of version <= 7 files (16 levels) expanded to 24 levels.
bool cvr_old_colors(CvrReader& r, std::vector<uint8_t>& out) {
    out.assign(0x1B02, 0);
    if (!r.read(&out[0], 1) || !r.read(&out[1], 1) || !r.read(&out[2], 0x300)) return false;
    std::vector<uint8_t> t(0x1000);
    if (!r.read(t.data(), 0x1000)) return false;
    for (int32_t c = 0; c < 0x100; c++)
        for (int32_t k = 0, l = 0; k < 0x30; k += 2, l++) out[0x302 + l * 0x100 + c] = t[(k / 3) * 0x100 + c];
    return true;
}

// 0x638720: palette chunk (0x03000000).
bool cvr_palette(CvrReader& r, CaviarModel* m, int32_t len) {
    len -= 8;
    while (len) {
        int32_t tag, l;
        if (!r.header(&tag, &l)) return false;
        len -= l;
        if (len < 0) return false;
        if (tag == 0x1010000) {
            if (!r.string(&m->palette_name, l)) return false;
        } else if (tag == 0x1020000) {
            if (m->version > 7.0f) {
                m->colors.assign((size_t)std::max(l - 8, 0), 0);
                if (!r.read(m->colors.data(), l - 8)) return false;
            } else if (!cvr_old_colors(r, m->colors)) {
                return false;
            }
        } else if (tag == 0x1030000) {
            m->chunk_0103.assign((size_t)std::max(l - 8, 0), 0);
            if (!r.read(m->chunk_0103.data(), l - 8)) return false;
        } else if (!r.skip(l - 8)) {
            return false;
        }
    }
    return true;
}

// 0x638CC0: mesh chunk (0x04040200).
bool cvr_mesh(CvrReader& r, CaviarModel::Mesh* mesh, int32_t len) {
    len -= 8;
    while (len) {
        int32_t tag, l;
        if (!r.header(&tag, &l)) return false;
        len -= l;
        if (len < 0) return false;
        if (tag == 0x4040201) {
            mesh->data.assign(mesh->size ? mesh->size : (size_t)std::max(l - 8, 0), 0);
            if ((size_t)std::max(l - 8, 0) > mesh->data.size()) return false;
            if (!r.read(mesh->data.data(), l - 8)) return false;
        } else if (tag == 0x4040202) {
            if (!r.read(&mesh->size, l - 8 > 4 ? 4 : l - 8)) return false;
            if (l - 8 > 4 && !r.skip(l - 12)) return false;
        } else if (!r.skip(l - 8)) {
            return false;
        }
    }
    return true;
}

// 0x638DE0: animation frames (0x04040300).
bool cvr_frames(CvrReader& r, CaviarModel::Frames* f, int32_t len, int32_t frames) {
    len -= 8;
    while (len) {
        int32_t tag, l;
        if (!r.header(&tag, &l)) return false;
        len -= l;
        if (len < 0) return false;
        if (tag == 0x4040301) {
            f->flags.assign((size_t)std::max(frames, 0), 0);
            if (!r.read(f->flags.data(), frames)) return false;
        } else if (tag == 0x4040302) {
            f->pos.assign((size_t)std::max(frames, 0) * 3, 0.0f);
            if (!r.read(f->pos.data(), frames * 0xC)) return false;
        } else if (tag == 0x4040303) {
            f->mat.assign((size_t)std::max(frames, 0) * 9, 0.0f);
            if (!r.read(f->mat.data(), frames * 0x24)) return false;
        } else if (!r.skip(l - 8)) {
            return false;
        }
    }
    return true;
}

// 0x638AD0: one object (0x04040000).
bool cvr_object(CvrReader& r, CaviarModel* m, int32_t index, int32_t len) {
    len -= 8;
    if (index < 0 || index >= m->object_count) return false;
    while (len) {
        int32_t tag, l;
        if (!r.header(&tag, &l)) return false;
        len -= l;
        if (len < 0) return false;
        if (tag == 0x4040100) {
            if (!r.string(&m->object_names[(size_t)index], l)) return false;
        } else if (tag == 0x4040200) {
            if (m->meshes.empty()) m->meshes.assign((size_t)m->object_count, CaviarModel::Mesh());
            CaviarModel::Mesh& mesh = m->meshes[(size_t)index];
            if (!cvr_mesh(r, &mesh, l)) return false;
            if (mesh.size == 0) {
                m->flags &= ~1u;
                int32_t s = 0;
                if (mesh.data.size() >= 8) std::memcpy(&s, &mesh.data[4], 4);
                mesh.size = (uint32_t)s;
            } else {
                m->flags |= 1u;
            }
        } else if (tag == 0x4040300) {
            if (!cvr_frames(r, &m->frames[(size_t)index], l, m->frame_count)) return false;
        } else if (!r.skip(l - 8)) {  // 0x04040400 (0x638F30): not in the game's files
            return false;
        }
    }
    return true;
}

// 0x6388C0: the object list (0x04000000).
bool cvr_objects(CvrReader& r, CaviarModel* m, int32_t len) {
    len -= 8;
    int32_t declared = len, index = 0;
    bool counted = false;
    while (len) {
        int32_t tag, l;
        if (!r.header(&tag, &l)) return false;
        len -= l;
        if (len < 0) return false;
        if (tag == 0x4020000) {
            int32_t n = 0;
            if (!r.read(&n, 4) || n < 0) return false;
            m->object_names.assign((size_t)n, std::string());
            m->frames.assign((size_t)n, CaviarModel::Frames());
            m->object_count = n;
            declared = n;
            counted = true;
            index = 0;
        } else if (tag == 0x4010000) {
            if (!r.string(&m->object_set_name, l)) return false;
        } else if (tag == 0x4030000) {
            if (!r.read(&m->frame_count, 4)) return false;
        } else if (tag == 0x4040000) {
            if (!cvr_object(r, m, index, l)) return false;
            index++;
        } else if (!r.skip(l - 8)) {  // 0x04050000 (0x639090, mesh patches): not in the game's files
            return false;
        }
    }
    return counted && declared == index;
}

// 0x6381F0: reads a .cvr file (nullptr: bad file).
CaviarModel* cvr_read(CvrReader& r) {
    int32_t tag, len;
    if (!r.header(&tag, &len) || tag != 0x525643) return nullptr;
    int32_t left = len - 8;
    auto m = std::make_unique<CaviarModel>();
    while (left) {
        int32_t l;
        if (!r.header(&tag, &l)) return nullptr;
        left -= l;
        if (left < 0) return nullptr;
        bool ok;
        if (tag == 0x1000000) ok = r.read(&m->version, 4);
        else if (tag == 0x2000000) ok = r.string(&m->name, l);
        else if (tag == 0x3000000) ok = cvr_palette(r, m.get(), l);
        else if (tag == 0x4000000) ok = cvr_objects(r, m.get(), l);
        else if (tag == 0xF000000) {
            ok = r.skip(l - 8);  // user data (no handler is installed)
        } else ok = r.skip(l - 8);
        if (!ok) return nullptr;
    }
    m->state = 3;
    return m.release();
}

std::map<uint32_t, std::unique_ptr<CaviarHandle>>& handles() {
    static std::map<uint32_t, std::unique_ptr<CaviarHandle>> h;
    return h;
}

}  // namespace

CaviarHandle* caviar_handle(uint32_t addr) {
    auto& h = handles()[addr];
    if (!h) h = std::make_unique<CaviarHandle>();
    return h.get();
}

void THISCALL CaviarHandle_ctor(CaviarHandle* self) { *self = CaviarHandle(); }

int32_t THISCALL CaviarHandle_load(CaviarHandle* self, const char* file) {
    if (!file) return 0x10;
    CaviarHandle_free(self);
    char resolved[1024];
    platform::resolve_path(file, resolved, sizeof(resolved));
    uint32_t size = 0;
    uint8_t* data = win32::read_file(resolved, &size);
    if (!data) {
        self->fd = -1;
        return 6;
    }
    CvrReader r;
    r.file.assign(data, data + size);
    win32::free_file(data);
    self->fd = 1;
    self->data = cvr_read(r);
    if (!self->data) return 0x17;
    self->fd = 0;
    return 0;
}

void THISCALL CaviarHandle_free(CaviarHandle* self) {
    delete self->data;
    self->data = nullptr;
}

// ---- scene -----------------------------------------------------------------------------------------

void THISCALL Scene_ctor(VoxelScene* self) {
    for (int32_t i = 0; i < 200; i++) self->objects[i] = VoxelObject{nullptr, nullptr, nullptr};
    self->f108 = 10000.0f;
    self->count = 0;
    self->f00 = 2.0f / 3.0f;  // 0x3F2AAAAB
}

int32_t THISCALL Scene_init(VoxelScene* self) {
    for (int32_t i = 0; i < 200; i++) self->objects[i] = VoxelObject{&self->s_flag[i], self->s_pos[i], self->s_mat[i]};
    self->count = 0;
    self->f108 = 100.0f;
    // (sin/cos of 1 and 0 for the shadow direction, overwritten below)
    self->v_pos[0] = self->v_pos[1] = self->v_pos[2] = 0.0f;
    self->v_scale = 1.0f;
    self->c_scale = 1.0f;
    self->c_zmin = 0.0f;
    self->c_zmax = 65535.0f;  // 0x477FFF00
    self->c_clip[0] = 0;
    self->c_clip[1] = 0;
    self->c_clip[2] = 0xFF;
    self->c_clip[3] = 0xFF;
    self->c_clip[4] = 0x100;
    self->c_clip[5] = 0x100;
    self->v_offset[0] = self->v_offset[1] = 0.0f;
    self->v_shift[0] = self->v_shift[1] = 0.0f;
    self->v_zoom = 1.0f;
    self->v_shadow_shift[0] = self->v_shadow_shift[1] = 0.0f;
    self->v_shadow_zoom[0] = self->v_shadow_zoom[1] = 1.0f;
    self->c_persp = 0.0f;
    self->c_near = -65535.0f;  // 0xC77FFF00
    self->c_far = 65535.0f;
    self->v_unused = 0;
    self->v_count = 0;
    self->v_light[0] = 0.0f;
    self->v_light[1] = 0.0f;
    self->v_light[2] = -1.0f;
    vec3_normalize(self->v_light, self->v_light);
    std::memset(self->v_mat, 0, sizeof(self->v_mat));
    self->v_mat[0] = self->v_mat[4] = self->v_mat[8] = 1.0f;
    self->v_shadow[0] = self->v_light[0];
    self->v_shadow[1] = self->v_light[1];
    self->v_shadow_z = 0.0f;
    std::memset(self->c_mat, 0, sizeof(self->c_mat));
    self->c_mat[0] = self->c_mat[4] = self->c_mat[8] = 1.0f;
    self->v_shadows = 1;
    self->v_shadow[2] = self->v_light[2];
    self->flags |= 1;
    return 0;
}

namespace {
// The body of the Scene_add variants: frame `frame` of every object of the model.
int32_t scene_add(VoxelScene* self, CaviarHandle* h, int32_t frame, const float* offset, const uint8_t* colors,
                  bool own_colors) {
    CaviarModel* m = h->data;
    if (!m) return 7;
    if (m->frame_count == 0) return 7;  // (the original divides by zero)
    const int32_t n = m->object_count;
    const int32_t f = frame % m->frame_count;
    for (int32_t i = 0; i < n; i++) {
        if (self->count == 200) return 1;
        // (Objects without frame data or mesh would crash the original; none in the game files.)
        const CaviarModel::Frames& fr = m->frames[(size_t)i];
        VoxelObject& o = self->objects[self->count];
        static const float zero[9] = {};
        const bool has = fr.flags.size() > (size_t)f && fr.pos.size() >= (size_t)f * 3 + 3 &&
                         fr.mat.size() >= (size_t)f * 9 + 9;
        *const_cast<uint8_t*>(o.flag) = has ? fr.flags[(size_t)f] : 0x80;
        const float* p = has ? &fr.pos[(size_t)f * 3] : zero;
        if (offset) vec3_add(p, offset, o.pos);
        else std::memcpy(o.pos, p, 12);
        std::memcpy(o.mat, has ? &fr.mat[(size_t)f * 9] : zero, 36);
        self->colors[self->count] = own_colors ? colors : m->shading();
        const CaviarModel::Mesh* mesh = (size_t)i < m->meshes.size() ? &m->meshes[(size_t)i] : nullptr;
        self->meshes[self->count] =
            VoxelMeshRef{mesh ? mesh->size : 0, mesh && !mesh->data.empty() ? mesh->data.data() : nullptr};
        self->count++;
    }
    return 0;
}
}  // namespace

int32_t THISCALL Scene_add(VoxelScene* self, CaviarHandle* model, int32_t frame) {
    if (!model) return 0x10;
    return scene_add(self, model, frame, nullptr, nullptr, false);
}

int32_t THISCALL Scene_add_offset(VoxelScene* self, CaviarHandle* model, const float* offset, int32_t frame) {
    if (!model || !offset) return 0x10;
    return scene_add(self, model, frame, offset, nullptr, false);
}

int32_t THISCALL Scene_add_colors_offset(VoxelScene* self, CaviarHandle* model, const uint8_t* colors,
                                         const float* offset, int32_t frame) {
    if (!model || !offset || !colors) return 0x10;
    return scene_add(self, model, frame, offset, colors, true);
}

int32_t THISCALL Scene_add_colors(VoxelScene* self, CaviarHandle* model, const uint8_t* colors, int32_t frame) {
    if (!model) return 0x10;
    return scene_add(self, model, frame, nullptr, colors, true);
}

void THISCALL Scene_set_camera(VoxelScene* self, const float* pos, const float* mat) {
    if (!pos || !mat) return;
    std::memcpy(self->c_pos, pos, 12);
    std::memcpy(self->c_mat, mat, 36);
}

void THISCALL Scene_set_rotation(VoxelScene* self, float a, float b, float c) {
    const float angles[3] = {a, b, c};
    mat3_from_euler(angles, self->v_mat);
}

void THISCALL Scene_set_scale(VoxelScene* self, float scale) { self->c_scale = scale; }

float THISCALL Scene_get_scale(VoxelScene* self) { return self->c_scale; }
