// WAV entries inside FPA archives: no RIFF wrapper, the entry starts directly at the "fmt " chunk.
// Seen in the game data: PCM16 mono 8 kHz (SYS0 sound effects) and IMA ADPCM (tag 0x11) mono 22.05 kHz,
// block 512 bytes (TAK1/TAK2 voices). Everything is decoded to signed 16-bit PCM.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "pm3/abp.h"

namespace pm3 {

struct Pcm {
    int rate = 0, channels = 0;
    std::vector<int16_t> samples;  // interleaved
};

// Returns false for unsupported formats / malformed data. Trailing bytes after the data chunk are ignored.
bool wav_decode(const uint8_t* p, size_t n, Pcm& out);

// Mono mix-down + linear-interpolation resample to `rate` Hz (the viewer plays everything on one 22.05 kHz mono device).
std::vector<int16_t> pcm_to_mono_rate(const Pcm& pcm, int rate);

// Standard 44-byte-header RIFF/WAVE file (PCM16) for exporting or feeding other players.
Bytes pcm_to_riff(const Pcm& pcm);

}  // namespace pm3
