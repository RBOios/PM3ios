#include "pm3/x86.h"

#include <cstdio>
#include <cstring>

namespace pm3 {
namespace {
inline uint32_t maskOf(int sz) { return sz == 4 ? 0xFFFFFFFFu : sz == 2 ? 0xFFFFu : 0xFFu; }
inline uint32_t signOf(int sz) { return sz == 4 ? 0x80000000u : sz == 2 ? 0x8000u : 0x80u; }
inline int32_t sext(uint32_t v, int sz) { return sz == 4 ? (int32_t)v : sz == 2 ? (int32_t)(int16_t)v : (int32_t)(int8_t)v; }
[[noreturn]] void fail(const char* fmt, uint32_t a = 0, uint32_t b = 0) { char buf[160]; snprintf(buf, sizeof buf, fmt, a, b); throw std::runtime_error(buf); }
}  // namespace

X86::X86(size_t mem_size) : mem(mem_size, 0) {}

uint32_t X86::add_host(HostFn f) { host_.push_back(std::move(f)); return HOST_BASE + (uint32_t)(host_.size() - 1); }

uint32_t X86::rd(uint32_t a, int sz) const {
    if ((uint64_t)a + sz > mem.size()) fail("read fault @%08x (eip %08x)", a, eip);
    uint32_t v = 0; std::memcpy(&v, &mem[a], sz); return v;   // little-endian host
}
void X86::wr(uint32_t a, int sz, uint32_t v) {
    if ((uint64_t)a + sz > mem.size()) fail("write fault @%08x (eip %08x)", a, eip);
    std::memcpy(&mem[a], &v, sz);
}
void X86::write_bytes(uint32_t a, const uint8_t* p, size_t n) {
    if ((uint64_t)a + n > mem.size()) fail("write_bytes fault @%08x", a);
    std::memcpy(&mem[a], p, n);
}
uint32_t X86::fetch8() { uint32_t v = rd(eip, 1); eip += 1; return v; }
uint32_t X86::fetch16() { uint32_t v = rd(eip, 2); eip += 2; return v; }
uint32_t X86::fetch32() { uint32_t v = rd(eip, 4); eip += 4; return v; }

uint32_t X86::getReg(int i, int sz) const {
    if (sz == 4) return r[i];
    if (sz == 2) return r[i] & 0xFFFF;
    return i < 4 ? (r[i] & 0xFF) : ((r[i - 4] >> 8) & 0xFF);
}
void X86::setReg(int i, int sz, uint32_t v) {
    if (sz == 4) r[i] = v;
    else if (sz == 2) r[i] = (r[i] & 0xFFFF0000u) | (v & 0xFFFF);
    else if (i < 4) r[i] = (r[i] & ~0xFFu) | (v & 0xFF);
    else r[i - 4] = (r[i - 4] & ~0xFF00u) | ((v & 0xFF) << 8);
}

X86::M X86::modrm() {
    uint32_t b = fetch8(); M m{}; m.regop = (b >> 3) & 7; int mod = b >> 6, rm = b & 7;
    if (mod == 3) { m.reg = true; m.idx = rm; return m; }
    uint32_t ea = 0;
    if (rm == 4) {
        uint32_t s = fetch8(); int sc = s >> 6, ix = (s >> 3) & 7, bs = s & 7;
        if (bs == 5 && mod == 0) ea = fetch32(); else ea = r[bs];
        if (ix != 4) ea += r[ix] << sc;
    } else if (rm == 5 && mod == 0) ea = fetch32();
    else ea = r[rm];
    if (mod == 1) ea += (uint32_t)(int32_t)(int8_t)fetch8(); else if (mod == 2) ea += fetch32();
    m.reg = false; m.idx = 0; m.ea = ea; return m;
}

void X86::setszp(uint32_t res, int sz) {
    res &= maskOf(sz); ZF = res == 0; SF = (res & signOf(sz)) != 0;
    uint8_t p = (uint8_t)res; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1; PF = !(p & 1);
}

uint32_t X86::alu(int op, uint32_t a, uint32_t b, int sz) {   // 0 add 1 or 2 adc 3 sbb 4 and 5 sub 6 xor 7 cmp
    uint32_t mk = maskOf(sz), sg = signOf(sz); a &= mk; b &= mk; uint32_t res = 0;
    switch (op) {
    case 0: case 2: { uint32_t c = (op == 2 && CF) ? 1 : 0; uint64_t t = (uint64_t)a + b + c; res = (uint32_t)t & mk;
        CF = t > mk; OF = ((a ^ res) & (b ^ res) & sg) != 0; AF = ((a ^ b ^ res) & 0x10) != 0; break; }
    case 3: case 5: case 7: { uint32_t c = (op == 3 && CF) ? 1 : 0; uint64_t t = (uint64_t)a - b - c; res = (uint32_t)t & mk;
        CF = (uint64_t)a < (uint64_t)b + c; OF = ((a ^ b) & (a ^ res) & sg) != 0; AF = ((a ^ b ^ res) & 0x10) != 0; break; }
    case 1: res = a | b; CF = OF = 0; AF = 0; break;
    case 4: res = a & b; CF = OF = 0; AF = 0; break;
    case 6: res = a ^ b; CF = OF = 0; AF = 0; break;
    }
    setszp(res, sz); return res;
}

uint32_t X86::shift(int op, uint32_t a, uint32_t cnt, int sz) {   // 0 rol 1 ror 2 rcl 3 rcr 4 shl 5 shr 6 shl 7 sar
    cnt &= 31; uint32_t mk = maskOf(sz), sg = signOf(sz); a &= mk; int bits = sz * 8;
    if (cnt == 0) return a;
    uint32_t res = a;
    switch (op) {
    case 4: case 6: res = (cnt >= (uint32_t)bits + 0) ? 0 : (a << cnt) & mk; CF = cnt <= (uint32_t)bits ? ((a >> (bits - cnt)) & 1) : 0;
        OF = ((res & sg) != 0) != CF; setszp(res, sz); break;
    case 5: res = cnt >= (uint32_t)bits ? 0 : a >> cnt; CF = ((a >> (cnt - 1)) & 1); OF = (a & sg) != 0; setszp(res, sz); break;
    case 7: { int32_t s = sext(a, sz); res = (uint32_t)(s >> (cnt >= 32 ? 31 : cnt)) & mk; CF = (((uint32_t)s >> (cnt > 31 ? 31 : cnt - 1)) & 1); OF = 0; setszp(res, sz); break; }
    case 0: { uint32_t c = cnt % bits; res = c ? ((a << c) | (a >> (bits - c))) & mk : a; CF = res & 1; OF = ((res & sg) != 0) != CF; break; }
    case 1: { uint32_t c = cnt % bits; res = c ? ((a >> c) | (a << (bits - c))) & mk : a; CF = (res & sg) != 0; OF = ((res ^ (res << 1)) & sg) != 0; break; }
    default: fail("rcl/rcr unimplemented @%08x", eip);
    }
    return res;
}

bool X86::cond(int c) const {
    bool r2 = false;
    switch (c >> 1) {
    case 0: r2 = OF; break; case 1: r2 = CF; break; case 2: r2 = ZF; break; case 3: r2 = CF || ZF; break;
    case 4: r2 = SF; break; case 5: r2 = PF; break; case 6: r2 = SF != OF; break; default: r2 = ZF || (SF != OF);
    }
    return (c & 1) ? !r2 : r2;
}

void X86::unary_group(int sz, const M& m, int regop) {   // F6/F7
    uint32_t mk = maskOf(sz); uint32_t v = getRM(m, sz) & mk;
    switch (regop) {
    case 0: case 1: { uint32_t imm = sz == 1 ? fetch8() : sz == 2 ? fetch16() : fetch32(); alu(4, v, imm, sz); break; }   // test
    case 2: setRM(m, sz, ~v & mk); break;                                                                           // not
    case 3: { uint32_t res = alu(5, 0, v, sz); setRM(m, sz, res); CF = v != 0; break; }                              // neg
    case 4: { if (sz == 1) { uint32_t t = (r[0] & 0xFF) * v; setReg(0, 2, t); CF = OF = (t >> 8) != 0; }
              else if (sz == 2) { uint32_t t = (r[0] & 0xFFFF) * v; setReg(0, 2, t); setReg(2, 2, t >> 16); CF = OF = (t >> 16) != 0; }
              else { uint64_t t = (uint64_t)r[0] * v; r[0] = (uint32_t)t; r[2] = (uint32_t)(t >> 32); CF = OF = r[2] != 0; } break; }   // mul
    case 5: { if (sz == 4) { int64_t t = (int64_t)(int32_t)r[0] * (int32_t)v; r[0] = (uint32_t)t; r[2] = (uint32_t)((uint64_t)t >> 32); CF = OF = t != (int64_t)(int32_t)r[0]; }
              else if (sz == 2) { int32_t t = (int16_t)r[0] * (int16_t)v; setReg(0, 2, t); setReg(2, 2, (uint32_t)t >> 16); CF = OF = t != (int16_t)t; }
              else { int32_t t = (int8_t)r[0] * (int8_t)v; setReg(0, 2, t); CF = OF = t != (int8_t)t; } break; }                   // imul
    case 6: { if (v == 0) fail("div by zero @%08x", eip);
              if (sz == 4) { uint64_t n = ((uint64_t)r[2] << 32) | r[0]; if (n / v > 0xFFFFFFFFull) fail("div overflow @%08x", eip); r[0] = (uint32_t)(n / v); r[2] = (uint32_t)(n % v); }
              else if (sz == 2) { uint32_t n = ((r[2] & 0xFFFF) << 16) | (r[0] & 0xFFFF); setReg(0, 2, n / v); setReg(2, 2, n % v); }
              else { uint32_t n = r[0] & 0xFFFF; setReg(0, 1, n / v); setReg(4, 1, n % v); } break; }                               // div
    default: { if (v == 0) fail("idiv by zero @%08x", eip);
              if (sz == 4) { int64_t n = (int64_t)(((uint64_t)r[2] << 32) | r[0]); int64_t d = (int32_t)v; int64_t q = n / d; if (q > INT32_MAX || q < INT32_MIN) fail("idiv overflow @%08x", eip); r[0] = (uint32_t)q; r[2] = (uint32_t)(n % d); }
              else if (sz == 2) { int32_t n = (int32_t)(((r[2] & 0xFFFF) << 16) | (r[0] & 0xFFFF)); int32_t d = (int16_t)v; setReg(0, 2, n / d); setReg(2, 2, n % d); }
              else { int32_t n = (int16_t)r[0]; int32_t d = (int8_t)v; setReg(0, 1, n / d); setReg(4, 1, n % d); } break; }          // idiv
    }
}

void X86::string_op(uint8_t op, int sz, int rep) {
    int d = DF ? -sz : sz;
    auto once = [&]() {
        switch (op) {
        case 0xA4: case 0xA5: wr(r[7], sz, rd(r[6], sz)); r[6] += d; r[7] += d; break;
        case 0xAA: case 0xAB: wr(r[7], sz, r[0] & maskOf(sz)); r[7] += d; break;
        case 0xAC: case 0xAD: setReg(0, sz, rd(r[6], sz)); r[6] += d; break;
        case 0xA6: case 0xA7: alu(7, rd(r[6], sz), rd(r[7], sz), sz); r[6] += d; r[7] += d; break;
        case 0xAE: case 0xAF: alu(7, r[0] & maskOf(sz), rd(r[7], sz), sz); r[7] += d; break;
        }
    };
    if (!rep) { once(); return; }
    bool cmpOp = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;
    while (r[1] != 0) { once(); r[1]--; if (cmpOp) { if (rep == 1 && !ZF) break; if (rep == 2 && ZF) break; } }
}

void X86::ret_stdcall(int nargs, uint32_t retval) { r[0] = retval; eip = pop32(); r[4] += 4 * nargs; }

void X86::step() {
    if (eip >= HOST_BASE) { uint32_t id = eip - HOST_BASE; if (id >= host_.size()) fail("bad host call %08x", eip); host_[id](*this); return; }
    uint32_t start = eip; bool o16 = false; int rep = 0; uint8_t op;
    for (;;) {
        op = (uint8_t)fetch8();
        if (op == 0x66) o16 = true; else if (op == 0xF3) rep = 1; else if (op == 0xF2) rep = 2;
        else if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0x64 || op == 0x65) {} else break;
    }
    int os = o16 ? 2 : 4;
    if (op < 0x40 && (op & 7) < 6) {   // ALU
        int a = op >> 3, form = op & 7;
        if (form == 4) { uint32_t res = alu(a, r[0] & 0xFF, fetch8(), 1); if (a != 7) setReg(0, 1, res); return; }
        if (form == 5) { uint32_t imm = o16 ? fetch16() : fetch32(); uint32_t res = alu(a, getReg(0, os), imm, os); if (a != 7) setReg(0, os, res); return; }
        int sz = (form & 1) ? os : 1; M m = modrm();
        if (form < 2) { uint32_t res = alu(a, getRM(m, sz), getReg(m.regop, sz), sz); if (a != 7) setRM(m, sz, res); }
        else { uint32_t res = alu(a, getReg(m.regop, sz), getRM(m, sz), sz); if (a != 7) setReg(m.regop, sz, res); }
        return;
    }
    if (op >= 0x40 && op <= 0x4F) {   // inc/dec
        int i = op & 7; bool c = CF; uint32_t v = getReg(i, os);
        uint32_t res = alu(op < 0x48 ? 0 : 5, v, 1, os); CF = c; setReg(i, os, res); return;
    }
    if (op >= 0x50 && op <= 0x57) { push32(r[op & 7]); return; }
    if (op >= 0x58 && op <= 0x5F) { uint32_t v = pop32(); r[op & 7] = v; return; }
    if (op >= 0x70 && op <= 0x7F) { int32_t rel = (int8_t)fetch8(); if (cond(op & 15)) eip += rel; return; }
    if (op >= 0xB0 && op <= 0xB7) { setReg(op & 7, 1, fetch8()); return; }
    if (op >= 0xB8 && op <= 0xBF) { setReg(op & 7, os, o16 ? fetch16() : fetch32()); return; }
    switch (op) {
    case 0x68: push32(fetch32()); return;
    case 0x6A: push32((uint32_t)(int32_t)(int8_t)fetch8()); return;
    case 0x69: case 0x6B: { M m = modrm(); int32_t imm = op == 0x6B ? (int8_t)fetch8() : (o16 ? (int16_t)fetch16() : (int32_t)fetch32());
        int64_t t = (int64_t)sext(getRM(m, os), os) * imm; uint32_t res = (uint32_t)t & maskOf(os); setReg(m.regop, os, res);
        CF = OF = t != (int64_t)sext(res, os); setszp(res, os); return; }
    case 0x80: case 0x81: case 0x83: { int sz = op == 0x80 ? 1 : os; M m = modrm(); uint32_t imm;
        if (op == 0x81) imm = o16 ? fetch16() : fetch32(); else imm = (uint32_t)(int32_t)(int8_t)fetch8();
        uint32_t res = alu(m.regop, getRM(m, sz), imm, sz); if (m.regop != 7) setRM(m, sz, res); return; }
    case 0x84: case 0x85: { int sz = op == 0x84 ? 1 : os; M m = modrm(); alu(4, getRM(m, sz), getReg(m.regop, sz), sz); return; }
    case 0x86: case 0x87: { int sz = op == 0x86 ? 1 : os; M m = modrm(); uint32_t a = getRM(m, sz), b = getReg(m.regop, sz); setRM(m, sz, b); setReg(m.regop, sz, a); return; }
    case 0x88: case 0x89: { int sz = op == 0x88 ? 1 : os; M m = modrm(); setRM(m, sz, getReg(m.regop, sz)); return; }
    case 0x8A: case 0x8B: { int sz = op == 0x8A ? 1 : os; M m = modrm(); setReg(m.regop, sz, getRM(m, sz)); return; }
    case 0x8D: { M m = modrm(); if (m.reg) fail("lea reg @%08x", start); setReg(m.regop, os, m.ea); return; }
    case 0x8F: { M m = modrm(); setRM(m, 4, pop32()); return; }
    case 0x90: return;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { uint32_t t = r[0]; r[0] = r[op & 7]; r[op & 7] = t; return; }
    case 0x98: if (o16) setReg(0, 2, (uint32_t)(int32_t)(int8_t)r[0]); else r[0] = (uint32_t)(int32_t)(int16_t)r[0]; return;
    case 0x99: if (o16) setReg(2, 2, (r[0] & 0x8000) ? 0xFFFF : 0); else r[2] = (int32_t)r[0] < 0 ? 0xFFFFFFFFu : 0; return;
    case 0xA0: setReg(0, 1, rd(fetch32(), 1)); return;
    case 0xA1: setReg(0, os, rd(fetch32(), os)); return;
    case 0xA2: wr(fetch32(), 1, r[0] & 0xFF); return;
    case 0xA3: wr(fetch32(), os, getReg(0, os)); return;
    case 0xA4: case 0xAA: case 0xAC: case 0xA6: case 0xAE: string_op(op, 1, rep); return;
    case 0xA5: case 0xAB: case 0xAD: case 0xA7: case 0xAF: string_op(op, os, rep); return;
    case 0xA8: alu(4, r[0] & 0xFF, fetch8(), 1); return;
    case 0xA9: alu(4, getReg(0, os), o16 ? fetch16() : fetch32(), os); return;
    case 0xC0: case 0xC1: { int sz = op == 0xC0 ? 1 : os; M m = modrm(); uint32_t c = fetch8(); setRM(m, sz, shift(m.regop, getRM(m, sz), c, sz)); return; }
    case 0xD0: case 0xD1: { int sz = op == 0xD0 ? 1 : os; M m = modrm(); setRM(m, sz, shift(m.regop, getRM(m, sz), 1, sz)); return; }
    case 0xD2: case 0xD3: { int sz = op == 0xD2 ? 1 : os; M m = modrm(); setRM(m, sz, shift(m.regop, getRM(m, sz), r[1] & 0xFF, sz)); return; }
    case 0xC2: { uint32_t n = fetch16(); eip = pop32(); r[4] += n; return; }
    case 0xC3: eip = pop32(); return;
    case 0xC6: case 0xC7: { int sz = op == 0xC6 ? 1 : os; M m = modrm(); uint32_t imm = sz == 1 ? fetch8() : sz == 2 ? fetch16() : fetch32(); setRM(m, sz, imm); return; }
    case 0xC9: r[4] = r[5]; r[5] = pop32(); return;
    case 0xE3: { int32_t rel = (int8_t)fetch8(); if (r[1] == 0) eip += rel; return; }
    case 0xE8: { int32_t rel = (int32_t)fetch32(); push32(eip); eip += rel; return; }
    case 0xE9: { int32_t rel = (int32_t)fetch32(); eip += rel; return; }
    case 0xEB: { int32_t rel = (int8_t)fetch8(); eip += rel; return; }
    case 0xF6: case 0xF7: { int sz = op == 0xF6 ? 1 : os; M m = modrm(); unary_group(sz, m, m.regop); return; }
    case 0xF8: CF = 0; return; case 0xF9: CF = 1; return; case 0xFC: DF = 0; return; case 0xFD: DF = 1; return;
    case 0xFE: { M m = modrm(); bool c = CF; uint32_t res = alu(m.regop == 0 ? 0 : 5, getRM(m, 1), 1, 1); CF = c; setRM(m, 1, res); return; }
    case 0xFF: { M m = modrm();
        switch (m.regop) {
        case 0: case 1: { bool c = CF; uint32_t res = alu(m.regop == 0 ? 0 : 5, getRM(m, os), 1, os); CF = c; setRM(m, os, res); return; }
        case 2: { uint32_t t = getRM(m, 4); push32(eip); eip = t; return; }
        case 4: eip = getRM(m, 4); return;
        case 6: push32(getRM(m, 4)); return;
        default: fail("FF /%d unimplemented @%08x", m.regop, start);
        } }
    case 0x0F: {
        uint8_t o2 = (uint8_t)fetch8();
        if (o2 >= 0x80 && o2 <= 0x8F) { int32_t rel = (int32_t)fetch32(); if (cond(o2 & 15)) eip += rel; return; }
        if (o2 >= 0x90 && o2 <= 0x9F) { M m = modrm(); setRM(m, 1, cond(o2 & 15) ? 1 : 0); return; }
        if (o2 >= 0x40 && o2 <= 0x4F) { M m = modrm(); uint32_t v = getRM(m, os); if (cond(o2 & 15)) setReg(m.regop, os, v); return; }   // cmovcc
        switch (o2) {
        case 0xB6: { M m = modrm(); setReg(m.regop, os, getRM(m, 1)); return; }
        case 0xB7: { M m = modrm(); setReg(m.regop, os, getRM(m, 2)); return; }
        case 0xBE: { M m = modrm(); setReg(m.regop, os, (uint32_t)(int32_t)(int8_t)getRM(m, 1) & maskOf(os)); return; }
        case 0xBF: { M m = modrm(); setReg(m.regop, os, (uint32_t)(int32_t)(int16_t)getRM(m, 2)); return; }
        case 0xAF: { M m = modrm(); int64_t t = (int64_t)sext(getReg(m.regop, os), os) * sext(getRM(m, os), os); uint32_t res = (uint32_t)t & maskOf(os);
            setReg(m.regop, os, res); CF = OF = t != (int64_t)sext(res, os); setszp(res, os); return; }
        default: fail("0F %02x unimplemented", o2, start);
        } }
    default: fail("opcode %02x unimplemented @%08x", op, start);
    }
}

bool X86::run(uint32_t stop, uint64_t max_steps) {
    uint64_t n = 0;
    try {
        while (eip != stop) {
            if (n++ >= max_steps) { err = "step limit"; return false; }
            steps++; step();
        }
    } catch (std::exception& e) { err = e.what(); return false; }
    return true;
}

uint32_t X86::call_cdecl(uint32_t addr, std::initializer_list<uint32_t> args, uint64_t max_steps) {
    uint32_t sp0 = r[4]; std::vector<uint32_t> a(args);
    for (size_t i = a.size(); i-- > 0;) push32(a[i]);
    push32(RET_STOP); eip = addr;
    if (!run(RET_STOP, max_steps)) { r[4] = sp0; throw std::runtime_error("x86: " + err); }
    r[4] = sp0; return r[0];
}

}  // namespace pm3
