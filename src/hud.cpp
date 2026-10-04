// On-screen overlays: FPS counter and the always-on pet stat HUD.

#include "common.h"

float fps_frame_times_ms[FPS_WINDOW] = {};

int fps_frame_index = 0;
int fps_frame_count = 0;
