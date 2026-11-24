// EXE game-state variables reachable from EVX GETV(57)/SETV(56).  Extracted from PM3WIN.EXE:
//   GETV 0x4249D5 (jump table 0x424C1C, vars 4..75) and SETV 0x40D38B (jump table 0x40D85A, vars 1..54).
// Only variables whose GETV and SETV touch the SAME field are listed (34 of them); `add` is the offset the EXE
// applies when reading (stored = value - add).  Everything else falls back to a plain map in EvxVm.
#pragma once
#include <cstdint>

namespace pm3 {

struct EvxVarSpec { int var; uint32_t addr; uint8_t size; int8_t add; };

static const EvxVarSpec kEvxVars[] = {
    {4, 0x4920e8, 2, 10},
    {5, 0x4920ea, 1, 0},
    {6, 0x492194, 2, 0},
    {7, 0x492198, 2, 1},
    {8, 0x49219a, 2, 1},
    {9, 0x4921a8, 4, 0},
    {10, 0x4921ac, 4, 0},
    {11, 0x4921b0, 4, 0},
    {12, 0x4921b4, 4, 0},
    {13, 0x4921b8, 4, 0},
    {16, 0x4921bc, 4, 0},
    {17, 0x4921c0, 4, 0},
    {18, 0x4921c4, 4, 0},
    {19, 0x4921c8, 4, 0},
    {20, 0x4921cc, 4, 0},
    {21, 0x4921d0, 4, 0},
    {22, 0x4921d4, 4, 0},
    {23, 0x4921d8, 4, 0},
    {24, 0x4921dc, 4, 0},
    {25, 0x4921e0, 4, 0},
    {26, 0x4921e4, 4, 0},
    {27, 0x4921e8, 4, 0},
    {28, 0x4921a0, 2, 1},
    {29, 0x4921a2, 2, 1},
    {30, 0x49219e, 1, 0},
    {32, 0x4921ec, 4, 0},
    {33, 0x4921a4, 1, 0},
    {34, 0x4921a5, 1, 0},
    {35, 0x4921a6, 1, 0},
    {40, 0x491980, 2, 1},
    {41, 0x491982, 2, 1},
    {42, 0x491984, 2, 1},
    {43, 0x491986, 2, 1},
    {46, 0x491988, 2, 0},
};
static const int kEvxVarCount = (int)(sizeof(kEvxVars) / sizeof(kEvxVars[0]));
constexpr uint32_t kEvxGsBase = 0x491800;   // backing store covers 0x491800..0x4923FF
constexpr uint32_t kEvxGsSize = 0xC00;
constexpr int kEvxDirectLo = 0x100, kEvxDirectHi = 0x380;   // vars stored as WORD[0x48D244 + 2*v]

}  // namespace pm3
