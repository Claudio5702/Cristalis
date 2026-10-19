#include "game_state.h"

#include "guest.h"

#include <atomic>
#include <cctype>
#include <cstring>

// The wave of the stage being played, one byte in the game's heap: found by comparing the game's memory
// between waves; the same through other paths into a stage (the heap is laid out the same way each run).
// Read only during a stage, and only believed when it looks like a wave.
constexpr uint32_t kWaveAddress = 0x4007F840;
constexpr int kMaxWave = 200;

static std::atomic<int> g_pack, g_stage;

void GameStateFileOpened(const char* guestPath)
{
    const char* name = strrchr(guestPath, '\\');
    name = name != nullptr ? name + 1 : guestPath;
    auto lower = [](char c) { return char(tolower((unsigned char)c)); };
    // W1_ST_0.bin: the data of the first stage of W1, read as the stage starts
    if (lower(name[0]) == 'w' && name[1] >= '1' && name[1] <= '3' && strncmp(name + 2, "_ST_", 4) == 0 && name[6] >= '0' &&
        name[6] <= '3')
    {
        g_stage = name[6] - '0' + 1;
        g_pack = name[1] - '0';
        return;
    }
    // the title, or the menus of a pack (its background, its wallpaper): out of the stage
    if (strncmp(name, "all_titlelogo", 13) == 0 || (lower(name[0]) == 'w' && name[2] == '_' &&
        (strncmp(name + 3, "back", 4) == 0 || strncmp(name + 3, "wallpaper", 9) == 0)))
        g_pack = 0;
}

GameState GameStateNow()
{
    GameState state{ g_pack, g_stage, 0 };
    if (state.pack == 0)
        return { 0, 0, 0 };
    int wave = g_base[kWaveAddress];
    state.wave = wave >= 1 && wave <= kMaxWave ? wave : 0;
    return state;
}
