// Self-contained tests that need no game data.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pm3/abp.h"
#include "pm3/bitreader.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

struct BitWriter {  // MSB-first into LE32 words
    std::vector<uint8_t> out; uint32_t cur = 0; int n = 0;
    void put(uint32_t v, int bits) {
        for (int i = bits - 1; i >= 0; i--) {
            cur = (cur << 1) | ((v >> i) & 1);
            if (++n == 32) flush();
        }
    }
    void flush() { for (int i = 0; i < 4; i++) out.push_back((cur >> (8 * i)) & 0xFF); cur = 0; n = 0; }
    void finish() { if (n) { cur <<= (32 - n); flush(); } }
};

int main() {
    // BitReader: words are LE, bits MSB-first
    { uint8_t b[8] = {0x78, 0x56, 0x34, 0x12, 0xFF, 0, 0, 0};
      pm3::BitReader br(b, 8, 0);
      CHECK(br.bits(8) == 0x12); CHECK(br.bits(4) == 0x3); CHECK(br.bits(20) == 0x45678); CHECK(br.bits(8) == 0); }
    // BitReader past the end yields zeros
    { uint8_t b[2] = {0xFF, 0xFF}; pm3::BitReader br(b, 2, 0); CHECK(br.bits(16) == 0); }

    // ABP: literals "ABC", then copy (pos=1,len=3 -> "ABC"), then terminator.
    { BitWriter w;
      for (char c : std::string("ABC")) { w.put(1, 1); w.put((uint8_t)c, 8); }
      w.put(0, 1); w.put(1, 12); w.put(1, 4);   // pos 1, len = 1+2 = 3
      w.put(0, 1); w.put(0, 12);                // end
      w.finish();
      std::vector<uint8_t> blob(0x18, 0);
      std::memcpy(blob.data(), "ABP_", 4); blob[4] = 6;
      blob.insert(blob.end(), w.out.begin(), w.out.end());
      pm3::Bytes out; uint32_t d = 0;
      CHECK(pm3::abp_decompress(blob.data(), blob.size(), out, &d));
      CHECK(d == 6); CHECK(std::string(out.begin(), out.end()) == "ABCABC"); }
    // ABP: bad magic
    { uint8_t z[0x20] = {0}; pm3::Bytes o; CHECK(!pm3::abp_decompress(z, sizeof z, o)); }

    std::printf(fails ? "FAILED\n" : "all tests passed\n");
    return fails ? 1 : 0;
}
