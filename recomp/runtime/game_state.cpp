#include "game_state.h"

#include "guest.h"
#include "ppc_detail.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstring>

// The wave of the stage being played, one byte in the game's heap: found by comparing the game's memory
// between waves; the same through other paths into a stage (the heap is laid out the same way each run).
// Read only during a stage, and only believed when it looks like a wave.
constexpr uint32_t kWaveAddress = 0x4007F840;
constexpr int kMaxWave = 200;

// The stage's gold, a word of the map object (+0x1298C), found by comparing memory before and after a
// deployment (CD_POKE=<frame>:40065D7C=999: gold for tests).
// The stage's cursor: the point of the map's 40 x 40 grid it is on, two big-endian words in the game's heap
// (found like the wave, by comparing memory as the cursor moved). Only read: written, the cursor jumps
// there without the game checking where a unit may stand. The game keeps it from 2 to 38 (a unit takes
// 2 x 2 squares around it). On the 1280 x 720 picture the grid's
// point 0, 0 is at (316.5, 52.5), one point every 16.2 pixels across and 13.78 down.
constexpr uint32_t kCursorAddress = 0x40060184;
constexpr double kGridLeft = 316.5, kGridTop = 52.5, kGridStepX = 16.2, kGridStepY = 13.78;

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

bool GameStateGridPoint(double x, double y, int& gridX, int& gridY)
{
    double fx = (x - kGridLeft) / kGridStepX, fy = (y - kGridTop) / kGridStepY;
    // the map and half a square around it
    if (fx < -0.5 || fx > 40.5 || fy < -0.5 || fy > 40.5)
        return false;
    // the game's own move (sub_820BDA48) goes two points at a time (a unit takes 2 x 2 squares), and puts an
    // odd point back on an even one: on the points between, a unit may not be deployed
    gridX = std::clamp(int(std::lround(fx / 2)) * 2, 2, 38);
    gridY = std::clamp(int(std::lround(fy / 2)) * 2, 2, 38);
    return true;
}

void GameStateCursor(int& gridX, int& gridY)
{
    gridX = int(Load32(kCursorAddress));
    gridY = int(Load32(kCursorAddress + 4));
}

// What the stage shows, a word after the cursor: 0 the map (the cursor moves), 1 the row of units to
// deploy (A on the map), 2 a deployed unit's menu (Level +, Quit). And in the row, the unit chosen: from 0
// at the left, along the rows (the arrows wrap around in a row; up and down change rows). Found like the
// cursor.
constexpr uint32_t kModeAddress = 0x40060190, kUnitChoiceAddress = 0x40065D30;
// W1's row of units on the 1280 x 720 picture: six of them, the first centred at 397, then every 97.3
// pixels, between 205 and 310 down (see GameStateUnitGrid for W2 and W3).
constexpr double kUnitsLeft = 397, kUnitsStep = 97.3, kUnitsTop = 205, kUnitsBottom = 310;

int GameStateMode()
{
    return int(Load32(kModeAddress));
}

int GameStateUnitChoice()
{
    return int(Load32(kUnitChoiceAddress));
}

UnitGrid GameStateUnitGrid()
{
    // W2: five units a row, its second row the last two units and the three power crystals; W3: six, and
    // a second row with three more and the crystals (measured like the first)
    if (g_pack == 2)
        return { 5, 2, 400, 119.5, 250, 130 };
    if (g_pack == 3)
        return { 6, 2, kUnitsLeft, kUnitsStep, 255, 125 };
    return { 6, 1, kUnitsLeft, kUnitsStep, (kUnitsTop + kUnitsBottom) / 2, 0 };
}

int GameStateUnitAt(double x, double y)
{
    UnitGrid grid = GameStateUnitGrid();
    double column = std::round((x - grid.left) / grid.step);
    double row = grid.rows > 1 ? std::round((y - grid.top) / grid.rowStep) : 0;
    if (column < 0 || column >= grid.columns || row < 0 || row >= grid.rows ||
        std::abs(x - grid.left - column * grid.step) > grid.step / 2 || std::abs(y - grid.top - row * grid.rowStep) > 53)
        return -1;
    return int(row) * grid.columns + int(column);
}

// The menus before a stage. Which one is shown: a word that is 1 on the title's, 12 on the choice of pack
// and 4 within a pack, then a word that is 0 on the pack's menu and 2 on its choice of stage (and in its
// stages). Each list's choice is a word nearby, counted from the top. Found like the cursor; the rows'
// places measured on the 1280 x 720 picture.
constexpr uint32_t kScreenAddress = 0x4005FAA0, kPackScreenAddress = 0x4005FB28;
constexpr uint32_t kPackChoice = 0x4005FB2C; // the pack menu's row chosen: 0 start, 1 best scores, 2 slow motion
// In a stage's help (its screen 25), what it shows: kHelpMenu its menu, 9 the pages (the map object's +0x97C)
constexpr uint32_t kHelpPart = 0x40053D6C, kHelpMenu = 22, kHelpPages = 9, kHelpSettings = 10;

bool GameStateHelpSettings()
{
    return GameStateStageScreen() == 25 && Load32(kHelpPart) == kHelpSettings;
}

GameMenu GameStateMenu()
{
    if (g_pack != 0)
    {
        // In a stage, a deployed unit's menu (sub_820CC160 opens it: the stage's mode 2, the map object's
        // +0x24718 its choice from the top, +0x2471C its rows; the game's other menus of mode 2 set
        // +0x12900 instead): with two rows, Level + and Quit, as the console shows them.
        // The stage's screen, a word of the map object (+0x12980, set by sub_820C5460): 9 the map, 11 the row
        // of units, 10 a deployed unit's menu, 15 the pause menu, 0x12 the power crystals' (W2).
        constexpr uint32_t kMenuChoice = 0x400533F0 + 0x12900, kUnitChoice = 0x400533F0 + 0x24718;
        constexpr uint32_t kScreen = 0x400533F0 + 0x12980, kReplay = 0x400533F0 + 0x2C44C;
        uint32_t screen = Load32(kScreen);
        if (screen == 10 && Load32(kUnitChoice + 4) == 2)
            return { GameMenu::kList, kUnitChoice, 2, 370, 720, 303, 44 };
        // a power crystal's menu (W2): Recharge, see the recharged range, see the range now, Quit (found
        // with the debug mode, debug_script.h)
        if (screen == 10 && Load32(kUnitChoice + 4) == 4)
            return { GameMenu::kList, kUnitChoice, 4, 440, 820, 332, 42.5 };
        // The pause menu (sub_820C04F8 opens it, sub_820C0650 draws it; in a replay sub_820C0880 and another
        // drawing): +0x12900 the entry chosen, +0x12904 the rows; of fifteen entries, each hidden when its word
        // at +0x128C4 is set (no row), and greyed (passed over by the arrows) when its word at +0x12888 is.
        if (screen == 15 && Load32(kReplay) == 0)
        {
            GameMenu menu{ GameMenu::kList, kMenuChoice, 0, 460, 840, 150, 43 };
            menu.entries = true;
            for (int entry = 0; entry < 15; entry++)
                if (Load32(0x400533F0 + 0x128C4 + entry * 4) == 0)
                {
                    menu.greyed[menu.rows] = Load32(0x400533F0 + 0x12888 + entry * 4) != 0;
                    menu.entry[menu.rows++] = entry;
                }
            return menu;
        }
        // A question, Yes then No (sub_820C00D0 opens one: two rows, +0x12900 the choice from the top): 22
        // "Back to the main screen?" from the pause menu, 0x12 a power crystal's (W2)
        // the pause menu's help (screen 25): first its menu of four (How to Play, Controls, Settings,
        // Credits), then the pages chosen (their arrows: GameStateScoresAt); found with the debug mode
        if (screen == 25 && Load32(kHelpPart) == kHelpMenu)
            return { GameMenu::kList, 0x4008101C, 4, 520, 800, 308, 43.3 };
        // its Settings: Sound and Vibration, their values changed by left and right (the volume at the map
        // object's +0xE80); 23 its Controls, 26 its Credits, 27 the online Ranking: nothing to choose
        if (screen == 25 && Load32(kHelpPart) == kHelpSettings)
            return { GameMenu::kList, 0x4005FAB0, 2, 380, 820, 254, 44 };
        if (screen == 0x12 && Load32(kMenuChoice + 4) == 2) // "The summon needs 5 crystals. Continue?"
            return { GameMenu::kList, kMenuChoice, 2, 460, 820, 393, 42 };
        // the other questions, only those (the end screens, 12 and 13, have no rows): 16 "Start the wave
        // again?", 22 "Back to the main screen?", 24 save and "Quit the game?" (found with the debug mode)
        if ((screen == 16 || screen == 22 || screen == 24) && Load32(kMenuChoice + 4) == 2)
            return { GameMenu::kList, kMenuChoice, 2, 460, 820, 375, 45 };
        return {};
    }
    uint32_t screen = Load32(kScreenAddress), packScreen = Load32(kPackScreenAddress);
    if (screen == 1) // the title's menu turns: the choice stays in the middle, two rows on each side
        return { GameMenu::kCarousel, 0, 5, 430, 850, 516 - 2 * 32.5, 32.5 };
    if (screen == 9) // Mastery's demos to play: eight a page (the choice counted from the first page's top)
    {
        GameMenu menu{ GameMenu::kList, 0x4005FAD4, 8, 360, 930, 131, 43.4 };
        menu.paged = true;
        return menu;
    }
    if (screen == 12) // the three packs, each a band under two lines of text
        return { GameMenu::kList, 0x4005FA94, 3, 440, 860, 307, 150 };
    if (screen == 4 && packScreen == 0) // the pack's menu: start, best scores, slow motion, quit
        return { GameMenu::kList, kPackChoice, 4, 430, 850, 449, 37.8 };
    if (screen == 4 && packScreen == 2 && Load32(kPackChoice) != 1) // the four stages (to play, or slowed)
        return { GameMenu::kList, 0x4005FAF8, 4, 345, 935, 231, 43 };
    return {};
}

int GameStateScoresAt(double x, double y)
{
    // How to Play, from the pause menu (the stage's screen 25): nine pages, an arrow on each side (found with
    // the debug mode)
    if (GameStateStageScreen() == 25 && Load32(kHelpPart) == kHelpPages)
    {
        if (std::abs(y - 375) <= 35 && (std::abs(x - 336) <= 30 || std::abs(x - 943) <= 30))
            return x < 640 ? 0 : 1;
        return -1;
    }
    // Mastery's demos (the title's screen 9): only their arrows, for the other page (the rows are a list)
    if (g_pack == 0 && Load32(kScreenAddress) == 9)
    {
        if (std::abs(y - 285) <= 35 && (std::abs(x - 338) <= 30 || std::abs(x - 943) <= 30))
            return x < 640 ? 0 : 1;
        return -2;
    }
    // the best scores: the pack's screen 2 reached from its menu's second row; a stage's ten best, the
    // first with its replay (A), and arrows on both sides for the other stages
    if (g_pack != 0 || Load32(kScreenAddress) != 4 || Load32(kPackScreenAddress) != 2 || Load32(kPackChoice) != 1)
        return -2;
    if (std::abs(y - 370) <= 35 && std::abs(x - 375) <= 30)
        return 0;
    if (std::abs(y - 370) <= 35 && std::abs(x - 903) <= 30)
        return 1;
    if (std::abs(y - 243) <= 19 && x >= 390 && x <= 885)
        return 2;
    return -1;
}

int GameStateStageScreen()
{
    return g_pack != 0 ? int(Load32(0x400533F0 + 0x12980)) : -1;
}

int GameStateSummonAt(double x, double y)
{
    // W2's summons (the stage's screen 17, a third row under the units and the power crystals: down from
    // them, up back): the summon in the middle of the panel (the choice at the map object's +0x12AC8), an
    // arrow above it. The two rows of units (W2, W3) have the same arrows, above and below. Measured on the
    // 1280 x 720 picture.
    int screen = GameStateStageScreen();
    if (screen == 11 && GameStateUnitGrid().rows > 1)
    {
        if (std::abs(x - 640) <= 40 && std::abs(y - 187) <= 22)
            return 0;
        if (std::abs(x - 640) <= 40 && std::abs(y - 453) <= 22)
            return 1;
        return -2; // (elsewhere the row's units)
    }
    if (screen != 17)
        return -2;
    if (std::abs(x - 640) <= 40 && std::abs(y - 187) <= 22)
        return 0;
    if (std::abs(x - 640) <= 40 && std::abs(y - 453) <= 22)
        return 1;
    if (std::abs(x - 638) <= 60 && std::abs(y - 325) <= 60)
        return 2;
    return -1;
}

bool GameStateBottomBarShown()
{
    int screen = GameStateStageScreen();
    return screen == 9 || screen == 10 || screen == 11;
}

int GameStateMenuRow(const GameMenu& menu, double x, double y)
{
    double k = std::round((y - menu.firstRow) / menu.step);
    if (menu.kind == GameMenu::kNone || x < menu.left || x > menu.right || k < 0 || k >= menu.rows || menu.greyed[int(k)])
        return -1;
    return int(k);
}

int GameStateMenuChoice(const GameMenu& menu)
{
    if (menu.kind != GameMenu::kList)
        return -1;
    int choice = int(Load32(menu.choiceAddress));
    if (menu.paged)
        return choice % menu.rows;
    if (!menu.entries)
        return choice;
    for (int row = 0; row < menu.rows; row++)
        if (menu.entry[row] == choice)
            return row;
    return -1;
}

// The cursor moved at once (the mouse), as the game's own move does it (sub_820BDA48, read with Ghidra on
// the player's copy): the point written (from 2 to 38), then sub_820CB7B8(map, x, y), which looks at what
// lies there (the ground, a unit) and says whether a unit may be deployed, sub_820BD4D8(map, 0), which
// sets where the hand glides to and looks again, and sub_820D3B20(map), the range of a unit being placed.
// The map object is the one the cursor's words belong to. Called on the game's own thread, as it reads
// the controller.
constexpr uint32_t kMapObject = 0x400533F0, kLookUnderCursor = 0x820CB7B8, kSetMode = 0x820BD4D8, kShowRange = 0x820D3B20;
static std::atomic<int> g_cursorRequest{ -1 }; // x << 8 | y, -1: none

void GameStateRequestCursor(int gridX, int gridY)
{
    g_cursorRequest = gridX << 8 | gridY;
}

void GameStateApplyCursor(PPCContext& game)
{
    int request = g_cursorRequest.exchange(-1);
    // only on the map itself: while a stage loads, its map object is not ready for the game's functions
    if (request < 0 || g_pack == 0 || GameStateMode() != 0 || GameStateStageScreen() != 9)
        return;
    int x = request >> 8, y = request & 0xFF, nowX, nowY;
    GameStateCursor(nowX, nowY);
    if (nowX < 2 || nowX > 38 || nowY < 2 || nowY > 38 || (x == nowX && y == nowY))
        return;
    Store32(kCursorAddress, uint32_t(x));
    Store32(kCursorAddress + 4, uint32_t(y));
    // as the game's own code calls them: its thread, below its stack frame (a context of another thread
    // made for the call took the place of the game thread's own, and the game stopped)
    auto call = [&](uint32_t function, uint32_t r4, uint32_t r5)
    {
        PPCContext context = game;
        context.r1.u64 = game.r1.u64 - 0x400;
        context.r3.u64 = kMapObject;
        context.r4.u64 = r4;
        context.r5.u64 = r5;
        GuestResolveIndirect(g_base, function)(context, g_base);
    };
    call(kLookUnderCursor, uint32_t(x), uint32_t(y));
    call(kSetMode, 0, uint32_t(y));
    call(kShowRange, 0, 0);
}
