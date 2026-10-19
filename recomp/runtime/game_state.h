#pragma once

#include <cstdint>

// Where the player is, for DarkLauncher's crash report and Discord status: worked out from the files the
// game opens (Media\W<pack>_ST_<stage>.bin when a stage starts, the title or a pack's menus when it
// ends) and, during a stage, the wave number in the game's own memory.
struct GameState
{
    int pack;  // 1..3 (W1, W2, W3), 0 outside a stage
    int stage; // 1..4, in the order of the stage list
    int wave;  // 0 when not known
};

// Called for each file the game opens, with its guest path ("game:\Media\...").
void GameStateFileOpened(const char* guestPath);
GameState GameStateNow();
