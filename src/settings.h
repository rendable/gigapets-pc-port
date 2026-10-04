// Runtime options toggled from the Mod Menu (render filter, FPS limit, HUD toggles, ...).
#pragma once

#include "base.h"

enum RenderFilter { FILTER_CRISP = 0, FILTER_SMOOTH = 1, FILTER_SHARP = 2, FILTER_CRT = 3, FILTER_COUNT = 4 };

extern int g_render_filter;
extern int g_fps_limit_idx;
extern int g_export_scale_idx;
extern bool g_interp_enabled;
extern bool g_quest_arrow_enabled;
extern bool g_live_sprite_capture;
extern bool g_extractor_skip_player;
extern bool g_show_stat_hud;
extern bool g_show_fps_hud;

void save_fps_limit();
