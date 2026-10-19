// Content packages (save games): host directories mounted under guest root names.
#pragma once

#include <filesystem>
#include <string>

// Directory holding default.xex: the root of game:\.
std::filesystem::path GameRoot();
// Host directory holding one sub-directory per content package: the "saves" folder.
std::filesystem::path ContentDirectory();
// Moves the saves of versions before 1.07 (userdata\content) into the saves folder, and makes the
// saves and captures folders, so that the player finds them from the first start.
void PrepareSaveFolders();
void MountContentRoot(const std::string& root, const std::filesystem::path& directory);
void UnmountContentRoot(const std::string& root);
