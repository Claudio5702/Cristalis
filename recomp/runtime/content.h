// Content packages (save games): host directories mounted under guest root names.
#pragma once

#include <filesystem>
#include <string>

// Directory holding default.xex: the root of game:\.
std::filesystem::path GameRoot();
// Host directory holding one sub-directory per content package.
std::filesystem::path ContentDirectory();
void MountContentRoot(const std::string& root, const std::filesystem::path& directory);
void UnmountContentRoot(const std::string& root);
