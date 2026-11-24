// Scene composition helpers: WMP background scenes (port of py/wmp_render.py build()/frames()) and
// EVX -> (background, sprite) extraction used by the viewer's SCENE pages.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "pm3/fpa.h"
#include "pm3/image.h"
#include "pm3/wbd.h"

namespace pm3 {

// 640x480 background frames. Frame 0 = static background + overlays at their first frame; when the
// scene has animated overlay objects there are up to 24 frames (lcm of the object frame counts),
// shown at kWmpFrameMs each.
constexpr int kWmpFrameMs = 150;
struct WmpScene {
    std::vector<Image> frames;
};
WmpScene wmp_scene_load(const uint8_t* r, size_t n);   // throws std::runtime_error

struct ScenePortrait { int slot, hi, lo, x, y; };   // WBD "B<hi:3><lo:3>" at slot position (x, y) (op 4a units)

// One dialogue line (op 71 narration / op 84 speech) with the scene state at that moment.
struct SceneStep {
    std::string text;                       // cp949, ESC codes already resolved/stripped, '\n' kept
    int op = 0, who = -1;                   // 0x71 / 0x84, first arg of op 84
    int bg = -1, sprite = -1;
    int ticks = 0;                          // WAIT ticks (1/30 s) the script spends after this line, before the next line
    std::vector<ScenePortrait> portraits;
};
// Removes the engine's inline codes: ESC '/' N -> "N" placeholder "?", ESC '[' ... ESC ']' markers, ESC 's'NN.
std::string evx_clean_text(const std::string& raw);

struct EvxSceneInfo {
    std::vector<SceneStep> steps;
    std::vector<ScenePortrait> portraits;   // state of the 4 portrait slots at the end of the run (loaded ones only)
    std::vector<int> backgrounds;   // every BACKGROUND (op 6f / 8b mode 2) id, in execution order
    std::vector<int> sprites;       // every SPRITES (op 5f) id, in execution order (-1 = none)
    int bg() const { return backgrounds.empty() ? -1 : backgrounds.back(); }
    int sprite() const {            // last valid sprite id
        for (size_t i = sprites.size(); i-- > 0;) if (sprites[i] >= 0) return sprites[i];
        return -1;
    }
};
// Runs the EVX script (age=12, seed=1, 60000 steps) and records what it asked to display.
bool evx_scene_info(const Fpa& sys0, const FpaEntry& e, EvxSceneInfo& out);

// Screen position of a portrait rect: (rec.dst + 2*x - 256, rec.dst + 2*y)   (EXE 0x4311A8 / 0x43155F).
// `frame` = Wbd::render() output (bbox-sized, transparent background).
void draw_portrait(const Wbd& w, const Image& frame, int x, int y, Image& canvas);

}  // namespace pm3
