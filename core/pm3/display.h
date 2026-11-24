// Frame buffer + presentation maths shared by every front end (SDL viewer, iOS Metal view).
//  * Frame buffer = uint32_t per pixel, 0xAARRGGBB  (little-endian memory order B,G,R,A == MTLPixelFormatBGRA8Unorm, SDL_PIXELFORMAT_ARGB8888).
//  * Aspect-fit (letterbox) viewport + inverse mapping so touches land on the right logical pixel.
#pragma once
#include <cstdint>

namespace pm3 {

constexpr int kGameW = 640, kGameH = 480;      // logical resolution (the game runs 320x240 art at x2)

struct Viewport { float x = 0, y = 0, w = 0, h = 0, scale = 1; int game_w = kGameW, game_h = kGameH; };

// Largest game_w:game_h rectangle that fits into view_w x view_h, centred. integer_only snaps the scale down to a whole number (>=1) for crisp pixels.
Viewport fit_viewport(float view_w, float view_h, int game_w = kGameW, int game_h = kGameH, bool integer_only = false);
// View point (same units as the viewport) -> game pixel, clamped to the frame. Returns false if the point is in the letterbox bars.
bool view_to_game(const Viewport& vp, float vx, float vy, int* gx, int* gy);
// Game pixel -> view point (top-left of the pixel).
void game_to_view(const Viewport& vp, int gx, int gy, float* vx, float* vy);

enum class PixFmt { Pal8, Rgb565, Rgb555, Bgr24, Bgrx32 };
// Converts a source surface to 0xAARRGGBB (alpha forced to 0xFF). `pal` = 256 entries 0x00RRGGBB / 0xAARRGGBB (Pal8 only).
// src_pitch is in bytes. dst is tightly packed (w pixels per row).
void convert_to_argb(const uint8_t* src, int w, int h, int src_pitch, PixFmt fmt, const uint32_t* pal, uint32_t* dst);
// Windows RGBQUAD / DirectDraw PALETTEENTRY style palette: bytes B,G,R,x  ->  0xFFRRGGBB.
void palette_from_bgrx(const uint8_t* bgrx, int count, uint32_t* out);

}  // namespace pm3
