#pragma once

#include <cstdint>

// The debug mode (tests only, never seen by a player): CD_DEBUG holds a script of commands separated by
// ';' (or CD_DEBUG=@<file>, one command a line), run one picture at a time by DarkLauncher's Frame. Its
// waits are on the game's own state, not on a clock, so a scenario lands the same however fast the
// machine runs:
//   until title|stage|pack|mode|wave|gold <n> [<s>]   the pre-stage screen (0x4005FAA0), the stage's screen,
//                                                 the pack (0 outside a stage), the stage's mode, the gold; gives up
//                                                 after <seconds> (60) with a line in the log; <n written
//                                                 "<n": below n (a deployment paid: gold <999), "13|12": either
//   wait <seconds>                                a pause
//   tap <hex buttons> [<pictures>]                the controller's buttons (XINPUT bits), held 6 pictures
//   tapuntil <hex> title|stage|pack|mode <n> [<s>] tapped again and again until that state
//   move <x> <y>, click <x> <y>, rclick, mclick   the pointer on the 1280 x 720 picture; a click holds the
//                                                 button 4 pictures and lets go 4; hide takes it away
//   wheel <notches>                               the mouse's wheel (up: positive)
//   clickuntil <x> <y> title|stage|pack|mode <n> [<s>]  left clicks there until that state
//   gold <n> [keep]                               the stage's gold (keep: every picture, in a stage)
//   poke <hex address> <value>                    a 32-bit word of the game's memory
//   peek <hex address>                            logs one
//   keep <hex address> <value>|off                one written at every picture in a stage (the crystals:
//                                                 keep 40065EA8 20, a stage never lost)
//   shot <name>                                   the next picture saved as <name>.bmp
//   dump <name>                                   the game's heap saved as <name>.bin (to compare two)
//   log <text>                                    a line in the log, with the game's state
//   repeat <n> ... endrepeat                      the commands between, n times
//   jumpif <state> <n> <name>, label <name>       on to the label (further down) when the state is reached
// Each command is logged as it runs ("Debug: ...").
struct DebugInput
{
    bool pointer = false; // over the picture, at x, y (1280 x 720), with the mouse buttons
    double x = 0, y = 0;
    unsigned mouse = 0;     // bit 0 left, 1 right, 2 middle
    uint16_t buttons = 0;   // the controller's, added
    int wheel = 0;          // notches of the mouse's wheel (up: positive)
};

bool DebugScriptActive();
DebugInput DebugScriptStep(); // once a picture, from DarkLauncher's Frame
