# CrystalDefendersRecomp

**Unofficial** native PC port of *Crystal Defenders* (Xbox 360).

A static recompilation of the Xbox Live Arcade game *Crystal Defenders*
for Windows. The game's PowerPC code is translated to C++ with
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and runs on a small runtime
written for this one game.

**This repository contains no game data.** You need your own copy of the game.
This project is not affiliated with or endorsed by Square Enix.

## Made with AI

This port was written by an AI: [Claude Code](https://claude.com/claude-code)
running Claude Opus 5.5, directed and play-tested by the repository's owner. The
runtime, the tools and this documentation were all produced that way.

## State

Playable from start to finish: graphics, sound, saves, six languages, replays.
Tested by very few people so far; expect rough edges.

Known differences from the console:

- No online leaderboards (they needed Xbox LIVE).
- Music loops are handled by the runtime rather than by the game's original mechanism.
- Windows only.

## Features

- Renders at the window's resolution (up to 4K), or the original 1280×720.
- In-game settings menu (F1, right click, or both sticks): language, resolution,
  rendering, full screen, volume, vibration, pause in background.
- Achievements stored locally, with a notification and a list.
- Keyboard (rebindable in `crystal_defenders.ini`) and any XInput controller.
- F12 screenshot, Alt+Enter / F11 full screen.

## Playing

1. Download a release and unzip it.
2. Run `crystal_defenders.exe`.
3. On first launch, choose the folder of your own copy of the game: the
   `CRYSTAL DEFENDERS` (or `58410989`) folder as copied from the console's
   storage, or a folder already holding `default.xex` and `Media`.

Windows 10 or 11, 64-bit, a Direct3D 11 graphics card.

## Building

Requirements: Windows, clang (MinGW-w64 target), CMake, Ninja, Python 3, an FFmpeg
LGPL shared build (for XMA audio), and your own `default.xex`.

1. Get XenonRecomp and apply the changes described in
   [`recomp/tools/README.md`](recomp/tools/README.md).
2. Run XenonRecomp with `recomp/config/crystal_defenders.toml` on your
   `default.xex`; it writes the translated code to `recomp/ppc`.
3. Generate the import stubs with `recomp/tools/gen_imports.py`.
4. Configure and build `recomp/` with CMake.

The paths to XenonRecomp and FFmpeg are set at the top of `recomp/CMakeLists.txt`.

## Layout

| Path | Contents |
|---|---|
| `recomp/config` | XenonRecomp configuration: function bounds, jump tables, hooks |
| `recomp/runtime` | The runtime: kernel and XAM imports, GPU command processor, Direct3D 11 renderer, XMA audio, settings menu, first-launch setup |
| `recomp/tools` | Scripts used during the port and to package it |

## Credits

- Written with [Claude Code](https://claude.com/claude-code) (Claude Opus 5.5).
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev.
- [FFmpeg](https://ffmpeg.org) for XMA decoding (LGPL).
- [Xenia](https://xenia.jp) documentation of the Xbox 360, used as reference.
- *Crystal Defenders* © Square Enix.

## License

The code in this repository is under the [MIT license](LICENSE). It grants no
rights to the game itself.
