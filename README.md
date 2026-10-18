# CrystalDefendersRecomp

**Crystal Defenders (Xbox 360) — unofficial native port by static recompilation**

![The settings screen, DarkLauncher in the colours of the game](docs/screenshots/launcher-en.png)

This project runs Crystal Defenders, the Xbox Live Arcade game, as a native Windows program: the
game's PowerPC code is translated ahead of time to C++ with
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and runs on a small runtime written for this
one game. A Linux build is experimental.

This repository contains **no game data and none of the game's code**: the translated code is
generated locally from your own `default.xex`, and the pictures, sounds and texts are read from your
own copy of the game.

**You must supply your own copy of Crystal Defenders (Xbox 360).** Crystal Defenders belongs to
Square Enix; this project is not affiliated with or endorsed by Square Enix.

**Contents:** [Playing](#playing) · [Features](#features) · [Settings screen](#settings-screen) ·
[Controls](#controls) · [Files](#files) · [Uninstalling](#uninstalling) · [Linux](#linux) ·
[Known limits](#known-limits) · [For developers](#for-developers) · [FAQ](#faq) ·
[Roadmap](#roadmap) · [License](#license) · [Credits](#credits)

## Playing

1. Copy the game from your console: the `CRYSTAL DEFENDERS` folder (or the folder named after its
   title id, `58410989`) as found on the console's storage, for example on a USB drive.
2. With the installer: put that folder next to `Crystal Defenders - Setup.exe` and run it. The game
   is installed from it, with shortcuts and an entry in Windows' list of applications. Without the
   folder, the last window of the installer asks where it is.
3. Without the installer: run `crystal_defenders.exe`. On the first start it asks for that folder
   (or a folder already holding `default.xex` and `Media`), or takes it at once when it lies next
   to the program.

The game's data is then kept in a `game` folder next to the program; the console's folder is no
longer needed. Keep these files to yourself.

Windows 10 or 11, 64-bit, a Direct3D 11 graphics card.

### The game to use

| | |
| --- | --- |
| Title id | `58410989` (Xbox Live Arcade) |
| Console package | `58410989\000D0000\18F4739D37853F8596CE621540680922AFE9F23A`, 58,126,336 bytes, SHA-1 `118C64C901976CD8E2B570D77B0A5E01C15C7A46` |
| `default.xex` inside it | 1,658,880 bytes, SHA-1 `31A90704D4233F6E730714515CD5F20BF08D7F0D` |

The program checks that `default.xex` is the one its code was translated from, and says so when it
is not.

## Features

- **The game in [DarkLauncher](https://github.com/Claudio5702/DarkLauncher)**, the launcher made
  for Kahna (Bahamut Lagoon Recomp): a settings screen before the game and a pause menu in game, in
  seven languages, with the keyboard, a controller or the mouse. Its look follows the game's art
  direction: the blue of its windows, the gold of its crystals, a starry night. DarkLauncher draws
  all of it; nothing is taken from the game's files.
- **Six languages of the game** (French, English, German, Spanish, Italian, Japanese), chosen in the
  settings; the game restarts in the new one.
- **Display through the graphics card**: the game drawn at 1280×720, 1920×1080, 2560×1440 or 3840×2160, then sharp,
  smooth or smooth-edged at any window size; scanlines, a flat or curved CRT look, HDR.
- **Achievements stored on the computer**, with a notice over the game and a list in the settings.
- **The console's own offline behaviour on request**: the "Not connected to Xbox LIVE" notice and
  the choice of the storage device are off by default, and can be turned on.
- **Screenshots** of the picture as shown, filters included, and a gallery in the settings.
- **Controllers**: Xbox (XInput), and PlayStation 4 and 5 read directly (USB or Bluetooth), with vibration on both; keys and buttons chosen on the Controls page. A PlayStation 3 pad needs the DsHidMini driver.
- **An installer and an uninstaller**, in six languages.

## Settings screen

At start-up DarkLauncher shows its settings screen, with the game running in the preview. **Play**
starts it; in game, **Esc** (or the controller's View button, held) opens a pause menu over the paused
picture: resume, screenshot, settings, quit.

A bar on the left lists the sections; the page of the section under the selection is shown beside
it. Enter or Right goes into it, Esc comes back.

- **Screenshots**: the screenshots taken, newest first. Enter opens one, Delete twice moves it to the
  Recycle Bin.
- **Game**: the "Not connected to Xbox LIVE" notice and the choice of the storage device
  (both as on an offline console; from the next start).
- **Achievements**: the game's twelve achievements, with the date each was earned and what each asks for.
- **Languages**: the game's language (the game restarts in it) and the menu's.
- **Picture**: the internal resolution (1280×720, 1920×1080, 2560×1440 or 3840×2160), full screen, integer scaling,
  display through the graphics card or in software, filter, scanlines, CRT, HDR, vertical sync,
  frame rate, and "Try it full size".
- **Sound**: the volume and the sounds of the menu.
- **Controls**: the keyboard keys and controller buttons of the game, vibration, and the shortcuts.
- At the bottom: the game folder, the default settings, quit.

![The Picture page](docs/screenshots/picture-en.png)

## Controls

Keys follow the **physical position** (the same on AZERTY and QWERTY keyboards); all of them can be
changed on the Controls page.

| Game (Xbox 360) | Keyboard (QWERTY positions) | Controller (PlayStation) |
| --- | --- | --- |
| D-pad | Arrows | D-pad / left stick |
| A (confirm) / B (back) | X / Z | A / B (cross / circle) |
| X / Y | S / A | X / Y (square / triangle) |
| LB / RB | Q / W | LB / RB (L1 / R1) |
| Start / Back | Enter / Right Shift | Start / Back (Options / Share); held, Back opens the pause menu |

`Esc`: pause menu — `F11` or `Alt+Enter`: full screen — `F12`: screenshot into `captures\` —
`Tab` (held): fast-forward.

## Files

Everything stays in the program's folder:

| | |
| --- | --- |
| `game\` | the game's data, taken out of your copy on the first start |
| `userdata\` | the game's saves and its record of the achievements earned |
| `saves\` | DarkLauncher's list of the achievements, with their dates |
| `captures\` | screenshots |
| `crystal_defenders.ini` | settings |
| `crystal_defenders.log` | log of the last session (`.prev.log`: the one before) |

## Uninstalling

Installed with the installer: run `Uninstall.exe` in the game folder, or remove "Crystal Defenders"
from the list of installed applications in the Windows settings. By default your saves, settings,
screenshots and the game's data stay in the folder, in case you reinstall; tick the box to remove
them as well, and the folder goes with them. Without the installer: delete the folder.

## Linux

**Experimental.** A native build for x86-64 Linux with glibc 2.35 or newer (Ubuntu 22.04, Debian 12,
Fedora 36, SteamOS 3.5 and later), with the same DarkLauncher as on Windows: its settings screen,
pause menu, seven languages, controllers (Xbox and PlayStation, with vibration), achievements and
theme. The game draws with OpenGL 3.3. It needs OpenGL, FreeType and fontconfig from the system;
SDL3 comes with it, in `lib/`, and FFmpeg is built in.

1. Unpack the Linux release.
2. Copy your `CRYSTAL DEFENDERS` (or `58410989`) folder into it.
3. Run `crystal_defenders.sh`. The game is installed on the first launch.

It has only been run under WSL so far (on a real graphics card through it, but no controller); nobody
has tried it on a real Linux desktop or a Steam Deck yet. On a Steam Deck, add `crystal_defenders.sh` to Steam
as a non-Steam game, without forcing a compatibility tool. The Windows release also runs through
Proton.

## Known limits

- No online leaderboards: they needed Xbox LIVE.
- No save states: the game is a whole Xbox 360 program with its own threads; it saves by itself.
- Music loops are handled by the runtime rather than by the console's own mechanism.
- Tested by very few people so far; controllers have been tried less than the keyboard, and PlayStation pads and vibration not yet on real hardware.

## For developers

How the project works, how to build it, translate the game's code again, check it and make the
installer: [docs/DEVELOPERS.md](docs/DEVELOPERS.md).

## FAQ

**Is it an emulator?** No. The game's own program is translated ahead of time into native code for
the PC (static recompilation); what the console's hardware did (graphics, XMA sound, the system's
functions) is written again for Windows.

**Why do I need my own copy?** The repository holds none of the game: the translated code is
generated from your `default.xex`, and the game reads its pictures, sounds and texts from your copy.

**Can I use my saves from the console?** The saves are in `userdata\`, in the game's own format;
copying saves between the console and the PC has not been tried.

**Is anything sent over the Internet?** No. The program does not use the network at all.

## Roadmap

- Linux with DarkLauncher, and a real test on a Steam Deck.
- The menu sounds and more of the game's art direction in DarkLauncher's theme.
- More testing with controllers and on other computers.

## License

The source code of this project is under the MIT license (see [LICENSE](LICENSE)). That license does
not cover the game, its data or its code.

## Credits

This project was made 100 % with [Claude Code](https://claude.com/claude-code) (Claude Opus 5.5),
the AI coding assistant by Anthropic: all the code, the tools and the documentation were written by
it, following the requests and the tests of the project's author.

- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) by hedge-dev: the recompiler that translates
  the game's code.
- [DarkLauncher](https://github.com/Claudio5702/DarkLauncher): the settings screen, the pause menu
  and the picture filters, made for Kahna.
- [FFmpeg](https://ffmpeg.org) (LGPL): XMA decoding.
- [Xenia](https://xenia.jp): its documentation of the Xbox 360, used as reference.
- [llvm-mingw](https://github.com/mstorsjo/llvm-mingw): the compiler of the program and the installer.

And of course to Square Enix, who made Crystal Defenders.
