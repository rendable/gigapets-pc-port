# Giga Pets Explorer - PC Port

A cycle-accurate PC port of the Giga Pets Explorer TV game. It runs the original ROM on a CPU core and SPG2xx video/audio emulation taken from [MAME](https://www.mamedev.org/), with [raylib](https://www.raylib.com/) for rendering and input.

**You must supply your own legally-owned ROM.** No game data is included in this repository.

> Windows only for now. This is an unofficial fan project, not affiliated with or endorsed by the original publisher.

## Quick start (prebuilt, no build needed)

1. Go to the [Releases](../../releases) page and download the latest `GigaPetsPC_dist.zip`.
2. Extract it anywhere.
3. Rename your ROM file to `rom.u7` and place it in `resources\data\`.
4. Run `GigaPetsPC.exe`.

Prefer to build it yourself? See [Building from source](#building-from-source) below.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Move / D-Pad |
| Z, X, C | A, B, C buttons |
| Enter | Start |
| Tab | Mod Menu |
| F7 | Toggle live sprite capture |
| F8 | Export visible sprites to `extracted_sprites/` as PNGs |
| F9 | Jump to the hidden hardware debug/test menu |

Gamepads are supported; bind buttons in the Mod Menu.

## Features

- **Mod Menu (Tab):** edit any stat, freeze values, give items and money, pin favorites.
- **Custom mods:** add your own row by RAM address to track or edit anything.
- **Minipets:** spawn and despawn any of the 8 species from the Mod Menu.
- **Rendering filters:** Crisp, Smooth, Sharp, or CRT shader.
- **Overworld no-clip** and adjustable movement speed.
- **Pet stat HUD:** see hidden pet stats on screen.
- **Sprite extractor (F7/F8):** rip sprites from the running game with an adjustable export scale.
- **Debug menu (F9):** the real hidden hardware test menu.
- **Emulated save data**, written automatically to `resources/data/gigapets_save.eep`.

### Work in progress

- **Quest arrow:** an on-screen pointer toward your current quest target. Not always accurate yet.

### Known issues

- **Mystery Island:** needs a minipet spawned *and* the area reloaded (leave and re-enter, or go in and out of a building) before it lets you travel there.

More features are planned.

## Building from source

**You need:**

- Windows 10 or 11 (64-bit)
- [Visual Studio](https://visualstudio.microsoft.com/downloads/) 2022 or newer, or just the free **Build Tools for Visual Studio**, with the **Desktop development with C++** workload installed
- [CMake](https://cmake.org/download/) 3.25 or newer (add it to your PATH during install)
- [Git](https://git-scm.com/downloads) (or download the source as a zip from GitHub)
- An internet connection for the first build, which downloads raylib 6.0 automatically (about 50 MB)

**Steps:**

```
git clone <this repo's URL>
cd <repo folder>
cmake -S . -B build
cmake --build build --config Release
```

Run these in any terminal (PowerShell, Command Prompt, or Git Bash). The first build takes a few minutes.

**Result:** `build\Release\GigaPetsPC.exe`, with the `resources\` folder copied next to it automatically. Put your `rom.u7` in `build\Release\resources\data\` and run it. Your save file and settings are stored in that same `resources\data\` folder.

The exe is self-contained (the C++ runtime is linked in), so you can copy the whole `build\Release` folder anywhere.

**Troubleshooting:**

- *"No CMAKE_CXX_COMPILER could be found"*: the C++ workload isn't installed. Re-run the Visual Studio installer and add **Desktop development with C++**.
- *`cmake` not found*: install CMake and reopen your terminal so the PATH updates.
- *raylib download fails*: check your internet connection and re-run the configure step.

## Credits and license

- CPU core and SPG2xx hardware emulation are derived from [MAME](https://github.com/mamedev/mame) (SunPlus unSP core by Segher Boessenkool, Ryan Holtz and David Haywood; SPG2xx audio/video/IO by Ryan Holtz, Jonathan Gevaryahu and contributors; IMA ADPCM by Andrew Gardner and Aaron Giles).
- Rendering and input by [raylib](https://github.com/raysan5/raylib) (zlib license).

Because it incorporates GPL-licensed MAME code, this project is licensed under the **GNU GPL v2 or later**. See [LICENSE](LICENSE). Individual source files keep their original license headers.
