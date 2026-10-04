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
    // Room/area transition detection - see LOCATION_ID_ADDR comment
    // above. GAME_CURRENT_AREA_INDEX_ADDR (0x21E3) is NOT used here
    // despite the name/comment inherited from an earlier, unverified
    // recovery pass: Ghidra shows every reference to it is a READ
    // from per-slot helpers (SetAreaPositionX/Y, GetSlotParam1/2,
    // DrawActiveSlotSprites) with zero writers in ROM - it's a
    // transient "active sprite/object slot" index, not a room ID,
    // and changes on essentially every tick regardless of the
    // player's own movement. Using it here made location_transitioned
    // true almost constantly, permanently suppressing the speed
    // cheat. Location ID + the raw per-tick delta clamp below are
    // the real safety net.
    uint16_t current_location_id = ram[LOCATION_ID_ADDR];
    bool location_transitioned = g_have_last_location_id && current_location_id != g_last_location_id;
    if (location_transitioned) g_location_change_grace_frames = 2;
    bool suppress_amplify = g_location_change_grace_frames > 0;
    if (g_location_change_grace_frames > 0) g_location_change_grace_frames--;
    g_last_location_id = current_location_id; g_have_last_location_id = true;

    // Movement speed cheat: amplify whatever delta the game's own
    // movement code just applied to the player's world position this
    // tick, rather than faking extra input presses (which the ROM's
    // own per-tick movement/animation logic isn't built to receive
    // more than one of). Suppressed across room/area transitions
    // (see above) and clamped against an already-huge raw delta as a
    // second safety net - either would otherwise fling the player
    // outside the new room's valid bounds instead of just moving
    // them faster.
    int speed_mult = MOVEMENT_SPEED_LEVELS[g_movement_speed_idx];
    int16_t cur_x = (int16_t)ram[PLAYER_WORLD_X];
    int16_t cur_y = (int16_t)ram[PLAYER_WORLD_Y];
    if (g_have_last_player_pos && speed_mult > 1 && !suppress_amplify) {
        int16_t dx = cur_x - g_last_player_x;
        int16_t dy = cur_y - g_last_player_y;
        if (abs((int)dx) > MOVEMENT_TELEPORT_THRESHOLD) dx = 0;
        if (abs((int)dy) > MOVEMENT_TELEPORT_THRESHOLD) dy = 0;
        if (g_minipet_spawned && (dx != 0 || dy != 0)) {
            // Backfill the intermediate normal-speed steps this
            // amplified jump is skipping over, so the minipet's
            // trail-following distance stays constant regardless of
            // speed multiplier - see minipet_trail_record_step().
            //
            // Capped against the real trail buffer's current free
            // space (same formula as GetMiniPetTrailFreeSpace, ROM
            // 0x045f3e): the buffer only has 60 slots and the ROM's
            // own read side only advances 1-2 slots/tick regardless
            // of player speed, so writing more backfill entries per
            // tick than that lets the write side lap the read side
            // and corrupt the circular buffer - the "minipet just
            // keeps teleporting" symptom above ~2x. Keeping a 30-slot
            // margin above the real 24-slot fallback threshold means
            // our own bulk writes never spuriously trip Follow's
            // "fall back to zap-in" check either.
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
