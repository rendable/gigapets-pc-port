// Runtime options toggled from the Mod Menu (render filter, FPS limit, HUD toggles, ...).

#include "common.h"

int g_render_filter = 0;
int g_fps_limit_idx = 0;
int g_export_scale_idx = 3; // default index 3 -> 8x
bool g_interp_enabled = false;

void save_fps_limit() {}

bool g_quest_arrow_enabled = false;
bool g_live_sprite_capture = false; // F7 toggle - see export_visible_sprite_clusters

// Mod Menu toggle, applies to both F8 and F7 (export_visible_sprite_clusters).
// This is a player-centered camera - g_cameraScrollX/Y is literally the
// player's own world position (see PLAYER_WORLD_X/Y) - so the player's own
// sprite cluster lands at/near screen center every single frame by
// construction, unlike any other object. Used as the "is this the player"
// heuristic rather than tracking a specific sprite slot, since the ROM
// assigns sprite slots dynamically and there's no fixed player slot index.
bool g_extractor_skip_player = false;
bool g_show_stat_hud = false;
bool g_show_fps_hud = false;
