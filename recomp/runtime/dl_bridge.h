// The game on DarkLauncher (kit\launcher): DarkLauncher owns the window, the settings screen, the
// sound output and the controls, and asks for one picture at a time. The game's own code runs in its
// own thread; these functions pass pictures, sound and buttons between the two. See darklauncher.cpp.
// Only in the build made with CD_DARKLAUNCHER.
#pragma once

#include <cstdint>

// Runs DarkLauncher on this thread; it starts runGuest in a thread of its own. Returns the exit code.
int DarkLauncherMain(int argc, char* argv[], void (*runGuest)());

// From the renderer, once the game has drawn a picture (BGRA rows of pitch bytes): keeps a copy for
// DarkLauncher, then waits until DarkLauncher asks for the next picture. The game is held there
// while DarkLauncher's menu is open.
void DlBridgePresent(const void* pixels, uint32_t width, uint32_t height, uint32_t pitch);
// The multiple of 1280 x 720 the game should draw at (Picture page, "Internal resolution").
uint32_t DlBridgeScale();
// The game's controller buttons now (XINPUT_GAMEPAD bits).
uint16_t DlBridgeButtons();
// From the sound mixer: stereo 16-bit frames at 48 kHz. Waits while enough sound is already waiting.
void DlBridgeAudio(const int16_t* samples, uint32_t frames);
// An achievement the game has just unlocked (its id in the game), passed on to DarkLauncher's thread.
void DlBridgeUnlock(uint32_t id);
