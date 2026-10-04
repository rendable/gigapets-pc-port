// Player-affecting mods: movement speed, no-clip, and shadow removal.
#pragma once

#include "base.h"

// Movement speed: applied as a post-frame delta amplification (see the main
// loop, right after the CPU's per-frame instruction budget is spent) rather
// than by patching game logic - the game's own ~7 collision-checked
// sub-steps per frame already moved the player once this frame; we just
// scale that observed delta up further. This means the "extra" portion of
// movement at speeds above 1x does not get its own collision check until
// the *next* frame's normal movement re-evaluates from the new position -
// an acceptable tradeoff for a cheat, but it can clip slightly into walls
// at higher multipliers before the game's own collision catches up.
static const int MOVEMENT_SPEED_LEVELS[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static const int MOVEMENT_SPEED_LEVEL_COUNT = sizeof(MOVEMENT_SPEED_LEVELS) / sizeof(MOVEMENT_SPEED_LEVELS[0]);

// Room/area transitions reposition the player to a new spawn point in a
// single frame - a large, deliberate jump, not organic walking - and must
// not be amplified the same way (doing so was flinging the player outside
// the new room's valid bounds, getting them stuck). Location ID is coarse
// (it tracks broad unlockable game regions - Downtown, Beach, etc, per the
// CT table's dropdown - not individual rooms/buildings, so it won't catch
// every building-entry transition), but the raw per-tick magnitude clamp
// below is the real backstop: any single-tick delta big enough to matter
// (a teleport-style reposition) is far larger than this threshold, so it
// gets zeroed regardless of whether a location-ID change also fired.
static const int MOVEMENT_TELEPORT_THRESHOLD = 32; // raw per-frame delta beyond this is treated as a non-organic jump
extern int g_movement_speed_idx;
extern int16_t g_last_player_x;
extern int16_t g_last_player_y;
extern bool g_have_last_player_pos;
extern uint16_t g_last_location_id;
extern bool g_have_last_location_id;
extern int g_location_change_grace_frames;
// No-Clip: ported directly from the user's own Cheat Engine AOB-scan script
// (3 "collision check returns 0" instruction sites, each patched so the
// result is 1/free-space instead). Cheat Engine found these by scanning our
// own process's raw memory - i.e. this rom[] array - so the same patterns
// are searched here directly, word-addressed instead of byte-addressed.
// Our CPU core runs the plain interpreter (unsp.cpp's execute_run(), not
// the DRC recompiler in unspdrc.cpp), so there is no instruction cache to
// invalidate - a patched rom[] word takes effect on the very next fetch.
struct NoClipPatch { int32_t patch_word_addr = -1; uint16_t original_value = 0; };

extern NoClipPatch g_noclip_patches[3];
extern bool g_cheat_noclip;
extern uint16_t g_shadow_patch_original;
extern bool g_shadows_disabled;

int32_t find_word_pattern(const uint16_t* haystack, uint32_t haystack_len, const uint16_t* needle, uint32_t needle_len);
void find_noclip_patch_addresses();
void set_shadows_disabled(bool disabled);
void set_noclip_enabled(bool enabled);
