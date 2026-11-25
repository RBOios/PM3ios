#include "pm3/touch.h"

namespace pm3 {

void TouchMouse::touch_begin(int id, float vx, float vy) {
    if (active_) return;                                   // second finger: ignored
    int gx, gy; if (!view_to_game(vp_, vx, vy, &gx, &gy)) return;   // started in the bars
    active_ = true; id_ = id; lx_ = gx; ly_ = gy; push(MouseEvent::Move, gx, gy);
}

void TouchMouse::touch_move(int id, float vx, float vy) {
    if (!active_ || id != id_) return;
    int gx, gy; view_to_game(vp_, vx, vy, &gx, &gy);       // clamped to the frame while dragging
    if (gx == lx_ && gy == ly_) return;
    lx_ = gx; ly_ = gy; push(MouseEvent::Move, gx, gy);
}

void TouchMouse::touch_end(int id, float vx, float vy) {
    if (!active_ || id != id_) return;
    int gx, gy; view_to_game(vp_, vx, vy, &gx, &gy);
    if (gx != lx_ || gy != ly_) push(MouseEvent::Move, gx, gy);
    push(MouseEvent::ButtonDown, gx, gy); push(MouseEvent::ButtonUp, gx, gy);
    active_ = false;
}

void TouchMouse::touch_cancel(int id) { if (active_ && id == id_) active_ = false; }

void TouchMouse::right_click(float vx, float vy) {
    active_ = false;                                       // a left touch that was still down becomes this gesture: no left click on release
    int gx, gy; if (!view_to_game(vp_, vx, vy, &gx, &gy)) return;
    lx_ = gx; ly_ = gy; push(MouseEvent::Move, gx, gy);
    MouseEvent d; d.type = MouseEvent::ButtonDown; d.x = gx; d.y = gy; d.button = 1; q_.push_back(d);
    MouseEvent u; u.type = MouseEvent::ButtonUp; u.x = gx; u.y = gy; u.button = 1; q_.push_back(u);
}

bool TouchMouse::poll(MouseEvent* ev) { if (q_.empty()) return false; *ev = q_.front(); q_.pop_front(); return true; }

}  // namespace pm3
