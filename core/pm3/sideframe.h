// Decorative side panels for the letterbox bars of the 4:3 game on a wider screen (iOS landscape).
// The artwork is one 160x720 wooden panel per side (ios_native/Resources/side_left.png / side_right.png). The panel is scaled to the full bar height and
// stretched horizontally to the full bar width (a very narrow bar shows only the part next to the game, unsqueezed). Portable C++ (no UIKit/Metal) so it can be tested off-device.
#pragma once
#include <cstdint>
#include <vector>

namespace pm3 {

// panel : pw x ph pixels, 0xAARRGGBB (alpha ignored).  out : out_w x out_h pixels, 0xFFRRGGBB.
// left_bar = true  -> the bar is on the LEFT of the game (the game edge is the bar's right edge; use the left panel art)
//            false -> the bar is on the RIGHT of the game (game edge = bar's left edge; use the right panel art)
void compose_side_bar(const uint32_t* panel, int pw, int ph, int out_w, int out_h, bool left_bar, std::vector<uint32_t>& out);

}  // namespace pm3
