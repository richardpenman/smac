// WVE movie decoder (wve.h): EA's container, TQI video and EA ADPCM sound. TQI frames: a header
// (width, height, quantiser, 4 bytes), then a bitstream read as byte-swapped 32-bit words of
// macroblocks of six MPEG-1 intra blocks (4 Y, Cb, Cr; DC differences per component from 0, AC
// coefficients of table B.14 in zigzag order), dequantised with the MPEG-1 default intra matrix
// scaled for the AAN transform by (215 - 2 x quantiser) x 5, and EA's integer IDCT. The sound: per
// SCDl block the sample count, both channels' last two samples, then 30-byte groups of 28 stereo
// samples (two filter coefficient pairs and shifts, then 4-bit residuals).
#include "ui/wve.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "platform/win32.h"
#include "ui/wve_tables.h"

namespace {

const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                             41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                             30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
const uint8_t kIntraMatrix[64] = {8,  16, 19, 22, 26, 27, 29, 34, 16, 16, 22, 24, 27, 29, 34, 37, 19, 22, 26, 27, 29, 34,
                                  34, 38, 22, 22, 26, 27, 29, 34, 37, 40, 22, 26, 27, 29, 32, 35, 40, 48, 26, 27, 29, 32,
                                  35, 40, 48, 58, 26, 27, 29, 34, 38, 46, 56, 69, 27, 29, 35, 38, 46, 56, 69, 83};

// Binary trees of prefix codes: pairs of children, >= 0 node, < 0 leaf -(symbol + 1).
struct Tree {
    std::vector<int16_t> n;
    void add(uint32_t code, int32_t len, int32_t sym) {
        if (n.empty()) n.assign(2, 0);
        int32_t cur = 0;
        for (int32_t b = len - 1; b >= 0; b--) {
            int16_t& slot = n[(size_t)(cur * 2 + ((code >> b) & 1))];
            if (!b) {
                slot = (int16_t)(-(sym + 1));
            } else {
                if (slot <= 0) {
                    slot = (int16_t)(n.size() / 2);
                    n.resize(n.size() + 2, 0);
                }
                cur = n[(size_t)(cur * 2 + ((code >> b) & 1))];
            }
        }
    }
};

struct Trees {
    Tree ac, dc_lum, dc_chrom;
    Trees() {
        for (int32_t i = 0; i < 113; i++) ac.add(wve_ac_code[i], wve_ac_len[i], i);
        static const char* const lum[12] = {"100",    "00",      "01",       "101",       "110",       "1110",
                                            "11110",  "111110",  "1111110",  "11111110",  "111111110", "111111111"};
        static const char* const chrom[12] = {"00",      "01",       "10",        "110",        "1110",       "11110",
                                              "111110",  "1111110",  "11111110",  "111111110",  "1111111110", "1111111111"};
        for (int32_t i = 0; i < 12; i++) {
            dc_lum.add((uint32_t)std::stoul(lum[i], nullptr, 2), (int32_t)std::strlen(lum[i]), i);
            dc_chrom.add((uint32_t)std::stoul(chrom[i], nullptr, 2), (int32_t)std::strlen(chrom[i]), i);
        }
    }
};
const Trees& trees() {
    static const Trees t;
    return t;
}

struct Bits {
    std::vector<uint8_t> buf;  // byte-swapped words
    size_t pos = 0, end = 0;
    uint32_t get(int32_t k) {
        uint32_t v = 0;
        for (int32_t i = 0; i < k; i++) {
            uint32_t bit = pos < end ? (buf[pos >> 3] >> (7 - (pos & 7))) & 1 : 0;
            v = v << 1 | bit;
            pos++;
        }
        return v;
    }
    uint32_t peek(int32_t k) {
        size_t p = pos;
        uint32_t v = get(k);
        pos = p;
        return v;
    }
    int32_t sym(const Tree& t) {
        int32_t cur = 0;
        for (int32_t i = 0; i < 20; i++) {
            if (pos >= end) return -1;
            int16_t next = t.n[(size_t)(cur * 2 + get(1))];
            if (next < 0) return -next - 1;
            if (next == 0) return -1;
            cur = next;
        }
        return -1;
    }
};

// EA's IDCT (8-bit output, >> 4), on columns then rows.
const int32_t ASQRT = 181, A4 = 669, A2 = 277, A5 = 196;
template <class Out>
void transform(const int32_t* s, int32_t stride, Out out) {
    const int32_t a1 = s[1 * stride] + s[7 * stride], a7 = s[1 * stride] - s[7 * stride];
    const int32_t a5 = s[5 * stride] + s[3 * stride], a3 = s[5 * stride] - s[3 * stride];
    const int32_t a2 = s[2 * stride] + s[6 * stride], a6 = (ASQRT * (s[2 * stride] - s[6 * stride])) >> 8;
    const int32_t a0 = s[0] + s[4 * stride], a4 = s[0] - s[4 * stride];
    const int32_t b0 = (((A4 - A5) * a7 - A5 * a3) >> 9) + a1 + a5;
    const int32_t b1 = (((A4 - A5) * a7 - A5 * a3) >> 9) + ((ASQRT * (a1 - a5)) >> 8);
    const int32_t b2 = (((A2 + A5) * a3 + A5 * a7) >> 9) + ((ASQRT * (a1 - a5)) >> 8);
    const int32_t b3 = ((A2 + A5) * a3 + A5 * a7) >> 9;
    out(0, a0 + a2 + a6 + b0);
    out(1, a4 + a6 + b1);
    out(2, a4 - a6 + b2);
    out(3, a0 - a2 - a6 + b3);
    out(4, a0 - a2 - a6 - b3);
    out(5, a4 - a6 - b2);
    out(6, a4 + a6 - b1);
    out(7, a0 + a2 + a6 - b0);
}

void idct_put(int32_t* block, uint8_t* dest, int32_t pitch) {
    int32_t temp[64];
    block[0] += 4;
    for (int32_t i = 0; i < 8; i++) {
        const int32_t* s = block + i;
        if (!(s[8] | s[16] | s[24] | s[32] | s[40] | s[48] | s[56])) {
            for (int32_t k = 0; k < 8; k++) temp[i + 8 * k] = s[0];
        } else {
            transform(s, 8, [&](int32_t k, int32_t v) { temp[i + 8 * k] = v; });
        }
    }
    for (int32_t r = 0; r < 8; r++) {
        uint8_t* d = dest + r * pitch;
        transform(temp + 8 * r, 1, [&](int32_t k, int32_t v) { d[k] = (uint8_t)std::max(0, std::min(255, v >> 4)); });
    }
}

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
int16_t le16s(const uint8_t* p) { return (int16_t)(p[0] | p[1] << 8); }
int16_t clip16(int32_t v) { return (int16_t)std::max(-32768, std::min(32767, v)); }

void decode_adpcm(const uint8_t* p, uint32_t len, std::vector<int16_t>* out) {
    static const int32_t kTable[20] = {0, 240, 460, 392, 0, 0, -208, -220, 0, 1, 3, 4, 7, 8, 10, 11, 0, -1, -3, -4};
    if (len < 12) return;
    uint32_t n = le32(p);
    int32_t cur_l = le16s(p + 4), prev_l = le16s(p + 6), cur_r = le16s(p + 8), prev_r = le16s(p + 10);
    const uint8_t* q = p + 12;
    const uint8_t* end = p + len;
    for (uint32_t b = 0; b < n / 28 && q + 30 <= end; b++) {
        int32_t c1l = kTable[q[0] >> 4], c2l = kTable[(q[0] >> 4) + 4];
        int32_t c1r = kTable[q[0] & 15], c2r = kTable[(q[0] & 15) + 4];
        int32_t shl = 20 - (q[1] >> 4), shr = 20 - (q[1] & 15);
        q += 2;
        for (int32_t k = 0; k < 28; k++, q++) {
            int32_t nl = (int32_t)(int8_t)(q[0] & 0xF0) >> 4;
            int32_t nr = (int32_t)(int8_t)(q[0] << 4) >> 4;
            nl = (int32_t)((nl * (1 << shl) + cur_l * c1l + prev_l * c2l + 0x80) >> 8);
            nr = (int32_t)((nr * (1 << shr) + cur_r * c1r + prev_r * c2r + 0x80) >> 8);
            prev_l = cur_l;
            cur_l = clip16(nl);
            prev_r = cur_r;
            cur_r = clip16(nr);
            out->push_back((int16_t)cur_l);
            out->push_back((int16_t)cur_r);
        }
    }
}

}  // namespace

bool WveMovie::open(const char* path) {
    uint32_t size = 0;
    uint8_t* d = win32::read_file(path, &size);
    if (!d) return false;
    data_.assign(d, d + size);
    win32::free_file(d);
    if (size < 8 || std::memcmp(data_.data(), "SCHl", 4)) return false;
    auto pcm = std::make_shared<platform::Pcm>();
    pcm->channels = 2;
    pcm->rate = 22050;
    for (uint32_t i = 0; i + 8 <= size;) {
        uint32_t len = le32(&data_[i + 4]);
        if (len < 8 || i + len > size) break;
        const uint8_t* tag = &data_[i];
        if (!std::memcmp(tag, "pIQT", 4) && len >= 16) {
            if (!width_) {
                width_ = data_[i + 8] | data_[i + 9] << 8;
                height_ = data_[i + 10] | data_[i + 11] << 8;
            }
            video_.push_back({i + 8, len - 8});
        } else if (!std::memcmp(tag, "SCDl", 4)) {
            decode_adpcm(&data_[i + 8], len - 8, &pcm->samples);
        } else if (!std::memcmp(tag, "SCEl", 4)) {
            break;
        }
        i += len;
    }
    if (!pcm->samples.empty()) sound_ = pcm;
    return width_ > 0 && height_ > 0 && !video_.empty();
}

bool WveMovie::decode(int32_t n, std::vector<uint32_t>* rgb) {
    if (n < 0 || n >= frames()) return false;
    const uint8_t* f = &data_[video_[(size_t)n].first];
    uint32_t len = video_[(size_t)n].second;
    int32_t w = f[0] | f[1] << 8, h = f[2] | f[3] << 8;
    if (w != width_ || h != height_ || len < 8) return false;
    // The quantiser matrix (tqi_calculate_qtable).
    int32_t qm[64];
    int64_t qscale = (215 - 2 * (int32_t)f[4]) * 5;
    qm[0] = (wve_inv_aanscales[0] * kIntraMatrix[0]) >> 11;
    for (int32_t i = 1; i < 64; i++) qm[i] = (int32_t)((wve_inv_aanscales[i] * kIntraMatrix[i] * qscale + 32) >> 14);
    Bits b;
    uint32_t words = (len - 8) / 4;
    b.buf.resize((size_t)words * 4);
    for (uint32_t i = 0; i < words; i++)
        for (int32_t k = 0; k < 4; k++) b.buf[i * 4 + k] = f[8 + i * 4 + 3 - k];
    b.end = b.buf.size() * 8;
    int32_t mbw = (w + 15) / 16, mbh = (h + 15) / 16;
    int32_t yp = mbw * 16, cp = mbw * 8;
    std::vector<uint8_t> Y((size_t)yp * mbh * 16, 0), U((size_t)cp * mbh * 8, 128), V((size_t)cp * mbh * 8, 128);
    const Trees& t = trees();
    int32_t last_dc[3] = {0, 0, 0};
    bool ok = true;
    for (int32_t my = 0; my < mbh && ok; my++)
        for (int32_t mx = 0; mx < mbw && ok; mx++) {
            int32_t block[6][64];
            std::memset(block, 0, sizeof(block));
            for (int32_t bn = 0; bn < 6 && ok; bn++) {
                int32_t comp = bn < 4 ? 0 : bn - 3;
                int32_t size = b.sym(comp ? t.dc_chrom : t.dc_lum);
                if (size < 0) {
                    ok = false;
                    break;
                }
                int32_t diff = 0;
                if (size) {
                    diff = (int32_t)b.get(size);
                    if (diff < (1 << (size - 1))) diff -= (1 << size) - 1;
                }
                last_dc[comp] += diff;
                int32_t* blk = block[bn];
                blk[0] = last_dc[comp] * qm[0];
                int32_t i = 0;
                for (;;) {
                    if (b.peek(2) == 2) {
                        b.get(2);
                        break;
                    }
                    int32_t s = b.sym(t.ac);
                    if (s < 0) {
                        ok = false;
                        break;
                    }
                    int32_t level;
                    if (s < 111) {
                        i += wve_ac_run[s] + 1;
                        if (i > 63) {
                            ok = false;
                            break;
                        }
                        int32_t j = kZigzag[i];
                        level = (wve_ac_level[s] * qm[j]) >> 4;
                        level = (level - 1) | 1;
                        if (b.get(1)) level = -level;
                        blk[j] = level;
                    } else if (s == 111) {
                        i += (int32_t)b.get(6) + 1;
                        level = (int8_t)b.get(8);
                        if (level == -128) level = (int32_t)b.get(8) - 256;
                        else if (level == 0) level = (int32_t)b.get(8);
                        if (i > 63) {
                            ok = false;
                            break;
                        }
                        int32_t j = kZigzag[i];
                        int32_t a = std::abs(level);
                        a = (a * qm[j]) >> 4;
                        a = (a - 1) | 1;
                        blk[j] = level < 0 ? -a : a;
                    } else {
                        break;  // end of block
                    }
                }
            }
            if (!ok) break;
            uint8_t* y0 = &Y[(size_t)(my * 16) * yp + mx * 16];
            idct_put(block[0], y0, yp);
            idct_put(block[1], y0 + 8, yp);
            idct_put(block[2], y0 + 8 * yp, yp);
            idct_put(block[3], y0 + 8 * yp + 8, yp);
            idct_put(block[4], &U[(size_t)(my * 8) * cp + mx * 8], cp);
            idct_put(block[5], &V[(size_t)(my * 8) * cp + mx * 8], cp);
        }
    // BT.601 (video range) to RGB.
    rgb->resize((size_t)w * h);
    for (int32_t y = 0; y < h; y++)
        for (int32_t x = 0; x < w; x++) {
            int32_t yy = (Y[(size_t)y * yp + x] - 16) * 298;
            int32_t u = U[(size_t)(y / 2) * cp + x / 2] - 128, v = V[(size_t)(y / 2) * cp + x / 2] - 128;
            int32_t r = (yy + 409 * v + 128) >> 8, g = (yy - 100 * u - 208 * v + 128) >> 8, bl = (yy + 516 * u + 128) >> 8;
            auto c = [](int32_t z) { return (uint32_t)std::max(0, std::min(255, z)); };
            (*rgb)[(size_t)y * w + x] = 0xFF000000u | c(r) << 16 | c(g) << 8 | c(bl);
        }
    return ok;
}
