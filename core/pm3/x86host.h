// Runs the original PM3WIN.EXE inside the x86 interpreter with the Win32/DirectDraw/DirectSound HLE (x86host.cpp).
// Without hooks it is the batch tool used by tools/x86_boot (runs N steps, prints a trace). With hooks the host runs in slices so a
// platform shell can drive it (iOS: on a worker thread, the Metal view reads the frame buffer owned by the bridge).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace pm3 {

class MidiPlayer;

struct X86Frame {
    const uint8_t* pixels = nullptr;   // primary surface memory (valid only during the callback)
    int width = 0, height = 0, pitch = 0, bpp = 0;   // bpp 16 = RGB565 (the mode the HLE reports), 32 = XRGB8888
    bool idle = false;                 // the guest spent the slice polling for messages (a shell may sleep to save battery)
    uint64_t steps = 0;                // total guest instructions so far
};

struct X86Hooks {
    uint64_t slice_steps = 200000;                          // guest instructions per slice (the shell gets control after each)
    std::string save_dir;                                   // empty: <game_dir>/_save
    std::function<uint32_t()> now_ms;                       // wall clock for timeGetTime(); empty: guest steps / 1000 (deterministic)
    std::function<bool(const X86Frame&)> on_frame;          // after every slice; return false to stop the guest
    std::function<bool(int* type, int* x, int* y)> poll_mouse;
    // waveOut output of the guest: raw PCM exactly as the game wrote it (bits 8 = unsigned, 16 = signed little endian). Called from the guest thread.
    std::function<void(const uint8_t* data, uint32_t bytes, int channels, int rate, int bits)> on_pcm;
    // Same data as on_pcm plus the kind: voice = true for the game's IMA ADPCM streams (voices), false for plain PCM (sound effects). When set, on_pcm is not called.
    std::function<void(const uint8_t* data, uint32_t bytes, int channels, int rate, int bits, bool voice)> on_pcm_ex;
    MidiPlayer* midi = nullptr;                             // BGM: PMMIDI.dll's MIDI_* are served by this player (null: they stay silent stubs)
    bool verbose = false;                                   // false: the host's trace output (API log, HLE notes) is silenced (env PM3_VERBOSE=1 turns it on)   // type 0 move, 1 left down, 2 left up, 3 right down, 4 right up; x/y in 640x480 game pixels
};

// argv[1] = game directory (contains PM3WIN.EXE), argv[2] = max steps (0 = unlimited when hooks are given). Returns 0 on a clean stop.
int x86host_main(int argc, char** argv, const X86Hooks* hooks);

}  // namespace pm3
