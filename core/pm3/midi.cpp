#include "pm3/midi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pm3 {

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }

bool MidiPlayer::load(const uint8_t* d, size_t n) {
    if (n < 14 || std::memcmp(d, "MThd", 4) != 0) return false;
    uint32_t hl = be32(d + 4); if (hl < 6 || 8 + hl > n) return false;
    uint32_t ntrk = be16(d + 10), div = be16(d + 12); if (div == 0 || (div & 0x8000)) return false;   // SMPTE time is not used by this game
    std::vector<Ev> ev; size_t p = 8 + hl;
    for (uint32_t t = 0; t < ntrk && p + 8 <= n; t++) {
        uint32_t len = be32(d + p + 4); size_t b = p + 8, e = std::min(n, b + (size_t)len); p = b + len;
        if (std::memcmp(d + b - 8, "MTrk", 4) != 0) continue;
        uint32_t tick = 0; uint8_t run = 0; size_t q = b;
        auto vlq = [&](uint32_t* out) { uint32_t v = 0; for (int i = 0; i < 4 && q < e; i++) { uint8_t c = d[q++]; v = (v << 7) | (c & 0x7F); if (!(c & 0x80)) break; } *out = v; };
        while (q < e) {
            uint32_t dt; vlq(&dt); tick += dt; if (q >= e) break;
            uint8_t st = d[q];
            if (st == 0xFF) { if (q + 2 > e) break; uint8_t type = d[q + 1]; q += 2; uint32_t l; vlq(&l); if (q + l > e) break;
                if (type == 0x51 && l == 3) ev.push_back({tick, 0, 0, 0, (uint32_t)d[q] << 16 | d[q + 1] << 8 | d[q + 2]});
                q += l; if (type == 0x2F) break; continue; }
            if (st == 0xF0 || st == 0xF7) { q++; uint32_t l; vlq(&l); q += l; continue; }
            if (st & 0x80) { run = st; q++; } else if (!run) break;
            int hi = run & 0xF0; int need = (hi == 0xC0 || hi == 0xD0) ? 1 : 2; if (q + need > e) break;
            uint8_t d1 = d[q], d2 = need == 2 ? d[q + 1] : 0; q += need;
            ev.push_back({tick, run, d1, d2, 0});
        }
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick; });
    double t = 0; uint32_t lt = 0, tempo = 500000;   // song length for tests / UI
    for (auto& e : ev) { t += (double)(e.tick - lt) * tempo / div / 1e6; lt = e.tick; if (e.st == 0) tempo = e.tempo; }
    std::lock_guard<std::mutex> lk(mu_); ev_ = std::move(ev); div_ = (int)div; length_s_ = t; playing_ = false; return !ev_.empty();
}

void MidiPlayer::reset_state() { for (auto& c : ch_) c = Chan(); for (auto& v : v_) v.on = false; tempo_ = 500000; tick_ = 0; pos_ = 0; tail_ = 0; }
void MidiPlayer::play(bool loop) { std::lock_guard<std::mutex> lk(mu_); if (ev_.empty()) return; reset_state(); loop_ = loop; playing_ = true; }
void MidiPlayer::stop() { std::lock_guard<std::mutex> lk(mu_); playing_ = false; for (auto& v : v_) v.on = false; }
bool MidiPlayer::playing() { std::lock_guard<std::mutex> lk(mu_); return playing_; }
void MidiPlayer::set_volume(float v) { std::lock_guard<std::mutex> lk(mu_); volume_ = v < 0 ? 0 : (v > 1 ? 1 : v); }

void MidiPlayer::note_on(int ch, int note, int vel) {
    if (vel == 0) { note_off(ch, note); return; }
    Voice* f = nullptr; for (auto& v : v_) if (!v.on) { f = &v; break; }
    if (!f) { float best = 1e9f; for (auto& v : v_) { float s = v.released ? v.rel_env - 10.f : v.age * 0.01f; if (s < best) { best = s; f = &v; } } }   // steal the quietest/oldest
    Voice& v = *f; v = Voice(); v.on = true; v.ch = ch; v.note = note; v.prog = ch_[ch].prog; v.vel = vel / 127.f; v.drum = ch == 9; v.freq = 440.f * std::pow(2.f, (note - 69) / 12.f); v.seed = 12345u + note * 7919u + ch;
}
void MidiPlayer::note_off(int ch, int note) {
    for (auto& v : v_) if (v.on && !v.released && v.ch == ch && v.note == note && !v.drum) { if (ch_[ch].sustain) v.held = true; else { v.released = true; v.rel_env = v.env; } }
}
void MidiPlayer::all_off(bool hard) { for (auto& v : v_) if (v.on) { if (hard) v.on = false; else { v.released = true; v.rel_env = v.env; } } }

void MidiPlayer::dispatch(const Ev& e) {
    if (e.st == 0) { tempo_ = e.tempo; return; }
    int ch = e.st & 15; Chan& c = ch_[ch];
    switch (e.st & 0xF0) {
        case 0x90: note_on(ch, e.d1, e.d2); break;
        case 0x80: note_off(ch, e.d1); break;
        case 0xC0: c.prog = e.d1; break;
        case 0xE0: c.bend = ((e.d2 << 7 | e.d1) - 8192) / 8192.f * 2.f; break;   // +-2 semitones
        case 0xB0:
            if (e.d1 == 7) c.vol = e.d2 / 127.f; else if (e.d1 == 11) c.expr = e.d2 / 127.f; else if (e.d1 == 10) c.pan = e.d2 / 127.f;
            else if (e.d1 == 64) { bool on = e.d2 >= 64; if (c.sustain && !on) for (auto& v : v_) if (v.on && v.ch == ch && v.held) { v.held = false; v.released = true; v.rel_env = v.env; } c.sustain = on; }
            else if (e.d1 == 123 || e.d1 == 120) { for (auto& v : v_) if (v.on && v.ch == ch) { v.released = true; v.rel_env = v.env; } }
            else if (e.d1 == 121) { c.vol = 100 / 127.f; c.expr = 1.f; c.bend = 0; c.sustain = false; }
            break;
        default: break;
    }
}

static inline float saw(float p) { return 2.f * p - 1.f; }
static inline float tri(float p) { return 4.f * std::fabs(p - 0.5f) - 1.f; }
static inline float pulse(float p, float w) { return p < w ? 1.f : -1.f; }

void MidiPlayer::render(float* out, int frames) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!playing_ && tail_ <= 0) { bool any = false; for (auto& v : v_) any |= v.on; if (!any) return; }
    const float inv = 1.f / rate_; const int CH = 32;   // control-rate chunk: events are dispatched every CH samples
    for (int base = 0; base < frames; base += CH) {
        int n = std::min(CH, frames - base);
        if (playing_) {
            tick_ += (double)n * div_ * 1e6 / ((double)tempo_ * rate_);   // ticks per sample = div * 1e6 / (tempo_us * rate)
            while (pos_ < ev_.size() && ev_[pos_].tick <= tick_) dispatch(ev_[pos_++]);
            if (pos_ >= ev_.size()) { if (loop_) { all_off(false); pos_ = 0; tick_ = 0; tempo_ = 500000; for (auto& c : ch_) { c.sustain = false; } } else { playing_ = false; all_off(false); tail_ = rate_ / 2; } }
        } else if (tail_ > 0) tail_ -= n;
        for (auto& v : v_) {
            if (!v.on) continue;
            const Chan& c = ch_[v.ch];
            float bend = std::pow(2.f, c.bend / 12.f), gain = volume_ * 0.22f * v.vel * c.vol * c.expr; float pl = std::sqrt(1.f - c.pan), pr = std::sqrt(c.pan);
            for (int i = 0; i < n; i++) {
                float s = 0.f, a = 0.f; int p = v.prog; float t = v.age * inv; v.age += 1.f;
                if (v.drum) {   // synthesized percussion (GM drum map families)
                    int k = v.note; v.seed = v.seed * 1664525u + 1013904223u; float nz = ((v.seed >> 9) & 0xFFFF) / 32768.f - 1.f;
                    float tail = (k == 35 || k == 36) ? 0.28f : (k == 38 || k == 40 || k == 37 || k == 39) ? 0.2f : (k == 42 || k == 44) ? 0.06f : (k == 46) ? 0.3f : (k >= 49 && k <= 59 && k != 56) ? 0.9f : (k >= 41 && k <= 50) ? 0.3f : 0.08f;
                    float e = std::exp(-t * 5.f / tail); if (t > tail * 1.6f) { v.on = false; break; }
                    if (k == 35 || k == 36) { float f = 50.f + 110.f * std::exp(-t * 30.f); v.ph += f * inv; s = std::sin(6.2831853f * v.ph) * 1.4f; }
                    else if (k == 38 || k == 40) { v.ph += 190.f * inv; s = 0.5f * std::sin(6.2831853f * v.ph) * std::exp(-t * 40.f) + 0.8f * nz; }
                    else if (k >= 41 && k <= 50 && k != 42 && k != 44 && k != 46 && k != 49) { float f = 70.f + (k - 41) * 14.f; v.ph += (f * (1.f + 0.6f * std::exp(-t * 18.f))) * inv; s = std::sin(6.2831853f * v.ph) * 1.1f; }
                    else { v.lp = nz - 0.6f * v.lp; s = (nz - v.lp * 0.5f) * 0.55f; }   // hats / cymbals / misc: high-passed noise
                    a = e;
                    float m = s * a * gain * 1.5f; out[2 * (base + i)] += m * pl; out[2 * (base + i) + 1] += m * pr; continue;
                }
                float f = v.freq * bend; float atk = 0.005f, dec = 1.f, sus = 1.f; bool perc = false;
                if (p < 8) { atk = 0.002f; dec = 0.9f; sus = 0.f; perc = true; }                   // pianos
                else if (p < 16) { atk = 0.001f; dec = 0.5f; sus = 0.f; perc = true; }             // chromatic percussion
                else if (p < 24) { atk = 0.01f; }                                                  // organs
                else if (p < 32) { atk = 0.002f; dec = 0.6f; sus = 0.1f; }                         // guitars
                else if (p < 40) { atk = 0.004f; dec = 0.5f; sus = 0.5f; }                         // basses
                else if (p < 56) { atk = 0.09f; }                                                  // strings / ensembles
                else if (p < 72) { atk = 0.03f; }                                                  // brass / reeds
                else if (p < 80) { atk = 0.05f; }                                                  // pipes
                else if (p < 96) { atk = 0.02f; }                                                  // synth lead / pad
                else { atk = 0.002f; dec = 0.7f; sus = 0.f; perc = true; }
                float e = t < atk ? t / atk : 1.f; if (perc) e *= std::exp(-t / dec); else if (sus < 1.f) e *= sus + (1.f - sus) * std::exp(-t / dec);
                if (v.released) { v.rel_env *= 0.9992f - (p >= 40 && p < 56 ? 0.f : 0.0006f); e = v.rel_env; if (e < 0.0008f) { v.on = false; break; } } else v.env = e;
                v.ph += f * inv; if (v.ph >= 1.f) v.ph -= 1.f;
                if (p < 8) { s = std::sin(6.2831853f * v.ph) + 0.4f * std::sin(12.566371f * v.ph) * std::exp(-t * 3.f) + 0.15f * std::sin(18.849556f * v.ph); }
                else if (p < 16) { s = std::sin(6.2831853f * v.ph) + 0.5f * std::sin(6.2831853f * v.ph * 3.f) * std::exp(-t * 6.f); }
                else if (p < 24) { s = 0.6f * std::sin(6.2831853f * v.ph) + 0.3f * std::sin(12.566371f * v.ph) + 0.2f * std::sin(18.849556f * v.ph); }
                else if (p < 32) { s = saw(v.ph) * 0.5f + tri(v.ph) * 0.5f; }
                else if (p < 40) { s = tri(v.ph) * 0.8f + pulse(v.ph, 0.5f) * 0.2f; }
                else if (p < 56) { v.ph2 += f * 1.006f * inv; if (v.ph2 >= 1.f) v.ph2 -= 1.f; s = 0.5f * (saw(v.ph) + saw(v.ph2)) * 0.7f; }
                else if (p < 64) { s = saw(v.ph) * 0.7f + pulse(v.ph, 0.3f) * 0.2f; }
                else if (p < 80) { s = pulse(v.ph, 0.25f + 0.1f * std::sin(t * 5.f)) * 0.5f + tri(v.ph) * 0.3f; }
                else if (p < 88) { s = pulse(v.ph, 0.5f) * 0.5f + saw(v.ph) * 0.3f; }
                else if (p < 96) { v.ph2 += f * 1.004f * inv; if (v.ph2 >= 1.f) v.ph2 -= 1.f; s = 0.5f * (saw(v.ph) + saw(v.ph2)) * 0.6f; }
                else { s = std::sin(6.2831853f * v.ph) + 0.3f * std::sin(6.2831853f * v.ph * 2.76f) * std::exp(-t * 4.f); }
                float m = s * e * gain; out[2 * (base + i)] += m * pl; out[2 * (base + i) + 1] += m * pr;
            }
        }
    }
}

}  // namespace pm3
