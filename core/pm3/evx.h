// EVX event scripts: loader, linear-sweep disassembler and stack VM.
// Port of py/evxdis2.py, py/evx_disasm.py (strings) and py/evx_vm.py.  Engine ops (everything that is not
// stack/flow/arith) are only *logged* here (same text as the Python prototype); gameplay semantics come later.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "pm3/evx_vars.h"
#include "pm3/pyrandom.h"

namespace pm3 {

struct EvxIns { uint32_t pc; uint8_t op; int32_t arg; uint8_t len; };   // arg: u16 (s16 for jumps/call), u8, or 0

struct EvxScript {
    std::string name;
    std::vector<uint8_t> d;
    uint32_t code = 0, entry = 0;
    std::map<uint32_t, std::string> strs;   // offset -> cp949 bytes (raw)
    // Throws std::runtime_error if the data is not a valid EVX0 script.
    static EvxScript load(const std::string& name, std::vector<uint8_t> data);
    const std::string* text(uint32_t off) const;   // raw cp949 bytes, nullptr if `off` is not inside the string area
    std::vector<EvxIns> sweep() const;             // linear sweep from `code`
};

bool evx_is_a16(uint8_t op);   // 01 0c 10 11 12
bool evx_is_a8(uint8_t op);    // 03 05 07 09 0a

// Structured record of what the script asked the engine to show (the text `log` stays unchanged).
struct EvxEvent {
    enum Kind { Background, Sprites, Portrait, Picture, Move, Text, Wait } kind;
    int a, b, c, d;   // Background: a = M id; Sprites: a = P id (-1 = none); Portrait: slot,mode,B hi,B lo; Picture: x,y,id; Move (op 4a): slot,x,y,mode;
    // Text (ops 71/84/1d/90): a = op, b = first arg when there are two (84: speaker/window), else -1; text = raw cp949 (may hold ESC codes)
    // Wait (op 1f): a = ticks (1/30 s)
    std::string text;
    EvxEvent(Kind k, int a_ = 0, int b_ = 0, int c_ = 0, int d_ = 0) : kind(k), a(a_), b(b_), c(c_), d(d_) {}
};

class EvxVm {
public:
    EvxVm(const EvxScript& sc, uint32_t seed = 1);
    std::map<int, int> V;                 // game variables (s16 index -> s16/u16 value)
    std::vector<std::string> log;
    std::vector<EvxEvent> events;
    bool run(int limit = 300000);         // true = script ended, false = step limit
    int steps() const { return steps_; }
    // EXE-faithful variable semantics (off by default so the Python-parity traces stay valid):
    //  - vars 256..895 live in a WORD array, others are clamped to 0..9999 on SETV (EXE 0x40D38B)
    //  - 34 special vars map to game-state fields (evx_vars.h); SETV returns the old value in R
    bool exe_vars = false;
    int getv(int var) const;                 // value a script sees for GETV
    void setv(int var, int value);           // SETV; may update R
    uint16_t result() const { return R_; }
private:
    std::vector<int16_t> direct_ = std::vector<int16_t>(0x280, 0);
    std::vector<uint8_t> gs_ = std::vector<uint8_t>(kEvxGsSize, 0);
    int gs_read(const EvxVarSpec& s) const;
    void gs_write(const EvxVarSpec& s, int v);
    const EvxScript& sc_;
    std::vector<uint8_t> mem_;
    uint32_t sp_ = 0, fp_ = 0, pc_ = 0;
    uint16_t R_ = 0; int steps_ = 0, flag3_ = 0, polls_ = 0;
    PyRandom rng_;
    void push(uint16_t v); uint16_t pop(); void push32(uint32_t v); uint32_t pop32();
    uint16_t arg(int i) const;
    void engine(uint8_t op);
    std::string fmt(uint16_t v, bool text) const;
};

}  // namespace pm3
