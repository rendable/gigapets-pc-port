// One emulated 1/60s tick: runs the CPU for a frame's worth of cycles with the ROM hooks and
// cycle-interleaved audio generation, then per-tick bookkeeping.
#pragma once

#include "base.h"

// Advances the whole emulated machine by exactly one 1/60s tick.
void sim_tick();
