// Headless end-to-end check of the shell path: bridge -> worker thread -> x86 host -> frame buffer, and touch -> WM_MOUSE* -> guest.
//   pm3_headless <game_dir> [save_dir] [tap_x tap_y]     writes /tmp/pm3_frame_a.ppm (before) and /tmp/pm3_frame_b.ppm (after the tap)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "pm3/bridge.h"

static void save_ppm(const char* path, const std::vector<uint32_t>& fb) {
    FILE* f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6\n640 480\n255\n");
    for (uint32_t v : fb) { uint8_t rgb[3] = {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(rgb, 1, 3, f); } fclose(f);
}
static size_t diff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) { size_t n = 0; for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) n++; return n; }
static Pm3Bridge* g_b = nullptr; static std::vector<int16_t> g_audio;   // everything the shell would have played (PM3_WAV=<file> writes it as a 44.1 kHz stereo WAV)
static void wait_ms(int ms) {   // sleep while pulling audio like a real audio callback (441 frames every 10 ms)
    for (int t = 0; t < ms; t += 10) { int16_t buf[441 * 2]; pm3_bridge_pull_audio(g_b, buf, 441); g_audio.insert(g_audio.end(), buf, buf + 441 * 2); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
static void save_wav(const char* path) {
    FILE* f = fopen(path, "wb"); if (!f) return; uint32_t n = (uint32_t)(g_audio.size() * 2), rate = PM3_AUDIO_RATE, br = rate * 4; uint16_t ch = 2, bits = 16, ba = 4, fmt = 1; uint32_t c16 = 16, riff = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&c16, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(g_audio.data(), 2, g_audio.size(), f); fclose(f); }
static size_t nonblack(const std::vector<uint32_t>& a) { size_t n = 0; for (uint32_t v : a) if (v & 0xFFFFFF) n++; return n; }

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: pm3_headless <game_dir> [save_dir] [tap_x tap_y]\n"); return 2; }
    const char* save = argc > 2 ? argv[2] : "/tmp/pm3_save"; int tx = argc > 4 ? atoi(argv[3]) : -1, ty = argc > 4 ? atoi(argv[4]) : -1;
    if (argc == 4) { tx = atoi(argv[2]); ty = atoi(argv[3]); save = "/tmp/pm3_save"; }
    Pm3Bridge* b = pm3_bridge_create(); g_b = b; pm3_bridge_set_paths(b, argv[1], save); pm3_bridge_set_view_size(b, 640, 480, 0);
    if (!pm3_bridge_start_game(b)) { fprintf(stderr, "cannot start (PM3WIN.EXE missing?)\n"); return 1; }
    std::vector<uint32_t> a(640 * 480), c(640 * 480);
    wait_ms(6000); pm3_bridge_copy_frame(b, a.data());
    printf("before tap: %zu non-black pixels, running=%d\n", nonblack(a), pm3_bridge_game_running(b)); save_ppm("/tmp/pm3_frame_a.ppm", a);
    if (getenv("PM3_PAUSE")) {   // pause for 3 s: the frame must stay identical and the game must keep working afterwards
        pm3_bridge_set_paused(b, 1); std::this_thread::sleep_for(std::chrono::milliseconds(300)); pm3_bridge_copy_frame(b, c.data());
        wait_ms(3000); std::vector<uint32_t> d(640 * 480); pm3_bridge_copy_frame(b, d.data());
        printf("paused 3 s: %zu pixels changed while paused\n", diff(c, d)); pm3_bridge_set_paused(b, 0);
        wait_ms(2000); }
    if (tx >= 0) {
        printf("tap at (%d,%d)\n", tx, ty); pm3_bridge_touch_begin(b, 1, (float)tx, (float)ty); std::this_thread::sleep_for(std::chrono::milliseconds(100)); pm3_bridge_touch_end(b, 1, (float)tx, (float)ty);
        wait_ms(6000); pm3_bridge_copy_frame(b, c.data());
        printf("after tap: %zu non-black pixels, %zu pixels differ from before, running=%d\n", nonblack(c), diff(a, c), pm3_bridge_game_running(b)); save_ppm("/tmp/pm3_frame_b.ppm", c);
    }
    if (const char* wp = getenv("PM3_WAV")) { size_t loud = 0; for (int16_t v : g_audio) if (v > 64 || v < -64) loud++; save_wav(wp); printf("audio: %zu frames pulled, %zu loud samples -> %s\n", g_audio.size() / 2, loud, wp); }
    pm3_bridge_stop_game(b); printf("stopped, running=%d\n", pm3_bridge_game_running(b)); pm3_bridge_destroy(b); return 0;
}
