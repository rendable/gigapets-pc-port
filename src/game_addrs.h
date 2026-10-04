// Named RAM addresses in the emulated game, verified against the ROM (see docs/ROM_NOTES.md).
// All are word addresses into ram[] unless noted.
#pragma once

#include "base.h"

// Player world position. Despite the ROM's name for them (g_cameraScrollX/Y), these are what
// actually change while walking (+-2/+-3 per tick, written by HandleMiniPetRoomInputAndStoryLogic,
// ROM 0x0130BE). The ROM's g_playerWorldPosX/Y at 0x1AB5/0x1AB6 are only written on room entry.
static const uint16_t PLAYER_WORLD_X = 0x1CC1;
static const uint16_t PLAYER_WORLD_Y = 0x1CC2;
static const uint16_t LOCATION_ID_ADDR = 0x1AA4;  // current area/room id

// Top-level game state (g_gameState). State 10 is in-room gameplay: it gates
// RunPetRoomGameplayLoop (call site 0x03B986), which reads the location and player position every
// frame while walking a pet around. Used to hide the stat HUD on the main menu and other
// non-gameplay screens, where the underlying values are blank.
static const uint16_t GAME_STATE_ADDR = 0x1A97;
static const uint16_t GAME_STATE_IN_ROOM = 10;

// Pet stats and currency.
static const uint16_t CHEAT_MONEY_BASE = 0x0C4D;
static const uint16_t CHEAT_HALOS = 0x2182;
static const uint16_t CHEAT_PITCHFORKS = 0x2183;
static const uint16_t CHEAT_STARS = 0x2184;
static const uint16_t CHEAT_LIGHTBULBS = 0x2185;
static const uint16_t CHEAT_HUNGER = 0x2186;
static const uint16_t CHEAT_MOOD = 0x2189;
static const uint16_t CHEAT_SLEEPINESS = 0x218A;
static const uint16_t CHEAT_HYGEINE = 0x218B;
static const uint16_t CHEAT_HEALTH = 0x218D;

// Money. Real purchases save it immediately: AdjustPlayerMoney (ROM 0x02DC02) calls
// SaveMoneyToSlot(field = 0, slot) at 0x02E4A8 right after updating it, independent of Quit. The
// Mod Menu calls the same routine (see cheats.cpp) so edits persist. Args are pushed in the real
// caller's order: field first, slot last.
static const uint16_t MONEY_STAT_ADDR = 0x0C4D;

// Minipets. On real hardware the species is decided by which physical handheld is link-cabled; there
// is no in-game picker, so the Mod Menu simulates one. spawn_minipet() reproduces the RAM effects of
// the real zap-in (InitMiniPetZapAnimation 0x045ED0, PlayMiniPetZapAnimAlt 0x046667,
// PlaySpriteAnimation 0x04DC51) as plain data writes: an injected CPU call corrupted rendering
// (ds:-relative addressing can't be reproduced), and setting only the tracking flag leaves the
// animation struct empty so the state machine ping-pongs every tick ("zap spam").
static const uint16_t MINIPET_TRACKING_FLAG_ADDR = 0x1980;
static const uint16_t MINIPET_TRAIL_BASE = 0x1983;   // 20 slots x 3 words {X, Y, poseId}
static const int MINIPET_TRAIL_SLOT_COUNT = 20;
static const uint16_t MINIPET_TRAIL_READ_IDX_ADDR = 0x197d;
static const uint16_t MINIPET_TRAIL_WRITE_IDX_ADDR = 0x197e;
static const uint16_t MINIPET_ANIM_STRUCT_ADDR = 0x1a37; // SpriteAnimState, 10 words

// Doubles as the minipet species index (InitMiniPetZapAnimation's lookup). Otherwise effectively
// unused by the game (GetMiniPetState has no writers), so it is safe to drive directly. Only
// survives while the animation struct holds the 0x7B sentinel (see spawn_minipet).
static const uint16_t GAME_CURRENT_ROOM_ID_ADDR = 0x1a96;

// g_linkFeatureAvailable: real hardware's "handheld is link-cabled" flag, set by Link_UpdateAndSync.
// While tracking is active, a zero value takes the "unplugged" branch, which force-writes the
// tracking flag to 4 (despawn) and scrambles GAME_CURRENT_ROOM_ID_ADDR. The port pins it to 1 for
// the whole spawned lifetime.
static const uint16_t MINIPET_LINK_AVAILABLE_ADDR = 0x1b19;

// Disable Shadows: patches DrawShadow's prologue (ROM 0x01DEB5, "push bp,sp" = 0xDA88) to a bare
// "retf" (0x9A90) so it returns before drawing, using the same live rom[] patching as No-Clip.
// Because the function itself is neutered, shadows also vanish from the sprite extractor's output.
static const uint32_t SHADOW_PATCH_ADDR = 0x01DEB5;
static const uint16_t SHADOW_RETF_OPCODE = 0x9A90;

// Save-routine plumbing (see cheats.cpp set_stat_value / set_inv_value).
static const uint16_t CURRENT_SAVE_SLOT_ADDR = 0x1AFF;  // slot index the ROM's save routines expect as an argument
static const uint16_t ITEM_QUANTITY_BASE_ADDR = 0x0C51; // item N's owned-quantity word lives at 0x0C51 + N (the ROM's item id)

// Hidden test menu and quest tracking.
static const uint16_t TEST_MENU_UNLOCK_FLAG_ADDR = 0x1A4E;  // nonzero = hardware test menu is reachable
static const uint16_t ACTIVE_STORY_OBJECT_ID_ADDR = 0x1AA5; // nearby interactable's object id, 0xFFFF = none
static const uint16_t QUEST_OBJECTIVE_FLAG_ADDR = 0x1E4F;   // 0xFFFF = no quest objective active
static const uint16_t QUEST_TARGET_POOL_IDX_ADDR = 0x1E68;  // NPC pool index of the quest's delivery target
