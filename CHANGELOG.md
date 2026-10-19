# Versions of Cristalis

The number is `GAME_VERSION` in `project.h`, numbered as [DarkLauncher](kit/launcher)
does: small improvements move the second figure (1.01, 1.02...); a large novelty goes to the next ten
(1.1, that is 1.10, then 1.11...); the first figure changes (2.0) for a change of the whole. Each
version has its Git tag (`v1.0`, `v1.01`...). What is new in a version is also shown once in the game,
on DarkLauncher's "What's new" page.

## Next version (not numbered yet)

- **DarkLauncher 2.02**: the game says when it gives the last picture again (`dl_picture_same`), so
  that `--bench` and `--gpu-check` count the new pictures apart; a game that stops making them, as on
  Linux before 1.03, now shows at once ("new pictures 1187" of 1194 under WSL).

## 1.03

The first public version: ready-made programs on the Releases page, as Unleashed Recompiled
does (no game asset in them; the player's own copy is still needed).

- **DarkLauncher 2.01**, open source under the MIT license ("DarkLauncher contributors"): its source
  is in `kit/launcher` as before, with its license (`kit/launcher/LICENSE`), which the installer and
  the Linux archive now carry as `DarkLauncher-LICENSE.txt`.
- **Linux: the game no longer stops at its title screen.** Each draw gave the vertex buffer a new
  store (`glBufferData`, a few hundred times a picture); under WSL, Mesa's driver for Direct3D 12 locked
  up in its own thread at the title screen, and the "40 pictures a second" measured were DarkLauncher
  showing the last picture again. The vertices now go one after another into one large buffer: the
  game runs at 60 pictures a second under WSL (0.9 ms of drawing a picture). It may well never have
  happened with a real Linux PC's driver, but nothing proved it.
- Linux: the picture is read back for DarkLauncher without waiting for the graphics card (two pixel
  buffers, shown one picture later), so that a slower machine such as the Steam Deck keeps the card and
  the processor busy at once.
- The project is now called **Cristalis**, "Crystal Defenders Recomp", as Kahna is "Bahamut Lagoon
  Recomp": the setup screen, the window title, the READMEs, the installer and the crash report say so.
  The program's files keep their names (`crystal_defenders.exe`, `.ini`, saves), and an installed copy
  is found again by the installer.
- A new icon, as Kahna's carries its "K": the initial C of Cristalis in the gold of the setup screen's
  title, with the crystal in its opening (program, installer, window, Linux).
- The uninstaller has the same icon in shades of grey, so that `Uninstall.exe` is not mistaken for the
  game next to it.
- The repository is now `Claudio5702/Cristalis` (the old address leads there), and the published files
  are `Cristalis-Setup.exe` and `Cristalis-linux-x86_64.tar.gz`.

## 1.02

Prepared as the first public version, with ready-made programs on the Releases page; never
published, as the name Cristalis and DarkLauncher's license came first.

- **DarkLauncher 1.33**: a sturdier crash report (the error line is written without the C library's
  heap, a lock that blocks it no longer hangs the program, a stack overflow is reported). Nothing
  changes for the player; the report still names the error on Windows (checked with a forced crash).
- The READMEs say where to download it (the Windows installer, the Linux archive), and what the
  releases hold and do not hold.

## 1.01

- **DarkLauncher 1.32**: a lighter bar (Play, Screenshots, Game, Achievements, Settings, and Quit at
  the bottom); Languages, Picture, Sound, Controls, the game folder and the defaults are in Settings.
  The Picture page gains a preset and an Accessibility page (high contrast, colour-blind filter, less
  motion, large help text).
- **Crash report**: on Windows it also names the error ("error 0xC0000005 ... access violation"):
  the runtime's own handler now hands the error to DarkLauncher before ending the program.
- The READMEs, their pictures and the texts that come with the game follow the new bar, and name the
  PlayStation 4 and 5 controllers.
- The version is in `project.h`, shown in the Windows list of installed applications too.

## 1.0

The first numbered version.

- The game on **DarkLauncher** (1.3), on Windows and on Linux: settings screen, pause menu, seven
  languages, keyboard, Xbox and PlayStation 4 and 5 controllers with vibration, achievements, its
  theme in the game's colours.
- The game's six languages; internal resolution up to 3840 × 2160; filters, CRT and HDR on Windows.
- Every draw of the game handled (the one shader left aside writes nothing).
- **What's new** page; a **crash report** that says where the game was ("W1, Stage 2, Wave 5");
  the same line ready for a Discord status.
- An installer and an uninstaller in six languages; a Linux archive with SDL3 inside.
- READMEs in the game's six languages.
