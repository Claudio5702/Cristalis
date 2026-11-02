#pragma once

#include <cstdint>

struct PPCContext;

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

// The stage's cursor, for the mouse: the point of the map's grid it is on (x, y from 2 to 38), from a
// point of the console's 1280 x 720 picture. The map fills the same place in every stage.
bool GameStateGridPoint(double x, double y, int& gridX, int& gridY);
void GameStateCursor(int& gridX, int& gridY);
// The cursor put on a point at once (from any thread), done on the game's thread by GameStateApplyCursor
// (called as the game reads the controller): the point written, then the game's own look at what lies
// there, so that its checks of where a unit may stand hold. Nothing while one of its menus is open.
void GameStateRequestCursor(int gridX, int gridY);
void GameStateApplyCursor(PPCContext& game); // game: the guest context of the thread reading the controller
// What a stage shows: 0 the map, 1 the row of units to deploy, 2 a deployed unit's menu.
int GameStateMode();
// The units to deploy: a grid on the 1280 x 720 picture (W1 one row of six; W2 two of five; W3 two of
// six), the choice counted along the rows. The unit chosen, and the one at a point (-1: none).
struct UnitGrid
{
    int columns, rows;
    double left, step, top, rowStep; // the first's centre, then every step across and rowStep down
};
UnitGrid GameStateUnitGrid();
int GameStateUnitChoice();
// The choice put at once on a unit (its name and text follow; the game's hand stays, until an arrow moves it)
void GameStateSetUnitChoice(int unit);
int GameStateUnitAt(double x, double y);

// The menu shown before a stage, for the mouse: its rows on the 1280 x 720 picture.
struct GameMenu
{
    enum Kind { kNone, kList, kCarousel } kind = kNone; // list: the highlight moves; carousel: the rows do
    uint32_t choiceAddress = 0; // a list's choice, from 0 at the top
    int rows = 0;
    double left = 0, right = 0, firstRow = 0, step = 0; // the rows' centres: firstRow, then every step
    bool entries = false; // the choice is an entry's number, and entry[row] the one each row shows (hidden
    int entry[16]{};      // entries take no row); otherwise the choice is the row
    bool greyed[16]{};    // rows the arrows pass over: no pointing at them
    bool paged = false;   // the choice counts the rows of the pages before too
};
// The game's heap is there: its words may be read (not in the first moments of the program).
bool GameStateReady();
GameMenu GameStateMenu();
// The help's Settings page (sound, vibration): values changed by left and right (the wheel there).
bool GameStateHelpSettings();
// On the best scores, what is at a point: 0 the left arrow, 1 the right one (the stage before, after), 2
// the first row (its replay); -1 nothing, -2 not on that screen. On Mastery's demos and How to Play (in a stage) only the arrows (0, 1;
// elsewhere -2: its rows are a GameMenu list).
int GameStateScoresAt(double x, double y);
// On W2's summons, what is at a point: 0 the arrow above, 1 the one below, 2 the summon shown (A); -1
// nothing, -2 not on that screen. On the two rows of units of W2 and W3, their arrows (0, 1; else -2).
int GameStateSummonAt(double x, double y);
// The stage's screen (the map object's +0x12980: 9 the map, 11 the row of units, 10 a unit's menu, 15 the
// pause menu...), -1 outside a stage.
int GameStateStageScreen();
// The stage's bar of gold and crystals, and its messages, below this line of the 1280 x 720 picture: shown
// with nothing over it on the map, its row of units and a unit's menu (the pause menu, the questions, have
// boxes from above down into it). Filling a taller screen, the renderers put it at the bottom.
constexpr double kBottomBarTop = 570, kBottomBarLeft = 230; // (the button guide, left of it, stays)
bool GameStateBottomBarShown();
// The row at a point (-1: none; in a carousel the middle one is 2), and the list's choice (-1: a carousel).
int GameStateMenuRow(const GameMenu& menu, double x, double y);
int GameStateMenuChoice(const GameMenu& menu);
// A list's choice put at once on a row (its highlight follows; the game's own sound and moves do not)
void GameStateSetMenuRow(const GameMenu& menu, int row);
