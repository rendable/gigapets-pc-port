// Player-affecting mods: movement speed, no-clip, and shadow removal.
#pragma once

#include "base.h"

// Movement speed: applied as a post-tick delta amplification (player_mods_tick) rather than by
// patching game logic. The game's own collision-checked sub-steps already moved the player once this
// tick; we scale that observed delta up. The extra distance gets no collision check until the next
// tick re-evaluates from the new position, so high multipliers can clip slightly into walls.
static const int MOVEMENT_SPEED_LEVELS[] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static const int MOVEMENT_SPEED_LEVEL_COUNT = sizeof(MOVEMENT_SPEED_LEVELS) / sizeof(MOVEMENT_SPEED_LEVELS[0]);

// Room changes reposition the player in a single tick (a deliberate jump, not walking) and must not
// be amplified, or the player is flung outside the new room's bounds. The location id is coarse
// (broad regions, not individual buildings), so any per-tick delta above this threshold is also
// treated as a non-organic jump and zeroed.
static const int MOVEMENT_TELEPORT_THRESHOLD = 32;
extern int g_movement_speed_idx;
extern int16_t g_last_player_x;
extern int16_t g_last_player_y;
extern bool g_have_last_player_pos;
extern uint16_t g_last_location_id;
extern bool g_have_last_location_id;
extern int g_location_change_grace_frames;

// No-Clip: three "collision check returns 0" instruction sites in the ROM, found by scanning rom[]
// for their byte patterns (originally from a Cheat Engine AOB-scan script) and patched so the check
// reports free space. The CPU core is a plain interpreter with no instruction cache, so a patched
// rom[] word takes effect on the very next fetch.
struct NoClipPatch { int32_t patch_word_addr = -1; uint16_t original_value = 0; };

extern NoClipPatch g_noclip_patches[3];
extern bool g_cheat_noclip;
extern uint16_t g_shadow_patch_original;
extern bool g_shadows_disabled;

int32_t find_word_pattern(const uint16_t* haystack, uint32_t haystack_len, const uint16_t* needle, uint32_t needle_len);
void find_noclip_patch_addresses();
void set_shadows_disabled(bool disabled);
void set_noclip_enabled(bool enabled);
void player_mods_tick();
