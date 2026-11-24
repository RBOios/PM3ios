#include "pm3/wav.h"

#include <algorithm>
#include <cstring>

namespace pm3 {
namespace {
const int kStep[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
                       88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
                       598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
                       3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
                       12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kIdx[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Ch { int pred, index; };
int16_t expand(Ch& c, int nib) {
    int step = kStep[c.index], diff = step >> 3;
    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    if (nib & 8) diff = -diff;
    c.pred = std::max(-32768, std::min(32767, c.pred + diff));
    c.index = std::max(0, std::min(88, c.index + kIdx[nib & 7]));
    return (int16_t)c.pred;
}
}  // namespace

bool wav_decode(const uint8_t* p, size_t n, Pcm& out) {
    if (n < 8 + 16 || std::memcmp(p, "fmt ", 4) != 0) return false;
    uint32_t fsz = u32(p + 4);
    if (fsz < 16 || 8 + (size_t)fsz > n) return false;
    uint16_t tag = u16(p + 8), ch = u16(p + 10), align = u16(p + 20), bits = u16(p + 22);
    uint32_t rate = u32(p + 12);
    if (ch < 1 || ch > 2 || rate == 0) return false;
    size_t pos = 8 + fsz + (fsz & 1);
    uint32_t fact = 0; bool haveFact = false;
    const uint8_t* data = nullptr; size_t dlen = 0;
    while (pos + 8 <= n) {
        uint32_t sz = u32(p + pos + 4);
        if (std::memcmp(p + pos, "fact", 4) == 0 && sz >= 4 && pos + 12 <= n) { fact = u32(p + pos + 8); haveFact = true; }
        if (std::memcmp(p + pos, "data", 4) == 0) {
            data = p + pos + 8; dlen = std::min<size_t>(sz, n - pos - 8);  // clamp: entry may be cut short
            break;
        }
        pos += 8 + (size_t)sz + (sz & 1);
    }
    if (!data) return false;
    out.rate = (int)rate; out.channels = ch; out.samples.clear();
    if (tag == 1) {
        if (bits != 16) return false;
        out.samples.resize(dlen / 2 / ch * ch);
        for (size_t i = 0; i < out.samples.size(); i++) out.samples[i] = (int16_t)u16(data + i * 2);
        return true;
    }
    if (tag == 0x11) {
        if (bits != 4 || align < 4u * ch + 1) return false;
        for (size_t b = 0; b + 4u * ch <= dlen; b += align) {
            size_t blen = std::min<size_t>(align, dlen - b);
            const uint8_t* blk = data + b;
            Ch st[2];
            for (int c = 0; c < ch; c++) {
                st[c].pred = (int16_t)u16(blk + c * 4);
                st[c].index = std::min<int>(blk[c * 4 + 2], 88);
                out.samples.push_back((int16_t)st[c].pred);
            }
            // After the headers the data comes in groups of 4 bytes (8 samples) per channel, channels interleaved.
            size_t q = 4u * ch;
            std::vector<int16_t> chunk[2];
            while (q + 4u * ch <= blen) {
                for (int c = 0; c < ch; c++, q += 4)
                    for (int k = 0; k < 4; k++) {
                        chunk[c].push_back(expand(st[c], blk[q + k] & 15));
                        chunk[c].push_back(expand(st[c], blk[q + k] >> 4));
                    }
                for (size_t s = 0; s < chunk[0].size(); s++)
                    for (int c = 0; c < ch; c++) out.samples.push_back(chunk[c][s]);
                chunk[0].clear(); chunk[1].clear();
            }
        }
        if (haveFact && (size_t)fact * ch < out.samples.size()) out.samples.resize((size_t)fact * ch);
        return true;
    }
    return false;
}

std::vector<int16_t> pcm_to_mono_rate(const Pcm& pcm, int rate) {
    std::vector<int16_t> out;
    if (pcm.channels < 1 || pcm.rate < 1 || rate < 1) return out;
    size_t frames = pcm.samples.size() / (size_t)pcm.channels;
    std::vector<int16_t> mono(frames);
    for (size_t i = 0; i < frames; i++) {
        int sum = 0;
        for (int c = 0; c < pcm.channels; c++) sum += pcm.samples[i * pcm.channels + c];
        mono[i] = (int16_t)(sum / pcm.channels);
    }
    if (rate == pcm.rate || frames == 0) return mono;
    size_t n = (size_t)((uint64_t)frames * (uint64_t)rate / (uint64_t)pcm.rate);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        uint64_t num = (uint64_t)i * (uint64_t)pcm.rate;   // position in source frames = num / rate
        size_t i0 = (size_t)(num / (uint64_t)rate);
        int fr = (int)(((num % (uint64_t)rate) * 256) / (uint64_t)rate);   // 0..255
        int a = mono[std::min(i0, frames - 1)], b = mono[std::min(i0 + 1, frames - 1)];
        out[i] = (int16_t)(a + ((b - a) * fr) / 256);
    }
    return out;
}

Bytes pcm_to_riff(const Pcm& pcm) {
    Bytes b; uint32_t dsz = (uint32_t)(pcm.samples.size() * 2);
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); };
    auto w16 = [&](uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); };
    b.insert(b.end(), {'R', 'I', 'F', 'F'}); w32(36 + dsz);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); w32(16);
    w16(1); w16((uint16_t)pcm.channels); w32((uint32_t)pcm.rate); w32((uint32_t)(pcm.rate * pcm.channels * 2));
    w16((uint16_t)(pcm.channels * 2)); w16(16);
    b.insert(b.end(), {'d', 'a', 't', 'a'}); w32(dsz);
    for (int16_t s : pcm.samples) w16((uint16_t)s);
    return b;
}

}  // namespace pm3
