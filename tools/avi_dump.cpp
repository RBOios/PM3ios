// avi_dump in.avi out_prefix [n_frames]  : decodes the first frames to PPM (compare with ffmpeg), prints stream info
#include "pm3/avi.h"
#include <cstdio>
#include <cstdlib>
int main(int c, char** v) {
    if (c < 3) return 1; pm3::AviMovie m; if (!m.open(fopen(v[1], "rb"))) { puts("open failed"); return 2; }
    printf("%dx%d %.2f fps, %d frames, audio %d Hz x%d, %zu samples (%.1f s)\n", m.width(), m.height(), m.fps(), m.frame_count(), m.audio_rate(), m.audio_channels(), m.audio().size(), (double)m.audio().size() / (m.audio_rate() * m.audio_channels()));
    int n = c > 3 ? atoi(v[3]) : 5;
    for (int i = 0; i < n; i++) { if (!m.decode_next()) { printf("decode failed at %d\n", i); return 3; } char p[256]; snprintf(p, sizeof p, "%s%04d.ppm", v[2], i); FILE* f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", m.width(), m.height()); fwrite(m.rgb().data(), 1, m.rgb().size(), f); fclose(f); }
}
