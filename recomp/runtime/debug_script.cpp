#include "debug_script.h"

#include "game_state.h"
#include "guest.h"
#include "renderer.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t kTitleScreen = 0x4005FAA0, kGold = 0x40065D7C; // see game_state.cpp

std::vector<std::string> Load()
{
    std::vector<std::string> commands;
    const char* script = getenv("CD_DEBUG");
    if (script == nullptr)
        return commands;
    std::string text;
    if (script[0] == '@')
    {
        // a file: one command a line, '#' starting a comment line
        std::ifstream file(script + 1);
        std::string line;
        while (std::getline(file, line))
        {
            size_t start = line.find_first_not_of(" \t\r");
            if (start != std::string::npos && line[start] != '#')
                text += line + ";";
        }
    }
    else
        text = script;
    std::string command;
    std::stringstream lines(text);
    std::vector<size_t> repeats; // where each open "repeat <n>" starts in commands, n in its own word
    std::vector<int> counts;
    while (std::getline(lines, command, ';'))
    {
        size_t start = command.find_first_not_of(" \t\r");
        if (start == std::string::npos || command[start] == '#')
            continue;
        command = command.substr(start);
        // repeat <n> ... endrepeat: the commands between, n times (nested ones too)
        if (command.rfind("repeat ", 0) == 0)
        {
            repeats.push_back(commands.size());
            counts.push_back(atoi(command.c_str() + 7));
            continue;
        }
        if (command.rfind("endrepeat", 0) == 0 && !repeats.empty())
        {
            std::vector<std::string> body(commands.begin() + repeats.back(), commands.end());
            for (int k = 1; k < counts.back(); k++)
                commands.insert(commands.end(), body.begin(), body.end());
            repeats.pop_back();
            counts.pop_back();
            continue;
        }
        commands.push_back(command);
    }
    return commands;
}

std::vector<std::string> g_commands = Load();
size_t g_next;                 // the command running
int g_picture;                 // pictures into it
ULONGLONG g_started;           // when it started
const ULONGLONG g_epoch = GetTickCount64();
DebugInput g_pointer;          // where the pointer stays between commands
int g_keepGold = -1;
std::vector<std::pair<uint32_t, uint32_t>> g_keeps; // words written at every picture in a stage ("keep")

int StateOf(const std::string& what)
{
    if (!GameStateReady())
        return -1;
    if (what == "title")
        return int(Load32(kTitleScreen));
    if (what == "stage")
        return GameStateStageScreen();
    if (what == "pack")
        return GameStateNow().pack;
    if (what == "mode")
        return GameStateNow().pack != 0 ? GameStateMode() : -1;
    if (what == "wave")
        return GameStateNow().wave;
    if (what == "gold") // (a deployment paid: the gold below what it was set to)
        return GameStateNow().pack != 0 ? int(Load32(kGold)) : -1;
    return -999;
}

// The state reached: <what> equal to <value>, or below it when <value> starts with '<'
bool Reached(const std::string& what, const std::string& value)
{
    if (!value.empty() && value[0] == '<')
        return StateOf(what) < atoi(value.c_str() + 1);
    // "13|12": either
    for (size_t start = 0; start <= value.size();)
    {
        size_t bar = value.find('|', start);
        if (StateOf(what) == atoi(value.substr(start, bar - start).c_str()))
            return true;
        if (bar == std::string::npos)
            break;
        start = bar + 1;
    }
    return false;
}

void LogState(const char* what)
{
    int gridX = 0, gridY = 0;
    if (GameStateReady())
        GameStateCursor(gridX, gridY);
    Log("Debug: %s (%.1f s; pack %d, wave %d, title %d, stage %d, mode %d, gold %d, cursor %d,%d)", what,
        (GetTickCount64() - g_epoch) / 1000.0, StateOf("pack"), StateOf("wave"), StateOf("title"), StateOf("stage"), StateOf("mode"),
        StateOf("pack") > 0 ? int(Load32(kGold)) : -1, gridX, gridY);
}

} // namespace

bool DebugScriptActive()
{
    return !g_commands.empty();
}

DebugInput DebugScriptStep()
{
    if (g_keepGold >= 0 && GameStateNow().pack != 0)
        Store32(kGold, uint32_t(g_keepGold));
    if (GameStateNow().pack != 0)
        for (auto& [at, value] : g_keeps)
            Store32(at, value);

    DebugInput input = g_pointer;
    input.mouse = 0;
    input.wheel = 0;
    input.digit = 0;
    // a few commands a picture at most, those that take none run straight on
    for (int budget = 0; budget < 16 && g_next < g_commands.size(); budget++)
    {
        const std::string& line = g_commands[g_next];
        std::stringstream words(line);
        std::string verb;
        words >> verb;
        if (g_picture == 0)
        {
            g_started = GetTickCount64();
            LogState(line.c_str());
        }
        bool done = true;
        if (verb == "until")
        {
            std::string what;
            std::string value;
            double seconds = 60;
            words >> what >> value >> seconds;
            done = Reached(what, value);
            if (!done && GetTickCount64() - g_started > ULONGLONG(seconds * 1000))
            {
                LogState(("timed out: " + line).c_str());
                done = true;
            }
        }
        else if (verb == "tapuntil")
        {
            // the buttons tapped (6 pictures held, 18 let go) until the state is reached
            std::string hex, what, value;
            double seconds = 60;
            words >> hex >> what >> value >> seconds;
            done = Reached(what, value);
            if (!done && g_picture % 24 < 6)
                input.buttons |= uint16_t(strtoul(hex.c_str(), nullptr, 16));
            if (!done && GetTickCount64() - g_started > ULONGLONG(seconds * 1000))
            {
                LogState(("timed out: " + line).c_str());
                done = true;
            }
        }
        else if (verb == "clickuntil")
        {
            // a left click at x, y (held 4 pictures, let go 20) again and again until the state is reached
            std::string what, value;
            double seconds = 20;
            words >> g_pointer.x >> g_pointer.y >> what >> value >> seconds;
            g_pointer.pointer = true;
            input = g_pointer;
            done = Reached(what, value);
            if (!done && g_picture % 24 >= 1 && g_picture % 24 < 5)
                input.mouse = 1;
            if (!done && GetTickCount64() - g_started > ULONGLONG(seconds * 1000))
            {
                LogState(("timed out: " + line).c_str());
                done = true;
            }
        }
        else if (verb == "label")
            ; // (a place to jump to)
        else if (verb == "jumpif")
        {
            // jumpif title|stage|pack|mode|wave|gold <n> <label>: on to that label when the state is reached
            std::string what, value, label;
            words >> what >> value >> label;
            if (Reached(what, value))
            {
                for (size_t k = g_next + 1; k < g_commands.size(); k++)
                    if (g_commands[k] == "label " + label)
                    {
                        g_next = k; // (then past it, below)
                        break;
                    }
            }
        }
        else if (verb == "wait")
        {
            double seconds = 0;
            words >> seconds;
            done = GetTickCount64() - g_started >= ULONGLONG(seconds * 1000);
        }
        else if (verb == "tap")
        {
            std::string hex;
            int pictures = 6;
            words >> hex >> pictures;
            uint16_t buttons = uint16_t(strtoul(hex.c_str(), nullptr, 16));
            if (g_picture < pictures)
                input.buttons |= buttons;
            done = g_picture >= 2 * pictures; // held, then let go as long
        }
        else if (verb == "move" || verb == "click" || verb == "rclick" || verb == "mclick")
        {
            words >> g_pointer.x >> g_pointer.y;
            g_pointer.pointer = true;
            input = g_pointer;
            unsigned button = verb == "click" ? 1 : verb == "rclick" ? 2 : verb == "mclick" ? 4 : 0;
            // a move first (the game sees the pointer there), then the button held 4 pictures, let go 4
            if (button != 0 && g_picture >= 1 && g_picture < 5)
                input.mouse = button;
            done = button == 0 ? g_picture >= 1 : g_picture >= 9;
        }
        else if (verb == "touch")
        {
            // a finger: the pointer there and the button down in the same picture (no hover before), held 4
            // pictures, then lifted and gone (DarkLauncher's dl_mouse gives a touch as the left button)
            double x = 0, y = 0;
            words >> x >> y;
            input = g_pointer;
            input.x = x, input.y = y;
            input.pointer = g_picture < 4;
            input.mouse = g_picture < 4 ? 1 : 0;
            g_pointer.pointer = false;
            done = g_picture >= 8;
        }
        else if (verb == "wheel")
            words >> input.wheel;
        else if (verb == "digit")
            words >> input.digit;
        else if (verb == "hide")
            g_pointer.pointer = false, input.pointer = false;
        else if (verb == "gold")
        {
            int value = 0;
            std::string keep;
            words >> value >> keep;
            if (GameStateNow().pack != 0)
                Store32(kGold, uint32_t(value));
            g_keepGold = keep == "keep" ? value : -1;
        }
        else if (verb == "poke")
        {
            std::string address;
            uint32_t value = 0;
            words >> address >> value;
            Store32(uint32_t(strtoul(address.c_str(), nullptr, 16)), value);
        }
        else if (verb == "keep")
        {
            // keep <hex address> <value>: written at every picture in a stage (keep <address> off: no more)
            std::string address, value;
            words >> address >> value;
            uint32_t at = uint32_t(strtoul(address.c_str(), nullptr, 16));
            g_keeps.erase(std::remove_if(g_keeps.begin(), g_keeps.end(), [&](auto& k) { return k.first == at; }), g_keeps.end());
            if (value != "off")
                g_keeps.push_back({ at, uint32_t(strtoul(value.c_str(), nullptr, 10)) });
        }
        else if (verb == "peek")
        {
            std::string address;
            words >> address;
            uint32_t at = uint32_t(strtoul(address.c_str(), nullptr, 16));
            Log("Debug: peek %08X = %u", at, Load32(at));
        }
        else if (verb == "dump")
        {
            // the game's heap (0x40000000-0x50000000) in <name>.bin, as (address, 4 KB page) pairs of the
            // committed pages, as CD_MEMDUMP writes it: two compared find a word (memdiff.py)
            std::string name;
            words >> name;
            if (FILE* file = fopen((name + ".bin").c_str(), "wb"))
            {
                for (uint32_t page = 0x40000000; page < 0x50000000; page += 0x1000)
                {
                    MEMORY_BASIC_INFORMATION info;
                    if (VirtualQuery(g_base + page, &info, sizeof(info)) == 0 || info.State != MEM_COMMIT)
                        continue;
                    fwrite(&page, 4, 1, file);
                    fwrite(g_base + page, 1, 0x1000, file);
                }
                fclose(file);
            }
        }
        else if (verb == "shot")
        {
            std::string name;
            words >> name;
            RendererRequestShot(name.c_str());
        }
        else if (verb == "log")
            ; // (logged with the state as it started)
        else
            Log("Debug: unknown command '%s'", line.c_str());

        if (!done)
        {
            g_picture++;
            break;
        }
        g_next++;
        g_picture = 0;
        if (g_next == g_commands.size())
            LogState("script done");
        if (verb == "tap" || verb == "click" || verb == "rclick" || verb == "mclick" || verb == "move" || verb == "wheel" || verb == "digit" || verb == "touch")
            break; // (one input a picture)
    }
    return input;
}
