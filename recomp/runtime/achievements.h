// Locally stored achievements. See achievements.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Records achievements the game reports as earned; new ones get a pop-up.
void AchievementsUnlock(const std::vector<uint32_t>& ids);
// Text listing every achievement with its state, and a one-line total.
std::wstring AchievementsSummary(std::wstring& title);
// For the log, which is UTF-8.
std::string Utf8(const std::wstring& wide);
