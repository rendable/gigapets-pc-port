// On-screen overlays: FPS counter and the always-on pet stat HUD.
#pragma once

#include "base.h"

// FPS / 1% low tracking, for diagnosing audio stutter caused by the game
// loop actually running below 60fps in real wall-clock time.
static const int FPS_WINDOW = 240; // ~4s of history at 60fps
extern float fps_frame_times_ms[FPS_WINDOW];
extern int fps_frame_index;
extern int fps_frame_count;
void hud_record_frame_time(float frame_ms);
void hud_draw_fps(float frame_ms);
void hud_draw_stats();
