// midi_render in.mid out.wav [seconds] : renders with the built-in synth, prints RMS/peak
#include "pm3/midi.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
int main(int c, char** v) {
    if (c < 3) return 1; FILE* f = fopen(v[1], "rb"); if (!f) return 1; std::vector<uint8_t> d; int ch; while ((ch = fgetc(f)) != EOF) d.push_back((uint8_t)ch); fclose(f);
    pm3::MidiPlayer m(44100); if (!m.load(d.data(), d.size())) { puts("load failed"); return 2; }
    double sec = c > 3 ? atof(v[3]) : m.length_seconds() + 1; int n = (int)(sec * 44100); std::vector<float> b(n * 2, 0.f); m.play(false);
    for (int i = 0; i < n; i += 512) m.render(&b[i * 2], std::min(512, n - i));
    double e = 0, pk = 0; std::vector<int16_t> o(n * 2); for (int i = 0; i < n * 2; i++) { e += b[i] * b[i]; pk = std::max(pk, (double)std::fabs(b[i])); float x = std::max(-1.f, std::min(1.f, b[i])); o[i] = (int16_t)(x * 32767); }
    printf("len=%.1fs rms=%.4f peak=%.3f playing_after=%d\n", m.length_seconds(), std::sqrt(e / (n * 2)), pk, (int)m.playing());
    FILE* w = fopen(v[2], "wb"); uint32_t ds = n * 4, rt = 44100, br = rt * 4; uint16_t fm = 1, chn = 2, ba = 4, bps = 16; uint32_t fl = 16, rl = 36 + ds;
    fwrite("RIFF", 1, 4, w); fwrite(&rl, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w); fwrite(&fl, 4, 1, w); fwrite(&fm, 2, 1, w); fwrite(&chn, 2, 1, w); fwrite(&rt, 4, 1, w); fwrite(&br, 4, 1, w); fwrite(&ba, 2, 1, w); fwrite(&bps, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&ds, 4, 1, w); fwrite(o.data(), 2, n * 2, w); fclose(w);
}
