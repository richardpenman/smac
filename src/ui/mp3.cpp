// Minimal MPEG audio layer III decoder (see mp3.h), written from the standard's description:
// frame headers, side information, the bit reservoir, scale factors (MPEG-1 and the MPEG-2 LSF
// partitions), Huffman decoding of the big values and count1 regions, requantisation, mid/side
// stereo, short block reordering, alias reduction, IMDCT with the block type windows, overlap and
// frequency inversion, and the polyphase synthesis filter bank. Floating point throughout.
#include "ui/mp3.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "ui/mp3_tables.h"

namespace {

const double kPi = 3.14159265358979323846;

// ---- Huffman trees built from the (code, length) tables ----
struct Tree {
    std::vector<int16_t> node;  // pairs: child for bit 0 / 1; >= 0 node index, < 0 leaf -(value + 1)
};

Tree build_tree(const uint16_t* code, const uint8_t* len, int32_t n) {
    Tree t;
    t.node.assign(2, 0);
    for (int32_t v = 0; v < n; v++) {
        int32_t cur = 0;
        for (int32_t b = len[v] - 1; b >= 0; b--) {
            int32_t bit = (code[v] >> b) & 1;
            int16_t& slot = t.node[(size_t)(cur * 2 + bit)];
            if (b == 0) {
                slot = (int16_t)(-(v + 1));
            } else {
                if (slot <= 0) {
                    slot = (int16_t)(t.node.size() / 2);
                    t.node.resize(t.node.size() + 2, 0);
                }
                cur = t.node[(size_t)(cur * 2 + bit)];
            }
        }
    }
    return t;
}

struct Tables {
    Tree big[32];  // by table_select (16..23 -> 16, 24..31 -> 24)
    int32_t xlen[32] = {};
    Tree quad[2];
    double pow43[8207];
    double D[512];
    double imdct_long[36][18], imdct_short[12][6];
    double win[4][36];
    double cs[8], ca[8];
    double N[64][32];
    Tables() {
        struct {
            int32_t id;
            const uint16_t* c;
            const uint8_t* l;
            int32_t xl;
        } src[] = {{1, mp3_h1_code, mp3_h1_len, 2},   {2, mp3_h2_code, mp3_h2_len, 3},
                   {3, mp3_h3_code, mp3_h3_len, 3},   {5, mp3_h5_code, mp3_h5_len, 4},
                   {6, mp3_h6_code, mp3_h6_len, 4},   {7, mp3_h7_code, mp3_h7_len, 6},
                   {8, mp3_h8_code, mp3_h8_len, 6},   {9, mp3_h9_code, mp3_h9_len, 6},
                   {10, mp3_h10_code, mp3_h10_len, 8}, {11, mp3_h11_code, mp3_h11_len, 8},
                   {12, mp3_h12_code, mp3_h12_len, 8}, {13, mp3_h13_code, mp3_h13_len, 16},
                   {15, mp3_h15_code, mp3_h15_len, 16}, {16, mp3_h16_code, mp3_h16_len, 16},
                   {24, mp3_h24_code, mp3_h24_len, 16}};
        for (auto& s : src) {
            big[s.id] = build_tree(s.c, s.l, s.xl * s.xl);
            xlen[s.id] = s.xl;
        }
        for (int32_t i = 17; i < 24; i++) big[i] = big[16], xlen[i] = 16;
        for (int32_t i = 25; i < 32; i++) big[i] = big[24], xlen[i] = 16;
        // count1 tables: the library's codes carry the sign bits; strip them.
        uint16_t qc[2][16];
        for (int32_t v = 0; v < 16; v++) {
            int32_t s = __builtin_popcount((unsigned)v);
            qc[0][v] = (uint16_t)(mp3_h32_code[v] >> s);
            qc[1][v] = (uint16_t)(mp3_h33_code[v] >> s);
        }
        quad[0] = build_tree(qc[0], mp3_h32_len, 16);
        quad[1] = build_tree(qc[1], mp3_h33_len, 16);
        for (int32_t i = 0; i < 8207; i++) pow43[i] = std::pow((double)i, 4.0 / 3.0);
        // D[i]: the window's first half and its mirror, the sign changing every 64 taps (as mpg123's
        // make_decode_tables builds it from the same 257 values).
        for (int32_t i = 0; i < 512; i++) {
            double v = mp3_window[i <= 256 ? i : 512 - i] / 65536.0;
            D[i] = ((i >> 6) & 1) ? -v : v;
        }
        for (int32_t i = 0; i < 36; i++)
            for (int32_t k = 0; k < 18; k++) imdct_long[i][k] = std::cos(kPi / 72 * (2 * i + 1 + 18) * (2 * k + 1));
        for (int32_t i = 0; i < 12; i++)
            for (int32_t k = 0; k < 6; k++) imdct_short[i][k] = std::cos(kPi / 24 * (2 * i + 1 + 6) * (2 * k + 1));
        for (int32_t i = 0; i < 36; i++) {
            win[0][i] = std::sin(kPi / 36 * (i + 0.5));
            win[1][i] = i < 18 ? win[0][i] : i < 24 ? 1.0 : i < 30 ? std::sin(kPi / 12 * (i - 18 + 0.5)) : 0.0;
            win[3][i] = i < 6 ? 0.0 : i < 12 ? std::sin(kPi / 12 * (i - 6 + 0.5)) : i < 18 ? 1.0 : win[0][i];
            win[2][i] = i < 12 ? std::sin(kPi / 12 * (i + 0.5)) : 0.0;
        }
        const double c[8] = {-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
        for (int32_t i = 0; i < 8; i++) {
            cs[i] = 1.0 / std::sqrt(1.0 + c[i] * c[i]);
            ca[i] = c[i] / std::sqrt(1.0 + c[i] * c[i]);
        }
        for (int32_t i = 0; i < 64; i++)
            for (int32_t k = 0; k < 32; k++) N[i][k] = std::cos((16 + i) * (2 * k + 1) * kPi / 64);
    }
};

const Tables& T() {
    static const Tables t;
    return t;
}

struct Bits {
    const uint8_t* p;
    size_t pos = 0, end = 0;  // in bits
    uint32_t get(int32_t n) {
        uint32_t v = 0;
        for (int32_t i = 0; i < n; i++) {
            uint32_t bit = pos < end ? (p[pos >> 3] >> (7 - (pos & 7))) & 1 : 0;
            v = v << 1 | bit;
            pos++;
        }
        return v;
    }
};

int32_t huff(Bits& b, const Tree& t, size_t limit) {
    int32_t cur = 0;
    for (;;) {
        if (b.pos >= limit) return -1;
        int16_t next = t.node[(size_t)(cur * 2 + b.get(1))];
        if (next < 0) return -next - 1;
        if (next == 0) return -1;
        cur = next;
    }
}

struct Granule {
    uint32_t part2_3, big_values, global_gain, sfc, window_switching, block_type, mixed;
    uint32_t table[3], subblock_gain[3], region0, region1, preflag, sf_scale, count1_table;
};

const int32_t kPretab[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0};
const uint8_t kSlen[16][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {3, 0}, {1, 1}, {1, 2}, {1, 3},
                              {2, 1}, {2, 2}, {2, 3}, {3, 1}, {3, 2}, {3, 3}, {4, 2}, {4, 3}};
const uint8_t kNrSfb[3][3][4] = {{{6, 5, 5, 5}, {9, 9, 9, 9}, {6, 9, 9, 9}},
                                 {{6, 5, 7, 3}, {9, 9, 12, 6}, {6, 9, 12, 6}},
                                 {{11, 10, 0, 0}, {18, 18, 0, 0}, {15, 18, 0, 0}}};

class Decoder {
public:
    std::vector<int16_t> out;
    int32_t channels = 0, rate = 0;

    bool frame(const uint8_t* f, size_t avail, size_t* size) {
        uint32_t h = (uint32_t)f[0] << 24 | f[1] << 16 | f[2] << 8 | f[3];
        if ((h >> 21) != 0x7FF) return false;
        int32_t ver = (h >> 19) & 3, layer = (h >> 17) & 3, prot = (h >> 16) & 1;
        int32_t bri = (h >> 12) & 15, sri = (h >> 10) & 3, pad = (h >> 9) & 1, mode = (h >> 6) & 3;
        int32_t mode_ext = (h >> 4) & 3;
        if (ver == 1 || layer != 1 || bri == 0 || bri == 15 || sri == 3) return false;
        bool lsf = ver != 3;
        static const int32_t kBr[2][15] = {{0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
                                           {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}};
        static const int32_t kSr[3] = {44100, 48000, 32000};
        int32_t sr = kSr[sri] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
        int32_t sfreq = sri + (ver == 3 ? 0 : ver == 2 ? 3 : 6);
        int32_t br = kBr[lsf][bri] * 1000;
        *size = (size_t)((lsf ? 72 : 144) * br / sr + pad);
        if (*size > avail || *size < 4) return false;
        int32_t nch = mode == 3 ? 1 : 2;
        if (!channels) {
            channels = nch;
            rate = sr;
        }
        if (nch != channels || sr != rate) return true;  // a stream change: skip the frame
        size_t side_len = lsf ? (nch == 1 ? 9 : 17) : (nch == 1 ? 17 : 32);
        size_t hdr = 4 + (prot ? 0 : 2);
        if (hdr + side_len > *size) return false;
        Bits b{f + hdr, 0, side_len * 8};
        uint32_t main_begin = b.get(lsf ? 8 : 9);
        b.get(lsf ? (nch == 1 ? 1 : 2) : (nch == 1 ? 5 : 3));
        uint32_t scfsi[2][4] = {};
        if (!lsf)
            for (int32_t ch = 0; ch < nch; ch++)
                for (int32_t i = 0; i < 4; i++) scfsi[ch][i] = b.get(1);
        int32_t ngr = lsf ? 1 : 2;
        Granule g[2][2] = {};
        for (int32_t gr = 0; gr < ngr; gr++)
            for (int32_t ch = 0; ch < nch; ch++) {
                Granule& q = g[gr][ch];
                q.part2_3 = b.get(12);
                q.big_values = b.get(9);
                q.global_gain = b.get(8);
                q.sfc = b.get(lsf ? 9 : 4);
                q.window_switching = b.get(1);
                if (q.window_switching) {
                    q.block_type = b.get(2);
                    q.mixed = b.get(1);
                    q.table[0] = b.get(5);
                    q.table[1] = b.get(5);
                    for (int32_t w = 0; w < 3; w++) q.subblock_gain[w] = b.get(3);
                    q.region0 = (q.block_type == 2 && !q.mixed) ? 8 : 7;
                    q.region1 = 36;
                } else {
                    for (int32_t i = 0; i < 3; i++) q.table[i] = b.get(5);
                    q.region0 = b.get(4);
                    q.region1 = b.get(3);
                }
                q.preflag = lsf ? 0 : b.get(1);
                q.sf_scale = b.get(1);
                q.count1_table = b.get(1);
            }
        // Main data with the bit reservoir.
        size_t main_len = *size - hdr - side_len;
        const uint8_t* main = f + hdr + side_len;
        if (main_begin > reservoir_.size()) {
            reservoir_.insert(reservoir_.end(), main, main + main_len);
            return true;  // not enough history (stream start): silence
        }
        std::vector<uint8_t> data(reservoir_.end() - main_begin, reservoir_.end());
        data.insert(data.end(), main, main + main_len);
        reservoir_.insert(reservoir_.end(), main, main + main_len);
        if (reservoir_.size() > 4096) reservoir_.erase(reservoir_.begin(), reservoir_.end() - 4096);
        Bits m{data.data(), 0, data.size() * 8};
        for (int32_t gr = 0; gr < ngr; gr++) {
            double xr[2][576];
            for (int32_t ch = 0; ch < nch; ch++) {
                size_t start = m.pos;
                Granule& q = g[gr][ch];
                if (lsf) scalefactors_lsf(m, q, ch);
                else scalefactors(m, q, gr, ch, scfsi[ch]);
                decode_spectrum(m, q, sfreq, start + q.part2_3, xr[ch], ch);
                m.pos = start + q.part2_3;
            }
            if (nch == 2 && mode == 1 && (mode_ext & 2)) {
                for (int32_t i = 0; i < 576; i++) {
                    double mm = xr[0][i], s = xr[1][i];
                    xr[0][i] = (mm + s) * 0.70710678118654752;
                    xr[1][i] = (mm - s) * 0.70710678118654752;
                }
            }
            double pcm[2][576];
            for (int32_t ch = 0; ch < nch; ch++) {
                hybrid(g[gr][ch], xr[ch], ch);
                synth(ch, pcm[ch]);
            }
            for (int32_t i = 0; i < 576; i++)
                for (int32_t ch = 0; ch < nch; ch++) {
                    double v = pcm[ch][i] * 32768.0;
                    out.push_back((int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : std::lrint(v)));
                }
        }
        return true;
    }

private:
    std::vector<uint8_t> reservoir_;
    int32_t sf_l_[2][22] = {};
    int32_t sf_s_[2][13][3] = {};
    double overlap_[2][32][18] = {};
    double v_[2][1024] = {};
    int32_t voff_[2] = {};
    // the hybrid filter's output: [subband][time]
    double sub_[2][32][18];

    void scalefactors(Bits& m, const Granule& q, int32_t gr, int32_t ch, const uint32_t* scfsi) {
        int32_t s1 = kSlen[q.sfc][0], s2 = kSlen[q.sfc][1];
        if (q.window_switching && q.block_type == 2) {
            if (q.mixed) {
                for (int32_t sfb = 0; sfb < 8; sfb++) sf_l_[ch][sfb] = (int32_t)m.get(s1);
                for (int32_t sfb = 3; sfb < 12; sfb++)
                    for (int32_t w = 0; w < 3; w++) sf_s_[ch][sfb][w] = (int32_t)m.get(sfb < 6 ? s1 : s2);
            } else {
                for (int32_t sfb = 0; sfb < 12; sfb++)
                    for (int32_t w = 0; w < 3; w++) sf_s_[ch][sfb][w] = (int32_t)m.get(sfb < 6 ? s1 : s2);
            }
            for (int32_t w = 0; w < 3; w++) sf_s_[ch][12][w] = 0;
        } else {
            static const int32_t band[5] = {0, 6, 11, 16, 21};
            for (int32_t i = 0; i < 4; i++) {
                if (gr == 1 && scfsi[i]) continue;
                for (int32_t sfb = band[i]; sfb < band[i + 1]; sfb++) sf_l_[ch][sfb] = (int32_t)m.get(i < 2 ? s1 : s2);
            }
            sf_l_[ch][21] = 0;
        }
    }

    void scalefactors_lsf(Bits& m, Granule& q, int32_t ch) {
        uint32_t sfc = q.sfc;
        int32_t slen[4], tbl;
        if (sfc < 400) {
            slen[0] = (int32_t)((sfc >> 4) / 5);
            slen[1] = (int32_t)((sfc >> 4) % 5);
            slen[2] = (int32_t)((sfc & 15) >> 2);
            slen[3] = (int32_t)(sfc & 3);
            tbl = 0;
        } else if (sfc < 500) {
            sfc -= 400;
            slen[0] = (int32_t)((sfc >> 2) / 5);
            slen[1] = (int32_t)((sfc >> 2) % 5);
            slen[2] = (int32_t)(sfc & 3);
            slen[3] = 0;
            tbl = 1;
        } else {
            sfc -= 500;
            slen[0] = (int32_t)(sfc / 3);
            slen[1] = (int32_t)(sfc % 3);
            slen[2] = slen[3] = 0;
            q.preflag = 1;
            tbl = 2;
        }
        int32_t bt = (q.window_switching && q.block_type == 2) ? (q.mixed ? 2 : 1) : 0;
        int32_t vals[54], n = 0;
        for (int32_t p = 0; p < 4; p++)
            for (int32_t i = 0; i < kNrSfb[tbl][bt][p]; i++) vals[n++] = (int32_t)m.get(slen[p]);
        int32_t k = 0;
        if (bt == 0) {
            for (int32_t sfb = 0; sfb < 21; sfb++) sf_l_[ch][sfb] = k < n ? vals[k++] : 0;
            sf_l_[ch][21] = 0;
        } else {
            int32_t first = 0;
            if (bt == 2) {
                for (int32_t sfb = 0; sfb < 6; sfb++) sf_l_[ch][sfb] = vals[k++];
                first = 3;
            }
            for (int32_t sfb = first; sfb < 12; sfb++)
                for (int32_t w = 0; w < 3; w++) sf_s_[ch][sfb][w] = k < n ? vals[k++] : 0;
            for (int32_t w = 0; w < 3; w++) sf_s_[ch][12][w] = 0;
        }
    }

    void decode_spectrum(Bits& m, const Granule& q, int32_t sfreq, size_t limit, double* xr, int32_t ch) {
        const Tables& t = T();
        int32_t is[576] = {};
        const int16_t* sl = mp3_sfb_long[sfreq];
        const int16_t* ss = mp3_sfb_short[sfreq];
        int32_t big = (int32_t)std::min<uint32_t>(q.big_values * 2, 576);
        int32_t r1, r2;
        if (q.window_switching && q.block_type == 2) {
            r1 = (sfreq == 8 ? 72 : 36);
            r2 = 576;
        } else if (q.window_switching) {
            r1 = sl[8];
            r2 = 576;
        } else {
            r1 = sl[std::min<uint32_t>(q.region0 + 1, 22)];
            r2 = sl[std::min<uint32_t>(q.region0 + q.region1 + 2, 22)];
        }
        int32_t i = 0;
        for (; i < big; i += 2) {
            int32_t tbl = (int32_t)q.table[i < r1 ? 0 : i < r2 ? 1 : 2];
            if (tbl == 0 || tbl == 4 || tbl == 14) {
                is[i] = is[i + 1] = 0;
                continue;
            }
            int32_t v = huff(m, t.big[tbl], limit);
            if (v < 0) break;
            int32_t xl = t.xlen[tbl];
            int32_t x = v / xl, y = v % xl;
            int32_t lb = mp3_linbits[tbl];
            if (lb && x == 15) x += (int32_t)m.get(lb);
            if (x && m.get(1)) x = -x;
            if (lb && y == 15) y += (int32_t)m.get(lb);
            if (y && m.get(1)) y = -y;
            is[i] = x;
            is[i + 1] = y;
        }
        while (i + 4 <= 576 && m.pos < limit) {
            int32_t v = huff(m, t.quad[q.count1_table], limit);
            if (v < 0) break;
            int32_t vals[4] = {(v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1};
            for (int32_t k = 0; k < 4; k++)
                if (vals[k] && m.get(1)) vals[k] = -vals[k];
            if (m.pos > limit) break;
            for (int32_t k = 0; k < 4; k++) is[i + k] = vals[k];
            i += 4;
        }
        // Requantisation. Long blocks: xr[line]. Short blocks: window w's line l at
        // xr[short_index(mixed, w, l)] (the long part of a mixed block: lines 0..35).
        double gain = std::pow(2.0, 0.25 * ((int32_t)q.global_gain - 210));
        double mult = q.sf_scale ? 1.0 : 0.5;
        auto value = [&](int32_t k, double expo) {
            int32_t a = std::abs(is[k]);
            if (!a) return 0.0;
            double v = (a < 8207 ? t.pow43[a] : std::pow((double)a, 4.0 / 3.0)) * gain * std::pow(2.0, expo);
            return is[k] < 0 ? -v : v;
        };
        for (int32_t j = 0; j < 576; j++) xr[j] = 0;
        bool shortb = q.window_switching && q.block_type == 2;
        int32_t k = 0;
        int32_t long_end = shortb ? (q.mixed ? 36 : 0) : 576;
        for (int32_t sfb = 0; sfb < 22 && k < long_end; sfb++)
            for (; k < sl[sfb + 1] && k < long_end; k++)
                xr[k] = value(k, -mult * (sf_l_[ch][sfb] + (q.preflag ? kPretab[sfb] : 0)));
        if (!shortb) return;
        for (int32_t sfb = q.mixed ? 3 : 0; sfb < 13; sfb++) {
            int32_t width = ss[sfb + 1] - ss[sfb];
            for (int32_t w = 0; w < 3; w++) {
                double expo = -2.0 * q.subblock_gain[w] - mult * sf_s_[ch][sfb][w];
                for (int32_t j = 0; j < width && k < 576; j++, k++) xr[short_index(q.mixed, w, ss[sfb] + j)] = value(k, expo);
            }
        }
    }

    static int32_t short_index(uint32_t mixed, int32_t w, int32_t line) {
        return mixed ? 36 + w * 180 + (line - 12) : w * 192 + line;
    }

    void hybrid(const Granule& q, double* xr, int32_t ch) {
        const Tables& t = T();
        bool shortb = q.window_switching && q.block_type == 2;
        if (!shortb) {
            for (int32_t sb = 1; sb < 32; sb++)
                for (int32_t i = 0; i < 8; i++) {
                    double a = xr[18 * sb - 1 - i], bb = xr[18 * sb + i];
                    xr[18 * sb - 1 - i] = a * t.cs[i] - bb * t.ca[i];
                    xr[18 * sb + i] = bb * t.cs[i] + a * t.ca[i];
                }
        } else if (q.mixed) {
            for (int32_t i = 0; i < 8; i++) {
                double a = xr[17 - i], bb = xr[18 + i];
                xr[17 - i] = a * t.cs[i] - bb * t.ca[i];
                xr[18 + i] = bb * t.cs[i] + a * t.ca[i];
            }
        }
        double z[36];
        for (int32_t sb = 0; sb < 32; sb++) {
            bool long_sb = !shortb || (q.mixed && sb < 2);
            if (long_sb) {
                int32_t bt = (q.window_switching && !shortb) ? (int32_t)q.block_type : 0;
                const double* x = xr + sb * 18;
                for (int32_t i = 0; i < 36; i++) {
                    double s = 0;
                    for (int32_t k = 0; k < 18; k++) s += x[k] * t.imdct_long[i][k];
                    z[i] = s * t.win[bt][i];
                }
            } else {
                for (int32_t i = 0; i < 36; i++) z[i] = 0;
                for (int32_t w = 0; w < 3; w++) {
                    const double* x = xr + short_index(q.mixed, w, sb * 6);
                    for (int32_t i = 0; i < 12; i++) {
                        double s = 0;
                        for (int32_t k = 0; k < 6; k++) s += x[k] * t.imdct_short[i][k];
                        z[6 + 6 * w + i] += s * t.win[2][i];
                    }
                }
            }
            for (int32_t i = 0; i < 18; i++) {
                double v = z[i] + overlap_[ch][sb][i];
                overlap_[ch][sb][i] = z[18 + i];
                if ((sb & 1) && (i & 1)) v = -v;
                sub_[ch][sb][i] = v;
            }
        }
    }

    void synth(int32_t ch, double* pcm) {
        const Tables& t = T();
        for (int32_t s = 0; s < 18; s++) {
            voff_[ch] = (voff_[ch] - 64) & 1023;
            double* V = v_[ch];
            for (int32_t i = 0; i < 64; i++) {
                double sum = 0;
                for (int32_t k = 0; k < 32; k++) sum += t.N[i][k] * sub_[ch][k][s];
                V[(voff_[ch] + i) & 1023] = sum;
            }
            for (int32_t j = 0; j < 32; j++) {
                double sum = 0;
                for (int32_t i = 0; i < 8; i++) {
                    sum += V[(voff_[ch] + 128 * i + j) & 1023] * t.D[64 * i + j];
                    sum += V[(voff_[ch] + 128 * i + 96 + j) & 1023] * t.D[64 * i + 32 + j];
                }
                pcm[s * 32 + j] = sum;
            }
        }
    }

};

}  // namespace

std::shared_ptr<const platform::Pcm> mp3_decode(const uint8_t* data, uint32_t size) {
    size_t p = 0;
    if (size >= 10 && !std::memcmp(data, "ID3", 3))
        p = 10 + ((size_t)(data[6] & 0x7F) << 21 | (data[7] & 0x7F) << 14 | (data[8] & 0x7F) << 7 | (data[9] & 0x7F));
    Decoder d;
    int32_t frames = 0;
    while (p + 4 <= size) {
        size_t len = 0;
        if (data[p] == 0xFF && (data[p + 1] & 0xE0) == 0xE0 && d.frame(data + p, size - p, &len)) {
            p += len;
            frames++;
            continue;
        }
        p++;
    }
    if (!frames || d.out.empty()) return nullptr;
    auto pcm = std::make_shared<platform::Pcm>();
    pcm->channels = d.channels;
    pcm->rate = d.rate;
    pcm->samples = std::move(d.out);
    return pcm;
}
