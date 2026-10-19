# Cristalis (Crystal Defenders Recomp) — for developers

How the project works, how to build it, check it and make the installer. The repository never holds
the game's data or its translated code: both come from your own copy of the game.

## How it works

1. **Static recompilation.** [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) translates the
   PowerPC code of the game's `default.xex` into C++ (`recomp/ppc`, generated locally). Function
   bounds, jump tables and hooks the game needs are in `recomp/config`.
2. **The runtime** (`recomp/runtime`) stands in for the console:
   - guest memory with the Xbox 360's views of it, threads, kernel objects and the kernel and XAM
     functions the game imports (`kernel*.cpp`, `xam*.cpp`, `imports.cpp`);
   - the Xenos GPU's command processor (`gpu.cpp`), which recognises the game's shaders and turns its
     draws into Direct3D 11 ones (`renderer.cpp`);
   - XMA sound decoded with FFmpeg and the game's mixer driven like the console's (`audio.cpp`);
   - files (`kernel_io.cpp`), saves and achievements (`xam_content.cpp`, `achievements.cpp`), the
     settings (`config.cpp`), the first start without the game's data (`setup.cpp`), and where the
     player is (`game_state.cpp`: the pack and stage from the files the game opens, the wave from one
     byte of its memory) for DarkLauncher's crash report and Discord status.
3. **DarkLauncher** (`kit/launcher`, from the [DarkLauncher](https://github.com/Claudio5702/DarkLauncher)
   kit) owns the window, the settings screen, the controls and the sound output. The game's side is
   `runtime/darklauncher.cpp`; see below.

### The game on DarkLauncher

DarkLauncher asks for one picture at a time. The game, a whole Xbox 360 program, runs in a thread of
its own:

- the renderer draws off screen; when the game presents a picture, it is read back and handed to
  DarkLauncher (`DlBridgePresent` in `dl_bridge.h`), and the game waits there until DarkLauncher asks
  for the next one. So the game goes at DarkLauncher's pace, and stops while its menu is open;
- the sound mixer fills a reserve that DarkLauncher's frame empties, about 800 sample frames each;
- DarkLauncher's buttons become the game's XInput buttons, laid out as on an Xbox controller
  (`DL_PAD_XBOX`); View held opens the pause menu, a short press stays the game's Back (`DL_MENU_HOLD`);
- the game's own pages: its language (the program starts again with `dl_relaunch`), the internal
  resolution and the console's offline notices; the achievements go to DarkLauncher's page (`dl_achievements`, `dl_unlock`) and the game's rumble to `dl_rumble`, which drives Xbox and PlayStation pads;
- the theme (`DlTheme`) follows the game's art direction and is drawn by DarkLauncher: nothing is
  read from the game's files for it;
- under DarkLauncher, the runtime's own window, start screen (`launcher.cpp`) and settings file are
  not used: DarkLauncher keeps `crystal_defenders.ini` in its own format.

`project.h` gives DarkLauncher this project's names. The copy in `kit/` comes from DarkLauncher's
repository and is never changed here:

```powershell
powershell -ExecutionPolicy Bypass -File <DarkLauncher>\tools\sync_to_project.ps1 -Project .
```

An improvement DarkLauncher needs is made in its own repository (and its demo), then copied again.

## Building

Requirements: Windows, clang (llvm-mingw), CMake, Ninja, Python 3, an FFmpeg LGPL shared build (for
XMA sound, path at the top of `recomp/CMakeLists.txt`), and your own `default.xex`.

1. Get XenonRecomp, apply the changes described in
   [`recomp/tools/README.md`](../recomp/tools/README.md) and build it into `tools/build-xenonrecomp`
   (or pass its path with `-XenonRecomp`).
2. `.\recompile.ps1 -Xex <your default.xex>` translates the game's code into `recomp/ppc`, generates
   the import table, then builds.
3. After a change to the runtime, `.\build.ps1` rebuilds what changed (`-Clean`: everything).

The program lands in `build\`, next to the FFmpeg DLLs, and can be started from there; it finds the
game's data in `extracted\` or `build\game\`. `.\build.ps1 -Classic` builds the version without
DarkLauncher (its own window, start screen and F1 menu) into `build-classic\`. Never build while that
copy of the game runs: the script refuses.

### Linux

`runtime/linux` supplies the small part of the Windows API the runtime is written against and an
OpenGL 3.3 renderer (`renderer_sdl.cpp`). With DarkLauncher (the default), DarkLauncher's Linux
library (`kit/launcher/linux/libdarklauncher.so`, copied with `sync_to_project.ps1 -Linux`, on SDL3) has
the window, the menus, the sound and the controllers; the program finds it, and SDL3, in its `lib`
folder (the build puts it there).
The game draws in an OpenGL context of its own without a window (EGL), and each picture is read back
for DarkLauncher, as on Windows. SDL2 is then not used at all (two SDLs cannot be in one program);
`-DCD_DARKLAUNCHER=OFF` builds the earlier version on SDL2, with its own window and F1 menu.

Needs clang, CMake, Ninja, SDL3 (SDL2 for the earlier version), FreeType, fontconfig, OpenGL and EGL
development packages, and FFmpeg: a static build with only the XMA decoders (`-DCD_FFMPEG_ROOT=...`)
keeps the program free of the system's FFmpeg. `recomp/tools/package_linux.sh <build> <out>`
assembles the release, with SDL3 in `lib/` (the program looks there first) and the icon.

## Checking

Always on a copy of the program, with a copy of the settings (`--ini`) and of the saves, never on a
real installation:

```powershell
build\crystal_defenders.exe <default.xex> --ui-audit <folder> --ini <copy.ini>   # 0 issues, 7 languages
build\crystal_defenders.exe <default.xex> --gpu-check <folder> --ini <copy.ini>  # "ok" for every picture
build\crystal_defenders.exe <default.xex> --bench 30 --ini <copy.ini>            # pictures per second, sound gaps
```

Other options: `--play` (straight into the game), `--nomouse`, `--no-theme` (DarkLauncher's own look),
`--no-launcher` (classic version: no start screen).

Test hooks, as environment variables:

| | |
| --- | --- |
| `CD_THEME=f,c,s,t,b[,colour]` | other theme styles for a moment (DarkLauncher's `DL_FRAME_*`, `DL_CURSOR_*`, `DL_SKY_*`, `DL_TITLE_*`, `DL_BEEP_*`) |
| `CD_AUTOPAD`, `CD_AUTOPAD_START`, `CD_AUTOPAD_SHOTS` | classic version: taps buttons on a timer, saves the picture before each |
| `CD_SCREENSHOT=n,n…`, `CD_SCALE=n` | classic version: saves those frames; draws at that multiple of 1280×720 |
| `CD_AUDIO_DUMP=<file>` | keeps everything played, raw 48 kHz stereo 16-bit |
| `CD_STATS=1` | one line of sound statistics every five seconds |
| `CD_SETUP_SOURCE=<folder>` | first start: takes the game from there without asking |
| `CD_LAUNCHER_SHOT=<file.bmp>` | classic version: draws the start screen once into a file |

## The installer

`recomp/tools/make_installer.ps1` packs `build\` into `dist\Crystal Defenders - Setup.exe` (the
program, the FFmpeg DLLs, the read-me files; never the game's data, saves or settings). The program
holds the game's code translated to C++, so the installer is for your own use only.

The installer (`recomp/tools/setup`) has one window, shortcuts, an entry in Windows' list of
applications and an uninstaller. It installs the game's data from the console's folder placed next
to it, or asks for it at the end, through the program's `--install-from`. Silent use, for tests:

```powershell
"Crystal Defenders - Setup.exe" /S /D=C:\scratch\cd /NOSHORTCUTS
C:\scratch\cd\Uninstall.exe /S          # keeps the saves, settings and game data
C:\scratch\cd\Uninstall.exe /S /ALL     # removes everything
```

## Layout

| Path | Contents |
| --- | --- |
| `recomp/config` | XenonRecomp configuration: function bounds, jump tables, hooks |
| `recomp/runtime` | the runtime (see above); `linux/` for the Linux build |
| `recomp/tools` | scripts used during the port, packaging, the installer (`setup/`) |
| `recomp/resources` | the program's icon (an original crystal, drawn by `tools/make_icon.ps1`) |
| `kit/launcher` | DarkLauncher 2.0 and later: `dl.h` and the built library (`DarkLauncher.dll` and its import library, `linux/libdarklauncher.so`), copied by its `sync_to_project.ps1`, under its own license (`LICENSE.txt`); not changed here |
| `project.h` | this project's names for DarkLauncher and its installer |
| `build.ps1`, `recompile.ps1` | build, and translate the game's code again |
