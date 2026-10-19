# Versions of CrystalDefendersRecomp

The number is `GAME_VERSION` in `project.h`, numbered as [DarkLauncher](https://github.com/Claudio5702/DarkLauncher)
does: small improvements move the second figure (1.01, 1.02...); a large novelty goes to the next ten
(1.1, that is 1.10, then 1.11...); the first figure changes (2.0) for a change of the whole. Each
version has its Git tag (`v1.0`, `v1.01`...). What is new in a version is also shown once in the game,
on DarkLauncher's "What's new" page.

## Next version (not numbered yet)

- **DarkLauncher 1.33**: a sturdier crash report (the error line is written without the C library's
  heap, a lock that blocks it no longer hangs the program, a stack overflow is reported). Nothing
  changes for the player; the report still names the error on Windows (checked with a forced crash).

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
