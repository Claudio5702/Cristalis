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
- Windows is the complete version; the Linux build is experimental.

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

Instead of step 3, you can drop the console's `CRYSTAL DEFENDERS` (or `58410989`)
folder next to `crystal_defenders.exe` before the first launch: the game is then
installed from it without any question.

### Linux

**Experimental.** So far it has only been run under WSL on a PC, with software
rendering and no controller; nobody has tried it on a real Linux desktop or a
Steam Deck yet.

A native build (SDL2 and OpenGL 3.3) for x86-64 Linux with glibc 2.35 or newer:
Ubuntu 22.04, Debian 12, Fedora 36, SteamOS 3.5 and later. It needs SDL2 and
OpenGL from the system; FFmpeg is built in.

1. Unpack the Linux release.
2. Copy your `CRYSTAL DEFENDERS` (or `58410989`) folder into it.
3. Run `crystal_defenders.sh`. The game is installed on the first launch.

It has sound, saves, keyboard and controllers, six languages and the
high-resolution rendering. It does not have the settings menu, the achievement
pop-up or the pause in background yet: settings are changed in
`crystal_defenders.ini`.

On a Steam Deck, add `crystal_defenders.sh` to Steam as a non-Steam game, without
forcing a compatibility tool. The Windows release also runs through Proton.

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

On Linux: clang, CMake, Ninja, SDL2, FFmpeg and OpenGL development packages;
the same steps, and `runtime/linux` supplies the small part of the Windows API
the runtime is written against.

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
