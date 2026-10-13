// First-launch setup. See setup.cpp.
#pragma once

#include <filesystem>

// Asks for the folder of the player's own copy of the game and copies it to
// <programFolder>\game. Returns false if the player gave up or it failed.
bool RunFirstTimeSetup(const std::filesystem::path& programFolder);
