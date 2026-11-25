// C API between the C++ core and a platform shell (iOS Metal view in ios_native/, SDL viewer, tests).
// The shell owns the run loop: it calls pm3_bridge_* from its own frame callback (MTKView draw / CADisplayLink); the core never blocks.
//   frame  : pm3_bridge_framebuffer() -> 640x480 uint32_t 0xAARRGGBB (== BGRA8Unorm), upload with replaceRegion every frame
//   layout : pm3_bridge_set_view_size() -> aspect-fit viewport for the shader / MTLViewport
//   input  : pm3_bridge_touch_*() -> mouse events polled by the game side via pm3_bridge_poll_mouse()
//   files  : pm3_bridge_set_paths(game_dir, save_dir): save_dir = Documents on iOS; lookups are case-insensitive
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Pm3Bridge Pm3Bridge;
typedef struct { float x, y, w, h, scale; } Pm3Viewport;
typedef struct { int type; int x, y, button; } Pm3MouseEvent;   // type: 0 move, 1 button down, 2 button up
enum { PM3_FMT_PAL8 = 0, PM3_FMT_RGB565 = 1, PM3_FMT_RGB555 = 2, PM3_FMT_BGR24 = 3, PM3_FMT_BGRX32 = 4 };

Pm3Bridge* pm3_bridge_create(void);
void pm3_bridge_destroy(Pm3Bridge*);
void pm3_bridge_set_paths(Pm3Bridge*, const char* game_dir, const char* save_dir);

int pm3_bridge_width(void);     // 640
int pm3_bridge_height(void);    // 480
uint32_t* pm3_bridge_framebuffer(Pm3Bridge*);
// Copies/converts a guest surface into the frame buffer (nearest-neighbour if the size differs). pal = 256 x 0x00RRGGBB for PAL8.
void pm3_bridge_present_surface(Pm3Bridge*, const uint8_t* src, int w, int h, int pitch, int fmt, const uint32_t* pal);

void pm3_bridge_set_view_size(Pm3Bridge*, float view_w, float view_h, int integer_scale);
Pm3Viewport pm3_bridge_viewport(const Pm3Bridge*);

void pm3_bridge_touch_begin(Pm3Bridge*, int id, float x, float y);
void pm3_bridge_touch_move(Pm3Bridge*, int id, float x, float y);
void pm3_bridge_touch_end(Pm3Bridge*, int id, float x, float y);
void pm3_bridge_touch_cancel(Pm3Bridge*, int id);
void pm3_bridge_right_click(Pm3Bridge*, float x, float y);   // right mouse button at a view point (iOS: two-finger tap / long press) = the PC game's back / cancel
int pm3_bridge_poll_mouse(Pm3Bridge*, Pm3MouseEvent* out);   // 1 if an event was returned

// Runs the original PM3WIN.EXE (x86 interpreter + Win32 HLE, core/pm3/x86host.cpp) on a worker thread. Call after pm3_bridge_set_paths().
//   * the frame buffer is refreshed from the guest's primary surface after every slice -> read it with pm3_bridge_copy_frame()
//   * touch events (pm3_bridge_touch_*) become WM_MOUSEMOVE / WM_LBUTTONDOWN / WM_LBUTTONUP for the guest
//   * timeGetTime() follows the wall clock; when the guest only polls for messages the worker sleeps (battery)
int pm3_bridge_start_game(Pm3Bridge*);        // 1 on start, 0 if already running or PM3WIN.EXE is missing
void pm3_bridge_stop_game(Pm3Bridge*);        // blocks until the worker has stopped
int pm3_bridge_game_running(const Pm3Bridge*);
// Audio: the guest's waveOut PCM is converted to 16-bit stereo at PM3_AUDIO_RATE and mixed into a small queue; the shell's audio callback pulls from it.
// Sounds that start while another is still queued are added on top of it (mixed). Silence is written when nothing is queued.
#define PM3_AUDIO_RATE 44100
int pm3_bridge_pull_audio(Pm3Bridge*, int16_t* dst_stereo, int frames);   // returns the number of frames that held sound (the rest of dst is zeroed)
void pm3_bridge_set_paused(Pm3Bridge*, int paused);   // 1 = freeze the guest (app went to background); its clock does not advance while paused
void pm3_bridge_copy_frame(Pm3Bridge*, uint32_t* dst640x480);   // thread-safe copy of the current frame (use this instead of pm3_bridge_framebuffer while the game runs)

// File access for the game side (case-insensitive, save dir shadows game dir). Returns a FILE* usable with stdio, or NULL.
void* pm3_bridge_fopen(Pm3Bridge*, const char* guest_path, const char* mode);
int pm3_bridge_file_exists(Pm3Bridge*, const char* guest_path);

#ifdef __cplusplus
}
#endif
