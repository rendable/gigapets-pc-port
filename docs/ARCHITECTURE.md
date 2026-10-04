# Architecture

This port runs the original Giga Pets Explorer ROM on a CPU core and hardware emulation borrowed from
[MAME](https://github.com/mamedev/mame), and uses [raylib](https://www.raylib.com/) for the window,
input, audio output and drawing. It is an *emulator of the handheld*, not a rewrite of the game:
all game logic is the original ROM running as-is. The code in this repository is the machine
around it, plus the Mod Menu and other host-side features.

## How a frame works

`main.cpp` runs one loop iteration per displayed frame:

```
ui_update_hud_camera()          map the fixed UI space to the real window size
while (time owed >= 1/60 s)
    sim_tick()                  emulation.cpp: advance the machine one 1/60 s tick
hud_record_frame_time()
cheats_reassert_frozen()        re-apply "frozen" Mod Menu values
audio_queue_feed_stream()       hand queued audio samples to raylib
mod_menu_update()               Mod Menu input
render_game_frame()             build the 320x240 framebuffer from video regs + RAM
handle_hotkeys()                F7 / F8 / F9 / F11
present_game_frame()            draw the framebuffer to the window (filters/shaders)
overlays                        HUD, quest arrow, Mod Menu (drawn through g_hud_cam)
```

The simulation uses a fixed 1/60 s timestep that is independent of rendering speed, so the game runs
at the right speed whatever the display refresh rate or FPS limit is.

One `sim_tick()` (`emulation.cpp`):

1. `watchdog_tick()` - the hardware watchdog countdown.
2. `test_menu_tick()` - the F9 test-menu button script.
3. `emulate_frame()` - fire the vblank IRQ, then step the CPU for 27 000 000 / 60 cycles. Before and
   after every instruction `rom_hooks_before_step` / `rom_hooks_after_step` run, and
   `audio_run_cycles` generates audio samples in step with the CPU's cycle count.
4. `player_mods_tick()` - room-change detection and the movement-speed multiplier.
5. `video_snapshot_sprites()` - saves the sprite table for render interpolation.

## Source layout

| File | Responsibility |
| --- | --- |
| `main.cpp` | Startup, the frame loop above, shutdown. |
| `emulation.cpp` | One 1/60 s tick: CPU stepping with hooks and interleaved audio. |
| `machine.cpp` | The emulated address spaces (`ram`, `io`, `rom`, `video_regs`), the CPU pointer, watchdog, and `call_rom_function`. |
| `memory_map.cpp` | `memory_read16` / `memory_write16`: every CPU access to RAM, video, audio, GPIO and DMA lands here. This is where buttons are read and the EEPROM is bit-banged. |
| `hw_regs.h` | Names for the SPG2xx register addresses (named after MAME's sources). |
| `eeprom.cpp` | The 93C66 serial EEPROM state machine and the save file. |
| `audio.cpp` | 16-channel PCM/ADPCM synth, envelopes, beat timer, sample queue. Ported from MAME. |
| `video.cpp` | Tilemap + sprite rendering into an RGBA framebuffer, sprite interpolation, and presenting with Crisp/Smooth/Sharp/CRT. |
| `rom_hooks.cpp` / `rom_addrs.h` | Per-instruction hooks keyed on the PC, and the named ROM addresses they use. |
| `game_addrs.h` | Named RAM addresses in the running game. |
| `cheat_tables.h` | The Mod Menu's data: pet stats and the item catalogue. |
| `cheats.cpp` | Favorites, frozen values, custom mods, their persistence, and `set_stat_value` / `set_inv_value`. |
| `mod_menu.cpp` / `.h` | Row layout, input handling, and drawing of the Mod Menu. |
| `player_mods.cpp` | Movement speed, no-clip, shadow removal. |
| `minipet.cpp` | Minipet spawn/despawn. |
| `quest_arrow.cpp` | Door-link table, routing, and the quest arrow. |
| `sprite_export.cpp` | The F7/F8 sprite extractor. |
| `hud.cpp`, `ui.cpp` | FPS/stat HUD; shared UI font, textures, shaders and the HUD camera. |
| `input.cpp` | Key/gamepad bindings and their persistence. |
| `settings.cpp` | Runtime options toggled from the Mod Menu. |
| `window_util.cpp` | Win32 helpers (hand-declared so `windows.h` doesn't clash with raylib). |
| `selftest.cpp` | The deterministic regression harness (see below). |
| `unsp*.cpp`, `unsp.h` | The unSP CPU core, taken from MAME (GPL-2.0+). Treat as upstream code. |
| `emu.h`, `emuopts.h`, `logmacro.h` | Stubs that let MAME's CPU core compile outside MAME. |

Every module `.cpp` (everything except the MAME-derived `unsp*.cpp`) includes `common.h`, which pulls in
all module headers.

## Addresses and conventions

* The unSP CPU is **word-addressed**: every address in this code (`ram[...]`, `rom[...]`, hook
  addresses, `memory_read16`) counts 16-bit words, not bytes.
* Program addresses are 22-bit "full" addresses, `(bank << 16) | offset`, as returned by `full_pc()`.
  Hooks compare against these.
* **The Ghidra project uses byte addresses**, which are exactly twice the word addresses used here.
  `docs/rom_functions.csv` and `docs/rom_ram_globals.csv` are already converted to word addresses, so
  their values can be used in code directly. If you read addresses straight out of the Ghidra
  project, divide by two.
* CPU memory map: see the table at the top of `hw_regs.h`.

## ROM hooks and calling ROM code

Two mechanisms let host code cooperate with the running ROM:

* **Hooks** (`rom_hooks.cpp`): when the CPU is about to execute (or has just executed) a given
  address, override a register. The ROM's own next compare then goes the way real hardware would
  have taken it. This is preferred over changing memory, which other ROM code also reads. Each hook
  is explained in [ROM_NOTES.md](ROM_NOTES.md).
* **`call_rom_function`** (`machine.cpp`): runs a real ROM function synchronously from host code,
  e.g. so a Mod Menu edit saves exactly the way an in-game action does.

## Files the port writes

All next to the exe, under `resources/data/`: `gigapets_save.eep` (the EEPROM), and small binary
files for cheat state, custom mods, key bindings and gamepad bindings. None are committed.

## Regression testing

`GIGAPETS_SELFTEST=1` runs the emulator deterministically (fixed timestep, fixed RNG seed, scripted
input and Mod Menu actions) and writes hashes of the framebuffer, RAM, IO, video registers, generated
audio, CPU registers and on-screen pixels at fixed frames. `tools/selftest.ps1` runs it in a clean
temp directory and compares against a baseline you recorded earlier. A refactor that doesn't change
behavior produces identical hashes. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## ROM symbols

`docs/rom_functions.csv` lists all 1,209 named ROM functions (word address, size, signature) and
`docs/rom_ram_globals.csv` lists the named RAM variables. They come from the Ghidra project used to
reverse engineer the ROM; `tools/ghidra/ExportSymbols.java` regenerates them.
