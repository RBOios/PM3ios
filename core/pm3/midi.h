// Standard MIDI File player with a small built-in software synthesizer (no sound font needed; General-MIDI-ish timbre families,
// synthesized drums). Thread-safe: the game thread calls load/play/stop, the audio thread calls render().
#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

namespace pm3 {

class MidiPlayer {
public:
    explicit MidiPlayer(int sample_rate = 44100) : rate_(sample_rate) {}
    bool load(const uint8_t* data, size_t size);          // SMF format 0/1; false if malformed
    void play(bool loop);                                 // from the start
    void stop();                                          // all notes off
    bool playing();                                       // false once a non-looping song reached its end (and the tail faded)
    void set_volume(float v);
    void render(float* stereo, int frames);               // ADDS into the buffer (interleaved L R, floats ~[-1,1])
    double length_seconds() const { return length_s_; }
private:
    struct Ev { uint32_t tick; uint8_t st, d1, d2; uint32_t tempo; };   // st = 0 for a tempo change (tempo in us per quarter)
    struct Voice { bool on = false, released = false; int ch = 0, note = 0, prog = 0; float vel = 0, freq = 0, ph = 0, ph2 = 0, env = 0, rel_env = 0, age = 0; bool drum = false; float noise = 0, lp = 0; uint32_t seed = 1; bool held = false; };
    struct Chan { int prog = 0; float vol = 100 / 127.f, expr = 1.f, pan = 0.5f, bend = 0; bool sustain = false; };
    void dispatch(const Ev& e);
    void note_on(int ch, int note, int vel);
    void note_off(int ch, int note);
    void all_off(bool hard);
    void reset_state();
    std::mutex mu_; int rate_; std::vector<Ev> ev_; int div_ = 48; double length_s_ = 0;
    bool playing_ = false, loop_ = false; size_t pos_ = 0; double tick_ = 0; uint32_t tempo_ = 500000; float volume_ = 0.7f; int tail_ = 0;
    Chan ch_[16]; Voice v_[40];
};

}  // namespace pm3
