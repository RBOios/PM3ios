// TFN bitmap fonts (FONT22.TFN / FONT24.TFN, "TGLFontFile"). Layout (derived from the EXE loader @0x41350E):
//   "TGLFontFile" | u32 size | u32 mult(=24) | u8 width[0x47C0] | half-width block (128 glyphs x 2*mult bytes:
//   2 bytes/row, 16 px, MSB first) | 18368 full-width glyphs x 3*mult bytes (3 bytes/row, 24 px, MSB first).
// Full-width glyph index = cp949 code - 0x8140 (lead 0x81..0xC8); half-width index = ASCII byte (< 0x80).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pm3/image.h"

namespace pm3 {

class Tfn {
public:
    bool load(const uint8_t* d, size_t n, std::string* err = nullptr);
    int size() const { return size_; }            // nominal point size (22 / 24)
    int height() const { return rows_; }          // bitmap rows (24)
    // 1bpp glyph rows; returns false if code is out of range. `wide` = full-width (24 px) else 16 px.
    bool glyph(uint16_t code, bool wide, const uint8_t** rows, int* bytes_per_row) const;
    // Draws one char into RGBA canvas at (x,y), colour 0xRRGGBB; returns advance in px (12 half, 24 full).
    int draw_char(Image& im, int x, int y, uint16_t code, bool wide, uint32_t rgb) const;
    // Draws a cp949 byte string; returns end x.
    int draw_text(Image& im, int x, int y, const std::string& cp949, uint32_t rgb) const;
    int text_width(const std::string& cp949) const;
private:
    int size_ = 0, rows_ = 0;
    std::vector<uint8_t> half_, full_, widths_;
};

// Dialogue box: darkens a strip at the bottom of `im` and draws the word-wrapped cp949 text (24 px glyphs) on it.
void draw_dialogue_box(Image& im, const Tfn& font, const std::string& cp949);

}  // namespace pm3
