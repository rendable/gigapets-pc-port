# Contributing

Thanks for helping out. This project is a PC emulator-style port of Giga Pets Explorer, so most work
falls into a few kinds of change: adding things to the Mod Menu, hooking the ROM, finding new RAM/ROM
addresses, and rendering/audio fixes. Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how
the pieces fit together and [docs/ROM_NOTES.md](docs/ROM_NOTES.md) for why the odd parts exist.

## Ground rules

* **Never commit the ROM**, saves, or anything copyrighted from the game. `rom.u7` and
  `resources/data/*` are git-ignored; keep it that way. Describe what you found instead of pasting
  game data.
* The project is licensed **GPL-2.0-or-later** (it contains MAME code). By contributing you agree your
  changes are under the same license.
* Windows only for now (raylib itself is portable, but `window_util.cpp` uses Win32).

## Building

See the README. In short:

```
cmake -S . -B build
cmake --build build --config Release
```

## Code style

* 4 spaces, no tabs, LF line endings (`.editorconfig` covers this).
* Comments say what the code does and *why*, in the present tense. Don't write a diary ("we tried X,
  then Y"); put investigation history in `docs/ROM_NOTES.md` instead.
* Use the named constants: RAM addresses in `game_addrs.h`, hardware registers in `hw_regs.h`, ROM
  addresses in `rom_addrs.h`. If you find a new address, add it there with a comment rather than
  leaving a bare `0x1234` in code.
* Addresses are **word** addresses everywhere. The Ghidra project uses byte addresses (2x); see
  ARCHITECTURE.md.
* Don't hand-edit `src/unsp*.cpp` unless you are fixing the CPU core itself; they are MAME's code.

## Recipes

### Add a pet stat to the Mod Menu

Add a row to `CHEAT_STATS` in `src/cheat_tables.h`: `{ "Name", ram_address, min, max }`, optionally
with a preset list (see `Sickness`). Favorite/freeze state is saved automatically, keyed by the RAM
address, so adding or reordering rows is safe. If the ROM saves that field immediately when it changes
(as it does for money), also call its save routine from `set_stat_value` in `src/cheats.cpp`; see
"Saving" in ROM_NOTES.md.

### Add an inventory item

Add an entry to `INVENTORY_ITEMS` in `src/cheat_tables.h` inside the right category, and update that
category's `start`/`count` in `INVENTORY_CATEGORIES` (and the `start` of every later category).

### Add a Mod Menu option (toggle or selector)

1. Add the state variable to `settings.h` / `settings.cpp`.
2. Add a `ROW_*` constant to the chain in `mod_menu.h`.
3. Append it to `logical_order` in `compute_mod_menu_layout` (`mod_menu.cpp`).
4. Handle it in `adjust_row` (`mod_menu.cpp`) and draw its label and value in `draw_mod_menu_row`.
5. Use the variable where the feature lives.

### Hook the ROM

Add the address to `rom_addrs.h` with a description of what instruction it is, then add an
`if (full_pc() == ...)` in `rom_hooks_before_step` (fires before the instruction executes) or
`rom_hooks_after_step` (fires right after, which is where you override a register the next compare
reads). Explain *why* in a comment and add a section to `docs/ROM_NOTES.md` for anything non-obvious.

### Find an address

Run the ROM in MAME with its debugger (`-debug`) and set a write watchpoint on the RAM word you care
about, for example `wpset 1aa5,1,w`. When it fires, MAME shows the PC of the instruction that wrote
it; look that address up in `docs/rom_functions.csv` to see which ROM function it is in.
`docs/rom_ram_globals.csv` lists the RAM variables already identified. Both are word addresses.

## Checking your change

There are no unit tests, but there is a deterministic regression check. It needs your own ROM:

```
# 1. On a clean checkout of main, build and record a baseline
.\tools\selftest.ps1 -Rom C:\path\to\rom.u7 -Save baseline.txt

# 2. After your change, rebuild and compare
.\tools\selftest.ps1 -Rom C:\path\to\rom.u7 -Expected baseline.txt
```

For a refactor or bug fix that shouldn't change behavior, the hashes must match exactly. For an
intended change the hashes will differ; that's expected, just say so in the pull request. The
`screen=` hash depends on your GPU and driver, so record your own baseline rather than sharing one.
The harness drives the Mod Menu actions and filters, but not keyboard hotkeys or mouse input, so also
try your change in the running game.

## Pull requests

* Keep each PR focused, and explain what changed and how you checked it.
* Make sure it still builds from a clean clone (`cmake -S . -B build`).
* If it adds or changes a ROM hook, update `docs/ROM_NOTES.md`.
