// What makes an installed program this project (Cristalis), for DarkLauncher
// (kit\launcher) and the installer. Started from DarkLauncher's project.h; every name is this
// project's own.
//
// Read by C and by DarkLauncher's tools\project.ps1, which only understands the simple lines
// '#define NAME "text"' and '#define NAME number'.
#ifndef PROJECT_H
#define PROJECT_H

// ---- names
#define PROJECT_NAME "Cristalis"                  // name of the project: window titles, crash report
#define PROJECT_SUBTITLE "Crystal Defenders Recomp" // what it is, beside the name
#define PROJECT_LOGO_FIRST "C"                    // the setup screen's title: a large initial (also on the icon),
#define PROJECT_LOGO_REST "RISTALIS"              // then the rest in smaller capitals
#define GAME_VERSION "1.07"                    // this port's version (CHANGELOG.md): 1.01, 1.02... as DarkLauncher's
#define GAME_NAME "Crystal Defenders"             // the program as the user sees it: install folder, shortcuts, Windows list of programs
#define FILE_STEM "crystal_defenders"             // crystal_defenders.exe, crystal_defenders.ini...
#define WINDOW_CLASS "CrystalDefendersRecomp"     // window class of the program (the name before Cristalis: kept)
#define SETUP_CLASS "CrystalDefendersRecompSetup" // window class of the installer
#define UNINSTALL_ID "CrystalDefendersRecomp"     // key of the entry in the Windows list of programs (kept: an installed copy is found again)
#define SETUP_MAGIC "CDRECOMP"                    // exactly 8 characters ending the installer's payload

// ---- game file
// 0: no ROM. The game's data comes from the player's own copy of the Xbox 360 game (the console's
// "CRYSTAL DEFENDERS" folder), which the program installs itself on its first launch.
#define SETUP_ROM 0
#define SETUP_ROM_EXTS "xex"
#define SETUP_PATCH_EXTS "none"
#define SETUP_ROM_FILTER L"Xbox 360 (default.xex)\0default.xex\0*.*\0*.*\0"
#define SETUP_ROM_KINDS "default.xex"
#define SETUP_ROM_LIST "default.xex"

// ---- what the uninstaller removes besides the installed files (which it knows from Uninstall.lst)
// files the program makes for itself, removed with it
#define SETUP_PROGRAM_FILES "gpu_shader*", FILE_STEM ".log", FILE_STEM ".prev.log"
// folders of work files, removed with the program
#define SETUP_WORK_FOLDERS "trace"
// the user's own things, removed only when "everything" is ticked: settings, saves and achievements,
// screenshots, the game's data taken from the player's copy
#define SETUP_PERSONAL_FILES FILE_STEM ".ini", "launcher_bg.*"
#define SETUP_PERSONAL_FOLDERS "userdata", "captures", "saves", "game"

#define PROJECT_TITLE PROJECT_NAME " - " PROJECT_SUBTITLE

#endif
