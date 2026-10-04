// Win32 window helpers (hand-declared to avoid windows.h clashing with raylib).
#pragma once

#include "base.h"

void toggle_expand_window();

// Modal error box. The app is built for the windowed subsystem (no console), so
// printf output is invisible to the player - use this for fatal startup errors.
void show_error_dialog(const char* message);
