// FLC animations (see flic.h).
#include "ui/flic.h"

#include <algorithm>
#include <map>

#include "engine/crt.h"
#include "ui/colortables.h"

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }

bool flc_load(const std::string& name, Flc* out, const Palette* target) {
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
    for (int32_t i = 0; i < 256; i++) map[i] = target ? 0 : (uint8_t)i;
    auto remap = [&]() {
        for (int32_t i = 0; i < 256; i++)
            map[i] = !target ? (uint8_t)i : (uint8_t)Palette_get_nearest_palette_index(const_cast<Palette*>(target), pal[i][0] << 2 >> 2, pal[i][1], pal[i][2], 1);
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
    flc_load(name, &f, &GamePalette);
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

