// Minimal 32-bit x86 interpreter for running the original PM3WIN.EXE code (integer subset actually used by the EXE).
// No JIT: suitable for iOS.  Host API calls (Win32/DirectX shims) are dispatched through HOST_BASE addresses.
#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace pm3 {

class X86 {
public:
    static constexpr uint32_t HOST_BASE = 0xF0000000u;   // eip >= HOST_BASE -> host function #(eip-HOST_BASE)
    static constexpr uint32_t RET_STOP = 0xE0000000u;    // return sentinel for call_cdecl
    using HostFn = std::function<void(X86&)>;             // must pop return address / args itself (see ret_stdcall)

    explicit X86(size_t mem_size = 0x2000000);

    uint32_t r[8] = {0};        // eax ecx edx ebx esp ebp esi edi
    uint32_t eip = 0;
    bool CF = 0, PF = 0, AF = 0, ZF = 0, SF = 0, DF = 0, OF = 0;
    std::vector<uint8_t> mem;
    uint64_t steps = 0;
    std::string err;

    uint32_t add_host(HostFn f);                              // returns guest address to put into an IAT slot
    // memory helpers (guest addresses)
    uint32_t rd(uint32_t a, int sz) const;
    void wr(uint32_t a, int sz, uint32_t v);
    uint8_t rd8(uint32_t a) const { return (uint8_t)rd(a, 1); }
    uint32_t rd32(uint32_t a) const { return rd(a, 4); }
    void wr32(uint32_t a, uint32_t v) { wr(a, 4, v); }
    void write_bytes(uint32_t a, const uint8_t* p, size_t n);
    // calling convention helpers
    uint32_t arg(int i) const { return rd32(r[4] + 4 + 4 * i); }            // inside a host fn (stdcall/cdecl arg i)
    void ret_stdcall(int nargs, uint32_t retval);                            // pop return addr + nargs*4, eax = retval
    uint32_t call_cdecl(uint32_t addr, std::initializer_list<uint32_t> args, uint64_t max_steps = 500000000ull);
    bool run(uint32_t stop, uint64_t max_steps);
    void push32(uint32_t v) { r[4] -= 4; wr(r[4], 4, v); }
    uint32_t pop32() { uint32_t v = rd(r[4], 4); r[4] += 4; return v; }

private:
    std::vector<HostFn> host_;
    void step();
    uint32_t fetch8(), fetch16(), fetch32();
    struct M { bool reg; int idx; uint32_t ea; int regop; };
    M modrm();
    uint32_t getReg(int i, int sz) const;
    void setReg(int i, int sz, uint32_t v);
    uint32_t getRM(const M& m, int sz) const { return m.reg ? getReg(m.idx, sz) : rd(m.ea, sz); }
    void setRM(const M& m, int sz, uint32_t v) { if (m.reg) setReg(m.idx, sz, v); else wr(m.ea, sz, v); }
    uint32_t alu(int op, uint32_t a, uint32_t b, int sz);
    uint32_t shift(int op, uint32_t a, uint32_t cnt, int sz);
    void setszp(uint32_t res, int sz);
    bool cond(int c) const;
    void unary_group(int sz, const M& m, int regop);
    void string_op(uint8_t op, int sz, int rep);
};

}  // namespace pm3
