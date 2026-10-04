// Minipet spawn/despawn (replicates the RAM effects of the real zap-in animation).

#include "common.h"

// Species -> animation frame ID, literal values from PlayMiniPetZapAnimAlt's
// own stack-local table (not a ROM table - baked into the function itself).
static const uint16_t MINIPET_ANIM_FRAME_IDS[8] = { 0x57, 0x71, 0x6c, 0x121, 0x135, 0x150, 0x155, 0x13a };

int g_minipet_picker_idx = 0;  // 0-7, browsing only - no side effect, doesn't spawn/despawn anything
bool g_minipet_spawned = false; // separate action row toggles this

void spawn_minipet(int species_idx) {
    if (species_idx < 0 || species_idx > 7) return;
    int16_t anchor_x = (int16_t)ram[PLAYER_WORLD_X];
    int16_t anchor_y = (int16_t)ram[PLAYER_WORLD_Y];

    // Trail buffer: slot 0 seeds at the player's position; each later slot's
    // X is the previous slot's X + 2 (a ramp, not identical - confirmed via
    // disassembly), Y and poseId constant across all 20 slots.
    ram[MINIPET_TRAIL_BASE + 0] = (uint16_t)anchor_x;
    ram[MINIPET_TRAIL_BASE + 1] = (uint16_t)(anchor_y - 6);
    ram[MINIPET_TRAIL_BASE + 2] = 0;
    for (int i = 1; i < MINIPET_TRAIL_SLOT_COUNT; i++) {
        uint16_t prev_x = ram[MINIPET_TRAIL_BASE + (i - 1) * 3];
        ram[MINIPET_TRAIL_BASE + i * 3 + 0] = (uint16_t)(prev_x + 2);
        ram[MINIPET_TRAIL_BASE + i * 3 + 1] = (uint16_t)(anchor_y - 6);
        ram[MINIPET_TRAIL_BASE + i * 3 + 2] = 0;
    }
    ram[MINIPET_TRAIL_READ_IDX_ADDR] = MINIPET_TRAIL_SLOT_COUNT - 1;
    ram[MINIPET_TRAIL_WRITE_IDX_ADDR] = 0;

    // Animation struct field 0 must start as the sentinel 0x7B, not a species frame id.
    // FeedOrAdvanceMiniPet (ROM 0x046172) runs every tick once tracking is nonzero and treats any
    // other value as an unhandled found-item reaction, which cascades into a forced despawn. The
    // species frame id is only valid during the real zap-in ceremony screen, which hands off with
    // the struct already reset to this sentinel; skipping to it here skips that one-time visual but
    // lands directly in the stable tracked state. See docs/ROM_NOTES.md, "Minipets".
    ram[MINIPET_ANIM_STRUCT_ADDR + 0] = 0x7B;
    ram[MINIPET_ANIM_STRUCT_ADDR + 1] = 2;
    ram[MINIPET_ANIM_STRUCT_ADDR + 2] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 3] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 4] = 14;
    ram[MINIPET_ANIM_STRUCT_ADDR + 5] = 2;
    ram[MINIPET_ANIM_STRUCT_ADDR + 6] = 0x1000;
    ram[MINIPET_ANIM_STRUCT_ADDR + 7] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 8] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 9] = 0;

    // This ROM variable doubles as the species index (read by PlayMiniPetAnimForRoom and
    // PlayMiniPetWalkAnim).
    ram[GAME_CURRENT_ROOM_ID_ADDR] = (uint16_t)species_idx;

    // Simulate "accessory handheld physically connected".
    ram[MINIPET_LINK_AVAILABLE_ADDR] = 1;

    ram[MINIPET_TRACKING_FLAG_ADDR] = 2; // matches InitMiniPetZapAnimation's own real write exactly
}

// Replicates TickMiniPetZapAndFollowState's own trail-buffer write (ROM 0x046267-0x046289:
// decrement the write index, wrap 0 -> 0x3B, store {X, Y-6, pose 0}) so the movement-speed mod can
// backfill the steps a normal-speed walk would have recorded. The follower trails by a fixed number
// of slots, so without this each slot covers more distance and the pet lags farther behind.
void minipet_trail_record_step(int16_t x, int16_t y) {
    uint16_t write_index = ram[MINIPET_TRAIL_WRITE_IDX_ADDR];
    write_index = (write_index == 0) ? 0x3b : (write_index - 1);
    ram[MINIPET_TRAIL_WRITE_IDX_ADDR] = write_index;
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 0] = (uint16_t)x;
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 1] = (uint16_t)(y - 6);
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 2] = 0;
}

void despawn_minipet() {
    ram[MINIPET_ANIM_STRUCT_ADDR] = 0xFFFF; // -1, matches the real despawn state's own write
    ram[MINIPET_TRACKING_FLAG_ADDR] = 0;
    ram[MINIPET_LINK_AVAILABLE_ADDR] = 0; // "unplug" the simulated handheld
}
