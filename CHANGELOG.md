# Versions of Cristalis

The number is `GAME_VERSION` in `project.h`, numbered as [DarkLauncher](kit/launcher)
does: small improvements move the second figure (1.01, 1.02...); a large novelty goes to the next ten
(1.1, that is 1.10, then 1.11...); the first figure changes (2.0) for a change of the whole. Each
version has its Git tag (`v1.0`, `v1.01`...). What is new in a version is also shown once in the game,
on DarkLauncher's "What's new" page.

## 1.11

- **Played with the mouse (DarkLauncher 2.74)**: on the map the cursor goes at once to the square
  under the pointer, with the game's own checks of where a unit may stand (the game's functions that
  look under the cursor are called, as its own move does); a click deploys or opens a unit's menu. The
  row of units to deploy, a unit's menu, the pause menu and the menus before a stage (the title's
  turning menu, the packs, a pack's menu, the stages) follow the pointer, and the wheel moves through
  them; in W2 and W3 the units and the power crystals stand in two rows, and the pointer reaches both;
  a click on the arrows above and below goes up or down, to W2's summons too (a click on the summon
  chooses it), a power crystal's menu of four rows, every entry of the pause menu and its questions,
  and its help (its menu, How to Play's nine pages with their arrows, the Settings whose sound the
  wheel changes, the Credits that the wheel scrolls). So
  do the game's questions (Yes or No, such as "Back to the main screen?") the best scores and
  Mastery's demos (a click on an arrow turns the page, the wheel too). The right button is B (back, cancel).
- **The touch screen (DarkLauncher 2.96)**: a finger works as the mouse's left button, on a Steam Deck
  or a Windows tablet: a tap on a square brings the cursor there and confirms.
- **Fast forward kept on**: the game's own fast forward is RB held; a new function keeps it on without
  holding anything, from one press to the next: F on the keyboard, a controller button chosen on the
  Controls page, the mouse's middle button, or the pause menu. It stops when the stage ends.
- **About ten times less processor time**: the game's Direct3D waits for the graphics processor by
  asking again and again whether it may stop (`sub_820F3110`, read with Ghidra); on the console a core
  spun there, and here a whole core of the computer did too, about 16 ms of every picture. After a few
  questions each one now waits a millisecond: 30 s at 1280 x 720 took 31 s of processor time, now
  2.7 s (4K: 1.8 s), still at 60 pictures a second without a gap in the sound. Held to a single
  processor core, the game still keeps 60 pictures a second (it used to fall short with the core full).
  The thread that reads the game's graphics commands now sleeps until the game sends some (`sub_820F8B30`,
  its only writer of the ring buffer's pointer, found with `CD_WATCH`) instead of looking every
  millisecond: paused, in the menus or behind another window it wakes 60 times a second, not 1000. The
  sound thread, with enough sound waiting, now sleeps until DarkLauncher takes some, instead of looking
  every 2 ms (582 wake-ups a second, now 252). On a
  laptop or a Steam Deck, less heat and a longer battery.
- **Mouse fix**: the pointer puts the cursor on the map's even points only, as the game's own moves do
  (two points at a time): on the points between, a unit could not be deployed ("Unit deployment not
  possible here!") although the ground was right. And the cursor is moved only on the map itself; while a stage loaded, a pointer over the
  window could make the game look at a map not yet ready, and the game could stop there.
- **The button guide follows the device (DarkLauncher 2.94)**: the game's own button pictures (A, B, X,
  Y, RB) are replaced by the keys or the PlayStation buttons in use, and come back for an Xbox
  controller.
- **Fill the screen (DarkLauncher 2.95)**: on a screen or a window of other proportions (the Steam
  Deck's 16:10, 21:9, 4:3), the game's backgrounds are widened to fill it and everything else stays in
  the middle, without black bands; on a taller screen the stage's bar of gold and crystals goes down to
  its bottom. On by default, in Settings, Picture.
- **The launcher's own pictures**: a land of crystals in pixel art as its backdrop, a picture while the
  game starts, icons for its bar and a small knight as the program's icon, painted for the project
  (FLUX.2 [klein], prompts beside them in `recomp/resources`). The achievements show their own
  pictures, read from the player's copy of the game.
- **No longer called a beta on Windows**: the game has been played to the end; the Linux build stays
  experimental.
- DarkLauncher 2.50 to 2.98 come with it (2.98: its own part of a picture is now about 0.06 ms, and it
  draws nothing again while the game is paused under its menu): among others the console mode found by itself under Steam's
  Big Picture and Game Mode, FreeSync and G-Sync on Windows, the xBR filter, soft fades between pages,
  and Chinese and Korean in its menus.

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
- **Linux on a real PC**: a whole game was played under Bazzite (a Linux close to SteamOS), in Steam's
  Game Mode with the new Steam Controller, with no problem seen, the first time outside WSL. The READMEs say so; the build stays experimental.
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
