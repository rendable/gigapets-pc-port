# Giga Pets Explorer - PC Port

A cycle-accurate PC port of the Giga Pets Explorer TV game. It runs the original ROM on a CPU core and SPG2xx video/audio emulation taken from [MAME](https://www.mamedev.org/), with [raylib](https://www.raylib.com/) for rendering and input.

**You must supply your own legally-owned ROM.** No game data is included in this repository.

> Windows only for now. This is an unofficial fan project, not affiliated with or endorsed by the original publisher.

## Quick start (prebuilt)

1. Download the latest release zip and extract it anywhere.
2. Rename your ROM file to `rom.u7` and place it in `resources\data\`.
3. Run `GigaPetsPC.exe`.

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

Requires Windows, CMake 3.15+ and Visual Studio (MSVC) with the C++ workload. raylib 6.0 is downloaded automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

The exe lands in `build\Release\` with `resources\` copied next to it. Put your `rom.u7` in `build\Release\resources\data\`.

## Credits and license

- CPU core and SPG2xx hardware emulation are derived from [MAME](https://github.com/mamedev/mame) (SunPlus unSP core by Segher Boessenkool, Ryan Holtz and David Haywood; SPG2xx audio/video/IO by Ryan Holtz, Jonathan Gevaryahu and contributors; IMA ADPCM by Andrew Gardner and Aaron Giles).
- Rendering and input by [raylib](https://github.com/raysan5/raylib) (zlib license).

Because it incorporates GPL-licensed MAME code, this project is licensed under the **GNU GPL v2 or later**. See [LICENSE](LICENSE). Individual source files keep their original license headers.
