// Image decoders for PM3 formats (GDT / AGP_ 8bpp + 24bpp codecs, DIB-in-GDT, WPC sheets, WMP).
// Ports of py/agp.py, py/agp24.py and the dispatch logic of py/export_images.py.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pm3 {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;  // w*h*4, tightly packed, alpha = 255
};

struct Indexed8 {  // 8bpp palettised image, rows padded to `pitch`
    int w = 0, h = 0, pitch = 0;
    std::vector<uint8_t> pal;  // RGB triples
    std::vector<uint8_t> pix;  // pitch*h (+ slack)
    Image toRgba() const;
};

// All decoders throw std::runtime_error on malformed input; the try_ wrappers return false + message.

// 8bpp GDT (header mode 0). `r` = GDT bytes (AGP_ without its 4-byte magic).
Indexed8 decode_gdt8(const uint8_t* r, size_t n);
// 24bpp GDT / WMP payload (header mode 2).
Image decode_gdt24(const uint8_t* r, size_t n);
// GDT that stores a Windows DIB at +10 (1/4/8/24/32 bpp, uncompressed).
Image decode_gdt_dib(const uint8_t* r, size_t n);
// Same dispatch as py/export_images.py for GDT entries of PM3SYS0.
Image decode_gdt_auto(const uint8_t* r, size_t n);
// WPC: 4 u32 offsets (+1 unused); every non-zero offset starts an 8bpp GDT "sheet".
std::vector<Image> decode_wpc_sheets(const uint8_t* r, size_t n);
size_t wpc_sheet_count(const uint8_t* hdr20, size_t n);  // needs the first 20 bytes only
// WMP: u32 offset @0, 24bpp GDT payload there.
Image decode_wmp(const uint8_t* r, size_t n);

bool write_ppm(const std::string& path, const Image& im);

}  // namespace pm3
