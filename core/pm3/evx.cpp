#include "pm3/evx.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace pm3 {
namespace {
std::string esc(const std::string& t) {   // keep one trace entry on one line (control bytes -> \xNN, like Python repr)
    std::string o; char b[8];
    for (unsigned char c : t) {
        if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if (c == '\r') o += "\\r";
        else if (c < 0x20 || c == 0x7f) { snprintf(b, sizeof b, "\\x%02x", c); o += b; } else o += (char)c;
    }
    return o;
}
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
int16_t s16(uint32_t v) { return (int16_t)(uint16_t)v; }
bool isText(uint8_t b) { return b >= 0x20 || b == 0x0a || b == 0x09 || b == 0x0d || b == 0x1b; }
bool isJump(uint8_t op) { return op == 0x10 || op == 0x11 || op == 0x12 || op == 0x0c; }
}  // namespace

bool evx_is_a16(uint8_t op) { return op == 0x01 || op == 0x0c || op == 0x10 || op == 0x11 || op == 0x12; }
bool evx_is_a8(uint8_t op) { return op == 0x03 || op == 0x05 || op == 0x07 || op == 0x09 || op == 0x0a; }

EvxScript EvxScript::load(const std::string& name, std::vector<uint8_t> data) {
    if (data.size() < 0x12 || std::memcmp(data.data(), "EVX0", 4) != 0) throw std::runtime_error("not an EVX0 script");
    EvxScript s; s.name = name; s.d = std::move(data);
    s.entry = rd16(&s.d[8]);
    if (s.entry >= s.d.size()) throw std::runtime_error("bad EVX entry");
    uint32_t p = 0x10;   // strings: NUL-terminated text until the first non-text string (= start of code)
    while (p < s.entry) {
        size_t e = p; while (e < s.entry && s.d[e] != 0) e++;
        if (e >= s.entry) break;
        bool ok = true; for (size_t i = p; i < e; i++) if (!isText(s.d[i])) { ok = false; break; }
        if (!ok) break;
        s.strs[p] = std::string((const char*)&s.d[p], e - p);
        p = (uint32_t)e + 1;
    }
    s.code = p;
    return s;
}

const std::string* EvxScript::text(uint32_t off) const {
    auto it = strs.find(off);
    if (it != strs.end()) return &it->second;
    if (off >= 0x10 && off < code) {   // inside a string (not at its start): tail of that string
        for (auto& kv : strs) {
            if (kv.first < off && off <= kv.first + kv.second.size()) {
                static thread_local std::string tmp; tmp = kv.second.substr(off - kv.first); return &tmp;
            }
        }
    }
    return nullptr;
}

std::vector<EvxIns> EvxScript::sweep() const {
    std::vector<EvxIns> out; size_t p = code;
    while (p < d.size()) {
        uint8_t op = d[p]; EvxIns i{(uint32_t)p, op, 0, 1};
        if (evx_is_a16(op)) { if (p + 3 > d.size()) break; i.arg = isJump(op) ? (int32_t)(int16_t)rd16(&d[p + 1]) : (int32_t)rd16(&d[p + 1]); i.len = 3; }
        else if (evx_is_a8(op)) { if (p + 2 > d.size()) break; i.arg = d[p + 1]; i.len = 2; }
        out.push_back(i); p += i.len;
    }
    return out;
}

EvxVm::EvxVm(const EvxScript& sc, uint32_t seed) : sc_(sc), mem_(65536, 0), rng_(seed) {
    pc_ = sc.entry; sp_ = fp_ = 0; push(0xffff); push32(0xffffffffu); fp_ = sp_;
}

void EvxVm::push(uint16_t v) { mem_[sp_] = v & 0xff; mem_[sp_ + 1] = v >> 8; sp_ += 2; }
uint16_t EvxVm::pop() { sp_ -= 2; return rd16(&mem_[sp_]); }
void EvxVm::push32(uint32_t v) { for (int i = 0; i < 4; i++) mem_[sp_ + i] = (v >> (8 * i)) & 0xff; sp_ += 4; }
uint32_t EvxVm::pop32() { sp_ -= 4; return (uint32_t)mem_[sp_] | ((uint32_t)mem_[sp_ + 1] << 8) | ((uint32_t)mem_[sp_ + 2] << 16) | ((uint32_t)mem_[sp_ + 3] << 24); }
uint16_t EvxVm::arg(int i) const { return rd16(&mem_[sp_ - 2 - 2 * i]); }

std::string EvxVm::fmt(uint16_t v, bool text) const {
    if (text) { if (const std::string* t = sc_.text(v)) return "'" + esc(*t) + "'"; }
    return std::to_string(s16(v));
}

bool EvxVm::run(int limit) {
    const std::vector<uint8_t>& d = sc_.d;
    while (steps_ < limit) {
        steps_++; uint32_t pc = pc_; uint8_t op = d[pc];
        if (op == 0xff) { log.push_back("<end of script>"); return true; }
        switch (op) {
        case 0x01: push(rd16(&d[pc + 1])); pc_ += 3; break;
        case 0x03: { int n = d[pc + 1]; push(rd16(&mem_[fp_ + 2 * n])); pc_ += 2; break; }
        case 0x09: { int n = d[pc + 1]; uint16_t v = pop(); mem_[fp_ + 2 * n] = v & 0xff; mem_[fp_ + 2 * n + 1] = v >> 8; pc_ += 2; break; }
        case 0x05: { int n = d[pc + 1]; push(rd16(&mem_[fp_ - 8 - 2 * n])); pc_ += 2; break; }
        case 0x07: { int n = d[pc + 1]; uint16_t v = pop(); uint32_t q = fp_ - 8 - 2 * n; mem_[q] = v & 0xff; mem_[q + 1] = v >> 8; pc_ += 2; break; }
        case 0x04: push(R_); pc_ += 1; break;
        case 0x06: R_ = pop(); pc_ += 1; break;
        case 0x0a: sp_ -= 2 * d[pc + 1]; pc_ += 2; break;
        case 0x0f: push(0); pc_ += 1; break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x2c: case 0x2d: case 0x2e: case 0x33: case 0x30: case 0x31: {
            int a = s16(pop()), b = s16(pop()), r = 0;
            switch (op) {
            case 0x20: r = a + b; break; case 0x21: r = a - b; break; case 0x22: r = a * b; break;
            case 0x23: r = b ? a / b : 0; break;
            case 0x24: r = a & b; break; case 0x25: r = a | b; break;
            case 0x2c: r = a == b; break; case 0x2d: r = a != b; break; case 0x2e: r = a >= b; break; case 0x33: r = a < b; break;
            case 0x30: r = (a != 0) && (b != 0); break; case 0x31: r = (a != 0) || (b != 0); break;
            }
            R_ = (uint16_t)(r & 0xffff); pc_ += 1; break;
        }
        case 0x10: pc_ = pc + 3 + (int16_t)rd16(&d[pc + 1]); break;
        case 0x11: pc_ = pc + 3 + (R_ ? (int16_t)rd16(&d[pc + 1]) : 0); break;
        case 0x12: pc_ = pc + 3 + (!R_ ? (int16_t)rd16(&d[pc + 1]) : 0); break;
        case 0x0c: {
            int rel = (int16_t)rd16(&d[pc + 1]); push((uint16_t)fp_); push32(pc + 3); fp_ = sp_; pc_ = pc + 3 + rel; break;
        }
        case 0x0b: {
            sp_ = fp_; uint32_t r = pop32(); fp_ = pop();
            if (r == 0xffffffffu) { log.push_back("<return from main>"); return true; }
            pc_ = r; break;
        }
        default: engine(op);
        }
    }
    log.push_back("<step limit>"); return false;
}

static const EvxVarSpec* find_spec(int var) {
    for (int i = 0; i < kEvxVarCount; i++) if (kEvxVars[i].var == var) return &kEvxVars[i];
    return nullptr;
}

int EvxVm::gs_read(const EvxVarSpec& s) const {
    uint32_t o = s.addr - kEvxGsBase; uint32_t v = 0;
    for (int i = 0; i < s.size; i++) v |= (uint32_t)gs_[o + i] << (8 * i);
    return (int)v;   // EXE reads are movzx / 32-bit mov (unsigned for size < 4)
}

void EvxVm::gs_write(const EvxVarSpec& s, int v) {
    uint32_t o = s.addr - kEvxGsBase;
    for (int i = 0; i < s.size; i++) gs_[o + i] = (uint8_t)(((uint32_t)v >> (8 * i)) & 0xff);
}

int EvxVm::getv(int var) const {
    if (var >= kEvxDirectLo && var < kEvxDirectHi) return direct_[var - kEvxDirectLo];
    if (const EvxVarSpec* s = find_spec(var)) return gs_read(*s) + s->add;
    auto it = V.find(var); return it == V.end() ? 0 : it->second;   // not modelled yet
}

void EvxVm::setv(int var, int value) {
    if (var >= kEvxDirectLo && var < kEvxDirectHi) { R_ = (uint16_t)direct_[var - kEvxDirectLo]; direct_[var - kEvxDirectLo] = (int16_t)value; return; }
    int v = value < 0 ? 0 : (value > 9999 ? 9999 : value);   // EXE clamps 0..0x270F for everything else
    if (const EvxVarSpec* s = find_spec(var)) { R_ = (uint16_t)(gs_read(*s) + s->add); gs_write(*s, v - s->add); return; }
    V[var] = v;                                              // not modelled yet: plain storage
}

void EvxVm::engine(uint8_t op) {
    const std::vector<uint8_t>& d = sc_.d; uint32_t pc = pc_;
    bool nx = d[pc + 1] == 0x0a; int k = nx ? d[pc + 2] : 0;
    switch (op) {
    case 0x57: k = 1; break; case 0x56: k = 2; break; case 0x58: k = 2; break; case 0x5e: k = 1; break;
    case 0x6c: k = 0; break; case 0x6d: k = 0; break; case 0x51: k = 1; break; case 0x1f: k = 1; break;
    case 0x6f: k = 1; break; case 0x8b: k = 2; break; case 0x3e: k = 4; break; case 0x5f: k = 1; break; case 0x85: k = 3; break;
    default: break;
    }
    std::vector<uint16_t> a; for (int i = 0; i < k; i++) a.push_back(arg(i));
    pc_ += 1;
    if (op == 0x4a && a.size() >= 4) events.push_back({EvxEvent::Move, s16(a[0]), s16(a[1]), s16(a[2]), s16(a[3])});   // EXE 0x40C738: portrait slot -> (x, y)
    bool tx = op == 0x84 || op == 0x71 || op == 0x1d || op == 0x48 || op == 0x49 || op == 0x90 || op == 0x4a || op == 0x52;
    auto f = [&]() { std::string s; for (size_t i = 0; i < a.size(); i++) { if (i) s += ", "; s += fmt(a[i], tx); } return s; };
    char b[256];
    if (op == 0x57) R_ = exe_vars ? (uint16_t)getv(s16(a[0])) : (uint16_t)(V.count(s16(a[0])) ? V[s16(a[0])] : 0);
    else if (op == 0x56) { if (exe_vars) setv(s16(a[0]), s16(a[1])); else V[s16(a[0])] = s16(a[1]); snprintf(b, sizeof b, "SETV[%d] = %d", s16(a[0]), s16(a[1])); log.push_back(b); }
    else if (op == 0x58) { int cur = V.count(s16(a[0])) ? V[s16(a[0])] : 0; V[s16(a[0])] = cur + s16(a[1]); snprintf(b, sizeof b, "ADDV?[%d] += %d", s16(a[0]), s16(a[1])); log.push_back(b); }
    else if (op == 0x5e) R_ = (uint16_t)rng_.randrange((uint32_t)(s16(a[0]) < 1 ? 1 : s16(a[0])));
    else if (op == 0x6c) { R_ = (uint16_t)flag3_; flag3_ = 1; }
    else if (op == 0x6d) { R_ = (uint16_t)flag3_; flag3_ = 0; }
    else if (op == 0x51) { polls_++; R_ = (polls_ % 4 == 0) ? 4 : 0; }
    else if (op == 0x1f) { log.push_back("WAIT(" + f() + ")"); if (!a.empty()) events.push_back(EvxEvent(EvxEvent::Wait, s16(a[0]))); }
    else if (op == 0x6f || (op == 0x8b && !a.empty() && s16(a[0]) == 2)) { snprintf(b, sizeof b, "BACKGROUND M%04d", s16(a.back())); log.push_back(b); events.push_back({EvxEvent::Background, s16(a.back()), 0, 0, 0}); }
    else if (op == 0x3e) { snprintf(b, sizeof b, "PORTRAIT slot=%d mode=%d B%03d%03d", s16(a[0]), s16(a[1]), s16(a[2]), s16(a[3])); log.push_back(b);  events.push_back({EvxEvent::Portrait, s16(a[0]), s16(a[1]), s16(a[2]), s16(a[3])}); }
    else if (op == 0x5f) { snprintf(b, sizeof b, "SPRITES P%03d", s16(a[0])); log.push_back(b);  events.push_back({EvxEvent::Sprites, s16(a[0]), 0, 0, 0}); }
    else if (op == 0x85) { snprintf(b, sizeof b, "PICTURE x=%d y=%d id=%d", s16(a[0]), s16(a[1]), (int)(s16(a[2]) & 0xffff)); log.push_back(b);  events.push_back({EvxEvent::Picture, s16(a[0]), s16(a[1]), (int)(s16(a[2]) & 0xffff), 0}); }
    else if (op == 0x71 || op == 0x84 || op == 0x1d || op == 0x90) {
        snprintf(b, sizeof b, "TEXT[%02x] ", op); log.push_back(std::string(b) + f());
        if (!a.empty()) {
            const std::string* t = sc_.text(a.back());
            if (t) { events.push_back(EvxEvent(EvxEvent::Text, op, a.size() > 1 ? (int)s16(a[0]) : -1)); events.back().text = *t; }
        }
    }
    else if (op == 0x41) { std::string s; if (!a.empty()) { const std::string* t = sc_.text(a[0]); s = (t && !t->empty()) ? "'" + esc(*t) + "'" : std::to_string(s16(a[0])); } log.push_back("DEBUG " + s); }
    else { snprintf(b, sizeof b, "op_%02x(", op); log.push_back(std::string(b) + f() + ")"); }
}

}  // namespace pm3
