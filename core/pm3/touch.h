// Touch -> mouse emulation for a point-and-click game (read from the notes: move = hover, lift = click).
//   touch_begin : cursor jumps under the finger (Move)                      -- hover/tooltips react
//   touch_move  : Move events while dragging                                -- hover/highlight follow the finger
//   touch_end   : Move to the last position, then ButtonDown + ButtonUp      -- the click happens on release
//   touch_cancel: no click.
//   right_click : two-finger tap / long press on iOS = mouse right button (PC: right click = back / cancel).
// Only the first finger is tracked; a touch that starts in the letterbox bars is ignored. Coordinates are in view units
// (iOS points or pixels, whatever the viewport was built with) and are mapped to 640x480 logical pixels.
#pragma once
#include <deque>

#include "pm3/display.h"

namespace pm3 {

struct MouseEvent { enum Type { Move, ButtonDown, ButtonUp } type = Move; int x = 0, y = 0; int button = 0; };   // button 0 = left

class TouchMouse {
public:
    void set_viewport(const Viewport& vp) { vp_ = vp; }
    void touch_begin(int id, float vx, float vy);
    void touch_move(int id, float vx, float vy);
    void touch_end(int id, float vx, float vy);
    void touch_cancel(int id);
    void right_click(float vx, float vy);      // right button (the PC game's "back / cancel"): Move, then ButtonDown + ButtonUp with button = 1; drops a finger that is still tracked
    bool poll(MouseEvent* ev);                 // pops the next event
    bool tracking() const { return active_; }

private:
    void push(MouseEvent::Type t, int x, int y) { MouseEvent e; e.type = t; e.x = x; e.y = y; q_.push_back(e); }
    Viewport vp_; bool active_ = false; int id_ = 0; int lx_ = 0, ly_ = 0; std::deque<MouseEvent> q_;
};

}  // namespace pm3
