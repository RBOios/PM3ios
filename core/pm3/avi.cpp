#include "pm3/avi.h"

#include <algorithm>
#include <cstring>

namespace pm3 {

static uint32_t rb32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t rb16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t le16(const uint8_t* p) { return (uint32_t)p[0] | p[1] << 8; }

// ------------------------------------------------------------------ IMA ADPCM
std::vector<int16_t> ima_adpcm_decode(const uint8_t* d, size_t len, int ch, int align) {
    static const int8_t idx_adj[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
    static const int16_t steps[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    std::vector<int16_t> out; if (ch < 1 || ch > 2 || align < 4 * ch) return out;
    for (size_t b = 0; b + 4 * ch <= len; b += align) {
        size_t bl = std::min<size_t>(align, len - b); const uint8_t* p = d + b; int pred[2] = {0, 0}, idx[2] = {0, 0};
        for (int c = 0; c < ch; c++) { pred[c] = (int16_t)(p[4 * c] | (p[4 * c + 1] << 8)); idx[c] = std::min<int>(88, p[4 * c + 2]); }
        size_t ns = (bl - 4 * ch) * 2 / ch + 1, base = out.size(); out.resize(base + ns * ch);
        for (int c = 0; c < ch; c++) out[base + c] = (int16_t)pred[c];
        size_t pos = 4 * ch, k = 1;   // after the headers: groups of 4 bytes (8 samples) per channel, channels interleaved
        while (pos + 4 * ch <= bl) {
            for (int c = 0; c < ch; c++) for (int j = 0; j < 4; j++) { uint8_t by = p[pos + c * 4 + j];
                for (int h = 0; h < 2; h++) { int nib = h ? by >> 4 : by & 15, st = steps[idx[c]], df = st >> 3; if (nib & 1) df += st >> 2; if (nib & 2) df += st >> 1; if (nib & 4) df += st; if (nib & 8) pred[c] -= df; else pred[c] += df;
                    pred[c] = std::max(-32768, std::min(32767, pred[c])); idx[c] = std::max(0, std::min(88, idx[c] + idx_adj[nib]));
                    size_t si = k + (size_t)j * 2 + h; if (si < ns) out[base + si * ch + c] = (int16_t)pred[c]; } }
            pos += 4 * ch; k += 8; }
    }
    return out;
}

// ------------------------------------------------------------------ Cinepak
// chunk ids: 0x20 V4 full, 0x21 V4 update, 0x22 V1 full, 0x23 V1 update (+0x04 = 8-bit gray)
static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

void Cinepak::load_codebook(std::vector<Entry>& cb, int id, const uint8_t* p, size_t n) {
    bool gray = (id & 0x0400) != 0, update = (id & 0x0100) != 0; size_t esz = gray ? 4 : 6, pos = 0; uint32_t flags = 0, mask = 0;
    for (int i = 0; i < 256; i++) {
        if (update) { if (!(mask >>= 1)) { if (pos + 4 > n) return; flags = rb32(p + pos); pos += 4; mask = 0x80000000u; } if (!(flags & mask)) continue; }
        if (pos + esz > n) return;
        int y[4] = {p[pos], p[pos + 1], p[pos + 2], p[pos + 3]}, u = 0, v = 0; if (!gray) { u = (int8_t)p[pos + 4]; v = (int8_t)p[pos + 5]; } pos += esz;
        for (int k = 0; k < 4; k++) { if (gray) { cb[i].c[k][0] = cb[i].c[k][1] = cb[i].c[k][2] = (uint8_t)y[k]; }
            else { cb[i].c[k][0] = clamp8(y[k] + v * 2); cb[i].c[k][1] = clamp8(y[k] - (u >> 1) - v); cb[i].c[k][2] = clamp8(y[k] + u * 2); } }
    }
}

bool Cinepak::decode_vectors(Strip& s, int id, const uint8_t* p, size_t n, int y0, int y1) {
    size_t pos = 0; uint32_t flags = 0, mask = 0; bool inter = (id & 0x0100) != 0 && id != 0x3200, v1only = id == 0x3200;
    auto put2 = [&](int x, int y, const Entry& e) { for (int k = 0; k < 4; k++) { int px = x + (k & 1), py = y + (k >> 1); if (px >= w_ || py >= h_) continue; std::memcpy(&rgb_[((size_t)py * w_ + px) * 3], e.c[k], 3); } };
    auto bit = [&]() -> int { if (!(mask >>= 1)) { if (pos + 4 > n) return -1; flags = rb32(p + pos); pos += 4; mask = 0x80000000u; } return (flags & mask) ? 1 : 0; };
    for (int y = y0; y < y1; y += 4) for (int x = 0; x < w_; x += 4) {
        bool v4 = false;
        if (!v1only) {
            if (inter) { int b = bit(); if (b < 0) return false; if (!b) continue; }   // inter: first bit 0 = block unchanged
            int b = bit(); if (b < 0) return false; v4 = b == 1;
        }
        if (v4) { if (pos + 4 > n) return false; for (int q = 0; q < 4; q++) put2(x + (q & 1) * 2, y + (q >> 1) * 2, s.v4[p[pos + q]]); pos += 4; }
        else { if (pos + 1 > n) return false; const Entry& e = s.v1[p[pos++]]; for (int q = 0; q < 4; q++) { Entry t; for (int k = 0; k < 4; k++) std::memcpy(t.c[k], e.c[q], 3); put2(x + (q & 1) * 2, y + (q >> 1) * 2, t); } }
    }
    return true;
}

bool Cinepak::decode(const uint8_t* d, size_t n) {
    if (n < 10) return false;
    int w = (int)rb16(d + 4), h = (int)rb16(d + 6), ns = (int)rb16(d + 8); if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    if (w != w_ || h != h_) { w_ = w; h_ = h; rgb_.assign((size_t)w * h * 3, 0); }
    if ((int)strips_.size() < ns) strips_.resize(ns);
    size_t pos = 10; int ytop = 0;
    for (int si = 0; si < ns; si++) {
        if (pos + 12 > n) return false;
        size_t ssz = rb16(d + pos + 2); int sy1 = (int)rb16(d + pos + 4), sy2 = (int)rb16(d + pos + 8); size_t end = std::min(n, pos + ssz); size_t q = pos + 12; pos = end;
        if (si > 0 && !(d[0] & 1)) { strips_[si].v1 = strips_[si - 1].v1; strips_[si].v4 = strips_[si - 1].v4; }   // frame flag bit 0 clear: a strip starts from the previous strip's codebooks; set: each strip keeps its own from the previous frame
        int ybot = ytop + std::max(0, sy2 - sy1); if (ybot > h_) ybot = h_; Strip& st = strips_[si];
        while (q + 4 <= end) {
            int id = (int)rb16(d + q); size_t cs = rb16(d + q + 2); if (cs < 4) break; size_t cend = std::min(end, q + cs); const uint8_t* cp = d + q + 4; size_t cl = cend - q - 4;
            switch (id & 0xFF00) {
                case 0x2000: case 0x2100: case 0x2200: case 0x2300: case 0x2400: case 0x2500: case 0x2600: case 0x2700:
                    load_codebook((id & 0x0200) ? st.v1 : st.v4, id, cp, cl); break;
                case 0x3000: case 0x3100: case 0x3200: if (!decode_vectors(st, id, cp, cl, ytop, ybot)) return false; break;
                default: break;
            }
            q = cend;
        }
        ytop = ybot;
    }
    return true;
}

// ------------------------------------------------------------------ AVI
bool AviMovie::open(FILE* f) {
    if (!f) return false;
    f_ = f; uint8_t h[12]; fseek(f, 0, SEEK_SET);
    if (fread(h, 1, 12, f) != 12 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "AVI ", 4)) return false;
    uint32_t riff_end = 8 + le32(h + 4); std::vector<uint8_t> abuf; int atag = 0, align = 0; bool have_vid = false; uint32_t period_us = 0; int cur_type = 0;
    // walk the chunk tree: LIST hdrl (avih, LIST strl: strh + strf), LIST movi (00dc / 01wb chunks, maybe inside 'rec ' lists)
    struct Frame { uint32_t end; }; std::vector<uint32_t> ends; uint32_t pos = 12;
    while (pos + 8 <= riff_end) {
        while (!ends.empty() && pos >= ends.back()) ends.pop_back();
        uint8_t ch[12]; fseek(f, pos, SEEK_SET); if (fread(ch, 1, 8, f) != 8) break; uint32_t sz = le32(ch + 4), body = pos + 8;
        if (!std::memcmp(ch, "LIST", 4) || !std::memcmp(ch, "RIFF", 4)) { if (fread(ch + 8, 1, 4, f) != 4) break; ends.push_back(body + sz); pos = body + 4; continue; }
        std::vector<uint8_t> b; auto rd = [&](uint32_t n) { b.resize(n); fseek(f, body, SEEK_SET); if (fread(b.data(), 1, n, f) != n) b.clear(); };
        if (!std::memcmp(ch, "avih", 4)) { rd(std::min<uint32_t>(sz, 56)); if (b.size() >= 4) period_us = le32(b.data()); }
        else if (!std::memcmp(ch, "strh", 4)) { rd(std::min<uint32_t>(sz, 56)); if (b.size() >= 4) cur_type = !std::memcmp(b.data(), "vids", 4) ? 1 : !std::memcmp(b.data(), "auds", 4) ? 2 : 0;
            if (cur_type == 1 && b.size() >= 28) { uint32_t sc = le32(b.data() + 20), rt = le32(b.data() + 24); if (sc && rt) fps_ = (double)rt / sc; } }
        else if (!std::memcmp(ch, "strf", 4)) { rd(std::min<uint32_t>(sz, 64));
            if (cur_type == 1 && b.size() >= 24) { w_ = (int)le32(b.data() + 4); h_ = (int)le32(b.data() + 8); if (h_ < 0) h_ = -h_; have_vid = std::memcmp(b.data() + 16, "cvid", 4) == 0; }
            else if (cur_type == 2 && b.size() >= 16) { atag = (int)le16(b.data()); ach_ = (int)le16(b.data() + 2); arate_ = (int)le32(b.data() + 4); align = (int)le16(b.data() + 12); } }
        else if (ch[2] == 'd' && (ch[3] == 'c' || ch[3] == 'b') && ch[0] >= '0' && ch[0] <= '9') vid_.push_back({body, sz});
        else if (ch[2] == 'w' && ch[3] == 'b' && ch[0] >= '0' && ch[0] <= '9') aud_.push_back({body, sz});
        pos = body + sz + (sz & 1);
    }
    if (!have_vid || vid_.empty() || w_ <= 0 || h_ <= 0) return false;
    if (period_us && fps_ <= 0) fps_ = 1e6 / period_us;
    if (atag == 0x11 && align > 0) {   // decode the soundtrack once (about 7 MB of PCM for the opening)
        for (auto& a : aud_) { std::vector<uint8_t> b(a.size); fseek(f, a.off, SEEK_SET); if (fread(b.data(), 1, a.size, f) != a.size) break; abuf.insert(abuf.end(), b.begin(), b.end()); }
        audio_ = ima_adpcm_decode(abuf.data(), abuf.size(), ach_, align);
    } else if (atag == 1) { for (auto& a : aud_) { std::vector<uint8_t> b(a.size); fseek(f, a.off, SEEK_SET); if (fread(b.data(), 1, a.size, f) != a.size) break; for (size_t i = 0; i + 1 < b.size(); i += 2) audio_.push_back((int16_t)(b[i] | (b[i + 1] << 8))); } }
    return true;
}

bool AviMovie::decode_next() {
    if (next_ >= (int)vid_.size()) return false;
    const Ch& c = vid_[next_++]; std::vector<uint8_t> b(c.size); fseek(f_, c.off, SEEK_SET); if (fread(b.data(), 1, c.size, f_) != c.size) return false;
    if (c.size == 0) return true;   // dropped frame: repeat the previous one
    return cp_.decode(b.data(), b.size());
}

}  // namespace pm3
