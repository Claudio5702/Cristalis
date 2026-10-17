// User settings, read once from crystal_defenders.ini in the working directory.
// The file is created with the defaults and an explanation the first time.
#pragma once

#include <cstdint>
#include <vector>

struct KeyBinding
{
    int key;         // Windows virtual key code
    uint16_t button; // XINPUT_GAMEPAD bit
};

struct Config
{
    bool fullscreen = false;
    uint32_t windowWidth = 0; // size of the image in the window, 0 = default
    uint32_t windowHeight = 0;
    uint32_t framesPerSecond = 60; // the console's rate; the game's speed follows it
    bool sharp = true;       // draw at the window's resolution with crisp pixels
    int volume = 100;        // percent, 0 to 100
    uint32_t language = 0;   // Xbox language id, 0 = follow Windows
    bool vibration = true;
    bool pauseInBackground = true; // hold the game while its window is not the active one
    bool storageDialog = false; // let the game ask which storage device to use
    bool liveWarning = false; // show the game's "not connected to Xbox LIVE" dialog
    bool launcher = true;     // show the start screen before the game
    std::vector<KeyBinding> keys;
};

// True on a Steam Deck (the game then runs through Proton, which passes this
// on from SteamOS). The window fills the screen and nothing depends on which
// window is active, as the Deck shows one game at a time.
bool OnSteamDeck();

const Config& GetConfig();
// For the settings menu: changes the live settings...
Config& MutableConfig();
// ...and writes one value back to the file, keeping the rest of it as it is.
void ConfigSave(const char* section, const char* key, const char* value);
