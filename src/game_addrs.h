// Named RAM addresses in the emulated game (verified against the ROM; see docs/ARCHITECTURE.md).
#pragma once

#include "base.h"

// g_cameraScrollX/Y - confirmed via decompile of HandleMiniPetRoomInputAndStoryLogic
// (ROM 0x0130be, the real per-tick overworld input handler for normal,
// non-link-cable play): ordinary walking increments this by +-2/+-3 every
// tick (g_cameraScrollX +=/-= 2 or 3 depending on held direction and
// g_miniPetPromptState) - it's what actually moves during gameplay, not
// 0x1AB5/0x1AB6 (g_playerWorldPosX/Y), which this same decompile shows is
// only ever written once on room-transition/spawn, never during normal
// movement. Matches the user's own CT table's "Player X/Y" entry - the
// speed cheat below watches/amplifies this because it's genuinely the only
// address that changes while walking, despite the "camera scroll" name.
static const uint16_t PLAYER_WORLD_X = 0x1CC1;
static const uint16_t PLAYER_WORLD_Y = 0x1CC2;
static const uint16_t LOCATION_ID_ADDR = 0x1AA4;

// ===========================================================================
// SPG2xx Audio: real 16-channel ADPCM/PCM synth hardware, ported near-verbatim
// from mame/src/devices/machine/spg2xx_audio.cpp/.h and
// mame/src/devices/sound/imaadpcm.cpp/.h. Register map: 0x3000-0x31FF =
// per-channel regs (audio_r/audio_w), 0x3200-0x33FF = per-channel phase/pitch
// regs (audio_phase_r/audio_phase_w), 0x3400-0x37FF = global control regs
// (audio_ctrl_r/audio_ctrl_w). Sample/loop/envelope addresses are full 22-bit
// values the game assembles itself in these registers, read via the same
// flat memory_read16() already used for tile/sprite graphics - unrelated to
// read, etc). State 10 is specifically what gates RunPetRoomGameplayLoop
// (confirmed via xrefs to its call site, 0x03b986) - the function that reads
// Location ID/player position every frame while actually walking a pet
// around a room. Used to hide the HUD on the main menu and other non-
// gameplay screens, where the underlying stats are meaningless/blank.
static const uint16_t GAME_STATE_ADDR = 0x1A97;
static const uint16_t GAME_STATE_IN_ROOM = 10;
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

// Ground-truthed live against real MAME (watchpoint on the money address
// during an actual shop purchase): the ROM saves per-field, immediately,
// as part of the action that changed the value - AdjustPlayerMoney (ROM
// 0x02dc02) calls SaveMoneyToSlot(0x02e4a8) right after updating money,
// completely independent of Quit. A mod-menu edit was just a raw ram[]
// poke with no equivalent save call, which is the real reason money/item
// changes never persisted - not a Quit-timing issue at all. Calling the
// real save function ourselves (via call_rom_function) makes a mod-menu
// edit behave exactly like the real in-game action that changes this
// value, using the ROM's own proven-correct save-bit-packing logic
// instead of reimplementing it. Args match SaveMoneyToSlot's real
// calling convention, pushed in the same order its real caller uses:
// field(=0) first/deep, then slot last/close (verified via its own
// prologue: bp+8 = slot).
static const uint16_t MONEY_STAT_ADDR = 0x0C4D;

// Minipets: on real hardware, which of the 8 accessory-handheld species
// appears is decided entirely by which physical minipet toy is link-cable-
// connected - there is no in-game picker. This mod-menu row exists to
// simulate that (pick a species directly) since the PC port obviously has
// no such hardware. Everything below is a byte-verified reconstruction of
// the real spawn RAM effects (InitMiniPetZapAnimation @ 0x045ed0 +
// PlayMiniPetZapAnimAlt @ 0x046667 + PlaySpriteAnimation @ 0x04dc51, all
// traced via live decompile+disassembly during development), replicated as
// direct data writes rather than an injected CPU call - the previous
// attempt at a live call corrupted rendering via `ds:`-relative addressing
// context a synthetic call can't reproduce (see prior investigation notes).
// Likewise, writing the tracking flag alone without this setup is the
// original "zap spam" bug: the animation struct never gets populated, so
// the state machine's busy-check reads false from tick one and the states
// ping-pong every tick instead of playing a real zap-in and settling into
// Follow.
static const uint16_t MINIPET_TRACKING_FLAG_ADDR = 0x1980;
static const uint16_t MINIPET_TRAIL_BASE = 0x1983;   // 20 slots x 3 words {X, Y, poseId}
static const int MINIPET_TRAIL_SLOT_COUNT = 20;
static const uint16_t MINIPET_TRAIL_READ_IDX_ADDR = 0x197d;
static const uint16_t MINIPET_TRAIL_WRITE_IDX_ADDR = 0x197e;
static const uint16_t MINIPET_ANIM_STRUCT_ADDR = 0x1a37; // SpriteAnimState, 10 words

// Also read by InitMiniPetZapAnimation's real species-lookup - confirmed
// dead/always-0-in-practice on real hardware too (GetMiniPetState() has no
// writers anywhere in this ROM, so it always returns its power-on default),
// which is why this is safe to drive directly instead: nothing else
// legitimately owns it. Only survives if the struct is already the 0x7B
// sentinel - see spawn_minipet()'s comment on why that matters.
static const uint16_t GAME_CURRENT_ROOM_ID_ADDR = 0x1a96;

// g_linkFeatureAvailable - real hardware's "minipet handheld is physically
// link-cabled to the console" flag (set/reset by Link_UpdateAndSync from
// live cart-port I/O). MiniPet_HandleOverworldInput reads this every tick
// once tracking is active (FeedOrAdvanceMiniPet call at ROM 0x0146d8): with
// this nonzero AND tracking active it's a genuine no-op (real Follow just
// ticks normally), but with this at 0 (our default, since we have no real
// cart port) it takes the "handheld unplugged" branch, which the decompiler
// hid behind a fake trampoline return - confirmed via raw disassembly at
// 0x046231-0x046235, it force-writes the tracking flag to 4 (despawn)
// almost immediately after Follow begins. That same cascade's dead
// GetMiniPetState()-remap (0x0461cc) also stomps GAME_CURRENT_ROOM_ID_ADDR
// back toward 0 whenever it runs, which is why species selection looked
// scrambled - one root cause explains both bugs. Simulate "handheld always
// connected" by keeping this pinned to 1 for the whole spawned lifetime.
static const uint16_t MINIPET_LINK_AVAILABLE_ADDR = 0x1b19;

// Disable Shadows: same rom[] live-patch technique as No-Clip above, but a
// single fixed, already-named address instead of an AOB scan - a tester
// found DrawShadow (ROM 0x01DEB5, independently confirmed as Ghidra's own
// name for this exact address) and patched its real "push bp,sp" prologue
// (0xDA88) to a bare "retf" (0x9A90), making it return before drawing
// anything. Same effect here: it's a real ROM function neutered in place,
// not a synthetic effect, so it also naturally removes shadows from the
// sprite extractor's output (DrawShadow never populates a shadow sprite
// table entry to begin with).
static const uint32_t SHADOW_PATCH_ADDR = 0x01DEB5;
static const uint16_t SHADOW_RETF_OPCODE = 0x9A90;
