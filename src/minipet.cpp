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

    // Animation struct: field 0 (animId) MUST be the sentinel 0x7B, not a
    // species-specific frame ID - confirmed via live MAME testing + full
    // disassembly of FeedOrAdvanceMiniPet (ROM 0x046172), which runs every
    // single tick once tracking is nonzero and treats "struct[0] != 0x7B" as
    // an unhandled found-item-reaction event: it overwrites the struct with
    // its own animId=0x7B (exact match to what was observed clobbering this
    // struct one tick after spawning), resets g_currentRoomId, and rewrites
    // the tracking flag - a cascade that ends in the animation getting
    // force-despawned. The species-specific frame ID (from
    // MINIPET_ANIM_FRAME_IDS) is only valid transiently during the brief
    // real zap-in ceremony screen (EnterActivityAndRunLoop), which hands off
    // with the struct already reset to this sentinel before normal per-tick
    // logic (TickMiniPetZapAndFollowState et al) ever sees it - skipping
    // straight to the sentinel here skips that one-time visual but lands
    // directly in the stable, correctly-tracked state.
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

    // g_currentRoomId doubles as the species index - confirmed ground truth
    // via PlayMiniPetAnimForRoom/PlayMiniPetWalkAnim's decompile (not
    // PlayMiniPetZapAnimAlt as originally guessed - that table is never
    // actually reached since flag never passes through state 3 here).
    ram[GAME_CURRENT_ROOM_ID_ADDR] = (uint16_t)species_idx;

    // Simulate "handheld physically connected" - harmless to keep even
    // though the real despawn fix turned out to be the sentinel-gate patch
    // in the main tick loop, not this flag.
    ram[MINIPET_LINK_AVAILABLE_ADDR] = 1;

    ram[MINIPET_TRACKING_FLAG_ADDR] = 2; // matches InitMiniPetZapAnimation's own real write exactly
}

// Replicates TickMiniPetZapAndFollowState's own trail-buffer write exactly
// (ROM 0x046267-0x046289: decrement writeIndex, wrap 0->0x3b, write
// {X, Y-6, poseId=0}) so the movement-speed cheat can backfill the
// intermediate steps a real normal-speed walk would have recorded. Without
// this, the amplified per-tick jump gets recorded as a single trail entry,
// and since the follower always trails by a fixed NUMBER of slots (not a
// fixed distance), each slot then represents proportionally more real
// distance - the pet visibly lags farther behind the faster you move.
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
