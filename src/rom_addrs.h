// Addresses inside the Giga Pets Explorer ROM that this port hooks or calls.
//
// Code addresses are full 22-bit program addresses ((bank << 16) | offset), exactly as shown in
// Ghidra and MAME's debugger, and as returned by full_pc(). Function names come from the
// reverse-engineered ROM (see docs/ROM_NOTES.md).
#pragma once

#include <stdint.h>

// --- Functions called from host code (call_rom_function) -----------------------------------------
static const uint32_t ROM_SAVE_MONEY_TO_SLOT = 0x02E4A8;       // SaveMoneyToSlot(field, slot)
static const uint32_t ROM_SAVE_ITEM_FIELD_TO_SLOT = 0x02E5C0;  // SaveItemFieldToSlot(itemIdx, field, slot)

// --- Hook points (rom_hooks.cpp) -------------------------------------------------------------------
// FeedOrAdvanceMiniPet: about to execute `r1 = [MINIPET_ANIM_STRUCT_ADDR]`; the `cmp r1, 0x7B`
// right after it is what the synthetic minipet needs to see as "parked".
static const uint32_t ROM_MINIPET_ANIM_LOAD = 0x04617D;
// Gate for the hidden hardware test menu; forcing TEST_MENU_UNLOCK_FLAG_ADDR to 1 here unlocks it.
static const uint32_t ROM_TEST_MENU_GATE = 0x03C9E8;
// PlaySoundEffect, just after its prologue: r2 holds the sound id argument.
static const uint32_t ROM_PLAY_SOUND_EFFECT_ARG_READY = 0x04D856;
// PowerDownHardware: where "Exit Test Mode" ends up (real hardware just switches off).
static const uint32_t ROM_POWER_DOWN_HARDWARE = 0x04F658;
// WaitForNextTick: the compare following FloatCompare against the idle-timeout threshold.
static const uint32_t ROM_IDLE_TIMEOUT_COMPARE = 0x01D31C;
// Link_UpdateAndSync: final `cmp r1, 0` gate on the link-cable-detect GPIO bit.
static const uint32_t ROM_LINK_PARTNER_GATE = 0x046C3F;
// GeneratePaletteBlendTable: the `cmp r1, r3` of its vblank-tick wait loop.
static const uint32_t ROM_PALETTE_BLEND_TICK_WAIT = 0x01CE31;

// --- ROM data tables -------------------------------------------------------------------------------
static const uint32_t ROM_NPC_AREA_TABLE = 0xDAEC;       // per-NPC home area, one entry per NPC...
static const uint32_t ROM_NPC_AREA_TABLE_STRIDE = 16;    // ...every 16 words
