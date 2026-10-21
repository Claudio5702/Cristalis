# Versions of Cristalis

The number is `GAME_VERSION` in `project.h`, numbered as [DarkLauncher](kit/launcher)
does: small improvements move the second figure (1.01, 1.02...); a large novelty goes to the next ten
(1.1, that is 1.10, then 1.11...); the first figure changes (2.0) for a change of the whole. Each
version has its Git tag (`v1.0`, `v1.01`...). What is new in a version is also shown once in the game,
on DarkLauncher's "What's new" page.

## 1.10

- **Lighter at high resolutions (DarkLauncher 2.40)**: on Windows the picture now goes to DarkLauncher
  in a texture shared on the graphics card (`dl_picture_shared`), instead of being read back through
  the processor at every picture. At 3840 x 2160 the game takes about 17 % less processor time, as
  little as at 1280 x 720 (30 s of play: 39.6 s, now 32.9 s). While DarkLauncher cannot show it (the
  first seconds, a display without the graphics card), the pixels go as before. Linux is unchanged.
- **DarkLauncher 2.41**: on Linux, the display without the graphics card is about ten times faster at
  4K, and Steam's virtual keyboard no longer opens at start (Steam's Game Mode); the menus before play
  keep their own pace, and a very slow synced picture turns vsync off for two seconds.
- The installer is titled "Cristalis (Crystal Defenders)", installs into a "Cristalis" folder and names
  its shortcuts "Cristalis – Crystal Defenders" (taking away those of the name before). An installed
  copy stays where it is; the program's files keep their names (crystal_defenders.exe, .ini, the
  saves), so that updates and Steam shortcuts go on working.
- DarkLauncher 2.20 and 2.30 come with it, with nothing visible in this game.

## 1.09

- **DarkLauncher 2.15**: the music behind the menus no longer cuts out at every move, and the
  preview keeps its pace (seen full screen at 4K, before the game is started).

## 1.08

- **DarkLauncher 2.14**: on the Internet page, "The new version's page" and "Update" appear only once
  a newer version has been found; until then the page shows only the look for new versions, with
  "This is the newest version." The first version that a 1.07 can install by itself.

## 1.07

- **The saves are in the `saves` folder**, where a player looks for them: before, the game's saves
  were in `userdata\content`, and `saves` held only DarkLauncher's list of achievements. The game
  moves the old ones there at its first start (and the game's own record of achievements with
  them); `userdata` goes once empty.
- The `saves` and `captures` folders are made at the first start, so that they can be seen before the
  first save or screenshot.
- **Updates (DarkLauncher 2.11, 2.12)**: at each start DarkLauncher reads the newest release of this
  repository (on at first; it can be turned off in Settings, Internet), says when one is out, and its "Update" downloads the release's file, checks its size and SHA-256
  against GitHub's, and installs it: the installer on Windows (it now accepts `/WAIT=<pid>` and
  `/RUN`, and gives the Windows list entry the new version), the archive's files on Linux. Saves,
  screenshots, settings and the game's data are left as they are; the game starts again.
- **DarkLauncher 2.07**: its version is no longer written at the bottom right of the menus (it is
  still in `--version` and in the crash report).
- **DarkLauncher 2.12, 2.13**: Internet is a row of the Settings page (it was the last row of Controls);
  the help texts name only what is offered (the Picture help no longer names the CRT filter and the
  scanlines); when GitHub cannot be reached, it no longer says the version is the latest.
- The READMEs say what goes over the Internet: only the look for new versions, one request to
  GitHub at each start.

## 1.06

- **DarkLauncher 2.06**: its fast forward is no longer offered (`DL_HIDE_TURBO`): it could not really
  speed this game up, which makes each picture when DarkLauncher asks for it, and it juddered. The
  game's own fast forward stays. The READMEs no longer name the Tab key for it.

## 1.05

- **No more crash when the program closes** (Windows): after DarkLauncher had saved everything, the
  program wound down while the game's threads still ran, and could crash on the way out; the next
  start then opened in safe mode (software display, in a window). It now ends at once, as on Linux.

## 1.04

The first public version: ready-made programs on the Releases page, as Unleashed Recompiled
does (no game asset in them; the player's own copy is still needed).

- **DarkLauncher 2.05**: the safe-mode page names only what the player sees (no CRT here, no HDR on
  Linux).
- **DarkLauncher 2.04**: on Linux its help texts no longer name Windows (the system's language, the
  image viewer, the trash, the file manager); the game's own "Automatic (Windows)" became "Automatic
  (system)" there, and the Linux subtitle says "(experimental)".
- **DarkLauncher 2.03**: the CRT filter and the scanlines are no longer offered on the Picture page
  (`game.hide`): a 2009 HD game made for flat screens gains little from a tube's look. They are not
  applied either; the settings file keeps their values. The READMEs and their picture follow.
- **CRT filter**: the game now tells DarkLauncher its picture is drawn 2 or 3 times larger than the
  console's 720 lines (internal 2560 x 1440 or 3840 x 2160), so that the tube's lines follow the
  console's lines; before, at 3840 x 2160 they were drawn at 2160 lines, too fine to be seen, and
  only the glow and the darker corners were left.
- A last review of the READMEs (six languages), the developers' guide, the license and the texts
  that come with the game: the Windows list entry's name, the crash report, the game's own high
  scores, DarkLauncher's MIT license, the View button held for the pause menu.
- **DarkLauncher 2.02**: the game says when it gives the last picture again (`dl_picture_same`), so
  that `--bench` and `--gpu-check` count the new pictures apart; a game that stops making them, as on
  Linux before 1.03, now shows at once ("new pictures 1187" of 1194 under WSL).

## 1.03

Prepared as the first public version; never published, as the review and the simpler Picture
page of 1.04 came first.

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
