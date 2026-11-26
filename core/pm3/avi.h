// AVI (RIFF) demuxer + Cinepak video decoder + IMA ADPCM audio decoder: the opening movie MOVIE\PM3OP.AVI (320x240, 15 fps Cinepak, 22.05 kHz IMA ADPCM).
// Used by the MCI HLE in x86host.cpp (the original game plays it through MCI "avivideo", which does not exist on iOS).
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>

namespace pm3 {

// IMA/DVI ADPCM (WAVE_FORMAT 0x11) blocks -> signed 16-bit PCM, interleaved. `align` = block size in bytes.
std::vector<int16_t> ima_adpcm_decode(const uint8_t* d, size_t len, int ch, int align);

class Cinepak {
public:
    bool decode(const uint8_t* data, size_t size);                 // one frame; false if malformed. Inter frames build on the previous call.
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint8_t>& rgb() const { return rgb_; }       // width*height*3 (R,G,B)
private:
    struct Entry { uint8_t c[4][3]; };
    struct Strip { std::vector<Entry> v1 = std::vector<Entry>(256), v4 = std::vector<Entry>(256); };
    void load_codebook(std::vector<Entry>& cb, int id, const uint8_t* p, size_t n);
    bool decode_vectors(Strip& s, int id, const uint8_t* p, size_t n, int y0, int y1);
    int w_ = 0, h_ = 0; std::vector<uint8_t> rgb_; Strip strip_[1]; std::vector<Strip> strips_;
};

class AviMovie {
public:
    ~AviMovie() { if (f_) fclose(f_); }
    bool open(FILE* f);                                           // takes ownership of f (closed in the destructor); false if not a Cinepak AVI
    int width() const { return w_; }
    int height() const { return h_; }
    double fps() const { return fps_; }
    int frame_count() const { return (int)vid_.size(); }
    int next_frame() const { return next_; }
    bool decode_next();                                           // decode frame next_ and advance; false at the end / on error
    void rewind() { next_ = 0; }                                  // the following decode_next() starts again at frame 0 (a key frame)
    const std::vector<uint8_t>& rgb() const { return cp_.rgb(); } // last decoded frame, RGB24
    const std::vector<int16_t>& audio() const { return audio_; }  // whole soundtrack, decoded
    int audio_rate() const { return arate_; }
    int audio_channels() const { return ach_; }
private:
    struct Ch { uint32_t off, size; };
    FILE* f_ = nullptr; int w_ = 0, h_ = 0, next_ = 0, arate_ = 0, ach_ = 1; double fps_ = 15;
    std::vector<Ch> vid_, aud_; std::vector<int16_t> audio_; Cinepak cp_;
};

}  // namespace pm3
