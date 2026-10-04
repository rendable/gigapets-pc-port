// Player-affecting mods: movement speed, no-clip, and shadow removal.

#include "common.h"

int g_movement_speed_idx = 0;
int16_t g_last_player_x = 0;
int16_t g_last_player_y = 0;
bool g_have_last_player_pos = false;
uint16_t g_last_location_id = 0;
bool g_have_last_location_id = false;
int g_location_change_grace_frames = 0;
NoClipPatch g_noclip_patches[3];
bool g_cheat_noclip = false;
uint16_t g_shadow_patch_original = 0;
bool g_shadows_disabled = false;

int32_t find_word_pattern(const uint16_t* haystack, uint32_t haystack_len, const uint16_t* needle, uint32_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len) return -1;
    for (uint32_t i = 0; i + needle_len <= haystack_len; i++) {
        bool match = true;
        for (uint32_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return (int32_t)i;
    }
    return -1;
}

void find_noclip_patch_addresses() {
    // Each pattern is the Cheat Engine AOB bytes reinterpreted as little-
    // endian uint16_t words; the patch target is always +4 bytes (+2 words)
    // into the match, matching the CT script's "NoClipN+4: db 41 92".
    static const uint16_t pattern1[] = { 0x9F0F, 0xC1E5, 0x9240, 0x9888, 0x9A90, 0xDA88, 0x2049 };
    static const uint16_t pattern2[] = { 0x9F0F, 0xC253, 0x9240, 0x9F0F, 0xC254, 0x9241, 0x0049 };
    static const uint16_t pattern3[] = { 0x9F0F, 0xC2FC, 0x9240, 0x9F0F, 0xC305, 0x980A, 0x0841 };
    const uint16_t* patterns[3] = { pattern1, pattern2, pattern3 };
    for (int p = 0; p < 3; p++) {
        int32_t match = find_word_pattern(rom, 0x400000, patterns[p], 7);
        if (match >= 0) {
            g_noclip_patches[p].patch_word_addr = match + 2;
            g_noclip_patches[p].original_value = rom[match + 2];
        }
    }
    

}

void set_shadows_disabled(bool disabled) {
    rom[SHADOW_PATCH_ADDR] = disabled ? SHADOW_RETF_OPCODE : g_shadow_patch_original;
}

void set_noclip_enabled(bool enabled) {
    for (int p = 0; p < 3; p++) {
        if (g_noclip_patches[p].patch_word_addr < 0) continue;
        rom[g_noclip_patches[p].patch_word_addr] = enabled
            ? (uint16_t)(g_noclip_patches[p].original_value | 0x0001)
            : g_noclip_patches[p].original_value;
    }
}

// Per-tick player mods: room-change detection and the movement speed multiplier.
void player_mods_tick() {
    // Room/area change detection via LOCATION_ID_ADDR. (RAM 0x21E3 looks like an area index but is
    // actually a transient per-sprite-slot index that changes every tick - don't use it here.)
    uint16_t current_location_id = ram[LOCATION_ID_ADDR];
    bool location_transitioned = g_have_last_location_id && current_location_id != g_last_location_id;
    if (location_transitioned) g_location_change_grace_frames = 2;
    bool suppress_amplify = g_location_change_grace_frames > 0;
    if (g_location_change_grace_frames > 0) g_location_change_grace_frames--;
    g_last_location_id = current_location_id; g_have_last_location_id = true;

    // Movement speed: multiply whatever delta the game's own movement code just applied to the
    // player's position this tick (the ROM can't take more than one input per tick). Skipped for a
    // couple of ticks after a room change, and for implausibly large deltas (teleports/transitions),
    // either of which would fling the player out of the new room's bounds.
    int speed_mult = MOVEMENT_SPEED_LEVELS[g_movement_speed_idx];
    int16_t cur_x = (int16_t)ram[PLAYER_WORLD_X];
    int16_t cur_y = (int16_t)ram[PLAYER_WORLD_Y];
    if (g_have_last_player_pos && speed_mult > 1 && !suppress_amplify) {
        int16_t dx = cur_x - g_last_player_x;
        int16_t dy = cur_y - g_last_player_y;
        if (abs((int)dx) > MOVEMENT_TELEPORT_THRESHOLD) dx = 0;
        if (abs((int)dy) > MOVEMENT_TELEPORT_THRESHOLD) dy = 0;
        if (g_minipet_spawned && (dx != 0 || dy != 0)) {
            // Backfill the intermediate normal-speed steps the amplified jump skips, so the minipet's
            // trail distance stays constant at any speed (see minipet_trail_record_step).
            // Capped by the trail buffer's free space (same formula as GetMiniPetTrailFreeSpace,
            // ROM 0x045F3E): the buffer has 60 slots and the ROM only consumes 1-2 per tick, so
            // writing more would lap the read side and corrupt it (the minipet keeps teleporting).
            // The 30-slot margin keeps us above the ROM's own 24-slot "fall back to zap-in" threshold.
            uint16_t read_idx = ram[MINIPET_TRAIL_READ_IDX_ADDR];
            uint16_t write_idx = ram[MINIPET_TRAIL_WRITE_IDX_ADDR];
            int free_space = (read_idx <= write_idx) ? (0x3D - (write_idx - read_idx)) : ((read_idx - write_idx) + 1);
            int safe_budget = free_space - 30;
            if (safe_budget < 0) safe_budget = 0;
            int steps = std::min(speed_mult - 1, safe_budget);
            for (int step = 1; step <= steps; step++) {
                minipet_trail_record_step((int16_t)(g_last_player_x + dx * step), (int16_t)(g_last_player_y + dy * step));
            }
        }
        if (dx != 0) cur_x = (int16_t)(cur_x + dx * (speed_mult - 1));
        if (dy != 0) cur_y = (int16_t)(cur_y + dy * (speed_mult - 1));
        ram[PLAYER_WORLD_X] = (uint16_t)cur_x;
        ram[PLAYER_WORLD_Y] = (uint16_t)cur_y;
    }
    g_last_player_x = cur_x;
    g_last_player_y = cur_y;
    g_have_last_player_pos = true;
}
