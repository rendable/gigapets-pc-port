# ROM notes

Why the port does the odd things it does. Each section is something that was found by tracing the
real ROM (usually against MAME running the same ROM) and that is not obvious from the code. Addresses
are word addresses, as everywhere in this repo (see [ARCHITECTURE.md](ARCHITECTURE.md)); function
names match `docs/rom_functions.csv`.

## Watchdog and Quit

Hitting **Quit** in the game used to freeze the screen while the music kept playing. The ROM's Quit
flow arms the hardware watchdog (`REG_SYSTEM_CTRL` bit 15) and then deliberately stops feeding it
(`REG_WATCHDOG_CLEAR` = `0x55AA`). After 750 ms the watchdog resets the CPU, which lands at
`ResetVector` (`0x00FA72`), restores default RAM (GameState 0) and returns to the main menu. The port
never implemented those two registers, so the reset never happened. `memory_write16` now arms a
45-tick countdown and `watchdog_tick()` performs the reset. Found by comparing a MAME watchpoint
trace of the real Quit flow with the port's.

## Idle timeout

`WaitForNextTick` (`0x01D2B9`) computes idle time, compares it with an 8-minute threshold using
`FloatCompare`, and calls `IdleTimeoutScreen`, which spins forever waiting for a physical power
button. A PC window has no separate "off" state, so `ROM_IDLE_TIMEOUT_COMPARE` forces the compare
result (`r1`) to 0 and the jump never fires. This is deliberate and not a bug to fix.

## Link-cable GPIO

`WaitForNextTick` may call `Link_UpdateAndSync` (`0x046D9B`). If the link-cable-detect bits on
`REG_IOA_DATA` (0x400/0x100/0x200, distinct from the 7 button bits) look connected, it proceeds into
`Cart_WriteBytes`, which calls `Cart_SuspendAudioForTiming` (disables video IRQs) but never calls
`Cart_RestoreAudioAfterTiming`. With video IRQs off the next `WaitForNextTick` hangs forever. On real
hardware with nothing plugged in those pins pull to a state that bails out early. Rather than emulate
link-cable GPIO, `ROM_LINK_PARTNER_GATE` (`cmp r1, 0` on the 0x200 bit) is forced to "no partner".

## Palette blend tick wait

`GeneratePaletteBlendTable` (`0x01CDB7`) waits for the vblank tick counter (`ram[0x13]`/`[0x14]`) to
change before each of 256 colors. On real hardware a vblank IRQ can interrupt that spin at any
instruction; here the tick only advances once per frame's cycle budget, so the wait could never end
(7,400+ spins with no progress were observed). `ROM_PALETTE_BLEND_TICK_WAIT` makes the "did it change"
compare read as unequal so the ROM's own completion logic proceeds.

## Minipets

On real hardware the species is decided by which physical minipet handheld is link-cabled; there is no
in-game picker. The Mod Menu simulates one by reproducing the RAM effects of the real zap-in
(`InitMiniPetZapAnimation`, `PlayMiniPetZapAnimAlt`, `PlaySpriteAnimation`) as plain data writes in
`spawn_minipet()`. An injected CPU call corrupted rendering because `ds:`-relative addressing can't be
reproduced from outside, and writing only the tracking flag leaves the animation struct empty, so the
state machine ping-pongs every tick ("zap spam").

Things that make a spawned minipet stay put:

* **Sentinel.** `FeedOrAdvanceMiniPet` (`0x046172`) runs every tick while `MINIPET_TRACKING_FLAG_ADDR`
  is nonzero. It treats animation struct word 0 other than `0x7B` as an unhandled found-item reaction
  and cascades into a forced despawn. The species frame id (from `PlayMiniPetAnimForRoom`) is only
  valid transiently, so the struct is initialised to `0x7B`. Once the follow state starts writing a
  real walk-pose id there, `rom_hooks.cpp` fakes only the `r1` register that the `cmp r1, 0x7B` reads
  (writing `0x7B` into the struct itself broke the walk animation, because rendering reads the same
  memory).
* **Link flag.** `MiniPet_HandleOverworldInput` reads `g_linkFeatureAvailable` (`0x1B19`). Zero takes
  the "handheld unplugged" branch, which force-writes the tracking flag to 4 (despawn) and stomps the
  species index (`0x1A96`). The port pins it to 1 while a minipet is spawned.
* **Trail buffer.** The follower reads a 60-slot circular buffer of the player's past positions
  (`MINIPET_TRAIL_BASE`). The movement-speed mod backfills the intermediate steps so the follower's
  distance stays constant, capped by the buffer's free space so the write side never laps the reader.

## Hidden test menu

The ROM contains a hardware test menu. It is gated by a RAM flag (`TEST_MENU_UNLOCK_FLAG_ADDR`) that
is checked at `ROM_TEST_MENU_GATE`; forcing it to 1 there unlocks it. The real input combo is still
required: hold Left+Select before the splash screen until the chime plays (~182 frames in; releasing
earlier resets the flag), release, then press Up, Down, Menu, Back in that order. F9 automates that
button sequence after a fresh reset. "Exit Test Mode" reaches `PowerDownHardware`, and
`TestRomChecksum` erases the entire EEPROM as part of its test, so the port snapshots the EEPROM on
entry and restores it and reboots on exit.

## Mystery Island travel (known issue)

The dock trigger is a silent walk-into-tile warp, gated by `g_activeStoryObjectId`
(`ACTIVE_STORY_OBJECT_ID_ADDR`) reading `0x68` there. That value is the id of the nearest interactable
object and updates as you walk (`0xFFFF` = none); `LoadRoom` resets it to `0xFFFF` on every room load.
Collision around the dock also depends on a minipet being tracked. Teleporting to the dock tile with
the minipet already tracked **at the time the room loaded** works; spawning a minipet from the Mod Menu
while already in the room does not, until the room reloads (leave and re-enter, or go in and out of a
building). Fixing it properly means calling `LoadRoom` again after a spawn, which needs its argument
semantics worked out: `LoadRoom(roomId, argA, argB, argC, posPtr)` takes five words, and `argA/B/C`
(seen as 4, 0x5C8, 5 at one call site) are not understood yet. A wrong guess risks corrupting state.

## Saving

The ROM saves **per field, immediately**, as part of the action that changes the value: for example
`AdjustPlayerMoney` (`0x02DC02`) calls `SaveMoneyToSlot` (`0x02E4A8`) right after updating money, and
`Item_IncrementOwnedAndSave` (`0x02DA4E`) calls `SaveItemFieldToSlot` (`0x02E5C0`). It is independent of
Quit. Mod Menu edits were raw `ram[]` writes with no equivalent save call, which is why they never
persisted. `set_stat_value` / `set_inv_value` now call the same ROM routines through
`call_rom_function`. Pitfalls found along the way:

* The return check must compare the full banked PC, not the 16-bit PC register, or it only works from
  bank 0.
* Item saves wait on the vblank tick counter, so `call_rom_function` pumps it.
* The emulated EEPROM drops writes while locked (a real 93C66 needs an unlock command first); the
  injected call skips whatever normally unlocks it, so it is unlocked for the duration of the call.
* `SaveItemFieldToSlot`'s `itemIdx` is the ROM's item id, which equals the item's RAM address minus
  `0x0C51`. It is **not** the index in the port's `INVENTORY_ITEMS` array (which is grouped by
  category).

## Audio

* **Tempo.** Generating a frame's audio in one batch after the CPU had used its cycle budget let
  several "beat expired" IRQs coalesce, so `Music_SequencerTick` ran too rarely (~10% slow music).
  Audio is now generated one sample per 384 CPU cycles (27 MHz / 70312.5 Hz) interleaved with CPU
  execution, as on real hardware.
* **Silence.** `AUDIO_CHANNEL_STOP` is write-1-to-clear. Treating it as a plain write left every
  channel's stop bit stuck after its first one-shot sample, and `CHANNEL_ENABLE` refuses to restart a
  stopped channel.
* **Choppiness.** raylib reports a stream buffer as processed only after `AUDIO_STREAM_CHUNK` frames
  have played; top-ups must be exactly that size or the output runs dry.
* **Phase registers.** Some ROM code writes a phase word and spins reading it back; every offset must
  really store. Writes also derive the channel's playback rate.

## EEPROM

* An erased 93C66 reads as all 1s (`0xFFFF` per word). The ROM's "valid save or first-time setup" check
  keys off that, so a missing save file starts as `0xFF`, not zero.
* Sequential reads auto-increment the address: while the host keeps clocking past one word with CS
  held, the chip streams the next word. Without it, multi-word reads returned the first word forever.
* Writes are only accepted while unlocked.

## Rendering

* Sprite and tile positions are 9-bit toroidal coordinates (0-511). Mask them, use them unsigned, and
  re-mask each row/column so sprites near the top/left wrap into view. Sign-extending them makes
  sprites right of X~256 disappear.
* Render interpolation blends only sprite positions. The background scroll is not interpolated: the
  game streams new tile columns into a toroidal buffer once per real tick, keyed to that tick's exact
  scroll value, so in-between scroll values show mismatched edge tiles.
* In Crisp mode the window is snapped to an integer scale, debounced so it doesn't fight a live drag,
  and never while maximized or fullscreen.

## Player position, movement speed, no-clip, shadows

* The ROM's `g_cameraScrollX/Y` (`PLAYER_WORLD_X/Y`) is the real player position; `g_playerWorldPosX/Y`
  at `0x1AB5/0x1AB6` is only written on room entry. RAM `0x21E3` looks like an area index but is a
  per-sprite-slot index that changes every tick.
* Movement speed multiplies the per-tick delta after the game moved the player. Deltas above
  `MOVEMENT_TELEPORT_THRESHOLD` and the two ticks after a room change are not amplified.
* No-clip patches three "collision check returns 0" sites found by scanning `rom[]` for byte patterns.
* Disable Shadows patches `DrawShadow`'s prologue (`0x01DEB5`) to a bare `retf`.

## Quest arrow and door links

`DOOR_LINKS` was gathered by walking through each door and recording
`(fromArea, door object id, toArea)`. The quest target is the NPC at `QUEST_TARGET_POOL_IDX_ADDR`
(`0x1E68`, set once when the quest is rolled; the ROM's inherited "QuestGiver" name is misleading), a
quest is active when `QUEST_OBJECTIVE_FLAG_ADDR` (`0x1E4F`) is not `0xFFFF`, and the NPC's home area is
in a ROM table (`ROM_NPC_AREA_TABLE`).
